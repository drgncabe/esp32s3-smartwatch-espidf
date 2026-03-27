#pragma once

#ifndef POWER_MGMT_H
#define POWER_MGMT_H

#include <stdbool.h>

void init_battery_adc(void);
int get_battery_percentage(void);
float get_battery_voltage(void);
bool usb_connected(void);
bool is_charging(void);
void power_off_watch(void);

typedef void (*pwr_off_callback_t)(void);
typedef void (*pwr_low_callback_t)(void);

void set_pwr_off_callback(pwr_off_callback_t callback);
void set_pwr_low_callback(pwr_low_callback_t callback);

extern float voltageGlobal;
extern float last_vGlobal;
extern float max_v_seen;

#endif
