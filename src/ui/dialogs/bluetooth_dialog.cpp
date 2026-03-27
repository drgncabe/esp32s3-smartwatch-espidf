#include <cmath>
#include <ctime>
#include <string.h>

#include "context/app_context.h"
#include "core/memory/memory_manager.h"
#include "esp_bt.h"
#include "esp_gattc_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network/modules/bluetooth_scanner.h"
#include "ui/components/nice_button.h"
#include "ui/components/toast.h"
#include "ui/dialogs/bluetooth_dialog.h"
#include "ui/screens/home_page.h"
#include "ui/screens/settings_page.h"
#include "util/util.h"

static const char *TAG = "bluetooth_dialog";

// UI state
static lv_obj_t *bluetooth_screen = NULL;
static lv_obj_t *device_list = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *scan_btn = NULL;
static lv_obj_t *stop_btn = NULL;
static bool dialog_open = false;

// Persistent device list for UI (fixes dangling pointer bug!) - in PSRAM to save internal RAM
EXT_RAM_BSS_ATTR static ble_device_t ui_devices[MAX_BLE_DEVICES];
static int ui_device_count = 0;

// Refresh timer
static lv_timer_t *refresh_timer = NULL;

// Helper: Get icon for device type
static const char* get_device_icon(ble_device_type_t type)
{
    switch (type)
    {
        case BLE_DEVICE_PHONE:      return LV_SYMBOL_CALL;
        case BLE_DEVICE_WATCH:      return LV_SYMBOL_REFRESH;  // Clock-like symbol
        case BLE_DEVICE_TRACKER:    return LV_SYMBOL_GPS;
        case BLE_DEVICE_SMART_HOME: return LV_SYMBOL_HOME;
        case BLE_DEVICE_FITNESS:    return LV_SYMBOL_BATTERY_FULL;  // Activity-like
        case BLE_DEVICE_AUDIO:      return LV_SYMBOL_AUDIO;
        case BLE_DEVICE_COMPUTER:   return LV_SYMBOL_LIST;  // Computer-like
        case BLE_DEVICE_BEACON:     return LV_SYMBOL_WARNING;
        default:                    return LV_SYMBOL_BLUETOOTH;
    }
}

// Helper: Get color for device type
__attribute__((unused)) static uint32_t get_device_color(ble_device_type_t type)
{
    switch (type)
    {
        case BLE_DEVICE_PHONE:      return 0x2196F3;  // Blue
        case BLE_DEVICE_WATCH:      return 0x9C27B0;  // Purple
        case BLE_DEVICE_TRACKER:    return 0xFF9800;  // Orange
        case BLE_DEVICE_SMART_HOME: return 0x4CAF50;  // Green
        case BLE_DEVICE_FITNESS:    return 0xE91E63;  // Pink
        case BLE_DEVICE_AUDIO:      return 0x00BCD4;  // Cyan
        case BLE_DEVICE_COMPUTER:   return 0x607D8B;  // Blue Grey
        case BLE_DEVICE_BEACON:     return 0xFFC107;  // Amber
        default:                    return 0x888888;  // Grey
    }
}

// Device details dialog state
static lv_obj_t *device_detail_screen = NULL;
static bool detail_dialog_open = false;
static ble_device_t selected_device;

// Close device details dialog
static void close_device_detail_dialog()
{
    if (!detail_dialog_open || device_detail_screen == NULL)
        return;
    
    lv_obj_del(device_detail_screen);
    device_detail_screen = NULL;
    detail_dialog_open = false;
}

// Back button callback for device details
static void detail_back_btn_cb(lv_event_t *e)
{
    play_haptic_click();
    close_device_detail_dialog();
}

// Service scan state
static lv_obj_t *service_scan_screen = NULL;
static lv_obj_t *service_list = NULL;
static lv_obj_t *service_status_label = NULL;
static bool service_scan_open = false;
EXT_RAM_BSS_ATTR static ble_gatt_service_t scanned_services[MAX_GATT_SERVICES];
static int service_count = 0;

// Expanded service state (for showing characteristics)
static int expanded_service_index = -1;  // -1 = no service expanded
static ble_device_t current_device;  // Store current device for characteristic reading

// Global characteristic storage (allocated on demand to avoid memory bloat)
static ble_gatt_char_t *global_char_storage = NULL;
static int global_char_count = 0;
static const int MAX_TOTAL_CHARS = 50;  // Total characteristics across all services

// Forward declarations
static void service_card_clicked_cb(lv_event_t *e);
static void char_read_btn_cb(lv_event_t *e);
static void ui_render_services_cb(void *user);
static void decode_characteristic_value(const ble_gatt_char_t *ch, char *decoded_str, size_t decoded_len);

// Decode characteristic value into human-readable format
static void decode_characteristic_value(const ble_gatt_char_t *ch, char *decoded_str, size_t decoded_len)
{
    // Initialize output string
    if (decoded_str && decoded_len > 0) {
        decoded_str[0] = '\0';
    }
    
    if (ch == NULL || decoded_str == NULL || decoded_len == 0) {
        return;
    }
    
    // Handle empty value
    if (ch->value_len == 0) {
        snprintf(decoded_str, decoded_len, "Empty value (0 bytes)");
        return;
    }
    
    const uint8_t *value = ch->value;
    size_t len = ch->value_len;
    
    // Debug: Log what we're decoding (use ESP_LOGI for visibility)
    ESP_LOGI("bt_dialog", "Decoding UUID 0x%04X (%s), len=%zu, handle=0x%04X", 
            ch->uuid16, ch->name, len, ch->handle);
    if (len > 0) {
        ESP_LOGI("bt_dialog", "First 4 bytes: %02X %02X %02X %02X", 
                value[0], len > 1 ? value[1] : 0, len > 2 ? value[2] : 0, len > 3 ? value[3] : 0);
    }
    
    // Try UUID-specific decoding first
    switch (ch->uuid16) {
        case 0x2A19: // Battery Level
            if (len == 1) {
                snprintf(decoded_str, decoded_len, "Battery: %d%%", value[0]);
                return;
            }
            break;
            
        case 0x2A6E: // Temperature
            if (len == 2) {
                int16_t temp = (int16_t)((value[1] << 8) | value[0]);
                snprintf(decoded_str, decoded_len, "Temperature: %.2f°C", temp / 100.0f);
                return;
            }
            break;
            
        case 0x2A6F: // Humidity
            if (len == 2) {
                uint16_t hum = (value[1] << 8) | value[0];
                snprintf(decoded_str, decoded_len, "Humidity: %.2f%%", hum / 100.0f);
                return;
            }
            break;
            
        case 0x2A6D: // Pressure
            if (len == 4) {
                uint32_t press = (value[3] << 24) | (value[2] << 16) | (value[1] << 8) | value[0];
                snprintf(decoded_str, decoded_len, "Pressure: %.2f Pa", press / 100.0f);
                return;
            }
            break;
            
        case 0x2A58: // Heart Rate Measurement
            if (len >= 2) {
                (void)value[0]; // flags unused
                uint8_t hr = value[1];
                snprintf(decoded_str, decoded_len, "Heart Rate: %d bpm", hr);
                return;
            }
            break;
            
        case 0x2A6B: // Blood Pressure
            if (len >= 7) {
                uint16_t systolic = (value[1] << 8) | value[0];
                uint16_t diastolic = (value[3] << 8) | value[2];
                snprintf(decoded_str, decoded_len, "BP: %d/%d mmHg", systolic, diastolic);
                return;
            }
            break;
            
        case 0x2A2B: // Current Time
            if (len >= 10) {
                // Current Time format: Year (2), Month (1), Day (1), Hours (1), Minutes (1), Seconds (1), DayOfWeek (1), Fractions (1), Reason (1)
                uint16_t year = (value[1] << 8) | value[0];
                uint8_t month = value[2];
                uint8_t day = value[3];
                uint8_t hours = value[4];
                uint8_t minutes = value[5];
                uint8_t seconds = value[6];
                snprintf(decoded_str, decoded_len, "Time: %04d-%02d-%02d %02d:%02d:%02d", 
                        year, month, day, hours, minutes, seconds);
                return;
            }
            break;
            
        case 0x2A0F: // Local Time Information
            if (len >= 2) {
                int8_t timezone = (int8_t)value[0];  // Timezone offset in 15-minute increments
                uint8_t dst = value[1];  // DST offset in 15-minute increments
                snprintf(decoded_str, decoded_len, "TZ: %+d (DST: %d)", timezone * 15, dst * 15);
                return;
            }
            break;
            
        case 0x2A00: // Device Name
        case 0x2A29: // Manufacturer Name
        case 0x2A24: // Model Number
        case 0x2A25: // Serial Number
        case 0x2A27: // Hardware Revision
        case 0x2A26: // Firmware Revision
        case 0x2A28: // Software Revision
            // Try as string - but only if it's actually printable
            if (len > 0 && len < 64) {
                bool is_printable = true;
                for (size_t i = 0; i < len; i++) {
                    // Check if byte is printable ASCII (32-126) or null terminator
                    if (value[i] != 0 && (value[i] < 32 || value[i] > 126)) {
                        is_printable = false;
                        break;
                    }
                }
                if (is_printable && len > 0) {
                    // Only show as string if we have at least one non-null printable character
                    bool has_content = false;
                    for (size_t i = 0; i < len; i++) {
                        if (value[i] != 0 && value[i] >= 32) {
                            has_content = true;
                            break;
                        }
                    }
                    if (has_content) {
                        snprintf(decoded_str, decoded_len, "String: \"%.*s\"", (int)len, (const char*)value);
                        return;
                    }
                }
            }
            // Fall through to generic decoding if not printable
            break;
    }
    
    // Generic decoding based on length and content
    if (len == 1) {
        // Single byte - show as number
        snprintf(decoded_str, decoded_len, "Uint8: %u (0x%02X)", value[0], value[0]);
    } else if (len == 2) {
        // Two bytes - try as uint16 or int16
        uint16_t u16 = (value[1] << 8) | value[0];
        int16_t i16 = (int16_t)u16;
        if (i16 >= 0 && i16 < 1000) {
            snprintf(decoded_str, decoded_len, "Uint16: %u | Int16: %d", u16, i16);
        } else {
            snprintf(decoded_str, decoded_len, "Uint16: %u (0x%04X)", u16, u16);
        }
    } else if (len == 4) {
        // Four bytes - try as uint32 or int32 or float
        uint32_t u32 = (value[3] << 24) | (value[2] << 16) | (value[1] << 8) | value[0];
        int32_t i32 = (int32_t)u32;
        float f32;
        memcpy(&f32, value, 4);
        
        // Check if it looks like a float (reasonable range)
        // Simple check: if it's in a reasonable range and not obviously an integer
        if (f32 >= -1000.0f && f32 <= 1000.0f && (f32 != (float)(int32_t)f32 || u32 < 1000)) {
            snprintf(decoded_str, decoded_len, "Float: %.2f | Uint32: %lu", f32, (unsigned long)u32);
        } else {
            snprintf(decoded_str, decoded_len, "Uint32: %lu | Int32: %ld", (unsigned long)u32, (long)i32);
        }
    } else if (len <= 20) {
        // Try as string if all bytes are printable
        bool is_printable = true;
        for (size_t i = 0; i < len; i++) {
            if (value[i] < 32 || value[i] > 126) {
                if (value[i] != 0) {
                    is_printable = false;
                    break;
                }
            }
        }
        if (is_printable) {
            snprintf(decoded_str, decoded_len, "String: \"%.*s\"", (int)len, (const char*)value);
            return;
        }
    }
    
    // Fallback: show as hex (always run if we haven't set decoded_str yet)
    // This handles values > 20 bytes or any other unhandled cases
    char hex_buf[128] = {0};
    int hex_pos = 0;
    size_t max_bytes = (len < 32) ? len : 32;  // Show up to 32 bytes
    for (size_t i = 0; i < max_bytes && hex_pos < (int)sizeof(hex_buf) - 4; i++) {
        hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02X ", value[i]);
    }
    if (len > 32) {
        snprintf(decoded_str, decoded_len, "Hex (%zu bytes): %s...", len, hex_buf);
    } else {
        snprintf(decoded_str, decoded_len, "Hex (%zu bytes): %s", len, hex_buf);
    }
}

