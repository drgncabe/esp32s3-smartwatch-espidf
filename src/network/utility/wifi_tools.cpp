#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network/utility/wifi_tools.h"

/**
 * @brief Override ESP-IDF's frame sanity check to allow raw frame transmission
 * This function replaces the internal WiFi stack validation to bypass restrictions
 * on sending raw 802.11 frames (deauth, beacons, etc.). It applies globally to all
 * WiFi operations in this project.
 */
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}

static const char *TAG = "wifi_pentest";

#define HOP_TASK_STACK_SIZE 2560

// Common vendor OUI database (first 3 bytes of MAC)
// Format: OUI bytes, vendor name
typedef struct {
    uint8_t oui[3];
    const char *vendor;
} oui_vendor_t;

static const oui_vendor_t oui_vendors[] = {
    {{0x00, 0x03, 0x93}, "Apple"},
    {{0x00, 0x0A, 0x95}, "Apple"},
    {{0x00, 0x1C, 0xB3}, "Apple"},
    {{0x00, 0x1E, 0x52}, "Apple"},
    {{0x00, 0x1F, 0x5B}, "Apple"},
    {{0x00, 0x21, 0xE9}, "Apple"},
    {{0x00, 0x23, 0x12}, "Apple"},
    {{0x00, 0x23, 0x32}, "Apple"},
    {{0x00, 0x23, 0xDF}, "Apple"},
    {{0x00, 0x25, 0x00}, "Apple"},
    {{0x00, 0x25, 0x4B}, "Apple"},
    {{0x00, 0x25, 0xBC}, "Apple"},
    {{0x00, 0x26, 0x08}, "Apple"},
    {{0x00, 0x26, 0x4A}, "Apple"},
    {{0x00, 0x26, 0xB0}, "Apple"},
    {{0x00, 0x26, 0xBB}, "Apple"},
    {{0x04, 0x0C, 0xCE}, "Apple"},
    {{0x04, 0x15, 0x52}, "Apple"},
    {{0x08, 0x00, 0x07}, "Apple"},
    {{0x0C, 0x3E, 0x9F}, "Apple"},
    {{0x0C, 0x74, 0xC2}, "Apple"},
    {{0x10, 0x9A, 0xDD}, "Apple"},
    {{0x10, 0xDD, 0xB1}, "Apple"},
    {{0x14, 0x10, 0x9F}, "Apple"},
    {{0x14, 0x5A, 0x05}, "Apple"},
    {{0x18, 0xE7, 0xF4}, "Apple"},
    {{0x1C, 0x36, 0xBB}, "Apple"},
    {{0x20, 0xC9, 0xD0}, "Apple"},
    {{0x24, 0xA0, 0x74}, "Apple"},
    {{0x28, 0xE1, 0x4C}, "Apple"},
    {{0x2C, 0xBE, 0x08}, "Apple"},
    {{0x30, 0xF7, 0xC5}, "Apple"},
    {{0x34, 0xC0, 0x59}, "Apple"},
    {{0x38, 0xC9, 0x86}, "Apple"},
    {{0x3C, 0x2E, 0xF9}, "Apple"},
    {{0x40, 0xA6, 0xD9}, "Apple"},
    {{0x44, 0xD8, 0x84}, "Apple"},
    {{0x48, 0xA1, 0x95}, "Apple"},
    {{0x4C, 0x57, 0xCA}, "Apple"},
    {{0x50, 0xEA, 0xD6}, "Apple"},
    {{0x54, 0x72, 0x4F}, "Apple"},
    {{0x58, 0xB0, 0x35}, "Apple"},
    {{0x5C, 0x95, 0xAE}, "Apple"},
    {{0x60, 0x69, 0x44}, "Apple"},
    {{0x60, 0xF8, 0x1D}, "Apple"},
    {{0x64, 0xA3, 0xCB}, "Apple"},
    {{0x68, 0xAE, 0x20}, "Apple"},
    {{0x6C, 0x4D, 0x73}, "Apple"},
    {{0x6C, 0x70, 0x9F}, "Apple"},
    {{0x70, 0xCD, 0x60}, "Apple"},
    {{0x74, 0xE1, 0xB6}, "Apple"},
    {{0x78, 0x3A, 0x84}, "Apple"},
    {{0x78, 0xD7, 0x5F}, "Apple"},
    {{0x7C, 0xC3, 0xA1}, "Apple"},
    {{0x80, 0x92, 0x9F}, "Apple"},
    {{0x80, 0xE6, 0x50}, "Apple"},
    {{0x84, 0x78, 0xAC}, "Apple"},
    {{0x88, 0x53, 0x95}, "Apple"},
    {{0x8C, 0x85, 0x90}, "Apple"},
    {{0x90, 0x84, 0x0D}, "Apple"},
    {{0x94, 0x94, 0x26}, "Apple"},
    {{0x98, 0xF0, 0xAB}, "Apple"},
    {{0x9C, 0x20, 0x7B}, "Apple"},
    {{0xA0, 0x99, 0x9B}, "Apple"},
    {{0xA4, 0x5E, 0x60}, "Apple"},
    {{0xA4, 0xD1, 0x8C}, "Apple"},
    {{0xA8, 0x66, 0x7F}, "Apple"},
    {{0xAC, 0x3C, 0x0B}, "Apple"},
    {{0xB0, 0x65, 0xBD}, "Apple"},
    {{0xB4, 0xF0, 0xAB}, "Apple"},
    {{0xB8, 0x09, 0x8A}, "Apple"},
    {{0xB8, 0x78, 0x2E}, "Apple"},
    {{0xBC, 0x3B, 0xAF}, "Apple"},
    {{0xBC, 0x9F, 0xEF}, "Apple"},
    {{0xC0, 0xCE, 0xCD}, "Apple"},
    {{0xC4, 0x2C, 0x03}, "Apple"},
    {{0xC8, 0x2A, 0x14}, "Apple"},
    {{0xC8, 0xB5, 0xB7}, "Apple"},
    {{0xCC, 0x08, 0x8D}, "Apple"},
    {{0xD0, 0x03, 0x4B}, "Apple"},
    {{0xD0, 0xE1, 0x40}, "Apple"},
    {{0xD4, 0x9A, 0x20}, "Apple"},
    {{0xD8, 0x00, 0x4D}, "Apple"},
    {{0xD8, 0xCF, 0x9C}, "Apple"},
    {{0xDC, 0x2B, 0x2A}, "Apple"},
    {{0xE0, 0xB5, 0x2D}, "Apple"},
    {{0xE4, 0xE4, 0xAB}, "Apple"},
    {{0xE8, 0x80, 0x2E}, "Apple"},
    {{0xEC, 0x35, 0x86}, "Apple"},
    {{0xF0, 0xB4, 0x79}, "Apple"},
    {{0xF0, 0xDB, 0xE2}, "Apple"},
    {{0xF4, 0xF9, 0x51}, "Apple"},
    {{0xF8, 0x1E, 0xDF}, "Apple"},
    {{0xFC, 0x25, 0x3F}, "Apple"},
    
    {{0x00, 0x50, 0xF2}, "Microsoft"},
    {{0x00, 0x0D, 0x3A}, "Microsoft"},
    {{0x00, 0x12, 0x5A}, "Microsoft"},
    {{0x00, 0x15, 0x5D}, "Microsoft"},
    {{0x00, 0x17, 0xFA}, "Microsoft"},
    {{0x00, 0x1D, 0xD8}, "Microsoft"},
    {{0x00, 0x22, 0x48}, "Microsoft"},
    {{0x00, 0x24, 0xD7}, "Microsoft"},
    {{0x00, 0x25, 0xAE}, "Microsoft"},
    {{0x7C, 0xED, 0x8D}, "Microsoft"},
    {{0xD0, 0x57, 0x7B}, "Microsoft"},
    
    {{0x00, 0x13, 0x10}, "Linksys"},
    {{0x00, 0x14, 0xBF}, "Linksys"},
    {{0x00, 0x18, 0x39}, "Linksys"},
    {{0x00, 0x18, 0xF8}, "Linksys"},
    {{0x00, 0x1A, 0x70}, "Linksys"},
    {{0x00, 0x1C, 0x10}, "Linksys"},
    {{0x00, 0x1D, 0x7E}, "Linksys"},
    {{0x00, 0x1E, 0xE5}, "Linksys"},
    {{0x00, 0x21, 0x29}, "Linksys"},
    {{0x00, 0x22, 0x6B}, "Linksys"},
    {{0x00, 0x23, 0x69}, "Linksys"},
    {{0x00, 0x25, 0x9C}, "Linksys"},
    {{0x68, 0x7F, 0x74}, "Linksys"},
    {{0xC0, 0xC1, 0xC0}, "Linksys"},
    
    {{0x00, 0x04, 0x20}, "Cisco"},
    {{0x00, 0x06, 0x2A}, "Cisco"},
    {{0x00, 0x0A, 0x41}, "Cisco"},
    {{0x00, 0x0B, 0x45}, "Cisco"},
    {{0x00, 0x0C, 0x30}, "Cisco"},
    {{0x00, 0x0D, 0x28}, "Cisco"},
    {{0x00, 0x0E, 0x38}, "Cisco"},
    {{0x00, 0x0F, 0x23}, "Cisco"},
    {{0x00, 0x10, 0x11}, "Cisco"},
    {{0x00, 0x11, 0x20}, "Cisco"},
    {{0x00, 0x11, 0x92}, "Cisco"},
    {{0x00, 0x12, 0x00}, "Cisco"},
    {{0x00, 0x12, 0x43}, "Cisco"},
    {{0x00, 0x12, 0x7F}, "Cisco"},
    {{0x00, 0x12, 0xD9}, "Cisco"},
    {{0x00, 0x13, 0x19}, "Cisco"},
    {{0x00, 0x13, 0x5F}, "Cisco"},
    {{0x00, 0x13, 0xC3}, "Cisco"},
    {{0x00, 0x14, 0x1B}, "Cisco"},
    {{0x00, 0x14, 0x69}, "Cisco"},
    {{0x00, 0x14, 0xA8}, "Cisco"},
    {{0x00, 0x14, 0xF1}, "Cisco"},
    
    {{0x00, 0x1B, 0x63}, "Samsung"},
    {{0x00, 0x1C, 0x43}, "Samsung"},
    {{0x00, 0x1D, 0x25}, "Samsung"},
    {{0x00, 0x1E, 0x7D}, "Samsung"},
    {{0x00, 0x1E, 0xE1}, "Samsung"},
    {{0x00, 0x1F, 0xCD}, "Samsung"},
    {{0x00, 0x21, 0x4C}, "Samsung"},
    {{0x00, 0x23, 0x39}, "Samsung"},
    {{0x00, 0x23, 0xD6}, "Samsung"},
    {{0x00, 0x24, 0x54}, "Samsung"},
    {{0x00, 0x24, 0x90}, "Samsung"},
    {{0x00, 0x24, 0xE9}, "Samsung"},
    {{0x00, 0x25, 0x66}, "Samsung"},
    {{0x00, 0x26, 0x37}, "Samsung"},
    {{0x00, 0x26, 0x5D}, "Samsung"},
    {{0x00, 0x26, 0xE2}, "Samsung"},
    {{0x18, 0x3F, 0x47}, "Samsung"},
    {{0x1C, 0x62, 0xB8}, "Samsung"},
    {{0x34, 0x23, 0xBA}, "Samsung"},
    {{0x38, 0xAA, 0x3C}, "Samsung"},
    {{0x3C, 0x8B, 0xFE}, "Samsung"},
    {{0x40, 0x0E, 0x85}, "Samsung"},
    {{0x44, 0x4E, 0x1A}, "Samsung"},
    {{0x50, 0x32, 0x37}, "Samsung"},
    {{0x5C, 0x0A, 0x5B}, "Samsung"},
    {{0x74, 0x45, 0xCE}, "Samsung"},
    {{0x78, 0x1F, 0xDB}, "Samsung"},
    {{0x80, 0x57, 0x19}, "Samsung"},
    {{0x84, 0x25, 0x3F}, "Samsung"},
    {{0xA0, 0x0B, 0xBA}, "Samsung"},
    {{0xA4, 0xEB, 0xD3}, "Samsung"},
    {{0xBC, 0x14, 0x85}, "Samsung"},
    {{0xBC, 0x20, 0xBA}, "Samsung"},
    {{0xBC, 0x47, 0x60}, "Samsung"},
    {{0xC8, 0x19, 0xF7}, "Samsung"},
    {{0xCC, 0x07, 0xAB}, "Samsung"},
    {{0xD0, 0x17, 0x6A}, "Samsung"},
    {{0xD0, 0x22, 0xBE}, "Samsung"},
    {{0xD4, 0x87, 0xD8}, "Samsung"},
    {{0xE8, 0x03, 0x9A}, "Samsung"},
    {{0xEC, 0x1F, 0x72}, "Samsung"},
    {{0xF4, 0x7B, 0x5E}, "Samsung"},
    {{0xFC, 0x00, 0x12}, "Samsung"},
    
    {{0x00, 0x1A, 0x11}, "Google"},
    {{0xDA, 0xA1, 0x19}, "Google"},
    {{0xF4, 0xF5, 0xD8}, "Google"},
    {{0x54, 0x60, 0x09}, "Google"},
    {{0x3C, 0x5A, 0xB4}, "Google"},
    {{0xCC, 0x3A, 0x61}, "Google"},
    
    {{0x00, 0x1F, 0x3A}, "Amazon"},
    {{0x00, 0x27, 0x0E}, "Amazon"},
    {{0x38, 0xF7, 0x3D}, "Amazon"},
    {{0x44, 0x65, 0x0D}, "Amazon"},
    {{0x50, 0xDC, 0xE7}, "Amazon"},
    {{0x74, 0xC2, 0x46}, "Amazon"},
    {{0xB4, 0x7C, 0x9C}, "Amazon"},
    {{0xFC, 0xA6, 0x67}, "Amazon"},
    
    {{0x00, 0x19, 0x5B}, "LG"},
    {{0x00, 0x1C, 0x62}, "LG"},
    {{0x00, 0x1E, 0x75}, "LG"},
    {{0x00, 0x1F, 0x6B}, "LG"},
    {{0x00, 0x21, 0xFB}, "LG"},
    {{0x00, 0x22, 0xA9}, "LG"},
    {{0x00, 0x24, 0x83}, "LG"},
    {{0x00, 0x26, 0xE2}, "LG"},
    {{0x10, 0x68, 0x3F}, "LG"},
    {{0x18, 0x1E, 0xB0}, "LG"},
    {{0x40, 0x5D, 0x82}, "LG"},
    {{0x68, 0xEB, 0xAE}, "LG"},
    {{0xA0, 0x91, 0x69}, "LG"},
    
    {{0x00, 0x1A, 0xA2}, "Sony"},
    {{0x00, 0x1D, 0xBA}, "Sony"},
    {{0x00, 0x1E, 0x3D}, "Sony"},
    {{0x00, 0x23, 0x8A}, "Sony"},
    {{0x00, 0x24, 0xBE}, "Sony"},
    {{0x08, 0x86, 0x3B}, "Sony"},
    {{0x54, 0xE6, 0xFC}, "Sony"},
    {{0xAC, 0x9B, 0x0A}, "Sony"},
    
    {{0x00, 0x0C, 0x76}, "Hon Hai/Foxconn"},
    {{0x00, 0x15, 0xAF}, "Hon Hai/Foxconn"},
    {{0x00, 0x17, 0xC9}, "Hon Hai/Foxconn"},
    {{0x00, 0x1B, 0xFC}, "Hon Hai/Foxconn"},
    {{0x00, 0x1E, 0xC2}, "Hon Hai/Foxconn"},
    {{0x00, 0x24, 0x1D}, "Hon Hai/Foxconn"},
    
    {{0x00, 0x13, 0xCE}, "TP-Link"},
    {{0x00, 0x1D, 0x0F}, "TP-Link"},
    {{0x00, 0x27, 0x19}, "TP-Link"},
    {{0x14, 0xCF, 0x92}, "TP-Link"},
    {{0x1C, 0x3B, 0xF3}, "TP-Link"},
    {{0x30, 0xB5, 0xC2}, "TP-Link"},
    {{0x50, 0xC7, 0xBF}, "TP-Link"},
    {{0x74, 0xEA, 0x3A}, "TP-Link"},
    {{0xA0, 0xF3, 0xC1}, "TP-Link"},
    {{0xC4, 0x6E, 0x1F}, "TP-Link"},
    {{0xD8, 0x0D, 0x17}, "TP-Link"},
    {{0xEC, 0x08, 0x6B}, "TP-Link"},
    {{0xF4, 0xEC, 0x38}, "TP-Link"},
    
    {{0x00, 0x0F, 0xB5}, "Netgear"},
    {{0x00, 0x14, 0x6C}, "Netgear"},
    {{0x00, 0x18, 0x4D}, "Netgear"},
    {{0x00, 0x1B, 0x2F}, "Netgear"},
    {{0x00, 0x1E, 0x2A}, "Netgear"},
    {{0x00, 0x1F, 0x33}, "Netgear"},
    {{0x00, 0x22, 0x3F}, "Netgear"},
    {{0x00, 0x24, 0xB2}, "Netgear"},
    {{0x00, 0x26, 0xF2}, "Netgear"},
    {{0x20, 0xE5, 0x2A}, "Netgear"},
    {{0x28, 0xC6, 0x8E}, "Netgear"},
    {{0x84, 0x1B, 0x5E}, "Netgear"},
    {{0xA0, 0x21, 0xB7}, "Netgear"},
    {{0xA0, 0x63, 0x91}, "Netgear"},
    {{0xC0, 0x3F, 0x0E}, "Netgear"},
    {{0xE0, 0x46, 0x9A}, "Netgear"},
};

