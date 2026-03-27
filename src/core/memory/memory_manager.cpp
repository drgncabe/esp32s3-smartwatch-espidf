#include "configuration/pin_config.h"
#include "core/display/lcd_169_drv.h"
#include "core/memory/memory_manager.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "lvgl_mem_mgr";

static void schedule_buffer_restoration_retry();

static int retry_count = 0;
static const int MAX_RETRIES = 3;
static bool retry_in_progress = false;
static bool retry_use_partial = true;
static bool lvgl_buffers_freed = false;


extern lv_disp_t *disp;
extern lv_color_t *buf1;
extern lv_color_t *buf2;
extern lv_disp_draw_buf_t disp_buf;
extern bool buffers_reduced;


static inline void log_dma_heap(const char *label)
{
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    size_t largest  = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "%s: Free DMA=%u KB, Largest DMA block=%u KB",
             label, (unsigned)(free_dma / 1024), (unsigned)(largest / 1024));
}

esp_err_t lvgl_reduce_buffers()
{
    if (buffers_reduced) {
        ESP_LOGW(TAG, "Buffers already reduced");
        return ESP_OK;
    }

    if (!buf1 || !buf2 || !disp) {
        ESP_LOGE(TAG, "LVGL not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Reducing LVGL buffers from 1/2 to 1/10 screen...");

    // Flush pending renders (already on LVGL thread via lv_async_call)
    lv_refr_now(disp);
    vTaskDelay(pdMS_TO_TICKS(50));

    log_dma_heap("Before buffer reduction");

    lv_color_t *old_buf1 = buf1;
    lv_color_t *old_buf2 = buf2;

    lv_color_t *new_buf1 = (lv_color_t *)heap_caps_malloc(
        BUFFER_REDUCED_SIZE * sizeof(lv_color_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );

    lv_color_t *new_buf2 = (lv_color_t *)heap_caps_malloc(
        BUFFER_REDUCED_SIZE * sizeof(lv_color_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );

    if (!new_buf1 || !new_buf2) {
        ESP_LOGE(TAG, "Failed to allocate reduced buffers!");
        if (new_buf1) heap_caps_free(new_buf1);
        if (new_buf2) heap_caps_free(new_buf2);
        return ESP_FAIL;
    }

    buf1 = new_buf1;
    buf2 = new_buf2;
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, BUFFER_REDUCED_SIZE);
    disp->driver->draw_buf = &disp_buf;

    lv_refr_now(disp);
    vTaskDelay(pdMS_TO_TICKS(50));

    heap_caps_free(old_buf1);
    heap_caps_free(old_buf2);

    buffers_reduced = true;

    log_dma_heap("After buffer reduction");

    size_t freed_kb = (BUFFER_FULL_SIZE * 2 - BUFFER_REDUCED_SIZE * 2) * sizeof(lv_color_t) / 1024;
    ESP_LOGI(TAG, "Buffers reduced: ~%zu KB freed for Bluetooth", freed_kb);

    return ESP_OK;
}

static void buffer_restoration_retry_task(void *param)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)param;
    
    ESP_LOGI(TAG, "Buffer restoration retry task started, waiting %lu ms", (unsigned long)delay_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    
    if (retry_in_progress) {
        ESP_LOGW(TAG, "Another retry already in progress, aborting this one");
        vTaskDelete(NULL);
        return;
    }
    
    retry_in_progress = true;
    
    ESP_LOGI(TAG, "Retry timer fired - attempting buffer restoration (attempt %d/%d)", 
             retry_count, MAX_RETRIES);
    
    lv_async_call([](void *user_data) {
        esp_err_t result = lvgl_restore_buffers(retry_use_partial);
        
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Buffer restoration succeeded");
            retry_count = 0;
            retry_in_progress = false;
        } else {
            if (retry_count < MAX_RETRIES) {
                ESP_LOGW(TAG, "Restoration retry %d/%d failed, will try again later", retry_count, MAX_RETRIES);
                retry_in_progress = false; // Allow next retry to run
                schedule_buffer_restoration_retry();
            } else {
                ESP_LOGW(TAG, "All %d restoration retries exhausted, staying reduced", MAX_RETRIES);
                retry_count = 0;
                retry_in_progress = false;
            }
        }
    }, NULL);
    
    vTaskDelete(NULL);
}

