// Power latch and boot sequence management
//
// The board uses a power latch: PWR_BTN is active-low input, SYS_EN must
// stay HIGH to keep power on. early_power_latch() runs as a constructor(101)
// to latch before the user releases the button (~200-300ms hold required).
// power_button_task then monitors for single/double/long press events.

#include "configuration/pin_config.h"
#include "core/boot/boot.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LONG_PRESS_TIME 1500
#define DOUBLE_PRESS_TIME 300

static const char *TAG = "boot";

static unsigned long btn_down_time = 0;
static unsigned long last_btn_up_time = 0;
static bool btn_was_down = false;
static bool waiting_for_second_press = false;
static uint32_t boot_ignore_until = 0;

static void early_power_latch(void) __attribute__((constructor(101)));
static void early_power_latch(void)
{
    gpio_reset_pin(BUZZER_PIN);
    gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(BUZZER_PIN, 0);

    gpio_reset_pin(SYS_EN);
    gpio_set_direction(SYS_EN, GPIO_MODE_OUTPUT);
    
    // Rapid latch attempts to catch before button release
    for (int i = 0; i < 10; i++) {
        gpio_set_level(SYS_EN, 1);
        esp_rom_delay_us(100);
    }
    
    gpio_set_level(SYS_EN, 1);
    esp_rom_delay_us(1000);
    gpio_set_level(SYS_EN, 1);
    
    gpio_reset_pin(PWR_BTN);
    gpio_set_direction(PWR_BTN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PWR_BTN, GPIO_PULLUP_ONLY);
}

void latch_power(void)
{
    gpio_set_level(BUZZER_PIN, 0);
    gpio_set_level(SYS_EN, 1);
    esp_rom_delay_us(100);
    gpio_set_level(SYS_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(SYS_EN, 1);
}

void power_button_task(void *arg)
{
    gpio_set_direction(PWR_BTN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PWR_BTN, GPIO_PULLUP_ONLY);

    boot_ignore_until = esp_timer_get_time() / 1000 + 250;
    
    // Reinforce latch during boot
    for (int i = 0; i < 5; i++) {
        gpio_set_level(SYS_EN, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint32_t latch_refresh_counter = 0;
    
    while (1)
    {
        uint32_t now = esp_timer_get_time() / 1000;

        // Refresh latch ~every 1s
        latch_refresh_counter++;
        if (latch_refresh_counter >= 100) {
            gpio_set_level(SYS_EN, 1);
            latch_refresh_counter = 0;
        }

        if (now < boot_ignore_until)
        {
            gpio_set_level(SYS_EN, 1);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        bool pressed = (gpio_get_level(PWR_BTN) == 0);

        if (pressed && !btn_was_down)
        {
            btn_was_down = true;
            btn_down_time = now;
        }

        if (!pressed && btn_was_down)
        {
            btn_was_down = false;
            uint32_t press_duration = now - btn_down_time;

            if (press_duration >= LONG_PRESS_TIME)
            {
                ESP_LOGI(TAG, "LONG PRESS - Powering off");
                gpio_set_level(SYS_EN, 0);
                waiting_for_second_press = false;
                continue;
            }

            if (!waiting_for_second_press)
            {
                waiting_for_second_press = true;
                last_btn_up_time = now;
            }
            else
            {
                waiting_for_second_press = false;
                ESP_LOGI(TAG, "DOUBLE PRESS");
            }
        }

        if (waiting_for_second_press && (now - last_btn_up_time > DOUBLE_PRESS_TIME))
        {
            waiting_for_second_press = false;
            ESP_LOGI(TAG, "SINGLE PRESS");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
