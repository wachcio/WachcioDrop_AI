#pragma once
#include "esp_err.h"

// Synchronizacja czasu z serwera NTP → DS3231
// Uruchamiany po nawiązaniu połączenia WiFi, powtarzany co 24h

esp_err_t ntp_init(void);

// Task - uruchomić z main po połączeniu WiFi
void ntp_task(void *arg);
