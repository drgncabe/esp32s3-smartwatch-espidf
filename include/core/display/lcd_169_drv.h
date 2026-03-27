#pragma once

#ifndef LCD_169_DRV_H
#define LCD_169_DRV_H

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

typedef void (*ui_init_callback_t)(void);

esp_err_t lcd_init(void);
void lvgl_init(void);
void log_lvgl_memory(void);
void init_app_safe(ui_init_callback_t callback);
esp_err_t lvgl_free_buffers(void);
void lvgl_reinit_timers(void);

extern lv_disp_t *disp;
extern lv_color_t *buf1;
extern lv_color_t *buf2;
extern lv_disp_draw_buf_t disp_buf;
extern bool buffers_reduced;

#endif
