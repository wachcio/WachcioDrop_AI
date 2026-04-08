#include "valve.h"
#include "leds/leds.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "valve";

typedef struct {
    bool     active;
    uint32_t remaining_sec;  // 0 = bezterminowo
} section_state_t;

static section_state_t s_sections[SECTIONS_COUNT + 1]; // [0]=master (auto), [1-8]=sekcje
static SemaphoreHandle_t s_mutex;

// Przelicz aktualny stan sekcji → bity 74HC595 i wyślij
static void apply_state(void)
{
    uint16_t bits = leds_get();

    // Wyczyść bity sekcji i master
    bits &= ~(BIT_MASTER |
              BIT_SECTION_1 | BIT_SECTION_2 | BIT_SECTION_3 |
              BIT_SECTION_4 | BIT_SECTION_5 | BIT_SECTION_6 |
              BIT_SECTION_7 | BIT_SECTION_8);

    bool any_active = false;
    for (int i = 1; i <= SECTIONS_COUNT; i++) {
        if (s_sections[i].active) {
            bits |= (1 << i);  // BIT_SECTION_n = (1 << n), n=1..8
            any_active = true;
        }
    }
    if (any_active) {
        bits |= BIT_MASTER;
    }
    leds_set(bits);
}

esp_err_t valve_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    for (int i = 0; i <= SECTIONS_COUNT; i++) {
        s_sections[i].active        = false;
        s_sections[i].remaining_sec = 0;
    }
    apply_state();
    ESP_LOGI(TAG, "init OK, %d sections", SECTIONS_COUNT);
    return ESP_OK;
}

esp_err_t valve_section_on(uint8_t section, uint32_t duration_sec)
{
    if (section < 1 || section > SECTIONS_COUNT) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_sections[section].active        = true;
    s_sections[section].remaining_sec = duration_sec;
    apply_state();
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "section %d ON (duration=%lus)", section, (unsigned long)duration_sec);
    return ESP_OK;
}

esp_err_t valve_section_off(uint8_t section)
{
    if (section < 1 || section > SECTIONS_COUNT) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_sections[section].active        = false;
    s_sections[section].remaining_sec = 0;
    apply_state();
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "section %d OFF", section);
    return ESP_OK;
}

esp_err_t valve_sections_on(uint8_t section_mask, uint32_t duration_sec)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 1; i <= SECTIONS_COUNT; i++) {
        if (section_mask & (1 << (i - 1))) {
            s_sections[i].active        = true;
            s_sections[i].remaining_sec = duration_sec;
        }
    }
    apply_state();
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "sections 0x%02X ON (duration=%lus)",
             section_mask, (unsigned long)duration_sec);
    return ESP_OK;
}

esp_err_t valve_all_off(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i <= SECTIONS_COUNT; i++) {
        s_sections[i].active        = false;
        s_sections[i].remaining_sec = 0;
    }
    apply_state();
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "all sections OFF");
    return ESP_OK;
}

uint8_t valve_get_active_mask(void)
{
    uint8_t mask = 0;
    for (int i = 1; i <= SECTIONS_COUNT; i++) {
        if (s_sections[i].active) {
            mask |= (1 << (i - 1));
        }
    }
    return mask;
}

bool valve_is_section_active(uint8_t section)
{
    if (section < 1 || section > SECTIONS_COUNT) return false;
    return s_sections[section].active;
}

// Task tick co 1 sekundę - odlicza czasy trwania sekcji
void valve_task(void *arg)
{
    ESP_LOGI(TAG, "task started");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        bool changed = false;
        for (int i = 1; i <= SECTIONS_COUNT; i++) {
            if (s_sections[i].active && s_sections[i].remaining_sec > 0) {
                s_sections[i].remaining_sec--;
                if (s_sections[i].remaining_sec == 0) {
                    s_sections[i].active = false;
                    changed = true;
                    ESP_LOGI(TAG, "section %d timer expired, OFF", i);
                }
            }
        }
        if (changed) {
            apply_state();
        }
        xSemaphoreGive(s_mutex);
    }
}
