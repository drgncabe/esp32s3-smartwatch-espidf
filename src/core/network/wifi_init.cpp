#include <string.h>

#include "api/prefs.h"
#include "api/weather_api.h"
#include "context/app_context.h"
#include "core/network/console_init.h"
#include "core/network/time_init.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lvgl.h"
#include "network/modules/wifi_scanner.h"

static const char *TAG = "wifi";

static char last_connected_ssid[32] = {0};
static char last_connected_password[64] = {0};

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const int WIFI_MAX_RETRIES = 5;
static const int WIFI_MAX_RETRIES_PARA1 = 10;
static const int WIFI_MAX_RETRIES_PARA2 = 20;
static const int WIFI_MAX_RETRIES_PARA3 = 28;

typedef enum
{
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
    WIFI_OFF,
    CONSOLE_ON
} WiFiState;

typedef struct
{
    char ssid[32];
    char password[64];
} wifi_connect_params_t;

static WiFiState wifi_state = WIFI_IDLE;
static bool first_connect = true;
static int retry_count = 0;
static bool break_connection = false;

static TaskHandle_t wifi_connect_task_handle = NULL;
static TaskHandle_t wifi_startup_scan_task_handle = NULL;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data);

void force_wifi_state_update(WiFiState newState)
{
    wifi_state = newState;
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi started, connecting...");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (break_connection)
        {
            ESP_LOGI(TAG, "Disconnected by user request");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            return;
        }

        if (retry_count < WIFI_MAX_RETRIES)
        {
            // Step down TX power on repeated failures
            if (retry_count == WIFI_MAX_RETRIES_PARA1)
            {
                ESP_LOGI(TAG, "Setting TX power to 18.5dBm");
                esp_wifi_set_max_tx_power(74); // 18.5dBm
            }
            else if (retry_count == WIFI_MAX_RETRIES_PARA2)
            {
                ESP_LOGI(TAG, "Setting TX power to 15dBm");
                esp_wifi_set_max_tx_power(60); // 15dBm
            }
            else if (retry_count == WIFI_MAX_RETRIES_PARA3)
            {
                ESP_LOGI(TAG, "Setting TX power to 11dBm");
                esp_wifi_set_max_tx_power(44); // 11dBm
            }

            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "Retry %d/%d", retry_count, WIFI_MAX_RETRIES);
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect after %d retries", WIFI_MAX_RETRIES);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

WiFiState wifi_get_state(void)
{
    return wifi_state;
}

bool wifi_is_connected(void)
{
    return wifi_state == WIFI_CONNECTED;
}

static void wifi_connect_task(void *pvParameters)
{
    wifi_connect_params_t *params = (wifi_connect_params_t *)pvParameters;

    ESP_LOGI(TAG, "WiFi connection task started");

    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Stopped existing WiFi connection");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    else if (stop_err != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGE(TAG, "Error stopping WiFi: %s", esp_err_to_name(stop_err));
    }

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    char active_ssid[32] = {0};
    char active_password[64] = {0};
    strncpy(active_ssid, params->ssid, sizeof(active_ssid) - 1);
    active_ssid[sizeof(active_ssid) - 1] = '\0';
    strncpy(active_password, params->password, sizeof(active_password) - 1);
    active_password[sizeof(active_password) - 1] = '\0';

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, params->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, params->password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(78));

    wifi_state = WIFI_CONNECTING;
    retry_count = 0;
    break_connection = false;

    ESP_LOGI(TAG, "WiFi init finished, waiting for connection...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        wifi_state = WIFI_CONNECTED;
        ESP_LOGI(TAG, "Connected to WiFi");
        save_network_prefs(active_ssid, active_password);
        strncpy(last_connected_ssid, active_ssid, sizeof(last_connected_ssid) - 1);
        last_connected_ssid[sizeof(last_connected_ssid) - 1] = '\0';
        strncpy(last_connected_password, active_password, sizeof(last_connected_password) - 1);
        last_connected_password[sizeof(last_connected_password) - 1] = '\0';

        if (first_connect)
        {
            first_connect = false;
            long gmt_offset = g_settings.get_gmt_offset_sec();
            int daylight_offset = g_settings.get_daylight_offset_sec();
            initialize_network_time(gmt_offset, daylight_offset);
            start_weather_task(true);
        }
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        wifi_state = WIFI_FAILED;
        ESP_LOGE(TAG, "Failed to connect to WiFi. Using cached time.");
        sync_cached_time();
        handle_weather_completion_callback(false);
    }
    else
    {
        wifi_state = WIFI_FAILED;
        ESP_LOGE(TAG, "WiFi connection timeout - no response from network");

        esp_wifi_disconnect();
        esp_wifi_stop();
        handle_weather_completion_callback(false);
    }

    free(params);
    wifi_connect_task_handle = NULL;
    vTaskDelete(NULL);
}

