/*
 * KlimasanAndonV2 - IR Remote Module
 * NEC protokolü ile IR kumanda alıcısı
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ir_remote.h"
#include "pin_config.h"

static const char *TAG = "ir_remote";

// IR decode state
static uint32_t ir_data = 0;
static uint8_t ir_bit_count = 0;

// Input state
static ir_input_mode_t g_input_mode = IR_INPUT_NONE;
static uint32_t g_input_value = 0;
static uint8_t g_input_digit_count = 0;

// Callback function
static ir_command_callback_t g_ir_callback = NULL;

// ============ Helper Functions ============

static uint32_t reverse_bits_32(uint32_t value) {
    uint32_t reversed = 0;
    for (int i = 0; i < 32; i++) {
        reversed = (reversed << 1) | ((value >> i) & 1);
    }
    return reversed;
}

static void ir_parse_nec_code(uint32_t code) {
    bool is_non_standard = ((code & 0xFF000000) == 0x33000000);
    
    code = reverse_bits_32(code);
    
    uint8_t address = (code >> 24) & 0xFF;
    uint8_t address_inv = (code >> 16) & 0xFF;
    uint8_t command = (code >> 8) & 0xFF;
    uint8_t command_inv = code & 0xFF;
    
    if (!is_non_standard && (address ^ address_inv) != 0xFF) {
        ESP_LOGE(TAG, "Address checksum fail");
        return;
    }
    
    if (!is_non_standard && (command ^ command_inv) != 0xFF) {
        ESP_LOGE(TAG, "Command checksum fail");
        return;
    }
    
    ESP_LOGI(TAG, "NEC: Addr=0x%02X, Cmd=0x%02X", address, command);
    
    if (g_ir_callback != NULL) {
        g_ir_callback(address, command);
    }
}

// ============ IR Receiver Task ============

static void ir_rx_task(void *pvParameters) {
    ESP_LOGI(TAG, "IR receiver task started");
    
    uint8_t last_ir_state = 1;
    int64_t pulse_start_us = esp_timer_get_time();
    uint32_t idle_yield_counter = 0;
    
    while (1) {
        uint8_t ir_state = gpio_get_level(IR_SENSOR_PIN);
        int64_t now_us = esp_timer_get_time();
        
        if (ir_state != last_ir_state) {
            int64_t duration_us = now_us - pulse_start_us;
            
            if (ir_state == 0) { // HIGH -> LOW edge (just finished a HIGH pulse)
                // NEC: Start HIGH is 4.5ms, Bit 0 HIGH is ~560us, Bit 1 HIGH is ~1.6ms
                if ((duration_us >= 4000 && duration_us <= 5000)) {
                    // Start pulse detected
                    ir_bit_count = 0;
                    ir_data = 0;
                } 
                else if (ir_bit_count < 32 && duration_us >= 400 && duration_us < 2000) {
                    if (duration_us < 900) {
                        ir_data = (ir_data << 1) | 0;
                    } else {
                        ir_data = (ir_data << 1) | 1;
                    }
                    ir_bit_count++;
                    
                    if (ir_bit_count == 32) {
                        ir_parse_nec_code(ir_data);
                        ir_bit_count = 0;
                        ir_data = 0;
                    }
                }
            } else {
                 // LOW -> HIGH edge
                 // Could check for 9ms leading pulse here if needed
            }
            
            pulse_start_us = now_us;
            last_ir_state = ir_state;
            idle_yield_counter = 0;
        } else {
            // No state change
            // Timeout reset if in middle of packet
            if (ir_bit_count > 0 && (now_us - pulse_start_us > 100000)) {
                ir_bit_count = 0;
                ir_data = 0;
            }
            
            // Cooperatively yield
            if (ir_bit_count == 0 && ir_state == 1) {
                // Truly idle
                vTaskDelay(pdMS_TO_TICKS(5)); // Relax more in idle
            } else {
                // In packet or active LOW pulse, yield 0 to come back immediately
                if (++idle_yield_counter > 1000) {
                   vTaskDelay(0);
                   idle_yield_counter = 0;
                }
            }
        }
    }
}

// ============ GPIO Initialization ============

static void gpio_init_ir(void) {
    gpio_config_t io_conf_ir = {
        .pin_bit_mask = (1ULL << IR_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf_ir);
    ESP_LOGI(TAG, "IR GPIO initialized (Pin %d)", IR_SENSOR_PIN);
}

// ============ Input Value Handling ============

void ir_remote_add_digit(uint8_t digit) {
    if (g_input_mode == IR_INPUT_NONE) return;
    if (digit > 9) return;
    
    g_input_value = g_input_value * 10 + digit;
    if (g_input_value > 9999) g_input_value %= 10000;
    
    ESP_LOGI(TAG, "Input value: %u (mode: %d)", (unsigned int)g_input_value, g_input_mode);
}

// ============ Public Functions ============

esp_err_t ir_remote_init(void) {
    gpio_init_ir();
    ir_data = 0;
    ir_bit_count = 0;
    ESP_LOGI(TAG, "IR remote initialized");
    return ESP_OK;
}

void ir_remote_start_task(void) {
    // Priority 5 (LED task 10'dur, onu ezmez), Core 1'e sabitle
    xTaskCreatePinnedToCore(ir_rx_task, "ir_rx_task", 4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "IR receiver task started (Core 1, Priority 5)");
}

void ir_remote_set_callback(ir_command_callback_t callback) {
    g_ir_callback = callback;
}

ir_input_mode_t ir_remote_get_input_mode(void) {
    return g_input_mode;
}

void ir_remote_set_input_mode(ir_input_mode_t mode) {
    g_input_mode = mode;
    g_input_value = 0;
    g_input_digit_count = 0;
    ESP_LOGI(TAG, "Input mode set to %d", mode);
}

void ir_remote_clear_input(void) {
    g_input_value = 0;
    g_input_digit_count = 0;
}

uint32_t ir_remote_get_input_value(void) {
    return g_input_value;
}
