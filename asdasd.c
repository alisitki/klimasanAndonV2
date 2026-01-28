#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "nvs.h"

// Sistem yapısı
typedef struct {
    uint32_t idle_time;      // Atıl zaman (saniye)
    uint32_t work_time;      // Çalışma zamanı (saniye)
    uint32_t produced_count; // Gerçekleşen adet
    uint32_t target_count;   // Hedef adet
} system_data_t;

static system_data_t sys_data = {0, 0, 0, 0};

// Sistem durumları
typedef enum {
    STATE_IDLE,      // Sistem boş
    STATE_RUNNING,   // Çalışma zamanı akıyor
    STATE_PAUSED     // Atıl zaman akıyor
} system_state_t;

static system_state_t current_state = STATE_IDLE;

// Panel durumu
typedef enum {
    PANEL_CLOSED = 0,  // Pano kapalı
    PANEL_OPEN = 1     // Pano açık
} panel_state_t;

static panel_state_t panel_state = PANEL_CLOSED;

// Sistem durumu backup (NVS için)
typedef struct {
    uint8_t panel_st;
    uint8_t current_st;  // STATE_IDLE, STATE_RUNNING, STATE_PAUSED
    uint32_t work_t;
    uint32_t idle_t;
    uint32_t prod_cnt;
    uint32_t last_upd;
} system_state_backup_t;

// NVS kayıt kuyruğu
typedef struct {
    uint8_t panel;
    uint32_t work;
    uint32_t idle;
    uint32_t produced;
    uint32_t target;  // Hedef adet de kayıt et
} nvs_save_request_t;

static QueueHandle_t nvs_save_queue = NULL;
static bool ds1307_available = false;

// 74HC138 Seçim Pinleri
#define HC138_A0_PIN    23
#define HC138_A1_PIN    4
#define HC138_A2_PIN    16

// CD4543 Data Pinleri (BCD)
#define CD4543_D0_PIN   22
#define CD4543_D1_PIN   21
#define CD4543_D2_PIN   19
#define CD4543_D3_PIN   18

// CD4543 Latch Display (LD) Pinleri
#define CD4543_LD1_PIN  17
#define CD4543_LD2_PIN   5
#define CD4543_LD3_PIN  26
#define CD4543_LD4_PIN  14

// I2C DS1307
#define I2C_SDA_PIN     25
#define I2C_SCL_PIN     33
#define DS1307_ADDR     0x68

// IR Sensör
#define IR_SENSOR_PIN   27

// Butonlar
#define BUTTON1_PIN     35  // Sarı - Çalışmaya başla (PIN SWAP)
#define BUTTON2_PIN     34  // Yeşil - Adet say (PIN SWAP)
#define BUTTON3_PIN     32  // Kırmızı - Duraklat (atıl zaman)

static const char *TAG = "KLIMASAN";
#define DISPLAY_BLANK    0x0F

// Forward declarations
void process_ir_command(uint8_t address, uint8_t command);
void update_scan_data();
void save_system_state(uint8_t panel, uint32_t work, uint32_t idle, uint32_t produced);
system_state_backup_t load_system_state();
void nvs_save_task(void *pvParameters);
static void fill_counter_digits(uint32_t value, uint8_t out_digits[5]);
static esp_err_t ds1307_read_register(uint8_t reg, uint8_t *value);
static esp_err_t ds1307_write_register(uint8_t reg, uint8_t value);
static void ds1307_start_if_halted(void);
static esp_err_t ds1307_get_epoch(time_t *epoch_out);
static uint32_t get_wall_time_seconds(void);

