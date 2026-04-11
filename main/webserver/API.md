# WachcioDrop REST API

Wszystkie endpointy (poza `/api/setup`) wymagają nagłówka:
```
Authorization: Bearer <token>
```
Token jest generowany przy pierwszym uruchomieniu i widoczny na wyświetlaczu OLED. Można go odczytać lub zmienić przez `POST /api/settings`.

Base URL: `http://<ip>/`

---

## Sekcje

### GET /api/sections
Pobierz stan wszystkich sekcji i zaworu głównego.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/sections
```

Odpowiedź:
```json
{
  "master": false,
  "sections": [
    {"id": 1, "active": false},
    {"id": 2, "active": true},
    ...
  ]
}
```

---

### POST /api/sections/{id}/on
Włącz sekcję. `id` = 1–8. Pole `duration` w sekundach, `0` = bezterminowo.

```bash
# Włącz sekcję 3 na 5 minut
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"duration":300}' \
  http://192.168.20.230/api/sections/3/on

# Włącz bezterminowo
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/sections/3/on
```

Odpowiedź:
```json
{"ok": true}
```

---

### POST /api/sections/{id}/off
Wyłącz sekcję.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/sections/3/off
```

---

### POST /api/sections/all/off
Wyłącz wszystkie sekcje natychmiast.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/sections/all/off
```

---

## Status systemu

### GET /api/status
Ogólny stan systemu.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/status
```

Odpowiedź:
```json
{
  "uptime_sec": 3600,
  "ip": "192.168.20.230",
  "rssi": -55,
  "sections_active": 1,
  "master_active": true,
  "irrigation_today": true,
  "time": "2025-06-15T10:30:00"
}
```

---

## Harmonogram

### GET /api/schedule
Pobierz wszystkie 16 wpisów harmonogramu.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/schedule
```

Odpowiedź:
```json
[
  {
    "id": 0,
    "enabled": true,
    "days_mask": 127,
    "hour": 6,
    "minute": 30,
    "duration_sec": 600,
    "section_mask": 3,
    "group_mask": 0
  },
  ...
]
```

Pola:
| Pole | Opis |
|------|------|
| `id` | Indeks wpisu 0–15 |
| `enabled` | Czy wpis aktywny |
| `days_mask` | Dni tygodnia: bit0=Pon, bit1=Wt, …, bit6=Nie. `127` = każdy dzień |
| `hour` / `minute` | Godzina uruchomienia (czas lokalny) |
| `duration_sec` | Czas trwania w sekundach |
| `section_mask` | Sekcje: bit0=sekcja1, …, bit7=sekcja8 |
| `group_mask` | Grupy: bit0=grupa1, …, bit9=grupa10 (rozwijane do section_mask) |

---

### PUT /api/schedule/{id}
Ustaw wpis harmonogramu. `id` = 0–15.

```bash
# Włącz sekcje 1 i 2 w poniedziałek i środę o 6:30 przez 10 minut
curl -X PUT \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "days_mask": 5,
    "hour": 6,
    "minute": 30,
    "duration_sec": 600,
    "section_mask": 3,
    "group_mask": 0
  }' \
  http://192.168.20.230/api/schedule/0
```

Przykłady `days_mask`:
| Wartość | Dni |
|---------|-----|
| `1` | Poniedziałek |
| `5` | Pon + Śr (bit0 + bit2) |
| `31` | Pon–Pt |
| `96` | Sob + Nie (bit5 + bit6) |
| `127` | Cały tydzień |

---

### DELETE /api/schedule/{id}
Wyczyść wpis harmonogramu.

```bash
curl -X DELETE \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/schedule/0
```

---

## Grupy

### GET /api/groups
Pobierz wszystkie grupy.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/groups
```

Odpowiedź:
```json
[
  {"id": 1, "name": "Trawnik", "section_mask": 7},
  {"id": 2, "name": "Ogrod",   "section_mask": 24},
  ...
]
```

---

### PUT /api/groups/{id}
Ustaw grupę. `id` = 1–10.

