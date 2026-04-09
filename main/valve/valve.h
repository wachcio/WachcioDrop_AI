#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Sterowanie sekcjami nawadniania przez 74HC595.
// Sekcje numerowane 1-8 (indeks 1..SECTIONS_COUNT).
// Master valve (0) otwierany automatycznie gdy dowolna sekcja aktywna.
// Ten sam sygnał steruje SSR relay (zaworem 24VAC) i diodą LED.

esp_err_t valve_init(void);

// Włącz sekcję na czas duration_sec (0 = bezterminowo)
esp_err_t valve_section_on(uint8_t section, uint32_t duration_sec);

// Wyłącz sekcję
esp_err_t valve_section_off(uint8_t section);

// Włącz wiele sekcji naraz (section_mask: bit0=sekcja1, ..., bit7=sekcja8)
esp_err_t valve_sections_on(uint8_t section_mask, uint32_t duration_sec);

// Wyłącz wszystko (wszystkie sekcje + master)
esp_err_t valve_all_off(void);

// Pobierz aktualny stan (section_mask aktywnych sekcji)
uint8_t valve_get_active_mask(void);

// Sprawdź czy dana sekcja jest aktywna
bool valve_is_section_active(uint8_t section);

// Pobierz pozostały czas sekcji w sekundach (0 = bezterminowo lub nieaktywna)
uint32_t valve_get_remaining_sec(uint8_t section);

// Opcjonalny callback wywoływany przy każdej zmianie stanu sekcji
// Rejestrowany przez mqtt_manager, aby nie tworzyć zależności valve → mqtt
typedef void (*valve_state_cb_t)(void);
void valve_set_state_callback(valve_state_cb_t cb);

// Task zarządzający licznikami czasu (uruchamiany z main)
void valve_task(void *arg);
