#pragma once
#include "esp_err.h"
#include "buttons/buttons.h"

// System menu dla OLED 128x64
// Nawigacja: przyciski UP/DOWN = poruszanie, SELECT = wybór, BACK = wyjście/anuluj

typedef enum {
    MENU_SCREEN_HOME = 0,   // ekran główny: czas, stan sekcji, WiFi
    MENU_SCREEN_MAIN,       // główne menu
    MENU_SCREEN_MANUAL,     // ręczne sterowanie sekcjami
    MENU_SCREEN_INFO,       // informacje: IP, RSSI, uptime
} menu_screen_t;

esp_err_t menu_init(void);

// Przetwórz zdarzenie z enkodera
void menu_handle_event(encoder_event_t evt);

// Task odświeżający wyświetlacz (10Hz)
void menu_task(void *arg);
