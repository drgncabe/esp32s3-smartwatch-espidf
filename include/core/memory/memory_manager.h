#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <stdbool.h>

#include "configuration/pin_config.h"
#include "esp_err.h"

#define BUFFER_FULL_SIZE (LCD_WIDTH * LCD_HEIGHT / 2)
#define BUFFER_PARTIAL_SIZE (LCD_WIDTH * LCD_HEIGHT / 3)
#define BUFFER_REDUCED_SIZE (LCD_WIDTH * LCD_HEIGHT / 10)

esp_err_t lvgl_init_reserved_buffers(void);
esp_err_t lvgl_reduce_buffers();
esp_err_t lvgl_restore_buffers(bool using_partial_size = true);
bool lvgl_buffers_are_reduced();

#endif
