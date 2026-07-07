#pragma once

/*
 * Compatibility shim for Arduino-oriented third-party libraries that are used
 * from a pure ESP-IDF build.
 *
 * SensorLib's DRV2605 haptic driver calls millis() and delay(). Those helpers
 * are normally provided by Arduino, but this project builds with framework=espidf.
 */

#include <stdint.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline void delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

#ifdef __cplusplus
}
#endif
