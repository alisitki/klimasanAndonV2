/*
 * KlimasanAndonV2 - LED Strip Module
 * Cycle Bar - WS2812B LED strip kontrolü
 */
#ifndef LED_STRIP_H
#define LED_STRIP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ============ Cycle Bar Renk Eşikleri ============
// 0.0 - 0.7  : Yeşil
// 0.7 - 0.9  : Turuncu
// 0.9 - 1.0  : Kırmızı
// > 1.0      : Kırmızı + Alarm

// ============ Varsayılan Değerler ============
#define DEFAULT_CYCLE_TARGET_SEC    60      // Varsayılan cycle süresi (saniye)
#define FRAME_MS                    33      // Render periyodu (30 FPS - Flicker engellemek için)

// ============ Fonksiyonlar ============

/**
 * @brief LED strip modülünü başlat
 * @return ESP_OK başarılı
 */
esp_err_t led_strip_init(void);

/**
 * @brief LED strip task'ını başlat
 */
void led_strip_start_task(void);

/**
 * @brief Parlaklık ayarla (0.0 - 1.0)
 */
void led_strip_set_brightness(float brightness);

/**
 * @brief Cycle'ı başlat/sıfırla
 * Turuncu buton basıldığında çağrılır
 */
void led_strip_start_cycle(void);

/**
 * @brief Cycle hedef süresini ayarla
 * @param seconds Hedef süre (saniye)
 */
void led_strip_set_cycle_target(uint32_t seconds);

/**
 * @brief Cycle hedef süresini al
 * @return Hedef süre (saniye)
 */
uint32_t led_strip_get_cycle_target(void);

/**
 * @brief Alarm durumunu kontrol et
 * @return true alarm aktif
 */
bool led_strip_is_alarm_active(void);

/**
 * @brief Alarmı kapat (IR kumanda ile)
 */
void led_strip_acknowledge_alarm(void);

/**
 * @brief Tüm LED'leri söndür
 */
void led_strip_clear(void);

#endif // LED_STRIP_H
