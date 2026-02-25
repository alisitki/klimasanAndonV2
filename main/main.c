/*
 * KlimasanAndonV2 - Ana Uygulama
 * 
 * State Machine:
 * - MODE_WORK: Çalışma zamanı sayar
 * - MODE_IDLE: Atıl zaman sayar
 * - MODE_PLANNED: Planlı duruş sayar
 * 
 * Butonlar:
 * - Yeşil: WORK moduna geç
 * - Kırmızı: IDLE moduna geç
 * - Sarı: PLANNED moduna geç
 * - Turuncu: Adet +1 (sadece WORK modunda)
 */
#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Modüller
#include "system_state.h"
#include "pin_config.h"
#include "andon_display.h"
#include "led_strip.h"
#include "rtc_ds1307.h"
#include "ir_remote.h"
#include "button_handler.h"
#include "nvs_storage.h"

static const char *TAG = "klimasan_main";

// ============ Global Değişken Tanımları ============
volatile system_data_t sys_data = {0};
work_mode_t current_mode = MODE_STANDBY;
shift_state_t shift_state = SHIFT_RUNNING;
portMUX_TYPE sys_data_mux = portMUX_INITIALIZER_UNLOCKED;

// ============ Duruş Süresi Yönetimi ============
// WORK dışındaki modlarda çalışır, WORK'e geçince donar

static void start_durus_timer(void) {
    sys_data.durus_running = true;
    ESP_LOGI(TAG, "Duruş timer started");
}

static void stop_durus_timer(void) {
    if (sys_data.durus_running) {
        sys_data.durus_running = false;
        ESP_LOGI(TAG, "Duruş timer stopped: %lu sec (frozen)", (unsigned long)sys_data.durus_time);
    }
}

static void update_durus_timer(void) {
    if (sys_data.durus_running) {
        sys_data.durus_time++;  // Basit artış, jump engeller
    }
}

// ============ Mode Değişim Fonksiyonları ============

static void switch_to_work_mode(void) {
    // Sayaçları aktif et (her ihtimale karşı zorla)
    sys_data.counting_active = true;

    if (current_mode == MODE_WORK) {
        andon_display_update();
        return;
    }
    
    // Duruş timer'ı durdur (frozen value)
    stop_durus_timer();
    
    current_mode = MODE_WORK;
    led_strip_clear(); // WORK'e geçince LED barı söndür (adet gelince başlayacak)
    ESP_LOGI(TAG, "🟢 MODE: WORK (Çalışma zamanı sayılıyor)");
    nvs_storage_save_state_immediate();
    andon_display_update();
}

static void switch_to_idle_mode(void) {
    // Sayaçları aktif et
    sys_data.counting_active = true;

    if (current_mode == MODE_IDLE) {
        andon_display_update();
        return;
    }
    
    // Duruş timer'ı başlat (eğer daha önce WORK'te idiysek veya Standby'da isek)
    if (current_mode == MODE_WORK || current_mode == MODE_STANDBY) {
        sys_data.durus_time = 0;  // Yeni duruş, sıfırdan başla
    }
    start_durus_timer();
    
    current_mode = MODE_IDLE;
    ESP_LOGI(TAG, "🔴 MODE: IDLE (Atıl zaman sayılıyor)");
    nvs_storage_save_state_immediate();
    andon_display_update();
}

static void switch_to_planned_mode(void) {
    // Sayaçları aktif et
    sys_data.counting_active = true;

    if (current_mode == MODE_PLANNED) {
        andon_display_update();
        return;
    }
    
    // Duruş timer'ı başlat (eğer daha önce WORK'te idiysek veya Standby'da isek)
    if (current_mode == MODE_WORK || current_mode == MODE_STANDBY) {
        sys_data.durus_time = 0;  // Yeni duruş, sıfırdan başla
    }
    start_durus_timer();
    
    current_mode = MODE_PLANNED;
    ESP_LOGI(TAG, "🟡 MODE: PLANNED (Planlı duruş sayılıyor)");
    nvs_storage_save_state_immediate();
    andon_display_update();
}