void gpio_init() {
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
    
    // CD4543 LD pinleri output - BAŞLANGIÇTA HIGH TUTUTMA
    gpio_config_t io_conf_cd4543_ld = {
        .pin_bit_mask = (1ULL << CD4543_LD1_PIN) | (1ULL << CD4543_LD2_PIN) | 
                        (1ULL << CD4543_LD3_PIN) | (1ULL << CD4543_LD4_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf_cd4543_ld);
    
    // LD pinlerini LOW tut (inactive state - active-high test)
    gpio_set_level(CD4543_LD1_PIN, 0);
    gpio_set_level(CD4543_LD2_PIN, 0);
    gpio_set_level(CD4543_LD3_PIN, 0);
    gpio_set_level(CD4543_LD4_PIN, 0);
    
    // Buton pinleri input
    gpio_config_t io_conf_buttons = {
        .pin_bit_mask = (1ULL << BUTTON1_PIN) | (1ULL << BUTTON2_PIN) | (1ULL << BUTTON3_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf_buttons);
    
    // IR sensör input
    gpio_config_t io_conf_ir = {
        .pin_bit_mask = (1ULL << IR_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&io_conf_ir);
}

// HC138 seçim: hane numarası (0-5) seçer, ULN2003 transistörleri aktif eder
void select_hane(int hane) {
    gpio_set_level(HC138_A0_PIN, (hane >> 0) & 1);
    gpio_set_level(HC138_A1_PIN, (hane >> 1) & 1);
    gpio_set_level(HC138_A2_PIN, (hane >> 2) & 1);
    esp_rom_delay_us(10);  // Stabilize
}

// CD4543'e BCD değeri gönder
void send_bcd_to_display(int bcd_value) {
    gpio_set_level(CD4543_D0_PIN, (bcd_value >> 0) & 1);
    gpio_set_level(CD4543_D1_PIN, (bcd_value >> 1) & 1);
    gpio_set_level(CD4543_D2_PIN, (bcd_value >> 2) & 1);
    gpio_set_level(CD4543_D3_PIN, (bcd_value >> 3) & 1);
    esp_rom_delay_us(10);  // Stabilize
}

// Tarama (scan) ve Latch kombinasyonları için data matrix
// scan_data[tarama][latch] = digit değeri (0-9)
// Tarama 0-5, Latch 0-3 (LD1-LD4'e karşılık)
static uint8_t scan_data[6][4] = {0};

static void fill_counter_digits(uint32_t value, uint8_t out_digits[5]) {
    for (int i = 0; i < 5; i++) {
        out_digits[i] = value % 10U;
        value /= 10U;
    }

    bool blanking = true;
    for (int pos = 4; pos >= 0; pos--) {
        if (blanking) {
            if (out_digits[pos] == 0U) {
                if (pos != 0) {
                    out_digits[pos] = DISPLAY_BLANK;
                }
            } else {
                blanking = false;
            }
        }
    }
}

// scan_data'yı güncelle - tüm zaman/adet değerlerinden basamakları çıkar
void update_scan_data() {
    // work_time'ı sn, dk, saat'e böl
    uint32_t work_total_sec = sys_data.work_time;
    uint32_t work_sec = work_total_sec % 60;
    uint32_t work_min = (work_total_sec / 60) % 60;
    uint32_t work_hour = (work_total_sec / 3600) % 100;
    
    // idle_time'ı sn, dk, saat'e böl
    uint32_t idle_total_sec = sys_data.idle_time;
    uint32_t idle_sec = idle_total_sec % 60;
    uint32_t idle_min = (idle_total_sec / 60) % 60;
    uint32_t idle_hour = (idle_total_sec / 3600) % 100;
    
    // target_count ve produced_count'u 0-99999 aralığında tut
    uint32_t target = sys_data.target_count % 100000;
    uint32_t produced = sys_data.produced_count % 100000;
    uint8_t produced_digits[5];
    uint8_t target_digits[5];
    fill_counter_digits(produced, produced_digits);
    fill_counter_digits(target, target_digits);
    
    // Tarama 0: sn sinin birler basamağı (LD1: work, LD2: idle, LD3-4: boş)
    scan_data[0][0] = work_sec % 10;
    scan_data[0][1] = idle_sec % 10;
    scan_data[0][2] = 0;  
    scan_data[0][3] = 0;  
    
    // Tarama 1: sn sinin onlar basamağı (LD1: work, LD2: idle, LD3: produced, LD4: target) - SWAP
    scan_data[1][0] = (work_sec / 10) % 10;
    scan_data[1][1] = (idle_sec / 10) % 10;
    scan_data[1][2] = produced_digits[0];        // LD3 -> produced (yer değişti)
    scan_data[1][3] = target_digits[0];          // LD4 -> target (yer değişti)
    
    // Tarama 2: dk sinin birler basamağı
    scan_data[2][0] = work_min % 10;
    scan_data[2][1] = idle_min % 10;
    scan_data[2][2] = produced_digits[1];     // LD3 -> produced
    scan_data[2][3] = target_digits[1];       // LD4 -> target
    
    // Tarama 3: dk sinin onlar basamağı
    scan_data[3][0] = (work_min / 10) % 10;
    scan_data[3][1] = (idle_min / 10) % 10;
    scan_data[3][2] = produced_digits[2];    // LD3 -> produced
    scan_data[3][3] = target_digits[2];      // LD4 -> target
    
    // Tarama 4: saatin birler basamağı
    scan_data[4][0] = work_hour % 10;
    scan_data[4][1] = idle_hour % 10;
    scan_data[4][2] = produced_digits[3];   // LD3 -> produced
    scan_data[4][3] = target_digits[3];     // LD4 -> target
    
    // Tarama 5: saatin onlar basamağı
    scan_data[5][0] = (work_hour / 10) % 10;
    scan_data[5][1] = (idle_hour / 10) % 10;
    scan_data[5][2] = produced_digits[4];  // LD3 -> produced
    scan_data[5][3] = target_digits[4];    // LD4 -> target
}

// Display scanning task (multiplexing) - DOĞRU SIRA: Latch → Tarama → Bekle → Kapat
void display_scan_task(void *pvParameters) {
    const gpio_num_t ld_pins[4] = {
        CD4543_LD1_PIN,  // Latch 0
        CD4543_LD2_PIN,  // Latch 1
        CD4543_LD3_PIN,  // Latch 2
        CD4543_LD4_PIN,  // Latch 3
    };

    ESP_LOGI(TAG, "Display multiplexing başladı - DOĞRU SIRA (Latch→Tarama→Bekle→Kapat)");

    while (1) {
        // Panel kapalıysa ekran söndür
        if (panel_state == PANEL_CLOSED) {
            select_hane(6);  // Tüm taramaları OFF (ekran karanlık)
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // 6 tarama döngüsü (0-5)
        for (int scan = 0; scan < 6; scan++) {
            // 1. LATCH: Her scan'da 4 latch'i sırayla işle - DATA + LATCH PULSE
            for (int latch = 0; latch < 4; latch++) {
                uint8_t digit = scan_data[scan][latch];
                
                // BCD datası gönder
                send_bcd_to_display(digit);
                esp_rom_delay_us(10);
                
                // Latch pulse (CD4543'e veri sakla)
                gpio_set_level(ld_pins[latch], 1);
                esp_rom_delay_us(10);
                gpio_set_level(ld_pins[latch], 0);  // Latch'i 0'a çek (veri kilitlendi)
                esp_rom_delay_us(10);
            }
            
            // 2. TARAMA: Latch'ler hazırlandıktan sonra taramayı seç (HC138)
            select_hane(scan);
            
            // 3. BEKLE: Bu tarama'da kal (görüş süresi)
            esp_rom_delay_us(1000);  // 3ms per scan
            
            // 4. KAPAT: Taramayı kapat (hiçbiri aktif değil = scan6, boş)
            select_hane(6);  // 6 geçersiz, tümü OFF
            esp_rom_delay_us(1);
        }
    
        // CPU'ya nefes (tüm 6 scan bittikten sonra)
        vTaskDelay(1);
    }
}

// I2C başlatma
void i2c_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    
    i2c_param_config(I2C_NUM_0, &conf);
    esp_err_t ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C başlatıldı");
    } else {
        ESP_LOGE(TAG, "I2C başlatılamadı: %s", esp_err_to_name(ret));
        ds1307_available = false;
        return;
    }

    ds1307_start_if_halted();

    time_t ds_now = 0;
    if (ds1307_get_epoch(&ds_now) == ESP_OK) {
        ds1307_available = true;
        struct tm tm_buf;
        localtime_r(&ds_now, &tm_buf);
    ESP_LOGI(TAG, "DS1307 RTC hazır (epoch=%lld, %04d-%02d-%02d %02d:%02d:%02d)",
         (long long)ds_now,
                 tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1,
                 tm_buf.tm_mday,
                 tm_buf.tm_hour,
                 tm_buf.tm_min,
                 tm_buf.tm_sec);
    } else {
        ds1307_available = false;
        ESP_LOGW(TAG, "DS1307 RTC tespit edilemedi, sistem zamanına düşülecek");
    }
}

static esp_err_t ds1307_read_register(uint8_t reg, uint8_t *value) {
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t ds1307_write_register(uint8_t reg, uint8_t value) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void ds1307_start_if_halted(void) {
    uint8_t sec_reg = 0;
    esp_err_t ret = ds1307_read_register(0x00, &sec_reg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DS1307 saniye oku başarısız: %s", esp_err_to_name(ret));
        return;
    }

    if ((sec_reg & 0x80U) != 0U) {
        ESP_LOGW(TAG, "DS1307 CH biti açık (0x%02X) → saniye reset", sec_reg);
        uint8_t new_sec = 0x00;  // 00 saniye, CH=0
        ret = ds1307_write_register(0x00, new_sec);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "DS1307 osilatör başlatıldı, saniye 00 olarak ayarlandı");
        } else {
            ESP_LOGE(TAG, "DS1307 CH bit temizlenemedi: %s", esp_err_to_name(ret));
        }
    }
}

static uint8_t bcd_to_bin(uint8_t value) {
    return ((value >> 4) * 10U) + (value & 0x0FU);
}

static esp_err_t ds1307_read_tm(struct tm *out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[7] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);  // register pointer
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, raw, 6, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, raw + 6, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t hour_reg = raw[2];
    uint8_t hour_dec;
    if (hour_reg & 0x40U) {
        // 12-hour format
        hour_dec = bcd_to_bin(hour_reg & 0x1FU);
        bool pm_flag = (hour_reg & 0x20U) != 0;
        if (hour_dec == 12U) {
            hour_dec = pm_flag ? 12U : 0U;
        } else if (pm_flag) {
            hour_dec = (hour_dec + 12U) % 24U;
        }
    } else {
        // 24-hour format
        hour_dec = bcd_to_bin(hour_reg & 0x3FU);
    }

    struct tm tm_snapshot = {
        .tm_sec = bcd_to_bin(raw[0] & 0x7FU),
        .tm_min = bcd_to_bin(raw[1] & 0x7FU),
        .tm_hour = hour_dec,
        .tm_mday = bcd_to_bin(raw[4] & 0x3FU),
        .tm_mon = bcd_to_bin(raw[5] & 0x1FU) - 1,
        .tm_year = bcd_to_bin(raw[6]) + 100,  // DS1307 stores 0-99 → 2000+
        .tm_isdst = -1,
    };

    *out = tm_snapshot;
    return ESP_OK;
}

