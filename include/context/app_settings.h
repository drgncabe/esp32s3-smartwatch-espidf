#pragma once

#include <stdint.h>

#include "api/geocode.h"
#include "api/prefs.h"

class AppSettings {
public:
    AppSettings();
    static AppSettings from_prefs();

    void load_prefs_into_app();

    int get_network_mode() const;
    int get_gyro_mode() const;
    uint8_t get_brightness() const;
    int get_haptics_enabled() const;
    int get_power_mode() const;
    int get_network_time_mode() const;
    long get_gmt_offset_sec() const;
    int get_daylight_offset_sec() const;
    int get_zip_code() const;
    int get_periodic_connect_mode() const;
    int get_hotspot_mode() const;
    const char *get_hotspot_ssid() const;
    const char *get_hotspot_password() const;

    void set_network_mode(int networkMode, bool save, bool connect = true);
    void set_brightness(uint8_t brightness, bool save);
    void set_gyro_mode(int gyroMode, bool save);
    void set_haptics_enabled(int hapticsEnabled, bool save);
    void set_power_mode(int powerMode, bool save);
    void enable_power_save(bool fromAutoLock = false);
    void disable_power_save(bool fromAutoLock = false);
    void set_network_time_mode(int networkTimeMode, bool save, bool sync = true);
    void set_gmt_offset(long gmtOffset_sec, bool save);
    void set_daylight_offset(int daylightOffset_sec, bool save);
    void set_zip_geocode_data(int zipCode, bool save, bool getFromAPI = true);
    FriendlyGeocodeData get_zip_geocode_data(int zipCode);

    void set_periodic_connect_mode(int periodicConnectMode, bool save);
    void set_hotspot_mode(int hotspotMode, bool save);
    void set_hotspot_ssid(const char *ssid, bool save);
    void set_hotspot_password(const char *password, bool save);

    bool isAutoLocked;

private:
    AppSettings(int networkMode, int gyroMode, uint8_t brightness, int hapticsEnabled, int powerMode = 0, int networkTimeMode = 1, long gmtOffset_sec = -18000, int daylightOffset_sec = 3600, int zipCode = 0, int periodicConnectMode = 0, int hotspotMode = 0, const char *hotspotSSID = "ESP32-Hotspot", const char *hotspotPassword = "12345678");

    int networkMode;
    int gyroMode;
    uint8_t brightness;
    int hapticsEnabled;
    int powerMode;
    int networkTimeMode;
    long gmtOffset_sec;
    int daylightOffset_sec;
    int zipCode;
    int periodicConnectMode;
    int hotspotMode;
    const char *hotspotSSID;
    const char *hotspotPassword;

    int prevNetworkMode;
    int prevGyroMode;
    uint8_t prevBrightness;
    int prevHapticsEnabled;
    int prevCpuFreq;
    bool prevLightSleep;
    bool hasPowerSaveSnapshot;
};
