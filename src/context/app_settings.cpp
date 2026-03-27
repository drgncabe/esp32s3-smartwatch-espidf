#include <stdlib.h>
#include <string.h>

#include "api/geocode.h"
#include "api/gyroscope.h"
#include "api/prefs.h"
#include "configuration/app_config.h"
#include "context/app_context.h"
#include "context/app_settings.h"
#include "core/network/napt_interface.h"
#include "core/network/wifi_init.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "soc/rtc.h"
#include "util/util.h"

static const char *TAG = "app_settings";

AppSettings::AppSettings()
    : networkMode(0),
      gyroMode(0),
      brightness(255),
      hapticsEnabled(1),
      powerMode(0),
      networkTimeMode(1),
      gmtOffset_sec(DEFAULT_GMT_OFFSET_SEC),
      daylightOffset_sec(DEFAULT_DAYLIGHT_OFFSET_SEC),
      zipCode(0),
      periodicConnectMode(0),
      hotspotMode(0),
      hotspotSSID(DEFAULT_HOTSPOT_SSID),
      hotspotPassword(DEFAULT_HOTSPOT_PASSWORD)
{
    hasPowerSaveSnapshot = false;
    isAutoLocked = false;
}

AppSettings::AppSettings(int networkMode, int gyroMode, uint8_t bright, int hapticsEnabled, int powerMode, int networkTimeMode, long gmtOffset_sec, int daylightOffset_sec, int zipCode, int periodicConnectMode, int hotspotMode, const char *hotspotSSID, const char *hotspotPassword)
    : networkMode(networkMode),
      gyroMode(gyroMode),
      brightness(bright),
      hapticsEnabled(hapticsEnabled),
      powerMode(powerMode),
      networkTimeMode(networkTimeMode),
      gmtOffset_sec(gmtOffset_sec),
      daylightOffset_sec(daylightOffset_sec),
      zipCode(zipCode),
      periodicConnectMode(periodicConnectMode),
      hotspotMode(hotspotMode),
      hotspotSSID(hotspotSSID),
      hotspotPassword(hotspotPassword)
{
    hasPowerSaveSnapshot = false;
    isAutoLocked = false;
}

// -------- FACTORY --------
AppSettings AppSettings::from_prefs()
{
    int networkMode = read_pref_int("networkMode", 0);
    networkMode = networkMode == 2 ? 0 : networkMode;
    int gyroMode = read_pref_int("gyroMode", 0);
    uint8_t brightness = read_pref_uint("brightness", 255);
    int hapticsEnabled = read_pref_int("hapticsEnabled", 1);

    int networkTimeMode = read_pref_int("networkTimeMode", 1);
    long gmtOffset_sec = read_pref_long("gmtOfst", DEFAULT_GMT_OFFSET_SEC);
    int daylightOffset_sec = read_pref_int("dayOfst", DEFAULT_DAYLIGHT_OFFSET_SEC);
    int zipCode = read_pref_int("zipCode", 0);
    int periodicConnectMode = read_pref_int("periodicConMode", 0);
    int hotspotMode = read_pref_int("hotspotMode", 0);
    char *hotspotSSID = read_pref_string("hotspotSSID", DEFAULT_HOTSPOT_SSID);
    char *hotspotPassword = read_pref_string("hotspotPassword", DEFAULT_HOTSPOT_PASSWORD);

    ESP_LOGI("AppSettings", "GMT offset: %ld", gmtOffset_sec);
    ESP_LOGI("AppSettings", "Daylight offset: %d", daylightOffset_sec);
    ESP_LOGI("AppSettings", "Network mode: %d", networkMode);

    static char hotspot_ssid_buf[33];
    static char hotspot_pass_buf[65];

    strncpy(hotspot_ssid_buf, hotspotSSID, sizeof(hotspot_ssid_buf));
    hotspot_ssid_buf[sizeof(hotspot_ssid_buf) - 1] = '\0';

    strncpy(hotspot_pass_buf, hotspotPassword, sizeof(hotspot_pass_buf));
    hotspot_pass_buf[sizeof(hotspot_pass_buf) - 1] = '\0';

    free(hotspotSSID);
    free(hotspotPassword);

    int powerMode = read_pref_int("powerMode", 0);

    return AppSettings(networkMode, gyroMode, brightness, hapticsEnabled, powerMode, networkTimeMode, gmtOffset_sec, daylightOffset_sec, zipCode, periodicConnectMode, hotspotMode, hotspot_ssid_buf, hotspot_pass_buf);
}

