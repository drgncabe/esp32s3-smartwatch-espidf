#include <assert.h>
#include <string.h>

#include "api/prefs.h"
#include "context/app_context.h"
#include "core/network/wifi_init.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "network/modules/wifi_scanner.h"

static const char *TAG = "wifi_scanner";

// Store scan results
static uint16_t scan_result_count = 0;
static wifi_ap_record_t *scan_results = NULL;
static bool scan_in_progress = false;
static bool scan_done = false;

// Event handler for WiFi scan
static void wifi_scan_event_handler(void* arg, esp_event_base_t event_base,
                                     int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        scan_in_progress = false;
        scan_done = true;
        ESP_LOGI(TAG, "WiFi scan completed");
    }
}

void start_wifi_scan()
{
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    // Free previous scan results if any
    if (scan_results != NULL) {
        free(scan_results);
        scan_results = NULL;
    }
    scan_result_count = 0;
    scan_done = false;
    
    // Initialize WiFi if not already done
    static bool wifi_initialized = false;
    if (!wifi_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        
        // Create event loop if it doesn't exist
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(err);
        }
        
        // Create WiFi STA interface if it doesn't exist
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif == NULL) {
            sta_netif = esp_netif_create_default_wifi_sta();
            assert(sta_netif);
        }
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, 
                                                    WIFI_EVENT_SCAN_DONE, 
                                                    &wifi_scan_event_handler, 
                                                    NULL));
        wifi_initialized = true;
    }
    
    // Always ensure WiFi is in STA mode and started before scanning
    wifi_mode_t current_mode;
    esp_err_t mode_err = esp_wifi_get_mode(&current_mode);
    
    // Set mode to STA if needed
    if (mode_err != ESP_OK || current_mode == WIFI_MODE_NULL || current_mode != WIFI_MODE_STA) {
        ESP_LOGI(TAG, "Setting WiFi mode to STA...");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Always try to start WiFi (safe to call even if already started)
    // This ensures WiFi is started even if it was stopped by disable_wifi()
    esp_err_t start_err = esp_wifi_start();
    if (start_err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi started (or already running)");
    } else if (start_err == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "WiFi not initialized - this should not happen");
        return; // Can't scan if WiFi not initialized
    } else {
        ESP_LOGW(TAG, "WiFi start returned: %s, will retry scan", esp_err_to_name(start_err));
    }
    
    vTaskDelay(pdMS_TO_TICKS(150)); // Wait for WiFi to be ready
    
    // Make sure we're not connecting - disconnect if needed
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Start async scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        }
    };
    
    scan_in_progress = true;
    esp_err_t err = esp_wifi_scan_start(&scan_config, false); // false = async
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan start failed: %s", esp_err_to_name(err));
        scan_in_progress = false;
        // If scan failed due to state, wait and retry once
        if (err == ESP_ERR_WIFI_STATE) {
            ESP_LOGW(TAG, "WiFi in wrong state, waiting and retrying...");
            vTaskDelay(pdMS_TO_TICKS(500));
            err = esp_wifi_scan_start(&scan_config, false);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "WiFi scan retry also failed: %s", esp_err_to_name(err));
                scan_in_progress = false;
            }
        }
    }
}

bool wifi_scan_complete()
{
    if (!scan_done) {
        return false;
    }
    
    // Get scan results if we haven't already
    if (scan_results == NULL && scan_done) {
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&scan_result_count));
        
        if (scan_result_count > 0) {
            scan_results = (wifi_ap_record_t *)heap_caps_malloc(sizeof(wifi_ap_record_t) * scan_result_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!scan_results) scan_results = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * scan_result_count); // Fallback
            if (scan_results != NULL) {
                ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&scan_result_count, scan_results));
                ESP_LOGI(TAG, "Found %d WiFi networks", scan_result_count);
            }
        }
    }
    
    return true;
}

int get_wifi_scan_count()
{
    if (!scan_done || scan_results == NULL) {
        return -1; // Scan not complete or no results
    }
    return scan_result_count;
}

const char* get_wifi_ssid(int i)
{
    if (scan_results == NULL || i < 0 || i >= scan_result_count) {
        return "";
    }
    return (const char*)scan_results[i].ssid;
}

bool wifi_is_open(int i)
{
    if (scan_results == NULL || i < 0 || i >= scan_result_count) {
        return false;
    }
    return scan_results[i].authmode == WIFI_AUTH_OPEN;
}

int8_t get_wifi_rssi(int i)
{
    if (scan_results == NULL || i < 0 || i >= scan_result_count) {
        return -100;
    }
    return scan_results[i].rssi;
}

wifi_auth_mode_t get_wifi_auth_mode(int i)
{
    if (scan_results == NULL || i < 0 || i >= scan_result_count) {
        return WIFI_AUTH_OPEN;
    }
    return scan_results[i].authmode;
}

void cleanup_wifi_scan()
{
    if (scan_results != NULL) {
        free(scan_results);
        scan_results = NULL;
    }
    scan_result_count = 0;
    scan_done = false;
}