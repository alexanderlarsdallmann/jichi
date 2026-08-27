/* bytesum.c - sum the raw byte values of a buffer.
 * It compiles with zero warnings under strict C89 -Werror, runs, and prints
 * a number. On your machine it prints one answer; on a Raspberry Pi it prints
 * a DIFFERENT one -- from the same source, same standard, no undefined
 * behaviour. The culprit is IMPLEMENTATION-DEFINED behaviour: whether `char`
 * is signed is left to the compiler (signed on x86, unsigned on most ARM).
 * Nothing traps; the program is simply not portable. Find it, prove it, fix
 * it so the answer is the same everywhere -- and correct.
 */
#include <stdio.h>

/* Each element of `data` is a raw byte, meant as 0..255. */
static long byte_sum(const char *data, int n)
{
    long total = 0;
    int i;
    for (i = 0; i < n; i++) {
        total += data[i]; /* a byte >= 0x80 sign-extends where char is signed */
    }
    return total;
}

int main(void)
{
    /* Eight raw bytes; four have the high bit set (0x80..0xFF). */
    static const unsigned char raw[8] = { 1, 128, 255, 127, 192, 64, 254, 2 };
    printf("byte sum = %ld\n", byte_sum((const char *)raw, 8));
    return 0;
}
