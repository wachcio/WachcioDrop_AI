#include "rest_api.h"
#include "file_server.h"
#include "valve/valve.h"
#include "leds/leds.h"
#include "config.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_spiffs.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include "schedule/schedule.h"
#include "groups/groups.h"
#include "storage/nvs_storage.h"
#include "rtc/rtc.h"
#include "ntp/ntp.h"
#include "wifi/wifi_manager.h"
#include "config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "logging/log_manager.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "rest_api";

extern app_config_t g_config;
extern bool         g_irrigation_today;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

#define JSON_RESP(req, json_str) do { \
    httpd_resp_set_type(req, "application/json"); \
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); \
    httpd_resp_send(req, json_str, strlen(json_str)); \
} while(0)

#define CHECK_AUTH(req) do { \
    if (!rest_auth_check(req)) { \
        httpd_resp_set_status(req, "401 Unauthorized"); \
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer"); \
        httpd_resp_send(req, "{\"error\":\"Unauthorized\"}", 23); \
        return ESP_OK; \
    } \
} while(0)

static int read_body(httpd_req_t *req, char *buf, size_t max)
{
    int total = 0, remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf + total,
                               MIN(remaining, (int)(max - total - 1)));
        if (n <= 0) return -1;
        total     += n;
        remaining -= n;
    }
    buf[total] = '\0';
    return total;
}

bool rest_auth_check(httpd_req_t *req)
{
    if (g_config.api_token[0] == '\0') return true; // token nie ustawiony

    char auth[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization",
                                    auth, sizeof(auth)) != ESP_OK) {
        return false;
    }
    // Oczekiwany format: "Bearer <token>"
    if (strncmp(auth, "Bearer ", 7) != 0) return false;
    return strcmp(auth + 7, g_config.api_token) == 0;
}

// --------------------------------------------------------------------------
// GET /api/status
// --------------------------------------------------------------------------
// GET /api/info
// --------------------------------------------------------------------------
static esp_err_t handle_info(httpd_req_t *req)
{
    CHECK_AUTH(req);

    const esp_app_desc_t *app = esp_app_get_description();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "fw_version",  FW_VERSION);
    cJSON_AddStringToObject(root, "author",       FW_AUTHOR);
    cJSON_AddStringToObject(root, "idf_version",  IDF_VER);
    cJSON_AddStringToObject(root, "app_name",     app->project_name);
    cJSON_AddStringToObject(root, "build_date",   app->date);
    cJSON_AddStringToObject(root, "build_time",   app->time);
    char build_hash[65] = {0};
    for (int i = 0; i < 32; i++) {
        snprintf(build_hash + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    }
    cJSON_AddStringToObject(root, "build_hash", build_hash);
    cJSON_AddNumberToObject(root, "flash_mb",     CONFIG_ESPTOOLPY_FLASHSIZE_16MB ? 16 : 4);
    cJSON_AddNumberToObject(root, "psram_kb",     esp_psram_get_size() / 1024);
    cJSON_AddNumberToObject(root, "heap_free",    esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "sections",          SECTIONS_COUNT);
    cJSON_AddNumberToObject(root, "groups_max",        GROUPS_MAX);
    cJSON_AddNumberToObject(root, "schedule_max",      SCHEDULE_ENTRIES);
    cJSON_AddNumberToObject(root, "daily_check_hour",  DAILY_CHECK_HOUR);
    cJSON_AddNumberToObject(root, "daily_check_minute", DAILY_CHECK_MINUTE);

    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    JSON_RESP(req, s);
    free(s);
    return ESP_OK;
}

