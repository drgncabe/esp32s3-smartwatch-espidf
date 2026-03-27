#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "api/gyroscope.h"
#include "api/prefs.h"
#include "context/app_context.h"
#include "core/network/console_init.h"
#include "core/network/napt_interface.h"
#include "core/network/time_init.h"
#include "core/network/wifi_init.h"
#include "core/power/power_mgmt.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network/modules/heartbeat_sensor.h"
#include "network/modules/open_scanner.h"
#include "ui/components/keyboard_helpers.h"
#include "ui/components/nice_button.h"
#include "ui/components/time_formatting.h"
#include "ui/components/toast.h"
#include "ui/components/ui_helpers.h"
#include "ui/dialogs/bluetooth_dialog.h"
#include "ui/dialogs/opentime_dialog.h"
#include "ui/dialogs/scan_dialog.h"
#include "ui/screens/components/settings/settings_common.h"
#include "ui/screens/components/settings/settings_groups.h"
#include "ui/screens/home_page.h"
#include "ui/screens/settings_page.h"
#include "util/util.h"
#include "core/display/cst_816_drv.h"

static const char *TAG = "settings";
static bool settings_open = false;

lv_obj_t *settings_screen = NULL;
lv_obj_t *main_settings_container = NULL;
lv_obj_t *group_detail_container = NULL;
bool prevent_toggle = false;
lv_obj_t *voltage_label = NULL;
lv_obj_t *gyro_temp_label = NULL;
lv_obj_t *external_temp_label = NULL;
lv_obj_t *gyro_axis_label = NULL;
lv_obj_t *accel_axis_label = NULL;
lv_obj_t *is_screen_face_up_label = NULL;
lv_obj_t *is_screen_level_label = NULL;
char pending_zipCode_buf[33] = {0};
lv_obj_t *active_ta = NULL;
lv_obj_t *active_kb = NULL;
weather_update_cb_t weather_update_callback = NULL;
zip_edit_state_t zip_state = {NULL, NULL, NULL, NULL, NULL, false};
hotspot_settings_state_t hotspot_state = {NULL, NULL, NULL, NULL, NULL, NULL};
SettingsUserData settings_ud;
bool settings_need_saved = false;

extern void open_time_picker_dialog(lv_event_t *e);
extern void open_timezone_picker_dialog(lv_event_t *e);

// Update the time field label with current time (called from layout.cpp)
void update_time_field_label(void)
{
    if (settings_ud.networkTime && settings_ud.networkTime->obj_two)
    {
        lv_obj_t *time_field_label = settings_ud.networkTime->obj_two;

        if (g_settings.get_network_time_mode() == 0)
        {
            char time_str[32];
            format_time_12hour(time_str, sizeof(time_str), true);
            lv_label_set_text(time_field_label, time_str);
        }
    }
}

// Change callbacks ------------------------------------------------------------