// -------- Setters --------
void AppSettings::set_gyro_mode(int gMode, bool save)
{
    gyroMode = gMode;
    if (gyroMode == 1)
    {
        enable_gyroscope();
    }
    else
    {
        disable_gyroscope();
    }
    if (save)
    {
        write_pref_int("gyroMode", &gyroMode, NULL);
    }
}

void AppSettings::set_network_mode(int nMode, bool save, bool connect)
{
    networkMode = nMode;
    change_wifi_mode(nMode, connect);
    if (save && nMode != 2)
    {
        write_pref_int("networkMode", &networkMode, NULL);
    }
}

void AppSettings::set_brightness(uint8_t bLevel, bool save)
{
    brightness = bLevel;
    g_display.set_brightness(brightness);
    if (save)
    {
        write_pref_uint("brightness", &brightness, NULL);
    }
}

void AppSettings::set_haptics_enabled(int he, bool save)
{
    hapticsEnabled = he;
    if (save)
    {
        write_pref_int("powerMode", &powerMode, NULL);
    }
}

void AppSettings::set_power_mode(int pMode, bool save)
{
    if (pMode == powerMode)
        return;

    if (pMode == 1)
        enable_power_save();
    else
        disable_power_save();
}

void AppSettings::set_network_time_mode(int nTimeMode, bool save, bool sync)
{
    networkTimeMode = nTimeMode;
    if (sync)
    {
        initialize_time();
    }
    if (save)
    {
        write_pref_int("networkTimeMode", &networkTimeMode, NULL);
    }
}

void AppSettings::set_gmt_offset(long gmtOffset_sec, bool save)
{
    this->gmtOffset_sec = gmtOffset_sec;
    if (save)
    {
        write_pref_long("gmtOfst", &gmtOffset_sec, NULL);
    }
}
void AppSettings::set_daylight_offset(int daylightOffset_sec, bool save)
{
    this->daylightOffset_sec = daylightOffset_sec;
    if (save)
    {
        write_pref_int("dayOfst", &daylightOffset_sec, NULL);
    }
}

void AppSettings::set_zip_geocode_data(int zipCode, bool save, bool getFromAPI)
{
    this->zipCode = zipCode;

    FriendlyGeocodeData friendlyGeocodeData = get_saved_geocode_data(zipCode, "");
    if (friendlyGeocodeData.friendlyAPIGeodata != NULL && friendlyGeocodeData.friendlyName != NULL && strlen(friendlyGeocodeData.friendlyAPIGeodata) > 0 && strlen(friendlyGeocodeData.friendlyName) > 0)
    {
        ESP_LOGI(TAG, "Geocode data found in prefs");
        ESP_LOGI(TAG, "%s", friendlyGeocodeData.friendlyAPIGeodata);
        ESP_LOGI(TAG, "%s", friendlyGeocodeData.friendlyName);
        // Free the dynamically allocated strings
        free(const_cast<char *>(friendlyGeocodeData.friendlyAPIGeodata));
        free(const_cast<char *>(friendlyGeocodeData.friendlyName));
    }
    else
    {
        if (wifi_is_connected())
        {
            ESP_LOGI(TAG, "Geocode data not found in prefs, getting from API");
            // Free the dynamically allocated strings (only if not NULL)
            if (friendlyGeocodeData.friendlyAPIGeodata != NULL)
            {
                free(const_cast<char *>(friendlyGeocodeData.friendlyAPIGeodata));
            }
            if (friendlyGeocodeData.friendlyName != NULL)
            {
                free(const_cast<char *>(friendlyGeocodeData.friendlyName));
            }
            if (getFromAPI)
            {
                GeocodeData geocodeData = get_lat_lon_from_zip(zipCode);
                if ((geocodeData.latitude > 0 || geocodeData.latitude < 0) && (geocodeData.longitude > 0 || geocodeData.longitude < 0))
                {
                    save_geocode_data(zipCode, geocodeData.latitude, geocodeData.longitude, geocodeData.placeName, geocodeData.stateAbr);
                }
                else
                {
                    ESP_LOGI(TAG, "Geocode data not found in API");
                }
            }
        }else{
            ESP_LOGI(TAG, "WiFi not connected, can't get geocode data from API");
        }
    }

    if (save)
    {
        write_pref_int("zipCode", &zipCode, NULL);
        ESP_LOGI(TAG, "Saved zip code and geocode data to prefs");
    }
}

