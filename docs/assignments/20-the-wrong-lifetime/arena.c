/* arena.c - see arena.h. Fixed-block bump allocator; blocks are chained and
 * all but the first are released on reset (the jichi jc_mem shape, shrunk). */
#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 4096

struct block {
    struct block *next;
    size_t used;
    size_t cap;
    /* payload follows */
};

struct arena {
    struct block *head;
    size_t handed_out;
};

static struct block *block_new(size_t payload)
{
    struct block *b;
    if (payload < BLOCK_SIZE) {
        payload = BLOCK_SIZE;
    }
    b = (struct block *)malloc(sizeof(*b) + payload);
    if (b == NULL) {
        return NULL;
    }
    b->next = NULL;
    b->used = 0;
    b->cap = payload;
    return b;
}

struct arena *arena_new(void)
{
    struct arena *a = (struct arena *)malloc(sizeof(*a));
    if (a == NULL) {
        return NULL;
    }
    a->head = block_new(0);
    a->handed_out = 0;
    if (a->head == NULL) {
        free(a);
        return NULL;
    }
    return a;
}

void arena_free(struct arena *a)
{
    struct block *b;
    if (a == NULL) {
        return;
    }
    b = a->head;
    while (b != NULL) {
        struct block *next = b->next;
        free(b);
        b = next;
    }
    free(a);
}

void *arena_alloc(struct arena *a, size_t n)
{
    struct block *b = a->head;
    if (n > b->cap - b->used) {
        struct block *nb = block_new(n);
        if (nb == NULL) {
            return NULL;
        }
        nb->next = a->head;
        a->head = nb;
        b = nb;
    }
    b->used += n;
    a->handed_out += n;
    return (char *)(b + 1) + (b->used - n);
}

char *arena_strdup(struct arena *a, const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)arena_alloc(a, n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

void arena_reset(struct arena *a)
{
    struct block *b = a->head;
    while (b->next != NULL) {
        struct block *next = b->next;
        free(b);
        b = next;
    }
    b->used = 0;
    a->head = b;
    a->handed_out = 0;
}

size_t arena_used(const struct arena *a)
{
    return a->handed_out;
}
