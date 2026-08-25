#ifndef UI_BUTTONS_H
#define UI_BUTTONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lvgl/lvgl.h"

#define UI_BTN_COUNT 4

/** �� parent �ϴ�����ť����Ļ��ʼ�������һ�Σ��� */
void ui_buttons_create(lv_obj_t * parent);

/** ����ť���»ص����� freertos.c �ײ�ʵ�֡� */
void UiBtn1_OnPress(void);
void UiBtn2_OnPress(void);
void UiBtn3_OnPress(void);
void UiBtn4_OnPress(void);

/** ����ţ̌��ص����� freertos.c �ײ�ʵ�֡� */
void UiBtn1_OnRelease(void);
void UiBtn2_OnRelease(void);
void UiBtn3_OnRelease(void);
void UiBtn4_OnRelease(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