static const int oui_vendors_count = sizeof(oui_vendors) / sizeof(oui_vendor_t);

// Lookup vendor from MAC OUI (first 3 bytes)
static const char* lookup_vendor(const uint8_t *mac) {
    for (int i = 0; i < oui_vendors_count; i++) {
        if (memcmp(mac, oui_vendors[i].oui, 3) == 0) {
            return oui_vendors[i].vendor;
        }
    }
    return "Unknown";
}

// Tracked devices - place in PSRAM to save internal RAM
EXT_RAM_BSS_ATTR static wifi_device_t tracked_devices[MAX_TRACKED_DEVICES];
static int device_count = 0;
static SemaphoreHandle_t device_mutex = NULL;

// State
static bool is_scanning = false;
static TaskHandle_t channel_hop_task_handle = NULL;

// Current channel for scanning
static uint8_t current_channel = 1;

// Channel hopping strategy - prioritize common channels
static const uint8_t channel_sequence[] = {1, 6, 11, 2, 7, 3, 8, 4, 9, 5, 10};
static const uint8_t channel_sequence_len = 11;
static uint8_t channel_idx = 0;

// Channel hopping task
static void channel_hop_task(void *pvParameters) {
    ESP_LOGI(TAG, "Channel hopping task started");
    
    while (is_scanning) {
        // Use optimized channel sequence (1, 6, 11 are most common)
        current_channel = channel_sequence[channel_idx];
        channel_idx = (channel_idx + 1) % channel_sequence_len;
        
        esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        ESP_LOGD(TAG, "Hopped to channel %d", current_channel);
        
        // Balanced dwell time - long enough to catch traffic, short enough to cycle quickly
        uint32_t dwell_time_ms;
        if (current_channel == 1 || current_channel == 6 || current_channel == 11) {
            dwell_time_ms = DWELL_TIME_MS_COMMON;  // 600ms on common channels
        } else {
            dwell_time_ms = DWELL_TIME_MS_UNCOMMON;  // 350ms on other channels
        }
        
        vTaskDelay(pdMS_TO_TICKS(dwell_time_ms));
    }
    
    ESP_LOGI(TAG, "Channel hopping task stopped");
    channel_hop_task_handle = NULL;
    vTaskDelete(NULL);
}

