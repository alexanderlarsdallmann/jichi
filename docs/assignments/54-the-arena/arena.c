#include "arena.h"
#include <stddef.h>

/* TODO: implement the arena. test_arena.c is the spec -- do NOT edit it, make
   your implementation pass it, then leave a one-line DESIGN.md naming the shape.

   Sketch:
     struct arena { char *base; size_t cap; size_t off; };
     - arena_new:   malloc the struct + a `cap`-byte backing block.
     - arena_alloc: round `off` UP to an alignment (sizeof(void *) is safe),
                    bounds-check `aligned + n <= cap`, advance `off`, return
                    base + aligned.
     - arena_reset: set off back to 0.
     - arena_free:  free the backing block and the struct.
     - arena_used:  return off.

   This stub compiles and links so the suite builds, but every call fails. */

struct arena { int placeholder; };

arena *arena_new(size_t cap) { (void)cap; return NULL; }
void  *arena_alloc(arena *a, size_t n) { (void)a; (void)n; return NULL; }
void   arena_reset(arena *a) { (void)a; }
void   arena_free(arena *a) { (void)a; }
size_t arena_used(arena *a) { (void)a; return 0; }
