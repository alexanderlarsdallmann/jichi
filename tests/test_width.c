/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_width.c - every chrome string fits a terminal, in every language.
 *
 * WHY THIS EXISTS (M554). The accessibility work of M549-M553 replaced compact
 * chrome with PROSE:
 *
 *     [tokens in=4,946 out=37]   ->   4,946 input tokens used, and 37 output
 *                                     tokens used.
 *
 * 25 columns became 50. That is the right trade for a listener and it spends a
 * budget nobody had measured, and the budget is **shared**: a chrome line that
 * wraps is worse for BOTH audiences -- some screen readers re-announce a
 * wrapped line, and visual alignment breaks. The design assumed German +20-35%
 * from UI-string folklore; MEASURED it is **+50%** on the entry the two
 * languages share (the approval prompt, 44 -> 66 columns). That is the whole
 * argument for measuring instead of assuming.
 *
 * `jc_term_str_cols` is the right instrument because it carries the UAX #11
 * East Asian Width table -- a Japanese glyph occupies two columns, so `strlen`
 * would report ja/zh in bytes and hide the thing that matters: **Japanese prose
 * is shorter in characters and wider in columns than English.**
 *
 * WHAT M557 (STAGE A2) CHANGED, and it is that stage's whole point. The chrome
 * sentences used to be inline `printf` literals in src/tui/jc_tui.c, so no unit
 * test could reach them and no language but English could ever have them. This
 * file therefore held **copies**, guarded by a smoke lint that checked the
 * copies still matched the source. Both are gone: the sentences are catalog
 * entries now, so the loop below measures them **in all five languages**
 * automatically, and a translation that does not fit fails the build the day it
 * lands rather than the day somebody notices.
 *
 * TWO THINGS THE LOOP HAS TO KNOW, because a catalog entry is not a rendered
 * line:
 *   1. A FORMAT SPECIFIER is not its own width. `%s` occupies two columns in the
 *      catalog and up to thirty on screen, so each entry carrying specifiers has
 *      a stated substitution allowance (below).
 *   2. An UNTRANSLATED entry is NULL by design (M557): `jc_msg()` falls back to
 *      English, so its rendered width is English's, already measured. Skipped
 *      rather than failed -- tests/test_msg.c owns the coverage count.
 */
#include "jc_test.h"
#include "jc_msg.h"
#include "jc_term.h"

#include <stdio.h>
#include <string.h>

/* The budget. 80 columns is the classic terminal, and chrome is printed with up
 * to a two-space indent, so a line may spend 78. WARN is a printed notice, not
 * a failure: an entry at 75 columns is fine today and will not survive being
 * translated into a longer language, which is information a translator needs
 * rather than a build break. */
#define CHROME_COLS_MAX  78
#define CHROME_COLS_WARN 66

/* Substitution allowances, per entry that carries specifiers. WIDEST PLAUSIBLE
 * values, not typical ones:
 *   a token count via jc_group_num  11  ("1,234,567,890")
 *   a model display string          30  ("fast (jlu/qwen3-coder-next)" = 27)
 *   a tool name                     24  ("run_terminal_command" = 20)
 *   a %.4f cost                      8  ("0.1234" = 6)
 *
 * THREE ENTRIES ARE EXEMPT because what follows them is CONTENT, not chrome: the
 * argument summary is capped at 200 bytes and the tool-result body at 360, and
 * content is the channel this project does not reflow (the operator's rule:
 * "within program code all symbols are important, and must be read"). Exempt is
 * recorded per entry rather than left implicit, so a fourth cannot join them
 * quietly. */
struct chrome_budget {
    enum jc_msg_id id;
    int allow;      /* columns the substitutions may add */
    int exempt;     /* 1: content follows, so unbounded by design */
};

