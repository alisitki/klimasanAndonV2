/*
 * KlimasanAndonV2 - 7-Segment Display Module
 * HC138 + CD4543 ile multiplexed display kontrolü
 * 
 * Display Layout:
 * LD1: 6 digit - SAAT (HH:MM:SS)
 * LD2: 4 digit - DURUŞ SÜRESİ (MM:SS)
 * LD3: 6 digit - ÇALIŞMA ZAMANI (HH:MM:SS)
 * LD4: 6 digit - ATIL ZAMAN (HH:MM:SS)
 * LD5: 6 digit - PLANLI DURUŞ (HH:MM:SS)
 * LD6: 4 digit - HEDEF ADET
 * LD7: 4 digit - GERÇEKLEŞEN ADET
 * LD8: 2 digit - VERİM (%)
 */
#ifndef ANDON_DISPLAY_H
#define ANDON_DISPLAY_H

#include <stdint.h>
#include "esp_err.h"

// Blank display value (CD4543)
#define DISPLAY_BLANK    0x0F

/**
 * @brief Display modülünü başlat
 * @return ESP_OK başarılı
 */
esp_err_t andon_display_init(void);

/**
 * @brief Display scan task'ını başlat
 */
void andon_display_start_task(void);

/**
 * @brief Scan data'yı güncelle (sistem verilerinden)
 */
void andon_display_update(void);


#endif // ANDON_DISPLAY_H
