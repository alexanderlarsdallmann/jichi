/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fuzz_libfuzzer.c - libFuzzer entry point (FUZZ=1). See jc_fuzz.h.
 *
 * The deterministic in-tree driver (jc_fuzz_main.c) is what `make ci` runs: it
 * is hermetic, seeded, and needs no toolchain beyond the project's own. It is
 * also a blind mutator -- it cannot see which branches an input reached, so on a
 * structured input (JSON, SSE framing, YAML frontmatter, LSP headers) it spends
 * most of its budget on bytes that die at the first parse check. Coverage
 * feedback is what gets past that wall, and it is the whole reason to have this.
 *
 * What it is NOT, stated because the tempting claim is wrong: it would not have
 * caught M269's broken path-fence boundary check. That break survived 20,000
 * blind iterations because the ASSERTION could not reach it -- mutating a
 * relative seed never synthesizes the absolute root prefix -- and coverage
 * guidance would not have synthesized that prefix either. The fix was rewriting
 * the property to glue the fuzz bytes onto the root itself. No fuzzer, however
 * guided, finds a bug the target never asks about.
 *
 * So this is the opt-in depth pass, never part of ci: one binary, the target
 * chosen by JC_FUZZ_TARGET, reusing the SAME registry the deterministic driver
 * walks -- a target written once is fuzzed both ways, and a find from either can
 * be committed to tests/fuzz/corpus/<target>/ where the deterministic runner
 * replays it forever.
 *
 * Why one env-selected binary rather than one binary per target: libFuzzer's own
 * CLI already owns the corpus directory, iteration count and timeout knobs, so
 * the only thing left to choose is the target -- and N link rules for N targets
 * would be Makefile churn for nothing.
 *
 *   make libfuzz TARGET=prop_pathfence
 */
#include "jc_fuzz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libFuzzer's contract. Declared here (not in jc_fuzz.h) because nothing in the
 * project calls it -- the fuzzer runtime does. */
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t len);

static jc_fuzz_fn g_fn = NULL;

static void resolve_target(void)
{
    const char *want = getenv("JC_FUZZ_TARGET");
    int i;

    if (want != NULL && want[0] != '\0') {
        for (i = 0; i < JC_FUZZ_N_TARGETS; i++) {
            if (strcmp(want, JC_FUZZ_TARGETS[i].name) == 0) {
                g_fn = JC_FUZZ_TARGETS[i].fn;
                return;
            }
        }
    }
    /* Unset or unknown: name the valid set and stop. Failing loudly beats
     * silently fuzzing an arbitrary target for an hour. */
    fprintf(stderr, "jc_fuzz_lf: set JC_FUZZ_TARGET to one of:\n");
    for (i = 0; i < JC_FUZZ_N_TARGETS; i++) {
        fprintf(stderr, "  %s\n", JC_FUZZ_TARGETS[i].name);
    }
    exit(2);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t len)
{
    if (g_fn == NULL) {
        resolve_target();
    }
    g_fn(data, len);
    return 0; /* libFuzzer: non-zero is reserved for future use */
}
