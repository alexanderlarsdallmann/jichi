/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_msg.c - the runtime UI message catalog (M137).
 *
 * Pure and offline: language resolution takes the env values as parameters,
 * so nothing here calls setenv/getenv. The invariants that matter: the
 * approval keys ([y]/[n]/[a]/[e]/[v]) survive every translation, every id
 * resolves to a non-empty string in every language, and a non-UTF-8 terminal
 * collapses to English.
 */

#include "jc_test.h"
#include "jc_msg.h"

#include <string.h>

/* Collect a string's printf conversion characters in order: "a %s b %.4f" ->
 * "sf". Used to prove a translated format string is substitutable for the
 * English one (M557). Deliberately records the CONVERSION only, not the flags
 * or precision -- "%.4f" and "%f" are interchangeable at the call, "%d" and
 * "%s" are not. */
static void msg_specs(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    if (out == NULL || cap == 0) { return; }
    out[0] = '\0';
    if (s == NULL) { return; }
    while (*s != '\0' && o + 1 < cap) {
        if (*s == '%') {
            s++;
            if (*s == '%') { s++; continue; }   /* a literal percent */
            while (*s == '.' || *s == '-' || *s == '+' || *s == ' ' ||
                   (*s >= '0' && *s <= '9')) {
                s++;
            }
            if (*s != '\0') { out[o++] = *s++; }
        } else {
            s++;
        }
    }
    out[o] = '\0';
}