// Helper to extract sequence number from frame
static uint16_t extract_sequence_num(const uint8_t *frame) {
    // Sequence control is at bytes 22-23
    uint16_t seq_ctrl = (frame[23] << 8) | frame[22];
    return (seq_ctrl >> 4) & 0xFFF;  // Upper 12 bits
}

// Helper to parse encryption from capability info
static uint8_t parse_encryption(const uint8_t *frame, uint16_t frame_len) {
    if (frame_len < 36) return 0;
    
    uint16_t capabilities = (frame[35] << 8) | frame[34];
    bool privacy = (capabilities & 0x0010) != 0;  // Privacy bit
    
    if (!privacy) return 0;  // Open
    
    // Check for RSN (WPA2/WPA3) and WPA information elements
    uint16_t offset = 36;  // Start after fixed params
    while (offset + 2 < frame_len) {
        uint8_t elem_id = frame[offset];
        uint8_t elem_len = frame[offset + 1];
        
        if (offset + 2 + elem_len > frame_len) break;
        
        if (elem_id == 48) {  // RSN IE (WPA2/WPA3)
            // Check for SAE (WPA3)
            if (elem_len >= 8 && offset + 10 < frame_len) {
                // Check AKM suite
                if (frame[offset + 9] == 0x08) return 4;  // WPA3
            }
            return 3;  // WPA2
        } else if (elem_id == 221 && elem_len >= 8) {  // Vendor specific
            // Check for WPA OUI (00:50:F2:01)
            if (frame[offset + 2] == 0x00 && frame[offset + 3] == 0x50 &&
                frame[offset + 4] == 0xF2 && frame[offset + 5] == 0x01) {
                return 2;  // WPA
            }
        }
        
        offset += 2 + elem_len;
    }
    
    return 1;  // WEP (privacy without WPA/WPA2)
}

