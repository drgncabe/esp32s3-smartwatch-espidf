#pragma once

#ifndef NICE_BUTTON_H
#define NICE_BUTTON_H

#include <lvgl.h>
#include <stdint.h>

typedef enum {
    BTN_STYLE_PRIMARY,
    BTN_STYLE_SECONDARY,
    BTN_STYLE_SUCCESS,
    BTN_STYLE_DANGER,
    BTN_STYLE_GHOST
} btn_style_t;

typedef struct {
    const char *text;
    const char *icon;
    btn_style_t style;
    uint16_t width;
    uint16_t height;
    lv_event_cb_t callback;
    void *user_data;
} btn_config_t;

lv_obj_t *qc_create_button(lv_obj_t *parent, btn_config_t *config);
lv_obj_t *qc_button_get_label(lv_obj_t *btn);
lv_obj_t *qc_button_get_icon(lv_obj_t *btn);

#endif
