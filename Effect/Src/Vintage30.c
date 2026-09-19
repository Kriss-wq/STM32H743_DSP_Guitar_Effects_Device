#include "stdint.h"
#include "stddef.h"
#include "arm_math.h"
#include "Vintage30.h"
#include "Cenzo_Celestion_V30_Mix_1024.h"
#include "db.h"
#include "effect.h"

#define V30_MAX_SLOTS      8
#define V30_FIR_STATE_LEN  (V30_IR_LEN + 64 - 1)

static void Vintage30_Effect_Init(effect_t* effect);
static void Vintage30_Effect_Setup(effect_t* effect, uint8_t Gain, uint8_t Tone, uint8_t Level);
static void Vintage30_Effect_Process(effect_t* effect, float *in, float *out, uint16_t size);
effect_t* Get_Vintage30_t(void);

effect_t Vintage30_t = {
    .name = "Vintage30",
    .param_name = {"Gain", "Tone", "Level"},
    .Init = Vintage30_Effect_Init,
    .Setup = Vintage30_Effect_Setup,
    .Process = Vintage30_Effect_Process
};


static float v30_fir_state[V30_MAX_SLOTS][V30_FIR_STATE_LEN]

__attribute__((aligned(32))) __attribute__((section(".ram")));
static arm_fir_instance_f32 v30_fir[V30_MAX_SLOTS];

static int v30_slot_index(effect_t *effect)
{
    effect_process_t *buf = effect_buffer_get();
    ptrdiff_t idx = effect - &buf->Effect[0];
    if (idx < 0 || idx >= V30_MAX_SLOTS)
        return 0;
    return (int)idx;
}

static void Vintage30_Effect_Init(effect_t* effect)
{
    int slot = v30_slot_index(effect);

    effect->param[0] = 50u;
    effect->param[1] = 50u;
    effect->param[2] = 50u;

    arm_fir_init_f32(&v30_fir[slot], V30_IR_LEN,
                     cenzo_v30_mix_1024, v30_fir_state[slot], 64);
}

static void Vintage30_Effect_Setup(effect_t* effect, uint8_t Gain, uint8_t Tone, uint8_t Level)
{
    effect->param[0] = Gain;
    effect->param[1] = Tone;
    effect->param[2] = Level;
}

static void Vintage30_Effect_Process(effect_t* effect, float *in, float *out, uint16_t size)
{
    int slot = v30_slot_index(effect);
    arm_fir_f32(&v30_fir[slot], in, out, size);
    db_reduce(out, out, size, DB_15);
}

effect_t* Get_Vintage30_t(void)
{
    return &Vintage30_t;
}