// --------------------------------------------------------------------------
static esp_err_t handle_status(httpd_req_t *req)
{
    CHECK_AUTH(req);

    char ip[16] = "0.0.0.0";
    wifi_get_ip(ip, sizeof(ip));

    time_t now = time(NULL);
    struct tm t_local;
    localtime_r(&now, &t_local);
    char tstr[32];
    strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t_local);

    uint8_t mask = valve_get_active_mask();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime_sec",
                            (double)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(root, "ip",             ip);
    cJSON_AddNumberToObject(root, "rssi",           wifi_get_rssi());
    cJSON_AddNumberToObject(root, "sections_active", __builtin_popcount(mask));
    cJSON_AddBoolToObject(root,   "master_active",  (bool)(leds_get() & BIT_MASTER));
    cJSON_AddBoolToObject(root,   "irrigation_today", g_irrigation_today);
    cJSON_AddBoolToObject(root,   "ignore_php",      g_config.ignore_php);
    cJSON_AddBoolToObject(root,   "php_url_set",     g_config.php_url[0] != '\0');
    cJSON_AddStringToObject(root, "time",           tstr);

    // Wszystkie sekcje
    cJSON *sections_arr = cJSON_AddArrayToObject(root, "sections");
    for (int i = 1; i <= SECTIONS_COUNT; i++) {
        bool     active  = valve_is_section_active(i);
        uint32_t rem     = valve_get_remaining_sec(i);
        time_t   started = valve_get_started_at(i);

        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "id",     i);
        cJSON_AddBoolToObject(s,   "active", active);

        if (active && started > 0) {
            struct tm st_tm;
            localtime_r(&started, &st_tm);
            char st_str[32];
            strftime(st_str, sizeof(st_str), "%Y-%m-%dT%H:%M:%S", &st_tm);
            cJSON_AddStringToObject(s, "started_at", st_str);
        } else {
            cJSON_AddNullToObject(s, "started_at");
        }

        if (active && rem > 0) {
            time_t ends_ts = now + (time_t)rem;
            struct tm ends_tm;
            localtime_r(&ends_ts, &ends_tm);
            char ends_str[32];
            strftime(ends_str, sizeof(ends_str), "%Y-%m-%dT%H:%M:%S", &ends_tm);
            cJSON_AddStringToObject(s, "ends_at",       ends_str);
            cJSON_AddNumberToObject(s, "remaining_sec", (double)rem);
        } else if (active && rem == 0) {
            cJSON_AddNullToObject(s, "ends_at");
            cJSON_AddNullToObject(s, "remaining_sec");
        } else {
            cJSON_AddNullToObject(s, "ends_at");
            cJSON_AddNullToObject(s, "remaining_sec");
        }

        cJSON_AddItemToArray(sections_arr, s);
    }

    // Wszystkie grupy — aktywna jest tylko ta, którą jawnie aktywowano przez groups_activate()
    const irrigation_group_t *grps = groups_get_all();
    uint8_t active_group_id = groups_get_active_id();
    cJSON *groups_arr = cJSON_AddArrayToObject(root, "groups");
    for (int i = 0; i < GROUPS_MAX; i++) {
        bool grp_active = (active_group_id != 0) &&
                          (grps[i].id == active_group_id) &&
                          (grps[i].section_mask != 0) &&
                          ((mask & grps[i].section_mask) != 0);

        cJSON *g = cJSON_CreateObject();
        cJSON_AddNumberToObject(g, "id",           grps[i].id);
        cJSON_AddStringToObject(g, "name",         grps[i].name);
        cJSON_AddNumberToObject(g, "section_mask", grps[i].section_mask);
        cJSON_AddBoolToObject(g,   "active",       grp_active);

        if (grp_active) {
            // started_at = najwcześniejszy czas startu aktywnych sekcji grupy
            time_t earliest_start = 0;
            uint32_t min_rem = UINT32_MAX;
            bool any_indefinite = false;
            for (int b = 0; b < SECTIONS_COUNT; b++) {
                if (!(grps[i].section_mask & (1 << b))) continue;
                if (!valve_is_section_active(b + 1)) continue;
                time_t st = valve_get_started_at(b + 1);
                if (earliest_start == 0 || st < earliest_start) earliest_start = st;
                uint32_t r = valve_get_remaining_sec(b + 1);
                if (r == 0) { any_indefinite = true; }
                else if (r < min_rem) min_rem = r;
            }

            if (earliest_start > 0) {
                struct tm st_tm;
                localtime_r(&earliest_start, &st_tm);
                char st_str[32];
                strftime(st_str, sizeof(st_str), "%Y-%m-%dT%H:%M:%S", &st_tm);
                cJSON_AddStringToObject(g, "started_at", st_str);
            } else {
                cJSON_AddNullToObject(g, "started_at");
            }

            if (any_indefinite || min_rem == UINT32_MAX) {
                cJSON_AddNullToObject(g, "ends_at");
                cJSON_AddNullToObject(g, "remaining_sec");
            } else {
                time_t ends_ts = now + (time_t)min_rem;
                struct tm ends_tm;
                localtime_r(&ends_ts, &ends_tm);
                char ends_str[32];
                strftime(ends_str, sizeof(ends_str), "%Y-%m-%dT%H:%M:%S", &ends_tm);
                cJSON_AddStringToObject(g, "ends_at",       ends_str);
                cJSON_AddNumberToObject(g, "remaining_sec", (double)min_rem);
            }
        } else {
            cJSON_AddNullToObject(g, "started_at");
            cJSON_AddNullToObject(g, "ends_at");
            cJSON_AddNullToObject(g, "remaining_sec");
        }

        cJSON_AddItemToArray(groups_arr, g);
    }

    char *s = cJSON_PrintUnformatted(root);
    JSON_RESP(req, s);
    free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// GET /api/sections
// --------------------------------------------------------------------------
static esp_err_t handle_sections_get(httpd_req_t *req)
{
    CHECK_AUTH(req);

    uint8_t mask = valve_get_active_mask();
    cJSON *root  = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "master", (bool)(leds_get() & BIT_MASTER));
    cJSON *arr = cJSON_AddArrayToObject(root, "sections");
    for (int i = 1; i <= SECTIONS_COUNT; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "id",     i);
        cJSON_AddBoolToObject(s,   "active", (bool)(mask & (1 << (i-1))));
        cJSON_AddItemToArray(arr, s);
    }

    char *str = cJSON_PrintUnformatted(root);
    JSON_RESP(req, str);
    free(str);
    cJSON_Delete(root);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// POST /api/sections/*
