#include "mqtt_manager.h"
#include "valve/valve.h"
#include "leds/leds.h"
#include "schedule/schedule.h"
#include "groups/groups.h"
#include "wifi/wifi_manager.h"
#include "storage/nvs_storage.h"
#include "rtc/rtc.h"
#include "ntp/ntp.h"
#include "temperature/temperature.h"
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

#define MQTT_PREFIX      "wachciodrop"
#define MQTT_HA_PREFIX   "homeassistant"
#define MQTT_AVAIL_TOPIC  MQTT_PREFIX "/availability"
#define DEVICE_NAME      "WachcioDrop"
#define DEVICE_ID        "wachciodrop_esp32s3"

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
// HA discovery helpers
// --------------------------------------------------------------------------

static cJSON *make_device_json(void)
{
    cJSON *dev = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    cJSON_AddItemToArray(ids, cJSON_CreateString(DEVICE_ID));
    cJSON_AddItemToObject(dev, "identifiers",  ids);
    cJSON_AddStringToObject(dev, "name",         DEVICE_NAME);
    cJSON_AddStringToObject(dev, "model",        "ESP32-S3 N16R8");
    cJSON_AddStringToObject(dev, "manufacturer", "Custom");
    cJSON_AddStringToObject(dev, "sw_version",   FW_VERSION);
    return dev;
}

static void add_avail(cJSON *d)
{
    cJSON_AddStringToObject(d, "availability_topic",    MQTT_AVAIL_TOPIC);
    cJSON_AddStringToObject(d, "payload_available",     "online");
    cJSON_AddStringToObject(d, "payload_not_available", "offline");
}

// --------------------------------------------------------------------------
// HA Autodiscovery
// --------------------------------------------------------------------------

