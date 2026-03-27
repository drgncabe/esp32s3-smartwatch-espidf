#include <string.h>

#include <lvgl.h>
#include "api/prefs.h"
#include "context/app_context.h"
#include "context/app_settings.h"
#include "core/network/wifi_init.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network/modules/wifi_scanner.h"
#include "ui/dialogs/scan_dialog.h"
#include "ui/screens/home_page.h"

static const char *TAG = "scan_dialog";

static lv_timer_t *wifi_scan_timer = NULL;
static lv_obj_t *wifi_dialog = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *wifi_list_press_label = NULL;
static lv_obj_t *kb = NULL;

static lv_obj_t *wifi_pre_dialog = NULL;

static lv_obj_t *wifi_loading_container = NULL;
static lv_obj_t *wifi_spinner = NULL;
static lv_obj_t *wifi_loading_label = NULL;

static lv_obj_t *pwd_dialog = NULL;
static lv_obj_t *pwd_ta = NULL;
static lv_obj_t *pwd_error_label = NULL;
static char selected_ssid[33] = "";

// Forward declarations
static void keyboard_event_cb(lv_event_t *e);

static void open_keyboard(lv_obj_t *ta)
{
    if (!kb)
    {
        kb = lv_keyboard_create(lv_scr_act());
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_set_size(kb, LV_PCT(100), LV_PCT(38));
        lv_obj_set_style_pad_all(kb, 4, LV_PART_ITEMS);
        lv_obj_set_style_text_font(kb, &lv_font_montserrat_12, LV_PART_ITEMS);
        lv_obj_set_style_pad_row(kb, 4, LV_PART_ITEMS);
        lv_obj_set_style_pad_column(kb, 4, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(kb, lv_color_hex(0x3A3A3A), (lv_style_selector_t)(LV_PART_ITEMS | LV_STATE_PRESSED));
        lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, (lv_style_selector_t)(LV_PART_ITEMS | LV_STATE_PRESSED));
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_ALL, NULL);
    }
    else
    {
        // Keyboard exists, just show it
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

    lv_keyboard_set_textarea(kb, ta);
}

static void close_keyboard(void)
{
    if (kb)
    {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void pwd_ta_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED)
    {
        if (!kb || lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN))
        {
            open_keyboard(ta);
        }
    }
    else if (code == LV_EVENT_DEFOCUSED)
    {
        // Don't close keyboard on defocus - let user tap outside
    }
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *keyboard = lv_event_get_target(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
    {
        close_keyboard();
    }
}

void wifi_ssid_tapped(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    // Only trigger on long press
    if (code != LV_EVENT_LONG_PRESSED)
    {
        return;
    }

    play_haptic_click();

    lv_obj_t *btn = lv_event_get_target(e);
    WifiEntry *entry = (WifiEntry *)lv_obj_get_user_data(btn);

    if (!entry)
        return;

    ESP_LOGI(TAG, "WiFi long-pressed: %s (open: %d)", entry->ssid, entry->open);

    if (entry->open)
    {
        connect_to_wifi(entry->ssid, "");
    }
    else
    {
        network_pref_data_t netdata = get_saved_network_prefs(entry->ssid);
        if (netdata.password && netdata.password[0] != '\0')
        {
            open_wifi_password_dialog(entry->ssid, netdata.password);
        }
        else
        {
            open_wifi_password_dialog(entry->ssid, NULL);
        }
        // Free the dynamically allocated strings
        free(const_cast<char *>(netdata.ssid));
        free(const_cast<char *>(netdata.password));
    }
}

