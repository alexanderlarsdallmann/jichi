/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mem.h - arena (bump) allocator.
 *
 * C89 has no RAII and no garbage collector. An arena makes per-turn memory
 * management trivial: allocate freely during a unit of work, then reset or
 * free the whole arena at once. jichi uses one arena per agent turn.
 *
 * Allocations are never individually freed. jc_arena_reset frees all but the
 * oldest block back to the system and rewinds that one for reuse;
 * jc_arena_free releases everything.
 */
#ifndef JC_MEM_H
#define JC_MEM_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_arena;

/* Create an arena whose blocks are at least `block_size` bytes each.
 * Pass 0 for a sensible default. Returns NULL on OOM. */
struct jc_arena *jc_arena_new(jc_size block_size);

/* Allocate `n` bytes, suitably aligned. Returns NULL only on OOM.
 * A zero-length request returns a valid unique-ish pointer. */
void *jc_arena_alloc(struct jc_arena *a, jc_size n);

/* Allocate and zero `n` bytes. */
void *jc_arena_calloc(struct jc_arena *a, jc_size n);

/* Duplicate a NUL-terminated string into the arena. NULL in -> NULL out. */
char *jc_arena_strdup(struct jc_arena *a, const char *s);

/* Duplicate at most `n` bytes of `s` and NUL-terminate. */
char *jc_arena_strndup(struct jc_arena *a, const char *s, jc_size n);

/* Total bytes handed out across all blocks (M140: the footprint gauge).
 * When `cap_out` is non-NULL it receives the total block capacity malloc'd
 * (used <= cap; the difference is block tails + the reset-retained block).
 * NULL arena => 0. Cheap: walks the block list. */
jc_size jc_arena_used(const struct jc_arena *a, jc_size *cap_out);

/* Free all but the oldest block back to the system; rewind that one. */
void jc_arena_reset(struct jc_arena *a);

/* Release all blocks and the arena itself. Safe on NULL. */
void jc_arena_free(struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_MEM_H */
