/*
 * KlimasanAndonV2 - Pin Konfigürasyonu
 * Tüm GPIO pin tanımları tek bir yerde
 */
#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// ============ 74HC138 Seçim Pinleri (6 tarama için) ============
#define HC138_A0_PIN    23
#define HC138_A1_PIN    13
#define HC138_A2_PIN    14

// ============ CD4543 Data Pinleri (BCD) ============
#define CD4543_D0_PIN   0
#define CD4543_D1_PIN   12
#define CD4543_D2_PIN   15
#define CD4543_D3_PIN    2

// ============ CD4543 Latch Display (LD) Pinleri ============
// LD1: 6 digit - SAAT (HH:MM:SS)
// LD2: 4 digit - DURUŞ SÜRESİ (MM:SS)
// LD3: 6 digit - ÇALIŞMA ZAMANI (HH:MM:SS)
// LD4: 6 digit - ATIL ZAMAN (HH:MM:SS)
// LD5: 6 digit - PLANLI DURUŞ (HH:MM:SS)
// LD6: 4 digit - HEDEF ADET
// LD7: 4 digit - GERÇEKLEŞEN ADET
// LD8: 2 digit - VERİM (%)
#define CD4543_LD1_PIN  22
#define CD4543_LD2_PIN  21
#define CD4543_LD3_PIN  19
#define CD4543_LD4_PIN  18
#define CD4543_LD5_PIN   5
#define CD4543_LD6_PIN  17
#define CD4543_LD7_PIN  16
#define CD4543_LD8_PIN   4

#define NUM_LATCHES     8

// ============ I2C DS1307 ============
#define I2C_SDA_PIN     25
#define I2C_SCL_PIN     33
#define DS1307_ADDR     0x68

// ============ IR Sensör ============
#define IR_SENSOR_PIN   27

// ============ Butonlar ============
#define BUTTON_GREEN_PIN    35  // Yeşil - WORK moduna geç
#define BUTTON_RED_PIN      34  // Kırmızı - IDLE moduna geç
#define BUTTON_YELLOW_PIN   36  // Sarı - PLANNED moduna geç
#define BUTTON_ORANGE_PIN   39  // Turuncu - Adet +1

// ============ Buzzer ============
#define BUZZER_PIN      32

// ============ WS2812B LED Strip ============
#define LED_STRIP_GPIO_NUM      26
#define LED_STRIP_LED_COUNT     107
#define LED_STRIP_RMT_RES_HZ    10000000  // 10MHz RMT çözünürlüğü

#endif // PIN_CONFIG_H