// Helper to parse HT/VHT capabilities
static void parse_capabilities(const uint8_t *frame, uint16_t frame_len, bool *has_ht, bool *has_vht) {
    *has_ht = false;
    *has_vht = false;
    
    if (frame_len < 36) return;
    
    uint16_t offset = 36;
    while (offset + 2 < frame_len) {
        uint8_t elem_id = frame[offset];
        uint8_t elem_len = frame[offset + 1];
        
        if (offset + 2 + elem_len > frame_len) break;
        
        if (elem_id == 45) {  // HT Capabilities
            *has_ht = true;
        } else if (elem_id == 191) {  // VHT Capabilities
            *has_vht = true;
        }
        
        offset += 2 + elem_len;
    }
}

// Helper to initialize device with common fields
static void init_device(wifi_device_t *dev, const uint8_t *mac, const uint8_t *bssid_mac, 
                       int8_t rssi, uint8_t channel, device_type_t type) {
    memset(dev, 0, sizeof(wifi_device_t));
    memcpy(dev->mac, mac, 6);
    if (bssid_mac) {
        memcpy(dev->bssid_mac, bssid_mac, 6);
        format_mac_address(bssid_mac, dev->bssid, sizeof(dev->bssid));
    }
    dev->rssi = rssi;
    dev->channel = channel;
    dev->type = type;
    dev->last_seen = xTaskGetTickCount();
    dev->packet_count = 1;
    
    // Set vendor from MAC OUI
    const char *vendor = lookup_vendor(mac);
    strncpy(dev->vendor, vendor, sizeof(dev->vendor) - 1);
}