```bash
curl -X PUT \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name": "Trawnik", "section_mask": 7}' \
  http://192.168.20.230/api/groups/1
```

`section_mask`: bit0=sekcja1, …, bit7=sekcja8. Wartość `7` = sekcje 1, 2, 3.

---

### POST /api/groups/{id}/activate
Aktywuj grupę ręcznie.

```bash
# Włącz grupę 1 na 15 minut
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"duration": 900}' \
  http://192.168.20.230/api/groups/1/activate
```

---

## Czas

### GET /api/time
Pobierz aktualny czas z RTC.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/time
```

Odpowiedź:
```json
{
  "time": "2025-06-15T10:30:00",
  "unix": 1750000200,
  "tz": 1
}
```

---

### POST /api/time
Ustaw czas RTC — trzy warianty wywołania.

**Wariant 1: unix timestamp (UTC)**
```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"unix": 1750000200}' \
  http://192.168.20.230/api/time
```

**Wariant 2: datetime string (czas lokalny)**
```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"datetime": "2025-06-15T10:30:00"}' \
  http://192.168.20.230/api/time
```

**Wariant 3: pola osobno (czas lokalny)**
```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"year":2025,"month":6,"day":15,"hour":10,"minute":30,"second":0}' \
  http://192.168.20.230/api/time
```

Odpowiedź (potwierdzenie ustawionego czasu):
```json
{"ok": true, "time": "2025-06-15T10:30:00", "unix": 1750000200}
```

> Czas lokalny uwzględnia polską strefę czasową z automatycznym DST (CET/CEST).

---

### POST /api/time/sntp
Wymuś natychmiastową synchronizację czasu z serwera NTP i zapisz do RTC. Blokuje do 30s.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/time/sntp
```

Odpowiedź (sukces):
```json
{"ok": true, "time": "2025-06-15T10:30:05", "unix": 1750000205}
```

Odpowiedź (brak WiFi):
```json
{"error": "no WiFi connection"}   // HTTP 503
```

Odpowiedź (timeout):
```json
{"error": "NTP sync timeout"}     // HTTP 504
```

---

## Ustawienia

### GET /api/settings
Pobierz aktualną konfigurację (hasła i token nie są zwracane).

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/settings
```

Odpowiedź:
```json
{
  "wifi_ssid": "MojaSiec",
  "mqtt_uri": "mqtt://192.168.20.10",
  "mqtt_user": "user",
  "php_url": "http://example.com/check.php",
  "ntp_server": "pl.pool.ntp.org",
  "tz_offset": 1
}
```

---

### POST /api/settings
Zapisz konfigurację. Można podać tylko zmieniane pola.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "wifi_ssid":  "MojaSiec",
    "wifi_pass":  "haslo123",
    "mqtt_uri":   "mqtt://192.168.20.10",
    "mqtt_user":  "user",
    "mqtt_pass":  "mqttpass",
    "php_url":    "http://example.com/check.php",
    "ntp_server": "pl.pool.ntp.org",
    "api_token":  "nowytoken"
  }' \
  http://192.168.20.230/api/settings
```

---

### GET /api/settings/export
Pobierz pełny backup urządzenia jako JSON: konfiguracja (wraz z hasłami), harmonogram (16 wpisów), grupy (10 grup).

```bash
curl -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/settings/export \
  -o backup.json
```

Odpowiedź:
```json
{
  "config": {
    "wifi_ssid": "MojaSiec",
    "wifi_pass": "haslo123",
    "mqtt_uri": "mqtt://192.168.20.10",
    "mqtt_user": "user",
    "mqtt_pass": "mqttpass",
    "php_url": "http://example.com/check.php",
    "ntp_server": "pl.pool.ntp.org",
    "api_token": "mojtoken",
    "tz_offset": 1
  },
  "schedule": [
    {"id": 0, "enabled": true, "days_mask": 127, "hour": 6, "minute": 30,
     "duration_sec": 600, "section_mask": 3, "group_mask": 0},
    ...
  ],
  "groups": [
    {"id": 1, "name": "Trawnik", "section_mask": 7},
    ...
  ]
}
```

---

