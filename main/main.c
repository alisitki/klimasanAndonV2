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
work_mode_t current_mode = MODE_IDLE;
shift_state_t shift_state = SHIFT_RUNNING;

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
    if (current_mode == MODE_WORK) return;
    
    // Sayaçları aktif et
    sys_data.counting_active = true;
    
    // Duruş timer'ı durdur (frozen value)
    stop_durus_timer();
    
    current_mode = MODE_WORK;
    led_strip_clear(); // WORK'e geçince LED barı söndür (adet gelince başlayacak)
    ESP_LOGI(TAG, "🟢 MODE: WORK (Çalışma zamanı sayılıyor)");
    nvs_storage_save_state_immediate();
    andon_display_update();
}

static void switch_to_idle_mode(void) {
    if (current_mode == MODE_IDLE && sys_data.counting_active) return;
    
    // Sayaçları aktif et
    sys_data.counting_active = true;
    
    // Duruş timer'ı başlat (eğer daha önce WORK'te idiysek)
    if (current_mode == MODE_WORK) {
        sys_data.durus_time = 0;  // Yeni duruş, sıfırdan başla
    }
    start_durus_timer();
    
    current_mode = MODE_IDLE;
    ESP_LOGI(TAG, "🔴 MODE: IDLE (Atıl zaman sayılıyor)");
    nvs_storage_save_state_immediate();
    andon_display_update();
}

