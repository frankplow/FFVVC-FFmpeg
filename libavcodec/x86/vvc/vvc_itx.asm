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

%define m(x) mangle(private_prefix %+ _ %+ x %+ SUFFIX)

; Replace -x with mx, for use in a symbol
%define COEF(=num) %cond(num >=0, num, m %+ %abs(num))

%macro COEF_PAIR 2
    const vvc_pw_ %+ COEF(%1) %+ _ %+ COEF(%2), times 8 dw  %1, %2
%endmacro

%macro COEF_PAIR_EVEN 2
    COEF_PAIR %1, %2
    COEF_PAIR %1, -%2
%endmacro

COEF_PAIR_EVEN 64, 64
COEF_PAIR -36, 83
COEF_PAIR -83, -36
COEF_PAIR_EVEN 64, 83
COEF_PAIR_EVEN 64, 36
COEF_PAIR_EVEN -64, -83
COEF_PAIR -18, 50
COEF_PAIR -50, 89
COEF_PAIR -75, 18
COEF_PAIR -89, -75
COEF_PAIR -75, 89
COEF_PAIR -18, -75
COEF_PAIR 89, 50
COEF_PAIR -50, -18

const vvc_pd_64, times 8 dd 64
const vvc_pd_512, times 8 dd 512

align 32
const vvc_deint, dd 0, 2, 4, 6, 1, 5, 3, 7

SECTION .text

%macro LOAD_COEFFS 1 ; rows
    %assign i 0
    %rep %1
        mova        m %+ i, [coeffq + mmsize*i]
        %assign i i+1
    %endrep
%endmacro

%macro STORE_COEFFS 1 ; rows
    %assign i 0
    %rep %1
        mova        [dstq + mmsize*i], m %+ i
        %assign i i+1
    %endrep
%endmacro

INIT_XMM avx2

cglobal vvc_inv_dct2_dct2_4x4_10, 4, 4, 6, dst, coeff, nzw, log2_transform_range
    LOAD_COEFFS     4
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

    STORE_COEFFS    4

    RET

ALIGN function_align
cglobal_label .main
    punpckhwd       m14, m0, m1
    punpcklwd       m0, m1

    pmaddwd         m1, m0, [vvc_pw_64_m64]     ; z1
    pmaddwd         m15, m14, [vvc_pw_m83_m36]  ; z3
    pmaddwd         m0, [vvc_pw_64_64]          ; z0
    pmaddwd         m14, [vvc_pw_m36_83]        ; z2

    paddd           m3, m0, m15
    paddd           m2, m1, m14
    psubd           m0, m15
    psubd           m1, m14

    ret

; @TODO: Refactor this macro to have a nicer calling convention.
%macro IDCT2_8 1 ; width
    ; m0 and m1 contain even rows
    ; m12 and m13 contain odd rows
    packssdw        m0, m2
    packssdw        m12, m1, m5
    packssdw        m1, m4, m6
    packssdw        m13, m3, m7

    call            m(vvc_inv_dct2_dct2_%1x4_10).main

    punpckhwd       m11, m12, m13
    punpcklwd       m7, m12, m13

    pmaddwd         m4, m7, [vvc_pw_m18_50]
    pmaddwd         m5, m7, [vvc_pw_m50_89]
    pmaddwd         m6, m7, [vvc_pw_m75_18]
    pmaddwd         m7, [vvc_pw_m89_m75]

    pmaddwd         m8, m11, [vvc_pw_m75_89]
    pmaddwd         m9, m11, [vvc_pw_m18_m75]
    pmaddwd         m10, m11, [vvc_pw_89_50]
    pmaddwd         m11, [vvc_pw_m50_m18]
    
    paddd           m8, m4      ; z4
    paddd           m9, m5      ; z5
    paddd           m10, m6     ; z6
    paddd           m11, m7     ; z7

    paddd           m7, m0, m11
    paddd           m6, m1, m10
    paddd           m5, m2, m9
    paddd           m4, m3, m8
    psubd           m0, m11
    psubd           m1, m10
    psubd           m2, m9
    psubd           m3, m8
%endmacro

