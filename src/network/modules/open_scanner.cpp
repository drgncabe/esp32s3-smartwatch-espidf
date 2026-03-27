#include <algorithm>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <vector>

#include "context/app_context.h"
#include "core/network/time_init.h"
#include "core/network/wifi_init.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "network/modules/open_scanner.h"
#include "network/modules/wifi_scanner.h"

static const char *TAG = "open_scanner";

static bool open_scanner_running = false;
static uint32_t lastPeriodicOpenScannerCheck = 0;
const uint32_t periodicOpenScannerCheckInterval = 1000 * 60 * 1; // 1 minute
const char *open_scanner_status_text = "Initializing...";

bool is_open_scanner_running(void)
{
    return open_scanner_running;
}

void start_open_scanner(void)
{
    static char buf[32];

    ESP_LOGI(TAG, "Starting WiFi scan for open networks...");
    start_wifi_scan();

    // Wait up to 9 seconds for scan to complete
    int attempts = 0;

    while (!wifi_scan_complete() && attempts < 30)
    {
        vTaskDelay(pdMS_TO_TICKS(300));
        attempts++;
    }

    int n = get_wifi_scan_count();

    // Handle results
    if (n < 0)
    {
        open_scanner_status_text = "WiFi scan failed";
        ESP_LOGE(TAG, "WiFi scan failed");
        return;
    }

    if (n == 0)
    {
        open_scanner_status_text = "No networks found";
        ESP_LOGI(TAG, "Scan complete - no networks found");
        return;
    }

    // Found networks - check for open ones
    snprintf(buf, sizeof(buf), "Found %d networks total", n);
    open_scanner_status_text = buf;

    ESP_LOGI(TAG, "Found %d networks total", n);
    int openCount = 0;

    for (int i = 0; i < n; i++)
    {
        if (wifi_is_open(i))
        {
            ESP_LOGI(TAG, "  [OPEN] %s (RSSI: %d dBm)",
                     get_wifi_ssid(i),
                     get_wifi_rssi(i));
            openCount++;
        }
    }

    if (openCount == 0)
    {
        ESP_LOGI(TAG, "No open networks found.");
    }
    else
    {
        ESP_LOGI(TAG, "Found %d open network(s)", openCount);
    }

    cleanup_wifi_scan(); // Clean up
}

