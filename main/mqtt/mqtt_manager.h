#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t mqtt_manager_init(void);
void      mqtt_manager_task(void *arg);

// Sekcje / master
void mqtt_publish_section_state(uint8_t section, bool active);
void mqtt_publish_all_states(void);

// Ogólny status (IP, RSSI, uptime)
void mqtt_publish_status(void);

// Temperatura DS18B20
void mqtt_publish_temperature(float temp, bool available);

// Nawadnianie dziś
void mqtt_publish_irrigation_today(bool active);

// Ochrona przed mrozem (aktywny stan)
void mqtt_publish_frost_active(bool active);