// Helper to update BSSID for existing device (more reliable)
static void update_device_bssid(wifi_device_t *dev, const uint8_t *bssid_mac) {
    if (bssid_mac && memcmp(bssid_mac, "\x00\x00\x00\x00\x00\x00", 6) != 0) {
        // Only update if BSSID is valid (not all zeros)
        if (memcmp(dev->bssid_mac, "\x00\x00\x00\x00\x00\x00", 6) == 0 ||
            memcmp(dev->bssid_mac, bssid_mac, 6) != 0) {
            // Update if we don't have a BSSID yet, or it changed
            memcpy(dev->bssid_mac, bssid_mac, 6);
            format_mac_address(bssid_mac, dev->bssid, sizeof(dev->bssid));
        }
    }
}

// Promiscuous mode callback
static void wifi_sniffer_cb(void *recv_buf, wifi_promiscuous_pkt_type_t type)
{
    if (!is_scanning || recv_buf == NULL || device_mutex == NULL)
        return;
    
    const wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)recv_buf;
    if (pkt == NULL)
        return;
    
    const uint8_t *frame = pkt->payload;
    const uint16_t frame_len = pkt->rx_ctrl.sig_len;

    if (frame_len < 24 || frame_len > 2500)
        return;

    // Parse 802.11 frame
    uint8_t frame_type = (frame[0] & 0x0C) >> 2;  // Bits 2-3
    uint8_t frame_subtype = (frame[0] & 0xF0) >> 4;  // Bits 4-7

    int8_t rssi = pkt->rx_ctrl.rssi;
    uint8_t channel = pkt->rx_ctrl.channel;

    // Management frames (type 0)
    if (frame_type == 0)
    {
        // Beacon frames (subtype 8) and Probe responses (subtype 5)
        if (frame_subtype == 8 || frame_subtype == 5)
        {
            uint8_t *src_mac = (uint8_t *)&frame[10];  // Source MAC (BSSID for APs)
            
            // Extract SSID from beacon/probe response
            char ssid[33] = {0};
            if (frame_len > 37)
            {
                uint8_t ssid_len = frame[37];
                if (ssid_len > 0 && ssid_len < 33 && (38 + ssid_len) <= frame_len)
                {
                    memcpy(ssid, &frame[38], ssid_len);
                    ssid[ssid_len] = '\0';
                }
            }

            // Parse encryption and capabilities
            uint8_t encryption = parse_encryption(frame, frame_len);
            bool has_ht = false, has_vht = false;
            parse_capabilities(frame, frame_len, &has_ht, &has_vht);
            uint16_t seq_num = extract_sequence_num(frame);

            // Add or update AP
            if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                int existing = -1;
                for (int i = 0; i < device_count; i++)
                {
                    if (tracked_devices[i].type == DEVICE_TYPE_AP && 
                        memcmp(tracked_devices[i].mac, src_mac, 6) == 0)
                    {
                        existing = i;
                        break;
                    }
                }

                if (existing >= 0)
                {
                    tracked_devices[existing].rssi = rssi;
                    tracked_devices[existing].last_seen = xTaskGetTickCount();
                    tracked_devices[existing].packet_count++;
                    tracked_devices[existing].sequence_num = seq_num;
                    tracked_devices[existing].encryption = encryption;
                    tracked_devices[existing].has_ht_caps = has_ht;
                    tracked_devices[existing].has_vht_caps = has_vht;
                    if (ssid[0] != '\0')
                    {
                        strncpy(tracked_devices[existing].ssid, ssid, sizeof(tracked_devices[existing].ssid) - 1);
                    }
                    update_device_bssid(&tracked_devices[existing], src_mac);
                }
                else if (device_count < MAX_TRACKED_DEVICES)
                {
                    init_device(&tracked_devices[device_count], src_mac, src_mac, rssi, channel, DEVICE_TYPE_AP);
                    strncpy(tracked_devices[device_count].ssid, ssid, sizeof(tracked_devices[device_count].ssid) - 1);
                    tracked_devices[device_count].sequence_num = seq_num;
                    tracked_devices[device_count].encryption = encryption;
                    tracked_devices[device_count].has_ht_caps = has_ht;
                    tracked_devices[device_count].has_vht_caps = has_vht;
                    device_count++;
                }

                xSemaphoreGive(device_mutex);
            }
        }
        // Probe requests (subtype 4) - clients actively scanning
        else if (frame_subtype == 4 && frame_len >= 24)
        {
            uint8_t *client_mac = (uint8_t *)&frame[10];
            
            // Skip multicast/broadcast addresses
            if (client_mac[0] & 0x01) {
                return;
            }
            
            uint16_t seq_num = extract_sequence_num(frame);
            
            // Track client from probe request
            if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                int existing = -1;
                for (int i = 0; i < device_count; i++)
                {
                    if (memcmp(tracked_devices[i].mac, client_mac, 6) == 0)
                    {
                        existing = i;
                        break;
                    }
                }

                if (existing >= 0)
                {
                    // Only update if it's already a client (don't mess with APs!)
                    if (tracked_devices[existing].type == DEVICE_TYPE_CLIENT)
                    {
                        tracked_devices[existing].rssi = rssi;
                        tracked_devices[existing].last_seen = xTaskGetTickCount();
                        tracked_devices[existing].packet_count++;
                        tracked_devices[existing].sequence_num = seq_num;
                        tracked_devices[existing].probe_count++;
                    }
                }
                else if (device_count < MAX_TRACKED_DEVICES)
                {
                    init_device(&tracked_devices[device_count], client_mac, NULL, rssi, channel, DEVICE_TYPE_CLIENT);
                    strcpy(tracked_devices[device_count].ssid, "[Probing]");
                    tracked_devices[device_count].sequence_num = seq_num;
                    tracked_devices[device_count].probe_count = 1;
                    device_count++;
                }

                xSemaphoreGive(device_mutex);
            }
        }
        // Association/Reassociation requests (subtypes 0, 2) - client connecting to AP
        else if ((frame_subtype == 0 || frame_subtype == 2) && frame_len >= 28)
        {
            uint8_t *client_mac = (uint8_t *)&frame[10];  // Source (client)
            uint8_t *ap_mac = (uint8_t *)&frame[4];       // Dest (AP/BSSID)
            
            uint16_t seq_num = extract_sequence_num(frame);
            
            if (!(client_mac[0] & 0x01) && !(ap_mac[0] & 0x01))
            {
                if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
                {
                    int existing = -1;
                    for (int i = 0; i < device_count; i++)
                    {
                        if (memcmp(tracked_devices[i].mac, client_mac, 6) == 0)
                        {
                            existing = i;
                            break;
                        }
                    }

                    if (existing >= 0)
                    {
                        tracked_devices[existing].rssi = rssi;
                        tracked_devices[existing].last_seen = xTaskGetTickCount();
                        tracked_devices[existing].packet_count++;
                        tracked_devices[existing].type = DEVICE_TYPE_CLIENT;
                        tracked_devices[existing].sequence_num = seq_num;
                        tracked_devices[existing].is_associated = true;
                        memcpy(tracked_devices[existing].ap_mac, ap_mac, 6);
                        update_device_bssid(&tracked_devices[existing], ap_mac);
                    }
                    else if (device_count < MAX_TRACKED_DEVICES)
                    {
                        init_device(&tracked_devices[device_count], client_mac, ap_mac, rssi, channel, DEVICE_TYPE_CLIENT);
                        memcpy(tracked_devices[device_count].ap_mac, ap_mac, 6);
                        strcpy(tracked_devices[device_count].ssid, "[Associating]");
                        tracked_devices[device_count].sequence_num = seq_num;
                        tracked_devices[device_count].is_associated = true;
                        device_count++;
                    }

                    xSemaphoreGive(device_mutex);
                }
            }
        }
        // Authentication frames (subtype 11) - client authenticating
        else if (frame_subtype == 11 && frame_len >= 30)
        {
            uint8_t *client_mac = (uint8_t *)&frame[10];  // Source
            uint8_t *ap_mac = (uint8_t *)&frame[4];       // Dest (AP/BSSID)
            
            uint16_t seq_num = extract_sequence_num(frame);
            
            if (!(client_mac[0] & 0x01) && !(ap_mac[0] & 0x01))
            {
                if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
                {
                    int existing = -1;
                    for (int i = 0; i < device_count; i++)
                    {
                        if (memcmp(tracked_devices[i].mac, client_mac, 6) == 0)
                        {
                            existing = i;
                            break;
                        }
                    }

                    if (existing >= 0)
                    {
                        tracked_devices[existing].rssi = rssi;
                        tracked_devices[existing].last_seen = xTaskGetTickCount();
                        tracked_devices[existing].packet_count++;
                        tracked_devices[existing].type = DEVICE_TYPE_CLIENT;
                        tracked_devices[existing].sequence_num = seq_num;
                        tracked_devices[existing].is_authenticated = true;
                        memcpy(tracked_devices[existing].ap_mac, ap_mac, 6);
                        update_device_bssid(&tracked_devices[existing], ap_mac);
                    }
                    else if (device_count < MAX_TRACKED_DEVICES)
                    {
                        init_device(&tracked_devices[device_count], client_mac, ap_mac, rssi, channel, DEVICE_TYPE_CLIENT);
                        memcpy(tracked_devices[device_count].ap_mac, ap_mac, 6);
                        strcpy(tracked_devices[device_count].ssid, "[Authenticating]");
                        tracked_devices[device_count].sequence_num = seq_num;
                        tracked_devices[device_count].is_authenticated = true;
                        device_count++;
                    }

                    xSemaphoreGive(device_mutex);
                }
            }
        }
    }
    // Data frames (type 2) - these show client activity
    else if (frame_type == 2 && frame_len >= 24)
    {
        // ToDS=1, FromDS=0: frame from STA to AP
        // ToDS=0, FromDS=1: frame from AP to STA
        // ToDS=1, FromDS=1: WDS (wireless distribution system)
        // ToDS=0, FromDS=0: IBSS/Ad-hoc
        
        uint8_t to_ds = (frame[1] & 0x01);
        uint8_t from_ds = (frame[1] & 0x02) >> 1;
        
        uint8_t *addr1 = (uint8_t *)&frame[4];   // Receiver
        uint8_t *addr2 = (uint8_t *)&frame[10];  // Transmitter
        uint8_t *addr3 = (uint8_t *)&frame[16];  // BSS ID
        
        uint8_t *client_mac = NULL;
        uint8_t *ap_mac = NULL;
        uint8_t *bssid_mac = addr3;  // BSSID is always addr3 in data frames
        
        uint16_t seq_num = extract_sequence_num(frame);
        uint8_t data_rate = pkt->rx_ctrl.rate;  // Physical data rate
        
        // STA to AP (ToDS=1, FromDS=0)
        if (to_ds && !from_ds)
        {
            client_mac = addr2;  // Source is client
            ap_mac = addr1;      // Dest is AP
        }
        // AP to STA (ToDS=0, FromDS=1)
        else if (!to_ds && from_ds)
        {
            client_mac = addr1;  // Dest is client
            ap_mac = addr2;      // Source is AP
        }
        
        // If we identified a client and AP, track the client
        if (client_mac && ap_mac && 
            !(client_mac[0] & 0x01) &&  // Not multicast
            !(ap_mac[0] & 0x01))         // Not multicast
        {
            if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                // Check if client already exists
                int existing = -1;
                for (int i = 0; i < device_count; i++)
                {
                    if (memcmp(tracked_devices[i].mac, client_mac, 6) == 0)
                    {
                        existing = i;
                        break;
                    }
                }

                if (existing >= 0)
                {
                    // Data frames are strong evidence of client activity - override AP classification
                    tracked_devices[existing].rssi = rssi;
                    tracked_devices[existing].last_seen = xTaskGetTickCount();
                    tracked_devices[existing].packet_count++;
                    tracked_devices[existing].type = DEVICE_TYPE_CLIENT;  // Promote to client
                    tracked_devices[existing].sequence_num = seq_num;
                    tracked_devices[existing].data_rate = data_rate;
                    tracked_devices[existing].data_count++;
                    tracked_devices[existing].is_associated = true;  // Data = associated
                    // Update AP association and BSSID
                    memcpy(tracked_devices[existing].ap_mac, ap_mac, 6);
                    update_device_bssid(&tracked_devices[existing], bssid_mac);
                }
                else if (device_count < MAX_TRACKED_DEVICES)
                {
                    init_device(&tracked_devices[device_count], client_mac, bssid_mac, rssi, channel, DEVICE_TYPE_CLIENT);
                    memcpy(tracked_devices[device_count].ap_mac, ap_mac, 6);
                    tracked_devices[device_count].sequence_num = seq_num;
                    tracked_devices[device_count].data_rate = data_rate;
                    tracked_devices[device_count].data_count = 1;
                    tracked_devices[device_count].is_associated = true;
                    device_count++;
                }

                xSemaphoreGive(device_mutex);
            }
        }
    }
}

