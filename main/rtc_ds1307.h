/*
 * KlimasanAndonV2 - DS1307 RTC Module
 * I2C üzerinden gerçek zaman saati kontrolü
 */
#ifndef RTC_DS1307_H
#define RTC_DS1307_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

/**
 * @brief RTC modülünü başlat (I2C)
 * @return ESP_OK başarılı
 */
esp_err_t rtc_ds1307_init(void);

/**
 * @brief RTC'nin kullanılabilir olup olmadığını kontrol et
 * @return true kullanılabilir
 */
bool rtc_ds1307_is_available(void);

/**
 * @brief RTC'den epoch zamanını oku
 * @param epoch_out Çıktı epoch değeri
 * @return ESP_OK başarılı
 */
esp_err_t rtc_ds1307_get_epoch(time_t *epoch_out);

/**
 * @brief RTC'den struct tm oku
 * @param out Çıktı tm yapısı
 * @return ESP_OK başarılı
 */
esp_err_t rtc_ds1307_read_tm(struct tm *out);

/**
 * @brief Wall time (saniye cinsinden) al
 * RTC varsa RTC'den, yoksa sistem zamanından
 * @return Epoch saniye
 */
uint32_t rtc_get_wall_time_seconds(void);

/**
 * @brief RTC zamanını ayarla
 * @param hours Saat (0-23)
 * @param minutes Dakika (0-59)
 * @return ESP_OK başarılı
 */
esp_err_t rtc_ds1307_set_time(uint8_t hours, uint8_t minutes);

#endif // RTC_DS1307_H
