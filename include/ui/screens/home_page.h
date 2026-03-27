#ifndef LAYOUT_H
#define LAYOUT_H

#include <lvgl.h>

/** Extends clickable area around UI elements. Range 10–25 recommended. */
#define GLOBAL_EXT_CLICK_AREA 30

void initialize_layout(void);
void refresh_weather_container(bool success = true);

#endif
