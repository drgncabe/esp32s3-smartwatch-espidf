#include "ui/components/ui_helpers.h"

// Global constant for extended click area (can be adjusted)
#define UI_EXT_CLICK_AREA 20

lv_obj_t *ui_create_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 4, 0);  
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);  
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *ui_create_group_card(lv_obj_t *parent, const char *title, const char *subtitle, 
                                 const char *icon, lv_event_cb_t callback)
{
    lv_obj_t *card = ui_create_card(parent);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(card, UI_EXT_CLICK_AREA);
    lv_obj_add_event_cb(card, callback, LV_EVENT_CLICKED, NULL);

    // Icon
    lv_obj_t *icon_label = lv_label_create(card);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(0x2196F3), 0);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 0, 0);

    // Title
    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_align_to(title_label, icon_label, LV_ALIGN_OUT_RIGHT_TOP, 12, -2);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(title_label, LV_SIZE_CONTENT);

    // Subtitle
    lv_obj_t *subtitle_label = lv_label_create(card);
    lv_label_set_text(subtitle_label, subtitle);
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(subtitle_label, title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(subtitle_label, LV_PCT(70));
    lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_CLIP);

    // Chevron
    lv_obj_t *chevron = lv_label_create(card);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, lv_color_hex(0x888888), 0);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);

    return card;
}

lv_obj_t *ui_create_transparent_container(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    return container;
}

lv_obj_t *ui_create_scrollable_flex_container(lv_obj_t *parent, lv_flex_flow_t flow)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_flex_flow(container, flow);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    return container;
}

lv_obj_t *ui_create_title_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    return label;
}

lv_obj_t *ui_create_subtitle_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    return label;
}

lv_obj_t *ui_create_body_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    return label;
}