static esp_err_t ds1307_get_epoch(time_t *epoch_out) {
    if (epoch_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm tm_snapshot = {0};
    esp_err_t ret = ds1307_read_tm(&tm_snapshot);
    if (ret != ESP_OK) {
        return ret;
    }

    time_t epoch = mktime(&tm_snapshot);
    if (epoch == (time_t)-1) {
        return ESP_FAIL;
    }

    *epoch_out = epoch;
    return ESP_OK;
}

static uint32_t get_wall_time_seconds(void) {
    time_t epoch = 0;
    if (ds1307_available) {
        if (ds1307_get_epoch(&epoch) == ESP_OK) {
            return (uint32_t)epoch;
        }
        ESP_LOGW(TAG, "DS1307 okuma başarısız, sistem zamanına düşülüyor");
        ds1307_available = false;
    }
    epoch = time(NULL);
    return (uint32_t)epoch;
}

// Buton işleyicileri
void button_yellow_pressed() {
    // Sarı buton: Çalışmaya başla
    if (current_state != STATE_RUNNING) {
        current_state = STATE_RUNNING;
        ESP_LOGI(TAG, "🟡 Çalışma başladı");
        // Durumu HEMEN kaydet (blocking, ama event bazlı - sık olmaz)
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
            nvs_set_u8(my_handle, "panel_state", panel_state);
            nvs_set_u8(my_handle, "current_state", (uint8_t)current_state);
            nvs_set_u32(my_handle, "work_time", sys_data.work_time);
            nvs_set_u32(my_handle, "idle_time", sys_data.idle_time);
            nvs_set_u32(my_handle, "produced_cnt", sys_data.produced_count);
            nvs_set_u32(my_handle, "last_update", get_wall_time_seconds());  // ⏱️ TIMESTAMP
            nvs_commit(my_handle);
            nvs_close(my_handle);
            ESP_LOGI(TAG, "💾 HEMEN kaydedildi (STATE_RUNNING başladı)");
        }
        // Display hemen güncelle
        update_scan_data();
    }
}

