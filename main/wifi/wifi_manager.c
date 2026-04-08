#include "wifi_manager.h"
#include "captive_portal.h"
#include "storage/nvs_storage.h"
#include "leds/leds.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static wifi_state_t       s_state      = WIFI_STATE_UNCONFIGURED;
static int                s_retries    = 0;
static char               s_ip[16]     = "0.0.0.0";
static bool               s_force_ap   = false;

extern app_config_t g_config;

static void event_handler(void *arg, esp_event_base_t base,
                           int32_t event_id, void *data)
{
    if (base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
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

static void start_sta(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid,     ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();
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

    // Użyj globalnej konfiguracji
    app_config_t *cfg = &g_config;

    bool has_creds = (cfg->wifi_ssid[0] != '\0');

    if (!has_creds || s_force_ap) {
        goto start_ap;
    }

    // Próba połączenia STA
    s_state   = WIFI_STATE_CONNECTING;
    s_retries = 0;
    start_sta(cfg->wifi_ssid, cfg->wifi_pass);

    {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
        if (bits & WIFI_CONNECTED_BIT) {
            s_state = WIFI_STATE_CONNECTED;
            ESP_LOGI(TAG, "STA connected");
            // Monitoruj połączenie w pętli
            while (1) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                if (s_force_ap) {
                    esp_wifi_stop();
                    goto start_ap;
                }
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
                    ESP_LOGW(TAG, "connection lost");
                    s_state = WIFI_STATE_CONNECTING;
                    leds_set_bit(BIT_LED_WIFI, false);
                    s_retries = 0;
                    xEventGroupClearBits(s_wifi_event_group,
                                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
                    esp_wifi_connect();
                    EventBits_t rb = xEventGroupWaitBits(
                        s_wifi_event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdTRUE, pdFALSE,
                        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
                    if (rb & WIFI_CONNECTED_BIT) {
                        s_state = WIFI_STATE_CONNECTED;
                    } else {
                        esp_wifi_stop();
                        goto start_ap;
                    }
                }
            }
        }
    }

start_ap:
    ESP_LOGI(TAG, "starting AP mode");
    s_state   = WIFI_STATE_AP_MODE;
    s_retries = 0;
    esp_wifi_stop();

    captive_portal_start(cfg);

    leds_set_bit(BIT_LED_WIFI, false);
    // Migaj LED WiFi w AP mode
    while (1) {
        leds_set_bit(BIT_LED_WIFI, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        leds_set_bit(BIT_LED_WIFI, false);
        vTaskDelay(pdMS_TO_TICKS(500));

        if (s_force_ap) s_force_ap = false;

        // Sprawdź czy portal zapisał konfigurację i mamy nowe poświadczenia
        if (cfg->wifi_ssid[0] != '\0' &&
            captive_portal_config_received()) {
            ESP_LOGI(TAG, "new WiFi config received, reconnecting...");
            captive_portal_stop();
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            s_state   = WIFI_STATE_CONNECTING;
            s_retries = 0;
            xEventGroupClearBits(s_wifi_event_group,
                                 WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
            start_sta(cfg->wifi_ssid, cfg->wifi_pass);
            EventBits_t bits = xEventGroupWaitBits(
                s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdTRUE, pdFALSE,
                pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
            if (bits & WIFI_CONNECTED_BIT) {
                s_state = WIFI_STATE_CONNECTED;
                ESP_LOGI(TAG, "connected after portal setup");
                leds_set_bit(BIT_LED_WIFI, true);
                // Monitoruj w pętli (uproszczona wersja)
                while (1) vTaskDelay(pdMS_TO_TICKS(10000));
            } else {
                ESP_LOGE(TAG, "still can't connect, going back to AP");
                esp_wifi_stop();
                captive_portal_start(cfg);
            }
        }
    }
}
