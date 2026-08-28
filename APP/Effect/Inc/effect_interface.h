
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
    void (*Init)();
    void (*Process)(float *in, float *out, uint16_t size);
    void (*Setup)(uint8_t Gain,uint8_t Tone,uint8_t Level);
}effect_t;

typedef struct
{
    effect_t Effect[8];
}effect_all_t;
#endif //PROJECT2_EFFECT_INTERFACE_H