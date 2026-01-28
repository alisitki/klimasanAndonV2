/*
 * KlimasanAndonV2 - DS1307 RTC Module
 * I2C üzerinden gerçek zaman saati kontrolü
 */
#include <time.h>
#include <stdbool.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rtc_ds1307.h"
#include "pin_config.h"

static const char *TAG = "rtc_ds1307";
static bool ds1307_available = false;

// ============ I2C Register Functions ============

static esp_err_t ds1307_read_register(uint8_t reg, uint8_t *value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t ds1307_write_register(uint8_t reg, uint8_t value) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void ds1307_start_if_halted(void) {
    uint8_t sec_reg = 0;
    esp_err_t ret = ds1307_read_register(0x00, &sec_reg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DS1307 second read failed: %s", esp_err_to_name(ret));
        return;
    }

    // CH bit (bit 7) = 1 means clock is halted
    if ((sec_reg & 0x80U) != 0U) {
        ESP_LOGW(TAG, "DS1307 CH bit set (0x%02X) -> resetting seconds", sec_reg);
        uint8_t new_sec = 0x00;  // 00 seconds, CH=0
        ret = ds1307_write_register(0x00, new_sec);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "DS1307 oscillator started, seconds set to 00");
        } else {
            ESP_LOGE(TAG, "DS1307 CH bit clear failed: %s", esp_err_to_name(ret));
        }
    }
}

static uint8_t bcd_to_bin(uint8_t value) {
    return ((value >> 4) * 10U) + (value & 0x0FU);
}

static uint8_t bin_to_bcd(uint8_t value) {
    return ((value / 10U) << 4) | (value % 10U);
}

// ============ Public Functions ============

esp_err_t rtc_ds1307_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    
    i2c_param_config(I2C_NUM_0, &conf);
    esp_err_t ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        ds1307_available = false;
        return ret;
    }
    
    ESP_LOGI(TAG, "I2C initialized");

    ds1307_start_if_halted();

    time_t ds_now = 0;
    if (rtc_ds1307_get_epoch(&ds_now) == ESP_OK) {
        ds1307_available = true;
        struct tm tm_buf;
        localtime_r(&ds_now, &tm_buf);
        ESP_LOGI(TAG, "DS1307 RTC ready (epoch=%lld, %04d-%02d-%02d %02d:%02d:%02d)",
                 (long long)ds_now,
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1,
                 tm_buf.tm_mday,
                 tm_buf.tm_hour,
                 tm_buf.tm_min,
                 tm_buf.tm_sec);
    } else {
        ds1307_available = false;
        ESP_LOGW(TAG, "DS1307 RTC not detected, falling back to system time");
    }
    
    return ESP_OK;
}

bool rtc_ds1307_is_available(void) {
    return ds1307_available;
}

esp_err_t rtc_ds1307_read_tm(struct tm *out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[7] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);  // register pointer
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, raw, 6, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, raw + 6, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t hour_reg = raw[2];
    uint8_t hour_dec;
    if (hour_reg & 0x40U) {
        // 12-hour format
        hour_dec = bcd_to_bin(hour_reg & 0x1FU);
        bool pm_flag = (hour_reg & 0x20U) != 0;
        if (hour_dec == 12U) {
            hour_dec = pm_flag ? 12U : 0U;
        } else if (pm_flag) {
            hour_dec = (hour_dec + 12U) % 24U;
        }
    } else {
        // 24-hour format
        hour_dec = bcd_to_bin(hour_reg & 0x3FU);
    }

    struct tm tm_snapshot = {
        .tm_sec = bcd_to_bin(raw[0] & 0x7FU),
        .tm_min = bcd_to_bin(raw[1] & 0x7FU),
        .tm_hour = hour_dec,
        .tm_mday = bcd_to_bin(raw[4] & 0x3FU),
        .tm_mon = bcd_to_bin(raw[5] & 0x1FU) - 1,
        .tm_year = bcd_to_bin(raw[6]) + 100,  // DS1307 stores 0-99 → 2000+
        .tm_isdst = -1,
    };

    *out = tm_snapshot;
    return ESP_OK;
}

esp_err_t rtc_ds1307_get_epoch(time_t *epoch_out) {
    if (epoch_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm tm_snapshot = {0};
    esp_err_t ret = rtc_ds1307_read_tm(&tm_snapshot);
    if (ret != ESP_OK) {
        return ret;
    }

    time_t epoch = mktime(&tm_snapshot);
    if (epoch == (time_t)-1) {
        return ESP_FAIL;
    }

    *epoch_out = epoch;
    return ESP_OK;
}

uint32_t rtc_get_wall_time_seconds(void) {
    time_t epoch = 0;
    if (ds1307_available) {
        if (rtc_ds1307_get_epoch(&epoch) == ESP_OK) {
            return (uint32_t)epoch;
        }
        ESP_LOGW(TAG, "DS1307 read failed, falling back to system time");
        ds1307_available = false;
    }
    epoch = time(NULL);
    return (uint32_t)epoch;
}

esp_err_t rtc_ds1307_set_time(uint8_t hours, uint8_t minutes) {
    if (hours > 23 || minutes > 59) return ESP_ERR_INVALID_ARG;
    
    // Sadece saat ve dakikayı güncelliyoruz, saniyeyi 00 yapıyoruz
    esp_err_t ret;
    ret = ds1307_write_register(0x00, 0x00); // Seconds = 00, CH = 0
    if (ret != ESP_OK) return ret;
    
    ret = ds1307_write_register(0x01, bin_to_bcd(minutes));
    if (ret != ESP_OK) return ret;
    
    ret = ds1307_write_register(0x02, bin_to_bcd(hours)); // 24-hour mode implicit
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "RTC time set to %02d:%02d", hours, minutes);
    return ESP_OK;
}
