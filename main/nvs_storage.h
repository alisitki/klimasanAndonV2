/*
 * KlimasanAndonV2 - NVS Storage Module
 * Non-Volatile Storage işlemleri
 */
#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include <stdint.h>
#include "esp_err.h"
#include "system_state.h"

/**
 * @brief NVS modülünü başlat
 * @return ESP_OK başarılı
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief NVS kayıt task'ını başlat
 */
void nvs_storage_start_task(void);

/**
 * @brief Hedef adet'i kaydet
 */
void nvs_storage_save_target(uint32_t target);

/**
 * @brief Hedef adet'i yükle
 */
uint32_t nvs_storage_load_target(void);

/**
 * @brief Cycle target süresini kaydet
 */
void nvs_storage_save_cycle_target(uint32_t seconds);

/**
 * @brief Cycle target süresini yükle
 */
uint32_t nvs_storage_load_cycle_target(void);

/**
 * @brief LED Parlaklık seviyesini (1-5) kaydet/yükle
 */
void nvs_storage_save_brightness(uint8_t level);
uint8_t nvs_storage_load_brightness(void);

/**
 * @brief Sistem durumunu kaydet (async)
 */
void nvs_storage_save_state(void);

/**
 * @brief Sistem durumunu yükle
 */
system_state_backup_t nvs_storage_load_state(void);

/**
 * @brief Sistem durumunu hemen kaydet (blocking)
 */
void nvs_storage_save_state_immediate(void);

#endif // NVS_STORAGE_H
