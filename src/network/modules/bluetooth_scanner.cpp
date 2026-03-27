#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "network/modules/bluetooth_scanner.h"

static const char *TAG = "ble_scanner";

// Forward declarations
static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

// PSRAM-backed device array to keep internal RAM free
EXT_RAM_BSS_ATTR static ble_device_t tracked_devices[MAX_BLE_DEVICES];
static int device_count = 0;
static SemaphoreHandle_t device_mutex = NULL;
static bool is_scanning = false;
static bool bt_initialized = false;
static SemaphoreHandle_t scan_stop_sem = NULL;

// Connection state
static bool is_connected = false;
static uint16_t connected_conn_id = 0;
static esp_gatt_if_t connected_gatt_if = 0;
static uint8_t connected_addr[6] = {0};
static esp_ble_addr_type_t connected_addr_type = BLE_ADDR_TYPE_PUBLIC;

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE, // active scan required to get scan responses with names
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,  // 50*0.625ms = 31.25ms
    .scan_window   = 0x30,  // 30*0.625ms = 18.75ms
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE};

static ble_device_type_t infer_device_type(uint16_t appearance, uint16_t company_id, const char *name)
{
    if (appearance != 0)
    {
        if (appearance >= 64 && appearance <= 127)
            return BLE_DEVICE_PHONE;
        if (appearance >= 192 && appearance <= 255)
            return BLE_DEVICE_WATCH;
        if (appearance >= 384 && appearance <= 447)
            return BLE_DEVICE_AUDIO;
        if (appearance >= 832 && appearance <= 895)
            return BLE_DEVICE_FITNESS;
    }

    if (company_id == 0x004C)
        return BLE_DEVICE_PHONE; // Apple
    if (company_id == 0x0075)
        return BLE_DEVICE_PHONE; // Samsung
    if (company_id == 0x00E0)
        return BLE_DEVICE_PHONE; // Google

    if (name && name[0] != '\0')
    {
        if (strstr(name, "iPhone") || strstr(name, "iPad"))
            return BLE_DEVICE_PHONE;
        if (strstr(name, "Watch") || strstr(name, "watch"))
            return BLE_DEVICE_WATCH;
        if (strstr(name, "AirTag") || strstr(name, "Tile"))
            return BLE_DEVICE_TRACKER;
        if (strstr(name, "Bulb") || strstr(name, "Light") || strstr(name, "Lock"))
            return BLE_DEVICE_SMART_HOME;
        if (strstr(name, "Band") || strstr(name, "Fit") || strstr(name, "HR"))
            return BLE_DEVICE_FITNESS;
        if (strstr(name, "Buds") || strstr(name, "Pods") || strstr(name, "Speaker"))
            return BLE_DEVICE_AUDIO;
        if (strstr(name, "MacBook") || strstr(name, "Laptop"))
            return BLE_DEVICE_COMPUTER;
        if (strstr(name, "Beacon") || strstr(name, "iBeacon"))
            return BLE_DEVICE_BEACON;
    }

    return BLE_DEVICE_UNKNOWN;
}

static void parse_adv_data(uint8_t *adv_data, uint8_t adv_data_len, ble_device_t *device)
{
    uint8_t *ptr = adv_data;
    uint8_t *end = adv_data + adv_data_len;

    while (ptr < end)
    {
        uint8_t len = *ptr++;
        if (len == 0 || ptr + len > end)
            break;

        uint8_t type = *ptr++;
        uint8_t data_len = len - 1;

        switch (type)
        {
        case 0x01: // Flags
            break;

        case 0x08: // Shortened Local Name
            if (!device->has_name && data_len > 0) {
                size_t copy_len = data_len;
                if (copy_len >= sizeof(device->name)) {
                    copy_len = sizeof(device->name) - 1;
                }

                memset(device->name, 0, sizeof(device->name));
                memcpy(device->name, ptr, copy_len);
                device->name[copy_len] = '\0';
                device->has_name = true;

                for (size_t i = 0; i < copy_len; i++) {
                    if ((uint8_t)device->name[i] < 0x20 && device->name[i] != '\0') {
                        device->name[i] = '?';
                    }
                }

                ESP_LOGD(TAG, "Found shortened name: %s", device->name);
            }
            break;
            
        case 0x09: // Complete Local Name
            if (data_len > 0) {
                size_t copy_len = data_len;
                if (copy_len >= sizeof(device->name)) {
                    copy_len = sizeof(device->name) - 1;
                }

                memset(device->name, 0, sizeof(device->name));
                memcpy(device->name, ptr, copy_len);
                device->name[copy_len] = '\0';
                device->has_name = true;

                for (size_t i = 0; i < copy_len; i++) {
                    if ((uint8_t)device->name[i] < 0x20 && device->name[i] != '\0') {
                        device->name[i] = '?';
                    }
                }

                ESP_LOGD(TAG, "Found complete name: '%s' (len=%d)", device->name, data_len);
            }
            break;

        case 0x19: // Appearance
            if (data_len >= 2)
            {
                device->appearance = ptr[0] | (ptr[1] << 8);
            }
            break;

        case 0xFF: // Manufacturer Specific Data
            if (data_len >= 2)
            {
                device->company_id = ptr[0] | (ptr[1] << 8);
            }
            break;
        }

        ptr += data_len;
    }
}

