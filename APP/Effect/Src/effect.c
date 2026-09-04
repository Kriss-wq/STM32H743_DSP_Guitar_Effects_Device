#include "effect_interface.h"
#include "effect_conf.h"
#include "effect.h"

#include <stddef.h>
#include <string.h>

#include "Test.h"
#include "Vintage30.h"
#include "TS808.h"
#include "cpptest.h"
#include "Namtest.h"
#include "Delay.h"
#define EFFECT_CHAIN_LEN   8
#define EFFECT_MAX_BLOCK   64

effect_t* All_Effect[EFFECT_SIZE];
effect_process_t Effect_Buffer;

/* 链式中间缓冲:两块乒乓。CPU 只在 in/out 与这两块之间倒手,任何一级都不原地覆盖输入 */
__attribute__((aligned(32)))__attribute__((section(".ram")))
static float effect_scratch[2][EFFECT_MAX_BLOCK];
/*
 如果要添加新效果器，直接在这里Get对应效果器的结构体就可以了，不需要其他操作
 */
void effect_init(void)
{
    All_Effect[0] = Get_Test_t();
    All_Effect[1] = Get_Vintage30_t();
    All_Effect[2] = Get_TS808_t();
    All_Effect[3] = Get_NAMTest_t();
    All_Effect[4] = Get_Delay_t();
}

/* 清空一个槽位:整块清零后 Process==NULL,链遍历时会自动跳过 */
void effect_slot_clear(uint8_t slot)
{
    if (slot < EFFECT_CHAIN_LEN)
        memset(&Effect_Buffer.Effect[slot], 0, sizeof(effect_t));
}

void effect_process(float *in, float *out, uint16_t size)
{
    if (in == NULL || out == NULL || size == 0)
        return;
    if (size > EFFECT_MAX_BLOCK)
        size = EFFECT_MAX_BLOCK;

    float  *cur    = in;
    uint8_t active = 0;

    for (uint8_t i = 0; i < EFFECT_CHAIN_LEN; i++)
    {
        effect_t *fx = &Effect_Buffer.Effect[i];
        if (!fx->enable || fx->Process == NULL)
            continue;

        float *next = (active == 0) ? effect_scratch[0]: (cur == effect_scratch[0]) ? effect_scratch[1]: effect_scratch[0];

        fx->Process(fx, cur, next, size);
        cur = next;
        active++;
    }

    if (active == 0)
    {
        if (in != out)
            memcpy(out, in, (size_t)size * sizeof(float));
    }
    else if (cur != out)
    {
        memcpy(out, cur, (size_t)size * sizeof(float));
    }
}
effect_t** effect_get(void)
{
    return All_Effect;
}

effect_process_t* effect_buffer_get(void)
{
    return &Effect_Buffer;
}