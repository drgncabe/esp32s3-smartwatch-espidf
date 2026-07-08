#pragma once

#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "soc/gpio_num.h"

// Pin map for Waveshare ESP32-S3-LCD-1.69 v2
#define XPOWERS_CHIP_AXP2101 0

#define LCD_DC GPIO_NUM_4
#define LCD_CS GPIO_NUM_5
#define LCD_SCK GPIO_NUM_6
#define LCD_MOSI GPIO_NUM_7
#define LCD_RST GPIO_NUM_8
#define LCD_BL GPIO_NUM_15

#define I2C_SDA GPIO_NUM_11
#define I2C_SCL GPIO_NUM_10
#define TP_RST GPIO_NUM_13
#define TP_INT GPIO_NUM_14
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_FREQ_HZ 400000

#define BAT_ADC ADC_CHANNEL_0
#define SYS_EN GPIO_NUM_41
#define PWR_BTN GPIO_NUM_40
#define BUZZER_PIN GPIO_NUM_42

#define LCD_WIDTH 240
#define LCD_HEIGHT 280

#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLK 20000000
