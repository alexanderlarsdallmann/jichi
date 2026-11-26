/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_delegreport_kind.c - def-377 W6.1 (M1194): the summary's loop KIND.
 *
 * THE WIRE CONTRACT UNDER TEST. A child a LOOP stop ended sends
 * `stop=max_iters` plus `looped:N` (M869), and a W4 soft stop (def-377's
 * wrapup/stop) ends the child's loop the SAME way -- `last_run_capped` set,
 * the child answers with the report it was asked for -- with a FIFTH looped
 * value (5) that names the stop `supervised`. The parent's `[delegate]`
 * summary line therefore reads `stop=max_iters` FIRST -- three drivers grep
 * it (M885 rejected a new stop name on the wire) -- with the loop KIND
 * following as an ADDITIVE field, in words.
 *
 * What is asserted here:
 *   (a) a supervised stop (looped=5) renders `stop=max_iters` before the
 *       kind word `supervised`, both on the summary line;
 *   (b) the first token is INVARIANT: every looped value, and a real
 *       iteration cap (looped=0), still read `stop=max_iters` -- the kind
 *       never replaces or precedes the stop name;
 *   (c) an OLD READER -- one that knows only the four existing kinds, the
 *       way a parent built before W6.1 reads this line -- still reads
 *       `max_iters` as the stop name.
 *
 * RED on the head: the renderer does not print the kind words at all, so
 * the (a) kind assertion fails. The actions themselves (wrapup/stop, the
 * soft-stop sentinel) are the driver's red (notes/M1194.md cells 6-8, the
 * live pool + mock children); this cell is the renderer's, unit-reachable
 * through the same path test_delegreport.c uses.
 */
#include "jc_test.h"
#include "jc_delegreport.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <string.h>

static void render(const struct jc_delegate_report *r, struct jc_sb *sb)
{
    jc_sb_init(sb);
    jc_delegreport_render(r, sb);
}

/* The loop KIND word the renderer must print for a given looped value:
 * same_result / refused / placeholder / rotation (the four existing,
 * jc_agent.c:5923-5924) plus the fifth, supervised (W4). */
static const char *kind_word(int looped)
{
    switch (looped) {
    case 1:  return "same_result";
    case 2:  return "refused";
    case 3:  return "placeholder";
    case 4:  return "rotation";
    case 5:  return "supervised";
    default: return NULL;
    }
}

void test_delegreport_kind(void)
{
    struct jc_delegate_report r;
    struct jc_sb sb;
    int looped;

    /* --- (a) a supervised stop (W4) names its KIND after the stop name --- */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_MAX_ITERS;
    r.looped = 5;   /* def-377 W4: the soft stop (wrapup/stop) */
    render(&r, &sb);
    JC_CHECK(sb.data != NULL);
    if (JC_REQUIRE(sb.data != NULL)) {
        const char *stop = strstr(sb.data, "stop=max_iters");
        const char *kind = strstr(sb.data, kind_word(5));
        /* both on the line, in order: the stop name FIRST, the kind after */
        JC_CHECK(stop != NULL && kind != NULL);
        if (stop != NULL && kind != NULL) {
            JC_CHECK(stop < kind);
        }
    }
    jc_sb_free(&sb);

    /* --- (b) the first token is invariant across every kind (and a cap) --- */
    for (looped = 1; looped <= 5; looped++) {
        jc_delegreport_init(&r);
        r.stop = JC_DELEG_MAX_ITERS;
        r.looped = looped;
        render(&r, &sb);
        JC_CHECK(sb.data != NULL && strstr(sb.data, "stop=max_iters") != NULL);
        if (JC_REQUIRE(sb.data != NULL)) {
            const char *stop = strstr(sb.data, "stop=max_iters");
            const char *kind = strstr(sb.data, kind_word(looped));
            /* the kind follows, never precedes, the stop name */
            JC_CHECK(kind != NULL);
            if (kind != NULL) {
                JC_CHECK(stop < kind);
            }
        }
        jc_sb_free(&sb);
    }
    /* A real iteration cap (looped=0) stays exactly what it was: no kind. */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_MAX_ITERS;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "stop=max_iters") != NULL);
    for (looped = 1; looped <= 5; looped++) {
        JC_CHECK(sb.data != NULL && strstr(sb.data, kind_word(looped)) == NULL);
    }
    jc_sb_free(&sb);

    /* --- (c) an OLD READER still reads max_iters as the stop name --------
     * It knows only the four existing kinds: for it a `looped` value it has
     * not seen is an unknown loop, and its parsing of the STOP NAME must be
     * untouched by the kind. Model the reader the way a pre-W6.1 parent did
     * -- `stop=<name>` at the head of the summary line, the name one of the
     * known stops, the kind, if any, additive AFTER it. */
    for (looped = 1; looped <= 5; looped++) {
        jc_delegreport_init(&r);
        r.stop = JC_DELEG_MAX_ITERS;
        r.looped = looped;
        render(&r, &sb);
        if (JC_REQUIRE(sb.data != NULL)) {
            const char *s = strstr(sb.data, "[delegate] stop=");
            char tok[32];
            int j;
            /* the stop NAME must be the first token after the marker -- the
             * kind, when there is one, follows ADDITIVELY, never replacing it */
            JC_CHECK(s != NULL);
            if (s != NULL) {
                s = s + (int)strlen("[delegate] stop=");
                for (j = 0; s[j] != '\0' && s[j] != ' ' && s[j] != '\n' &&
                         j < 31; j++) {
                    tok[j] = s[j];
                }
                tok[j] = '\0';
                JC_CHECK(strcmp(tok, "max_iters") == 0);
            }
        }
        jc_sb_free(&sb);
    }

    /* --- (d) R:6 of the A4 audit (naming 243363e5): the loop-stop advice
     * override must be KIND-AWARE. The override as landed fires for any
     * looped > 0, so a supervised stop (looped=5) renders "it repeated the
     * same call without progress and was stopped" -- directly contradicted
     * by the `loop_kind=supervised` word two lines up: a supervised stop is
     * the lead's policy stop (wrapup or stop), and the child was asked to
     * report, not caught in a loop. It also must not claim a merge: the
     * hard-stop fallback keeps the work as a ref. Kinds 1-4 keep their
     * text exactly (test_delegreport.c pins looped=2 on it). */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_MAX_ITERS;
    r.looped = 5;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL);
    if (JC_REQUIRE(sb.data != NULL)) {
        JC_CHECK(strstr(sb.data, "repeated the same call") == NULL);
        JC_CHECK(strstr(sb.data, "supervision") != NULL);
    }
    jc_sb_free(&sb);
    /* the four loop kinds are UNTOUCHED: kind 1 still says repeated itself */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_MAX_ITERS;
    r.looped = 1;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "repeated the same call") != NULL);
    jc_sb_free(&sb);

    /* NULL-safety: like test_delegreport.c, both tools call this on failure. */
    jc_sb_init(&sb);
    jc_delegreport_render(NULL, &sb);
    JC_CHECK(sb.data == NULL || sb.data[0] == '\0');
    jc_sb_free(&sb);
}
