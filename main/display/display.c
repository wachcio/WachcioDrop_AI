#include "display.h"
#include "config.h"
#include "ssd1306.h"
#include "font8x8_basic.h"
#include "esp_log.h"
#include "driver/spi_master.h"

static const char *TAG = "display";
static SSD1306_t s_dev;

esp_err_t display_init(void)
{
    // Inicjalizacja SSD1306 przez SPI2
    // Biblioteka nopnop2002/esp-idf-ssd1306 używa własnego API
    spi_master_init(&s_dev,
                    PIN_OLED_MOSI,
                    PIN_OLED_CLK,
                    PIN_OLED_CS,
                    PIN_OLED_DC,
                    PIN_OLED_RESET);

    ssd1306_init(&s_dev, DISPLAY_W, DISPLAY_H);
    ssd1306_clear_screen(&s_dev, false);
    ssd1306_contrast(&s_dev, 0xFF);

    ESP_LOGI(TAG, "init OK (%dx%d SPI MOSI=%d CLK=%d CS=%d DC=%d RST=%d)",
             DISPLAY_W, DISPLAY_H,
             PIN_OLED_MOSI, PIN_OLED_CLK, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RESET);
    return ESP_OK;
}

void display_clear(void)
{
    ssd1306_clear_screen(&s_dev, false);
}

void display_update(void)
{
    // nopnop2002 biblioteka renderuje bezpośrednio przy każdej operacji
    // (brak manualnego flush), ta funkcja istnieje dla kompatybilności API
}

void display_text(int col, int row, const char *text, bool invert)
{
    ssd1306_display_text(&s_dev, row, (char *)text, strlen(text), invert);
}

void display_rect(int x, int y, int w, int h)
{
    // Obramowanie przez cztery linie
    ssd1306_draw_rect(&s_dev, x, y, w, h);
}

void display_hline(int x, int y, int w)
{
    ssd1306_draw_line(&s_dev, x, y, x + w - 1, y);
}
