/* arena.h - a bump allocator with explicit lifetimes.
 *
 * An arena hands out memory fast and frees it all at once: arena_reset()
 * reclaims everything allocated since the last reset, arena_free() destroys
 * the arena. The one design question an arena forces you to answer is the
 * one this task is about: HOW LONG does each piece of data need to live?
 */
#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

struct arena;

struct arena *arena_new(void);
void          arena_free(struct arena *a);

/* Allocate n bytes (never freed individually). NULL on exhaustion. */
void *arena_alloc(struct arena *a, size_t n);

/* Copy a NUL-terminated string into the arena. */
char *arena_strdup(struct arena *a, const char *s);

/* Reclaim everything allocated since creation / the last reset. */
void arena_reset(struct arena *a);

/* Bytes currently handed out (the footprint gauge the test reads). */
size_t arena_used(const struct arena *a);

#endif
