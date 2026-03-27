/***************************************************************************************
 *  File        : power_mgmt.cpp
 *  Description : Power management interface for the AXP2101 PMIC
 *  Author      : Noah Clark
 *  Created     : 2025-03-27
 *--------------------------------------------------------------------------------------
 *  Part of the ESP-32 Smartwatch Firmware
 *--------------------------------------------------------------------------------------
 *  Notes:
 *   - This interface is designed to communicate with the AXP2101 PMIC to enable accurate and reliable
 *     battery voltage and charging status monitoring.
 *   - If the AXP2101 PMIC is not working or not available, the interface will fall back to using
 *     the ESP32 ADC.
 *     - Note: This relies on "#define XPOWERS_CHIP_AXP2101 1" to determine which method to use.
 ***************************************************************************************/

#include "configuration/pin_config.h"
#include "configuration/app_config.h"
#include "core/power/power_mgmt.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "soc/usb_serial_jtag_reg.h"
#include "soc/usb_serial_jtag_struct.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AXP2101_ADDR 0x34
#define AXP2101_STATUS1_REG 0x00
#define AXP2101_STATUS2_REG 0x01
#define AXP2101_COMM_STAT0_REG 0x02
#define AXP2101_COMM_STAT1_REG 0x03
#define AXP2101_OFF_CTL_REG 0x10
#define AXP2101_PWRON_STATUS 0x20
#define AXP2101_VBAT_H_REG 0x34
#define AXP2101_VBAT_L_REG 0x35
#define AXP2101_BAT_PERCENT_REG 0xA4

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;

float voltageGlobal = 0.0f;
float last_vGlobal = 0.0f;
float max_v_seen = 0.0f;

static pwr_off_callback_t pwr_off_callback = NULL;
static pwr_low_callback_t pwr_low_callback = NULL;

void set_pwr_off_callback(pwr_off_callback_t callback)
{
    pwr_off_callback = callback;
}

void set_pwr_low_callback(pwr_low_callback_t callback)
{
    pwr_low_callback = callback;
}