// Obsługuje: /api/sections/{id}/on  /api/sections/{id}/off  /api/sections/all/off
// Uwaga: httpd_uri_match_wildcard obsługuje '*' tylko na końcu wzorca,
//        dlatego używamy jednego handlera z dispatching po URI.
// --------------------------------------------------------------------------
static esp_err_t handle_section_post(httpd_req_t *req)
{
    CHECK_AUTH(req);

    const char *uri = req->uri;

    // /api/sections/all/off
    if (strstr(uri, "/all/off")) {
        valve_all_off();
        JSON_RESP(req, "{\"ok\":true}");
        return ESP_OK;
    }

    // Rozróżnij /on i /off po końcówce URI
    bool is_on  = (strstr(uri, "/on")  != NULL);
    bool is_off = (strstr(uri, "/off") != NULL);

    if (!is_on && !is_off) {
        httpd_resp_set_status(req, "404 Not Found");
        JSON_RESP(req, "{\"error\":\"unknown action\"}");
        return ESP_OK;
    }

    int section_id = 0;
    sscanf(uri, "/api/sections/%d/", &section_id);
    if (section_id < 1 || section_id > SECTIONS_COUNT) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid section\"}");
        return ESP_OK;
    }

    if (is_on) {
        uint32_t duration = 0;
        char body[128] = {0};
        if (req->content_len > 0 && read_body(req, body, sizeof(body)) > 0) {
            cJSON *j = cJSON_Parse(body);
            if (j) {
                cJSON *d = cJSON_GetObjectItem(j, "duration");
                if (d) duration = (uint32_t)d->valuedouble;
                cJSON_Delete(j);
            }
        }
        valve_section_on((uint8_t)section_id, duration);
    } else {
        valve_section_off((uint8_t)section_id);
    }

    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// GET/PUT/DELETE /api/schedule[/{id}]
// --------------------------------------------------------------------------
static esp_err_t handle_schedule_get(httpd_req_t *req)
{
    CHECK_AUTH(req);

    const schedule_entry_t *entries = schedule_get_all();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < SCHEDULE_ENTRIES; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id",           entries[i].id);
        cJSON_AddBoolToObject(e,   "enabled",      entries[i].enabled);
        cJSON_AddNumberToObject(e, "days_mask",    entries[i].days_mask);
        cJSON_AddNumberToObject(e, "hour",         entries[i].hour);
        cJSON_AddNumberToObject(e, "minute",       entries[i].minute);
        cJSON_AddNumberToObject(e, "duration_sec", entries[i].duration_sec);
        cJSON_AddNumberToObject(e, "section_mask", entries[i].section_mask);
        cJSON_AddNumberToObject(e, "group_mask",   entries[i].group_mask);
        cJSON_AddItemToArray(arr, e);
    }
    char *s = cJSON_PrintUnformatted(arr);
    JSON_RESP(req, s);
    free(s);
    cJSON_Delete(arr);
    return ESP_OK;
}

static esp_err_t handle_schedule_put(httpd_req_t *req)
{
    CHECK_AUTH(req);

    int id = 0;
    sscanf(req->uri, "/api/schedule/%d", &id);
    if (id < 0 || id >= SCHEDULE_ENTRIES) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid id\"}");
        return ESP_OK;
    }

    char body[256] = {0};
    if (read_body(req, body, sizeof(body)) <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"no body\"}");
        return ESP_OK;
    }

    cJSON *j = cJSON_Parse(body);
    if (!j) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid JSON\"}");
        return ESP_OK;
    }

    schedule_entry_t entry = {0};
    entry.id = (uint8_t)id;
    cJSON *v;
    if ((v = cJSON_GetObjectItem(j, "enabled")))      entry.enabled       = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "days_mask")))    entry.days_mask     = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "hour")))         entry.hour          = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "minute")))       entry.minute        = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "duration_sec"))) entry.duration_sec  = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "section_mask"))) entry.section_mask  = (uint8_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "group_mask")))   entry.group_mask    = (uint16_t)v->valuedouble;
    cJSON_Delete(j);

    schedule_set(&entry);
    APP_LOGI("api", "Harmonogram #%d: %s dni=0x%02X %02d:%02d czas=%us sek=0x%02X grp=0x%04X",
             id, entry.enabled ? "wł" : "wył",
             entry.days_mask, entry.hour, entry.minute,
             entry.duration_sec, entry.section_mask, entry.group_mask);
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t handle_schedule_delete(httpd_req_t *req)
{
    CHECK_AUTH(req);

    int id = 0;
    sscanf(req->uri, "/api/schedule/%d", &id);
    if (id < 0 || id >= SCHEDULE_ENTRIES) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid id\"}");
        return ESP_OK;
    }
    schedule_delete((uint8_t)id);
    APP_LOGI("api", "Harmonogram #%d: usunięty", id);
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// GET/PUT /api/groups[/{id}]
// POST /api/groups/{id}/activate
// --------------------------------------------------------------------------
static esp_err_t handle_groups_get(httpd_req_t *req)
{
    CHECK_AUTH(req);

    const irrigation_group_t *grps = groups_get_all();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < GROUPS_MAX; i++) {
        cJSON *g = cJSON_CreateObject();
        cJSON_AddNumberToObject(g, "id",           grps[i].id);
        cJSON_AddStringToObject(g, "name",         grps[i].name);
        cJSON_AddNumberToObject(g, "section_mask", grps[i].section_mask);
        cJSON_AddItemToArray(arr, g);
    }
    char *s = cJSON_PrintUnformatted(arr);
    JSON_RESP(req, s);
    free(s);
    cJSON_Delete(arr);
    return ESP_OK;
}

