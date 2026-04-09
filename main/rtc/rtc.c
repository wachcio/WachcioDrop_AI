#include "rtc.h"
#include "config.h"
#include "esp_log.h"
#include "ds3231.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

// timegm nie istnieje w newlib — własna implementacja UTC struct tm → unix timestamp
static time_t utc_mktime(struct tm *tm)
{
    // Skopiuj aktualną TZ żeby ją przywrócić
    char tz_save[64] = {0};
    const char *tz = getenv("TZ");
    if (tz) strncpy(tz_save, tz, sizeof(tz_save) - 1);

    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(tm);

    if (tz_save[0]) setenv("TZ", tz_save, 1);
    else            unsetenv("TZ");
    tzset();
    return t;
}

static const char *TAG = "rtc";
static i2c_dev_t s_dev;
static bool      s_available = false;  // false = DS3231 nie odpowiada, pomijaj kolejne próby

esp_err_t rtc_ds3231_init(void)
{
    esp_err_t err = i2cdev_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2cdev_init failed: %s", esp_err_to_name(err));
        return err;
    }

    memset(&s_dev, 0, sizeof(s_dev));
    err = ds3231_init_desc(&s_dev, I2C_PORT, PIN_I2C_SDA, PIN_I2C_SCL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ds3231_init_desc failed: %s - RTC disabled", esp_err_to_name(err));
        return ESP_OK;  // nie blokuj bootu gdy DS3231 nieobecny
    }

    // Ustaw systemowy czas z DS3231 przy starcie
    // DS3231 przechowuje UTC — używamy timegm (nie mktime) żeby uniknąć
    // podwójnej korekcji strefy czasowej
    struct tm t = {0};
    err = ds3231_get_time(&s_dev, &t);
    if (err == ESP_OK) {
        s_available = true;
        struct timeval tv = { .tv_sec = utc_mktime(&t), .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "time loaded from DS3231: %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        ESP_LOGW(TAG, "DS3231 not available (%s) - using system time only",
                 esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "init OK (SDA=%d SCL=%d)", PIN_I2C_SDA, PIN_I2C_SCL);
    return ESP_OK;
}

esp_err_t rtc_get_time(struct tm *out_tm)
{
    if (!s_available) {
        // Fallback: użyj czasu systemowego (może być nieaktualny bez DS3231)
        time_t now = time(NULL);
        localtime_r(&now, out_tm);
        return ESP_OK;
    }
    esp_err_t err = ds3231_get_time(&s_dev, out_tm);
    if (err != ESP_OK) {
        // Fallback na czas systemowy jeśli DS3231 przestał odpowiadać
        time_t now = time(NULL);
        localtime_r(&now, out_tm);
    }
    return err;
}

esp_err_t rtc_set_time(const struct tm *tm)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    struct tm writable = *tm;
    esp_err_t err = ds3231_set_time(&s_dev, &writable);
    if (err == ESP_OK) {
        // tm zawiera UTC — timegm konwertuje UTC struct tm → unix timestamp
        struct timeval tv = { .tv_sec = utc_mktime(&writable), .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "time set: %04d-%02d-%02d %02d:%02d:%02d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    return err;
}

esp_err_t rtc_set_time_unix(time_t t)
{
    struct tm *tm = gmtime(&t);
    return rtc_set_time(tm);
}

time_t rtc_get_unix(void)
{
    if (!s_available) return time(NULL);
    struct tm t = {0};
    if (ds3231_get_time(&s_dev, &t) != ESP_OK) return time(NULL);
    return mktime(&t);
}

esp_err_t rtc_get_temperature(int16_t *temp_hundredths)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    float temp = 0.0f;
    esp_err_t err = ds3231_get_temp_float(&s_dev, &temp);
    if (err == ESP_OK) {
        *temp_hundredths = (int16_t)(temp * 100);
    }
    return err;
}
