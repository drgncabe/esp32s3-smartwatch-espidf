/***************************************************************************************
 *  File        : napt_interface.h
 *  Description : ESP32 internet sharing with NAT and DNS forwarding
 *  Author      : Noah Clark
 *  Created     : 2026-01-29
 *--------------------------------------------------------------------------------------
 *  Part of the ESP-32 Smartwatch Firmware
 *--------------------------------------------------------------------------------------
 *  Notes:
 *   - Relevant build flags and sdkconfig settings are needed for this to work properly.
 ***************************************************************************************/

#ifndef NAPT_INTERFACE_H
#define NAPT_INTERFACE_H

#include <stdbool.h>

#include "configuration/app_config.h"

void enable_hotspot(const char *ssid = nullptr, const char *password = nullptr);
void disable_hotspot(void);
bool is_hotspot_enabled(void);

#endif
