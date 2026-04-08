#include "encoder.h"
#include "config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "encoder";

#define DEBOUNCE_MS         5
#define LONG_PRESS_MS       1000
#define ENCODER_QUEUE_SIZE  16

static QueueHandle_t s_queue;
static volatile int64_t s_btn_press_time = 0;
static volatile bool    s_btn_pressed    = false;

// Lookup table dla dekodera quadraturowego
// Indeks = (prev_ab << 2) | curr_ab → wartość: -1, 0, +1
static const int8_t s_enc_table[16] = {
    0,  -1,  1,  0,
    1,   0,  0, -1,
   -1,   0,  0,  1,
    0,   1, -1,  0
};

static volatile uint8_t s_enc_state = 0;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio = (uint32_t)(uintptr_t)arg;
    BaseType_t higher_woken = pdFALSE;

    if (gpio == PIN_ENC_A || gpio == PIN_ENC_B) {
        uint8_t a = gpio_get_level(PIN_ENC_A);
        uint8_t b = gpio_get_level(PIN_ENC_B);
        uint8_t curr = (a << 1) | b;
        uint8_t prev = s_enc_state & 0x03;
        s_enc_state  = (prev << 2) | curr;

        int8_t dir = s_enc_table[s_enc_state & 0x0F];
        if (dir != 0) {
            encoder_event_t evt = (dir > 0) ? ENCODER_EVENT_RIGHT : ENCODER_EVENT_LEFT;
            xQueueSendFromISR(s_queue, &evt, &higher_woken);
        }
    } else if (gpio == PIN_ENC_SW) {
        int level = gpio_get_level(PIN_ENC_SW);
        if (level == 0) {
            // przycisk wciśnięty (active low)
            s_btn_press_time = esp_timer_get_time();
            s_btn_pressed    = true;
        } else {
            // przycisk zwolniony
            if (s_btn_pressed) {
                int64_t held_ms = (esp_timer_get_time() - s_btn_press_time) / 1000;
                encoder_event_t evt = (held_ms >= LONG_PRESS_MS)
                                      ? ENCODER_EVENT_LONG
                                      : ENCODER_EVENT_PRESS;
                xQueueSendFromISR(s_queue, &evt, &higher_woken);
                s_btn_pressed = false;
            }
        }
    }

    if (higher_woken) portYIELD_FROM_ISR();
}

esp_err_t encoder_init(void)
{
    s_queue = xQueueCreate(ENCODER_QUEUE_SIZE, sizeof(encoder_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_ENC_A) |
                        (1ULL << PIN_ENC_B) |
                        (1ULL << PIN_ENC_SW),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ENC_A,  gpio_isr_handler, (void *)(uintptr_t)PIN_ENC_A);
    gpio_isr_handler_add(PIN_ENC_B,  gpio_isr_handler, (void *)(uintptr_t)PIN_ENC_B);
    gpio_isr_handler_add(PIN_ENC_SW, gpio_isr_handler, (void *)(uintptr_t)PIN_ENC_SW);

    // Inicjalny stan enkodera
    uint8_t a = gpio_get_level(PIN_ENC_A);
    uint8_t b = gpio_get_level(PIN_ENC_B);
    s_enc_state = (a << 1) | b;

    ESP_LOGI(TAG, "init OK (A=%d B=%d SW=%d)", PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);
    return ESP_OK;
}

QueueHandle_t encoder_get_queue(void)
{
    return s_queue;
}
