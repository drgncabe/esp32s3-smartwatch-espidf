#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include <lvgl.h>

lv_obj_t *ui_create_card(lv_obj_t *parent);
lv_obj_t *ui_create_group_card(lv_obj_t *parent, const char *title, const char *subtitle,
                                const char *icon, lv_event_cb_t callback);
lv_obj_t *ui_create_transparent_container(lv_obj_t *parent);
lv_obj_t *ui_create_scrollable_flex_container(lv_obj_t *parent, lv_flex_flow_t flow);
lv_obj_t *ui_create_title_label(lv_obj_t *parent, const char *text);
lv_obj_t *ui_create_subtitle_label(lv_obj_t *parent, const char *text);
lv_obj_t *ui_create_body_label(lv_obj_t *parent, const char *text);

#endif