static esp_err_t axp2101_write_byte(uint8_t reg_addr, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AXP2101_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t axp2101_read_byte(uint8_t reg_addr, uint8_t *data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AXP2101_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AXP2101_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

void power_off_watch_legacy(void)
{
    ESP_LOGI("POWER", "Power off requested");
    
    if(pwr_off_callback != NULL)
    {
        pwr_off_callback();
    }
    
    gpio_set_level(LCD_BL, 0);
    vTaskDelay(pdMS_TO_TICKS(30));
    
    // Wait for button release (max 2s)
    int wait_count = 0;
    while (gpio_get_level(PWR_BTN) == 0 && wait_count < 400) {
        vTaskDelay(pdMS_TO_TICKS(5));
        wait_count++;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI("POWER", "Powering off...");
    portDISABLE_INTERRUPTS();
    
    for (int i = 0; i < 5; i++) {
        gpio_set_level(SYS_EN, 0);
        esp_rom_delay_us(1000);
    }

    while (true) {
        gpio_set_level(SYS_EN, 0);
        esp_rom_delay_us(10000);
    }
}

void power_off_watch_axp2101(void)
{
    ESP_LOGI("POWER", "Power off requested");

    if (pwr_off_callback != NULL)
    {
        pwr_off_callback();
    }

    gpio_set_level(LCD_BL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI("POWER", "Shutting down via AXP2101...");

    // 0x01 to OFF_CTL initiates clean power-off
    esp_err_t ret = axp2101_write_byte(AXP2101_OFF_CTL_REG, 0x01);

    if (ret == ESP_OK)
    {
        ESP_LOGI("POWER", "AXP2101 shutdown command sent successfully");
    }
    else
    {
        ESP_LOGE("POWER", "Shutdown command failed: %d, disabling outputs", ret);
        axp2101_write_byte(0x90, 0x00);
        axp2101_write_byte(0x91, 0x00);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    // Fallback: try GPIO latch release
    ESP_LOGW("POWER", "AXP2101 shutdown didn't complete, trying GPIO fallback");
    portDISABLE_INTERRUPTS();
    for (int i = 0; i < 10; i++)
    {
        gpio_set_level(SYS_EN, 0);
        esp_rom_delay_us(10000);
    }

    ESP_LOGE("POWER", "Failed to power off, halting CPU");
    while (true)
    {
        esp_rom_delay_us(1000000);
    }
}

void power_off_watch(void)
{
    #if XPOWERS_CHIP_AXP2101 == 1
        power_off_watch_axp2101();
    #else
        power_off_watch_legacy();
    #endif
}

static float read_battery_voltage_filtered_axp2101(void)
{
    static bool initialized = false;
    static float v_filtered = 0.0f;
    static uint64_t last_read = 0;
    static bool use_axp2101 = true;

    uint64_t now = esp_timer_get_time();

    // Throttle reads to every 200ms
    if (!initialized || (now - last_read) >= 200000)
    {
        float v = 0.0f;

        if (use_axp2101)
        {
            uint8_t vbat_h = 0, vbat_l = 0;
            esp_err_t ret_h = axp2101_read_byte(AXP2101_VBAT_H_REG, &vbat_h);
            esp_err_t ret_l = axp2101_read_byte(AXP2101_VBAT_L_REG, &vbat_l);

            if (ret_h == ESP_OK && ret_l == ESP_OK)
            {
                // 14-bit ADC: high byte << 6 | low 6 bits
                uint16_t vbat_raw = (vbat_h << 6) | (vbat_l & 0x3F);
                v = vbat_raw * 0.00465f;


                if (v < 3.0f || v > 6.0f)
                {
                    ESP_LOGW("BATTERY", "AXP2101 voltage out of range: %.3fV, falling back to ADC", v);
                    use_axp2101 = false;
                    v = 0.0f;
                }
            }
            else
            {
                ESP_LOGW("BATTERY", "AXP2101 read failed (err: %d, %d), falling back to ADC", ret_h, ret_l);
                use_axp2101 = false;
            }
        }

        // ADC fallback
        if (!use_axp2101 || v == 0.0f)
        {
            int raw_voltage = 0;

            if (adc1_handle != NULL)
            {
                int sum = 0;
                for (int i = 0; i < 10; i++)
                {
                    int sample = 0;
                    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BAT_ADC, &sample));
                    sum += sample;
                    esp_rom_delay_us(100);
                }
                raw_voltage = sum / 10;
            }
            else
            {
                return v_filtered; // Return last known value
            }

            int voltage_mv = 0;
            if (adc1_cali_handle != NULL)
            {
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw_voltage, &voltage_mv));
            }
            else
            {
                return v_filtered;
            }

            const float VOLTAGE_DIVIDER = 2.0f / 1.269f;
            v = (voltage_mv / 1000.0f) * VOLTAGE_DIVIDER;
        }

        if (!initialized)
        {
            v_filtered = v;
            initialized = true;
        }
        else
        {
            const float alpha = 0.3f;
            v_filtered = v_filtered * (1.0f - alpha) + v * alpha;
        }

        last_read = now;
        last_vGlobal = voltageGlobal;
        voltageGlobal = v_filtered;

        if (v_filtered > max_v_seen)
        {
            max_v_seen = v_filtered;
        }
    }

    return voltageGlobal;
}

static float read_battery_voltage_filtered_legacy(void)
{
    const float CAL = 1.088f;

    static bool initialized = false;
    static float v_filtered = 0.0f;
    static uint64_t last_read = 0;

    uint64_t now = esp_timer_get_time();

    if (!initialized || (now - last_read) >= 200000) {
        int raw_voltage = 0;
        
        // Average 10 samples for stability
        if (adc1_handle != NULL) {
            int sum = 0;
            for (int i = 0; i < 10; i++) {
                int sample = 0;
                ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BAT_ADC, &sample));
                sum += sample;
                esp_rom_delay_us(100);
            }
            raw_voltage = sum / 10;
        } else {
            return 0.0f;
        }
        
        int voltage_mv = 0;
        if (adc1_cali_handle != NULL) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw_voltage, &voltage_mv));
        } else {
            return 0.0f;
        }
        
        float v = (voltage_mv / 1000.0f) * 3.0f * CAL;

        if (!initialized) {
            v_filtered = v;
            initialized = true;
        } else {
            const float alpha = 0.3f;
            v_filtered = v_filtered * (1.0f - alpha) + v * alpha;
        }

        last_read = now;
        last_vGlobal = voltageGlobal;
        voltageGlobal = v_filtered;
        
        if (v_filtered > max_v_seen) {
            max_v_seen = v_filtered;
        }
    }

    return voltageGlobal;
}

