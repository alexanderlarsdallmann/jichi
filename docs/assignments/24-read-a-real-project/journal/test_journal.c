/* test_journal.c - the project's own tests. All green. Coverage is the
 * question the assignment asks you to answer by READING, not by trusting
 * the green. */
#include "journal.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int n, const char *name, int ok)
{
    if (ok) {
        printf("ok %d - %s\n", n, name);
    } else {
        printf("not ok %d - %s\n", n, name);
        failures++;
    }
}

int main(void)
{
    struct journal j;
    journal_init(&j);

    printf("1..6\n");
    check(1, "fresh journal is empty", journal_count(&j) == 0);
    check(2, "append succeeds", journal_append(&j, "first note") == 0);
    journal_append(&j, "second note about cats");
    journal_append(&j, "third note");
    check(3, "count follows appends", journal_count(&j) == 3);
    check(4, "get(0) is the oldest",
          strcmp(journal_get(&j, 0), "first note") == 0);
    check(5, "find matches a word",
          strcmp(journal_find(&j, "cats"), "second note about cats") == 0);
    check(6, "find misses politely", journal_find(&j, "dogs") == NULL);
    return failures == 0 ? 0 : 1;
}
