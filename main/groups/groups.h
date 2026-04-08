#pragma once
#include "storage/nvs_storage.h"
#include "esp_err.h"

// Zarządzanie grupami sekcji
// Grupy numerowane 1-GROUPS_MAX (id 1-10)
// Sekcja może należeć do wielu grup jednocześnie

esp_err_t groups_init(void);

// Pobierz wszystkie grupy
const irrigation_group_t *groups_get_all(void);

// Pobierz/ustaw pojedynczą grupę (id 1-10)
esp_err_t groups_get(uint8_t id, irrigation_group_t *out);
esp_err_t groups_set(const irrigation_group_t *group);

// Rozwiń maskę grup (bit0=gr1...bit9=gr10) na maskę sekcji (bit0-7)
uint8_t groups_expand_mask(uint16_t group_mask);

// Aktywuj grupę na czas duration_sec (wywołuje valve_sections_on z rozwinięciem)
esp_err_t groups_activate(uint8_t group_id, uint32_t duration_sec);
