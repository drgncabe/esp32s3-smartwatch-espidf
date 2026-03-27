// #include "driver/gpio.h"
// #include "esp_timer.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "pin_config.h"
// #include "api/buzzer.h"

// void buzzer_init(void)
// {
//     gpio_reset_pin(BUZZER_PIN);
//     gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT);
//     gpio_set_level(BUZZER_PIN, 0); 
// }

// void buzz_tone(int freq, int duration_ms)
// {
//     int period_us = 1000000 / freq;
//     int half = period_us / 2;

//     int cycles = (duration_ms * 1000) / period_us;

//     for (int i = 0; i < cycles; i++) {
//         gpio_set_level(BUZZER_PIN, 1);
//         esp_rom_delay_us(half);
//         gpio_set_level(BUZZER_PIN, 0);
//         esp_rom_delay_us(half);
//     }
// }

// void buzz_haptic_soft(int duration_ms)
// {
//     const int freq = 900;        
//     const float duty = 0.12f;    
//     const int burst_ms = 6;      
//     const int gap_ms = 10;       

//     int period_us = 1000000 / freq;
//     int on_us  = (int)(period_us * duty);
//     int off_us = period_us - on_us;

//     int elapsed = 0;

//     while (elapsed < duration_ms) {
//         int cycles = (burst_ms * 1000) / period_us;

//         for (int i = 0; i < cycles; i++) {
//             gpio_set_level(BUZZER_PIN, 1);
//             esp_rom_delay_us(on_us);
//             gpio_set_level(BUZZER_PIN, 0);
//             esp_rom_delay_us(off_us);
//         }

//         vTaskDelay(pdMS_TO_TICKS(gap_ms));
//         elapsed += burst_ms + gap_ms;
//     }
// }


// void buzz_wave(int freq, float duty, int duration_ms)
// {
//     int period_us = 1000000 / freq;
//     int on_us  = (int)(period_us * duty);
//     int off_us = period_us - on_us;

//     int cycles = (duration_ms * 1000) / period_us;

//     for (int i = 0; i < cycles; i++) {
//         gpio_set_level(BUZZER_PIN, 1);
//         esp_rom_delay_us(on_us);
//         gpio_set_level(BUZZER_PIN, 0);
//         esp_rom_delay_us(off_us);
//     }
// }

// void buzz_click(void)
// {
//     buzz_tone(2800, 12);
// }

// void buzz_notify(void)
// {
//     buzz_tone(2000, 20);
//     vTaskDelay(pdMS_TO_TICKS(30));
//     buzz_tone(2000, 20);
// }

// void buzz_error(void)
// {
//     buzz_tone(500, 60);
// }