/* The spec -- do NOT edit it. Make arena.c pass it. Run under AddressSanitizer:
   writing into arena_alloc's result is how a too-loose bounds check gets caught. */
#include "arena.h"
#include <string.h>
#include <stdio.h>

int main(void)
{
    arena *a;
    char *s;
    int *xs;
    int i;
    int ok = 1;

    a = arena_new(1024);
    if (a == NULL) { printf("arena_new returned NULL\n"); return 2; }

    /* two separate allocations must not alias */
    s = (char *)arena_alloc(a, 6);
    if (s == NULL) { printf("arena_alloc returned NULL\n"); return 2; }
    strcpy(s, "hello");

    xs = (int *)arena_alloc(a, 4 * sizeof(int));
    if (xs == NULL) { printf("arena_alloc returned NULL\n"); return 2; }
    for (i = 0; i < 4; i++) xs[i] = i * i;

    if (strcmp(s, "hello") != 0) ok = 0;   /* the first block survived the second */
    if (xs[3] != 9) ok = 0;
    if (arena_used(a) == 0) ok = 0;        /* used advanced */

    /* an allocation that cannot fit must report failure, not overflow */
    if (arena_alloc(a, 100000) != NULL) ok = 0;

    arena_reset(a);
    if (arena_used(a) != 0) ok = 0;        /* reset reclaims everything */

    s = (char *)arena_alloc(a, 8);         /* and the arena is usable again */
    if (s == NULL) ok = 0;

    arena_free(a);
    printf(ok ? "ok\n" : "bad\n");
    return ok ? 0 : 1;
}
