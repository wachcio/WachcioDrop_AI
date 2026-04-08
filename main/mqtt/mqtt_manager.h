#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// MQTT manager z obsługą Home Assistant autodiscovery
// Tematy: irrigation/section/{1-8}/state|command
//         irrigation/section/master/state
//         irrigation/section/all/command
//         irrigation/group/{1-10}/command
//         irrigation/schedule/state|set
//         irrigation/status

esp_err_t mqtt_manager_init(void);
void      mqtt_manager_task(void *arg);

// Publikuj stan sekcji (wywoływany przez valve po zmianie stanu)
void mqtt_publish_section_state(uint8_t section, bool active);
void mqtt_publish_all_states(void);
void mqtt_publish_status(void);