void AppSettings::set_periodic_connect_mode(int periodicConnectMode, bool save)
{
    this->periodicConnectMode = periodicConnectMode;
    if (save)
    {
        write_pref_int("periodicConMode", &periodicConnectMode, NULL);
    }
}

void AppSettings::set_hotspot_mode(int hotspotMode, bool save)
{
    this->hotspotMode = hotspotMode;
    if (hotspotMode == 1)
    {
        enable_hotspot(hotspotSSID, hotspotPassword);
    }
    else
    {
        disable_hotspot();
    }
    if (save)
    {
        write_pref_int("hotspotMode", &hotspotMode, NULL);
    }
}

void AppSettings::set_hotspot_ssid(const char *ssid, bool save)
{
    this->hotspotSSID = ssid;
    if (save)
    {
        write_pref_char("hotspotSSID", ssid, NULL);
    }
}

void AppSettings::set_hotspot_password(const char *password, bool save)
{
    this->hotspotPassword = password;
    if (save)
    {
        write_pref_char("hotspotPassword", password, NULL);
    }
}

// -------- Getters --------

const char *AppSettings::get_hotspot_ssid() const
{
    return hotspotSSID;
}
const char *AppSettings::get_hotspot_password() const
{
    return hotspotPassword;
}

int AppSettings::get_hotspot_mode() const
{
    return hotspotMode;
}

int AppSettings::get_periodic_connect_mode() const
{
    return periodicConnectMode;
}

FriendlyGeocodeData AppSettings::get_zip_geocode_data(int zipCode)
{
    FriendlyGeocodeData friendlyGeocodeData = get_saved_geocode_data(zipCode, "");
    if (friendlyGeocodeData.friendlyAPIGeodata != NULL && friendlyGeocodeData.friendlyName != NULL && strlen(friendlyGeocodeData.friendlyAPIGeodata) > 0 && strlen(friendlyGeocodeData.friendlyName) > 0)
    {
        ESP_LOGI(TAG, "Geocode data found in prefs");
        ESP_LOGI(TAG, "%s", friendlyGeocodeData.friendlyAPIGeodata);
        ESP_LOGI(TAG, "%s", friendlyGeocodeData.friendlyName);
        return friendlyGeocodeData;
    }
    else
    {
        if (wifi_is_connected())
        {
            ESP_LOGI(TAG, "Geocode data not found in prefs, getting from API");
            GeocodeData geocodeData = get_lat_lon_from_zip(zipCode);
            ESP_LOGI(TAG, "Geocode data place name: %s", geocodeData.placeName ? geocodeData.placeName : "(null)");
            ESP_LOGI(TAG, "Geocode data state abbreviation: %s", geocodeData.stateAbr ? geocodeData.stateAbr : "(null)");
            if ((geocodeData.latitude > 0 || geocodeData.latitude < 0) && (geocodeData.longitude > 0 || geocodeData.longitude < 0))
            {
                save_geocode_data(zipCode, geocodeData.latitude, geocodeData.longitude, geocodeData.placeName, geocodeData.stateAbr);
                return get_saved_geocode_data(zipCode, "");
            }
        }
    }
    ESP_LOGI(TAG, "Can't get geocode data, using default");
    // Return default values with dynamically allocated strings (so they can be safely freed)
    FriendlyGeocodeData defaultData;
    defaultData.friendlyName = strdup("Mount Pleasant, SC");
    defaultData.friendlyAPIGeodata = strdup("latitude=32.82204253623829&longitude=-79.81838788885577");
    return defaultData;
}