esp_err_t wifi_pentest_init(void)
{
    if (device_mutex == NULL)
    {
        device_mutex = xSemaphoreCreateMutex();
        if (device_mutex == NULL)
        {
            return ESP_FAIL;
        }
    }

    memset(tracked_devices, 0, sizeof(tracked_devices));
    device_count = 0;
    is_scanning = false;
    
    return ESP_OK;
}

esp_err_t wifi_pentest_start_scan(void)
{
    if (is_scanning)
    {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting pentest scan - resetting WiFi");
    
    // Fully disconnect and reset WiFi to prevent auto-connect
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Set to NULL mode to fully reset
    esp_wifi_set_mode(WIFI_MODE_NULL);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Deinit and reinit WiFi to clear all state including stored credentials
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Reinitialize WiFi with RAM storage (no flash persistence)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;  // Disable NVS - no stored credentials
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_LOGI(TAG, "WiFi reinitialized with RAM-only storage");

    // Set WiFi mode to station
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start WiFi (now it won't have any saved credentials to connect to)
    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    // Small delay to ensure WiFi is fully started
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // ========== BOOST WiFi SENSITIVITY & PERFORMANCE ==========
    
    // Set maximum WiFi TX power (20.5 dBm max for ESP32-S3)
    esp_wifi_set_max_tx_power(84);  // 84 = 21 dBm (maximum legal power)
    ESP_LOGI(TAG, "WiFi TX power set to maximum (21 dBm)");
    
    // Enable 802.11b/g/n protocols for maximum compatibility
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    ESP_LOGI(TAG, "WiFi protocols enabled: 802.11b/g/n");
    
    // Set WiFi bandwidth to 40MHz for better reception (HT40)
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    ESP_LOGI(TAG, "WiFi bandwidth set to 40MHz (HT40)");

    // Enable promiscuous mode with ALL packet types
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | 
                       WIFI_PROMIS_FILTER_MASK_DATA |
                       WIFI_PROMIS_FILTER_MASK_CTRL   // Add control frames
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb);

    // Start on channel 1
    current_channel = 1;
    esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);

    // Start channel hopping task
    is_scanning = true;
    xTaskCreate(channel_hop_task, "channel_hop", HOP_TASK_STACK_SIZE, NULL, 5, &channel_hop_task_handle);
    
    ESP_LOGI(TAG, "Started WiFi scanning with channel hopping (1-11)");

    return ESP_OK;
}

