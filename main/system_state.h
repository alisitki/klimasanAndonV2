/*
 * KlimasanAndonV2 - Sistem Durumu Tanımları
 * Ortak veri yapıları ve global değişkenler
 */
#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>

// ============ Çalışma Modları (State Machine) ============
typedef enum {
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
} system_data_t;

// ============ NVS Backup Yapısı ============
typedef struct {
    uint8_t work_mode;          // MODE_WORK, MODE_IDLE, MODE_PLANNED
    uint8_t shift_state;        // SHIFT_RUNNING, SHIFT_STOPPED
    uint32_t work_t;
    uint32_t idle_t;
    uint32_t planned_t;
    uint32_t prod_cnt;
    uint32_t target_cnt;
    uint32_t cycle_target;
    uint32_t last_upd;
} system_state_backup_t;

// ============ Global Değişkenler (extern) ============
extern volatile system_data_t sys_data;
extern work_mode_t current_mode;
extern shift_state_t shift_state;

#endif // SYSTEM_STATE_H
