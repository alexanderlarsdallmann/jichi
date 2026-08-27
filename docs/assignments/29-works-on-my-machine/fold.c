/* fold.c - a rolling hash over a list of ids (the classic acc*31 + x).
 * It compiles clean, runs, and prints a plausible number -- at -O0. The
 * bug is not a crash you can see; it is UNDEFINED BEHAVIOUR the standard
 * gives no meaning: the accumulator is a signed int and it overflows.
 * An optimizing compiler is free to exploit that; a sanitizer catches it
 * red-handed. The wrap-around this hash *wants* is only defined for
 * UNSIGNED arithmetic. Find it, prove it, fix it.
 */
#include <stdio.h>

/* Fold each id into a running hash. Overflow (wrap-around) is the intended
 * behaviour of a hash -- but signed overflow is undefined, not wrap-around. */
static int fold(const int *v, int n)
{
    int acc = 0;
    int i;
    for (i = 0; i < n; i++) {
        acc = acc * 31 + v[i]; /* signed overflow == undefined behaviour */
    }
    return acc;
}

int main(void)
{
    int ids[6];
    ids[0] = 1000003; ids[1] = 999983; ids[2] = 100000007;
    ids[3] = 2000000011; ids[4] = 1500000001; ids[5] = 777777773;
    printf("hash = %u\n", (unsigned int)fold(ids, 6));
    return 0;
}
