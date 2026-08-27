/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_vec.c - generic dynamic array (see jc_vec.h). */

#include "jc_vec.h"
#include <stdlib.h>
#include <string.h>

void jc_vec_init(struct jc_vec *v, jc_size elem_size)
{
    v->data = NULL;
    v->len = 0;
    v->cap = 0;
    v->elem = elem_size;
}

void jc_vec_free(struct jc_vec *v)
{
    free(v->data);
    v->data = NULL;
    v->len = 0;
    v->cap = 0;
}

void jc_vec_clear(struct jc_vec *v)
{
    v->len = 0;
}

jc_status jc_vec_reserve(struct jc_vec *v, jc_size n_elems)
{
    jc_size newcap;
    void *p;
    if (n_elems <= v->cap) {
        return JC_OK;
    }
    newcap = (v->cap == 0) ? 8 : v->cap;
    while (newcap < n_elems) {
        newcap *= 2;
    }
    p = realloc(v->data, newcap * v->elem);
    if (p == NULL) {
        return JC_ERR_OOM;
    }
    v->data = p;
    v->cap = newcap;
    return JC_OK;
}

jc_status jc_vec_push(struct jc_vec *v, const void *elem)
{
    jc_status st = jc_vec_reserve(v, v->len + 1);
    if (st != JC_OK) {
        return st;
    }
    memcpy((char *)v->data + v->len * v->elem, elem, v->elem);
    v->len++;
    return JC_OK;
}

void *jc_vec_push_slot(struct jc_vec *v)
{
    void *slot;
    if (jc_vec_reserve(v, v->len + 1) != JC_OK) {
        return NULL;
    }
    slot = (char *)v->data + v->len * v->elem;
    memset(slot, 0, v->elem);
    v->len++;
    return slot;
}

void *jc_vec_at(struct jc_vec *v, jc_size i)
{
    if (i >= v->len) {
        return NULL;
    }
    return (char *)v->data + i * v->elem;
}
