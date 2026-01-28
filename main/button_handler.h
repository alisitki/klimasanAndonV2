/*
 * KlimasanAndonV2 - Button Handler Module
 * 4 buton: Yeşil (WORK), Kırmızı (IDLE), Sarı (PLANNED), Turuncu (Adet+1)
 */
#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdint.h>
#include "esp_err.h"

// Buton event türleri
typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_GREEN,     // Yeşil - WORK moduna geç
    BUTTON_EVENT_RED,       // Kırmızı - IDLE moduna geç
    BUTTON_EVENT_YELLOW,    // Sarı - PLANNED moduna geç
    BUTTON_EVENT_ORANGE,    // Turuncu - Adet +1
} button_event_t;

// Buton callback tipi
typedef void (*button_callback_t)(button_event_t event);

/**
 * @brief Buton modülünü başlat
 * @return ESP_OK başarılı
 */
esp_err_t button_handler_init(void);

/**
 * @brief Buton task'ını başlat
 */
void button_handler_start_task(void);

/**
 * @brief Buton callback ayarla
 * @param callback Buton basıldığında çağrılacak fonksiyon
 */
void button_handler_set_callback(button_callback_t callback);

#endif // BUTTON_HANDLER_H