void change_wifi_mode(lv_event_t *e)
{
    if (prevent_toggle)
        return;
    play_haptic_click();
    prevent_toggle = true;

    multi_event_data_t *data = (multi_event_data_t *)lv_event_get_user_data(e);
    lv_obj_t *sw = data->obj_one;
    lv_obj_t *console_ip = data->obj_two;
    lv_obj_t *console_ip_title = data->obj_three;

    int currentMode = g_settings.get_network_mode();
    int newMode = (currentMode == 0) ? 1 : 0;

    if (newMode == 1)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);

    g_settings.set_network_mode(newMode, false);
    settings_need_saved = true;

    if (newMode == 1)
    {
        lv_label_set_text(console_ip, consoleIp);
        lv_label_set_text(console_ip_title, "Console IP:");
        lv_obj_clear_flag(console_ip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(console_ip_title, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(console_ip, "");
        lv_label_set_text(console_ip_title, "");
        lv_obj_add_flag(console_ip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(console_ip_title, LV_OBJ_FLAG_HIDDEN);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    prevent_toggle = false;
}

void save_hotspot_settings(lv_event_t *e)
{
    play_haptic_click();

    if (hotspot_state.ssid_kb && !lv_obj_has_flag(hotspot_state.ssid_kb, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_add_flag(hotspot_state.ssid_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (hotspot_state.password_kb && !lv_obj_has_flag(hotspot_state.password_kb, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_add_flag(hotspot_state.password_kb, LV_OBJ_FLAG_HIDDEN);
    }

    if (active_ta)
    {
        lv_obj_clear_state(active_ta, LV_STATE_FOCUSED);
    }
    active_ta = NULL;
    active_kb = NULL;

    const char *ssid = lv_textarea_get_text(hotspot_state.ssid_ta);
    const char *password = lv_textarea_get_text(hotspot_state.password_ta);

    g_settings.set_hotspot_ssid(ssid, false);
    g_settings.set_hotspot_password(password, false);
    settings_need_saved = true;

    show_toast("Hotspot settings saved", 2000);
}

// Task to enable/disable hotspot in background
static void hotspot_mode_task(void *pvParameters)
{
    int newMode = (int)(intptr_t)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(100));
    g_settings.set_hotspot_mode(newMode, false);
    vTaskDelete(NULL);
}

void change_hotspot_mode(lv_event_t *e)
{
    if (prevent_toggle)
        return;
    play_haptic_click();
    prevent_toggle = true;

    multi_event_data_t *data = (multi_event_data_t *)lv_event_get_user_data(e);
    lv_obj_t *sw = data->obj_one;

    int currentMode = g_settings.get_hotspot_mode();
    int newMode = (currentMode == 0) ? 1 : 0;

    if (newMode == 1)
    {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }

    if (hotspot_state.container)
    {
        lv_obj_t *parent = lv_obj_get_parent(hotspot_state.container);
        lv_obj_t *content = parent ? lv_obj_get_parent(parent) : NULL;

        if (newMode == 1)
        {

            lv_obj_clear_flag(hotspot_state.container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_height(hotspot_state.container, LV_SIZE_CONTENT);
        }
        else
        {

            lv_obj_set_height(hotspot_state.container, 0);
            lv_obj_add_flag(hotspot_state.container, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_invalidate(hotspot_state.ssid_ta);
        lv_obj_invalidate(hotspot_state.password_ta);
        lv_obj_invalidate(hotspot_state.save_btn);
        lv_obj_invalidate(hotspot_state.container);

        if (parent)
        {
            lv_obj_refr_size(parent);
            lv_obj_invalidate(parent);
        }

        if (content)
        {
            lv_obj_refr_size(content);
            lv_obj_invalidate(content);
        }

        lv_refr_now(NULL);
    }

    xTaskCreate(hotspot_mode_task, "hotspot_mode", 3072, (void *)(intptr_t)newMode, 5, NULL);

    settings_need_saved = true;

    if (newMode == 1)
    {
        show_toast("Hotspot mode enabled", 2000);
    }
    else
    {
        show_toast("Hotspot mode disabled", 2000);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    prevent_toggle = false;
}

void change_periodic_connect_mode(lv_event_t *e)
{
    if (prevent_toggle)
        return;
    play_haptic_click();
    prevent_toggle = true;

    multi_event_data_t *data = (multi_event_data_t *)lv_event_get_user_data(e);
    lv_obj_t *sw = data->obj_one;

    int currentMode = g_settings.get_periodic_connect_mode();
    int newMode = (currentMode == 0) ? 1 : 0;

    if (newMode == 1)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);

    g_settings.set_periodic_connect_mode(newMode, false);
    settings_need_saved = true;

    if (newMode == 1)
    {
        show_toast("Open network connect enabled", 2000);
    }
    else
    {
        show_toast("Open network connect disabled", 2000);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    prevent_toggle = false;
}

// Task to initialize time in background (after UI updates)
static void time_init_task(void *pvParameters)
{
    int newMode = (int)(intptr_t)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(100));
    g_settings.set_network_time_mode(newMode, true, true);
    vTaskDelete(NULL);
}

void change_network_time_mode(lv_event_t *e)
{
    if (prevent_toggle)
        return;
    play_haptic_click();
    prevent_toggle = true;

    multi_event_data_t *data = (multi_event_data_t *)lv_event_get_user_data(e);
    lv_obj_t *sw = data->obj_one;
    lv_obj_t *time_field_label = data->obj_two;

    int currentMode = g_settings.get_network_time_mode();
    int newMode = (currentMode == 0) ? 1 : 0;

    if (newMode == 1)
    {
        lv_obj_add_state(sw, LV_STATE_CHECKED);

        g_settings.set_network_time_mode(1, false, false);
        settings_need_saved = true;

        int gmtOffset_hours = g_settings.get_gmt_offset_sec() / 3600;
        char gmtOffset_str[32];
        snprintf(gmtOffset_str, sizeof(gmtOffset_str), "Timezone: UTC %+d", gmtOffset_hours);
        lv_label_set_text(time_field_label, gmtOffset_str);

        show_toast("Network time sync enabled", 2000);
    }
    else
    {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
        g_settings.set_network_time_mode(0, false, false);
        settings_need_saved = true;

        char time_str[32];
        format_time_12hour(time_str, sizeof(time_str), true);
        lv_label_set_text(time_field_label, time_str);

        show_toast("Manual time mode enabled", 2000);
    }

    xTaskCreate(time_init_task, "time_init", 3072, (void *)(intptr_t)newMode, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(50));
    prevent_toggle = false;
}

__attribute__((unused)) static void set_manual_time_correct(int hour, int minute)
{
    ESP_LOGI(TAG, "Setting manual time to: %02d:%02d", hour, minute);

    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);

    struct tm new_time = {};
    new_time.tm_year = timeinfo.tm_year;
    new_time.tm_mon = timeinfo.tm_mon;
    new_time.tm_mday = timeinfo.tm_mday;
    new_time.tm_hour = hour;
    new_time.tm_min = minute;
    new_time.tm_sec = 0;
    new_time.tm_isdst = timeinfo.tm_isdst;

    time_t new_time_t = mktime(&new_time);

    struct timeval tv = {.tv_sec = new_time_t, .tv_usec = 0};
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "Time set successfully - local time should now be %02d:%02d", hour, minute);
    show_toast("Time updated", 2000);
}

void on_time_field_clicked(lv_event_t *e)
{
    play_haptic_click();

    if (g_settings.get_network_time_mode() == 1)
    {
        open_timezone_picker_dialog(e);
    }
    else
    {
        open_time_picker_dialog(e);
    }
}

void start_open_scanner_ts(lv_event_t *e)
{
    play_haptic_click();
    open_opentime_dialog();
}

void change_gyro_mode(lv_event_t *e)
{
    if (prevent_toggle)
        return;
    play_haptic_click();
    prevent_toggle = true;

    multi_event_data_t *data = (multi_event_data_t *)lv_event_get_user_data(e);
    lv_obj_t *sw = data->obj_one;

    int currentMode = g_settings.get_gyro_mode();
    int newMode = (currentMode == 0) ? 1 : 0;

    if (newMode == 1)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);

    g_settings.set_gyro_mode(newMode, false);
    settings_need_saved = true;

    vTaskDelay(pdMS_TO_TICKS(50));
    prevent_toggle = false;
}

void change_haptics_mode(lv_event_t *e)
{
    if (prevent_toggle)
        return;
    play_haptic_click();
    prevent_toggle = true;

    multi_event_data_t *data = (multi_event_data_t *)lv_event_get_user_data(e);
    lv_obj_t *sw = data->obj_one;

    int currentMode = g_settings.get_haptics_enabled();
    int newMode = (currentMode == 0) ? 1 : 0;

    if (newMode == 1)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);

    g_settings.set_haptics_enabled(newMode, false);
    settings_need_saved = true;

    vTaskDelay(pdMS_TO_TICKS(50));
    prevent_toggle = false;
}

void change_power_mode(lv_event_t *e)
{
    if (prevent_toggle)
        return;
    play_haptic_click();
    prevent_toggle = true;

    multi_event_data_t *data = (multi_event_data_t *)lv_event_get_user_data(e);
    lv_obj_t *sw = data->obj_one;

    int currentMode = g_settings.get_power_mode();
    int newMode = (currentMode == 0) ? 1 : 0;

    if (newMode == 1)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);

    g_settings.set_power_mode(newMode, false);
    settings_need_saved = true;

    if (newMode == 1)
        show_toast("Power saving mode enabled", 2000);
    else
        show_toast("Power saving mode disabled", 2000);

    vTaskDelay(pdMS_TO_TICKS(50));
    prevent_toggle = false;
}

void power_off_watch_handler(lv_event_t *e)
{
    play_haptic_click();
    show_toast("Powering off...", 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    power_off_watch();
}

void restart_watch_handler(lv_event_t *e)
{
    play_haptic_click();
    show_toast("Restarting...", 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

// Group detail pages ------------------------------------------------------------

void close_group_detail(lv_event_t *e)
{
    if (group_detail_container == NULL)
        return;

    if (e)
        play_haptic_click();

    if (active_kb)
    {
        lv_obj_add_flag(active_kb, LV_OBJ_FLAG_HIDDEN);
    }

    voltage_label = NULL;
    gyro_temp_label = NULL;
    external_temp_label = NULL;
    gyro_axis_label = NULL;
    accel_axis_label = NULL;
    is_screen_face_up_label = NULL;
    is_screen_level_label = NULL;

    zip_state.ta = NULL;
    zip_state.kb = NULL;
    zip_state.edit_btn = NULL;
    zip_state.save_btn = NULL;
    zip_state.cancel_btn = NULL;
    zip_state.is_editing = false;

    hotspot_state.ssid_ta = NULL;
    hotspot_state.password_ta = NULL;
    hotspot_state.ssid_kb = NULL;
    hotspot_state.password_kb = NULL;
    hotspot_state.save_btn = NULL;
    hotspot_state.container = NULL;

    active_ta = NULL;
    active_kb = NULL;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, group_detail_container);
    lv_anim_set_values(&a, 0, LV_HOR_RES);
    lv_anim_set_time(&a, 160);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)(void (*)(void *, int32_t))lv_obj_set_x);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_deleted_cb(&a, [](lv_anim_t *a)
                           { lv_obj_del((lv_obj_t *)a->var); });
    lv_anim_start(&a);

    group_detail_container = NULL;

    lv_obj_clear_flag(main_settings_container, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *create_group_detail_page(const char *title)
{
    lv_obj_add_flag(main_settings_container, LV_OBJ_FLAG_HIDDEN);

    group_detail_container = lv_obj_create(settings_screen);
    lv_obj_set_size(group_detail_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(group_detail_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group_detail_container, 0, 0);
    lv_obj_set_style_pad_all(group_detail_container, 0, 0);
    lv_obj_clear_flag(group_detail_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *content = lv_obj_create(group_detail_container);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 8, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 6, 0);
    lv_obj_set_scroll_snap_y(content, LV_SCROLL_SNAP_NONE); // Disable snap for smoother scrolling

    // Header
    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 30);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 24, 24);
    lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_add_event_cb(back_btn, close_group_detail, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(back_btn, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    // Title
    lv_obj_t *title_label = lv_label_create(header);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);

    // Spacer (to balance layout)
    lv_obj_t *spacer = lv_obj_create(header);
    lv_obj_set_size(spacer, 24, 24);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    // Slide in animation - faster and smoother
    lv_obj_set_x(group_detail_container, LV_HOR_RES);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, group_detail_container);
    lv_anim_set_values(&a, LV_HOR_RES, 0);
    lv_anim_set_time(&a, 160); // Reduced from 180ms
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)(void (*)(void *, int32_t))lv_obj_set_x);
    lv_anim_start(&a);

    return content;
}

// API group (open_api_group and zip helpers in api_group.cpp) - shared callbacks for keyboard/click below

void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
    {
        ESP_LOGI(TAG, "Keyboard closed");
        // keyboard closed
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        if (active_ta)
        {
            lv_obj_clear_state(active_ta, LV_STATE_FOCUSED);
        }
        active_ta = NULL;
        active_kb = NULL;
        // Restore touch offsets so ui feels natural again
        // This h
        keyboard_cst816_touch_offsets(false);
    }
}

void content_clicked_cb(lv_event_t *e)
{
    // If any keyboard is active and user clicks outside textarea, hide it
    if (active_kb && active_ta)
    {
        lv_obj_t *target = lv_event_get_target(e);
        (void)target;

        // Check if click is on textarea or keyboard or their children
        bool is_textarea_related = (target == active_ta || target == active_kb);
        if (!is_textarea_related)
        {
            // Check if target is a child of textarea or keyboard
            lv_obj_t *parent = lv_obj_get_parent(target);
            while (parent != NULL)
            {
                if (parent == active_ta || parent == active_kb)
                {
                    is_textarea_related = true;
                    break;
                }
                parent = lv_obj_get_parent(parent);
            }
        }

        if (!is_textarea_related)
        {
            // Hide keyboard - user can tap textarea again to reopen
            if (active_kb)
            {
                lv_obj_add_flag(active_kb, LV_OBJ_FLAG_HIDDEN);
                keyboard_cst816_touch_offsets(false);
            }
            if (active_ta)
            {
                lv_obj_clear_state(active_ta, LV_STATE_FOCUSED);
            }
            // Don't clear active_ta and active_kb - keep them so we can reopen keyboard
        }
    }
}

// Main settings screen ------------------------------------------------------------

void open_settings(lv_event_t *e)
{
    if (settings_open)
        return;

    play_haptic_click();

    // Get weather update callback from user_data if provided
    if (e)
    {
        void *user_data = lv_event_get_user_data(e);
        if (user_data)
        {
            weather_update_callback = (weather_update_cb_t)user_data;
        }
    }

    // Create overlay backdrop
    settings_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(settings_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(settings_screen, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(settings_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_screen, 0, 0);
    lv_obj_set_style_pad_all(settings_screen, 8, 0);
    lv_obj_clear_flag(settings_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Slide-up animation - faster
    lv_obj_set_y(settings_screen, LV_VER_RES);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, settings_screen);
    lv_anim_set_values(&a, LV_VER_RES, 0);
    lv_anim_set_time(&a, 160);                         // Reduced from 140ms
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out); // Changed to ease_out for snappier feel
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)(void (*)(void *, int32_t))lv_obj_set_y);
    lv_anim_start(&a);

    // Main content container - optimized for performance
    main_settings_container = lv_obj_create(settings_screen);
    lv_obj_set_size(main_settings_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_settings_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_settings_container, 0, 0);
    lv_obj_set_style_pad_all(main_settings_container, 4, 0);
    lv_obj_set_scroll_dir(main_settings_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(main_settings_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(main_settings_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_settings_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(main_settings_container, 6, 0);
    lv_obj_set_scroll_snap_y(main_settings_container, LV_SCROLL_SNAP_NONE); // Disable snap for smoother scrolling

    // Header
    lv_obj_t *header = lv_obj_create(main_settings_container);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 30);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 24, 24);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, close_settings, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(close_btn, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);

    // Spacer between header and content
    lv_obj_t *spacer = lv_obj_create(main_settings_container);
    lv_obj_set_size(spacer, LV_PCT(100), 15); // 15px height spacer
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    // Create group cards
    ui_create_group_card(main_settings_container, "Display", "Brightness", LV_SYMBOL_IMAGE, open_display_group);
    ui_create_group_card(main_settings_container, "Network", "Wi-Fi, Console, Hotspot", LV_SYMBOL_WIFI, open_network_group);
    ui_create_group_card(main_settings_container, "Time", "Time sync, Timezone", LV_SYMBOL_REFRESH, open_time_group);
    ui_create_group_card(main_settings_container, "API", "Weather location", LV_SYMBOL_GPS, open_api_group);
    ui_create_group_card(main_settings_container, "Sensors", "Gyroscope, Haptics", LV_SYMBOL_SETTINGS, open_sensors_group);
    ui_create_group_card(main_settings_container, "System", "Battery, Temperature", LV_SYMBOL_LIST, open_system_group);
    ui_create_group_card(main_settings_container, "Power", "Power save, Shutdown", LV_SYMBOL_POWER, open_power_group);

    lv_obj_set_user_data(settings_screen, &settings_ud);
    settings_open = true;
}

void close_settings(lv_event_t *e)
{
    if (!settings_open || settings_screen == NULL)
        return;

    play_haptic_click();

    // Save settings if needed
    if (settings_need_saved)
    {
        settings_need_saved = false;
        ESP_LOGI(TAG, "Settings saved on close");
    }

    // Clear label pointers
    voltage_label = NULL;
    gyro_temp_label = NULL;
    external_temp_label = NULL;
    gyro_axis_label = NULL;
    accel_axis_label = NULL;
    is_screen_face_up_label = NULL;
    is_screen_level_label = NULL;
    active_ta = NULL;
    active_kb = NULL;
    pending_zipCode_buf[0] = '\0';

    // Slide-down animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, settings_screen);
    lv_anim_set_values(&a, 0, LV_VER_RES);
    lv_anim_set_time(&a, 160);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)(void (*)(void *, int32_t))lv_obj_set_y);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_deleted_cb(&a, [](lv_anim_t *a)
                           {
        lv_obj_t *obj = (lv_obj_t *)a->var;
        SettingsUserData *ud = (SettingsUserData *)lv_obj_get_user_data(obj);
        
        // Free allocated memory
        if (ud->gyro) free(ud->gyro);
        if (ud->wifi) free(ud->wifi);
        if (ud->haptics) free(ud->haptics);
        if (ud->powerSave) free(ud->powerSave);
        if (ud->periodicConnect) free(ud->periodicConnect);
        if (ud->hotspot) free(ud->hotspot);
        if (ud->networkTime) free(ud->networkTime);

        ud->gyro = NULL;
        ud->wifi = NULL;
        ud->haptics = NULL;
        ud->powerSave = NULL;
        ud->periodicConnect = NULL;
        ud->hotspot = NULL;
        ud->networkTime = NULL;

        lv_obj_del(obj); });
    lv_anim_start(&a);

    settings_screen = NULL;
    main_settings_container = NULL;
    group_detail_container = NULL;
    settings_open = false;
}

