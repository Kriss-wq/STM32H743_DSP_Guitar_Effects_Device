#include "Namtest.h"

#include <stddef.h>

#include "stdint.h"
#include "db.h"
#include "nam_model.h"
#include "model01_data.h"

/* NAMB: magic "NAMB" (LE: 42 4D 41 4E), then version, file_size,
 * weights_offset, num_weights. Floats start at weights_offset, not +32. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t file_size;
    uint32_t weights_offset;
    uint32_t num_weights;
} namb_header_t;

static void NAMTest_Effect_Init(effect_t* effect);
static void NAMTest_Effect_Setup(effect_t* effect,uint8_t Gain,uint8_t Tone,uint8_t Level);
static void NAMTest_Effect_Process(effect_t* effect,float *in, float *out, uint16_t size);
effect_t* Get_NAMTest_t(void);

effect_t NAMTest_t = {
    .name = "NAMTest",
    .param_name = {"Gain","Tone","Level"},
    .Init = NAMTest_Effect_Init,
    .Setup = NAMTest_Effect_Setup,
    .Process = NAMTest_Effect_Process
};
static nam_state_t nam_state;
static uint8_t nam_loaded;
int rc;
static void NAMTest_Effect_Init(effect_t* effect)
{
    nam_loaded = 0u;
    nam_init(&nam_state);

    const namb_header_t *hdr = (const namb_header_t *)model01_data;
    if (hdr->magic != 0x4E414D42u) /* 'NAMB' */
        return;
    if (hdr->weights_offset + hdr->num_weights * 4u > model01_size)
        return;

    const float *weights = (const float *)(model01_data + hdr->weights_offset);
    rc = nam_load_weights(weights, (int)hdr->num_weights);
    if (rc != 0)
        return;
    nam_loaded = 1u;
}

static void NAMTest_Effect_Setup(effect_t* effect,uint8_t Gain,uint8_t Tone,uint8_t Level)
{
    effect->param[0] = Gain;
    effect->param[1] = Tone;
    effect->param[2] = Level;
}

static void NAMTest_Effect_Process(effect_t* effect, float* in, float* out, uint16_t size)
{
    for (uint16_t i = 0; i < size; i++)
        out[i] = in[i];


    nam_process(&nam_state, &in, &out, (int)size);
    //db_reduce(out, out, size, DB_10);
}

effect_t* Get_NAMTest_t(void)
{
    return &NAMTest_t;
}