/* test_wc.c - TAP tests for wc_words.h. Do not change this file: it covers
 * the reported symptom AND the inputs a symptom-patch quietly ignores. */
#include <stdio.h>
#include "wc_words.h"

static int failures = 0;

static void check(int n, const char *name, int got, int want)
{
    if (got == want) {
        printf("ok %d - %s\n", n, name);
    } else {
        printf("not ok %d - %s: got %d, want %d\n", n, name, got, want);
        failures++;
    }
}

int main(void)
{
    printf("1..5\n");
    check(1, "two plain words", count_words("one two"), 2);
    check(2, "trailing space (the reported symptom)",
          count_words("one two "), 2);
    check(3, "empty string has no words", count_words(""), 0);
    check(4, "double space is one separator", count_words("a  b"), 2);
    check(5, "leading space", count_words(" a"), 1);
    return failures == 0 ? 0 : 1;
}