void button_green_pressed() {
    // Yeşil buton: Adet say (her ürün için)
    if (current_state == STATE_RUNNING) {
        sys_data.produced_count++;
        ESP_LOGI(TAG, "✓ Adet arttırıldı - Gerçekleşen: %lu, Hedef: %lu", 
                 sys_data.produced_count, sys_data.target_count);
        // Durumu kaydet
        save_system_state(panel_state, sys_data.work_time, sys_data.idle_time, sys_data.produced_count);
        // Display hemen güncelle
        update_scan_data();
    }
}

void button_red_pressed() {
    // Kırmızı buton: Çalışmayı duraklat (atıl zaman başla)
    if (current_state == STATE_RUNNING) {
        current_state = STATE_PAUSED;
        ESP_LOGI(TAG, "🔴 Çalışma durduruldu - Atıl zaman başladı");
        // Durumu HEMEN kaydet (blocking, ama event bazlı - sık olmaz)
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
            nvs_set_u8(my_handle, "panel_state", panel_state);
            nvs_set_u8(my_handle, "current_state", (uint8_t)current_state);
            nvs_set_u32(my_handle, "work_time", sys_data.work_time);
            nvs_set_u32(my_handle, "idle_time", sys_data.idle_time);
            nvs_set_u32(my_handle, "produced_cnt", sys_data.produced_count);
            nvs_set_u32(my_handle, "last_update", get_wall_time_seconds());  // ⏱️ TIMESTAMP
            nvs_commit(my_handle);
            nvs_close(my_handle);
            ESP_LOGI(TAG, "💾 HEMEN kaydedildi (STATE_PAUSED başladı)");
        }
        // Display hemen güncelle
        update_scan_data();
    }
}

// Zaman sayıcı task'ı (her saniye çalışır)
void timer_task(void *pvParameters) {
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 1 saniye bekle
        
        // Panel açıksa zaman sayılacak
        if (panel_state == PANEL_OPEN) {
            if (current_state == STATE_RUNNING) {
                sys_data.work_time++;
            } else if (current_state == STATE_PAUSED) {
                sys_data.idle_time++;
            }
        }
        
        update_scan_data();
    }
}

// Buton okuma task'ı
void button_task(void *pvParameters) {
    uint8_t last_button1_state = 1;
    uint8_t last_button2_state = 1;
    uint8_t last_button3_state = 1;
    const uint32_t debounce_ms = 150;  // Tek basışta çift algılamayı önle
    const uint32_t release_ms = 80;     // Tuşun bırakıldığını onaylamak için minimum süre
    uint32_t last_press_time[3] = {0, 0, 0};
    bool button_held[3] = {false, false, false};
    uint32_t high_duration[3] = {0, 0, 0};
    
    while (1) {
        vTaskDelay(10 / portTICK_PERIOD_MS); // 10ms check interval
        
        uint8_t button1 = gpio_get_level(BUTTON1_PIN);
        uint8_t button2 = gpio_get_level(BUTTON2_PIN);
        uint8_t button3 = gpio_get_level(BUTTON3_PIN);
        
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Button1 (Sarı) - basılı state'e girişte bir kere çalışma
        if (button1 == 0 && last_button1_state == 1 && !button_held[0]) {
            if ((current_time - last_press_time[0]) > debounce_ms) {
                button_yellow_pressed();
                last_press_time[0] = current_time;
                button_held[0] = true;
            }
        }
        if (button1 == 0) {
            high_duration[0] = 0;
        } else {
            if (high_duration[0] < release_ms) {
                high_duration[0] += 10;
            }
            if (button_held[0] && high_duration[0] >= release_ms) {
                button_held[0] = false;
            }
        }
        last_button1_state = button1;
        
        // Button2 (Yeşil) - basılı state'e girişte bir kere çalışma
        if (button2 == 0 && last_button2_state == 1 && !button_held[1]) {
            if ((current_time - last_press_time[1]) > debounce_ms) {
                button_green_pressed();
                last_press_time[1] = current_time;
                button_held[1] = true;
            }
        }
        if (button2 == 0) {
            high_duration[1] = 0;
        } else {
            if (high_duration[1] < release_ms) {
                high_duration[1] += 10;
            }
            if (button_held[1] && high_duration[1] >= release_ms) {
                button_held[1] = false;
            }
        }
        last_button2_state = button2;
        
        // Button3 (Kırmızı) - basılı state'e girişte bir kere çalışma
        if (button3 == 0 && last_button3_state == 1 && !button_held[2]) {
            if ((current_time - last_press_time[2]) > debounce_ms) {
                button_red_pressed();
                last_press_time[2] = current_time;
                button_held[2] = true;
            }
        }
        if (button3 == 0) {
            high_duration[2] = 0;
        } else {
            if (high_duration[2] < release_ms) {
                high_duration[2] += 10;
            }
            if (button_held[2] && high_duration[2] >= release_ms) {
                button_held[2] = false;
            }
        }
        last_button3_state = button3;
    }
}

