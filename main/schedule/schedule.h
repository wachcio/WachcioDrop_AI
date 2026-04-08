#pragma once
#include "storage/nvs_storage.h"
#include "esp_err.h"

// Moduł harmonogramu
// Task scheduler_task sprawdza co minutę, czy któryś wpis pasuje do aktualnego czasu

esp_err_t schedule_init(void);

// Pobierz wszystkie wpisy (wskaźnik na tablicę wewnętrzną)
const schedule_entry_t *schedule_get_all(void);

// Pobierz/ustaw pojedynczy wpis (id 0-15)
esp_err_t schedule_get(uint8_t id, schedule_entry_t *out);
esp_err_t schedule_set(const schedule_entry_t *entry);
esp_err_t schedule_delete(uint8_t id);

// Task - uruchomić z main
void scheduler_task(void *arg);
