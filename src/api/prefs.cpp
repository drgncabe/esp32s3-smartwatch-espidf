#include <stdlib.h>
#include <string.h>

#include "api/geocode.h"
#include "api/prefs.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "util/util.h"

static const char *TAG = "prefs";

static const char *appNamespace = APP_NAMESPACE;
static const char *credentialsNamespace = CREDENTIALS_NAMESPACE;
static const char *geocodeNamespace = GEOCODE_NAMESPACE;
const char *heartbeatNamespace = HEARTBEAT_NAMESPACE;

bool write_pref_char(const char *key, const char *charValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_str(handle, key, charValue);
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}

bool write_pref_uint(const char *key, const uint8_t *uint8Value, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_u8(handle, key, *uint8Value);
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}

bool write_pref_int(const char *key, const int *intValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_i32(handle, key, *intValue);
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}

bool write_pref_bool(const char *key, const bool *boolValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_u8(handle, key, *boolValue ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}

bool write_pref_long(const char *key, const long *longValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_i64(handle, key, *longValue);
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}


long read_pref_long(const char *key, const long defaultValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return defaultValue;
    }

    int64_t value = defaultValue;
    err = nvs_get_i64(handle, key, &value);
    nvs_close(handle);
    
    return (err == ESP_OK) ? (long)value : defaultValue;
}

int read_pref_int(const char *key, const int defaultValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return defaultValue;
    }

    int32_t value = defaultValue;
    err = nvs_get_i32(handle, key, &value);
    nvs_close(handle);
    
    return (err == ESP_OK) ? (int)value : defaultValue;
}

uint8_t read_pref_uint(const char *key, const uint8_t defaultValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return defaultValue;
    }

    uint8_t value = defaultValue;
    err = nvs_get_u8(handle, key, &value);
    nvs_close(handle);
    
    return (err == ESP_OK) ? value : defaultValue;
}

// Changed return type from String to char* (caller must free)
char* read_pref_string(const char *key, const char *defaultValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return strdup(defaultValue ? defaultValue : "");
    }

    size_t required_size = 0;
    err = nvs_get_str(handle, key, NULL, &required_size);
    if (err != ESP_OK || required_size == 0) {
        nvs_close(handle);
        return strdup(defaultValue ? defaultValue : "");
    }

    char *value = (char*)malloc(required_size);
    if (value == NULL) {
        nvs_close(handle);
        return strdup(defaultValue ? defaultValue : "");
    }

    err = nvs_get_str(handle, key, value, &required_size);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        free(value);
        return strdup(defaultValue ? defaultValue : "");
    }

    return value;
}

bool read_pref_bool(const char *key, const bool defaultValue, const char *nspace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nspace != NULL ? nspace : appNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return defaultValue;
    }

    uint8_t value = defaultValue ? 1 : 0;
    err = nvs_get_u8(handle, key, &value);
    nvs_close(handle);
    
    return (err == ESP_OK) ? (bool)value : defaultValue;
}

void save_geocode_data(int zipCode, double latitude, double longitude, const char * placeName, const char * stateAbr) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(geocodeNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    char coordKey[33];
    char placeKey[33];
    char geoString[65];
    char placeString[65];

    snprintf(coordKey, sizeof(coordKey), "%d_coords", zipCode);
    snprintf(geoString, sizeof(geoString), "latitude=%.6f&longitude=%.6f", latitude, longitude);
    snprintf(placeKey, sizeof(placeKey), "%d_place", zipCode);
    snprintf(placeString, sizeof(placeString), "%s, %s", placeName, stateAbr);

    nvs_set_str(handle, coordKey, geoString);
    nvs_set_str(handle, placeKey, placeString);

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Saved geocode data to prefs");
    ESP_LOGI(TAG, "%s", coordKey);
    ESP_LOGI(TAG, "%s", geoString);
}

void clear_geocode_namespace(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(geocodeNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error clearing namespace: %s", esp_err_to_name(err));
    }

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Cleared geocode namespace");
}

FriendlyGeocodeData get_saved_geocode_data(int zipCode, const char * defaultValue) {
    nvs_handle_t handle;
    FriendlyGeocodeData geoData = {NULL, NULL};

    esp_err_t err = nvs_open(geocodeNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return geoData;
    }

    char coordKey[33];
    char placeKey[33];

    snprintf(coordKey, sizeof(coordKey), "%d_coords", zipCode);
    snprintf(placeKey, sizeof(placeKey), "%d_place", zipCode);

    // Read coord string
    size_t coord_size = 0;
    err = nvs_get_str(handle, coordKey, NULL, &coord_size);
    if (err == ESP_OK && coord_size > 0) {
        char *coord_buf = (char*)malloc(coord_size);
        if (coord_buf) {
            nvs_get_str(handle, coordKey, coord_buf, &coord_size);
            geoData.friendlyAPIGeodata = coord_buf;
        }
    }

    // Read place string
    size_t place_size = 0;
    err = nvs_get_str(handle, placeKey, NULL, &place_size);
    if (err == ESP_OK && place_size > 0) {
        char *place_buf = (char*)malloc(place_size);
        if (place_buf) {
            nvs_get_str(handle, placeKey, place_buf, &place_size);
            geoData.friendlyName = place_buf;
        }
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Got saved Geocode data: ");
    if (geoData.friendlyAPIGeodata) ESP_LOGI(TAG, "%s", geoData.friendlyAPIGeodata);
    if (geoData.friendlyName) ESP_LOGI(TAG, "%s", geoData.friendlyName);

    return geoData;
}

void save_network_prefs(const char * ssid, const char * password) {
    ESP_LOGI(TAG, "Saving network credentials to prefs: %s", ssid);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(credentialsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_str(handle, ssid, password);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Saved network credentials to prefs: %s", ssid);
}

network_pref_data_t get_saved_network_prefs(const char * ssid) {
    nvs_handle_t handle;
    network_pref_data_t netdata = {NULL, NULL};

    esp_err_t err = nvs_open(credentialsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return netdata;
    }

    // Check if key exists
    size_t required_size = 0;
    err = nvs_get_str(handle, ssid, NULL, &required_size);
    if (err != ESP_OK || required_size == 0) {
        nvs_close(handle);
        return netdata;
    }

    // Allocate and read password
    char *pass_buf = (char*)malloc(required_size);
    if (pass_buf == NULL) {
        nvs_close(handle);
        return netdata;
    }

    err = nvs_get_str(handle, ssid, pass_buf, &required_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        free(pass_buf);
        return netdata;
    }

    ESP_LOGI(TAG, "Got saved SSID: %s", ssid);
    ESP_LOGI(TAG, "Got saved Password: %s", pass_buf);

    // Allocate SSID copy
    char *ssid_buf = strdup(ssid);
    if (ssid_buf == NULL) {
        free(pass_buf);
        return netdata;
    }

    netdata.ssid = ssid_buf;
    netdata.password = pass_buf;

    return netdata;
}