// IR NEC Decoder
typedef enum {
    IR_IDLE,           // Bekleme
    IR_LEAD_HIGH,      // Lead pulse HIGH (9ms)
    IR_LEAD_LOW,       // Lead pulse LOW (4.5ms)
    IR_DATA,           // Data bits
    IR_COMPLETE        // Frame tamamlandı
} ir_state_t;

static ir_state_t ir_decode_state = IR_IDLE;
static uint32_t ir_data = 0;
static uint8_t ir_bit_count = 0;

// 32-bit'i reverse et (LSB-first → MSB-first)
static uint32_t reverse_bits_32(uint32_t value) {
    uint32_t reversed = 0;
    for (int i = 0; i < 32; i++) {
        reversed = (reversed << 1) | ((value >> i) & 1);
    }
    return reversed;
}

// NEC kodu parse et
void ir_parse_nec_code(uint32_t code) {
    // Ham kodu kontrol et (non-standard kumanda tespiti için - 0x33 ile başlayan tüm kodlar)
    bool is_non_standard = ((code & 0xFF000000) == 0x33000000);
    
    // NEC protokolü LSB-first gönderir, tüm 32-bit'i reverse et
    code = reverse_bits_32(code);
    
    uint8_t address = (code >> 24) & 0xFF;
    uint8_t address_inv = (code >> 16) & 0xFF;
    uint8_t command = (code >> 8) & 0xFF;
    uint8_t command_inv = code & 0xFF;
    
    // Checksum doğrulaması (bazı kumandalar için atla)
    
    if (!is_non_standard && (address ^ address_inv) != 0xFF) {
        ESP_LOGE(TAG, "❌ Adres checksum fail: 0x%02X XOR 0x%02X", address, address_inv);
        return;
    }
    
    if (!is_non_standard && (command ^ command_inv) != 0xFF) {
        ESP_LOGE(TAG, "❌ Komut checksum fail: 0x%02X XOR 0x%02X", command, command_inv);
        return;
    }
    
    ESP_LOGI(TAG, "✅ NEC OK: Adres=0x%02X, Komut=0x%02X", address, command);
    
    // Process IR command
    process_ir_command(address, command);
}

// IR alıcı - Basit GPIO polling (NEC protokolü)
void ir_rx_init() {
    ESP_LOGI(TAG, "✓ IR alıcı başlatıldı (GPIO polling, Pin %d)", IR_SENSOR_PIN);
    ir_decode_state = IR_IDLE;
    ir_data = 0;
    ir_bit_count = 0;
}

// IR sinyalini oku ve NEC decode et
void ir_rx_task(void *pvParameters) {
    ESP_LOGI(TAG, "IR başladı");
    
    uint8_t last_ir_state = 1;
    int64_t pulse_start_us = esp_timer_get_time();
    uint32_t cycle_count = 0;
    
    while (1) {
        uint8_t ir_state = gpio_get_level(IR_SENSOR_PIN);
        int64_t now_us = esp_timer_get_time();
        
        if (ir_state != last_ir_state) {
            int64_t duration_us = now_us - pulse_start_us;
            
            if (ir_state == 0) {
                // HIGH → LOW (HIGH pulse süresi ölçüldü)
                
                // START pulse: 8-10ms (normal) veya 4-5ms (repeat)
                if ((duration_us >= 8000 && duration_us <= 10000) ||
                    (duration_us >= 4000 && duration_us <= 5000)) {
                    ir_bit_count = 0;
                    ir_data = 0;
                } 
                // Normal bit: 400-2000µs
                else if (ir_bit_count < 32 && duration_us >= 400 && duration_us < 2000) {
                    if (duration_us < 900) {
                        ir_data = (ir_data << 1) | 0;
                    } else {
                        ir_data = (ir_data << 1) | 1;
                    }
                    ir_bit_count++;
                    
                    if (ir_bit_count == 32) {
                        ESP_LOGI(TAG, "✅ KOD: 0x%08lX", ir_data);
                        ir_parse_nec_code(ir_data);
                        ir_bit_count = 0;
                        ir_data = 0;
                    }
                } 
                // STOP/gap: >2000µs incomplete frame reset
                else if (duration_us > 2000 && ir_bit_count > 0 && ir_bit_count < 32) {
                    ir_bit_count = 0;
                    ir_data = 0;
                }
            }
            
            pulse_start_us = now_us;
            last_ir_state = ir_state;
            cycle_count = 0;  // Reset on state change
        }
        
        // Yield with minimal delay - every few cycles
        cycle_count++;
        if (cycle_count > 500) {  // ~500 CPU cycles ≈ few microseconds
            vTaskDelay(pdMS_TO_TICKS(0));
            cycle_count = 0;
        }
    }
}

// NVS (Non-Volatile Storage) işlemleri
void nvs_init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS initialized");
}

void save_target_count(uint32_t target) {
    // Queue'ye kayıt isteği gönder (non-blocking)
    if (nvs_save_queue != NULL) {
        nvs_save_request_t req = {PANEL_CLOSED, 0, 0, 0, target};  // Panel/work/idle/produced = dummy
        xQueueOverwrite(nvs_save_queue, &req);
    }
}

