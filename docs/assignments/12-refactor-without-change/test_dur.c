/* test_dur.c - TAP tests for dur.h. Do not edit as part of the assignment:
 * the refactor's promise is that these pass untouched. */
#include <stdio.h>
#include "dur.h"

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
    printf("1..8\n");
    check(1, "midnight", hms_to_seconds(0, 0, 0), 0);
    check(2, "one past noon", hms_to_seconds(12, 0, 1), 43201);
    check(3, "last second", hms_to_seconds(23, 59, 59), 86399);
    check(4, "bad hour rejected", hms_to_seconds(24, 0, 0), -1);
    check(5, "remaining at midnight", seconds_remaining(0, 0, 0), 86400);
    check(6, "remaining at last second", seconds_remaining(23, 59, 59), 1);
    check(7, "two days", days_to_seconds(2), 172800);
    check(8, "negative days rejected", days_to_seconds(-1), -1);
    return failures == 0 ? 0 : 1;
}
