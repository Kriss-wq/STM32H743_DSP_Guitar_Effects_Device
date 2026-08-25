#include "ui_buttons.h"
#include "ui.h"

#include <stdint.h>

static lv_obj_t * s_btns[UI_BTN_COUNT];

static void ui_btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    if(code == LV_EVENT_PRESSED) {
        switch(id) {
        case 0: UiBtn1_OnPress(); break;
        case 1: UiBtn2_OnPress(); break;
        case 2: UiBtn3_OnPress(); break;
        case 3: UiBtn4_OnPress(); break;
        default: break;
        }
    } else if(code == LV_EVENT_RELEASED) {
        switch(id) {
        case 0: UiBtn1_OnRelease(); break;
        case 1: UiBtn2_OnRelease(); break;
        case 2: UiBtn3_OnRelease(); break;
        case 3: UiBtn4_OnRelease(); break;
        default: break;
        }
    }
}

void ui_buttons_create(lv_obj_t * parent)
{
    static const char * const captions[UI_BTN_COUNT] = {
        "Btn1", "Btn2", "Btn3", "Btn4"
    };
    /* �ײ�һ�ţ��ܿ��м���б�ǩ */
    static const lv_coord_t pos_x[UI_BTN_COUNT] = {
        -270, -90, 90, 270
    };
    uint8_t i;

    if(parent == NULL) {
        return;
    }

    for(i = 0; i < UI_BTN_COUNT; i++) {
        s_btns[i] = lv_btn_create(parent);
        lv_obj_set_size(s_btns[i], 140, 56);
        lv_obj_set_x(s_btns[i], pos_x[i]);
        lv_obj_set_y(s_btns[i], 180);
        lv_obj_set_align(s_btns[i], LV_ALIGN_CENTER);
        lv_obj_add_event_cb(s_btns[i], ui_btn_event_cb, LV_EVENT_PRESSED,
                            (void *)(uintptr_t)i);
        lv_obj_add_event_cb(s_btns[i], ui_btn_event_cb, LV_EVENT_RELEASED,
                            (void *)(uintptr_t)i);

        lv_obj_t * label = lv_label_create(s_btns[i]);
        lv_label_set_text(label, captions[i]);
        lv_obj_set_style_text_font(label, &ui_font_Font1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(label);
    }
}
