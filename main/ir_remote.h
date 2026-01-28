/*
 * KlimasanAndonV2 - IR Remote Module
 * NEC protokolü ile IR kumanda alıcısı
 * 
 * Fonksiyonlar:
 * - Hedef Adet Girme (rakam tuşları)
 * - Hedef Sıfırlama
 * - Cycle Süresi Girme
 * - Ekran Reset
 * - Vardiya Durdur
 * - Alarm Kabul
 * - Yetkili Giriş (PIN)
 */
#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#include <stdint.h>
#include "esp_err.h"

// IR giriş modları
typedef enum {
    IR_INPUT_NONE,
    IR_INPUT_TARGET,        // Hedef adet girişi
    IR_INPUT_CYCLE_TIME,    // Cycle süresi girişi
    IR_INPUT_PIN,           // Yetkili PIN girişi
} ir_input_mode_t;

/**
 * @brief IR alıcı modülünü başlat
 */
esp_err_t ir_remote_init(void);

/**
 * @brief IR alıcı task'ını başlat
 */
void ir_remote_start_task(void);

/**
 * @brief IR komut callback ayarla
 */
typedef void (*ir_command_callback_t)(uint8_t address, uint8_t command);
void ir_remote_set_callback(ir_command_callback_t callback);

/**
 * @brief IR giriş modunu al
 */
ir_input_mode_t ir_remote_get_input_mode(void);

/**
 * @brief IR giriş modunu ayarla
 */
void ir_remote_set_input_mode(ir_input_mode_t mode);

/**
 * @brief Giriş buffer'ını sıfırla
 */
void ir_remote_clear_input(void);

/**
 * @brief Giriş değerini al
 */
uint32_t ir_remote_get_input_value(void);

#endif // IR_REMOTE_H
