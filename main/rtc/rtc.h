#pragma once
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

// Wrapper nad DS3231 z esp-idf-lib
// Zapewnia: inicjalizację I2C, get/set time, sync z NTP

esp_err_t rtc_ds3231_init(void);

// Pobierz czas z DS3231 i ustaw systemowy czas Unix (settimeofday)
esp_err_t rtc_get_time(struct tm *out_tm);

// Zapisz czas do DS3231 (np. po synchronizacji NTP)
esp_err_t rtc_set_time(const struct tm *tm);

// Ustaw czas z Unix timestamp
esp_err_t rtc_set_time_unix(time_t t);

// Pobierz aktualny czas jako Unix timestamp (z DS3231)
time_t rtc_get_unix(void);

// Pobierz temperaturę z DS3231 (°C * 100)
esp_err_t rtc_get_temperature(int16_t *temp_hundredths);
