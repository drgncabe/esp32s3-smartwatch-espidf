#ifndef SETTINGS_PAGE_H
#define SETTINGS_PAGE_H

#include <lvgl.h>
#include <stdint.h>

void open_settings(lv_event_t *e);
void close_settings(lv_event_t *e);
bool is_settings_open(void);
void update_settings_system_info(void);
void on_time_field_clicked(lv_event_t *e);
void update_time_field_label(void);

#endif
