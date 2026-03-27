#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/prefs.h"
#include "context/app_context.h"
#include "esp_log.h"
#include "ui/components/keyboard_helpers.h"
#include "ui/components/nice_button.h"
#include "ui/components/toast.h"
#include "ui/components/ui_helpers.h"
#include "ui/screens/components/settings/settings_common.h"
#include "ui/screens/components/settings/settings_groups.h"
#include "core/display/cst_816_drv.h"

static const char *TAG = "settings";

void cancel_zip_edit(lv_event_t *e)
{
    play_haptic_click();

    if (zip_state.kb)
    {
        lv_obj_add_flag(zip_state.kb, LV_OBJ_FLAG_HIDDEN);
    }

    if (zip_state.ta && g_settings.get_zip_code() > 0)
    {
        char zipCodeStr[16];
        snprintf(zipCodeStr, sizeof(zipCodeStr), "%d", g_settings.get_zip_code());
        lv_textarea_set_text(zip_state.ta, zipCodeStr);
        lv_obj_clear_state(zip_state.ta, LV_STATE_FOCUSED | LV_STATE_EDITED);
    }

    if (zip_state.ta)
    {
        lv_obj_add_state(zip_state.ta, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(zip_state.ta, lv_color_hex(0x2D2D2D), LV_STATE_DISABLED);
    }

    if (zip_state.edit_btn)
        lv_obj_clear_flag(zip_state.edit_btn, LV_OBJ_FLAG_HIDDEN);
    if (zip_state.save_btn)
        lv_obj_add_flag(zip_state.save_btn, LV_OBJ_FLAG_HIDDEN);
    if (zip_state.cancel_btn)
        lv_obj_add_flag(zip_state.cancel_btn, LV_OBJ_FLAG_HIDDEN);

    zip_state.is_editing = false;
    active_ta = NULL;
    active_kb = NULL;
    keyboard_cst816_touch_offsets(false);
    close_group_detail(NULL);
}

void start_zip_edit(lv_event_t *e)
{
    play_haptic_click();

    if (!zip_state.ta || !zip_state.kb)
        return;

    lv_obj_clear_state(zip_state.ta, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(zip_state.ta, lv_color_hex(0x1E1E1E), 0);
    lv_obj_add_flag(zip_state.ta, LV_OBJ_FLAG_CLICKABLE);

    active_ta = zip_state.ta;
    active_kb = zip_state.kb;
    lv_keyboard_set_textarea(zip_state.kb, zip_state.ta);
    lv_obj_clear_flag(zip_state.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(zip_state.ta, LV_STATE_FOCUSED);

    ui_scroll_to_textarea(zip_state.ta);

    if (zip_state.edit_btn)
        lv_obj_add_flag(zip_state.edit_btn, LV_OBJ_FLAG_HIDDEN);
    if (zip_state.save_btn)
        lv_obj_clear_flag(zip_state.save_btn, LV_OBJ_FLAG_HIDDEN);
    if (zip_state.cancel_btn)
        lv_obj_clear_flag(zip_state.cancel_btn, LV_OBJ_FLAG_HIDDEN);
    keyboard_cst816_touch_offsets(true);
    zip_state.is_editing = true;
}

void zip_ta_clicked_cb(lv_event_t *e)
{
    if (zip_state.is_editing && zip_state.ta && zip_state.kb)
    {
        if (lv_obj_has_flag(zip_state.kb, LV_OBJ_FLAG_HIDDEN))
        {
            active_ta = zip_state.ta;
            active_kb = zip_state.kb;
            lv_keyboard_set_textarea(zip_state.kb, zip_state.ta);
            lv_obj_clear_flag(zip_state.kb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(zip_state.ta, LV_STATE_FOCUSED);
            ui_scroll_to_textarea(zip_state.ta);
            keyboard_cst816_touch_offsets(true);
        }
    }
}

void zipCode_changed_event(lv_event_t *e)
{
    pending_zipCode_buf[0] = '\0';

    lv_obj_t *ta = lv_event_get_target(e);
    const char *text = lv_textarea_get_text(ta);

    strncpy(pending_zipCode_buf, text, sizeof(pending_zipCode_buf));
    pending_zipCode_buf[sizeof(pending_zipCode_buf) - 1] = '\0';

    ESP_LOGI(TAG, "Zip Code changed to: %s", pending_zipCode_buf);
}

void save_weather_settings(lv_event_t *e)
{
    play_haptic_click();

    if (zip_state.kb)
    {
        lv_obj_add_flag(zip_state.kb, LV_OBJ_FLAG_HIDDEN);
        keyboard_cst816_touch_offsets(false);
    }

    if (active_ta)
    {
        lv_obj_clear_state(active_ta, LV_STATE_FOCUSED);
    }
    active_ta = NULL;
    active_kb = NULL;

    if (zip_state.ta)
    {
        const char *text = lv_textarea_get_text(zip_state.ta);
        if (text && text[0] != '\0')
        {
            int zipCode = atoi(text);
            if (zipCode > 0)
            {
                ESP_LOGI(TAG, "Saving zip code: %d", zipCode);
                g_settings.set_zip_geocode_data(zipCode, true);
                show_toast("Weather location saved", 2000);

                strncpy(pending_zipCode_buf, text, sizeof(pending_zipCode_buf));
                pending_zipCode_buf[sizeof(pending_zipCode_buf) - 1] = '\0';

                if (weather_update_callback)
                {
                    weather_update_callback();
                }
            }
            else
            {
                show_toast("Invalid zip code", 2000);
                if (g_settings.get_zip_code() > 0)
                {
                    char zipCodeStr[16];
                    snprintf(zipCodeStr, sizeof(zipCodeStr), "%d", g_settings.get_zip_code());
                    lv_textarea_set_text(zip_state.ta, zipCodeStr);
                }
            }
        }
        else
        {
            show_toast("No zip code entered", 2000);
        }

        lv_obj_clear_state(zip_state.ta, LV_STATE_FOCUSED | LV_STATE_EDITED);
    }

    active_ta = NULL;
    active_kb = NULL;
    zip_state.is_editing = false;

    close_group_detail(NULL);
}

void open_api_group(lv_event_t *e)
{
    play_haptic_click();
    lv_obj_t *content = create_group_detail_page("API");

    lv_obj_add_event_cb(content, content_clicked_cb, LV_EVENT_CLICKED, NULL);
    if (group_detail_container)
    {
        lv_obj_add_event_cb(group_detail_container, content_clicked_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *weather_card = ui_create_card(content);
    lv_obj_add_event_cb(weather_card, content_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *weather_title = lv_label_create(weather_card);
    lv_label_set_text(weather_title, "Weather Settings");
    lv_obj_set_style_text_color(weather_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(weather_title, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *zipCode_sub = lv_label_create(weather_card);
    lv_label_set_text(zipCode_sub, "Weather Location");
    lv_obj_set_style_text_color(zipCode_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(zipCode_sub, &lv_font_montserrat_12, 0);
    lv_obj_align_to(zipCode_sub, weather_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *input_container = lv_obj_create(weather_card);
    lv_obj_set_width(input_container, LV_PCT(100));
    lv_obj_set_height(input_container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(input_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_container, 0, 0);
    lv_obj_set_style_pad_all(input_container, 0, 0);
    lv_obj_clear_flag(input_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(input_container, zipCode_sub, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    lv_obj_t *zipCode_ta = lv_textarea_create(input_container);
    lv_obj_set_width(zipCode_ta, LV_PCT(80));
    lv_obj_align(zipCode_ta, LV_ALIGN_LEFT_MID, 0, 0);
    lv_textarea_set_placeholder_text(zipCode_ta, "No zip code set");
    lv_textarea_set_one_line(zipCode_ta, true);
    lv_textarea_set_max_length(zipCode_ta, 10);
    lv_obj_add_state(zipCode_ta, LV_STATE_DISABLED);

    lv_obj_set_style_bg_color(zipCode_ta, lv_color_hex(0x2D2D2D), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(zipCode_ta, lv_color_hex(0xCCCCCC), LV_STATE_DISABLED);
    lv_obj_set_style_text_font(zipCode_ta, &lv_font_montserrat_14, LV_STATE_DISABLED);
    lv_obj_set_style_pad_all(zipCode_ta, 8, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(zipCode_ta, lv_color_hex(0x888888), (lv_style_selector_t)(LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DISABLED));

    lv_obj_set_style_text_color(zipCode_ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(zipCode_ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(zipCode_ta, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_text_color(zipCode_ta, lv_color_hex(0x888888), LV_PART_TEXTAREA_PLACEHOLDER);

    lv_obj_clear_flag(zipCode_ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(zipCode_ta, zip_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    if (g_settings.get_zip_code() > 0)
    {
        char zipCodeStr[16];
        snprintf(zipCodeStr, sizeof(zipCodeStr), "%d", g_settings.get_zip_code());
        lv_textarea_set_text(zipCode_ta, zipCodeStr);
        strncpy(pending_zipCode_buf, zipCodeStr, sizeof(pending_zipCode_buf));
    }

    lv_obj_t *edit_btn = lv_btn_create(input_container);
    lv_obj_set_size(edit_btn, 40, 40);
    lv_obj_align(edit_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(edit_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(edit_btn, 8, 0);
    lv_obj_add_event_cb(edit_btn, start_zip_edit, LV_EVENT_CLICKED, NULL);

    lv_obj_t *edit_icon = lv_label_create(edit_btn);
    lv_label_set_text(edit_icon, LV_SYMBOL_EDIT);
    lv_obj_set_style_text_color(edit_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(edit_icon);

    lv_obj_t *zipCode_kb = lv_keyboard_create(settings_screen);
    lv_obj_set_size(zipCode_kb, LV_PCT(100), 120);
    lv_obj_align(zipCode_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(zipCode_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(zipCode_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(zipCode_kb, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(zipCode_ta, zipCode_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn_container = lv_obj_create(weather_card);
    lv_obj_set_width(btn_container, LV_PCT(100));
    lv_obj_set_height(btn_container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_set_style_pad_all(btn_container, 0, 0);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align_to(btn_container, input_container, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    btn_config_t save_btn_config = {
        .text = "Save",
        .icon = LV_SYMBOL_SAVE,
        .style = BTN_STYLE_PRIMARY,
        .width = LV_PCT(48),
        .height = 0,
        .callback = save_weather_settings,
        .user_data = NULL};
    lv_obj_t *save_btn = qc_create_button(btn_container, &save_btn_config);
    lv_obj_add_flag(save_btn, LV_OBJ_FLAG_HIDDEN);

    btn_config_t cancel_btn_config = {
        .text = "Cancel",
        .icon = LV_SYMBOL_CLOSE,
        .style = BTN_STYLE_SECONDARY,
        .width = LV_PCT(48),
        .height = 0,
        .callback = cancel_zip_edit,
        .user_data = NULL};
    lv_obj_t *cancel_btn = qc_create_button(btn_container, &cancel_btn_config);
    lv_obj_add_flag(cancel_btn, LV_OBJ_FLAG_HIDDEN);

    zip_state.ta = zipCode_ta;
    zip_state.kb = zipCode_kb;
    zip_state.edit_btn = edit_btn;
    zip_state.save_btn = save_btn;
    zip_state.cancel_btn = cancel_btn;
    zip_state.is_editing = false;
}
