/* notekeeper.c - a tiny request loop with a memory-lifetime bug.
 *
 * The keeper serves "requests": each request is a line of text; the keeper
 * uppercases it, counts the words, and remembers ONLY the running totals
 * (requests served, words seen). Nothing about an individual request needs
 * to outlive that request.
 *
 * The built-in selftest serves the same batch of synthetic requests twice
 * and prints the arena gauge after each batch. A keeper with correct
 * lifetimes holds a FLAT footprint: batch 2 must not cost more than batch 1.
 */
#include "arena.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The keeper's state: totals live as long as the keeper does. */
struct keeper {
    struct arena *lifetime;   /* the keeper's own arena: lives to exit */
    long requests;
    long words;
};

/* Serve one request: uppercase a working copy, count its words, update the
 * totals. The working copy is REQUEST-scoped data -- it is dead the moment
 * this function returns. */
static void serve(struct keeper *k, const char *request)
{
    char *copy = arena_strdup(k->lifetime, request);
    long words = 0;
    int in_word = 0;
    char *p;

    if (copy == NULL) {
        return;
    }
    for (p = copy; *p != '\0'; p++) {
        *p = (char)toupper((unsigned char)*p);
        if (*p == ' ' || *p == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    k->requests++;
    k->words += words;
}

static void batch(struct keeper *k)
{
    char line[128];
    int i;
    for (i = 0; i < 500; i++) {
        sprintf(line, "note %d: the quick brown fox jumps over request %d",
                i, i);
        serve(k, line);
    }
}

int main(int argc, char **argv)
{
    struct keeper k;
    size_t after_batch1;
    size_t after_batch2;

    (void)argc;
    (void)argv;
    k.lifetime = arena_new();
    k.requests = 0;
    k.words = 0;
    if (k.lifetime == NULL) {
        return 1;
    }

    batch(&k);
    after_batch1 = arena_used(k.lifetime);
    batch(&k);
    after_batch2 = arena_used(k.lifetime);

    printf("requests=%ld words=%ld\n", k.requests, k.words);
    printf("arena_after_batch1=%lu\n", (unsigned long)after_batch1);
    printf("arena_after_batch2=%lu\n", (unsigned long)after_batch2);

    arena_free(k.lifetime);
    return 0;
}