static esp_err_t handle_groups_put(httpd_req_t *req)
{
    CHECK_AUTH(req);

    int id = 0;
    sscanf(req->uri, "/api/groups/%d", &id);
    if (id < 1 || id > GROUPS_MAX) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid id\"}");
        return ESP_OK;
    }

    char body[128] = {0};
    if (read_body(req, body, sizeof(body)) <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"no body\"}");
        return ESP_OK;
    }

    cJSON *j = cJSON_Parse(body);
    if (!j) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid JSON\"}");
        return ESP_OK;
    }

    irrigation_group_t group = {0};
    group.id = (uint8_t)id;
    cJSON *v;
    if ((v = cJSON_GetObjectItem(j, "name"))) {
        strncpy(group.name, v->valuestring, sizeof(group.name) - 1);
    }
    if ((v = cJSON_GetObjectItem(j, "section_mask"))) {
        group.section_mask = (uint8_t)v->valuedouble;
    }
    cJSON_Delete(j);

    groups_set(&group);
    APP_LOGI("api", "Grupa %d: nazwa='%s' sekcje=0x%02X", id, group.name, group.section_mask);
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// DELETE /api/groups/{id} — reset grupy do domyślnych wartości
static esp_err_t handle_groups_delete(httpd_req_t *req)
{
    CHECK_AUTH(req);

    int id = 0;
    sscanf(req->uri, "/api/groups/%d", &id);
    if (id < 1 || id > GROUPS_MAX) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid id\"}");
        return ESP_OK;
    }

    irrigation_group_t g = {0};
    g.id = (uint8_t)id;
    snprintf(g.name, sizeof(g.name), "Grupa %d", id);
    g.section_mask = 0;
    groups_set(&g);
    APP_LOGI("api", "Grupa %d: usunięta (reset do domyślnych)", id);

    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// POST /api/groups/* — obsługuje /activate i zwykły POST (dispatch po URI)
static esp_err_t handle_groups_post(httpd_req_t *req)
{
    CHECK_AUTH(req);

    int id = 0;
    sscanf(req->uri, "/api/groups/%d", &id);
    if (id < 1 || id > GROUPS_MAX) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid id\"}");
        return ESP_OK;
    }

    uint32_t duration = 0;
    char body[128] = {0};
    if (req->content_len > 0) read_body(req, body, sizeof(body));

    cJSON *j = cJSON_Parse(body);
    if (j) {
        cJSON *d = cJSON_GetObjectItem(j, "duration");
        if (d) duration = (uint32_t)d->valuedouble;
        cJSON_Delete(j);
    }

    groups_activate((uint8_t)id, duration);
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// GET/POST /api/settings
// --------------------------------------------------------------------------
static esp_err_t handle_settings_get(httpd_req_t *req)
{
    CHECK_AUTH(req);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid",   g_config.wifi_ssid);
    cJSON_AddStringToObject(root, "mqtt_uri",    g_config.mqtt_uri);
    cJSON_AddStringToObject(root, "mqtt_user",   g_config.mqtt_user);
    cJSON_AddStringToObject(root, "php_url",     g_config.php_url);
    cJSON_AddStringToObject(root, "ntp_server",     g_config.ntp_server);
    cJSON_AddNumberToObject(root, "tz_offset",      g_config.timezone_offset);
    cJSON_AddStringToObject(root, "graylog_host",   g_config.graylog_host);
    cJSON_AddNumberToObject(root, "graylog_port",   g_config.graylog_port);
    cJSON_AddBoolToObject  (root, "graylog_enabled",g_config.graylog_enabled);
    cJSON_AddNumberToObject(root, "graylog_level",  g_config.graylog_level);
    // Nie zwracamy hasła i tokenu w GET

    char *s = cJSON_PrintUnformatted(root);
    JSON_RESP(req, s);
    free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_settings_post(httpd_req_t *req)
{
    CHECK_AUTH(req);

    char body[512] = {0};
    if (read_body(req, body, sizeof(body)) <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"no body\"}");
        return ESP_OK;
    }

    cJSON *j = cJSON_Parse(body);
    if (!j) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *v;
    bool wifi_changed = false;
    #define COPY_STR(key, field) \
        if ((v = cJSON_GetObjectItem(j, key)) && v->valuestring) \
            strncpy(g_config.field, v->valuestring, sizeof(g_config.field) - 1)

    if ((v = cJSON_GetObjectItem(j, "wifi_ssid")) && v->valuestring) {
        strncpy(g_config.wifi_ssid, v->valuestring, sizeof(g_config.wifi_ssid) - 1);
        wifi_changed = true;
    }
    if ((v = cJSON_GetObjectItem(j, "wifi_pass")) && v->valuestring) {
        strncpy(g_config.wifi_pass, v->valuestring, sizeof(g_config.wifi_pass) - 1);
        wifi_changed = true;
    }
    COPY_STR("mqtt_uri",   mqtt_uri);
    COPY_STR("mqtt_user",  mqtt_user);
    COPY_STR("mqtt_pass",  mqtt_pass);
    COPY_STR("php_url",    php_url);
    COPY_STR("ntp_server", ntp_server);
    COPY_STR("api_token",  api_token);
    #undef COPY_STR

    if ((v = cJSON_GetObjectItem(j, "tz_offset")))
        g_config.timezone_offset = (int8_t)v->valuedouble;

    #define COPY_STR2(key, field) \
        if ((v = cJSON_GetObjectItem(j, key)) && v->valuestring) \
            strncpy(g_config.field, v->valuestring, sizeof(g_config.field) - 1)
    COPY_STR2("graylog_host", graylog_host);
    #undef COPY_STR2

    if ((v = cJSON_GetObjectItem(j, "graylog_port")))
        g_config.graylog_port = (uint16_t)v->valuedouble;
    if ((v = cJSON_GetObjectItem(j, "graylog_enabled")))
        g_config.graylog_enabled = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "graylog_level")))
        g_config.graylog_level = (uint8_t)v->valuedouble;

    cJSON_Delete(j);
    storage_save_config(&g_config);
    APP_LOGI("api", "Settings saved");
    if (wifi_changed && g_config.wifi_ssid[0] != '\0')
        wifi_trigger_reconnect();
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// GET/POST /api/time
// --------------------------------------------------------------------------
static esp_err_t handle_time_get(httpd_req_t *req)
{
    CHECK_AUTH(req);

    time_t now = time(NULL);
    struct tm t_local = {0};
    localtime_r(&now, &t_local);
    char tstr[32];
    strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t_local);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "time",  tstr);
    cJSON_AddNumberToObject(root, "unix",  (double)now);
    cJSON_AddNumberToObject(root, "tz",    g_config.timezone_offset);
    char *s = cJSON_PrintUnformatted(root);
    JSON_RESP(req, s);
    free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// POST /api/time
