#include "daily_check.h"
#include "storage/nvs_storage.h"
#include "wifi/wifi_manager.h"
#include "rtc/rtc.h"
#include "config.h"
#include "logging/log_manager.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "daily_check";

extern app_config_t g_config;
extern bool         g_irrigation_today;

#define HTTP_RESPONSE_MAX 512

static char s_response_buf[HTTP_RESPONSE_MAX];
static int  s_response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int remaining = HTTP_RESPONSE_MAX - s_response_len - 1;
        if (remaining > 0) {
            int copy = evt->data_len < remaining ? evt->data_len : remaining;
            memcpy(s_response_buf + s_response_len, evt->data, copy);
            s_response_len += copy;
            s_response_buf[s_response_len] = '\0';
        }
    }
    return ESP_OK;
}

esp_err_t daily_check_now(void)
{
    if (g_config.php_url[0] == '\0') {
        ESP_LOGI(TAG, "PHP URL not configured, skipping");
        return ESP_OK;
    }
    if (g_config.ignore_php) {
        ESP_LOGI(TAG, "ignore_php=true, skipping PHP check");
        return ESP_OK;
    }
    if (wifi_get_state() != WIFI_STATE_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected, skipping");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "checking: %s", g_config.php_url);

    s_response_len = 0;
    memset(s_response_buf, 0, sizeof(s_response_buf));

    esp_http_client_config_t cfg = {
        .url            = g_config.php_url,
        .event_handler  = http_event_handler,
        .timeout_ms     = 10000,
        .method         = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && s_response_len > 0) {
            cJSON *j = cJSON_Parse(s_response_buf);
            if (j) {
                cJSON *active = cJSON_GetObjectItem(j, "active");
                if (active) {
                    g_irrigation_today = cJSON_IsTrue(active);
                    APP_LOGI("daily", "Sprawdzanie PHP: nawadnianie=%s",
                             g_irrigation_today ? "tak" : "nie");

                    // Zapisz do NVS
                    g_config.irrigation_today = g_irrigation_today;
                    storage_save_config(&g_config);
                } else {
                    ESP_LOGW(TAG, "no 'active' field in response");
                }
                cJSON_Delete(j);
            } else {
                ESP_LOGW(TAG, "JSON parse error: %s", s_response_buf);
            }
        } else {
            ESP_LOGW(TAG, "HTTP status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t daily_check_init(void)
{
    ESP_LOGI(TAG, "init OK");
    return ESP_OK;
}

void daily_check_task(void *arg)
{
    ESP_LOGI(TAG, "task started");

    // Czekaj na WiFi
    while (wifi_get_state() != WIFI_STATE_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    // Pierwsze sprawdzenie po połączeniu
    vTaskDelay(pdMS_TO_TICKS(3000));
    daily_check_now();

    while (1) {
        // Sprawdź o DAILY_CHECK_HOUR:DAILY_CHECK_MINUTE (domyślnie 00:05)
        struct tm now = {0};
        if (rtc_get_time(&now) == ESP_OK) {
            if (now.tm_hour == DAILY_CHECK_HOUR &&
                now.tm_min  == DAILY_CHECK_MINUTE) {
                daily_check_now();
                // Czekaj > 1 minutę żeby nie sprawdzać wielokrotnie w tej samej minucie
                vTaskDelay(pdMS_TO_TICKS(90000));
                continue;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); // sprawdzaj co 30s
    }
}
