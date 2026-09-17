/* lookup.h - two ways to find a value by string key.
 *
 * scan_*  is GIVEN and correct: a contiguous array searched front to back
 *         with strcmp -- the shape of jichi's jc_tool_registry_find, whose N
 *         is 17. Do not edit scan.c; it is the baseline you measure against.
 * ht_*    is YOURS to build in ht.c: a hash table with the same contract.
 *
 * The contract below is what the grader checks. Read it twice; the failures
 * it names are the ones a first hash table usually has.
 */
#ifndef LOOKUP_H
#define LOOKUP_H

#include <stddef.h>

/* --- the baseline: a linear scan over a contiguous array (given) ----------- */

struct scan_table;

struct scan_table *scan_new(void);
/* 0 on success, -1 on out-of-memory. An existing key's value is REPLACED. */
int  scan_put(struct scan_table *t, const char *key, long value);
/* 1 and *out set when found, 0 when absent. */
int  scan_get(const struct scan_table *t, const char *key, long *out);
/* 1 when a key was removed, 0 when it was absent. */
int  scan_del(struct scan_table *t, const char *key);
size_t scan_count(const struct scan_table *t);
void scan_free(struct scan_table *t);

/* --- yours: a hash table with the same contract --------------------------- */

struct ht;

/* `nbuckets` is the number of buckets to START with. The grader creates a
 * table of 64 buckets and inserts 5,000 keys into it: collisions are not an
 * edge case here, they are the normal case, and the table must stay correct
 * whether or not you choose to grow it. Growing (rehashing past a load
 * factor) is allowed and is not required. */
struct ht *ht_new(size_t nbuckets);

/* 0 on success, -1 on out-of-memory. An existing key's value is REPLACED,
 * and the table's count does not change.
 *
 * The table OWNS A COPY of the key. The caller may overwrite or free its
 * buffer the moment ht_put returns; a table that stored the pointer will
 * answer later lookups from memory it does not own. The grader mutates the
 * buffer it passed and then asks for the key again. */
int  ht_put(struct ht *t, const char *key, long value);

/* 1 and *out set when found, 0 when absent. Never allocates. */
int  ht_get(const struct ht *t, const char *key, long *out);

/* 1 when a key was removed, 0 when it was absent. After ht_del(k):
 *   - ht_get(k) is 0;
 *   - EVERY OTHER KEY is still found. Under open addressing a blanked slot
 *     breaks the probe chain that ran through it, so deletion needs a
 *     tombstone, a backward shift, or a rehash. Chaining has no such trap.
 *     State which you chose, and why, in MEASURE.md's "## Deletion" section;
 *   - ht_put(k) again works, and is found. */
int  ht_del(struct ht *t, const char *key);

/* Live keys (puts that added minus dels that removed). */
size_t ht_count(const struct ht *t);

/* Frees the table and every key it copied. The grader runs under
 * LeakSanitizer: a key you copied and did not free is reported. */
void ht_free(struct ht *t);

#endif /* LOOKUP_H */