void initialize_wifi(const char *ssid, const char *password, bool force = false)
{
    if (wifi_state == WIFI_CONNECTING && !force)
    {
        ESP_LOGI(TAG, "WiFi already connecting");
        return;
    }

    ESP_LOGI(TAG, "Initializing WiFi (non-blocking)");

    static bool netif_initialized = false;
    if (!netif_initialized)
    {
        ESP_LOGI(TAG, "Initializing TCP/IP stack");
        ESP_ERROR_CHECK(esp_netif_init());

        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_ERROR_CHECK(err);
        }

        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif == NULL)
        {
            sta_netif = esp_netif_create_default_wifi_sta();
            assert(sta_netif);
        }

        netif_initialized = true;
    }

    static bool wifi_driver_initialized = false;
    if (!wifi_driver_initialized)
    {
        ESP_LOGI(TAG, "Initializing WiFi driver");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        s_wifi_event_group = xEventGroupCreate();


        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &event_handler,
                                                            NULL,
                                                            NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &event_handler,
                                                            NULL,
                                                            NULL));
        wifi_driver_initialized = true;
    }

    if (wifi_connect_task_handle != NULL)
    {
        ESP_LOGI(TAG, "Cancelling previous connection attempt");
        vTaskDelete(wifi_connect_task_handle);
        wifi_connect_task_handle = NULL;
    }

    wifi_connect_params_t *params = (wifi_connect_params_t *)heap_caps_malloc(sizeof(wifi_connect_params_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!params)
        params = (wifi_connect_params_t *)malloc(sizeof(wifi_connect_params_t));
    if (params == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for WiFi connection parameters");
        wifi_state = WIFI_FAILED;
        return;
    }

    strncpy(params->ssid, ssid, sizeof(params->ssid) - 1);
    params->ssid[sizeof(params->ssid) - 1] = '\0';
    strncpy(params->password, password, sizeof(params->password) - 1);
    params->password[sizeof(params->password) - 1] = '\0';

    BaseType_t result = xTaskCreate(
        wifi_connect_task,
        "wifi_connect",
        4096,
        params,
        5,
        &wifi_connect_task_handle);

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create WiFi connection task");
        free(params);
        wifi_state = WIFI_FAILED;
        return;
    }

    ESP_LOGI(TAG, "WiFi connection task created - running in background");
}

void disable_wifi()
{
    ESP_LOGI(TAG, "Disabling WiFi");

    break_connection = true;
    wifi_state = WIFI_IDLE;

    esp_wifi_disconnect();
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "WiFi disabled");
}

void enable_wifi(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Enabling WiFi");

    if (wifi_state == WIFI_CONNECTING)
    {
        ESP_LOGI(TAG, "WiFi already connecting");
        return;
    }
    if (ssid == NULL || ssid[0] == '\0' || password == NULL || password[0] == '\0')
    {
        if (last_connected_ssid[0] == '\0' || last_connected_password[0] == '\0')
        {
            ESP_LOGI(TAG, "No last connected network found");
            return;
        }
        initialize_wifi(last_connected_ssid, last_connected_password, true);
    }
    else
    {
        initialize_wifi(ssid, password, true);
    }
}

void power_off_wifi(void)
{
    esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_wifi_stop();
    force_wifi_state_update(WIFI_OFF);
    ESP_LOGI(TAG, "WiFi and Bluetooth powered off");
}

