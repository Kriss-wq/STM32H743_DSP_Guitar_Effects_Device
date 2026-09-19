#ifndef PROJECT2_EFFECT_H
#define PROJECT2_EFFECT_H
#include "effect_interface.h"

void effect_init(void);
void effect_slot_clear(uint8_t slot);      //清空链上某个槽位(等效于置 NULL)
effect_t** effect_get(void); //返回所有效果器的字段
effect_process_t* effect_buffer_get(void); //返回效果器处理链的字段
void effect_process(float *in, float *out, uint16_t size);
#endif