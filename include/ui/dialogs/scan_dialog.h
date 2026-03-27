#ifndef SCAN_DIALOG_H
#define SCAN_DIALOG_H

#include <lvgl.h>

void wifi_ssid_tapped(lv_event_t *e);
void update_wifi_scan_results(lv_timer_t *t);
void close_wifi_dialog();
void open_wifi_scan_dialog();
void open_wifi_scan_pre_dialog();
void open_wifi_password_dialog(const char *ssid, const char *password);
void wifi_password_submit(lv_event_t *);
void connect_to_wifi(const char *ssid, const char *pass);

#endif