static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "BLE scan parameters set");
        esp_ble_gap_start_scanning(0); // 0 = scan continuously
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(TAG, "BLE scan started successfully");
            is_scanning = true;
        }
        else
        {
            ESP_LOGE(TAG, "BLE scan start failed: %d", param->scan_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;

        switch (scan_result->scan_rst.search_evt)
        {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
        {
            if (device_mutex == NULL || !bt_initialized)
                break;
            
            if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                // Phones with random addresses may appear as multiple entries
                int existing = -1;
                for (int i = 0; i < device_count; i++)
                {
                    if (memcmp(tracked_devices[i].addr, scan_result->scan_rst.bda, 6) == 0)
                    {
                        existing = i;
                        break;
                    }
                }
                
                if (existing >= 0)
                {
                    tracked_devices[existing].rssi = scan_result->scan_rst.rssi;
                    tracked_devices[existing].last_seen = xTaskGetTickCount();

                    if (scan_result->scan_rst.adv_data_len > 0)
                    {
                        uint16_t old_company_id = tracked_devices[existing].company_id;
                        parse_adv_data(scan_result->scan_rst.ble_adv,
                                       scan_result->scan_rst.adv_data_len,
                                       &tracked_devices[existing]);
                        if (tracked_devices[existing].company_id == 0 && old_company_id != 0) {
                            tracked_devices[existing].company_id = old_company_id;
                        }
                    }
                    
                    // Scan response often contains the device name
                    if (scan_result->scan_rst.scan_rsp_len > 0)
                    {
                        uint8_t *scan_rsp_ptr = scan_result->scan_rst.ble_adv;
                        uint8_t total_len = scan_result->scan_rst.adv_data_len + scan_result->scan_rst.scan_rsp_len;
                        
                        if (scan_result->scan_rst.adv_data_len < total_len) {
                            parse_adv_data(scan_rsp_ptr + scan_result->scan_rst.adv_data_len, 
                                          scan_result->scan_rst.scan_rsp_len, 
                                          &tracked_devices[existing]);
                            
                            if (tracked_devices[existing].has_name) {
                                ESP_LOGD(TAG, "Found name in scan response: '%s'", tracked_devices[existing].name);
                            }
                        }
                    }

                    tracked_devices[existing].type = infer_device_type(
                        tracked_devices[existing].appearance,
                        tracked_devices[existing].company_id,
                        tracked_devices[existing].name);
                }
                else if (device_count < MAX_BLE_DEVICES)
                {
                    ble_device_t *dev = &tracked_devices[device_count];
                    memset(dev, 0, sizeof(ble_device_t));

                    memcpy(dev->addr, scan_result->scan_rst.bda, 6);
                    dev->addr_type = scan_result->scan_rst.ble_addr_type;
                    dev->rssi = scan_result->scan_rst.rssi;
                    dev->last_seen = xTaskGetTickCount();
                    dev->has_name = false;

                    dev->connectable = (scan_result->scan_rst.ble_evt_type == ESP_BLE_EVT_CONN_ADV ||
                                        scan_result->scan_rst.ble_evt_type == ESP_BLE_EVT_CONN_DIR_ADV);

                        if (scan_result->scan_rst.adv_data_len > 0)
                        {
                            parse_adv_data(scan_result->scan_rst.ble_adv,
                                           scan_result->scan_rst.adv_data_len,
                                           dev);
                        }
                    
                    // Scan response often contains the device name
                    if (scan_result->scan_rst.scan_rsp_len > 0)
                    {
                        uint8_t *scan_rsp_ptr = scan_result->scan_rst.ble_adv + scan_result->scan_rst.adv_data_len;
                        parse_adv_data(scan_rsp_ptr, scan_result->scan_rst.scan_rsp_len, dev);
                        
                        if (dev->has_name) {
                            ESP_LOGD(TAG, "Found name in scan response: '%s'", dev->name);
                        }
                    }

                    dev->type = infer_device_type(dev->appearance, dev->company_id, dev->name);

                    if (!dev->has_name)
                    {
                        format_ble_address(dev->addr, dev->name, sizeof(dev->name));
                        ESP_LOGD(TAG, "No name found, using MAC: %s", dev->name);
                    }
                    
                    device_count++;
                }

                xSemaphoreGive(device_mutex);
            }
            break;
        }

        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            ESP_LOGI(TAG, "BLE scan complete");
            break;

        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            is_scanning = false;
            if (scan_stop_sem)
                xSemaphoreGive(scan_stop_sem);
        }
        else
        {
            ESP_LOGE(TAG, "Scan stop failed: %d", param->scan_stop_cmpl.status);
            if (scan_stop_sem)
                xSemaphoreGive(scan_stop_sem);
        }
        break;

    default:
        break;
    }
}

static void stop_scan_and_wait(void)
{
    bluetooth_scanner_stop_and_wait(2000); // 2 second timeout
}

