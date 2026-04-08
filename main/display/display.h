#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Wrapper nad ssd1306 (SPI, 128x64)
// Udostępnia prymitywy rysowania do menu

esp_err_t display_init(void);

void display_clear(void);
void display_update(void);  // wypchnij bufor na ekran

// Tekst (font 8x8 wbudowany w bibliotekę)
void display_text(int col, int row, const char *text, bool invert);

// Prostokąt (obramowanie)
void display_rect(int x, int y, int w, int h);

// Linia pozioma
void display_hline(int x, int y, int w);

// Szerokość/wysokość ekranu
#define DISPLAY_W  128
#define DISPLAY_H  64
#define DISPLAY_ROWS  8   // 64px / 8px per row
#define DISPLAY_COLS  16  // 128px / 8px per char
