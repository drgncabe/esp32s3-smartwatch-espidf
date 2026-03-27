#include <stdio.h>
#include <string.h>

#include <lvgl.h>
#include "context/app_context.h"
#include "esp_log.h"
#include "network/modules/heartbeat_sensor.h"
#include "ui/components/heartbeat_radar_widget.h"
#include "ui/components/nice_button.h"
#include "ui/screens/heartbeat_sensor_page.h"

static const char *TAG = "heartbeat_sensor_page";

static lv_obj_t *start_btn = NULL;
static lv_obj_t *stop_btn = NULL;
static lv_obj_t *start_label = NULL;
static lv_obj_t *stop_label = NULL;
static lv_obj_t *radar_widget = NULL;
static lv_obj_t *tracked_label = NULL;
static lv_obj_t *flagged_label = NULL;

void heartbeat_sensor_page_callback(heartbeat_item_t *got_heartbeats, int count);

static void generate_dummy_heartbeat_data(heartbeat_item_t *items, int count)
{
    const char *dummy_ssids[] = {
        "HomeNetwork",
        "CoffeeShop_WiFi",
        "iPhone_12",
        "AndroidAP_4B3C",
        "Corp_Guest",
        "Neighbor_2.4G"};

    for (int i = 0; i < count; i++)
    {

        items[i].last_mac[0] = 0x02;
        items[i].last_mac[1] = 0x00;
        items[i].last_mac[2] = (i * 17) & 0xFF;
        items[i].last_mac[3] = (i * 31) & 0xFF;
        items[i].last_mac[4] = (i * 47) & 0xFF;
        items[i].last_mac[5] = (i * 13) & 0xFF;

        items[i].heartbeat_id = i + 1;

        items[i].frequency = 5 + (i * 3) % 50;

        snprintf(items[i].last_ssid, SSID_MAX_LEN, "%s",
                 dummy_ssids[i % 6]);

        items[i].difference_score = (i * 7) % 100;
        items[i].flagged_id_mismatch = (i % 3 == 0) ? 1 : 0;
        items[i].flagged_frequency = (i % 4 == 0) ? 1 : 0;

        if (i % 2 == 0)
        {
            memcpy(items[i].parent_mac, items[i].last_mac, 6);
            items[i].parent_mac[5] ^= 0x01;
        }
        else
        {
            memset(items[i].parent_mac, 0, 6);
        }
    }
}

void heartbeat_sensor_page_callback(heartbeat_item_t *got_heartbeats, int count)
{
    int tracked_count = count;
    int flagged_count = 0;
    if (!radar_widget)
    {
        ESP_LOGE(TAG, "Radar widget not found!");
        return;
    }

    if (!got_heartbeats || count <= 0)
    {
        ESP_LOGW(TAG, "Invalid heartbeat data: got_heartbeats=%p, count=%d", got_heartbeats, count);
        return;
    }

    heartbeat_radar_widget_update(radar_widget, got_heartbeats, count);

    for (int i = 0; i < count && i < 5; i++)
    {
        heartbeat_item_t *heartbeat = &got_heartbeats[i];
        char client_mac_str[18];
        char parent_mac_str[18];

        format_heartbeat_mac_address(heartbeat->last_mac, client_mac_str, sizeof(client_mac_str));
        format_heartbeat_mac_address(heartbeat->parent_mac, parent_mac_str, sizeof(parent_mac_str));
        if (heartbeat->flagged_id_mismatch || heartbeat->flagged_frequency)
        {
            flagged_count++;
        }
    }

    if (tracked_label)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Tracked: %d", tracked_count);
        lv_label_set_text(tracked_label, buf);
    }
    if (flagged_label)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Flagged: %d", flagged_count);
        lv_label_set_text(flagged_label, buf);
    }
}