static void publish_ha_discovery(void)
{
    if (!s_connected) return;

    char topic[160];

    // ── Sekcje + master ────────────────────────────────────────────────────
    for (int i = 0; i <= SECTIONS_COUNT; i++) {
        char name[32], uid[48], obj[32];
        if (i == 0) {
            snprintf(name, sizeof(name), "Master");
            snprintf(uid,  sizeof(uid),  "%s_master",    DEVICE_ID);
            snprintf(obj,  sizeof(obj),  "section_master");
        } else {
            snprintf(name, sizeof(name), "Sekcja %d", i);
            snprintf(uid,  sizeof(uid),  "%s_s%d", DEVICE_ID, i);
            snprintf(obj,  sizeof(obj),  "section_%d", i);
        }

        snprintf(topic, sizeof(topic),
                 MQTT_HA_PREFIX "/switch/" DEVICE_ID "/%s/config", obj);

        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",      name);
        cJSON_AddStringToObject(d, "unique_id", uid);

        // state topic
        char st[64];
        if (i == 0)
            snprintf(st, sizeof(st), MQTT_PREFIX "/section/master/state");
        else
            snprintf(st, sizeof(st), MQTT_PREFIX "/section/%d/state", i);
        cJSON_AddStringToObject(d, "state_topic", st);

        // command topic (master jest read-only)
        if (i != 0) {
            char ct[64];
            snprintf(ct, sizeof(ct), MQTT_PREFIX "/section/%d/command", i);
            cJSON_AddStringToObject(d, "command_topic", ct);
        }

        cJSON_AddStringToObject(d, "payload_on",  "ON");
        cJSON_AddStringToObject(d, "payload_off", "OFF");
        cJSON_AddStringToObject(d, "icon",        "mdi:water");
        add_avail(d);
        cJSON_AddItemToObject(d, "device", make_device_json());

        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }

    // ── Grupy ──────────────────────────────────────────────────────────────
    const irrigation_group_t *grps = groups_get_all();
    for (int i = 0; i < GROUPS_MAX; i++) {
        if (grps[i].section_mask == 0) continue;   // pomiń niezdefiniowane

        char uid[48], obj[32];
        snprintf(uid, sizeof(uid), "%s_group_%d", DEVICE_ID, grps[i].id);
        snprintf(obj, sizeof(obj), "group_%d", grps[i].id);

        snprintf(topic, sizeof(topic),
                 MQTT_HA_PREFIX "/switch/" DEVICE_ID "/%s/config", obj);

        char st[64], ct[64];
        snprintf(st, sizeof(st), MQTT_PREFIX "/group/%d/state",   grps[i].id);
        snprintf(ct, sizeof(ct), MQTT_PREFIX "/group/%d/command", grps[i].id);

        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",          grps[i].name);
        cJSON_AddStringToObject(d, "unique_id",     uid);
        cJSON_AddStringToObject(d, "state_topic",   st);
        cJSON_AddStringToObject(d, "command_topic", ct);
        cJSON_AddStringToObject(d, "payload_on",    "ON");
        cJSON_AddStringToObject(d, "payload_off",   "OFF");
        cJSON_AddStringToObject(d, "icon",          "mdi:sprinkler-variant");
        add_avail(d);
        cJSON_AddItemToObject(d, "device", make_device_json());

        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }

    // ── Nawadnianie dziś ───────────────────────────────────────────────────
    snprintf(topic, sizeof(topic),
             MQTT_HA_PREFIX "/switch/" DEVICE_ID "/irrigation_today/config");

    {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",          "Nawadnianie dziś");
        cJSON_AddStringToObject(d, "unique_id",     DEVICE_ID "_irrigation_today");
        cJSON_AddStringToObject(d, "state_topic",   MQTT_PREFIX "/irrigation/today/state");
        cJSON_AddStringToObject(d, "command_topic", MQTT_PREFIX "/irrigation/today/command");
        cJSON_AddStringToObject(d, "payload_on",    "ON");
        cJSON_AddStringToObject(d, "payload_off",   "OFF");
        cJSON_AddStringToObject(d, "icon",          "mdi:calendar-check");
        add_avail(d);
        cJSON_AddItemToObject(d, "device", make_device_json());
        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }

    // ── Ochrona przed mrozem — przełącznik ────────────────────────────────
    snprintf(topic, sizeof(topic),
             MQTT_HA_PREFIX "/switch/" DEVICE_ID "/frost_protection/config");

    {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",          "Ochrona przed mrozem");
        cJSON_AddStringToObject(d, "unique_id",     DEVICE_ID "_frost_protection");
        cJSON_AddStringToObject(d, "state_topic",   MQTT_PREFIX "/frost/protection/state");
        cJSON_AddStringToObject(d, "command_topic", MQTT_PREFIX "/frost/protection/command");
        cJSON_AddStringToObject(d, "payload_on",    "ON");
        cJSON_AddStringToObject(d, "payload_off",   "OFF");
        cJSON_AddStringToObject(d, "icon",          "mdi:snowflake-alert");
        add_avail(d);
        cJSON_AddItemToObject(d, "device", make_device_json());
        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }

    // ── Mróz aktywny — binary sensor ──────────────────────────────────────
    snprintf(topic, sizeof(topic),
             MQTT_HA_PREFIX "/binary_sensor/" DEVICE_ID "/frost_active/config");

    {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",          "Mróz aktywny");
        cJSON_AddStringToObject(d, "unique_id",     DEVICE_ID "_frost_active");
        cJSON_AddStringToObject(d, "state_topic",   MQTT_PREFIX "/frost/active/state");
        cJSON_AddStringToObject(d, "payload_on",    "ON");
        cJSON_AddStringToObject(d, "payload_off",   "OFF");
        cJSON_AddStringToObject(d, "device_class",  "cold");
        cJSON_AddStringToObject(d, "icon",          "mdi:snowflake");
        add_avail(d);
        cJSON_AddItemToObject(d, "device", make_device_json());
        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }

    // ── Temperatura DS18B20 ────────────────────────────────────────────────
    snprintf(topic, sizeof(topic),
             MQTT_HA_PREFIX "/sensor/" DEVICE_ID "/temperature/config");

    {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",                "Temperatura DS18B20");
        cJSON_AddStringToObject(d, "unique_id",           DEVICE_ID "_temperature");
        cJSON_AddStringToObject(d, "state_topic",         MQTT_PREFIX "/sensor/temperature/state");
        cJSON_AddStringToObject(d, "unit_of_measurement", "°C");
        cJSON_AddStringToObject(d, "device_class",        "temperature");
        cJSON_AddStringToObject(d, "state_class",         "measurement");
        cJSON_AddStringToObject(d, "icon",                "mdi:thermometer");
        add_avail(d);
        cJSON_AddItemToObject(d, "device", make_device_json());
        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }

    // ── WiFi RSSI ──────────────────────────────────────────────────────────
    snprintf(topic, sizeof(topic),
             MQTT_HA_PREFIX "/sensor/" DEVICE_ID "/rssi/config");

    {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",                "WiFi RSSI");
        cJSON_AddStringToObject(d, "unique_id",           DEVICE_ID "_rssi");
        cJSON_AddStringToObject(d, "state_topic",         MQTT_PREFIX "/sensor/rssi/state");
        cJSON_AddStringToObject(d, "unit_of_measurement", "dBm");
        cJSON_AddStringToObject(d, "device_class",        "signal_strength");
        cJSON_AddStringToObject(d, "state_class",         "measurement");
        cJSON_AddStringToObject(d, "entity_category",     "diagnostic");
        cJSON_AddStringToObject(d, "icon",                "mdi:wifi");
        add_avail(d);
        cJSON_AddItemToObject(d, "device", make_device_json());
        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }

    // ── Uptime ─────────────────────────────────────────────────────────────
    snprintf(topic, sizeof(topic),
             MQTT_HA_PREFIX "/sensor/" DEVICE_ID "/uptime/config");

    {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",                "Uptime");
        cJSON_AddStringToObject(d, "unique_id",           DEVICE_ID "_uptime");
        cJSON_AddStringToObject(d, "state_topic",         MQTT_PREFIX "/sensor/uptime/state");
        cJSON_AddStringToObject(d, "unit_of_measurement", "s");
        cJSON_AddStringToObject(d, "device_class",        "duration");
        cJSON_AddStringToObject(d, "state_class",         "total_increasing");
        cJSON_AddStringToObject(d, "entity_category",     "diagnostic");
        cJSON_AddStringToObject(d, "icon",                "mdi:timer-outline");
        add_avail(d);
        cJSON_AddItemToObject(d, "device", make_device_json());
        pub_json(topic, d, 1, 1);
        cJSON_Delete(d);
    }

    ESP_LOGI(TAG, "HA discovery published");
}

