/* team_vec.h - a team of players in a growable array, the jichi way.
 *
 * The contract the implementation must honour. The pristine team_vec.c
 * breaks the one rule that matters here: it remembers the captain as a
 * POINTER into the array, and the array moves when it grows.
 */
#ifndef TEAM_VEC_H
#define TEAM_VEC_H

#include <stddef.h>

struct player {
    char name[32];
    long goals;
};

struct team_vec;

struct team_vec *team_vec_new(void);
/* Appends a player. 0 on success, -1 on out-of-memory. The array may be
 * REALLOCATED by this call: every pointer previously obtained from
 * team_vec_at() or team_vec_captain() may now dangle. Hold an INDEX. */
int  team_vec_add(struct team_vec *t, const char *name);
/* The player at position `i` (0-based, in order of adding), or NULL when
 * `i` is out of range. The pointer is valid until the next team_vec_add. */
struct player *team_vec_at(struct team_vec *t, size_t i);
size_t team_vec_count(const struct team_vec *t);
/* Make the player at `i` captain. -1 when `i` is out of range. */
int  team_vec_set_captain(struct team_vec *t, size_t i);
/* The captain, or NULL when none was set -- CORRECT after any number of
 * later adds. The pointer is valid until the next team_vec_add: the
 * implementation must therefore remember WHICH player is captain (an
 * index), never WHERE they were (a pointer), and look the address up on
 * every call. */
struct player *team_vec_captain(struct team_vec *t);
void team_vec_free(struct team_vec *t);

#endif /* TEAM_VEC_H */
