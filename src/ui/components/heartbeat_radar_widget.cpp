#include <math.h>
#include <stdio.h>
#include <string.h>

#include "context/app_context.h"
#include "esp_log.h"
#include "ui/components/heartbeat_radar_widget.h"

// False = use frequency to determine point placement
// True = use rssi distance to determine point placement
bool use_heartbeat_distance = true;

static const char *TAG = "heartbeat_radar";

static lv_obj_t *item_detail_dialog = NULL;

typedef struct
{
    lv_obj_t *canvas;
    lv_obj_t *item_container;
    heartbeat_item_t *items;
    int item_count;
    int radius;
} heartbeat_radar_data_t;

static void close_item_dialog(lv_event_t *e)
{
    if (item_detail_dialog)
    {
        lv_obj_del(item_detail_dialog);
        item_detail_dialog = NULL;
    }
}

static void ignore_item_cb(lv_event_t *e)
{
    play_haptic_click();
}

static void show_item_dialog(heartbeat_item_t *item)
{

    if (item_detail_dialog)
    {
        lv_obj_del(item_detail_dialog);
    }

    item_detail_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(item_detail_dialog, 220, 190);
    lv_obj_center(item_detail_dialog);
    lv_obj_set_style_bg_color(item_detail_dialog, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(item_detail_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(item_detail_dialog, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_border_width(item_detail_dialog, 2, 0);
    lv_obj_set_style_radius(item_detail_dialog, 8, 0);
    lv_obj_set_style_pad_all(item_detail_dialog, 10, 0);

    lv_obj_t *title = lv_label_create(item_detail_dialog);
    lv_label_set_text(title, "Device Info");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    char distance_str[32];
    snprintf(distance_str, sizeof(distance_str), "Distance: %.2f ft", item->est_distance);
    lv_obj_t *distance_label = lv_label_create(item_detail_dialog);
    lv_label_set_text(distance_label, distance_str);
    lv_obj_set_style_text_font(distance_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(distance_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(distance_label, LV_ALIGN_TOP_LEFT, 0, 25);

    char mac_str[32];
    snprintf(mac_str, sizeof(mac_str), "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             item->last_mac[0], item->last_mac[1], item->last_mac[2],
             item->last_mac[3], item->last_mac[4], item->last_mac[5]);
    lv_obj_t *mac_label = lv_label_create(item_detail_dialog);
    lv_label_set_text(mac_label, mac_str);
    lv_obj_set_style_text_font(mac_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mac_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(mac_label, LV_ALIGN_TOP_LEFT, 0, 45);

    char ssid_str[48];
    snprintf(ssid_str, sizeof(ssid_str), "SSID: %s", item->last_ssid[0] ? item->last_ssid : "N/A");
    lv_obj_t *ssid_label = lv_label_create(item_detail_dialog);
    lv_label_set_text(ssid_label, ssid_str);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 0, 65);

    char freq_str[32];
    snprintf(freq_str, sizeof(freq_str), "Seen: %d times", item->frequency);
    lv_obj_t *freq_label = lv_label_create(item_detail_dialog);
    lv_label_set_text(freq_label, freq_str);
    lv_obj_set_style_text_font(freq_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(freq_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(freq_label, LV_ALIGN_TOP_LEFT, 0, 85);

    const char *status = "Normal";
    lv_color_t status_color = lv_color_hex(0x00DDFF);
    if (item->flagged_id_mismatch || item->flagged_frequency)
    {
        status = "FLAGGED";
        status_color = lv_color_hex(0xFF4400);
    }
    char status_str[32];
    snprintf(status_str, sizeof(status_str), "Status: %s", status);
    lv_obj_t *status_label = lv_label_create(item_detail_dialog);
    lv_label_set_text(status_label, status_str);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label, status_color, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 0, 105);

    lv_obj_t *close_btn = lv_btn_create(item_detail_dialog);
    lv_obj_set_size(close_btn, 80, 30);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x444444), 0);
    lv_obj_add_event_cb(close_btn, close_item_dialog, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);

    lv_obj_t *ignore_btn = lv_btn_create(item_detail_dialog);
    lv_obj_set_size(ignore_btn, 80, 30);
    lv_obj_align(ignore_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(ignore_btn, lv_color_hex(0xFF4400), 0);
    lv_obj_add_event_cb(ignore_btn, ignore_item_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ignore_label = lv_label_create(ignore_btn);
    lv_label_set_text(ignore_label, "Ignore");
    lv_obj_center(ignore_label);
}

static void marker_clicked(lv_event_t *e)
{
    heartbeat_item_t *item = (heartbeat_item_t *)lv_event_get_user_data(e);
    if (item)
    {
        show_item_dialog(item);
    }
}

static void heartbeat_radar_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_DELETE)
    {

        heartbeat_radar_data_t *data = (heartbeat_radar_data_t *)lv_obj_get_user_data(obj);
        if (data)
        {
            if (data->items)
            {
                free(data->items);
            }
            free(data);
        }

        if (item_detail_dialog)
        {
            lv_obj_del(item_detail_dialog);
            item_detail_dialog = NULL;
        }
    }
}

__attribute__((unused)) static void draw_radar_background(lv_obj_t *canvas, int radius)
{
    // TODO: Draw concentric circles for the radar
    // TODO: Draw grid lines/spokes radiating from center
    // TODO: Add distance markers

    ESP_LOGI(TAG, "Drawing radar background with radius %d (stub)", radius);
}

__attribute__((unused)) static void calculate_item_position(heartbeat_item_t *item, int radius, int *x, int *y)
{
    // TODO: Use RSSI to determine distance from center
    // TODO: Use other heartbeat data (frequency, difference_score) for angle
    // TODO: Map to x,y coordinates

    *x = radius;
    *y = radius;

    ESP_LOGD(TAG, "Calculating position for item (stub)");
}

__attribute__((unused)) static lv_obj_t *create_item_marker(lv_obj_t *parent, heartbeat_item_t *item, int x, int y)
{

    // TODO: Set color based on item properties (flagged, frequency, etc.)
    // TODO: Add label with MAC address or other info
    // TODO: Make it clickable for details

    ESP_LOGD(TAG, "Creating item marker at (%d, %d) (stub)", x, y);

    lv_obj_t *marker = lv_obj_create(parent);
    lv_obj_set_size(marker, 10, 10);
    lv_obj_set_pos(marker, x, y);

    return marker;
}

lv_obj_t *heartbeat_radar_widget_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating heartbeat radar widget");

    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, 200, 200);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    heartbeat_radar_data_t *data = (heartbeat_radar_data_t *)malloc(sizeof(heartbeat_radar_data_t));
    if (!data)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for radar data");
        lv_obj_del(container);
        return NULL;
    }

    memset(data, 0, sizeof(heartbeat_radar_data_t));
    data->radius = 90;

    int center_x = 100;
    int center_y = 100;

    for (int i = 1; i <= 3; i++)
    {
        lv_obj_t *circle = lv_obj_create(container);
        int radius = (data->radius / 3) * i;
        lv_obj_set_size(circle, radius * 2, radius * 2);
        lv_obj_set_pos(circle, center_x - radius, center_y - radius);
        lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(circle, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_border_width(circle, 1, 0);
        lv_obj_set_style_border_opa(circle, i == 3 ? LV_OPA_80 : LV_OPA_40, 0);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *h_line = lv_obj_create(container);
    lv_obj_set_size(h_line, data->radius * 2, 1);
    lv_obj_set_pos(h_line, center_x - data->radius, center_y);
    lv_obj_set_style_bg_color(h_line, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_bg_opa(h_line, LV_OPA_30, 0);
    lv_obj_set_style_border_width(h_line, 0, 0);
    lv_obj_clear_flag(h_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(h_line, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *v_line = lv_obj_create(container);
    lv_obj_set_size(v_line, 1, data->radius * 2);
    lv_obj_set_pos(v_line, center_x, center_y - data->radius);
    lv_obj_set_style_bg_color(v_line, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_bg_opa(v_line, LV_OPA_30, 0);
    lv_obj_set_style_border_width(v_line, 0, 0);
    lv_obj_clear_flag(v_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(v_line, LV_OBJ_FLAG_CLICKABLE);

    data->item_container = lv_obj_create(container);
    lv_obj_set_size(data->item_container, 200, 200);
    lv_obj_set_pos(data->item_container, 0, 0);
    lv_obj_set_style_bg_opa(data->item_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(data->item_container, 0, 0);
    lv_obj_set_style_pad_all(data->item_container, 0, 0);
    lv_obj_clear_flag(data->item_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(data->item_container, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_user_data(container, data);

    lv_obj_add_event_cb(container, heartbeat_radar_event_cb, LV_EVENT_DELETE, NULL);

    ESP_LOGI(TAG, "Radar widget created with background");

    return container;
}

void heartbeat_radar_widget_update(lv_obj_t *radar_widget, heartbeat_item_t *items, int count)
{
    if (!radar_widget || !items || count <= 0)
    {
        ESP_LOGW(TAG, "Invalid parameters for radar update");
        return;
    }

    heartbeat_radar_data_t *data = (heartbeat_radar_data_t *)lv_obj_get_user_data(radar_widget);
    if (!data || !data->item_container)
    {
        ESP_LOGE(TAG, "Radar widget has no user data or item container");
        return;
    }

    ESP_LOGI(TAG, "Updating radar with %d items", count);

    lv_obj_clean(data->item_container);

    if (data->items)
    {
        free(data->items);
    }
    data->items = (heartbeat_item_t *)malloc(sizeof(heartbeat_item_t) * count);
    if (!data->items)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for items");
        return;
    }
    memcpy(data->items, items, sizeof(heartbeat_item_t) * count);
    data->item_count = count;

    int center_x = 100;
    int center_y = 100;

    for (int i = 0; i < count && i < 20; i++)
    {
        heartbeat_item_t *item = &data->items[i];

        int max_freq = 50;
        int distance = 0;

        if (use_heartbeat_distance)
        {
            distance = item->est_distance;
        }
        else
        {
            distance = data->radius - ((item->frequency * data->radius) / (max_freq + 20));
            if (distance < 10)
                distance = 10;
            if (distance > data->radius)
                distance = data->radius;
        }

        uint32_t mac_hash = (item->last_mac[0] << 16) | (item->last_mac[1] << 8) | item->last_mac[2];
        float angle = (mac_hash % 360) * 3.14159f / 180.0f;

        int x = center_x + (int)(distance * cosf(angle));
        int y = center_y + (int)(distance * sinf(angle));

        lv_obj_t *marker = lv_obj_create(data->item_container);
        lv_obj_set_size(marker, 16, 16);
        lv_obj_set_pos(marker, x - 8, y - 8);
        lv_obj_set_style_bg_opa(marker, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(marker, 0, 0);
        lv_obj_set_style_pad_all(marker, 0, 0);
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(marker, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_add_event_cb(marker, marker_clicked, LV_EVENT_CLICKED, item);

        lv_obj_t *dot = lv_obj_create(marker);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_center(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        if (item->flagged_id_mismatch || item->flagged_frequency)
        {

            lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF4400), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

            lv_obj_t *ring = lv_obj_create(marker);
            lv_obj_set_size(ring, 14, 14);
            lv_obj_center(ring);
            lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(ring, lv_color_hex(0xFF4400), 0);
            lv_obj_set_style_border_width(ring, 1, 0);
            lv_obj_set_style_border_opa(ring, LV_OPA_60, 0);
            lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_move_background(ring);
        }
        else
        {

            lv_obj_set_style_bg_color(dot, lv_color_hex(0x00DDFF), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_80, 0);
        }
    }

    ESP_LOGI(TAG, "Radar updated with %d markers", count > 20 ? 20 : count);
}

void heartbeat_radar_widget_clear(lv_obj_t *radar_widget)
{
    if (!radar_widget)
    {
        return;
    }

    heartbeat_radar_data_t *data = (heartbeat_radar_data_t *)lv_obj_get_user_data(radar_widget);
    if (!data)
    {
        return;
    }

    ESP_LOGI(TAG, "Clearing radar widget (stub)");

    lv_obj_clean(data->item_container);
    // TODO: Remove all item markers
    // TODO: Free items array
    // TODO: Redraw empty radar
}

void heartbeat_radar_widget_set_radius(lv_obj_t *radar_widget, int radius)
{
    if (!radar_widget || radius <= 0)
    {
        ESP_LOGW(TAG, "Invalid radius: %d", radius);
        return;
    }

    heartbeat_radar_data_t *data = (heartbeat_radar_data_t *)lv_obj_get_user_data(radar_widget);
    if (!data)
    {
        return;
    }

    ESP_LOGI(TAG, "Setting radar radius to %d (stub)", radius);

    data->radius = radius;

    // TODO: Resize canvas
    // TODO: Redraw background with new radius
    // TODO: Recalculate all item positions
}

void heartbeat_radar_widget_delete(lv_obj_t *radar_widget)
{
    if (!radar_widget)
    {
        return;
    }

    ESP_LOGI(TAG, "Deleting radar widget");

    // Cleanup will be handled by the LV_EVENT_DELETE callback
    lv_obj_del(radar_widget);
}