// ============ Timer Task (her saniye) ============
static void timer_task(void *pvParameters) {
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        
        // Ekran kapalıysa hiçbir sayaç artmaz
        if (!sys_data.screen_on) {
            continue;
        }
        
        // Sayaçlar aktif değilse (veya bekleme modundaysak) sadece display güncelle
        if (!sys_data.counting_active || current_mode == MODE_STANDBY) {
            andon_display_update();
            continue;
        }
        
        // Vardiya durdurulmuşsa hiçbir sayaç artmaz
        if (shift_state == SHIFT_STOPPED) {
            andon_display_update();
            continue;
        }
        
        // Mevcut moda göre ilgili sayaç artar (spinlock korumalı)
        taskENTER_CRITICAL(&sys_data_mux);
        switch (current_mode) {
            case MODE_STANDBY:
                // Standby modunda hiçbir şey sayılmaz
                break;
            case MODE_WORK:
                sys_data.work_time++;
                break;
            case MODE_IDLE:
                sys_data.idle_time++;
                update_durus_timer();
                break;
            case MODE_PLANNED:
                sys_data.planned_time++;
                update_durus_timer();
                break;
        }
        taskEXIT_CRITICAL(&sys_data_mux);
        
        // Display güncelle
        andon_display_update();
        
        // Periyodik kayıt (60 saniyede bir - Flash ömrü için)
        static uint8_t save_counter = 0;
        if (++save_counter >= 60) {
            save_counter = 0;
            nvs_storage_save_state();
        }
    }
}

// ============ Buton Callback ============
static void on_button_event(button_event_t event) {
    switch (event) {
        case BUTTON_EVENT_GREEN:
            // Yeşil buton: WORK moduna geç
            switch_to_work_mode();
            break;
            
        case BUTTON_EVENT_RED:
            // Kırmızı buton: IDLE moduna geç
            switch_to_idle_mode();
            break;
            
        case BUTTON_EVENT_YELLOW:
            // Sarı buton: PLANNED moduna geç
            switch_to_planned_mode();
            break;
            
        case BUTTON_EVENT_ORANGE:
            // Turuncu buton: Adet +1 (sadece WORK modunda)
            if (current_mode == MODE_WORK) {
                taskENTER_CRITICAL(&sys_data_mux);
                sys_data.produced_count++;
                taskEXIT_CRITICAL(&sys_data_mux);
                ESP_LOGI(TAG, "🟠 Adet: %lu / %lu", 
                         (unsigned long)sys_data.produced_count, (unsigned long)sys_data.target_count);
                
                // Cycle bar'ı başlat/sıfırla
                led_strip_start_cycle();
                
                nvs_storage_save_state();
                andon_display_update();
            } else {
                ESP_LOGW(TAG, "Turuncu buton IDLE/PLANNED modda çalışmaz");
            }
            break;
            
        default:
            break;
    }
}

// ============ IR Komut Callback ============

// Helper: Rakam tuşunu decode et
static int8_t decode_ir_digit(uint8_t address, uint8_t command) {
    // Standart kumanda
    if (address != 0xFF && address != 0xFE) {
        if (address == 0xEE) return 1;
        if (address == 0xED) return 2;
        if (address == 0xEC) return 3;
        if (address == 0xEB) return 4;
        if (address == 0xEA) return 5;
        if (address == 0xE9) return 6;
        if (address == 0xE8) return 7;
        if (address == 0xE7) return 8;
        if (address == 0xE6) return 9;
        if (address == 0xEF) return 0;
    }
    // Non-standard kumanda
    if (address == 0xFF) {
        if (command == 0x07) return 1;
        if (command == 0x15) return 2;
        if (command == 0x0D) return 3;
        if (command == 0x0C) return 4;
        if (command == 0x18) return 5;
        if (command == 0x5E) return 6;
        if (command == 0x08) return 7;
        if (command == 0x1C) return 8;
        if (command == 0x5A) return 9;
        if (command == 0x52) return 0;
    }
    return -1;  // Rakam değil
}

