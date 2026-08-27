/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fuzz_main.c - in-tree deterministic fuzzer (M123).
 *
 * No dependencies: a seeded PRNG + a mutation loop over each target's seed. Runs
 * a bounded number of iterations per target so it fits `make ci` (hermetic +
 * reproducible), while `make fuzz ITERS=<big>` / a specific TARGET drive longer
 * hunts. Pair with SAN=1 (ASan+UBSan) -- that is what actually catches the bugs.
 *
 * Reproducing a find: rerun with the SAME `JC_FUZZ_SEED` and `TARGET`; the mutation
 * sequence is deterministic, so it recreates the exact crashing input.
 */
#include "jc_fuzz.h"

#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define FUZZ_MAX 4096

/* A committed crash input may exceed the mutation cap, so replay reads up to
 * this much per file. */
#define CORPUS_MAX 1048576L

static unsigned long g_state;

static unsigned long xorshift(void)
{
    /* xorshift32 (deterministic; seeded). */
    unsigned long x = g_state;
    x ^= (x << 13) & 0xFFFFFFFFUL;
    x ^= (x >> 17);
    x ^= (x << 5) & 0xFFFFFFFFUL;
    g_state = x & 0xFFFFFFFFUL;
    return g_state;
}

static unsigned rnd(unsigned n)
{
    return (n == 0) ? 0 : (unsigned)(xorshift() % n);
}

/* Build a mutated buffer from `seed` into `buf` (cap FUZZ_MAX); return length. */
static size_t mutate(const char *seed, unsigned char *buf)
{
    size_t len = 0;
    int ops, k;
    if (seed != NULL) {
        len = strlen(seed);
        if (len > FUZZ_MAX) len = FUZZ_MAX;
        memcpy(buf, seed, len);
    }
    ops = 1 + (int)rnd(6);
    for (k = 0; k < ops; k++) {
        unsigned choice = rnd(6);
        size_t pos;
        if (choice == 0 && len > 0) {            /* bit flip */
            pos = rnd((unsigned)len);
            buf[pos] ^= (unsigned char)(1u << rnd(8));
        } else if (choice == 1 && len > 0) {     /* set byte */
            pos = rnd((unsigned)len);
            buf[pos] = (unsigned char)rnd(256);
        } else if (choice == 2 && len < FUZZ_MAX) { /* insert */
            pos = rnd((unsigned)(len + 1));
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos] = (unsigned char)rnd(256);
            len++;
        } else if (choice == 3 && len > 0) {     /* delete */
            pos = rnd((unsigned)len);
            memmove(buf + pos, buf + pos + 1, len - pos - 1);
            len--;
        } else if (choice == 4 && len > 0) {     /* truncate */
            len = rnd((unsigned)len);
        } else if (choice == 5 && len > 0 && len < FUZZ_MAX / 2) { /* dup region */
            size_t n = 1 + rnd((unsigned)len);
            if (len + n > FUZZ_MAX) n = FUZZ_MAX - len;
            memcpy(buf + len, buf, n);
            len += n;
        }
    }
    return len;
}

/* Replay every file in tests/fuzz/corpus/<target>/ through the target before
 * mutating. That is what turns a past find into a permanent regression: commit
 * the crashing bytes as corpus/<target>/crash-<desc> and every future run
 * re-executes them, no seed/PRNG archaeology needed. Returns the file count. */
static unsigned long replay_corpus(const struct jc_fuzz_target *t,
                                   const char *root)
{
    char path[1024];
    DIR *d;
    struct dirent *e;
    unsigned char *buf;
    unsigned long n = 0;

    if (jc_snprintf(path, sizeof(path), "%s/%s", root, t->name) < 0) return 0;
    d = opendir(path);
    if (d == NULL) return 0; /* no corpus for this target: fine */

    /* One buffer for the whole directory, not one per file: the read buffer is
     * 1 MB and the contents are consumed before the next iteration. */
    buf = (unsigned char *)malloc((size_t)CORPUS_MAX);
    if (buf == NULL) { closedir(d); return 0; }

    while ((e = readdir(d)) != NULL) {
        FILE *f;
        size_t got;
        if (e->d_name[0] == '.') continue;
        if (jc_snprintf(path, sizeof(path), "%s/%s/%s", root, t->name,
                        e->d_name) < 0) continue;
        f = fopen(path, "rb");
        if (f == NULL) continue;
        got = fread(buf, 1, (size_t)CORPUS_MAX, f);
        fclose(f);
        /* Each input is independent, so readdir order cannot affect the
         * verdict -- only which crash reports first. */
        t->fn(buf, got);
        n++;
    }
    free(buf);
    closedir(d);
    return n;
}

static unsigned long env_ulong(const char *name, unsigned long def)
{
    const char *v = getenv(name);
    if (v == NULL || v[0] == '\0') return def;
    return strtoul(v, NULL, 0);
}

int main(int argc, char **argv)
{
    const char *only = (argc > 1 && argv[1][0] != '\0') ? argv[1] : NULL;
    unsigned long seed = env_ulong("JC_FUZZ_SEED", 0x9E3779B9UL);
    unsigned long iters = env_ulong("JC_FUZZ_ITERS", 3000UL);
    unsigned char buf[FUZZ_MAX];
    const char *corpus = getenv("JC_FUZZ_CORPUS");
    int i;
    int ran = 0;

    if (corpus == NULL || corpus[0] == '\0') corpus = "tests/fuzz/corpus";

    if (seed == 0) seed = 1; /* xorshift must not start at 0 */
    printf("fuzz: seed=0x%lx iters=%lu/target%s%s\n", seed, iters,
           only ? " target=" : "", only ? only : "");

    for (i = 0; i < JC_FUZZ_N_TARGETS; i++) {
        const struct jc_fuzz_target *t = &JC_FUZZ_TARGETS[i];
        unsigned long n;
        unsigned long ncorpus;
        if (only != NULL && strcmp(only, t->name) != 0) continue;
        printf("  fuzz: %-14s ", t->name);
        fflush(stdout); /* so a sanitizer abort shows which target */
        g_state = seed;
        /* the committed corpus first: a past find must fail fast, and before
         * any mutation can mask it */
        ncorpus = replay_corpus(t, corpus);
        /* the seed itself, unmutated */
        if (t->seed != NULL) {
            t->fn((const unsigned char *)t->seed, strlen(t->seed));
        }
        /* the empty input */
        t->fn((const unsigned char *)"", 0);
        for (n = 0; n < iters; n++) {
            size_t len = mutate(t->seed, buf);
            t->fn(buf, len);
        }
        /* Report the corpus denominator: a replay count is the difference
         * between "the committed finds passed" and "there were none to run". */
        if (ncorpus > 0) {
            printf("ok (corpus:%lu)\n", ncorpus);
        } else {
            printf("ok\n");
        }
        ran++;
    }

    if (only != NULL && ran == 0) {
        fprintf(stderr, "fuzz: no target named '%s'\n", only);
        return 2;
    }
    printf("fuzz: %d target(s) ok\n", ran);
    return 0;
}
