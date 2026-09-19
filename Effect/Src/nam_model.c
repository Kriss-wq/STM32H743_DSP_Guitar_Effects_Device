/* 由 nam2c.py 生成 — 请勿手动编辑 */
#include "nam_model.h"
#include <math.h>
#include <string.h>


/* --- SIMD 检测与辅助函数 --- */
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
  #define NAM_HAS_SSE 1
  #include <xmmintrin.h>
#endif
#if defined(__SSE3__) || defined(__AVX__)
  #define NAM_HAS_SSE3 1
  #include <pmmintrin.h>
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  #define NAM_HAS_NEON 1
  #include <arm_neon.h>
#endif

/*
 * 按列 GEMV：y[0..out_ch-1] = M * x[0..in_ch-1]
 * M 为列主序 (out_ch x in_ch)：第 i 列起始于 M[i * out_ch]。
 * 计算 y = sum_i( x[i] * M[:, i] ) — 每列在内存中连续。
 */
static inline void nam_gemv_set(float* restrict dst, const float* restrict mat,
                                const float* restrict vec, int out_ch, int in_ch)
{
    int o;
    /* 清零输出 */
#if NAM_HAS_NEON
    for (o = 0; o + 3 < out_ch; o += 4)
        vst1q_f32(dst + o, vdupq_n_f32(0.0f));
    for (; o < out_ch; o++)
        dst[o] = 0.0f;
    /* 累加：dst += vec[i] * col_i */
    for (int i = 0; i < in_ch; i++) {
        const float* col = mat + i * out_ch;
        float32x4_t vx = vdupq_n_f32(vec[i]);
        for (o = 0; o + 3 < out_ch; o += 4) {
            float32x4_t d = vld1q_f32(dst + o);
            float32x4_t c = vld1q_f32(col + o);
            d = vmlaq_f32(d, c, vx);
            vst1q_f32(dst + o, d);
        }
        for (; o < out_ch; o++)
            dst[o] += vec[i] * col[o];
    }
#elif NAM_HAS_SSE
    for (o = 0; o + 3 < out_ch; o += 4)
        _mm_storeu_ps(dst + o, _mm_setzero_ps());
    for (; o < out_ch; o++)
        dst[o] = 0.0f;
    for (int i = 0; i < in_ch; i++) {
        const float* col = mat + i * out_ch;
        __m128 vx = _mm_set1_ps(vec[i]);
        for (o = 0; o + 3 < out_ch; o += 4) {
            __m128 d = _mm_loadu_ps(dst + o);
            __m128 c = _mm_loadu_ps(col + o);
            d = _mm_add_ps(d, _mm_mul_ps(c, vx));
            _mm_storeu_ps(dst + o, d);
        }
        for (; o < out_ch; o++)
            dst[o] += vec[i] * col[o];
    }
#else
    for (o = 0; o < out_ch; o++)
        dst[o] = 0.0f;
    for (int i = 0; i < in_ch; i++) {
        const float* col = mat + i * out_ch;
        float xi = vec[i];
        for (o = 0; o < out_ch; o++)
            dst[o] += xi * col[o];
    }
#endif
}

/* y[0..out_ch-1] += M * x[0..in_ch-1] */
static inline void nam_gemv_add(float* restrict dst, const float* restrict mat,
                                const float* restrict vec, int out_ch, int in_ch)
{
    int o;
#if NAM_HAS_NEON
    for (int i = 0; i < in_ch; i++) {
        const float* col = mat + i * out_ch;
        float32x4_t vx = vdupq_n_f32(vec[i]);
        for (o = 0; o + 3 < out_ch; o += 4) {
            float32x4_t d = vld1q_f32(dst + o);
            float32x4_t c = vld1q_f32(col + o);
            d = vmlaq_f32(d, c, vx);
            vst1q_f32(dst + o, d);
        }
        for (; o < out_ch; o++)
            dst[o] += vec[i] * col[o];
    }
#elif NAM_HAS_SSE
    for (int i = 0; i < in_ch; i++) {
        const float* col = mat + i * out_ch;
        __m128 vx = _mm_set1_ps(vec[i]);
        for (o = 0; o + 3 < out_ch; o += 4) {
            __m128 d = _mm_loadu_ps(dst + o);
            __m128 c = _mm_loadu_ps(col + o);
            d = _mm_add_ps(d, _mm_mul_ps(c, vx));
            _mm_storeu_ps(dst + o, d);
        }
        for (; o < out_ch; o++)
            dst[o] += vec[i] * col[o];
    }
#else
    for (int i = 0; i < in_ch; i++) {
        const float* col = mat + i * out_ch;
        float xi = vec[i];
        for (o = 0; o < out_ch; o++)
            dst[o] += xi * col[o];
    }
#endif
}

/* 逐元素向量运算，用于 bias_add、depthwise、z = a + b */
static inline void nam_vec_add(float* restrict dst, const float* restrict src, int n)
{
#if NAM_HAS_NEON
    int i = 0;
    for (; i + 3 < n; i += 4) {
        float32x4_t d = vld1q_f32(dst + i);
        float32x4_t s = vld1q_f32(src + i);
        vst1q_f32(dst + i, vaddq_f32(d, s));
    }
    for (; i < n; i++)
        dst[i] += src[i];
#elif NAM_HAS_SSE
    int i = 0;
    for (; i + 3 < n; i += 4) {
        __m128 d = _mm_loadu_ps(dst + i);
        __m128 s = _mm_loadu_ps(src + i);
        _mm_storeu_ps(dst + i, _mm_add_ps(d, s));
    }
    for (; i < n; i++)
        dst[i] += src[i];
#else
    for (int i = 0; i < n; i++)
        dst[i] += src[i];
#endif
}


#ifdef NAM_PROFILE_ACTIVATIONS
#define NAM_PROFILE_NUM_LAYERS 23
static float _nam_act_peak[23] = {0};
static float _nam_act_peak_global = 0.0f;
float nam_get_act_peak(void) { return _nam_act_peak_global; }
int nam_get_num_layers(void) { return 23; }
float nam_get_layer_peak(int layer) { return _nam_act_peak[layer]; }
static inline void _nam_track_peak(int layer, float v) {
    float a = fabsf(v);
    if (a > _nam_act_peak[layer]) _nam_act_peak[layer] = a;
    if (a > _nam_act_peak_global) _nam_act_peak_global = a;
}
#endif

/* 激活函数（快速多项式近似） */
static inline float nam_leaky_relu(float x) { return x > 0.0f ? x : 0.01f * x; }

/* ===== SRAM 环形缓冲 ===== */
/* 所有延迟线环形缓冲放在 AXI SRAM（.ram）。权重 / 临时区仍放在 DTCM。 */

static float NAM_SRAM la0_l0_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l1_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l2_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l3_conv_rb[3 * NAM_RB_FRAMES_256];
static float NAM_SRAM la0_l4_conv_rb[3 * NAM_RB_FRAMES_512];
static float NAM_SRAM la0_l5_conv_rb[3 * NAM_RB_FRAMES_1024];
static float NAM_SRAM la0_l6_conv_rb[3 * NAM_RB_FRAMES_2048];
static float NAM_SRAM la0_l7_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l8_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l9_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l10_conv_rb[3 * NAM_RB_FRAMES_256];
static float NAM_SRAM la0_l11_conv_rb[3 * NAM_RB_FRAMES_512];
static float NAM_SRAM la0_l12_conv_rb[3 * NAM_RB_FRAMES_1024];
static float NAM_SRAM la0_l13_conv_rb[3 * NAM_RB_FRAMES_2048];
static float NAM_SRAM la0_l14_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l15_conv_rb[3 * NAM_RB_FRAMES_256];
static float NAM_SRAM la0_l16_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l17_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l18_conv_rb[3 * NAM_RB_FRAMES_128];
static float NAM_SRAM la0_l19_conv_rb[3 * NAM_RB_FRAMES_256];
static float NAM_SRAM la0_l20_conv_rb[3 * NAM_RB_FRAMES_512];
static float NAM_SRAM la0_l21_conv_rb[3 * NAM_RB_FRAMES_1024];
static float NAM_SRAM la0_l22_conv_rb[3 * NAM_RB_FRAMES_2048];
static float NAM_SRAM la0_head_rechannel_rb[3 * NAM_RB_FRAMES_128];

/* ===== 模型权重 ===== */

static float NAM_DTCM head_scale;

static float NAM_DTCM la0_rechannel_w[3];

static float NAM_DTCM la0_l0_conv_w0[9];
static float NAM_DTCM la0_l0_conv_w1[9];
static float NAM_DTCM la0_l0_conv_w2[9];
static float NAM_DTCM la0_l0_conv_w3[9];
static float NAM_DTCM la0_l0_conv_w4[9];
static float NAM_DTCM la0_l0_conv_w5[9];
static float NAM_DTCM la0_l0_conv_b[3];

