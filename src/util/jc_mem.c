/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mem.c - arena allocator (see jc_mem.h). */

#include "jc_mem.h"
#include "jc_fault.h"
#include <stdlib.h>
#include <string.h>

#define JC_ARENA_DEFAULT_BLOCK 8192

/* Alignment unit: a union of the widest common types. */
union jc_align {
    long   l;
    double d;
    void  *p;
};
#define JC_ALIGN (sizeof(union jc_align))

struct jc_block {
    struct jc_block *next;
    jc_size cap;   /* usable bytes in `data` */
    jc_size used;  /* bytes handed out       */
    char   *data;
};

struct jc_arena {
    struct jc_block *head;  /* current block (most recently allocated) */
    jc_size block_size;     /* default size for new blocks             */
};

static struct jc_block *block_new(jc_size cap)
{
    struct jc_block *b;
    if (JC_FAULT_HIT(JC_FAULT_ALLOC)) {
        return NULL; /* M198: simulated allocation failure */
    }
    b = (struct jc_block *)malloc(sizeof(struct jc_block));
    if (b == NULL) {
        return NULL;
    }
    b->data = (char *)malloc(cap);
    if (b->data == NULL) {
        free(b);
        return NULL;
    }
    b->next = NULL;
    b->cap = cap;
    b->used = 0;
    return b;
}

struct jc_arena *jc_arena_new(jc_size block_size)
{
    struct jc_arena *a;
    if (block_size == 0) {
        block_size = JC_ARENA_DEFAULT_BLOCK;
    }
    a = (struct jc_arena *)malloc(sizeof(struct jc_arena));
    if (a == NULL) {
        return NULL;
    }
    a->head = block_new(block_size);
    if (a->head == NULL) {
        free(a);
        return NULL;
    }
    a->block_size = block_size;
    return a;
}

void *jc_arena_alloc(struct jc_arena *a, jc_size n)
{
    struct jc_block *b;
    jc_size aligned;
    void *p;

    /* Round the request up to the alignment unit (and never return 0 bytes). */
    aligned = (n + (JC_ALIGN - 1)) & ~(JC_ALIGN - 1);
    if (aligned == 0) {
        aligned = JC_ALIGN;
    }

    b = a->head;
    if (b->used + aligned > b->cap) {
        /* Need a fresh block. Oversized requests get a dedicated block. */
        jc_size cap = a->block_size;
        struct jc_block *nb;
        if (aligned > cap) {
            cap = aligned;
        }
        nb = block_new(cap);
        if (nb == NULL) {
            return NULL;
        }
        nb->next = a->head;
        a->head = nb;
        b = nb;
    }

    p = b->data + b->used;
    b->used += aligned;
    return p;
}

void *jc_arena_calloc(struct jc_arena *a, jc_size n)
{
    void *p = jc_arena_alloc(a, n);
    if (p != NULL && n > 0) {
        memset(p, 0, n);
    }
    return p;
}

char *jc_arena_strdup(struct jc_arena *a, const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    return jc_arena_strndup(a, s, strlen(s));
}

char *jc_arena_strndup(struct jc_arena *a, const char *s, jc_size n)
{
    char *p;
    if (s == NULL) {
        return NULL;
    }
    p = (char *)jc_arena_alloc(a, n + 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

jc_size jc_arena_used(const struct jc_arena *a, jc_size *cap_out)
{
    const struct jc_block *b;
    jc_size used = 0;
    jc_size cap = 0;
    if (a != NULL) {
        for (b = a->head; b != NULL; b = b->next) {
            used += b->used;
            cap += b->cap;
        }
    }
    if (cap_out != NULL) {
        *cap_out = cap;
    }
    return used;
}

void jc_arena_reset(struct jc_arena *a)
{
    struct jc_block *b;
    /* Free all but the last (oldest) block, then rewind that one. */
    while (a->head->next != NULL) {
        b = a->head;
        a->head = b->next;
        free(b->data);
        free(b);
    }
    a->head->used = 0;
}

void jc_arena_free(struct jc_arena *a)
{
    struct jc_block *b;
    if (a == NULL) {
        return;
    }
    b = a->head;
    while (b != NULL) {
        struct jc_block *next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    free(a);
}