void change_wifi_mode(int nMode, bool connect)
{
    if (nMode == 0)
    {
        stop_console();
        esp_wifi_set_mode(WIFI_MODE_STA);
        if (connect && last_connected_ssid[0] != '\0' && last_connected_password[0] != '\0')
        {
            enable_wifi(last_connected_ssid, last_connected_password);
        }
        ESP_LOGI(TAG, "Wifi mode changed to: wifi");
    }
    else if (nMode == 1)
    {
        disable_wifi();
        esp_wifi_set_mode(WIFI_MODE_AP);
        start_console();
        force_wifi_state_update(CONSOLE_ON);
        ESP_LOGI(TAG, "Wifi mode changed to: console");
    }
    else if (nMode == 2)
    {
        esp_wifi_set_mode(WIFI_MODE_NULL);
        esp_wifi_stop();
        force_wifi_state_update(WIFI_OFF);
        ESP_LOGI(TAG, "Wifi mode changed to: off");
    }
}

void wifi_startup_scan_task(void *pvParameters)
{
    network_pref_data_t *connect_to = (network_pref_data_t *)pvParameters;
    start_wifi_scan();
    while (!wifi_scan_complete())
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    int n = get_wifi_scan_count();
    if (n > 0)
    {
        for (int i = 0; i < n; i++)
        {
            const char *ssid = get_wifi_ssid(i);
            int rssi = get_wifi_rssi(i);

            if (!ssid || ssid[0] == '\0')
            {
                continue;
            }

            bool isOpen = wifi_is_open(i);

            network_pref_data_t netdata = get_saved_network_prefs(ssid);
            if ((isOpen == false && netdata.password && netdata.password[0] != '\0' && netdata.ssid && netdata.ssid[0] != '\0') || (isOpen == true && netdata.ssid && netdata.ssid[0] != '\0'))
            {

                const char *ssid_copy = strdup(netdata.ssid);
                const char *password_copy = strdup(netdata.password);

                connect_to->ssid = ssid_copy;
                connect_to->password = password_copy;
                break;
            }
        }
        cleanup_wifi_scan();
    }

    wifi_startup_scan_task_handle = NULL;

    lv_async_call([](void *con_to)
                  {
                      network_pref_data_t *con_to_ptr = (network_pref_data_t *)con_to;
                      if (con_to_ptr->ssid && con_to_ptr->ssid[0] != '\0' && con_to_ptr->password && con_to_ptr->password[0] != '\0')
                      {
                          ESP_LOGI(TAG, "Found saved network: %s", (const char *)con_to_ptr->ssid);
                          ESP_LOGI(TAG, "Connecting to saved network: %s", (const char *)con_to_ptr->ssid);
                          enable_wifi(con_to_ptr->ssid, con_to_ptr->password);
                      }
                  },
                  connect_to);
    vTaskDelete(NULL);
}

void run_wifi_startup_scan()
{
    ESP_LOGI(TAG, "Running WiFi startup scan");
    network_pref_data_t *connect_to = (network_pref_data_t *)malloc(sizeof(network_pref_data_t));
    if (!connect_to)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for WiFi startup scan");
        return;
    }
    connect_to->ssid = NULL;
    connect_to->password = NULL;

    BaseType_t result = xTaskCreate(
        wifi_startup_scan_task,
        "wifi_connect",
        4096,
        connect_to,
        5,
        &wifi_startup_scan_task_handle);

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create WiFi startup scan task");
        wifi_state = WIFI_FAILED;
        return;
    }
}

void initialize_time()
{
    increment_boot_count();
    bool use_network_time = g_settings.get_network_time_mode() == 1;

    if (use_network_time && wifi_state == WIFI_CONNECTED)
    {
        ESP_LOGI(TAG, "Initializing network time");
        long gmt_offset = g_settings.get_gmt_offset_sec();
        int daylight_offset = g_settings.get_daylight_offset_sec();
        initialize_network_time(gmt_offset, daylight_offset);
    }
    else
    {
        ESP_LOGI(TAG, "Syncing cached time");
        sync_cached_time();
    }
}
