#include "effect_interface.h"
#include "effect_conf.h"
#include "effect.h"
effect_t* All_Effect[EFFECT_SIZE];
effect_all_t Effect_Buffer;
void effect_init(void)
{
    All_Effect[0] = Get_Test_t();
}

effect_t** effect_get(void)
{
    return All_Effect;
}

effect_all_t* effect_buffer_get(void)
{
    return &Effect_Buffer;
}