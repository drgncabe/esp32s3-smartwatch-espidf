#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BLE_DEVICES 30
#define MAX_GATT_SERVICES 20
#define MAX_GATT_CHARS_PER_SERVICE 10

typedef enum {
    BLE_DEVICE_UNKNOWN,
    BLE_DEVICE_PHONE,
    BLE_DEVICE_WATCH,
    BLE_DEVICE_TRACKER,
    BLE_DEVICE_SMART_HOME,
    BLE_DEVICE_FITNESS,
    BLE_DEVICE_AUDIO,
    BLE_DEVICE_COMPUTER,
    BLE_DEVICE_BEACON
} ble_device_type_t;

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    int8_t rssi;
    char name[32];
    ble_device_type_t type;
    bool connectable;
    uint16_t appearance;
    uint16_t company_id;
    uint32_t last_seen;
    bool has_name;
    int adv_data_len;
    uint8_t adv_data[62];
} ble_device_t;

esp_err_t bluetooth_scanner_init(void);
esp_err_t bluetooth_scanner_start(void);
esp_err_t bluetooth_scanner_stop(void);
esp_err_t bluetooth_scanner_stop_and_wait(uint32_t timeout_ms);
int bluetooth_scanner_get_devices(ble_device_t *devices, int max_devices);
bool bluetooth_scanner_is_scanning(void);
void bluetooth_scanner_clear_devices(void);
esp_err_t bluetooth_scanner_deinit(void);

void format_ble_address(const uint8_t *addr, char *str, size_t len);
const char *get_ble_device_type_name(ble_device_type_t type);

typedef struct {
    uint16_t uuid16;
    uint8_t uuid128[16];
    char uuid_str[40];
    char name[64];
    uint16_t handle;
    uint8_t properties;
    uint8_t value[64];
    uint16_t value_len;
    bool value_read;
} ble_gatt_char_t;

typedef struct {
    uint16_t uuid16;
    uint8_t uuid128[16];
    char uuid_str[40];
    char name[64];
    uint16_t handle;
    uint16_t end_handle;
    uint8_t num_chars;
    int char_start_idx;
} ble_gatt_service_t;

int bluetooth_scanner_scan_services(const uint8_t *addr,
                                   uint8_t addr_type,
                                   ble_gatt_service_t *services,
                                   int max_services,
                                   ble_gatt_char_t *chars,
                                   int max_chars,
                                   void (*progress_callback)(int percent, const char *status));

void bluetooth_scanner_stop_gatt(void);

int bluetooth_scanner_read_characteristics(const uint8_t *addr,
                                            uint8_t addr_type,
                                            uint16_t service_uuid16,
                                            uint16_t service_handle,
                                            ble_gatt_char_t *chars,
                                            int max_chars,
                                            void (*progress_callback)(int percent, const char *status));

int bluetooth_scanner_read_char_value(const uint8_t *addr,
                                      uint8_t addr_type,
                                      uint16_t char_uuid16,
                                      uint16_t char_handle,
                                      uint8_t *value_out,
                                      uint16_t max_len,
                                      uint16_t *value_len_out);

esp_err_t bluetooth_scanner_read_char_by_uuid(const uint8_t *addr,
                                              uint8_t addr_type,
                                              uint16_t char_uuid,
                                              uint8_t *value,
                                              size_t *len);

esp_err_t bluetooth_scanner_write_char_by_uuid(const uint8_t *addr,
                                              uint8_t addr_type,
                                              uint16_t char_uuid,
                                              const uint8_t *value,
                                              size_t len);

const char *get_service_name(uint16_t uuid16);
esp_err_t bluetooth_scanner_connect_and_pair(uint8_t *device_addr, uint8_t addr_type);
esp_err_t bluetooth_scanner_disconnect(void);
bool bluetooth_scanner_is_connected(void);
bool bluetooth_scanner_get_connected_addr(uint8_t *addr_out);

#ifdef __cplusplus
}
#endif
