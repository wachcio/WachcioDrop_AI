#pragma once
#include "storage/nvs_storage.h"
#include "esp_err.h"

// Captive portal: tryb AP + serwer DNS + strona konfiguracji
// Konfiguruje: WiFi SSID/pass, MQTT, PHP URL, NTP, API token

esp_err_t captive_portal_start(app_config_t *cfg);
esp_err_t captive_portal_stop(void);

// Czy portal otrzymał nową konfigurację (SSID/pass)?
bool captive_portal_config_received(void);
void captive_portal_reset_flag(void);
