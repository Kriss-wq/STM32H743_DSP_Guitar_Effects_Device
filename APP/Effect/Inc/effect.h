#ifndef PROJECT2_EFFECT_H
#define PROJECT2_EFFECT_H

#include "Test.h"
void effect_init(void);
effect_t** effect_get(void); //返回所有效果器的字段
effect_all_t* effect_buffer_get(void); //返回效果器处理链的字段
#endif