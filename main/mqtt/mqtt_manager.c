#include "mqtt_manager.h"
#include "valve/valve.h"
#include "leds/leds.h"
#include "schedule/schedule.h"
#include "groups/groups.h"
#include "wifi/wifi_manager.h"
#include "storage/nvs_storage.h"
#include "rtc/rtc.h"
#include "ntp/ntp.h"
#include "config.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client    = NULL;
static bool                     s_connected = false;
static SemaphoreHandle_t        s_mutex;

extern app_config_t g_config;
extern bool         g_irrigation_today;

#define MQTT_PREFIX     "wachciodrop"
#define MQTT_HA_PREFIX  "homeassistant"
#define DEVICE_NAME     "WachcioDrop"
#define DEVICE_ID       "wachciodrop_esp32s3"

// --------------------------------------------------------------------------
// Publish helpers
// --------------------------------------------------------------------------

static void pub(const char *topic, const char *payload, int qos, int retain)
{
    if (!s_connected) return;
    esp_mqtt_client_publish(s_client, topic, payload, strlen(payload), qos, retain);
}

static void pub_json(const char *topic, cJSON *root, int qos, int retain)
{
    char *s = cJSON_PrintUnformatted(root);
    if (s) {
        pub(topic, s, qos, retain);
        free(s);
    }
}

// --------------------------------------------------------------------------
// HA Autodiscovery
// --------------------------------------------------------------------------

static void publish_ha_discovery(void)
{
    if (!s_connected) return;

    char topic[128];

    for (int i = 0; i <= SECTIONS_COUNT; i++) {
        char sec_name[32];
        if (i == 0) {
            strncpy(sec_name, "Master", sizeof(sec_name));
        } else {
            snprintf(sec_name, sizeof(sec_name), "Sekcja %d", i);
        }

        char uid[32], obj_id[32];
        if (i == 0) {
            snprintf(uid,    sizeof(uid),    "%s_master", DEVICE_ID);
            snprintf(obj_id, sizeof(obj_id), "section_master");
        } else {
            snprintf(uid,    sizeof(uid),    "%s_s%d", DEVICE_ID, i);
            snprintf(obj_id, sizeof(obj_id), "section_%d", i);
        }

        snprintf(topic, sizeof(topic),
                 MQTT_HA_PREFIX "/switch/" DEVICE_ID "/%s/config", obj_id);

        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",      sec_name);
        cJSON_AddStringToObject(d, "unique_id", uid);
        cJSON_AddStringToObject(d, "platform",  "mqtt");
        if (i == 0) {
            cJSON_AddStringToObject(d, "state_topic",
                MQTT_PREFIX "/section/master/state");
        } else {
            char st[64]; snprintf(st, sizeof(st),
                MQTT_PREFIX "/section/%d/state", i);
            cJSON_AddStringToObject(d, "state_topic", st);
            char ct[64]; snprintf(ct, sizeof(ct),
                MQTT_PREFIX "/section/%d/command", i);
            cJSON_AddStringToObject(d, "command_topic", ct);
        }
        cJSON_AddStringToObject(d, "payload_on",  "ON");
        cJSON_AddStringToObject(d, "payload_off", "OFF");
        cJSON_AddStringToObject(d, "availability_topic", MQTT_PREFIX "/status");
        cJSON_AddStringToObject(d, "payload_available",     "{\"online\":true}");
        cJSON_AddStringToObject(d, "payload_not_available", "{\"online\":false}");

        cJSON *dev = cJSON_CreateObject();
        cJSON_AddStringToObject(dev, "identifiers",  DEVICE_ID);
        cJSON_AddStringToObject(dev, "name",         DEVICE_NAME);
        cJSON_AddStringToObject(dev, "model",        "ESP32-S3 N16R8");
        cJSON_AddStringToObject(dev, "manufacturer", "Custom");
        cJSON_AddItemToObject(d, "device", dev);

        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }
    ESP_LOGI(TAG, "HA discovery published");
}

// --------------------------------------------------------------------------
// Publish section states
// --------------------------------------------------------------------------

void mqtt_publish_section_state(uint8_t section, bool active)
{
    if (!s_connected) return;
    char topic[64];
    if (section == 0) {
        snprintf(topic, sizeof(topic), MQTT_PREFIX "/section/master/state");
    } else {
        snprintf(topic, sizeof(topic), MQTT_PREFIX "/section/%d/state", section);
    }
    pub(topic, active ? "ON" : "OFF", 0, 0);
}

