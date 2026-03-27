#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "context/app_context.h"
#include "core/network/time_init.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "time_init";

static const uint32_t TIME_CACHE_INTERVAL_MS = 30 * 1000;
static uint32_t last_time_cache_ms = 0;
static bool time_cached_valid = false;
static uint32_t current_boot_count = 0;

static const char *ntp_server = NTP_SERVER;

void increment_boot_count()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("time", NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        err = nvs_get_u32(nvs, "boot_count", &current_boot_count);
        if (err != ESP_OK)
            current_boot_count = 0;

        current_boot_count++;
        nvs_set_u32(nvs, "boot_count", current_boot_count);
        nvs_commit(nvs);
        nvs_close(nvs);

        ESP_LOGI(TAG, "Boot count: %lu", current_boot_count);
    }
}

void save_time_from_ntp(time_t unix_time)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("time", NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        nvs_set_u64(nvs, "unix", (uint64_t)unix_time);
        nvs_set_u64(nvs, "uptime", esp_timer_get_time());
        nvs_set_u32(nvs, "boot_count", current_boot_count);
        nvs_commit(nvs);
        nvs_close(nvs);

        time_cached_valid = true;
        ESP_LOGI(TAG, "Cached time saved: %lld", unix_time);
    }
}

bool get_cached_time(time_t &out_time)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("time", NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "No cached time found");
        return false;
    }

    uint64_t last_unix = 0;
    uint64_t last_uptime = 0;
    uint32_t saved_boot_count = 0;

    nvs_get_u64(nvs, "unix", &last_unix);
    nvs_get_u64(nvs, "uptime", &last_uptime);
    nvs_get_u32(nvs, "boot_count", &saved_boot_count);
    nvs_close(nvs);

    if (last_unix == 0)
    {
        ESP_LOGI(TAG, "No cached time found");
        return false;
    }

    uint64_t now_uptime = esp_timer_get_time();
    bool different_boot = (saved_boot_count != current_boot_count);

    if (different_boot)
    {
        out_time = (time_t)last_unix;
        ESP_LOGI(TAG, "Different boot - using stale time: %lld", out_time);
        return true;
    }

    if (now_uptime < last_uptime)
    {
        out_time = (time_t)last_unix;
        ESP_LOGI(TAG, "Uptime wrapped - using last time");
        return true;
    }

    uint64_t delta_sec = (now_uptime - last_uptime) / 1000000ULL;
    out_time = (time_t)(last_unix + delta_sec);

    ESP_LOGI(TAG, "Same boot - cached time: %lld (+%llus)", out_time, delta_sec);
    return true;
}

bool sync_cached_time()
{
    time_t cached_time;
    if (!get_cached_time(cached_time))
    {
        ESP_LOGI(TAG, "No cached time available");
        return false;
    }

    long gmt_offset_sec = g_settings.get_gmt_offset_sec();
    int daylight_offset_sec = g_settings.get_daylight_offset_sec();
    
    int std_offset_hours = -(gmt_offset_sec / 3600);
    int dst_offset_hours = -((gmt_offset_sec + daylight_offset_sec) / 3600);
    
    static char tz_string[64];
    snprintf(tz_string, sizeof(tz_string), "EST%dEDT%d,M3.2.0/2:00:00,M11.1.0/2:00:00", 
             std_offset_hours, dst_offset_hours);
    
    ESP_LOGI(TAG, "Setting timezone for cached time: %s (GMT offset: %ld, DST offset: %d)", 
             tz_string, gmt_offset_sec, daylight_offset_sec);
    setenv("TZ", tz_string, 1);
    tzset();

    // Set the system time
    struct timeval tv = {
        .tv_sec = cached_time,
        .tv_usec = 0};

    settimeofday(&tv, nullptr);
    time_cached_valid = true;

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    ESP_LOGI(TAG, "System time restored from cache: %lld", cached_time);
    ESP_LOGI(TAG, "Local time after restore: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    return true;
}

void maybe_cache_time(bool force)
{
    if (!time_cached_valid && !force)
        return;

    uint32_t now = esp_timer_get_time() / 1000;
    if (now - last_time_cache_ms < TIME_CACHE_INTERVAL_MS && !force)
        return;

    time_t now_unix = time(nullptr);

    struct tm tmp;
    localtime_r(&now_unix, &tmp);
    if ((tmp.tm_year + 1900) < 2024)
    {
        ESP_LOGI(TAG, "Time sanity check failed - not caching");
        return;
    }

    save_time_from_ntp(now_unix);
    last_time_cache_ms = now;

    ESP_LOGI(TAG, "Cached time updated: %lld", now_unix);
}

void set_manual_time(int year, int month, int day, int hour, int minute, int second)
{
    struct tm timeinfo = {};
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;

    time_t t = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, NULL);
}

