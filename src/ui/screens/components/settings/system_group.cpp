#include "api/prefs.h"
#include "context/app_context.h"
#include "ui/components/ui_helpers.h"
#include "ui/screens/components/settings/settings_common.h"
#include "ui/screens/components/settings/settings_groups.h"

void open_system_group(lv_event_t *e)
{
    play_haptic_click();
    lv_obj_t *content = create_group_detail_page("System");

    lv_obj_t *system_card = ui_create_card(content);

    lv_obj_t *sys_title = lv_label_create(system_card);
    lv_label_set_text(sys_title, "System Information");
    lv_obj_set_style_text_color(sys_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_14, 0);
    lv_obj_align(sys_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *voltage_heading = lv_label_create(system_card);
    lv_label_set_text(voltage_heading, "Battery voltage");
    lv_obj_set_style_text_color(voltage_heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(voltage_heading, &lv_font_montserrat_12, 0);
    lv_obj_align_to(voltage_heading, sys_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    voltage_label = lv_label_create(system_card);
    lv_label_set_text(voltage_label, "00.00V");
    lv_obj_set_style_text_color(voltage_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(voltage_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(voltage_label, voltage_heading, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *gyro_temp_heading = lv_label_create(system_card);
    lv_label_set_text(gyro_temp_heading, "Gyroscope temperature");
    lv_obj_set_style_text_color(gyro_temp_heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(gyro_temp_heading, &lv_font_montserrat_12, 0);
    lv_obj_align_to(gyro_temp_heading, voltage_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    gyro_temp_label = lv_label_create(system_card);
    lv_label_set_text(gyro_temp_label, "00.00°F");
    lv_obj_set_style_text_color(gyro_temp_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(gyro_temp_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(gyro_temp_label, gyro_temp_heading, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *external_temp_heading = lv_label_create(system_card);
    lv_label_set_text(external_temp_heading, "Est. External temp.");
    lv_obj_set_style_text_color(external_temp_heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(external_temp_heading, &lv_font_montserrat_12, 0);
    lv_obj_align_to(external_temp_heading, gyro_temp_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    external_temp_label = lv_label_create(system_card);
    lv_label_set_text(external_temp_label, "00.00°F");
    lv_obj_set_style_text_color(external_temp_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(external_temp_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(external_temp_label, external_temp_heading, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *gyro_axis_heading = lv_label_create(system_card);
    lv_label_set_text(gyro_axis_heading, "Gyroscope axis");
    lv_obj_set_style_text_color(gyro_axis_heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(gyro_axis_heading, &lv_font_montserrat_12, 0);
    lv_obj_align_to(gyro_axis_heading, external_temp_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    gyro_axis_label = lv_label_create(system_card);
    lv_label_set_text(gyro_axis_label, "X: 00.00, Y: 00.00, Z: 00.00");
    lv_obj_set_style_text_color(gyro_axis_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(gyro_axis_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(gyro_axis_label, gyro_axis_heading, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *accel_axis_heading = lv_label_create(system_card);
    lv_label_set_text(accel_axis_heading, "Accelerometer axis");
    lv_obj_set_style_text_color(accel_axis_heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(accel_axis_heading, &lv_font_montserrat_12, 0);
    lv_obj_align_to(accel_axis_heading, gyro_axis_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    accel_axis_label = lv_label_create(system_card);
    lv_label_set_text(accel_axis_label, "X: 00.00, Y: 00.00, Z: 00.00");
    lv_obj_set_style_text_color(accel_axis_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(accel_axis_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(accel_axis_label, accel_axis_heading, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *is_screen_face_up_heading = lv_label_create(system_card);
    lv_label_set_text(is_screen_face_up_heading, "Screen face up:");
    lv_obj_set_style_text_color(is_screen_face_up_heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(is_screen_face_up_heading, &lv_font_montserrat_12, 0);
    lv_obj_align_to(is_screen_face_up_heading, accel_axis_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    is_screen_face_up_label = lv_label_create(system_card);
    lv_label_set_text(is_screen_face_up_label, "No");
    lv_obj_set_style_text_color(is_screen_face_up_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(is_screen_face_up_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(is_screen_face_up_label, is_screen_face_up_heading, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *is_screen_level_heading = lv_label_create(system_card);
    lv_label_set_text(is_screen_level_heading, "Screen level:");
    lv_obj_set_style_text_color(is_screen_level_heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(is_screen_level_heading, &lv_font_montserrat_12, 0);
    lv_obj_align_to(is_screen_level_heading, is_screen_face_up_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    is_screen_level_label = lv_label_create(system_card);
    lv_label_set_text(is_screen_level_label, "No");
    lv_obj_set_style_text_color(is_screen_level_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(is_screen_level_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(is_screen_level_label, is_screen_level_heading, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
}
