/* team_list.h - the same team as a linked list: nodes with STABLE addresses
 * and an O(1) splice between teams. This is what a vector cannot promise,
 * and it is the only reason to pay a list's price (a node per player, a
 * pointer chase per step, no locality).
 *
 * Yours to build in team_list.c, which starts as a stub.
 */
#ifndef TEAM_LIST_H
#define TEAM_LIST_H

#include <stddef.h>
#include "team_vec.h"     /* struct player */

struct team_list;
struct team_node;         /* opaque; its address never changes while it lives */

struct team_list *team_list_new(void);
/* Appends a player and returns its node, or NULL on out-of-memory. The
 * node's address is valid until the node is removed or the list freed --
 * across ANY number of later adds, in this list or any other. */
struct team_node *team_list_add(struct team_list *t, const char *name);
/* The player inside a node. */
struct player *team_node_player(struct team_node *n);
/* Walking: the first node, and the node after `n` (NULL at the end).
 * Order is order of adding, then of moving. */
struct team_node *team_list_first(struct team_list *t);
struct team_node *team_node_next(struct team_node *n);
size_t team_list_count(const struct team_list *t);
/* Move `n` from `from` to the end of `to`, in O(1): the SAME node, the same
 * address, no copy -- pointers to it stay valid; only its neighbours change.
 * -1 when `n` is not in `from`. */
int  team_list_move(struct team_list *from, struct team_list *to, struct team_node *n);
/* Unlink and free `n`. -1 when `n` is not in `t`. The classic bug lives in
 * the caller who does `for (n = first; n; n = team_node_next(n))
 * team_list_remove(t, n);` -- reading `next` through a node already freed.
 * Your API cannot prevent it; the grader's probe saves `next` first, and
 * ASan is watching the implementation's own frees. */
int  team_list_remove(struct team_list *t, struct team_node *n);
/* Frees the list and every node still in it. */
void team_list_free(struct team_list *t);

#endif /* TEAM_LIST_H */