// Akceptuje:
//   {"unix": 1750000200}                  — unix timestamp UTC
//   {"datetime": "2025-06-15T10:30:00"}   — czas lokalny (strefa z TZ)
//   {"year":2025,"month":6,"day":15,
//    "hour":10,"minute":30,"second":0}    — pola osobno (czas lokalny)
// --------------------------------------------------------------------------
static esp_err_t handle_time_post(httpd_req_t *req)
{
    CHECK_AUTH(req);

    char body[128] = {0};
    if (read_body(req, body, sizeof(body)) <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"no body\"}");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    if (!j) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid JSON\"}");
        return ESP_OK;
    }

    time_t t = 0;
    cJSON *v;

    if ((v = cJSON_GetObjectItem(j, "unix")) != NULL) {
        t = (time_t)(long long)v->valuedouble;

    } else if ((v = cJSON_GetObjectItem(j, "datetime")) != NULL && v->valuestring) {
        struct tm tm = {0};
        if (strptime(v->valuestring, "%Y-%m-%dT%H:%M:%S", &tm) == NULL) {
            cJSON_Delete(j);
            httpd_resp_set_status(req, "400 Bad Request");
            JSON_RESP(req, "{\"error\":\"invalid datetime format, expected YYYY-MM-DDTHH:MM:SS\"}");
            return ESP_OK;
        }
        tm.tm_isdst = -1;
        t = mktime(&tm);

    } else {
        cJSON *yr = cJSON_GetObjectItem(j, "year");
        cJSON *mo = cJSON_GetObjectItem(j, "month");
        cJSON *dy = cJSON_GetObjectItem(j, "day");
        cJSON *hr = cJSON_GetObjectItem(j, "hour");
        cJSON *mn = cJSON_GetObjectItem(j, "minute");
        cJSON *sc = cJSON_GetObjectItem(j, "second");

        if (!yr || !mo || !dy || !hr || !mn) {
            cJSON_Delete(j);
            httpd_resp_set_status(req, "400 Bad Request");
            JSON_RESP(req, "{\"error\":\"provide unix, datetime, or year/month/day/hour/minute\"}");
            return ESP_OK;
        }

        struct tm tm = {0};
        tm.tm_year  = (int)yr->valuedouble - 1900;
        tm.tm_mon   = (int)mo->valuedouble - 1;
        tm.tm_mday  = (int)dy->valuedouble;
        tm.tm_hour  = (int)hr->valuedouble;
        tm.tm_min   = (int)mn->valuedouble;
        tm.tm_sec   = sc ? (int)sc->valuedouble : 0;
        tm.tm_isdst = -1;
        t = mktime(&tm);
    }

    cJSON_Delete(j);

    if (t <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"time conversion failed\"}");
        return ESP_OK;
    }

    rtc_set_time_unix(t);

    struct tm t_local;
    localtime_r(&t, &t_local);
    char tstr[32];
    strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t_local);
    APP_LOGI("api", "Czas ustawiony: %s", tstr);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp,   "ok",   true);
    cJSON_AddStringToObject(resp, "time", tstr);
    cJSON_AddNumberToObject(resp, "unix", (double)t);
    char *s = cJSON_PrintUnformatted(resp);
    JSON_RESP(req, s);
    free(s);
    cJSON_Delete(resp);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// POST /api/time/sntp
// --------------------------------------------------------------------------
static esp_err_t handle_time_sntp(httpd_req_t *req)
{
    CHECK_AUTH(req);

    esp_err_t err = ntp_force_sync();

    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        JSON_RESP(req, "{\"error\":\"no WiFi connection\"}");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "504 Gateway Timeout");
        JSON_RESP(req, "{\"error\":\"NTP sync timeout\"}");
        return ESP_OK;
    }

    time_t now = time(NULL);
    struct tm t_local;
    localtime_r(&now, &t_local);
    char tstr[32];
    strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t_local);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp,   "ok",   true);
    cJSON_AddStringToObject(resp, "time", tstr);
    cJSON_AddNumberToObject(resp, "unix", (double)now);
    char *s = cJSON_PrintUnformatted(resp);
    JSON_RESP(req, s);
    free(s);
    cJSON_Delete(resp);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// GET /api/settings/export