uint32_t load_target_count() {
    nvs_handle_t my_handle;
    uint32_t target = 0;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        nvs_get_u32(my_handle, "target_cnt", &target);
        nvs_close(my_handle);
    }
    ESP_LOGI(TAG, "Hedef adet yüklendi: %lu", target);
    return target;
}

void save_system_state(uint8_t panel, uint32_t work, uint32_t idle, uint32_t produced) {
    // Queue'ye kayıt isteği gönder (eski değeri değiştirir, non-blocking)
    if (nvs_save_queue != NULL) {
        nvs_save_request_t req = {panel, work, idle, produced, sys_data.target_count};
        xQueueOverwrite(nvs_save_queue, &req);  // Latest değeri tut
    }
}

system_state_backup_t load_system_state() {
    system_state_backup_t state = {PANEL_CLOSED, STATE_IDLE, 0, 0, 0, 0};
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        nvs_get_u8(my_handle, "panel_state", &state.panel_st);
        nvs_get_u8(my_handle, "current_state", &state.current_st);  // STATE oku
        nvs_get_u32(my_handle, "work_time", &state.work_t);
        nvs_get_u32(my_handle, "idle_time", &state.idle_t);
        nvs_get_u32(my_handle, "produced_cnt", &state.prod_cnt);
        nvs_get_u32(my_handle, "last_update", &state.last_upd);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Sistem durumu yüklendi (Panel:%d, State:%d, Work:%lu, Idle:%lu)", 
                 state.panel_st, state.current_st, state.work_t, state.idle_t);
    }
    return state;
}

// NVS yazma task'ı (background, non-blocking)
void nvs_save_task(void *pvParameters) {
    nvs_save_request_t req;
    nvs_save_request_t last_req = {0xFF, 0, 0, 0, 0};  // Son yazılan değer
    uint32_t last_save_time = 0;
    
    while (1) {
        // Queue'den kayıt isteği bekle (500ms timeout - sık olmaz)
        if (xQueueReceive(nvs_save_queue, &req, pdMS_TO_TICKS(500)) == pdTRUE) {
            uint32_t now = xTaskGetTickCount();
            
            // Eğer 1 saniye geçmişse veya farklı veri varsa yazma yap
            if ((now - last_save_time) >= 1000 || 
                req.panel != last_req.panel || 
                req.work != last_req.work || 
                req.idle != last_req.idle || 
                req.produced != last_req.produced ||
                req.target != last_req.target) {
                
                nvs_handle_t my_handle;
                esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
                if (err == ESP_OK) {
                    // Sadece hedef adet mi yazılıyor (system state değil)?
                    if (req.target != last_req.target && req.panel == PANEL_CLOSED) {
                        nvs_set_u32(my_handle, "target_cnt", req.target);
                        ESP_LOGI(TAG, "💾 Hedef adet kaydedildi: %lu", req.target);
                    } else {
                        // Sistem durumu yazma
                        nvs_set_u8(my_handle, "panel_state", req.panel);
                        nvs_set_u8(my_handle, "current_state", (uint8_t)current_state);  // STATE kaydı
                        nvs_set_u32(my_handle, "work_time", req.work);
                        nvs_set_u32(my_handle, "idle_time", req.idle);
                        nvs_set_u32(my_handle, "produced_cnt", req.produced);
                        nvs_set_u32(my_handle, "last_update", get_wall_time_seconds());
                        ESP_LOGI(TAG, "💾 Sistem durumu kaydedildi (Panel:%d, State:%d, Work:%lu, Idle:%lu)", req.panel, (uint8_t)current_state, req.work, req.idle);
                    }
                    nvs_commit(my_handle);
                    nvs_close(my_handle);
                    
                    last_req = req;
                    last_save_time = now;
                }
            }
        }
    }
}

