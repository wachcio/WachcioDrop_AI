#include "schedule.h"
#include "groups/groups.h"
#include "valve/valve.h"
#include "storage/nvs_storage.h"
#include "rtc/rtc.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>

static const char *TAG = "schedule";

static schedule_entry_t s_entries[SCHEDULE_ENTRIES];
static SemaphoreHandle_t s_mutex;

// Zewnętrzny wskaźnik do app_config (ustawiany przez main)
extern bool g_irrigation_today;

esp_err_t schedule_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    esp_err_t err = storage_load_schedule(s_entries, SCHEDULE_ENTRIES);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to load schedule: %s", esp_err_to_name(err));
        memset(s_entries, 0, sizeof(s_entries));
        for (int i = 0; i < SCHEDULE_ENTRIES; i++) s_entries[i].id = i;
    }
    ESP_LOGI(TAG, "init OK (%d entries loaded)", SCHEDULE_ENTRIES);
    return ESP_OK;
}

const schedule_entry_t *schedule_get_all(void)
{
    return s_entries;
}

esp_err_t schedule_get(uint8_t id, schedule_entry_t *out)
{
    if (id >= SCHEDULE_ENTRIES) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_entries[id];
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t schedule_set(const schedule_entry_t *entry)
{
    if (entry->id >= SCHEDULE_ENTRIES) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_entries[entry->id] = *entry;
    xSemaphoreGive(s_mutex);
    return storage_save_schedule_entry(entry);
}

esp_err_t schedule_delete(uint8_t id)
{
    if (id >= SCHEDULE_ENTRIES) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(&s_entries[id], 0, sizeof(schedule_entry_t));
    s_entries[id].id = id;
    xSemaphoreGive(s_mutex);
    return storage_save_schedule_entry(&s_entries[id]);
}

// Sprawdź i uruchom wpisy pasujące do aktualnego czasu
static void check_and_fire(const struct tm *now)
{
    if (!g_irrigation_today) {
        ESP_LOGI(TAG, "irrigation disabled today by daily_check");
        return;
    }

    // Przelicz na bit0=Pon...bit6=Nie (tm_wday: 0=Nie, 1=Pon...)
    uint8_t sched_day = (now->tm_wday == 0) ? (1 << 6) : (uint8_t)(1 << (now->tm_wday - 1));

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < SCHEDULE_ENTRIES; i++) {
        schedule_entry_t *e = &s_entries[i];
        if (!e->enabled) continue;
        if (!(e->days_mask & sched_day)) continue;
        if (e->hour != now->tm_hour) continue;
        if (e->minute != now->tm_min) continue;

        // Wyznacz sekcje z maski sekcji + rozwinięcie grup
        uint8_t final_mask = e->section_mask;
        if (e->group_mask) {
            final_mask |= groups_expand_mask(e->group_mask);
        }

        if (final_mask) {
            ESP_LOGI(TAG, "entry %d firing: sections=0x%02X duration=%us",
                     i, final_mask, e->duration_sec);
            valve_sections_on(final_mask, e->duration_sec);
        }
    }
    xSemaphoreGive(s_mutex);
}

void scheduler_task(void *arg)
{
    ESP_LOGI(TAG, "task started");

    int last_minute = -1;
    int sync_counter = 0;           // co 6 iteracji × 10s = co 60s sync DS3231 → system
    const int SYNC_EVERY = 6;

    while (1) {
        // Cykliczny sync DS3231 → zegar systemowy (eliminuje dryft ESP32 ~±2s/h)
        if (++sync_counter >= SYNC_EVERY) {
            sync_counter = 0;
            rtc_sync_system_clock();
        }

        // Użyj zegara systemowego z lokalną strefą czasową
        time_t now_t = time(NULL);
        struct tm now = {0};
        localtime_r(&now_t, &now);

        if (now.tm_min != last_minute) {
            last_minute = now.tm_min;
            check_and_fire(&now);
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // co 10s
    }
}
