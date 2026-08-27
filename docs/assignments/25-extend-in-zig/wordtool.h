/* wordtool.h - text statistics. The implementation language of each
 * function is an implementation detail behind this C header -- which is
 * exactly the point of this track. */
#ifndef WORDTOOL_H
#define WORDTOOL_H

long wt_count_words(const char *text);
long wt_longest_word(const char *text);

#endif