// IR komut işleme
void process_ir_command(uint8_t address, uint8_t command) {
    // IR remote button mapping
    ESP_LOGI(TAG, "IR işleme: Adres=0x%02X, Komut=0x%02X", address, command);
    
    // Silme tuşu (0xFE address)
    if (address == 0xFE) {
        sys_data.target_count = 0;
        ESP_LOGI(TAG, "IR: Hedef adet silindi");
        save_target_count(0);  // Kaydet
        update_scan_data();
        return;
    }
    
    // Açma/Kapama tuşu - STANDART KUMANDA: 0xFF/0xFE, NON-STANDARD: 0xFF/0x1D (0x33B800FF)
    if ((address == 0xFF && command == 0xFE) || (address == 0xFF && command == 0x1D)) {
        if (panel_state == PANEL_CLOSED) {
            // PANEL AÇ - HER ZAMAN YENİ BAŞLANGIC
            panel_state = PANEL_OPEN;
            current_state = STATE_IDLE;  // Manuel olarak buton basılınca başlasın
            
            // En son kaydedilen hedef adet'i yükle
            sys_data.target_count = load_target_count();
            
            // Değerler sıfırla (IR ile açılırsa daima yeni başlangıç)
            sys_data.idle_time = 0;
            sys_data.work_time = 0;
            sys_data.produced_count = 0;
            
            ESP_LOGI(TAG, "🆕 Panel açıldı (IR) - Yeni başlangıç");
            
            // Durumu HEMEN kaydet
            nvs_handle_t my_handle;
            esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
            if (err == ESP_OK) {
                nvs_set_u8(my_handle, "panel_state", panel_state);
                nvs_set_u8(my_handle, "current_state", (uint8_t)current_state);
                nvs_set_u32(my_handle, "work_time", sys_data.work_time);
                nvs_set_u32(my_handle, "idle_time", sys_data.idle_time);
                nvs_set_u32(my_handle, "produced_cnt", sys_data.produced_count);
                nvs_set_u32(my_handle, "last_update", get_wall_time_seconds());  // ⏱️ TIMESTAMP
                nvs_commit(my_handle);
                nvs_close(my_handle);
                ESP_LOGI(TAG, "💾 Panel AÇILDI - HEMEN kaydedildi");
            }
        } else {
            // PANEL KAPAT - Değerleri sıfırla
            panel_state = PANEL_CLOSED;
            current_state = STATE_IDLE;
            sys_data.idle_time = 0;
            sys_data.work_time = 0;
            sys_data.produced_count = 0;
            
            // Durumu HEMEN kaydet (sıfırlanmış)
            nvs_handle_t my_handle;
            esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
            if (err == ESP_OK) {
                nvs_set_u32(my_handle, "target_cnt", sys_data.target_count);
                nvs_set_u8(my_handle, "panel_state", panel_state);
                nvs_set_u8(my_handle, "current_state", (uint8_t)current_state);
                nvs_set_u32(my_handle, "work_time", sys_data.work_time);
                nvs_set_u32(my_handle, "idle_time", sys_data.idle_time);
                nvs_set_u32(my_handle, "produced_cnt", sys_data.produced_count);
                nvs_set_u32(my_handle, "last_update", get_wall_time_seconds());  // ⏱️ TIMESTAMP
                nvs_commit(my_handle);
                nvs_close(my_handle);
                ESP_LOGI(TAG, "💾 Panel KAPANDI - HEMEN kaydedildi (sıfırlandı)");
            }
            
            ESP_LOGI(TAG, "IR: Pano kapatıldı - Ekran sönüyor");
        }
        update_scan_data();
        return;
    }
    
    // Panel açık değilse rakam tuşlarını işleme
    if (panel_state == PANEL_CLOSED) {
        return;
    }
    
    // Rakam tuşları - STANDART ve NON-STANDARD kumandalar
    uint8_t digit = 0xFF;
    
    // STANDART KUMANDA: address=rakam_kodu, command=0xFE
    if (address != 0xFF && address != 0xFE) {
        if (address == 0xEE) digit = 1;
        else if (address == 0xED) digit = 2;
        else if (address == 0xEC) digit = 3;
        else if (address == 0xEB) digit = 4;
        else if (address == 0xEA) digit = 5;
        else if (address == 0xE9) digit = 6;
        else if (address == 0xE8) digit = 7;
        else if (address == 0xE7) digit = 8;
        else if (address == 0xE6) digit = 9;
        else if (address == 0xEF) digit = 0;
    }
    // NON-STANDARD KUMANDA (0x33B8xxxx): address=0xFF, command=rakam_kodu
    else if (address == 0xFF && command != 0xFE && command != 0x1D) {
        // Bu branch, 0x33B8xxxx kodları için (address=0xFF olur)
        // Rakam mapping'i command'a göre
        if (command == 0x07) digit = 1;
        else if (command == 0x15) digit = 2;  
        else if (command == 0x0D) digit = 3;
        else if (command == 0x0C) digit = 4;
        else if (command == 0x18) digit = 5;
        else if (command == 0x5E) digit = 6;
        else if (command == 0x08) digit = 7;
        else if (command == 0x1C) digit = 8;
        else if (command == 0x5A) digit = 9;
        else if (command == 0x52) digit = 0;
    }
    
    if (digit != 0xFF) {
        // Hedef adet'i sola kaydır, yeni rakamı sağdan ekle
        sys_data.target_count = (sys_data.target_count % 10000) * 10 + digit;
        
        // Max 5 digit (99999)
        if (sys_data.target_count > 99999) {
            sys_data.target_count = digit;  // Reset ve yeni rakamdan başla
        }
        
        ESP_LOGI(TAG, "IR: Hedef adet → %lu", sys_data.target_count);
        save_target_count(sys_data.target_count);  // Kaydet
        update_scan_data();
    }
}

// Hedef adet girişi (IR kumanda sayı tuşları: 0-9)
void ir_set_target_count(uint8_t digit, uint8_t position) {
    // position: 0-4 (sağdan sola)
    uint32_t multiplier = 1;
    for (int i = 0; i < position; i++) {
        multiplier *= 10;
    }
    
    // Hedef adette belirli hanemi güncelle
    uint32_t old_digit = (sys_data.target_count / multiplier) % 10;
    sys_data.target_count = sys_data.target_count - (old_digit * multiplier) + (digit * multiplier);
    
    ESP_LOGI(TAG, "Hedef adet: %lu", sys_data.target_count);
    update_scan_data();
}

// Pano aç (IR kumanda ile)
void ir_open_panel() {
    current_state = STATE_IDLE;
    sys_data.idle_time = 0;
    sys_data.work_time = 0;
    sys_data.produced_count = 0;
    sys_data.target_count = 0;
    
    ESP_LOGI(TAG, "Pano açıldı - Hedef adet girişi bekleniyor");
    update_scan_data();
}

// Pano kapat/durdur (IR kumanda ile)
void ir_close_panel() {
    current_state = STATE_IDLE;
    ESP_LOGI(TAG, "Pano kapatıldı - Tüm veriler sıfırlandı");
    
    ESP_LOGI(TAG, "Sonuç - Atıl: %lu, Çalışma: %lu, Üretim: %lu/%lu", 
             sys_data.idle_time, sys_data.work_time, 
             sys_data.produced_count, sys_data.target_count);
    
    // Verileri sıfırla
    sys_data.idle_time = 0;
    sys_data.work_time = 0;
    sys_data.produced_count = 0;
    sys_data.target_count = 0;
    update_scan_data();
}

