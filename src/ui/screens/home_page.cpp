#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include <lvgl.h>
#include "api/weather_api.h"
#include "context/app_context.h"
#include "core/network/wifi_init.h"
#include "core/power/power_mgmt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/components/nice_button.h"
#include "ui/components/time_formatting.h"
#include "ui/components/toast.h"
#include "ui/dialogs/scan_dialog.h"
#include "ui/screens/heartbeat_sensor_page.h"
#include "ui/screens/home_page.h"
#include "ui/screens/settings_page.h"
#include "util/util.h"

static const char *TAG = "layout";

static lv_obj_t *time_label;
static lv_obj_t *date_label;
static lv_obj_t *battery_icon = NULL;
static lv_obj_t *battery_percent = NULL;
static lv_obj_t *battery_fill = NULL;
static lv_obj_t *battery_charge_icon = NULL;
static lv_obj_t *weather_page_container = NULL;
static lv_obj_t *weather_page_parent = NULL;
static lv_obj_t *weather_temp_label = NULL;

static lv_obj_t *network_label = NULL;
static lv_obj_t *network_btn = NULL;

static lv_obj_t *save_weather_btn = NULL;

lv_obj_t *weather_location_subheading = NULL;

static lv_obj_t *time_picker_dialog = NULL;
static lv_obj_t *timezone_picker_dialog = NULL;
static int selected_hour = 0;
static int selected_minute = 0;
static int selected_timezone_offset = 0; // in hours, e.g., -5 for EST

// Forward declarations

void update_time_label(lv_timer_t *timer);
void update_date_label(lv_timer_t *timer);

static lv_obj_t *active_ta = NULL;
static lv_obj_t *active_kb = NULL;

static bool should_show_weather_toast = false;

LV_FONT_DECLARE(fa_weather_24);

// forward declarations
void create_emergency_contacts(lv_obj_t *parent);
void create_weather_page(lv_obj_t *parent);

