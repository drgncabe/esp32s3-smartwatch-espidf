#include "api/gyroscope.h"
#include "configuration/pin_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gyroscope";

// QMI8658 I2C Address
#define QMI8658_SLAVE_ADDRESS 0x6B

// QMI8658 Register Map
#define QMI8658_WHO_AM_I        0x00
#define QMI8658_CTRL1           0x02
#define QMI8658_CTRL2           0x03
#define QMI8658_CTRL3           0x04
#define QMI8658_CTRL7           0x08
#define QMI8658_STATUS0         0x2D
#define QMI8658_TEMP_L          0x33
#define QMI8658_TEMP_H          0x34
#define QMI8658_AX_L            0x35
#define QMI8658_AX_H            0x36
#define QMI8658_AY_L            0x37
#define QMI8658_AY_H            0x38
#define QMI8658_AZ_L            0x39
#define QMI8658_AZ_H            0x3A
#define QMI8658_GX_L            0x3B
#define QMI8658_GX_H            0x3C
#define QMI8658_GY_L            0x3D
#define QMI8658_GY_H            0x3E
#define QMI8658_GZ_L            0x3F
#define QMI8658_GZ_H            0x40
#define QMI8658_RESET           0x60

// Accelerometer and Gyroscope ranges
#define ACC_RANGE_4G            0x01
#define ACC_ODR_250HZ           0x05
#define GYR_RANGE_256DPS        0x03
#define GYR_ODR_250HZ           0x05

IMUdata acc;   // accelerometer data
IMUdata gyr;   // gyroscope data

bool gyroSetup = false;
bool gyroEnabled = false;

static float acc_scale = 1.0f;
static float gyr_scale = 1.0f;

// I2C helper functions
static esp_err_t qmi8658_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t write_buf[2] = {reg, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, QMI8658_SLAVE_ADDRESS, 
                                      write_buf, 2, pdMS_TO_TICKS(1000));
}

static esp_err_t qmi8658_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, QMI8658_SLAVE_ADDRESS,
                                        &reg, 1, data, len, pdMS_TO_TICKS(1000));
}

static esp_err_t qmi8658_read_data(uint8_t reg, int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t data[6];
    esp_err_t ret = qmi8658_read_reg(reg, data, 6);
    if (ret == ESP_OK) {
        *x = (int16_t)((data[1] << 8) | data[0]);
        *y = (int16_t)((data[3] << 8) | data[2]);
        *z = (int16_t)((data[5] << 8) | data[4]);
    }
    return ret;
}

void setup_gyroscope(void)
{
    if (gyroSetup) return;
    
    ESP_LOGI(TAG, "QMI8658 IMU initialization starting...");

    // Note: I2C driver should already be initialized by main.cpp
    // We don't need to initialize it again here

    // Check WHO_AM_I
    uint8_t chip_id = 0;
    esp_err_t ret = qmi8658_read_reg(QMI8658_WHO_AM_I, &chip_id, 1);
    if (ret != ESP_OK || chip_id != 0x05) {
        ESP_LOGE(TAG, "Failed to find QMI8658! ID: 0x%02X", chip_id);
        return;
    }
    
    ESP_LOGI(TAG, "QMI8658 Device ID: 0x%02X", chip_id);

    // Software reset
    qmi8658_write_reg(QMI8658_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Configure accelerometer: +/-4g range, 250Hz ODR
    // CTRL2: [7:4]=ODR, [3:1]=Range, [0]=Enable
    qmi8658_write_reg(QMI8658_CTRL2, (ACC_ODR_250HZ << 4) | (ACC_RANGE_4G << 1));
    acc_scale = 4.0f / 32768.0f;

    // Configure gyroscope: +/-256dps range, 250Hz ODR
    // CTRL3: [7:4]=ODR, [3:1]=Range, [0]=Enable
    qmi8658_write_reg(QMI8658_CTRL3, (GYR_ODR_250HZ << 4) | (GYR_RANGE_256DPS << 1));
    gyr_scale = 256.0f / 32768.0f;

    // Enable sensors in CTRL7
    // Bit 0: Accel enable, Bit 1: Gyro enable
    qmi8658_write_reg(QMI8658_CTRL7, 0x03);

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "IMU configured successfully!");
    gyroSetup = true;
}

void enable_gyroscope(void)
{
    if (gyroEnabled) return;
    if (!gyroSetup) {
        setup_gyroscope();
    }

    // Enable both accelerometer and gyroscope
    qmi8658_write_reg(QMI8658_CTRL7, 0x03);
    
    gyroEnabled = true;
    ESP_LOGI(TAG, "Gyroscope enabled");
}