static void on_ir_command(uint8_t address, uint8_t command) {
    ESP_LOGI(TAG, "IR: Addr=0x%02X, Cmd=0x%02X", address, command);
    
    ir_input_mode_t input_mode = ir_remote_get_input_mode();

    // ========== MENÜ/SAAT AYARI LOCKOUT ==========
    // Eğer LED Menü modundaysak, sadece LED ayar tuşlarını işle
    if (sys_data.menu_step > 0) {
        bool allowed = false;
        if (address == 0xFD && command == 0x1D) allowed = true; // Menu tuşu (kendi tuşu)
        if ((address == 0xFF && command == 0x02) || (address == 0xFE)) allowed = true; // MUTE (sıfırlama)
        if (address == 0xFA && command == 0x1D) allowed = true; // UP
        if (address == 0xF9 && command == 0x1D) allowed = true; // DOWN
        if (decode_ir_digit(address, command) >= 0) allowed = true; // Rakamlar
        
        if (!allowed) {
            ESP_LOGW(TAG, "IR: LED Menü modunda bu komut engellendi (Addr:0x%02X, Cmd:0x%02X)", address, command);
            return;
        }
    }
    // Eğer Saat Ayarı modundaysak, sadece Saat ayar tuşlarını işle
    else if (sys_data.clock_step > 0) {
        bool allowed = false;
        if (address == 0xFB && command == 0x1D) allowed = true; // Saat Ayarı tuşu (kendi tuşu)
        if (decode_ir_digit(address, command) >= 0) allowed = true; // Rakamlar
        if ((address == 0xFF && command == 0xF0) || (address == 0xF0)) allowed = true; // OK tuşu (bazı durumlarda çıkış için)

        if (!allowed) {
            ESP_LOGW(TAG, "IR: Saat Ayarı modunda bu komut engellendi (Addr:0x%02X, Cmd:0x%02X)", address, command);
            return;
        }
    }
    
    // Rakam girişi modunda
    int8_t digit = decode_ir_digit(address, command);
    if (digit >= 0 && sys_data.screen_on) {
        if (input_mode == IR_INPUT_CLOCK) {
            // SAAT AYARI MODU
            if (sys_data.clock_step == 1) {
                // Saat hanesi (Artık kısıtlama yok, tamamlanınca bakılacak)
                sys_data.clock_hours = (sys_data.clock_hours % 10) * 10 + digit;
                ESP_LOGI(TAG, "Clock Entry: Hour = %02d (Validation at step-end)", sys_data.clock_hours);
            } else if (sys_data.clock_step == 2) {
                // Dakika hanesi (Artık kısıtlama yok, tamamlanınca bakılacak)
                sys_data.clock_minutes = (sys_data.clock_minutes % 10) * 10 + digit;
                ESP_LOGI(TAG, "Clock Entry: Minute = %02d (Validation at end)", sys_data.clock_minutes);
            }
        } else if (input_mode == IR_INPUT_MENU_BRIGHT) {
            // Parlaklık Ayarı modundayken yukarı/aşağı kullanılır (rakam ignored)
            ESP_LOGW(TAG, "Rakam ignored in Brightness mode. Use UP/DOWN.");
        } else if (input_mode == IR_INPUT_MENU_TIME) {
            // LED Süre modundayken rakamlara basarak ayarlanır
            uint32_t val = led_strip_get_cycle_target();
            val = (val % 100000) * 10 + digit;
            led_strip_set_cycle_target(val);
            ESP_LOGI(TAG, "Menu LED Time Entry: %lu", (unsigned long)val);
        } else if (input_mode == IR_INPUT_CYCLE_TIME) {
            // Cycle Süresi modundayken buraya girer
            uint32_t val = led_strip_get_cycle_target();
            val = (val % 1000) * 10 + digit;
            led_strip_set_cycle_target(val);
            nvs_storage_save_cycle_target(val);
            ESP_LOGI(TAG, "Cycle Target: %lu sec", (unsigned long)val);
        } else {
            // Standart: Hedef Adet'i güncelle
            uint32_t val = sys_data.target_count;
            val = (val % 1000) * 10 + digit;
            sys_data.target_count = val;
            nvs_storage_save_target(val);
            ESP_LOGI(TAG, "Hedef Adet (Hızlı Giriş): %lu", (unsigned long)val);
        }
        andon_display_update();
        return;
    }
    
    // === Özel Komutlar ===
    
    // ========== EKRAN AÇ/KAPA (ON/OFF) ==========
    // 0xFF, 0x1D → Toggle screen on/off
    if (address == 0xFF && command == 0x1D) {
        if (sys_data.screen_on) {
            // Ekranı KAPAT
            sys_data.screen_on = false;
            sys_data.counting_active = false;
            led_strip_clear(); // Ekran kapanınca LED barı da söndür
            ESP_LOGI(TAG, "📴 EKRAN KAPANDI");
        } else {
            // Ekranı AÇ - tüm değerler sıfırlanır, hiçbir süre saymaz
            sys_data.screen_on = true;
            sys_data.counting_active = false;  // Buton basılana kadar sayma
            sys_data.work_time = 0;
            sys_data.idle_time = 0;
            sys_data.planned_time = 0;
            sys_data.produced_count = 0;
            sys_data.durus_time = 0;
            sys_data.durus_running = false;
            current_mode = MODE_STANDBY;
            
            // Hedef adet NVS'den yükle
            sys_data.target_count = nvs_storage_load_target();
            
            led_strip_clear();
            ESP_LOGI(TAG, "📱 EKRAN AÇILDI - Hedef: %lu (sayaçlar beklemede)", (unsigned long)sys_data.target_count);
        }
        nvs_storage_save_state_immediate();
        andon_display_update();
        return;
    }
    
    // Ekran kapalıysa diğer komutları işleme
    if (!sys_data.screen_on) {
        ESP_LOGW(TAG, "Ekran kapalı - komut ignored");
        return;
    }

    // ========== MENU TUŞU (LED AYARLARI) ==========
    // 0xFD, 0x1D → Menu tuşu
    if (address == 0xFD && command == 0x1D) {
        if (sys_data.clock_step > 0) {
            ESP_LOGW(TAG, "Saat ayarı modundayken LED Menüye girilemez");
            return;
        }
        if (sys_data.menu_step == 0) {
            // Normalden -> Parlaklık Ayarına
            sys_data.menu_step = 1;
            ir_remote_set_input_mode(IR_INPUT_MENU_BRIGHT);
            led_strip_set_menu_preview(true);
            ESP_LOGI(TAG, "IR: Menu -> LED Parlaklık Ayarı");
        } else if (sys_data.menu_step == 1) {
            // Parlaklıktan -> Süre Ayarına
            sys_data.menu_step = 2;
            ir_remote_set_input_mode(IR_INPUT_MENU_TIME);
            led_strip_set_menu_preview(true); // Preview stays true during time adjustment
            ESP_LOGI(TAG, "IR: Menu -> LED Süre Ayarı");
        } else {
            // Süreden -> Çıkış ve Kaydet
            nvs_storage_save_brightness(sys_data.led_brightness_idx);
            nvs_storage_save_cycle_target(led_strip_get_cycle_target());
            sys_data.menu_step = 0;
            ir_remote_set_input_mode(IR_INPUT_NONE);
            led_strip_set_menu_preview(false); // Only now turn off preview
            ESP_LOGI(TAG, "IR: Menu -> Ayarlar Kaydedildi ve Çıkıldı");
        }
        andon_display_update();
        return;
    }

    // ========== YUKARI / AŞAĞI TUŞLARI (Parlaklık için) ==========
    if (sys_data.menu_step == 1) {
        if (address == 0xFA && command == 0x1D) { // YUKARI
            if (sys_data.led_brightness_idx < 4) sys_data.led_brightness_idx++;
            led_strip_set_brightness_idx(sys_data.led_brightness_idx);
            ESP_LOGI(TAG, "IR: Parlaklık Artırıldı: %d", sys_data.led_brightness_idx);
            andon_display_update();
            return;
        }
        if (address == 0xF9 && command == 0x1D) { // AŞAĞI
            if (sys_data.led_brightness_idx > 1) sys_data.led_brightness_idx--;
            led_strip_set_brightness_idx(sys_data.led_brightness_idx);
            ESP_LOGI(TAG, "IR: Parlaklık Azaltıldı: %d", sys_data.led_brightness_idx);
            andon_display_update();
            return;
        }
    }
    
    // ========== IR BUTON → MOD DEĞİŞİMİ ==========
    // 0xDA, 0x1D → Yeşil → WORK modu
    if (address == 0xDA && command == 0x1D) {
        switch_to_work_mode();
        ESP_LOGI(TAG, "IR: Yeşil → WORK modu");
        return;
    }
    
    // 0xDB, 0x1D → Kırmızı → IDLE modu
    if (address == 0xDB && command == 0x1D) {
        switch_to_idle_mode();
        ESP_LOGI(TAG, "IR: Kırmızı → IDLE modu");
        return;
    }
    
    // 0xD9, 0x1D → Sarı → PLANNED modu
    if (address == 0xD9 && command == 0x1D) {
        switch_to_planned_mode();
        ESP_LOGI(TAG, "IR: Sarı → PLANNED modu");
        return;
    }
    
    // 0xD8, 0x1D → Mavi → Adet +1 (Sadece WORK modunda ve Sayaç aktifken)
    if (address == 0xD8 && command == 0x1D) {
        if (current_mode == MODE_WORK && sys_data.counting_active) {
            sys_data.produced_count++;
            ESP_LOGI(TAG, "IR: Mavi → Adet: %lu / %lu", 
                     (unsigned long)sys_data.produced_count, (unsigned long)sys_data.target_count);
            led_strip_start_cycle();
            nvs_storage_save_state_immediate(); // Kritik: Adet artınca hemen kaydet
            andon_display_update();
        } else {
            ESP_LOGW(TAG, "IR: Mavi buton sadece aktif WORK modunda çalışır (Timer:%d)", sys_data.counting_active);
        }
        return;
    }
    
    // ========== DİĞER KOMUTLAR ==========
    
    // Hedef Sıfırlama (0xFE address)
    // MUTE / SIFIRLA (0xFF, 0x02 veya 0xFE adresi)
    if ((address == 0xFF && command == 0x02) || (address == 0xFE)) {
        // Eğer alarm aktifse SADECE sustur (sıfırlama yapma)
        if (led_strip_is_alarm_active()) {
            led_strip_acknowledge_alarm();
            ESP_LOGI(TAG, "IR: MUTE -> Alarm susturuldu");
            return;
        }

        if (sys_data.menu_step == 2) {
            led_strip_set_cycle_target(0);
            ESP_LOGI(TAG, "IR: Menu -> LED Süre sıfırlandı");
            andon_display_update();
            return;
        }
        sys_data.target_count = 0;
        nvs_storage_save_target(0);
        ir_remote_set_input_mode(IR_INPUT_NONE);
        andon_display_update();
        ESP_LOGI(TAG, "IR: MUTE → Hedef sıfırlandı");
        return;
    }
    

    
    // Vardiya Durdur/Başlat (0xFC, 0x1D)
    if (address == 0xFC && command == 0x1D) {
        if (shift_state == SHIFT_RUNNING) {
            shift_state = SHIFT_STOPPED;
            ESP_LOGI(TAG, "IR: Vardiya DURDURULDU (ekran donuk)");
        } else {
            shift_state = SHIFT_RUNNING;
            ESP_LOGI(TAG, "IR: Vardiya BAŞLATILDI");
        }
        nvs_storage_save_state_immediate();
        return;
    }
    
    // Ekran Reset
    if ((address == 0xFF && command == 0xC0) || (address == 0xC0)) {
        sys_data.work_time = 0;
        sys_data.idle_time = 0;
        sys_data.planned_time = 0;
        sys_data.produced_count = 0;
        sys_data.durus_time = 0;
        sys_data.durus_running = false;
        current_mode = MODE_IDLE;
        led_strip_clear();
        nvs_storage_save_state_immediate();
        andon_display_update();
        ESP_LOGI(TAG, "IR: Ekran RESET");
        return;
    }
    
    // Saat Ayarı Modu (FKB / 0xFB, 0x1D)
    if (address == 0xFB && command == 0x1D) {
        if (sys_data.menu_step > 0) {
            ESP_LOGW(TAG, "LED Menü modundayken Saat Ayarına girilemez");
            return;
        }
        if (sys_data.clock_step == 0) {
            // Modu başlat: Saat adımına geç
            ir_remote_set_input_mode(IR_INPUT_CLOCK);
            sys_data.clock_step = 1;
            
            // Mevcut zamanı al ve yedekle
            struct tm tm_now;
            if (rtc_ds1307_read_tm(&tm_now) != ESP_OK) {
                time_t now = time(NULL);
                struct tm *tm_local = localtime(&now);
                tm_now = *tm_local;
            }
            sys_data.clock_hours = tm_now.tm_hour;
            sys_data.clock_minutes = tm_now.tm_min;
            sys_data.clock_backup_hours = tm_now.tm_hour;
            sys_data.clock_backup_minutes = tm_now.tm_min;
            sys_data.clock_blink_on = true;
            ESP_LOGI(TAG, "IR: Saat Ayarı Modu Başladı (Yedek: %02d:%02d)", sys_data.clock_backup_hours, sys_data.clock_backup_minutes);
        } else if (sys_data.clock_step == 1) {
            // Saat bitti, dakikaya geçmeden önce SAATİ doğrula
            if (sys_data.clock_hours > 23) {
                ESP_LOGW(TAG, "IR: Geçersiz SAAT (%d) -> Eski değere (%d) dönülüyor", sys_data.clock_hours, sys_data.clock_backup_hours);
                sys_data.clock_hours = sys_data.clock_backup_hours;
            }
            sys_data.clock_step = 2;
            ESP_LOGI(TAG, "IR: Saat Ayarı (Dakika Adımı)");
        } else {
            // Dakika bitti, DAKİKAYI doğrula
            if (sys_data.clock_minutes > 59) {
                ESP_LOGW(TAG, "IR: Geçersiz DAKİKA (%d) -> Eski değere (%d) dönülüyor", sys_data.clock_minutes, sys_data.clock_backup_minutes);
                sys_data.clock_minutes = sys_data.clock_backup_minutes;
            }
            // Kaydet ve Çık
            rtc_ds1307_set_time(sys_data.clock_hours, sys_data.clock_minutes);
            sys_data.clock_step = 0;
            ir_remote_set_input_mode(IR_INPUT_NONE);
            ESP_LOGI(TAG, "IR: Saat Ayarı Kaydedildi ve Çıkıldı");
        }
        andon_display_update();
        return;
    }

    // Hedef Adet Girme Modu (Manuel)
    if ((address == 0xFF && command == 0xD0) || (address == 0xD0)) {
        ir_remote_set_input_mode(IR_INPUT_TARGET);
        ESP_LOGI(TAG, "IR: Hedef adet giriş modu");
        return;
    }
    
    // Cycle Süresi Girme Modu
    if ((address == 0xFF && command == 0xE0) || (address == 0xE0)) {
        ir_remote_set_input_mode(IR_INPUT_CYCLE_TIME);
        ESP_LOGI(TAG, "IR: Cycle süresi giriş modu");
        return;
    }
    
    // Giriş modundan çık (OK tuşu)
    if ((address == 0xFF && command == 0xF0) || (address == 0xF0)) {
        if (sys_data.clock_step > 0) {
            // Saat ayarındaysak OK'e basınca bir sonraki adıma geçer veya kaydeder
            if (sys_data.clock_step == 1) {
                sys_data.clock_step = 2;
            } else {
                rtc_ds1307_set_time(sys_data.clock_hours, sys_data.clock_minutes);
                sys_data.clock_step = 0;
                ir_remote_set_input_mode(IR_INPUT_NONE);
            }
        } else {
            ir_remote_set_input_mode(IR_INPUT_NONE);
        }
        andon_display_update();
        ESP_LOGI(TAG, "IR: Giriş/Ayar modu kapatıldı");
        return;
    }
}

