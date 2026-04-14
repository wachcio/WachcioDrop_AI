#include "encoder.h"
#include "config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "encoder";

#define ENC_DEBOUNCE_MS     8       // debounce zbocza A enkodera
#define SW_DEBOUNCE_MS      20      // debounce przycisku SW
#define LONG_PRESS_MS       1000
#define ENCODER_QUEUE_SIZE  16
#define STARTUP_IGNORE_MS   500     // ignoruj zdarzenia przez pierwsze 500ms

static QueueHandle_t s_queue;
static volatile int64_t s_btn_press_time  = 0;
static volatile bool    s_btn_pressed     = false;
static volatile int64_t s_sw_last_edge    = 0;
static volatile int64_t s_enc_last_edge   = 0;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio = (uint32_t)(uintptr_t)arg;
    BaseType_t higher_woken = pdFALSE;
    int64_t now = esp_timer_get_time();

    if (gpio == PIN_ENC_A) {
        // Tylko rosnące zbocze A (POSEDGE) — 1 zdarzenie na 1 klik
        // Poziom B w momencie zbocza A określa kierunek:
        //   B=0 → obrót w prawo (RIGHT), B=1 → obrót w lewo (LEFT)
        if (now - s_enc_last_edge < (int64_t)ENC_DEBOUNCE_MS * 1000) goto done;
        s_enc_last_edge = now;

        encoder_event_t evt = gpio_get_level(PIN_ENC_B)
                              ? ENCODER_EVENT_LEFT
                              : ENCODER_EVENT_RIGHT;
        xQueueSendFromISR(s_queue, &evt, &higher_woken);

    } else if (gpio == PIN_ENC_SW) {
        int64_t now = esp_timer_get_time();

        // Ignoruj zdarzenia tuż po starcie (piny się stabilizują)
        if (now < (int64_t)STARTUP_IGNORE_MS * 1000) goto done;

        // Debounce przycisku
        if (now - s_sw_last_edge < (int64_t)SW_DEBOUNCE_MS * 1000) goto done;
        s_sw_last_edge = now;

        int level = gpio_get_level(PIN_ENC_SW);
        if (level == 0) {
            // przycisk wciśnięty (active low)
            s_btn_press_time = now;
            s_btn_pressed    = true;
        } else {
            // przycisk zwolniony
            if (s_btn_pressed) {
                int64_t held_ms = (now - s_btn_press_time) / 1000;
                encoder_event_t evt = (held_ms >= LONG_PRESS_MS)
                                      ? ENCODER_EVENT_LONG
                                      : ENCODER_EVENT_PRESS;
                xQueueSendFromISR(s_queue, &evt, &higher_woken);
                s_btn_pressed = false;
            }
        }
    }

done:
    if (higher_woken) portYIELD_FROM_ISR();
}

esp_err_t encoder_init(void)
{
    s_queue = xQueueCreate(ENCODER_QUEUE_SIZE, sizeof(encoder_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    // Pin A — przerwanie tylko na rosnącym zboczu (1 zdarzenie na klik)
    gpio_config_t cfg_a = {
        .pin_bit_mask = (1ULL << PIN_ENC_A),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    // Pin B — wejście bez przerwania (tylko odczyt poziomu przy zboczu A)
    gpio_config_t cfg_b = {
        .pin_bit_mask = (1ULL << PIN_ENC_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    // Pin SW — oba zbocza (detekcja wciśnięcia i zwolnienia)
    gpio_config_t cfg_sw = {
        .pin_bit_mask = (1ULL << PIN_ENC_SW),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };

    esp_err_t err;
    if ((err = gpio_config(&cfg_a))  != ESP_OK) return err;
    if ((err = gpio_config(&cfg_b))  != ESP_OK) return err;
    if ((err = gpio_config(&cfg_sw)) != ESP_OK) return err;

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ENC_A,  gpio_isr_handler, (void *)(uintptr_t)PIN_ENC_A);
    gpio_isr_handler_add(PIN_ENC_SW, gpio_isr_handler, (void *)(uintptr_t)PIN_ENC_SW);
    // PIN_ENC_B bez handlera — tylko odczyt poziomu

    ESP_LOGI(TAG, "init OK (A=%d B=%d SW=%d)", PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);
    return ESP_OK;
}

QueueHandle_t encoder_get_queue(void)
{
    return s_queue;
}