// Callback for characteristic read button
static void char_read_btn_cb(lv_event_t *e)
{
    play_haptic_click();
    
    // Get service and char indices from user data
    uint32_t indices = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    int service_idx = (indices >> 16) & 0xFFFF;
    int char_idx = indices & 0xFFFF;
    
    ESP_LOGI(TAG, "Read button clicked for service %d, char %d", service_idx, char_idx);
    
    if (service_idx >= service_count || service_idx < 0) {
        show_toast("Invalid service", 1500);
        return;
    }
    
    ble_gatt_service_t *svc = &scanned_services[service_idx];
    if (svc->char_start_idx < 0 || char_idx < 0 || char_idx >= svc->num_chars) {
        show_toast("Invalid characteristic", 1500);
        return;
    }
    
    int global_char_idx = svc->char_start_idx + char_idx;
    if (global_char_idx >= global_char_count) {
        show_toast("Characteristic not found", 1500);
        return;
    }
    
    ble_gatt_char_t *ch = &global_char_storage[global_char_idx];
    
    ESP_LOGI(TAG, "Reading characteristic: %s (UUID: %s, Handle: 0x%04X, Props: 0x%02X)", 
             ch->name, ch->uuid_str, ch->handle, ch->properties);
    
    // Verify characteristic is readable
    if (!(ch->properties & ESP_GATT_CHAR_PROP_BIT_READ)) {
        ESP_LOGW(TAG, "Warning: Characteristic does not have READ property set!");
        show_toast("Not readable", 2000);
        return;
    }
    
    show_toast("Reading...", 1000);
    
    // Create a task to read the characteristic in background (blocking BLE operation)
    static struct {
        ble_device_t device;
        ble_gatt_char_t *ch_ptr;
    } read_task_params;
    
    read_task_params.device = current_device;
    read_task_params.ch_ptr = ch;
    
    xTaskCreate([](void *param) {
        auto *params = (decltype(read_task_params)*)param;
        ble_gatt_char_t *ch = params->ch_ptr;
        
        uint8_t value_buf[64];
        uint16_t value_len = 0;
        
        ESP_LOGI(TAG, "Waiting for BLE stack to recover memory...");
        ESP_LOGI(TAG, "Free heap before read: %lu bytes", (unsigned long)esp_get_free_heap_size());
        
        // Wait for memory to recover after service scan
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "Initiating GATT read for characteristic 0x%04X", ch->uuid16);
        ESP_LOGI(TAG, "Free heap at read start: %lu bytes", (unsigned long)esp_get_free_heap_size());
        
        int result = bluetooth_scanner_read_char_value(
            params->device.addr,
            params->device.addr_type,
            ch->uuid16,
            ch->handle,
            value_buf,
            sizeof(value_buf),
            &value_len
        );
        
        if (result == ESP_OK) {
            // Success - copy value to characteristic (even if 0 bytes)
            ch->value_len = (value_len < sizeof(ch->value)) ? value_len : sizeof(ch->value);
            if (value_len > 0) {
                memcpy(ch->value, value_buf, ch->value_len);
            }
            ch->value_read = true;
            
            ESP_LOGI(TAG, "Read complete: %d bytes", value_len);
            
            // Format value for toast
            char toast_msg[64];
            if (value_len == 0) {
                snprintf(toast_msg, sizeof(toast_msg), "Read OK (empty)");
            } else if (ch->uuid16 == 0x2A19 && value_len == 1) {  // Battery Level
                snprintf(toast_msg, sizeof(toast_msg), "Battery: %d%%", value_buf[0]);
            } else if (value_len <= 4) {
                // Show as decimal for small values
                uint32_t dec_val = 0;
                for (int i = 0; i < value_len; i++) {
                    dec_val |= (value_buf[i] << (i * 8));
                }
                snprintf(toast_msg, sizeof(toast_msg), "Value: %lu", (unsigned long)dec_val);
            } else {
                snprintf(toast_msg, sizeof(toast_msg), "Read OK (%d bytes)", value_len);
            }
            
            lv_async_call([](void *msg) {
                show_toast((const char*)msg, 2000);
                free(msg);
            }, strdup(toast_msg));
        } else {
            ESP_LOGE(TAG, "Read failed: %d", result);
            lv_async_call([](void *p) {
                show_toast("Read failed", 2000);
            }, NULL);
        }
        
        // Refresh UI to show the new value
        lv_async_call(ui_render_services_cb, NULL);
        
        vTaskDelete(NULL);
    }, "char_read", 4096, &read_task_params, 5, NULL);
}

