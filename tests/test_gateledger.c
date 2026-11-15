/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_gateledger.c - the gate ledger's PURE classification core (M1154,
 * H9b, task 2, wave 1): the reason-ORDER join (ledger.sh's fixed order:
 * author, committer, co-authored-by, Jichi-Build, Jichi-Model,
 * priced-model), the DD11 accept-list decision (a FULL 40-hex id listed
 * AND every reason a missing-trailer one; a wrong address or a priced
 * model keeps the commit bad even when listed), and the priced-model id
 * classifier. The engine is pure (no git, no I/O) and exported from
 * jc_hp_gateledger.c for this table; the smoke driver's L1-L11 cells
 * hold the END-TO-END arms (the real git fixture), and this table pins
 * each half at the boundary.
 *
 * Teeth: inverting the reason order or the accept decision fires the
 * matching checks below (the driver's L5/L10/L11 pin the byte shapes
 * end-to-end). */

#include "jc_test.h"
#include "../src/harness/jc_hp_gateledger.h"
#include "jc_vec.h"

#include <string.h>

/* Local forward declaration (-Wmissing-prototypes; the M1124 idiom -- the
 * runner's declaration in test_main.c is in a different translation unit). */
void test_gateledger(void);

void
test_gateledger(void)
{
    struct jc_hp_gateledger_commit c;
    char why[4096];
    const char *addr = "ops@example.com";
    const char *other = "other@example.com";

    /* A clean commit: every field the address, one good Co-Authored-By,
     * both trailers, an unpriced model. No reasons. */
    {
        const char *co[1] = { "jichi 0.13.0 <ops@example.com>" };
        struct jc_vec colist;
        colist.data = (void *)co;
        colist.len = 1;
        colist.cap = 1;
        colist.elem = sizeof (const char *);
        memset(&c, 0, sizeof c);
        c.ae = addr;
        c.ce = addr;
        c.co_list = &colist;
        c.jb = "53a1ef4e";
        c.jm = "jlu/qwen3-coder-next";
        jc_hp_gateledger_reasons(&c, addr, why, sizeof why);
        JC_CHECK_STR(why, "");
        JC_CHECK(jc_hp_gateledger_all_missing_trailers(&c, addr) == 1);
    }

    /* (1) REASON ORDER: a commit that is wrong in EVERY way carries the
     *     six reasons in ledger.sh's order (author, committer,
     *     co-authored-by, Jichi-Build, Jichi-Model, priced-model) --
     *     joined with a leading space per reason. */
    {
        const char *co[1] = { "jichi 0.13.0 <other@example.com>" };
        struct jc_vec colist;
        colist.data = (void *)co;
        colist.len = 1;
        colist.cap = 1;
        colist.elem = sizeof (const char *);
        memset(&c, 0, sizeof c);
        c.ae = other;
        c.ce = other;
        c.co_list = &colist;
        c.jb = "";
        c.jm = "anthropic/claude-x";
        jc_hp_gateledger_reasons(&c, addr, why, sizeof why);
        JC_CHECK_STR(why,
                     " author=other@example.com"
                     " committer=other@example.com"
                     " co-authored-by=[jichi 0.13.0 <other@example.com>]"
                     " no-Jichi-Build"
                     " priced-model=anthropic/claude-x");
        /* Not a missing-trailer-only shape: the address + the priced
         * model disqualify the accept list. */
        JC_CHECK(jc_hp_gateledger_all_missing_trailers(&c, addr) == 0);
    }

    /* (2) A Jichi-Model absent is the no-Jichi-Model reason, NOT a priced
     *     one (the reference's :23 case only runs when jm is present). */
    {
        const char *co[1] = { "jichi 0.13.0 <other@example.com>" };
        struct jc_vec colist;
        colist.data = (void *)co;
        colist.len = 1;
        colist.cap = 1;
        colist.elem = sizeof (const char *);
        memset(&c, 0, sizeof c);
        c.ae = addr;
        c.ce = addr;
        c.co_list = &colist;
        c.jb = "53a1ef4e";
        c.jm = "";
        jc_hp_gateledger_reasons(&c, addr, why, sizeof why);
        JC_CHECK_STR(why, " co-authored-by=[jichi 0.13.0 <other@example.com>] no-Jichi-Model");
    }

    /* (3) The co-authored-by join is the VALUES with | between them
     *     (ledger.sh:18's tr '\n' '|' over the valueonly,unfold list) --
     *     two wrong values, in order, no trailing | (the reference's
     *     bytes, measured). */
    {
        const char *co[2] = { "a <x@example.com>", "b <q@example.com>" };
        struct jc_vec colist;
        colist.data = (void *)co;
        colist.len = 2;
        colist.cap = 2;
        colist.elem = sizeof (const char *);
        memset(&c, 0, sizeof c);
        c.ae = addr;
        c.ce = addr;
        c.co_list = &colist;
        c.jb = "53a1ef4e";
        c.jm = "jlu/qwen3-coder-next";
        jc_hp_gateledger_reasons(&c, addr, why, sizeof why);
        JC_CHECK_STR(why, " co-authored-by=[a <x@example.com>|b <q@example.com>]");
    }

    /* (4) One good + one bad Co-Authored-By: ANY value without <addr> is
     *     the reason, and the join lists BOTH (never only the bad one). */
    {
        const char *co[2] =
            { "jichi 0.13.0 <ops@example.com>",
              "jichi 0.13.0 <other@example.com>" };
        struct jc_vec colist;
        colist.data = (void *)co;
        colist.len = 2;
        colist.cap = 2;
        colist.elem = sizeof (const char *);
        memset(&c, 0, sizeof c);
        c.ae = addr;
        c.ce = addr;
        c.co_list = &colist;
        c.jb = "53a1ef4e";
        c.jm = "jlu/qwen3-coder-next";
        jc_hp_gateledger_reasons(&c, addr, why, sizeof why);
        JC_CHECK_STR(why,
                     " co-authored-by=[jichi 0.13.0 <ops@example.com>|"
                     "jichi 0.13.0 <other@example.com>]");
    }

    /* (5) MISSING-TRAILER-ONLY: no Co-Authored-By + no Jichi-Build + no
     *     Jichi-Model, everything else clean -- the DD11 accept shape. */
    {
        memset(&c, 0, sizeof c);
        c.ae = addr;
        c.ce = addr;
        c.co_list = NULL;
        c.jb = "";
        c.jm = "";
        jc_hp_gateledger_reasons(&c, addr, why, sizeof why);
        JC_CHECK_STR(why, " no-Co-Authored-By no-Jichi-Build no-Jichi-Model");
        JC_CHECK(jc_hp_gateledger_all_missing_trailers(&c, addr) == 1);
    }

    /* (6) A wrong ADDRESS + missing trailers: still NOT missing-trailer-
     *     only (the L11 arm -- the accept list cannot exempt it). */
    {
        memset(&c, 0, sizeof c);
        c.ae = other;
        c.ce = addr;
        c.co_list = NULL;
        c.jb = "53a1ef4e";
        c.jm = "jlu/qwen3-coder-next";
        jc_hp_gateledger_reasons(&c, addr, why, sizeof why);
        JC_CHECK_STR(why, " author=other@example.com no-Co-Authored-By");
        JC_CHECK(jc_hp_gateledger_all_missing_trailers(&c, addr) == 0);
    }

    /* (7) A PRICED model + missing Jichi-Build: still not missing-trailer-
     *     only. */
    {
        memset(&c, 0, sizeof c);
        c.ae = addr;
        c.ce = addr;
        c.co_list = NULL;
        c.jb = "";
        c.jm = "openai/gpt-x";
        jc_hp_gateledger_reasons(&c, addr, why, sizeof why);
        JC_CHECK_STR(why, " no-Co-Authored-By no-Jichi-Build priced-model=openai/gpt-x");
        JC_CHECK(jc_hp_gateledger_all_missing_trailers(&c, addr) == 0);
    }

    /* (8) The priced-model classifier: the six provider names, substring
     *     match (ledger.sh:23's case patterns). */
    JC_CHECK(jc_hp_gateledger_is_priced("anthropic/claude-x") == 1);
    JC_CHECK(jc_hp_gateledger_is_priced("openai/gpt-4") == 1);
    JC_CHECK(jc_hp_gateledger_is_priced("vertex_ai/gemini-pro") == 1);
    JC_CHECK(jc_hp_gateledger_is_priced("gemini/gemini-1") == 1);
    JC_CHECK(jc_hp_gateledger_is_priced("bedrock/claude-x") == 1);
    JC_CHECK(jc_hp_gateledger_is_priced("azure/gpt-x") == 1);
    JC_CHECK(jc_hp_gateledger_is_priced("jlu/qwen3-coder-next") == 0);
    JC_CHECK(jc_hp_gateledger_is_priced("") == 0);
    JC_CHECK(jc_hp_gateledger_is_priced(NULL) == 0);

    /* (9) The accept-list decision: FULL 40-hex id only (an 8-hex sha8 or
     *     a 41-char run is NOT listed; the driver's L10 writes full ids). */
    {
        const char *ids[3] =
            { "abcd1234abcd1234abcd1234abcd1234abcd1234", "short", "abc" };
        struct jc_vec acc;
        acc.data = (void *)ids;
        acc.len = 3;
        acc.cap = 3;
        acc.elem = sizeof (const char *);
        JC_CHECK(jc_hp_gateledger_accept_listed(
            "abcd1234abcd1234abcd1234abcd1234abcd1234", &acc) == 1);
        JC_CHECK(jc_hp_gateledger_accept_listed("abcd1234", &acc) == 0);
        JC_CHECK(jc_hp_gateledger_accept_listed(NULL, &acc) == 0);
        JC_CHECK(jc_hp_gateledger_accept_listed(
            "abcd1234abcd1234abcd1234abcd1234abcd1234", NULL) == 0);
    }

    /* (9b) M1178: a gate with NO accept list gets an EMPTY, initialised
     *      vector. Before M1178 the run initialised it only inside the read,
     *      so a run without a list read it in the walk and freed it at the
     *      end (valgrind measured both on the real binary). The vector is
     *      poisoned first, so a loader that skips the init on a NULL or
     *      empty path leaves 0xA5 bytes and fails here on every machine,
     *      not only under valgrind. No arena is needed: no file is read, and
     *      nothing is freed here: an empty vector owns nothing, and freeing
     *      a poisoned one would crash the suite instead of failing a check. */
    {
        struct jc_vec acc;

        memset(&acc, 0xA5, sizeof acc);
        JC_CHECK(jc_hp_gateledger_load_accept(NULL, NULL, &acc) == 0);
        JC_CHECK(acc.len == 0 && acc.data == NULL);
        JC_CHECK(acc.elem == sizeof (const char *));
        memset(&acc, 0xA5, sizeof acc);
        JC_CHECK(jc_hp_gateledger_load_accept("", NULL, &acc) == 0);
        JC_CHECK(acc.len == 0 && acc.data == NULL);
    }

    /* (10) NULL / empty guards: no crash, no reasons. */
    memset(&c, 0, sizeof c);
    jc_hp_gateledger_reasons(NULL, addr, why, sizeof why);
    JC_CHECK_STR(why, "");
    jc_hp_gateledger_reasons(&c, NULL, why, sizeof why);
    JC_CHECK_STR(why, "");
    JC_CHECK(jc_hp_gateledger_all_missing_trailers(NULL, addr) == 0);

    /* (11) THE DD6 GUARD (the reference's `[ -z "$why" ] &&
     *      [ "$co" = "$SUP" ]`): author AND committer the operator + the
     *      single Co-Authored-By EXACTLY SUP. A second Co-Authored-By value,
     *      another value, or another author is the fall-through (the normal
     *      co-authored-by reason), never the accept. (C89: the co arrays
     *      initialize from a file-static address, a constant expression.) */
    {
        static const char sup[] = "Claude Opus 5.5 <noreply@anthropic.com>";
        const char *co1[1] = { sup };
        const char *co2[2] = { sup, sup };
        const char *co_other[1] = { "jichi 0.13.0 <other@example.com>" };
        struct jc_vec colist;
        colist.data = (void *)co1;
        colist.len = 1;
        colist.cap = 1;
        colist.elem = sizeof (const char *);
        memset(&c, 0, sizeof c);
        c.ae = addr;
        c.ce = addr;
        c.co_list = &colist;
        JC_CHECK(jc_hp_gateledger_dd6_guard(&c, addr) == 1);
        /* a second Co-Authored-By line: $co is multi-line -> not equal. */
        colist.data = (void *)co2;
        colist.len = 2;
        colist.cap = 2;
        JC_CHECK(jc_hp_gateledger_dd6_guard(&c, addr) == 0);
        /* another value -> not SUP. */
        colist.data = (void *)co_other;
        colist.len = 1;
        colist.cap = 1;
        JC_CHECK(jc_hp_gateledger_dd6_guard(&c, addr) == 0);
        /* another author: the reference's why is non-empty. */
        colist.data = (void *)co1;
        c.ae = other;
        JC_CHECK(jc_hp_gateledger_dd6_guard(&c, addr) == 0);
        c.ae = addr;
        c.ce = other;   /* another committer. */
        JC_CHECK(jc_hp_gateledger_dd6_guard(&c, addr) == 0);
        /* no Co-Authored-By at all. */
        c.ce = addr;
        c.co_list = NULL;
        JC_CHECK(jc_hp_gateledger_dd6_guard(&c, addr) == 0);
        JC_CHECK(jc_hp_gateledger_dd6_guard(NULL, addr) == 0);
    }

    /* (12) THE DD6 CLASSIFIER: the guard passes and the changed paths decide.
     *      Every path under docs/internal/ -> 1 (the accepted arm); a path
     *      OUTSIDE -> 0 with "" (the fall-through to the normal reasons);
     *      paths unreadable (dd6_paths NULL) -> 0 with the reference's
     *      could-not-read reason. The guard's own arms (another author, a
     *      second Co-Authored-By, a non-SUP value) -> 0 with "" (the
     *      fall-through; nothing is said about paths that were never read). */
    {
        static const char sup[] = "Claude Opus 5.5 <noreply@anthropic.com>";
        const char *co1[1] = { sup };
        const char *paths_in[2] = { "docs/internal/inner.txt", "f" };
        const char *paths_doc[2] = { "docs/internal/inner.txt",
                                     "docs/internal/drive/LOG.md" };
        const char *paths_out[2] = { "docs/internal/inner.txt",
                                     "docs/analysis/dd6.txt" };
        struct jc_vec colist, plist;
        colist.data = (void *)co1;
        colist.len = 1;
        colist.cap = 1;
        colist.elem = sizeof (const char *);
        memset(&c, 0, sizeof c);
        c.ae = addr;
        c.ce = addr;
        c.co_list = &colist;
        /* docs/internal/ only (one NEW file + one MODIFIED under
         * docs/internal/, the driver's cell 30: inner-1.txt + base.txt)
         * -> the accept; the classifier sees exactly what the reference's
         * grep sees, line by line. */
        plist.data = (void *)paths_doc;
        plist.len = 2;
        plist.cap = 2;
        plist.elem = sizeof (const char *);
        c.dd6_paths = &plist;
        JC_CHECK(jc_hp_gateledger_dd6(&c, addr, why, sizeof why) == 1);
        JC_CHECK_STR(why, "");
        /* a path outside docs/internal/ (the driver's cell 31) -> fall
         * through, out stays "" (the normal reasons, computed by the
         * caller). */
        plist.data = (void *)paths_out;
        c.dd6_paths = &plist;
        JC_CHECK(jc_hp_gateledger_dd6(&c, addr, why, sizeof why) == 0);
        JC_CHECK_STR(why, "");
        /* the guard passing is not enough: the paths still decide (a
         * mixed set with an outside path is never the accept). */
        plist.data = (void *)paths_in;
        c.dd6_paths = &plist;
        JC_CHECK(jc_hp_gateledger_dd6(&c, addr, why, sizeof why) == 0);
        JC_CHECK_STR(why, "");
        /* paths UNREADABLE (the reference's `||` arm: a range's first commit
         * with the SUP trailer -- `c^` fails) -> the could-not-read reason,
         * the commit counted bad. */
        c.dd6_paths = NULL;
        JC_CHECK(jc_hp_gateledger_dd6(&c, addr, why, sizeof why) == 0);
        JC_CHECK_STR(why,
                     " the supervisor's trailer, but its changed paths "
                     "could not be read");
        /* the guard's own fall-throughs: another author / a second
         * Co-Authored-By -> 0 with "" (out untouched; the normal reasons
         * apply). */
        c.ae = other;
        JC_CHECK(jc_hp_gateledger_dd6(&c, addr, why, sizeof why) == 0);
        c.ae = addr;
        {
            const char *co2[2] = { sup, sup };   /* sup is file-static: a
                                                   * constant expression (C89) */
            colist.data = (void *)co2;
            colist.len = 2;
            colist.cap = 2;
            JC_CHECK(jc_hp_gateledger_dd6(&c, addr, why, sizeof why) == 0);
            colist.data = (void *)co1;
            colist.len = 1;
        }
        JC_CHECK_STR(why, "");   /* the guard failed: out stays untouched */
        JC_CHECK(jc_hp_gateledger_dd6(NULL, addr, why, sizeof why) == 0);
        JC_CHECK(jc_hp_gateledger_dd6(&c, NULL, why, sizeof why) == 0);
    }
}