void try_open_network_sync(bool isTimeSync)
{
    static char buf[64];

    ESP_LOGI(TAG, "Scanning for open networks...");
    open_scanner_status_text = "Scanning for open networks...";

    // Disable WiFi and wait for it to fully stop
    disable_wifi();
    vTaskDelay(pdMS_TO_TICKS(200));

    // Make sure WiFi is fully stopped and disconnected
    esp_wifi_disconnect();
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Set WiFi mode to STA and wait
    esp_wifi_set_mode(WIFI_MODE_STA);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Now start the scan
    start_wifi_scan();

    // Wait for scan to complete
    while (!wifi_scan_complete())
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    int n = get_wifi_scan_count();

    if (n == 0)
    {
        ESP_LOGI(TAG, "No networks found");
        open_scanner_status_text = "No networks found";
        return;
    }

    // Build list of open networks with their info
    struct OpenNetwork
    {
        char ssid[33];
        int8_t rssi;
        int index;
    };

    std::vector<OpenNetwork> openNetworks;

    for (int i = 0; i < n; i++)
    {
        if (wifi_is_open(i))
        {
            OpenNetwork net;
            strncpy(net.ssid, get_wifi_ssid(i), sizeof(net.ssid));
            net.ssid[sizeof(net.ssid) - 1] = '\0';
            net.rssi = get_wifi_rssi(i);
            net.index = i;
            openNetworks.push_back(net);
        }
    }

    if (openNetworks.empty())
    {
        ESP_LOGI(TAG, "No open networks found");
        open_scanner_status_text = "No open networks found";
        cleanup_wifi_scan();
        return;
    }

    ESP_LOGI(TAG, "Found %d open network(s)", openNetworks.size());

    snprintf(buf, sizeof(buf), "Found %d open network(s)", (int)openNetworks.size());
    open_scanner_status_text = buf;

    open_scanner_status_text = "Sorting networks by signal strength...";

    // Sort by signal strength (strongest first)
    std::sort(openNetworks.begin(), openNetworks.end(),
              [](const OpenNetwork &a, const OpenNetwork &b)
              {
                  return a.rssi > b.rssi; // Higher RSSI = stronger signal
              });

    // Try each network in order
    bool timeSync = false;
    open_scanner_status_text = "Iterating through networks...";

    for (size_t i = 0; i < openNetworks.size() && !timeSync; i++)
    {
        OpenNetwork &net = openNetworks[i];

        ESP_LOGI(TAG, "[%d/%d] Trying: %s (RSSI: %d dBm)",
                 i + 1, openNetworks.size(), net.ssid, net.rssi);
        snprintf(buf, sizeof(buf), "Trying: %s (RSSI: %d dBm)", net.ssid, net.rssi);
        open_scanner_status_text = buf;

        // Connect to this open network using ESP-IDF
        wifi_config_t wifi_config = {};
        strncpy((char *)wifi_config.sta.ssid, net.ssid, sizeof(wifi_config.sta.ssid));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));

        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_connect();

        // Wait for connection (10 seconds max)
        int timeout = 0;
        bool connected = false;

        while (timeout < 20)
        {
            vTaskDelay(pdMS_TO_TICKS(500));

            // Check if we have an IP address (indicates connection)
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif != NULL)
            {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
                {
                    // Check if we have a valid IP (not 0.0.0.0)
                    if (ip_info.ip.addr != 0)
                    {
                        connected = true;
                        break;
                    }
                }
            }

            ESP_LOGI(TAG, ".");
            timeout++;
        }

        if (!connected)
        {
            ESP_LOGI(TAG, "  Connection failed");
            open_scanner_status_text = "Connection failed. Trying next network...";
            continue; // Try next network
        }

        ESP_LOGI(TAG, "  Connected!");

        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
        {
            ESP_LOGI(TAG, "  IP: " IPSTR, IP2STR(&ip_info.ip));
        }

        snprintf(buf, sizeof(buf), "Connected to: %s", net.ssid);
        open_scanner_status_text = buf;

        long gmtOffset_sec = g_settings.get_gmt_offset_sec();
        int daylightOffset_sec = g_settings.get_daylight_offset_sec();

        ESP_LOGI(TAG, "Initializing network time with GMT offset: %ld, Daylight offset: %d",
                 gmtOffset_sec, daylightOffset_sec);
        open_scanner_status_text = "Initializing time sync...";

        // Use the same time initialization as wifi_init.cpp
        // This function will wait up to 30 seconds for sync to complete
        // Returns true if sync completed successfully
        bool should_sync_time = g_settings.get_network_time_mode() == 1 || isTimeSync;
        if (should_sync_time)
        {
            bool sync_success = initialize_network_time(gmtOffset_sec, daylightOffset_sec, true);

            if (!sync_success)
            {
                ESP_LOGW(TAG, "Time sync did not complete");
                open_scanner_status_text = "Time sync timeout. Trying next network...";
                continue; // Try next network
            }

            ESP_LOGI(TAG, "Time sync verified, reading current time...");
            open_scanner_status_text = "Time sync complete!";
        }
        // Wait a bit more to ensure time is fully propagated
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Read time multiple times to ensure we get the synced time
        time_t unix_time = 0;
        struct tm timeinfo;
        int read_attempts = 0;

        while (read_attempts < 5)
        {
            time(&unix_time);
            localtime_r(&unix_time, &timeinfo);

            // Verify time is reasonable (after 2021 and not too far in future)
            if (unix_time > 1609459200 && unix_time < 2000000000)
            {
                // Check if time seems current (not stuck at old value)
                // If we read the same time twice, it's likely synced
                time_t check_time;
                vTaskDelay(pdMS_TO_TICKS(200));
                time(&check_time);
                if (check_time >= unix_time && check_time - unix_time < 5)
                {
                    // Time is advancing, likely synced
                    break;
                }
            }
            read_attempts++;
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (unix_time > 1609459200)
        { // After 2021
            ESP_LOGI(TAG, "  Successfully synced time from NTP");
            ESP_LOGI(TAG, "  Time: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900,
                     timeinfo.tm_mon + 1,
                     timeinfo.tm_mday,
                     timeinfo.tm_hour,
                     timeinfo.tm_min,
                     timeinfo.tm_sec);

            // Get the current time one more time to ensure we have the latest
            time_t now = time(NULL);
            struct tm newtimeinfo;
            if (localtime_r(&now, &newtimeinfo) == NULL)
            {
                ESP_LOGW(TAG, "Failed to get local time");
                continue; // Try next network
            }

            char time_str[16];
            strftime(time_str, sizeof(time_str), "%I:%M %p", &newtimeinfo);

            ESP_LOGI(TAG, "Local time after sync: %s (unix: %lld)", time_str, (long long)now);
            snprintf(buf, sizeof(buf), "SUCCESS: Time: %s", time_str);
            open_scanner_status_text = buf;

            save_time_from_ntp(now);
            // Force cache the synced time
            maybe_cache_time(true);
            timeSync = true; // Success! Stop trying other networks
        }
        else
        {
            open_scanner_status_text = "FAILED: Connected but couldn't get time";
            ESP_LOGI(TAG, "  Connected but couldn't get time");
            ESP_LOGI(TAG, "    (Network may require captive portal login)");
        }

        // Disconnect before trying next network
        if (isTimeSync || !timeSync)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_disconnect();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    open_scanner_status_text = "Scan complete. Cleaning up...";
    cleanup_wifi_scan();
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (timeSync)
    {
        if (!isTimeSync)
        {
            ESP_LOGI(TAG, "Open network connected successfully");
            force_wifi_state_update(WIFI_CONNECTED);
            play_haptic_notification();
            vTaskDelay(pdMS_TO_TICKS(200));
            play_haptic_notification();
            vTaskDelay(pdMS_TO_TICKS(200));
            play_haptic_notification();
        }
        else
        {
            ESP_LOGI(TAG, "Time synchronization successful");
            open_scanner_status_text = "Time synchronization successful!";
        }
    }
    else
    {
        open_scanner_status_text = "FAILED: Failed to sync time from any open network";
        ESP_LOGI(TAG, "Failed to sync time from any open network");
        ESP_LOGI(TAG, "  All networks either failed to connect or require login");
    }
}

