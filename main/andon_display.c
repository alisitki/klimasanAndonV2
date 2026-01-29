/*
 * KlimasanAndonV2 - 7-Segment Display Module
 * HC138 + CD4543 ile multiplexed display kontrolü
 * 
 * Display Layout (8 Latch):
 * LD1: 6 digit - SAAT (HH:MM:SS) - RTC'den
 * LD2: 4 digit - DURUŞ SÜRESİ (MM:SS)
 * LD3: 6 digit - ÇALIŞMA ZAMANI (HH:MM:SS)
 * LD4: 6 digit - ATIL ZAMAN (HH:MM:SS)
 * LD5: 6 digit - PLANLI DURUŞ (HH:MM:SS)
 * LD6: 4 digit - HEDEF ADET (max 9999)
 * LD7: 4 digit - GERÇEKLEŞEN ADET (max 9999)
 * LD8: 2 digit - VERİM (00-99%)
 * 
 * Tarama: HC138 ile 6 hane (0-5), her taramada 8 latch'e veri gönderilir
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "andon_display.h"
#include "pin_config.h"
#include "system_state.h"
#include "rtc_ds1307.h"
#include "led_strip.h"

static const char *TAG = "andon_display";

// Double buffering for scan_data to prevent race conditions
// Tarama 0 = en sağdaki hane (birler), Tarama 5 = en soldaki hane
static uint8_t scan_data_buffers[2][6][8] = {0};
static volatile int active_buffer = 0;  // Display reads from this
static volatile int write_buffer = 1;   // Update writes to this

// Macro for easier access
#define SCAN_DATA_READ  scan_data_buffers[active_buffer]
#define SCAN_DATA_WRITE scan_data_buffers[write_buffer]

// Özel karakterler (CD4543 BCD -> Segment mapping varsayımları)
// L=12, E=14, d=13, P=11, r=10 (standard symbol mapping)
#define CHAR_L 12
#define CHAR_E 14
#define CHAR_D 13
#define CHAR_P 11
#define CHAR_R 10
#define CHAR_S 5   // '5' looks like 'S'
#define CHAR_U 11  // Some decoders show U at 11

// ============ HC138 Selection (0-5 valid, 6-7 = all off) ============
void andon_display_select_hane(int hane) {
    gpio_set_level(HC138_A0_PIN, (hane >> 0) & 1);
    gpio_set_level(HC138_A1_PIN, (hane >> 1) & 1);
    gpio_set_level(HC138_A2_PIN, (hane >> 2) & 1);
    esp_rom_delay_us(10);
}

// ============ CD4543 BCD Output ============
void andon_display_send_bcd(int bcd_value) {
    gpio_set_level(CD4543_D0_PIN, (bcd_value >> 0) & 1);
    gpio_set_level(CD4543_D1_PIN, (bcd_value >> 1) & 1);
    gpio_set_level(CD4543_D2_PIN, (bcd_value >> 2) & 1);
    gpio_set_level(CD4543_D3_PIN, (bcd_value >> 3) & 1);
    esp_rom_delay_us(10);
}

// ============ Helper: Time to digits (HH:MM:SS -> 6 digits) ============
static void time_to_6digits(uint32_t total_sec, uint8_t out[6]) {
    uint32_t sec = total_sec % 60;
    uint32_t min = (total_sec / 60) % 60;
    uint32_t hour = (total_sec / 3600) % 100;  // Max 99 saat
    
    // out[0] = saniye birler, out[5] = saat onlar
    out[0] = sec % 10;
    out[1] = sec / 10;
    out[2] = min % 10;
    out[3] = min / 10;
    out[4] = hour % 10;
    out[5] = hour / 10;
}

// ============ Helper: Time to digits (MM:SS -> 4 digits) ============
static void time_to_4digits(uint32_t total_sec, uint8_t out[4]) {
    uint32_t sec = total_sec % 60;
    uint32_t min = (total_sec / 60) % 100;  // Max 99 dakika
    
    out[0] = sec % 10;
    out[1] = sec / 10;
    out[2] = min % 10;
    out[3] = min / 10;
}

// ============ Helper: Count to digits with leading blank ============
static void count_to_4digits(uint32_t value, uint8_t out[4]) {
    value = value % 10000;  // Max 9999
    
    out[0] = value % 10;              // Birler (always shown)
    out[1] = (value / 10) % 10;       // Onlar
    out[2] = (value / 100) % 10;      // Yuzler  
    out[3] = (value / 1000) % 10;     // Binler
    
    // Leading zero blanking (en soldaki sıfırları kapat, birler hariç)
    // Sadece out[3], out[2], out[1] blank olabilir
    if (out[3] == 0) {
        out[3] = DISPLAY_BLANK;
        if (out[2] == 0) {
            out[2] = DISPLAY_BLANK;
            if (out[1] == 0) {
                out[1] = DISPLAY_BLANK;
            }
        }
    }
}

// ============ Helper: Verim to 2 digits ============
static void verim_to_2digits(uint32_t verim, uint8_t out[2]) {
    verim = verim % 100;  // Max 99
    out[0] = verim % 10;
    out[1] = verim / 10;
    
    // Eğer onlar basamağı 0 ise kapat (blank)
    if (out[1] == 0) {
        out[1] = DISPLAY_BLANK;
    }
}

// ============ Update scan data from system values ============
void andon_display_update(void) {
    uint8_t saat[6];        // LD1: RTC saat
    uint8_t durus[4];       // LD2: Duruş süresi (MM:SS)
    uint8_t calisma[6];     // LD3: Çalışma zamanı
    uint8_t atil[6];        // LD4: Atıl zaman
    uint8_t planli[6];      // LD5: Planlı duruş
    uint8_t hedef[4];       // LD6: Hedef adet
    uint8_t gerceklesen[4]; // LD7: Gerçekleşen adet
    uint8_t verim[2];       // LD8: Verim %
    
    // LD1: SAAT - RTC'den al
    struct tm tm_now;
    if (sys_data.clock_step == 0) {
        if (rtc_ds1307_read_tm(&tm_now) == ESP_OK) {
            saat[0] = tm_now.tm_sec % 10;
            saat[1] = tm_now.tm_sec / 10;
            saat[2] = tm_now.tm_min % 10;
            saat[3] = tm_now.tm_min / 10;
            saat[4] = tm_now.tm_hour % 10;
            saat[5] = tm_now.tm_hour / 10;
        } else {
            time_t now = time(NULL);
            struct tm *tm_local = localtime(&now);
            saat[0] = tm_local->tm_sec % 10;
            saat[1] = tm_local->tm_sec / 10;
            saat[2] = tm_local->tm_min % 10;
            saat[3] = tm_local->tm_min / 10;
            saat[4] = tm_local->tm_hour % 10;
            saat[5] = tm_local->tm_hour / 10;
        }
    } else {
        // SAAT AYARI MODU - HH:MM:00
        saat[0] = 0; // Saniye her zaman 0
        saat[1] = 0;
        
        // Dakika Haneleri
        if (sys_data.clock_step == 2 && !sys_data.clock_blink_on) {
            saat[2] = DISPLAY_BLANK;
            saat[3] = DISPLAY_BLANK;
        } else {
            saat[2] = sys_data.clock_minutes % 10;
            saat[3] = sys_data.clock_minutes / 10;
        }
        
        // Saat Haneleri
        if (sys_data.clock_step == 1 && !sys_data.clock_blink_on) {
            saat[4] = DISPLAY_BLANK;
            saat[5] = DISPLAY_BLANK;
        } else {
            saat[4] = sys_data.clock_hours % 10;
            saat[5] = sys_data.clock_hours / 10;
        }
    }
    
    // LD2: DURUŞ SÜRESİ (MM:SS)
    time_to_4digits(sys_data.durus_time, durus);
    
    // LD3: ÇALIŞMA ZAMANI
    time_to_6digits(sys_data.work_time, calisma);
    
    // LD4: ATIL ZAMAN
    time_to_6digits(sys_data.idle_time, atil);
    
    // LD5: PLANLI DURUŞ
    time_to_6digits(sys_data.planned_time, planli);
    
    // LD6: HEDEF ADET
    count_to_4digits(sys_data.target_count, hedef);
    
    // LD7: GERÇEKLEŞEN ADET
    count_to_4digits(sys_data.produced_count, gerceklesen);
    
    // LD8: VERİM (%)
    uint32_t verim_val = 0;
    if (sys_data.target_count > 0) {
        verim_val = (sys_data.produced_count * 100) / sys_data.target_count;
        if (verim_val > 99) verim_val = 99;  // Max 99%
    }
    verim_to_2digits(verim_val, verim);
    
    // ========== MENÜ AYAR EKRANI MODU ==========
    if (sys_data.menu_step > 0) {
        // Tüm haneleri temizle varsayılan olarak
        memset(saat, DISPLAY_BLANK, sizeof(saat));
        memset(durus, DISPLAY_BLANK, sizeof(durus));
        memset(calisma, DISPLAY_BLANK, sizeof(calisma));
        memset(atil, DISPLAY_BLANK, sizeof(atil));
        memset(planli, DISPLAY_BLANK, sizeof(planli));
        memset(hedef, DISPLAY_BLANK, sizeof(hedef));
        memset(gerceklesen, DISPLAY_BLANK, sizeof(gerceklesen));
        memset(verim, DISPLAY_BLANK, sizeof(verim));

        if (sys_data.menu_step == 1) {
            // Parlaklık Ayarı: Değer LD4 (Atıl Zaman) hanesinde görünür
            atil[0] = sys_data.led_brightness_idx;
        } else if (sys_data.menu_step == 2) {
            // Süre Ayarı: Değer LD4 (Atıl Zaman) hanesinde görünür
            uint32_t target = led_strip_get_cycle_target();
            atil[0] = target % 10;
            atil[1] = (target / 10) % 10;
            atil[2] = (target / 100) % 10;
            atil[3] = (target / 1000) % 10;
            atil[4] = (target / 10000) % 10;
            atil[5] = (target / 100000) % 10;
        }
    }
    
    // Write to WRITE buffer (display reads from ACTIVE buffer)
    // Önce write buffer'ı temizle (Ghosting veya eski verileri engellemek için)
    memset(SCAN_DATA_WRITE, DISPLAY_BLANK, sizeof(SCAN_DATA_WRITE));
    
    // Her tarama (0-5) bir hane pozisyonu
    // Tarama 0 = en sağ (birler), Tarama 5 = en sol (yüzler/onlar)
    for (int scan = 0; scan < 6; scan++) {
        // LD1: SAAT (6 hane)
        SCAN_DATA_WRITE[scan][0] = saat[scan];
        
        // LD2: DURUŞ (4 hane, SOLA YASLI: scan 0-1 için blank, scan 2-5 için veri)
        if (scan >= 2) {
            SCAN_DATA_WRITE[scan][1] = durus[scan - 2];
        } else {
            SCAN_DATA_WRITE[scan][1] = DISPLAY_BLANK;
        }
        
        // LD3: ÇALIŞMA (6 hane)
        SCAN_DATA_WRITE[scan][2] = calisma[scan];
        
        // LD4: ATIL (6 hane)
        SCAN_DATA_WRITE[scan][3] = atil[scan];
        
        // LD5: PLANLI (6 hane)
        SCAN_DATA_WRITE[scan][4] = planli[scan];
        
        // LD6: HEDEF (4 hane, SOLA YASLI)
        if (scan >= 2) {
            SCAN_DATA_WRITE[scan][5] = hedef[scan - 2];
        } else {
            SCAN_DATA_WRITE[scan][5] = DISPLAY_BLANK;
        }
        
        // LD7: GERÇEKLEŞEN (4 hane, SOLA YASLI)
        if (scan >= 2) {
            SCAN_DATA_WRITE[scan][6] = gerceklesen[scan - 2];
        } else {
            SCAN_DATA_WRITE[scan][6] = DISPLAY_BLANK;
        }
        
        // LD8: VERİM (2 hane, SOLA YASLI)
        if (scan >= 4) {
            SCAN_DATA_WRITE[scan][7] = verim[scan - 4];
        } else {
            SCAN_DATA_WRITE[scan][7] = DISPLAY_BLANK;
        }
    }
    
    // Atomic buffer swap - display will use new data on next scan cycle
    int temp = active_buffer;
    active_buffer = write_buffer;
    write_buffer = temp;
}

// ============ Display Scan Task (Multiplexing) ============
static void display_scan_task(void *pvParameters) {
    const gpio_num_t ld_pins[8] = {
        CD4543_LD1_PIN,
        CD4543_LD2_PIN,
        CD4543_LD3_PIN,
        CD4543_LD4_PIN,
        CD4543_LD5_PIN,
        CD4543_LD6_PIN,
        CD4543_LD7_PIN,
        CD4543_LD8_PIN,
    };

    ESP_LOGI(TAG, "Display multiplexing started (8 latches)");

    while (1) {
        // Ekran kapalıysa hiçbir şey gösterme
        if (!sys_data.screen_on) {
            andon_display_select_hane(7);  // Tüm taramalar OFF
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Vardiya durdurulmuşsa ekran donuk kalır (güncellenmiyor ama gösteriliyor)
        // shift_state == SHIFT_STOPPED durumunda update yapılmaz ama scan devam eder
        
        // 6 tarama döngüsü (0-5)
        for (int scan = 0; scan < 6; scan++) {
            // 1. LATCH: Her scan'da 8 latch'e sırayla veri gönder
            for (int latch = 0; latch < 8; latch++) {
                uint8_t digit = SCAN_DATA_READ[scan][latch];
                
                // BCD datası gönder
                andon_display_send_bcd(digit);
                esp_rom_delay_us(10);
                
                // Latch pulse
                gpio_set_level(ld_pins[latch], 1);
                esp_rom_delay_us(10);
                gpio_set_level(ld_pins[latch], 0);
                esp_rom_delay_us(10);
            }
            
            // 2. TARAMA: Latch'ler hazırlandıktan sonra taramayı seç
            andon_display_select_hane(scan);
            
            // 3. BEKLE: Bu tarama'da kal
            esp_rom_delay_us(1200);  // 1.5ms per scan
            
            // 4. KAPAT: Taramayı kapat
            andon_display_select_hane(7);  // 7 = all off
            esp_rom_delay_us(5);
        }
    
        // CPU'ya nefes
        vTaskDelay(1);
    }
}

// ============ GPIO Initialization ============
static void gpio_init_display(void) {
    // HC138 pinleri output
    gpio_config_t io_conf_hc138 = {
        .pin_bit_mask = (1ULL << HC138_A0_PIN) | (1ULL << HC138_A1_PIN) | (1ULL << HC138_A2_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf_hc138);
    
    // CD4543 data pinleri output
    gpio_config_t io_conf_cd4543_data = {
        .pin_bit_mask = (1ULL << CD4543_D0_PIN) | (1ULL << CD4543_D1_PIN) | 
                        (1ULL << CD4543_D2_PIN) | (1ULL << CD4543_D3_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf_cd4543_data);
    
    // CD4543 LD pinleri output (8 adet)
    gpio_config_t io_conf_cd4543_ld = {
        .pin_bit_mask = (1ULL << CD4543_LD1_PIN) | (1ULL << CD4543_LD2_PIN) | 
                        (1ULL << CD4543_LD3_PIN) | (1ULL << CD4543_LD4_PIN) |
                        (1ULL << CD4543_LD5_PIN) | (1ULL << CD4543_LD6_PIN) |
                        (1ULL << CD4543_LD7_PIN) | (1ULL << CD4543_LD8_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf_cd4543_ld);
    
    // LD pinlerini LOW tut
    gpio_set_level(CD4543_LD1_PIN, 0);
    gpio_set_level(CD4543_LD2_PIN, 0);
    gpio_set_level(CD4543_LD3_PIN, 0);
    gpio_set_level(CD4543_LD4_PIN, 0);
    gpio_set_level(CD4543_LD5_PIN, 0);
    gpio_set_level(CD4543_LD6_PIN, 0);
    gpio_set_level(CD4543_LD7_PIN, 0);
    gpio_set_level(CD4543_LD8_PIN, 0);
    
    ESP_LOGI(TAG, "Display GPIO initialized (8 latches)");
}

// ============ Public Functions ============

esp_err_t andon_display_init(void) {
    gpio_init_display();
    andon_display_update();
    ESP_LOGI(TAG, "Andon display initialized");
    return ESP_OK;
}

void andon_display_start_task(void) {
    xTaskCreatePinnedToCore(display_scan_task, "display_scan", 4096, NULL, 20, NULL, 0);
    ESP_LOGI(TAG, "Display scan task started (Core 0, Priority 20)");
}
