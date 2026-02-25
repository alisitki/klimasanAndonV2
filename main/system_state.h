/*
 * KlimasanAndonV2 - Sistem Durumu Tanımları
 * Ortak veri yapıları ve global değişkenler
 */
#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

// ============ Çalışma Modları (State Machine) ============
typedef enum {
    MODE_STANDBY,   // Bekleme modu - Hiçbir şey saymaz (açılış varsayılanı)
    MODE_WORK,      // Çalışma modu - work_time sayar
    MODE_IDLE,      // Atıl mod - idle_time sayar
    MODE_PLANNED,   // Planlı duruş - planned_time sayar
} work_mode_t;

// ============ Vardiya Durumu ============
typedef enum {
    SHIFT_RUNNING = 0,  // Vardiya aktif
    SHIFT_STOPPED = 1,  // Vardiya durduruldu (ekran donuk)
} shift_state_t;

// ============ Sistem Verileri ============
typedef struct {
    // Zaman sayaçları (saniye)
    uint32_t work_time;         // Çalışma zamanı
    uint32_t idle_time;         // Atıl zaman
    uint32_t planned_time;      // Planlı duruş
    
    // Duruş süresi (WORK dışında çalışır)
    uint32_t durus_time;        // Mevcut duruş süresi (saniye)
    uint32_t durus_start_epoch; // Duruş başlangıç zamanı (epoch)
    bool durus_running;         // Duruş sayacı çalışıyor mu
    
    // Adet sayaçları
    uint32_t target_count;      // Hedef adet
    uint32_t produced_count;    // Gerçekleşen adet
    
    // Cycle bar (LED strip)
    uint32_t cycle_target_seconds;  // IR'dan alınan hedef süre
    uint32_t cycle_start_epoch;     // Cycle başlangıç zamanı
    bool cycle_running;             // Cycle aktif mi
    bool cycle_alarm_active;        // Alarm çalıyor mu
    
    // RTC saat (epoch)
    uint32_t current_epoch;     // Mevcut zaman (RTC'den)
    
    // Ekran durumu
    bool screen_on;             // Ekran açık/kapalı
    bool counting_active;       // Sayaçlar aktif mi (buton basılınca true)
    
    // Saat ayarı modu yardımcıları
    uint8_t clock_step;         // 0: mod kapalı, 1: saat seçili, 2: dakika seçili
    uint8_t clock_hours;
    uint8_t clock_minutes;
    uint8_t clock_backup_hours;   // Reversion için yedek
    uint8_t clock_backup_minutes; // Reversion için yedek
    bool clock_blink_on;        // Yan-sön durumu
    
    // Menü ayarları yardımcıları
    uint8_t menu_step;          // 0:Kapalı, 1:Parlaklık, 2:Süre
    uint8_t led_brightness_idx; // 1-5 arası parlaklık seviyesi
} system_data_t;

// ============ NVS Backup Yapısı ============
typedef struct {
    bool valid;                 // Veri başarıyla yüklendi mi?
    uint8_t work_mode;          // MODE_WORK, MODE_IDLE, MODE_PLANNED
    uint8_t shift_state;        // SHIFT_RUNNING, SHIFT_STOPPED
    uint32_t work_t;
    uint32_t idle_t;
    uint32_t planned_t;
    uint32_t prod_cnt;
    uint32_t target_cnt;
    uint32_t cycle_target;
    uint32_t durus_t;           // Duruş süresi yedeği
    uint32_t last_upd;
} system_state_backup_t;

// ============ Global Değişkenler (extern) ============
extern volatile system_data_t sys_data;
extern work_mode_t current_mode;
extern shift_state_t shift_state;
extern portMUX_TYPE sys_data_mux;

#endif // SYSTEM_STATE_H
