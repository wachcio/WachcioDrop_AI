#pragma once
#include "esp_err.h"

// Codzienne odpytywanie PHP endpointu o 00:05
// Oczekiwany format odpowiedzi: {"active": true} lub {"active": false}
// Wynik zapisywany do g_irrigation_today i NVS

esp_err_t daily_check_init(void);
void      daily_check_task(void *arg);

// Wymuś natychmiastowe sprawdzenie (np. z API)
esp_err_t daily_check_now(void);
