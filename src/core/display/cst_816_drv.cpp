#include "configuration/pin_config.h"
#include "configuration/app_config.h"
#include "core/display/cst_816_drv.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "cst_816_drv";

#define CST816_ADDR 0x15

static volatile bool touch_irq_flag = false;
static bool touch_pressed = false;
static bool was_pressed = false;
static bool prevent_offsets = false;
static bool keyboard_offsets = false;
static int16_t touch_x = 0;
static int16_t touch_y = 0;
static uint32_t last_read_time = 0;
#define TOUCH_POLL_INTERVAL_MS 15

#define TOUCH_FILTER_SIZE 3
static int16_t touch_x_history[TOUCH_FILTER_SIZE] = {0};
static int16_t touch_y_history[TOUCH_FILTER_SIZE] = {0};
static uint8_t history_index = 0;
static bool filter_initialized = false;

static screen_touch_callback_t screen_touch_callback = NULL;

void keyboard_cst816_touch_offsets(bool enable){
    if(enable == keyboard_offsets) return;
    ESP_LOGI(TAG, "Keyboard touch offsets: %d", enable);
    keyboard_offsets = enable;
}

void prevent_cst816_touch_offsets(bool prevent){
    ESP_LOGI(TAG, "Preventing touch offsets: %d", prevent);
    prevent_offsets = prevent;
}

void set_cst816_touch_callback(screen_touch_callback_t callback)
{
    screen_touch_callback = callback;
}

static void IRAM_ATTR touch_isr_handler(void *arg)
{
    touch_irq_flag = true;
}

esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = I2C_FREQ_HZ,
        },
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK)
        return err;

    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t cst816_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, CST816_ADDR,
                                        &reg, 1, data, len,
                                        pdMS_TO_TICKS(1000));
}

esp_err_t cst816_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, CST816_ADDR,
                                      buf, 2, pdMS_TO_TICKS(1000));
}

esp_err_t cst816_init(void)
{
    gpio_reset_pin(TP_RST);
    gpio_set_direction(TP_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TP_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(TP_INT, touch_isr_handler, NULL);

    uint8_t chip_id;
    esp_err_t ret = cst816_read_reg(0xA7, &chip_id, 1);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "CST816T Chip ID: 0x%02X", chip_id);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read CST816T chip ID!");
        return ret;
    }

    for (int i = 0; i < TOUCH_FILTER_SIZE; i++) {
        touch_x_history[i] = 0;
        touch_y_history[i] = 0;
    }
    history_index = 0;
    filter_initialized = false;
    was_pressed = false;

    ESP_LOGI(TAG, "CST816T initialized");
    return ESP_OK;
}

static void apply_touch_filter(int16_t raw_x, int16_t raw_y, int16_t *filtered_x, int16_t *filtered_y)
{
    touch_x_history[history_index] = raw_x;
    touch_y_history[history_index] = raw_y;
    history_index = (history_index + 1) % TOUCH_FILTER_SIZE;
    
    if (!filter_initialized) {
        if (history_index == 0) {
            filter_initialized = true;
        }
        *filtered_x = raw_x;
        *filtered_y = raw_y;
        return;
    }
    
    int32_t sum_x = 0, sum_y = 0;
    for (int i = 0; i < TOUCH_FILTER_SIZE; i++) {
        sum_x += touch_x_history[i];
        sum_y += touch_y_history[i];
    }
    
    *filtered_x = sum_x / TOUCH_FILTER_SIZE;
    *filtered_y = sum_y / TOUCH_FILTER_SIZE;
}

static void cst816_read_touch(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    bool should_read = touch_irq_flag || 
                       (touch_pressed && (now - last_read_time >= TOUCH_POLL_INTERVAL_MS)) ||
                       (now - last_read_time >= 30); // Always read at least every 30ms

    if (!should_read)
    {
        return;
    }

    uint8_t data[5];
    esp_err_t ret = cst816_read_reg(0x02, data, 5);
    
    last_read_time = now;
    touch_irq_flag = false;

    if (ret != ESP_OK)
    {
        return;
    }

    uint8_t finger_num = data[0];
    uint16_t raw_x = ((data[1] & 0x0F) << 8) | data[2];
    uint16_t raw_y = ((data[3] & 0x0F) << 8) | data[4];

    // Clamp to valid display range
    if (raw_x >= LCD_WIDTH) raw_x = LCD_WIDTH - 1;
    if (raw_y >= LCD_HEIGHT) raw_y = LCD_HEIGHT - 1;

    if (finger_num > 0)
    {
        int16_t filtered_x, filtered_y;
        apply_touch_filter(raw_x, raw_y, &filtered_x, &filtered_y);
        
        touch_x = filtered_x;
        touch_y = filtered_y;
        touch_pressed = true;
    }
    else
    {
        touch_pressed = false;
        filter_initialized = false;
        history_index = 0;
    }
}

static void cancel_scroll_animations(lv_obj_t *obj)
{
    if (obj == NULL) return;
    
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)) {
        lv_anim_del(obj, NULL);
    }
    
    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        cancel_scroll_animations(child);
    }
}

void lvgl_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    cst816_read_touch();

    if (touch_pressed)
    {
        // Cancel scroll inertia on new press
        if (!was_pressed) {
            lv_obj_t *act_scr = lv_scr_act();
            if (act_scr) {
                cancel_scroll_animations(act_scr);
            }
        }

        float calib_modifier_y = 0.0f;

        if(keyboard_offsets){
            calib_modifier_y = 10.0f;
        }
        
        float calibrated_x = (touch_x * (prevent_offsets ? 1.0f : TOUCH_SCALE_X)) + (prevent_offsets ? 0.0f : TOUCH_OFFSET_X);
        float calibrated_y = (touch_y * (prevent_offsets ? 1.0f : TOUCH_SCALE_Y)) + (prevent_offsets ? 0.0f : TOUCH_OFFSET_Y) + calib_modifier_y;

        if (calibrated_x < 0) calibrated_x = 0;
        if (calibrated_x >= LCD_WIDTH) calibrated_x = LCD_WIDTH - 1;
        if (calibrated_y < 0) calibrated_y = 0;
        if (calibrated_y >= LCD_HEIGHT) calibrated_y = LCD_HEIGHT - 1;

        data->state = LV_INDEV_STATE_PR;
        data->point.x = (int16_t)calibrated_x;
        data->point.y = (int16_t)calibrated_y;
        
        if (screen_touch_callback != NULL)
        {
            screen_touch_callback();
        }

        was_pressed = true;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
        was_pressed = false;
    }
}
