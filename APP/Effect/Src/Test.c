#include "stdint.h"
#include "Test.h"
#include "db.h"
//测试范例为TS808
static void Test_Effect_Init(effect_t* effect);
static void Test_Effect_Setup(effect_t* effect,uint8_t Gain,uint8_t Tone,uint8_t Level);
static void Test_Effect_Process(effect_t* effect,float *in, float *out, uint16_t size);
effect_t* Get_Test_t(void);

effect_t Test_t = {
    .name = "Test",
    .param_name = {"Gain","Tone","Level"},
    .Init = Test_Effect_Init,
    .Setup = Test_Effect_Setup,
    .Process = Test_Effect_Process
};

static void Test_Effect_Init(effect_t* effect)
{
    effect->param[0] = 50u;
    effect->param[1] = 50u;
    effect->param[2] = 50u;
}

static void Test_Effect_Setup(effect_t* effect,uint8_t Gain,uint8_t Tone,uint8_t Level)
{
    effect->param[0] = Gain;
    effect->param[1] = Tone;
    effect->param[2] = Level;
}

static void Test_Effect_Process(effect_t* effect,float *in, float *out, uint16_t size)
{
    float gain = 1.0f + ((float)effect->param[0] / 100.0f) * 19.0f;
    float level = (float)effect->param[2] / 100.0f;

    for (uint16_t i = 0; i < size; i++)
    {
        float x = in[i] * gain;
        float x_abs = (x < 0.0f) ? -x : x;
        float sign = (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f);

        // Schetzen 非线性对称软削波
        float clipped;
        if (x_abs < (1.0f / 3.0f))
        {
            clipped = 2.0f * x;
        }
        else if (x_abs > (2.0f / 3.0f))
        {
            clipped = sign;
        }
        else
        {
            float temp = 2.0f - 3.0f * x_abs;
            clipped = sign * (3.0f - temp * temp) / 3.0f;
        }

        out[i] = clipped * level;
    }
    db_reduce(out, out, size, DB_15);
}

effect_t* Get_Test_t(void)
{
    return &Test_t;
}