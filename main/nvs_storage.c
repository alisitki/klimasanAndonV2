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

// ============ NVS Save Task ============

static void nvs_save_task(void *pvParameters) {
    uint32_t last_save_time = 0;
    uint8_t dummy;
    
    ESP_LOGI(TAG, "NVS save task started");
    
    while (1) {
        // Wait for save request with timeout
        if (xQueueReceive(nvs_save_queue, &dummy, pdMS_TO_TICKS(1000)) == pdTRUE) {
            uint32_t now = xTaskGetTickCount();
            
            // Throttle: en az 1 saniye arayla kaydet
            if ((now - last_save_time) >= 1000) {
                nvs_handle_t my_handle;
                esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
                if (err == ESP_OK) {
                    nvs_set_u8(my_handle, "work_mode", (uint8_t)current_mode);
                    nvs_set_u8(my_handle, "shift_state", (uint8_t)shift_state);
                    nvs_set_u32(my_handle, "work_time", sys_data.work_time);
                    nvs_set_u32(my_handle, "idle_time", sys_data.idle_time);
                    nvs_set_u32(my_handle, "planned_time", sys_data.planned_time);
                    nvs_set_u32(my_handle, "produced_cnt", sys_data.produced_count);
                    nvs_set_u32(my_handle, "target_cnt", sys_data.target_count);
                    nvs_set_u32(my_handle, "cycle_target", led_strip_get_cycle_target());
                    nvs_set_u32(my_handle, "durus_time", sys_data.durus_time); // DURUS EKLE
                    nvs_set_u32(my_handle, "last_update", rtc_get_wall_time_seconds());
                    nvs_commit(my_handle);
                    nvs_close(my_handle);
                    
                    ESP_LOGI(TAG, "State saved (Mode:%d, Work:%lu, Prod:%lu, Durus:%lu)", 
                             current_mode, (unsigned long)sys_data.work_time, 
                             (unsigned long)sys_data.produced_count, (unsigned long)sys_data.durus_time);
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
    xTaskCreatePinnedToCore(nvs_save_task, "nvs_save", 2048, NULL, 1, NULL, 1);
    ESP_LOGI(TAG, "NVS save task started (Core 1)");
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
        uint8_t dummy = 1;
        xQueueOverwrite(nvs_save_queue, &dummy);
    }
}

system_state_backup_t nvs_storage_load_state(void) {
    system_state_backup_t state = {0};
    state.valid = false;
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        // En kritik veri: work_mode. Eğer bu yoksa yeni cihaz kabul et.
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
            nvs_get_u32(my_handle, "durus_time", &state.durus_t); // DURUS OKU
            nvs_get_u32(my_handle, "last_update", &state.last_upd);
            
            ESP_LOGI(TAG, "State loaded Successfully (Mode:%d, Work:%lu, Prod:%lu, Durus:%lu)", 
                     state.work_mode, (unsigned long)state.work_t, (unsigned long)state.prod_cnt, (unsigned long)state.durus_t);
        } else {
            ESP_LOGW(TAG, "NVS: work_mode not found, fresh start assumed");
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGW(TAG, "NVS: Open failed (%s), fresh start assumed", esp_err_to_name(err));
    }
    return state;
}

void nvs_storage_save_state_immediate(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_u8(my_handle, "work_mode", (uint8_t)current_mode);
        nvs_set_u8(my_handle, "shift_state", (uint8_t)shift_state);
        nvs_set_u32(my_handle, "work_time", sys_data.work_time);
        nvs_set_u32(my_handle, "idle_time", sys_data.idle_time);
        nvs_set_u32(my_handle, "planned_time", sys_data.planned_time);
        nvs_set_u32(my_handle, "produced_cnt", sys_data.produced_count);
        nvs_set_u32(my_handle, "target_cnt", sys_data.target_count);
        nvs_set_u32(my_handle, "cycle_target", led_strip_get_cycle_target());
        nvs_set_u32(my_handle, "durus_time", sys_data.durus_time); // DURUS EKLE
        nvs_set_u32(my_handle, "last_update", rtc_get_wall_time_seconds());
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "State saved immediately (Durus:%lu)", (unsigned long)sys_data.durus_time);
    }
}
