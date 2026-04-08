#pragma once
#include "esp_err.h"
#include "encoder/encoder.h"

// System menu dla OLED 128x64
// Nawigacja: enkoder lewo/prawo = poruszanie po menu, przycisk = wybór/wejście
// Długie wciśnięcie = wyjście/anuluj

typedef enum {
    MENU_SCREEN_HOME = 0,   // ekran główny: czas, stan sekcji, WiFi
    MENU_SCREEN_MAIN,       // główne menu
    MENU_SCREEN_MANUAL,     // ręczne sterowanie sekcjami
    MENU_SCREEN_SCHEDULE,   // podgląd harmonogramu
    MENU_SCREEN_GROUPS,     // podgląd grup
    MENU_SCREEN_INFO,       // informacje: IP, RSSI, uptime
    MENU_SCREEN_SETTINGS,   // ustawienia lokalne (jasność)
} menu_screen_t;

esp_err_t menu_init(void);

// Przetwórz zdarzenie z enkodera
void menu_handle_event(encoder_event_t evt);

// Task odświeżający wyświetlacz (10Hz)
void menu_task(void *arg);