static void schedule_buffer_restoration_retry()
{
    if (retry_in_progress) {
        ESP_LOGW(TAG, "Retry already in progress, not scheduling another");
        return;
    }
    
    retry_count++;
    
    if (retry_count > MAX_RETRIES) {
        ESP_LOGW(TAG, "Cannot schedule retry - already at maximum (%d)", MAX_RETRIES);
        retry_in_progress = false;
        retry_use_partial = true;
        return;
    }

    if(lvgl_buffers_freed) {
        ESP_LOGI(TAG, "LVGL buffers freed. Re initializing timers...");
        lvgl_reinit_timers();
        lvgl_buffers_freed = false;
        ESP_LOGI(TAG, "Timers reinitialized");
    }
    
    uint32_t delay_ms = retry_count * 10000; // 10s, 20s, 30s
    
    ESP_LOGI(TAG, "Scheduling buffer restoration retry %d/%d in %lu seconds", 
             retry_count, MAX_RETRIES, (unsigned long)(delay_ms / 1000));
    
    BaseType_t ret = xTaskCreate(buffer_restoration_retry_task, "buf_retry", 4096, 
                (void *)(uintptr_t)delay_ms, 3, NULL);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create retry task!");
        retry_count--;
        retry_in_progress = false;
        retry_use_partial = true;
    }
}

esp_err_t lvgl_restore_buffers(bool using_partial_size)
{
    if (!buffers_reduced) {
        ESP_LOGW(TAG, "Buffers already at full size");
        return ESP_OK;
    }

    if (!disp) {
        ESP_LOGE(TAG, "LVGL not initialized");
        return ESP_FAIL;
    }

    if(using_partial_size) {
        ESP_LOGI(TAG, "Restoring LVGL buffers to partial size...");
    } else {
        ESP_LOGI(TAG, "Restoring LVGL buffers to full size...");
    }

    // Flush pending renders (already on LVGL thread via lv_async_call)
    lv_refr_now(disp);

    // Give memory time to coalesce after BT deinit
    vTaskDelay(pdMS_TO_TICKS(500));

    log_dma_heap("Before buffer restoration");

    lv_color_t *old_buf1 = buf1;
    lv_color_t *old_buf2 = buf2;

    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    size_t needed_per_buffer = using_partial_size ? BUFFER_PARTIAL_SIZE * sizeof(lv_color_t) : BUFFER_FULL_SIZE * sizeof(lv_color_t);
    size_t total_needed = needed_per_buffer * 2;

    ESP_LOGI(TAG, "Need %zu KB total (%zu KB per buffer), largest block: %zu KB",
             total_needed / 1024, needed_per_buffer / 1024, largest / 1024);

    lv_color_t *new_buf1 = NULL;
    lv_color_t *new_buf2 = NULL;

    // Try single contiguous allocation first, fall back to individual
    if (largest >= (total_needed + 1024)) {
        void *combined = heap_caps_malloc(total_needed, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (combined) {
            new_buf1 = (lv_color_t *)combined;
            new_buf2 = (lv_color_t *)((uint8_t*)combined + needed_per_buffer);
            ESP_LOGI(TAG, "Allocated both buffers from single block");
        }
    }

    if (!new_buf1) {
        new_buf1 = (lv_color_t *)heap_caps_malloc(
            needed_per_buffer,
            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
        );

        if (new_buf1) {
            new_buf2 = (lv_color_t *)heap_caps_malloc(
                needed_per_buffer,
                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
            );
        }
    }

    if (!new_buf1) {
        ESP_LOGE(TAG, "Failed to allocate first buffer");
        log_dma_heap("Failed - staying reduced");
        schedule_buffer_restoration_retry();
        
        return ESP_FAIL;
    }

    if (!new_buf2) {
        ESP_LOGW(TAG, "Failed second buffer, keeping reduced");
        heap_caps_free(new_buf1);
        buffers_reduced = true;
        log_dma_heap("Keeping reduced buffers");
        schedule_buffer_restoration_retry();
        
        return ESP_ERR_NO_MEM;
    }

    buf1 = new_buf1;
    buf2 = new_buf2;
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, using_partial_size ? BUFFER_PARTIAL_SIZE : BUFFER_FULL_SIZE);
    disp->driver->draw_buf = &disp_buf;

    lv_refr_now(disp);
    vTaskDelay(pdMS_TO_TICKS(50));

    heap_caps_free(old_buf1);
    heap_caps_free(old_buf2);

    buffers_reduced = false;

    log_dma_heap("After buffer restoration");

    ESP_LOGI(TAG, "Restored full double-buffer mode");
    retry_count = 0;

    if(using_partial_size) {
        retry_use_partial = false;
        buffers_reduced = true;
        ESP_LOGI(TAG, "Partial succeeded. Scheduling buffer restoration retry for full size...");
        schedule_buffer_restoration_retry();
    }

    if(lvgl_buffers_freed) {
        ESP_LOGI(TAG, "LVGL buffers freed. Re initializing timers...");
        lvgl_reinit_timers();
        lvgl_buffers_freed = false;
        ESP_LOGI(TAG, "Timers reinitialized");
    }

    return ESP_OK;
}

bool lvgl_buffers_are_reduced()
{
    return buffers_reduced;
}