void app_main() {
    ESP_LOGI(TAG, "Sistem başlıyor...");
    
    // NVS initialization
    nvs_init();
    
    gpio_init();
    i2c_init();
    ir_rx_init();
    
    // IR task için watchdog'u disable et (tight polling loop)
    esp_task_wdt_deinit();
    
    // POWER-ON RECOVERY: Son durumu EEPROM'dan oku
    system_state_backup_t last_state = load_system_state();
    
    // Son duruma göre sistemi initiyalize et
    if (last_state.panel_st == PANEL_OPEN && last_state.current_st == STATE_RUNNING) {
        // Cihaz açık halde kapatılmıştı VE ÇALIŞIYORDU - durumu geri yükle
        panel_state = PANEL_OPEN;
        current_state = STATE_RUNNING;  // Recovery: STATE_RUNNING'den devam
        sys_data.work_time = last_state.work_t;
        sys_data.idle_time = last_state.idle_t;
        sys_data.produced_count = last_state.prod_cnt;
        sys_data.target_count = load_target_count();
        
        // ⏱️ TIME-DELTA HESAPLA: Offline süresi ekle
        uint32_t current_time = get_wall_time_seconds();
        if (last_state.last_upd > 0 && current_time > last_state.last_upd) {
            uint32_t offline_seconds = current_time - last_state.last_upd;
            if (offline_seconds < 86400) {  // 0 ile 24 saat arasındaysa
                sys_data.work_time += offline_seconds;
                ESP_LOGI(TAG, "⏱️ Offline süresi: %lu saniye → work_time += %lu (Toplam: %lu)", 
                         offline_seconds, offline_seconds, sys_data.work_time);
            }
        }
        
        ESP_LOGI(TAG, "🔄 RECOVERY: Panel açıktı ve çalışıyordu - STATE_RUNNING devam ediyor (Work:%lu, Idle:%lu)", 
                 sys_data.work_time, sys_data.idle_time);
    } else if (last_state.panel_st == PANEL_OPEN && last_state.current_st == STATE_PAUSED) {
        // Cihaz açık halde kapatılmıştı VE DURAKLATILMIŞTI - devam et (atıl zaman sayacak)
        panel_state = PANEL_OPEN;
        current_state = STATE_PAUSED;  // Recovery: STATE_PAUSED'dan devam
        sys_data.work_time = last_state.work_t;
        sys_data.idle_time = last_state.idle_t;
        sys_data.produced_count = last_state.prod_cnt;
        sys_data.target_count = load_target_count();
        
        // ⏱️ TIME-DELTA HESAPLA: Offline süresi idle_time'a ekle
        uint32_t current_time = get_wall_time_seconds();
        if (last_state.last_upd > 0 && current_time > last_state.last_upd) {
            uint32_t offline_seconds = current_time - last_state.last_upd;
            if (offline_seconds < 86400) {  // 0 ile 24 saat arasındaysa
                sys_data.idle_time += offline_seconds;
                ESP_LOGI(TAG, "⏱️ Offline süresi: %lu saniye → idle_time += %lu (Toplam: %lu)", 
                         offline_seconds, offline_seconds, sys_data.idle_time);
            }
        }
        
        ESP_LOGI(TAG, "🔄 RECOVERY: Panel açıktı ve duraklatılmıştı - STATE_PAUSED devam ediyor (Work:%lu, Idle:%lu)", 
                 sys_data.work_time, sys_data.idle_time);
    } else {
        // Cihaz kapalı durumda VEYA açık ama STATE_IDLE - yeni başlangıç
        panel_state = last_state.panel_st;  // NVS'deki panel durumunu oku (CLOSED ise CLOSED kalsın)
        current_state = STATE_IDLE;  // Manuel olarak buton basılınca başlasın
        sys_data.idle_time = 0;
        sys_data.work_time = 0;
        sys_data.produced_count = 0;
        sys_data.target_count = load_target_count();
        
        if (panel_state == PANEL_CLOSED) {
            ESP_LOGI(TAG, "Panel KAPALI - İlk açılışa hazır");
        } else {
            ESP_LOGI(TAG, "Panel AÇIK ama STATE_IDLE - Butona basılmaya hazır");
        }
    }
    
    // Başlangıç verilerini güncelle
    update_scan_data();

    // NVS queue oluştur (1 eleman - sadece latest kayıt tut, sık yazma yok)
    nvs_save_queue = xQueueCreate(1, sizeof(nvs_save_request_t));

    // Display scanning task'ı başlat (multiplexing) - CORE 0
    xTaskCreatePinnedToCore(display_scan_task, "display_scan", 2048, NULL, 3, NULL, 0);

    // Zamanlayıcı, buton ve IR task'larını başlat
    xTaskCreate(timer_task, "timer_task", 2048, NULL, 2, NULL);
    xTaskCreate(button_task, "button_task", 2048, NULL, 2, NULL);
    xTaskCreate(ir_rx_task, "ir_rx_task", 2048, NULL, 1, NULL);
    
    // NVS kayıt task'ı - CORE 1 (hafıza yazma, display'i rahatsız etmesin)
    xTaskCreatePinnedToCore(nvs_save_task, "nvs_save", 2048, NULL, 1, NULL, 1);
}