// ============ Power-on Recovery ============
static void power_on_recovery(void) {
    system_state_backup_t last = nvs_storage_load_state();
    
    // Varsayılan değerler
    sys_data.target_count = nvs_storage_load_target();
    led_strip_set_cycle_target(nvs_storage_load_cycle_target());
    sys_data.led_brightness_idx = nvs_storage_load_brightness();
    led_strip_set_brightness_idx(sys_data.led_brightness_idx);
    sys_data.menu_step = 0;
    
    if (last.valid && last.shift_state == SHIFT_STOPPED) {
        // Vardiya durdurulmuş olarak kalmıştı — kaydedilen modda devam et
        shift_state = SHIFT_STOPPED;
        current_mode = (work_mode_t)last.work_mode;
        sys_data.work_time = last.work_t;
        sys_data.idle_time = last.idle_t;
        sys_data.planned_time = last.planned_t;
        sys_data.produced_count = last.prod_cnt;
        sys_data.durus_time = last.durus_t;
        sys_data.counting_active = false;  // Vardiya durmuştu, sayaç pasif
        ESP_LOGI(TAG, "🔄 RECOVERY: Shift STOPPED, mode=%d, ekran donuk", current_mode);
        
    } else if (last.valid && (last.work_mode == MODE_IDLE || last.work_mode == MODE_PLANNED)) {
        // IDLE veya PLANNED modunda güç kesilmişti
        current_mode = (work_mode_t)last.work_mode;
        sys_data.work_time = last.work_t;
        sys_data.idle_time = last.idle_t;
        sys_data.planned_time = last.planned_t;
        sys_data.produced_count = last.prod_cnt;
        sys_data.durus_time = last.durus_t; // DURUS RESTORE
        
        // Offline süresini ilgili sayaca ekle
        uint32_t now = rtc_get_wall_time_seconds();
        if (last.last_upd > 0 && now > last.last_upd) {
            uint32_t offline = now - last.last_upd;
            if (offline < 86400) {
                if (current_mode == MODE_IDLE) {
                    sys_data.idle_time += offline;
                } else {
                    sys_data.planned_time += offline;
                }
                sys_data.durus_time += offline; // Offline süresini mevcut duruşa da ekle
                ESP_LOGI(TAG, "⏱️ Offline: %lu sec added to mode %d and durus_time", (unsigned long)offline, current_mode);
            }
        }
        
        // Duruş timer'ı başlat
        sys_data.counting_active = true;
        start_durus_timer();
        ESP_LOGI(TAG, "🔄 RECOVERY: MODE_%s continues", 
                 current_mode == MODE_IDLE ? "IDLE" : "PLANNED");
        
    } else {
        // Yeni başlangıç veya geçersiz veri -> STANDBY'da bekle
        current_mode = MODE_STANDBY;
        shift_state = SHIFT_RUNNING;
        sys_data.work_time = 0;
        sys_data.idle_time = 0;
        sys_data.planned_time = 0;
        sys_data.produced_count = 0;
        sys_data.counting_active = false; // Kullanıcı butona basana kadar bekle
        ESP_LOGI(TAG, "Fresh start (NVS invalid or empty) - MODE_STANDBY");
    }
    
    // Ekran varsayılan olarak AÇIK
    sys_data.screen_on = true;
    sys_data.menu_step = 0;
    andon_display_update();
}

