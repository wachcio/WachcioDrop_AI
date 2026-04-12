# WachcioDrop REST API

All endpoints (except `/api/setup`) require the following header:
```
Authorization: Bearer <token>
```
The token is generated on first boot and displayed on the OLED screen. It can be read or changed via `POST /api/settings`.

Base URL: `http://<ip>/`

---

## Sections

### GET /api/sections
Get the state of all sections and the master valve.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/sections
```

Response:
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
Turn on a section. `id` = 1–8. `duration` field in seconds, `0` = indefinite.
Turning on one section automatically turns off all others (exclusive mode).

```bash
# Turn on section 3 for 5 minutes
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"duration":300}' \
  http://192.168.20.230/api/sections/3/on

# Turn on indefinitely
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/sections/3/on
```

Response:
```json
{"ok": true}
```

---

### POST /api/sections/{id}/off
Turn off a section.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/sections/3/off
```

---

### POST /api/sections/all/off
Turn off all sections immediately.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/sections/all/off
```

---

## System Status

### GET /api/status
General system state including all sections and groups.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/status
```

Response:
```json
{
  "uptime_sec": 3600,
  "ip": "192.168.20.230",
  "rssi": -55,
  "sections_active": 1,
  "master_active": true,
  "irrigation_today": true,
  "ignore_php": false,
  "php_url_set": true,
  "time": "2025-06-15T10:30:00",
  "sections": [
    {
      "id": 1,
      "active": false,
      "started_at": null,
      "ends_at": null,
      "remaining_sec": null
    },
    {
      "id": 2,
      "active": true,
      "started_at": "2025-06-15T10:20:00",
      "ends_at": "2025-06-15T10:30:00",
      "remaining_sec": 600
    }
  ],
  "groups": [
    {
      "id": 1,
      "name": "Lawn",
      "section_mask": 3,
      "active": true,
      "started_at": "2025-06-15T10:20:00",
      "ends_at": "2025-06-15T10:30:00",
      "remaining_sec": 600
    },
    {
      "id": 2,
      "name": "Garden",
      "section_mask": 24,
      "active": false,
      "started_at": null,
      "ends_at": null,
      "remaining_sec": null
    }
  ]
}
```

Status fields:
| Field | Description |
|-------|-------------|
| `irrigation_today` | Whether irrigation is allowed today (PHP check result or manual override) |
| `ignore_php` | Whether the device ignores the PHP script result (always irrigates on schedule) |
| `php_url_set` | Whether a PHP script URL is configured (`false` means `ignore_php` is irrelevant) |

Section/group fields:
| Field | Description |
|-------|-------------|
| `started_at` | Start time (ISO 8601, local time), `null` when inactive |
| `ends_at` | Expected end time, `null` when indefinite or inactive |
| `remaining_sec` | Remaining time in seconds, `null` when indefinite or inactive |

`active` field for groups:
- `true` only when that specific group was activated via `POST /api/groups/{id}/activate`
- Manually turning on any section clears the active group (`active: false` for all groups)
- At most one group can have `active: true` at any given time

---

## Schedule

### GET /api/schedule
Get all 16 schedule entries.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/schedule
```

Response:
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

Fields:
| Field | Description |
|-------|-------------|
| `id` | Entry index 0–15 |
| `enabled` | Whether the entry is active |
| `days_mask` | Days of week: bit0=Mon, bit1=Tue, …, bit6=Sun. `127` = every day |
| `hour` / `minute` | Start time (local time) |
| `duration_sec` | Duration in seconds |
| `section_mask` | Sections: bit0=section1, …, bit7=section8 |
| `group_mask` | Groups: bit0=group1, …, bit9=group10 (expanded to section_mask) |

---

### PUT /api/schedule/{id}
Set a schedule entry. `id` = 0–15.

```bash
# Enable sections 1 and 2 on Monday and Wednesday at 6:30 for 10 minutes
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

`days_mask` examples:
| Value | Days |
|-------|------|
| `1` | Monday |
| `5` | Mon + Wed (bit0 + bit2) |
| `31` | Mon–Fri |
| `96` | Sat + Sun (bit5 + bit6) |
| `127` | Every day |

---

### DELETE /api/schedule/{id}
Clear a schedule entry.

```bash
curl -X DELETE \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/schedule/0
```

---

## Groups

### GET /api/groups
Get all groups.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/groups
```

Response:
```json
[
  {"id": 1, "name": "Lawn",   "section_mask": 7},
  {"id": 2, "name": "Garden", "section_mask": 24},
  ...
]
```

---

### PUT /api/groups/{id}
Set a group. `id` = 1–10.

```bash
curl -X PUT \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name": "Lawn", "section_mask": 7}' \
  http://192.168.20.230/api/groups/1
```

`section_mask`: bit0=section1, …, bit7=section8. Value `7` = sections 1, 2, 3.

---

### POST /api/groups/{id}/activate
Manually activate a group. First turns off all sections (`all_off`), then activates the sections belonging to this group. Only one group can be active at a time.

```bash
# Turn on group 1 for 15 minutes
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"duration": 900}' \
  http://192.168.20.230/api/groups/1/activate
