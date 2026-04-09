#include "ntp.h"
#include "rtc/rtc.h"
#include "wifi/wifi_manager.h"
#include "storage/nvs_storage.h"
#include "config.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <string.h>

static const char *TAG = "ntp";

extern app_config_t g_config;

static volatile bool s_synced = false;

static void sntp_sync_cb(struct timeval *tv)
{
    s_synced = true;
    ESP_LOGI(TAG, "SNTP sync done, unix=%lld", (long long)tv->tv_sec);
}

esp_err_t ntp_init(void)
{
    ESP_LOGI(TAG, "init OK");
    return ESP_OK;
}

void ntp_task(void *arg)
{
    ESP_LOGI(TAG, "task started");

    while (1) {
        // Czekaj na połączenie WiFi
        while (wifi_get_state() != WIFI_STATE_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        const char *ntp_server = (g_config.ntp_server[0] != '\0')
                                  ? g_config.ntp_server
                                  : DEFAULT_NTP_SERVER;
        int8_t tz = g_config.timezone_offset;

        // Strefa czasowa: Polska = CET/CEST z automatycznym DST
        // Ignoruj timezone_offset — używamy pełnego POSIX TZ string
        (void)tz;
        const char *posix_tz = "CET-1CEST,M3.5.0,M10.5.0/3";
        setenv("TZ", posix_tz, 1);
        tzset();
        ESP_LOGI(TAG, "syncing with %s (TZ: %s)...", ntp_server, posix_tz);

        // ESP-IDF 5.x SNTP API
        esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server);
        sntp_cfg.sync_cb = sntp_sync_cb;
        s_synced = false;

        esp_netif_sntp_init(&sntp_cfg);

        // Czekaj na synchronizację (max 30s)
        esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000));

        if (err == ESP_OK || s_synced) {
            time_t now = time(NULL);
            rtc_set_time_unix(now);
            struct tm *t = localtime(&now);
            char tstr[32];
            strftime(tstr, sizeof(tstr), "%Y-%m-%d %H:%M:%S", t);
            ESP_LOGI(TAG, "DS3231 updated: %s", tstr);
        } else {
            ESP_LOGW(TAG, "NTP sync timeout");
        }

        esp_netif_sntp_deinit();

        // Następna synchronizacja za 24h
        vTaskDelay(pdMS_TO_TICKS(NTP_SYNC_INTERVAL_MS));
    }
}
