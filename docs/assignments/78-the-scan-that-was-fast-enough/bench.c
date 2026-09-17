/* bench.c - time the scan against your table, across N, on THIS machine.
 *
 * GIVEN. Build it beside your ht.c:
 *
 *     cc -std=c89 -O2 -o bench bench.c scan.c ht.c && ./bench
 *
 * It prints a markdown table you can paste into MEASURE.md's "## Results".
 * Read it before you trust it (task 22's lesson: slope lies, keep the peak):
 *
 *   - -O2, not the sanitizer build. ASan slows both sides and not equally.
 *   - Every lookup is for a key that IS present, chosen by a fixed LCG, so the
 *     two sides see the same sequence. Absent keys make the scan look worse
 *     (it walks the whole array) -- if your workload has them, measure them.
 *   - ns/lookup is total time over the same number of lookups at every N, so
 *     the rows are comparable; the number of lookups is chosen so the smallest
 *     N still runs long enough for clock() to see it.
 *   - Run it three times. If the rows move by more than the gap between the
 *     two columns, you have not measured anything yet.
 */
#include "lookup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOOKUPS 300000L

static unsigned long lcg(unsigned long *s)
{
    *s = *s * 1103515245UL + 12345UL;
    return (*s >> 8) & 0xffffffUL;
}

static void make_key(char *buf, long i)
{
    /* "k" + decimal, so keys share a prefix and a length band: strcmp walks
       the shared prefix on every comparison, which is the scan's real cost. */
    buf[0] = 'k';
    sprintf(buf + 1, "%06ld", i);
}

int main(void)
{
    static const long NS[] = { 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 };
    size_t ni;
    char key[32];   /* "k" + 6 digits; wide so the compiler cannot doubt it */

    printf("| N | scan ns/lookup | hash ns/lookup |\n|---|---|---|\n");
    for (ni = 0; ni < sizeof NS / sizeof NS[0]; ni++) {
        long n = NS[ni];
        struct scan_table *st = scan_new();
        struct ht *ht = ht_new(64);
        unsigned long seed = 12345UL;
        long i, hit = 0;
        clock_t t0, t1;
        double scan_ns, hash_ns;
        long v;

        if (st == NULL || ht == NULL) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        for (i = 0; i < n; i++) {
            make_key(key, i);
            if (scan_put(st, key, i) != 0 || ht_put(ht, key, i) != 0) {
                fprintf(stderr, "put failed at N=%ld (is ht_put built yet?)\n", n);
                return 1;
            }
        }

        seed = 12345UL;
        t0 = clock();
        for (i = 0; i < LOOKUPS; i++) {
            make_key(key, (long)(lcg(&seed) % (unsigned long)n));
            hit += scan_get(st, key, &v);
        }
        t1 = clock();
        scan_ns = (double)(t1 - t0) * 1e9 / CLOCKS_PER_SEC / (double)LOOKUPS;

        seed = 12345UL;
        t0 = clock();
        for (i = 0; i < LOOKUPS; i++) {
            make_key(key, (long)(lcg(&seed) % (unsigned long)n));
            hit += ht_get(ht, key, &v);
        }
        t1 = clock();
        hash_ns = (double)(t1 - t0) * 1e9 / CLOCKS_PER_SEC / (double)LOOKUPS;

        if (hit != 2 * LOOKUPS) {
            fprintf(stderr, "N=%ld: %ld of %ld lookups found -- the table is "
                    "wrong, so its timing means nothing\n", n, hit, 2 * LOOKUPS);
            return 1;
        }
        printf("| %ld | %.1f | %.1f |\n", n, scan_ns, hash_ns);
        scan_free(st);
        ht_free(ht);
    }
    return 0;
}
