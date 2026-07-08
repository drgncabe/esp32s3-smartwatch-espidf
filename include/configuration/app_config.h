#pragma once

#ifndef APP_CONFIG_H
#define APP_CONFIG_H
#include "config_types.h"

// Begin Configurables (Can modify) --------------------------------------------------------------

// See config_types.h for available board types
// Changing this value will switch support between different boards
#undef BOARD_TYPE
#define BOARD_TYPE BOARD_ESP32_S3_169_V2

// ------- HOTSPOT CONFIGURATION -------
#define DEFAULT_HOTSPOT_SSID "ESP32-Hotspot"
#define DEFAULT_HOTSPOT_PASSWORD "12345678"
#define HOTSPOT_CHANNEL 1
#define HOTSPOT_MAX_CONNECTIONS 4

// ------- AP CONFIGURATION -------
#define AP_CONSOLE_SSID "ESP32-Console"
#define AP_CONSOLE_PASSWORD "12345678"

// ------- TIME CONFIGURATION -------
#define NTP_SERVER "pool.ntp.org"
#define DEFAULT_GMT_OFFSET_SEC -18000
#define DEFAULT_DAYLIGHT_OFFSET_SEC 3600

// ------- PERIODIC TASK CONFIGURATION -------
#define PERIODIC_TASK_INTERVAL_MS 1000
#define PERIODIC_TASK_STACK_SIZE 4096

// ------- TOUCH CALIBRATION -------
// If tap registering too high up, increase Y offset.
// If tap registering too far right, increase X offset.
// If x or y edges of tap area are not registering taps, increase scale.
#if BOARD_TYPE == BOARD_ESP32_S3_183
    // Recommended values
    #define TOUCH_OFFSET_X 0 
    #define TOUCH_OFFSET_Y -30 
    #define TOUCH_SCALE_X 1.0f 
    #define TOUCH_SCALE_Y 1.0f 
#elif BOARD_TYPE == BOARD_ESP32_S3_169_V2 || BOARD_TYPE == BOARD_ESP32_S3_169_V1
    #define TOUCH_OFFSET_X 0 
    #define TOUCH_OFFSET_Y 0
    #define TOUCH_SCALE_X 1.0f 
    #define TOUCH_SCALE_Y 1.0f 
#endif


// ------- BATTERY THRESHOLDS -------
// Note: These only apply to boards that do not use the AXP2101 PMIC (like the 1.69" V1/V2 boards)
// Battery voltage thresholds - adjust these for different LiPo battery types
// Standard LiPo: 3.0V (cutoff) to 4.2V (full)
// High Voltage LiPo: 3.0V (cutoff) to 4.35V (full)
#define BATTERY_FULL_VOLTAGE 4.2f      // Typical full charge voltage (4.2V standard, 4.35V for HV LiPo)
#define BATTERY_HIGH_VOLTAGE 4.1f      // High voltage threshold for charging detection (adjust if using HV LiPo)
#define BATTERY_LOW_VOLTAGE 3.6f       // Low voltage threshold (universal for most LiPo)


// End Configurables --------------------------------------------------------------
#endif
