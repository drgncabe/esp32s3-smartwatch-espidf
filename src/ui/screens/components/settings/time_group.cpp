#include <stdio.h>
#include <stdlib.h>

#include "api/prefs.h"
#include "context/app_context.h"
#include "esp_heap_caps.h"
#include "ui/components/nice_button.h"
#include "ui/components/time_formatting.h"
#include "ui/components/ui_helpers.h"
#include "ui/dialogs/opentime_dialog.h"
#include "ui/screens/components/settings/settings_common.h"
#include "ui/screens/components/settings/settings_groups.h"
#include "ui/screens/home_page.h"

extern void open_time_picker_dialog(lv_event_t *e);
extern void open_timezone_picker_dialog(lv_event_t *e);

void open_time_group(lv_event_t *e)
{
    play_haptic_click();
    lv_obj_t *content = create_group_detail_page("Time");

    lv_obj_t *net_time_card = ui_create_card(content);

    lv_obj_t *net_time_title = lv_label_create(net_time_card);
    lv_label_set_text(net_time_title, "Network time sync");
    lv_obj_set_style_text_color(net_time_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(net_time_title, &lv_font_montserrat_14, 0);
    lv_obj_align(net_time_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *net_time_sub = lv_label_create(net_time_card);
    lv_label_set_text(net_time_sub, "Auto sync time from network");
    lv_obj_set_style_text_color(net_time_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(net_time_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(net_time_sub, net_time_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(net_time_sub, LV_PCT(65));

    lv_obj_t *net_time_switch = lv_switch_create(net_time_card);
    lv_obj_set_size(net_time_switch, 42, 22);
    lv_obj_clear_flag(net_time_switch, LV_OBJ_FLAG_CHECKABLE);
    if (g_settings.get_network_time_mode() == 1)
        lv_obj_add_state(net_time_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(net_time_switch, LV_STATE_CHECKED);
    lv_obj_align(net_time_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_ext_click_area(net_time_switch, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *time_field_bg = lv_obj_create(net_time_card);
    lv_obj_set_size(time_field_bg, LV_PCT(100), 36);
    lv_obj_set_style_bg_color(time_field_bg, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_radius(time_field_bg, 8, 0);
    lv_obj_set_style_border_width(time_field_bg, 1, 0);
    lv_obj_set_style_border_color(time_field_bg, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_pad_all(time_field_bg, 8, 0);
    lv_obj_set_style_pad_top(time_field_bg, 15, 0);
    lv_obj_align_to(time_field_bg, net_time_sub, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
    lv_obj_clear_flag(time_field_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *time_field_label = lv_label_create(time_field_bg);
    int gmtOffset_hours = g_settings.get_gmt_offset_sec() / 3600;
    char gmtOffset_str[32];
    snprintf(gmtOffset_str, sizeof(gmtOffset_str), "Timezone: UTC %+d", gmtOffset_hours);

    if (g_settings.get_network_time_mode() == 1)
    {
        lv_label_set_text(time_field_label, gmtOffset_str);
    }
    else
    {
        char time_str[32];
        format_time_12hour(time_str, sizeof(time_str), true);
        lv_label_set_text(time_field_label, time_str);
    }
    lv_obj_set_style_text_color(time_field_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(time_field_label, &lv_font_montserrat_12, 0);
    lv_obj_align(time_field_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *chevron = lv_label_create(time_field_bg);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, lv_color_hex(0x888888), 0);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *time_field_clickable = lv_obj_create(time_field_bg);
    lv_obj_set_size(time_field_clickable, LV_PCT(25), LV_PCT(100));
    lv_obj_align(time_field_clickable, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(time_field_clickable, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_field_clickable, 0, 0);
    lv_obj_set_style_pad_all(time_field_clickable, 0, 0);
    lv_obj_clear_flag(time_field_clickable, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(time_field_clickable, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(time_field_clickable, GLOBAL_EXT_CLICK_AREA);

    multi_event_data_t *net_time_data = (multi_event_data_t *)heap_caps_malloc(sizeof(multi_event_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!net_time_data)
        net_time_data = (multi_event_data_t *)malloc(sizeof(multi_event_data_t));
    net_time_data->obj_one = net_time_switch;
    net_time_data->obj_two = time_field_label;
    settings_ud.networkTime = net_time_data;

    lv_obj_add_event_cb(net_time_switch, change_network_time_mode, LV_EVENT_PRESSED, net_time_data);
    lv_obj_add_event_cb(time_field_clickable, on_time_field_clicked, LV_EVENT_PRESSED, NULL);

    lv_obj_t *opentime_card = ui_create_card(content);

    lv_obj_t *opentime_title = lv_label_create(opentime_card);
    lv_label_set_text(opentime_title, "OpenTime Sync");
    lv_obj_set_style_text_color(opentime_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(opentime_title, &lv_font_montserrat_14, 0);
    lv_obj_align(opentime_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *opentime_sub = lv_label_create(opentime_card);
    lv_label_set_text(opentime_sub, "Attempt to sync time from open wifi networks");
    lv_obj_set_style_text_color(opentime_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(opentime_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(opentime_sub, opentime_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(opentime_sub, LV_PCT(80));

    btn_config_t opentime_btn_cfg = {
        .text = NULL,
        .icon = LV_SYMBOL_WIFI,
        .style = BTN_STYLE_PRIMARY,
        .width = LV_PCT(40),
        .height = 0,
        .callback = start_open_scanner_ts,
        .user_data = NULL};
    lv_obj_t *opentime_btn = qc_create_button(opentime_card, &opentime_btn_cfg);
    lv_obj_align_to(opentime_btn, opentime_sub, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
}
