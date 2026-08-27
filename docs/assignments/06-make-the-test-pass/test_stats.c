/* test_stats.c - TAP-style tests for stats.h. Do not edit as part of the
 * assignment: a fix that changes the test has proven nothing. */
#include <stdio.h>
#include "stats.h"

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
    int a[] = { 3, 5, 8 };
    int b[] = { -5, -2, -9 };
    int c[] = { 7 };

    printf("1..4\n");
    check(1, "max of positives", stats_max(a, 3), 8);
    check(2, "max of negatives", stats_max(b, 3), -2);
    check(3, "max of one", stats_max(c, 1), 7);
    check(4, "sum", stats_sum(a, 3), 16);
    return failures == 0 ? 0 : 1;
}
