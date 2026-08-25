#include "ui_center_labels.h"
#include "ui.h"

#include <stdarg.h>
#include <stdio.h>

#define UI_CENTER_LABEL_BUF_SIZE 128

static lv_obj_t * s_labels[UI_CENTER_LABEL_COUNT];

/* 4 行相对屏幕中心的垂直偏移（分辨率 800x480）。 */
static const lv_coord_t s_line_y[UI_CENTER_LABEL_COUNT] = {
    -90, -30, 30, 90
};

void ui_center_labels_create(lv_obj_t * parent)
{
    uint8_t i;

    if(parent == NULL) {
        return;
    }

    for(i = 0; i < UI_CENTER_LABEL_COUNT; i++) {
        s_labels[i] = lv_label_create(parent);
        lv_obj_set_width(s_labels[i], 720);
        lv_obj_set_height(s_labels[i], LV_SIZE_CONTENT);
        lv_obj_set_x(s_labels[i], 0);
        lv_obj_set_y(s_labels[i], s_line_y[i]);
        lv_obj_set_align(s_labels[i], LV_ALIGN_CENTER);
        lv_label_set_long_mode(s_labels[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_labels[i], "");
        lv_obj_set_style_text_align(s_labels[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(s_labels[i], &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void ui_center_labels_set_text(uint8_t line, const char * text)
{
    if(line >= UI_CENTER_LABEL_COUNT || s_labels[line] == NULL) {
        return;
    }

    lv_label_set_text(s_labels[line], text != NULL ? text : "");
}

void ui_center_labels_set_text_fmt(uint8_t line, const char * fmt, ...)
{
    char buf[UI_CENTER_LABEL_BUF_SIZE];
    va_list args;

    if(line >= UI_CENTER_LABEL_COUNT || s_labels[line] == NULL || fmt == NULL) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    lv_label_set_text(s_labels[line], buf);
}
