# WachcioDrop MQTT API

The broker is configured in `/api/settings` (field `mqtt_uri`, e.g. `mqtt://192.168.20.10`).

> **Note:** `get` topics require subscribing to the response topic **before** sending the request, since responses are not retained. The easiest way to monitor everything is:
> ```bash
> mosquitto_sub -h 192.168.20.10 -t "wachciodrop/#" -v
> ```

---

## Sections

| Topic (publish → ESP32) | Payload | Description |
|-------------------------|---------|-------------|
| `wachciodrop/section/{1-8}/command` | `ON` | Turn on section indefinitely (turns off all others) |
| `wachciodrop/section/{1-8}/command` | `OFF` | Turn off section |
| `wachciodrop/section/{1-8}/command` | `{"on":true,"duration":300}` | Turn on with timer in seconds |
| `wachciodrop/section/all/command` | `OFF` | Turn off all sections |

| Topic (subscribe ← ESP32) | Value | Description |
|---------------------------|-------|-------------|
| `wachciodrop/section/{1-8}/state` | `ON` / `OFF` | Section state |
| `wachciodrop/section/master/state` | `ON` / `OFF` | Master valve state |
| `wachciodrop/status` | JSON | IP, RSSI, uptime, irrigation_today, online (every 60s) |

---

## Groups

| Topic (publish → ESP32) | Payload | Description |
|-------------------------|---------|-------------|
| `wachciodrop/groups/get` | _(empty)_ | Get list of groups |
| `wachciodrop/group/{1-10}/command` | `{"on":true,"duration":300}` | Activate group with timer |
| `wachciodrop/group/{1-10}/command` | `{"on":false}` | Turn off all sections in group |
| `wachciodrop/group/{1-10}/set` | `{"name":"Lawn","section_mask":7}` | Set group |

| Topic (subscribe ← ESP32) | Description |
|---------------------------|-------------|
| `wachciodrop/groups/state` | JSON array with group list (response to `get` and `set`) |

`section_mask`: bit0=section1, …, bit7=section8. Value `7` = sections 1, 2, 3.

---

## Schedule

| Topic (publish → ESP32) | Payload | Description |
|-------------------------|---------|-------------|
| `wachciodrop/schedule/get` | _(empty)_ | Get all 16 entries |
| `wachciodrop/schedule/set` | JSON array (16 entries) | Bulk update entire schedule |
| `wachciodrop/schedule/{0-15}/set` | JSON of single entry | Set one entry |
| `wachciodrop/schedule/{0-15}/delete` | _(empty)_ | Clear an entry |

| Topic (subscribe ← ESP32) | Description |
|---------------------------|-------------|
| `wachciodrop/schedule/state` | JSON array of 16 entries (response to `get`) |
| `wachciodrop/schedule/set/result` | `{"ok":true}` (confirmation of `{id}/set`) |
| `wachciodrop/schedule/delete/result` | `{"ok":true}` (confirmation of `{id}/delete`) |

Schedule entry format:
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

`days_mask`: bit0=Mon, bit1=Tue, …, bit6=Sun. `127` = every day.

---

## Time

| Topic (publish → ESP32) | Payload | Description |
|-------------------------|---------|-------------|
| `wachciodrop/time/get` | _(empty)_ | Get current time |
| `wachciodrop/time/set` | `{"unix":1750000200}` | Set time (unix UTC) |
| `wachciodrop/time/set` | `{"datetime":"2025-06-15T10:30:00"}` | Set local time |
| `wachciodrop/time/set` | `{"year":2025,"month":6,"day":15,"hour":10,"minute":30}` | Set time (individual fields) |
| `wachciodrop/time/sntp` | _(empty)_ | Force NTP sync (blocks up to 30s) |

| Topic (subscribe ← ESP32) | Description |
|---------------------------|-------------|
| `wachciodrop/time/state` | `{"time":"2025-06-15T10:30:00","unix":1750000200,"tz_offset":1}` |

---

## Settings

| Topic (publish → ESP32) | Payload | Description |
|-------------------------|---------|-------------|
| `wachciodrop/settings/get` | _(empty)_ | Get settings (without passwords) |
| `wachciodrop/settings/set` | JSON (optional fields) | Update settings |

| Topic (subscribe ← ESP32) | Description |
|---------------------------|-------------|
| `wachciodrop/settings/state` | JSON with settings (without passwords) |
| `wachciodrop/settings/set/result` | `{"ok":true}` (confirmation of `set`) |

`settings/set` fields:
```json
{
  "wifi_ssid":  "MyNetwork",
  "wifi_pass":  "password123",
  "mqtt_uri":   "mqtt://192.168.20.10",
  "mqtt_user":  "user",
  "mqtt_pass":  "mqttpass",
  "php_url":    "http://example.com/check.php",
  "ntp_server": "pl.pool.ntp.org",
  "api_token":  "newtoken",
  "tz_offset":  1
}
```

---

## Home Assistant Autodiscovery

Upon connecting to the broker, the ESP32 automatically publishes HA configuration to:
```
homeassistant/switch/wachciodrop_esp32s3/section_{1-8}/config
homeassistant/switch/wachciodrop_esp32s3/section_master/config
```
Devices will appear in HA automatically after restart or MQTT reconnection.

---

## mosquitto Examples

```bash
# Monitor all topics (run in a separate terminal)
mosquitto_sub -h 192.168.20.10 -t "wachciodrop/#" -v

# Turn on section 2 for 10 minutes
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/section/2/command" \
  -m '{"on":true,"duration":600}'

# Turn off everything
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/section/all/command" -m "OFF"

# Get schedule
mosquitto_pub -h 192.168.20.10 -t "wachciodrop/schedule/get" -m ""

# Set entry 0 (sections 1+2, Mon+Wed, 6:30, 10 min)
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/schedule/0/set" \
  -m '{"enabled":true,"days_mask":5,"hour":6,"minute":30,"duration_sec":600,"section_mask":3,"group_mask":0}'

# Delete entry 0
mosquitto_pub -h 192.168.20.10 -t "wachciodrop/schedule/0/delete" -m ""

# Set group 1
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/group/1/set" \
  -m '{"name":"Lawn","section_mask":7}'

# Turn on group 1 for 15 minutes
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/group/1/command" \
  -m '{"on":true,"duration":900}'

# Force NTP sync
mosquitto_pub -h 192.168.20.10 -t "wachciodrop/time/sntp" -m ""

# Change NTP server
mosquitto_pub -h 192.168.20.10 \
  -t "wachciodrop/settings/set" \
  -m '{"ntp_server":"pool.ntp.org"}'
```
