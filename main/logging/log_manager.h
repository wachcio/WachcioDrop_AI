#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Poziomy zgodne z syslog / GELF
typedef enum {
    LOG_LVL_ERROR = 3,
    LOG_LVL_WARN  = 4,
    LOG_LVL_INFO  = 6,
    LOG_LVL_DEBUG = 7,
} log_lvl_t;

#define LOG_BUFFER_SIZE  200
#define LOG_TAG_LEN       12
#define LOG_MSG_LEN       96

typedef struct {
    uint32_t  unix_ts;
    log_lvl_t level;
    char      tag[LOG_TAG_LEN];
    char      msg[LOG_MSG_LEN];
} log_entry_t;

esp_err_t log_manager_init(void);

// Zapisz wpis (thread-safe)
void log_write(log_lvl_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Pobierz wpisy z bufora (offset=0 → najstarszy), zwraca liczbę skopiowanych
int  log_get_entries(log_entry_t *out, int max_count, int offset);
int  log_get_total(void);   // całkowita liczba wpisów w buforze
void log_clear(void);

// Skróty
#define APP_LOGI(tag, ...) log_write(LOG_LVL_INFO,  tag, __VA_ARGS__)
#define APP_LOGW(tag, ...) log_write(LOG_LVL_WARN,  tag, __VA_ARGS__)
#define APP_LOGE(tag, ...) log_write(LOG_LVL_ERROR, tag, __VA_ARGS__)
#define APP_LOGD(tag, ...) log_write(LOG_LVL_DEBUG, tag, __VA_ARGS__)
