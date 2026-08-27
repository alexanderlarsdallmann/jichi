/* wc_words.h */
#ifndef WC_WORDS_H
#define WC_WORDS_H

/* Number of words in s: maximal runs of non-space characters. The empty
 * string has zero words; repeated spaces separate the same two words. */
int count_words(const char *s);

#endif /* WC_WORDS_H */
