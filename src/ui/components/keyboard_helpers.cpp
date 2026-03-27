#include "ui/components/keyboard_helpers.h"

void ui_scroll_to_textarea(lv_obj_t *ta)
{
    if (!ta) return;
    
    // Find the scrollable content container
    lv_obj_t *content = lv_obj_get_parent(ta);
    while (content && !lv_obj_has_flag(content, LV_OBJ_FLAG_SCROLLABLE))
    {
        content = lv_obj_get_parent(content);
    }
    
    if (!content) return;
    
    // Get textarea position relative to content
    lv_coord_t ta_y = lv_obj_get_y(ta);
    lv_obj_t *parent = lv_obj_get_parent(ta);
    while (parent && parent != content)
    {
        ta_y += lv_obj_get_y(parent);
        parent = lv_obj_get_parent(parent);
    }
    
    // Get dimensions
    lv_coord_t ta_height = lv_obj_get_height(ta);
    lv_coord_t content_height = lv_obj_get_height(content);
    lv_coord_t keyboard_height = 120;
    
    // Calculate target scroll position
    // We want the textarea to be visible above the keyboard with some padding
    lv_coord_t visible_height = content_height - keyboard_height;
    lv_coord_t target_y = ta_y - (visible_height - ta_height - 20); // 20px padding
    
    // Only scroll if needed
    lv_coord_t current_scroll = lv_obj_get_scroll_y(content);
    if (target_y > current_scroll)
    {
        lv_obj_scroll_to_y(content, target_y, LV_ANIM_ON);
    }
}
