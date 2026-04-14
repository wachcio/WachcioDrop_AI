#include "menu.h"
#include "display.h"
#include "valve/valve.h"
#include "leds/leds.h"
#include "rtc/rtc.h"
#include "wifi/wifi_manager.h"
#include "encoder/encoder.h"
#include "storage/nvs_storage.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "menu";

extern app_config_t g_config;

static menu_screen_t s_screen     = MENU_SCREEN_HOME;
static int           s_cursor     = 0;
static int           s_scroll     = 0;  // dla list dłuższych niż ekran

// Pozycje menu głównego
static const char *MAIN_MENU[] = {
    "Reczne",
    "Harmonogram",
    "Grupy",
    "Informacje",
    "Ustawienia",
    NULL
};
#define MAIN_MENU_COUNT 5
#define MENU_VISIBLE_ROWS 5  // wiersze 1-5 (wiersz 0 = nagłówek)

// --------------------------------------------------------------------------
// Ekrany
// --------------------------------------------------------------------------

static void draw_home(void)
{
    char buf[18];

    // Wiersz 0: czas
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(buf, sizeof(buf), "%H:%M %d-%m-%Y", &t);
    display_text_full(0, buf, false);

    uint8_t mask = valve_get_active_mask();

    // Wiersz 1: sekcje 1-4  "1:# 2:- 3:# 4:-"
    snprintf(buf, sizeof(buf), "1:%c 2:%c 3:%c 4:%c",
             (mask & (1<<0)) ? '#' : '-',
             (mask & (1<<1)) ? '#' : '-',
             (mask & (1<<2)) ? '#' : '-',
             (mask & (1<<3)) ? '#' : '-');
    display_text_full(1, buf, false);

    // Wiersz 2: sekcje 5-8  "5:# 6:- 7:# 8:-"
    snprintf(buf, sizeof(buf), "5:%c 6:%c 7:%c 8:%c",
             (mask & (1<<4)) ? '#' : '-',
             (mask & (1<<5)) ? '#' : '-',
             (mask & (1<<6)) ? '#' : '-',
             (mask & (1<<7)) ? '#' : '-');
    display_text_full(2, buf, false);

    // Wiersz 3: zawór główny
    bool master = (mask != 0);
    snprintf(buf, sizeof(buf), "Glowny:%s", master ? "OTWARTY" : "ZAMKNIETY");
    display_text_full(3, buf, false);

    // Wiersz 4: WiFi
    wifi_state_t ws = wifi_get_state();
    const char *wifi_str = (ws == WIFI_STATE_CONNECTED) ? "WiFi: OK" :
                           (ws == WIFI_STATE_AP_MODE)   ? "WiFi: AP" : "WiFi: --";
    display_text_full(4, wifi_str, false);

    display_text_full(5, "", false);
    display_text_full(6, "", false);
    display_text_full(7, "Pokret=menu", false);
}

static void draw_main_menu(void)
{
    display_text_full(0, "=== MENU ===", false);

    for (int i = 0; i < MENU_VISIBLE_ROWS; i++) {
        int idx = s_scroll + i;
        char buf[18];
        if (MAIN_MENU[idx] != NULL) {
            snprintf(buf, sizeof(buf), "%s%s",
                     (idx == s_cursor) ? "> " : "  ",
                     MAIN_MENU[idx]);
            display_text_full(i + 1, buf, idx == s_cursor);
        } else {
            display_text_full(i + 1, "", false);
        }
    }
    display_text_full(6, "", false);
    display_text_full(7, "LNG=powrot", false);
}

static void draw_manual(void)
{
    display_text_full(0, "Reczne sterowanie", false);
    display_text_full(1, "----------------", false);

    uint8_t mask = valve_get_active_mask();
    for (int i = 0; i < SECTIONS_COUNT && i < 6; i++) {
        char buf[18];
        uint32_t rem = valve_get_remaining_sec(i + 1);
        if (mask & (1 << i)) {
            if (rem > 0) {
                snprintf(buf, sizeof(buf), "%sS%d: %02lu:%02lu",
                         (i == s_cursor) ? ">" : " ",
                         i + 1,
                         (unsigned long)(rem / 60),
                         (unsigned long)(rem % 60));
            } else {
                snprintf(buf, sizeof(buf), "%sSekcja %d: ON ",
                         (i == s_cursor) ? ">" : " ", i + 1);
            }
        } else {
            snprintf(buf, sizeof(buf), "%sSekcja %d: OFF",
                     (i == s_cursor) ? ">" : " ", i + 1);
        }
        display_text_full(i + 1, buf, i == s_cursor);
    }
    display_text_full(7, "SW=30min LNG=wyjdz", false);
}

