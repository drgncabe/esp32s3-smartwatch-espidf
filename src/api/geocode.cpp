#include <stdlib.h>
#include <string.h>

#include "api/geocode.h"
#include "api/weather_api.h"
#include "context/app_context.h"
#include "core/network/wifi_init.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "geocode";

static bool geocode_task_running = false;

// Manual JSON parsing - avoids cJSON's massive memory overhead
static bool parse_string_value(const char *json, const char *key, char *out, size_t out_size) {
    char search[128];
    const char *pos = NULL;
    
    // Try different patterns: "key":"value", "key": "value", "key" : "value"
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    pos = strstr(json, search);
    if (pos) {
        ESP_LOGI(TAG, "Found key '%s' with pattern 'key\":\"'", key);
    } else {
        // Try with space after colon
        snprintf(search, sizeof(search), "\"%s\": \"", key);
        pos = strstr(json, search);
        if (pos) {
            ESP_LOGI(TAG, "Found key '%s' with pattern 'key\": \"'", key);
        } else {
            // Try with space before colon
            snprintf(search, sizeof(search), "\"%s\" : \"", key);
            pos = strstr(json, search);
            if (pos) {
                ESP_LOGI(TAG, "Found key '%s' with pattern 'key\" : \"'", key);
            } else {
                // Try with spaces on both sides
                snprintf(search, sizeof(search), "\"%s\" :  \"", key);
                pos = strstr(json, search);
                if (pos) {
                    ESP_LOGI(TAG, "Found key '%s' with pattern 'key\" :  \"'", key);
                }
            }
        }
    }
    
    if (!pos) {
        ESP_LOGW(TAG, "Key '%s' not found in JSON", key);
        return false;
    }
    
    pos += strlen(search);
    const char *end = strchr(pos, '"');
    if (!end) {
        ESP_LOGW(TAG, "No closing quote found for key '%s'", key);
        return false;
    }
    
    size_t len = end - pos;
    if (len >= out_size) len = out_size - 1;
    
    strncpy(out, pos, len);
    out[len] = '\0';
    ESP_LOGI(TAG, "Extracted value for '%s': '%s'", key, out);
    return true;
}

static const char* findArrayElement(const char *array_start, int index) {
    int current_index = 0;
    int depth = 0;
    bool in_string = false;
    const char *element_start = NULL;
    const char *element_end = NULL;
    
    for (const char *p = array_start; *p && *p != ']'; p++) {
        if (*p == '"' && (p == array_start || *(p-1) != '\\')) {
            in_string = !in_string;
        } else if (!in_string) {
            if (*p == '{') {
                if (depth == 0 && current_index == index) {
                    // Found the start of the element we want
                    element_start = p + 1;
                }
                depth++;
            } else if (*p == '}') {
                depth--;
                if (depth == 0 && element_start != NULL) {
                    // Found the end of the element we want
                    element_end = p;
                    break;
                }
                if (depth < 0) break;
            } else if (*p == ',' && depth == 0) {
                current_index++;
                if (current_index > index) break;
            }
        }
    }
    
    if (element_start != NULL && element_end != NULL) {
        return element_start;
    }
    return NULL;
}

GeocodeData parse_geocode_data(const char *json)
{
    GeocodeData geocodeData = {};
    geocodeData.latitude = 0;
    geocodeData.longitude = 0;
    geocodeData.placeName = nullptr;
    geocodeData.stateAbr = nullptr;
    static char pnBuf[65] = {0};
    static char stBuf[5] = {0};

    if (!json || strlen(json) == 0) {
        ESP_LOGE(TAG, "Error: JSON string is NULL or empty");
        return geocodeData;
    }

    ESP_LOGI(TAG, "Parsing geocode JSON manually (no cJSON), free heap: %lu", 
             (unsigned long)esp_get_free_heap_size());

    // Find "places" array
    const char *places_section = strstr(json, "\"places\"");
    if (!places_section) {
        ESP_LOGE(TAG, "No 'places' array found in JSON");
        return geocodeData;
    }

    // Find the start of the places array
    const char *array_start = strchr(places_section, '[');
    if (!array_start) {
        ESP_LOGE(TAG, "No array start found");
        return geocodeData;
    }
    array_start++; // Skip the '['

    // Get first element of the array
    const char *placeItem = findArrayElement(array_start, 0);
    if (!placeItem) {
        ESP_LOGE(TAG, "No first element found in places array");
        return geocodeData;
    }

    // Debug: log the found element (first 100 chars)
    char debug_buf[101];
    size_t debug_len = strlen(placeItem);
    if (debug_len > 100) debug_len = 100;
    strncpy(debug_buf, placeItem, debug_len);
    debug_buf[debug_len] = '\0';
    ESP_LOGI(TAG, "Found place item element: %.100s", debug_buf);

    // Parse latitude (as string, then convert to double)
    char lat_str[32] = {0};
    if (parse_string_value(placeItem, "latitude", lat_str, sizeof(lat_str))) {
        geocodeData.latitude = atof(lat_str);
        ESP_LOGI(TAG, "Parsed latitude string: '%s', value: %f", lat_str, geocodeData.latitude);
    } else {
        ESP_LOGW(TAG, "Failed to parse latitude");
    }

    // Parse longitude (as string, then convert to double)
    char lon_str[32] = {0};
    if (parse_string_value(placeItem, "longitude", lon_str, sizeof(lon_str))) {
        geocodeData.longitude = atof(lon_str);
        ESP_LOGI(TAG, "Parsed longitude string: '%s', value: %f", lon_str, geocodeData.longitude);
    } else {
        ESP_LOGW(TAG, "Failed to parse longitude");
    }

    // Parse place name
    if (parse_string_value(placeItem, "place name", pnBuf, sizeof(pnBuf))) {
        geocodeData.placeName = pnBuf;
        ESP_LOGI(TAG, "Parsed place name: %s", geocodeData.placeName);
    } else {
        ESP_LOGW(TAG, "Failed to parse place name");
    }

    // Parse state abbreviation
    if (parse_string_value(placeItem, "state abbreviation", stBuf, sizeof(stBuf))) {
        geocodeData.stateAbr = stBuf;
        ESP_LOGI(TAG, "Parsed state abbreviation: %s", geocodeData.stateAbr);
    } else {
        ESP_LOGW(TAG, "Failed to parse state abbreviation");
    }

    ESP_LOGI(TAG, "Successfully parsed geocode data, free heap: %lu", 
             (unsigned long)esp_get_free_heap_size());
    return geocodeData;
}