static void switch_to_planned_mode(void) {
    if (current_mode == MODE_PLANNED && sys_data.counting_active) return;
    
    // Sayaçları aktif et
    sys_data.counting_active = true;
    
    // Duruş timer'ı başlat (eğer daha önce WORK'te idiysek)
    if (current_mode == MODE_WORK) {
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
        
        // Sayaçlar aktif değilse (veya saat ayarı modundaysak) sadece display güncelle
        if (!sys_data.counting_active || sys_data.clock_step > 0) {
            andon_display_update();
            continue;
        }
        
        // Vardiya durdurulmuşsa hiçbir sayaç artmaz
        if (shift_state == SHIFT_STOPPED) {
            andon_display_update();
            continue;
        }
        
        // Mevcut moda göre ilgili sayaç artar
        switch (current_mode) {
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
        
        // Display güncelle
        andon_display_update();
        
        // Periyodik kayıt
        nvs_storage_save_state();
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
                sys_data.produced_count++;
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
    
    // Rakam girişi modunda
    int8_t digit = decode_ir_digit(address, command);
    if (digit >= 0 && sys_data.screen_on) {
        if (input_mode == IR_INPUT_CLOCK) {
            // SAAT AYARI MODU
            if (sys_data.clock_step == 1) {
                // Saat hanesi
                sys_data.clock_hours = (sys_data.clock_hours % 10) * 10 + digit;
                if (sys_data.clock_hours > 23) sys_data.clock_hours = 23;
                ESP_LOGI(TAG, "Clock Entry: Hour = %02d", sys_data.clock_hours);
            } else if (sys_data.clock_step == 2) {
                // Dakika hanesi
                sys_data.clock_minutes = (sys_data.clock_minutes % 10) * 10 + digit;
                if (sys_data.clock_minutes > 59) sys_data.clock_minutes = 59;
                ESP_LOGI(TAG, "Clock Entry: Minute = %02d", sys_data.clock_minutes);
            }
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
            current_mode = MODE_IDLE;
            
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
    
    // 0xD8, 0x1D → Mavi → Adet +1 (WORK modunda)
    if (address == 0xD8 && command == 0x1D) {
        if (current_mode == MODE_WORK) {
            sys_data.produced_count++;
            ESP_LOGI(TAG, "IR: Mavi → Adet: %lu / %lu", 
                     (unsigned long)sys_data.produced_count, (unsigned long)sys_data.target_count);
            led_strip_start_cycle();
            nvs_storage_save_state();
            andon_display_update();
        } else {
            ESP_LOGW(TAG, "IR: Mavi buton sadece WORK modunda çalışır");
        }
        return;
    }
    
    // ========== DİĞER KOMUTLAR ==========
    
    // Hedef Sıfırlama (0xFE address)
    // MUTE / SIFIRLA (0xFF, 0x02 veya 0xFE adresi)
    if ((address == 0xFF && command == 0x02) || (address == 0xFE)) {
        sys_data.target_count = 0;
        nvs_storage_save_target(0);
        ir_remote_set_input_mode(IR_INPUT_NONE);
        andon_display_update();
        ESP_LOGI(TAG, "IR: MUTE → Hedef sıfırlandı");
        return;
    }
    
    // Alarm Kabul
    if ((address == 0xFF && command == 0xA0) || (address == 0xA0)) {
        led_strip_acknowledge_alarm();
        ESP_LOGI(TAG, "IR: Alarm kabul edildi");
        return;
    }
    
    // Vardiya Durdur/Başlat
    if ((address == 0xFF && command == 0xB0) || (address == 0xB0)) {
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
        if (sys_data.clock_step == 0) {
            // Modu başlat: Saat adımına geç
            ir_remote_set_input_mode(IR_INPUT_CLOCK);
            sys_data.clock_step = 1;
            
            // Mevcut zamanı al
            struct tm tm_now;
            if (rtc_ds1307_read_tm(&tm_now) != ESP_OK) {
                time_t now = time(NULL);
                struct tm *tm_local = localtime(&now);
                tm_now = *tm_local;
            }
            sys_data.clock_hours = tm_now.tm_hour;
            sys_data.clock_minutes = tm_now.tm_min;
            sys_data.clock_blink_on = true;
            ESP_LOGI(TAG, "IR: Saat Ayarı Modu Başladı (Saat Adımı)");
        } else if (sys_data.clock_step == 1) {
            // Saat bitti, dakikaya geç
            sys_data.clock_step = 2;
            ESP_LOGI(TAG, "IR: Saat Ayarı (Dakika Adımı)");
        } else {
            // Dakika da bitti, Kaydet ve Çık
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
    
    if (last.shift_state == SHIFT_STOPPED) {
        // Vardiya durdurulmuş olarak kalmıştı
        shift_state = SHIFT_STOPPED;
        current_mode = (work_mode_t)last.work_mode;
        sys_data.work_time = last.work_t;
        sys_data.idle_time = last.idle_t;
        sys_data.planned_time = last.planned_t;
        sys_data.produced_count = last.prod_cnt;
        ESP_LOGI(TAG, "🔄 RECOVERY: Shift STOPPED, ekran donuk");
        
    } else if (last.work_mode == MODE_WORK) {
        // WORK modunda güç kesilmişti
        current_mode = MODE_WORK;
        sys_data.work_time = last.work_t;
        sys_data.idle_time = last.idle_t;
        sys_data.planned_time = last.planned_t;
        sys_data.produced_count = last.prod_cnt;
        
        // Offline süresini work_time'a ekle
        uint32_t now = rtc_get_wall_time_seconds();
        if (last.last_upd > 0 && now > last.last_upd) {
            uint32_t offline = now - last.last_upd;
            if (offline < 86400) {  // Max 24 saat
                sys_data.work_time += offline;
                ESP_LOGI(TAG, "⏱️ Offline: %lu sec → work_time += %lu", (unsigned long)offline, (unsigned long)offline);
            }
        }
        ESP_LOGI(TAG, "🔄 RECOVERY: MODE_WORK continues");
        
    } else if (last.work_mode == MODE_IDLE || last.work_mode == MODE_PLANNED) {
        // IDLE veya PLANNED modunda güç kesilmişti
        current_mode = (work_mode_t)last.work_mode;
        sys_data.work_time = last.work_t;
        sys_data.idle_time = last.idle_t;
        sys_data.planned_time = last.planned_t;
        sys_data.produced_count = last.prod_cnt;
        
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
                ESP_LOGI(TAG, "⏱️ Offline: %lu sec added to mode %d", offline, current_mode);
            }
        }
        
        // Duruş timer'ı başlat
        start_durus_timer();
        ESP_LOGI(TAG, "🔄 RECOVERY: MODE_%s continues", 
                 current_mode == MODE_IDLE ? "IDLE" : "PLANNED");
        
    } else {
        // Yeni başlangıç
        current_mode = MODE_IDLE;
        shift_state = SHIFT_RUNNING;
        start_durus_timer();
        ESP_LOGI(TAG, "Fresh start - MODE_IDLE");
    }
    
    // Ekran varsayılan olarak AÇIK
    sys_data.screen_on = true;
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
