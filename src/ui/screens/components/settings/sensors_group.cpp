#include <stdlib.h>

#include "api/prefs.h"
#include "context/app_context.h"
#include "esp_heap_caps.h"
#include "ui/components/ui_helpers.h"
#include "ui/screens/components/settings/settings_common.h"
#include "ui/screens/components/settings/settings_groups.h"
#include "ui/screens/home_page.h"

void open_sensors_group(lv_event_t *e)
{
    play_haptic_click();
    lv_obj_t *content = create_group_detail_page("Sensors");

    lv_obj_t *gyro_card = ui_create_card(content);

    lv_obj_t *gyro_title = lv_label_create(gyro_card);
    lv_label_set_text(gyro_title, "Gyroscope");
    lv_obj_set_style_text_color(gyro_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(gyro_title, &lv_font_montserrat_14, 0);
    lv_obj_align(gyro_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *gyro_sub = lv_label_create(gyro_card);
    lv_label_set_text(gyro_sub, "Enable gyroscope for motion detection");
    lv_obj_set_style_text_color(gyro_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(gyro_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(gyro_sub, gyro_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(gyro_sub, LV_PCT(65));

    lv_obj_t *gyro_switch = lv_switch_create(gyro_card);
    lv_obj_set_size(gyro_switch, 42, 22);
    lv_obj_clear_flag(gyro_switch, LV_OBJ_FLAG_CHECKABLE);
    if (g_settings.get_gyro_mode() == 1)
        lv_obj_add_state(gyro_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(gyro_switch, LV_STATE_CHECKED);
    lv_obj_align(gyro_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_ext_click_area(gyro_switch, GLOBAL_EXT_CLICK_AREA);

    multi_event_data_t *gyro_data = (multi_event_data_t *)heap_caps_malloc(sizeof(multi_event_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gyro_data)
        gyro_data = (multi_event_data_t *)malloc(sizeof(multi_event_data_t));
    gyro_data->obj_one = gyro_switch;
    settings_ud.gyro = gyro_data;
    lv_obj_add_event_cb(gyro_switch, change_gyro_mode, LV_EVENT_PRESSED, gyro_data);

    lv_obj_t *haptics_card = ui_create_card(content);

    lv_obj_t *haptics_title = lv_label_create(haptics_card);
    lv_label_set_text(haptics_title, "Haptics");
    lv_obj_set_style_text_color(haptics_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(haptics_title, &lv_font_montserrat_14, 0);
    lv_obj_align(haptics_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *haptics_sub = lv_label_create(haptics_card);
    lv_label_set_text(haptics_sub, "Vibrate on taps and alerts");
    lv_obj_set_style_text_color(haptics_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(haptics_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(haptics_sub, haptics_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(haptics_sub, LV_PCT(65));

    lv_obj_t *haptics_switch = lv_switch_create(haptics_card);
    lv_obj_set_size(haptics_switch, 42, 22);
    lv_obj_clear_flag(haptics_switch, LV_OBJ_FLAG_CHECKABLE);
    if (g_settings.get_haptics_enabled() == 1)
        lv_obj_add_state(haptics_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(haptics_switch, LV_STATE_CHECKED);
    lv_obj_align(haptics_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_ext_click_area(haptics_switch, GLOBAL_EXT_CLICK_AREA);

    multi_event_data_t *haptics_data = (multi_event_data_t *)heap_caps_malloc(sizeof(multi_event_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!haptics_data)
        haptics_data = (multi_event_data_t *)malloc(sizeof(multi_event_data_t));
    haptics_data->obj_one = haptics_switch;
    settings_ud.haptics = haptics_data;
    lv_obj_add_event_cb(haptics_switch, change_haptics_mode, LV_EVENT_PRESSED, haptics_data);
}
