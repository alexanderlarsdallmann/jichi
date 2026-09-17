/* bst.c - the binary search tree jichi did not write. Yours to build; this
 * stub compiles and holds nothing. Unbalanced is allowed. In-order traversal
 * for the range walk: recursion is fine at these sizes for RANDOM input, but
 * think about what the bench's SORTED input does to your recursion depth --
 * an unbalanced tree on sorted keys is a linked list N deep.
 */
#include "ordered.h"
#include <stdlib.h>

struct bst {
    size_t count;
};

struct bst *bst_new(void)
{
    struct bst *m = (struct bst *)malloc(sizeof *m);
    if (m != NULL) {
        m->count = 0;
    }
    return m;
}

int bst_put(struct bst *m, long key, long value)
{
    (void)m; (void)key; (void)value;
    return -1;
}

int bst_get(const struct bst *m, long key, long *out)
{
    (void)m; (void)key; (void)out;
    return 0;
}

size_t bst_range(const struct bst *m, long lo, long hi, om_visit_fn fn, void *ctx)
{
    (void)m; (void)lo; (void)hi; (void)fn; (void)ctx;
    return 0;
}

int bst_min(const struct bst *m, long *out) { (void)m; (void)out; return 0; }
int bst_max(const struct bst *m, long *out) { (void)m; (void)out; return 0; }

size_t bst_count(const struct bst *m)
{
    return m->count;
}

void bst_free(struct bst *m)
{
    free(m);
}
