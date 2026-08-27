/* test_rot13.c - TAP tests for rot13.h. This file was always right; do not
 * change it as part of the assignment. */
#include <stdio.h>
#include "rot13.h"

static int failures = 0;

static void check(int n, const char *name, int got, int want)
{
    if (got == want) {
        printf("ok %d - %s\n", n, name);
    } else {
        printf("not ok %d - %s: got '%c', want '%c'\n", n, name, got, want);
        failures++;
    }
}

int main(void)
{
    printf("1..5\n");
    check(1, "lowercase rotates", rot13_char('a'), 'n');
    check(2, "lowercase wraps", rot13_char('n'), 'a');
    check(3, "uppercase rotates", rot13_char('A'), 'N');
    check(4, "uppercase wraps", rot13_char('N'), 'A');
    check(5, "non-letters pass through", rot13_char('!'), '!');
    return failures == 0 ? 0 : 1;
}
