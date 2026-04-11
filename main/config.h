#pragma once

// =============================================================================
// ESP32-S3 N16R8 - Irrigation Controller - GPIO Configuration
// =============================================================================

// OLED SSD1306 - SPI (przepięty z GPIO35/36 z powodu konfliktu z Octal PSRAM)
#define PIN_OLED_MOSI       11
#define PIN_OLED_CLK        12
#define PIN_OLED_RESET      40
#define PIN_OLED_DC         41
#define PIN_OLED_CS         42

// DS3231 RTC - I2C
#define PIN_I2C_SDA         8
#define PIN_I2C_SCL         9
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         100000

// Rotary Encoder
#define PIN_ENC_A           4
#define PIN_ENC_B           5
#define PIN_ENC_SW          6

// 74HC595 Shift Register (x2 daisy-chain - 16 bitów)
// Steruje jednocześnie: LED-ami + SSR relay (zawory)
#define PIN_595_SER         18   // SER  - data
#define PIN_595_SRCLK       21   // SRCLK - shift clock
#define PIN_595_RCLK        38   // RCLK  - latch clock

// =============================================================================
// 74HC595 Bit Mapping (16-bit: chip2_high | chip1_low)
// Bit HIGH = SSR ON (zawór otwarty) + LED ON
// =============================================================================
#define BIT_MASTER          (1 << 0)   // Q0 chip1 - zawór główny
#define BIT_SECTION_1       (1 << 1)   // Q1 chip1
#define BIT_SECTION_2       (1 << 2)   // Q2 chip1
#define BIT_SECTION_3       (1 << 3)   // Q3 chip1
#define BIT_SECTION_4       (1 << 4)   // Q4 chip1
#define BIT_SECTION_5       (1 << 5)   // Q5 chip1
#define BIT_SECTION_6       (1 << 6)   // Q6 chip1
#define BIT_SECTION_7       (1 << 7)   // Q7 chip1
#define BIT_SECTION_8       (1 << 8)   // Q0 chip2
#define BIT_LED_WIFI        (1 << 9)   // Q1 chip2 - tylko LED
#define BIT_LED_POWER       (1 << 10)  // Q2 chip2 - tylko LED
// Bity 11-15: rezerwa

#define SECTIONS_COUNT      8
#define GROUPS_MAX          10
#define SCHEDULE_ENTRIES    16

// =============================================================================
// Application defaults
// =============================================================================
#define DEFAULT_NTP_SERVER      "pl.pool.ntp.org"
#define DEFAULT_AP_SSID         "WachcioDrop"
#define DEFAULT_AP_PASS         "wachciodrop123"
#define AP_IP_ADDR              "192.168.4.1"

#define WIFI_CONNECT_TIMEOUT_MS (30 * 1000)
#define WIFI_MAX_RETRIES        3
#define NTP_SYNC_INTERVAL_MS    (24 * 60 * 60 * 1000)
#define DAILY_CHECK_HOUR        0
#define DAILY_CHECK_MINUTE      5

#define API_TOKEN_LEN           32
#define SCHEDULE_DURATION_MAX   (4 * 60 * 60)  // 4 godziny max

// =============================================================================
// FreeRTOS task priorities (1=idle, configMAX_PRIORITIES-1=highest)
// =============================================================================
#define TASK_PRIO_VALVE         6
#define TASK_PRIO_SCHEDULER     5
#define TASK_PRIO_ENCODER       4
#define TASK_PRIO_WIFI          4
#define TASK_PRIO_DISPLAY       3
#define TASK_PRIO_MQTT          3
#define TASK_PRIO_WEBSERVER     3
#define TASK_PRIO_NTP           2
#define TASK_PRIO_LEDS          2
#define TASK_PRIO_DAILY_CHECK   2

#define TASK_STACK_VALVE        2048
#define TASK_STACK_SCHEDULER    4096
#define TASK_STACK_ENCODER      2048
#define TASK_STACK_DISPLAY      4096
#define TASK_STACK_LEDS         2048
#define TASK_STACK_WIFI         4096
#define TASK_STACK_NTP          3072
#define TASK_STACK_MQTT         6144
#define TASK_STACK_WEBSERVER    8192
#define TASK_STACK_DAILY_CHECK  4096