static const struct chrome_budget g_budget[] = {
    { JC_MSG_TOKENS,            22, 0 },
    { JC_MSG_TOKENS_CACHED,     11, 0 },
    { JC_MSG_MODEL_RESPONDS,    30, 0 },
    { JC_MSG_TOOL_CALL_ARG,      0, 1 },   /* + a 200-byte arg summary  */
    { JC_MSG_TOOL_CALL,         24, 0 },
    { JC_MSG_TOOL_OK,           24, 1 },   /* + a 360-byte result body  */
    { JC_MSG_TOOL_FAIL,         24, 1 },   /* + a 360-byte result body  */
    { JC_MSG_SESSION_TOKENS,    22, 0 },
    { JC_MSG_SESSION_COST,       8, 0 },
    { JC_MSG_PRIV_PROMPT_ACC,    0, 0 },
    { JC_MSG_KINETIC_PROMPT_ACC, 0, 0 },
    /* M573: the refusal threshold, one or two digits. */
    { JC_MSG_DENY_HINT,          2, 0 }
};

/* An entry's FIXED width: its columns minus the conversion specifiers, which are
 * replaced rather than printed. "%%" is a literal percent and does print. */
static int chrome_fixed_cols(const char *s)
{
    const char *q = s;
    int spec_cols = 0;
    int total;
    if (s == NULL) { return 0; }
    total = jc_term_str_cols(s, strlen(s));
    while (*q != '\0') {
        if (*q == '%') {
            const char *start = q;
            q++;
            if (*q == '%') { q++; continue; }   /* prints one column */
            while (*q == '.' || *q == '-' || *q == '+' || *q == ' ' ||
                   (*q >= '0' && *q <= '9')) {
                q++;
            }
            if (*q != '\0') { q++; }
            spec_cols += (int)(q - start);
        } else {
            q++;
        }
    }
    return total - spec_cols;
}

