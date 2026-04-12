#include "groups.h"
#include "valve/valve.h"
#include "storage/nvs_storage.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "groups";

static irrigation_group_t s_groups[GROUPS_MAX];
static SemaphoreHandle_t  s_mutex;
static uint8_t            s_active_group_id = 0; // 0 = brak aktywnej grupy

esp_err_t groups_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    esp_err_t err = storage_load_groups(s_groups, GROUPS_MAX);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to load groups: %s", esp_err_to_name(err));
        for (int i = 0; i < GROUPS_MAX; i++) {
            memset(&s_groups[i], 0, sizeof(irrigation_group_t));
            s_groups[i].id = i + 1;
            snprintf(s_groups[i].name, sizeof(s_groups[i].name), "Grupa %d", i + 1);
        }
    }
    ESP_LOGI(TAG, "init OK (%d groups)", GROUPS_MAX);
    return ESP_OK;
}

const irrigation_group_t *groups_get_all(void)
{
    return s_groups;
}

esp_err_t groups_get(uint8_t id, irrigation_group_t *out)
{
    if (id < 1 || id > GROUPS_MAX) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_groups[id - 1];
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t groups_set(const irrigation_group_t *group)
{
    if (group->id < 1 || group->id > GROUPS_MAX) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_groups[group->id - 1] = *group;
    xSemaphoreGive(s_mutex);
    return storage_save_group(group);
}

uint8_t groups_get_active_id(void) { return s_active_group_id; }
void    groups_clear_active(void)  { s_active_group_id = 0; }

uint8_t groups_expand_mask(uint16_t group_mask)
{
    uint8_t sections = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < GROUPS_MAX; i++) {
        if (group_mask & (1 << i)) {
            sections |= s_groups[i].section_mask;
        }
    }
    xSemaphoreGive(s_mutex);
    return sections;
}

esp_err_t groups_activate(uint8_t group_id, uint32_t duration_sec)
{
    if (group_id < 1 || group_id > GROUPS_MAX) return ESP_ERR_INVALID_ARG;
    uint16_t mask = (uint16_t)(1 << (group_id - 1));
    uint8_t sections = groups_expand_mask(mask);
    if (!sections) {
        ESP_LOGW(TAG, "group %d has no sections", group_id);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "group %d activate: sections=0x%02X duration=%lus",
             group_id, sections, (unsigned long)duration_sec);
    valve_all_off();               // czyści active_group_id wewnątrz
    s_active_group_id = group_id; // ustaw PO all_off
    return valve_sections_on(sections, duration_sec);
}
