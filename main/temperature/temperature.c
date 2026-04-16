#include "temperature.h"
#include "config.h"
#include "logging/log_manager.h"
#include "storage/nvs_storage.h"
#include "valve/valve.h"
#include "mqtt/mqtt_manager.h"
#include "ds18x20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "temp";

extern app_config_t g_config;
extern bool         g_irrigation_today;

static bool            s_available = false;
static ds18x20_addr_t  s_addr      = DS18X20_ANY;
static float           s_temp      = 0.0f;

// Stan ochrony przed mrozem
static bool     s_frost_active   = false;  // true gdy poniżej progu lub trwa odliczanie
static bool     s_frost_disabled = false;  // true gdy to my wyłączyliśmy nawadnianie
static uint32_t s_recovery_sec   = 0;      // sekundy od wzrostu temp powyżej progu

esp_err_t temperature_init(void)
{
    size_t found = 0;
    ds18x20_addr_t addrs[1];

    esp_err_t err = ds18x20_scan_devices(PIN_DS18B20, addrs, 1, &found);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return ESP_OK; // nie przerywaj inicjalizacji systemu
    }

    if (found == 0) {
        ESP_LOGW(TAG, "DS18B20 not found on GPIO%d", PIN_DS18B20);
        return ESP_OK;
    }

    s_addr      = addrs[0];
    s_available = true;
    ESP_LOGI(TAG, "DS18B20 found on GPIO%d, addr=0x%llx", PIN_DS18B20,
             (unsigned long long)s_addr);
    return ESP_OK;
}

bool temperature_available(void)
{
    return s_available;
}

float temperature_get(void)
{
    return s_temp;
}

void temperature_task(void *arg)
{
    ESP_LOGI(TAG, "task started (interval %ds)", DS18B20_READ_INTERVAL_SEC);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(DS18B20_READ_INTERVAL_SEC * 1000));

        if (!s_available) {
            // Spróbuj ponownie wykryć czujnik
            size_t found = 0;
            ds18x20_addr_t addrs[1];
            if (ds18x20_scan_devices(PIN_DS18B20, addrs, 1, &found) == ESP_OK
                    && found > 0) {
                s_addr      = addrs[0];
                s_available = true;
                ESP_LOGI(TAG, "DS18B20 detected, addr=0x%llx",
                         (unsigned long long)s_addr);
                APP_LOGI("temp", "DS18B20 wykryty na GPIO%d", PIN_DS18B20);
            }
            continue;
        }

        float temp = 0.0f;
        esp_err_t err = ds18b20_measure_and_read(PIN_DS18B20, s_addr, &temp);
        if (err == ESP_OK) {
            s_temp = temp;
            ESP_LOGD(TAG, "%.2f °C", temp);
            mqtt_publish_temperature(temp, true);

            if (g_config.frost_protection_enabled) {
                float thr = (float)g_config.frost_temp_threshold;

                if (temp < thr) {
                    // Temperatura poniżej progu — resetuj licznik reaktywacji
                    s_recovery_sec = 0;

                    if (!s_frost_active) {
                        // Nowe zdarzenie mrozu
                        s_frost_active = true;
                        mqtt_publish_frost_active(true);
                        ESP_LOGW(TAG, "frost: %.1f°C < %.0f°C — disabling irrigation",
                                 temp, thr);
                        APP_LOGI("temp",
                                 "Ochrona przed mrozem: %.1f°C < %.0f°C, nawadnianie wyłączone",
                                 temp, thr);
                        valve_all_off();
                        if (g_irrigation_today) {
                            s_frost_disabled = true;
                            g_irrigation_today = false;
                            g_config.irrigation_today = false;
                            storage_save_config(&g_config);
                        }
                    }
                } else if (s_frost_active) {
                    // Temperatura powyżej progu, trwa odliczanie opóźnienia reaktywacji
                    s_recovery_sec += DS18B20_READ_INTERVAL_SEC;
                    uint32_t delay_sec = (uint32_t)g_config.frost_recovery_delay_min * 60;

                    ESP_LOGI(TAG, "frost recovery: %.1f°C, elapsed %lus / %lus",
                             temp, (unsigned long)s_recovery_sec,
                             (unsigned long)delay_sec);

                    if (s_recovery_sec >= delay_sec) {
                        s_frost_active = false;
                        s_recovery_sec = 0;
                        mqtt_publish_frost_active(false);

                        if (s_frost_disabled) {
                            s_frost_disabled = false;
                            ESP_LOGI(TAG, "frost recovery complete — re-enabling irrigation");
                            APP_LOGI("temp",
                                     "Reaktywacja po mrozie: temperatura wróciła powyżej %.0f°C",
                                     thr);
                            g_irrigation_today = true;
                            g_config.irrigation_today = true;
                            storage_save_config(&g_config);
                        }
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "read error: %s", esp_err_to_name(err));
            // Po kilku błędach z rzędu oznacz jako niedostępny
            s_available = false;
            mqtt_publish_temperature(0, false);
            APP_LOGI("temp", "DS18B20 utracony, ponawiam wykrywanie");
        }
    }
}