esp_err_t wifi_pentest_start_scan_apsta(void)
{
    if (is_scanning)
    {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_OK;
    }

    wifi_mode_t current_mode;
    esp_err_t ret = esp_wifi_get_mode(&current_mode);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }

    if (current_mode == WIFI_MODE_AP)
    {
        ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to switch to APSTA mode: %s", esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    else if (current_mode != WIFI_MODE_APSTA)
    {
        ESP_LOGE(TAG, "WiFi must be in AP or APSTA mode for this function");
        return ESP_ERR_INVALID_STATE;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_wifi_set_max_tx_power(84);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | 
                       WIFI_PROMIS_FILTER_MASK_DATA |
                       WIFI_PROMIS_FILTER_MASK_CTRL
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb);

    current_channel = 1;
    esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);

    is_scanning = true;
    if (channel_hop_task_handle == NULL)
    {
        xTaskCreate(channel_hop_task, "channel_hop", HOP_TASK_STACK_SIZE, NULL, 5, &channel_hop_task_handle);
    }

    ESP_LOGI(TAG, "Started WiFi scanning in APSTA mode (AP preserved)");
    return ESP_OK;
}

esp_err_t wifi_pentest_stop_scan(void)
{
    if (!is_scanning)
        return ESP_OK;

    ESP_LOGI(TAG, "Stopping pentest scan and restoring WiFi");
    
    // Stop scanning flag first
    is_scanning = false;
    
    // Wait for channel hopping task to finish
    if (channel_hop_task_handle != NULL) {
        int wait_count = 0;
        while (channel_hop_task_handle != NULL && wait_count < 20) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_count++;
        }
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Deinit WiFi (we started it with RAM-only storage)
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Reinitialize WiFi with normal flash storage so saved networks work again
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_LOGI(TAG, "WiFi restored to normal mode with flash storage");
    ESP_LOGI(TAG, "Stopped WiFi scanning");
    
    return ESP_OK;
}

