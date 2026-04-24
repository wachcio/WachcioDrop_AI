#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    ENCODER_EVENT_LEFT           = -1,  // przycisk DOWN
    ENCODER_EVENT_RIGHT          = 1,   // przycisk UP
    ENCODER_EVENT_PRESS          = 2,   // krótkie wciśnięcie SELECT
    ENCODER_EVENT_LONG           = 3,   // krótkie wciśnięcie BACK lub długie SELECT
    ENCODER_EVENT_FACTORY_RESET  = 99,  // BACK przytrzymany >= 10s
} encoder_event_t;

esp_err_t     buttons_init(void);
QueueHandle_t buttons_get_queue(void);
