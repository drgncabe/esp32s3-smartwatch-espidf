#include <string.h>

#include "api/prefs.h"
#include "configuration/pin_config.h"
#include "context/app_context.h"
#include "context/display_controller.h"
#include "core/network/wifi_init.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "soc/rtc.h"
#include "util/util.h"

static const char *TAG = "util";

static uint64_t last_tap = 0;
static bool was_brightness_changed = false;

// 60 seconds since last tap, decrease brightness to 20
static uint64_t brightness_lower_timeout = 60000000; // microseconds
static uint8_t brightness_threshold = 20;

// 30 seconds since last decrease, turn off display
static uint64_t brightness_off_timeout = 30000000; // microseconds
static uint8_t brightness_off_threshold = 0;
static uint8_t last_brightness = 255;

// 1 second delay between checks
const uint32_t delayed_loop_interval_ms = 1000;
static uint32_t lastDelayedLoopRun = 0;

// Helper functions for CPU frequency management
 uint32_t get_cpu_freq_mhz()
{
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_get_config(&conf);
    return conf.freq_mhz;
}

 void set_cpu_freq_mhz(uint32_t freq_mhz)
{
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_mhz_to_config(freq_mhz, &conf);
    rtc_clk_cpu_freq_set_config(&conf);
}



void set_brightness(uint8_t level)
{ // 0-255
    g_display.set_brightness(level);
}

void check_timed_tap_events(bool force)
{
    uint32_t nowMillis = esp_timer_get_time() / 1000; // Convert microseconds to milliseconds
    if (!(nowMillis - lastDelayedLoopRun >= delayed_loop_interval_ms) && !(force && (g_settings.isAutoLocked || was_brightness_changed)))
    {
        return;
    }
    lastDelayedLoopRun = nowMillis;

    if (force)
    {
        ESP_LOGI(TAG, "Force checking timed tap events");
    }

    uint64_t now = esp_timer_get_time(); // microseconds
    uint64_t elapsed = now - last_tap;
    uint8_t current_brightness = g_display.get_brightness();

    if (elapsed > brightness_lower_timeout)
    {
        if (current_brightness > brightness_threshold)
        {
            last_brightness = current_brightness;
            ESP_LOGI(TAG, "60 seconds since last tap, decreasing brightness");
            set_brightness(brightness_threshold);
            was_brightness_changed = true;
        }
        else
        {
            if (elapsed > (brightness_lower_timeout + brightness_off_timeout) &&
                current_brightness > brightness_off_threshold)
            {
                ESP_LOGI(TAG, "30 seconds since last decrease, turning off display");
                g_settings.enable_power_save(true);
                was_brightness_changed = true;
            }
        }
    }
    else
    {
        if (current_brightness <= brightness_threshold && was_brightness_changed)
        {
            if (g_settings.isAutoLocked)
            {
                g_settings.disable_power_save(true);
            }
            else
            {
                last_brightness = last_brightness > 15 ? last_brightness : 15;
                set_brightness(last_brightness);
            }
            was_brightness_changed = false;
        }
    }
}

void register_last_tap(void)
{
    last_tap = esp_timer_get_time(); // microseconds
}

// -- I2C Scanner --
void i2c_scanner(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus...");
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Found device at address 0x%02X", addr);
        }
    }
    ESP_LOGI(TAG, "I2C scan complete");
}