esp_err_t wifi_pentest_stop_scan_apsta(void)
{
    if (!is_scanning)
        return ESP_OK;

    ESP_LOGI(TAG, "Stopping pentest scan (preserving AP)");

    is_scanning = false;

    if (channel_hop_task_handle != NULL) {
        int wait_count = 0;
        while (channel_hop_task_handle != NULL && wait_count < 20) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_count++;
        }
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_disconnect();

    wifi_mode_t current_mode;
    esp_err_t ret = esp_wifi_get_mode(&current_mode);
    if (ret == ESP_OK && current_mode == WIFI_MODE_APSTA) {
        ret = esp_wifi_set_mode(WIFI_MODE_AP);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to switch back to AP mode: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Stopped WiFi scanning (AP preserved)");
    return ESP_OK;
}

int wifi_pentest_get_devices(wifi_device_t *devices, int max_devices)
{
    if (devices == NULL || max_devices <= 0)
        return 0;

    int count = 0;
    if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        count = (device_count < max_devices) ? device_count : max_devices;
        memcpy(devices, tracked_devices, count * sizeof(wifi_device_t));
        xSemaphoreGive(device_mutex);
    }

    return count;
}

bool wifi_pentest_is_scanning(void)
{
    return is_scanning;
}

esp_err_t wifi_pentest_deinit(void)
{
    is_scanning = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    wifi_pentest_stop_scan();
    
    if (device_mutex != NULL)
    {
        vSemaphoreDelete(device_mutex);
        device_mutex = NULL;
    }
    
    device_count = 0;
    ESP_LOGI(TAG, "WiFi penetration testing module deinitialized");
    
    return ESP_OK;
}

void format_mac_address(const uint8_t *mac, char *str, size_t len)
{
    snprintf(str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char* get_encryption_name(uint8_t enc_type)
{
    switch (enc_type) {
        case 0: return "Open";
        case 1: return "WEP";
        case 2: return "WPA";
        case 3: return "WPA2";
        case 4: return "WPA3";
        default: return "Unknown";
    }
}
