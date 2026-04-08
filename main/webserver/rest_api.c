#include "rest_api.h"
#include "file_server.h"
#include "valve/valve.h"
#include "leds/leds.h"
#include "config.h"
#include "esp_timer.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include "schedule/schedule.h"
#include "groups/groups.h"
#include "storage/nvs_storage.h"
#include "rtc/rtc.h"
#include "wifi/wifi_manager.h"
#include "config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
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
static esp_err_t handle_status(httpd_req_t *req)
{
    CHECK_AUTH(req);

    char ip[16] = "0.0.0.0";
    wifi_get_ip(ip, sizeof(ip));

    uint8_t mask = valve_get_active_mask();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime_sec",
                            (double)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddNumberToObject(root, "rssi",    wifi_get_rssi());
    cJSON_AddNumberToObject(root, "sections_active", __builtin_popcount(mask));
    cJSON_AddBoolToObject(root,   "master_active",
                          (bool)(leds_get() & BIT_MASTER));
    cJSON_AddBoolToObject(root,   "irrigation_today", g_irrigation_today);

    struct tm t = {0};
    rtc_get_time(&t);
    char tstr[32];
    strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t);
    cJSON_AddStringToObject(root, "time", tstr);

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
// POST /api/sections/{id}/on   POST /api/sections/{id}/off
// POST /api/sections/all/off
// --------------------------------------------------------------------------
static esp_err_t handle_section_on(httpd_req_t *req)
{
    CHECK_AUTH(req);

    // Wyciągnij id z URI: /api/sections/{id}/on
    const char *uri = req->uri;
    int section_id = 0;
    sscanf(uri, "/api/sections/%d/on", &section_id);
    if (section_id < 1 || section_id > SECTIONS_COUNT) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid section\"}");
        return ESP_OK;
    }

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
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t handle_section_off(httpd_req_t *req)
{
    CHECK_AUTH(req);

    const char *uri = req->uri;

    // Sprawdź /api/sections/all/off
    if (strstr(uri, "/all/off")) {
        valve_all_off();
        JSON_RESP(req, "{\"ok\":true}");
        return ESP_OK;
    }

    int section_id = 0;
    sscanf(uri, "/api/sections/%d/off", &section_id);
    if (section_id < 1 || section_id > SECTIONS_COUNT) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid section\"}");
        return ESP_OK;
    }
    valve_section_off((uint8_t)section_id);
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
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t handle_group_activate(httpd_req_t *req)
{
    CHECK_AUTH(req);

    int id = 0;
    sscanf(req->uri, "/api/groups/%d/activate", &id);
    if (id < 1 || id > GROUPS_MAX) {
        httpd_resp_set_status(req, "400 Bad Request");
        JSON_RESP(req, "{\"error\":\"invalid id\"}");
        return ESP_OK;
    }

    uint32_t duration = 0;
    char body[64] = {0};
    if (req->content_len > 0 && read_body(req, body, sizeof(body)) > 0) {
        cJSON *j = cJSON_Parse(body);
        if (j) {
            cJSON *d = cJSON_GetObjectItem(j, "duration");
            if (d) duration = (uint32_t)d->valuedouble;
            cJSON_Delete(j);
        }
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
    cJSON_AddStringToObject(root, "ntp_server",  g_config.ntp_server);
    cJSON_AddNumberToObject(root, "tz_offset",   g_config.timezone_offset);
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
    #define COPY_STR(key, field) \
        if ((v = cJSON_GetObjectItem(j, key)) && v->valuestring) \
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

    if ((v = cJSON_GetObjectItem(j, "tz_offset")))
        g_config.timezone_offset = (int8_t)v->valuedouble;

    cJSON_Delete(j);
    storage_save_config(&g_config);
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// GET/POST /api/time
// --------------------------------------------------------------------------
static esp_err_t handle_time_get(httpd_req_t *req)
{
    CHECK_AUTH(req);

    struct tm t = {0};
    rtc_get_time(&t);
    char tstr[32];
    strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "time",  tstr);
    cJSON_AddNumberToObject(root, "unix",  (double)mktime(&t));
    cJSON_AddNumberToObject(root, "tz",    g_config.timezone_offset);
    char *s = cJSON_PrintUnformatted(root);
    JSON_RESP(req, s);
    free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_time_post(httpd_req_t *req)
{
    CHECK_AUTH(req);

    char body[64] = {0};
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
    cJSON *v = cJSON_GetObjectItem(j, "unix");
    if (v) {
        rtc_set_time_unix((time_t)(long long)v->valuedouble);
    }
    cJSON_Delete(j);
    JSON_RESP(req, "{\"ok\":true}");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// POST /api/ota
// --------------------------------------------------------------------------
static esp_err_t handle_ota(httpd_req_t *req)
{
    CHECK_AUTH(req);

    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        JSON_RESP(req, "{\"error\":\"no OTA partition\"}");
        return ESP_OK;
    }

    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES,
                                   &ota_handle);
    if (err != ESP_OK) {
        JSON_RESP(req, "{\"error\":\"ota_begin failed\"}");
        return ESP_OK;
    }

    char buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf,
                               MIN(remaining, (int)sizeof(buf)));
        if (n <= 0) { esp_ota_abort(ota_handle); break; }
        esp_ota_write(ota_handle, buf, n);
        remaining -= n;
    }

    if (remaining == 0 && esp_ota_end(ota_handle) == ESP_OK) {
        esp_ota_set_boot_partition(update_partition);
        JSON_RESP(req, "{\"ok\":true,\"msg\":\"rebooting\"}");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        JSON_RESP(req, "{\"error\":\"ota failed\"}");
    }
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
// Rejestracja
// --------------------------------------------------------------------------
esp_err_t rest_api_register(httpd_handle_t server)
{
#define REG(u, m, h) do { \
    httpd_uri_t _u = { .uri = u, .method = m, .handler = h }; \
    httpd_register_uri_handler(server, &_u); \
} while(0)

    REG("/api/status",                 HTTP_GET,    handle_status);

    REG("/api/sections",               HTTP_GET,    handle_sections_get);
    REG("/api/sections/*/on",          HTTP_POST,   handle_section_on);
    REG("/api/sections/*/off",         HTTP_POST,   handle_section_off);

    REG("/api/schedule",               HTTP_GET,    handle_schedule_get);
    REG("/api/schedule/*",             HTTP_PUT,    handle_schedule_put);
    REG("/api/schedule/*",             HTTP_DELETE, handle_schedule_delete);

    REG("/api/groups",                 HTTP_GET,    handle_groups_get);
    REG("/api/groups/*/activate",      HTTP_POST,   handle_group_activate);
    REG("/api/groups/*",               HTTP_PUT,    handle_groups_put);

    REG("/api/settings",               HTTP_GET,    handle_settings_get);
    REG("/api/settings",               HTTP_POST,   handle_settings_post);

    REG("/api/time",                   HTTP_GET,    handle_time_get);
    REG("/api/time",                   HTTP_POST,   handle_time_post);

    REG("/api/ota",                    HTTP_POST,   handle_ota);

    REG("/api/*",                      HTTP_OPTIONS, handle_options);

#undef REG

    ESP_LOGI(TAG, "REST API registered");
    return ESP_OK;
}