esp_err_t bluetooth_scanner_init(void)
{
    if (bt_initialized) {
        ESP_LOGW(TAG, "Bluetooth already initialized, skipping");
        return ESP_OK;
    }
    
    // Clean up any partial state from a previous init
    ESP_LOGI(TAG, "Ensuring clean BT state before initialization");
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    if (device_mutex == NULL)
    {
        device_mutex = xSemaphoreCreateMutex();
        if (device_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Initializing Bluetooth scanner");
    ESP_LOGI(TAG, "Free internal heap: %lu bytes", 
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    esp_err_t ret;

    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to release classic BT memory: %s", esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.bluetooth_mode = ESP_BT_MODE_BLE;
    bt_cfg.ble_max_act = 3;

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize BT controller: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable BT controller: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize Bluedroid: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable Bluedroid: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_config_local_privacy(true);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to enable local privacy: %s (continuing anyway)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Enabled local privacy (random address mode)");
    }

    ret = esp_ble_gap_register_callback(gap_callback);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register GAP callback: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gattc_register_callback(gattc_event_handler);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register GATTC callback: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gattc_app_register(0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register GATTC app: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Bluetooth scanner initialized");
    bt_initialized = true;
    
    return ESP_OK;
}

esp_err_t bluetooth_scanner_start(void)
{
    if (is_scanning)
    {
        ESP_LOGW(TAG, "BLE scan already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting BLE scan");

    bluetooth_scanner_clear_devices();

    // Must set a random address before scanning with BLE_ADDR_TYPE_RANDOM
    if (ble_scan_params.own_addr_type == BLE_ADDR_TYPE_RANDOM) {
        uint8_t rand_addr[6];
        esp_fill_random(rand_addr, 6);
        rand_addr[0] = (rand_addr[0] & 0x3F) | 0xC0; // static random address
        
        esp_err_t ret = esp_ble_gap_set_rand_addr(rand_addr);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set random address: %s (continuing anyway)", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Random address set: %02X:%02X:%02X:%02X:%02X:%02X",
                     rand_addr[0], rand_addr[1], rand_addr[2],
                     rand_addr[3], rand_addr[4], rand_addr[5]);
        }
    }

    esp_err_t ret = esp_ble_gap_set_scan_params(&ble_scan_params);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set scan params: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t bluetooth_scanner_stop(void)
{
    if (!is_scanning)
        return ESP_OK;

    ESP_LOGI(TAG, "Stopping BLE scan");

    esp_err_t ret = esp_ble_gap_stop_scanning();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop scan: %s", esp_err_to_name(ret));
        return ret;
    }
    

    return ESP_OK;
}

esp_err_t bluetooth_scanner_stop_and_wait(uint32_t timeout_ms)
{
    if (!scan_stop_sem)
        scan_stop_sem = xSemaphoreCreateBinary();
    while (xSemaphoreTake(scan_stop_sem, 0) == pdTRUE)
    {
    }

    ESP_LOGI(TAG, "Stopping BLE scan and waiting...");

    esp_err_t ret = esp_ble_gap_stop_scanning();

    // "not scanning" is fine, just proceed
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "stop_scanning failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Only wait if we think scan was active OR if ret==ESP_OK (stop request accepted)
    if (ret == ESP_OK)
    {
        if (xSemaphoreTake(scan_stop_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        {
            ESP_LOGW(TAG, "Scan stop timeout");
            return ESP_ERR_TIMEOUT;
        }
        ESP_LOGI(TAG, "Scan stop confirmed");
    }

    is_scanning = false;
    return ESP_OK;
}

int bluetooth_scanner_get_devices(ble_device_t *devices, int max_devices)
{
    if (devices == NULL || max_devices <= 0)
        return 0;

    int count = 0;

    if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        count = (device_count < max_devices) ? device_count : max_devices;
        memcpy(devices, tracked_devices, count * sizeof(ble_device_t));
        xSemaphoreGive(device_mutex);
    }

    return count;
}

bool bluetooth_scanner_is_scanning(void)
{
    return is_scanning;
}

void bluetooth_scanner_clear_devices(void)
{
    if (xSemaphoreTake(device_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        device_count = 0;
        memset(tracked_devices, 0, sizeof(tracked_devices));
        xSemaphoreGive(device_mutex);
    }
}

esp_err_t bluetooth_scanner_deinit(void)
{
    if (!bt_initialized) {
        ESP_LOGW(TAG, "Bluetooth not initialized, skipping deinit");
        return ESP_OK;
    }
    
    bt_initialized = false;
    
    bluetooth_scanner_stop();
    bluetooth_scanner_stop_gatt();
    
    // Let pending BLE events finish before tearing down
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (device_mutex != NULL)
    {
        vSemaphoreDelete(device_mutex);
        device_mutex = NULL;
    }

    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    
    vTaskDelay(pdMS_TO_TICKS(300));

    esp_err_t ble_ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ble_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release BLE memory: %s", esp_err_to_name(ble_ret));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_err_t classic_ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (classic_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release classic BT memory: %s", esp_err_to_name(classic_ret));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    device_count = 0;
    ESP_LOGI(TAG, "Bluetooth scanner deinitialized");

    return ESP_OK;
}

void format_ble_address(const uint8_t *addr, char *str, size_t len)
{
    snprintf(str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

const char *get_ble_device_type_name(ble_device_type_t type)
{
    switch (type)
    {
    case BLE_DEVICE_PHONE:
        return "Phone";
    case BLE_DEVICE_WATCH:
        return "Watch";
    case BLE_DEVICE_TRACKER:
        return "Tracker";
    case BLE_DEVICE_SMART_HOME:
        return "Smart Home";
    case BLE_DEVICE_FITNESS:
        return "Fitness";
    case BLE_DEVICE_AUDIO:
        return "Audio";
    case BLE_DEVICE_COMPUTER:
        return "Computer";
    case BLE_DEVICE_BEACON:
        return "Beacon";
    default:
        return "Unknown";
    }
}

// GATT Service Scanning Implementation

static bool gatt_scanning = false;
static SemaphoreHandle_t gatt_complete_sem = NULL;
static esp_gatt_if_t gatt_if = 0;
static uint16_t gatt_conn_id = 0;
static ble_gatt_service_t *gatt_services_buffer = NULL;
static int gatt_services_found = 0;
static int gatt_services_max = 0;
static void (*gatt_progress_cb)(int percent, const char *status) = NULL;

// State for characteristic value reading
static bool char_read_in_progress = false;
static uint8_t *char_value_buffer = NULL;
static uint16_t char_value_max_len = 0;
static uint16_t char_value_actual_len = 0;
static bool char_read_success = false;
static uint16_t char_read_handle = 0;  // Handle to read
static SemaphoreHandle_t char_read_conn_sem = NULL;  // Semaphore for connection ready

// State for characteristic discovery
static bool char_discovery_in_progress = false;
static uint16_t char_discovery_target_uuid = 0;
static uint16_t char_discovery_service_handle = 0;
static uint16_t char_discovery_service_end_handle = 0;
static SemaphoreHandle_t char_discovery_service_sem = NULL;

// Known GATT Service UUIDs
static const struct
{
    uint16_t uuid;
    const char *name;
} known_services[] = {
    {0x1800, "Generic Access"},
    {0x1801, "Generic Attribute"},
    {0x1802, "Immediate Alert"},
    {0x1803, "Link Loss"},
    {0x1804, "Tx Power"},
    {0x1805, "Current Time"},
    {0x1806, "Reference Time Update"},
    {0x1807, "Next DST Change"},
    {0x1808, "Glucose"},
    {0x1809, "Health Thermometer"},
    {0x180A, "Device Information"},
    {0x180D, "Heart Rate"},
    {0x180E, "Phone Alert Status"},
    {0x180F, "Battery Service"},
    {0x1810, "Blood Pressure"},
    {0x1811, "Alert Notification"},
    {0x1812, "Human Interface Device"},
    {0x1813, "Scan Parameters"},
    {0x1814, "Running Speed and Cadence"},
    {0x1815, "Automation IO"},
    {0x1816, "Cycling Speed and Cadence"},
    {0x1818, "Cycling Power"},
    {0x1819, "Location and Navigation"},
    {0x181A, "Environmental Sensing"},
    {0x181B, "Body Composition"},
    {0x181C, "User Data"},
    {0x181D, "Weight Scale"},
    {0x181E, "Bond Management"},
    {0x181F, "Continuous Glucose Monitoring"},
    {0, NULL}};

const char *get_service_name(uint16_t uuid16)
{
    for (int i = 0; known_services[i].name != NULL; i++)
    {
        if (known_services[i].uuid == uuid16)
        {
            return known_services[i].name;
        }
    }
    return "Unknown Service";
}

static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(TAG, "GATTC registered, app_id: %d", param->reg.app_id);
        gatt_if = gattc_if;
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK)
        {
            ESP_LOGI(TAG, "Connected to device (conn_id: %d)", param->open.conn_id);
            gatt_conn_id = param->open.conn_id;
            
            is_connected = true;
            connected_conn_id = param->open.conn_id;
            connected_gatt_if = gattc_if;
            memcpy(connected_addr, param->open.remote_bda, 6);
            
            if (char_read_in_progress)
            {
                ESP_LOGI(TAG, "Connection established for characteristic read, signaling...");
                if (char_read_conn_sem)
                {
                    xSemaphoreGive(char_read_conn_sem);
                }
            }
            else if (char_discovery_in_progress)
            {
                ESP_LOGI(TAG, "Connection established for characteristic discovery, signaling...");
                if (char_read_conn_sem)
                {
                    xSemaphoreGive(char_read_conn_sem);
                }
            }
            else if (gatt_progress_cb)
            {
                gatt_progress_cb(20, "Connected");
                esp_ble_gattc_search_service(gattc_if, param->open.conn_id, NULL);
            }
            else
            {
                esp_ble_gattc_search_service(gattc_if, param->open.conn_id, NULL);
            }
        }
        else
        {
            ESP_LOGE(TAG, "Connection failed, status: %d", param->open.status);
            is_connected = false;
            if (gatt_progress_cb)
                gatt_progress_cb(100, "Connection failed");
            gatt_scanning = false;
            char_read_in_progress = false;
            if (gatt_complete_sem)
                xSemaphoreGive(gatt_complete_sem);
            if (char_read_conn_sem)
                xSemaphoreGive(char_read_conn_sem);
        }
        break;

    case ESP_GATTC_SEARCH_RES_EVT:
    {
        if (char_discovery_in_progress)
        {
            if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16)
            {
                uint16_t found_uuid = param->search_res.srvc_id.uuid.uuid.uuid16;
                if (found_uuid == char_discovery_target_uuid)
                {
                    char_discovery_service_handle = param->search_res.start_handle;
                    char_discovery_service_end_handle = param->search_res.end_handle;
                    ESP_LOGI(TAG, "Found target service 0x%04X: handles 0x%04X-0x%04X", 
                            found_uuid, char_discovery_service_handle, char_discovery_service_end_handle);
                    if (char_discovery_service_sem)
                    {
                        xSemaphoreGive(char_discovery_service_sem);
                    }
                }
            }
        }
        
        if (gatt_services_found < gatt_services_max)
        {
            ble_gatt_service_t *svc = &gatt_services_buffer[gatt_services_found];

            if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16)
            {
                svc->uuid16 = param->search_res.srvc_id.uuid.uuid.uuid16;
                snprintf(svc->uuid_str, sizeof(svc->uuid_str), "0x%04X", svc->uuid16);
                snprintf(svc->name, sizeof(svc->name), "%s", get_service_name(svc->uuid16));
            }
            else if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128)
            {
                svc->uuid16 = 0;
                memcpy(svc->uuid128, param->search_res.srvc_id.uuid.uuid.uuid128, 16);
                snprintf(svc->uuid_str, sizeof(svc->uuid_str),
                         "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                         svc->uuid128[15], svc->uuid128[14], svc->uuid128[13], svc->uuid128[12],
                         svc->uuid128[11], svc->uuid128[10], svc->uuid128[9], svc->uuid128[8],
                         svc->uuid128[7], svc->uuid128[6], svc->uuid128[5], svc->uuid128[4],
                         svc->uuid128[3], svc->uuid128[2], svc->uuid128[1], svc->uuid128[0]);
                snprintf(svc->name, sizeof(svc->name), "Custom Service");
            }

            svc->handle = param->search_res.start_handle;
            svc->end_handle = param->search_res.end_handle;
            svc->num_chars = 0;

            gatt_services_found++;
            ESP_LOGI(TAG, "Service found: %s (UUID: %s)", svc->name, svc->uuid_str);

            if (gatt_progress_cb)
            {
                int progress = 20 + (gatt_services_found * 60 / gatt_services_max);
                gatt_progress_cb(progress, "Scanning services...");
            }
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        ESP_LOGI(TAG, "Service discovery complete, found %d services", gatt_services_found);
        
        if (char_discovery_in_progress)
        {
            ESP_LOGI(TAG, "Service discovery complete, keeping connection for char discovery");
        }
        else if (gatt_progress_cb)
        {
            gatt_progress_cb(90, "Disconnecting...");
            esp_ble_gattc_close(gattc_if, gatt_conn_id);
        }
        else
        {
            esp_ble_gattc_close(gattc_if, gatt_conn_id);
        }
        break;

    case ESP_GATTC_CLOSE_EVT:
        ESP_LOGI(TAG, "Disconnected from device");
        is_connected = false;
        connected_conn_id = 0;
        connected_gatt_if = 0;
        memset(connected_addr, 0, 6);
        if (gatt_progress_cb)
            gatt_progress_cb(100, "Complete");
        gatt_scanning = false;
        char_read_in_progress = false;
        if (gatt_complete_sem)
            xSemaphoreGive(gatt_complete_sem);
        break;
    
    case ESP_GATTC_READ_CHAR_EVT:
        if (param->read.status == ESP_GATT_OK && char_read_in_progress)
        {
            ESP_LOGI(TAG, "Characteristic read successful, %d bytes", param->read.value_len);
            
            if (char_value_buffer != NULL && param->read.value_len > 0)
            {
                uint16_t copy_len = (param->read.value_len < char_value_max_len) ? 
                                   param->read.value_len : char_value_max_len;
                memcpy(char_value_buffer, param->read.value, copy_len);
                char_value_actual_len = copy_len;
                char_read_success = true;
                
                ESP_LOGI(TAG, "Read value (hex): ");
                for (int i = 0; i < copy_len && i < 16; i++) {
                    ESP_LOGI(TAG, "%02X ", param->read.value[i]);
                }
                ESP_LOGI(TAG, "");
            }
            else if (param->read.value_len == 0)
            {
                char_value_actual_len = 0;
                char_read_success = true;
                ESP_LOGI(TAG, "Characteristic read successful (empty value)");
            }
            else
            {
                ESP_LOGW(TAG, "Read successful but no buffer or invalid length");
                char_read_success = false;
            }
            
            if (gatt_complete_sem)
            {
                xSemaphoreGive(gatt_complete_sem);
            }
            
            esp_ble_gattc_close(gattc_if, gatt_conn_id);
        }
        else
        {
            const char *status_name = "Unknown";
            switch (param->read.status)
            {
                case ESP_GATT_OK: status_name = "OK"; break;
                case ESP_GATT_INVALID_HANDLE: status_name = "Invalid Handle"; break;
                case ESP_GATT_READ_NOT_PERMIT: status_name = "Read Not Permitted"; break;
                case ESP_GATT_WRITE_NOT_PERMIT: status_name = "Write Not Permitted"; break;
                case ESP_GATT_INSUF_AUTHORIZATION: status_name = "Insufficient Authorization"; break;
                case ESP_GATT_PREPARE_Q_FULL: status_name = "Prepare Queue Full"; break;
                case ESP_GATT_NOT_FOUND: status_name = "Not Found"; break;
                case ESP_GATT_NOT_LONG: status_name = "Not Long"; break;
                case ESP_GATT_INSUF_KEY_SIZE: status_name = "Insufficient Key Size"; break;
                case ESP_GATT_INVALID_ATTR_LEN: status_name = "Invalid Attribute Length"; break;
                case ESP_GATT_ERR_UNLIKELY: status_name = "Unlikely Error"; break;
                case ESP_GATT_INSUF_ENCRYPTION: status_name = "Insufficient Encryption"; break;
                case ESP_GATT_UNSUPPORT_GRP_TYPE: status_name = "Unsupported Group Type"; break;
                case ESP_GATT_INSUF_RESOURCE: status_name = "Insufficient Resource"; break;
                default: break;
            }
            
            ESP_LOGE(TAG, "Characteristic read failed, status: %d (%s)", 
                    param->read.status, status_name);
            char_read_success = false;
            
            if (gatt_complete_sem)
            {
                xSemaphoreGive(gatt_complete_sem);
            }
            
            esp_ble_gattc_close(gattc_if, gatt_conn_id);
        }
        break;


    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Device disconnected (reason: 0x%x)", param->disconnect.reason);
        if (gatt_scanning && gatt_conn_id != 0)
        {
            gatt_scanning = false;
            if (gatt_complete_sem)
                xSemaphoreGive(gatt_complete_sem);
        }
        gatt_conn_id = 0;
        break;

    default:
        break;
    }
}

int bluetooth_scanner_scan_services(const uint8_t *addr,
                                    uint8_t addr_type,
                                    ble_gatt_service_t *services,
                                    int max_services,
                                    ble_gatt_char_t *chars,
                                    int max_chars,
                                    void (*progress_callback)(int percent, const char *status))
{
    if (addr == NULL || services == NULL || max_services <= 0)
    {
        return -1;
    }

    if (gatt_scanning)
    {
        ESP_LOGW(TAG, "GATT scan already in progress");
        return -2;
    }

    if (gatt_if == 0)
    {
        ESP_LOGE(TAG, "GATT client not registered. Init failed?");
        return -3;
    }

    gatt_scanning = true;
    gatt_services_buffer = services;
    gatt_services_found = 0;
    gatt_services_max = max_services;
    gatt_progress_cb = progress_callback;
    gatt_conn_id = 0;

    if (gatt_complete_sem == NULL)
    {
        gatt_complete_sem = xSemaphoreCreateBinary();
    }
    else
    {
        while (xSemaphoreTake(gatt_complete_sem, 0) == pdTRUE)
        {
        }
    }

    if (progress_callback)
        progress_callback(0, "Stopping scan...");

    // Stop scanner to free BLE controller resources for GATT
    stop_scan_and_wait();
    vTaskDelay(pdMS_TO_TICKS(300));

    if (gatt_conn_id != 0)
    {
        ESP_LOGI(TAG, "Closing tracked GATT connection (conn_id: %d)...", gatt_conn_id);
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
        gatt_conn_id = 0;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Close any orphaned connections
    for (int i = 0; i < 4; i++) {
        esp_ble_gattc_close(gatt_if, i);
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    if (progress_callback)
        progress_callback(10, "Connecting...");

    esp_bd_addr_t bd_addr;
    memcpy(bd_addr, addr, 6);

    ESP_LOGI(TAG, "Connecting to %02X:%02X:%02X:%02X:%02X:%02X (type: %s)",
             bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5],
             addr_type == BLE_ADDR_TYPE_PUBLIC ? "PUBLIC" : "RANDOM");

    esp_err_t ret = esp_ble_gattc_open(gatt_if, bd_addr, (esp_ble_addr_type_t)addr_type, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GATTC open failed: %s", esp_err_to_name(ret));
        gatt_scanning = false;
        return -5;
    }

    if (xSemaphoreTake(gatt_complete_sem, pdMS_TO_TICKS(15000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "GATT scan timeout on first attempt");
        
        if (gatt_services_found > 0)
        {
            ESP_LOGI(TAG, "Timeout but found %d services - returning partial results", gatt_services_found);
            gatt_scanning = false;
            if (gatt_conn_id != 0)
            {
                esp_ble_gattc_close(gatt_if, gatt_conn_id);
                vTaskDelay(pdMS_TO_TICKS(500));  // Wait for close
            }
            return gatt_services_found;
        }
        
        gatt_scanning = false;
        if (gatt_conn_id != 0)
        {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return -6;
    }

    // Try opposite address type if first attempt failed
    if (gatt_services_found == 0 && gatt_conn_id == 0)
    {
        ESP_LOGW(TAG, "Connection failed with %s address, trying %s...",
                 addr_type == BLE_ADDR_TYPE_PUBLIC ? "PUBLIC" : "RANDOM",
                 addr_type == BLE_ADDR_TYPE_PUBLIC ? "RANDOM" : "PUBLIC");

        uint8_t alt_addr_type = (addr_type == BLE_ADDR_TYPE_PUBLIC) ? BLE_ADDR_TYPE_RANDOM : BLE_ADDR_TYPE_PUBLIC;

        gatt_scanning = true;
        gatt_services_found = 0;
        gatt_conn_id = 0;

        vTaskDelay(pdMS_TO_TICKS(1000));

        if (progress_callback)
            progress_callback(10, "Retrying...");

        ret = esp_ble_gattc_open(gatt_if, bd_addr, (esp_ble_addr_type_t)alt_addr_type, true);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "GATTC open retry failed: %s", esp_err_to_name(ret));
            gatt_scanning = false;
            return -5;
        }

        if (xSemaphoreTake(gatt_complete_sem, pdMS_TO_TICKS(15000)) != pdTRUE)
        {
            ESP_LOGW(TAG, "GATT scan timeout on retry");
            
            if (gatt_services_found > 0)
            {
                ESP_LOGI(TAG, "Timeout on retry but found %d services - returning results", gatt_services_found);
                gatt_scanning = false;
                if (gatt_conn_id != 0)
                {
                    esp_ble_gattc_close(gatt_if, gatt_conn_id);
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                return gatt_services_found;
            }
            
            gatt_scanning = false;
            if (gatt_conn_id != 0)
            {
                esp_ble_gattc_close(gatt_if, gatt_conn_id);
            }
            return -6;
        }
    }

    ESP_LOGI(TAG, "GATT scan complete, found %d services", gatt_services_found);

    ESP_LOGI(TAG, "Free heap before cleanup: %lu bytes", (unsigned long)esp_get_free_heap_size());
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    for (int i = 0; i < 8; i++) {
        esp_ble_gattc_close(gatt_if, i);
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    
    ESP_LOGI(TAG, "Free heap after cleanup: %lu bytes", (unsigned long)esp_get_free_heap_size());

    ESP_LOGI(TAG, "GATT operation complete, scanning remains stopped");

    return gatt_services_found;
}

// Known GATT Characteristic UUIDs
static const struct
{
    uint16_t uuid;
    const char *name;
} known_characteristics[] = {
    {0x2A00, "Device Name"},
    {0x2A01, "Appearance"},
    {0x2A04, "Peripheral Preferred Connection Parameters"},
    {0x2A05, "Service Changed"},
    {0x2A19, "Battery Level"},
    {0x2A29, "Manufacturer Name String"},
    {0x2A24, "Model Number String"},
    {0x2A25, "Serial Number String"},
    {0x2A27, "Hardware Revision String"},
    {0x2A26, "Firmware Revision String"},
    {0x2A28, "Software Revision String"},
    {0x2A23, "System ID"},
    {0x2A37, "Heart Rate Measurement"},
    {0x2A38, "Body Sensor Location"},
    {0x2A39, "Heart Rate Control Point"},
    {0x2A49, "Blood Pressure Feature"},
    {0x2A35, "Blood Pressure Measurement"},
    {0x2A4A, "HID Information"},
    {0x2A4B, "Report Map"},
    {0x2A4C, "HID Control Point"},
    {0x2A4D, "Report"},
    {0x2A2B, "Current Time"},
    {0x2A0F, "Local Time Information"},
    {0x2A11, "Time with DST"},
    {0, NULL}};

static const char *get_characteristic_name(uint16_t uuid16)
{
    for (int i = 0; known_characteristics[i].name != NULL; i++)
    {
        if (known_characteristics[i].uuid == uuid16)
        {
            return known_characteristics[i].name;
        }
    }
    return "Unknown Characteristic";
}

int bluetooth_scanner_read_characteristics(const uint8_t *addr,
                                          uint8_t addr_type,
                                          uint16_t service_uuid16,
                                          uint16_t service_handle,
                                          ble_gatt_char_t *chars,
                                          int max_chars,
                                          void (*progress_callback)(int percent, const char *status))
{
    if (addr == NULL || chars == NULL || max_chars <= 0)
    {
        return -1;
    }

    if (gatt_scanning || char_discovery_in_progress || char_read_in_progress)
    {
        ESP_LOGW(TAG, "GATT operation already in progress");
        return -2;
    }

    if (gatt_if == 0)
    {
        ESP_LOGE(TAG, "GATT client not registered");
        return -3;
    }

    ESP_LOGI(TAG, "Discovering characteristics for service 0x%04X", service_uuid16);
    
    char_discovery_in_progress = true;
    char_discovery_target_uuid = service_uuid16;
    char_discovery_service_handle = 0;
    char_discovery_service_end_handle = 0;
    
    if (char_discovery_service_sem == NULL)
    {
        char_discovery_service_sem = xSemaphoreCreateBinary();
    }
    else
    {
        while (xSemaphoreTake(char_discovery_service_sem, 0) == pdTRUE) {}
    }
    
    memset(chars, 0, sizeof(ble_gatt_char_t) * max_chars);
    
    stop_scan_and_wait();
    vTaskDelay(pdMS_TO_TICKS(300));
    
    for (int i = 0; i < 8; i++) {
        esp_ble_gattc_close(gatt_if, i);
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    
    esp_bd_addr_t bd_addr;
    memcpy(bd_addr, addr, 6);
    
    ESP_LOGI(TAG, "Connecting for characteristic discovery...");
    esp_err_t ret = esp_ble_gattc_open(gatt_if, bd_addr, (esp_ble_addr_type_t)addr_type, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GATTC open failed: %s", esp_err_to_name(ret));
        char_discovery_in_progress = false;
        return -4;
    }
    
    if (char_read_conn_sem == NULL)
    {
        char_read_conn_sem = xSemaphoreCreateBinary();
    }
    else
    {
        while (xSemaphoreTake(char_read_conn_sem, 0) == pdTRUE) {}
    }
    
    if (xSemaphoreTake(char_read_conn_sem, pdMS_TO_TICKS(8000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Connection timeout for characteristic discovery");
        char_discovery_in_progress = false;
        if (gatt_conn_id != 0)
        {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return -5;
    }
    
    ESP_LOGI(TAG, "Connected! Discovering characteristics...");
    
    vTaskDelay(pdMS_TO_TICKS(300));
    
    if (progress_callback)
    {
        progress_callback(20, "Discovering services...");
    }
    
    // Full service search required before get_service/get_all_char will work
    ret = esp_ble_gattc_search_service(gatt_if, gatt_conn_id, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Service search failed: %s", esp_err_to_name(ret));
        char_discovery_in_progress = false;
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
        return -6;
    }
    
    ESP_LOGI(TAG, "Waiting for service 0x%04X to be discovered...", service_uuid16);
    
    if (xSemaphoreTake(char_discovery_service_sem, pdMS_TO_TICKS(5000)) == pdTRUE)
    {
        uint16_t actual_service_handle = char_discovery_service_handle;
        uint16_t actual_service_end_handle = char_discovery_service_end_handle;
        ESP_LOGI(TAG, "Using discovered service handles: 0x%04X-0x%04X", 
                 actual_service_handle, actual_service_end_handle);
    }
    else
    {
        ESP_LOGW(TAG, "Service not found via events, trying database lookup...");
        esp_bt_uuid_t service_uuid;
        service_uuid.len = ESP_UUID_LEN_16;
        service_uuid.uuid.uuid16 = service_uuid16;
        
        esp_gattc_service_elem_t service_result;
        uint16_t service_count = 1;
        ret = esp_ble_gattc_get_service(gatt_if, gatt_conn_id, &service_uuid, &service_result, &service_count, 0);
        
        if (ret == ESP_OK && service_count > 0)
        {
            char_discovery_service_handle = service_result.start_handle;
            char_discovery_service_end_handle = service_result.end_handle;
            ESP_LOGI(TAG, "Found service via database: handles 0x%04X-0x%04X", 
                     char_discovery_service_handle, char_discovery_service_end_handle);
        }
        else
        {
            ESP_LOGW(TAG, "Could not get service from database (ret: %s, count: %d)", 
                    esp_err_to_name(ret), service_count);
            ESP_LOGW(TAG, "Falling back to provided handle 0x%04X with estimated range", service_handle);
            char_discovery_service_handle = service_handle;
            char_discovery_service_end_handle = service_handle + 20;
        }
    }
    
    uint16_t actual_service_handle = char_discovery_service_handle;
    uint16_t actual_service_end_handle = char_discovery_service_end_handle;
    
    if (progress_callback)
    {
        progress_callback(30, "Discovering characteristics...");
    }
    
    ESP_LOGI(TAG, "Querying characteristics for service handles 0x%04X-0x%04X", 
             actual_service_handle, actual_service_end_handle);
    
    esp_gattc_char_elem_t *char_results = (esp_gattc_char_elem_t*)heap_caps_malloc(
        sizeof(esp_gattc_char_elem_t) * max_chars, 
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (char_results == NULL)
    {
        ESP_LOGW(TAG, "PSRAM allocation failed, trying regular malloc");
        char_results = (esp_gattc_char_elem_t*)malloc(sizeof(esp_gattc_char_elem_t) * max_chars);
    }
    
    if (char_results == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate char buffer");
        char_discovery_in_progress = false;
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
        return -7;
    }
    
    uint16_t char_count = max_chars;
    ret = esp_ble_gattc_get_all_char(gatt_if, gatt_conn_id, actual_service_handle, actual_service_end_handle,
                                     char_results, &char_count, 0);
    
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Get all char failed: %s (ret: %d)", esp_err_to_name(ret), ret);
        ESP_LOGE(TAG, "Service handles were: 0x%04X-0x%04X", actual_service_handle, actual_service_end_handle);
        free(char_results);
        char_discovery_in_progress = false;
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
        return -6;
    }
    
    if (char_count == 0)
    {
        free(char_results);
        char_discovery_in_progress = false;
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
        ESP_LOGI(TAG, "Service has no characteristics");
        return 0;
    }
    
    int chars_to_copy = (char_count < max_chars) ? char_count : max_chars;
    for (int i = 0; i < chars_to_copy; i++)
    {
        ble_gatt_char_t *ch = &chars[i];
        ch->handle = char_results[i].char_handle;
        ch->properties = char_results[i].properties;
        
        if (char_results[i].uuid.len == ESP_UUID_LEN_16)
        {
            ch->uuid16 = char_results[i].uuid.uuid.uuid16;
            snprintf(ch->uuid_str, sizeof(ch->uuid_str), "0x%04X", ch->uuid16);
            snprintf(ch->name, sizeof(ch->name), "%s", get_characteristic_name(ch->uuid16));
        }
        else if (char_results[i].uuid.len == ESP_UUID_LEN_128)
        {
            ch->uuid16 = 0;
            memcpy(ch->uuid128, char_results[i].uuid.uuid.uuid128, 16);
            snprintf(ch->uuid_str, sizeof(ch->uuid_str),
                     "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                     ch->uuid128[15], ch->uuid128[14], ch->uuid128[13], ch->uuid128[12],
                     ch->uuid128[11], ch->uuid128[10], ch->uuid128[9], ch->uuid128[8],
                     ch->uuid128[7], ch->uuid128[6], ch->uuid128[5], ch->uuid128[4],
                     ch->uuid128[3], ch->uuid128[2], ch->uuid128[1], ch->uuid128[0]);
            snprintf(ch->name, sizeof(ch->name), "Custom Characteristic");
        }
        
        ch->value_len = 0;
        ch->value_read = false;
        
        ESP_LOGI(TAG, "Characteristic found: %s (UUID: %s, Handle: 0x%04X, Props: 0x%02X)", 
                ch->name, ch->uuid_str, ch->handle, ch->properties);
    }
    
    free(char_results);
    
    if (progress_callback)
    {
        progress_callback(90, "Discovery complete");
    }
    
    esp_ble_gattc_close(gatt_if, gatt_conn_id);
    vTaskDelay(pdMS_TO_TICKS(300));
    
    char_discovery_in_progress = false;
    
    ESP_LOGI(TAG, "Characteristic discovery complete: %d characteristics found", chars_to_copy);
    return chars_to_copy;
    
}

void bluetooth_scanner_stop_gatt(void)
{
    ESP_LOGI(TAG, "Force stopping GATT operations");
    gatt_scanning = false;
    char_read_in_progress = false;
    char_discovery_in_progress = false;
    
    if (gatt_conn_id != 0)
    {
        ESP_LOGI(TAG, "Closing GATT connection (conn_id: %d)", gatt_conn_id);
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
        gatt_conn_id = 0;
    }
    
    for (int i = 0; i < 8; i++) {
        esp_ble_gattc_close(gatt_if, i);
    }
    
    gatt_services_buffer = NULL;
    gatt_services_found = 0;
    gatt_services_max = 0;
    gatt_progress_cb = NULL;
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    size_t free_heap = esp_get_free_heap_size();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Free heap after cleanup: %lu bytes, Largest block: %zu bytes", 
             (unsigned long)free_heap, largest_block);
    ESP_LOGI(TAG, "Min free heap ever: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());
    
    // Below 15KB free heap, restart the BT stack to reclaim memory
    if (free_heap < 15000)
    {
        ESP_LOGW(TAG, "Critical memory - restarting BT stack");
        
        if (gatt_if != 0) {
            esp_ble_gattc_app_unregister(gatt_if);
            gatt_if = 0;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        
        ESP_LOGI(TAG, "Re-registering GATT client");
        esp_ble_gattc_app_register(0);
        vTaskDelay(pdMS_TO_TICKS(500));
        
        free_heap = esp_get_free_heap_size();
        largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free heap after BT restart: %lu bytes, Largest block: %zu bytes", 
                 (unsigned long)free_heap, largest_block);
    }
}

int bluetooth_scanner_read_char_value(const uint8_t *addr,
                                      uint8_t addr_type,
                                      uint16_t char_uuid16,
                                      uint16_t char_handle,
                                      uint8_t *value_out,
                                      uint16_t max_len,
                                      uint16_t *value_len_out)
{
    if (addr == NULL || value_out == NULL || value_len_out == NULL || max_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (char_read_in_progress || gatt_scanning)
    {
        ESP_LOGW(TAG, "GATT operation already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (gatt_if == 0)
    {
        ESP_LOGE(TAG, "GATT client not registered");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Reading characteristic 0x%04X (handle: 0x%04X)", char_uuid16, char_handle);

    char_read_in_progress = true;
    char_value_buffer = value_out;
    char_value_max_len = max_len;
    char_value_actual_len = 0;
    char_read_success = false;
    char_read_handle = char_handle;
    gatt_conn_id = 0;

    if (gatt_complete_sem == NULL)
    {
        gatt_complete_sem = xSemaphoreCreateBinary();
    }
    else
    {
        while (xSemaphoreTake(gatt_complete_sem, 0) == pdTRUE) {}
    }
    
    if (char_read_conn_sem == NULL)
    {
        char_read_conn_sem = xSemaphoreCreateBinary();
    }
    else
    {
        while (xSemaphoreTake(char_read_conn_sem, 0) == pdTRUE) {}
    }

    stop_scan_and_wait();
    vTaskDelay(pdMS_TO_TICKS(300));

    for (int i = 0; i < 8; i++) {
        esp_ble_gattc_close(gatt_if, i);
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    esp_bd_addr_t bd_addr;
    memcpy(bd_addr, addr, 6);

    ESP_LOGI(TAG, "Connecting for characteristic read...");
    esp_err_t ret = esp_ble_gattc_open(gatt_if, bd_addr, (esp_ble_addr_type_t)addr_type, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GATTC open failed: %s", esp_err_to_name(ret));
        char_read_in_progress = false;
        return ret;
    }

    if (xSemaphoreTake(char_read_conn_sem, pdMS_TO_TICKS(8000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Connection timeout for characteristic read");
        char_read_in_progress = false;
        if (gatt_conn_id != 0)
        {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Connected! Issuing read for handle 0x%04X", char_handle);
    
    vTaskDelay(pdMS_TO_TICKS(300));
    
    ret = esp_ble_gattc_read_char(gatt_if, gatt_conn_id, char_handle, ESP_GATT_AUTH_REQ_NONE);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Read char failed: %s (ret: %d)", esp_err_to_name(ret), ret);
        ESP_LOGE(TAG, "Connection ID: %d, Handle: 0x%04X", gatt_conn_id, char_handle);
        char_read_in_progress = false;
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
        return ret;
    }
    
    ESP_LOGI(TAG, "Read command issued successfully, waiting for response...");

    if (xSemaphoreTake(gatt_complete_sem, pdMS_TO_TICKS(10000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Read operation timeout");
        char_read_in_progress = false;
        if (gatt_conn_id != 0)
        {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ESP_ERR_TIMEOUT;
    }

    if (!char_read_success)
    {
        ESP_LOGE(TAG, "Characteristic read failed");
        char_read_in_progress = false;
        return ESP_FAIL;
    }

    *value_len_out = char_value_actual_len;
    char_read_in_progress = false;
    
    ESP_LOGI(TAG, "Characteristic read complete: %d bytes", char_value_actual_len);
    return ESP_OK;
}

esp_err_t bluetooth_scanner_read_char_by_uuid(const uint8_t *addr,
                                               uint8_t addr_type,
                                               uint16_t char_uuid,
                                               uint8_t *value,
                                               size_t *len)
{
    if (!bt_initialized || !addr || !value || !len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Searching for characteristic UUID 0x%04X", char_uuid);
    
    if (is_scanning) {
        esp_ble_gap_stop_scanning();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    bool was_connected = (gatt_conn_id != 0);
    
    if (!was_connected) {
        esp_err_t connect_ret = esp_ble_gattc_open(gatt_if, (uint8_t *)addr, (esp_ble_addr_type_t)addr_type, true);
        if (connect_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(connect_ret));
            return connect_ret;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        if (gatt_conn_id == 0) {
            ESP_LOGE(TAG, "Connection failed");
            return ESP_FAIL;
        }
        
        esp_err_t ret = esp_ble_gattc_search_service(gatt_if, gatt_conn_id, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start service search: %s", esp_err_to_name(ret));
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
            return ret;
        }
        
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    
    uint16_t count = 0;
    uint16_t offset = 0;
    esp_gattc_service_elem_t *services = NULL;
    
    esp_err_t ret = esp_ble_gattc_get_attr_count(gatt_if, gatt_conn_id,
                                        ESP_GATT_DB_ALL,
                                        0, 0, 0, &count);
    
    if (ret != ESP_OK || count == 0) {
        ESP_LOGW(TAG, "No services found");
        if (!was_connected) {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Found %d services", count);
    
    services = (esp_gattc_service_elem_t*)malloc(sizeof(esp_gattc_service_elem_t) * count);
    if (!services) {
        if (!was_connected) {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ESP_ERR_NO_MEM;
    }
    
    ret = esp_ble_gattc_get_service(gatt_if, gatt_conn_id, NULL, services, &count, offset);
    if (ret != ESP_OK) {
        free(services);
        if (!was_connected) {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ret;
    }
    
    bool found = false;
    uint16_t char_handle = 0;
    
    for (int i = 0; i < count && !found; i++) {
        uint16_t char_count = 0;
        ret = esp_ble_gattc_get_attr_count(gatt_if, gatt_conn_id,
                                            ESP_GATT_DB_CHARACTERISTIC,
                                            services[i].start_handle,
                                            services[i].end_handle,
                                            0, &char_count);
        
        if (ret != ESP_OK || char_count == 0) continue;
        
        esp_gattc_char_elem_t *chars = (esp_gattc_char_elem_t*)malloc(
            sizeof(esp_gattc_char_elem_t) * char_count);
        
        if (!chars) continue;
        
        ret = esp_ble_gattc_get_all_char(gatt_if, gatt_conn_id,
                                          services[i].start_handle,
                                          services[i].end_handle,
                                          chars, &char_count, 0);
        
        if (ret == ESP_OK) {
            for (int j = 0; j < char_count; j++) {
                if (chars[j].uuid.len == ESP_UUID_LEN_16 &&
                    chars[j].uuid.uuid.uuid16 == char_uuid) {
                    char_handle = chars[j].char_handle;
                    found = true;
                    ESP_LOGI(TAG, "Found characteristic 0x%04X at handle 0x%04X", 
                             char_uuid, char_handle);
                    break;
                }
            }
        }
        
        free(chars);
    }
    
    free(services);
    
    if (!found) {
        ESP_LOGW(TAG, "Characteristic UUID 0x%04X not found", char_uuid);
        if (!was_connected) {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ESP_ERR_NOT_FOUND;
    }
    
    uint16_t value_len_out = 0;
    ret = bluetooth_scanner_read_char_value(addr, (esp_ble_addr_type_t)addr_type, char_uuid, char_handle,
                                             value, (uint16_t)*len, &value_len_out);
    
    *len = value_len_out;
    
    if (!was_connected) {
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    return ret;
}

esp_err_t bluetooth_scanner_write_char_by_uuid(const uint8_t *addr,
                                                uint8_t addr_type,
                                                uint16_t char_uuid,
                                                const uint8_t *value,
                                                size_t len)
{
    if (!bt_initialized || !addr || !value || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Writing to characteristic UUID 0x%04X (len=%d)", char_uuid, len);
    
    if (is_scanning) {
        esp_ble_gap_stop_scanning();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    bool was_connected = (gatt_conn_id != 0);
    
    if (!was_connected) {
        esp_err_t connect_ret = esp_ble_gattc_open(gatt_if, (uint8_t *)addr, (esp_ble_addr_type_t)addr_type, true);
        if (connect_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(connect_ret));
            return connect_ret;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        if (gatt_conn_id == 0) {
            ESP_LOGE(TAG, "Connection failed");
            return ESP_FAIL;
        }
        
        esp_err_t ret = esp_ble_gattc_search_service(gatt_if, gatt_conn_id, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start service search: %s", esp_err_to_name(ret));
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
            return ret;
        }
        
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    
    uint16_t count = 0;
    uint16_t offset = 0;
    esp_gattc_service_elem_t *services = NULL;
    
    esp_err_t ret = esp_ble_gattc_get_attr_count(gatt_if, gatt_conn_id,
                                        ESP_GATT_DB_ALL,
                                        0, 0, 0, &count);
    
    if (ret != ESP_OK || count == 0) {
        ESP_LOGW(TAG, "No services found");
        if (!was_connected) {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ESP_ERR_NOT_FOUND;
    }
    
    services = (esp_gattc_service_elem_t*)malloc(sizeof(esp_gattc_service_elem_t) * count);
    if (!services) {
        if (!was_connected) {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ESP_ERR_NO_MEM;
    }
    
    ret = esp_ble_gattc_get_service(gatt_if, gatt_conn_id, NULL, services, &count, offset);
    if (ret != ESP_OK) {
        free(services);
        if (!was_connected) {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ret;
    }
    
    bool found = false;
    uint16_t char_handle = 0;
    
    for (int i = 0; i < count && !found; i++) {
        uint16_t char_count = 0;
        ret = esp_ble_gattc_get_attr_count(gatt_if, gatt_conn_id,
                                            ESP_GATT_DB_CHARACTERISTIC,
                                            services[i].start_handle,
                                            services[i].end_handle,
                                            0, &char_count);
        
        if (ret != ESP_OK || char_count == 0) continue;
        
        esp_gattc_char_elem_t *chars = (esp_gattc_char_elem_t*)malloc(
            sizeof(esp_gattc_char_elem_t) * char_count);
        
        if (!chars) continue;
        
        ret = esp_ble_gattc_get_all_char(gatt_if, gatt_conn_id,
                                          services[i].start_handle,
                                          services[i].end_handle,
                                          chars, &char_count, 0);
        
        if (ret == ESP_OK) {
            for (int j = 0; j < char_count; j++) {
                if (chars[j].uuid.len == ESP_UUID_LEN_16 &&
                    chars[j].uuid.uuid.uuid16 == char_uuid) {
                    char_handle = chars[j].char_handle;
                    found = true;
                    ESP_LOGI(TAG, "Found characteristic 0x%04X at handle 0x%04X", 
                             char_uuid, char_handle);
                    break;
                }
            }
        }
        
        free(chars);
    }
    
    free(services);
    
    if (!found) {
        ESP_LOGW(TAG, "Characteristic UUID 0x%04X not found", char_uuid);
        if (!was_connected) {
            esp_ble_gattc_close(gatt_if, gatt_conn_id);
        }
        return ESP_ERR_NOT_FOUND;
    }
    
    ret = esp_ble_gattc_write_char(gatt_if, gatt_conn_id, char_handle,
                                     len, (uint8_t *)value,
                                     ESP_GATT_WRITE_TYPE_NO_RSP,
                                     ESP_GATT_AUTH_REQ_NONE);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Write command sent successfully");
    } else {
        ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(ret));
    }
    
    if (!was_connected) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_ble_gattc_close(gatt_if, gatt_conn_id);
    }
    
    return ret;
}

esp_err_t bluetooth_scanner_connect_and_pair(uint8_t *device_addr, uint8_t addr_type)
{
    if (device_addr == NULL) {
        ESP_LOGE(TAG, "Invalid device address");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (is_connected) {
        ESP_LOGI(TAG, "Disconnecting existing connection first...");
        bluetooth_scanner_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    ESP_LOGI(TAG, "Connecting to device %02X:%02X:%02X:%02X:%02X:%02X (type: %d)",
             device_addr[0], device_addr[1], device_addr[2],
             device_addr[3], device_addr[4], device_addr[5], addr_type);
    
    if (is_scanning) {
        bluetooth_scanner_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    if (gatt_if == ESP_GATT_IF_NONE) {
        ESP_LOGE(TAG, "GATT client not registered");
        return ESP_ERR_INVALID_STATE;
    }
    
    connected_addr_type = (esp_ble_addr_type_t)addr_type;
    
    esp_err_t ret = esp_ble_gattc_open(gatt_if, device_addr, (esp_ble_addr_type_t)addr_type, true);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Connection initiated, waiting for GATTC_OPEN_EVT...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    return ESP_OK;
}

esp_err_t bluetooth_scanner_disconnect(void)
{
    if (!is_connected) {
        ESP_LOGW(TAG, "Not connected, nothing to disconnect");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Disconnecting from device...");
    
    esp_err_t ret = esp_ble_gattc_close(connected_gatt_if, connected_conn_id);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Disconnect command sent");
    } else {
        ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
        is_connected = false;
        connected_conn_id = 0;
        connected_gatt_if = 0;
        memset(connected_addr, 0, 6);
    }
    
    return ret;
}

bool bluetooth_scanner_is_connected(void)
{
    return is_connected;
}

bool bluetooth_scanner_get_connected_addr(uint8_t *addr_out)
{
    if (!is_connected || addr_out == NULL) {
        return false;
    }
    
    memcpy(addr_out, connected_addr, 6);
    return true;
}
