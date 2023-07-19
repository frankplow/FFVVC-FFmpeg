/*
 * Copyright (c) 2023 Wu Jianhua <toqsxw@outlook.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/avcodec.h"

#include "libavcodec/vvc/vvcdsp.h"
#include "libavcodec/vvc/vvcdec.h"

#include "checkasm.h"

static const uint32_t pixel_mask[] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff, 0x3fff3fff, 0xffffffff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define PIXEL_STRIDE (MAX_CTU_SIZE * 2)
#define EXTRA_BEFORE 3
#define EXTRA_AFTER  4
#define SRC_EXTRA    (EXTRA_BEFORE + EXTRA_AFTER)
#define SRC_BUF_SIZE (PIXEL_STRIDE + SRC_EXTRA) * (PIXEL_STRIDE + SRC_EXTRA)
#define DST_BUF_SIZE (MAX_CTU_SIZE * MAX_CTU_SIZE * 2)
#define SRC_OFFSET   ((PIXEL_STRIDE + EXTRA_BEFORE * 2) * EXTRA_BEFORE)

#define randomize_buffers(buf0, buf1, size)                 \
    do {                                                    \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];   \
        int k;                                              \
        for (k = 0; k < size; k += 4) {                     \
            uint32_t r = rnd() & mask;                      \
            AV_WN32A(buf0 + k, r);                          \
            AV_WN32A(buf1 + k, r);                          \
        }                                                   \
    } while (0)

#define CHECK_FUNC(func, ...) if (check_func(func, __VA_ARGS__)) {                  \
    memset(dst0, 0, DST_BUF_SIZE);                                                  \
    memset(dst1, 0, DST_BUF_SIZE);                                                  \
    call_ref(dst0, src0 + SRC_OFFSET, PIXEL_STRIDE, h, mx, my, w, hf_idx, vf_idx);  \
    call_new(dst1, src1 + SRC_OFFSET, PIXEL_STRIDE, h, mx, my, w, hf_idx, vf_idx);  \
    if (memcmp(dst0, dst1, DST_BUF_SIZE))                                           \
        fail();                                                                     \
    bench_new(dst1, src1 + SRC_OFFSET, PIXEL_STRIDE, h, mx, my, w, hf_idx, vf_idx); \
}

static void check_put_vvc_luma(VVCDSPContext *c, int bit_depth)
{
    LOCAL_ALIGNED_32(int16_t, dst0, [DST_BUF_SIZE / 2]);
    LOCAL_ALIGNED_32(int16_t, dst1, [DST_BUF_SIZE / 2]);
    LOCAL_ALIGNED_32(uint8_t, src0, [SRC_BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [SRC_BUF_SIZE]);

    declare_func(void, int16_t *dst, const uint8_t *src, const ptrdiff_t src_stride,
        const int height, const intptr_t mx, const intptr_t my, const int width,
        const int hf_idx, const int vf_idx);

    randomize_buffers(src0, src1, SRC_BUF_SIZE);

    for (int h = 4; h <= MAX_CU_SIZE; h *= 2) {
        for (int w = 4; w <= MAX_CU_SIZE; w *= 2) {
            int mx     = rnd() % 16;
            int my     = rnd() % 16;
            int hf_idx = rnd() % 3;
            int vf_idx = rnd() % 3;
            CHECK_FUNC(c->inter.put[LUMA][0][1], "put_vvc_luma_h_%d_%d_%d", bit_depth, w, h);
            CHECK_FUNC(c->inter.put[LUMA][1][0], "put_vvc_luma_v_%d_%d_%d",  bit_depth, w, h);
            CHECK_FUNC(c->inter.put[LUMA][1][1], "put_vvc_luma_hv_%d_%d_%d", bit_depth, w, h);
        }
    }
}

void checkasm_check_vvc_mc(void)
{
    int bit_depth;
    VVCDSPContext h;
    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vvc_dsp_init(&h, bit_depth, 0);
        check_put_vvc_luma(&h, bit_depth);
    }

    report("put_vvc_luma");
}