// --------------------------------------------------------------------------
// Forward declarations
// --------------------------------------------------------------------------

static void mqtt_publish_group_states(void);

// --------------------------------------------------------------------------
// Publish section states
// --------------------------------------------------------------------------

void mqtt_publish_section_state(uint8_t section, bool active)
{
    if (!s_connected) return;
    char topic[64];
    if (section == 0)
        snprintf(topic, sizeof(topic), MQTT_PREFIX "/section/master/state");
    else
        snprintf(topic, sizeof(topic), MQTT_PREFIX "/section/%d/state", section);
    pub(topic, active ? "ON" : "OFF", 0, 1);  // retain=1
}

void mqtt_publish_all_states(void)
{
    if (!s_connected) return;
    uint8_t mask = valve_get_active_mask();
    bool master  = (bool)(leds_get() & BIT_MASTER);
    mqtt_publish_section_state(0, master);
    for (int i = 1; i <= SECTIONS_COUNT; i++)
        mqtt_publish_section_state(i, (bool)(mask & (1 << (i - 1))));
    mqtt_publish_group_states();
}

static void mqtt_publish_group_states(void)
{
    if (!s_connected) return;
    uint8_t active_id = groups_get_active_id();
    const irrigation_group_t *grps = groups_get_all();
    for (int i = 0; i < GROUPS_MAX; i++) {
        if (grps[i].section_mask == 0) continue;
        char topic[64];
        snprintf(topic, sizeof(topic), MQTT_PREFIX "/group/%d/state", grps[i].id);
        pub(topic, (grps[i].id == active_id) ? "ON" : "OFF", 0, 1);
    }
}

// --------------------------------------------------------------------------
// Publish sensor values
// --------------------------------------------------------------------------

void mqtt_publish_temperature(float temp, bool available)
{
    if (!s_connected) return;
    char buf[32];
    if (available)
        snprintf(buf, sizeof(buf), "%.1f", temp);
    else
        snprintf(buf, sizeof(buf), "unavailable");
    pub(MQTT_PREFIX "/sensor/temperature/state", buf, 0, 1);
}

void mqtt_publish_irrigation_today(bool active)
{
    if (!s_connected) return;
    pub(MQTT_PREFIX "/irrigation/today/state", active ? "ON" : "OFF", 0, 1);
}

void mqtt_publish_frost_active(bool active)
{
    if (!s_connected) return;
    pub(MQTT_PREFIX "/frost/active/state", active ? "ON" : "OFF", 0, 1);
}

// --------------------------------------------------------------------------
// Publish status (co 60s + po połączeniu)
// --------------------------------------------------------------------------

