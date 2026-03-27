#include "configuration/pin_config.h"
#include "configuration/app_config.h"
#include "core/display/cst_816_drv.h"
#include "core/display/lcd_169_drv.h"
#include "core/memory/memory_manager.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "util/util.h"

#define LVGL_TICK_MS 2
#define LVGL_TASK_PRIO 4
#define LVGL_TASK_STACK 8192
#define LVGL_HANDLER_INTERVAL 25000 // 25ms / 40fps


lv_disp_t *disp = NULL;
SemaphoreHandle_t lvgl_mux = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_timer_handle_t lvgl_tick_timer = NULL;
static esp_timer_handle_t lvgl_handler_timer = NULL;
static const char *TAG = "lcd_169_drv";

lv_color_t *buf1 = NULL;
lv_color_t *buf2 = NULL;
lv_disp_draw_buf_t disp_buf;
bool buffers_reduced = false;

esp_err_t lcd_init(void)
{
    gpio_reset_pin(LCD_BL);
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 1);

    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLK,
        .trans_queue_depth = 20,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .quad_mode = 0,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "LCD initialized");
    return ESP_OK;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1; 
    int offsety2 = area->y2;

    #if BOARD_TYPE == BOARD_ESP32_S3_169_V1 || BOARD_TYPE == BOARD_ESP32_S3_169_V2
        offsety1 += 20;
        offsety2 += 20;
        // Byte-swap RGB565 pixels for this display variant
        size_t num_pixels = (size_t)(offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);
        uint16_t *p = (uint16_t *)color_map;
        for (size_t i = 0; i < num_pixels; i++) {
            p[i] = (uint16_t)((p[i] >> 8) | (p[i] << 8));
        }
    #endif

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void lvgl_tick_callback(void *arg)
{
    lv_tick_inc(LVGL_TICK_MS);
}

static void lvgl_timer_callback(void *arg)
{
    if (xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        lv_timer_handler();
        xSemaphoreGive(lvgl_mux);

        // Yield to feed watchdog during heavy rendering
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void lvgl_init(void)
{
    lv_init();

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);

    // Half-screen double buffers for smooth rendering
    buf1 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT / 2 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT / 2 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf1 && buf2);
    buffers_reduced = false;

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_WIDTH * LCD_HEIGHT / 2);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp = lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touchpad_read;
    
    indev_drv.long_press_time = 400;
    indev_drv.long_press_repeat_time = 400; 
    indev_drv.scroll_throw = 25;
    
    lv_indev_drv_register(&indev_drv);

    esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_callback,
        .name = "lvgl_tick"};
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_MS * 1000));

    esp_timer_create_args_t handler_timer_args = {
        .callback = lvgl_timer_callback,
        .name = "lvgl_handler"};
    ESP_ERROR_CHECK(esp_timer_create(&handler_timer_args, &lvgl_handler_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_handler_timer, LVGL_HANDLER_INTERVAL)); 

    ESP_LOGI(TAG, "LVGL initialized");
}

void lvgl_reinit_timers(void)
{
    esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_callback,
        .name = "lvgl_tick"};
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_MS * 1000));

    esp_timer_create_args_t handler_timer_args = {
        .callback = lvgl_timer_callback,
        .name = "lvgl_handler"};
    ESP_ERROR_CHECK(esp_timer_create(&handler_timer_args, &lvgl_handler_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_handler_timer, LVGL_HANDLER_INTERVAL));
    ESP_LOGI(TAG, "LVGL timers reinitialized");
}

void log_lvgl_memory(void)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "LVGL Memory: %d%% used (%lu / %lu bytes), %u frag",
             mon.used_pct,
             (unsigned long)(mon.total_size - mon.free_size),
             (unsigned long)mon.total_size,
             mon.frag_pct);
}

void init_app_safe(ui_init_callback_t callback)
{
    if (xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        if (callback != nullptr)
        {
            callback();
        }
        xSemaphoreGive(lvgl_mux);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take LVGL mutex for UI creation");
    }
}

// Stops LVGL rendering and frees buffers. Display will not work after this.
esp_err_t lvgl_free_buffers(void)
{
    if (!buf1 && !buf2)
    {
        ESP_LOGW(TAG, "Buffers already freed");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping LVGL rendering and freeing buffers...");

    if (lvgl_tick_timer)
    {
        esp_timer_stop(lvgl_tick_timer);
        esp_timer_delete(lvgl_tick_timer);
        lvgl_tick_timer = NULL;
    }
    if (lvgl_handler_timer)
    {
        esp_timer_stop(lvgl_handler_timer);
        esp_timer_delete(lvgl_handler_timer);
        lvgl_handler_timer = NULL;
    }

    // Flush any pending renders
    if (lvgl_mux)
    {
        if (xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            if (disp)
            {
                lv_refr_now(disp);
            }
            xSemaphoreGive(lvgl_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (buf1)
    {
        heap_caps_free(buf1);
        buf1 = NULL;
    }
    if (buf2)
    {
        heap_caps_free(buf2);
        buf2 = NULL;
    }

    buffers_reduced = false;

    ESP_LOGI(TAG, "Display buffers freed. Display will not work, but system continues running.");
    return ESP_OK;
}
