#include "buttons.h"
#include "config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "buttons";

#define BUTTONS_QUEUE_SIZE      16
#define STARTUP_IGNORE_MS       500

static QueueHandle_t s_queue;

// Stan przycisków SELECT i BACK
static volatile bool    s_select_pressed  = false;
static volatile int64_t s_back_press_time = 0;
static volatile bool    s_back_pressed    = false;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio = (uint32_t)(uintptr_t)arg;
    BaseType_t higher_woken = pdFALSE;
    int64_t now = esp_timer_get_time();

    if (now < (int64_t)STARTUP_IGNORE_MS * 1000) goto done;

    if (gpio == PIN_BTN_UP) {
        // Wyślij zdarzenie natychmiast przy dotyku
        if (gpio_get_level(PIN_BTN_UP)) {
            encoder_event_t evt = ENCODER_EVENT_RIGHT;
            xQueueSendFromISR(s_queue, &evt, &higher_woken);
        }

    } else if (gpio == PIN_BTN_DOWN) {
        if (gpio_get_level(PIN_BTN_DOWN)) {
            encoder_event_t evt = ENCODER_EVENT_LEFT;
            xQueueSendFromISR(s_queue, &evt, &higher_woken);
        }

    } else if (gpio == PIN_BTN_SELECT) {
        // Tylko zatwierdź — zawsze PRESS niezależnie od czasu trzymania
        if (gpio_get_level(PIN_BTN_SELECT) == 0 && s_select_pressed) {
            encoder_event_t evt = ENCODER_EVENT_PRESS;
            xQueueSendFromISR(s_queue, &evt, &higher_woken);
            s_select_pressed = false;
        } else if (gpio_get_level(PIN_BTN_SELECT) == 1) {
            s_select_pressed = true;
        }

    } else if (gpio == PIN_BTN_BACK) {
        int level = gpio_get_level(PIN_BTN_BACK);
        if (level == 1) {
            s_back_press_time = now;
            s_back_pressed    = true;
        } else {
            if (s_back_pressed) {
                int64_t held_ms = (now - s_back_press_time) / 1000;
                s_back_pressed = false;
                encoder_event_t evt = (held_ms >= FACTORY_RESET_HOLD_MS)
                                      ? ENCODER_EVENT_FACTORY_RESET
                                      : ENCODER_EVENT_LONG;
                xQueueSendFromISR(s_queue, &evt, &higher_woken);
            }
        }
    }

done:
    if (higher_woken) portYIELD_FROM_ISR();
}

esp_err_t buttons_init(void)
{
    s_queue = xQueueCreate(BUTTONS_QUEUE_SIZE, sizeof(encoder_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    // Wszystkie przyciski TTP223: active-high (HIGH = dotknięty)
    // Pull-down zabezpiecza przed floating gdy czujnik odłączony
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_DOWN) |
                        (1ULL << PIN_BTN_SELECT) | (1ULL << PIN_BTN_BACK),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_BTN_UP,     gpio_isr_handler, (void *)(uintptr_t)PIN_BTN_UP);
    gpio_isr_handler_add(PIN_BTN_DOWN,   gpio_isr_handler, (void *)(uintptr_t)PIN_BTN_DOWN);
    gpio_isr_handler_add(PIN_BTN_SELECT, gpio_isr_handler, (void *)(uintptr_t)PIN_BTN_SELECT);
    gpio_isr_handler_add(PIN_BTN_BACK,   gpio_isr_handler, (void *)(uintptr_t)PIN_BTN_BACK);

    ESP_LOGI(TAG, "init OK (UP=%d DOWN=%d SELECT=%d BACK=%d)",
             PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_SELECT, PIN_BTN_BACK);
    return ESP_OK;
}

QueueHandle_t buttons_get_queue(void)
{
    return s_queue;
}
