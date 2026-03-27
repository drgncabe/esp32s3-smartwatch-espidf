#ifndef HEARTBEAT_RADAR_WIDGET_H
#define HEARTBEAT_RADAR_WIDGET_H

#include "lvgl.h"
#include "network/modules/heartbeat_sensor.h"

lv_obj_t *heartbeat_radar_widget_create(lv_obj_t *parent);
void heartbeat_radar_widget_update(lv_obj_t *radar_widget, heartbeat_item_t *items, int count);
void heartbeat_radar_widget_clear(lv_obj_t *radar_widget);
void heartbeat_radar_widget_set_radius(lv_obj_t *radar_widget, int radius);
void heartbeat_radar_widget_delete(lv_obj_t *radar_widget);

#endif
