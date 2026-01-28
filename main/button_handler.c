/*
 * KlimasanAndonV2 - Button Handler Module
 * 4 buton: Yeşil (WORK), Kırmızı (IDLE), Sarı (PLANNED), Turuncu (Adet+1)
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "button_handler.h"
#include "pin_config.h"

static const char *TAG = "button_handler";

// Callback function
static button_callback_t g_button_callback = NULL;

// Debounce parameters
#define DEBOUNCE_MS     150
#define RELEASE_MS      80

// ============ Button Task ============

static void button_task(void *pvParameters) {
    uint8_t last_state[4] = {1, 1, 1, 1};
    uint32_t last_press_time[4] = {0, 0, 0, 0};
    bool button_held[4] = {false, false, false, false};
    uint32_t high_duration[4] = {0, 0, 0, 0};
    
    const gpio_num_t btn_pins[4] = {
        BUTTON_GREEN_PIN,
        BUTTON_RED_PIN,
        BUTTON_YELLOW_PIN,
        BUTTON_ORANGE_PIN
    };
    
    const button_event_t btn_events[4] = {
        BUTTON_EVENT_GREEN,
        BUTTON_EVENT_RED,
        BUTTON_EVENT_YELLOW,
        BUTTON_EVENT_ORANGE
    };
    
    const char *btn_names[4] = {"GREEN", "RED", "YELLOW", "ORANGE"};
    
    ESP_LOGI(TAG, "Button task started (4 buttons)");
    
    while (1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        for (int i = 0; i < 4; i++) {
            uint8_t btn_state = gpio_get_level(btn_pins[i]);
            
            // Button press detection
            if (btn_state == 0 && last_state[i] == 1 && !button_held[i]) {
                if ((current_time - last_press_time[i]) > DEBOUNCE_MS) {
                    ESP_LOGI(TAG, "%s button pressed", btn_names[i]);
                    if (g_button_callback) {
                        g_button_callback(btn_events[i]);
                    }
                    last_press_time[i] = current_time;
                    button_held[i] = true;
                }
            }
            
            // Release detection
            if (btn_state == 0) {
                high_duration[i] = 0;
            } else {
                if (high_duration[i] < RELEASE_MS) {
                    high_duration[i] += 10;
                }
                if (button_held[i] && high_duration[i] >= RELEASE_MS) {
                    button_held[i] = false;
                }
            }
            last_state[i] = btn_state;
        }
    }
}

// ============ GPIO Initialization ============

static void gpio_init_buttons(void) {
    gpio_config_t io_conf_buttons = {
        .pin_bit_mask = (1ULL << BUTTON_GREEN_PIN) | (1ULL << BUTTON_RED_PIN) | 
                        (1ULL << BUTTON_YELLOW_PIN) | (1ULL << BUTTON_ORANGE_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,  // GPIO 34-39 dahili pullup yok
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf_buttons);
    ESP_LOGI(TAG, "Button GPIO initialized (Pins %d, %d, %d, %d)", 
             BUTTON_GREEN_PIN, BUTTON_RED_PIN, BUTTON_YELLOW_PIN, BUTTON_ORANGE_PIN);
}

// ============ Public Functions ============

esp_err_t button_handler_init(void) {
    gpio_init_buttons();
    ESP_LOGI(TAG, "Button handler initialized");
    return ESP_OK;
}

void button_handler_start_task(void) {
    xTaskCreatePinnedToCore(button_task, "button_task", 4096, NULL, 2, NULL, 1);
    ESP_LOGI(TAG, "Button task started (Core 1, Priority 2)");
}

void button_handler_set_callback(button_callback_t callback) {
    g_button_callback = callback;
}