void test_width(void)
{
    static const char *const lname[] = { "en", "de", "es", "ja", "zh" };
    static const enum jc_msg_lang langs[] = {
        JC_MSGL_EN, JC_MSGL_DE, JC_MSGL_ES, JC_MSGL_JA, JC_MSGL_ZH
    };
    unsigned int li;
    int id;
    int maxper[5];
    int nmeasured = 0;
    int over = 0;
    int widest = 0;
    int nexempt = 0;

    /* The instrument, proved before it is trusted. If jc_term_str_cols counted
     * BYTES, every ja/zh figure below would be wrong in the direction that hides
     * a defect. "\xe3\x81\x82" is U+3042 HIRAGANA A, East Asian Wide. */
    JC_CHECK(jc_term_str_cols("abc", 3) == 3);
    JC_CHECK(jc_term_str_cols("\xe3\x81\x82", 3) == 2);
    JC_CHECK(jc_term_str_cols("", 0) == 0);

    /* And the specifier walker, proved the same way: a bug in it would shrink
     * every fixed part and hide an over-budget line. */
    JC_CHECK(chrome_fixed_cols("%s x %.4f") == 3);   /* two spaces and the x */
    JC_CHECK(chrome_fixed_cols("100%% sure") == 10); /* "%%" prints one       */

    for (li = 0; li < 5; li++) { maxper[li] = 0; }

    printf("\n  chrome width in columns (UAX #11 + substitution allowance):\n");
    printf("  %-4s %4s %4s %4s %4s %4s\n", "id", "en", "de", "es", "ja", "zh");

    for (id = 0; id < (int)JC_MSG__COUNT; id++) {
        int w[5];
        int allow = 0;
        int exempt = 0;
        unsigned int bi;
        for (bi = 0; bi < sizeof(g_budget) / sizeof(g_budget[0]); bi++) {
            if (g_budget[bi].id == (enum jc_msg_id)id) {
                allow = g_budget[bi].allow;
                exempt = g_budget[bi].exempt;
            }
        }
        for (li = 0; li < 5; li++) {
            const char *s = jc_msg_raw(langs[li], (enum jc_msg_id)id);
            if (s == NULL || s[0] == '\0') {
                w[li] = -1;         /* untranslated: jc_msg serves English */
                continue;
            }
            w[li] = chrome_fixed_cols(s) + allow;
            nmeasured++;
            if (exempt) { nexempt++; continue; }
            if (w[li] > maxper[li]) { maxper[li] = w[li]; }
            if (w[li] > widest) { widest = w[li]; }
            if (w[li] > CHROME_COLS_MAX) {
                over++;
                printf("  OVER BUDGET: id %d / %s = %d columns (max %d): %s\n",
                       id, lname[li], w[li], CHROME_COLS_MAX, s);
            }
        }
        printf("  %-4d", id);
        for (li = 0; li < 5; li++) {
            if (w[li] < 0) { printf("    -"); } else { printf(" %4d", w[li]); }
        }
        printf("%s\n", exempt ? "   (exempt: content follows)" : "");
    }

    printf("  %-4s", "MAX");
    for (li = 0; li < 5; li++) { printf(" %4d", maxper[li]); }
    printf("   (non-exempt)\n");

    /* THE EXPANSION FIGURE, PER ENTRY AND IN BOTH UNITS. The first version of
     * this divided the per-language MAXIMA, which compares different entries --
     * `de` peaks on the bracket prompt and `en` on the accessible one -- and so
     * reported German as 18% SHORTER than English when it is +50% on the entry
     * they share. The percentage misleads the other way too: `ja` measures
     * +100%, which is DENIED going from 6 columns to 12. True, dramatic, and
     * harmless. What decides whether a line WRAPS is the absolute count. */
    {
        int worst[5];
        int wabs[5];
        for (li = 0; li < 5; li++) { worst[li] = 0; wabs[li] = 0; }
        for (id = 0; id < (int)JC_MSG__COUNT; id++) {
            const char *e = jc_msg_raw(JC_MSGL_EN, (enum jc_msg_id)id);
            int we = chrome_fixed_cols(e);
            if (we <= 0) { continue; }
            for (li = 1; li < 5; li++) {
                const char *s = jc_msg_raw(langs[li], (enum jc_msg_id)id);
                int w;
                int pct;
                if (s == NULL || s[0] == '\0') { continue; }
                w = chrome_fixed_cols(s);
                pct = (w - we) * 100 / we;
                if (pct > worst[li]) { worst[li] = pct; }
                if (w - we > wabs[li]) { wabs[li] = w - we; }
            }
        }
        printf("  worst per-entry expansion vs en, percent:  de %+d%%  "
               "es %+d%%  ja %+d%%  zh %+d%%\n",
               worst[1], worst[2], worst[3], worst[4]);
        printf("  worst per-entry expansion vs en, COLUMNS:  de %+d  es %+d  "
               "ja %+d  zh %+d   <- this is the one that wraps a line\n",
               wabs[1], wabs[2], wabs[3], wabs[4]);
        /* Data, not a tripwire -- but a language that suddenly doubled would
         * mean a translation went wrong, not that German grew. */
        JC_CHECK(worst[1] < 200);
    }

    /* ---- floors ----------------------------------------------------------
     * Without these the loop above passes trivially on an empty catalog, which
     * is the vacuous shape this project keeps finding. 23 ids; English and
     * German are complete and es/ja/zh are each missing twelve, so
     * 23 + 23 + 3*11 = 79 measurements. A MINIMUM, so a new entry does not
     * fail the build -- but one that does not fit does. (Arithmetic corrected
     * at M568: this comment still said "22 ids" and "4*11" from before M565
     * added an entry and M568 completed German. The FLOOR was right and the
     * explanation was stale, which is its own small lesson -- a number that
     * passes is not a number anybody re-derived.) */
    JC_CHECK(nmeasured >= 79);
    JC_CHECK(widest > 0);
    /* The exemption is fenced: three entries, now in TWO languages. A fourth
     * exempt entry, or a translation of one of these three, moves this number
     * and makes somebody look at whether the exemption is still right -- which
     * is exactly what happened at M568, when German translated all three and
     * this went 3 -> 6. Looked at, and still right: what follows those three
     * is CONTENT (a 200-byte argument summary, a 360-byte result body), and
     * content is unbounded in every language. */
    JC_CHECK(nexempt == 6);

    /* ---- the gate -------------------------------------------------------- */
    JC_CHECK(over == 0);

    /* ---- and the warning band, printed rather than failed ---------------- */
    for (id = 0; id < (int)JC_MSG__COUNT; id++) {
        for (li = 0; li < 5; li++) {
            const char *s = jc_msg_raw(langs[li], (enum jc_msg_id)id);
            int w;
            if (s == NULL || s[0] == '\0') { continue; }
            w = chrome_fixed_cols(s);
            if (w > CHROME_COLS_WARN && w <= CHROME_COLS_MAX) {
                printf("  near budget: id %d / %s = %d columns\n", id,
                       lname[li], w);
            }
        }
    }

    jc_msg_set_lang(JC_MSGL_EN);
}