void refresh_weather_container(bool success)
{
    if (weather_page_container != NULL)
    {
        lv_obj_del(weather_page_container);
    }

    if (weather_page_parent != NULL)
    {
        weather_page_container = lv_obj_create(weather_page_parent);
        lv_obj_set_size(weather_page_container, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(weather_page_container, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(weather_page_container, 0, 0);

        create_weather_page(weather_page_container);
        if (weather_location_subheading != NULL)
        {
            if (success)
            {
                FriendlyGeocodeData geoData = g_settings.get_zip_geocode_data(g_settings.get_zip_code());
                lv_label_set_text(weather_location_subheading, geoData.friendlyName);
                if (geoData.friendlyAPIGeodata != NULL)
                {
                    free(const_cast<char *>(geoData.friendlyAPIGeodata));
                }
                if (geoData.friendlyName != NULL)
                {
                    free(const_cast<char *>(geoData.friendlyName));
                }
            }
            else
            {
                lv_label_set_text(weather_location_subheading, "Weather Unavailable");
            }
        }

        if (weather_temp_label != NULL && !success)
        {
            lv_label_set_text(weather_temp_label, "-");
            lv_obj_set_style_pad_left(weather_temp_label, 0, 0);
        }

        ESP_LOGI(TAG, "Weather display refreshed");
    }

    if (save_weather_btn)
    {
        lv_obj_set_style_bg_color(save_weather_btn, lv_color_hex(0x2196F3), 0);
        lv_obj_t *content = lv_obj_get_child(save_weather_btn, 0);
        if (content != NULL)
        {
            int child_count = lv_obj_get_child_cnt(content);
            lv_obj_t *label = NULL;
            if (child_count == 2)
            {
                label = lv_obj_get_child(content, 1);
            }
            else if (child_count == 1)
            {
                label = lv_obj_get_child(content, 0);
            }

            if (label != NULL)
            {
                lv_label_set_text(label, "Save Changes");
            }
        }
    }

    if (should_show_weather_toast)
    {
        show_toast("Weather settings saved", 2000);
        should_show_weather_toast = false;
    }
}

void update_weather(void)
{
    ESP_LOGI(TAG, "Updating weather for place:");
    FriendlyGeocodeData geoData = g_settings.get_zip_geocode_data(g_settings.get_zip_code());
    ESP_LOGI(TAG, "%s", geoData.friendlyName);
    if (weather_location_subheading != NULL)
    {
        ESP_LOGI(TAG, "Updating weather location subheading:");
        lv_label_set_text(weather_location_subheading, geoData.friendlyName);
    }
    if (geoData.friendlyAPIGeodata != NULL)
    {
        free(const_cast<char *>(geoData.friendlyAPIGeodata));
    }
    if (geoData.friendlyName != NULL)
    {
        free(const_cast<char *>(geoData.friendlyName));
    }
    start_weather_task();
}

void refresh_weather_display(lv_timer_t *timer)
{
    update_weather();
}

void update_network_status(lv_timer_t *timer)
{
    static int last_flash = 0xFFFF00;
    if (network_label != NULL)
    {
        WiFiState wifi_state = wifi_get_state();
        if (g_settings.get_network_mode() == 0)
        {
            lv_label_set_text(network_label, LV_SYMBOL_WIFI);
        }
        else if (g_settings.get_network_mode() == 1)
        {
            lv_label_set_text(network_label, LV_SYMBOL_DRIVE);
        }
        if (g_settings.get_network_mode() == 0 && wifi_state == WIFI_CONNECTING)
        {
            if (last_flash == 0xFFFF00)
            {
                lv_obj_set_style_text_color(network_label, lv_color_hex(0xFF0000), 0);
                last_flash = 0xFF0000;
            }
            else if (last_flash == 0xFF0000)
            {
                lv_obj_set_style_text_color(network_label, lv_color_hex(0xFFFF00), 0);
                last_flash = 0xFFFF00;
            }
        }
        else
        {
            if ((wifi_state == WIFI_CONNECTED && g_settings.get_network_mode() == 0) || (wifi_state == CONSOLE_ON && g_settings.get_network_mode() == 1))
            {
                lv_obj_set_style_text_color(network_label, lv_color_hex(0x00FF00), 0);
            }
            else
            {
                lv_label_set_text(network_label, LV_SYMBOL_WIFI);
                lv_obj_set_style_text_color(network_label, lv_color_hex(0xFF0000), 0);
            }
        }
    }
}

void update_battery_display(lv_timer_t *timer)
{
    int percent = get_battery_percentage();

    // Label
    if (battery_percent != NULL)
    {
        char percent_str[8];
        snprintf(percent_str, sizeof(percent_str), "%d%%", percent);
        lv_label_set_text(battery_percent, percent_str);
    }

    // Bar
    if (battery_fill != NULL)
    {
        lv_obj_set_width(battery_fill, (16 * percent) / 100);

        bool charging = is_charging();

        if (charging)
        {
            lv_obj_set_style_bg_color(battery_fill, lv_color_hex(0x00FFA5), 0);
        }
        else
        {
            if (percent > 20)
                lv_obj_set_style_bg_color(battery_fill, lv_color_hex(0x00FF00), 0);
            else if (percent > 10)
                lv_obj_set_style_bg_color(battery_fill, lv_color_hex(0xFFA500), 0);
            else
                lv_obj_set_style_bg_color(battery_fill, lv_color_hex(0xFF0000), 0);
        }
    }

    // Charging icon overlay
    if (battery_charge_icon != NULL)
    {
        if (is_charging())
            lv_obj_clear_flag(battery_charge_icon, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(battery_charge_icon, LV_OBJ_FLAG_HIDDEN);
    }

    if (is_settings_open())
    {
        update_settings_system_info();
    }
}

void on_manual_time_set(int hour, int minute)
{
    selected_hour = hour;
    selected_minute = minute;

    ESP_LOGI(TAG, "Manual time set to: %02d:%02d", hour, minute);

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
    update_time_field_label();

    show_toast("Time updated", 2000);
}

void on_timezone_set(int offset_hours, bool has_dst)
{
    selected_timezone_offset = offset_hours;
    long gmtOffset_sec = offset_hours * 3600;
    int daylightOffset_sec = has_dst ? 3600 : 0;

    ESP_LOGI(TAG, "Timezone set to: UTC%+d", offset_hours);
    ESP_LOGI(TAG, "GMT Offset (seconds): %ld", gmtOffset_sec);
    ESP_LOGI(TAG, "DST Offset (seconds): %d", daylightOffset_sec);

    g_settings.set_gmt_offset(gmtOffset_sec, true);
    g_settings.set_daylight_offset(daylightOffset_sec, true);

    vTaskDelay(pdMS_TO_TICKS(20));

    initialize_time();

    show_toast("Timezone updated", 2000);
}

static void close_time_picker_dialog(lv_event_t *e)
{
    if (time_picker_dialog != NULL)
    {
        lv_obj_del(time_picker_dialog);
        time_picker_dialog = NULL;
    }
}

static void close_timezone_picker_dialog(lv_event_t *e)
{
    if (timezone_picker_dialog != NULL)
    {
        lv_obj_del(timezone_picker_dialog);
        timezone_picker_dialog = NULL;
    }
}

void open_time_picker_dialog(lv_event_t *e)
{
    if (time_picker_dialog != NULL)
        return;

    play_haptic_click();

    time_picker_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(time_picker_dialog, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(time_picker_dialog, lv_color_hex(0x1B1B1F), 0);
    lv_obj_set_style_radius(time_picker_dialog, 16, 0);
    lv_obj_set_style_border_width(time_picker_dialog, 0, 0);
    lv_obj_set_style_shadow_width(time_picker_dialog, 16, 0);
    lv_obj_set_style_shadow_opa(time_picker_dialog, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(time_picker_dialog, 16, 0);
    lv_obj_center(time_picker_dialog);

    lv_obj_t *title = lv_label_create(time_picker_dialog);
    lv_label_set_text(title, "Set Time");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *roller_cont = lv_obj_create(time_picker_dialog);
    lv_obj_set_size(roller_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(roller_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(roller_cont, 0, 0);
    lv_obj_set_style_pad_all(roller_cont, 0, 0);
    lv_obj_set_flex_flow(roller_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(roller_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align_to(roller_cont, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    lv_obj_t *hour_roller = lv_roller_create(roller_cont);
    lv_roller_set_options(hour_roller,
                          "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
                          "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
                          LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(hour_roller, 5);
    lv_obj_set_width(hour_roller, 70);
    lv_obj_set_style_bg_color(hour_roller, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_color(hour_roller, lv_color_hex(0x3D3D3D), LV_PART_SELECTED);
    lv_obj_set_style_text_color(hour_roller, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);

    lv_obj_t *colon = lv_label_create(roller_cont);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(colon, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *min_roller = lv_roller_create(roller_cont);
    lv_roller_set_options(min_roller,
                          "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
                          "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
                          "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
                          "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
                          "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
                          "50\n51\n52\n53\n54\n55\n56\n57\n58\n59",
                          LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(min_roller, 5);
    lv_obj_set_width(min_roller, 70);
    lv_obj_set_style_bg_color(min_roller, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_color(min_roller, lv_color_hex(0x3D3D3D), LV_PART_SELECTED);
    lv_obj_set_style_text_color(min_roller, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);

    lv_obj_update_layout(time_picker_dialog);
    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    lv_roller_set_selected(hour_roller, timeinfo.tm_hour, LV_ANIM_OFF);
    lv_roller_set_selected(min_roller, timeinfo.tm_min, LV_ANIM_OFF);
    lv_obj_invalidate(hour_roller);
    lv_obj_invalidate(min_roller);

    lv_obj_t *btn_cont = lv_obj_create(time_picker_dialog);
    lv_obj_set_size(btn_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align_to(btn_cont, roller_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    lv_obj_t *cancel_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(cancel_btn, 80, 36);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, close_time_picker_dialog, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    typedef struct
    {
        lv_obj_t *hour;
        lv_obj_t *minute;
    } roller_data_t;

    roller_data_t *roller_data = (roller_data_t *)malloc(sizeof(roller_data_t));
    roller_data->hour = hour_roller;
    roller_data->minute = min_roller;

    lv_obj_t *set_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(set_btn, 80, 36);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_radius(set_btn, 8, 0);

    lv_obj_add_event_cb(set_btn, [](lv_event_t *e)
                        {
        roller_data_t *data = (roller_data_t *)lv_event_get_user_data(e);
        
        int hour_idx = lv_roller_get_selected(data->hour);
        int min_idx = lv_roller_get_selected(data->minute);
        int hour = hour_idx;
        int minute = min_idx;
        
        if (hour < 0 || hour > 23) {
            ESP_LOGW(TAG, "Invalid hour %d, clamping", hour);
            hour = (hour < 0) ? 0 : 23;
        }
        if (minute < 0 || minute > 59) {
            ESP_LOGW(TAG, "Invalid minute %d, clamping", minute);
            minute = (minute < 0) ? 0 : 59;
        }
        
        ESP_LOGI(TAG, "Time picker: hour_idx=%d -> %d, min_idx=%d -> %d", 
                 hour_idx, hour, min_idx, minute);
        
        on_manual_time_set(hour, minute);
        free(data);
        close_time_picker_dialog(e); }, LV_EVENT_CLICKED, roller_data);

    lv_obj_t *set_label = lv_label_create(set_btn);
    lv_label_set_text(set_label, "Set");
    lv_obj_center(set_label);
}

void open_timezone_picker_dialog(lv_event_t *e)
{
    if (timezone_picker_dialog != NULL)
        return;

    play_haptic_click();

    timezone_picker_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(timezone_picker_dialog, LV_PCT(90), LV_PCT(90));
    lv_obj_set_style_bg_color(timezone_picker_dialog, lv_color_hex(0x1B1B1F), 0);
    lv_obj_set_style_radius(timezone_picker_dialog, 16, 0);
    lv_obj_set_style_border_width(timezone_picker_dialog, 0, 0);
    lv_obj_set_style_shadow_width(timezone_picker_dialog, 16, 0);
    lv_obj_set_style_shadow_opa(timezone_picker_dialog, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(timezone_picker_dialog, 16, 0);
    lv_obj_set_style_pad_bottom(timezone_picker_dialog, 0, 0);
    lv_obj_center(timezone_picker_dialog);

    lv_obj_t *title = lv_label_create(timezone_picker_dialog);
    lv_label_set_text(title, "Set Timezone");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *offset_roller = lv_roller_create(timezone_picker_dialog);
    lv_roller_set_options(offset_roller,
                          "UTC-12\nUTC-11\nUTC-10\nUTC-9\nUTC-8\nUTC-7\nUTC-6\nUTC-5\nUTC-4\nUTC-3\n"
                          "UTC-2\nUTC-1\nUTC+0\nUTC+1\nUTC+2\nUTC+3\nUTC+4\nUTC+5\nUTC+6\nUTC+7\n"
                          "UTC+8\nUTC+9\nUTC+10\nUTC+11\nUTC+12",
                          LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(offset_roller, 5);
    lv_obj_set_width(offset_roller, LV_PCT(80));
    lv_obj_set_height(offset_roller, LV_PCT(60));
    lv_obj_set_style_bg_color(offset_roller, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_color(offset_roller, lv_color_hex(0x3D3D3D), LV_PART_SELECTED);
    lv_obj_set_style_text_color(offset_roller, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);
    lv_obj_align_to(offset_roller, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    int current_offset = g_settings.get_gmt_offset_sec() / 3600;
    int roller_index = current_offset + 12;
    if (roller_index < 0)
        roller_index = 0;
    if (roller_index > 24)
        roller_index = 24;
    lv_roller_set_selected(offset_roller, roller_index, LV_ANIM_OFF);

    lv_obj_t *dst_label = lv_label_create(timezone_picker_dialog);
    lv_label_set_text(dst_label, "Daylight Saving Time");
    lv_obj_set_style_text_font(dst_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dst_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(dst_label, offset_roller, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    lv_obj_t *dst_switch = lv_switch_create(timezone_picker_dialog);
    lv_obj_set_size(dst_switch, 42, 22);
    bool dst_enabled = (g_settings.get_daylight_offset_sec() != 0);
    if (dst_enabled)
        lv_obj_add_state(dst_switch, LV_STATE_CHECKED);
    lv_obj_align_to(dst_switch, dst_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    lv_obj_t *btn_cont = lv_obj_create(timezone_picker_dialog);
    lv_obj_set_size(btn_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align_to(btn_cont, dst_switch, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    lv_obj_t *cancel_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(cancel_btn, 80, 36);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, close_timezone_picker_dialog, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    typedef struct
    {
        lv_obj_t *offset_roller;
        lv_obj_t *dst_switch;
    } timezone_data_t;

    timezone_data_t *tz_data = (timezone_data_t *)malloc(sizeof(timezone_data_t));
    tz_data->offset_roller = offset_roller;
    tz_data->dst_switch = dst_switch;

    lv_obj_t *set_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(set_btn, 80, 36);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_radius(set_btn, 8, 0);

    lv_obj_add_event_cb(set_btn, [](lv_event_t *e)
                        {
        timezone_data_t *data = (timezone_data_t *)lv_event_get_user_data(e);
        
        int selected = lv_roller_get_selected(data->offset_roller);
        int offset_hours = selected - 12;
        bool has_dst = lv_obj_has_state(data->dst_switch, LV_STATE_CHECKED);
        
        on_timezone_set(offset_hours, has_dst);
        free(data);
        close_timezone_picker_dialog(e); }, LV_EVENT_CLICKED, tz_data);

    lv_obj_t *set_label = lv_label_create(set_btn);
    lv_label_set_text(set_label, "Set");
    lv_obj_center(set_label);
}

void on_network_btn_clicked(lv_event_t *e)
{
    play_haptic_click();
    int network_mode = g_settings.get_network_mode();
    if (network_mode == 1)
    {
        return;
    }
    open_wifi_scan_pre_dialog();
}

void initialize_layout(void)
{
    lv_obj_set_style_pad_all(lv_scr_act(), 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0X000000), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    lv_obj_t *main_scroll_container = lv_obj_create(lv_scr_act());
    lv_obj_set_scroll_dir(main_scroll_container, LV_DIR_HOR);
    lv_obj_set_size(main_scroll_container, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_snap_x(main_scroll_container, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_style_pad_row(main_scroll_container, 0, 0);
    lv_obj_set_style_pad_column(main_scroll_container, 0, 0);
    lv_obj_set_style_pad_all(main_scroll_container, 0, 0);
    lv_obj_set_flex_flow(main_scroll_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_color(main_scroll_container, lv_color_hex(0X000000), 0);
    lv_obj_set_style_border_width(main_scroll_container, 0, 0);
    lv_obj_set_style_bg_opa(main_scroll_container, LV_OPA_COVER, 0);
    lv_obj_set_style_anim_time(main_scroll_container, 80, 0); // Reduced to 80ms for faster snapping

    // Configure scroll behavior to prevent jitter when interrupted
    lv_obj_set_scroll_snap_y(main_scroll_container, LV_SCROLL_SNAP_NONE); // Only snap on X axis
    lv_obj_add_flag(main_scroll_container, LV_OBJ_FLAG_SCROLL_MOMENTUM);  // Enable momentum but allow interruption
    lv_obj_clear_flag(main_scroll_container, LV_OBJ_FLAG_SCROLL_ELASTIC); // Disable elastic effect to prevent bounce jitter

    // Page 1: Emergency Contacts (left of main page)
    lv_obj_t *emergency_page = lv_obj_create(main_scroll_container);
    lv_obj_set_flex_flow(emergency_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(emergency_page, 10, 0);
    lv_obj_set_style_pad_left(emergency_page, 10, 0);
    lv_obj_set_flex_align(emergency_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_snap_y(emergency_page, LV_SCROLL_SNAP_START);
    lv_obj_set_size(emergency_page, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_dir(emergency_page, LV_DIR_VER);
    lv_obj_set_style_border_width(emergency_page, 0, 0);
    lv_obj_set_style_pad_right(emergency_page, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(emergency_page, lv_color_hex(0X000000), 0);

    create_emergency_contacts(emergency_page);

    // Page 2: Main page (landing/default page)
    lv_obj_t *main_page = lv_obj_create(main_scroll_container);
    lv_obj_set_flex_flow(main_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(main_page, 10, 0);
    lv_obj_set_style_pad_left(main_page, 10, 0);
    lv_obj_set_flex_align(main_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_snap_y(main_page, LV_SCROLL_SNAP_START);
    lv_obj_set_size(main_page, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_dir(main_page, LV_DIR_VER);
    lv_obj_set_style_border_width(main_page, 0, 0);
    lv_obj_set_style_pad_right(main_page, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(main_page, lv_color_hex(0X000000), 0);

    lv_obj_t *header_bar = lv_obj_create(main_page);
    lv_obj_set_size(header_bar, lv_pct(100), 35);
    lv_obj_set_flex_flow(header_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_bar,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(header_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_bar, 0, 0);
    lv_obj_set_style_pad_left(header_bar, 10, 0);
    lv_obj_set_style_pad_right(header_bar, 10, 0);
    lv_obj_clear_flag(header_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(header_bar, LV_OBJ_FLAG_EVENT_BUBBLE); // Allow clicks to propagate to children

    lv_obj_t *settings_btn = lv_btn_create(header_bar);
    lv_obj_set_size(settings_btn, 30, 30);
    lv_obj_set_style_radius(settings_btn, 15, 0);
    lv_obj_set_style_bg_color(settings_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(settings_btn, 0, 0);
    lv_obj_set_style_border_width(settings_btn, 0, 0);
    lv_obj_add_flag(settings_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(settings_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(settings_btn, open_settings, LV_EVENT_PRESSED, (void *)update_weather);
    lv_obj_set_ext_click_area(settings_btn, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *settings_icon = lv_label_create(settings_btn);
    lv_label_set_text(settings_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(settings_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(settings_icon, &lv_font_montserrat_16, 0);
    lv_obj_center(settings_icon);
    lv_obj_clear_flag(settings_icon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *header_spacer = lv_obj_create(header_bar);
    lv_obj_set_size(header_spacer, 0, 0);
    lv_obj_set_style_bg_opa(header_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_spacer, 0, 0);
    lv_obj_clear_flag(header_spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(header_spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_grow(header_spacer, 1);

    network_btn = lv_btn_create(header_bar);
    lv_obj_set_size(network_btn, 30, 30);
    lv_obj_set_style_radius(network_btn, 15, 0);
    lv_obj_set_style_bg_color(network_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(network_btn, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(network_btn, 0, 0);
    lv_obj_set_style_border_width(network_btn, 0, 0);
    lv_obj_add_flag(network_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(network_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(network_btn, on_network_btn_clicked, LV_EVENT_PRESSED, NULL);

    network_label = lv_label_create(network_btn);
    if (g_settings.get_network_mode() == 0)
    {
        lv_label_set_text(network_label, LV_SYMBOL_WIFI);
    }
    else if (g_settings.get_network_mode() == 1)
    {
        lv_label_set_text(network_label, LV_SYMBOL_DRIVE);
    }
    if ((wifi_get_state() == WIFI_CONNECTED && g_settings.get_network_mode() == 0) || (wifi_get_state() == CONSOLE_ON && g_settings.get_network_mode() == 1))
    {
        lv_obj_set_style_text_color(network_label, lv_color_hex(0x00FF00), 0);
    }
    else
    {
        lv_label_set_text(network_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(network_label, lv_color_hex(0xFF0000), 0);
    }

    lv_obj_set_style_text_font(network_label, &lv_font_montserrat_16, 0);
    lv_obj_center(network_label);
    lv_obj_clear_flag(network_label, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *battery_container = lv_obj_create(header_bar);
    lv_obj_set_size(battery_container, 60, 30);
    lv_obj_set_flex_flow(battery_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(battery_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_container, 0, 0);
    lv_obj_set_style_pad_all(battery_container, 0, 0);
    lv_obj_set_style_pad_column(battery_container, 3, 0);
    lv_obj_clear_flag(battery_container, LV_OBJ_FLAG_SCROLLABLE);

    battery_icon = lv_obj_create(battery_container);
    lv_obj_set_size(battery_icon, 20, 12);
    lv_obj_set_style_radius(battery_icon, 2, 0);
    lv_obj_set_style_bg_opa(battery_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_icon, 1, 0);
    lv_obj_set_style_border_color(battery_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(battery_icon, 1, 0);
    lv_obj_clear_flag(battery_icon, LV_OBJ_FLAG_SCROLLABLE);

    battery_fill = lv_obj_create(battery_icon);
    lv_obj_set_size(battery_fill, 16, 8);
    lv_obj_set_style_radius(battery_fill, 1, 0);
    lv_obj_set_style_bg_color(battery_fill, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(battery_fill, 0, 0);
    lv_obj_align(battery_fill, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *battery_terminal = lv_obj_create(battery_container);
    lv_obj_set_size(battery_terminal, 2, 6);
    lv_obj_set_style_radius(battery_terminal, 1, 0);
    lv_obj_set_style_bg_color(battery_terminal, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(battery_terminal, 0, 0);
    lv_obj_set_style_pad_all(battery_terminal, 0, 0);
    lv_obj_align_to(battery_terminal, battery_icon, LV_ALIGN_OUT_RIGHT_MID, -1, 0);

    battery_charge_icon = lv_label_create(battery_icon);
    lv_label_set_text(battery_charge_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(battery_charge_icon, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(battery_charge_icon, &lv_font_montserrat_10, 0);
    lv_obj_center(battery_charge_icon);
    lv_obj_add_flag(battery_charge_icon, LV_OBJ_FLAG_HIDDEN);

    battery_percent = lv_label_create(battery_container);
    lv_label_set_text(battery_percent, "100%");
    lv_obj_set_style_text_color(battery_percent, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(battery_percent, &lv_font_montserrat_12, 0);

    lv_timer_create(update_battery_display, 3000, NULL);

    lv_timer_create(update_network_status, 1000, NULL);

    // ========== MODERN MINIMAL CLOCK UI ==========
    lv_obj_t *main_content = lv_obj_create(main_page);
    lv_obj_set_style_translate_y(main_content, -35, 0); // Move up more for better centering
    lv_obj_set_style_pad_all(main_content, 0, 0);
    lv_obj_set_flex_flow(main_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(main_content, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_align(main_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(main_content, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(main_content, 0, 0);
    lv_obj_clear_flag(main_content, LV_OBJ_FLAG_SCROLLABLE);

    // Time display - moderately sized for readability
    time_label = lv_label_create(main_content);
    lv_label_set_text(time_label, "");
    lv_timer_create(update_time_label, 1000, NULL);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_top(time_label, 0, 0); // Remove extra top padding

    // Thin accent line under time
    lv_obj_t *accent_line = lv_obj_create(main_content);
    lv_obj_set_size(accent_line, 60, 2);
    lv_obj_set_style_bg_color(accent_line, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(accent_line, 0, 0);
    lv_obj_set_style_radius(accent_line, 1, 0);
    lv_obj_set_style_pad_top(accent_line, 8, 0);
    lv_obj_clear_flag(accent_line, LV_OBJ_FLAG_SCROLLABLE);

    // Date display
    date_label = lv_label_create(main_content);
    lv_label_set_text(date_label, "");
    lv_timer_create(update_date_label, 1000, NULL);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(date_label, 12, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);

    weather_page_parent = main_page;

    weather_page_container = lv_obj_create(weather_page_parent);
    lv_obj_set_size(weather_page_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(weather_page_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(weather_page_container, 0, 0);
    create_weather_page(weather_page_container);
    lv_timer_create(refresh_weather_display, 180000, NULL);


    // Page 4: Heartbeat Sensor page
    lv_obj_t *heartbeat_sensor_page = lv_obj_create(main_scroll_container);
    lv_obj_set_flex_flow(heartbeat_sensor_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(heartbeat_sensor_page, 10, 0);
    lv_obj_set_style_pad_left(heartbeat_sensor_page, 10, 0);
    lv_obj_set_flex_align(heartbeat_sensor_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_snap_y(heartbeat_sensor_page, LV_SCROLL_SNAP_START);
    lv_obj_set_size(heartbeat_sensor_page, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_dir(heartbeat_sensor_page, LV_DIR_VER);
    lv_obj_set_style_border_width(heartbeat_sensor_page, 0, 0);
    lv_obj_set_style_pad_right(heartbeat_sensor_page, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(heartbeat_sensor_page, lv_color_hex(0X000000), 0);

    create_heartbeat_sensor_page(heartbeat_sensor_page);

    // Scroll to main page (second page) to make it the default landing page
    lv_obj_scroll_to_view(main_page, LV_ANIM_OFF);
}

const char *get_weather_icon(double weather_code)
{
    int code = (int)weather_code;

    static const char *SUN = "\xEF\x86\x85";
    static const char *CLOUD_SUN = "\xEF\x9B\x84";
    static const char *CLOUD = "\xEF\x83\x82";
    static const char *FOG = "\xEF\x9D\x9F";
    static const char *DRIZZLE = "\xEF\x9C\xBD";
    static const char *RAIN = "\xEF\x9D\x80";
    static const char *SNOW = "\xEF\x8B\x9C";
    static const char *STORM = "\xEF\x9D\xAC";

    if (code == 0)
        return SUN;
    if (code <= 3)
        return CLOUD_SUN;
    if (code <= 49)
        return FOG;
    if (code <= 59)
        return DRIZZLE;
    if (code <= 69)
        return RAIN;
    if (code <= 79)
        return SNOW;
    if (code <= 99)
        return STORM;

    return CLOUD;
}
lv_color_t get_weather_icon_color(double weather_code)
{
    int code = (int)weather_code;

    lv_color_t COL_SUN = lv_color_hex(0xFFD700);
    lv_color_t COL_PARTLY = lv_color_hex(0xFFCC33);
    lv_color_t COL_CLOUD = lv_color_hex(0xD0D0D0);
    lv_color_t COL_FOG = lv_color_hex(0xE6E6E6);
    lv_color_t COL_DRIZZLE = lv_color_hex(0xA7C7E7);
    lv_color_t COL_RAIN = lv_color_hex(0x4A90E2);
    lv_color_t COL_SNOW = lv_color_hex(0xFFFFFF);
    lv_color_t COL_STORM = lv_color_hex(0xFFB000);

    if (code == 0)
        return COL_SUN;
    if (code <= 3)
        return COL_PARTLY;
    if (code <= 49)
        return COL_FOG;
    if (code <= 59)
        return COL_DRIZZLE;
    if (code <= 69)
        return COL_RAIN;
    if (code <= 79)
        return COL_SNOW;
    if (code <= 99)
        return COL_STORM;

    return COL_CLOUD;
}


void create_weather_page(lv_obj_t *container)
{
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_pad_left(container, 15, 0);
    lv_obj_set_style_pad_right(container, 15, 0);
    lv_obj_set_style_pad_top(container, 0, 0);
    lv_obj_set_style_pad_bottom(container, 0, 0);
    lv_obj_set_style_pad_row(container, 12, 0);

    lv_obj_t *spacer = lv_obj_create(container);
    lv_obj_set_size(spacer, 0, 20);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *current_card = lv_obj_create(container);
    lv_obj_set_size(current_card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(current_card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(current_card, 12, 0);
    lv_obj_set_style_border_width(current_card, 0, 0);
    lv_obj_set_style_shadow_width(current_card, 15, 0);
    lv_obj_set_style_shadow_color(current_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(current_card, LV_OPA_30, 0);
    lv_obj_clear_flag(current_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *current_content = lv_obj_create(current_card);
    lv_obj_set_size(current_content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(current_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(current_content, 0, 0);
    lv_obj_set_style_pad_all(current_content, 15, 0);
    lv_obj_set_flex_flow(current_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(current_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(current_content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *now_label = lv_label_create(current_content);
    lv_label_set_text(now_label, "Now");
    lv_obj_set_style_text_color(now_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(now_label, &lv_font_montserrat_14, 0);

    weather_temp_label = lv_label_create(current_content);
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.0f°", currentTemperature);
    lv_label_set_text(weather_temp_label, temp_str);
    lv_obj_set_style_text_color(weather_temp_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(weather_temp_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_pad_left(weather_temp_label, 10, 0);

    lv_obj_t *weather_icon = lv_label_create(current_content);
    lv_obj_set_style_text_font(weather_icon, &fa_weather_24, LV_PART_MAIN);
    lv_label_set_text(weather_icon, get_weather_icon(currentWeatherCode));
    lv_color_t maincolor = get_weather_icon_color(currentWeatherCode);
    lv_obj_set_style_text_color(weather_icon, maincolor, 0);

    weather_location_subheading = lv_label_create(container);
    lv_label_set_text(weather_location_subheading, "Loading...");
    lv_obj_set_style_text_color(weather_location_subheading, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(weather_location_subheading, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(weather_location_subheading, 5, 0);
    lv_obj_set_style_pad_bottom(weather_location_subheading, 5, 0);

    lv_obj_t *forecast_header = lv_label_create(container);
    lv_label_set_text(forecast_header, "This Week");
    lv_obj_set_style_text_color(forecast_header, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(forecast_header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(forecast_header, 5, 0);
    lv_obj_set_style_pad_bottom(forecast_header, 5, 0);

    char today_date[11];
    format_current_date(today_date, sizeof(today_date));

    int days_shown = 0;
    for (size_t i = 0; i < highLowsCount && days_shown < 6; i++)
    {
        if (strcmp(globalWeatherData[i].date, today_date) == 0)
        {
            continue;
        }

        lv_obj_t *day_card = lv_obj_create(container);
        lv_obj_set_size(day_card, lv_pct(100), 50);
        lv_obj_set_style_bg_color(day_card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_radius(day_card, 10, 0);
        lv_obj_set_style_border_width(day_card, 0, 0);
        lv_obj_set_style_shadow_width(day_card, 8, 0);
        lv_obj_set_style_shadow_color(day_card, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(day_card, LV_OPA_20, 0);
        lv_obj_clear_flag(day_card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *day_content = lv_obj_create(day_card);
        lv_obj_set_size(day_content, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(day_content, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(day_content, 0, 0);
        lv_obj_set_style_pad_left(day_content, 15, 0);
        lv_obj_set_style_pad_right(day_content, 15, 0);
        lv_obj_set_flex_flow(day_content, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(day_content, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(day_content, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *day_label = lv_label_create(day_content);
        lv_label_set_text(day_label, get_day_of_week_from_date(globalWeatherData[i].date));
        lv_obj_set_style_text_color(day_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(day_label, &lv_font_montserrat_16, 0);
        lv_obj_set_width(day_label, 40);

        lv_obj_t *icon = lv_label_create(day_content);
        lv_obj_set_style_text_font(icon, &fa_weather_24, 0);
        lv_label_set_text(icon, get_weather_icon(globalWeatherData[i].weather_code));
        lv_color_t color = get_weather_icon_color(globalWeatherData[i].weather_code);
        lv_obj_set_style_text_color(icon, color, 0);

        lv_obj_t *spacer = lv_obj_create(day_content);
        lv_obj_set_size(spacer, LV_SIZE_CONTENT, 1);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_grow(spacer, 1);

        lv_obj_t *temp_container = lv_obj_create(day_content);
        lv_obj_set_size(temp_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(temp_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(temp_container, 0, 0);
        lv_obj_set_style_pad_all(temp_container, 0, 0);
        lv_obj_set_flex_flow(temp_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(temp_container, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(temp_container, 8, 0);
        lv_obj_clear_flag(temp_container, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *high_label = lv_label_create(temp_container);
        char high_str[16];
        snprintf(high_str, sizeof(high_str), "%.0f°", globalWeatherData[i].high);
        lv_label_set_text(high_label, high_str);
        lv_obj_set_style_text_color(high_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(high_label, &lv_font_montserrat_16, 0);

        lv_obj_t *low_label = lv_label_create(temp_container);
        char low_str[16];
        snprintf(low_str, sizeof(low_str), "%.0f°", globalWeatherData[i].low);
        lv_label_set_text(low_label, low_str);
        lv_obj_set_style_text_color(low_label, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(low_label, &lv_font_montserrat_16, 0);

        days_shown++;
    }
}

void create_emergency_contacts(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(container, 8, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(container, 8, 0);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLL_ONE);

    // Header
    lv_obj_t *header = lv_obj_create(container);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Emergency\nContacts");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_height(title, LV_SIZE_CONTENT);
    lv_obj_set_style_align(title, LV_ALIGN_CENTER, 0);

    lv_obj_t *contact1_card = lv_obj_create(container);
    lv_obj_set_width(contact1_card, LV_PCT(100));
    lv_obj_set_height(contact1_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(contact1_card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(contact1_card, 10, 0);
    lv_obj_set_style_border_width(contact1_card, 0, 0);
    lv_obj_set_style_pad_all(contact1_card, 14, 0);
    lv_obj_set_style_shadow_width(contact1_card, 4, 0);
    lv_obj_set_style_shadow_color(contact1_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(contact1_card, LV_OPA_20, 0);
    lv_obj_clear_flag(contact1_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon1 = lv_label_create(contact1_card);
    lv_label_set_text(icon1, LV_SYMBOL_CALL);
    lv_obj_set_style_text_font(icon1, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon1, lv_color_hex(0xFF5722), 0);
    lv_obj_align(icon1, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *name1 = lv_label_create(contact1_card);
    lv_label_set_text(name1, "John Doe");
    lv_obj_set_style_text_color(name1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(name1, &lv_font_montserrat_16, 0);
    lv_obj_align_to(name1, icon1, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *relation1 = lv_label_create(contact1_card);
    lv_label_set_text(relation1, "Brother");
    lv_obj_set_style_text_color(relation1, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(relation1, &lv_font_montserrat_12, 0);
    lv_obj_align_to(relation1, icon1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *phone1 = lv_label_create(contact1_card);
    lv_label_set_text(phone1, "123-456-7890");
    lv_obj_set_style_text_color(phone1, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(phone1, &lv_font_montserrat_14, 0);
    lv_obj_align_to(phone1, relation1, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    lv_obj_t *contact2_card = lv_obj_create(container);
    lv_obj_set_width(contact2_card, LV_PCT(100));
    lv_obj_set_height(contact2_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(contact2_card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(contact2_card, 10, 0);
    lv_obj_set_style_border_width(contact2_card, 0, 0);
    lv_obj_set_style_pad_all(contact2_card, 14, 0);
    lv_obj_set_style_shadow_width(contact2_card, 4, 0);
    lv_obj_set_style_shadow_color(contact2_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(contact2_card, LV_OPA_20, 0);
    lv_obj_clear_flag(contact2_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon2 = lv_label_create(contact2_card);
    lv_label_set_text(icon2, LV_SYMBOL_CALL);
    lv_obj_set_style_text_font(icon2, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon2, lv_color_hex(0x2196F3), 0);
    lv_obj_align(icon2, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *name2 = lv_label_create(contact2_card);
    lv_label_set_text(name2, "James Doe");
    lv_obj_set_style_text_color(name2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(name2, &lv_font_montserrat_16, 0);
    lv_obj_align_to(name2, icon2, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *relation2 = lv_label_create(contact2_card);
    lv_label_set_text(relation2, "Father");
    lv_obj_set_style_text_color(relation2, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(relation2, &lv_font_montserrat_12, 0);
    lv_obj_align_to(relation2, icon2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *phone2 = lv_label_create(contact2_card);
    lv_label_set_text(phone2, "123-456-7890");
    lv_obj_set_style_text_color(phone2, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(phone2, &lv_font_montserrat_14, 0);
    lv_obj_align_to(phone2, relation2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    lv_obj_t *contact3_card = lv_obj_create(container);
    lv_obj_set_width(contact3_card, LV_PCT(100));
    lv_obj_set_height(contact3_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(contact3_card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(contact3_card, 10, 0);
    lv_obj_set_style_border_width(contact3_card, 0, 0);
    lv_obj_set_style_pad_all(contact3_card, 14, 0);
    lv_obj_set_style_shadow_width(contact3_card, 4, 0);
    lv_obj_set_style_shadow_color(contact3_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(contact3_card, LV_OPA_20, 0);
    lv_obj_clear_flag(contact3_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon3 = lv_label_create(contact3_card);
    lv_label_set_text(icon3, LV_SYMBOL_CALL);
    lv_obj_set_style_text_font(icon3, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon3, lv_color_hex(0x9C27B0), 0);
    lv_obj_align(icon3, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *name3 = lv_label_create(contact3_card);
    lv_label_set_text(name3, "Jane Doe");
    lv_obj_set_style_text_color(name3, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(name3, &lv_font_montserrat_16, 0);
    lv_obj_align_to(name3, icon3, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *relation3 = lv_label_create(contact3_card);
    lv_label_set_text(relation3, "Mother");
    lv_obj_set_style_text_color(relation3, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(relation3, &lv_font_montserrat_12, 0);
    lv_obj_align_to(relation3, icon3, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *phone3 = lv_label_create(contact3_card);
    lv_label_set_text(phone3, "123-456-7890");
    lv_obj_set_style_text_color(phone3, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(phone3, &lv_font_montserrat_14, 0);
    lv_obj_align_to(phone3, relation3, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
}

void update_time_label(lv_timer_t *timer)
{
    time_t now = time(NULL);

    if (now < 946684800)
    {
        return;
    }

    struct tm timeinfo;
    if (localtime_r(&now, &timeinfo) == NULL)
    {
        return;
    }

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%I:%M %p", &timeinfo);
    lv_label_set_text(time_label, time_str);
}

void update_date_label(lv_timer_t *timer)
{
    time_t now = time(NULL);

    if (now < 946684800)
    {
        return;
    }

    struct tm timeinfo;
    if (localtime_r(&now, &timeinfo) == NULL)
    {
        return;
    }

    char date_str[32];
    strftime(date_str, sizeof(date_str), "%a • %b %d", &timeinfo);
    lv_label_set_text(date_label, date_str);
}