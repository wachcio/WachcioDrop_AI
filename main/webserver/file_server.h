#pragma once
#include "esp_http_server.h"
#include "esp_err.h"

// Serwer plików statycznych ze SPIFFS
// Serwuje React build z /spiffs/

esp_err_t file_server_init(void);
esp_err_t file_server_register(httpd_handle_t server);
void      file_server_start(void);  // Uruchamia httpd + rejestruje REST API
