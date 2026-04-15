#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Inicjalizacja — skanuje magistralę 1-Wire w poszukiwaniu DS18B20.
// Jeśli czujnik nie jest podłączony, moduł działa z available=false.
esp_err_t temperature_init(void);

// Zwraca true jeśli czujnik DS18B20 został wykryty.
bool      temperature_available(void);

// Zwraca ostatnio odczytaną temperaturę w stopniach Celsjusza.
// Wartość jest odświeżana co DS18B20_READ_INTERVAL_SEC sekund przez task.
// Jeśli czujnik niedostępny, zwraca 0.0f.
float     temperature_get(void);

// FreeRTOS task — odpytuje czujnik co DS18B20_READ_INTERVAL_SEC sekund.
// Uruchamiany przez main.c po inicjalizacji.
void      temperature_task(void *arg);
