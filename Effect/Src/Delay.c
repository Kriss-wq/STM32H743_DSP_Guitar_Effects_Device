#include "Delay.h"
#include <string.h>

#define DELAY_FS           48000u
#define DELAY_BUF_LEN      24000u
#define DELAY_MAX_MS       500u

static void Delay_Effect_Init(effect_t* effect);
static void Delay_Effect_Setup(effect_t* effect, uint8_t Time, uint8_t Mix, uint8_t Repeat);
static void Delay_Effect_Process(effect_t* effect, float *in, float *out, uint16_t size);

effect_t Delay_t = {
    .name = "Delay",
    .param_name = {"Time", "Mix", "Repeat"},
    .Init = Delay_Effect_Init,
    .Setup = Delay_Effect_Setup,
    .Process = Delay_Effect_Process
};


__attribute__((aligned(32))) __attribute__((section(".ram")))static float DelayEffect_Buffer[DELAY_BUF_LEN];

static uint16_t write_pos;

static void Delay_Effect_Init(effect_t* effect)
{
    effect->param[0] = 50u;
    effect->param[1] = 50u;
    effect->param[2] = 50u;
    write_pos = 0u;
    memset(DelayEffect_Buffer, 0, sizeof(DelayEffect_Buffer));
}

static void Delay_Effect_Setup(effect_t* effect, uint8_t Time, uint8_t Mix, uint8_t Repeat)
{
    effect->param[0] = Time;
    effect->param[1] = Mix;
    effect->param[2] = Repeat;
}

static void Delay_Effect_Process(effect_t* effect, float *in, float *out, uint16_t size)
{
    uint32_t delay_ms = ((uint32_t)effect->param[0] * DELAY_MAX_MS) / 100u;
    uint32_t delay_samples = (delay_ms * DELAY_FS) / 1000u;
    if (delay_samples >= DELAY_BUF_LEN)
        delay_samples = DELAY_BUF_LEN - 1u;
    if (delay_samples == 0u)
        delay_samples = 1u;

    float mix = (float)effect->param[1] * 0.01f;
    float fb  = (float)effect->param[2] * 0.01f;
    if (fb > 0.95f)
        fb = 0.95f;

    uint16_t w = write_pos;
    for (uint16_t i = 0; i < size; i++)
    {
        uint16_t r = (uint16_t)((w + DELAY_BUF_LEN - delay_samples) % DELAY_BUF_LEN);
        float delayed = DelayEffect_Buffer[r];
        float dry = in[i];

        DelayEffect_Buffer[w] = dry + delayed * fb;
        out[i] = dry * (1.0f - mix) + delayed * mix;

        w = (uint16_t)((w + 1u) % DELAY_BUF_LEN);
    }
    write_pos = w;
}

effect_t* Get_Delay_t(void)
{
    return &Delay_t;
}
