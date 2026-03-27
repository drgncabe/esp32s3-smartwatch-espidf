#pragma once

#include <stdbool.h>
#include <stdint.h>

void set_brightness(uint8_t level);
void check_timed_tap_events(bool force = false);
void register_last_tap(void);
void i2c_scanner(void);
uint32_t get_cpu_freq_mhz(void);
void set_cpu_freq_mhz(uint32_t freq_mhz);
