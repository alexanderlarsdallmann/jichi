/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_gatesweep.c - the gate sweep stage's PURE line-assembly (M1154, H9b,
 * task 5, wave 2): the unit table for the exported line builders in
 * jc_hp_gatesweep.c. The engine is pure (no git, no I/O): it assembles the
 * EXACT bytes the stage prints (the reference's sweep.sh lines, with the
 * note's behaviour 3 CHANGED), and the three tests/fixtures/sweep-*.txt files
 * pin the byte shapes the verdict consumes. Each case builds one line (or the
 * not-ok / excerpt half of a line) into a buffer and compares it to the
 * reference-derived literal. */

#include <stdio.h>
#include <string.h>

#include "jc_snprintf.h"
#include "jc_test.h"
#include "../src/harness/jc_hp_gatesweep.h"

/* The -Wmissing-prototypes self-declaration (the T2 / T3 test idiom: the
 * function is declared in tests/test_main.c, a different TU, so it names its
 * own prototype here). */
void test_gatesweep(void);

#define TS_BUF (16 * 1024)

static void
ts_eq(const char *buf, const char *want)
{
    JC_CHECK(strcmp(buf, want) == 0);
}

void test_gatesweep(void)
{
    char buf[TS_BUF];

    /* The build line (the note's CHANGED: the reference's
     * `  <./jichi --version | grep build>` becomes `  build: <sha8>`; the
     * verdict's `^  build: <s>$` pattern, sweep.sh:17). */
    JC_CHECK(jc_gatesweep_build_line("77d2406f", buf, sizeof buf) > 0);
    ts_eq(buf, "  build: 77d2406f\n");
    JC_CHECK(jc_gatesweep_build_line("9ada7cd7", buf, sizeof buf) > 0);
    ts_eq(buf, "  build: 9ada7cd7\n");

    /* The count line (sweep.sh:26): `sweep: ran N drivers on <s>;
     * red:<space-joined | none>`. */
    JC_CHECK(jc_gatesweep_count_line(2, "77d2406f", "", buf, sizeof buf) > 0);
    ts_eq(buf, "sweep: ran 2 drivers on 77d2406f; red: none\n");
    JC_CHECK(jc_gatesweep_count_line(2, "832c7930",
                                     "nobin_lint usesbin_lint", buf,
                                     sizeof buf) > 0);
    ts_eq(buf,
         "sweep: ran 2 drivers on 832c7930; red: nobin_lint usesbin_lint\n");

    /* A red driver's `  RED <b> rc=<r>` line (sweep.sh:23). */
    JC_CHECK(jc_gatesweep_red_line("nobin_lint", 1, buf, sizeof buf) > 0);
    ts_eq(buf, "  RED nobin_lint rc=1\n");

    /* The not-ok excerpt (sweep.sh:23's grep | head -2 | cut -c1-200): the
     * first two `^not ok` lines, cut 200, a non-not-ok line skipped. */
    {
        const char *d = "1..3\nok 1 - x\nnot ok 2 - the second failed\n"
                        "ok 3 - y\nnot ok 4 - the fourth failed\n"
                        "not ok 5 - the fifth\n";
        JC_CHECK(jc_gatesweep_notok_excerp(d, buf, sizeof buf) > 0);
        ts_eq(buf,
              "not ok 2 - the second failed\nnot ok 4 - the fourth failed\n");
    }
    /* The excerpt's 200-byte cut: a 240-byte not-ok line is truncated to
     * 200 (the line's bytes, not the excerpt's, are cut -- the reference's
     * `cut -c1-200` over the matched line). */
    {
        static char longline[256];
        char out[256];
        size_t i;

        for (i = 0; i < 11; i++) {
            longline[i] = 'n';
        }
        for (i = 11; i < 240; i++) {
            longline[i] = 'x';
        }
        longline[240] = '\0';
        snprintf(buf, sizeof buf, "not ok 1 - %s\nok 2 - fine\n", longline);
        JC_CHECK(jc_gatesweep_notok_excerp(buf, out, sizeof out) > 0);
        JC_CHECK(strlen(out) == 200 + 1); /* the cut line + its \n */
        JC_CHECK(out[200] == '\n');
        JC_CHECK(strncmp(out, "not ok 1 - ", 11) == 0);
        JC_CHECK(strchr(out, 'y') == NULL); /* the 40 'x' + tail are cut off */
    }

    /* The smoke-mutant line (sweep.sh:35): the FIRST `^mutant:` line of the
     * output, cut 150, after the `  smoke-mutant rc=<r> (MUTANT_BASE=<%.8s>):
     * ` prefix. A multi-line output -> only the first `mutant:` line. */
    {
        const char *m = "noise before\nmutant: OK (1 drivers swept, 0 "
                        "excluded)\nmutant: a second one\n";
        JC_CHECK(jc_gatesweep_mutant_line(0, "4a1aee78", m, buf, sizeof buf)
                 > 0);
        ts_eq(buf,
              "  smoke-mutant rc=0 (MUTANT_BASE=4a1aee78): mutant: OK (1 "
              "drivers swept, 0 excluded)\n");
    }
    /* A no-`mutant:` output -> an empty body after the colon. */
    {
        JC_CHECK(jc_gatesweep_mutant_line(1, "4a1aee78deadbeef",
                                          "no mutant line here\n", buf,
                                          sizeof buf) > 0);
        ts_eq(buf,
              "  smoke-mutant rc=1 (MUTANT_BASE=4a1aee78): \n");
    }
    /* The mutant's `not ok` excerpt (sweep.sh:36: head -4, cut 150, indented). */
    {
        const char *m = "ok 1\nnot ok 2 - a\nnot ok 3 - b\nnot ok 4 - c\n"
                        "not ok 5 - d\nnot ok 6 - e\n";
        JC_CHECK(jc_gatesweep_mutant_notok(m, buf, sizeof buf) > 0);
        ts_eq(buf, "    not ok 2 - a\n    not ok 3 - b\n"
                   "    not ok 4 - c\n    not ok 5 - d\n");
    }

    /* The no-origin/master INVALID (sweep.sh:38). */
    JC_CHECK(jc_gatesweep_mutant_nomb_line(buf, sizeof buf) > 0);
    ts_eq(buf, "  smoke-mutant: no origin/master in push.git: INVALID\n");

    /* The unit line (sweep.sh:51): the LAST `N checks, M failures` of the
     * output; a multi-line output -> the last one. The NO SUMMARY arm when
     * there is none. */
    {
        const char *u = "building...\n37482 checks, 0 failures\n"
                        "another suite 99 checks, 1 failures\n";
        JC_CHECK(jc_gatesweep_unit_line("san", 0, u, buf, sizeof buf) > 0);
        ts_eq(buf, "  unit san rc=0 99 checks, 1 failures\n");
    }
    {
        JC_CHECK(jc_gatesweep_unit_line("o2", 1, "make: No rule to make "
                                                 "target\n", buf, sizeof buf)
                 > 0);
        ts_eq(buf, "  unit o2 rc=1 NO SUMMARY (build failed or aborted)\n");
    }

    /* The unit's FAIL/ASan/runtime-error excerpt (sweep.sh:52: head -3, cut
     * 200, indented). */
    {
        const char *u = "FAIL tests/test_x.c:10\nERROR: "
                        "AddressSanitizer: SEGV\nok 1\nruntime error: "
                        "index out of bounds\n";
        JC_CHECK(jc_gatesweep_unit_excerp(u, buf, sizeof buf) > 0);
        ts_eq(buf, "    FAIL tests/test_x.c:10\n"
                   "    ERROR: AddressSanitizer: SEGV\n"
                   "    runtime error: index out of bounds\n");
    }

    /* The container line (sweep.sh:66): the ok / not ok counts, with the
     * SKIPPED INVALID tail when the driver skipped. */
    {
        JC_CHECK(jc_gatesweep_container_line("fetch_ssrf_reuse", 0, 4, 0, "",
                                             buf, sizeof buf) > 0);
        ts_eq(buf, "  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok\n");
        JC_CHECK(jc_gatesweep_container_line("resolve_timeout_connect", 0,
                                             0, 0,
                                             "1..0 # skip: no userns", buf,
                                             sizeof buf) > 0);
        ts_eq(buf,
              "  container resolve_timeout_connect rc=0 0 ok, 0 not ok -- "
              "SKIPPED: INVALID (1..0 # skip: no userns)\n");
    }
    /* The podman-absent container INVALID (the note's behaviour 3 CHANGED). */
    {
        JC_CHECK(jc_gatesweep_container_podman_line("fetch_ssrf_reuse", buf,
                                                    sizeof buf) > 0);
        ts_eq(buf,
              "  container fetch_ssrf_reuse rc=1 0 ok, 0 not ok -- SKIPPED: "
              "INVALID (podman not available)\n");
    }
    /* The container's `not ok` excerpt (sweep.sh:67: head -2, indented). */
    {
        const char *c = "ok 1\nnot ok 2 - the ssrf driver failed\nok 3\n"
                        "not ok 4 - a second failure\n";
        JC_CHECK(jc_gatesweep_container_notok(c, buf, sizeof buf) > 0);
        ts_eq(buf,
              "    not ok 2 - the ssrf driver failed\n"
              "    not ok 4 - a second failure\n");
    }

    /* The records line (sweep.sh:70, LAST in the stage's output). */
    {
        JC_CHECK(jc_gatesweep_records_line(
                     "/home/u/verify/sweep-77d2406f", buf, sizeof buf) > 0);
        ts_eq(buf, "records: /home/u/verify/sweep-77d2406f\n");
    }

    /* The build-failure INVALID (sweep.sh:16's || branch). */
    JC_CHECK(jc_gatesweep_buildfail_line(buf, sizeof buf) > 0);
    ts_eq(buf, "build failed: INVALID\n");

    /* F2 (M1154 T6): the git READ child's env. The review found the stage's
     * rev-parse child (gsw_git -> jc_hp_git_run, no credential env) leaking
     * ZD_TOKEN_FILE -- the one name jc_proc's built-in scrub lacks
     * (jc_proc.c:71-78). Pin the 16 gsw_keys (LENGTH + COMPOSITION, sweep.sh:8
     * as-is) and the READ child's env: exactly the 16 "KEY=" (empty) entries.
     * Before the fix the env carried only the two GIT_CONFIG_* entries: RED. */
    {
        static const char *want[16] = {
            "JICHI_API_KEY", "JLU_API_KEY", "ANTHROPIC_API_KEY",
            "OPENAI_API_KEY", "OPENROUTER_API_KEY", "GEMINI_API_KEY",
            "GOOGLE_API_KEY", "GROQ_API_KEY", "MISTRAL_API_KEY",
            "DEEPSEEK_API_KEY", "TOGETHER_API_KEY", "FIREWORKS_API_KEY",
            "TAVILY_API_KEY", "COHERE_API_KEY", "XAI_API_KEY",
            "ZD_TOKEN_FILE"
        };
        const char *kn[24];
        struct jc_vec env;
        struct jc_arena *a;
        int nkeys;
        int i;

        nkeys = jc_gatesweep_key_names(kn, 24);
        JC_CHECK(nkeys == 16);
        for (i = 0; i < 16 && i < nkeys; i++) {
            JC_CHECK(strcmp(kn[i], want[i]) == 0);
        }
        JC_CHECK(nkeys == 16 && strcmp(kn[15], "ZD_TOKEN_FILE") == 0);
        JC_CHECK(jc_gatesweep_key_names(NULL, 24) == -1);
        JC_CHECK(jc_gatesweep_key_names(kn, 15) == -1);

        a = jc_arena_new(65536);
        JC_CHECK(a != NULL);
        jc_vec_init(&env, sizeof (char *));
        JC_CHECK(jc_gatesweep_gitread_env(a, &env) == 0);
        JC_CHECK(env.len == 16);
        for (i = 0; i < 16; i++) {
            char slot[64];
            int hits = 0;
            int j;
            (void)jc_snprintf(slot, sizeof slot, "%s=", want[i]);
            for (j = 0; j < (int)env.len && j < 64; j++) {
                char *entry = JC_VEC_STR(&env, j);
                if (entry != NULL && strcmp(entry, slot) == 0) {
                    hits++;
                }
            }
            JC_CHECK(hits == 1);
        }
        jc_vec_free(&env);
        jc_arena_free(a);
    }
}