static float NAM_DTCM la0_l0_mixin_w[3];

static float NAM_DTCM la0_l0_layer1x1_w[9];
static float NAM_DTCM la0_l0_layer1x1_b[3];

static float NAM_DTCM la0_l1_conv_w0[9];
static float NAM_DTCM la0_l1_conv_w1[9];
static float NAM_DTCM la0_l1_conv_w2[9];
static float NAM_DTCM la0_l1_conv_w3[9];
static float NAM_DTCM la0_l1_conv_w4[9];
static float NAM_DTCM la0_l1_conv_w5[9];
static float NAM_DTCM la0_l1_conv_b[3];

static float NAM_DTCM la0_l1_mixin_w[3];

static float NAM_DTCM la0_l1_layer1x1_w[9];
static float NAM_DTCM la0_l1_layer1x1_b[3];

static float NAM_DTCM la0_l2_conv_w0[9];
static float NAM_DTCM la0_l2_conv_w1[9];
static float NAM_DTCM la0_l2_conv_w2[9];
static float NAM_DTCM la0_l2_conv_w3[9];
static float NAM_DTCM la0_l2_conv_w4[9];
static float NAM_DTCM la0_l2_conv_w5[9];
static float NAM_DTCM la0_l2_conv_b[3];

static float NAM_DTCM la0_l2_mixin_w[3];

static float NAM_DTCM la0_l2_layer1x1_w[9];
static float NAM_DTCM la0_l2_layer1x1_b[3];

static float NAM_DTCM la0_l3_conv_w0[9];
static float NAM_DTCM la0_l3_conv_w1[9];
static float NAM_DTCM la0_l3_conv_w2[9];
static float NAM_DTCM la0_l3_conv_w3[9];
static float NAM_DTCM la0_l3_conv_w4[9];
static float NAM_DTCM la0_l3_conv_w5[9];
static float NAM_DTCM la0_l3_conv_b[3];

static float NAM_DTCM la0_l3_mixin_w[3];

static float NAM_DTCM la0_l3_layer1x1_w[9];
static float NAM_DTCM la0_l3_layer1x1_b[3];

static float NAM_DTCM la0_l4_conv_w0[9];
static float NAM_DTCM la0_l4_conv_w1[9];
static float NAM_DTCM la0_l4_conv_w2[9];
static float NAM_DTCM la0_l4_conv_w3[9];
static float NAM_DTCM la0_l4_conv_w4[9];
static float NAM_DTCM la0_l4_conv_w5[9];
static float NAM_DTCM la0_l4_conv_b[3];

static float NAM_DTCM la0_l4_mixin_w[3];

static float NAM_DTCM la0_l4_layer1x1_w[9];
static float NAM_DTCM la0_l4_layer1x1_b[3];

static float NAM_DTCM la0_l5_conv_w0[9];
static float NAM_DTCM la0_l5_conv_w1[9];
static float NAM_DTCM la0_l5_conv_w2[9];
static float NAM_DTCM la0_l5_conv_w3[9];
static float NAM_DTCM la0_l5_conv_w4[9];
static float NAM_DTCM la0_l5_conv_w5[9];
static float NAM_DTCM la0_l5_conv_b[3];

static float NAM_DTCM la0_l5_mixin_w[3];

static float NAM_DTCM la0_l5_layer1x1_w[9];
static float NAM_DTCM la0_l5_layer1x1_b[3];

static float NAM_DTCM la0_l6_conv_w0[9];
static float NAM_DTCM la0_l6_conv_w1[9];
static float NAM_DTCM la0_l6_conv_w2[9];
static float NAM_DTCM la0_l6_conv_w3[9];
static float NAM_DTCM la0_l6_conv_w4[9];
static float NAM_DTCM la0_l6_conv_w5[9];
static float NAM_DTCM la0_l6_conv_b[3];

static float NAM_DTCM la0_l6_mixin_w[3];

static float NAM_DTCM la0_l6_layer1x1_w[9];
static float NAM_DTCM la0_l6_layer1x1_b[3];

static float NAM_DTCM la0_l7_conv_w0[9];
static float NAM_DTCM la0_l7_conv_w1[9];
static float NAM_DTCM la0_l7_conv_w2[9];
static float NAM_DTCM la0_l7_conv_w3[9];
static float NAM_DTCM la0_l7_conv_w4[9];
static float NAM_DTCM la0_l7_conv_w5[9];
static float NAM_DTCM la0_l7_conv_b[3];

static float NAM_DTCM la0_l7_mixin_w[3];

static float NAM_DTCM la0_l7_layer1x1_w[9];
static float NAM_DTCM la0_l7_layer1x1_b[3];

static float NAM_DTCM la0_l8_conv_w0[9];
static float NAM_DTCM la0_l8_conv_w1[9];
static float NAM_DTCM la0_l8_conv_w2[9];
static float NAM_DTCM la0_l8_conv_w3[9];
static float NAM_DTCM la0_l8_conv_w4[9];
static float NAM_DTCM la0_l8_conv_w5[9];
static float NAM_DTCM la0_l8_conv_b[3];

static float NAM_DTCM la0_l8_mixin_w[3];

static float NAM_DTCM la0_l8_layer1x1_w[9];
static float NAM_DTCM la0_l8_layer1x1_b[3];

static float NAM_DTCM la0_l9_conv_w0[9];
static float NAM_DTCM la0_l9_conv_w1[9];
static float NAM_DTCM la0_l9_conv_w2[9];
static float NAM_DTCM la0_l9_conv_w3[9];
static float NAM_DTCM la0_l9_conv_w4[9];
static float NAM_DTCM la0_l9_conv_w5[9];
static float NAM_DTCM la0_l9_conv_b[3];

static float NAM_DTCM la0_l9_mixin_w[3];

static float NAM_DTCM la0_l9_layer1x1_w[9];
static float NAM_DTCM la0_l9_layer1x1_b[3];

static float NAM_DTCM la0_l10_conv_w0[9];
static float NAM_DTCM la0_l10_conv_w1[9];
static float NAM_DTCM la0_l10_conv_w2[9];
static float NAM_DTCM la0_l10_conv_w3[9];
static float NAM_DTCM la0_l10_conv_w4[9];
static float NAM_DTCM la0_l10_conv_w5[9];
static float NAM_DTCM la0_l10_conv_b[3];

static float NAM_DTCM la0_l10_mixin_w[3];

static float NAM_DTCM la0_l10_layer1x1_w[9];
static float NAM_DTCM la0_l10_layer1x1_b[3];

static float NAM_DTCM la0_l11_conv_w0[9];
static float NAM_DTCM la0_l11_conv_w1[9];
static float NAM_DTCM la0_l11_conv_w2[9];
static float NAM_DTCM la0_l11_conv_w3[9];
static float NAM_DTCM la0_l11_conv_w4[9];
static float NAM_DTCM la0_l11_conv_w5[9];
static float NAM_DTCM la0_l11_conv_b[3];

static float NAM_DTCM la0_l11_mixin_w[3];

static float NAM_DTCM la0_l11_layer1x1_w[9];
static float NAM_DTCM la0_l11_layer1x1_b[3];

static float NAM_DTCM la0_l12_conv_w0[9];
static float NAM_DTCM la0_l12_conv_w1[9];
static float NAM_DTCM la0_l12_conv_w2[9];
static float NAM_DTCM la0_l12_conv_w3[9];
static float NAM_DTCM la0_l12_conv_w4[9];
static float NAM_DTCM la0_l12_conv_w5[9];
static float NAM_DTCM la0_l12_conv_b[3];

static float NAM_DTCM la0_l12_mixin_w[3];

static float NAM_DTCM la0_l12_layer1x1_w[9];
static float NAM_DTCM la0_l12_layer1x1_b[3];

static float NAM_DTCM la0_l13_conv_w0[9];
static float NAM_DTCM la0_l13_conv_w1[9];
static float NAM_DTCM la0_l13_conv_w2[9];
static float NAM_DTCM la0_l13_conv_w3[9];
static float NAM_DTCM la0_l13_conv_w4[9];
static float NAM_DTCM la0_l13_conv_w5[9];
static float NAM_DTCM la0_l13_conv_b[3];

static float NAM_DTCM la0_l13_mixin_w[3];

static float NAM_DTCM la0_l13_layer1x1_w[9];
static float NAM_DTCM la0_l13_layer1x1_b[3];

