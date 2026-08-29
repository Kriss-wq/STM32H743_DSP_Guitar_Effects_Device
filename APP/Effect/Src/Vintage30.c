#include "stdint.h"
#include "Test.h"
#include "arm_math.h"
#include "Vintage30.h"
#include "Cenzo_Celestion_V30_Mix_1024.h"

static void Vintage30_Effect_Init(effect_t* effect);
static void Vintage30_Effect_Setup(effect_t* effect,uint8_t Gain,uint8_t Tone,uint8_t Level);
static void Vintage30_Effect_Process(effect_t* effect,float *in, float *out, uint16_t size);
effect_t* Get_Vintage30_t(void);

effect_t Vintage30_t = {
    .name = "Vintage30",
    .param_name = {"Gain","Tone","Level"},
    .Init = Vintage30_Effect_Init,
    .Setup = Vintage30_Effect_Setup,
    .Process = Vintage30_Effect_Process
};
static float v30_fir_state[1024 + 64 - 1];
static arm_fir_instance_f32 v30_fir;   /* 结构体对象,不是指针! */

static void Vintage30_Effect_Init(effect_t* effect)
{
    effect->param[0] = 50u;
    effect->param[1] = 50u;
    effect->param[2] = 50u;
    /* 只初始化一次:1024 抽头,最大 blockSize 64(与 EFFECT_MAX_BLOCK 一致) */
    arm_fir_init_f32(&v30_fir, V30_IR_LEN, cenzo_v30_mix_1024, v30_fir_state, 64);
}

static void Vintage30_Effect_Setup(effect_t* effect,uint8_t Gain,uint8_t Tone,uint8_t Level)
{
    effect->param[0] = Gain;
    effect->param[1] = Tone;
    effect->param[2] = Level;
}


static void Vintage30_Effect_Process(effect_t* effect,float *in, float *out, uint16_t size)
{
    arm_fir_f32(&v30_fir, in, out, size);
}

effect_t* Get_Vintage30_t(void)
{
    return &Vintage30_t;
}