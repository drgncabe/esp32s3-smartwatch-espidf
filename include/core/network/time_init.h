#ifndef TIME_INIT_H
#define TIME_INIT_H

#include <stdbool.h>
#include <time.h>

#include "configuration/app_config.h"

bool initialize_network_time(int gmt_offset_sec = 0, int daylight_offset_sec = 0, bool bypass_settings = false);
bool sync_cached_time();
void maybe_cache_time(bool force = false);
void increment_boot_count();
void set_manual_time(int year, int month, int day, int hour, int minute, int second);

void save_time_from_ntp(time_t unix_time);
bool get_cached_time(time_t &out_time);
void config_time(long gmtOffset_sec, int daylightOffset_sec, const char *server);
bool get_loc_time(struct tm *info, uint32_t *us, uint32_t timeout_ms);

#endif
