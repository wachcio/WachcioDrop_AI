#include "nvs_storage.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "storage";

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erase required, erasing...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "NVS init OK");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// Config
// --------------------------------------------------------------------------

esp_err_t storage_load_config(app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_CONFIG, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Pierwsze uruchomienie: ustaw wartości domyślne
        memset(cfg, 0, sizeof(*cfg));
        strncpy(cfg->ntp_server, DEFAULT_NTP_SERVER, sizeof(cfg->ntp_server) - 1);
        cfg->timezone_offset  = 1;
        cfg->irrigation_today = true;
        cfg->graylog_port             = 12201;
        cfg->graylog_level            = 6;
        cfg->frost_protection_enabled  = false;
        cfg->frost_temp_threshold      = 3;
        cfg->frost_recovery_delay_min  = 60;
        ESP_LOGI(TAG, "config not found, using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    size_t len;
    #define LOAD_STR(key, field) \
        len = sizeof(cfg->field); \
        nvs_get_str(h, key, cfg->field, &len)

    LOAD_STR(NVS_KEY_WIFI_SSID,  wifi_ssid);
    LOAD_STR(NVS_KEY_WIFI_PASS,  wifi_pass);
    LOAD_STR(NVS_KEY_MQTT_URI,   mqtt_uri);
    LOAD_STR(NVS_KEY_MQTT_USER,  mqtt_user);
    LOAD_STR(NVS_KEY_MQTT_PASS,  mqtt_pass);
    LOAD_STR(NVS_KEY_PHP_URL,    php_url);
    LOAD_STR(NVS_KEY_API_TOKEN,  api_token);
    LOAD_STR(NVS_KEY_NTP_SERVER, ntp_server);
    #undef LOAD_STR

    int8_t tz = 1;
    nvs_get_i8(h, NVS_KEY_TZ_OFFSET, &tz);
    cfg->timezone_offset = tz;

    uint8_t today = 1;
    nvs_get_u8(h, NVS_KEY_IRRIG_TODAY, &today);
    cfg->irrigation_today = (bool)today;

    uint8_t ignore_php = 0;
    nvs_get_u8(h, NVS_KEY_IGNORE_PHP, &ignore_php);
    cfg->ignore_php = (bool)ignore_php;

    len = sizeof(cfg->graylog_host);
    nvs_get_str(h, NVS_KEY_GRAYLOG_HOST, cfg->graylog_host, &len);

    cfg->graylog_port = 12201;
    nvs_get_u16(h, NVS_KEY_GRAYLOG_PORT, &cfg->graylog_port);

    uint8_t glog_en = 0;
    nvs_get_u8(h, NVS_KEY_GRAYLOG_EN, &glog_en);
    cfg->graylog_enabled = (bool)glog_en;

    cfg->graylog_level = 6; // INFO
    nvs_get_u8(h, NVS_KEY_GRAYLOG_LEVEL, &cfg->graylog_level);

    uint8_t frost_en = 0;
    nvs_get_u8(h, NVS_KEY_FROST_EN, &frost_en);
    cfg->frost_protection_enabled = (bool)frost_en;

    cfg->frost_temp_threshold = 3;
    int8_t frost_temp = 3;
    nvs_get_i8(h, NVS_KEY_FROST_TEMP, &frost_temp);
    cfg->frost_temp_threshold = frost_temp;

    cfg->frost_recovery_delay_min = 60;
    nvs_get_u16(h, NVS_KEY_FROST_DELAY, &cfg->frost_recovery_delay_min);

    nvs_close(h);
    ESP_LOGI(TAG, "config loaded (ssid='%s' ntp='%s')",
             cfg->wifi_ssid, cfg->ntp_server);
    return ESP_OK;
}

esp_err_t storage_save_config(const app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_CONFIG, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    #define SAVE_STR(key, field) nvs_set_str(h, key, cfg->field)
    SAVE_STR(NVS_KEY_WIFI_SSID,  wifi_ssid);
    SAVE_STR(NVS_KEY_WIFI_PASS,  wifi_pass);
    SAVE_STR(NVS_KEY_MQTT_URI,   mqtt_uri);
    SAVE_STR(NVS_KEY_MQTT_USER,  mqtt_user);
    SAVE_STR(NVS_KEY_MQTT_PASS,  mqtt_pass);
    SAVE_STR(NVS_KEY_PHP_URL,    php_url);
    SAVE_STR(NVS_KEY_API_TOKEN,  api_token);
    SAVE_STR(NVS_KEY_NTP_SERVER, ntp_server);
    #undef SAVE_STR

    nvs_set_i8(h, NVS_KEY_TZ_OFFSET,   cfg->timezone_offset);
    nvs_set_u8(h, NVS_KEY_IRRIG_TODAY, (uint8_t)cfg->irrigation_today);
    nvs_set_u8(h, NVS_KEY_IGNORE_PHP,  (uint8_t)cfg->ignore_php);
    nvs_set_str(h, NVS_KEY_GRAYLOG_HOST, cfg->graylog_host);
    nvs_set_u16(h, NVS_KEY_GRAYLOG_PORT, cfg->graylog_port);
    nvs_set_u8(h, NVS_KEY_GRAYLOG_EN,   (uint8_t)cfg->graylog_enabled);
    nvs_set_u8(h, NVS_KEY_GRAYLOG_LEVEL, cfg->graylog_level);
    nvs_set_u8(h,  NVS_KEY_FROST_EN,    (uint8_t)cfg->frost_protection_enabled);
    nvs_set_i8(h,  NVS_KEY_FROST_TEMP,  cfg->frost_temp_threshold);
    nvs_set_u16(h, NVS_KEY_FROST_DELAY, cfg->frost_recovery_delay_min);

    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "config saved");
    return err;
}

