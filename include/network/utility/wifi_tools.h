#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TRACKED_DEVICES 60
#define DWELL_TIME_MS_COMMON 1000
#define DWELL_TIME_MS_UNCOMMON 600

typedef enum {
    DEVICE_TYPE_AP,
    DEVICE_TYPE_CLIENT
} device_type_t;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    char ssid[33];
    uint8_t channel;
    device_type_t type;
    uint8_t ap_mac[6];
    uint8_t bssid_mac[6];
    char bssid[33];
    uint32_t last_seen;
    uint16_t packet_count;
    char vendor[32];
    uint16_t sequence_num;
    uint8_t data_rate;
    bool has_ht_caps;
    bool has_vht_caps;
    uint8_t encryption;
    bool is_associated;
    bool is_authenticated;
    uint8_t probe_count;
    uint8_t data_count;
} wifi_device_t;

esp_err_t wifi_pentest_init(void);
esp_err_t wifi_pentest_start_scan(void);
esp_err_t wifi_pentest_start_scan_apsta(void);
esp_err_t wifi_pentest_stop_scan(void);
esp_err_t wifi_pentest_stop_scan_apsta(void);
int wifi_pentest_get_devices(wifi_device_t *devices, int max_devices);
bool wifi_pentest_is_scanning(void);
esp_err_t wifi_pentest_deinit(void);

void format_mac_address(const uint8_t *mac, char *str, size_t len);
const char *get_encryption_name(uint8_t enc_type);

#ifdef __cplusplus
}
#endif
