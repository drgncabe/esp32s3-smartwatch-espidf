#include "ui/components/toast.h"

static void toast_delete_cb(lv_timer_t *timer)
{
    lv_obj_del((lv_obj_t *)timer->user_data);
}

static void msgbox_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    lv_obj_del(mbox);
}

void show_toast(const char *msg, uint32_t ms)
{
    lv_obj_t *toast = lv_obj_create(lv_scr_act());
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_pad_all(toast, 8, 0);
    lv_obj_set_style_border_width(toast, 0, 0);
    lv_obj_set_style_shadow_width(toast, 8, 0);
    lv_obj_set_style_shadow_opa(toast, LV_OPA_30, 0);

    lv_obj_t *lbl = lv_label_create(toast);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_timer_t *timer = lv_timer_create(toast_delete_cb, ms, toast);
    lv_timer_set_repeat_count(timer, 1);
}

void show_popup(const char *text)
{
    static const char *btns[] = {"OK", ""};

    lv_obj_t *mbox = lv_msgbox_create(NULL, "Info", text, btns, false);
    lv_obj_center(mbox);

    lv_obj_add_event_cb(mbox, msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