// ============ Main Entry Point ============
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  KlimasanAndonV2 Starting...");
    ESP_LOGI(TAG, "========================================");
    
    // 1. NVS başlat
    nvs_storage_init();
    
    // 2. RTC başlat (I2C)
    rtc_ds1307_init();
    
    // 3. IR task için watchdog'u disable et
    esp_task_wdt_deinit();
    
    // 4. Power-on recovery
    power_on_recovery();
    
    // 5. Modülleri başlat
    andon_display_init();
    led_strip_init();
    ir_remote_init();
    button_handler_init();
    
    // 6. Callback'leri ayarla
    button_handler_set_callback(on_button_event);
    ir_remote_set_callback(on_ir_command);
    
    // 7. İlk display güncellemesi
    andon_display_update();
    
    // 8. Task'ları başlat
    andon_display_start_task();  // Core 0, Priority 5 (DISPLAY HER ŞEYDEN ÖNCE GELİR)
    
    led_strip_start_task();      // Core 1, Priority 5 (LED BAR real-time olmalı)
    ir_remote_start_task();      // Core 1, Priority 4 (LED'in bir tık altında)
    button_handler_start_task(); // Core 1, Priority 3
    nvs_storage_start_task();    // Core 1, Priority 1
    
    // Timer task (Core 0, Priority 4 - Display ile aynı çekirdek ama altında)
    xTaskCreatePinnedToCore(timer_task, "timer_task", 4096, NULL, 4, NULL, 0);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  System Ready!");
    ESP_LOGI(TAG, "  Mode: %s", current_mode == MODE_WORK ? "WORK" : 
                                 current_mode == MODE_IDLE ? "IDLE" : "PLANNED");
    ESP_LOGI(TAG, "========================================");
}
