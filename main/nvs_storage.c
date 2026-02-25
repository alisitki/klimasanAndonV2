/*
 * KlimasanAndonV2 - NVS Storage Module
 * Non-Volatile Storage işlemleri
 */
#include <stdint.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nvs_storage.h"
#include "rtc_ds1307.h"
#include "system_state.h"
#include "led_strip.h"

static const char *TAG = "nvs_storage";

// NVS save queue
static QueueHandle_t nvs_save_queue = NULL;

// Queue mesaj tipleri
#define NVS_SAVE_THROTTLED  0   // Normal kayit, throttle uygulanir
#define NVS_SAVE_URGENT     1   // Acil kayit, throttle atlanir

// ============ NVS Save Task ============

static void nvs_save_task(void *pvParameters) {
    uint32_t last_save_time = 0;
    uint8_t msg;
    
    ESP_LOGI(TAG, "NVS save task started (Core 0)");
    
    while (1) {
        if (xQueueReceive(nvs_save_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            bool urgent = (msg == NVS_SAVE_URGENT);
            
            // Urgent: throttle atla. Normal: 2 saniye arayla kaydet.
            if (urgent || (now - last_save_time) >= 2000) {
                nvs_handle_t my_handle;
                esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
                if (err == ESP_OK) {
                    // valid=0: yazim basliyor
                    nvs_set_u8(my_handle, "valid", 0);
                    nvs_commit(my_handle);

                    nvs_set_u8(my_handle, "work_mode", (uint8_t)current_mode);
                    nvs_set_u8(my_handle, "shift_state", (uint8_t)shift_state);
                    nvs_set_u32(my_handle, "work_time", sys_data.work_time);
                    nvs_set_u32(my_handle, "idle_time", sys_data.idle_time);
                    nvs_set_u32(my_handle, "planned_time", sys_data.planned_time);
                    nvs_set_u32(my_handle, "produced_cnt", sys_data.produced_count);
                    nvs_set_u32(my_handle, "target_cnt", sys_data.target_count);
                    nvs_set_u32(my_handle, "cycle_target", led_strip_get_cycle_target());
                    nvs_set_u32(my_handle, "durus_time", sys_data.durus_time);
                    nvs_set_u32(my_handle, "last_update", rtc_get_wall_time_seconds());

                    // valid=1: tum veriler yazildi
                    nvs_set_u8(my_handle, "valid", 1);
                    nvs_commit(my_handle);
                    nvs_close(my_handle);
                    
                    ESP_LOGI(TAG, "%s saved (Mode:%d, Prod:%lu)",
                             urgent ? "Urgent" : "Periodic",
                             current_mode, (unsigned long)sys_data.produced_count);
                    last_save_time = now;
                }
            }
        }
    }
}

// ============ Public Functions ============

esp_err_t nvs_storage_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    // Create save queue
    nvs_save_queue = xQueueCreate(1, sizeof(uint8_t));
    
    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

void nvs_storage_start_task(void) {
    // Core 0'da calistir: flash yazarken Core 1 (display degil) suspend edilir
    xTaskCreatePinnedToCore(nvs_save_task, "nvs_save", 2048, NULL, 1, NULL, 0);
    ESP_LOGI(TAG, "NVS save task started (Core 0, Priority 1)");
}

void nvs_storage_save_target(uint32_t target) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_u32(my_handle, "target_cnt", target);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Target saved: %lu", (unsigned long)target);
    }
}

uint32_t nvs_storage_load_target(void) {
    nvs_handle_t my_handle;
    uint32_t target = 0;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        nvs_get_u32(my_handle, "target_cnt", &target);
        nvs_close(my_handle);
    }
    ESP_LOGI(TAG, "Target loaded: %lu", (unsigned long)target);
    return target;
}

void nvs_storage_save_cycle_target(uint32_t seconds) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_u32(my_handle, "cycle_target", seconds);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Cycle target saved: %lu sec", (unsigned long)seconds);
    }
}

uint32_t nvs_storage_load_cycle_target(void) {
    nvs_handle_t my_handle;
    uint32_t seconds = DEFAULT_CYCLE_TARGET_SEC;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        nvs_get_u32(my_handle, "cycle_target", &seconds);
        nvs_close(my_handle);
    }
    if (seconds < 1) seconds = DEFAULT_CYCLE_TARGET_SEC;
    ESP_LOGI(TAG, "Cycle target loaded: %lu sec", (unsigned long)seconds);
    return seconds;
}

void nvs_storage_save_brightness(uint8_t level) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_u8(my_handle, "led_bright", level);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Brightness level saved: %d", level);
    }
}

uint8_t nvs_storage_load_brightness(void) {
    nvs_handle_t my_handle;
    uint8_t level = 3; // Default
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        nvs_get_u8(my_handle, "led_bright", &level);
        nvs_close(my_handle);
    }
    if (level < 1 || level > 5) level = 3;
    ESP_LOGI(TAG, "Brightness level loaded: %d", level);
    return level;
}

void nvs_storage_save_state(void) {
    if (nvs_save_queue != NULL) {
        uint8_t msg = NVS_SAVE_THROTTLED;
        xQueueOverwrite(nvs_save_queue, &msg);
    }
}

system_state_backup_t nvs_storage_load_state(void) {
    system_state_backup_t state = {0};
    state.valid = false;
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        // Once valid flag kontrol et (partial write korumasi)
        uint8_t v = 0;
        nvs_get_u8(my_handle, "valid", &v);
        if (v != 1) {
            ESP_LOGW(TAG, "NVS: valid flag eksik, fresh start");
            nvs_close(my_handle);
            return state;
        }

        err = nvs_get_u8(my_handle, "work_mode", &state.work_mode);
        if (err == ESP_OK) {
            state.valid = true;
            nvs_get_u8(my_handle, "shift_state", &state.shift_state);
            nvs_get_u32(my_handle, "work_time", &state.work_t);
            nvs_get_u32(my_handle, "idle_time", &state.idle_t);
            nvs_get_u32(my_handle, "planned_time", &state.planned_t);
            nvs_get_u32(my_handle, "produced_cnt", &state.prod_cnt);
            nvs_get_u32(my_handle, "target_cnt", &state.target_cnt);
            nvs_get_u32(my_handle, "cycle_target", &state.cycle_target);
            nvs_get_u32(my_handle, "durus_time", &state.durus_t);
            nvs_get_u32(my_handle, "last_update", &state.last_upd);
            
            ESP_LOGI(TAG, "State loaded (Mode:%d, Work:%lu, Prod:%lu)", 
                     state.work_mode, (unsigned long)state.work_t, (unsigned long)state.prod_cnt);
        } else {
            ESP_LOGW(TAG, "NVS: work_mode not found, fresh start");
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGW(TAG, "NVS: Open failed (%s), fresh start", esp_err_to_name(err));
    }
    return state;
}

void nvs_storage_save_state_immediate(void) {
    // Core 1'den cagrildiginda direkt flash yazmak Core 0'i (display) suspend eder.
    // Bunun yerine Core 0'daki nvs_save_task'a urgent sinyal gonder.
    if (nvs_save_queue != NULL) {
        uint8_t msg = NVS_SAVE_URGENT;
        xQueueOverwrite(nvs_save_queue, &msg);
        ESP_LOGD(TAG, "Urgent save requested");
    }
}