void mqtt_publish_all_states(void)
{
    if (!s_connected) return;
    uint8_t mask = valve_get_active_mask();
    bool master  = (bool)(leds_get() & BIT_MASTER);
    mqtt_publish_section_state(0, master);
    for (int i = 1; i <= SECTIONS_COUNT; i++) {
        mqtt_publish_section_state(i, (bool)(mask & (1 << (i-1))));
    }
}

// --------------------------------------------------------------------------
// Publish status
// --------------------------------------------------------------------------

void mqtt_publish_status(void)
{
    if (!s_connected) return;
    char ip[16] = "0.0.0.0";
    wifi_get_ip(ip, sizeof(ip));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root,   "online",           true);
    cJSON_AddStringToObject(root, "ip",               ip);
    cJSON_AddNumberToObject(root, "rssi",             wifi_get_rssi());
    cJSON_AddBoolToObject(root,   "irrigation_today", g_irrigation_today);
    cJSON_AddNumberToObject(root, "uptime_sec",
                            (double)(esp_timer_get_time() / 1000000));
    pub_json(MQTT_PREFIX "/status", root, 0, 1);
    cJSON_Delete(root);
}

// --------------------------------------------------------------------------
// Incoming message handler
// --------------------------------------------------------------------------

static void handle_command(const char *topic, const char *data, int data_len)
{
    char *payload = malloc(data_len + 1);
    if (!payload) return;
    memcpy(payload, data, data_len);
    payload[data_len] = '\0';

    // ------------------------------------------------------------------
    // Sekcje
    // ------------------------------------------------------------------

    // wachciodrop/section/all/command → OFF
    if (strstr(topic, "/section/all/command")) {
        if (strcmp(payload, "OFF") == 0) {
            valve_all_off();
            mqtt_publish_all_states();
        }
        goto done;
    }

    // wachciodrop/section/{id}/command → ON / OFF / {"on":true,"duration":300}
    {
        int sec_id = 0;
        if (sscanf(topic, MQTT_PREFIX "/section/%d/command", &sec_id) == 1) {
            if (strcmp(payload, "ON") == 0) {
                valve_section_on((uint8_t)sec_id, 0);
                mqtt_publish_section_state(sec_id, true);
            } else if (strcmp(payload, "OFF") == 0) {
                valve_section_off((uint8_t)sec_id);
                mqtt_publish_section_state(sec_id, false);
            } else {
                cJSON *j = cJSON_Parse(payload);
                if (j) {
                    cJSON *on  = cJSON_GetObjectItem(j, "on");
                    cJSON *dur = cJSON_GetObjectItem(j, "duration");
                    uint32_t duration = dur ? (uint32_t)dur->valuedouble : 0;
                    if (cJSON_IsTrue(on)) {
                        valve_section_on((uint8_t)sec_id, duration);
                        mqtt_publish_section_state(sec_id, true);
                    } else {
                        valve_section_off((uint8_t)sec_id);
                        mqtt_publish_section_state(sec_id, false);
                    }
                    cJSON_Delete(j);
                }
            }
            goto done;
        }
    }

    // ------------------------------------------------------------------
    // Grupy
    // ------------------------------------------------------------------

    // wachciodrop/group/{id}/command → {"on":true,"duration":300}
    {
        int grp_id = 0;
        if (sscanf(topic, MQTT_PREFIX "/group/%d/command", &grp_id) == 1) {
            cJSON *j = cJSON_Parse(payload);
            if (j) {
                cJSON *on  = cJSON_GetObjectItem(j, "on");
                cJSON *dur = cJSON_GetObjectItem(j, "duration");
                if (cJSON_IsTrue(on)) {
                    uint32_t duration = dur ? (uint32_t)dur->valuedouble : 0;
                    groups_activate((uint8_t)grp_id, duration);
                    mqtt_publish_all_states();
                } else {
                    valve_all_off();
                    mqtt_publish_all_states();
                }
                cJSON_Delete(j);
            }
            goto done;
        }
    }

    // wachciodrop/group/{id}/set → {"name":"Trawnik","section_mask":7}
    {
        int grp_id = 0;
        if (sscanf(topic, MQTT_PREFIX "/group/%d/set", &grp_id) == 1) {
            if (grp_id >= 1 && grp_id <= GROUPS_MAX) {
                cJSON *j = cJSON_Parse(payload);
                if (j) {
                    irrigation_group_t group = {0};
                    group.id = (uint8_t)grp_id;
                    cJSON *v;
                    if ((v = cJSON_GetObjectItem(j, "name")) && v->valuestring)
                        strncpy(group.name, v->valuestring, sizeof(group.name) - 1);
                    if ((v = cJSON_GetObjectItem(j, "section_mask")))
                        group.section_mask = (uint8_t)v->valuedouble;
                    groups_set(&group);
                    cJSON_Delete(j);

                    // Odpowiedź: zaktualizowana lista grup
                    const irrigation_group_t *grps = groups_get_all();
                    cJSON *arr = cJSON_CreateArray();
                    for (int i = 0; i < GROUPS_MAX; i++) {
                        cJSON *g = cJSON_CreateObject();
                        cJSON_AddNumberToObject(g, "id",           grps[i].id);
                        cJSON_AddStringToObject(g, "name",         grps[i].name);
                        cJSON_AddNumberToObject(g, "section_mask", grps[i].section_mask);
                        cJSON_AddItemToArray(arr, g);
                    }
                    pub_json(MQTT_PREFIX "/groups/state", arr, 0, 0);
                    cJSON_Delete(arr);
                }
            }
            goto done;
        }
    }

    // wachciodrop/groups/get → opublikuj listę grup
    if (strcmp(topic, MQTT_PREFIX "/groups/get") == 0) {
        const irrigation_group_t *grps = groups_get_all();
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < GROUPS_MAX; i++) {
            cJSON *g = cJSON_CreateObject();
            cJSON_AddNumberToObject(g, "id",           grps[i].id);
            cJSON_AddStringToObject(g, "name",         grps[i].name);
            cJSON_AddNumberToObject(g, "section_mask", grps[i].section_mask);
            cJSON_AddItemToArray(arr, g);
        }
        pub_json(MQTT_PREFIX "/groups/state", arr, 0, 0);
        cJSON_Delete(arr);
        goto done;
    }

    // ------------------------------------------------------------------
    // Harmonogram
    // ------------------------------------------------------------------

    // wachciodrop/schedule/get → opublikuj harmonogram
    if (strcmp(topic, MQTT_PREFIX "/schedule/get") == 0) {
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
        pub_json(MQTT_PREFIX "/schedule/state", arr, 0, 0);
        cJSON_Delete(arr);
        goto done;
    }

    // wachciodrop/schedule/set → JSON array (bulk)
    if (strcmp(topic, MQTT_PREFIX "/schedule/set") == 0) {
        cJSON *arr = cJSON_Parse(payload);
        if (arr && cJSON_IsArray(arr)) {
            cJSON *item;
            cJSON_ArrayForEach(item, arr) {
                schedule_entry_t e = {0};
                cJSON *v;
                if ((v = cJSON_GetObjectItem(item, "id")))           e.id           = (uint8_t)v->valuedouble;
                if ((v = cJSON_GetObjectItem(item, "enabled")))      e.enabled      = cJSON_IsTrue(v);
                if ((v = cJSON_GetObjectItem(item, "days_mask")))    e.days_mask    = (uint8_t)v->valuedouble;
                if ((v = cJSON_GetObjectItem(item, "hour")))         e.hour         = (uint8_t)v->valuedouble;
                if ((v = cJSON_GetObjectItem(item, "minute")))       e.minute       = (uint8_t)v->valuedouble;
                if ((v = cJSON_GetObjectItem(item, "duration_sec"))) e.duration_sec = (uint16_t)v->valuedouble;
                if ((v = cJSON_GetObjectItem(item, "section_mask"))) e.section_mask = (uint8_t)v->valuedouble;
                if ((v = cJSON_GetObjectItem(item, "group_mask")))   e.group_mask   = (uint16_t)v->valuedouble;
                if (e.id < SCHEDULE_ENTRIES) schedule_set(&e);
            }
            cJSON_Delete(arr);
        }
        goto done;
    }

    // wachciodrop/schedule/{id}/set → pojedynczy wpis
    {
        int sched_id = -1;
        if (sscanf(topic, MQTT_PREFIX "/schedule/%d/set", &sched_id) == 1) {
            if (sched_id >= 0 && sched_id < SCHEDULE_ENTRIES) {
                cJSON *j = cJSON_Parse(payload);
                if (j) {
                    schedule_entry_t entry = {0};
                    entry.id = (uint8_t)sched_id;
                    cJSON *v;
                    if ((v = cJSON_GetObjectItem(j, "enabled")))      entry.enabled      = cJSON_IsTrue(v);
                    if ((v = cJSON_GetObjectItem(j, "days_mask")))    entry.days_mask    = (uint8_t)v->valuedouble;
                    if ((v = cJSON_GetObjectItem(j, "hour")))         entry.hour         = (uint8_t)v->valuedouble;
                    if ((v = cJSON_GetObjectItem(j, "minute")))       entry.minute       = (uint8_t)v->valuedouble;
                    if ((v = cJSON_GetObjectItem(j, "duration_sec"))) entry.duration_sec = (uint16_t)v->valuedouble;
                    if ((v = cJSON_GetObjectItem(j, "section_mask"))) entry.section_mask = (uint8_t)v->valuedouble;
                    if ((v = cJSON_GetObjectItem(j, "group_mask")))   entry.group_mask   = (uint16_t)v->valuedouble;
                    schedule_set(&entry);
                    cJSON_Delete(j);
                    pub(MQTT_PREFIX "/schedule/set/result", "{\"ok\":true}", 0, 0);
                }
            }
            goto done;
        }
    }

    // wachciodrop/schedule/{id}/delete
    {
        int sched_id = -1;
        if (sscanf(topic, MQTT_PREFIX "/schedule/%d/delete", &sched_id) == 1) {
            if (sched_id >= 0 && sched_id < SCHEDULE_ENTRIES) {
                schedule_delete((uint8_t)sched_id);
                pub(MQTT_PREFIX "/schedule/delete/result", "{\"ok\":true}", 0, 0);
            }
            goto done;
        }
    }

    // ------------------------------------------------------------------
    // Czas
    // ------------------------------------------------------------------

    // wachciodrop/time/get → opublikuj czas
    if (strcmp(topic, MQTT_PREFIX "/time/get") == 0) {
        time_t now = time(NULL);
        struct tm t_local;
        localtime_r(&now, &t_local);
        char tstr[32];
        strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t_local);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "time",      tstr);
        cJSON_AddNumberToObject(root, "unix",      (double)now);
        cJSON_AddNumberToObject(root, "tz_offset", g_config.timezone_offset);
        pub_json(MQTT_PREFIX "/time/state", root, 0, 0);
        cJSON_Delete(root);
        goto done;
    }

    // wachciodrop/time/set → {"unix":...} / {"datetime":"..."} / {year,month,...}
    if (strcmp(topic, MQTT_PREFIX "/time/set") == 0) {
        cJSON *j = cJSON_Parse(payload);
        if (j) {
            time_t t = 0;
            cJSON *v;
            if ((v = cJSON_GetObjectItem(j, "unix")) != NULL) {
                t = (time_t)(long long)v->valuedouble;
            } else if ((v = cJSON_GetObjectItem(j, "datetime")) != NULL
                       && v->valuestring) {
                struct tm tm = {0};
                if (strptime(v->valuestring, "%Y-%m-%dT%H:%M:%S", &tm)) {
                    tm.tm_isdst = -1;
                    t = mktime(&tm);
                }
            } else {
                cJSON *yr = cJSON_GetObjectItem(j, "year");
                cJSON *mo = cJSON_GetObjectItem(j, "month");
                cJSON *dy = cJSON_GetObjectItem(j, "day");
                cJSON *hr = cJSON_GetObjectItem(j, "hour");
                cJSON *mn = cJSON_GetObjectItem(j, "minute");
                cJSON *sc = cJSON_GetObjectItem(j, "second");
                if (yr && mo && dy && hr && mn) {
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
            }
            cJSON_Delete(j);
            if (t > 0) {
                rtc_set_time_unix(t);
                // Odpowiedź z potwierdzeniem
                struct tm t_local;
                localtime_r(&t, &t_local);
                char tstr[32];
                strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t_local);
                cJSON *resp = cJSON_CreateObject();
                cJSON_AddBoolToObject(resp,   "ok",   true);
                cJSON_AddStringToObject(resp, "time", tstr);
                cJSON_AddNumberToObject(resp, "unix", (double)t);
                pub_json(MQTT_PREFIX "/time/state", resp, 0, 0);
                cJSON_Delete(resp);
            }
        }
        goto done;
    }

    // wachciodrop/time/sntp → wymuś synchronizację NTP
    if (strcmp(topic, MQTT_PREFIX "/time/sntp") == 0) {
        esp_err_t err = ntp_force_sync();
        cJSON *resp = cJSON_CreateObject();
        if (err == ESP_OK) {
            time_t now = time(NULL);
            struct tm t_local;
            localtime_r(&now, &t_local);
            char tstr[32];
            strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%S", &t_local);
            cJSON_AddBoolToObject(resp,   "ok",   true);
            cJSON_AddStringToObject(resp, "time", tstr);
            cJSON_AddNumberToObject(resp, "unix", (double)now);
        } else {
            cJSON_AddBoolToObject(resp,   "ok",    false);
            cJSON_AddStringToObject(resp, "error",
                err == ESP_ERR_INVALID_STATE ? "no WiFi" : "NTP timeout");
        }
        pub_json(MQTT_PREFIX "/time/state", resp, 0, 0);
        cJSON_Delete(resp);
        goto done;
    }

    // ------------------------------------------------------------------
    // Ustawienia
    // ------------------------------------------------------------------

    // wachciodrop/settings/get → opublikuj ustawienia (bez haseł)
    if (strcmp(topic, MQTT_PREFIX "/settings/get") == 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "wifi_ssid",  g_config.wifi_ssid);
        cJSON_AddStringToObject(root, "mqtt_uri",   g_config.mqtt_uri);
        cJSON_AddStringToObject(root, "mqtt_user",  g_config.mqtt_user);
        cJSON_AddStringToObject(root, "php_url",    g_config.php_url);
        cJSON_AddStringToObject(root, "ntp_server", g_config.ntp_server);
        cJSON_AddNumberToObject(root, "tz_offset",  g_config.timezone_offset);
        pub_json(MQTT_PREFIX "/settings/state", root, 0, 0);
        cJSON_Delete(root);
        goto done;
    }

    // wachciodrop/settings/set → zaktualizuj ustawienia (pola opcjonalne)
    if (strcmp(topic, MQTT_PREFIX "/settings/set") == 0) {
        cJSON *j = cJSON_Parse(payload);
        if (j) {
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
            pub(MQTT_PREFIX "/settings/set/result", "{\"ok\":true}", 0, 0);
        }
        goto done;
    }

