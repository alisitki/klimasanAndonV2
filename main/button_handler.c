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
    // active_level: buton basıldığında GPIO'nun aldığı değer
    // NO (Normal-Open, aktif-LOW) = 0
    // NC (Normal-Closed, aktif-HIGH) = 1  ← Kırmızı buton NC kontak
    const uint8_t active_level[4] = {0, 1, 0, 0}; // Yeşil, Kırmızı, Sarı, Turuncu

    // Başlangıç durumu: buton bırakılmış (idle) seviyesi = 1 - active_level
    uint8_t last_state[4] = {
        (uint8_t)(1 - active_level[0]),
        (uint8_t)(1 - active_level[1]),
        (uint8_t)(1 - active_level[2]),
        (uint8_t)(1 - active_level[3]),
    };
    uint32_t last_press_time[4] = {0, 0, 0, 0};
    bool button_held[4] = {false, false, false, false};
    uint32_t release_duration[4] = {0, 0, 0, 0};

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

    ESP_LOGI(TAG, "Button task started (4 buttons, RED=NC)");

    while (1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);

        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // === DIAGNOSIS: Her 2 saniyede ham GPIO değerlerini logla ===
        static uint32_t diag_counter = 0;
        if (++diag_counter >= 200) {  // 200 * 10ms = 2 saniye
            diag_counter = 0;
            ESP_LOGI(TAG, "BTN RAW: GREEN(G%d)=%d  RED(G%d)=%d  YELLOW(G%d)=%d  ORANGE(G%d)=%d",
                btn_pins[0], gpio_get_level(btn_pins[0]),
                btn_pins[1], gpio_get_level(btn_pins[1]),
                btn_pins[2], gpio_get_level(btn_pins[2]),
                btn_pins[3], gpio_get_level(btn_pins[3]));
        }

        for (int i = 0; i < 4; i++) {
            uint8_t raw = gpio_get_level(btn_pins[i]);
            uint8_t pressed = (raw == active_level[i]) ? 1 : 0;  // 1=basılı, 0=serbest
            uint8_t last_pressed = (last_state[i] == active_level[i]) ? 1 : 0;

            // Press edge: serbest → basılı
            if (pressed && !last_pressed && !button_held[i]) {
                if ((current_time - last_press_time[i]) > DEBOUNCE_MS) {
                    ESP_LOGI(TAG, "%s button pressed", btn_names[i]);
                    if (g_button_callback) {
                        g_button_callback(btn_events[i]);
                    }
                    last_press_time[i] = current_time;
                    button_held[i] = true;
                    release_duration[i] = 0;
                }
            }

            // Release detection
            if (pressed) {
                release_duration[i] = 0;
            } else {
                if (release_duration[i] < RELEASE_MS) {
                    release_duration[i] += 10;
                }
                if (button_held[i] && release_duration[i] >= RELEASE_MS) {
                    button_held[i] = false;
                }
            }

            last_state[i] = raw;
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
    TaskHandle_t handle = NULL;
    BaseType_t ret = xTaskCreatePinnedToCore(button_task, "button_task", 8192, NULL, 6, &handle, 1);
    if (ret != pdPASS || handle == NULL) {
        ESP_LOGE(TAG, "Button task creation FAILED! ret=%d", ret);
    } else {
        ESP_LOGI(TAG, "Button task started (Core 1, Priority 6, Stack 8192)");
    }
}

void button_handler_set_callback(button_callback_t callback) {
    g_button_callback = callback;
}
