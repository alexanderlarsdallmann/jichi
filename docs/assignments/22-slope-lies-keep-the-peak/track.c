/* track.c - see track.h. Sized free keeps this C89-simple: the caller
 * already knows its sizes, so no hidden header block is needed. */
#include "track.h"

#include <stdlib.h>
#include <string.h>

static size_t live = 0;
static size_t peak = 0;

static void bump(size_t n)
{
    live += n;
    if (live > peak) {
        peak = live;
    }
}

void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (p != NULL) {
        bump(n);
    }
    return p;
}

void *xrealloc(void *p, size_t old_n, size_t new_n)
{
    void *np = realloc(p, new_n);
    if (np != NULL) {
        live -= old_n;
        bump(new_n);
    }
    return np;
}

void xfree(void *p, size_t n)
{
    if (p != NULL) {
        live -= n;
    }
    free(p);
}

size_t track_live(void)
{
    return live;
}

size_t track_peak(void)
{
    return peak;
}
