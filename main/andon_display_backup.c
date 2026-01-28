/*
 * KlimasanAndonV2 - 7-Segment Display Module
 * HC138 + CD4543 ile multiplexed display kontrolü
 */
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "andon_display.h"
#include "pin_config.h"
#include "system_state.h"
#include "rtc_ds1307.h"

static const char *TAG = "andon_display";

static uint8_t scan_data_buffers[2][6][8] = {0};
static volatile int active_buffer = 0;
static volatile int write_buffer = 1;

#define SCAN_DATA_READ  scan_data_buffers[active_buffer]
#define SCAN_DATA_WRITE scan_data_buffers[write_buffer]

void andon_display_select_hane(int hane) {
    gpio_set_level(HC138_A0_PIN, (hane >> 0) & 1);
    gpio_set_level(HC138_A1_PIN, (hane >> 1) & 1);
    gpio_set_level(HC138_A2_PIN, (hane >> 2) & 1);
}

void andon_display_send_bcd(int bcd_value) {
    if (bcd_value == DISPLAY_BLANK) {
        // CD4543 has no blank input, we usually handle this by not pulsing latch
        // or by hardware BL pin if wired. Here we assume we just send 0 or mask.
        return; 
    }
    gpio_set_level(CD4543_D0_PIN, (bcd_value >> 0) & 1);
    gpio_set_level(CD4543_D1_PIN, (bcd_value >> 1) & 1);
    gpio_set_level(CD4543_D2_PIN, (bcd_value >> 2) & 1);
    gpio_set_level(CD4543_D3_PIN, (bcd_value >> 3) & 1);
}

static void display_scan_task(void *pvParameters) {
    const int ld_pins[8] = {
        CD4543_LD1_PIN, CD4543_LD2_PIN, CD4543_LD3_PIN, CD4543_LD4_PIN,
        CD4543_LD5_PIN, CD4543_LD6_PIN, CD4543_LD7_PIN, CD4543_LD8_PIN
    };

    ESP_LOGI(TAG, "Display task started");

    while (1) {
        if (!sys_data.screen_on) {
            andon_display_select_hane(7); // All OFF
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        for (int scan = 0; scan < 6; scan++) {
            // Update all 8 latches for this hane
            for (int latch = 0; latch < 8; latch++) {
                uint8_t val = SCAN_DATA_READ[scan][latch];
                if (val != DISPLAY_BLANK) {
                    andon_display_send_bcd(val);
                    gpio_set_level(ld_pins[latch], 1);
                    esp_rom_delay_us(5);
                    gpio_set_level(ld_pins[latch], 0);
                }
            }

            // Select hane and wait
            andon_display_select_hane(scan);
            esp_rom_delay_us(2000); // 2ms per hane
        }
        
        // Final yield to let other tasks run on this core
        vTaskDelay(0);
    }
}

static void gpio_init_display(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << HC138_A0_PIN) | (1ULL << HC138_A1_PIN) | (1ULL << HC138_A2_PIN) |
                        (1ULL << CD4543_D0_PIN) | (1ULL << CD4543_D1_PIN) | (1ULL << CD4543_D2_PIN) | (1ULL << CD4543_D3_PIN) |
                        (1ULL << CD4543_LD1_PIN) | (1ULL << CD4543_LD2_PIN) | (1ULL << CD4543_LD3_PIN) | (1ULL << CD4543_LD4_PIN) |
                        (1ULL << CD4543_LD5_PIN) | (1ULL << CD4543_LD6_PIN) | (1ULL << CD4543_LD7_PIN) | (1ULL << CD4543_LD8_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
}

esp_err_t andon_display_init(void) {
    gpio_init_display();
    return ESP_OK;
}

void andon_display_start_task(void) {
    xTaskCreatePinnedToCore(display_scan_task, "dsp_scan", 4096, NULL, 5, NULL, 0);
}

void andon_display_update(void) {
    // Fill SCAN_DATA_WRITE from sys_data
    // Simplified for recovery
    active_buffer = write_buffer;
    write_buffer = !active_buffer;
}
