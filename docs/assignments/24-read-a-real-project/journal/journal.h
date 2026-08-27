/* journal.h - an append-only note journal, bounded by trimming.
 *
 * Notes are kept oldest-first. journal_trim(j, keep) discards the OLDEST
 * notes until at most `keep` remain -- the newest `keep` notes survive.
 */
#ifndef JOURNAL_H
#define JOURNAL_H

#include <stddef.h>

#define JOURNAL_MAX 64
#define NOTE_MAX    80

struct journal {
    char   notes[JOURNAL_MAX][NOTE_MAX];
    size_t count;
};

void   journal_init(struct journal *j);
int    journal_append(struct journal *j, const char *text);   /* 0 ok */
size_t journal_count(const struct journal *j);
/* Index 0 is the OLDEST surviving note. NULL if out of range. */
const char *journal_get(const struct journal *j, size_t i);
/* First note containing `word`, oldest first; NULL if none. */
const char *journal_find(const struct journal *j, const char *word);
/* Keep the newest `keep` notes, discard the rest. keep >= count is a no-op. */
void   journal_trim(struct journal *j, size_t keep);

#endif