float get_battery_voltage(void)
{
    #if XPOWERS_CHIP_AXP2101 == 1
        return read_battery_voltage_filtered_axp2101();
    #else
        return read_battery_voltage_filtered_legacy();
    #endif
}

bool usb_serial_connected(void)
{
    // USB connected if SOF frame counter is incrementing
    uint32_t frame1 = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    esp_rom_delay_us(2000);
    uint32_t frame2 = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    bool usb_active = (frame2 != frame1);
    return usb_active;
}

bool is_charging_axp2101(void)
{
    static uint64_t last_check_time = 0;
    static uint64_t last_debug_time = 0;
    static bool last_charging_state = false;
    static bool cached_charging_state = false;

    uint64_t now = esp_timer_get_time();

    if (last_check_time == 0 || (now - last_check_time) >= 500000)
    {
        uint8_t status1 = 0;
        axp2101_read_byte(AXP2101_STATUS1_REG, &status1);

        bool axp_charging = (status1 & 0x20) != 0; // bit 5 = charging
        cached_charging_state = axp_charging;

        if (last_debug_time == 0 || (now - last_debug_time) >= 5000000)
        {
            float voltage = get_battery_voltage();
            uint8_t gauge_pct = 0xFF;
            axp2101_read_byte(AXP2101_BAT_PERCENT_REG, &gauge_pct);

            if (gauge_pct <= 100)
            {
                ESP_LOGI("BATTERY", "%d%% | %.3fV | %s",
                         gauge_pct, voltage, axp_charging ? "Charging" : "On Battery");
            }
            else
            {
                ESP_LOGI("BATTERY", "%.3fV | %s",
                         voltage, axp_charging ? "Charging" : "On Battery");
            }
            last_debug_time = now;
        }

        if (cached_charging_state != last_charging_state)
        {
            ESP_LOGI("BATTERY", "Charging state changed: %s",
                     cached_charging_state ? "CHARGING" : "DISCHARGING");
            last_charging_state = cached_charging_state;
        }

        last_check_time = now;
    }

    return cached_charging_state;
}

bool is_charging_legacy(void)
{
    bool usb_data_connected = usb_serial_connected();

    // Voltage trend analysis for wall chargers without USB data
    static float voltage_history[5] = {0};
    static int history_index = 0;
    static bool history_initialized = false;
    static uint64_t last_check_time = 0;
    static bool voltage_based_charging = false;

    float current_voltage = get_battery_voltage();
    uint64_t now = esp_timer_get_time();

    if (last_check_time == 0 || (now - last_check_time) >= 2000000)
    {
        voltage_history[history_index] = current_voltage;
        history_index = (history_index + 1) % 5;

        if (!history_initialized && history_index == 0 && voltage_history[4] > 0)
        {
            history_initialized = true;
        }

        if (history_initialized)
        {
            float avg_voltage = 0.0f;
            for (int i = 0; i < 5; i++)
            {
                avg_voltage += voltage_history[i];
            }
            avg_voltage /= 5.0f;

            float oldest_voltage = voltage_history[history_index];
            float newest_voltage = current_voltage;
            float voltage_trend = newest_voltage - oldest_voltage;

            if (voltage_trend > 0.02f)
            {
                voltage_based_charging = true;
            }
            else if (avg_voltage > BATTERY_HIGH_VOLTAGE && voltage_trend > -0.01f)
            {
                voltage_based_charging = true;
            }
            else if (voltage_trend < -0.01f || avg_voltage < BATTERY_LOW_VOLTAGE)
            {
                voltage_based_charging = false;
            }
        }
        else
        {
            voltage_based_charging = false;
        }

        last_check_time = now;
    }

    bool charging = usb_data_connected || voltage_based_charging;

    static bool last_charging_state = false;
    if (charging != last_charging_state)
    {
        ESP_LOGI("BATTERY", "Charging status changed: %s (USB: %s, Voltage: %.2fV, V-based: %s)",
                 charging ? "CHARGING" : "NOT CHARGING",
                 usb_data_connected ? "YES" : "NO",
                 current_voltage,
                 voltage_based_charging ? "YES" : "NO");
        last_charging_state = charging;
    }

    return charging;
}

bool is_charging(void)
{
#if XPOWERS_CHIP_AXP2101 == 1
    return is_charging_axp2101();
#else
    return is_charging_legacy();
#endif
}