GeocodeData get_lat_lon_from_zip(int zip)
{
    GeocodeData gd = {};
    gd.latitude = 0;
    gd.longitude = 0;
    gd.placeName = nullptr;
    gd.stateAbr = nullptr;

    // Check WiFi connection
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi not connected");
        return gd;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://api.zippopotam.us/us/%d", zip);

    // HTTP client configuration
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 10000;
    config.buffer_size = 2048;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return gd;
    }

    // Allocate response buffer
    char *response = (char *)malloc(2048);
    if (response == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        esp_http_client_cleanup(client);
        return gd;
    }
    
    // Open connection and write request
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        free(response);
        esp_http_client_cleanup(client);
        return gd;
    }
    
    // Get content length and status
    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    
    ESP_LOGI(TAG, "HTTP Status = %d, content_length = %d", status_code, content_length);
    
    if (status_code == 200 && content_length > 0) {
        // Read response data
        int total_read = 0;
        int read_len;
        
        while (total_read < content_length && total_read < 2047) {
            read_len = esp_http_client_read(client, response + total_read, 2047 - total_read);
            if (read_len <= 0) {
                break;
            }
            total_read += read_len;
        }
        
        response[total_read] = '\0';
        
        if (total_read > 0) {
            ESP_LOGI(TAG, "Response (%d bytes): %s", total_read, response);
            ESP_LOGI(TAG, "Parsing geocode data");
            
            GeocodeData geocodeData = parse_geocode_data(response);
            ESP_LOGI(TAG, "Geocode data lat: %f", geocodeData.latitude);
            ESP_LOGI(TAG, "Geocode data lon: %f", geocodeData.longitude);
            
            free(response);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return geocodeData;
        } else {
            ESP_LOGE(TAG, "Failed to read response data");
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
    }
    
    free(response);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return gd;
}

static void geocode_bg_store_task(void *parameter)
{
    bool firstConnect = *(bool *)parameter;
    
    if (wifi_get_state() != WIFI_CONNECTED) {
        ESP_LOGI(TAG, "Geocode background store task: WiFi not connected, skipping...");
        lv_async_call([](void *) {
            geocode_task_running = false;
        }, NULL);
        vTaskDelete(NULL);
        return;
    }

    FriendlyGeocodeData geoData = g_settings.get_zip_geocode_data(g_settings.get_zip_code());
    // Free the dynamically allocated strings (only if they're not NULL)
    if (geoData.friendlyAPIGeodata != NULL) {
        free(const_cast<char*>(geoData.friendlyAPIGeodata));
    }
    if (geoData.friendlyName != NULL) {
        free(const_cast<char*>(geoData.friendlyName));
    }
    
    // Allocate memory for firstConnect to pass to async call
    bool *fc_ptr = (bool *)malloc(sizeof(bool));
    if (fc_ptr) {
        *fc_ptr = firstConnect;
        lv_async_call([](void *user_data) {
            geocode_task_running = false;
            bool firstConnect = *(bool *)user_data;
            free(user_data);
            
            if (firstConnect) {
                ESP_LOGI(TAG, "First connect, starting weather task...");
                start_weather_task(false);
            }
        }, fc_ptr);
    }
    
    vTaskDelete(NULL);
}

void start_geocode_bg_store_task(bool firstConnect)
{
    if (geocode_task_running) {
        ESP_LOGI(TAG, "Geocode background store task already running, skipping...");
        return;
    }
    
    geocode_task_running = true;
    ESP_LOGI(TAG, "Starting geocode background store task...");
    
    // Allocate memory for parameter
    bool *fc_ptr = (bool *)malloc(sizeof(bool));
    if (fc_ptr) {
        *fc_ptr = firstConnect;
        xTaskCreatePinnedToCore(
            geocode_bg_store_task,
            "geocode_bg_store_task",
            8192,
            fc_ptr,
            1,
            NULL,
            1);
    }
}