static float NAM_DTCM la0_l14_conv_w0[9];
static float NAM_DTCM la0_l14_conv_w1[9];
static float NAM_DTCM la0_l14_conv_w2[9];
static float NAM_DTCM la0_l14_conv_w3[9];
static float NAM_DTCM la0_l14_conv_w4[9];
static float NAM_DTCM la0_l14_conv_w5[9];
static float NAM_DTCM la0_l14_conv_w6[9];
static float NAM_DTCM la0_l14_conv_w7[9];
static float NAM_DTCM la0_l14_conv_w8[9];
static float NAM_DTCM la0_l14_conv_w9[9];
static float NAM_DTCM la0_l14_conv_w10[9];
static float NAM_DTCM la0_l14_conv_w11[9];
static float NAM_DTCM la0_l14_conv_w12[9];
static float NAM_DTCM la0_l14_conv_w13[9];
static float NAM_DTCM la0_l14_conv_w14[9];
static float NAM_DTCM la0_l14_conv_b[3];

static float NAM_DTCM la0_l14_mixin_w[3];

static float NAM_DTCM la0_l14_layer1x1_w[9];
static float NAM_DTCM la0_l14_layer1x1_b[3];

static float NAM_DTCM la0_l15_conv_w0[9];
static float NAM_DTCM la0_l15_conv_w1[9];
static float NAM_DTCM la0_l15_conv_w2[9];
static float NAM_DTCM la0_l15_conv_w3[9];
static float NAM_DTCM la0_l15_conv_w4[9];
static float NAM_DTCM la0_l15_conv_w5[9];
static float NAM_DTCM la0_l15_conv_w6[9];
static float NAM_DTCM la0_l15_conv_w7[9];
static float NAM_DTCM la0_l15_conv_w8[9];
static float NAM_DTCM la0_l15_conv_w9[9];
static float NAM_DTCM la0_l15_conv_w10[9];
static float NAM_DTCM la0_l15_conv_w11[9];
static float NAM_DTCM la0_l15_conv_w12[9];
static float NAM_DTCM la0_l15_conv_w13[9];
static float NAM_DTCM la0_l15_conv_w14[9];
static float NAM_DTCM la0_l15_conv_b[3];

static float NAM_DTCM la0_l15_mixin_w[3];

static float NAM_DTCM la0_l15_layer1x1_w[9];
static float NAM_DTCM la0_l15_layer1x1_b[3];

static float NAM_DTCM la0_l16_conv_w0[9];
static float NAM_DTCM la0_l16_conv_w1[9];
static float NAM_DTCM la0_l16_conv_w2[9];
static float NAM_DTCM la0_l16_conv_w3[9];
static float NAM_DTCM la0_l16_conv_w4[9];
static float NAM_DTCM la0_l16_conv_w5[9];
static float NAM_DTCM la0_l16_conv_b[3];

static float NAM_DTCM la0_l16_mixin_w[3];

static float NAM_DTCM la0_l16_layer1x1_w[9];
static float NAM_DTCM la0_l16_layer1x1_b[3];

static float NAM_DTCM la0_l17_conv_w0[9];
static float NAM_DTCM la0_l17_conv_w1[9];
static float NAM_DTCM la0_l17_conv_w2[9];
static float NAM_DTCM la0_l17_conv_w3[9];
static float NAM_DTCM la0_l17_conv_w4[9];
static float NAM_DTCM la0_l17_conv_w5[9];
static float NAM_DTCM la0_l17_conv_b[3];

static float NAM_DTCM la0_l17_mixin_w[3];

static float NAM_DTCM la0_l17_layer1x1_w[9];
static float NAM_DTCM la0_l17_layer1x1_b[3];

static float NAM_DTCM la0_l18_conv_w0[9];
static float NAM_DTCM la0_l18_conv_w1[9];
static float NAM_DTCM la0_l18_conv_w2[9];
static float NAM_DTCM la0_l18_conv_w3[9];
static float NAM_DTCM la0_l18_conv_w4[9];
static float NAM_DTCM la0_l18_conv_w5[9];
static float NAM_DTCM la0_l18_conv_b[3];

static float NAM_DTCM la0_l18_mixin_w[3];

static float NAM_DTCM la0_l18_layer1x1_w[9];
static float NAM_DTCM la0_l18_layer1x1_b[3];

static float NAM_DTCM la0_l19_conv_w0[9];
static float NAM_DTCM la0_l19_conv_w1[9];
static float NAM_DTCM la0_l19_conv_w2[9];
static float NAM_DTCM la0_l19_conv_w3[9];
static float NAM_DTCM la0_l19_conv_w4[9];
static float NAM_DTCM la0_l19_conv_w5[9];
static float NAM_DTCM la0_l19_conv_b[3];

static float NAM_DTCM la0_l19_mixin_w[3];

static float NAM_DTCM la0_l19_layer1x1_w[9];
static float NAM_DTCM la0_l19_layer1x1_b[3];

static float NAM_DTCM la0_l20_conv_w0[9];
static float NAM_DTCM la0_l20_conv_w1[9];
static float NAM_DTCM la0_l20_conv_w2[9];
static float NAM_DTCM la0_l20_conv_w3[9];
static float NAM_DTCM la0_l20_conv_w4[9];
static float NAM_DTCM la0_l20_conv_w5[9];
static float NAM_DTCM la0_l20_conv_b[3];

static float NAM_DTCM la0_l20_mixin_w[3];

static float NAM_DTCM la0_l20_layer1x1_w[9];
static float NAM_DTCM la0_l20_layer1x1_b[3];

static float NAM_DTCM la0_l21_conv_w0[9];
static float NAM_DTCM la0_l21_conv_w1[9];
static float NAM_DTCM la0_l21_conv_w2[9];
static float NAM_DTCM la0_l21_conv_w3[9];
static float NAM_DTCM la0_l21_conv_w4[9];
static float NAM_DTCM la0_l21_conv_w5[9];
static float NAM_DTCM la0_l21_conv_b[3];

static float NAM_DTCM la0_l21_mixin_w[3];

static float NAM_DTCM la0_l21_layer1x1_w[9];
static float NAM_DTCM la0_l21_layer1x1_b[3];

static float NAM_DTCM la0_l22_conv_w0[9];
static float NAM_DTCM la0_l22_conv_w1[9];
static float NAM_DTCM la0_l22_conv_w2[9];
static float NAM_DTCM la0_l22_conv_w3[9];
static float NAM_DTCM la0_l22_conv_w4[9];
static float NAM_DTCM la0_l22_conv_w5[9];
static float NAM_DTCM la0_l22_conv_b[3];

static float NAM_DTCM la0_l22_mixin_w[3];

static float NAM_DTCM la0_l22_layer1x1_w[9];
static float NAM_DTCM la0_l22_layer1x1_b[3];

static float NAM_DTCM la0_head_rechannel_w0[3];
static float NAM_DTCM la0_head_rechannel_w1[3];
static float NAM_DTCM la0_head_rechannel_w2[3];
static float NAM_DTCM la0_head_rechannel_w3[3];
static float NAM_DTCM la0_head_rechannel_w4[3];
static float NAM_DTCM la0_head_rechannel_w5[3];
static float NAM_DTCM la0_head_rechannel_w6[3];
static float NAM_DTCM la0_head_rechannel_w7[3];
static float NAM_DTCM la0_head_rechannel_w8[3];
static float NAM_DTCM la0_head_rechannel_w9[3];
static float NAM_DTCM la0_head_rechannel_w10[3];
static float NAM_DTCM la0_head_rechannel_w11[3];
static float NAM_DTCM la0_head_rechannel_w12[3];
static float NAM_DTCM la0_head_rechannel_w13[3];
static float NAM_DTCM la0_head_rechannel_w14[3];
static float NAM_DTCM la0_head_rechannel_w15[3];
static float NAM_DTCM la0_head_rechannel_b[1];


/* 环形缓冲辅助函数 — 长度为 2 的幂，无需 memmove */

/* 将 num_frames 帧交织通道数据写入环形缓冲。
 * 不跨边界时一次 memcpy；写跨越绕回点时拆成两次 memcpy。 */
static inline void rb_write_circular(float* buf, int wpos, const float* data, int channels,
                                     int num_frames, int mask)
{
    int buf_frames = mask + 1;
    int start = wpos & mask;
    int tail = buf_frames - start; /* 到绕回边界前剩余帧数 */
    size_t stride = (size_t)channels * sizeof(float);
    if (num_frames <= tail) {
        memcpy(&buf[start * channels], data, (size_t)num_frames * stride);
    } else {
        memcpy(&buf[start * channels], data, (size_t)tail * stride);
        memcpy(buf, &data[tail * channels], (size_t)(num_frames - tail) * stride);
    }
}




void nam_init(nam_state_t* state) {
    nam_reset(state);
}

