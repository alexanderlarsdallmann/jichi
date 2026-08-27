/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_calib.c - per-model token-estimate calibration (M77). */

#include "jc_test.h"
#include "jc_calib.h"
#include "jc_mem.h"

#include <stdio.h>

static int approx(double a, double b)
{
    double d = a - b;
    if (d < 0.0) {
        d = -d;
    }
    return d < 1e-9;
}

void test_calib(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_calib c;

    /* --- clamp: pure band [0.5, 8.0] --- */
    JC_CHECK(approx(jc_calib_clamp(0.1), JC_CALIB_MIN));
    JC_CHECK(approx(jc_calib_clamp(99.0), JC_CALIB_MAX));
    JC_CHECK(approx(jc_calib_clamp(2.0), 2.0));

    /* --- blend: first sample taken verbatim (clamped); later ones move toward
     * the sample by 1/(n+1). --- */
    JC_CHECK(approx(jc_calib_blend(0.0, 0, 2.0), 2.0));   /* first sample */
    JC_CHECK(approx(jc_calib_blend(9.0, 0, 9.0), JC_CALIB_MAX)); /* clamped */
    /* second sample: old 2.0, samples=1 -> weight 1/2 -> 2.0 + (4.0-2.0)/2 */
    JC_CHECK(approx(jc_calib_blend(2.0, 1, 4.0), 3.0));
    /* repeated identical samples converge to that value (stays put). */
    JC_CHECK(approx(jc_calib_blend(2.0, 5, 2.0), 2.0));

    /* --- init / default --- */
    jc_calib_init(&c, a);
    JC_CHECK(approx(jc_calib_get(&c, "qwen"), 1.0)); /* uncalibrated => 1.0 */
    JC_CHECK(approx(jc_calib_get(&c, NULL), 1.0));

    /* --- observe: learns real/est; a single obs sets the ratio to the sample. */
    jc_calib_observe(&c, "qwen", 200, 100);          /* sample 2.0 */
    JC_CHECK(approx(jc_calib_get(&c, "qwen"), 2.0));
    JC_CHECK(c.dirty == 1);

    /* a second, larger sample pulls the average up but is damped. */
    jc_calib_observe(&c, "qwen", 400, 100);          /* sample 4.0, w=1/2 */
    JC_CHECK(approx(jc_calib_get(&c, "qwen"), 3.0));

    /* a different model is tracked independently. */
    jc_calib_observe(&c, "gemma", 150, 100);         /* sample 1.5 */
    JC_CHECK(approx(jc_calib_get(&c, "gemma"), 1.5));
    JC_CHECK(approx(jc_calib_get(&c, "qwen"), 3.0));

    /* guards: non-positive real/est, empty id, out-of-band sample are ignored. */
    jc_calib_observe(&c, "gemma", 0, 100);
    jc_calib_observe(&c, "gemma", 150, 0);
    jc_calib_observe(&c, "", 200, 100);
    jc_calib_observe(&c, "gemma", 10000, 100);       /* sample 100 -> out of band */
    JC_CHECK(approx(jc_calib_get(&c, "gemma"), 1.5)); /* unchanged */

    /* --- save / load round-trip via a temp file --- */
    {
        const char *path = jc_test_tmp("jichi_test_calib.json");
        struct jc_calib c2;
        struct jc_arena *a2 = jc_arena_new(0);

        remove(path);
        c.path = jc_arena_strdup(a, path); /* stand in for jc_calib_load's stash */
        JC_CHECK(jc_calib_save(&c) == JC_OK);
        JC_CHECK(c.dirty == 0);            /* clean after a successful save */

        jc_calib_init(&c2, a2);
        JC_CHECK(jc_calib_load(&c2, path) == JC_OK);
        JC_CHECK(approx(jc_calib_get(&c2, "qwen"), 3.0));
        JC_CHECK(approx(jc_calib_get(&c2, "gemma"), 1.5));
        JC_CHECK(approx(jc_calib_get(&c2, "absent"), 1.0));
        JC_CHECK(c2.dirty == 0);           /* load doesn't dirty */

        jc_calib_free(&c2);
        jc_arena_free(a2);
        remove(path);
    }

    /* a missing file loads cleanly into an empty table. */
    {
        struct jc_calib c3;
        jc_calib_init(&c3, a);
        JC_CHECK(jc_calib_load(&c3, jc_test_tmp("jichi_calib_does_not_exist.json"))
                 == JC_OK);
        JC_CHECK(approx(jc_calib_get(&c3, "anything"), 1.0));
        jc_calib_free(&c3);
    }

    /* --- M286: the schema version gates the file, because a ratio is only
     * meaningful against the estimate basis it was measured on. A v1 file (no
     * "v" key) was learned against `history + 2000` and reads systematically
     * high, so it must be DISCARDED, not trusted or averaged down. --- */
    {
        const char *path = jc_test_tmp("jichi_test_calib_v1.json");
        struct jc_calib c4;
        FILE *f = fopen(path, "w");
        JC_CHECK(f != NULL);
        if (f != NULL) {
            /* Shaped exactly like a real pre-M286 file: no "v" key. */
            fputs("{\"jlu/qwen3-coder-next\":{\"ratio\":2.717,\"samples\":2635}}",
                  f);
            fclose(f);
        }
        jc_calib_init(&c4, a);
        JC_CHECK(jc_calib_load(&c4, path) == JC_OK);
        /* Discarded => the model reads as uncalibrated (1.0), NOT 2.717. */
        JC_CHECK(approx(jc_calib_get(&c4, "jlu/qwen3-coder-next"), 1.0));
        JC_CHECK(c4.entries.len == 0);
        jc_calib_free(&c4);
        remove(path);
    }

    /* A file stamped with the current schema is honoured, and the "v" key
     * itself is never mistaken for a model entry. */
    {
        const char *path = jc_test_tmp("jichi_test_calib_v2.json");
        struct jc_calib c5;
        FILE *f = fopen(path, "w");
        JC_CHECK(f != NULL);
        if (f != NULL) {
            fprintf(f, "{\"v\":%d,\"qwen\":{\"ratio\":1.19,\"samples\":900}}",
                    JC_CALIB_SCHEMA);
            fclose(f);
        }
        jc_calib_init(&c5, a);
        JC_CHECK(jc_calib_load(&c5, path) == JC_OK);
        JC_CHECK(approx(jc_calib_get(&c5, "qwen"), 1.19));
        JC_CHECK(c5.entries.len == 1);       /* "v" did not become an entry */
        JC_CHECK(approx(jc_calib_get(&c5, "v"), 1.0));
        jc_calib_free(&c5);
        remove(path);
    }

    /* And what we WRITE must be loadable by us: without the version stamp on
     * save, every save/load round-trip would silently discard the table. */
    {
        const char *path = jc_test_tmp("jichi_test_calib_rt.json");
        struct jc_calib c6, c7;
        struct jc_arena *a7 = jc_arena_new(0);

        remove(path);
        jc_calib_init(&c6, a);
        jc_calib_observe(&c6, "qwen", 119, 100);      /* sample 1.19 */
        c6.path = jc_arena_strdup(a, path);
        JC_CHECK(jc_calib_save(&c6) == JC_OK);

        jc_calib_init(&c7, a7);
        JC_CHECK(jc_calib_load(&c7, path) == JC_OK);
        JC_CHECK(c7.entries.len == 1);                /* survived the round-trip */
        JC_CHECK(approx(jc_calib_get(&c7, "qwen"), 1.19));

        jc_calib_free(&c7);
        jc_arena_free(a7);
        jc_calib_free(&c6);
        remove(path);
    }

    jc_calib_free(&c);
    jc_arena_free(a);
}
