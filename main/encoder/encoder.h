#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    ENCODER_EVENT_LEFT   = -1,  // obrót w lewo
    ENCODER_EVENT_RIGHT  = 1,   // obrót w prawo
    ENCODER_EVENT_PRESS  = 2,   // krótkie wciśnięcie przycisku
    ENCODER_EVENT_LONG   = 3,   // długie wciśnięcie (>1s)
} encoder_event_t;

esp_err_t encoder_init(void);

// Ustaw liczbę kroków kwadraturowych na jeden klik (1/2/4, domyślnie 4)
void encoder_set_steps(uint8_t steps);

// Pobierz kolejkę zdarzeń (QueueHandle_t encoder_event_t)
QueueHandle_t encoder_get_queue(void);
