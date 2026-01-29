/*
 * KlimasanAndonV2 - LED Strip Module
 * Cycle Bar - Her turuncu basışta sıfırlanır
 * 
 * Renk Kuralları:
 * 0.0 - 0.7  : Yeşil
 * 0.7 - 0.9  : Turuncu
 * 0.9 - 1.0  : Kırmızı
 * > 1.0      : Kırmızı + Buzzer Alarm
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"

#include "led_strip.h"
#include "led_strip_encoder.h"
#include "pin_config.h"
#include "system_state.h"
#include "andon_display.h"

static const char *TAG = "led_strip";

// ============ LED Pixel Buffer ============
static uint8_t led_strip_pixels[LED_STRIP_LED_COUNT * 3];

// ============ RMT Handles ============
static rmt_channel_handle_t g_led_chan = NULL;
static rmt_encoder_handle_t g_led_encoder = NULL;

// ============ Base Colors (RGB) ============
#define GREEN_R     0
#define GREEN_G     255
#define GREEN_B     0

#define ORANGE_R    255
#define ORANGE_G    80
#define ORANGE_B    0

#define RED_R       255
#define RED_G       0
#define RED_B       0

// ============ State Variables ============
static volatile float g_brightness = 0.3f; 
static volatile uint32_t g_cycle_target_sec = DEFAULT_CYCLE_TARGET_SEC;
static volatile uint32_t g_cycle_elapsed = 0;  
static volatile uint32_t g_frame_counter = 0;   
static volatile bool g_cycle_running = false;
static volatile bool g_alarm_active = false;
static volatile bool g_alarm_acknowledged = false;
static bool g_menu_preview = false;

// Parlaklık kademeleri (1-5)
static const float brightness_levels[] = {0.0f, 0.05f, 0.15f, 0.35f, 0.65f, 1.0f};

// ============ Helper Functions ============

static void set_rgb(int idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx < 0 || idx >= LED_STRIP_LED_COUNT) return;
    int base = idx * 3;
    float br = g_brightness;
    // GRB format for WS2812
    led_strip_pixels[base + 0] = (uint8_t)(g * br);
    led_strip_pixels[base + 1] = (uint8_t)(r * br);
    led_strip_pixels[base + 2] = (uint8_t)(b * br);
}

static void transmit_leds(void) {
    if (g_led_chan == NULL || g_led_encoder == NULL) return;
    
    rmt_transmit_config_t tx_config = {.loop_count = 0};
    esp_err_t ret = rmt_transmit(g_led_chan, g_led_encoder, led_strip_pixels,
                                 sizeof(led_strip_pixels), &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(ret));
        return;
    }
    // Wait for transmit to complete
    ret = rmt_tx_wait_all_done(g_led_chan, pdMS_TO_TICKS(100)); // 30 FPS is 33ms, 100ms is more than safe
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_tx_wait failed: %s", esp_err_to_name(ret));
    }
}

static void render_cycle_bar(float ratio) {
    int filled = (int)(ratio * LED_STRIP_LED_COUNT);
    if (filled > LED_STRIP_LED_COUNT) filled = LED_STRIP_LED_COUNT;
    if (filled < 0) filled = 0;
    
    int green_end = (int)(0.7f * LED_STRIP_LED_COUNT);
    int orange_end = (int)(0.9f * LED_STRIP_LED_COUNT);
    
    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
        // Fiziksel yön değişimi için indeksi ters çeviriyoruz (Soldan Sağa ilerleme için)
        int led_idx = LED_STRIP_LED_COUNT - 1 - i;
        
        if (i < filled) {
            // Hangi renk bölgesinde
            if (i < green_end) {
                set_rgb(led_idx, GREEN_R, GREEN_G, GREEN_B);
            } else if (i < orange_end) {
                set_rgb(led_idx, ORANGE_R, ORANGE_G, ORANGE_B);
            } else {
                set_rgb(led_idx, RED_R, RED_G, RED_B);
            }
        } else {
            set_rgb(led_idx, 0, 0, 0);
        }
    }
}

static void clear_all_leds(void) {
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
}

// ============ Buzzer Control ============

static void buzzer_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    gpio_set_level(BUZZER_PIN, 0); // Start with LOW (SILENT)
}

static void buzzer_on(void) {
    gpio_set_level(BUZZER_PIN, 1); // HIGH = ON
}

static void buzzer_off(void) {
    gpio_set_level(BUZZER_PIN, 0); // LOW = OFF
}

// ============ Cycle Task ============

static void led_strip_task(void *arg) {
    (void)arg;
    uint32_t blink_counter = 0;
    bool blink_state = true;
    bool last_running = false;
    
    ESP_LOGI(TAG, "LED task started (Core 1, 30 FPS)");

    while (1) {
        if (g_menu_preview) {
            render_cycle_bar(1.0f);
            transmit_leds();
            last_running = true;
        } else if (g_cycle_running) {
            // Sadece WORK modunda sayaç ilerler
            if (current_mode == MODE_WORK) {
                last_running = true;
                g_frame_counter++;
                
                // 30 FPS için 30 frame = 1 saniye
                if (g_frame_counter >= 30) {
                    g_cycle_elapsed++;
                    g_frame_counter = 0;
                }
            } else {
                // WORK modunda değiliz, bar olduğu yerde durmalı
                // last_running true kalır ki rendering devam etsin ama g_frame_counter artmaz
                last_running = true;
            }
            
            // Alt-saniye hassasiyeti (sub-second precision) ile ratio hesapla
            // Bu sayede 107 LED saniye atlamadan, tek tek (smooth) ilerler
            float ratio = ((float)g_cycle_elapsed + ((float)g_frame_counter / 30.0f)) / (float)g_cycle_target_sec;
            
            if (ratio > 1.0f && !g_alarm_acknowledged) {
                g_alarm_active = true;
                blink_counter++;
                if (blink_counter >= 15) { // ~0.5s blink rate at 30fps
                    blink_state = !blink_state;
                    blink_counter = 0;
                }
                
                if (blink_state) {
                    render_cycle_bar(1.0f);
                    buzzer_on();
                } else {
                    clear_all_leds();
                    buzzer_off();
                }
                transmit_leds();
            } else {
                g_alarm_active = false;
                buzzer_off();
                render_cycle_bar(ratio);
                transmit_leds();
            }
        } else {
            if (last_running) {
                clear_all_leds();
                transmit_leds();
                last_running = false;
                buzzer_off();
            }
        }

        // Saat ayarı yan-sön logic (30 FPS hızında kontrol ediyoruz)
        if (sys_data.clock_step > 0) {
            static uint8_t clock_blink_cnt = 0;
            clock_blink_cnt++;
            if (clock_blink_cnt >= 10) { // ~333ms blink rate (30fps / 10)
                sys_data.clock_blink_on = !sys_data.clock_blink_on;
                clock_blink_cnt = 0;
                // Ayar modundayken ekranı daha sık tazele ki yan-sön akıcı olsun
                andon_display_update();
            }
        } else {
            sys_data.clock_blink_on = true;
        }

        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}

// ============ Public Functions ============

esp_err_t led_strip_init(void) {
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &g_led_chan));

    led_strip_encoder_config_t encoder_config = {
        .resolution = LED_STRIP_RMT_RES_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &g_led_encoder));
    ESP_ERROR_CHECK(rmt_enable(g_led_chan));

    buzzer_init();

    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
    transmit_leds();

    ESP_LOGI(TAG, "LED strip initialized (GPIO %d, %d LEDs)", 
             LED_STRIP_GPIO_NUM, LED_STRIP_LED_COUNT);
    return ESP_OK;
}

void led_strip_start_task(void) {
    xTaskCreatePinnedToCore(led_strip_task, "led_strip_task", 4096, NULL, 10, NULL, 1);
}

void led_strip_set_brightness(float brightness) {
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    g_brightness = brightness;
}

void led_strip_start_cycle(void) {
    g_cycle_elapsed = 0;
    g_frame_counter = 0;
    g_cycle_running = true;
    g_alarm_active = false;
    g_alarm_acknowledged = false;
    ESP_LOGI(TAG, "Cycle started (%lu sec)", (unsigned long)g_cycle_target_sec);
}

void led_strip_set_cycle_target(uint32_t seconds) {
    if (seconds < 1) seconds = 1;
    g_cycle_target_sec = seconds;
}

uint32_t led_strip_get_cycle_target(void) {
    return g_cycle_target_sec;
}

bool led_strip_is_alarm_active(void) {
    return g_alarm_active;
}

void led_strip_acknowledge_alarm(void) {
    g_alarm_acknowledged = true;
    g_alarm_active = false;
    buzzer_off();
}

void led_strip_clear(void) {
    g_cycle_running = false;
    g_menu_preview = false;
    buzzer_off();
}

void led_strip_set_menu_preview(bool active) {
    g_menu_preview = active;
}

void led_strip_set_brightness_idx(uint8_t index) {
    if (index >= 1 && index <= 5) {
        g_brightness = brightness_levels[index];
    }
}
