#include "stdint.h"
#include "Test.h"
//测试范例为TS808

static struct
{
    uint8_t Gain;
    uint8_t Tone;
    uint8_t Level;
}Test_Param_t;
static void Test_Effect_Init(void)
{
    Test_Param_t.Gain = 50u;
    Test_Param_t.Tone = 50u;
    Test_Param_t.Level = 50u;
}

static void Test_Effect_Setup(uint8_t Gain,uint8_t Tone,uint8_t Level)
{
    Test_Param_t.Gain = Gain;
    Test_Param_t.Tone = Tone;
    Test_Param_t.Level = Level;
}

static void Test_Effect_Process(float *in, float *out, uint16_t size)
{
    float gain = 1.0f + ((float)Test_Param_t.Gain / 100.0f) * 19.0f;
    float level = (float)Test_Param_t.Level / 100.0f;

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
}

effect_t Test_t = {
    .name = "Test",
    .Init = Test_Effect_Init,
    .Setup = Test_Effect_Setup,
    .Process = Test_Effect_Process
};

effect_t Get_Test_t(void)
{
    return Test_t;
}