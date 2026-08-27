/* wc_words.c - see wc_words.h. */
#include "wc_words.h"

int count_words(const char *s)
{
    int n = 1;
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] == ' ') {
            n++;
        }
    }
    return n;
}
