/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_vec.h - generic byte-oriented dynamic array.
 *
 * Stores `len` elements of `elem` bytes each in a contiguous heap buffer.
 * Typed access is via casts: ((T *)jc_vec_at(v, i)). Pushing copies the
 * element bytes in by value.
 */
#ifndef JC_VEC_H
#define JC_VEC_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_vec {
    void   *data;
    jc_size len;   /* number of elements */
    jc_size cap;   /* capacity in elements */
    jc_size elem;  /* size of one element in bytes */
};

void      jc_vec_init(struct jc_vec *v, jc_size elem_size);
void      jc_vec_free(struct jc_vec *v);
jc_status jc_vec_reserve(struct jc_vec *v, jc_size n_elems);

/* Copy `elem_size` bytes from `elem` to the back of the vector. */
jc_status jc_vec_push(struct jc_vec *v, const void *elem);

/* Reserve and return a pointer to a new zeroed slot at the back. NULL on OOM.
 * Useful for in-place construction without a temporary. */
void *jc_vec_push_slot(struct jc_vec *v);

/* Pointer to element `i`. No bounds checking beyond an assert-style guard
 * that returns NULL when out of range. */
void *jc_vec_at(struct jc_vec *v, jc_size i);

void jc_vec_clear(struct jc_vec *v);  /* len = 0, keep capacity */

#ifdef __cplusplus
}
#endif
#endif /* JC_VEC_H */
