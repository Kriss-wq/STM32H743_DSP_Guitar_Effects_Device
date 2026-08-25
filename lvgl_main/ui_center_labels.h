#ifndef UI_CENTER_LABELS_H
#define UI_CENTER_LABELS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lvgl/lvgl.h"

#define UI_CENTER_LABEL_COUNT 4

/** 在 parent 上创建 4 个居中标签（屏幕初始化后调用一次）。 */
void ui_center_labels_create(lv_obj_t * parent);

/** 更新某一行文本。line：0 ~ 3。text 为 NULL 时清空，可安全调用。 */
void ui_center_labels_set_text(uint8_t line, const char * text);

/** 格式化更新文本，等同于 snprintf + set_text。line：0 ~ 3。 */
void ui_center_labels_set_text_fmt(uint8_t line, const char * fmt, ...);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
