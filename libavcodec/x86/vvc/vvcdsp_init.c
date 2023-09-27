/*
 * VVC DSP init for x86
 *
 * Copyright (C) 2022-2023 Nuo Mi
 * Copyright (c) 2023 Wu Jianhua <toqsxw@outlook.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavutil/cpu.h"
#include "libavcodec/vvc/vvcdec.h"
#include "libavcodec/vvc/vvcdsp.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include <stdlib.h>
#include <time.h>

#define bf(fn, bd,  opt) fn##_##bd##_##opt
#define BF(fn, bpc, opt) fn##_##bpc##bpc_##opt

#define ALF_BPC_FUNCS(bpc, opt)                                                                                         \
void BF(ff_vvc_alf_filter_luma, bpc, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                           \
    const uint8_t *src, ptrdiff_t src_stride, ptrdiff_t width, ptrdiff_t height,                                        \
    const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);               \
void BF(ff_vvc_alf_filter_chroma, bpc, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                         \
    const uint8_t *src, ptrdiff_t src_stride, ptrdiff_t width, ptrdiff_t height,                                        \
    const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);               \
void BF(ff_vvc_alf_classify_grad, bpc, opt)(int *gradient_sum,                                                          \
    const uint8_t *src, ptrdiff_t src_stride, intptr_t width, intptr_t height, intptr_t vb_pos);                        \
void BF(ff_vvc_alf_classify, bpc, opt)(int *class_idx, int *transpose_idx, const int *gradient_sum,                     \
    intptr_t width, intptr_t height, intptr_t vb_pos, intptr_t bit_depth);                                              \

#define ALF_FUNCS(bpc, bd, opt)                                                                                         \
static void bf(alf_classify, bd, opt)(int *class_idx, int *transpose_idx,                                               \
    const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, int *gradient_tmp)                     \
{                                                                                                                       \
    BF(ff_vvc_alf_classify_grad, bpc, opt)(gradient_tmp, src, src_stride, width, height, vb_pos);                       \
    BF(ff_vvc_alf_classify, bpc, opt)(class_idx, transpose_idx, gradient_tmp, width, height, vb_pos, bd);               \
}                                                                                                                       \
static void bf(alf_filter_luma, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,  \
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)                                \
{                                                                                                                       \
    const int param_stride  = (width >> 2) * ALF_NUM_COEFF_LUMA;                                                        \
    BF(ff_vvc_alf_filter_luma, bpc, opt)(dst, dst_stride, src, src_stride, width, height,                               \
        filter, clip, param_stride, vb_pos, (1 << bd)  - 1);                                                            \
}                                                                                                                       \
static void bf(alf_filter_chroma, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,\
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)                                \
{                                                                                                                       \
    BF(ff_vvc_alf_filter_chroma, bpc, opt)(dst, dst_stride, src, src_stride, width, height,                             \
        filter, clip, 0, vb_pos,(1 << bd)  - 1);                                                                        \
}                                                                                                                       \

ALF_BPC_FUNCS(8,  avx2)
ALF_BPC_FUNCS(16, avx2)

ALF_FUNCS(8,  8,  avx2)
ALF_FUNCS(16, 10, avx2)
ALF_FUNCS(16, 12, avx2)

#define ALF_INIT(bd) do {                                                       \
        c->alf.filter[LUMA] = alf_filter_luma_##bd##_avx2;                      \
        c->alf.filter[CHROMA] = alf_filter_chroma_##bd##_avx2;                  \
        c->alf.classify = alf_classify_##bd##_avx2;                             \
    } while (0)


#define SAO_FILTER_FUNCS(w, bd, opt)                                                  \
void ff_vvc_sao_band_filter_##w##_##bd##_##opt(                                       \
    uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src, \
    const int16_t *sao_offset_val, int sao_left_class, int width, int height);        \
void ff_vvc_sao_edge_filter_##w##_##bd##_##opt(                                       \
    uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,                         \
    const int16_t *sao_offset_val, int eo, int width, int height);                    \

#define SAO_FUNCS(bd, opt)                                                            \
    SAO_FILTER_FUNCS(8,   bd, opt)                                                    \
    SAO_FILTER_FUNCS(16,  bd, opt)                                                    \
    SAO_FILTER_FUNCS(32,  bd, opt)                                                    \
    SAO_FILTER_FUNCS(48,  bd, opt)                                                    \
    SAO_FILTER_FUNCS(64,  bd, opt)                                                    \
    SAO_FILTER_FUNCS(80,  bd, opt)                                                    \
    SAO_FILTER_FUNCS(96,  bd, opt)                                                    \
    SAO_FILTER_FUNCS(112, bd, opt)                                                    \
    SAO_FILTER_FUNCS(128, bd, opt)                                                    \

SAO_FUNCS(8,  avx2)
SAO_FUNCS(10, avx2)
SAO_FUNCS(12, avx2)

#define SAO_FILTER_INIT(type, bd, opt) do {                                       \
    c->sao.type##_filter[0]       = ff_vvc_sao_##type##_filter_8_##bd##_##opt;    \
    c->sao.type##_filter[1]       = ff_vvc_sao_##type##_filter_16_##bd##_##opt;   \
    c->sao.type##_filter[2]       = ff_vvc_sao_##type##_filter_32_##bd##_##opt;   \
    c->sao.type##_filter[3]       = ff_vvc_sao_##type##_filter_48_##bd##_##opt;   \
    c->sao.type##_filter[4]       = ff_vvc_sao_##type##_filter_64_##bd##_##opt;   \
    c->sao.type##_filter[5]       = ff_vvc_sao_##type##_filter_80_##bd##_##opt;   \
    c->sao.type##_filter[6]       = ff_vvc_sao_##type##_filter_96_##bd##_##opt;   \
    c->sao.type##_filter[7]       = ff_vvc_sao_##type##_filter_112_##bd##_##opt;  \
    c->sao.type##_filter[8]       = ff_vvc_sao_##type##_filter_128_##bd##_##opt;  \
} while (0)

#define SAO_INIT(bd, opt) do {                                                    \
    SAO_FILTER_INIT(edge, bd, opt);                                               \
    SAO_FILTER_INIT(band, bd, opt);                                               \
} while (0)

#define AVG_BPC_FUNC(bpc, opt)                                                                      \
void BF(ff_vvc_avg, bpc, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                   \
    const int16_t *src0, const int16_t *src1, intptr_t width, intptr_t height, intptr_t pixel_max); \
void BF(ff_vvc_w_avg, bpc, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                 \
    const int16_t *src0, const int16_t *src1, intptr_t width, intptr_t height,                      \
    intptr_t denom, intptr_t w0, intptr_t w1,  intptr_t o0, intptr_t o1, intptr_t pixel_max);       \


#define AVG_FUNCS(bpc, bd, opt)                                                                     \
static void bf(avg, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                    \
    const int16_t *src0, const int16_t *src1, int width, int height)                                \
{                                                                                                   \
    BF(ff_vvc_avg, bpc, opt)(dst, dst_stride, src0, src1, width, height, (1 << bd)  - 1);           \
}                                                                                                   \
static void bf(w_avg, bd, opt)(uint8_t *dst, ptrdiff_t dst_stride,                                  \
    const int16_t *src0, const int16_t *src1, int width, int height,                                \
    int denom, int w0, int w1, int o0, int o1)                                                      \
{                                                                                                   \
    BF(ff_vvc_w_avg, bpc, opt)(dst, dst_stride, src0, src1, width, height,                          \
        denom, w0, w1, o0, o1, (1 << bd)  - 1);                                                     \
}

AVG_BPC_FUNC(8,   avx2)
AVG_BPC_FUNC(16,  avx2)

AVG_FUNCS(8,  8,  avx2)
AVG_FUNCS(16, 10, avx2)
AVG_FUNCS(16, 12, avx2)

#define AVG_INIT(bd, opt) do {                                          \
    c->inter.avg    = bf(avg, bd, opt);                                 \
    c->inter.w_avg  = bf(w_avg, bd, opt);                               \
} while (0)

void ff_vvc_dsp_init_x86(VVCDSPContext *const c, const int bd)
{
    const int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_AVX2(cpu_flags)) {
        switch (bd) {
            case 8:
                ALF_INIT(8);
                AVG_INIT(8, avx2);
                c->sao.band_filter[0] = ff_vvc_sao_band_filter_8_8_avx2;
                c->sao.band_filter[1] = ff_vvc_sao_band_filter_16_8_avx2;
                break;
            case 10:
                ALF_INIT(10);
                AVG_INIT(10, avx2);
                c->sao.band_filter[0] = ff_vvc_sao_band_filter_8_10_avx2;
                break;
            case 12:
                ALF_INIT(12);
                AVG_INIT(12, avx2);
                break;
            default:
                break;
        }
    }
    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        switch (bd) {
            case 8:
                SAO_INIT(8, avx2);
                break;
            case 10:
                SAO_INIT(10, avx2);
                break;
            case 12:
                SAO_INIT(12, avx2);
            default:
                break;
        }
    }
}