static void start_scan_cb(lv_event_t *e)
{
    if (is_heartbeat_sensor_running)
    {
        ESP_LOGI(TAG, "Heartbeat sensor already running");
        return;
    }
    play_haptic_click();
    ESP_LOGI(TAG, "Starting heartbeat sensor...");

    heartbeat_sensor_cfg_t cfg = {
        .callback = heartbeat_sensor_page_callback,
        .scan_interval = 2000,
        .max_items = 60,
        .frequency_threshold = 100,
    };

    bool ret = heartbeat_sensor_start(cfg);
    if (ret == true)
    {
        if (stop_label)
        {
            lv_label_set_text(stop_label, LV_SYMBOL_STOP " Stop");
        }
        if (start_btn)
        {

            lv_obj_set_style_bg_color(start_btn, lv_color_hex(0xbcbcbc), 0);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start scan");
    }
}

static void stop_scan_cb(lv_event_t *e)
{
    play_haptic_click();

    if (is_heartbeat_sensor_running)
    {
        ESP_LOGI(TAG, "Stopping scan...");
        heartbeat_sensor_stop();
    }
    else
    {
        ESP_LOGI(TAG, "Clearing store...");
        heartbeat_sensor_clear_store();
        heartbeat_radar_widget_clear(radar_widget);
    }

    if (stop_label)
    {
        lv_label_set_text(stop_label, LV_SYMBOL_TRASH " Clear");
    }

    if (start_btn)
    {
        lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x00AA66), 0);
    }
}
void create_heartbeat_sensor_page(lv_obj_t *parent)
{
    // Remove default padding from parent to allow full height
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_right(parent, 0, LV_PART_SCROLLBAR);

    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container, 4, 0);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *header = lv_obj_create(container);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 2, 0);
    lv_obj_set_style_pad_left(header, 10, 0);
    lv_obj_set_style_pad_right(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_GPS " Heartbeat Sensor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *subtitle = lv_label_create(header);
    lv_label_set_text(subtitle, "Device Tracking Radar");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *stats_container = lv_obj_create(container);
    lv_obj_set_width(stats_container, lv_pct(95));
    lv_obj_set_height(stats_container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(stats_container, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(stats_container, LV_OPA_80, 0);
    lv_obj_set_style_border_width(stats_container, 1, 0);
    lv_obj_set_style_border_color(stats_container, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(stats_container, 4, 0);
    lv_obj_set_style_pad_all(stats_container, 6, 0);
    lv_obj_clear_flag(stats_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(stats_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    tracked_label = lv_label_create(stats_container);
    lv_label_set_text(tracked_label, "Tracked: 0");
    lv_obj_set_style_text_color(tracked_label, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_text_font(tracked_label, &lv_font_montserrat_14, 0);

    flagged_label = lv_label_create(stats_container);
    lv_label_set_text(flagged_label, "Flagged: 0");
    lv_obj_set_style_text_color(flagged_label, lv_color_hex(0xFF8800), 0);
    lv_obj_set_style_text_font(flagged_label, &lv_font_montserrat_14, 0);

    lv_obj_t *radar_container = lv_obj_create(container);
    lv_obj_set_width(radar_container, lv_pct(100));
    lv_obj_set_height(radar_container, 210);
    lv_obj_set_style_bg_color(radar_container, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(radar_container, LV_OPA_90, 0);
    lv_obj_set_style_border_width(radar_container, 2, 0);
    lv_obj_set_style_border_color(radar_container, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_radius(radar_container, 8, 0);
    lv_obj_set_style_pad_all(radar_container, 5, 0);
    lv_obj_clear_flag(radar_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(radar_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(radar_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    radar_widget = heartbeat_radar_widget_create(radar_container);
    lv_obj_set_size(radar_widget, 200, 200);
    lv_obj_center(radar_widget);

    lv_obj_t *info_label = lv_label_create(container);
    lv_label_set_text(info_label,
                      "Position indicates signal strength\n"
                      "Color shows tracking status\n"
                      "Tap items for details");
    lv_obj_set_width(info_label, lv_pct(95));
    lv_obj_set_style_text_color(info_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_left(info_label, 10, 0);
    lv_obj_set_style_pad_right(info_label, 10, 0);

    lv_obj_t *button_container = lv_obj_create(container);
    lv_obj_set_width(button_container, lv_pct(100));
    lv_obj_set_height(button_container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(button_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button_container, 0, 0);
    lv_obj_set_style_pad_all(button_container, 2, 0);
    lv_obj_clear_flag(button_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(button_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    start_btn = lv_btn_create(button_container);
    lv_obj_set_size(start_btn, 100, 40);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x00AA66), 0);
    start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, LV_SYMBOL_PLAY " Start");
    lv_obj_center(start_label);
    lv_obj_add_event_cb(start_btn, start_scan_cb, LV_EVENT_CLICKED, NULL);

    stop_btn = lv_btn_create(button_container);
    lv_obj_set_size(stop_btn, 100, 40);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xAA3300), 0);
    stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_TRASH " Clear");
    lv_obj_center(stop_label);
    lv_obj_add_event_cb(stop_btn, stop_scan_cb, LV_EVENT_CLICKED, NULL);
}