// Zwraca pełny backup urządzenia: config (z hasłami), harmonogram, grupy.
// --------------------------------------------------------------------------
static esp_err_t handle_settings_export(httpd_req_t *req)
{
    CHECK_AUTH(req);

    cJSON *root = cJSON_CreateObject();

    // --- config ---
    cJSON *cfg = cJSON_AddObjectToObject(root, "config");
    cJSON_AddStringToObject(cfg, "wifi_ssid",   g_config.wifi_ssid);
    cJSON_AddStringToObject(cfg, "wifi_pass",   g_config.wifi_pass);
    cJSON_AddStringToObject(cfg, "mqtt_uri",    g_config.mqtt_uri);
    cJSON_AddStringToObject(cfg, "mqtt_user",   g_config.mqtt_user);
    cJSON_AddStringToObject(cfg, "mqtt_pass",   g_config.mqtt_pass);
    cJSON_AddStringToObject(cfg, "php_url",     g_config.php_url);
    cJSON_AddStringToObject(cfg, "ntp_server",  g_config.ntp_server);
    cJSON_AddStringToObject(cfg, "api_token",   g_config.api_token);
    cJSON_AddNumberToObject(cfg, "tz_offset",   g_config.timezone_offset);

    // --- schedule ---
    const schedule_entry_t *entries = schedule_get_all();
    cJSON *sched = cJSON_AddArrayToObject(root, "schedule");
    for (int i = 0; i < SCHEDULE_ENTRIES; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id",           entries[i].id);
        cJSON_AddBoolToObject(e,   "enabled",      entries[i].enabled);
        cJSON_AddNumberToObject(e, "days_mask",    entries[i].days_mask);
        cJSON_AddNumberToObject(e, "hour",         entries[i].hour);
        cJSON_AddNumberToObject(e, "minute",       entries[i].minute);
        cJSON_AddNumberToObject(e, "duration_sec", entries[i].duration_sec);
        cJSON_AddNumberToObject(e, "section_mask", entries[i].section_mask);
        cJSON_AddNumberToObject(e, "group_mask",   entries[i].group_mask);
        cJSON_AddItemToArray(sched, e);
    }

    // --- groups ---
    const irrigation_group_t *grps = groups_get_all();
    cJSON *groups = cJSON_AddArrayToObject(root, "groups");
    for (int i = 0; i < GROUPS_MAX; i++) {
        cJSON *g = cJSON_CreateObject();
        cJSON_AddNumberToObject(g, "id",           grps[i].id);
        cJSON_AddStringToObject(g, "name",         grps[i].name);
        cJSON_AddNumberToObject(g, "section_mask", grps[i].section_mask);
        cJSON_AddItemToArray(groups, g);
    }

    char *s = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"wachciodrop_backup.json\"");
    httpd_resp_send(req, s, strlen(s));
    free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// PUT /api/settings/import