// 0 = off, 1 = on
int AppSettings::get_gyro_mode() const
{
    return gyroMode;
}
// 0 = wifi, 1 = console
int AppSettings::get_network_mode() const
{
    return networkMode;
}
// 0 - 255
uint8_t AppSettings::get_brightness() const
{
    return brightness;
}
int AppSettings::get_haptics_enabled() const
{
    return hapticsEnabled;
}
int AppSettings::get_power_mode() const
{
    return powerMode;
}
int AppSettings::get_network_time_mode() const
{
    return networkTimeMode;
}
long AppSettings::get_gmt_offset_sec() const
{
    return gmtOffset_sec;
}
int AppSettings::get_daylight_offset_sec() const
{
    return daylightOffset_sec;
}
int AppSettings::get_zip_code() const
{
    return zipCode;
}
// kina like init
void AppSettings::load_prefs_into_app()
{
    ESP_LOGI(TAG, "Loading prefs into app");
    uint8_t brightness = get_brightness();
    int gyroMode = get_gyro_mode();
    int networkMode = get_network_mode();
    int hapticsEnabled = get_haptics_enabled();
    int zipCode = get_zip_code();
    int periodicConnectMode = get_periodic_connect_mode();
    int hotspotMode = get_hotspot_mode();
    const char *hotspotSSID = get_hotspot_ssid();
    const char *hotspotPassword = get_hotspot_password();

    this->set_brightness(brightness, true);
    this->set_gyro_mode(gyroMode, true);
    this->set_network_mode(networkMode, true, false);
    this->set_haptics_enabled(hapticsEnabled, true);
    this->set_zip_geocode_data(zipCode, true, false);
    this->set_periodic_connect_mode(periodicConnectMode, true);
    this->set_hotspot_mode(hotspotMode, true);
    this->set_hotspot_ssid(hotspotSSID, true);
    this->set_hotspot_password(hotspotPassword, true);
}

static int prevAutoLockGyroMode = 0;
static int prevAutoLockHapticsEnabled = 0;
static uint8_t prevAutoLockBrightness = 15;
static int prevAutoLockCpuFreq = 0;
static wifi_ps_type_t prevAutoLockWifiPs = WIFI_PS_NONE;
static bool prevAutoLockHotspotWasOn = false;

