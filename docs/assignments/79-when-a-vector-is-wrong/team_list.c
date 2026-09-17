/* team_list.c - the list. Yours to build; this stub compiles and does
 * nothing. DATA_STRUCTURES.md section 4 ("Building one -- what to get
 * right") is the reading: a dummy head removes the special cases, doubly
 * linked makes removal O(1) given the node, and the interesting axis is
 * intrusive versus external links.
 */
#include "team_list.h"
#include <stdlib.h>

struct team_node {
    struct player p;
};

struct team_list {
    size_t count;
};

struct team_list *team_list_new(void)
{
    struct team_list *t = (struct team_list *)malloc(sizeof *t);
    if (t != NULL) {
        t->count = 0;
    }
    return t;
}

struct team_node *team_list_add(struct team_list *t, const char *name)
{
    (void)t; (void)name;
    return NULL;
}

struct player *team_node_player(struct team_node *n)
{
    return &n->p;
}

struct team_node *team_list_first(struct team_list *t)
{
    (void)t;
    return NULL;
}

struct team_node *team_node_next(struct team_node *n)
{
    (void)n;
    return NULL;
}

size_t team_list_count(const struct team_list *t)
{
    return t->count;
}

int team_list_move(struct team_list *from, struct team_list *to, struct team_node *n)
{
    (void)from; (void)to; (void)n;
    return -1;
}

int team_list_remove(struct team_list *t, struct team_node *n)
{
    (void)t; (void)n;
    return -1;
}

void team_list_free(struct team_list *t)
{
    free(t);
}
