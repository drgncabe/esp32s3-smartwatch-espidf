#pragma once

#ifndef CST_816_DRV_H
#define CST_816_DRV_H

#include "esp_err.h"
#include "lvgl.h"

typedef void (*screen_touch_callback_t)(void);

esp_err_t i2c_master_init(void);
esp_err_t cst816_init(void);
void lvgl_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
void set_cst816_touch_callback(screen_touch_callback_t callback);
void prevent_cst816_touch_offsets(bool prevent);
void keyboard_cst816_touch_offsets(bool enable);

#endif
