#include <math.h>
#include <string.h>

#include "api/prefs.h"
#include "core/store/nvs_fs.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "network/modules/heartbeat_sensor.h"
#include "network/utility/wifi_tools.h"
#include "util/hashing.h"
#include "util/util.h"

#define MAX_FREQ_CACHE_SIZE 256

static const char *TAG = "heartbeat_sensor";

bool is_heartbeat_sensor_running = false;

static lv_timer_t *refresh_timer = NULL;
EXT_RAM_BSS_ATTR static wifi_device_t devices[MAX_TRACKED_DEVICES];
static int device_list_count = 0;

static int scan_iteration_index = 0;
static int can_use_prefs_at_index = 5;

EXT_RAM_BSS_ATTR static heartbeat_item_t heartbeat_items[MAX_HEARTBEAT_ITEMS];
EXT_RAM_BSS_ATTR static heartbeat_item_t last_heartbeat_items[MAX_HEARTBEAT_ITEMS];

int *freq_cache = (int *)calloc(MAX_FREQ_CACHE_SIZE, sizeof(int));

heartbeat_sensor_cfg_t current_cfg = {
    .callback = NULL,
    .scan_interval = 2000,
    .max_items = 60,
};

/**
 * Estimates the distance to a WiFi device based on RSSI
 * 
 * @param rssi The received signal strength indicator in dBm (typically -30 to -100)
 * @param txPower The transmission power at 1 meter in dBm (default -50 for typical WiFi)
 * @return Estimated distance in feet
 * 
 * Note: This uses the log-distance path loss model. Accuracy varies based on:
 * - Environmental factors (walls, interference, etc.)
 * - Device transmission power
 * - Antenna characteristics
 * Typical accuracy: ±30-50% in indoor environments
 */
float estimate_distance_from_rssi(int rssi, int txPower) {
    if (rssi == 0) {
        return -1.0f; // Invalid RSSI
    }
    
    // Path loss exponent (2.0 = free space, 3.0-4.0 = indoor with obstacles)
    const float N = 3.0f;
    
    // Calculate distance in meters using log-distance path loss model
    // Distance = 10 ^ ((txPower - RSSI) / (10 * N))
    float distanceMeters = pow(10.0f, (txPower - rssi) / (10.0f * N));
    
    // Convert meters to feet (1 meter = 3.28084 feet)
    float distanceFeet = distanceMeters * 3.28084f;
    
    return distanceFeet;
}

static int mac_strcasecmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = (*a >= 'a' && *a <= 'f') ? *a - 32 : *a;
        char cb = (*b >= 'a' && *b <= 'f') ? *b - 32 : *b;
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return *a - *b;
}

void heartbeat_sensor_clear_store(void)
{
    ESP_LOGI(TAG, "Clearing heartbeat sensor store");
    nvs_delete_keys_with_prefix(heartbeatNamespace, "hb_freq_");
    if (freq_cache)
    {
        free(freq_cache);
        freq_cache = NULL;
        freq_cache = (int *)calloc(MAX_FREQ_CACHE_SIZE, sizeof(int));
    }
    ESP_LOGI(TAG, "Cleared heartbeat sensor store");
}
bool mac_list_contains(uint8_t *mac,
                       uint8_t **mac_list,
                       int count)
{
    if (!mac || !mac_list || count <= 0)
        return false;

    for (int i = 0; i < count; i++)
    {
        if (!mac_list[i])
            continue;

        if (memcmp(mac, mac_list[i], 6) == 0)
            return true;
    }
    return false;
}

