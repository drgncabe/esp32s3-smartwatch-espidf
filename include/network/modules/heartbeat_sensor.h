#ifndef HEARTBEAT_SENSOR_H
#define HEARTBEAT_SENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_HEARTBEAT_ITEMS 60
#define MAC_STR_LEN 18
#define MAX_ASSOCIATED_MACS 8
#define MAX_ASSOCIATED_SSIDS 4
#define SSID_MAX_LEN 33

typedef uint8_t mac_addr_t[6];

typedef struct {
    mac_addr_t associated_macs[MAX_ASSOCIATED_MACS];
    uint8_t mac_count;
    char associated_ssids[MAX_ASSOCIATED_SSIDS][SSID_MAX_LEN];
    uint8_t ssid_count;
} heartbeat_associations_t;

typedef struct {
    uint8_t heartbeat_id;
    uint8_t last_mac[6];
    heartbeat_associations_t associations;
    int frequency;
    char last_ssid[SSID_MAX_LEN];
    int difference_score;
    int flagged_id_mismatch;
    uint8_t parent_mac[6];
    int flagged_frequency;
    char last_bssid[33];
    float est_distance;
} heartbeat_item_t;

typedef void (*heartbeat_sensor_cb_t)(heartbeat_item_t *, int count);

typedef struct {
    heartbeat_sensor_cb_t callback;
    int scan_interval;
    int max_items;
    int frequency_threshold;
} heartbeat_sensor_cfg_t;

bool heartbeat_sensor_start(heartbeat_sensor_cfg_t cfg);
bool heartbeat_sensor_stop(void);
void format_heartbeat_mac_address(uint8_t *mac, char *str, size_t len);
void heartbeat_sensor_clear_store(void);
float estimate_distance_from_rssi(int rssi, int txPower = -50);

extern bool is_heartbeat_sensor_running;

#ifdef __cplusplus
}
#endif

#endif
