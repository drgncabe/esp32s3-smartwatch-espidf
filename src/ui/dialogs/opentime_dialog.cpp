#include <stdio.h>
#include <string.h>
#include <time.h>

#include <lvgl.h>
#include "esp_log.h"
#include "network/modules/open_scanner.h"
#include "ui/dialogs/opentime_dialog.h"

static const char *TAG = "opentime_dialog";

static lv_obj_t *opentime_dialog = NULL;
static lv_obj_t *log_container = NULL;

static char last_log_text[96] = "";
static uint16_t log_line_count = 0;

#define MAX_LOG_LINES 40

/* -------------------------------------------------------
   Helpers
------------------------------------------------------- */

static void get_time_string(char *out, size_t len)
{
    time_t now;
    time(&now);

    if (now < 1609459200) { // time not synced yet (before 2021)
        snprintf(out, len, "--:--:--");
        return;
    }

    struct tm t;
    localtime_r(&now, &t);
    snprintf(out, len, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
}

static void append_log_line(const char *text)
{
    if (!log_container || !text || !text[0])
        return;

    if (strcmp(text, last_log_text) == 0)
        return;

    strncpy(last_log_text, text, sizeof(last_log_text));
    last_log_text[sizeof(last_log_text) - 1] = '\0';

    char time_buf[16];
    char line_buf[128];

    get_time_string(time_buf, sizeof(time_buf));
    snprintf(line_buf, sizeof(line_buf), "[%s] %s", time_buf, text);

    lv_obj_t *lbl = lv_label_create(log_container);

    lv_label_set_text(lbl, line_buf);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_pad_bottom(lbl, 2, 0);

    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE6E6E6), 0);

    log_line_count++;

    while (log_line_count > MAX_LOG_LINES) {
        lv_obj_t *first = lv_obj_get_child(log_container, 0);
        if (!first)
            break;
        lv_obj_del(first);
        log_line_count--;
    }

    lv_obj_scroll_to_y(log_container, LV_COORD_MAX, LV_ANIM_OFF);
}

/* -------------------------------------------------------
   Timer
------------------------------------------------------- */

static void update_opentime_dialog(lv_timer_t *t)
{
    append_log_line(open_scanner_status_text);
}

/* -------------------------------------------------------
   Close
------------------------------------------------------- */

void close_opentime_dialog(lv_event_t *e)
{
    if (opentime_dialog) {
        lv_obj_del(opentime_dialog);
        opentime_dialog = NULL;
        log_container = NULL;
        log_line_count = 0;
        last_log_text[0] = '\0';
    }
}

/* -------------------------------------------------------
   Open
------------------------------------------------------- */

void open_opentime_dialog(void)
{
    if (opentime_dialog)
        return;

    /* =============================
       OVERLAY
       ============================= */
    opentime_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(opentime_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(opentime_dialog, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(opentime_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(opentime_dialog, 0, 0);
    lv_obj_set_style_pad_all(opentime_dialog, 8, 0);
    lv_obj_clear_flag(opentime_dialog, LV_OBJ_FLAG_SCROLLABLE);

    /* =============================
       CONTENT COLUMN
       ============================= */
    lv_obj_t *content = lv_obj_create(opentime_dialog);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* =============================
       HEADER (FIXED, CLEAN)
       ============================= */
    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 44);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_left(header, 6, 0);
    lv_obj_set_style_pad_right(header, 6, 0);
    lv_obj_set_style_pad_top(header, 0, 0);
    lv_obj_set_style_pad_bottom(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* Horizontal row */
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        header,
        LV_FLEX_ALIGN_START,  // main axis
        LV_FLEX_ALIGN_CENTER, // cross axis
        LV_FLEX_ALIGN_CENTER);

    /* --- Title wrapper (flex-grow) --- */
    lv_obj_t *title_wrap = lv_obj_create(header);
    lv_obj_set_flex_grow(title_wrap, 1);
    lv_obj_set_style_bg_opa(title_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_wrap, 0, 0);
    lv_obj_set_style_pad_all(title_wrap, 0, 0);
    lv_obj_clear_flag(title_wrap, LV_OBJ_FLAG_SCROLLABLE);

    /* Center title vertically & horizontally within wrapper */
    lv_obj_set_flex_flow(title_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        title_wrap,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(title_wrap);
    lv_label_set_text(title, "OpenTime Sync");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, LV_PCT(100));

    /* --- Close button (NO FLOATING) --- */
    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 28, 28);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, close_opentime_dialog, LV_EVENT_CLICKED, NULL);

    lv_obj_t *x = lv_label_create(close_btn);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_center(x);

    /* =============================
       LOG CARD
       ============================= */
    log_container = lv_obj_create(content);
    lv_obj_set_width(log_container, LV_PCT(100));
    lv_obj_set_flex_grow(log_container, 1);
    lv_obj_set_flex_flow(log_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(log_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(log_container, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_row(log_container, 1, 0);
    lv_obj_set_style_bg_opa(log_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(log_container, 0, 0);
    lv_obj_set_style_pad_bottom(log_container, 5, 0);

    /* =============================
       START
       ============================= */
    append_log_line("Starting OpenTime Sync...");
    start_open_time_task();

    lv_timer_create(update_opentime_dialog, 500, NULL);
}