// --------------------------------------------------------------------------
// Schedule
// --------------------------------------------------------------------------

esp_err_t storage_load_schedule(schedule_entry_t *entries, uint8_t count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_SCHEDULE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(entries, 0, count * sizeof(schedule_entry_t));
        for (uint8_t i = 0; i < count; i++) entries[i].id = i;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    for (uint8_t i = 0; i < count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "s%02d", i);
        size_t len = sizeof(schedule_entry_t);
        nvs_get_blob(h, key, &entries[i], &len);
        entries[i].id = i;
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t storage_save_schedule(const schedule_entry_t *entries, uint8_t count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_SCHEDULE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    for (uint8_t i = 0; i < count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "s%02d", i);
        nvs_set_blob(h, key, &entries[i], sizeof(schedule_entry_t));
    }
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "schedule saved (%d entries)", count);
    return err;
}

esp_err_t storage_save_schedule_entry(const schedule_entry_t *entry)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_SCHEDULE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    char key[8];
    snprintf(key, sizeof(key), "s%02d", entry->id);
    nvs_set_blob(h, key, entry, sizeof(schedule_entry_t));
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// --------------------------------------------------------------------------
// Groups
// --------------------------------------------------------------------------

esp_err_t storage_load_groups(irrigation_group_t *groups, uint8_t count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_GROUPS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(groups, 0, count * sizeof(irrigation_group_t));
        for (uint8_t i = 0; i < count; i++) {
            groups[i].id = i + 1;
            snprintf(groups[i].name, sizeof(groups[i].name), "Grupa %d", i + 1);
        }
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    for (uint8_t i = 0; i < count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "g%02d", i);
        size_t len = sizeof(irrigation_group_t);
        nvs_get_blob(h, key, &groups[i], &len);
        groups[i].id = i + 1;  // zawsze ustaw poprawne id (1-based)
        if (groups[i].name[0] == '\0')
            snprintf(groups[i].name, sizeof(groups[i].name), "Grupa %d", i + 1);
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t storage_save_groups(const irrigation_group_t *groups, uint8_t count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_GROUPS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    for (uint8_t i = 0; i < count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "g%02d", i);
        nvs_set_blob(h, key, &groups[i], sizeof(irrigation_group_t));
    }
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t storage_save_group(const irrigation_group_t *group)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_GROUPS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    char key[8];
    snprintf(key, sizeof(key), "g%02d", group->id - 1);
    nvs_set_blob(h, key, group, sizeof(irrigation_group_t));
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// --------------------------------------------------------------------------
// Token
// --------------------------------------------------------------------------

esp_err_t storage_generate_token(char *out, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    size_t bytes = (len - 1) / 2;
    for (size_t i = 0; i < bytes; i++) {
        uint8_t r = (uint8_t)esp_random();
        out[i * 2]     = hex[r >> 4];
        out[i * 2 + 1] = hex[r & 0x0F];
    }
    out[bytes * 2] = '\0';
    return ESP_OK;
}