void config_time(long gmtOffset_sec, int daylightOffset_sec, const char *server)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    esp_sntp_init();
}

bool get_loc_time(struct tm *info, uint32_t *us, uint32_t timeout_ms)
{
    time_t now;
    time(&now);
    localtime_r(&now, info);
    if (us)
        *us = 0;
    return now > 1609459200; // After 2021
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized from NTP");
    time_t now = tv->tv_sec;
    save_time_from_ntp(now);
    maybe_cache_time(true);
}

bool initialize_network_time(int gmt_offset_sec, int daylight_offset_sec, bool bypass_settings)
{
    bool use_network_time = g_settings.get_network_time_mode() == 1 || bypass_settings;

    if(!use_network_time)
    {
        ESP_LOGI(TAG, "Network time mode is disabled, using cached time");
        sync_cached_time();
        return true;
    }

    ESP_LOGI(TAG, "Initializing SNTP with GMT offset: %d, Daylight offset: %d", (int)gmt_offset_sec, daylight_offset_sec);

    esp_sntp_stop();
    
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // POSIX TZ: offset sign is inverted (UTC-5 = +5 in POSIX)
    int std_offset_hours = -(gmt_offset_sec / 3600);
    int dst_offset_hours = -((gmt_offset_sec + daylight_offset_sec) / 3600);
    
    static char tz_string[64];
    snprintf(tz_string, sizeof(tz_string), "EST%dEDT%d,M3.2.0/2:00:00,M11.1.0/2:00:00", 
             std_offset_hours, dst_offset_hours);
    
    ESP_LOGI(TAG, "Setting timezone: %s (GMT offset: %d, DST offset: %d)", 
             tz_string, (int)gmt_offset_sec, daylight_offset_sec);
    setenv("TZ", tz_string, 1);
    tzset();

    int retry = 0;
    const int max_retry = 150; // 30s
    bool sync_completed = false;
    
    while (retry < max_retry)
    {
        sntp_sync_status_t sync_status = esp_sntp_get_sync_status();
        if (sync_status == SNTP_SYNC_STATUS_COMPLETED)
        {
            sync_completed = true;
            break;
        }
        
        if (retry % 10 == 0)
        {
            ESP_LOGI(TAG, "Waiting for time sync... (%d/%d)", retry, max_retry);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        retry++;
    }

    if (!sync_completed)
    {
        ESP_LOGW(TAG, "Time sync did not complete within %d seconds", max_retry * 200 / 1000);
        return false;
    }
    
    ESP_LOGI(TAG, "Time sync completed after %d attempts", retry);
    
    maybe_cache_time(true);
    
    {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "Time synchronized: %s", asctime(&timeinfo));
        
        struct tm utc_tm;
        gmtime_r(&now, &utc_tm);
        ESP_LOGI(TAG, "UTC time: %s", asctime(&utc_tm));
        ESP_LOGI(TAG, "Local time (with timezone): %s", asctime(&timeinfo));
        
        int utc_hour = utc_tm.tm_hour;
        int local_hour = timeinfo.tm_hour;
        int actual_hour_diff = local_hour - utc_hour;
        if (timeinfo.tm_mday != utc_tm.tm_mday) {
            if (timeinfo.tm_mday < utc_tm.tm_mday)
                actual_hour_diff -= 24;
            else
                actual_hour_diff += 24;
        }
        
        // Rough DST check (March through November)
        bool is_dst = (timeinfo.tm_mon >= 2 && timeinfo.tm_mon <= 10) || 
                      (timeinfo.tm_mon == 2 && timeinfo.tm_mday >= 8) ||
                      (timeinfo.tm_mon == 10 && timeinfo.tm_mday <= 7);
        
        int expected_offset_hours = -((int)gmt_offset_sec + (is_dst ? daylight_offset_sec : 0)) / 3600;
        
        ESP_LOGI(TAG, "Hour difference (local - UTC): %d, Expected offset: %d hours", actual_hour_diff, expected_offset_hours);
        
        // If TZ string didn't take effect, manually adjust
        if (abs(actual_hour_diff) < 1 && expected_offset_hours != 0) {
            ESP_LOGW(TAG, "Timezone not applied, manually adjusting");
            int total_offset = (int)gmt_offset_sec + (is_dst ? daylight_offset_sec : 0);
            struct timeval tv = {.tv_sec = now + total_offset, .tv_usec = 0};
            settimeofday(&tv, NULL);
            setenv("TZ", "UTC", 1);
            tzset();
            time_t adjusted;
            time(&adjusted);
            struct tm adjusted_tm;
            localtime_r(&adjusted, &adjusted_tm);
            ESP_LOGI(TAG, "Time manually adjusted to: %s", asctime(&adjusted_tm));
        }
    }
    
    return true;
}