### PUT /api/settings/import
Wgraj backup. Przywraca konfigurację, harmonogram i grupy. Hasła i token są opcjonalne — jeśli nie podane w JSON, pozostają bez zmian. Maksymalny rozmiar pliku: 8192 B.

```bash
curl -X PUT \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d @backup.json \
  http://192.168.20.230/api/settings/import
```

Odpowiedź:
```json
{"ok": true}
```

> **Uwaga:** Po imporcie nowego tokenu lub WiFi należy zrestartować urządzenie, aby zmiany weszły w życie.

---

## OTA Update

### POST /api/ota
Wgraj nowy firmware. Po udanym wgraniu urządzenie restartuje się automatycznie.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @build/irrigation_controller.bin \
  http://192.168.20.230/api/ota
```

---

## Konfiguracja przez captive portal (bez autoryzacji)

### POST /api/setup
Używany przez captive portal przy pierwszej konfiguracji. Nie wymaga tokenu.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{
    "wifi_ssid": "MojaSiec",
    "wifi_pass": "haslo123",
    "mqtt_uri":  "mqtt://192.168.20.10",
    "php_url":   "http://example.com/check.php",
    "api_token": "mojtoken"
  }' \
  http://192.168.4.1/api/setup
```

---

## MQTT

Broker konfigurowany w `/api/settings` (pole `mqtt_uri`, np. `mqtt://192.168.20.10`).

> **Uwaga:** Topiki typu `get` wymagają subskrypcji odpowiedzi **przed** wysłaniem zapytania, ponieważ odpowiedź nie jest retained. Najwygodniej monitorować wszystko przez `mosquitto_sub -h 192.168.20.10 -t "wachciodrop/#" -v`.

---

### Sekcje

| Topik (publish → ESP32) | Payload | Opis |
|-------------------------|---------|------|
| `wachciodrop/section/{1-8}/command` | `ON` | Włącz sekcję bezterminowo |
| `wachciodrop/section/{1-8}/command` | `OFF` | Wyłącz sekcję |
| `wachciodrop/section/{1-8}/command` | `{"on":true,"duration":300}` | Włącz z timerem (sekundy) |
| `wachciodrop/section/all/command` | `OFF` | Wyłącz wszystkie sekcje |

| Topik (subscribe ← ESP32) | Wartość | Opis |
|---------------------------|---------|------|
| `wachciodrop/section/{1-8}/state` | `ON` / `OFF` | Stan sekcji |
| `wachciodrop/section/master/state` | `ON` / `OFF` | Stan zaworu głównego |
| `wachciodrop/status` | JSON | IP, RSSI, uptime, irrigation_today, online (co 60s) |

---

### Grupy

| Topik (publish → ESP32) | Payload | Opis |
|-------------------------|---------|------|
| `wachciodrop/groups/get` | _(pusty)_ | Pobierz listę grup |
| `wachciodrop/group/{1-10}/command` | `{"on":true,"duration":300}` | Aktywuj grupę z timerem |
| `wachciodrop/group/{1-10}/command` | `{"on":false}` | Wyłącz wszystkie sekcje grupy |
| `wachciodrop/group/{1-10}/set` | `{"name":"Trawnik","section_mask":7}` | Ustaw grupę |

| Topik (subscribe ← ESP32) | Opis |
|---------------------------|------|
| `wachciodrop/groups/state` | JSON array z listą grup (odpowiedź na `get` i `set`) |

---

### Harmonogram

| Topik (publish → ESP32) | Payload | Opis |
|-------------------------|---------|------|
| `wachciodrop/schedule/get` | _(pusty)_ | Pobierz wszystkie 16 wpisów |
| `wachciodrop/schedule/set` | JSON array (16 wpisów) | Bulk update całego harmonogramu |
| `wachciodrop/schedule/{0-15}/set` | JSON pojedynczego wpisu | Ustaw jeden wpis |
| `wachciodrop/schedule/{0-15}/delete` | _(pusty)_ | Wyczyść wpis |

