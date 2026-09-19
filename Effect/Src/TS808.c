#include "TS808.h"
#include "effect.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/*
 * Tube Screamer clipping + tone, ported from
 *   https://github.com/JamesStubbsEng/TS-808-Ultra  (GPL-3.0)
 * WDF topology: Werner et al. diode clipper (3 cascaded stages).
 * Tone analog TF: Yeh, DAFX 2007 p.6; bilinear as in the plugin.
 *
 * Plugin extras (dry LPF/comp/mix/input gain) are omitted so the
 * three UI sliders stay Drive / Tone / Level.
 */

#define TS808_MAX_SLOTS  8
#define TS808_FS         48000.0f
#define TS808_OS         2
#define TS808_FS_OS      (TS808_FS * (float)TS808_OS)
#define TS808_R_DRIVE    500000.0f
#define TS808_R_TONE     20000.0f
#define TS808_R6         51000.0f
#define TS808_SMOOTH_S   0.05f

typedef struct
{
    float a, b, R, G;
} wdf_t;

typedef struct
{
    wdf_t n;
    float z;
} wdf_cap_t;

typedef struct
{
    /* stage A */
    wdf_t   rin, ra, r5;
    wdf_cap_t c2;
    wdf_t   s1, s2, s3;
    float   s1_p1ref, s2_p1ref, s3_p1ref;

    /* stage B */
    wdf_t   r4;
    wdf_cap_t c3;
    wdf_t   sb;
    float   sb_p1ref;

    /* stage C */
    wdf_t   isrc;
    wdf_cap_t c4;
    wdf_t   p1;
    float   p1_p1ref;
    float   p1_btemp;
    float   p1_bdiff;
    float   isrc_i;
    float   dp_a;
    float   diode_R_Is;
    float   diode_R_Is_overVt;
    float   diode_logR_Is_overVt;

    float   pot_r;
    float   pot_r_z;
    float   pot_r_inc;

    /* tone IIR */
    float   tone_b[3];
    float   tone_a[2]; /* a1, a2 (a0 = 1) */
    float   tone_z1, tone_z2;
    float   tone_rl;
    float   tone_rl_z;
    float   tone_rl_inc;

    float   level;
    uint8_t last_drive;
    uint8_t last_tone;
} ts808_t;

static void TS808_Effect_Init(effect_t *effect);
static void TS808_Effect_Setup(effect_t *effect, uint8_t Drive, uint8_t Tone, uint8_t Level);
static void TS808_Effect_Process(effect_t *effect, float *in, float *out, uint16_t size);

effect_t TS808_t = {
    .name = "TS808",
    .param_name = {"Drive", "Tone", "Level"},
    .Init = TS808_Effect_Init,
    .Setup = TS808_Effect_Setup,
    .Process = TS808_Effect_Process
};

static ts808_t ts808_st[TS808_MAX_SLOTS];

/* 1N914 pair @ 25 C, VR = 20 V (plugin ClipWDFc) */
static const float TS808_IS = 25e-9f;
static const float TS808_VT = 25.85e-3f;

static int ts808_slot_index(effect_t *effect)
{
    effect_process_t *buf = effect_buffer_get();
    ptrdiff_t idx = effect - &buf->Effect[0];
    if (idx < 0 || idx >= TS808_MAX_SLOTS)
        return 0;
    return (int)idx;
}

static inline void wdf_set_r(wdf_t *n, float r)
{
    n->R = r;
    n->G = 1.0f / r;
}

static inline void cap_prepare(wdf_cap_t *c, float cap, float fs)
{
    wdf_set_r(&c->n, 1.0f / (2.0f * cap * fs));
    c->z = 0.0f;
    c->n.a = 0.0f;
    c->n.b = 0.0f;
}

/* D'Angelo omega4 (MIT), same order the plugin uses via chowdsp */
static inline float ts808_omega3(float x)
{
    if (x < -3.341459552768620f)
        return 0.0f;
    if (x < 8.0f)
        return 0.6313183464296682f
             + x * (0.3631952663804445f
             + x * (0.04775931364975583f
             + x * (-1.314293149877800e-3f)));
    return x - logf(x);
}

