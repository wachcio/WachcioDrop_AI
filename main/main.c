#include "config.h"
#include "leds/leds.h"
#include "valve/valve.h"
#include "rtc/rtc.h"
#include "encoder/encoder.h"
#include "display/display.h"
#include "display/menu.h"
#include "storage/nvs_storage.h"
#include "schedule/schedule.h"
#include "groups/groups.h"
#include "wifi/wifi_manager.h"
#include "ntp/ntp.h"
#include "webserver/file_server.h"
#include "mqtt/mqtt_manager.h"
#include "daily_check/daily_check.h"
#include "logging/log_manager.h"
#include "temperature/temperature.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <time.h>

static const char *TAG = "main";

static void on_shutdown(void)
{
    APP_LOGI("system", "WachcioDrop v" FW_VERSION " restart (software)");
    vTaskDelay(pdMS_TO_TICKS(200));
}

// Globalne zmienne dostępne z innych modułów
app_config_t g_config          = {0};
bool         g_irrigation_today = true;

void app_main(void)
{
    // Potwierdź że nowe firmware po OTA jest sprawne (zapobiega rollbackowi)
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, "=== Irrigation Controller v1.0 ===");
    ESP_LOGI(TAG, "ESP32-S3 N16R8 | ESP-IDF %s", esp_get_idf_version());

    // ------------------------------------------------------------------
    // 0. Log manager - jak najwcześniej
    // ------------------------------------------------------------------
    log_manager_init();
    APP_LOGI("main", "WachcioDrop v" FW_VERSION " start (IDF %s)", esp_get_idf_version());

    // Handler wywoływany przed softwarowym resetem (np. OTA, restart API)
    esp_register_shutdown_handler(on_shutdown);

    // ------------------------------------------------------------------
    // 1. LED driver (74HC595) - PIERWSZE: zeruje chipy, zapobiega
    //    losowemu stanowi wyjść przy power-on
    // ------------------------------------------------------------------
    ESP_ERROR_CHECK(leds_init());

    // ------------------------------------------------------------------
    // 2. NVS - musi być przed wszystkimi modułami używającymi storage
    // ------------------------------------------------------------------
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(storage_load_config(&g_config));
    g_irrigation_today = g_config.irrigation_today;

    // Wygeneruj token jeśli pusty (pierwsze uruchomienie)
    if (g_config.api_token[0] == '\0') {
        storage_generate_token(g_config.api_token, API_TOKEN_LEN + 1);
        storage_save_config(&g_config);
        ESP_LOGI(TAG, "generated API token: %s", g_config.api_token);
    }

    // ------------------------------------------------------------------
    // 3. Zawory SSR (przez 74HC595)
    // ------------------------------------------------------------------
    ESP_ERROR_CHECK(valve_init());

    // ------------------------------------------------------------------
    // 4. RTC DS3231
    // ------------------------------------------------------------------
    // Ustaw strefę czasową przed init RTC (Polska: CET/CEST z DST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    ESP_ERROR_CHECK(rtc_ds3231_init());
    temperature_init();

    // ------------------------------------------------------------------
    // 5. Enkoder
    // ------------------------------------------------------------------
    ESP_ERROR_CHECK(encoder_init());

    // ------------------------------------------------------------------
    // 6. Wyświetlacz OLED SSD1306 SPI
    // ------------------------------------------------------------------
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(menu_init());

    // Pokaż ekran startowy
    display_clear();
    display_text(0, 1, " WachcioDrop    ", false);
    display_text(0, 2, " v" FW_VERSION "          ", false);
    display_text(0, 3, " Uruchamianie...", false);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // ------------------------------------------------------------------
    // 7. Harmonogram + grupy
    // ------------------------------------------------------------------
    ESP_ERROR_CHECK(groups_init());
    ESP_ERROR_CHECK(schedule_init());

    // ------------------------------------------------------------------
    // 8. WiFi manager init
    // ------------------------------------------------------------------
    ESP_ERROR_CHECK(wifi_manager_init());

    // ------------------------------------------------------------------
    // 9. Pokaż token API na OLED (użyteczne przy pierwszym uruchomieniu)
    // ------------------------------------------------------------------
    if (g_config.wifi_ssid[0] == '\0') {
        display_clear();
        display_text(0, 0, "AP: IrrigSetup  ", false);
        display_text(0, 1, "Pass: irrig..123", false);
        display_text(0, 2, "IP: 192.168.4.1 ", false);
        display_text(0, 3, "Token:          ", false);
        display_text(0, 4, g_config.api_token, false);
    }

    // ------------------------------------------------------------------
    // 10. Serwer HTTP — MUSI być przed taskami WiFi/captive portal
    //     (wifi_manager_task woła file_server_get_handle() przy starcie)
    // ------------------------------------------------------------------
    file_server_init();
    file_server_start();

    // ------------------------------------------------------------------
    // 11. Uruchom taski FreeRTOS
    // ------------------------------------------------------------------

    // Task zaworów - najwyższy priorytet (liczy timery sekcji)
    xTaskCreate(valve_task, "valve",
                TASK_STACK_VALVE, NULL, TASK_PRIO_VALVE, NULL);

    // Task harmonogramu
    xTaskCreate(scheduler_task, "scheduler",
                TASK_STACK_SCHEDULER, NULL, TASK_PRIO_SCHEDULER, NULL);

    // Task menu/wyświetlacza (obsługuje też enkoder)
    xTaskCreate(menu_task, "display",
                TASK_STACK_DISPLAY, NULL, TASK_PRIO_DISPLAY, NULL);

    // Task WiFi (state machine + captive portal) — po file_server_start!
    xTaskCreate(wifi_manager_task, "wifi",
                TASK_STACK_WIFI, NULL, TASK_PRIO_WIFI, NULL);

    // Task NTP (synchronizacja po połączeniu WiFi)
    xTaskCreate(ntp_task, "ntp",
                TASK_STACK_NTP, NULL, TASK_PRIO_NTP, NULL);

    // Task MQTT
    mqtt_manager_init();
    xTaskCreate(mqtt_manager_task, "mqtt",
                TASK_STACK_MQTT, NULL, TASK_PRIO_MQTT, NULL);

    // Task daily check PHP
    daily_check_init();
    xTaskCreate(daily_check_task, "daily_chk",
                TASK_STACK_DAILY_CHECK, NULL, TASK_PRIO_DAILY_CHECK, NULL);

    // Task temperatury DS18B20
    xTaskCreate(temperature_task, "temp", TASK_STACK_TEMPERATURE, NULL, TASK_PRIO_NTP, NULL);

    ESP_LOGI(TAG, "all tasks started");

    // ------------------------------------------------------------------
    // main loop - watchdog / monitoring
    // ------------------------------------------------------------------
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "heap free: %lu KB, min: %lu KB",
                 (unsigned long)(esp_get_free_heap_size() / 1024),
                 (unsigned long)(esp_get_minimum_free_heap_size() / 1024));
    }
}
