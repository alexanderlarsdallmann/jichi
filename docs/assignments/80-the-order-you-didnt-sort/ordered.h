/* ordered.h - an ORDERED map from long keys to long values, built twice.
 *
 * A hash table cannot answer "every key between lo and hi, in order". Two
 * structures can, and this task has you build both to one contract:
 *
 *   sorted.c  a sorted array searched with bsearch -- what jichi does
 *             (qsort in 4 source files, bsearch in 2). Insert must KEEP the
 *             array sorted (find the slot, memmove the tail), which is O(n)
 *             per insert and the cost this task is about.
 *   bst.c     a binary search tree. Unbalanced is ALLOWED and is enough for
 *             the grader; balancing is where the complexity lives and is
 *             reading (DATA_STRUCTURES.md section 5), not grading. But the
 *             bench inserts SORTED input too, and an unbalanced tree turns
 *             into a linked list on it -- you will see that in your numbers,
 *             and MEASURE.md must say so.
 *
 * Both files implement THIS header, with the prefix telling them apart. The
 * grader compiles each against the same probe.
 */
#ifndef ORDERED_H
#define ORDERED_H

#include <stddef.h>

/* A callback for range walks: called once per key in ASCENDING order. */
typedef void (*om_visit_fn)(long key, long value, void *ctx);

/* ---- sorted array ------------------------------------------------------ */
struct sa;
struct sa *sa_new(void);
/* 0 ok, -1 out of memory. An existing key's value is REPLACED (count unchanged). */
int    sa_put(struct sa *m, long key, long value);
/* 1 and *out set when found, 0 when absent. */
int    sa_get(const struct sa *m, long key, long *out);
/* Every key with lo <= key <= hi, ascending, each once. Both bounds INCLUSIVE.
 * Returns how many were visited. An empty range visits nothing and returns 0. */
size_t sa_range(const struct sa *m, long lo, long hi, om_visit_fn fn, void *ctx);
/* 1 and *out set to the smallest / largest key, 0 when the map is empty. */
int    sa_min(const struct sa *m, long *out);
int    sa_max(const struct sa *m, long *out);
size_t sa_count(const struct sa *m);
void   sa_free(struct sa *m);

/* ---- binary search tree ------------------------------------------------ */
struct bst;
struct bst *bst_new(void);
int    bst_put(struct bst *m, long key, long value);
int    bst_get(const struct bst *m, long key, long *out);
size_t bst_range(const struct bst *m, long lo, long hi, om_visit_fn fn, void *ctx);
int    bst_min(const struct bst *m, long *out);
int    bst_max(const struct bst *m, long *out);
size_t bst_count(const struct bst *m);
/* Frees every node. LeakSanitizer judges it. */
void   bst_free(struct bst *m);

/* Deletion is deliberately NOT in this contract. In a sorted array it is a
 * memmove; in a BST it is the operation where the classic mistakes live
 * (two-child nodes, the successor swap), and that is a task of its own. Say
 * in MEASURE.md what you would do about it, and leave it out of the code. */

#endif /* ORDERED_H */