void test_msg(void)
{
    /* ---- resolution: precedence -------------------------------------- */

    /* ---- M569: advice that can actually be followed -------------------
     * The notice fires from queue_hold_end, whose NEXT TWO STATEMENTS free the
     * buffer -- so "press Enter to queue it" told the user to do something
     * that could not work, at the only moment it was ever said. The operator's
     * question found it: "is Enter the key to be pressed, or do other keys
     * have an effect, as well?"
     *
     * Pinned in both directions and in both languages: the key is still NAMED
     * (a listener needs to know which one queues), and the impossible
     * imperative is gone. Asserting only the absence would pass for an entry
     * that stopped mentioning Enter at all. */
    {
        jc_msg_set_lang(JC_MSGL_EN);
        JC_CHECK(strstr(jc_msg(JC_MSG_QUEUE_UNSENT), "Enter") != NULL);
        JC_CHECK(strstr(jc_msg(JC_MSG_QUEUE_UNSENT),
                        "press Enter to queue it") == NULL);
        jc_msg_set_lang(JC_MSGL_DE);
        JC_CHECK(strstr(jc_msg(JC_MSG_QUEUE_UNSENT), "Enter") != NULL);
        JC_CHECK(strstr(jc_msg(JC_MSG_QUEUE_UNSENT),
                        "mit Enter einreihen") == NULL);
        jc_msg_set_lang(JC_MSGL_EN);
    }

    /* ---- jc_msg_lang_match: NAMED versus merely resolved (M567) --------
     * The distinction resolve() cannot express, and doctor's warning depends
     * on entirely: an unmatched string and an English one both RESOLVE to
     * JC_MSGL_EN, and they need different advice -- "your locale asks for
     * English" versus "your locale asks for nothing", both of which end in an
     * English-speaking screen reader over German text. */
    {
        enum jc_msg_lang got = JC_MSGL_JA;   /* poisoned: must be overwritten */

        /* Names a language: 1, and *out is set. */
        JC_CHECK(jc_msg_lang_match("de", &got) == 1);
        JC_CHECK(got == JC_MSGL_DE);
        JC_CHECK(jc_msg_lang_match("deutsch", &got) == 1 && got == JC_MSGL_DE);
        JC_CHECK(jc_msg_lang_match("en", &got) == 1 && got == JC_MSGL_EN);

        /* A LOCALE VALUE, which is the form doctor actually receives -- the
         * charset and territory suffixes must be stripped, or every real $LANG
         * would read as "names nothing" and the warning would fire for
         * everybody. */
        JC_CHECK(jc_msg_lang_match("de_DE.UTF-8", &got) == 1);
        JC_CHECK(got == JC_MSGL_DE);
        JC_CHECK(jc_msg_lang_match("en_US.UTF-8", &got) == 1 &&
                 got == JC_MSGL_EN);
        JC_CHECK(jc_msg_lang_match("ja_JP.EUC-JP", &got) == 1 &&
                 got == JC_MSGL_JA);
        JC_CHECK(jc_msg_lang_match("de_DE@euro", &got) == 1 &&
                 got == JC_MSGL_DE);

        /* Names NOTHING: 0, and *out is left alone. Both asserted -- a
         * function that returned 0 while scribbling on *out would pass the
         * first half and corrupt doctor's comparison. */
        got = JC_MSGL_ZH;
        JC_CHECK(jc_msg_lang_match(NULL, &got) == 0);
        JC_CHECK(got == JC_MSGL_ZH);
        JC_CHECK(jc_msg_lang_match("", &got) == 0 && got == JC_MSGL_ZH);
        JC_CHECK(jc_msg_lang_match("C", &got) == 0 && got == JC_MSGL_ZH);
        JC_CHECK(jc_msg_lang_match("POSIX", &got) == 0 && got == JC_MSGL_ZH);
        JC_CHECK(jc_msg_lang_match("kl_GL.UTF-8", &got) == 0 &&
                 got == JC_MSGL_ZH);

        /* A NULL out is legal: the caller may only want the yes/no. */
        JC_CHECK(jc_msg_lang_match("de", NULL) == 1);
        JC_CHECK(jc_msg_lang_match("C", NULL) == 0);

        /* AND THE PAIR THAT IS THE WHOLE POINT: "C" and "en" both resolve to
         * English, and match() tells them apart. If this ever stops holding,
         * doctor's warning becomes either silent or universal. */
        JC_CHECK(jc_msg_lang_resolve(NULL, NULL, "C", 1) == JC_MSGL_EN);
        JC_CHECK(jc_msg_lang_resolve(NULL, NULL, "en", 1) == JC_MSGL_EN);
        JC_CHECK(jc_msg_lang_match("C", NULL) != jc_msg_lang_match("en", NULL));
    }

    /* Nothing set: English. */
    JC_CHECK(jc_msg_lang_resolve(NULL, NULL, NULL, 1) == JC_MSGL_EN);
    JC_CHECK(jc_msg_lang_resolve("", "", "", 1) == JC_MSGL_EN);

    /* The config answer language (M135) drives the UI, by code or name --
     * English name, native name, or ISO code all match. */
    JC_CHECK(jc_msg_lang_resolve("Japanese", NULL, NULL, 1) == JC_MSGL_JA);
    JC_CHECK(jc_msg_lang_resolve("ja", NULL, NULL, 1) == JC_MSGL_JA);
    JC_CHECK(jc_msg_lang_resolve("Deutsch", NULL, NULL, 1) == JC_MSGL_DE);
    JC_CHECK(jc_msg_lang_resolve("GERMAN", NULL, NULL, 1) == JC_MSGL_DE);
    JC_CHECK(jc_msg_lang_resolve("espa\xc3\xb1ol", NULL, NULL, 1)
             == JC_MSGL_ES);
    JC_CHECK(jc_msg_lang_resolve("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",
                                 NULL, NULL, 1) == JC_MSGL_JA); /* 日本語 */
    JC_CHECK(jc_msg_lang_resolve("\xe4\xb8\xad\xe6\x96\x87", NULL, NULL, 1)
             == JC_MSGL_ZH); /* 中文 */
    JC_CHECK(jc_msg_lang_resolve("Mandarin Chinese", NULL, NULL, 1)
             == JC_MSGL_EN); /* unrecognized free-form: English UI, and the
                              * answer language still reaches the model */

    /* $JICHI_LANG is the UI-specific override: UI English, answers Japanese. */
    JC_CHECK(jc_msg_lang_resolve("Japanese", "en", NULL, 1) == JC_MSGL_EN);
    JC_CHECK(jc_msg_lang_resolve(NULL, "zh", NULL, 1) == JC_MSGL_ZH);
    /* An unrecognized override falls through to the next source. */
    JC_CHECK(jc_msg_lang_resolve("Japanese", "tlh", NULL, 1) == JC_MSGL_JA);

    /* $LANG is the last resort, matched by its language prefix. */
    JC_CHECK(jc_msg_lang_resolve(NULL, NULL, "de_DE.UTF-8", 1) == JC_MSGL_DE);
    JC_CHECK(jc_msg_lang_resolve(NULL, NULL, "ja_JP.UTF-8", 1) == JC_MSGL_JA);
    JC_CHECK(jc_msg_lang_resolve(NULL, NULL, "en_US@euro", 1) == JC_MSGL_EN);
    JC_CHECK(jc_msg_lang_resolve(NULL, NULL, "C", 1) == JC_MSGL_EN);
    JC_CHECK(jc_msg_lang_resolve("Japanese", NULL, "de_DE.UTF-8", 1)
             == JC_MSGL_JA); /* config beats $LANG */

    /* A non-UTF-8 terminal collapses any non-English pick to English
     * (every non-English catalog contains UTF-8 bytes). */
    JC_CHECK(jc_msg_lang_resolve("Japanese", NULL, NULL, 0) == JC_MSGL_EN);
    JC_CHECK(jc_msg_lang_resolve(NULL, "de", NULL, 0) == JC_MSGL_EN);

    /* ---- the catalog itself ------------------------------------------ */

    {
        static const enum jc_msg_lang langs[] = {
            JC_MSGL_EN, JC_MSGL_DE, JC_MSGL_ES, JC_MSGL_JA, JC_MSGL_ZH
        };
        static const char *const lname[] = { "en", "de", "es", "ja", "zh" };
        unsigned int li;
        int id;
        for (li = 0; li < sizeof(langs) / sizeof(langs[0]); li++) {
            jc_msg_set_lang(langs[li]);
            JC_CHECK(jc_msg_get_lang() == langs[li]);
            /* The GETTER never yields NULL/empty -- the fallback contract.
             * This is all this loop can see: jc_msg() substitutes English
             * for a hole before any assertion looks (M380). */
            for (id = 0; id < (int)JC_MSG__COUNT; id++) {
                const char *s = jc_msg((enum jc_msg_id)id);
                JC_CHECK(s != NULL && s[0] != '\0');
            }
            /* THE CATALOG'S TRANSLATION COVERAGE, counted rather than
             * asserted absolute -- and the change from M380's version is
             * deliberate, so read why before relaxing it further.
             *
             * M380 added `raw != NULL` for every id because C89 positional
             * initialisation compiles a FORGOTTEN translation as NULL in
             * silence. That guard must survive. But M557 (stage A2) moved
             * eleven chrome sentences into the catalog with **no translations
             * yet**, deliberately: the operator's instruction was "let's do
             * English properly, first", and the alternative -- copying the
             * English text into the German table -- produces text that LOOKS
             * translated and is not. That is worse than a gap, because nobody
             * knows to fix it. The operator's verdict on the German entries
             * that do exist makes the point: "eingereiht/verworfen is not
             * idiomatic, and not the correct tense depending on the context."
             *
             * So an untranslated entry is a NULL, `jc_msg()` falls back to
             * English at runtime (its documented contract), and the COUNT is
             * pinned here and printed. A deliberate gap is visible; an
             * accidental one still fails the build, because the number moves.
             *
             * The figures are the A4 (German) and A6 (Japanese) to-do lists. */
            {
                int untranslated = 0;
                for (id = 0; id < (int)JC_MSG__COUNT; id++) {
                    const char *raw = jc_msg_raw(langs[li],
                                                 (enum jc_msg_id)id);
                    if (raw == NULL || raw[0] == '\0') { untranslated++; }
                }
                printf("  catalog: %s has %d of %d entries untranslated\n",
                       lname[li], untranslated, (int)JC_MSG__COUNT);
                /* English is the source and must never have a hole. */
                if (li == 0) {
                    JC_CHECK(untranslated == 0);
                } else if (li == 1) {
                    /* GERMAN IS COMPLETE as of M568 -- the operator is a native
                     * speaker and approved the twelve remaining entries by ear:
                     * "The German phrases read okay, so we are going to use
                     * them, for now, and may change them, later."
                     *
                     * Pinned at 0 rather than left in the >= band, so LOSING a
                     * German entry fails the build the way gaining one did. */
                    JC_CHECK(untranslated == 0);
                } else {
                    /* es/ja/zh: the eleven M557 chrome sentences plus M565's
                     * unrecognised-key notice, and nothing else. A different
                     * number means either a translation landed (raise the
                     * figure, and say so) or one was lost.
                     *
                     * M565 raised it from 11 to 12, and the gate is what said
                     * so: adding an English-only entry reddened this in all four
                     * languages, which is the check behaving exactly as
                     * designed. A count that moves silently would be no guard at
                     * all.
                     *
                     * M570 raised it 12 -> 13, and the gate said so again:
                     * JC_MSG_DENIED_STOP landed in English and German only, so
                     * es/ja/zh each gained a hole. German is written alongside
                     * every new entry now, which is why its pin above stays 0
                     * while this number climbs.
                     *
                     * M571 raised it 13 -> 14: JC_MSG_TOOL_REFUSED, again
                     * English and German only. M572 raised it to 15 with
                     * JC_MSG_DENY_HINT, and M578 to 16 with JC_MSG_SUGGESTION. */
                    JC_CHECK(untranslated == 16);
                }
            }

            /* FORMAT SPECIFIERS MUST MATCH ENGLISH, EXACTLY. The chrome
             * entries carry `%s` and `%.4f`, which the ten older entries do
             * not, and a catalog with format strings in it has a failure mode
             * the others never had: a translator who writes `%d` for `%s`, or
             * swaps two `%s`, causes UNDEFINED BEHAVIOUR at the printf rather
             * than an odd sentence.
             *
             * So this compares the specifier SEQUENCE -- count, order and
             * conversion character -- against English for every entry a
             * language actually provides. It is the price of admission for
             * putting a format string in a catalog, and it is why the chrome
             * could be moved here at all. */
            for (id = 0; id < (int)JC_MSG__COUNT; id++) {
                const char *en = jc_msg_raw(JC_MSGL_EN, (enum jc_msg_id)id);
                const char *tr = jc_msg_raw(langs[li], (enum jc_msg_id)id);
                char espec[16], tspec[16];
                if (tr == NULL || tr[0] == '\0') { continue; }
                msg_specs(en, espec, sizeof espec);
                msg_specs(tr, tspec, sizeof tspec);
                JC_CHECK(strcmp(espec, tspec) == 0);
            }
            /* The approval KEYS are never localized: whatever the language,
             * [y]/[n]/[a]/[e]/[v] must appear in the allow prompt, since they
             * are the accepted keypresses. */
            {
                const char *p = jc_msg(JC_MSG_ALLOW_PROMPT);
                JC_CHECK(strstr(p, "[y]") != NULL);
                JC_CHECK(strstr(p, "[n]") != NULL);
                JC_CHECK(strstr(p, "[a]") != NULL);
                JC_CHECK(strstr(p, "[e]") != NULL);
                JC_CHECK(strstr(p, "[v]") != NULL);
            }
            /* M551: the ACCESSIBLE prompt carries the same five keys and NOT
             * the brackets. The two assertions are complementary and both are
             * needed: the bracket check above passes for a string a screen
             * reader spells out character by character, and "no brackets"
             * alone passes for a string that lost a key.
             *
             * Each key must be a SPACE-DELIMITED token, which is a stronger
             * claim than merely present: strstr(p, "y") matches inside "yes",
             * "always" and "yo", so a table that dropped the standalone key
             * would pass a bare-substring check. It also matters most in
             * ja/zh, where no space separates a letter from the word beside
             * it unless the translation puts one there. */
            {
                const char *p = jc_msg(JC_MSG_ALLOW_PROMPT_ACC);
                /* M568: EITHER NOTATION PER POSITION, which is the same
                 * relaxation M564 made to the bracket assertion above and did
                 * not make here -- the gap surfaced the day German switched to
                 * digits, as five failures on one line. The accepted keys are a
                 * fixed superset (y/n/a/e/v AND 1/0/8/3/5, every language);
                 * what a translation ADVERTISES is its own choice, so the test
                 * must check that each of the five decisions is announced
                 * somehow, not that a particular glyph was picked.
                 * COMPLETENESS is still asserted: dropping the view option
                 * entirely reddens exactly one pair. */
                static const char *const let[] = {
                    " y ", " n ", " a ", " e ", " v "
                };
                static const char *const dig[] = {
                    " 1 ", " 0 ", " 8 ", " 3 ", " 5 "
                };
                unsigned int ti;
                JC_CHECK(strchr(p, '[') == NULL);
                JC_CHECK(strchr(p, ']') == NULL);
                for (ti = 0; ti < sizeof(let) / sizeof(let[0]); ti++) {
                    JC_CHECK(strstr(p, let[ti]) != NULL ||
                             strstr(p, dig[ti]) != NULL);
                }
                /* And it is not simply a copy of the bracket form. */
                JC_CHECK(strcmp(p, jc_msg(JC_MSG_ALLOW_PROMPT)) != 0);
            }
        }
    }

    /* M552: the ENGLISH accessible prompt must make each key RECOVERABLE from
     * the word beside it. The defect this pins was reported by ear: "the single
     * vowels a, and e are difficult to make out when the options are read."
     * `y`, `n` and `v` are consonants and survived; `a` and `e` did not -- and
     * "a for always" is additionally ambiguous with the indefinite article, so
     * a listener cannot tell the letter from ordinary grammar.
     *
     * The property that fixes it is not a phrasing but a RELATION: the key is
     * followed by a word that begins with the same letter, so hearing "as in
     * always" identifies A even if the A itself was lost. That is what this
     * asserts -- " y as in y", " a as in a" and so on -- rather than the
     * sentence, which is free to change.
     *
     * ENGLISH ONLY, and that is a real limit rather than laziness. The keys are
     * never localized (jc_msg.h), so they are the ENGLISH words' initials: the
     * German option words are ja/nein/immer/bearbeiten/ansehen, and "a wie
     * immer" would be false -- `a` is not the first letter of `immer`. No cue
     * exists there that is both correct and in that language, and no
     * screen-reader test has been run in de/es/ja/zh to design one against. */
    {
        /* M554: EITHER CONNECTOR, because the property is the relation and not
         * the wording. "as in" on all five keys measured 81 columns and wrapped
         * an 80-column terminal, so the cue is now on `a` and `e` only -- and
         * this assertion had pinned the sentence again, exactly the mistake
         * recorded three times already. What must hold for every key is that
         * the letter is followed by a word BEGINNING WITH THAT LETTER, whether
         * the connector is "for" or "as in". */
        static const char *const cue_for[] = {
            " y for y", " n for n", " a for a", " e for e", " v for v"
        };
        static const char *const cue_asin[] = {
            " y as in y", " n as in n", " a as in a",
            " e as in e", " v as in v"
        };
        const char *p;
        unsigned int ci;
        jc_msg_set_lang(JC_MSGL_EN);
        p = jc_msg(JC_MSG_ALLOW_PROMPT_ACC);
        for (ci = 0; ci < sizeof(cue_for) / sizeof(cue_for[0]); ci++) {
            JC_CHECK(strstr(p, cue_for[ci]) != NULL
                     || strstr(p, cue_asin[ci]) != NULL);
        }
        /* And at least one key still carries the STRONGER cue, so a build that
         * dropped "as in" everywhere -- reverting M552 -- is not silently fine.
         * `a` is the one the operator named first. */
        JC_CHECK(strstr(p, " a as in a") != NULL);
    }

    /* Spot-check content: Japanese approval echo says 許可 (permitted). */
    jc_msg_set_lang(JC_MSGL_JA);
    JC_CHECK(strstr(jc_msg(JC_MSG_ALLOWED), "\xe8\xa8\xb1\xe5\x8f\xaf")
             != NULL);
    /* German denial. */
    jc_msg_set_lang(JC_MSGL_DE);
    JC_CHECK(strcmp(jc_msg(JC_MSG_DENIED), "abgelehnt") == 0);

    /* Out-of-range ids return "" (never NULL, never a crash). */
    JC_CHECK(jc_msg((enum jc_msg_id)-1) != NULL);
    JC_CHECK(jc_msg((enum jc_msg_id)JC_MSG__COUNT)[0] == '\0');

    /* Leave the process-wide default as tests found it. */
    jc_msg_set_lang(JC_MSGL_EN);
}
