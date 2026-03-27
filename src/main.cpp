#include "configuration/app_config.h"
#include "configuration/pin_config.h"
#include "context/app_context.h"
#include "context/display_controller.h"
#include "core/boot/boot.h"
#include "core/display/cst_816_drv.h"
#include "core/display/lcd_169_drv.h"
#include "core/network/time_init.h"
#include "core/network/wifi_init.h"
#include "core/power/power_mgmt.h"
#include "core/store/nvs_fs.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network/modules/open_scanner.h"
#include "network/modules/wifi_scanner.h"
#include "ui/screens/home_page.h"
#include "util/util.h"



static const char *TAG = "main";

// Global display controller
DisplayController g_display(LCD_BL, 0);

static void on_power_off(void)
{
    maybe_cache_time(true);
    lv_timer_pause(lv_timer_get_next(NULL));
    power_off_wifi();
}

static void on_power_low(void)
{
    maybe_cache_time(true);
}

static void on_screen_touch(void)
{
    register_last_tap();
    check_timed_tap_events(true);
}

static void periodic_task_handler(void *parameter)
{
    while (1)
    {
        check_periodic_open_scanner();
        maybe_cache_time(false);
        check_timed_tap_events();
        vTaskDelay(pdMS_TO_TICKS(PERIODIC_TASK_INTERVAL_MS)); 
    }
}

// ========== Core UI ==========
static void init_app_core(void)
{
    initialize_layout();
    run_wifi_startup_scan();
    if(!sync_cached_time()){
        set_manual_time(2026, 1, 1, 0, 0, 0);
        ESP_LOGI(TAG, "Failed to sync cached time, setting manual time to 2026-01-01 00:00:00");
    }
    log_lvgl_memory();
}

// ========== Main ==========
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting Waveshare ESP32-S3 1.69\" Touch LCD");

    latch_power();

    // Initialize NVS (needed for WiFi and preferences)
    init_nvs_fs();

    // Initialize peripherals
    ESP_ERROR_CHECK(i2c_master_init());
    i2c_scanner();
    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(cst816_init());

    // Initialize global display controller and load settings
    g_display.begin();
    g_settings = AppSettings::from_prefs();
    g_settings.load_prefs_into_app();

    // Initialize LVGL
    lvgl_init();

    // Small delay to let LVGL tasks start
    vTaskDelay(pdMS_TO_TICKS(100));

    // Initialize battery ADC
    init_battery_adc();

    // Set power callbacks / graceful shutdown
    set_pwr_off_callback(on_power_off);
    set_pwr_low_callback(on_power_low);

    // Set screen touch callback
    set_cst816_touch_callback(on_screen_touch);

    // Small delay to let LVGL tasks start
    vTaskDelay(pdMS_TO_TICKS(100));

    // Create core application UI
    init_app_safe(init_app_core);

    // Start power button handler
    init_power_button_handler();

    // Start periodic task handler
    xTaskCreatePinnedToCore(
        periodic_task_handler,
        "periodic_task_handler",
        PERIODIC_TASK_STACK_SIZE,
        NULL,
        1,
        NULL,
        1);

    ESP_LOGI(TAG, "Initialization complete");
}