```

Response:
```json
{"ok": true}
```

---

## Irrigation

### POST /api/irrigation
Manually set the `irrigation_today` flag or `ignore_php`. One or both fields can be provided.

```bash
# Force irrigation today (regardless of PHP)
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"irrigation_today": true}' \
  http://192.168.20.230/api/irrigation

# Ignore PHP script result (always irrigate on schedule)
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"ignore_php": true}' \
  http://192.168.20.230/api/irrigation

# Set both fields at once
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"irrigation_today": false, "ignore_php": false}' \
  http://192.168.20.230/api/irrigation
```

Response:
```json
{"ok": true}
```

> Settings are saved to NVS and survive a reboot.

---

## Time

### GET /api/time
Get the current time from the RTC.

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/time
```

Response:
```json
{
  "time": "2025-06-15T10:30:00",
  "unix": 1750000200,
  "tz": 2
}
```

> The `time` field contains **local time** (applies the `tz_offset` from settings). The `unix` field is a UTC timestamp. The `tz` field is the currently configured offset in hours.

---

### POST /api/time
Set the RTC time — three calling variants.

**Variant 1: unix timestamp (UTC)**
```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"unix": 1750000200}' \
  http://192.168.20.230/api/time
```

**Variant 2: datetime string (local time)**
```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"datetime": "2025-06-15T10:30:00"}' \
  http://192.168.20.230/api/time
```

**Variant 3: individual fields (local time)**
```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"year":2025,"month":6,"day":15,"hour":10,"minute":30,"second":0}' \
  http://192.168.20.230/api/time
```

Response (confirmation of set time):
```json
{"ok": true, "time": "2025-06-15T10:30:00", "unix": 1750000200}
```

> Local time uses the Polish timezone with automatic DST (CET/CEST).

---

### POST /api/time/sntp
Force immediate time synchronization from NTP server and save to RTC. Blocks for up to 30s.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/time/sntp
```

Response (success):
```json
{"ok": true, "time": "2025-06-15T10:30:05", "unix": 1750000205}
```

Response (no WiFi):
```json
{"error": "no WiFi connection"}   // HTTP 503
```

Response (timeout):
```json
{"error": "NTP sync timeout"}     // HTTP 504
```

---

## Settings

### GET /api/settings
Get current configuration (passwords and token are not returned).

```bash
curl -H "Authorization: Bearer TOKEN" http://192.168.20.230/api/settings
```

Response:
```json
{
  "wifi_ssid": "MyNetwork",
  "mqtt_uri": "mqtt://192.168.20.10",
  "mqtt_user": "user",
  "php_url": "http://example.com/check.php",
  "ntp_server": "pl.pool.ntp.org",
  "tz_offset": 1
}
```

---

### POST /api/settings
Save configuration. Only changed fields need to be provided.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "wifi_ssid":  "MyNetwork",
    "wifi_pass":  "password123",
    "mqtt_uri":   "mqtt://192.168.20.10",
    "mqtt_user":  "user",
    "mqtt_pass":  "mqttpass",
    "php_url":    "http://example.com/check.php",
    "ntp_server": "pl.pool.ntp.org",
    "api_token":  "newtoken"
  }' \
  http://192.168.20.230/api/settings
```

---

### GET /api/settings/export
Download a full device backup as JSON: configuration (including passwords), schedule (16 entries), groups (10 groups).

```bash
curl -H "Authorization: Bearer TOKEN" \
  http://192.168.20.230/api/settings/export \
  -o backup.json
```

Response:
```json
{
  "config": {
    "wifi_ssid": "MyNetwork",
    "wifi_pass": "password123",
    "mqtt_uri": "mqtt://192.168.20.10",
    "mqtt_user": "user",
    "mqtt_pass": "mqttpass",
    "php_url": "http://example.com/check.php",
    "ntp_server": "pl.pool.ntp.org",
    "api_token": "mytoken",
    "tz_offset": 1
  },
  "schedule": [
    {"id": 0, "enabled": true, "days_mask": 127, "hour": 6, "minute": 30,
     "duration_sec": 600, "section_mask": 3, "group_mask": 0},
    ...
  ],
  "groups": [
    {"id": 1, "name": "Lawn", "section_mask": 7},
    ...
  ]
}
```

---

### PUT /api/settings/import
Restore a backup. Restores configuration, schedule, and groups. Passwords and token are optional — if not provided in JSON, they remain unchanged. Maximum file size: 8192 B.

```bash
curl -X PUT \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d @backup.json \
  http://192.168.20.230/api/settings/import
```

Response:
```json
{"ok": true}
```

> **Note:** After importing a new token or WiFi credentials, restart the device for changes to take effect.

---

## OTA Update

### POST /api/ota
Upload new firmware. The device restarts automatically after a successful upload.

```bash
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @build/irrigation_controller.bin \
  http://192.168.20.230/api/ota
```

---

## Captive Portal Configuration (no authorization required)

### POST /api/setup
Used by the captive portal for initial configuration. Does not require a token.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{
    "wifi_ssid": "MyNetwork",
    "wifi_pass": "password123",
    "mqtt_uri":  "mqtt://192.168.20.10",
    "php_url":   "http://example.com/check.php",
    "api_token": "mytoken"
  }' \
  http://192.168.4.1/api/setup
```
