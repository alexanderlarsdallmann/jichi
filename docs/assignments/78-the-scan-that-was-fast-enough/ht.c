/* ht.c - the hash table jichi does not have. Yours to build.
 *
 * This file compiles and satisfies nothing: every function is a stub that
 * says "absent". Replace it. DATA_STRUCTURES.md section 3 ("Building one --
 * what to get right") is the reading; lookup.h is the contract; the grader
 * inserts 5,000 keys into a 64-bucket table, so whatever you build will see
 * collisions on almost every insert.
 */
#include "lookup.h"
#include <stdlib.h>

struct ht {
    size_t nbuckets;
};

struct ht *ht_new(size_t nbuckets)
{
    struct ht *t = (struct ht *)malloc(sizeof *t);
    if (t == NULL) {
        return NULL;
    }
    t->nbuckets = nbuckets;
    return t;
}

int ht_put(struct ht *t, const char *key, long value)
{
    (void)t; (void)key; (void)value;
    return -1;
}

int ht_get(const struct ht *t, const char *key, long *out)
{
    (void)t; (void)key; (void)out;
    return 0;
}

int ht_del(struct ht *t, const char *key)
{
    (void)t; (void)key;
    return 0;
}

size_t ht_count(const struct ht *t)
{
    (void)t;
    return 0;
}

void ht_free(struct ht *t)
{
    free(t);
}
