#ifndef SETTINGS_COMMON_H
#define SETTINGS_COMMON_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    lv_obj_t *obj_one;
    lv_obj_t *obj_two;
    lv_obj_t *obj_three;
} multi_event_data_t;

typedef struct {
    lv_obj_t *ta;
    lv_obj_t *kb;
    lv_obj_t *edit_btn;
    lv_obj_t *save_btn;
    lv_obj_t *cancel_btn;
    bool is_editing;
} zip_edit_state_t;

typedef struct {
    lv_obj_t *ssid_ta;
    lv_obj_t *password_ta;
    lv_obj_t *ssid_kb;
    lv_obj_t *password_kb;
    lv_obj_t *save_btn;
    lv_obj_t *container;
} hotspot_settings_state_t;

typedef struct {
    multi_event_data_t *gyro;
    multi_event_data_t *wifi;
    multi_event_data_t *haptics;
    multi_event_data_t *powerSave;
    multi_event_data_t *periodicConnect;
    multi_event_data_t *hotspot;
    multi_event_data_t *networkTime;
} SettingsUserData;

typedef void (*weather_update_cb_t)(void);

extern lv_obj_t *settings_screen;
extern lv_obj_t *main_settings_container;
extern lv_obj_t *group_detail_container;
extern lv_obj_t *active_ta;
extern lv_obj_t *active_kb;
extern lv_obj_t *voltage_label;
extern lv_obj_t *gyro_temp_label;
extern lv_obj_t *external_temp_label;
extern lv_obj_t *gyro_axis_label;
extern lv_obj_t *accel_axis_label;
extern lv_obj_t *is_screen_face_up_label;
extern lv_obj_t *is_screen_level_label;

extern SettingsUserData settings_ud;
extern hotspot_settings_state_t hotspot_state;
extern zip_edit_state_t zip_state;
extern char pending_zipCode_buf[33];
extern bool prevent_toggle;
extern bool settings_need_saved;
extern weather_update_cb_t weather_update_callback;

lv_obj_t *create_group_detail_page(const char *title);
void close_group_detail(lv_event_t *e);

void content_clicked_cb(lv_event_t *e);
void change_wifi_mode(lv_event_t *e);
void save_hotspot_settings(lv_event_t *e);
void change_hotspot_mode(lv_event_t *e);
void change_periodic_connect_mode(lv_event_t *e);
void change_network_time_mode(lv_event_t *e);
void on_time_field_clicked(lv_event_t *e);
void start_open_scanner_ts(lv_event_t *e);
void change_gyro_mode(lv_event_t *e);
void change_modern_clock_ui_mode(lv_event_t *e);
void change_haptics_mode(lv_event_t *e);
void change_power_mode(lv_event_t *e);
void power_off_watch_handler(lv_event_t *e);
void restart_watch_handler(lv_event_t *e);
void kb_event_cb(lv_event_t *e);
void cancel_zip_edit(lv_event_t *e);
void start_zip_edit(lv_event_t *e);
void zip_ta_clicked_cb(lv_event_t *e);
void zipCode_changed_event(lv_event_t *e);
void save_weather_settings(lv_event_t *e);

#endif
