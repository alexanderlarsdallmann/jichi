/* team_vec.c - a team in a growable array. It remembers the captain by
 * address, which is the whole bug: realloc() is allowed to move the array,
 * and after the move that address points into freed memory. The program
 * keeps working until the day the array happens to move -- then it reads
 * garbage, or, under AddressSanitizer, stops on the spot and tells you.
 */
#include "team_vec.h"
#include <stdlib.h>
#include <string.h>

struct team_vec {
    struct player *items;
    size_t len;
    size_t cap;
    struct player *captain;     /* the bug: a pointer into `items` */
};

struct team_vec *team_vec_new(void)
{
    struct team_vec *t = (struct team_vec *)malloc(sizeof *t);
    if (t == NULL) {
        return NULL;
    }
    t->items = NULL;
    t->len = 0;
    t->cap = 0;
    t->captain = NULL;
    return t;
}

int team_vec_add(struct team_vec *t, const char *name)
{
    if (t->len == t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 4;
        struct player *n = (struct player *)realloc(t->items, ncap * sizeof *n);
        if (n == NULL) {
            return -1;
        }
        t->items = n;               /* moved -- and t->captain did not follow */
        t->cap = ncap;
    }
    strncpy(t->items[t->len].name, name, sizeof t->items[t->len].name - 1);
    t->items[t->len].name[sizeof t->items[t->len].name - 1] = '\0';
    t->items[t->len].goals = 0;
    t->len++;
    return 0;
}

struct player *team_vec_at(struct team_vec *t, size_t i)
{
    return i < t->len ? &t->items[i] : NULL;
}

size_t team_vec_count(const struct team_vec *t)
{
    return t->len;
}

int team_vec_set_captain(struct team_vec *t, size_t i)
{
    if (i >= t->len) {
        return -1;
    }
    t->captain = &t->items[i];
    return 0;
}

struct player *team_vec_captain(struct team_vec *t)
{
    return t->captain;
}

void team_vec_free(struct team_vec *t)
{
    if (t != NULL) {
        free(t->items);
        free(t);
    }
}
