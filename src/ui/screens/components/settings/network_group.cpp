#include <stdlib.h>
#include <string.h>

#include "api/prefs.h"
#include "context/app_context.h"
#include "core/network/console_init.h"
#include "esp_heap_caps.h"
#include "ui/components/keyboard_helpers.h"
#include "ui/components/nice_button.h"
#include "ui/components/ui_helpers.h"
#include "ui/screens/components/settings/settings_common.h"
#include "ui/screens/components/settings/settings_groups.h"
#include "ui/screens/home_page.h"

void open_network_group(lv_event_t *e)
{
    play_haptic_click();
    lv_obj_t *content = create_group_detail_page("Network");

    lv_obj_add_event_cb(content, content_clicked_cb, LV_EVENT_CLICKED, NULL);
    if (group_detail_container)
    {
        lv_obj_add_event_cb(group_detail_container, content_clicked_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *console_card = ui_create_card(content);

    lv_obj_t *console_title = lv_label_create(console_card);
    lv_label_set_text(console_title, "Console mode");
    lv_obj_set_style_text_color(console_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(console_title, &lv_font_montserrat_14, 0);
    lv_obj_align(console_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *console_sub = lv_label_create(console_card);
    lv_label_set_text(console_sub, "Use watch as Wi-Fi console");
    lv_obj_set_style_text_color(console_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(console_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(console_sub, console_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(console_sub, LV_PCT(65));

    lv_obj_t *wifi_switch = lv_switch_create(console_card);
    lv_obj_set_size(wifi_switch, 42, 22);
    lv_obj_clear_flag(wifi_switch, LV_OBJ_FLAG_CHECKABLE);
    if (g_settings.get_network_mode() == 1)
        lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(wifi_switch, LV_STATE_CHECKED);
    lv_obj_align(wifi_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_ext_click_area(wifi_switch, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *console_ip_title = lv_label_create(console_card);
    lv_obj_set_style_text_color(console_ip_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(console_ip_title, &lv_font_montserrat_12, 0);

    lv_obj_t *console_ip = lv_label_create(console_card);
    lv_obj_set_style_text_color(console_ip, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(console_ip, &lv_font_montserrat_12, 0);

    if (g_settings.get_network_mode() == 1)
    {
        lv_label_set_text(console_ip_title, "Console IP:");
        lv_label_set_text(console_ip, consoleIp);
        lv_obj_clear_flag(console_ip_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(console_ip, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(console_ip_title, "");
        lv_label_set_text(console_ip, "");
        lv_obj_add_flag(console_ip_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(console_ip, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_align_to(console_ip_title, console_sub, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_align_to(console_ip, console_ip_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    multi_event_data_t *wifi_data = (multi_event_data_t *)heap_caps_malloc(sizeof(multi_event_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wifi_data)
        wifi_data = (multi_event_data_t *)malloc(sizeof(multi_event_data_t));
    wifi_data->obj_one = wifi_switch;
    wifi_data->obj_two = console_ip;
    wifi_data->obj_three = console_ip_title;
    settings_ud.wifi = wifi_data;
    lv_obj_add_event_cb(wifi_switch, change_wifi_mode, LV_EVENT_PRESSED, wifi_data);

    lv_obj_t *hotspot_card = ui_create_card(content);
    lv_obj_add_event_cb(hotspot_card, content_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hotspot_title = lv_label_create(hotspot_card);
    lv_label_set_text(hotspot_title, "Hotspot mode");
    lv_obj_set_style_text_color(hotspot_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(hotspot_title, &lv_font_montserrat_14, 0);
    lv_obj_align(hotspot_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *hotspot_sub = lv_label_create(hotspot_card);
    lv_label_set_text(hotspot_sub, "Use watch as Wi-Fi hotspot");
    lv_obj_set_style_text_color(hotspot_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hotspot_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(hotspot_sub, hotspot_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(hotspot_sub, LV_PCT(65));

    lv_obj_t *hotspot_switch = lv_switch_create(hotspot_card);
    lv_obj_set_size(hotspot_switch, 42, 22);
    lv_obj_clear_flag(hotspot_switch, LV_OBJ_FLAG_CHECKABLE);
    if (g_settings.get_hotspot_mode() == 1)
        lv_obj_add_state(hotspot_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(hotspot_switch, LV_STATE_CHECKED);
    lv_obj_align_to(hotspot_switch, hotspot_sub, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
    lv_obj_set_ext_click_area(hotspot_switch, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *hotspot_settings_container = lv_obj_create(hotspot_card);
    lv_obj_set_width(hotspot_settings_container, LV_PCT(100));
    lv_obj_set_height(hotspot_settings_container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hotspot_settings_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hotspot_settings_container, 0, 0);
    lv_obj_set_style_pad_all(hotspot_settings_container, 0, 0);
    lv_obj_clear_flag(hotspot_settings_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(hotspot_settings_container, hotspot_sub, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

    if (g_settings.get_hotspot_mode() != 1)
    {
        lv_obj_add_flag(hotspot_settings_container, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *ssid_label = lv_label_create(hotspot_settings_container);
    lv_label_set_text(ssid_label, "SSID");
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_12, 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ssid_ta = lv_textarea_create(hotspot_settings_container);
    lv_obj_set_width(ssid_ta, LV_PCT(100));
    lv_obj_align_to(ssid_ta, ssid_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_textarea_set_placeholder_text(ssid_ta, "Enter SSID");
    lv_textarea_set_one_line(ssid_ta, true);
    lv_textarea_set_max_length(ssid_ta, 32);
    lv_obj_set_style_bg_color(ssid_ta, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_text_color(ssid_ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ssid_ta, &lv_font_montserrat_12, 0);

    const char *default_ssid = g_settings.get_hotspot_ssid();
    if (default_ssid && strlen(default_ssid) > 0)
    {
        lv_textarea_set_text(ssid_ta, default_ssid);
    }

    lv_obj_t *password_label = lv_label_create(hotspot_settings_container);
    lv_label_set_text(password_label, "Password");
    lv_obj_set_style_text_color(password_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(password_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(password_label, ssid_ta, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    lv_obj_t *password_ta = lv_textarea_create(hotspot_settings_container);
    lv_obj_set_width(password_ta, LV_PCT(100));
    lv_obj_align_to(password_ta, password_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_textarea_set_placeholder_text(password_ta, "Enter password");
    lv_textarea_set_one_line(password_ta, true);
    lv_textarea_set_max_length(password_ta, 64);
    lv_obj_set_style_bg_color(password_ta, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_text_color(password_ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(password_ta, &lv_font_montserrat_12, 0);

    const char *default_password = g_settings.get_hotspot_password();
    if (default_password && strlen(default_password) > 0)
    {
        lv_textarea_set_text(password_ta, default_password);
    }

    lv_obj_t *ssid_kb = lv_keyboard_create(settings_screen);
    lv_obj_set_size(ssid_kb, LV_PCT(100), 120);
    lv_obj_align(ssid_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(ssid_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(ssid_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ssid_kb, kb_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *password_kb = lv_keyboard_create(settings_screen);
    lv_obj_set_size(password_kb, LV_PCT(100), 120);
    lv_obj_align(password_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(password_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(password_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(password_kb, kb_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_add_event_cb(ssid_ta, [](lv_event_t *e)
                        {
        play_haptic_click();
        if (hotspot_state.ssid_ta && hotspot_state.ssid_kb)
        {
            if (hotspot_state.password_kb && !lv_obj_has_flag(hotspot_state.password_kb, LV_OBJ_FLAG_HIDDEN))
            {
                lv_obj_add_flag(hotspot_state.password_kb, LV_OBJ_FLAG_HIDDEN);
            }
            active_ta = hotspot_state.ssid_ta;
            active_kb = hotspot_state.ssid_kb;
            lv_keyboard_set_textarea(hotspot_state.ssid_kb, hotspot_state.ssid_ta);
            lv_obj_clear_flag(hotspot_state.ssid_kb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(hotspot_state.ssid_ta, LV_STATE_FOCUSED);
            ui_scroll_to_textarea(hotspot_state.ssid_ta);
        } }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(password_ta, [](lv_event_t *e)
                        {
        play_haptic_click();
        if (hotspot_state.password_ta && hotspot_state.password_kb)
        {
            if (hotspot_state.ssid_kb && !lv_obj_has_flag(hotspot_state.ssid_kb, LV_OBJ_FLAG_HIDDEN))
            {
                lv_obj_add_flag(hotspot_state.ssid_kb, LV_OBJ_FLAG_HIDDEN);
            }
            active_ta = hotspot_state.password_ta;
            active_kb = hotspot_state.password_kb;
            lv_keyboard_set_textarea(hotspot_state.password_kb, hotspot_state.password_ta);
            lv_obj_clear_flag(hotspot_state.password_kb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(hotspot_state.password_ta, LV_STATE_FOCUSED);
            ui_scroll_to_textarea(hotspot_state.password_ta);
        } }, LV_EVENT_CLICKED, NULL);

    btn_config_t save_hotspot_btn_config = {
        .text = "Save",
        .icon = LV_SYMBOL_SAVE,
        .style = BTN_STYLE_PRIMARY,
        .width = LV_PCT(100),
        .height = 0,
        .callback = save_hotspot_settings,
        .user_data = NULL};
    lv_obj_t *save_hotspot_btn = qc_create_button(hotspot_settings_container, &save_hotspot_btn_config);
    lv_obj_align_to(save_hotspot_btn, password_ta, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    hotspot_state.ssid_ta = ssid_ta;
    hotspot_state.password_ta = password_ta;
    hotspot_state.ssid_kb = ssid_kb;
    hotspot_state.password_kb = password_kb;
    hotspot_state.save_btn = save_hotspot_btn;
    hotspot_state.container = hotspot_settings_container;

    multi_event_data_t *hotspot_data = (multi_event_data_t *)heap_caps_malloc(sizeof(multi_event_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!hotspot_data)
        hotspot_data = (multi_event_data_t *)malloc(sizeof(multi_event_data_t));
    hotspot_data->obj_one = hotspot_switch;
    settings_ud.hotspot = hotspot_data;
    lv_obj_add_event_cb(hotspot_switch, change_hotspot_mode, LV_EVENT_PRESSED, hotspot_data);

    lv_obj_t *periodic_card = ui_create_card(content);

    lv_obj_t *periodic_title = lv_label_create(periodic_card);
    lv_label_set_text(periodic_title, "Open NetConnect");
    lv_obj_set_style_text_color(periodic_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(periodic_title, &lv_font_montserrat_14, 0);
    lv_obj_align(periodic_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *periodic_sub = lv_label_create(periodic_card);
    lv_label_set_text(periodic_sub, "Periodically scan and connect to open networks");
    lv_obj_set_style_text_color(periodic_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(periodic_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(periodic_sub, periodic_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_obj_set_width(periodic_sub, LV_PCT(65));

    lv_obj_t *periodic_switch = lv_switch_create(periodic_card);
    lv_obj_set_size(periodic_switch, 42, 22);
    lv_obj_clear_flag(periodic_switch, LV_OBJ_FLAG_CHECKABLE);
    if (g_settings.get_periodic_connect_mode() == 1)
        lv_obj_add_state(periodic_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(periodic_switch, LV_STATE_CHECKED);
    lv_obj_align(periodic_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_ext_click_area(periodic_switch, GLOBAL_EXT_CLICK_AREA);

    multi_event_data_t *periodic_data = (multi_event_data_t *)heap_caps_malloc(sizeof(multi_event_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!periodic_data)
        periodic_data = (multi_event_data_t *)malloc(sizeof(multi_event_data_t));
    periodic_data->obj_one = periodic_switch;
    settings_ud.periodicConnect = periodic_data;
    lv_obj_add_event_cb(periodic_switch, change_periodic_connect_mode, LV_EVENT_PRESSED, periodic_data);
}