bool is_settings_open(void)
{
    return settings_open;
}

void update_settings_system_info(void)
{
    if (!settings_open)
        return;

    // Update system info labels if they exist
    if (voltage_label != NULL)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.6fV", voltageGlobal);
        lv_label_set_text(voltage_label, buf);
    }

    if (gyro_temp_label != NULL && gyroEnabled)
    {
        float temp = get_gyro_temp_f();
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f°F", temp);
        lv_label_set_text(gyro_temp_label, buf);
    }

    if (external_temp_label != NULL && gyroEnabled)
    {
        float temp = gyro_temp_to_external_f();
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f°F", temp);
        lv_label_set_text(external_temp_label, buf);
    }

    if (gyro_axis_label != NULL && gyroEnabled)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "X: %.2f, Y: %.2f, Z: %.2f",
                 get_gyro_value(IMUAxis::X), get_gyro_value(IMUAxis::Y), get_gyro_value(IMUAxis::Z));
        lv_label_set_text(gyro_axis_label, buf);
    }

    if (accel_axis_label != NULL && gyroEnabled)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "X: %.2f, Y: %.2f, Z: %.2f",
                 get_accel_value(IMUAxis::X), get_accel_value(IMUAxis::Y), get_accel_value(IMUAxis::Z));
        lv_label_set_text(accel_axis_label, buf);
    }

    if (is_screen_face_up_label != NULL && gyroEnabled)
    {
        bool faceUp = is_screen_face_up();
        lv_label_set_text(is_screen_face_up_label, faceUp ? "Yes" : "No");
    }

    if (is_screen_level_label != NULL && gyroEnabled)
    {
        bool level = is_screen_level();
        lv_label_set_text(is_screen_level_label, level ? "Yes" : "No");
    }
}
