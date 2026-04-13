#include "log_manager.h"
#include "storage/nvs_storage.h"
#include "wifi/wifi_manager.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "log_mgr";

// --------------------------------------------------------------------------
// Ring buffer
// --------------------------------------------------------------------------

static log_entry_t  s_buf[LOG_BUFFER_SIZE];
static int          s_head   = 0;   // indeks następnego zapisu
static int          s_count  = 0;   // ile wpisów jest w buforze
static SemaphoreHandle_t s_mutex;

extern app_config_t g_config;

// --------------------------------------------------------------------------
// GELF UDP
// --------------------------------------------------------------------------

// Syslog RFC 3164 over UDP
static const char * const s_months[12] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};

static void syslog_send(const log_entry_t *e)
{
    if (!g_config.graylog_enabled) return;
    if (g_config.graylog_host[0] == '\0') return;
    if ((int)e->level > (int)g_config.graylog_level) return;
    if (wifi_get_state() != WIFI_STATE_CONNECTED) return;

    // PRI = facility(1=user) * 8 + severity; nasze levele odpowiadają syslog severity
    int pri = 8 + (int)e->level;

    struct tm t;
    time_t ts = (time_t)e->unix_ts;
    localtime_r(&ts, &t);

    // Format: <PRI>Mmm DD HH:MM:SS hostname tag: message  (RFC 3164, czas lokalny)
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "<%d>%s %2d %02d:%02d:%02d WachcioDrop %s: %s",
        pri,
        s_months[t.tm_mon < 12 ? t.tm_mon : 0], t.tm_mday,
        t.tm_hour, t.tm_min, t.tm_sec,
        e->tag, e->msg);

    if (len <= 0) return;

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(g_config.graylog_port);
    if (inet_aton(g_config.graylog_host, &dest.sin_addr) == 0) return;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return;
    sendto(sock, buf, len, 0, (struct sockaddr *)&dest, sizeof(dest));
    close(sock);
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

esp_err_t log_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    memset(s_buf, 0, sizeof(s_buf));
    ESP_LOGI(TAG, "init OK (buffer=%d entries)", LOG_BUFFER_SIZE);
    return ESP_OK;
}

void log_write(log_lvl_t level, const char *tag, const char *fmt, ...)
{
    log_entry_t e;
    e.unix_ts = (uint32_t)time(NULL);
    e.level   = level;
    strncpy(e.tag, tag ? tag : "", LOG_TAG_LEN - 1);
    e.tag[LOG_TAG_LEN - 1] = '\0';

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.msg, LOG_MSG_LEN, fmt, ap);
    va_end(ap);

    // Zapis do ring buffera
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_buf[s_head] = e;
        s_head = (s_head + 1) % LOG_BUFFER_SIZE;
        if (s_count < LOG_BUFFER_SIZE) s_count++;
        xSemaphoreGive(s_mutex);
    }

    // Przekaż do ESP_LOG (UART)
    switch (level) {
        case LOG_LVL_ERROR: ESP_LOGE(e.tag, "%s", e.msg); break;
        case LOG_LVL_WARN:  ESP_LOGW(e.tag, "%s", e.msg); break;
        case LOG_LVL_DEBUG: ESP_LOGD(e.tag, "%s", e.msg); break;
        default:            ESP_LOGI(e.tag, "%s", e.msg); break;
    }

    // Wyślij syslog UDP (poza mutexem — może zablokować)
    syslog_send(&e);
}

int log_get_entries(log_entry_t *out, int max_count, int offset)
{
    if (!out || max_count <= 0) return 0;

    int copied = 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        int total = s_count;
        // Wyznacz indeks najstarszego wpisu
        int start = (s_count < LOG_BUFFER_SIZE)
                    ? 0
                    : s_head; // gdy bufor pełny head wskazuje najstarszy

        for (int i = offset; i < total && copied < max_count; i++) {
            int idx = (start + i) % LOG_BUFFER_SIZE;
            out[copied++] = s_buf[idx];
        }
        xSemaphoreGive(s_mutex);
    }
    return copied;
}

int log_get_total(void)
{
    return s_count;
}

void log_clear(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        s_head  = 0;
        s_count = 0;
        memset(s_buf, 0, sizeof(s_buf));
        xSemaphoreGive(s_mutex);
    }
}