static void open_time_task(void *parameter)
{
    static bool was_wifi_enabled = false;
    if (open_scanner_running)
    {
        ESP_LOGI(TAG, "Open scanner already running, skipping...");
    }
    else
    {
        if (g_settings.get_network_mode() == 0 && wifi_get_state() == WIFI_CONNECTED)
        {
            was_wifi_enabled = true;
        }
        open_scanner_running = true;
        try_open_network_sync(true);

        lv_async_call([](void *)
                      {
            open_scanner_running = false;
            if (was_wifi_enabled) {
                enable_wifi(NULL, NULL);
            } else {
                esp_wifi_set_mode(WIFI_MODE_STA);
            } }, NULL);
    }
    vTaskDelete(NULL);
}

void start_open_time_task(void)
{
    ESP_LOGI(TAG, "Starting open time task...");
    xTaskCreatePinnedToCore(
        open_time_task,
        "open_time_task",
        8192,
        NULL,
        1,
        NULL,
        1);
}

static void open_net_task(void *parameter)
{
    static bool was_wifi_enabled = false;
    if (open_scanner_running)
    {
        ESP_LOGI(TAG, "Open scanner already running, skipping...");
    }
    else
    {
        if (g_settings.get_network_mode() == 0 && wifi_get_state() == WIFI_CONNECTED)
        {
            was_wifi_enabled = true;
        }
        open_scanner_running = true;
        try_open_network_sync(false);

        lv_async_call([](void *)
                      {
            open_scanner_running = false;
            if (was_wifi_enabled) {
                enable_wifi(NULL, NULL);
            } else {
                esp_wifi_set_mode(WIFI_MODE_STA);
            } }, NULL);
    }
    vTaskDelete(NULL);
}

void start_open_net_task(void)
{
    ESP_LOGI(TAG, "Starting open net task...");
    xTaskCreatePinnedToCore(
        open_net_task,
        "open_net_task",
        8192,
        NULL,
        1,
        NULL,
        1);
}

void check_periodic_open_scanner(void)
{
    if (g_settings.get_network_mode() == 2)
    {
        return;
    }

    uint32_t now = esp_timer_get_time() / 1000;

    if (now - lastPeriodicOpenScannerCheck >= periodicOpenScannerCheckInterval)
    {
        lastPeriodicOpenScannerCheck = now;
        if (g_settings.get_periodic_connect_mode() == 1)
        {
            if (wifi_get_state() == WIFI_CONNECTED)
            {
                ESP_LOGI(TAG, "WiFi is connected, skipping open net task...");
                return;
            }
            start_open_net_task();
        }
    }
}