static inline float ts808_omega4(float x)
{
    float y = ts808_omega3(x);
    return y - (y - expf(x - y)) / (y + 1.0f);
}

static inline float ts808_signum(float x)
{
    return (float)((x > 0.0f) - (x < 0.0f));
}

/* Drive 0..10 -> audio-taper 0..10 (plugin audioTaperPotSim) */
static float ts808_audio_taper(float in)
{
    if (in <= 5.0f)
        return in / 5.0f;
    return (9.0f / 5.0f) * in - 8.0f;
}

/* Tone 0..10 -> inverse-log 0..10 (plugin taperPotSim) */
static float ts808_invlog_taper(float in)
{
    if (in <= 5.0f)
        return (9.0f / 5.0f) * in;
    return (1.0f / 5.0f) * in + 8.0f;
}

static float ts808_jmap(float x, float in0, float in1, float out0, float out1)
{
    float t = (x - in0) / (in1 - in0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return out0 + t * (out1 - out0);
}

static void ts808_clipc_update_z(ts808_t *s)
{
    wdf_set_r(&s->isrc, TS808_R6 + s->pot_r_z);
    s->p1.G = s->isrc.G + s->c4.n.G;
    s->p1.R = 1.0f / s->p1.G;
    s->p1_p1ref = s->isrc.G / s->p1.G;

    s->diode_R_Is = s->p1.R * TS808_IS;
    s->diode_R_Is_overVt = s->diode_R_Is / TS808_VT;
    s->diode_logR_Is_overVt = logf(s->diode_R_Is_overVt);
}

static void ts808_tone_calc(ts808_t *s, float rL)
{
    const float Cs = 0.22e-6f;
    const float Rs = 1.0e3f;
    const float Ri = 10.0e3f;
    const float Cz = 0.22e-6f;
    const float Rz = 220.0f;
    const float Rf = 1.0e3f;
    const float rPot = TS808_R_TONE;
    const float fs = TS808_FS;

    const float wp = 1.0f / (Cs * Rs * Ri / (Rs + Ri));
    const float Rr = rPot - rL;
    const float rlrr = rL * Rr / (rL + Rr);
    const float wz = 1.0f / (Cz * (Rz + rlrr));
    const float Y = (rL + Rr) * (Rz + rlrr);
    const float X = (Rr / (rL + Rr)) / ((Rz + rlrr) * Y);
    const float W = Y / ((rL * Rf) + Y);
    const float alpha = (rL * Rf + Y) / (Y * Rs * Cs);

    const float bs0 = 0.0f;
    const float bs1 = alpha;
    const float bs2 = alpha * W * wz;
    const float as0 = 1.0f;
    const float as1 = wp + wz + X;
    const float as2 = wp * wz;

    float disc = as1 * as1 - 4.0f * as0 * as2;
    float wc = (disc < 0.0f) ? sqrtf(as2 / as0) : 0.0f;
    float K = (wc == 0.0f) ? (2.0f * fs) : (wc / tanf(wc / (2.0f * fs)));
    float K2 = K * K;

    float a0z = as0 * K2 + as1 * K + as2;
    s->tone_b[0] = (bs0 * K2 + bs1 * K + bs2) / a0z;
    s->tone_b[1] = (2.0f * bs2 - 2.0f * bs0 * K2) / a0z;
    s->tone_b[2] = (bs0 * K2 - bs1 * K + bs2) / a0z;
    s->tone_a[0] = (2.0f * as2 - 2.0f * as0 * K2) / a0z;
    s->tone_a[1] = (as0 * K2 - as1 * K + as2) / a0z;
}

static void ts808_prepare(ts808_t *s)
{
    memset(s, 0, sizeof(*s));

    wdf_set_r(&s->rin, 1.0f);
    wdf_set_r(&s->ra, 220.0f);
    wdf_set_r(&s->r5, 10000.0f);
    cap_prepare(&s->c2, 1.0e-6f, TS808_FS_OS);
    s->s1.R = s->ra.R + s->r5.R;
    s->s1.G = 1.0f / s->s1.R;
    s->s1_p1ref = s->ra.R / s->s1.R;
    s->s2.R = s->c2.n.R + s->s1.R;
    s->s2.G = 1.0f / s->s2.R;
    s->s2_p1ref = s->c2.n.R / s->s2.R;
    s->s3.R = s->rin.R + s->s2.R;
    s->s3.G = 1.0f / s->s3.R;
    s->s3_p1ref = s->rin.R / s->s3.R;

    wdf_set_r(&s->r4, 4700.0f);
    cap_prepare(&s->c3, 47.0e-9f, TS808_FS_OS);
    s->sb.R = s->c3.n.R + s->r4.R;
    s->sb.G = 1.0f / s->sb.R;
    s->sb_p1ref = s->c3.n.R / s->sb.R;

    cap_prepare(&s->c4, 51.0e-12f, TS808_FS_OS);
    s->pot_r = 10.0f;
    s->pot_r_z = 10.0f;
    ts808_clipc_update_z(s);

    s->tone_rl = 10.0f;
    s->tone_rl_z = 10.0f;
    ts808_tone_calc(s, s->tone_rl);
    s->level = 1.0f;
}

static inline float ts808_clip_a(ts808_t *s, float x)
{
    s->ra.b = 0.0f;
    s->r5.b = 0.0f;
    s->s1.b = 0.0f;
    s->c2.n.b = s->c2.z;
    s->s2.b = -(s->c2.n.b + s->s1.b);
    s->rin.b = 0.0f;
    s->s3.b = -(s->rin.b + s->s2.b);

    float vs_a = s->s3.b;
    float y = 0.5f * (s->r5.a + s->r5.b);
    float vs_b = -vs_a + 2.0f * x;

    float b_rin = s->rin.b - s->s3_p1ref * (vs_b + s->rin.b + s->s2.b);
    s->rin.a = b_rin;

    float x_s2 = -(vs_b + b_rin);
    float b_c2 = s->c2.n.b - s->s2_p1ref * (x_s2 + s->c2.n.b + s->s1.b);
    s->c2.n.a = b_c2;
    s->c2.z = b_c2;

    float x_s1 = -(x_s2 + b_c2);
    float b_ra = s->ra.b - s->s1_p1ref * (x_s1 + s->ra.b + s->r5.b);
    s->ra.a = b_ra;
    s->r5.a = -(x_s1 + b_ra);

    return y;
}

static inline float ts808_clip_b(ts808_t *s, float x)
{
    s->c3.n.b = s->c3.z;
    s->r4.b = 0.0f;
    s->sb.b = -(s->c3.n.b + s->r4.b);

    float vs_a = s->sb.b;
    float y = (s->r4.a - s->r4.b) * (0.5f * s->r4.G);
    float vs_b = -vs_a + 2.0f * x;

    float b_c3 = s->c3.n.b - s->sb_p1ref * (vs_b + s->c3.n.b + s->r4.b);
    s->c3.n.a = b_c3;
    s->c3.z = b_c3;
    s->r4.a = -(vs_b + b_c3);

    return y;
}

static inline float ts808_clip_c(ts808_t *s, float i_in)
{
    s->isrc_i = i_in;
    s->isrc.b = s->isrc.R * s->isrc_i;
    s->c4.n.b = s->c4.z;

    s->p1_bdiff = s->c4.n.b - s->isrc.b;
    s->p1_btemp = -s->p1_p1ref * s->p1_bdiff;
    s->p1.b = s->c4.n.b + s->p1_btemp;

    s->dp_a = s->p1.b;
    float lambda = ts808_signum(s->dp_a);
    float omega_in = s->diode_logR_Is_overVt
                   + lambda * s->dp_a / TS808_VT
                   + s->diode_R_Is_overVt;
    float dp_b = s->dp_a + 2.0f * lambda
               * (s->diode_R_Is - TS808_VT * ts808_omega4(omega_in));

    float y = 0.5f * (s->c4.n.a + s->c4.n.b);

    float b2 = dp_b + s->p1_btemp;
    s->isrc.a = s->p1_bdiff + b2;
    s->c4.n.a = b2;
    s->c4.z = b2;

    return y;
}

static inline float ts808_clip_sample(ts808_t *s, float x)
{
    float pot = s->pot_r_z;
    if (s->pot_r_inc != 0.0f)
    {
        pot += s->pot_r_inc;
        if ((s->pot_r_inc > 0.0f && pot >= s->pot_r) ||
            (s->pot_r_inc < 0.0f && pot <= s->pot_r))
        {
            pot = s->pot_r;
            s->pot_r_inc = 0.0f;
        }
        s->pot_r_z = pot;
        ts808_clipc_update_z(s);
    }

    float a = ts808_clip_a(s, x);
    float b = ts808_clip_b(s, a);
    return ts808_clip_c(s, b);
}

static inline float ts808_tone_sample(ts808_t *s, float x)
{
    float y = s->tone_b[0] * x + s->tone_z1;
    s->tone_z1 = s->tone_b[1] * x - s->tone_a[0] * y + s->tone_z2;
    s->tone_z2 = s->tone_b[2] * x - s->tone_a[1] * y;
    return y;
}

static void ts808_set_drive(ts808_t *s, float drive_0_10)
{
    float taper = ts808_audio_taper(drive_0_10);
    float target = ts808_jmap(taper, 0.0f, 10.0f, 10.0f, TS808_R_DRIVE);
    s->pot_r = target;
    float steps = TS808_FS_OS * TS808_SMOOTH_S;
    s->pot_r_inc = (target - s->pot_r_z) / steps;
}

static void ts808_set_tone(ts808_t *s, float tone_0_10)
{
    float taper = ts808_invlog_taper(tone_0_10);
    float target = ts808_jmap(taper, 0.0f, 10.0f, 10.0f, TS808_R_TONE);
    s->tone_rl = target;
    float steps = TS808_FS * TS808_SMOOTH_S;
    s->tone_rl_inc = (target - s->tone_rl_z) / steps;
}

static void TS808_Effect_Init(effect_t *effect)
{
    int slot = ts808_slot_index(effect);
    ts808_prepare(&ts808_st[slot]);

    effect->param[0] = 50u;
    effect->param[1] = 50u;
    effect->param[2] = 50u;
    TS808_Effect_Setup(effect, 50u, 50u, 50u);
}

static void TS808_Effect_Setup(effect_t *effect, uint8_t Drive, uint8_t Tone, uint8_t Level)
{
    int slot = ts808_slot_index(effect);
    ts808_t *s = &ts808_st[slot];

    effect->param[0] = Drive;
    effect->param[1] = Tone;
    effect->param[2] = Level;

    ts808_set_drive(s, 0.1f * (float)Drive);
    ts808_set_tone(s, 0.1f * (float)Tone);
    s->level = (float)Level / 50.0f;
    s->last_drive = Drive;
    s->last_tone = Tone;
}

static void TS808_Effect_Process(effect_t *effect, float *in, float *out, uint16_t size)
{
    int slot = ts808_slot_index(effect);
    ts808_t *s = &ts808_st[slot];

    if (effect->param[0] != s->last_drive)
    {
        ts808_set_drive(s, 0.1f * (float)effect->param[0]);
        s->last_drive = effect->param[0];
    }
    if (effect->param[1] != s->last_tone)
    {
        ts808_set_tone(s, 0.1f * (float)effect->param[1]);
        s->last_tone = effect->param[1];
    }
    s->level = (float)effect->param[2] / 50.0f;

    for (uint16_t i = 0; i < size; i++)
    {
        float x = in[i];
        /* 2x ZOH oversample, matching plugin clip rate = 2 * fs */
        float y0 = ts808_clip_sample(s, x);
        float y1 = ts808_clip_sample(s, x);
        float y = 0.5f * (y0 + y1);

        if (s->tone_rl_inc != 0.0f)
        {
            s->tone_rl_z += s->tone_rl_inc;
            if ((s->tone_rl_inc > 0.0f && s->tone_rl_z >= s->tone_rl) ||
                (s->tone_rl_inc < 0.0f && s->tone_rl_z <= s->tone_rl))
            {
                s->tone_rl_z = s->tone_rl;
                s->tone_rl_inc = 0.0f;
            }
            ts808_tone_calc(s, s->tone_rl_z);
        }

        out[i] = ts808_tone_sample(s, y) * s->level;
    }
}

effect_t *Get_TS808_t(void)
{
    return &TS808_t;
}
