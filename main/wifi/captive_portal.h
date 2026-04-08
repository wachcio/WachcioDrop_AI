#pragma once
#include "storage/nvs_storage.h"
#include "esp_err.h"
#include "esp_http_server.h"

// Captive portal: tryb AP + serwer DNS + strona konfiguracji
// Konfiguruje: WiFi SSID/pass, MQTT, PHP URL, NTP, API token
// Reużywa istniejącego httpd zamiast tworzyć własny (unika konfliktu portów)

esp_err_t captive_portal_start(httpd_handle_t server, app_config_t *cfg);
esp_err_t captive_portal_stop(httpd_handle_t server);

// Czy portal otrzymał nową konfigurację (SSID/pass)?
bool captive_portal_config_received(void);
void captive_portal_reset_flag(void);
