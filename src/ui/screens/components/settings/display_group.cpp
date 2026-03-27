#include <stdio.h>
#include <stdlib.h>

#include "api/prefs.h"
#include "context/app_context.h"
#include "esp_heap_caps.h"
#include "ui/components/ui_helpers.h"
#include "ui/screens/components/settings/settings_common.h"
#include "ui/screens/components/settings/settings_groups.h"
#include "ui/screens/home_page.h"

static void brightness_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);

    const int BRIGHTNESS_MIN = 10;
    const int BRIGHTNESS_MAX = 255;

    int32_t slider_val = lv_slider_get_value(slider);
    uint8_t level = BRIGHTNESS_MIN + (slider_val * (BRIGHTNESS_MAX - BRIGHTNESS_MIN) / 100);

    g_settings.set_brightness(level, false);
    settings_need_saved = true;

    if (value_label)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ld%%", (long)slider_val);
        lv_label_set_text(value_label, buf);
    }
}

void open_display_group(lv_event_t *e)
{
    play_haptic_click();
    lv_obj_t *content = create_group_detail_page("Display");

    lv_obj_t *brightness_card = ui_create_card(content);

    lv_obj_t *brightness_title = lv_label_create(brightness_card);
    lv_label_set_text(brightness_title, "Brightness");
    lv_obj_set_style_text_color(brightness_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(brightness_title, &lv_font_montserrat_14, 0);
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *brightness_sub = lv_label_create(brightness_card);
    lv_label_set_text(brightness_sub, "Adjust screen brightness");
    lv_obj_set_style_text_color(brightness_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(brightness_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(brightness_sub, brightness_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *brightness_slider = lv_slider_create(brightness_card);
    lv_slider_set_range(brightness_slider, 0, 100);
    lv_obj_set_width(brightness_slider, LV_PCT(65));
    lv_obj_set_height(brightness_slider, 8);
    lv_obj_set_style_radius(brightness_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(brightness_slider, 3, LV_PART_KNOB);
    lv_obj_set_style_radius(brightness_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_align_to(brightness_slider, brightness_sub, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *brightness_value = lv_label_create(brightness_card);
    lv_obj_set_style_pad_left(brightness_value, 10, 0);

    const int BRIGHTNESS_MIN = 10;
    const int BRIGHTNESS_MAX = 255;
    uint8_t cur = g_settings.get_brightness();
    if (cur < BRIGHTNESS_MIN)
        cur = BRIGHTNESS_MIN;
    if (cur > BRIGHTNESS_MAX)
        cur = BRIGHTNESS_MAX;
    int initial_slider = (cur - BRIGHTNESS_MIN) * 100 / (BRIGHTNESS_MAX - BRIGHTNESS_MIN);
    lv_slider_set_value(brightness_slider, initial_slider, LV_ANIM_OFF);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", initial_slider);
    lv_label_set_text(brightness_value, buf);
    lv_obj_set_style_text_color(brightness_value, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(brightness_value, &lv_font_montserrat_12, 0);
    lv_obj_align_to(brightness_value, brightness_slider, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    lv_obj_add_event_cb(brightness_slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, brightness_value);
}