| Topik (subscribe ← ESP32) | Opis |
|---------------------------|------|
| `wachciodrop/schedule/state` | JSON array 16 wpisów (odpowiedź na `get`) |
| `wachciodrop/schedule/set/result` | `{"ok":true}` (potwierdzenie `{id}/set`) |
| `wachciodrop/schedule/delete/result` | `{"ok":true}` (potwierdzenie `{id}/delete`) |

---

### Czas

| Topik (publish → ESP32) | Payload | Opis |
|-------------------------|---------|------|
| `wachciodrop/time/get` | _(pusty)_ | Pobierz aktualny czas |
| `wachciodrop/time/set` | `{"unix":1750000200}` | Ustaw czas (unix UTC) |
| `wachciodrop/time/set` | `{"datetime":"2025-06-15T10:30:00"}` | Ustaw czas (lokalny) |
| `wachciodrop/time/set` | `{"year":2025,"month":6,"day":15,"hour":10,"minute":30}` | Ustaw czas (pola) |
| `wachciodrop/time/sntp` | _(pusty)_ | Wymuś synchronizację NTP |

| Topik (subscribe ← ESP32) | Opis |
|---------------------------|------|
| `wachciodrop/time/state` | `{"time":"...","unix":...,"tz_offset":1}` |

---

### Ustawienia

| Topik (publish → ESP32) | Payload | Opis |
|-------------------------|---------|------|
| `wachciodrop/settings/get` | _(pusty)_ | Pobierz ustawienia (bez haseł) |
| `wachciodrop/settings/set` | JSON (pola opcjonalne) | Zmień ustawienia |

| Topik (subscribe ← ESP32) | Opis |
|---------------------------|------|
| `wachciodrop/settings/state` | JSON z ustawieniami (bez haseł) |
| `wachciodrop/settings/set/result` | `{"ok":true}` (potwierdzenie `set`) |

Pola `settings/set`:
```json
{
  "wifi_ssid": "MojaSiec",
  "wifi_pass": "haslo123",
  "mqtt_uri":  "mqtt://192.168.20.10",
  "mqtt_user": "user",
  "mqtt_pass": "mqttpass",
  "php_url":   "http://example.com/check.php",
  "ntp_server":"pl.pool.ntp.org",
  "api_token": "nowytoken",
  "tz_offset": 1
}
```

---

### Przykłady mosquitto

```bash
# Monitoruj wszystkie topiki (uruchom w osobnym terminalu)
mosquitto_sub -h 192.168.20.10 -t "wachciodrop/#" -v

# Włącz sekcję 2 na 10 minut
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/section/2/command" \
  -m '{"on":true,"duration":600}'

# Wyłącz wszystko
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/section/all/command" -m "OFF"

# Pobierz harmonogram
mosquitto_pub -h 192.168.20.10 -t "wachciodrop/schedule/get" -m ""

# Ustaw wpis 0 (sekcje 1+2, Pon+Śr, 6:30, 10 min)
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/schedule/0/set" \
  -m '{"enabled":true,"days_mask":5,"hour":6,"minute":30,"duration_sec":600,"section_mask":3,"group_mask":0}'

# Usuń wpis 0
mosquitto_pub -h 192.168.20.10 -t "wachciodrop/schedule/0/delete" -m ""

# Ustaw grupę 1
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/group/1/set" \
  -m '{"name":"Trawnik","section_mask":7}'

# Włącz grupę 1 na 15 minut
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/group/1/command" \
  -m '{"on":true,"duration":900}'

# Wymuś sync NTP
mosquitto_pub -h 192.168.20.10 -t "wachciodrop/time/sntp" -m ""

# Zmień serwer NTP
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/settings/set" \
  -m '{"ntp_server":"pl.pool.ntp.org"}'
```

---

### Home Assistant Autodiscovery
Po połączeniu z brokerem ESP32 automatycznie publikuje konfigurację HA pod:
```
homeassistant/switch/wachciodrop_esp32s3/section_{1-8}/config
homeassistant/switch/wachciodrop_esp32s3/section_master/config
```
Urządzenia pojawią się w HA automatycznie po restarcie lub ponownym połączeniu MQTT.