cglobal vvc_inv_dct2_dct2_4x8_10, 4, 4, 12, dst, coeff, nzw, log2_transform_range
    LOAD_COEFFS     8

    call            .pass1

.scale_clip:
    REPX            {paddd x, [vvc_pd_64]}, m0, m1, m2, m3, m4, m5, m6, m7
    REPX            {psrad x, 7}, m0, m1, m2, m3, m4, m5, m6, m7
    packssdw        m0, m1
    packssdw        m1, m2, m3
    packssdw        m2, m4, m5
    packssdw        m3, m6, m7

.transpose:
    punpckhwd       m4, m0, m1
    punpcklwd       m0, m1
    punpckhwd       m5, m2, m3
    punpcklwd       m2, m3
    ; punpckhwd       m4, m0, m1
    ; punpcklwd       m0, m1
    ; punpckhwd       m1, m0, m4
    ; punpcklwd       m0, m2

.pass2:
    call            m(vvc_inv_dct2_dct2_8x4_10).main

.scale:
    REPX            {paddd x, [vvc_pd_512]}, m0, m1, m2, m3, m4, m5, m6, m7
    REPX            {psrad x, 10}, m0, m1, m2, m3, m4, m5, m6, m7

    STORE_COEFFS    8

    RET

.pass1:

ALIGN function_align
cglobal_label .main
    IDCT2_8         4
    ret

INIT_YMM avx2

cglobal vvc_inv_dct2_dct2_8x4_10, 4, 4, 4, dst, coeff, nzw, log2_transform_range
    LOAD_COEFFS     4
    packssdw        m0, m1
    packssdw        m1, m2, m3

    call            .main

.scale_clip:
    REPX            {paddd x, [vvc_pd_64]}, m0, m1, m2, m3
    REPX            {psrad x, 7}, m0, m1, m2, m3
    packssdw        m0, m1
    packssdw        m1, m2, m3

.transpose:

.pass2:
    call            m(vvc_inv_dct2_dct2_4x8_10).main

.scale:
    REPX            {paddd x, [vvc_pd_512]}, m0, m1, m2, m3
    REPX            {psrad x, 10}, m0, m1, m2, m3

    STORE_COEFFS    4

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

    ret

cglobal vvc_inv_dct2_dct2_8x8_10, 4, 4, 16, dst, coeff, nzw, log2_transform_range
    LOAD_COEFFS     8

    call            .pass1

.scale_clip:
    REPX            {paddd x, [vvc_pd_64]}, m0, m1, m2, m3, m4, m5, m6, m7
    REPX            {psrad x, 7}, m0, m1, m2, m3, m4, m5, m6, m7

.transpose:
    punpckhdq       m8, m0, m1
    punpckldq       m0, m0, m1
    punpckhdq       m9, m2, m3
    punpckldq       m1, m2, m3
    punpckhdq       m10, m4, m5
    punpckldq       m2, m4, m5
    punpckhdq       m11, m6, m7
    punpckldq       m3, m6, m7

    punpckhqdq      m5, m0, m1
    punpcklqdq      m0, m0, m1
    punpckhqdq      m4, m8, m9
    punpcklqdq      m1, m8, m9
    punpckhqdq      m12, m2, m3
    punpcklqdq      m8, m2, m3
    punpckhqdq      m13, m10, m11
    punpcklqdq      m9, m10, m11

    vperm2i128      m2, m1, m9, 0x20
    vperm2i128      m6, m1, m9, 0x31
    vperm2i128      m1, m5, m12, 0x20
    vperm2i128      m5, m5, m12, 0x31
    vperm2i128      m7, m4, m13, 0x31
    vperm2i128      m3, m4, m13, 0x20
    vperm2i128      m4, m0, m8, 0x31
    vperm2i128      m0, m0, m8, 0x20

.pass2:
    call            .main

.scale:
    REPX            {paddd x, [vvc_pd_512]}, m0, m1, m2, m3, m4, m5, m6, m7
    REPX            {psrad x, 10}, m0, m1, m2, m3, m4, m5, m6, m7

    STORE_COEFFS    8

    RET

.pass1:
ALIGN function_align
cglobal_label .main
    IDCT2_8         8
    ret