// UI update function that ONLY runs on LVGL thread (thread-safe)
static void ui_render_services_cb(void *user)
{
    if (!service_scan_open || service_list == NULL) {
        ESP_LOGW(TAG, "Service scan dialog not open, skipping UI update");
        return;
    }

    // Save current scroll position to prevent auto-scroll to top
    lv_coord_t scroll_y = lv_obj_get_scroll_y(service_list);
    
    lv_obj_clean(service_list);
    
    if (service_count > 0) {
        for (int i = 0; i < service_count; i++) {
            ble_gatt_service_t *svc = &scanned_services[i];
            bool is_expanded = (expanded_service_index == i);
            
            // Create service card
            lv_obj_t *card = lv_obj_create(service_list);
            lv_obj_set_size(card, LV_PCT(95), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(card, is_expanded ? lv_color_hex(0x1565C0) : lv_color_hex(0x2A2A2A), 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, lv_color_hex(0x0D47A1), 0);
            lv_obj_set_style_pad_all(card, 8, 0);
            lv_obj_set_style_radius(card, 6, 0);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_row(card, 3, 0);
            
            // Make card clickable
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, service_card_clicked_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            
            // Service name with expand indicator
            lv_obj_t *name = lv_label_create(card);
            char name_text[70];
            snprintf(name_text, sizeof(name_text), "%s %s", is_expanded ? LV_SYMBOL_DOWN : LV_SYMBOL_RIGHT, svc->name);
            lv_label_set_text(name, name_text);
            lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
            lv_obj_set_width(name, LV_PCT(100));
            
            // Service UUID
            lv_obj_t *uuid = lv_label_create(card);
            lv_label_set_text(uuid, svc->uuid_str);
            lv_obj_set_style_text_color(uuid, lv_color_hex(0x888888), 0);
            lv_obj_set_style_text_font(uuid, &lv_font_montserrat_10, 0);
            lv_obj_set_width(uuid, LV_PCT(100));
            
            // If expanded, show "Tap to load characteristics" message
            if (is_expanded && svc->num_chars == 0) {
                lv_obj_t *load_label = lv_label_create(card);
                lv_label_set_text(load_label, "Loading characteristics...");
                lv_obj_set_style_text_color(load_label, lv_color_hex(0xFFC107), 0);
                lv_obj_set_style_text_font(load_label, &lv_font_montserrat_10, 0);
            }
            // If expanded and has characteristics, show them
            else if (is_expanded && svc->num_chars > 0) {
                // Characteristics header
                lv_obj_t *char_header = lv_label_create(card);
                char char_text[50];
                snprintf(char_text, sizeof(char_text), "--- %d Characteristic%s ---", svc->num_chars, svc->num_chars == 1 ? "" : "s");
                lv_label_set_text(char_header, char_text);
                lv_obj_set_style_text_color(char_header, lv_color_hex(0x4CAF50), 0);
                lv_obj_set_style_text_font(char_header, &lv_font_montserrat_10, 0);
                
                // Show each characteristic from global storage
                if (global_char_storage != NULL && svc->char_start_idx >= 0 && svc->char_start_idx < global_char_count) {
                    for (int j = 0; j < svc->num_chars && (svc->char_start_idx + j) < global_char_count; j++) {
                        ble_gatt_char_t *ch = &global_char_storage[svc->char_start_idx + j];
                    
                    // Characteristic mini-card
                    lv_obj_t *char_card = lv_obj_create(card);
                    lv_obj_set_size(char_card, LV_PCT(95), LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(char_card, lv_color_hex(0x1A1A1A), 0);
                    lv_obj_set_style_border_width(char_card, 1, 0);
                    lv_obj_set_style_border_color(char_card, lv_color_hex(0x444444), 0);
                    lv_obj_set_style_pad_all(char_card, 5, 0);
                    lv_obj_set_style_radius(char_card, 4, 0);
                    lv_obj_set_flex_flow(char_card, LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_style_pad_row(char_card, 2, 0);
                    
                    // Char name
                    lv_obj_t *char_name = lv_label_create(char_card);
                    lv_label_set_text(char_name, ch->name);
                    lv_obj_set_style_text_color(char_name, lv_color_hex(0xE0E0E0), 0);
                    lv_obj_set_style_text_font(char_name, &lv_font_montserrat_10, 0);
                    
                    // Char UUID
                    lv_obj_t *char_uuid = lv_label_create(char_card);
                    lv_label_set_text(char_uuid, ch->uuid_str);
                    lv_obj_set_style_text_color(char_uuid, lv_color_hex(0x666666), 0);
                    lv_obj_set_style_text_font(char_uuid, &lv_font_montserrat_10, 0);
                    
                    // Properties badges
                    lv_obj_t *props_row = lv_obj_create(char_card);
                    lv_obj_set_size(props_row, LV_PCT(100), LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_opa(props_row, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(props_row, 0, 0);
                    lv_obj_set_style_pad_all(props_row, 0, 0);
                    lv_obj_set_flex_flow(props_row, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(props_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
                    lv_obj_set_style_pad_column(props_row, 3, 0);
                    
                    char props_text[32] = "Props: ";
                    if (ch->properties & ESP_GATT_CHAR_PROP_BIT_READ) strcat(props_text, "R ");
                    if (ch->properties & ESP_GATT_CHAR_PROP_BIT_WRITE) strcat(props_text, "W ");
                    if (ch->properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) strcat(props_text, "N ");
                    if (ch->properties & ESP_GATT_CHAR_PROP_BIT_INDICATE) strcat(props_text, "I ");
                    
                    lv_obj_t *props_label = lv_label_create(props_row);
                    lv_label_set_text(props_label, props_text);
                    lv_obj_set_style_text_color(props_label, lv_color_hex(0xFFC107), 0);
                    lv_obj_set_style_text_font(props_label, &lv_font_montserrat_10, 0);
                    
                    // Read button if characteristic is readable
                    if (ch->properties & ESP_GATT_CHAR_PROP_BIT_READ) {
                        lv_obj_t *read_btn = lv_btn_create(char_card);
                        lv_obj_set_size(read_btn, LV_PCT(90), 25);
                        lv_obj_set_style_bg_color(read_btn, lv_color_hex(0x2196F3), 0);
                        lv_obj_set_style_radius(read_btn, 3, 0);
                        lv_obj_set_ext_click_area(read_btn, GLOBAL_EXT_CLICK_AREA);
                        
                        lv_obj_t *btn_label = lv_label_create(read_btn);
                        lv_label_set_text(btn_label, ch->value_read ? "Read Again" : "Read Value");
                        lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_10, 0);
                        lv_obj_center(btn_label);
                        
                        // Pack service index and char index into user data
                        uint32_t indices = ((uint32_t)i << 16) | (uint32_t)j;
                        lv_obj_add_event_cb(read_btn, char_read_btn_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)indices);
                    }
                    
                    // Show value if already read
                    if (ch->value_read && ch->value_len > 0) {
                        // Debug: Log which characteristic we're displaying
                        ESP_LOGD("bt_dialog", "Displaying value for UUID 0x%04X (%s), len=%d, first byte=0x%02X", 
                                ch->uuid16, ch->name, ch->value_len, ch->value[0]);
                        
                        // Decoded value
                        char decoded_text[128];
                        decode_characteristic_value(ch, decoded_text, sizeof(decoded_text));
                        
                        ESP_LOGD("bt_dialog", "Decoded text: %s", decoded_text);
                        
                        lv_obj_t *decoded_label = lv_label_create(char_card);
                        lv_label_set_text(decoded_label, decoded_text);
                        lv_obj_set_style_text_color(decoded_label, lv_color_hex(0x4CAF50), 0);
                        lv_obj_set_style_text_font(decoded_label, &lv_font_montserrat_10, 0);
                        
                        // Also show hex for reference
                        lv_obj_t *hex_label = lv_label_create(char_card);
                        char hex_text[128];
                        snprintf(hex_text, sizeof(hex_text), "Hex (%d bytes): ", ch->value_len);
                        
                        char hex_str[64];
                        int hex_len = 0;
                        for (int k = 0; k < ch->value_len && k < 20 && hex_len < (int)sizeof(hex_str) - 3; k++) {
                            hex_len += snprintf(hex_str + hex_len, sizeof(hex_str) - hex_len, "%02X ", ch->value[k]);
                        }
                        strcat(hex_text, hex_str);
                        
                        lv_label_set_text(hex_label, hex_text);
                        lv_obj_set_style_text_color(hex_label, lv_color_hex(0x888888), 0);
                        lv_obj_set_style_text_font(hex_label, &lv_font_montserrat_10, 0);
                    }
                    }  // End of for loop
                }  // End of if (global_char_storage != NULL)
            }  // End of else if (is_expanded && svc->num_chars > 0)
        }  // End of for (int i = 0; i < service_count; i++)
        
        if (service_status_label) {
            char final_text[50];
            snprintf(final_text, sizeof(final_text), "Found %d services (tap to expand)", service_count);
            lv_label_set_text(service_status_label, final_text);
            lv_obj_set_style_text_color(service_status_label, lv_color_hex(0x4CAF50), 0);
        }
    }
    
    // Restore scroll position to prevent jumping back to top
    lv_obj_scroll_to_y(service_list, scroll_y, LV_ANIM_OFF);
}

// UI error update function that ONLY runs on LVGL thread (thread-safe)
static void ui_render_error_cb(void *user)
{
    if (!service_scan_open || service_status_label == NULL) {
        return;
    }
    
    int error_code = (int)(intptr_t)user;
    const char *error_msg = "Scan failed";
    
    switch (error_code) {
        case -2: error_msg = "Scan already in progress"; break;
        case -3: error_msg = "Failed to register callback"; break;
        case -4: error_msg = "Failed to register app"; break;
        case -5: error_msg = "Failed to connect"; break;
        case -6: error_msg = "Connection timeout"; break;
        default: error_msg = "Unknown error"; break;
    }
    
    lv_label_set_text(service_status_label, error_msg);
    lv_obj_set_style_text_color(service_status_label, lv_color_hex(0xFF5252), 0);
}

// Close service scan dialog (deferred to avoid deletion during event processing)
static void close_service_scan_dialog()
{
    if (!service_scan_open || service_scan_screen == NULL)
        return;
    
    bluetooth_scanner_stop_gatt();
    lv_obj_del(service_scan_screen);
    service_scan_screen = NULL;
    service_list = NULL;
    service_status_label = NULL;
    service_scan_open = false;
}

// Deferred close function for thread safety
static void close_service_scan_dialog_async(void *p)
{
    close_service_scan_dialog();
}

// Service scan back button callback
static void service_scan_back_btn_cb(lv_event_t *e)
{
    play_haptic_click();
    // Defer deletion to avoid deleting during event processing
    lv_async_call(close_service_scan_dialog_async, NULL);
}

// Progress callback for GATT scan
static void gatt_scan_progress(int percent, const char *status)
{
    if (service_status_label != NULL) {
        char status_text[100];
        snprintf(status_text, sizeof(status_text), "%s (%d%%)", status, percent);
        lv_label_set_text(service_status_label, status_text);
    }
}

// Task to perform GATT scan in background (NEVER calls LVGL directly!)
static void gatt_scan_task(void *pvParameters)
{
    ble_device_t *device = (ble_device_t *)pvParameters;
    
    ESP_LOGI(TAG, "Starting GATT scan for %s", device->name);
    
    // CRITICAL: Stop scanning AND WAIT for controller confirmation
    ESP_LOGI(TAG, "Stopping active scan before GATT connection...");
    esp_err_t stop_ret = bluetooth_scanner_stop_and_wait(2000);
    if (stop_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop scanner: %s", esp_err_to_name(stop_ret));
    }
    
    // Perform the scan (this fills scanned_services[] and returns count)
    int result = bluetooth_scanner_scan_services(
        device->addr,
        device->addr_type,
        scanned_services,
        MAX_GATT_SERVICES,
        NULL,  // Not scanning characteristics yet
        0,
        gatt_scan_progress
    );
    
    if (result > 0) {
        service_count = result;
        ESP_LOGI(TAG, "GATT scan successful, found %d services", service_count);
        
        // Initialize characteristic indices
        for (int i = 0; i < service_count; i++) {
            scanned_services[i].char_start_idx = -1;
            scanned_services[i].num_chars = 0;
        }
        
        // Schedule UI update on LVGL thread (thread-safe!)
        lv_async_call(ui_render_services_cb, NULL);
    } else {
        ESP_LOGE(TAG, "GATT scan failed: %d", result);
        service_count = 0;
        
        // Schedule error UI update on LVGL thread (thread-safe!)
        lv_async_call(ui_render_error_cb, (void*)(intptr_t)result);
    }
    
    vTaskDelete(NULL);
}

// Service card clicked callback - expand/collapse to show characteristics
static void service_card_clicked_cb(lv_event_t *e)
{
    play_haptic_click();
    int service_idx = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (service_idx < 0 || service_idx >= service_count) {
        return;
    }
    
    // Toggle expansion
    if (expanded_service_index == service_idx) {
        // Collapse
        expanded_service_index = -1;
    } else {
        // Expand
        expanded_service_index = service_idx;
        
        // If characteristics not yet loaded, trigger loading in background
        if (scanned_services[service_idx].num_chars == 0) {
            ESP_LOGI(TAG, "Loading characteristics for service %d", service_idx);
            
            // Check if storage is available
            if (global_char_storage == NULL) {
                ESP_LOGE(TAG, "Characteristic storage not allocated!");
                show_toast("Storage error", 1500);
                return;
            }
            
            // Create a task to load characteristics (thread-safe)
            static int task_service_idx = 0;
            task_service_idx = service_idx;
            xTaskCreate([](void *param) {
                int idx = *((int*)param);
                ble_gatt_service_t *svc = &scanned_services[idx];
                
                ESP_LOGI(TAG, "Starting characteristic discovery for service 0x%04X", svc->uuid16);
                
                // Stop scanning first
                bluetooth_scanner_stop_and_wait(2000);
                
                // Check if we have space in global storage
                if (global_char_count >= MAX_TOTAL_CHARS) {
                    ESP_LOGE(TAG, "No space for more characteristics");
                    svc->num_chars = 0;
                    svc->char_start_idx = -1;
                    lv_async_call(ui_render_services_cb, NULL);
                    vTaskDelete(NULL);
                    return;
                }
                
                // Discover characteristics into temporary buffer
                ble_gatt_char_t temp_chars[MAX_GATT_CHARS_PER_SERVICE];
                int result = bluetooth_scanner_read_characteristics(
                    current_device.addr,
                    current_device.addr_type,
                    svc->uuid16,
                    svc->handle,
                    temp_chars,
                    MAX_GATT_CHARS_PER_SERVICE,
                    NULL  // No progress callback for now
                );
                
                if (result >= 0) {
                    // result >= 0 means success (0 = no characteristics, >0 = found some)
                    if (result > 0) {
                        // Copy to global storage
                        int space_available = MAX_TOTAL_CHARS - global_char_count;
                        int chars_to_copy = (result < space_available) ? result : space_available;
                        
                        memcpy(&global_char_storage[global_char_count], temp_chars, 
                               sizeof(ble_gatt_char_t) * chars_to_copy);
                        
                        svc->char_start_idx = global_char_count;
                        svc->num_chars = chars_to_copy;
                        global_char_count += chars_to_copy;
                        
                        ESP_LOGI(TAG, "Found %d characteristics for service 0x%04X (stored at idx %d)", 
                                 chars_to_copy, svc->uuid16, svc->char_start_idx);
                    } else {
                        // Service has no characteristics (valid case)
                        ESP_LOGI(TAG, "Service 0x%04X has no characteristics", svc->uuid16);
                        svc->num_chars = 0;
                        svc->char_start_idx = -1;
                    }
                } else {
                    // Negative result means error
                    ESP_LOGE(TAG, "Failed to discover characteristics: %d", result);
                    svc->num_chars = 0;
                    svc->char_start_idx = -1;
                }
                
                // Refresh UI on LVGL thread
                lv_async_call(ui_render_services_cb, NULL);
                
                vTaskDelete(NULL);
            }, "char_disc", 8192, &task_service_idx, 5, NULL);
        }
    }
    
    // Refresh UI to show expansion
    lv_async_call(ui_render_services_cb, NULL);
}

// Open service scan dialog
static void open_service_scan_dialog(ble_device_t *device)
{
    if (service_scan_open) return;
    
    service_scan_open = true;
    service_count = 0;
    expanded_service_index = -1;  // Reset expansion state
    memset(scanned_services, 0, sizeof(scanned_services));
    memcpy(&current_device, device, sizeof(ble_device_t));  // Store device for later use
    
    // Create full-screen dialog
    service_scan_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(service_scan_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(service_scan_screen, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(service_scan_screen, 0, 0);
    lv_obj_set_style_pad_all(service_scan_screen, 0, 0);
    lv_obj_clear_flag(service_scan_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header
    lv_obj_t *header = lv_obj_create(service_scan_screen);
    lv_obj_set_size(header, LV_PCT(100), 45);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0D47A1), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "GATT Services");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    
    btn_config_t back_cfg = {
        .text = LV_SYMBOL_LEFT,
        .icon = NULL,
        .style = BTN_STYLE_PRIMARY,
        .width = 30,
        .height = 25,
        .callback = service_scan_back_btn_cb,
        .user_data = NULL
    };
    lv_obj_t *back_btn = qc_create_button(header, &back_cfg);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // Status label
    service_status_label = lv_label_create(service_scan_screen);
    lv_label_set_text(service_status_label, "Initializing...");
    lv_obj_set_style_text_color(service_status_label, lv_color_hex(0xFFC107), 0);
    lv_obj_set_style_text_font(service_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(service_status_label, 10, 50);
    
    // Service list
    service_list = lv_obj_create(service_scan_screen);
    lv_obj_set_size(service_list, LV_PCT(100), LV_VER_RES - 70);
    lv_obj_set_pos(service_list, 0, 70);
    lv_obj_set_style_bg_color(service_list, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(service_list, 0, 0);
    lv_obj_set_style_pad_all(service_list, 5, 0);
    lv_obj_set_flex_flow(service_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(service_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(service_list, 5, 0);
    
    // Start GATT scan task
    static ble_device_t device_copy;
    memcpy(&device_copy, device, sizeof(ble_device_t));
    xTaskCreate(gatt_scan_task, "gatt_scan", 3072, &device_copy, 5, NULL); // Reduced from 4096
}

// Scan GATT services callback
static void scan_services_cb(lv_event_t *e)
{
    play_haptic_click();
    
    // Check if device is connectable before attempting GATT scan
    if (!selected_device.connectable) {
        show_toast("Not connectable (ADV only)", 2000);
        ESP_LOGW(TAG, "Cannot scan services: device is not connectable");
        return;
    }
    
    open_service_scan_dialog(&selected_device);
}

// Attempt connection callback
static void connect_device_cb(lv_event_t *e)
{
    play_haptic_click();
    
    if (!selected_device.connectable) {
        show_toast("Device not connectable!", 2000);
        ESP_LOGW(TAG, "Cannot connect: device is not connectable");
        return;
    }
    
    ESP_LOGI(TAG, "Attempting to connect to device: %s", selected_device.name);
    
    esp_err_t ret = bluetooth_scanner_connect_and_pair(
        selected_device.addr,
        selected_device.addr_type
    );
    
    if (ret == ESP_OK) {
        show_toast("Connecting... Check phone for pairing!", 3000);
        ESP_LOGI(TAG, "Connection initiated - check device for pairing dialog");
    } else {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "Connection failed: %s", esp_err_to_name(ret));
        show_toast(error_msg, 3000);
        ESP_LOGE(TAG, "Connection failed: %s", esp_err_to_name(ret));
    }
}

// Attempt to read device identifying information (Device Name, Owner Name, etc.)
__attribute__((unused)) static void read_device_identity_info_cb(lv_event_t *e)
{
    play_haptic_click();
    
    if (!selected_device.connectable) {
        show_toast("Not connectable", 2000);
        return;
    }
    
    ESP_LOGI(TAG, "Reading device identity information...");
    show_toast("Reading device info...", 1500);
    
    // Create a task to read identity characteristics
    static ble_device_t device_copy;
    memcpy(&device_copy, &selected_device, sizeof(ble_device_t));
    
    xTaskCreate([](void *param) {
        ble_device_t *device = (ble_device_t *)param;
        
        // Well-known characteristic UUIDs for device identity
        uint16_t identity_uuids[] = {
            0x2A00,  // Device Name
            0x2A29,  // Manufacturer
            0x2A24,  // Model Number
            0x2A25,  // Serial Number
            0x2A27,  // Hardware Rev
            0x2A26,  // Firmware Rev
        };
        
        const char *identity_names[] = {
            "Device Name",
            "Manufacturer",
            "Model Number",
            "Serial Number",
            "Hardware Rev",
            "Firmware Rev"
        };
        
        char found_values[6][64];
        bool found_flags[6] = {false};
        int found_count = 0;
        
        // Read all characteristics in one connection session
        for (int i = 0; i < 6; i++) {
            uint8_t value[64];
            size_t len = sizeof(value);
            
            ESP_LOGI(TAG, "Attempting to read %s (UUID: 0x%04X)...", identity_names[i], identity_uuids[i]);
            
            esp_err_t ret = bluetooth_scanner_read_char_by_uuid(
                device->addr,
                device->addr_type,
                identity_uuids[i],
                value,
                &len
            );
            
            if (ret == ESP_OK && len > 0) {
                size_t copy_len = (len < 63) ? len : 63;
                memcpy(found_values[i], value, copy_len);
                found_values[i][copy_len] = '\0';
                found_flags[i] = true;
                found_count++;
                
                ESP_LOGI(TAG, "Found %s: %s", identity_names[i], found_values[i]);
            } else {
                ESP_LOGW(TAG, "Failed to read %s: %s", identity_names[i], esp_err_to_name(ret));
            }
            
            // Give BT stack time to settle between reads
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        
        // Build result message
        if (found_count > 0) {
            char *msg = (char*)heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!msg) msg = (char*)malloc(256);
            
            if (msg) {
                int offset = 0;
                offset += snprintf(msg + offset, 256 - offset, "Device Identity:\n");
                
                for (int i = 0; i < 6; i++) {
                    if (found_flags[i]) {
                        offset += snprintf(msg + offset, 256 - offset, 
                                         "%s: %s\n", 
                                         identity_names[i], 
                                         found_values[i]);
                    }
                }
                
                lv_async_call([](void *user_data) {
                    char *message = (char*)user_data;
                    show_toast(message, 5000);
                    free(message);
                }, msg);
            }
        } else {
            lv_async_call([](void *user_data) {
                show_toast("No identity info found", 2000);
            }, NULL);
        }
        
        vTaskDelete(NULL);
    }, "read_identity", 4096, &device_copy, 5, NULL);
}

// Send alert to device (Find My Device / Make it beep)
static void send_alert_to_device_cb(lv_event_t *e)
{
    play_haptic_click();
    
    if (!selected_device.connectable) {
        show_toast("Not connectable", 2000);
        return;
    }
    
    ESP_LOGI(TAG, "Sending alert to device...");
    show_toast("Sending alert...", 1500);
    
    static ble_device_t device_copy;
    memcpy(&device_copy, &selected_device, sizeof(ble_device_t));
    
    xTaskCreate([](void *param) {
        ble_device_t *device = (ble_device_t *)param;
        
        // Try to write to Immediate Alert Service (0x1802) - Alert Level characteristic (0x2A06)
        // Alert levels: 0x00 = No Alert, 0x01 = Mild Alert, 0x02 = High Alert
        uint8_t alert_level = 0x02; // High Alert (loud beep/vibration)
        
        esp_err_t ret = bluetooth_scanner_write_char_by_uuid(
            device->addr,
            device->addr_type,
            0x2A06, // Alert Level characteristic
            &alert_level,
            1
        );
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Alert sent successfully!");
            lv_async_call([](void *user_data) {
                show_toast("Alert sent to device!", 2000);
            }, NULL);
        } else {
            ESP_LOGW(TAG, "Failed to send alert: %s", esp_err_to_name(ret));
            lv_async_call([](void *user_data) {
                show_toast("Device doesn't support alerts", 2000);
            }, NULL);
        }
        
        vTaskDelete(NULL);
    }, "send_alert", 4096, &device_copy, 5, NULL);
}

// Set current time on device (Current Time Service)
static void set_time_on_device_cb(lv_event_t *e)
{
    play_haptic_click();
    
    if (!selected_device.connectable) {
        show_toast("Not connectable", 2000);
        return;
    }
    
    ESP_LOGI(TAG, "Setting time on device...");
    show_toast("Setting time...", 1500);
    
    static ble_device_t device_copy;
    memcpy(&device_copy, &selected_device, sizeof(ble_device_t));
    
    xTaskCreate([](void *param) {
        ble_device_t *device = (ble_device_t *)param;
        
        // Current Time characteristic (0x2A2B) format:
        // Year (uint16), Month (uint8), Day (uint8), Hour (uint8), Minute (uint8), Second (uint8),
        // Day of Week (uint8), Fractions256 (uint8), Adjust Reason (uint8)
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        
        uint8_t time_data[10];
        uint16_t year = timeinfo.tm_year + 1900;
        time_data[0] = year & 0xFF;
        time_data[1] = (year >> 8) & 0xFF;
        time_data[2] = timeinfo.tm_mon + 1; // Month (1-12)
        time_data[3] = timeinfo.tm_mday;    // Day (1-31)
        time_data[4] = timeinfo.tm_hour;    // Hour (0-23)
        time_data[5] = timeinfo.tm_min;     // Minute (0-59)
        time_data[6] = timeinfo.tm_sec;     // Second (0-59)
        time_data[7] = timeinfo.tm_wday + 1; // Day of week (1=Monday, 7=Sunday)
        time_data[8] = 0;                   // Fractions256
        time_data[9] = 0x01;                // Manual time update
        
        esp_err_t ret = bluetooth_scanner_write_char_by_uuid(
            device->addr,
            device->addr_type,
            0x2A2B, // Current Time characteristic
            time_data,
            10
        );
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Time set successfully!");
            lv_async_call([](void *user_data) {
                show_toast("Time synchronized!", 2000);
            }, NULL);
        } else {
            ESP_LOGW(TAG, "Failed to set time: %s", esp_err_to_name(ret));
            lv_async_call([](void *user_data) {
                show_toast("Device doesn't support time sync", 2000);
            }, NULL);
        }
        
        vTaskDelete(NULL);
    }, "set_time", 4096, &device_copy, 5, NULL);
}

// Make phone ring (Ringer Control Point)
static void ring_device_cb(lv_event_t *e)
{
    play_haptic_click();
    
    if (!selected_device.connectable) {
        show_toast("Not connectable", 2000);
        return;
    }
    
    ESP_LOGI(TAG, "Triggering ringer on device...");
    show_toast("Ringing device...", 1500);
    
    static ble_device_t device_copy;
    memcpy(&device_copy, &selected_device, sizeof(ble_device_t));
    
    xTaskCreate([](void *param) {
        ble_device_t *device = (ble_device_t *)param;
        
        // Ringer Control Point (0x2A40) values:
        // 0x01 = Silent Mode, 0x02 = Mute Once, 0x03 = Cancel Silent Mode
        uint8_t ring_command = 0x03; // Cancel Silent Mode (make it ring)
        
        esp_err_t ret = bluetooth_scanner_write_char_by_uuid(
            device->addr,
            device->addr_type,
            0x2A40, // Ringer Control Point
            &ring_command,
            1
        );
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Ringer triggered successfully!");
            lv_async_call([](void *user_data) {
                show_toast("Device ringing!", 2000);
            }, NULL);
        } else {
            ESP_LOGW(TAG, "Failed to trigger ringer: %s", esp_err_to_name(ret));
            lv_async_call([](void *user_data) {
                show_toast("Device doesn't support ringer control", 2000);
            }, NULL);
        }
        
        vTaskDelete(NULL);
    }, "ring_dev", 4096, &device_copy, 5, NULL);
}

// Generic write to characteristic (advanced feature)
static lv_obj_t *write_dialog = NULL;
static lv_obj_t *write_uuid_input = NULL;
static lv_obj_t *write_value_input = NULL;

static void execute_generic_write_cb(lv_event_t *e)
{
    play_haptic_click();
    
    const char *uuid_str = lv_textarea_get_text(write_uuid_input);
    const char *value_str = lv_textarea_get_text(write_value_input);
    
    if (strlen(uuid_str) == 0 || strlen(value_str) == 0) {
        show_toast("UUID and Value required", 2000);
        return;
    }
    
    // Parse UUID (hex string)
    uint16_t uuid = 0;
    if (sscanf(uuid_str, "%hx", &uuid) != 1) {
        show_toast("Invalid UUID format (use hex)", 2000);
        return;
    }
    
    // Parse value (hex string, space-separated bytes)
    uint8_t value_data[256];
    size_t value_len = 0;
    
    const char *ptr = value_str;
    while (*ptr && value_len < 256) {
        while (*ptr == ' ' || *ptr == ',' || *ptr == ':') ptr++;
        if (!*ptr) break;
        
        unsigned int byte;
        if (sscanf(ptr, "%2x", &byte) == 1) {
            value_data[value_len++] = (uint8_t)byte;
            ptr += 2;
        } else {
            show_toast("Invalid hex value", 2000);
            return;
        }
    }
    
    if (value_len == 0) {
        show_toast("Value cannot be empty", 2000);
        return;
    }
    
    // Close dialog
    if (write_dialog) {
        lv_obj_t *backdrop = lv_obj_get_parent(write_dialog);
        if (backdrop) {
            lv_obj_del(backdrop);
        }
        write_dialog = NULL;
        write_uuid_input = NULL;
        write_value_input = NULL;
    }
    
    ESP_LOGI(TAG, "Writing %d bytes to UUID 0x%04X", value_len, uuid);
    show_toast("Writing...", 1500);
    
    // Copy data for task
    typedef struct {
        ble_device_t device;
        uint16_t uuid;
        uint8_t value[256];
        size_t len;
    } write_task_params_t;
    
    write_task_params_t *params = (write_task_params_t *)heap_caps_malloc(
        sizeof(write_task_params_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!params) {
        params = (write_task_params_t *)malloc(sizeof(write_task_params_t));
    }
    
    if (!params) {
        show_toast("Memory error", 2000);
        return;
    }
    
    memcpy(&params->device, &selected_device, sizeof(ble_device_t));
    params->uuid = uuid;
    memcpy(params->value, value_data, value_len);
    params->len = value_len;
    
    xTaskCreate([](void *param) {
        write_task_params_t *p = (write_task_params_t *)param;
        
        esp_err_t ret = bluetooth_scanner_write_char_by_uuid(
            p->device.addr,
            p->device.addr_type,
            p->uuid,
            p->value,
            p->len
        );
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Write successful!");
            lv_async_call([](void *user_data) {
                show_toast("Write successful!", 2000);
            }, NULL);
        } else {
            ESP_LOGW(TAG, "Write failed: %s", esp_err_to_name(ret));
            lv_async_call([](void *user_data) {
                show_toast("Write failed", 2000);
            }, NULL);
        }
        
        free(p);
        vTaskDelete(NULL);
    }, "write_char", 4096, params, 5, NULL);
}

static void generic_write_cb(lv_event_t *e)
{
    play_haptic_click();
    
    if (!selected_device.connectable) {
        show_toast("Not connectable", 2000);
        return;
    }
    
    // Create backdrop for tap-outside-to-close
    lv_obj_t *backdrop = lv_obj_create(lv_scr_act());
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create dialog
    write_dialog = lv_obj_create(backdrop);
    lv_obj_set_size(write_dialog, LV_PCT(90), LV_PCT(70)); // Larger dialog
    lv_obj_center(write_dialog);
    lv_obj_set_style_bg_color(write_dialog, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_color(write_dialog, lv_color_hex(0x00CED1), 0);
    lv_obj_set_style_border_width(write_dialog, 2, 0);
    lv_obj_set_style_pad_all(write_dialog, 15, 0);
    lv_obj_set_flex_flow(write_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(write_dialog, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(write_dialog, 8, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(write_dialog);
    lv_label_set_text(title, LV_SYMBOL_EDIT " Write to Characteristic");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00CED1), 0);
    
    // UUID input section
    lv_obj_t *uuid_label = lv_label_create(write_dialog);
    lv_label_set_text(uuid_label, "UUID (hex, e.g., 2A06):");
    lv_obj_set_style_pad_top(uuid_label, 5, 0);
    
    write_uuid_input = lv_textarea_create(write_dialog);
    lv_obj_set_size(write_uuid_input, LV_PCT(100), 45);
    lv_textarea_set_one_line(write_uuid_input, true);
    lv_textarea_set_placeholder_text(write_uuid_input, "2A06");
    lv_obj_set_style_text_font(write_uuid_input, &lv_font_montserrat_14, 0);
    
    // Value input section
    lv_obj_t *value_label = lv_label_create(write_dialog);
    lv_label_set_text(value_label, "Value (hex bytes, e.g., 02 FF A0):");
    lv_obj_set_style_pad_top(value_label, 5, 0);
    
    write_value_input = lv_textarea_create(write_dialog);
    lv_obj_set_size(write_value_input, LV_PCT(100), 45);
    lv_textarea_set_one_line(write_value_input, true);
    lv_textarea_set_placeholder_text(write_value_input, "02");
    lv_obj_set_style_text_font(write_value_input, &lv_font_montserrat_14, 0);
    
    // Add event to hide keyboard on unfocus
    lv_obj_add_event_cb(write_uuid_input, [](lv_event_t *e) {
        lv_obj_t *ta = lv_event_get_target(e);
        lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(ta);
        if (kb) {
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_DEFOCUSED, NULL);
    
    lv_obj_add_event_cb(write_value_input, [](lv_event_t *e) {
        lv_obj_t *ta = lv_event_get_target(e);
        lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(ta);
        if (kb) {
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_DEFOCUSED, NULL);
    
    // Spacer to push buttons to bottom
    lv_obj_t *spacer = lv_obj_create(write_dialog);
    lv_obj_set_size(spacer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);
    
    // Buttons at bottom
    lv_obj_t *btn_container = lv_obj_create(write_dialog);
    lv_obj_set_size(btn_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_set_style_pad_all(btn_container, 5, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    btn_config_t write_btn_cfg = {
        .text = "Write",
        .icon = LV_SYMBOL_OK,
        .style = BTN_STYLE_SUCCESS,
        .width = 100,
        .height = 35,
        .callback = execute_generic_write_cb,
        .user_data = NULL
    };
    qc_create_button(btn_container, &write_btn_cfg);
    
    btn_config_t cancel_btn_cfg = {
        .text = "Cancel",
        .icon = LV_SYMBOL_CLOSE,
        .style = BTN_STYLE_SECONDARY,
        .width = 100,
        .height = 35,
        .callback = [](lv_event_t *e) {
            play_haptic_click();
            if (write_dialog) {
                lv_obj_t *backdrop = lv_obj_get_parent(write_dialog);
                if (backdrop) {
                    lv_obj_del(backdrop);
                }
                write_dialog = NULL;
                write_uuid_input = NULL;
                write_value_input = NULL;
            }
        },
        .user_data = NULL
    };
    qc_create_button(btn_container, &cancel_btn_cfg);
    
    // Create keyboard
    lv_obj_t *kb = lv_keyboard_create(backdrop);
    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(40));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); // Start hidden
    
    // Set keyboard for both textareas
    lv_keyboard_set_textarea(kb, write_uuid_input);
    lv_obj_set_user_data(write_uuid_input, kb);
    lv_obj_set_user_data(write_value_input, kb);
    
    // Show keyboard on focus
    lv_obj_add_event_cb(write_uuid_input, [](lv_event_t *e) {
        lv_obj_t *ta = lv_event_get_target(e);
        lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(ta);
        if (kb) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_FOCUSED, NULL);
    
    lv_obj_add_event_cb(write_value_input, [](lv_event_t *e) {
        lv_obj_t *ta = lv_event_get_target(e);
        lv_obj_t *kb = (lv_obj_t *)lv_obj_get_user_data(ta);
        if (kb) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_FOCUSED, NULL);
    
    // Tap backdrop to close dialog (unfocus and hide keyboard)
    lv_obj_add_event_cb(backdrop, [](lv_event_t *e) {
        lv_obj_t *target = lv_event_get_target(e);
        lv_obj_t *clicked = lv_event_get_current_target(e);
        
        // Only close if backdrop itself was clicked, not its children
        if (target == clicked) {
            play_haptic_click();
            if (write_dialog) {
                lv_obj_del(clicked); // Delete backdrop (which deletes dialog and keyboard)
                write_dialog = NULL;
                write_uuid_input = NULL;
                write_value_input = NULL;
            }
        }
    }, LV_EVENT_CLICKED, NULL);
}

// Read manufacturer data callback
static void read_manufacturer_cb(lv_event_t *e)
{
    play_haptic_click();
    show_toast("Reading manufacturer data...", 2000);
    // TODO: Implement manufacturer data parsing
}

// Open device details dialog
static void open_device_detail_dialog(ble_device_t *device)
{
    if (detail_dialog_open || device == NULL)
        return;
    
    // Copy device data
    memcpy(&selected_device, device, sizeof(ble_device_t));
    
    // Create full-screen dialog
    device_detail_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(device_detail_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(device_detail_screen, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(device_detail_screen, 0, 0);
    lv_obj_set_style_pad_all(device_detail_screen, 0, 0);
    lv_obj_clear_flag(device_detail_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header
    lv_obj_t *header = lv_obj_create(device_detail_screen);
    lv_obj_set_size(header, LV_PCT(100), 45);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0D47A1), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Device Details");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    
    btn_config_t back_cfg = {
        .text = LV_SYMBOL_LEFT,
        .icon = NULL,
        .style = BTN_STYLE_PRIMARY,
        .width = 30,
        .height = 25,
        .callback = detail_back_btn_cb,
        .user_data = NULL
    };
    lv_obj_t *back_btn = qc_create_button(header, &back_cfg);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // Scrollable content area
    lv_obj_t *content = lv_obj_create(device_detail_screen);
    lv_obj_set_size(content, LV_PCT(100), LV_VER_RES - 45);
    lv_obj_set_pos(content, 0, 45);  // Position directly below header
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_style_pad_top(content, 5, 0);  // Minimal top padding
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 6, 0);
    
    // Device name and icon
    lv_obj_t *name_label = lv_label_create(content);
    char name_text[70];
    snprintf(name_text, sizeof(name_text), "%s %s", get_device_icon(device->type), device->name);
    lv_label_set_text(name_label, name_text);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(name_label, LV_PCT(95));
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    
    // Info section
    lv_obj_t *info_container = lv_obj_create(content);
    lv_obj_set_size(info_container, LV_PCT(95), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(info_container, 1, 0);
    lv_obj_set_style_border_color(info_container, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(info_container, 8, 0);
    lv_obj_set_style_pad_all(info_container, 8, 0);
    lv_obj_set_flex_flow(info_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info_container, 4, 0);
    
    // Device Name (if available)
    if (device->has_name) {
        lv_obj_t *name_info_label = lv_obj_create(info_container);
        lv_obj_set_size(name_info_label, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(name_info_label, lv_color_hex(0x1E1E1E), 0);
        lv_obj_set_style_border_width(name_info_label, 0, 0);
        lv_obj_set_style_radius(name_info_label, 4, 0);
        lv_obj_set_style_pad_all(name_info_label, 6, 0);
        lv_obj_set_flex_flow(name_info_label, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(name_info_label, 2, 0);
        
        lv_obj_t *name_title = lv_label_create(name_info_label);
        lv_label_set_text(name_title, LV_SYMBOL_EDIT " Device Name");
        lv_obj_set_style_text_color(name_title, lv_color_hex(0x00CED1), 0);
        lv_obj_set_style_text_font(name_title, &lv_font_montserrat_10, 0);
        
        lv_obj_t *name_value = lv_label_create(name_info_label);
        lv_label_set_text(name_value, device->name);
        lv_obj_set_style_text_color(name_value, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(name_value, &lv_font_montserrat_12, 0);
        lv_obj_set_width(name_value, LV_PCT(100));
        lv_label_set_long_mode(name_value, LV_LABEL_LONG_WRAP);
    }
    
    // Type
    lv_obj_t *type_label = lv_label_create(info_container);
    char type_text[50];
    snprintf(type_text, sizeof(type_text), "Type: %s", get_ble_device_type_name(device->type));
    lv_label_set_text(type_label, type_text);
    lv_obj_set_style_text_color(type_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(type_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(type_label, LV_PCT(100));
    
    // Manufacturer (if available)
    if (device->company_id != 0) {
        lv_obj_t *mfr_label = lv_label_create(info_container);
        char mfr_text[50];
        const char *mfr_name = "Unknown";
        if (device->company_id == 0x004C) mfr_name = "Apple Inc.";
        else if (device->company_id == 0x0075) mfr_name = "Samsung Electronics";
        else if (device->company_id == 0x00E0) mfr_name = "Google LLC";
        else if (device->company_id == 0x0006) mfr_name = "Microsoft";
        else if (device->company_id == 0x000F) mfr_name = "Broadcom";
        else if (device->company_id == 0x0087) mfr_name = "Garmin";
        else if (device->company_id == 0x02E5) mfr_name = "Fitbit";
        
        snprintf(mfr_text, sizeof(mfr_text), "Manufacturer: %s (0x%04X)", mfr_name, device->company_id);
        lv_label_set_text(mfr_label, mfr_text);
        lv_obj_set_style_text_color(mfr_label, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(mfr_label, &lv_font_montserrat_12, 0);
        lv_obj_set_width(mfr_label, LV_PCT(100));
        lv_label_set_long_mode(mfr_label, LV_LABEL_LONG_WRAP);
    }
    
    // Appearance (if available)
    if (device->appearance != 0) {
        lv_obj_t *app_label = lv_label_create(info_container);
        char app_text[50];
        snprintf(app_text, sizeof(app_text), "Appearance: 0x%04X", device->appearance);
        lv_label_set_text(app_label, app_text);
        lv_obj_set_style_text_color(app_label, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(app_label, &lv_font_montserrat_12, 0);
        lv_obj_set_width(app_label, LV_PCT(100));
    }
    
    // MAC
    lv_obj_t *mac_label = lv_label_create(info_container);
    char mac_text[50];
    char mac_str[20];
    format_ble_address(device->addr, mac_str, sizeof(mac_str));
    snprintf(mac_text, sizeof(mac_text), "MAC: %s", mac_str);
    lv_label_set_text(mac_label, mac_text);
    lv_obj_set_style_text_color(mac_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(mac_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(mac_label, LV_PCT(100));
    
    // RSSI with color
    lv_obj_t *rssi_label = lv_label_create(info_container);
    char rssi_text[50];
    const char *signal_strength = device->rssi > -60 ? "Excellent" :
                                  device->rssi > -70 ? "Good" :
                                  device->rssi > -80 ? "Fair" : "Poor";
    uint32_t rssi_color = device->rssi > -60 ? 0x4CAF50 :
                          device->rssi > -80 ? 0xFFC107 : 0xFF5252;
    snprintf(rssi_text, sizeof(rssi_text), "Signal: %d dBm (%s)", device->rssi, signal_strength);
    lv_label_set_text(rssi_label, rssi_text);
    lv_obj_set_style_text_color(rssi_label, lv_color_hex(rssi_color), 0);
    lv_obj_set_style_text_font(rssi_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(rssi_label, LV_PCT(100));
    
    // Connectable status
    lv_obj_t *conn_label = lv_label_create(info_container);
    snprintf(mac_text, sizeof(mac_text), "Connectable: %s", device->connectable ? "Yes" : "No");
    lv_label_set_text(conn_label, mac_text);
    lv_obj_set_style_text_color(conn_label, device->connectable ? lv_color_hex(0x4CAF50) : lv_color_hex(0xFF5252), 0);
    lv_obj_set_style_text_font(conn_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(conn_label, LV_PCT(100));
    
    // Action buttons
    lv_obj_t *actions_title = lv_label_create(content);
    lv_label_set_text(actions_title, "Actions:");
    lv_obj_set_style_text_color(actions_title, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(actions_title, &lv_font_montserrat_10, 0);
    
    // Scan Services button
    btn_config_t scan_cfg = {
        .text = "Scan Services",
        .icon = LV_SYMBOL_LIST,
        .style = BTN_STYLE_SECONDARY,
        .width = LV_PCT(90),
        .height = 35,
        .callback = scan_services_cb,
        .user_data = NULL
    };
    qc_create_button(content, &scan_cfg);
    
    // Communication/Control section header
    lv_obj_t *comm_header = lv_label_create(content);
    lv_label_set_text(comm_header, LV_SYMBOL_CALL " Device Control");
    lv_obj_set_style_text_font(comm_header, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(comm_header, lv_color_hex(0x00CED1), 0);
    lv_obj_set_style_pad_top(comm_header, 10, 0);
    lv_obj_set_style_pad_bottom(comm_header, 5, 0);
    
    // Find Device (Send Alert) button
    btn_config_t alert_cfg = {
        .text = "Find Device (Alert)",
        .icon = LV_SYMBOL_BELL,
        .style = BTN_STYLE_SECONDARY,
        .width = LV_PCT(90),
        .height = 35,
        .callback = send_alert_to_device_cb,
        .user_data = NULL
    };
    qc_create_button(content, &alert_cfg);
    
    // Set Time button
    btn_config_t time_cfg = {
        .text = "Sync Time",
        .icon = LV_SYMBOL_REFRESH,
        .style = BTN_STYLE_SECONDARY,
        .width = LV_PCT(90),
        .height = 35,
        .callback = set_time_on_device_cb,
        .user_data = NULL
    };
    qc_create_button(content, &time_cfg);
    
    // Ring Device button
    btn_config_t ring_cfg = {
        .text = "Ring Device",
        .icon = LV_SYMBOL_VOLUME_MAX,
        .style = BTN_STYLE_SECONDARY,
        .width = LV_PCT(90),
        .height = 35,
        .callback = ring_device_cb,
        .user_data = NULL
    };
    qc_create_button(content, &ring_cfg);
    
    // Generic Write button (advanced)
    btn_config_t write_cfg = {
        .text = "Write to Characteristic",
        .icon = LV_SYMBOL_EDIT,
        .style = BTN_STYLE_SECONDARY,
        .width = LV_PCT(90),
        .height = 35,
        .callback = generic_write_cb,
        .user_data = NULL
    };
    qc_create_button(content, &write_cfg);
    
    // Connect/Disconnect button (only if connectable)
    if (device->connectable) {
        // Check if already connected to this device
        bool is_connected = bluetooth_scanner_is_connected();
        uint8_t connected_addr[6] = {0};
        bool is_this_device = false;
        
        if (is_connected && bluetooth_scanner_get_connected_addr(connected_addr)) {
            is_this_device = (memcmp(connected_addr, device->addr, 6) == 0);
        }
        
        btn_config_t connect_cfg = {
            .text = is_this_device ? "Disconnect" : "Connect",
            .icon = is_this_device ? LV_SYMBOL_CLOSE : LV_SYMBOL_WIFI,
            .style = is_this_device ? BTN_STYLE_DANGER : BTN_STYLE_SUCCESS,
            .width = LV_PCT(90),
            .height = 35,
            .callback = is_this_device ? [](lv_event_t *e) {
                play_haptic_click();
                bluetooth_scanner_disconnect();
                show_toast("Disconnected", 2000);
                // Close dialog to refresh
                detail_back_btn_cb(e);
            } : connect_device_cb,
            .user_data = NULL
        };
        qc_create_button(content, &connect_cfg);
    }
    
    // Read Manufacturer button
    btn_config_t mfg_cfg = {
        .text = "Manufacturer Data",
        .icon = LV_SYMBOL_EYE_OPEN,
        .style = BTN_STYLE_GHOST,
        .width = LV_PCT(90),
        .height = 35,
        .callback = read_manufacturer_cb,
        .user_data = NULL
    };
    qc_create_button(content, &mfg_cfg);
    
    detail_dialog_open = true;
}

// Device clicked callback
static void device_clicked_cb(lv_event_t *e)
{
    play_haptic_click();
    
    ble_device_t *device = (ble_device_t *)lv_event_get_user_data(e);
    if (device == NULL) return;
    
    ESP_LOGI(TAG, "Device clicked: %s", device->name);
    open_device_detail_dialog(device);
}

// Refresh device list
static void refresh_device_list(void)
{
    if (!dialog_open || device_list == NULL)
        return;
    
    // Save current scroll position to prevent auto-scroll to top
    lv_coord_t scroll_y = lv_obj_get_scroll_y(device_list);
    
    // Clear existing list
    lv_obj_clean(device_list);
    
    // Get devices into PERSISTENT array (fixes dangling pointer!)
    ui_device_count = bluetooth_scanner_get_devices(ui_devices, MAX_BLE_DEVICES);
    
    if (ui_device_count == 0)
    {
        // No devices found
        lv_obj_t *no_devices = lv_label_create(device_list);
        lv_label_set_text(no_devices, "No devices found\nStart scan to discover BLE devices");
        lv_obj_set_style_text_align(no_devices, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(no_devices, lv_color_hex(0x888888), 0);
        lv_obj_center(no_devices);
        return;
    }
    
    // Add devices to list
    for (int i = 0; i < ui_device_count; i++)
    {
        ble_device_t *device = &ui_devices[i];  // Now points to persistent memory!
        
        // Create card for device
        lv_obj_t *card = lv_obj_create(device_list);
        lv_obj_set_size(card, LV_PCT(95), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x444444), 0);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(card, 4, 0);
        
        // Row 1: Icon + Name
        lv_obj_t *name_label = lv_label_create(card);
        char name_text[64];
        const char *icon = get_device_icon(device->type);
        snprintf(name_text, sizeof(name_text), "%s %s", icon, device->name);
        lv_label_set_text(name_label, name_text);
        lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(name_label, LV_PCT(100));
        
        // Row 2: Type and Connectable badge
        lv_obj_t *info_row = lv_obj_create(card);
        lv_obj_set_size(info_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(info_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(info_row, 0, 0);
        lv_obj_set_style_pad_all(info_row, 0, 0);
        lv_obj_set_flex_flow(info_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(info_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        
        lv_obj_t *type_label = lv_label_create(info_row);
        lv_label_set_text(type_label, get_ble_device_type_name(device->type));
        lv_obj_set_style_text_color(type_label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(type_label, &lv_font_montserrat_10, 0);
        
        if (device->connectable)
        {
            lv_obj_t *sep = lv_label_create(info_row);
            lv_label_set_text(sep, " • ");
            lv_obj_set_style_text_color(sep, lv_color_hex(0x666666), 0);
            lv_obj_set_style_text_font(sep, &lv_font_montserrat_10, 0);
            
            lv_obj_t *conn_label = lv_label_create(info_row);
            lv_label_set_text(conn_label, "Connectable");
            lv_obj_set_style_text_color(conn_label, lv_color_hex(0x4CAF50), 0);
            lv_obj_set_style_text_font(conn_label, &lv_font_montserrat_10, 0);
        }
        
        // Row 3: MAC Address
        lv_obj_t *mac_label = lv_label_create(card);
        char mac_str[20];
        format_ble_address(device->addr, mac_str, sizeof(mac_str));
        lv_label_set_text(mac_label, mac_str);
        lv_obj_set_style_text_color(mac_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(mac_label, &lv_font_montserrat_10, 0);
        
        // Row 4: RSSI with colored indicator
        lv_obj_t *rssi_label = lv_label_create(card);
        char rssi_text[32];
        const char *signal_icon = device->rssi > -60 ? LV_SYMBOL_WIFI : 
                                  device->rssi > -80 ? "≈" : "~";
        uint32_t rssi_color = device->rssi > -60 ? 0x4CAF50 :  // Green
                             device->rssi > -80 ? 0xFFC107 :   // Yellow
                                                  0xFF5252;    // Red
        snprintf(rssi_text, sizeof(rssi_text), "%s %d dBm", signal_icon, device->rssi);
        lv_label_set_text(rssi_label, rssi_text);
        lv_obj_set_style_text_color(rssi_label, lv_color_hex(rssi_color), 0);
        lv_obj_set_style_text_font(rssi_label, &lv_font_montserrat_10, 0);
        
        // Make card clickable
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, device_clicked_cb, LV_EVENT_CLICKED, &ui_devices[i]);  // Use persistent array!
    }
    
    // Restore scroll position to prevent jumping back to top
    lv_obj_scroll_to_y(device_list, scroll_y, LV_ANIM_OFF);
    
    // Update status (just show count in parentheses)
    char status_text[16];
    snprintf(status_text, sizeof(status_text), "(%d)", ui_device_count);
    lv_label_set_text(status_label, status_text);
}

// Timer callback for auto-refresh
static void refresh_timer_cb(lv_timer_t *timer)
{
    if (bluetooth_scanner_is_scanning())
    {
        refresh_device_list();
    }
}

// Start scan button callback
static void start_scan_cb(lv_event_t *e)
{
    play_haptic_click();
    
    bluetooth_scanner_start();
    
    lv_obj_add_flag(scan_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);
    
    // Status will update via refresh_device_list() showing count
    // No need to set "Scanning..." text
    
    ESP_LOGI(TAG, "BLE scan started");
}

// Stop scan button callback
static void stop_scan_cb(lv_event_t *e)
{
    play_haptic_click();
    
    bluetooth_scanner_stop();
    
    lv_obj_clear_flag(scan_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);
    
    // Keep showing device count, don't change status text
    // Status will update via refresh_device_list()
    
    ESP_LOGI(TAG, "BLE scan stopped");
}

// Back button callback
static void back_btn_cb(lv_event_t *e)
{
    play_haptic_click();
    close_bluetooth_dialog();
}

// Forward declaration
static void open_bluetooth_dialog_after_reduce(void);

// Async callback to reduce buffers (runs when LVGL is idle)
static void reduce_buffers_async(void *user_data)
{
    if (lvgl_reduce_buffers() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reduce LVGL buffers");
        show_toast("Memory optimization failed", 2000);
        return;
    }
    
    ESP_LOGI(TAG, "LVGL buffers reduced successfully - continuing with BT init");
    
    // Now continue with dialog opening
    open_bluetooth_dialog_after_reduce();
}

void open_bluetooth_dialog(void)
{
    if (dialog_open)
        return;
    
    ESP_LOGI(TAG, "Opening Bluetooth dialog - deferring buffer reduction to LVGL thread");
    
    // Reduce buffers asynchronously and continue after completion
    lv_async_call(reduce_buffers_async, NULL);
}

// Continue opening dialog after buffer reduction completes
static void open_bluetooth_dialog_after_reduce(void)
{
    ESP_LOGI(TAG, "Buffers reduced - initializing Bluetooth");
    
    // Allocate characteristic storage
    if (global_char_storage == NULL) {
        // Allocate in PSRAM to avoid fragmenting internal RAM
        global_char_storage = (ble_gatt_char_t*)heap_caps_malloc(
            sizeof(ble_gatt_char_t) * MAX_TOTAL_CHARS,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        
        if (global_char_storage == NULL) {
            ESP_LOGW(TAG, "PSRAM allocation failed, trying regular malloc");
            global_char_storage = (ble_gatt_char_t*)malloc(sizeof(ble_gatt_char_t) * MAX_TOTAL_CHARS);
        }
        
        global_char_count = 0;
        if (global_char_storage == NULL) {
            ESP_LOGE(TAG, "Failed to allocate characteristic storage (%d bytes)", 
                     sizeof(ble_gatt_char_t) * MAX_TOTAL_CHARS);
            show_toast("Low memory - try closing apps", 2000);
            return;
        }
        ESP_LOGI(TAG, "Allocated %d bytes for characteristic storage in PSRAM", 
                 sizeof(ble_gatt_char_t) * MAX_TOTAL_CHARS);
    }
    
    // Initialize Bluetooth scanner
    esp_err_t ret = bluetooth_scanner_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize Bluetooth scanner: %s", esp_err_to_name(ret));
        
        if (ret == ESP_ERR_NO_MEM)
        {
            show_toast("Not enough memory for BLE", 3000);
        }
        else
        {
            show_toast("BLE init failed", 2000);
        }
        return;
    }
    
    ESP_LOGI(TAG, "Bluetooth scanner initialized successfully");
    
    // Create full-screen container
    bluetooth_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bluetooth_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bluetooth_screen, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(bluetooth_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bluetooth_screen, 0, 0);
    lv_obj_set_style_pad_all(bluetooth_screen, 0, 0);
    lv_obj_clear_flag(bluetooth_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header
    lv_obj_t *header = lv_obj_create(bluetooth_screen);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0D47A1), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 10, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_BLUETOOTH " BLE Scanner");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Back button
    btn_config_t back_cfg = {
        .text = LV_SYMBOL_LEFT,
        .icon = NULL,
        .style = BTN_STYLE_PRIMARY,
        .width = 40,
        .height = 30,
        .callback = back_btn_cb,
        .user_data = NULL
    };
    lv_obj_t *back_btn = qc_create_button(header, &back_cfg);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // Status and button row container
    lv_obj_t *status_row = lv_obj_create(bluetooth_screen);
    lv_obj_set_size(status_row, LV_PCT(95), 40);
    lv_obj_align(status_row, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_style_pad_all(status_row, 0, 0);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Status label (left side)
    status_label = lv_label_create(status_row);
    lv_label_set_text(status_label, "(0)");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_left(status_label, 5, 0);
    
    // Button container (right side)
    lv_obj_t *btn_container = lv_obj_create(status_row);
    lv_obj_set_size(btn_container, LV_PCT(60), 40);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_set_style_pad_all(btn_container, 0, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Start scan button
    btn_config_t start_cfg = {
        .text = "Start Scan",
        .icon = LV_SYMBOL_REFRESH,
        .style = BTN_STYLE_SUCCESS,
        .width = LV_PCT(80),
        .height = 35,
        .callback = start_scan_cb,
        .user_data = NULL
    };
    scan_btn = qc_create_button(btn_container, &start_cfg);
    
    // Stop scan button
    btn_config_t stop_cfg = {
        .text = "Stop",
        .icon = LV_SYMBOL_STOP,
        .style = BTN_STYLE_DANGER,
        .width = LV_PCT(80),
        .height = 35,
        .callback = stop_scan_cb,
        .user_data = NULL
    };
    stop_btn = qc_create_button(btn_container, &stop_cfg);
    lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);  // Hidden initially
    
    // Device list container (moved up for more space)
    device_list = lv_obj_create(bluetooth_screen);
    lv_obj_set_size(device_list, LV_PCT(95), 195);  // Increased height (was 140, now 195)
    lv_obj_align(device_list, LV_ALIGN_TOP_MID, 0, 100);  // Moved up (was 140, now 100)
    lv_obj_set_style_bg_color(device_list, lv_color_hex(0x2A2A2A), 0);  // Slightly lighter than background
    lv_obj_set_style_bg_opa(device_list, LV_OPA_COVER, 0);  // Ensure it's visible
    lv_obj_set_style_border_width(device_list, 1, 0);
    lv_obj_set_style_border_color(device_list, lv_color_hex(0x444444), 0);
    lv_obj_set_style_pad_all(device_list, 5, 0);
    lv_obj_set_flex_flow(device_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(device_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(device_list, 5, 0);
    lv_obj_set_scrollbar_mode(device_list, LV_SCROLLBAR_MODE_AUTO);  // Only show scrollbar when needed
    
    // Initial empty state
    lv_obj_t *empty_label = lv_label_create(device_list);
    lv_label_set_text(empty_label, "No devices found");
    lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(empty_label, lv_color_hex(0x888888), 0);
    lv_obj_center(empty_label);
    
    // Create refresh timer (update list every 2 seconds during scan - slower to reduce load)
    refresh_timer = lv_timer_create(refresh_timer_cb, 2000, NULL);
    
    dialog_open = true;
    
    ESP_LOGI(TAG, "Bluetooth dialog opened");
}

// Background task to deinit Bluetooth (prevents UI freeze)
static void bluetooth_deinit_task(void *param)
{
    ESP_LOGI(TAG, "Deinitializing Bluetooth in background...");
    bluetooth_scanner_deinit();
    ESP_LOGI(TAG, "Bluetooth deinitialized");
    
    // Wait for BT stack to fully release memory
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Perform heap defragmentation to create larger contiguous blocks
    ESP_LOGI(TAG, "Defragmenting heap for buffer restoration...");
    
    // Allocate progressively larger blocks to encourage coalescing
    void *blocks[10];
    int count = 0;
    for (int i = 0; i < 10; i++) {
        size_t size = (i + 1) * 16384; // 16KB, 32KB, 48KB... up to 160KB
        blocks[i] = heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (blocks[i]) count++;
        else break;
    }
    
    // Free in reverse to encourage large block formation
    for (int i = count - 1; i >= 0; i--) {
        if (blocks[i]) {
            heap_caps_free(blocks[i]);
        }
    }
    
    // Wait for heap coalescing
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Log memory state
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "After defrag: Free=%zu KB, Largest=%zu KB", free_dma/1024, largest/1024);
    
    // Restore LVGL buffers to full size for smooth UI
    ESP_LOGI(TAG, "Restoring LVGL buffers to full size...");
    esp_err_t restore_result = lvgl_restore_buffers();
    if (restore_result != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore buffers fully - UI may be slower");
    }
    
    vTaskDelete(NULL);
}

void close_bluetooth_dialog(void)
{
    if (!dialog_open || bluetooth_screen == NULL)
        return;
    
    ESP_LOGI(TAG, "Closing Bluetooth dialog");
    
    // Stop any ongoing scan immediately
    bluetooth_scanner_stop();
    
    // Free characteristic storage
    if (global_char_storage != NULL) {
        free(global_char_storage);
        global_char_storage = NULL;
        global_char_count = 0;
    }
    
    // Try foreground deinit first
    // bluetooth_deinit_task(NULL);
    // Deinit Bluetooth in background task to avoid freezing UI (takes ~200ms)
    xTaskCreate(bluetooth_deinit_task, "bt_deinit", 3056, NULL, 5, NULL); // Increased stack for defrag code
    vTaskDelay(pdMS_TO_TICKS(3000));
    // Delete refresh timer
    if (refresh_timer)
    {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }
    
    // Slide-down animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bluetooth_screen);
    lv_anim_set_values(&a, 0, LV_VER_RES);
    lv_anim_set_time(&a, 200);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)(void(*)(void*, int32_t))lv_obj_set_y);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_deleted_cb(&a, [](lv_anim_t *a) {
        lv_obj_del((lv_obj_t *)a->var);
    });
    lv_anim_start(&a);
    
    bluetooth_screen = NULL;
    device_list = NULL;
    status_label = NULL;
    scan_btn = NULL;
    stop_btn = NULL;
    dialog_open = false;
    
    ESP_LOGI(TAG, "Bluetooth dialog closed");
}

