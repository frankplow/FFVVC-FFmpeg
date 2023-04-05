/*
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

#ifndef COMPAT_ATOMICS_WIN32_STDATOMIC_H
#define COMPAT_ATOMICS_WIN32_STDATOMIC_H

#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define ATOMIC_FLAG_INIT 0

#define ATOMIC_VAR_INIT(value) (value)

#define atomic_init(obj, value) \
do {                            \
    *(obj) = (value);           \
} while(0)

#define kill_dependency(y) ((void)0)

#define atomic_thread_fence(order) \
    MemoryBarrier();

#define atomic_signal_fence(order) \
    ((void)0)

#define atomic_is_lock_free(obj) 0

typedef          int       atomic_int;
typedef unsigned int       atomic_uint;
typedef          long      atomic_long;
typedef unsigned long      atomic_ulong;
typedef          long long atomic_llong;
typedef unsigned long long atomic_ullong;
typedef      int_least32_t atomic_int_least32_t;
typedef     uint_least32_t atomic_uint_least32_t;
typedef      int_least64_t atomic_int_least64_t;
typedef     uint_least64_t atomic_uint_least64_t;
typedef       int_fast32_t atomic_int_fast32_t;
typedef      uint_fast32_t atomic_uint_fast32_t;
typedef       int_fast64_t atomic_int_fast64_t;
typedef      uint_fast64_t atomic_uint_fast64_t;
typedef           intptr_t atomic_intptr_t;
typedef          uintptr_t atomic_uintptr_t;
typedef             size_t atomic_size_t;
typedef          ptrdiff_t atomic_ptrdiff_t;
typedef           intmax_t atomic_intmax_t;
typedef          uintmax_t atomic_uintmax_t;

#define atomic_store(object, desired)   \
do {                                    \
    *(object) = (desired);              \
    MemoryBarrier();                    \
} while (0)

#define atomic_store_explicit(object, desired, order) \
    atomic_store(object, desired)

#define atomic_load(object) \
    (MemoryBarrier(), *(object))

#define atomic_load_explicit(object, order) \
    atomic_load(object)

#define atomic_helper(operation, object, ...)                  \
    (sizeof(*object) == 4 ?                                    \
        operation((volatile LONG *) object, __VA_ARGS__)       \
    : sizeof(*object) == 8 ?                                   \
        operation##64((volatile LONG64 *) object, __VA_ARGS__) \
    : (abort(), 0))

#define atomic_exchange(object, desired) \
    atomic_helper(InterlockedExchange, object, desired)

#define atomic_exchange_explicit(object, desired, order) \
    atomic_exchange(object, desired)

#define atomic_compare_exchange_strong(object, expected, desired) \
    atomic_helper(InterlockedCompareExchange, object, desired, *expected)

#define atomic_compare_exchange_strong_explicit(object, expected, desired, success, failure) \
    atomic_compare_exchange_strong(object, expected, desired)

#define atomic_compare_exchange_weak(object, expected, desired) \
    atomic_compare_exchange_strong(object, expected, desired)

#define atomic_compare_exchange_weak_explicit(object, expected, desired, success, failure) \
    atomic_compare_exchange_weak(object, expected, desired)

#define atomic_fetch_add(object, operand) \
    atomic_helper(InterlockedExchangeAdd, object, operand)

#define atomic_fetch_sub(object, operand)    \
    atomic_fetch_add(object, -(operand))

#define atomic_fetch_or(object, operand) \
    atomic_helper(InterlockedOr, object, operand)

#define atomic_fetch_xor(object, operand) \
    atomic_helper(InterlockedXor, object, operand)

#define atomic_fetch_and(object, operand) \
    atomic_helper(InterlockedAnd, object, operand)

#define atomic_fetch_add_explicit(object, operand, order) \
    atomic_fetch_add(object, operand)

#define atomic_fetch_sub_explicit(object, operand, order) \
    atomic_fetch_sub(object, operand)

#define atomic_fetch_or_explicit(object, operand, order) \
    atomic_fetch_or(object, operand)

#define atomic_fetch_xor_explicit(object, operand, order) \
    atomic_fetch_xor(object, operand)

#define atomic_fetch_and_explicit(object, operand, order) \
    atomic_fetch_and(object, operand)

#define atomic_flag_test_and_set(object) \
    atomic_exchange(object, 1)

#define atomic_flag_test_and_set_explicit(object, order) \
    atomic_flag_test_and_set(object)

#define atomic_flag_clear(object) \
    atomic_store(object, 0)

#define atomic_flag_clear_explicit(object, order) \
    atomic_flag_clear(object)

#endif /* COMPAT_ATOMICS_WIN32_STDATOMIC_H */
