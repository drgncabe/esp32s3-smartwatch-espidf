#include <stdlib.h>

#include "api/prefs.h"
#include "context/app_context.h"
#include "core/power/power_mgmt.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "ui/components/nice_button.h"
#include "ui/components/ui_helpers.h"
#include "ui/screens/components/settings/settings_common.h"
#include "ui/screens/components/settings/settings_groups.h"
#include "ui/screens/home_page.h"

void open_power_group(lv_event_t *e)
{
    play_haptic_click();
    lv_obj_t *content = create_group_detail_page("Power");

    lv_obj_t *power_card = ui_create_card(content);

    lv_obj_t *power_title = lv_label_create(power_card);
    lv_label_set_text(power_title, "Power Save");
    lv_obj_set_style_text_color(power_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(power_title, &lv_font_montserrat_14, 0);
    lv_obj_align(power_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *power_sub = lv_label_create(power_card);
    lv_label_set_text(power_sub, "Enable power saving mode");
    lv_obj_set_style_text_color(power_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(power_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(power_sub, power_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(power_sub, LV_PCT(65));

    lv_obj_t *power_switch = lv_switch_create(power_card);
    lv_obj_set_size(power_switch, 42, 22);
    lv_obj_clear_flag(power_switch, LV_OBJ_FLAG_CHECKABLE);
    if (g_settings.get_power_mode() == 1)
        lv_obj_add_state(power_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(power_switch, LV_STATE_CHECKED);
    lv_obj_align(power_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_ext_click_area(power_switch, GLOBAL_EXT_CLICK_AREA);

    multi_event_data_t *power_data = (multi_event_data_t *)heap_caps_malloc(sizeof(multi_event_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!power_data)
        power_data = (multi_event_data_t *)malloc(sizeof(multi_event_data_t));
    power_data->obj_one = power_switch;
    settings_ud.powerSave = power_data;
    lv_obj_add_event_cb(power_switch, change_power_mode, LV_EVENT_PRESSED, power_data);

    lv_obj_t *power_off_card = ui_create_card(content);

    lv_obj_t *power_off_title = lv_label_create(power_off_card);
    lv_label_set_text(power_off_title, "Power Off");
    lv_obj_set_style_text_color(power_off_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(power_off_title, &lv_font_montserrat_14, 0);
    lv_obj_align(power_off_title, LV_ALIGN_TOP_LEFT, 0, 0);

    btn_config_t power_off_btn_cfg = {
        .text = NULL,
        .icon = LV_SYMBOL_POWER,
        .style = BTN_STYLE_DANGER,
        .width = LV_PCT(90),
        .height = 0,
        .callback = power_off_watch_handler,
        .user_data = NULL};
    lv_obj_t *power_off_btn = qc_create_button(power_off_card, &power_off_btn_cfg);
    lv_obj_align_to(power_off_btn, power_off_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *restart_card = ui_create_card(content);

    lv_obj_t *restart_title = lv_label_create(restart_card);
    lv_label_set_text(restart_title, "Restart");
    lv_obj_set_style_text_color(restart_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(restart_title, &lv_font_montserrat_14, 0);
    lv_obj_align(restart_title, LV_ALIGN_TOP_LEFT, 0, 0);

    btn_config_t restart_btn_cfg = {
        .text = NULL,
        .icon = LV_SYMBOL_REFRESH,
        .style = BTN_STYLE_PRIMARY,
        .width = LV_PCT(90),
        .height = 0,
        .callback = restart_watch_handler,
        .user_data = NULL};
    lv_obj_t *restart_btn = qc_create_button(restart_card, &restart_btn_cfg);
    lv_obj_align_to(restart_btn, restart_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
}