// Przywraca backup: config, harmonogram, grupy.
// Hasła i token są opcjonalne — jeśli nie podane, zostają bez zmian.
// --------------------------------------------------------------------------
static esp_err_t handle_settings_import(httpd_req_t *req)
{
    CHECK_AUTH(req);

    if (req->content_len == 0 || req->content_len > 8192) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid content length\"}");
        return ESP_OK;
    }

    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        JSON_RESP(req, "{\"error\":\"out of memory\"}");
        return ESP_OK;
    }
    if (read_body(req, body, req->content_len + 1) <= 0) {
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"read error\"}");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid JSON\"}");
        return ESP_OK;
    }

    // --- config ---
    cJSON *cfg = cJSON_GetObjectItem(root, "config");
    if (cfg) {
        cJSON *v;
        #define COPY_STR(key, field) \
            if ((v = cJSON_GetObjectItem(cfg, key)) && v->valuestring) \
                strncpy(g_config.field, v->valuestring, sizeof(g_config.field) - 1)
        COPY_STR("wifi_ssid",  wifi_ssid);
        COPY_STR("wifi_pass",  wifi_pass);
        COPY_STR("mqtt_uri",   mqtt_uri);
        COPY_STR("mqtt_user",  mqtt_user);
        COPY_STR("mqtt_pass",  mqtt_pass);
        COPY_STR("php_url",    php_url);
        COPY_STR("ntp_server", ntp_server);
        COPY_STR("api_token",  api_token);
        #undef COPY_STR
        if ((v = cJSON_GetObjectItem(cfg, "tz_offset")))
            g_config.timezone_offset = (int8_t)v->valuedouble;
        storage_save_config(&g_config);
    }

    // --- schedule ---
    cJSON *sched = cJSON_GetObjectItem(root, "schedule");
    if (cJSON_IsArray(sched)) {
        cJSON *e;
        cJSON_ArrayForEach(e, sched) {
            cJSON *id_j = cJSON_GetObjectItem(e, "id");
            if (!id_j) continue;
            int id = (int)id_j->valuedouble;
            if (id < 0 || id >= SCHEDULE_ENTRIES) continue;
            schedule_entry_t entry = {0};
            entry.id = (uint8_t)id;
            cJSON *v;
            if ((v = cJSON_GetObjectItem(e, "enabled")))      entry.enabled      = cJSON_IsTrue(v);
            if ((v = cJSON_GetObjectItem(e, "days_mask")))    entry.days_mask    = (uint8_t)v->valuedouble;
            if ((v = cJSON_GetObjectItem(e, "hour")))         entry.hour         = (uint8_t)v->valuedouble;
            if ((v = cJSON_GetObjectItem(e, "minute")))       entry.minute       = (uint8_t)v->valuedouble;
            if ((v = cJSON_GetObjectItem(e, "duration_sec"))) entry.duration_sec = (uint16_t)v->valuedouble;
            if ((v = cJSON_GetObjectItem(e, "section_mask"))) entry.section_mask = (uint8_t)v->valuedouble;
            if ((v = cJSON_GetObjectItem(e, "group_mask")))   entry.group_mask   = (uint16_t)v->valuedouble;
            schedule_set(&entry);
        }
    }

    // --- groups ---
    cJSON *groups = cJSON_GetObjectItem(root, "groups");
    if (cJSON_IsArray(groups)) {
        cJSON *g;
        cJSON_ArrayForEach(g, groups) {
            cJSON *id_j = cJSON_GetObjectItem(g, "id");
            if (!id_j) continue;
            int id = (int)id_j->valuedouble;
            if (id < 1 || id > GROUPS_MAX) continue;
            irrigation_group_t group = {0};
            group.id = (uint8_t)id;
            cJSON *v;
            if ((v = cJSON_GetObjectItem(g, "name")) && v->valuestring)
                strncpy(group.name, v->valuestring, sizeof(group.name) - 1);
            if ((v = cJSON_GetObjectItem(g, "section_mask")))
                group.section_mask = (uint8_t)v->valuedouble;
            groups_set(&group);
        }
    }

    cJSON_Delete(root);
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// POST /api/ota
// --------------------------------------------------------------------------
#define OTA_BUF_SIZE 4096
static esp_err_t handle_ota(httpd_req_t *req)
{
    CHECK_AUTH(req);

    if (req->content_len == 0) {
        JSON_RESP(req, "{\"error\":\"no data\"}");
        return ESP_OK;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        JSON_RESP(req, "{\"error\":\"no OTA partition\"}");
        return ESP_OK;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES,
                                  &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed: %s", esp_err_to_name(err));
        JSON_RESP(req, "{\"error\":\"ota_begin failed\"}");
        return ESP_OK;
    }

    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        JSON_RESP(req, "{\"error\":\"out of memory\"}");
        return ESP_OK;
    }

    APP_LOGI("ota", "OTA start: %d B → %s", req->content_len, update_partition->label);

    int remaining = req->content_len;
    bool write_ok = true;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, MIN(remaining, OTA_BUF_SIZE));
        if (n <= 0) {
            ESP_LOGE(TAG, "recv error %d, remaining %d", n, remaining);
            write_ok = false;
            break;
        }
        err = esp_ota_write(ota_handle, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_write failed: %s", esp_err_to_name(err));
            write_ok = false;
            break;
        }
        remaining -= n;
    }
    free(buf);

    if (!write_ok) {
        esp_ota_abort(ota_handle);
        APP_LOGI("ota", "OTA nieudane (błąd transferu)");
        JSON_RESP(req, "{\"error\":\"transfer failed\"}");
        return ESP_OK;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_end failed: %s", esp_err_to_name(err));
        APP_LOGI("ota", "OTA nieudane (weryfikacja obrazu): %s", esp_err_to_name(err));
        JSON_RESP(req, "{\"error\":\"image invalid\"}");
        return ESP_OK;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        JSON_RESP(req, "{\"error\":\"set boot failed\"}");
        return ESP_OK;
    }

    APP_LOGI("ota", "OTA sukces — restart");
    JSON_RESP(req, "{\"ok\":true,\"msg\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// POST /api/ota/spiffs
// --------------------------------------------------------------------------
static esp_err_t handle_spiffs_ota(httpd_req_t *req)
{
    CHECK_AUTH(req);

    if (req->content_len == 0) {
        JSON_RESP(req, "{\"error\":\"no data\"}");
        return ESP_OK;
    }

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!part) {
        JSON_RESP(req, "{\"error\":\"no spiffs partition\"}");
        return ESP_OK;
    }

    if ((uint32_t)req->content_len > part->size) {
        JSON_RESP(req, "{\"error\":\"file too large\"}");
        return ESP_OK;
    }

    APP_LOGI("ota", "SPIFFS OTA start: %d B", req->content_len);

    // Odmontuj SPIFFS przed zapisem
    esp_vfs_spiffs_unregister("spiffs");

    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spiffs erase failed: %s", esp_err_to_name(err));
        JSON_RESP(req, "{\"error\":\"erase failed\"}");
        return ESP_OK;
    }

    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf) {
        JSON_RESP(req, "{\"error\":\"out of memory\"}");
        return ESP_OK;
    }

    int remaining = req->content_len;
    uint32_t offset = 0;
    bool write_ok = true;

    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, MIN(remaining, OTA_BUF_SIZE));
        if (n <= 0) {
            ESP_LOGE(TAG, "spiffs recv error %d", n);
            write_ok = false;
            break;
        }
        err = esp_partition_write(part, offset, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spiffs write failed: %s", esp_err_to_name(err));
            write_ok = false;
            break;
        }
        offset += n;
        remaining -= n;
    }
    free(buf);

    if (!write_ok) {
        APP_LOGI("ota", "SPIFFS OTA nieudane");
        JSON_RESP(req, "{\"error\":\"write failed\"}");
        return ESP_OK;
    }

    APP_LOGI("ota", "SPIFFS OTA sukces: %lu B", (unsigned long)offset);
    JSON_RESP(req, "{\"ok\":true,\"msg\":\"spiffs updated\"}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// POST /api/restart
// --------------------------------------------------------------------------
static esp_err_t handle_restart(httpd_req_t *req)
{
    CHECK_AUTH(req);
    APP_LOGI("main", "Restart zdalny — API");
    JSON_RESP(req, "{\"ok\":true,\"msg\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// --------------------------------------------------------------------------
// OPTIONS handler (CORS preflight)
// --------------------------------------------------------------------------
static esp_err_t handle_options(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin",  "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization,Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// POST /api/irrigation  — ręczna zmiana irrigation_today i ignore_php
// Body: {"irrigation_today": true/false, "ignore_php": true/false}
// --------------------------------------------------------------------------
static esp_err_t handle_irrigation_post(httpd_req_t *req)
{
    CHECK_AUTH(req);

    char body[128] = {0};
    if (req->content_len > 0) read_body(req, body, sizeof(body));

    cJSON *j = cJSON_Parse(body);
    if (!j) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid json\"}");
        return ESP_OK;
    }

    cJSON *v;
    if ((v = cJSON_GetObjectItem(j, "irrigation_today")) && cJSON_IsBool(v)) {
        g_irrigation_today       = cJSON_IsTrue(v);
        g_config.irrigation_today = g_irrigation_today;
    }
    if ((v = cJSON_GetObjectItem(j, "ignore_php")) && cJSON_IsBool(v)) {
        g_config.ignore_php = cJSON_IsTrue(v);
    }
    cJSON_Delete(j);

    storage_save_config(&g_config);
    APP_LOGI("api", "Nawadnianie: dzisiaj=%s ignoruj_skrypt=%s",
             g_irrigation_today    ? "tak" : "nie",
             g_config.ignore_php   ? "tak" : "nie");
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// GET /api/logs   DELETE /api/logs
// --------------------------------------------------------------------------
static esp_err_t handle_logs_get(httpd_req_t *req)
{
    CHECK_AUTH(req);

    // Parsuj ?offset=N&limit=N z query string
    char query[64] = {0};
    int offset = 0, limit = 100;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "offset", val, sizeof(val)) == ESP_OK)
            offset = atoi(val);
        if (httpd_query_key_value(query, "limit",  val, sizeof(val)) == ESP_OK)
            limit  = atoi(val);
    }
    if (limit > LOG_BUFFER_SIZE) limit = LOG_BUFFER_SIZE;

    log_entry_t *entries = malloc(limit * sizeof(log_entry_t));
    if (!entries) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        JSON_RESP(req, "{\"error\":\"out of memory\"}");
        return ESP_OK;
    }

    int count = log_get_entries(entries, limit, offset);
    int total = log_get_total();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total",  total);
    cJSON_AddNumberToObject(root, "offset", offset);
    cJSON_AddNumberToObject(root, "count",  count);

    cJSON *arr = cJSON_AddArrayToObject(root, "entries");
    for (int i = 0; i < count; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "ts",    entries[i].unix_ts);
        cJSON_AddNumberToObject(e, "level", (int)entries[i].level);
        cJSON_AddStringToObject(e, "tag",   entries[i].tag);
        cJSON_AddStringToObject(e, "msg",   entries[i].msg);
        cJSON_AddItemToArray(arr, e);
    }

    free(entries);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    JSON_RESP(req, s);
    free(s);
    return ESP_OK;
}