static void draw_info(void)
{
    display_text_full(0, "=== INFO ===", false);

    char buf[20];
    char ip[16] = "---";
    wifi_get_ip(ip, sizeof(ip));
    snprintf(buf, sizeof(buf), "IP: %s", ip);
    display_text_full(1, buf, false);

    int rssi = wifi_get_rssi();
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", rssi);
    display_text_full(2, buf, false);

    int16_t temp = 0;
    rtc_get_temperature(&temp);
    snprintf(buf, sizeof(buf), "Temp: %d.%02d C", temp / 100, abs(temp % 100));
    display_text_full(3, buf, false);

    uint32_t uptime = esp_timer_get_time() / 1000000;
    snprintf(buf, sizeof(buf), "Up: %luh %02lum",
             (unsigned long)(uptime / 3600),
             (unsigned long)((uptime % 3600) / 60));
    display_text_full(4, buf, false);

    char tokbuf[20] = "Token:";
    strncat(tokbuf, g_config.api_token, sizeof(tokbuf) - 7);
    display_text_full(5, tokbuf, false);
    display_text_full(6, "", false);
    display_text_full(7, "LNG=wyjdz", false);
}

// --------------------------------------------------------------------------
// API
// --------------------------------------------------------------------------

esp_err_t menu_init(void)
{
    s_screen = MENU_SCREEN_HOME;
    s_cursor = 0;
    ESP_LOGI(TAG, "init OK");
    return ESP_OK;
}

void menu_handle_event(encoder_event_t evt)
{
    switch (s_screen) {
    case MENU_SCREEN_HOME:
        if (evt == ENCODER_EVENT_PRESS || evt == ENCODER_EVENT_LEFT ||
            evt == ENCODER_EVENT_RIGHT) {
            s_screen = MENU_SCREEN_MAIN;
            s_cursor = 0;
            s_scroll = 0;
        }
        break;

    case MENU_SCREEN_MAIN:
        if (evt == ENCODER_EVENT_RIGHT) {
            if (s_cursor < MAIN_MENU_COUNT - 1) {
                s_cursor++;
                if (s_cursor >= s_scroll + MENU_VISIBLE_ROWS)
                    s_scroll++;
            }
        } else if (evt == ENCODER_EVENT_LEFT) {
            if (s_cursor > 0) {
                s_cursor--;
                if (s_cursor < s_scroll) s_scroll--;
            }
        } else if (evt == ENCODER_EVENT_PRESS) {
            switch (s_cursor) {
            case 0: s_screen = MENU_SCREEN_MANUAL;   break;
            case 1: s_screen = MENU_SCREEN_SCHEDULE; break;
            case 2: s_screen = MENU_SCREEN_GROUPS;   break;
            case 3: s_screen = MENU_SCREEN_INFO;      break;
            case 4: s_screen = MENU_SCREEN_SETTINGS;  break;
            }
            s_cursor = 0;
            s_scroll = 0;
        } else if (evt == ENCODER_EVENT_LONG) {
            s_screen = MENU_SCREEN_HOME;
        }
        break;

    case MENU_SCREEN_MANUAL:
        if (evt == ENCODER_EVENT_RIGHT && s_cursor < SECTIONS_COUNT - 1) {
            s_cursor++;
        } else if (evt == ENCODER_EVENT_LEFT && s_cursor > 0) {
            s_cursor--;
        } else if (evt == ENCODER_EVENT_PRESS) {
            uint8_t sec = s_cursor + 1;
            if (valve_is_section_active(sec)) {
                valve_section_off(sec);
            } else {
                valve_section_on(sec, 30 * 60); // 30 minut domyślnie
            }
        } else if (evt == ENCODER_EVENT_LONG) {
            s_screen = MENU_SCREEN_HOME;
        }
        break;

    case MENU_SCREEN_INFO:
    case MENU_SCREEN_SCHEDULE:
    case MENU_SCREEN_GROUPS:
    case MENU_SCREEN_SETTINGS:
        if (evt == ENCODER_EVENT_LONG || evt == ENCODER_EVENT_PRESS) {
            s_screen = MENU_SCREEN_HOME;
        }
        break;

    default:
        break;
    }
}

void menu_task(void *arg)
{
    QueueHandle_t enc_queue = encoder_get_queue();
    ESP_LOGI(TAG, "task started");

    while (1) {
        // Obsłuż zdarzenia enkodera (nieblokująco)
        encoder_event_t evt;
        while (xQueueReceive(enc_queue, &evt, 0) == pdTRUE) {
            menu_handle_event(evt);
        }


        // Odśwież ekran (nadpisuje wszystkie wiersze bez poprzedniego czyszczenia)
        switch (s_screen) {
        case MENU_SCREEN_HOME:     draw_home();      break;
        case MENU_SCREEN_MAIN:     draw_main_menu(); break;
        case MENU_SCREEN_MANUAL:   draw_manual();    break;
        case MENU_SCREEN_INFO:     draw_info();      break;
        default:
            // Harmonogram / Grupy / Ustawienia — sterowanie przez WWW
            display_text_full(0, "", false);
            display_text_full(1, "", false);
            display_text_full(2, "", false);
            display_text_full(3, " Uzyj aplikacji", false);
            display_text_full(4, " webowej (WWW)", false);
            display_text_full(5, "", false);
            display_text_full(6, "", false);
            display_text_full(7, "LNG=powrot", false);
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz
    }
}