void format_heartbeat_mac_address(uint8_t *mac, char *str, size_t len)
{
    snprintf(str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int get_heartbeat_id(heartbeat_item_t *item)
{
    uint8_t *raw_mac = item->last_mac;

    // This takes infinite params, so adjust as needed.
    // Note: This expects chars, not char *.
    uint8_t identity = get_unordered_hash<uint8_t>(
        raw_mac[0], raw_mac[1], raw_mac[2],
        raw_mac[3], raw_mac[4], raw_mac[5]);

    return identity;
}

bool add_associated_mac(heartbeat_item_t *item, const uint8_t mac[6])
{
    if (item->associations.mac_count >= MAX_ASSOCIATED_MACS)
        return false;

    memcpy(item->associations.associated_macs[item->associations.mac_count],
           mac, 6);

    item->associations.mac_count++;
    return true;
}

int get_saved_frequency(heartbeat_item_t *item, bool can_use_prefs)
{
    if (!freq_cache[item->heartbeat_id] || freq_cache[item->heartbeat_id] == NULL || freq_cache[item->heartbeat_id] == 0)
    {
        freq_cache[item->heartbeat_id] = 0;
    }

    uint8_t heartbeat_id = item->heartbeat_id;
    int latest_freq = 0;
    if (can_use_prefs)
    {
        char key[32];
        snprintf(key, sizeof(key), "hb_freq_%d", heartbeat_id);
        latest_freq = read_pref_int(key, 0, heartbeatNamespace);
        freq_cache[heartbeat_id] = latest_freq;
    }
    else
    {
        latest_freq = freq_cache[heartbeat_id];
    }
    return latest_freq;
}

void save_frequency(heartbeat_item_t *item, int frequency, bool can_use_prefs)
{

    if (!freq_cache[item->heartbeat_id] || freq_cache[item->heartbeat_id] == NULL || freq_cache[item->heartbeat_id] == 0)
    {
        freq_cache[item->heartbeat_id] = 0;
    }

    if (can_use_prefs)
    {
        uint8_t heartbeat_id = item->heartbeat_id;
        char key[32];
        snprintf(key, sizeof(key), "hb_freq_%d", heartbeat_id);
        if (!write_pref_int(key, &frequency, heartbeatNamespace))
        {
            ESP_LOGE(TAG, "Error writing frequency to NVS");
            return;
        }
    }
    freq_cache[item->heartbeat_id] = frequency;
}

int get_difference_score(heartbeat_item_t *current_item)
{
    int difference_score = 0;
    static const uint8_t zero_mac[6] = {0};
    size_t last_hb_count = sizeof(last_heartbeat_items) / sizeof(last_heartbeat_items[0]);

    for (size_t i = 0; i < last_hb_count; i++)
    {
        heartbeat_item_t *last_item = &last_heartbeat_items[i];

        if (last_item->last_ssid[0] != '\0' && current_item->last_ssid[0] != '\0' && strcmp(last_item->last_ssid, current_item->last_ssid) != 0 && (strcmp(last_item->last_ssid, "[Probing]") != 0) && (strcmp(current_item->last_ssid, "[Probing]") != 0))
        {
            // MAC match, but ssid mismatch
            if (!(memcmp(last_item->last_mac, current_item->last_mac, 6) != 0 && (memcmp(last_item->last_mac, zero_mac, 6) != 0) && (memcmp(current_item->last_mac, zero_mac, 6) != 0)))
            {
                if (current_item->heartbeat_id == last_item->heartbeat_id)
                {
                    difference_score++;
                }
                else
                {
                    current_item->flagged_id_mismatch = 1;
                    difference_score++;
                }
            }
        }

        if (memcmp(last_item->last_mac, current_item->last_mac, 6) != 0 && (memcmp(last_item->last_mac, zero_mac, 6) != 0) && (memcmp(current_item->last_mac, zero_mac, 6) != 0))
        {
            // MAC mismatch, but ssid match
            if (!(last_item->last_ssid[0] != '\0' && current_item->last_ssid[0] != '\0' && strcmp(last_item->last_ssid, current_item->last_ssid) != 0) && (strcmp(last_item->last_ssid, "[Probing]") != 0) && (strcmp(current_item->last_ssid, "[Probing]") != 0))
            {
                if (current_item->heartbeat_id == last_item->heartbeat_id)
                {
                    difference_score++;
                }
                else
                {
                    current_item->flagged_id_mismatch = 1;
                    difference_score++;
                    // ESP_LOGI(TAG, "Flagged ID Mismatch: NEW: %s - OLD: %s - NEW ID: %d - OLD ID: %d - MAC: %s - OLD MAC: %s", current_item->last_ssid, last_item->last_ssid, current_item->heartbeat_id, last_item->heartbeat_id, current_item->last_mac, last_item->last_mac);
                }
            }
        }
    }
    return difference_score;
}

static void handle_device_conversion(wifi_device_t *device, int *heartbeat_item_count, uint8_t **mac_list, bool can_use_prefs)
{

    static const uint8_t zero_mac[6] = {0};
    // Store out the raw mac address for initial uniqueness check
    uint8_t *raw_mac = device->mac;
    uint8_t *raw_parent_mac = device->ap_mac;
    // char client_mac_str[18];

    if (memcmp(raw_mac, zero_mac, 6) == 0)
    {
        return;
    }

    // TODO: Could just use the raw mac to compare, convert later.
    // format_heartbeat_mac_address(raw_mac, client_mac_str, sizeof(client_mac_str));
    // Check if the mac address is already in the list
    if (mac_list_contains(raw_mac, mac_list, *heartbeat_item_count))
    {
        return;
    }

    // Allocate at 0 based index, instead of using device index
    mac_list[*heartbeat_item_count] = (uint8_t *)malloc(6);
    if (mac_list[*heartbeat_item_count])
    {
        memcpy(mac_list[*heartbeat_item_count], raw_mac, 6);
    }

    heartbeat_items[*heartbeat_item_count] = (heartbeat_item_t){
        .heartbeat_id = NULL,
        .last_mac = NULL,
        .associations = {
            .associated_macs = NULL,
            .associated_ssids = NULL},
        .frequency = 0,
        .last_ssid = NULL,
        .difference_score = 0,
        .flagged_id_mismatch = 0,
        .parent_mac = NULL,
        .flagged_frequency = 0,
        .last_bssid = NULL,
        .est_distance = 0,
    };


    // BEGIN: Set all data for the heartbeat item ---------------------------------------
    
    // Set last mac to the device mac
    memcpy(&heartbeat_items[*heartbeat_item_count].last_mac, raw_mac, 6);

    if (memcmp(raw_parent_mac, zero_mac, 6) != 0)
    {
        // Set parent mac to the device parent mac
        memcpy(&heartbeat_items[*heartbeat_item_count].parent_mac, raw_parent_mac, 6);
    }

    // Add the device mac to the associated macs
    add_associated_mac(&heartbeat_items[*heartbeat_item_count], raw_mac);

    // Set last ssid to the device ssid
    snprintf(heartbeat_items[*heartbeat_item_count].last_ssid, sizeof(heartbeat_items[*heartbeat_item_count].last_ssid), "%s", device->ssid);

    // Set last ssid to the device ssid
    snprintf(heartbeat_items[*heartbeat_item_count].last_bssid, sizeof(heartbeat_items[*heartbeat_item_count].last_bssid), "%s", device->bssid);

    float est_distance = estimate_distance_from_rssi(device->rssi);
    heartbeat_items[*heartbeat_item_count].est_distance = est_distance;

    // Update the heartbeat id.
    heartbeat_items[*heartbeat_item_count].heartbeat_id = get_heartbeat_id(&heartbeat_items[*heartbeat_item_count]);
    // Get the latest frequency of the heartbeat item
    heartbeat_items[*heartbeat_item_count].frequency = get_saved_frequency(&heartbeat_items[*heartbeat_item_count], can_use_prefs);

    int difference_score = get_difference_score(&heartbeat_items[*heartbeat_item_count]);
    heartbeat_items[*heartbeat_item_count].difference_score = difference_score;

    // // Compare the last heartbeat item with the current heartbeat item
    heartbeat_item_t *last_heartbeat_item = &last_heartbeat_items[heartbeat_items[*heartbeat_item_count].heartbeat_id];
    
    // End of data setting ------------------------------------------------------



    // Copy the current/new heartbeat item to the last heartbeat item slot
    memcpy(last_heartbeat_item, &heartbeat_items[*heartbeat_item_count], sizeof(heartbeat_item_t));

    // Increment the frequency of the heartbeat item and save it to NVS
    heartbeat_items[*heartbeat_item_count].frequency += 1;
    if (can_use_prefs)
    {
        save_frequency(&heartbeat_items[*heartbeat_item_count], heartbeat_items[*heartbeat_item_count].frequency, can_use_prefs);
    }

    if(heartbeat_items[*heartbeat_item_count].frequency > current_cfg.frequency_threshold)
    {
        heartbeat_items[*heartbeat_item_count].flagged_frequency = 1;
    }

    (*heartbeat_item_count)++;

    return;
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    bool can_use_prefs = false;
    int heartbeat_item_count = 0;
    // Get current devices
    device_list_count = 0;
    int total_list_count = wifi_pentest_get_devices(devices, MAX_TRACKED_DEVICES);
    if (total_list_count == 0)
    {
        ESP_LOGI(TAG, "No devices found");
        return;
    }

    // Convert wifi_device_t to heartbeat_item_t
    for (int i = 0; i < total_list_count; i++)
    {
        wifi_device_t *device = &devices[i];
        if (device->type == DEVICE_TYPE_CLIENT)
        {
            device_list_count++;
        }
    }

    uint8_t **mac_list = NULL;
    mac_list = (uint8_t **)malloc(device_list_count * sizeof(uint8_t *));
    if (!mac_list)
        return;

    scan_iteration_index++;
    if (scan_iteration_index >= can_use_prefs_at_index)
    {
        can_use_prefs = true;
        scan_iteration_index = 0;
    }

    // Convert wifi_device_t to heartbeat_item_t
    for (int i = 0; i < total_list_count; i++)
    {
        wifi_device_t *device = &devices[i];
        if (device->type == DEVICE_TYPE_CLIENT)
        {
            handle_device_conversion(device, &heartbeat_item_count, mac_list, can_use_prefs);
        }
    }

    // Free allocations
    for (int i = 0; i < heartbeat_item_count; i++)
    {
        free(mac_list[i]);
    }
    free(mac_list);
    mac_list = NULL;

    ESP_LOGI(TAG, "Heartbeat cb");
    // Call callback with new heartbeat items
    if (current_cfg.callback != NULL)
    {
        ESP_LOGI(TAG, "Calling callback with items");
        current_cfg.callback(heartbeat_items, heartbeat_item_count);
    }
}

bool heartbeat_start_initial_scan(void)
{
    esp_err_t ret = wifi_pentest_start_scan();
    if (ret == ESP_OK)
    {
        // Enable refresh timer
        if (refresh_timer)
        {
            lv_timer_resume(refresh_timer);
        }
        return true;
    }
    ESP_LOGE(TAG, "Failed to start scan");

    return false;
}

bool heartbeat_sensor_start(heartbeat_sensor_cfg_t cfg)
{
    if (is_heartbeat_sensor_running)
    {
        ESP_LOGW(TAG, "Heartbeat sensor already running");
        return false;
    }
    ESP_LOGI(TAG, "Starting heartbeat sensor");
    is_heartbeat_sensor_running = true;

    current_cfg = cfg;

    // Initialize pentest module
    wifi_pentest_init();
    refresh_timer = lv_timer_create(refresh_timer_cb, current_cfg.scan_interval, NULL);
    lv_timer_pause(refresh_timer);

    bool ret = heartbeat_start_initial_scan();
    if (ret)
    {
        return true;
    }
    return false;
}

bool heartbeat_sensor_stop(void)
{
    if (!is_heartbeat_sensor_running)
    {
        ESP_LOGW(TAG, "Heartbeat sensor not running");
        return false;
    }
    ESP_LOGI(TAG, "Stopping heartbeat sensor");

    is_heartbeat_sensor_running = false;
    if (refresh_timer)
    {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }

    wifi_pentest_stop_scan();
    wifi_pentest_deinit();
    return true;
}