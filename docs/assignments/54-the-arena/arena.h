#ifndef ARENA_H
#define ARENA_H
#include <stddef.h>

/* A bump ("arena") allocator: hand out memory by advancing one pointer, and
   free it all at once. This is the shape jichi's own memory model is built on
   (jc_mem: a session arena + a per-turn scratch arena). Set D taught you to
   REASON about an arena's lifetime; here you BUILD one. `arena` is opaque --
   you define the struct in arena.c. */
typedef struct arena arena;

/* Create an arena backed by `cap` bytes. NULL on allocation failure. */
arena *arena_new(size_t cap);

/* Hand out `n` bytes from the arena, suitably aligned; NULL if it would not
   fit. The returned memory belongs to the arena -- do not free it individually. */
void *arena_alloc(arena *a, size_t n);

/* Reuse the arena from the start. No per-object free; the old bytes are simply
   available again. */
void arena_reset(arena *a);

/* Release the whole arena (its backing bytes and the arena itself). */
void arena_free(arena *a);

/* Bytes handed out since the last arena_new/arena_reset. */
size_t arena_used(arena *a);

#endif