void mqtt_publish_status(void)
{
    if (!s_connected) return;

    // Dostępność
    pub(MQTT_AVAIL_TOPIC, "online", 1, 1);

    // Sensory diagnostyczne
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", wifi_get_rssi());
        pub(MQTT_PREFIX "/sensor/rssi/state", buf, 0, 1);
    }
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lld",
                 (long long)(esp_timer_get_time() / 1000000));
        pub(MQTT_PREFIX "/sensor/uptime/state", buf, 0, 1);
    }

    // Nawadnianie dziś
    mqtt_publish_irrigation_today(g_irrigation_today);

    // Ochrona przed mrozem — stan przełącznika
    pub(MQTT_PREFIX "/frost/protection/state",
        g_config.frost_protection_enabled ? "ON" : "OFF", 0, 1);

    // Temperatura (jeśli dostępna)
    if (temperature_available())
        mqtt_publish_temperature(temperature_get(), true);
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

    // ── Wszystkie sekcje OFF ───────────────────────────────────────────────
    if (strstr(topic, "/section/all/command")) {
        if (strcmp(payload, "OFF") == 0) {
            valve_all_off();
            mqtt_publish_all_states();
        }
        goto done;
    }

    // ── Sekcja {id}/command → ON / OFF / {"on":true,"duration":300} ───────
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

    // ── Grupa {id}/command → ON / OFF / {"on":true,"duration":300} ────────
    {
        int grp_id = 0;
        if (sscanf(topic, MQTT_PREFIX "/group/%d/command", &grp_id) == 1) {
            bool turn_on   = false;
            uint32_t dur   = 0;

            if (strcmp(payload, "ON") == 0) {
                turn_on = true;
                dur     = 0;   // bezterminowo
            } else if (strcmp(payload, "OFF") == 0) {
                turn_on = false;
            } else {
                cJSON *j = cJSON_Parse(payload);
                if (j) {
                    cJSON *on  = cJSON_GetObjectItem(j, "on");
                    cJSON *d   = cJSON_GetObjectItem(j, "duration");
                    turn_on = cJSON_IsTrue(on);
                    dur     = d ? (uint32_t)d->valuedouble : 0;
                    cJSON_Delete(j);
                }
            }

            if (turn_on) {
                groups_activate((uint8_t)grp_id, dur);
            } else {
                valve_all_off();
            }
            mqtt_publish_all_states();
            goto done;
        }
    }

    // ── Nawadnianie dziś ───────────────────────────────────────────────────
    if (strcmp(topic, MQTT_PREFIX "/irrigation/today/command") == 0) {
        bool active = (strcmp(payload, "ON") == 0);
        g_irrigation_today         = active;
        g_config.irrigation_today  = active;
        storage_save_config(&g_config);
        mqtt_publish_irrigation_today(active);
        goto done;
    }

    // ── Ochrona przed mrozem — włącz/wyłącz ──────────────────────────────
    if (strcmp(topic, MQTT_PREFIX "/frost/protection/command") == 0) {
        bool en = (strcmp(payload, "ON") == 0);
        g_config.frost_protection_enabled = en;
        storage_save_config(&g_config);
        pub(MQTT_PREFIX "/frost/protection/state", en ? "ON" : "OFF", 0, 1);
        goto done;
    }

    // ── Grupy — konfiguracja ───────────────────────────────────────────────
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

    // ── Grupy — lista ─────────────────────────────────────────────────────
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

    // ── Harmonogram — lista ───────────────────────────────────────────────
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

    // ── Harmonogram — bulk set ────────────────────────────────────────────
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

    // ── Harmonogram — single set ──────────────────────────────────────────
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

    // ── Harmonogram — delete ──────────────────────────────────────────────
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

    // ── Czas — get ────────────────────────────────────────────────────────
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

    // ── Czas — set ────────────────────────────────────────────────────────
    if (strcmp(topic, MQTT_PREFIX "/time/set") == 0) {
        cJSON *j = cJSON_Parse(payload);
        if (j) {
            time_t t = 0;
            cJSON *v;
            if ((v = cJSON_GetObjectItem(j, "unix")) != NULL) {
                t = (time_t)(long long)v->valuedouble;
            } else if ((v = cJSON_GetObjectItem(j, "datetime")) != NULL && v->valuestring) {
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

    // ── Czas — NTP sync ───────────────────────────────────────────────────
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

    // ── Ustawienia — get ──────────────────────────────────────────────────
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

    // ── Ustawienia — set ──────────────────────────────────────────────────
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

        // Nawadnianie dziś + ochrona przed mrozem
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/irrigation/today/command", 0);
        esp_mqtt_client_subscribe(s_client, MQTT_PREFIX "/frost/protection/command", 0);

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

        // Publikuj wszystko
        pub(MQTT_AVAIL_TOPIC, "online", 1, 1);
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
            const char *d = (evt->data && evt->data_len > 0) ? evt->data : "";
            handle_command(topic, d, evt->data_len > 0 ? evt->data_len : 0);
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
// Valve state callback
// --------------------------------------------------------------------------

static void on_valve_state_changed(void)
{
    mqtt_publish_all_states();
}

// --------------------------------------------------------------------------
// Init & task
// --------------------------------------------------------------------------

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

    while (wifi_get_state() != WIFI_STATE_CONNECTED)
        vTaskDelay(pdMS_TO_TICKS(2000));

    if (g_config.mqtt_uri[0] == '\0') {
        ESP_LOGI(TAG, "no MQTT URI configured, task exit");
        vTaskDelete(NULL);
        return;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri      = g_config.mqtt_uri,
        .credentials.username    = g_config.mqtt_user,
        .credentials.authentication.password = g_config.mqtt_pass,
        .session.keepalive        = 60,
        .session.last_will.topic  = MQTT_AVAIL_TOPIC,
        .session.last_will.msg    = "offline",
        .session.last_will.qos    = 1,
        .session.last_will.retain = 1,
        .buffer.size              = 4096,
        .buffer.out_size          = 4096,
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
