#include "mqtt_manager.h"
#include "valve/valve.h"
#include "leds/leds.h"
#include "schedule/schedule.h"
#include "groups/groups.h"
#include "wifi/wifi_manager.h"
#include "storage/nvs_storage.h"
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

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static bool                     s_connected = false;
static SemaphoreHandle_t        s_mutex;

extern app_config_t g_config;
extern bool         g_irrigation_today;

#define MQTT_PREFIX     "irrigation"
#define MQTT_HA_PREFIX  "homeassistant"
#define DEVICE_NAME     "IrrigationController"
#define DEVICE_ID       "irrigation_esp32s3"

// --------------------------------------------------------------------------
// HA Autodiscovery helpers
// --------------------------------------------------------------------------

static void publish_ha_discovery(void)
{
    if (!s_connected) return;

    char topic[128];

    // Discovery dla każdej sekcji
    for (int i = 0; i <= SECTIONS_COUNT; i++) {
        char sec_name[32];
        if (i == 0) {
            strncpy(sec_name, "Master", sizeof(sec_name));
        } else {
            snprintf(sec_name, sizeof(sec_name), "Sekcja %d", i);
        }

        char uid[32], obj_id[32];
        if (i == 0) {
            snprintf(uid,    sizeof(uid),    "%s_master",    DEVICE_ID);
            snprintf(obj_id, sizeof(obj_id), "section_master");
        } else {
            snprintf(uid,    sizeof(uid),    "%s_s%d",  DEVICE_ID, i);
            snprintf(obj_id, sizeof(obj_id), "section_%d", i);
        }

        snprintf(topic, sizeof(topic),
                 MQTT_HA_PREFIX "/switch/" DEVICE_ID "/%s/config", obj_id);

        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name",         sec_name);
        cJSON_AddStringToObject(d, "unique_id",    uid);
        cJSON_AddStringToObject(d, "platform",     "mqtt");
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
        cJSON_AddStringToObject(d, "payload_on",   "ON");
        cJSON_AddStringToObject(d, "payload_off",  "OFF");
        cJSON_AddStringToObject(d, "availability_topic",
            MQTT_PREFIX "/status");
        cJSON_AddStringToObject(d, "payload_available",     "{\"online\":true}");
        cJSON_AddStringToObject(d, "payload_not_available", "{\"online\":false}");

        // Device block
        cJSON *dev = cJSON_CreateObject();
        cJSON_AddStringToObject(dev, "identifiers",   DEVICE_ID);
        cJSON_AddStringToObject(dev, "name",          DEVICE_NAME);
        cJSON_AddStringToObject(dev, "model",         "ESP32-S3 N16R8");
        cJSON_AddStringToObject(dev, "manufacturer",  "Custom");
        cJSON_AddItemToObject(d, "device", dev);

        char *s = cJSON_PrintUnformatted(d);
        esp_mqtt_client_publish(s_client, topic, s, strlen(s), 1, 1);
        free(s);
        cJSON_Delete(d);
    }
    ESP_LOGI(TAG, "HA discovery published");
}

// --------------------------------------------------------------------------
// Publish state
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
    const char *payload = active ? "ON" : "OFF";
    esp_mqtt_client_publish(s_client, topic, payload, strlen(payload), 0, 0);
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

    char *s = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(s_client, MQTT_PREFIX "/status", s, strlen(s), 0, 1);
    free(s);
    cJSON_Delete(root);
}

// --------------------------------------------------------------------------
// Incoming message handler
// --------------------------------------------------------------------------

static void handle_command(const char *topic, const char *data, int data_len)
{
    char payload[128];
    int  plen = data_len < (int)sizeof(payload) - 1 ? data_len : (int)sizeof(payload) - 1;
    strncpy(payload, data, plen);
    payload[plen] = '\0';

    // irrigation/section/all/command → OFF
    if (strstr(topic, "/section/all/command")) {
        if (strcmp(payload, "OFF") == 0) {
            valve_all_off();
            mqtt_publish_all_states();
        }
        return;
    }

    // irrigation/section/{id}/command
    int sec_id = 0;
    if (sscanf(topic, MQTT_PREFIX "/section/%d/command", &sec_id) == 1) {
        if (strcmp(payload, "ON") == 0) {
            valve_section_on((uint8_t)sec_id, 0);
            mqtt_publish_section_state(sec_id, true);
        } else if (strcmp(payload, "OFF") == 0) {
            valve_section_off((uint8_t)sec_id);
            mqtt_publish_section_state(sec_id, false);
        } else {
            // JSON payload: {"on":true,"duration":300}
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
        return;
    }

    // irrigation/group/{id}/command → {"on":true,"duration":300}
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
        return;
    }

    // irrigation/schedule/set → JSON array
    if (strstr(topic, "/schedule/set")) {
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
        return;
    }
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

        // Subskrybuj tematy komend
        esp_mqtt_client_subscribe(s_client,
            MQTT_PREFIX "/section/+/command", 0);
        esp_mqtt_client_subscribe(s_client,
            MQTT_PREFIX "/section/all/command", 0);
        esp_mqtt_client_subscribe(s_client,
            MQTT_PREFIX "/group/+/command", 0);
        esp_mqtt_client_subscribe(s_client,
            MQTT_PREFIX "/schedule/set", 0);

        publish_ha_discovery();
        mqtt_publish_all_states();
        mqtt_publish_status();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        break;

    case MQTT_EVENT_DATA:
        if (evt->topic && evt->data) {
            char topic[128] = {0};
            int tlen = evt->topic_len < (int)sizeof(topic) - 1
                       ? evt->topic_len : (int)sizeof(topic) - 1;
            strncpy(topic, evt->topic, tlen);
            handle_command(topic, evt->data, evt->data_len);
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

esp_err_t mqtt_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "init OK");
    return ESP_OK;
}

void mqtt_manager_task(void *arg)
{
    ESP_LOGI(TAG, "task started");

    // Czekaj na połączenie WiFi
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
    };

    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    // Publikuj status co 60s
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        mqtt_publish_status();
    }
}
