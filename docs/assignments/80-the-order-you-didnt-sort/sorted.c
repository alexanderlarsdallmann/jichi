/* sorted.c - the sorted array, jichi's answer. Yours to build; this stub
 * compiles and holds nothing. Insert must keep the array sorted: find the
 * slot (a binary search), then memmove the tail up by one. That memmove is
 * the whole story of this task -- measure it.
 */
#include "ordered.h"
#include <stdlib.h>

struct sa {
    size_t len;
};

struct sa *sa_new(void)
{
    struct sa *m = (struct sa *)malloc(sizeof *m);
    if (m != NULL) {
        m->len = 0;
    }
    return m;
}

int sa_put(struct sa *m, long key, long value)
{
    (void)m; (void)key; (void)value;
    return -1;
}

int sa_get(const struct sa *m, long key, long *out)
{
    (void)m; (void)key; (void)out;
    return 0;
}

size_t sa_range(const struct sa *m, long lo, long hi, om_visit_fn fn, void *ctx)
{
    (void)m; (void)lo; (void)hi; (void)fn; (void)ctx;
    return 0;
}

int sa_min(const struct sa *m, long *out) { (void)m; (void)out; return 0; }
int sa_max(const struct sa *m, long *out) { (void)m; (void)out; return 0; }

size_t sa_count(const struct sa *m)
{
    return m->len;
}

void sa_free(struct sa *m)
{
    free(m);
}
