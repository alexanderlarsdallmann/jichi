/* stats.c - see wordtool.h. */
#include "wordtool.h"

long wt_count_words(const char *text)
{
    long n = 0;
    int in_word = 0;
    const char *p;
    for (p = text; *p != '\0'; p++) {
        if (*p == ' ' || *p == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            n++;
        }
    }
    return n;
}

long wt_longest_word(const char *text)
{
    long best = 0;
    long cur = 0;
    const char *p;
    for (p = text; ; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\0') {
            if (cur > best) {
                best = cur;
            }
            cur = 0;
            if (*p == '\0') {
                break;
            }
        } else {
            cur++;
        }
    }
    return best;
}
