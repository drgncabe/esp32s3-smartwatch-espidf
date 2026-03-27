#ifndef WIFI_INIT_H
#define WIFI_INIT_H

#include <stdbool.h>
#include <time.h>

#define wifi_ssid "Fort"
#define wifi_password "bdn@2024"
#define time_ntpServer "pool.ntp.org"
#define time_gmtOffset_sec -18000
#define time_daylightOffset_sec 3600

typedef enum {
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
    WIFI_OFF,
    CONSOLE_ON
} WiFiState;

void initialize_wifi(const char *ssid, const char *password, bool force = false);
void enable_wifi(const char *ssid, const char *password);
void disable_wifi();
void power_off_wifi(void);
void change_wifi_mode(int nMode, bool connect = true);
WiFiState wifi_get_state(void);
bool wifi_is_connected(void);
void force_wifi_state_update(WiFiState newState);
void run_wifi_startup_scan();

void initialize_time();

#endif
