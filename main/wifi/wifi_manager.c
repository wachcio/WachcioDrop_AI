#include "wifi_manager.h"
#include "captive_portal.h"
#include "webserver/file_server.h"
#include "storage/nvs_storage.h"
#include "leds/leds.h"
#include "logging/log_manager.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static wifi_state_t       s_state        = WIFI_STATE_UNCONFIGURED;
static int                s_retries      = 0;
static char               s_ip[16]       = "0.0.0.0";
static bool               s_force_ap     = false;
static bool               s_boot_logged  = false; // czy wysłano log startowy

extern app_config_t g_config;

static void event_handler(void *arg, esp_event_base_t base,
                           int32_t event_id, void *data)
{
    if (base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_retries++;
            leds_set_bit(BIT_LED_WIFI, false);
            if (s_retries < WIFI_MAX_RETRIES) {
                ESP_LOGI(TAG, "retry %d/%d...", s_retries, WIFI_MAX_RETRIES);
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&evt->ip_info.ip, s_ip, sizeof(s_ip));
        s_retries = 0;
        leds_set_bit(BIT_LED_WIFI, true);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "connected, IP: %s", s_ip);
    }
}

static void start_ap(void)
{
    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid,     DEFAULT_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
    strncpy((char *)ap_cfg.ap.password, DEFAULT_AP_PASS, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.ssid_len       = strlen(DEFAULT_AP_SSID);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 4;
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    ESP_LOGI(TAG, "AP configured: SSID='%s' IP=%s", DEFAULT_AP_SSID, AP_IP_ADDR);
}

static void start_sta(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid,     ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;  // minimum — akceptuje WPA2 i WPA3-SAE
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
    ESP_LOGI(TAG, "connecting to '%s'...", ssid);
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&wcfg);
    if (err != ESP_OK) return err;

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        event_handler, NULL, NULL);

    ESP_LOGI(TAG, "init OK");
    return ESP_OK;
}

wifi_state_t wifi_get_state(void) { return s_state; }

void wifi_get_ip(char *buf, size_t len)
{
    strncpy(buf, s_ip, len - 1);
    buf[len - 1] = '\0';
}

int wifi_get_rssi(void)
{
    if (s_state != WIFI_STATE_CONNECTED) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}

void wifi_force_ap_mode(void) { s_force_ap = true; }

void wifi_manager_task(void *arg)
{
    ESP_LOGI(TAG, "task started");

    app_config_t *cfg = &g_config;

    // Zawsze uruchamiamy w trybie APSTA — AP działa przez cały czas
    start_ap();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();
    ESP_LOGI(TAG, "AP started: SSID='%s' IP=%s", DEFAULT_AP_SSID, AP_IP_ADDR);

    // Portal startuje tylko gdy brak zapisanego SSID
    if (cfg->wifi_ssid[0] == '\0') {
        captive_portal_start(file_server_get_handle(), cfg);
        s_state = WIFI_STATE_AP_MODE;
    } else {
        // Mamy poświadczenia — pomiń portal, serwuj React app
        file_server_register(file_server_get_handle());
        s_state   = WIFI_STATE_CONNECTING;
        s_retries = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        vTaskDelay(pdMS_TO_TICKS(200));
        start_sta(cfg->wifi_ssid, cfg->wifi_pass);
    }

    while (1) {
        // Migaj LED gdy brak połączenia STA, świeć ciągłe gdy połączone
        if (s_state != WIFI_STATE_CONNECTED) {
            leds_set_bit(BIT_LED_WIFI, true);
            vTaskDelay(pdMS_TO_TICKS(500));
            leds_set_bit(BIT_LED_WIFI, false);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5000));
            // Sprawdź czy STA nadal połączone
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
                ESP_LOGW(TAG, "STA connection lost, reconnecting...");
                s_state = WIFI_STATE_CONNECTING;
                s_retries = 0;
                leds_set_bit(BIT_LED_WIFI, false);
                xEventGroupClearBits(s_wifi_event_group,
                                     WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
                esp_wifi_connect();
            }
        }

        // Nowa konfiguracja z portalu — przełącz na file server i połącz STA
        if (cfg->wifi_ssid[0] != '\0' && captive_portal_config_received()) {
            captive_portal_reset_flag();
            captive_portal_stop(file_server_get_handle());
            file_server_register(file_server_get_handle());
            ESP_LOGI(TAG, "new WiFi config, connecting STA to '%s'", cfg->wifi_ssid);
            s_state   = WIFI_STATE_CONNECTING;
            s_retries = 0;
            leds_set_bit(BIT_LED_WIFI, false);
            xEventGroupClearBits(s_wifi_event_group,
                                 WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
            // Rozłącz poprzednie połączenie STA jeśli było
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(200));
            start_sta(cfg->wifi_ssid, cfg->wifi_pass);
        }

        // Sprawdź wyniki próby połączenia STA (nieblokująco)
        if (s_state == WIFI_STATE_CONNECTING) {
            EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
            if (bits & WIFI_CONNECTED_BIT) {
                s_state = WIFI_STATE_CONNECTED;
                leds_set_bit(BIT_LED_WIFI, true);
                ESP_LOGI(TAG, "STA connected: %s", s_ip);
                // Wyślij log startowy raz po pierwszym połączeniu
                if (!s_boot_logged) {
                    s_boot_logged = true;
                    static const char * const reset_reasons[] = {
                        "nieznany", "zasilanie", "zewnętrzny", "oprogramowanie",
                        "wyjątek/panika", "watchdog int.", "watchdog zadania",
                        "deep sleep", "JTAG"
                    };
                    esp_reset_reason_t rr = esp_reset_reason();
                    int rri = (rr < 9) ? (int)rr : 0;
                    APP_LOGI("system", "WachcioDrop v" FW_VERSION " online | IP: %s | reset: %s",
                             s_ip, reset_reasons[rri]);
                }
            } else if (bits & WIFI_FAIL_BIT) {
                s_state = WIFI_STATE_AP_MODE;
                ESP_LOGW(TAG, "STA connect failed, staying in AP mode");
                xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }

        if (s_force_ap) {
            s_force_ap = false;
            esp_wifi_disconnect();
            s_state = WIFI_STATE_AP_MODE;
        }
    }
}