done:
    free(payload);
}

// --------------------------------------------------------------------------
// MQTT event handler
// --------------------------------------------------------------------------

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data)
{
    esp_mqtt_event_handle_t evt = (esp_mqtt_event_handle_t)data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        s_connected = true;

        // Sekcje
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/section/+/command", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/section/all/command", 0);

        // Grupy
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/group/+/command", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/group/+/set", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/groups/get", 0);

        // Harmonogram
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/schedule/get", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/schedule/set", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/schedule/+/set", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/schedule/+/delete", 0);

        // Czas
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/time/get", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/time/set", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/time/sntp", 0);

        // Ustawienia
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/settings/get", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/settings/set", 0);

        publish_ha_discovery();
        mqtt_publish_all_states();
        mqtt_publish_status();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        break;

    case MQTT_EVENT_DATA:
        if (evt->topic_len > 0) {
            char topic[128] = {0};
            int tlen = evt->topic_len < (int)sizeof(topic) - 1
                       ? evt->topic_len : (int)sizeof(topic) - 1;
            strncpy(topic, evt->topic, tlen);
            const char *data = (evt->data && evt->data_len > 0) ? evt->data : "";
            handle_command(topic, data, evt->data_len > 0 ? evt->data_len : 0);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "error");
        break;
    default:
        break;
    }
}

// --------------------------------------------------------------------------
// Init & task
// --------------------------------------------------------------------------

static void on_valve_state_changed(void)
{
    mqtt_publish_all_states();
}

esp_err_t mqtt_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    valve_set_state_callback(on_valve_state_changed);
    ESP_LOGI(TAG, "init OK");
    return ESP_OK;
}

void mqtt_manager_task(void *arg)
{
    ESP_LOGI(TAG, "task started");

    while (wifi_get_state() != WIFI_STATE_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (g_config.mqtt_uri[0] == '\0') {
        ESP_LOGI(TAG, "no MQTT URI configured, task exit");
        vTaskDelete(NULL);
        return;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri      = g_config.mqtt_uri,
        .credentials.username    = g_config.mqtt_user,
        .credentials.authentication.password = g_config.mqtt_pass,
        .session.keepalive       = 60,
        .session.last_will.topic = MQTT_PREFIX "/status",
        .session.last_will.msg   = "{\"online\":false}",
        .session.last_will.qos   = 1,
        .session.last_will.retain = 1,
        .buffer.size             = 4096,
        .buffer.out_size         = 4096,
    };

    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        mqtt_publish_status();
    }
}
