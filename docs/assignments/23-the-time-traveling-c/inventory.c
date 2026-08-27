// inventory.c - a small stock report, written in comfortable modern C.
// It compiles fine as C99/C11. Your job is to move it to 1989 -- same
// behavior, same output, -std=c89 -pedantic -Wall -Wextra -Werror clean.
#include <stdio.h>
#include <string.h>

struct item {
    const char *name;
    long count;
    long unit_cents;
};

static struct item stock[] = {
    { .name = "bolt",   .count = 1200, .unit_cents = 3 },
    { .name = "washer", .count = 4000, .unit_cents = 1 },
    { .name = "plate",  .count = 15,   .unit_cents = 950 },
};

static long long stock_value_cents(void)
{
    long long total = 0;
    for (int i = 0; i < 3; i++) {
        total += (long long)stock[i].count * stock[i].unit_cents;
    }
    return total;
}

static void report_line(char *out, size_t cap, const struct item *it)
{
    snprintf(out, cap, "%-8s x%-5ld @%4ld = %8ld",
             it->name, it->count, it->unit_cents,
             it->count * it->unit_cents);
}

int main(void)
{
    puts("stock report");
    for (int i = 0; i < 3; i++) {
        char line[64];
        report_line(line, sizeof(line), &stock[i]);
        puts(line);
    }
    long long value = stock_value_cents();
    printf("total value: %ld cents\n", (long)value);
    return 0;
}