void nam_reset(nam_state_t* state) {
    memset(state, 0, sizeof(nam_state_t));
    memset(la0_l0_conv_rb, 0, sizeof(la0_l0_conv_rb));
    memset(la0_l1_conv_rb, 0, sizeof(la0_l1_conv_rb));
    memset(la0_l2_conv_rb, 0, sizeof(la0_l2_conv_rb));
    memset(la0_l3_conv_rb, 0, sizeof(la0_l3_conv_rb));
    memset(la0_l4_conv_rb, 0, sizeof(la0_l4_conv_rb));
    memset(la0_l5_conv_rb, 0, sizeof(la0_l5_conv_rb));
    memset(la0_l6_conv_rb, 0, sizeof(la0_l6_conv_rb));
    memset(la0_l7_conv_rb, 0, sizeof(la0_l7_conv_rb));
    memset(la0_l8_conv_rb, 0, sizeof(la0_l8_conv_rb));
    memset(la0_l9_conv_rb, 0, sizeof(la0_l9_conv_rb));
    memset(la0_l10_conv_rb, 0, sizeof(la0_l10_conv_rb));
    memset(la0_l11_conv_rb, 0, sizeof(la0_l11_conv_rb));
    memset(la0_l12_conv_rb, 0, sizeof(la0_l12_conv_rb));
    memset(la0_l13_conv_rb, 0, sizeof(la0_l13_conv_rb));
    memset(la0_l14_conv_rb, 0, sizeof(la0_l14_conv_rb));
    memset(la0_l15_conv_rb, 0, sizeof(la0_l15_conv_rb));
    memset(la0_l16_conv_rb, 0, sizeof(la0_l16_conv_rb));
    memset(la0_l17_conv_rb, 0, sizeof(la0_l17_conv_rb));
    memset(la0_l18_conv_rb, 0, sizeof(la0_l18_conv_rb));
    memset(la0_l19_conv_rb, 0, sizeof(la0_l19_conv_rb));
    memset(la0_l20_conv_rb, 0, sizeof(la0_l20_conv_rb));
    memset(la0_l21_conv_rb, 0, sizeof(la0_l21_conv_rb));
    memset(la0_l22_conv_rb, 0, sizeof(la0_l22_conv_rb));
    memset(la0_head_rechannel_rb, 0, sizeof(la0_head_rechannel_rb));
    /* 环形缓冲：各层 wpos 错开，避免同尺寸层在同一音频块同时绕回，分散 D-Cache 压力。
       缓冲初始清零，配合掩码绕回，可为起始 lookback 提供零填充。 */
    state->la0_l13_conv_rb_wpos = 682;  /* 错开：p2=2048 的 1/3 */
    state->la0_l22_conv_rb_wpos = 1365;  /* 错开：p2=2048 的 2/3 */
}

/* Scratch [nf][3] 累加器，供 la0 中 tap-major 分解的层函数使用。 */
static float NAM_DTCM la0_tapmajor_tmp[NAM_MAX_BUFFER_SIZE * 3];

/* la0 各层的权重指针 */
typedef struct {
    const float* conv_w[6];
    const float* conv_b;
    const float* mixin_w;
    const float* l1x1_w;
    const float* l1x1_b;
} nam_la0_k6_weights_t;

