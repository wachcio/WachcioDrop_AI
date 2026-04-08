#include "menu.h"
#include "display.h"
#include "valve/valve.h"
#include "leds/leds.h"
#include "rtc/rtc.h"
#include "wifi/wifi_manager.h"
#include "encoder/encoder.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "menu";

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
    char buf[32];
    display_clear();

    // Wiersz 0: czas (strftime unika format-truncation warnings)
    struct tm t = {0};
    rtc_get_time(&t);
    strftime(buf, sizeof(buf), "%H:%M %d-%m-%Y", &t);
    display_text(0, 0, buf, false);

    // Wiersz 1: separator
    display_hline(0, 9, DISPLAY_W);

    // Wiersz 2: stan sekcji (0=master, 1-8=sekcje) - 16 znaków max
    uint8_t mask = valve_get_active_mask();
    bool master  = (mask != 0);
    // Format: "M:# 1:# 2:# 3:#" = 16 znaków
    buf[0]  = 'M'; buf[1]  = ':'; buf[2]  = master          ? '#' : '-'; buf[3]  = ' ';
    buf[4]  = '1'; buf[5]  = ':'; buf[6]  = (mask & 1)       ? '#' : '-'; buf[7]  = ' ';
    buf[8]  = '2'; buf[9]  = ':'; buf[10] = (mask & 2)       ? '#' : '-'; buf[11] = ' ';
    buf[12] = '3'; buf[13] = ':'; buf[14] = (mask & 4)       ? '#' : '-'; buf[15] = ' ';
    buf[16] = '\0';
    display_text(0, 2, buf, false);

    buf[0]  = '4'; buf[1]  = ':'; buf[2]  = (mask & 8)   ? '#' : '-'; buf[3]  = ' ';
    buf[4]  = '5'; buf[5]  = ':'; buf[6]  = (mask & 16)  ? '#' : '-'; buf[7]  = ' ';
    buf[8]  = '6'; buf[9]  = ':'; buf[10] = (mask & 32)  ? '#' : '-'; buf[11] = ' ';
    buf[12] = '7'; buf[13] = ':'; buf[14] = (mask & 64)  ? '#' : '-'; buf[15] = ' ';
    buf[16] = '\0';
    display_text(0, 3, buf, false);

    // Wiersz 5: WiFi status
    wifi_state_t ws = wifi_get_state();
    const char *wifi_str = (ws == WIFI_STATE_CONNECTED) ? "WiFi: OK" :
                           (ws == WIFI_STATE_AP_MODE)   ? "WiFi: AP" : "WiFi: --";
    display_text(0, 5, wifi_str, false);

    // Wiersz 7: hint
    display_text(0, 7, "Pokret=menu", false);
}

static void draw_main_menu(void)
{
    display_clear();
    display_text(0, 0, "=== MENU ===    ", false);
    display_hline(0, 9, DISPLAY_W);

    for (int i = 0; i < MAIN_MENU_COUNT && i < MENU_VISIBLE_ROWS; i++) {
        int idx = s_scroll + i;
        if (MAIN_MENU[idx] == NULL) break;
        char buf[18];
        snprintf(buf, sizeof(buf), "%s%s",
                 (idx == s_cursor) ? "> " : "  ",
                 MAIN_MENU[idx]);
        display_text(0, i + 1, buf, idx == s_cursor);
    }
}

static void draw_manual(void)
{
    display_clear();
    display_text(0, 0, "Reczne sterowanie", false);
    display_hline(0, 9, DISPLAY_W);

    uint8_t mask = valve_get_active_mask();
    for (int i = 0; i < SECTIONS_COUNT; i++) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%sSekcja %d: %s",
                 (i == s_cursor) ? ">" : " ",
                 i + 1,
                 (mask & (1 << i)) ? "ON " : "OFF");
        if (i < 6) {
            display_text(0, i + 1, buf, i == s_cursor);
        }
    }
    display_text(0, 7, "SW=toggle LNG=wyjdz", false);
}

static void draw_info(void)
{
    display_clear();
    display_text(0, 0, "=== INFO ===    ", false);

    char buf[20];
    char ip[16] = "---";
    wifi_get_ip(ip, sizeof(ip));
    snprintf(buf, sizeof(buf), "IP: %s", ip);
    display_text(0, 1, buf, false);

    int rssi = wifi_get_rssi();
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", rssi);
    display_text(0, 2, buf, false);

    int16_t temp = 0;
    rtc_get_temperature(&temp);
    snprintf(buf, sizeof(buf), "Temp: %d.%02d C", temp / 100, abs(temp % 100));
    display_text(0, 3, buf, false);

    uint32_t uptime = esp_timer_get_time() / 1000000;
    snprintf(buf, sizeof(buf), "Up: %luh %02lum",
             (unsigned long)(uptime / 3600),
             (unsigned long)((uptime % 3600) / 60));
    display_text(0, 4, buf, false);

    display_text(0, 7, "LNG=wyjdz", false);
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
                valve_section_on(sec, 0); // bezterminowo
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

        // Odśwież ekran
        switch (s_screen) {
        case MENU_SCREEN_HOME:     draw_home();      break;
        case MENU_SCREEN_MAIN:     draw_main_menu(); break;
        case MENU_SCREEN_MANUAL:   draw_manual();    break;
        case MENU_SCREEN_INFO:     draw_info();      break;
        default:
            display_clear();
            display_text(0, 3, "  [patrz www]   ", false);
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz
    }
}
