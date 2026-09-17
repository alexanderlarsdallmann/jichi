/* bench.c - the sorted array against the tree, on THIS machine. GIVEN.
 *
 *     cc -std=c89 -O2 -o bench bench.c sorted.c bst.c && ./bench
 *
 * Two workloads, because the answer depends on the workload and on nothing
 * else:
 *
 *   insert-heavy   N puts, then nothing. Random keys AND sorted keys, because
 *                  they are different problems: sorted input is the common
 *                  real-world case (log lines, ids, timestamps), the sorted
 *                  array's memmove is cheap on it (every insert appends), and
 *                  an unbalanced tree degenerates to a linked list on it.
 *   query-heavy    after N random puts, 20,000 range walks over ~1% of the
 *                  keyspace each, and 200,000 point lookups.
 *
 * Prints a markdown table you can paste into MEASURE.md. Read the header of
 * task 78's bench for the rules of the game: -O2 not ASan; run it three times;
 * a row that moves between runs by more than the gap between its columns has
 * measured nothing yet. A row that says "skipped" hit the safety cap -- an
 * unbalanced tree on sorted input past a few tens of thousands of keys is
 * both slow and deep; the cap is there so the bench finishes. That skip IS a
 * result: write it down.
 */
#include "ordered.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define QUERIES  200000L
#define RANGES   20000L
#define SORTED_CAP 20000L     /* the unbalanced-tree safety cap */

static unsigned long lcg(unsigned long *s)
{
    *s = *s * 1103515245UL + 12345UL;
    return (*s >> 8) & 0xffffffUL;
}

static void nop(long k, long v, void *ctx) { (void)k; (void)v; (void)ctx; }

static double ms_since(clock_t t0)
{
    return (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
}

int main(void)
{
    static const long NS[] = { 1000, 4000, 16000, 64000 };
    size_t ni;

    printf("| N | sa insert random ms | bst insert random ms | sa insert sorted ms | bst insert sorted ms | sa 200k gets ms | bst 200k gets ms | sa 20k ranges ms | bst 20k ranges ms |\n");
    printf("|---|---|---|---|---|---|---|---|---|\n");
    for (ni = 0; ni < sizeof NS / sizeof NS[0]; ni++) {
        long n = NS[ni];
        struct sa *sa; struct bst *bst;
        unsigned long seed;
        clock_t t0;
        double sa_ins, bst_ins, sa_sorted, bst_sorted, sa_get_ms, bst_get_ms, sa_rng, bst_rng;
        long i, v, hits = 0;
        int bst_sorted_skipped = (n > SORTED_CAP);

        /* insert-heavy, random keys */
        sa = sa_new(); bst = bst_new();
        if (sa == NULL || bst == NULL) { fprintf(stderr, "out of memory\n"); return 1; }
        seed = 777UL; t0 = clock();
        for (i = 0; i < n; i++) if (sa_put(sa, (long)lcg(&seed), i) != 0) { fprintf(stderr, "sa_put failed (built yet?)\n"); return 1; }
        sa_ins = ms_since(t0);
        seed = 777UL; t0 = clock();
        for (i = 0; i < n; i++) if (bst_put(bst, (long)lcg(&seed), i) != 0) { fprintf(stderr, "bst_put failed (built yet?)\n"); return 1; }
        bst_ins = ms_since(t0);

        /* query-heavy on those random keys */
        seed = 777UL; t0 = clock();
        for (i = 0; i < QUERIES; i++) hits += sa_get(sa, (long)lcg(&seed), &v);
        sa_get_ms = ms_since(t0);
        seed = 777UL; t0 = clock();
        for (i = 0; i < QUERIES; i++) hits += bst_get(bst, (long)lcg(&seed), &v);
        bst_get_ms = ms_since(t0);
        if (hits == 0) {
            fprintf(stderr, "no lookup found anything at N=%ld -- the maps are broken, so their timing means nothing\n", n);
            return 1;
        }
        seed = 999UL; t0 = clock();
        for (i = 0; i < RANGES; i++) { long lo = (long)lcg(&seed); sa_range(sa, lo, lo + 0xffffff / 100, nop, NULL); }
        sa_rng = ms_since(t0);
        seed = 999UL; t0 = clock();
        for (i = 0; i < RANGES; i++) { long lo = (long)lcg(&seed); bst_range(bst, lo, lo + 0xffffff / 100, nop, NULL); }
        bst_rng = ms_since(t0);
        sa_free(sa); bst_free(bst);

        /* insert-heavy, SORTED keys */
        sa = sa_new(); bst = bst_new();
        if (sa == NULL || bst == NULL) { fprintf(stderr, "out of memory\n"); return 1; }
        t0 = clock();
        for (i = 0; i < n; i++) sa_put(sa, i, i);
        sa_sorted = ms_since(t0);
        bst_sorted = 0.0;
        if (!bst_sorted_skipped) {
            t0 = clock();
            for (i = 0; i < n; i++) bst_put(bst, i, i);
            bst_sorted = ms_since(t0);
        }
        sa_free(sa); bst_free(bst);

        printf("| %ld | %.1f | %.1f | %.1f | ", n, sa_ins, bst_ins, sa_sorted);
        if (bst_sorted_skipped) printf("skipped (cap %ld) | ", SORTED_CAP);
        else printf("%.1f | ", bst_sorted);
        printf("%.1f | %.1f | %.1f | %.1f |\n", sa_get_ms, bst_get_ms, sa_rng, bst_rng);
    }
    return 0;
}
