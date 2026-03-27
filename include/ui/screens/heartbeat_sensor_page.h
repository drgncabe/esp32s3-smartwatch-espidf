#pragma once

#include "lvgl.h"
#include "network/modules/heartbeat_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

void create_heartbeat_sensor_page(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
