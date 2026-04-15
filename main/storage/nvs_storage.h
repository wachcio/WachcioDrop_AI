#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Klucze NVS
#define NVS_NAMESPACE_CONFIG    "cfg"
#define NVS_NAMESPACE_SCHEDULE  "sched"
#define NVS_NAMESPACE_GROUPS    "groups"

#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_MQTT_URI        "mqtt_uri"
#define NVS_KEY_MQTT_USER       "mqtt_user"
#define NVS_KEY_MQTT_PASS       "mqtt_pass"
#define NVS_KEY_PHP_URL         "php_url"
#define NVS_KEY_API_TOKEN       "api_token"
#define NVS_KEY_NTP_SERVER      "ntp_server"
#define NVS_KEY_TZ_OFFSET       "tz_offset"
#define NVS_KEY_IRRIG_TODAY     "irrig_today"
#define NVS_KEY_IGNORE_PHP      "ignore_php"
#define NVS_KEY_GRAYLOG_HOST    "glog_host"
#define NVS_KEY_GRAYLOG_PORT    "glog_port"
#define NVS_KEY_GRAYLOG_EN      "glog_en"
#define NVS_KEY_GRAYLOG_LEVEL   "glog_level"
#define NVS_KEY_FROST_EN        "frost_en"
#define NVS_KEY_FROST_TEMP      "frost_temp"
#define NVS_KEY_FROST_DELAY     "frost_delay"

typedef struct {
    char    wifi_ssid[64];
    char    wifi_pass[64];
    char    mqtt_uri[128];
    char    mqtt_user[64];
    char    mqtt_pass[64];
    char    php_url[256];
    char    api_token[64];
    char    ntp_server[64];
    int8_t  timezone_offset;
    bool    irrigation_today;
    bool    ignore_php;
    char    graylog_host[64];
    uint16_t graylog_port;
    bool    graylog_enabled;
    uint8_t graylog_level;     // LOG_LVL_* (6=INFO domyślnie)
    bool     frost_protection_enabled;
    int8_t   frost_temp_threshold;      // °C, domyślnie 3
    uint16_t frost_recovery_delay_min;  // minuty opóźnienia reaktywacji, domyślnie 60
} app_config_t;

typedef struct {
    uint8_t  id;
    bool     enabled;
    uint8_t  days_mask;     // bit0=Pon, bit1=Wt, ..., bit6=Nie
    uint8_t  hour;
    uint8_t  minute;
    uint16_t duration_sec;
    uint8_t  section_mask;  // bit0-7 = sekcje 1-8
    uint16_t group_mask;    // bit0-9 = grupy 1-10
} schedule_entry_t;

typedef struct {
    uint8_t id;             // 1-10
    char    name[16];
    uint8_t section_mask;   // bit0-7 = sekcje 1-8
} irrigation_group_t;

// Inicjalizacja NVS
esp_err_t storage_init(void);

// Konfiguracja
esp_err_t storage_load_config(app_config_t *cfg);
esp_err_t storage_save_config(const app_config_t *cfg);

// Harmonogram (16 wpisów, indeks 0-15)
esp_err_t storage_load_schedule(schedule_entry_t *entries, uint8_t count);
esp_err_t storage_save_schedule(const schedule_entry_t *entries, uint8_t count);
esp_err_t storage_save_schedule_entry(const schedule_entry_t *entry);

// Grupy (10 grup, indeks 0-9)
esp_err_t storage_load_groups(irrigation_group_t *groups, uint8_t count);
esp_err_t storage_save_groups(const irrigation_group_t *groups, uint8_t count);
esp_err_t storage_save_group(const irrigation_group_t *group);

// Generowanie losowego tokenu API (32 znaki hex)
esp_err_t storage_generate_token(char *out, size_t len);
