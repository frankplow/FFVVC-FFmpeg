;
; AVX2 functions for the VVC inverse transforms
;
; (c) 2023 Frank Plowman <post@frankplowman.com>
;
; This file is part of FFmpeg.
;
; FFmpeg is free software; you can redistribute it and/or
; modify it under the terms of the GNU Lesser General Public
; License as published by the Free Software Foundation; either
; version 2.1 of the License, or (at your option) any later version.
;
; FFmpeg is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; Lesser General Public License for more details.
;
; You should have received a copy of the GNU Lesser General Public
; License along with FFmpeg; if not, write to the Free Software
; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

; Replace -x with mx, for use in a symbol
%define COEF(=num) %cond(num >=0, num, m %+ %abs(num))

%macro COEF_PAIR_EVEN 2
    const vvc_pw_ %+ COEF(%1) %+ _ %+ COEF( %2), times 8 dw %1,  %2
    const vvc_pw_ %+ COEF(%1) %+ _ %+ COEF(-%2), times 8 dw %1, -%2
%endmacro

%macro COEF_PAIR_ODD 2
    const vvc_pw_ %+ COEF( %1) %+ _ %+ COEF(%2), times 8 dw  %1, %2
    const vvc_pw_ %+ COEF(-%2) %+ _ %+ COEF(%1), times 8 dw -%2, %1
%endmacro

COEF_PAIR_EVEN 64, 64
COEF_PAIR_ODD -36, 83

const vvc_pd_64, times 8 dd 64

const vvc_pd_64, times 8 dd 64
const vvc_pd_512, times 8 dd 512

SECTION .text

INIT_XMM avx2

cglobal vvc_inv_dct2_dct2_4x4_10, 4, 4, 5, dst, coeff, nzw, log2_transform_range
    mova            m0, [coeffq]
    mova            m1, [coeffq + 16]
    mova            m2, [coeffq + 32]
    mova            m3, [coeffq + 48]
    packssdw        m0, m1
    packssdw        m1, m2, m3

    call            .main

.scale_clip:
    REPX            {paddd x, [vvc_pd_64]}, m0, m1, m2, m3
    REPX            {psrad x, 7}, m0, m1, m2, m3
    packssdw        m0, m1
    packssdw        m1, m2, m3

.transpose:
    punpckhwd       m2, m0, m1
    punpcklwd       m0, m1
    punpckhwd       m1, m0, m2
    punpcklwd       m0, m2

.pass2:
    call            .main

.scale:
    REPX            {paddd x, [vvc_pd_512]}, m0, m1, m2, m3
    REPX            {psrad x, 10}, m0, m1, m2, m3

    mova            [dstq], m0
    mova            [dstq + 16], m1
    mova            [dstq + 32], m2
    mova            [dstq + 48], m3

    RET

ALIGN function_align
cglobal_label .main
    punpckhwd       m4, m0, m1
    punpcklwd       m0, m1

    pmaddwd         m1, m0, [vvc_pw_64_m64]     ; z1
    pmaddwd         m5, m4, [vvc_pw_m83_m36]    ; z3
    pmaddwd         m0, [vvc_pw_64_64]          ; z0
    pmaddwd         m4, [vvc_pw_m36_83]         ; z2

    paddd           m3, m0, m5
    paddd           m2, m1, m4
    psubd           m0, m5
    psubd           m1, m4

    RET