static void init_axp2101_gauge(void)
{
    uint8_t gauge_val = 0xFF;
    if (axp2101_read_byte(AXP2101_BAT_PERCENT_REG, &gauge_val) == ESP_OK)
    {
        if (gauge_val <= 100)
        {
            ESP_LOGI("BATTERY", "AXP2101 fuel gauge already active: %d%%", gauge_val);
            return;
        }
    }

    ESP_LOGI("BATTERY", "Attempting to enable AXP2101 fuel gauge...");

    uint8_t reg_val = 0;
    if (axp2101_read_byte(0x18, &reg_val) == ESP_OK)
    {
        ESP_LOGI("BATTERY", "  Current 0x18 = 0x%02X", reg_val);
        axp2101_write_byte(0x18, reg_val | 0x08); // Set bit 3
        vTaskDelay(pdMS_TO_TICKS(100));           // Wait for gauge to initialize
        if (axp2101_read_byte(AXP2101_BAT_PERCENT_REG, &gauge_val) == ESP_OK && gauge_val <= 100)
        {
            ESP_LOGI("BATTERY", "AXP2101 fuel gauge enabled: %d%%", gauge_val);
            return;
        }
    }

    ESP_LOGW("BATTERY", "AXP2101 fuel gauge not available or unsupported, using voltage-based calculation");
}

int get_battery_percentage_axp2101(void)
{
    static bool cached_time_low_power = false;

    uint8_t axp_percent = 0xFF;
    if (axp2101_read_byte(AXP2101_BAT_PERCENT_REG, &axp_percent) == ESP_OK && axp_percent <= 100)
    {
        int result = (int)axp_percent;


        if (result < 15 && !cached_time_low_power)
        {
            cached_time_low_power = true;
            if (pwr_low_callback != NULL)
            {
                pwr_low_callback();
            }
        }
        else if (result >= 15 && cached_time_low_power)
        {
            cached_time_low_power = false;
        }

        return result;
    }

    ESP_LOGW("BATTERY", "Failed to read AXP2101 fuel gauge");
    return 50;
}

int get_battery_percentage_legacy(void)
{
    static bool cached_time_low_power = false;

    float voltage = get_battery_voltage();

    // LiPo discharge curve (non-linear approximation)
    float percent;

    if (voltage >= 4.15f)
        percent = 90.0f + ((voltage - 4.15f) / 0.05f) * 10.0f;
    else if (voltage >= 4.00f)
        percent = 70.0f + ((voltage - 4.00f) / 0.15f) * 20.0f;
    else if (voltage >= 3.85f)
        percent = 50.0f + ((voltage - 3.85f) / 0.15f) * 20.0f;
    else if (voltage >= 3.75f)
        percent = 30.0f + ((voltage - 3.75f) / 0.10f) * 20.0f;
    else if (voltage >= 3.60f)
        percent = 10.0f + ((voltage - 3.60f) / 0.15f) * 20.0f;
    else if (voltage >= 3.40f)
        percent = ((voltage - 3.40f) / 0.20f) * 10.0f;
    else
    {
        percent = 0.0f;
    }

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    static bool initialized = false;
    static float smooth_percent = 0.0f;

    if (!initialized)
    {
        smooth_percent = percent;
        initialized = true;
    }
    else
    {
        const float alpha = 0.1f;
        smooth_percent = smooth_percent * (1.0f - alpha) + percent * alpha;
    }

    int result = (int)(smooth_percent + 0.5f);


    if (result < 15 && !cached_time_low_power)
    {
        cached_time_low_power = true;

        if (pwr_low_callback != NULL)
        {
            pwr_low_callback();
        }
    }
    else if (result >= 15 && cached_time_low_power)
    {
        cached_time_low_power = false;
    }

    return result;
}

void init_battery_adc(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_1);
    io_conf.mode = GPIO_MODE_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BAT_ADC, &config));

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle));

    ESP_LOGI("BATTERY", "Battery ADC initialized");
#if XPOWERS_CHIP_AXP2101 == 1
    init_axp2101_gauge();
#endif
}

int get_battery_percentage(void)
{
#if XPOWERS_CHIP_AXP2101 == 1
    return get_battery_percentage_axp2101();
#else
    return get_battery_percentage_legacy();
#endif
}