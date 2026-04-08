#include "rtc.h"
#include "config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "ds3231.h"

static const char *TAG = "rtc";
static i2c_dev_t s_dev;

esp_err_t rtc_init(void)
{
    esp_err_t err = i2cdev_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2cdev_init failed: %s", esp_err_to_name(err));
        return err;
    }

    memset(&s_dev, 0, sizeof(s_dev));
    err = ds3231_init_desc(&s_dev, I2C_PORT, PIN_I2C_SDA, PIN_I2C_SCL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ds3231_init_desc failed: %s", esp_err_to_name(err));
        return err;
    }

    // Ustaw systemowy czas z DS3231 przy starcie
    struct tm t = {0};
    err = ds3231_get_time(&s_dev, &t);
    if (err == ESP_OK) {
        struct timeval tv = { .tv_sec = mktime(&t), .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "time loaded from DS3231: %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        ESP_LOGW(TAG, "DS3231 read failed, using default time: %s",
                 esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "init OK (SDA=%d SCL=%d)", PIN_I2C_SDA, PIN_I2C_SCL);
    return ESP_OK;
}

esp_err_t rtc_get_time(struct tm *out_tm)
{
    return ds3231_get_time(&s_dev, out_tm);
}

esp_err_t rtc_set_time(const struct tm *tm)
{
    struct tm writable = *tm;
    esp_err_t err = ds3231_set_time(&s_dev, &writable);
    if (err == ESP_OK) {
        struct timeval tv = { .tv_sec = mktime(&writable), .tv_usec = 0 };
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
    struct tm t = {0};
    if (ds3231_get_time(&s_dev, &t) != ESP_OK) return 0;
    return mktime(&t);
}

esp_err_t rtc_get_temperature(int16_t *temp_hundredths)
{
    float temp = 0.0f;
    esp_err_t err = ds3231_get_temp_float(&s_dev, &temp);
    if (err == ESP_OK) {
        *temp_hundredths = (int16_t)(temp * 100);
    }
    return err;
}