static const nam_la0_k6_weights_t la0_l0_weights = {
    { la0_l0_conv_w0, la0_l0_conv_w1, la0_l0_conv_w2, la0_l0_conv_w3, la0_l0_conv_w4, la0_l0_conv_w5 },
    la0_l0_conv_b,
    la0_l0_mixin_w,
    la0_l0_layer1x1_w,
    la0_l0_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l1_weights = {
    { la0_l1_conv_w0, la0_l1_conv_w1, la0_l1_conv_w2, la0_l1_conv_w3, la0_l1_conv_w4, la0_l1_conv_w5 },
    la0_l1_conv_b,
    la0_l1_mixin_w,
    la0_l1_layer1x1_w,
    la0_l1_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l2_weights = {
    { la0_l2_conv_w0, la0_l2_conv_w1, la0_l2_conv_w2, la0_l2_conv_w3, la0_l2_conv_w4, la0_l2_conv_w5 },
    la0_l2_conv_b,
    la0_l2_mixin_w,
    la0_l2_layer1x1_w,
    la0_l2_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l3_weights = {
    { la0_l3_conv_w0, la0_l3_conv_w1, la0_l3_conv_w2, la0_l3_conv_w3, la0_l3_conv_w4, la0_l3_conv_w5 },
    la0_l3_conv_b,
    la0_l3_mixin_w,
    la0_l3_layer1x1_w,
    la0_l3_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l4_weights = {
    { la0_l4_conv_w0, la0_l4_conv_w1, la0_l4_conv_w2, la0_l4_conv_w3, la0_l4_conv_w4, la0_l4_conv_w5 },
    la0_l4_conv_b,
    la0_l4_mixin_w,
    la0_l4_layer1x1_w,
    la0_l4_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l5_weights = {
    { la0_l5_conv_w0, la0_l5_conv_w1, la0_l5_conv_w2, la0_l5_conv_w3, la0_l5_conv_w4, la0_l5_conv_w5 },
    la0_l5_conv_b,
    la0_l5_mixin_w,
    la0_l5_layer1x1_w,
    la0_l5_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l6_weights = {
    { la0_l6_conv_w0, la0_l6_conv_w1, la0_l6_conv_w2, la0_l6_conv_w3, la0_l6_conv_w4, la0_l6_conv_w5 },
    la0_l6_conv_b,
    la0_l6_mixin_w,
    la0_l6_layer1x1_w,
    la0_l6_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l7_weights = {
    { la0_l7_conv_w0, la0_l7_conv_w1, la0_l7_conv_w2, la0_l7_conv_w3, la0_l7_conv_w4, la0_l7_conv_w5 },
    la0_l7_conv_b,
    la0_l7_mixin_w,
    la0_l7_layer1x1_w,
    la0_l7_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l8_weights = {
    { la0_l8_conv_w0, la0_l8_conv_w1, la0_l8_conv_w2, la0_l8_conv_w3, la0_l8_conv_w4, la0_l8_conv_w5 },
    la0_l8_conv_b,
    la0_l8_mixin_w,
    la0_l8_layer1x1_w,
    la0_l8_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l9_weights = {
    { la0_l9_conv_w0, la0_l9_conv_w1, la0_l9_conv_w2, la0_l9_conv_w3, la0_l9_conv_w4, la0_l9_conv_w5 },
    la0_l9_conv_b,
    la0_l9_mixin_w,
    la0_l9_layer1x1_w,
    la0_l9_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l10_weights = {
    { la0_l10_conv_w0, la0_l10_conv_w1, la0_l10_conv_w2, la0_l10_conv_w3, la0_l10_conv_w4, la0_l10_conv_w5 },
    la0_l10_conv_b,
    la0_l10_mixin_w,
    la0_l10_layer1x1_w,
    la0_l10_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l11_weights = {
    { la0_l11_conv_w0, la0_l11_conv_w1, la0_l11_conv_w2, la0_l11_conv_w3, la0_l11_conv_w4, la0_l11_conv_w5 },
    la0_l11_conv_b,
    la0_l11_mixin_w,
    la0_l11_layer1x1_w,
    la0_l11_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l12_weights = {
    { la0_l12_conv_w0, la0_l12_conv_w1, la0_l12_conv_w2, la0_l12_conv_w3, la0_l12_conv_w4, la0_l12_conv_w5 },
    la0_l12_conv_b,
    la0_l12_mixin_w,
    la0_l12_layer1x1_w,
    la0_l12_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l13_weights = {
    { la0_l13_conv_w0, la0_l13_conv_w1, la0_l13_conv_w2, la0_l13_conv_w3, la0_l13_conv_w4, la0_l13_conv_w5 },
    la0_l13_conv_b,
    la0_l13_mixin_w,
    la0_l13_layer1x1_w,
    la0_l13_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l16_weights = {
    { la0_l16_conv_w0, la0_l16_conv_w1, la0_l16_conv_w2, la0_l16_conv_w3, la0_l16_conv_w4, la0_l16_conv_w5 },
    la0_l16_conv_b,
    la0_l16_mixin_w,
    la0_l16_layer1x1_w,
    la0_l16_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l17_weights = {
    { la0_l17_conv_w0, la0_l17_conv_w1, la0_l17_conv_w2, la0_l17_conv_w3, la0_l17_conv_w4, la0_l17_conv_w5 },
    la0_l17_conv_b,
    la0_l17_mixin_w,
    la0_l17_layer1x1_w,
    la0_l17_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l18_weights = {
    { la0_l18_conv_w0, la0_l18_conv_w1, la0_l18_conv_w2, la0_l18_conv_w3, la0_l18_conv_w4, la0_l18_conv_w5 },
    la0_l18_conv_b,
    la0_l18_mixin_w,
    la0_l18_layer1x1_w,
    la0_l18_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l19_weights = {
    { la0_l19_conv_w0, la0_l19_conv_w1, la0_l19_conv_w2, la0_l19_conv_w3, la0_l19_conv_w4, la0_l19_conv_w5 },
    la0_l19_conv_b,
    la0_l19_mixin_w,
    la0_l19_layer1x1_w,
    la0_l19_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l20_weights = {
    { la0_l20_conv_w0, la0_l20_conv_w1, la0_l20_conv_w2, la0_l20_conv_w3, la0_l20_conv_w4, la0_l20_conv_w5 },
    la0_l20_conv_b,
    la0_l20_mixin_w,
    la0_l20_layer1x1_w,
    la0_l20_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l21_weights = {
    { la0_l21_conv_w0, la0_l21_conv_w1, la0_l21_conv_w2, la0_l21_conv_w3, la0_l21_conv_w4, la0_l21_conv_w5 },
    la0_l21_conv_b,
    la0_l21_mixin_w,
    la0_l21_layer1x1_w,
    la0_l21_layer1x1_b,
};
static const nam_la0_k6_weights_t la0_l22_weights = {
    { la0_l22_conv_w0, la0_l22_conv_w1, la0_l22_conv_w2, la0_l22_conv_w3, la0_l22_conv_w4, la0_l22_conv_w5 },
    la0_l22_conv_b,
    la0_l22_mixin_w,
    la0_l22_layer1x1_w,
    la0_l22_layer1x1_b,
};

/* Tap-major：每个 tap 的 9 个权重每次调用只加载一次，而不是每帧加载。 */
__attribute__((noinline))
static void nam_process_layer_la0_k6(
    float* __restrict layer_buf,
    float* __restrict head_accum,
    const float* __restrict condition,
    float* __restrict ring_buf,
    int* __restrict wpos_ptr,
    const nam_la0_k6_weights_t* __restrict w,
#ifdef NAM_PROFILE_ACTIVATIONS
    int _profile_layer,
#endif
    int dilation, int nf, int rb_mask)
{
    /* 写入环形缓冲 */
    rb_write_circular(ring_buf, *wpos_ptr, layer_buf, 3, nf, rb_mask);
    const int wpos = *wpos_ptr;
    *wpos_ptr = (wpos + nf) & rb_mask;

    const float* const cb = w->conv_b;
    const float* const mw = w->mixin_w;
    const float* const lw = w->l1x1_w;
    const float* const lb = w->l1x1_b;

    float* const tmp = la0_tapmajor_tmp;

    /* Tap 0（d = dilation * 5）：用 conv_b 初始化 tmp */
    {
        const float* const wk = w->conv_w[0];
        const int d = dilation * 5;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        const float cb0 = cb[0];
        const float cb1 = cb[1];
        const float cb2 = cb[2];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], cb0);
            float a1 = fmaf(w1, _t[0], cb1);
            float a2 = fmaf(w2, _t[0], cb2);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 1（d = dilation * 4）：累加到 tmp */
    {
        const float* const wk = w->conv_w[1];
        const int d = dilation * 4;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 2（d = dilation * 3）：累加到 tmp */
    {
        const float* const wk = w->conv_w[2];
        const int d = dilation * 3;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 3（d = dilation * 2）：累加到 tmp */
    {
        const float* const wk = w->conv_w[3];
        const int d = dilation * 2;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 4（d = dilation * 1）：累加到 tmp */
    {
        const float* const wk = w->conv_w[4];
        const int d = dilation;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 5（d=0）：与 mixin / 激活 / head_accum / 残差融合 */
    {
        const float* const wk = w->conv_w[5];
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        const float mw0 = mw[0];
        const float mw1 = mw[1];
        const float mw2 = mw[2];
        const float lb0 = lb[0];
        const float lb1 = lb[1];
        const float lb2 = lb[2];
        const float lw0 = lw[0];
        const float lw1 = lw[1];
        const float lw2 = lw[2];
        const float lw3 = lw[3];
        const float lw4 = lw[4];
        const float lw5 = lw[5];
        const float lw6 = lw[6];
        const float lw7 = lw[7];
        const float lw8 = lw[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (0)) & rb_mask) * 3];
            float _c0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float _c1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float _c2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            _c0 = fmaf(w3, _t[1], _c0);
            _c1 = fmaf(w4, _t[1], _c1);
            _c2 = fmaf(w5, _t[1], _c2);
            _c0 = fmaf(w6, _t[2], _c0);
            _c1 = fmaf(w7, _t[2], _c1);
            _c2 = fmaf(w8, _t[2], _c2);
            { const float _cond = condition[f];
              _c0 += mw0 * _cond;
              _c1 += mw1 * _cond;
              _c2 += mw2 * _cond;
            }
            _c0 = _c0 > 0.0f ? _c0 : 0.01f * _c0;
            _c1 = _c1 > 0.0f ? _c1 : 0.01f * _c1;
            _c2 = _c2 > 0.0f ? _c2 : 0.01f * _c2;
#ifdef NAM_PROFILE_ACTIVATIONS
            _nam_track_peak(_profile_layer, _c0);
            _nam_track_peak(_profile_layer, _c1);
            _nam_track_peak(_profile_layer, _c2);
#endif
            head_accum[f * 3 + 0] += _c0;
            head_accum[f * 3 + 1] += _c1;
            head_accum[f * 3 + 2] += _c2;
            float _r0 = lb0;
            float _r1 = lb1;
            float _r2 = lb2;
            _r0 += lw0 * _c0;
            _r0 += lw3 * _c1;
            _r0 += lw6 * _c2;
            _r1 += lw1 * _c0;
            _r1 += lw4 * _c1;
            _r1 += lw7 * _c2;
            _r2 += lw2 * _c0;
            _r2 += lw5 * _c1;
            _r2 += lw8 * _c2;
            layer_buf[f * 3 + 0] += _r0;
            layer_buf[f * 3 + 1] += _r1;
            layer_buf[f * 3 + 2] += _r2;
        }
    }
}


/* la0 各层的权重指针 */
typedef struct {
    const float* conv_w[15];
    const float* conv_b;
    const float* mixin_w;
    const float* l1x1_w;
    const float* l1x1_b;
} nam_la0_k15_weights_t;

static const nam_la0_k15_weights_t la0_l14_weights = {
    { la0_l14_conv_w0, la0_l14_conv_w1, la0_l14_conv_w2, la0_l14_conv_w3, la0_l14_conv_w4, la0_l14_conv_w5, la0_l14_conv_w6, la0_l14_conv_w7, la0_l14_conv_w8, la0_l14_conv_w9, la0_l14_conv_w10, la0_l14_conv_w11, la0_l14_conv_w12, la0_l14_conv_w13, la0_l14_conv_w14 },
    la0_l14_conv_b,
    la0_l14_mixin_w,
    la0_l14_layer1x1_w,
    la0_l14_layer1x1_b,
};
static const nam_la0_k15_weights_t la0_l15_weights = {
    { la0_l15_conv_w0, la0_l15_conv_w1, la0_l15_conv_w2, la0_l15_conv_w3, la0_l15_conv_w4, la0_l15_conv_w5, la0_l15_conv_w6, la0_l15_conv_w7, la0_l15_conv_w8, la0_l15_conv_w9, la0_l15_conv_w10, la0_l15_conv_w11, la0_l15_conv_w12, la0_l15_conv_w13, la0_l15_conv_w14 },
    la0_l15_conv_b,
    la0_l15_mixin_w,
    la0_l15_layer1x1_w,
    la0_l15_layer1x1_b,
};

/* Tap-major：每个 tap 的 9 个权重每次调用只加载一次，而不是每帧加载。 */
__attribute__((noinline))
static void nam_process_layer_la0_k15(
    float* __restrict layer_buf,
    float* __restrict head_accum,
    const float* __restrict condition,
    float* __restrict ring_buf,
    int* __restrict wpos_ptr,
    const nam_la0_k15_weights_t* __restrict w,
#ifdef NAM_PROFILE_ACTIVATIONS
    int _profile_layer,
#endif
    int dilation, int nf, int rb_mask)
{
    /* 写入环形缓冲 */
    rb_write_circular(ring_buf, *wpos_ptr, layer_buf, 3, nf, rb_mask);
    const int wpos = *wpos_ptr;
    *wpos_ptr = (wpos + nf) & rb_mask;

    const float* const cb = w->conv_b;
    const float* const mw = w->mixin_w;
    const float* const lw = w->l1x1_w;
    const float* const lb = w->l1x1_b;

    float* const tmp = la0_tapmajor_tmp;

    /* Tap 0（d = dilation * 14）：用 conv_b 初始化 tmp */
    {
        const float* const wk = w->conv_w[0];
        const int d = dilation * 14;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        const float cb0 = cb[0];
        const float cb1 = cb[1];
        const float cb2 = cb[2];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], cb0);
            float a1 = fmaf(w1, _t[0], cb1);
            float a2 = fmaf(w2, _t[0], cb2);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 1（d = dilation * 13）：累加到 tmp */
    {
        const float* const wk = w->conv_w[1];
        const int d = dilation * 13;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 2（d = dilation * 12）：累加到 tmp */
    {
        const float* const wk = w->conv_w[2];
        const int d = dilation * 12;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 3（d = dilation * 11）：累加到 tmp */
    {
        const float* const wk = w->conv_w[3];
        const int d = dilation * 11;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 4（d = dilation * 10）：累加到 tmp */
    {
        const float* const wk = w->conv_w[4];
        const int d = dilation * 10;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 5（d = dilation * 9）：累加到 tmp */
    {
        const float* const wk = w->conv_w[5];
        const int d = dilation * 9;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 6（d = dilation * 8）：累加到 tmp */
    {
        const float* const wk = w->conv_w[6];
        const int d = dilation * 8;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 7（d = dilation * 7）：累加到 tmp */
    {
        const float* const wk = w->conv_w[7];
        const int d = dilation * 7;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 8（d = dilation * 6）：累加到 tmp */
    {
        const float* const wk = w->conv_w[8];
        const int d = dilation * 6;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 9（d = dilation * 5）：累加到 tmp */
    {
        const float* const wk = w->conv_w[9];
        const int d = dilation * 5;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 10（d = dilation * 4）：累加到 tmp */
    {
        const float* const wk = w->conv_w[10];
        const int d = dilation * 4;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 11（d = dilation * 3）：累加到 tmp */
    {
        const float* const wk = w->conv_w[11];
        const int d = dilation * 3;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 12（d = dilation * 2）：累加到 tmp */
    {
        const float* const wk = w->conv_w[12];
        const int d = dilation * 2;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 13（d = dilation * 1）：累加到 tmp */
    {
        const float* const wk = w->conv_w[13];
        const int d = dilation;
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (d)) & rb_mask) * 3];
            float a0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float a1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float a2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            a0 = fmaf(w3, _t[1], a0);
            a1 = fmaf(w4, _t[1], a1);
            a2 = fmaf(w5, _t[1], a2);
            a0 = fmaf(w6, _t[2], a0);
            a1 = fmaf(w7, _t[2], a1);
            a2 = fmaf(w8, _t[2], a2);
            tmp[f * 3 + 0] = a0;
            tmp[f * 3 + 1] = a1;
            tmp[f * 3 + 2] = a2;
        }
    }

    /* Tap 14（d=0）：与 mixin / 激活 / head_accum / 残差融合 */
    {
        const float* const wk = w->conv_w[14];
        const float w0 = wk[0];
        const float w1 = wk[1];
        const float w2 = wk[2];
        const float w3 = wk[3];
        const float w4 = wk[4];
        const float w5 = wk[5];
        const float w6 = wk[6];
        const float w7 = wk[7];
        const float w8 = wk[8];
        const float mw0 = mw[0];
        const float mw1 = mw[1];
        const float mw2 = mw[2];
        const float lb0 = lb[0];
        const float lb1 = lb[1];
        const float lb2 = lb[2];
        const float lw0 = lw[0];
        const float lw1 = lw[1];
        const float lw2 = lw[2];
        const float lw3 = lw[3];
        const float lw4 = lw[4];
        const float lw5 = lw[5];
        const float lw6 = lw[6];
        const float lw7 = lw[7];
        const float lw8 = lw[8];
        for (int f = 0; f < nf; f++) {
            const float* _t = &ring_buf[((wpos + f - (0)) & rb_mask) * 3];
            float _c0 = fmaf(w0, _t[0], tmp[f * 3 + 0]);
            float _c1 = fmaf(w1, _t[0], tmp[f * 3 + 1]);
            float _c2 = fmaf(w2, _t[0], tmp[f * 3 + 2]);
            _c0 = fmaf(w3, _t[1], _c0);
            _c1 = fmaf(w4, _t[1], _c1);
            _c2 = fmaf(w5, _t[1], _c2);
            _c0 = fmaf(w6, _t[2], _c0);
            _c1 = fmaf(w7, _t[2], _c1);
            _c2 = fmaf(w8, _t[2], _c2);
            { const float _cond = condition[f];
              _c0 += mw0 * _cond;
              _c1 += mw1 * _cond;
              _c2 += mw2 * _cond;
            }
            _c0 = _c0 > 0.0f ? _c0 : 0.01f * _c0;
            _c1 = _c1 > 0.0f ? _c1 : 0.01f * _c1;
            _c2 = _c2 > 0.0f ? _c2 : 0.01f * _c2;
#ifdef NAM_PROFILE_ACTIVATIONS
            _nam_track_peak(_profile_layer, _c0);
            _nam_track_peak(_profile_layer, _c1);
            _nam_track_peak(_profile_layer, _c2);
#endif
            head_accum[f * 3 + 0] += _c0;
            head_accum[f * 3 + 1] += _c1;
            head_accum[f * 3 + 2] += _c2;
            float _r0 = lb0;
            float _r1 = lb1;
            float _r2 = lb2;
            _r0 += lw0 * _c0;
            _r0 += lw3 * _c1;
            _r0 += lw6 * _c2;
            _r1 += lw1 * _c0;
            _r1 += lw4 * _c1;
            _r1 += lw7 * _c2;
            _r2 += lw2 * _c0;
            _r2 += lw5 * _c1;
            _r2 += lw8 * _c2;
            layer_buf[f * 3 + 0] += _r0;
            layer_buf[f * 3 + 1] += _r1;
            layer_buf[f * 3 + 2] += _r2;
        }
    }
}


/* DTCM 工作缓冲 — 把 D-Cache 留给 SRAM 环形缓冲 */
static float NAM_DTCM _work_la0_layer_buf[3 * NAM_MAX_BUFFER_SIZE];
static float NAM_DTCM _work_la0_head_accum[3 * NAM_MAX_BUFFER_SIZE];
static float NAM_DTCM _work_la0_head_out[1 * NAM_MAX_BUFFER_SIZE];
static float NAM_DTCM _work_condition[1 * NAM_MAX_BUFFER_SIZE];

void nam_process(nam_state_t* state, const float* const* input, float* const* output, int num_frames) {
    const int nf = num_frames;

    float* const la0_layer_buf = _work_la0_layer_buf;
    float* const la0_head_accum = _work_la0_head_accum;
    float* const la0_head_out = _work_la0_head_out;
    float* const _condition = _work_condition;

    /* 将输入拷贝到 condition 缓冲（栈 / DTCM） */
    for (int f = 0; f < nf; f++)
        _condition[f] = input[0][f];

    /* ===== 层组 0 ===== */
    {
        /* 通道重映射：1 -> 3 */
        for (int f = 0; f < nf; f++) {
            const float* _s = &_condition[f * 1];
            la0_layer_buf[f * 3 + 0] = la0_rechannel_w[0] * _s[0];
            la0_layer_buf[f * 3 + 1] = la0_rechannel_w[1] * _s[0];
            la0_layer_buf[f * 3 + 2] = la0_rechannel_w[2] * _s[0];
        }

        memset(la0_head_accum, 0, (size_t)nf * 3 * sizeof(float));

        /* 层 0.0（dilation=1） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l0_conv_rb, &state->la0_l0_conv_rb_wpos,
            &la0_l0_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            0,
#endif
            1, nf, NAM_RB_MASK_128);

        /* 层 0.1（dilation=3） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l1_conv_rb, &state->la0_l1_conv_rb_wpos,
            &la0_l1_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            1,
#endif
            3, nf, NAM_RB_MASK_128);

        /* 层 0.2（dilation=7） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l2_conv_rb, &state->la0_l2_conv_rb_wpos,
            &la0_l2_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            2,
#endif
            7, nf, NAM_RB_MASK_128);

        /* 层 0.3（dilation=17） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l3_conv_rb, &state->la0_l3_conv_rb_wpos,
            &la0_l3_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            3,
#endif
            17, nf, NAM_RB_MASK_256);

        /* 层 0.4（dilation=41） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l4_conv_rb, &state->la0_l4_conv_rb_wpos,
            &la0_l4_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            4,
#endif
            41, nf, NAM_RB_MASK_512);

        /* 层 0.5（dilation=101） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l5_conv_rb, &state->la0_l5_conv_rb_wpos,
            &la0_l5_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            5,
#endif
            101, nf, NAM_RB_MASK_1024);

        /* 层 0.6（dilation=239） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l6_conv_rb, &state->la0_l6_conv_rb_wpos,
            &la0_l6_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            6,
#endif
            239, nf, NAM_RB_MASK_2048);

        /* 层 0.7（dilation=1） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l7_conv_rb, &state->la0_l7_conv_rb_wpos,
            &la0_l7_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            7,
#endif
            1, nf, NAM_RB_MASK_128);

        /* 层 0.8（dilation=3） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l8_conv_rb, &state->la0_l8_conv_rb_wpos,
            &la0_l8_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            8,
#endif
            3, nf, NAM_RB_MASK_128);

        /* 层 0.9（dilation=7） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l9_conv_rb, &state->la0_l9_conv_rb_wpos,
            &la0_l9_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            9,
#endif
            7, nf, NAM_RB_MASK_128);

        /* 层 0.10（dilation=17） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l10_conv_rb, &state->la0_l10_conv_rb_wpos,
            &la0_l10_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            10,
#endif
            17, nf, NAM_RB_MASK_256);

        /* 层 0.11（dilation=41） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l11_conv_rb, &state->la0_l11_conv_rb_wpos,
            &la0_l11_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            11,
#endif
            41, nf, NAM_RB_MASK_512);

        /* 层 0.12（dilation=101） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l12_conv_rb, &state->la0_l12_conv_rb_wpos,
            &la0_l12_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            12,
#endif
            101, nf, NAM_RB_MASK_1024);

        /* 层 0.13（dilation=239） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l13_conv_rb, &state->la0_l13_conv_rb_wpos,
            &la0_l13_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            13,
#endif
            239, nf, NAM_RB_MASK_2048);

        /* 层 0.14（dilation=1） */
        nam_process_layer_la0_k15(la0_layer_buf, la0_head_accum, _condition,
            la0_l14_conv_rb, &state->la0_l14_conv_rb_wpos,
            &la0_l14_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            14,
#endif
            1, nf, NAM_RB_MASK_128);

        /* 层 0.15（dilation=13） */
        nam_process_layer_la0_k15(la0_layer_buf, la0_head_accum, _condition,
            la0_l15_conv_rb, &state->la0_l15_conv_rb_wpos,
            &la0_l15_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            15,
#endif
            13, nf, NAM_RB_MASK_256);

        /* 层 0.16（dilation=1） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l16_conv_rb, &state->la0_l16_conv_rb_wpos,
            &la0_l16_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            16,
#endif
            1, nf, NAM_RB_MASK_128);

        /* 层 0.17（dilation=3） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l17_conv_rb, &state->la0_l17_conv_rb_wpos,
            &la0_l17_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            17,
#endif
            3, nf, NAM_RB_MASK_128);

        /* 层 0.18（dilation=7） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l18_conv_rb, &state->la0_l18_conv_rb_wpos,
            &la0_l18_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            18,
#endif
            7, nf, NAM_RB_MASK_128);

        /* 层 0.19（dilation=17） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l19_conv_rb, &state->la0_l19_conv_rb_wpos,
            &la0_l19_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            19,
#endif
            17, nf, NAM_RB_MASK_256);

        /* 层 0.20（dilation=41） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l20_conv_rb, &state->la0_l20_conv_rb_wpos,
            &la0_l20_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            20,
#endif
            41, nf, NAM_RB_MASK_512);

        /* 层 0.21（dilation=101） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l21_conv_rb, &state->la0_l21_conv_rb_wpos,
            &la0_l21_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            21,
#endif
            101, nf, NAM_RB_MASK_1024);

        /* 层 0.22（dilation=239） */
        nam_process_layer_la0_k6(la0_layer_buf, la0_head_accum, _condition,
            la0_l22_conv_rb, &state->la0_l22_conv_rb_wpos,
            &la0_l22_weights,
#ifdef NAM_PROFILE_ACTIVATIONS
            22,
#endif
            239, nf, NAM_RB_MASK_2048);

        /* 头部通道重映射：3 -> 1 */
        rb_write_circular(la0_head_rechannel_rb, state->la0_head_rechannel_rb_wpos, la0_head_accum, 3, nf, NAM_RB_MASK_128);
        { const int _wpos_hr = state->la0_head_rechannel_rb_wpos;
        { const float w0_0 = la0_head_rechannel_w0[0], w1_0 = la0_head_rechannel_w0[1], w2_0 = la0_head_rechannel_w0[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 15) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], 0.0f);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w1[0], w1_0 = la0_head_rechannel_w1[1], w2_0 = la0_head_rechannel_w1[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 14) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w2[0], w1_0 = la0_head_rechannel_w2[1], w2_0 = la0_head_rechannel_w2[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 13) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w3[0], w1_0 = la0_head_rechannel_w3[1], w2_0 = la0_head_rechannel_w3[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 12) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w4[0], w1_0 = la0_head_rechannel_w4[1], w2_0 = la0_head_rechannel_w4[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 11) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w5[0], w1_0 = la0_head_rechannel_w5[1], w2_0 = la0_head_rechannel_w5[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 10) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w6[0], w1_0 = la0_head_rechannel_w6[1], w2_0 = la0_head_rechannel_w6[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 9) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w7[0], w1_0 = la0_head_rechannel_w7[1], w2_0 = la0_head_rechannel_w7[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 8) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w8[0], w1_0 = la0_head_rechannel_w8[1], w2_0 = la0_head_rechannel_w8[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 7) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w9[0], w1_0 = la0_head_rechannel_w9[1], w2_0 = la0_head_rechannel_w9[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 6) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w10[0], w1_0 = la0_head_rechannel_w10[1], w2_0 = la0_head_rechannel_w10[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 5) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w11[0], w1_0 = la0_head_rechannel_w11[1], w2_0 = la0_head_rechannel_w11[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 4) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w12[0], w1_0 = la0_head_rechannel_w12[1], w2_0 = la0_head_rechannel_w12[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 3) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w13[0], w1_0 = la0_head_rechannel_w13[1], w2_0 = la0_head_rechannel_w13[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 2) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w14[0], w1_0 = la0_head_rechannel_w14[1], w2_0 = la0_head_rechannel_w14[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 1) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        { const float w0_0 = la0_head_rechannel_w15[0], w1_0 = la0_head_rechannel_w15[1], w2_0 = la0_head_rechannel_w15[2];
          for (int _f = 0; _f < nf; _f++) {
            const float* _t = &la0_head_rechannel_rb[((_wpos_hr + _f - 0) & NAM_RB_MASK_128) * 3];
            float _a0 = fmaf(w0_0, _t[0], la0_head_out[_f]);
            _a0 = fmaf(w1_0, _t[1], _a0);
            _a0 = fmaf(w2_0, _t[2], _a0);
            la0_head_out[_f] = _a0;
          }
        }
        state->la0_head_rechannel_rb_wpos = (_wpos_hr + nf) & NAM_RB_MASK_128; }
        for (int _f = 0; _f < nf; _f++) {
            for (int _o = 0; _o < 1; _o++) {
                la0_head_out[_f * 1 + _o] += la0_head_rechannel_b[_o];
            }
        }

    }

    /* 乘以 head_scale 后输出 */
    for (int f = 0; f < nf; f++)
        output[0][f] = head_scale * la0_head_out[f];
}

/* ===== 动态权重加载 ===== */
/*
 * 将分组 Conv1x1 权重从 C++ 加载顺序重排为列主序。
 * C++ 顺序：for g, for out_i, for in_j -> 行主序分块
 * 列主序：w[in * out_ch + out] = w(out, in)
 */
static void load_conv1x1_colmajor(float* dst, const float** src,
                                   int in_ch, int out_ch, int groups)
{
    const float* w = *src;
    if (groups == in_ch && in_ch == out_ch) {
        /* Depthwise：按通道直接拷贝权重 */
        for (int c = 0; c < in_ch; c++) dst[c] = w[c];
        *src = w + in_ch;
        return;
    }
    int out_pg = out_ch / groups;
    int in_pg = in_ch / groups;
    /* 清零目标（分块对角可能留空位） */
    memset(dst, 0, (size_t)in_ch * (size_t)out_ch * sizeof(float));
    int idx = 0;
    for (int g = 0; g < groups; g++)
        for (int oi = 0; oi < out_pg; oi++)
            for (int ij = 0; ij < in_pg; ij++)
                dst[(g * in_pg + ij) * out_ch + (g * out_pg + oi)] = w[idx++];
    *src = w + idx;
}

static void load_conv1d_colmajor(float** tap_dsts, const float** src,
                                  int in_ch, int out_ch, int kernel_size, int groups)
{
    const float* w = *src;
    if (groups == in_ch && in_ch == out_ch) {
        /* Depthwise：C++ 顺序为 for c, for k：w[k][c] */
        for (int c = 0; c < in_ch; c++)
            for (int k = 0; k < kernel_size; k++)
                tap_dsts[k][c] = *w++;
        *src = w;
        return;
    }
    int out_pg = out_ch / groups;
    int in_pg = in_ch / groups;
    for (int k = 0; k < kernel_size; k++)
        memset(tap_dsts[k], 0, (size_t)in_ch * (size_t)out_ch * sizeof(float));
    for (int g = 0; g < groups; g++)
        for (int oi = 0; oi < out_pg; oi++)
            for (int ij = 0; ij < in_pg; ij++)
                for (int k = 0; k < kernel_size; k++)
                    tap_dsts[k][(g * in_pg + ij) * out_ch + (g * out_pg + oi)] = *w++;
    *src = w;
}

static void load_bias(float* dst, const float** src, int n)
{
    memcpy(dst, *src, (size_t)n * sizeof(float));
    *src += n;
}

int nam_load_weights(const float* weights, int num_weights)
{
    if (num_weights != 1871) return -1;
    const float* w = weights;

    load_conv1x1_colmajor(la0_rechannel_w, &w, 1, 3, 1);
    {
        float* _taps[6] = {
            la0_l0_conv_w0,
            la0_l0_conv_w1,
            la0_l0_conv_w2,
            la0_l0_conv_w3,
            la0_l0_conv_w4,
            la0_l0_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l0_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l0_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l0_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l0_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l1_conv_w0,
            la0_l1_conv_w1,
            la0_l1_conv_w2,
            la0_l1_conv_w3,
            la0_l1_conv_w4,
            la0_l1_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l1_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l1_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l1_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l1_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l2_conv_w0,
            la0_l2_conv_w1,
            la0_l2_conv_w2,
            la0_l2_conv_w3,
            la0_l2_conv_w4,
            la0_l2_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l2_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l2_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l2_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l2_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l3_conv_w0,
            la0_l3_conv_w1,
            la0_l3_conv_w2,
            la0_l3_conv_w3,
            la0_l3_conv_w4,
            la0_l3_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l3_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l3_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l3_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l3_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l4_conv_w0,
            la0_l4_conv_w1,
            la0_l4_conv_w2,
            la0_l4_conv_w3,
            la0_l4_conv_w4,
            la0_l4_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l4_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l4_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l4_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l4_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l5_conv_w0,
            la0_l5_conv_w1,
            la0_l5_conv_w2,
            la0_l5_conv_w3,
            la0_l5_conv_w4,
            la0_l5_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l5_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l5_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l5_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l5_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l6_conv_w0,
            la0_l6_conv_w1,
            la0_l6_conv_w2,
            la0_l6_conv_w3,
            la0_l6_conv_w4,
            la0_l6_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l6_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l6_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l6_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l6_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l7_conv_w0,
            la0_l7_conv_w1,
            la0_l7_conv_w2,
            la0_l7_conv_w3,
            la0_l7_conv_w4,
            la0_l7_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l7_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l7_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l7_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l7_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l8_conv_w0,
            la0_l8_conv_w1,
            la0_l8_conv_w2,
            la0_l8_conv_w3,
            la0_l8_conv_w4,
            la0_l8_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l8_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l8_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l8_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l8_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l9_conv_w0,
            la0_l9_conv_w1,
            la0_l9_conv_w2,
            la0_l9_conv_w3,
            la0_l9_conv_w4,
            la0_l9_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l9_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l9_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l9_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l9_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l10_conv_w0,
            la0_l10_conv_w1,
            la0_l10_conv_w2,
            la0_l10_conv_w3,
            la0_l10_conv_w4,
            la0_l10_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l10_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l10_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l10_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l10_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l11_conv_w0,
            la0_l11_conv_w1,
            la0_l11_conv_w2,
            la0_l11_conv_w3,
            la0_l11_conv_w4,
            la0_l11_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l11_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l11_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l11_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l11_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l12_conv_w0,
            la0_l12_conv_w1,
            la0_l12_conv_w2,
            la0_l12_conv_w3,
            la0_l12_conv_w4,
            la0_l12_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l12_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l12_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l12_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l12_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l13_conv_w0,
            la0_l13_conv_w1,
            la0_l13_conv_w2,
            la0_l13_conv_w3,
            la0_l13_conv_w4,
            la0_l13_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l13_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l13_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l13_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l13_layer1x1_b, &w, 3);
    {
        float* _taps[15] = {
            la0_l14_conv_w0,
            la0_l14_conv_w1,
            la0_l14_conv_w2,
            la0_l14_conv_w3,
            la0_l14_conv_w4,
            la0_l14_conv_w5,
            la0_l14_conv_w6,
            la0_l14_conv_w7,
            la0_l14_conv_w8,
            la0_l14_conv_w9,
            la0_l14_conv_w10,
            la0_l14_conv_w11,
            la0_l14_conv_w12,
            la0_l14_conv_w13,
            la0_l14_conv_w14,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 15, 1);
    }
    load_bias(la0_l14_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l14_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l14_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l14_layer1x1_b, &w, 3);
    {
        float* _taps[15] = {
            la0_l15_conv_w0,
            la0_l15_conv_w1,
            la0_l15_conv_w2,
            la0_l15_conv_w3,
            la0_l15_conv_w4,
            la0_l15_conv_w5,
            la0_l15_conv_w6,
            la0_l15_conv_w7,
            la0_l15_conv_w8,
            la0_l15_conv_w9,
            la0_l15_conv_w10,
            la0_l15_conv_w11,
            la0_l15_conv_w12,
            la0_l15_conv_w13,
            la0_l15_conv_w14,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 15, 1);
    }
    load_bias(la0_l15_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l15_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l15_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l15_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l16_conv_w0,
            la0_l16_conv_w1,
            la0_l16_conv_w2,
            la0_l16_conv_w3,
            la0_l16_conv_w4,
            la0_l16_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l16_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l16_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l16_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l16_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l17_conv_w0,
            la0_l17_conv_w1,
            la0_l17_conv_w2,
            la0_l17_conv_w3,
            la0_l17_conv_w4,
            la0_l17_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l17_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l17_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l17_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l17_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l18_conv_w0,
            la0_l18_conv_w1,
            la0_l18_conv_w2,
            la0_l18_conv_w3,
            la0_l18_conv_w4,
            la0_l18_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l18_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l18_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l18_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l18_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l19_conv_w0,
            la0_l19_conv_w1,
            la0_l19_conv_w2,
            la0_l19_conv_w3,
            la0_l19_conv_w4,
            la0_l19_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l19_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l19_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l19_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l19_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l20_conv_w0,
            la0_l20_conv_w1,
            la0_l20_conv_w2,
            la0_l20_conv_w3,
            la0_l20_conv_w4,
            la0_l20_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l20_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l20_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l20_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l20_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l21_conv_w0,
            la0_l21_conv_w1,
            la0_l21_conv_w2,
            la0_l21_conv_w3,
            la0_l21_conv_w4,
            la0_l21_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l21_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l21_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l21_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l21_layer1x1_b, &w, 3);
    {
        float* _taps[6] = {
            la0_l22_conv_w0,
            la0_l22_conv_w1,
            la0_l22_conv_w2,
            la0_l22_conv_w3,
            la0_l22_conv_w4,
            la0_l22_conv_w5,
        };
        load_conv1d_colmajor(_taps, &w, 3, 3, 6, 1);
    }
    load_bias(la0_l22_conv_b, &w, 3);
    load_conv1x1_colmajor(la0_l22_mixin_w, &w, 1, 3, 1);
    load_conv1x1_colmajor(la0_l22_layer1x1_w, &w, 3, 3, 1);
    load_bias(la0_l22_layer1x1_b, &w, 3);
    {
        float* _taps[16] = {
            la0_head_rechannel_w0,
            la0_head_rechannel_w1,
            la0_head_rechannel_w2,
            la0_head_rechannel_w3,
            la0_head_rechannel_w4,
            la0_head_rechannel_w5,
            la0_head_rechannel_w6,
            la0_head_rechannel_w7,
            la0_head_rechannel_w8,
            la0_head_rechannel_w9,
            la0_head_rechannel_w10,
            la0_head_rechannel_w11,
            la0_head_rechannel_w12,
            la0_head_rechannel_w13,
            la0_head_rechannel_w14,
            la0_head_rechannel_w15,
        };
        load_conv1d_colmajor(_taps, &w, 3, 1, 16, 1);
    }
    load_bias(la0_head_rechannel_b, &w, 1);

    /* head_scale 是最后一个权重 */
    head_scale = *w++;
    return 0;
}