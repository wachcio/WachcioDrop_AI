#pragma once
#include <stdint.h>
#include "esp_err.h"

// 74HC595 16-bit driver (2 chipy daisy-chain)
// Bity 0-8: zawory SSR + LED wskaźniki sekcji
// Bity 9-10: LED WiFi, LED zasilanie
// Bity 11-15: rezerwa

esp_err_t leds_init(void);

// Ustaw nową wartość 16-bit i wyślij do shift rejestru
void leds_set(uint16_t bits);

// Pobierz aktualny stan 16-bit
uint16_t leds_get(void);

// Ustaw/wyczyść pojedynczy bit
void leds_set_bit(uint16_t bit_mask, bool value);

// Wymuś aktualizację (ponowne wypchnięcie aktualnego stanu)
void leds_refresh(void);
