#include "context/display_controller.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "display_ctrl";

DisplayController::DisplayController(int blPin, int blChannel)
    : m_blPin(blPin), m_blChannel(blChannel) {}

void DisplayController::begin()
{
    // Set up LEDC (PWM) for backlight
    const int freq = 5000;   // 5 kHz
    const int res  = LEDC_TIMER_8_BIT;  // 0-255

    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = (ledc_timer_bit_t)res,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = freq,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = m_blPin,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = (ledc_channel_t)m_blChannel,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = m_brightness,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_LOGI(TAG, "Display backlight initialized (pin: %d, channel: %d)", m_blPin, m_blChannel);
}

void DisplayController::set_brightness(uint8_t lvl)
{
    // if (lvl > 255) lvl = 255;
    m_brightness = lvl;
    
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)m_blChannel, m_brightness));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)m_blChannel));
}

uint8_t DisplayController::get_brightness() const
{
    return m_brightness;
}