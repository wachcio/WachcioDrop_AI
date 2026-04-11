# WachcioDrop MQTT API

Broker konfigurowany w `/api/settings` (pole `mqtt_uri`, np. `mqtt://192.168.20.10`).

> **Uwaga:** Topiki typu `get` wymagają subskrypcji odpowiedzi **przed** wysłaniem zapytania, ponieważ odpowiedź nie jest retained. Najwygodniej monitorować wszystko przez:
> ```bash
> mosquitto_sub -h 192.168.20.10 -t "wachciodrop/#" -v
> ```

---

## Sekcje

| Topik (publish → ESP32) | Payload | Opis |
|-------------------------|---------|------|
| `wachciodrop/section/{1-8}/command` | `ON` | Włącz sekcję bezterminowo (wyłącza pozostałe) |
| `wachciodrop/section/{1-8}/command` | `OFF` | Wyłącz sekcję |
| `wachciodrop/section/{1-8}/command` | `{"on":true,"duration":300}` | Włącz z timerem w sekundach |
| `wachciodrop/section/all/command` | `OFF` | Wyłącz wszystkie sekcje |

| Topik (subscribe ← ESP32) | Wartość | Opis |
|---------------------------|---------|------|
| `wachciodrop/section/{1-8}/state` | `ON` / `OFF` | Stan sekcji |
| `wachciodrop/section/master/state` | `ON` / `OFF` | Stan zaworu głównego |
| `wachciodrop/status` | JSON | IP, RSSI, uptime, irrigation_today, online (co 60s) |

---

## Grupy

| Topik (publish → ESP32) | Payload | Opis |
|-------------------------|---------|------|
| `wachciodrop/groups/get` | _(pusty)_ | Pobierz listę grup |
| `wachciodrop/group/{1-10}/command` | `{"on":true,"duration":300}` | Aktywuj grupę z timerem |
| `wachciodrop/group/{1-10}/command` | `{"on":false}` | Wyłącz wszystkie sekcje grupy |
| `wachciodrop/group/{1-10}/set` | `{"name":"Trawnik","section_mask":7}` | Ustaw grupę |

| Topik (subscribe ← ESP32) | Opis |
|---------------------------|------|
| `wachciodrop/groups/state` | JSON array z listą grup (odpowiedź na `get` i `set`) |

`section_mask`: bit0=sekcja1, …, bit7=sekcja8. Wartość `7` = sekcje 1, 2, 3.

---

## Harmonogram

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

Format wpisu harmonogramu:
```json
{
  "id": 0,
  "enabled": true,
  "days_mask": 5,
  "hour": 6,
  "minute": 30,
  "duration_sec": 600,
  "section_mask": 3,
  "group_mask": 0
}
```

`days_mask`: bit0=Pon, bit1=Wt, …, bit6=Nie. `127` = każdy dzień.

---

## Czas

| Topik (publish → ESP32) | Payload | Opis |
|-------------------------|---------|------|
| `wachciodrop/time/get` | _(pusty)_ | Pobierz aktualny czas |
| `wachciodrop/time/set` | `{"unix":1750000200}` | Ustaw czas (unix UTC) |
| `wachciodrop/time/set` | `{"datetime":"2025-06-15T10:30:00"}` | Ustaw czas lokalny |
| `wachciodrop/time/set` | `{"year":2025,"month":6,"day":15,"hour":10,"minute":30}` | Ustaw czas (pola osobno) |
| `wachciodrop/time/sntp` | _(pusty)_ | Wymuś synchronizację NTP (blokuje do 30s) |

| Topik (subscribe ← ESP32) | Opis |
|---------------------------|------|
| `wachciodrop/time/state` | `{"time":"2025-06-15T10:30:00","unix":1750000200,"tz_offset":1}` |

---

## Ustawienia

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
  "wifi_ssid":  "MojaSiec",
  "wifi_pass":  "haslo123",
  "mqtt_uri":   "mqtt://192.168.20.10",
  "mqtt_user":  "user",
  "mqtt_pass":  "mqttpass",
  "php_url":    "http://example.com/check.php",
  "ntp_server": "pl.pool.ntp.org",
  "api_token":  "nowytoken",
  "tz_offset":  1
}
```

---

## Home Assistant Autodiscovery

Po połączeniu z brokerem ESP32 automatycznie publikuje konfigurację HA pod:
```
homeassistant/switch/wachciodrop_esp32s3/section_{1-8}/config
homeassistant/switch/wachciodrop_esp32s3/section_master/config
```
Urządzenia pojawią się w HA automatycznie po restarcie lub ponownym połączeniu MQTT.

---

## Przykłady mosquitto

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