static esp_err_t handle_logs_delete(httpd_req_t *req)
{
    CHECK_AUTH(req);
    log_clear();
    APP_LOGI("api", "Log buffer cleared via API");
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// Rejestracja
// --------------------------------------------------------------------------
esp_err_t rest_api_register(httpd_handle_t server)
{
#define REG(u, m, h) do { \
    httpd_uri_t _u = { .uri = u, .method = m, .handler = h }; \
    httpd_register_uri_handler(server, &_u); \
} while(0)

    REG("/api/info",                   HTTP_GET,    handle_info);
    REG("/api/logs",                   HTTP_GET,    handle_logs_get);
    REG("/api/logs",                   HTTP_DELETE, handle_logs_delete);
    REG("/api/status",                 HTTP_GET,    handle_status);
    REG("/api/irrigation",             HTTP_POST,   handle_irrigation_post);

    REG("/api/sections",               HTTP_GET,    handle_sections_get);
    REG("/api/sections/*",             HTTP_POST,   handle_section_post);

    REG("/api/schedule",               HTTP_GET,    handle_schedule_get);
    REG("/api/schedule/*",             HTTP_PUT,    handle_schedule_put);
    REG("/api/schedule/*",             HTTP_DELETE, handle_schedule_delete);

    REG("/api/groups",                 HTTP_GET,    handle_groups_get);
    REG("/api/groups/*",               HTTP_POST,   handle_groups_post);
    REG("/api/groups/*",               HTTP_PUT,    handle_groups_put);
    REG("/api/groups/*",               HTTP_DELETE, handle_groups_delete);

    REG("/api/settings/export",         HTTP_GET,    handle_settings_export);
    REG("/api/settings/import",        HTTP_PUT,    handle_settings_import);
    REG("/api/settings",               HTTP_GET,    handle_settings_get);
    REG("/api/settings",               HTTP_POST,   handle_settings_post);

    REG("/api/time",                   HTTP_GET,    handle_time_get);
    REG("/api/time",                   HTTP_POST,   handle_time_post);
    REG("/api/time/sntp",              HTTP_POST,   handle_time_sntp);

    REG("/api/ota",                    HTTP_POST,   handle_ota);
    REG("/api/ota/spiffs",             HTTP_POST,   handle_spiffs_ota);
    REG("/api/restart",                HTTP_POST,   handle_restart);

    REG("/api/*",                      HTTP_OPTIONS, handle_options);

#undef REG

    ESP_LOGI(TAG, "REST API registered");
    return ESP_OK;
}
