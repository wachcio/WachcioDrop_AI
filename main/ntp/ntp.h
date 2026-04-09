#pragma once
#include "esp_err.h"

// Synchronizacja czasu z serwera NTP → DS3231
// Uruchamiany po nawiązaniu połączenia WiFi, powtarzany co 24h

esp_err_t ntp_init(void);

// Wymuś natychmiastową synchronizację NTP → DS3231 (blokuje max 30s)
// Zwraca ESP_OK jeśli sync się udał, ESP_ERR_TIMEOUT jeśli nie
esp_err_t ntp_force_sync(void);

// Task - uruchomić z main po połączeniu WiFi
void ntp_task(void *arg);