void disable_gyroscope(void)
{
    if (!gyroEnabled) return;
    
    // Disable both sensors
    qmi8658_write_reg(QMI8658_CTRL7, 0x00);
    
    gyroEnabled = false;
    ESP_LOGI(TAG, "Gyroscope disabled");
}

float get_gyro_temp_f(void)
{
    uint8_t temp_data[2];
    if (qmi8658_read_reg(QMI8658_TEMP_L, temp_data, 2) == ESP_OK) {
        int16_t temp_raw = (int16_t)((temp_data[1] << 8) | temp_data[0]);
        float temp_c = temp_raw / 256.0f;
        return (temp_c * 9.0f / 5.0f) + 32.0f;
    }
    return 0.0f;
}

float gyro_temp_to_external_f(void)
{
    float baseExternal = 69.0f;
    float baseGyro = 88.6f;
    float gtempF = get_gyro_temp_f();
    float c1 = (baseExternal * gtempF) / baseGyro;
    return c1;
}

static bool get_data_ready(void)
{
    uint8_t status = 0;
    if (qmi8658_read_reg(QMI8658_STATUS0, &status, 1) == ESP_OK) {
        return (status & 0x03) != 0;  // Check if accel or gyro data ready
    }
    return false;
}

float get_gyro_value(IMUAxis axis)
{
    if (!get_data_ready()) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!get_data_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    int16_t gx, gy, gz;
    if (qmi8658_read_data(QMI8658_GX_L, &gx, &gy, &gz) == ESP_OK) {
        gyr.x = gx * gyr_scale;
        gyr.y = gy * gyr_scale;
        gyr.z = gz * gyr_scale;
        
        switch (axis) {
            case X: return gyr.x;
            case Y: return gyr.y;
            case Z: return gyr.z;
        }
    }
    return 0.0f;
}

float get_accel_value(IMUAxis axis)
{
    if (!get_data_ready()) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!get_data_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    int16_t ax, ay, az;
    if (qmi8658_read_data(QMI8658_AX_L, &ax, &ay, &az) == ESP_OK) {
        acc.x = ax * acc_scale;
        acc.y = ay * acc_scale;
        acc.z = az * acc_scale;
        
        switch (axis) {
            case X: return acc.x;
            case Y: return acc.y;
            case Z: return acc.z;
        }
    }
    return 0.0f;
}

bool is_screen_face_up(void)
{
    return get_accel_value(IMUAxis::Z) < 0;
}

bool is_screen_level(void)
{
    float az = get_accel_value(IMUAxis::Z);
    float ax = get_accel_value(IMUAxis::X);
    float ay = get_accel_value(IMUAxis::Y);
    return az < -0.9 && (ax >= .02 && ax <= .04) && (ay >= .01 && ay <= .03);
}

void run_gyro_test(void)
{
    if (get_data_ready()) {
        int16_t ax, ay, az;
        if (qmi8658_read_data(QMI8658_AX_L, &ax, &ay, &az) == ESP_OK) {
            acc.x = ax * acc_scale;
            acc.y = ay * acc_scale;
            acc.z = az * acc_scale;

            if (acc.y < -0.3) {
                ESP_LOGI(TAG, "Tilting to right");
            } else if (acc.y > 0.3) {
                ESP_LOGI(TAG, "Tilting to left");
            }

            if (acc.x < -0.3) {
                ESP_LOGI(TAG, "Tilting to top");
            } else if (acc.x > 0.3) {
                ESP_LOGI(TAG, "Tilting to bottom");
            }

            if (acc.z < 0) {
                ESP_LOGI(TAG, "Watch is face up");
            } else if (acc.z > 0) {
                ESP_LOGI(TAG, "Watch is face down");
            }
        } else {
            ESP_LOGE(TAG, "ACC read failed");
        }

        int16_t gx, gy, gz;
        if (qmi8658_read_data(QMI8658_GX_L, &gx, &gy, &gz) == ESP_OK) {
            gyr.x = gx * gyr_scale;
            gyr.y = gy * gyr_scale;
            gyr.z = gz * gyr_scale;
        } else {
            ESP_LOGE(TAG, "GYR read failed");
        }

        ESP_LOGI(TAG, "Temperature: %.2f °F", get_gyro_temp_f());
    }

    vTaskDelay(pdMS_TO_TICKS(200));
}