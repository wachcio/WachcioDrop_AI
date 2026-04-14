#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WIFI_STATE_UNCONFIGURED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
    WIFI_STATE_FAILED,
} wifi_state_t;

esp_err_t wifi_manager_init(void);

wifi_state_t wifi_get_state(void);
void         wifi_get_ip(char *buf, size_t len);
int          wifi_get_rssi(void);

// Wymusz przejście w AP mode (np. z menu)
void wifi_force_ap_mode(void);

// Wyzwól próbę ponownego połączenia STA (np. po zapisie ustawień WiFi)
void wifi_trigger_reconnect(void);

// Task zarządzający połączeniem
void wifi_manager_task(void *arg);
