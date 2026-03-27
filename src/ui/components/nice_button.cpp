#include <lvgl.h>
#include "ui/components/nice_button.h"


lv_obj_t *qc_create_button(lv_obj_t *parent, btn_config_t *config)
{

    lv_obj_t *btn = lv_btn_create(parent);

    uint16_t h = config->height > 0 ? config->height : 44;
    if (config->width > 0)
    {
        lv_obj_set_size(btn, config->width, h);
    }
    else
    {
        lv_obj_set_height(btn, h);
        lv_obj_set_width(btn, LV_SIZE_CONTENT);
    }

    uint32_t bg_color, bg_color_pressed;
    lv_opa_t bg_opa = LV_OPA_COVER;
    uint16_t border_width = 0;
    uint32_t border_color = 0xFFFFFF;

    switch (config->style)
    {
    case BTN_STYLE_PRIMARY:
        bg_color = 0x2196F3;
        bg_color_pressed = 0x1976D2;
        break;
    case BTN_STYLE_SECONDARY:
        bg_color = 0x424242;
        bg_color_pressed = 0x303030;
        break;
    case BTN_STYLE_SUCCESS:
        bg_color = 0x4CAF50;
        bg_color_pressed = 0x388E3C;
        break;
    case BTN_STYLE_DANGER:
        bg_color = 0xF44336;
        bg_color_pressed = 0xD32F2F;
        break;
    case BTN_STYLE_GHOST:
        bg_color = 0x000000;
        bg_color_pressed = 0x303030;
        bg_opa = LV_OPA_TRANSP;
        border_width = 2;
        border_color = 0x666666;
        break;
    default:
        bg_color = 0x424242;
        bg_color_pressed = 0x303030;
    }

    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, bg_opa, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, border_width, 0);
    if (border_width > 0)
    {
        lv_obj_set_style_border_color(btn, lv_color_hex(border_color), 0);
    }

    lv_obj_set_style_pad_left(btn, 16, 0);
    lv_obj_set_style_pad_right(btn, 16, 0);
    lv_obj_set_style_pad_top(btn, 8, 0);
    lv_obj_set_style_pad_bottom(btn, 8, 0);

    lv_obj_t *content = lv_obj_create(btn);
    lv_obj_set_size(content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(content, 8, 0);
    lv_obj_center(content);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);

    if (config->icon != NULL)
    {
        lv_obj_t *icon = lv_label_create(content);
        lv_label_set_text(icon, config->icon);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }

    if (config->text != NULL)
    {
        lv_obj_t *label = lv_label_create(content);
        lv_label_set_text(label, config->text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    }

    if (config->callback != NULL)
    {
        lv_obj_add_event_cb(btn, config->callback, LV_EVENT_CLICKED, config->user_data);
    }

    return btn;
}

lv_obj_t *qc_button_get_label(lv_obj_t *btn)
{
    lv_obj_t *content = lv_obj_get_child(btn, 0);
    if (!content)
        return NULL;

    uint32_t i = 0;
    lv_obj_t *child;
    while ((child = lv_obj_get_child(content, i++)))
    {

        if (!lv_obj_check_type(child, &lv_label_class))
            continue;

        const lv_font_t *font =
            lv_obj_get_style_text_font(child, LV_STATE_DEFAULT);

        // Text label uses smaller font
        if (font == &lv_font_montserrat_14)
        {
            return child;
        }
    }
    return NULL;
}

lv_obj_t *qc_button_get_icon(lv_obj_t *btn)
{
    lv_obj_t *content = lv_obj_get_child(btn, 0);
    if (!content)
        return NULL;

    uint32_t i = 0;
    lv_obj_t *child;
    while ((child = lv_obj_get_child(content, i++)))
    {
        if (!lv_obj_check_type(child, &lv_label_class))
            continue;

        const lv_font_t *font =
            lv_obj_get_style_text_font(child, LV_STATE_DEFAULT);

        // Icon uses larger font
        if (font == &lv_font_montserrat_16)
        {
            return child;
        }
    }
    return NULL;
}
