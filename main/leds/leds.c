#include "leds.h"
#include "config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "leds";
static uint16_t s_state = 0;
static SemaphoreHandle_t s_mutex;

static void shift_out(uint16_t data)
{
    // Wyślij 16 bitów MSB-first przez bit-bang
    // Sekwencja: SER → data bit, SRCLK ↑ → shift, po 16 bitach RCLK ↑ → latch
    data = ~data;  // active-low: inwertuj — bit=1 w logice → LOW na wyjściu
    for (int i = 15; i >= 0; i--) {
        gpio_set_level(PIN_595_SER, (data >> i) & 1);
        gpio_set_level(PIN_595_SRCLK, 1);
        // ~100ns przy 240MHz CPU (ok dla 74HC595 max 25MHz shift clock)
        __asm__ volatile("nop; nop; nop; nop;");
        gpio_set_level(PIN_595_SRCLK, 0);
        __asm__ volatile("nop; nop;");
    }
    // Latch: przepisz shift register → storage register → wyjścia
    gpio_set_level(PIN_595_RCLK, 1);
    __asm__ volatile("nop; nop; nop; nop;");
    gpio_set_level(PIN_595_RCLK, 0);
}

esp_err_t leds_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_595_SER) |
                        (1ULL << PIN_595_SRCLK) |
                        (1ULL << PIN_595_RCLK),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    gpio_set_level(PIN_595_SER,   0);
    gpio_set_level(PIN_595_SRCLK, 0);
    gpio_set_level(PIN_595_RCLK,  0);

    // Najpierw wyzeruj oba chipy (usuwa losowy stan po power-on)
    shift_out(0x0000);

    // Zapal LED zasilanie, reszta OFF
    s_state = BIT_LED_POWER;
    shift_out(s_state);

    ESP_LOGI(TAG, "init OK (SER=%d SRCLK=%d RCLK=%d)",
             PIN_595_SER, PIN_595_SRCLK, PIN_595_RCLK);
    return ESP_OK;
}

void leds_set(uint16_t bits)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = bits;
    shift_out(s_state);
    xSemaphoreGive(s_mutex);
}

uint16_t leds_get(void)
{
    return s_state;
}

void leds_set_bit(uint16_t bit_mask, bool value)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (value) {
        s_state |= bit_mask;
    } else {
        s_state &= ~bit_mask;
    }
    shift_out(s_state);
    xSemaphoreGive(s_mutex);
}

void leds_refresh(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    shift_out(s_state);
    xSemaphoreGive(s_mutex);
}
