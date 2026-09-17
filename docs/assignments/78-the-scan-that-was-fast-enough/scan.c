/* scan.c - the baseline, GIVEN and correct. Do not edit.
 *
 * A contiguous array of (key, value) pairs searched front to back with
 * strcmp. This is jc_tool_registry_find's shape (src/tools/jc_tool.c):
 * jichi resolves every tool call this way, over N = 17, and the page
 * DATA_STRUCTURES.md argues that at that N this is not a compromise but the
 * fast answer -- no hash to compute, no bucket to chase, a couple of cache
 * lines. Your job in ht.c is to build the alternative; your job in
 * MEASURE.md is to find out where, on YOUR machine, that argument stops
 * being true.
 */
#include "lookup.h"
#include <stdlib.h>
#include <string.h>

struct scan_entry {
    char *key;
    long  value;
};

struct scan_table {
    struct scan_entry *items;
    size_t len;
    size_t cap;
};

struct scan_table *scan_new(void)
{
    struct scan_table *t = (struct scan_table *)malloc(sizeof *t);
    if (t == NULL) {
        return NULL;
    }
    t->items = NULL;
    t->len = 0;
    t->cap = 0;
    return t;
}

static struct scan_entry *scan_find(const struct scan_table *t, const char *key)
{
    size_t i;
    for (i = 0; i < t->len; i++) {
        if (strcmp(t->items[i].key, key) == 0) {
            return &t->items[i];
        }
    }
    return NULL;
}

int scan_put(struct scan_table *t, const char *key, long value)
{
    struct scan_entry *e = scan_find(t, key);
    char *copy;
    if (e != NULL) {
        e->value = value;
        return 0;
    }
    if (t->len == t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 16;
        struct scan_entry *n =
            (struct scan_entry *)realloc(t->items, ncap * sizeof *n);
        if (n == NULL) {
            return -1;
        }
        t->items = n;
        t->cap = ncap;
    }
    copy = (char *)malloc(strlen(key) + 1);
    if (copy == NULL) {
        return -1;
    }
    strcpy(copy, key);
    t->items[t->len].key = copy;
    t->items[t->len].value = value;
    t->len++;
    return 0;
}

int scan_get(const struct scan_table *t, const char *key, long *out)
{
    struct scan_entry *e = scan_find(t, key);
    if (e == NULL) {
        return 0;
    }
    *out = e->value;
    return 1;
}

int scan_del(struct scan_table *t, const char *key)
{
    struct scan_entry *e = scan_find(t, key);
    if (e == NULL) {
        return 0;
    }
    free(e->key);
    /* Order is not part of the contract: move the last entry into the hole. */
    *e = t->items[t->len - 1];
    t->len--;
    return 1;
}

size_t scan_count(const struct scan_table *t)
{
    return t->len;
}

void scan_free(struct scan_table *t)
{
    size_t i;
    if (t == NULL) {
        return;
    }
    for (i = 0; i < t->len; i++) {
        free(t->items[i].key);
    }
    free(t->items);
    free(t);
}