void AppSettings::enable_power_save(bool fromAutoLock)
{
    if (!fromAutoLock)
    {
        // Direct power save mode - aggressive power saving
        if (powerMode == 1)
            return;

        ESP_LOGI(TAG, "Enabling POWER SAVE mode (direct)");

        // Snapshot current state (RAM only)
        prevNetworkMode = networkMode;
        prevGyroMode = gyroMode;
        prevBrightness = brightness == 0 ? 15 : brightness;
        prevHapticsEnabled = hapticsEnabled;
        prevCpuFreq = get_cpu_freq_mhz();
        hasPowerSaveSnapshot = true;

        // Check if hotspot is enabled - if so, keep it but optimize
        bool hotspotActive = (hotspotMode == 1);

        if (hotspotActive)
        {
            ESP_LOGI(TAG, "Hotspot active - keeping enabled but optimizing power");
            // Keep hotspot but use most aggressive WiFi power save
            esp_wifi_set_ps(WIFI_PS_MAX_MODEM); // Most aggressive power save
        }
        else
        {
            // No hotspot - can disable WiFi completely
            set_network_mode(2, false); // WiFi OFF
        }

        // Always disable these power-hungry features
        set_gyro_mode(0, false);       // Gyro OFF
        set_haptics_enabled(0, false); // Haptics OFF
        set_brightness(10, false);    // Very dim display (was 15)

        // Aggressive CPU frequency reduction
        set_cpu_freq_mhz(80); // Minimum stable frequency

        // WiFi power save (if WiFi is still on for hotspot)
        if (!hotspotActive)
        {
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        }

        powerMode = 1;
    }
    else
    {
        // Auto-lock power save - preserve user settings but optimize everything else
        isAutoLocked = true;
        ESP_LOGI(TAG, "Enabling auto-lock POWER SAVE mode");

        // Snapshot auto-lock state
        prevAutoLockGyroMode = gyroMode;
        prevAutoLockHapticsEnabled = hapticsEnabled;
        prevAutoLockBrightness = brightness == 0 ? 15 : brightness;
        prevAutoLockCpuFreq = get_cpu_freq_mhz();
        prevAutoLockHotspotWasOn = (hotspotMode == 1);

        // Get current WiFi power save mode
        wifi_ps_type_t current_ps;
        esp_wifi_get_ps(&current_ps);
        prevAutoLockWifiPs = current_ps;

        // Turn off display completely
        set_brightness(0, false);

        // Only apply additional optimizations if not already in direct power save
        if (powerMode == 0)
        {
            // Disable power-hungry features
            set_gyro_mode(0, false);       // Gyro OFF
            set_haptics_enabled(0, false); // Haptics OFF

            // Aggressive CPU frequency reduction
            set_cpu_freq_mhz(80); // Minimum stable frequency

            // Optimize WiFi power save based on what's active
            if (networkMode == 0 || networkMode == 1)
            {
                // WiFi is on (STA or console mode)
                if (prevAutoLockHotspotWasOn)
                {
                    // Hotspot is active - use most aggressive power save
                    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
                }
                else
                {
                    // Regular WiFi - use moderate power save
                    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                }
            }
            // If WiFi is off (networkMode == 2), no need to set power save
        }
        else
        {
            // Already in direct power save - just ensure WiFi is optimized
            if (prevAutoLockHotspotWasOn)
            {
                esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
            }
        }
    }
}

void AppSettings::disable_power_save(bool fromAutoLock)
{
    if (!fromAutoLock)
    {
        // Disable direct power save mode
        if (powerMode == 0)
            return;

        ESP_LOGI(TAG, "Disabling POWER SAVE mode (direct)");

        if (hasPowerSaveSnapshot)
        {
            set_network_mode(prevNetworkMode, false);
            set_gyro_mode(prevGyroMode, false);
            set_brightness(prevBrightness, false);
            set_haptics_enabled(prevHapticsEnabled, false);
            set_cpu_freq_mhz(prevCpuFreq);
        }

        esp_wifi_set_ps(WIFI_PS_NONE);

        hasPowerSaveSnapshot = false;
        powerMode = 0;
    }
    else
    {
        // Disable auto-lock power save mode
        isAutoLocked = false;
        ESP_LOGI(TAG, "Disabling auto-lock POWER SAVE mode");

        set_brightness(prevAutoLockBrightness, false);

        // Only restore other settings if not in direct power save mode
        if (powerMode == 0)
        {
            set_gyro_mode(prevAutoLockGyroMode, false);
            set_haptics_enabled(prevAutoLockHapticsEnabled, false);
            set_cpu_freq_mhz(prevAutoLockCpuFreq);

            esp_wifi_set_ps(prevAutoLockWifiPs);
        }
        else
        {
            // Still in direct power save, just restore WiFi power save to what it was
            if (prevAutoLockHotspotWasOn)
            {
                esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
            }
            else
            {
                esp_wifi_set_ps(prevAutoLockWifiPs);
            }
        }
    }
}