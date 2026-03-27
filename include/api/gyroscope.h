#pragma once

#include <stdbool.h>

struct IMUdata {
    float x;
    float y;
    float z;
};

enum IMUAxis {
    X,
    Y,
    Z
};

void enable_gyroscope();
void disable_gyroscope();
float get_gyro_temp_f();
float gyro_temp_to_external_f();
float get_gyro_value(IMUAxis axis);
float get_accel_value(IMUAxis axis);
bool is_screen_face_up();
bool is_screen_level();

extern bool gyroEnabled;
extern bool gyroSetup;
