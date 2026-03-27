#ifndef SCANNER_H
#define SCANNER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_wifi_types.h"

struct WifiEntry {
    char ssid[33];
    bool open;
};

void start_wifi_scan(void);
bool wifi_scan_complete(void);
int get_wifi_scan_count(void);
const char *get_wifi_ssid(int i);
bool wifi_is_open(int i);
int8_t get_wifi_rssi(int i);
wifi_auth_mode_t get_wifi_auth_mode(int i);
void cleanup_wifi_scan(void);

#endif
