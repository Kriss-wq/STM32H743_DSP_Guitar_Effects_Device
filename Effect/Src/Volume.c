#include "Volume.h"
#include "stdint.h"

static void Volume_Effect_Init(effect_t* effect);
static void Volume_Effect_Setup(effect_t* effect, uint8_t Gain, uint8_t Tone, uint8_t Level);
static void Volume_Effect_Process(effect_t* effect, float *in, float *out, uint16_t size);

effect_t Volume_t = {
    .name = "Volume",
    .param_name = {"-", "-", "Level"},
    .Init = Volume_Effect_Init,
    .Setup = Volume_Effect_Setup,
    .Process = Volume_Effect_Process
};

static void Volume_Effect_Init(effect_t* effect)
{
    effect->param[0] = 0u;
    effect->param[1] = 0u;
    effect->param[2] = 100u; /* 100 = 0 dB（原样），不要默认乘整数增益 */
}

static void Volume_Effect_Setup(effect_t* effect, uint8_t Gain, uint8_t Tone, uint8_t Level)
{
    effect->param[0] = Gain;
    effect->param[1] = Tone;
    effect->param[2] = Level;
}

static void Volume_Effect_Process(effect_t* effect, float *in, float *out, uint16_t size)
{

}

effect_t* Get_Volume_t(void)
{
    return &Volume_t;
}
