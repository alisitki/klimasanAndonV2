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
#include "rtc_ds1307.h"

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
static volatile float g_brightness = 0.3f; // %10'dan %30'a çıkarıldı
static volatile uint32_t g_cycle_target_sec = DEFAULT_CYCLE_TARGET_SEC;
static volatile uint32_t g_cycle_elapsed = 0;  // Cycle elapsed seconds
static volatile uint32_t g_frame_counter = 0;   // Frame counter for timing
static volatile bool g_cycle_running = false;
static volatile bool g_alarm_active = false;
static volatile bool g_alarm_acknowledged = false;

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
    // Wait for transmit to complete (max 50ms is enough for 107 LEDs, using 200ms for safety)
    ret = rmt_tx_wait_all_done(g_led_chan, pdMS_TO_TICKS(200));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_tx_wait failed: %s", esp_err_to_name(ret));
    }
    
    // Her 5 saniyede bir transmit başarısını logla
    static uint32_t tx_count = 0;
    if (++tx_count % 50 == 0) {
        ESP_LOGI(TAG, "transmit_leds: Success (count=%u)", (unsigned int)tx_count);
    }
}

static void render_cycle_bar(float ratio) {
    // Kaç LED yanacak
    int filled = (int)(ratio * LED_STRIP_LED_COUNT);
    if (filled > LED_STRIP_LED_COUNT) filled = LED_STRIP_LED_COUNT;
    if (filled < 0) filled = 0;
    
    // Renk eşikleri (LED pozisyonuna göre)
    int green_end = (int)(0.7f * LED_STRIP_LED_COUNT);
    int orange_end = (int)(0.9f * LED_STRIP_LED_COUNT);
    
    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
        if (i < filled) {
            // Hangi renk bölgesinde
            if (i < green_end) {
                set_rgb(i, GREEN_R, GREEN_G, GREEN_B);
            } else if (i < orange_end) {
                set_rgb(i, ORANGE_R, ORANGE_G, ORANGE_B);
            } else {
                set_rgb(i, RED_R, RED_G, RED_B);
            }
        } else {
            set_rgb(i, 0, 0, 0);
        }
    }
    // NOT: transmit_leds() task tarafından yapılacak
}

static void clear_all_leds(void) {
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
    // NOT: transmit_leds() task tarafından yapılacak
}

// ============ Buzzer Control ============

static void buzzer_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    gpio_set_level(BUZZER_PIN, 0);
}

static void buzzer_on(void) {
    gpio_set_level(BUZZER_PIN, 1);
}

static void buzzer_off(void) {
    gpio_set_level(BUZZER_PIN, 0);
}

// ============ Cycle Task ============

static void led_strip_task(void *arg) {
    (void)arg;
    
    uint32_t blink_counter = 0;
    bool blink_state = true;
    uint32_t debug_counter = 0;
    bool last_running = false;
    
    ESP_LOGI(TAG, "LED task started, entering main loop");

    while (1) {
        debug_counter++;
        
        if (debug_counter % 50 == 0) {
            ESP_LOGI(TAG, "HEARTBEAT: running=%d, elapsed=%u, target=%u",
                     g_cycle_running, (unsigned)g_cycle_elapsed, (unsigned)g_cycle_target_sec);
        }
        
        if (g_cycle_running) {
            g_frame_counter++;
            if (g_frame_counter % 50 == 0) {
                g_cycle_elapsed++;
                // Her saniye logla
                ESP_LOGI(TAG, "Cycle: %u/%u sec", (unsigned)g_cycle_elapsed, (unsigned)g_cycle_target_sec);
            }
            
            float ratio = (float)g_cycle_elapsed / (float)g_cycle_target_sec;
            
            if (ratio > 1.0f && !g_alarm_acknowledged) {
                g_alarm_active = true;
                blink_counter++;
                if (blink_counter >= 10) {
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
            last_running = true;
        } else {
            // Sadece durduğunda bir kez temizle
            if (last_running) {
                ESP_LOGI(TAG, "Cycle stopped, clearing LEDs once");
                clear_all_leds();
                transmit_leds();
                last_running = false;
                buzzer_off();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}

// ============ Public Functions ============

esp_err_t led_strip_init(void) {
    ESP_LOGI(TAG, "Initializing RMT TX channel");
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, // ESP32 standart kanal boyutu
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &g_led_chan));

    ESP_LOGI(TAG, "Installing LED strip encoder");
    led_strip_encoder_config_t encoder_config = {
        .resolution = LED_STRIP_RMT_RES_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &g_led_encoder));

    ESP_LOGI(TAG, "Enabling RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(g_led_chan));

    // Init buzzer
    buzzer_init();

    // --- Startup Hardware Test ---
    ESP_LOGI(TAG, "Hardware Test: All LEDs Blue for 0.5s");
    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
        set_rgb(i, 0, 0, 255); // Blue
    }
    transmit_leds();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Clear LEDs
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
    transmit_leds();

    ESP_LOGI(TAG, "LED strip initialized (GPIO %d, %d LEDs)", 
             LED_STRIP_GPIO_NUM, LED_STRIP_LED_COUNT);
    return ESP_OK;
}

void led_strip_start_task(void) {
    ESP_LOGI(TAG, "Creating LED strip task...");
    // Priority 10 (IR task 5'ten yüksektir, UI akıcı olmalı)
    BaseType_t ret = xTaskCreatePinnedToCore(led_strip_task, "led_strip_task", 4096, NULL, 10, NULL, 1);
    if (ret == pdPASS) {
        ESP_LOGI(TAG, "LED strip task created successfully (Core 1, Pri 10)");
    } else {
        ESP_LOGE(TAG, "FAILED to create LED strip task! ret=%d", ret);
    }
}

void led_strip_set_brightness(float brightness) {
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    g_brightness = brightness;
    ESP_LOGI(TAG, "Brightness set to %.0f%%", brightness * 100);
}

void led_strip_start_cycle(void) {
    g_cycle_elapsed = 0;  // Reset cycle counter
    g_frame_counter = 0;  // Reset frame counter
    g_cycle_running = true;
    g_alarm_active = false;
    g_alarm_acknowledged = false;
    ESP_LOGI(TAG, "Cycle started (target: %u sec)", (unsigned int)g_cycle_target_sec);
}

void led_strip_set_cycle_target(uint32_t seconds) {
    if (seconds < 1) seconds = 1;
    g_cycle_target_sec = seconds;
    ESP_LOGI(TAG, "Cycle target set to %u seconds", (unsigned int)seconds);
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
    ESP_LOGI(TAG, "Alarm acknowledged");
}

void led_strip_clear(void) {
    g_cycle_running = false;
    // Don't call transmit_leds here to avoid race conditions.
    // The task will see g_cycle_running == false and clear once.
    buzzer_off();
}
