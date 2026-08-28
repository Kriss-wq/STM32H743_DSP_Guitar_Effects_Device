#include "stdint.h"
#ifndef PROJECT2_EFFECT_INTERFACE_H
#define PROJECT2_EFFECT_INTERFACE_H
/*
 核心思想是有八个固定的结构体，内存分配位置固定，
 lvgl选择对应效果器后直接Init对应的结构体与Init为选择的效果器
 然后调整滑条可以调用Setup去修改参数
 最后由一条总处理函数去直接按照顺序遍历下来这8个固定的结构体处理，如果没有Init则跳过对应的结构体
 */
typedef struct
{
    char name[16];
    char param_name[3][16];
    uint8_t param[3];
    void (*Init)(void);
    void (*Process)(float *in, float *out, uint16_t size);
    void (*Setup)(uint8_t param1,uint8_t param2,uint8_t param3);
}effect_t;

typedef struct
{
    effect_t Effect[8];
}effect_all_t;
#endif //PROJECT2_EFFECT_INTERFACE_H