void update_wifi_scan_results(lv_timer_t *t)
{
    if (!wifi_scan_complete() || !wifi_list)
        return;

    int n = get_wifi_scan_count();
    if (n < 0)
        return;

    // Hide loading state
    if (wifi_loading_container)
    {
        lv_obj_add_flag(wifi_loading_container, LV_OBJ_FLAG_HIDDEN);
    }

    // Show list
    lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);

    lv_obj_clean(wifi_list);

    for (int i = 0; i < n; i++)
    {
        const char *ssid = get_wifi_ssid(i);
        int rssi = get_wifi_rssi(i);

        // Skip entries with empty or invalid SSIDs
        if (!ssid || ssid[0] == '\0')
        {
            continue;
        }

        WifiEntry *entry = (WifiEntry *)malloc(sizeof(WifiEntry));
        if (!entry)
            continue;

        strncpy(entry->ssid, ssid, sizeof(entry->ssid));
        entry->ssid[32] = '\0';
        entry->open = wifi_is_open(i);

        // Create custom card for each network
        lv_obj_t *network_card = lv_obj_create(wifi_list);
        lv_obj_set_width(network_card, LV_PCT(100));
        lv_obj_set_height(network_card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(network_card, lv_color_hex(0x1E1E1E), 0);
        lv_obj_set_style_radius(network_card, 8, 0);
        lv_obj_set_style_border_width(network_card, 0, 0);
        lv_obj_set_style_pad_all(network_card, 12, 0);
        lv_obj_set_style_shadow_width(network_card, 4, 0);
        lv_obj_set_style_shadow_color(network_card, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(network_card, LV_OPA_20, 0);
        lv_obj_add_flag(network_card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(network_card, GLOBAL_EXT_CLICK_AREA);
        lv_obj_clear_flag(network_card, LV_OBJ_FLAG_SCROLLABLE);

        // Create flex container for icon and text
        lv_obj_set_flex_flow(network_card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(network_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // WiFi icon with color based on signal strength
        lv_obj_t *wifi_icon = lv_label_create(network_card);
        lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_20, 0);

        // Color based on signal strength
        lv_color_t signal_color;
        if (rssi >= -50)
        {
            signal_color = lv_color_hex(0x4CAF50); // Green - Excellent
        }
        else if (rssi >= -60)
        {
            signal_color = lv_color_hex(0x8BC34A); // Light green - Good
        }
        else if (rssi >= -70)
        {
            signal_color = lv_color_hex(0xFFC107); // Yellow - Fair
        }
        else
        {
            signal_color = lv_color_hex(0xFF5722); // Orange/Red - Weak
        }
        lv_obj_set_style_text_color(wifi_icon, signal_color, 0);

        // Content container (SSID + details)
        lv_obj_t *text_container = lv_obj_create(network_card);
        lv_obj_set_flex_grow(text_container, 1);
        lv_obj_set_height(text_container, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(text_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(text_container, 0, 0);
        lv_obj_set_style_pad_all(text_container, 0, 0);
        lv_obj_set_style_pad_left(text_container, 10, 0);
        lv_obj_set_flex_flow(text_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(text_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(text_container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(text_container, LV_OBJ_FLAG_SCROLLABLE);

        // SSID label
        lv_obj_t *ssid_label = lv_label_create(text_container);
        lv_label_set_text(ssid_label, entry->ssid);
        lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssid_label, LV_PCT(90));

        // Details row (RSSI + Auth)
        lv_obj_t *details_row = lv_obj_create(text_container);
        lv_obj_set_width(details_row, LV_SIZE_CONTENT);
        lv_obj_set_height(details_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(details_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(details_row, 0, 0);
        lv_obj_set_style_pad_all(details_row, 0, 0);
        lv_obj_set_style_pad_top(details_row, 4, 0);
        lv_obj_set_flex_flow(details_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(details_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(details_row, 8, 0);
        lv_obj_clear_flag(details_row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(details_row, LV_OBJ_FLAG_SCROLLABLE);

        // RSSI badge
        char rssi_str[16];
        snprintf(rssi_str, sizeof(rssi_str), "%d dBm", rssi);
        lv_obj_t *rssi_badge = lv_label_create(details_row);
        lv_label_set_text(rssi_badge, rssi_str);
        lv_obj_set_style_text_font(rssi_badge, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(rssi_badge, lv_color_hex(0xAAAAAA), 0);

        // Security badge
        lv_obj_t *security_badge = lv_label_create(details_row);
        if (entry->open)
        {
            lv_label_set_text(security_badge, LV_SYMBOL_WIFI " Open");
            lv_obj_set_style_text_color(security_badge, lv_color_hex(0x4CAF50), 0);
        }
        else
        {
            lv_label_set_text(security_badge, LV_SYMBOL_WARNING " Secured");
            lv_obj_set_style_text_color(security_badge, lv_color_hex(0xFF9800), 0);
        }
        lv_obj_set_style_text_font(security_badge, &lv_font_montserrat_12, 0);

        // Store entry data and add event
        lv_obj_set_user_data(network_card, entry);
        lv_obj_add_event_cb(network_card, wifi_ssid_tapped, LV_EVENT_LONG_PRESSED, NULL);
    }

    if (wifi_list_press_label)
    {
        lv_label_set_text(wifi_list_press_label, "Long press to connect");
        lv_obj_clear_flag(wifi_list_press_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Clean up scan results
    cleanup_wifi_scan();

    lv_timer_del(t);
    wifi_scan_timer = NULL;
}

void close_wifi_dialog(void)
{
    if (!wifi_dialog)
        return;

    close_keyboard();

    // Clean up password dialog if it exists
    if (pwd_dialog)
    {
        lv_obj_del(pwd_dialog);
        pwd_dialog = NULL;
        pwd_ta = NULL;
        pwd_error_label = NULL;
    }

    // Clean up scan list entries
    if (wifi_list)
    {
        uint32_t cnt = lv_obj_get_child_cnt(wifi_list);
        for (uint32_t i = 0; i < cnt; i++)
        {
            lv_obj_t *card = lv_obj_get_child(wifi_list, i);
            WifiEntry *entry = (WifiEntry *)lv_obj_get_user_data(card);
            if (entry)
            {
                free(entry);
            }
        }
    }

    // Clean up scan timer
    if (wifi_scan_timer)
    {
        lv_timer_del(wifi_scan_timer);
        wifi_scan_timer = NULL;
    }

    // Clean up main dialog
    if (wifi_dialog)
    {
        lv_obj_del(wifi_dialog);
        wifi_dialog = NULL;
        wifi_list = NULL;
    }

    wifi_loading_container = NULL;
    wifi_spinner = NULL;
    wifi_loading_label = NULL;

    enable_wifi(NULL, NULL);
}

static void async_close_wifi_dialog(void *param)
{
    close_wifi_dialog();
}

void close_wifi_dialog_event(lv_event_t *e)
{
    close_wifi_dialog();
}

void open_wifi_scan_dialog(void)
{
    if (wifi_dialog)
        return;

    ESP_LOGI(TAG, "WiFi scan dialog opened, disabling WiFi");
    disable_wifi();
    vTaskDelay(pdMS_TO_TICKS(200));

    /* OVERLAY BACKDROP */
    wifi_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(wifi_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(wifi_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_dialog, 0, 0);
    lv_obj_set_style_pad_all(wifi_dialog, 0, 0);
    lv_obj_clear_flag(wifi_dialog, LV_OBJ_FLAG_SCROLLABLE);

    /* MAIN CONTENT COLUMN */
    lv_obj_t *content = lv_obj_create(wifi_dialog);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(content, 10, 0);

    /* HEADER */
    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_bottom(header, 4, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Networks");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    /* Close button */
    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 32, 32);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, close_wifi_dialog_event, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(close_btn, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *x = lv_label_create(close_btn);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(x, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(x);

    /* NETWORK LIST CONTAINER */
    lv_obj_t *list_container = lv_obj_create(content);
    lv_obj_set_width(list_container, LV_PCT(100));
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_radius(list_container, 12, 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_pad_all(list_container, 8, 0);
    lv_obj_set_style_shadow_width(list_container, 8, 0);
    lv_obj_set_style_shadow_color(list_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(list_container, LV_OPA_30, 0);
    lv_obj_clear_flag(list_container, LV_OBJ_FLAG_SCROLLABLE);

    /* LOADING STATE */
    wifi_loading_container = lv_obj_create(list_container);
    lv_obj_set_size(wifi_loading_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(wifi_loading_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_loading_container, 0, 0);
    lv_obj_set_flex_flow(wifi_loading_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_loading_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(wifi_loading_container, LV_OBJ_FLAG_SCROLLABLE);

    wifi_spinner = lv_spinner_create(wifi_loading_container, 1000, 60);
    lv_obj_set_size(wifi_spinner, 32, 32);
    lv_obj_set_style_arc_color(wifi_spinner, lv_color_hex(0x2196F3), LV_PART_INDICATOR);

    wifi_loading_label = lv_label_create(wifi_loading_container);
    lv_label_set_text(wifi_loading_label, "Scanning for networks...");
    lv_obj_set_style_text_color(wifi_loading_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(wifi_loading_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(wifi_loading_label, 10, 0);

    /* WIFI LIST (HIDDEN INITIALLY) */
    wifi_list = lv_obj_create(list_container);
    lv_obj_set_size(wifi_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(wifi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_style_pad_all(wifi_list, 0, 0);
    lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(wifi_list, 8, 0);
    lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);

    /* INSTRUCTION LABEL */
    wifi_list_press_label = lv_label_create(content);
    lv_label_set_text(wifi_list_press_label, "Long press to connect");
    lv_obj_set_style_text_color(wifi_list_press_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(wifi_list_press_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(wifi_list_press_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(wifi_list_press_label, LV_PCT(100));
    lv_obj_add_flag(wifi_list_press_label, LV_OBJ_FLAG_HIDDEN);

    start_wifi_scan();
    wifi_scan_timer = lv_timer_create(update_wifi_scan_results, 400, NULL);
}

static void async_close_wifi_pre_dialog(void *param)
{
    if (wifi_pre_dialog)
    {
        lv_obj_del(wifi_pre_dialog);
    }
    wifi_pre_dialog = NULL;
}

void open_wifi_scan_dialog_event(lv_event_t *e)
{
    open_wifi_scan_dialog();
    lv_async_call(async_close_wifi_pre_dialog, NULL);
}

const char *authModeToStr(wifi_auth_mode_t a)
{
    switch (a)
    {
    case WIFI_AUTH_OPEN:
        return "Open";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    default:
        return "Unknown";
    }
}

static char current_ssid_buf[33];

static void close_wifi_pre_dialog_event(lv_event_t *e)
{
    if (wifi_pre_dialog)
    {
        lv_obj_del(wifi_pre_dialog);
    }
    wifi_pre_dialog = NULL;
}

void open_wifi_scan_pre_dialog(void)
{
    if (wifi_pre_dialog)
        return;

    current_ssid_buf[0] = '\0';

    // Get current WiFi info using ESP-IDF
    wifi_ap_record_t ap;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);

    static char ssid_text[64];
    static char rssi_text[16];
    static char auth_text[32];

    if (err == ESP_OK)
    {
        // Connected
        strncpy(current_ssid_buf, (char *)ap.ssid, sizeof(current_ssid_buf));
        current_ssid_buf[sizeof(current_ssid_buf) - 1] = '\0';

        snprintf(ssid_text, sizeof(ssid_text), "SSID: %s", current_ssid_buf);
        snprintf(rssi_text, sizeof(rssi_text), "RSSI: %d dBm", ap.rssi);

        bool isProtected = (ap.authmode != WIFI_AUTH_OPEN);
        if (isProtected)
        {
            snprintf(auth_text, sizeof(auth_text), "Auth: %s", authModeToStr(ap.authmode));
        }
        else
        {
            snprintf(auth_text, sizeof(auth_text), "Open");
        }
    }
    else
    {
        snprintf(ssid_text, sizeof(ssid_text), "Not connected");
        rssi_text[0] = '\0';
        auth_text[0] = '\0';
    }

    /* OVERLAY BACKDROP */
    wifi_pre_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(wifi_pre_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_pre_dialog, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(wifi_pre_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_pre_dialog, 0, 0);
    lv_obj_set_style_pad_all(wifi_pre_dialog, 8, 0);
    lv_obj_clear_flag(wifi_pre_dialog, LV_OBJ_FLAG_SCROLLABLE);

    /* MAIN CONTENT COLUMN */
    lv_obj_t *content = lv_obj_create(wifi_pre_dialog);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(content, 8, 0);

    /* HEADER */
    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 40);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_top(header, 4, 0);
    lv_obj_set_style_pad_bottom(header, 4, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Title wrapper */
    lv_obj_t *title_wrap = lv_obj_create(header);
    lv_obj_set_flex_grow(title_wrap, 1);
    lv_obj_set_style_bg_opa(title_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_wrap, 0, 0);
    lv_obj_set_style_pad_left(title_wrap, 2, 0);

    lv_obj_t *title = lv_label_create(title_wrap);
    lv_label_set_text(title, "Network");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    /* Close button */
    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 24, 24);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_shadow_width(close_btn, 8, 0);
    lv_obj_set_style_shadow_opa(close_btn, LV_OPA_20, 0);
    lv_obj_add_event_cb(close_btn, close_wifi_pre_dialog_event, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(close_btn, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *x = lv_label_create(close_btn);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_center(x);

    /* NETWORK STATUS CARD */
    lv_obj_t *status_card = lv_obj_create(content);
    lv_obj_set_width(status_card, LV_PCT(100));
    lv_obj_set_height(status_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(0x1B1B1F), 0);
    lv_obj_set_style_radius(status_card, 12, 0);
    lv_obj_set_style_pad_all(status_card, 12, 0);
    lv_obj_set_style_pad_top(status_card, 15, 0);
    lv_obj_set_style_pad_bottom(status_card, 15, 0);
    lv_obj_set_style_shadow_width(status_card, 8, 0);
    lv_obj_set_style_shadow_opa(status_card, LV_OPA_20, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ssid_label = lv_label_create(status_card);
    lv_label_set_text(ssid_label, ssid_text);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(ssid_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_MID, 0, 0);

    if (rssi_text[0] != '\0' && auth_text[0] != '\0')
    {
        lv_obj_t *rssi_label = lv_label_create(status_card);
        lv_label_set_text(rssi_label, rssi_text);
        lv_obj_set_style_text_font(rssi_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rssi_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(rssi_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align_to(rssi_label, ssid_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

        lv_obj_t *auth_label = lv_label_create(status_card);
        lv_label_set_text(auth_label, auth_text);
        lv_obj_set_style_text_font(auth_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(auth_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(auth_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align_to(auth_label, rssi_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }

    /* Connect button */
    lv_obj_t *btn = lv_btn_create(content);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_align_to(btn, status_card, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_add_event_cb(btn, open_wifi_scan_dialog_event, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(btn, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Change Network");
    lv_obj_center(lbl);
}

static void back_to_scan_list(lv_event_t *e)
{
    play_haptic_click();

    close_keyboard();

    if (pwd_dialog)
    {
        if(kb){
            lv_obj_del(kb);
            kb = NULL;
        }
        lv_obj_del(pwd_dialog);
        pwd_dialog = NULL;
        pwd_ta = NULL;
        pwd_error_label = NULL;
    }

    // Show the scan dialog again if it was hidden
    if (wifi_dialog)
    {
        lv_obj_clear_flag(wifi_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

static void pwd_dialog_bg_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);

    if (code == LV_EVENT_CLICKED)
    {
        // Only close keyboard if clicking on the content background, not child elements
        if (target && kb && !lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN))
        {
            close_keyboard();
        }
    }
}

void open_wifi_password_dialog(const char *ssid, const char *password)
{
    strncpy(selected_ssid, ssid, sizeof(selected_ssid));
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';

    // Hide the scan dialog
    if (wifi_dialog)
    {
        lv_obj_add_flag(wifi_dialog, LV_OBJ_FLAG_HIDDEN);
    }

    /* FULL PAGE OVERLAY */
    pwd_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(pwd_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(pwd_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(pwd_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pwd_dialog, 0, 0);
    lv_obj_set_style_pad_all(pwd_dialog, 0, 0);
    lv_obj_clear_flag(pwd_dialog, LV_OBJ_FLAG_SCROLLABLE);

    /* MAIN CONTENT */
    lv_obj_t *content = lv_obj_create(pwd_dialog);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100)); // Full height, keyboard appears on top
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(content, pwd_dialog_bg_clicked, LV_EVENT_CLICKED, NULL);

    /* HEADER */
    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_bottom(header, 6, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);

    /* Back button */
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 32, 32);
    lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_add_event_cb(back_btn, back_to_scan_list, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(back_btn, GLOBAL_EXT_CLICK_AREA);

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Connect");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    /* PASSWORD INPUT CARD - with SSID as label */
    lv_obj_t *input_card = lv_obj_create(content);
    lv_obj_set_width(input_card, LV_PCT(100));
    lv_obj_set_height(input_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(input_card, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_radius(input_card, 10, 0);
    lv_obj_set_style_border_width(input_card, 0, 0);
    lv_obj_set_style_pad_all(input_card, 14, 0);
    lv_obj_set_style_shadow_width(input_card, 6, 0);
    lv_obj_set_style_shadow_color(input_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(input_card, LV_OPA_30, 0);
    lv_obj_clear_flag(input_card, LV_OBJ_FLAG_SCROLLABLE);

    /* Label showing SSID */
    lv_obj_t *pwd_label = lv_label_create(input_card);
    lv_label_set_text(pwd_label, ssid);
    lv_obj_set_style_text_font(pwd_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pwd_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_long_mode(pwd_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(pwd_label, LV_PCT(95));

    /* Password field */
    pwd_ta = lv_textarea_create(input_card);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_textarea_set_placeholder_text(pwd_ta, "Enter password");
    lv_obj_set_width(pwd_ta, LV_PCT(100));
    lv_obj_set_style_bg_color(pwd_ta, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_color(pwd_ta, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(pwd_ta, 2, 0);
    lv_obj_set_style_radius(pwd_ta, 8, 0);
    lv_obj_set_style_pad_all(pwd_ta, 10, 0);
    lv_obj_set_style_text_color(pwd_ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(pwd_ta, pwd_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_add_flag(pwd_ta, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pwd_ta, pwd_ta_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(pwd_ta, pwd_ta_event, LV_EVENT_CLICKED, NULL);

    if (password != NULL && password[0] != '\0')
    {
        lv_textarea_set_text(pwd_ta, password);
    }

    /* Error label (hidden by default) */
    pwd_error_label = lv_label_create(input_card);
    lv_label_set_text(pwd_error_label, LV_SYMBOL_WARNING " Invalid password");
    lv_obj_set_style_text_color(pwd_error_label, lv_color_hex(0xFF5555), 0);
    lv_obj_set_style_text_font(pwd_error_label, &lv_font_montserrat_10, 0);
    lv_obj_add_flag(pwd_error_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(pwd_error_label, pwd_ta, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    /* CONNECT BUTTON */
    lv_obj_t *connect_btn = lv_btn_create(content);
    lv_obj_set_width(connect_btn, LV_PCT(100));
    lv_obj_set_height(connect_btn, 44);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x1976D2), LV_STATE_PRESSED);
    lv_obj_set_style_radius(connect_btn, 10, 0);
    lv_obj_set_style_shadow_width(connect_btn, 6, 0);
    lv_obj_set_style_shadow_color(connect_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_shadow_opa(connect_btn, LV_OPA_30, 0);
    lv_obj_add_event_cb(connect_btn, wifi_password_submit, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(connect_btn, GLOBAL_EXT_CLICK_AREA);
    // lv_obj_set_style_pad_top(connect_btn, 10, 0);
    lv_obj_set_style_translate_y(connect_btn, 15, 0);

    lv_obj_t *btn_label = lv_label_create(connect_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_OK " Connect");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);
}

static char pending_password_buf[33];
static char pending_ssid_buf[33];

void wifi_password_submit(lv_event_t *e)
{
    if(kb){
        if(!lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN)){
            return;
        }
    }
    play_haptic_click();

    pending_password_buf[0] = '\0';
    const char *pass = lv_textarea_get_text(pwd_ta);
    strncpy(pending_password_buf, pass, sizeof(pending_password_buf));
    pending_password_buf[sizeof(pending_password_buf) - 1] = '\0';

    pending_ssid_buf[0] = '\0';
    strncpy(pending_ssid_buf, selected_ssid, sizeof(pending_ssid_buf));
    pending_ssid_buf[sizeof(pending_ssid_buf) - 1] = '\0';

    vTaskDelay(pdMS_TO_TICKS(20));
    connect_to_wifi(pending_ssid_buf, pending_password_buf);

    // Clean up password dialog
    if (pwd_dialog)
    {
        if(kb){
            lv_obj_del(kb);
            kb = NULL;
        }
        lv_obj_del(pwd_dialog);
        pwd_dialog = NULL;
        pwd_ta = NULL;
        pwd_error_label = NULL;
    }

    // Close the entire WiFi interface
    lv_async_call(async_close_wifi_dialog, NULL);
}

void connect_to_wifi(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG, "Connecting to %s", ssid);

    disable_wifi();
    vTaskDelay(pdMS_TO_TICKS(20));
    enable_wifi(ssid, pass);
}