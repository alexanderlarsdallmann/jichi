/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_msg.h - runtime UI message catalog (M137).
 *
 * M121 localized the *documentation*; this is the program side, phased the
 * same way: a small compiled-in catalog of the highest-traffic interactive
 * strings (the tool-approval prompt -- a safety surface -- and the working
 * indicator), in the languages the docs already cover (en/de/es/ja/zh).
 * Reference output (help text, subcommands, errors) stays English-canonical,
 * exactly like the reference docs; more strings/languages can be added table
 * by table without touching call sites.
 *
 * No gettext / catalogs on disk: compiled-in tables keep the "libcurl + cJSON
 * only" dependency rule and work on machines with no locale data installed.
 * Non-English text is UTF-8 (hex-escaped so the source stays ASCII, like the
 * TUI glyphs); when the terminal locale is not UTF-8 the catalog falls back
 * to English rather than emit mojibake (see docs/ENCODING.md).
 *
 * An untranslated entry falls back to English, so a partially translated
 * table is safe.
 *
 * THE APPROVAL KEYS ARE A FIXED SUPERSET, and each translation advertises the
 * subset that reads best in it (M564). Accepted everywhere, in every language:
 *
 *     y / n / a / e / v        yes, no, always, edit, view
 *     1 / 0 / 8 / 3 / 5        the same five, in digits
 *
 * This used to read "the keys are never localized -- every translation must keep
 * them". The superset is a STRONGER invariant, not a weaker one: both sets work
 * in every language, so muscle memory and scripts driving a PTY are unaffected
 * anywhere, and a user who learned the digits can use them in an English session.
 *
 * WHY DIGITS EXIST AT ALL. The letters are ENGLISH initials, and four milestones
 * worked around that rather than solving it: `a wie immer` is false because `a`
 * is not immer's initial, so M552's "as in" cue cannot be translated, DIN 5009
 * was needed to name the letter instead, and the resulting German prompt measured
 * ~108 columns against M554's 78-column budget. Digits have no language, they are
 * the most reliably spoken tokens a TTS has, and 1/0/8/3/5 is phonetically
 * distinct in English, German and Japanese (one/zero/three/five/eight;
 * eins/null/drei/fuenf/acht; ichi/zero/san/go/hachi). Consecutive digits would
 * have collided -- German zwei/drei rhyme.
 *
 * ENGLISH KEEPS THE LETTERS in its own prompt: `y`/yes and `n`/no are
 * self-explaining where `3`/edit must be memorised, and English has no
 * translation problem to solve. The digit mapping beyond 1/0 is arbitrary, and
 * that is the price -- paid once per user rather than once per language.
 */
#ifndef JC_MSG_H
#define JC_MSG_H


#ifdef __cplusplus
extern "C" {
#endif
enum jc_msg_lang {
    JC_MSGL_EN = 0,
    JC_MSGL_DE,
    JC_MSGL_ES,
    JC_MSGL_JA,
    JC_MSGL_ZH
};

/* M566: THE CATALOG SERVES TWO FRONT-ENDS, not one. Until M566 every entry
 * below was rendered only by src/tui/jc_tui.c, and src/main.c's headless
 * renderers printed their own compact literals -- so the accessibility work of
 * M549-M565 and every translation reached the TUI alone. A `jichi -p` run with
 * --accessible still spoke brackets and arrows, and JICHI_LANG was never even
 * resolved on that route. When you add an entry, ask which renderers print it;
 * when you add a renderer, ask whether an entry already says this.
 */
enum jc_msg_id {
    JC_MSG_WORKING = 0,     /* the word shown while the model responds     */
    JC_MSG_ALLOW_PROMPT,    /* tool approval: "Allow? [y]es [n]o ..."      */
    JC_MSG_ALLOWED,         /* approval echo: approved once                */
    JC_MSG_ALLOWED_ALWAYS,  /* approval echo: approved for the session     */
    JC_MSG_ALLOWED_EDITED,  /* approval echo: approved with edited args    */
    JC_MSG_DENIED,          /* approval echo: rejected                     */
    JC_MSG_QUEUED,          /* type-ahead: this line will be sent next      */
    JC_MSG_QUEUE_FULL,      /* type-ahead: the queue bound was reached      */
    JC_MSG_QUEUE_UNSENT,    /* type-ahead: typed but never committed        */
    JC_MSG_QUEUE_DROPPED,   /* type-ahead: the queue was dropped (Ctrl-K)   */
    /* M551: the same approval prompt with NO brackets, for --accessible.
     * `[y]es  [n]o  [a]lways` is a *visual* affordance -- the key shown
     * inside the word it names. Read aloud it is not a prompt but a spelling
     * exercise: a screen reader announces "bracket y bracket e s bracket n
     * bracket o", which is what the operator's own listening test reported
     * as "every single character was read... the options were unintelligible
     * with the brackets". Appended, never inserted (see above). */
    JC_MSG_ALLOW_PROMPT_ACC,
    /* ---- M557 (stage A2): the CHROME sentences ------------------------
     * M553 rewrote jichi's own status lines as prose for accessible mode --
     * `[tokens in=20 out=5]` became "20 input tokens used, and 5 output tokens
     * used." -- and wrote them as inline `printf` literals in src/tui/jc_tui.c.
     * That made them **English-only by construction**: a German or Japanese user
     * got a translated approval prompt and English for everything else, and no
     * amount of translation work could change it, because there was nothing for
     * a translation to attach to. Moving them here is what makes the
     * multilingual half of docs/proposals/2026-08-accessibility-by-default.md
     * possible at all.
     *
     * THESE ENTRIES CARRY FORMAT SPECIFIERS, which the ten above do not, and
     * that is a hazard worth naming: a translator who changes `%s` to `%d`, or
     * reorders two `%s`, produces undefined behaviour rather than an odd
     * sentence. `tests/test_msg.c` therefore asserts that every translated
     * entry's specifier sequence is IDENTICAL to English's -- same count, same
     * order, same conversions. That check is the price of admission for putting
     * a format string in a catalog.
     *
     * WORD ORDER IS WHY EACH IS A WHOLE SENTENCE. Composing chrome from
     * fragments ("The tool " + name + " failed.") is the classic localisation
     * mistake: German puts the verb last and Japanese is SOV, so a fragment
     * order that reads in English cannot be reordered by a translator. Hence
     * TOOL_OK and TOOL_FAIL as separate whole sentences rather than one entry
     * with a substituted verb. */
    JC_MSG_TOKENS,          /* "%s input tokens used, and %s output ..."   */
    JC_MSG_TOKENS_CACHED,   /* "%s of those input tokens came from cache." */
    JC_MSG_MODEL_RESPONDS,  /* "Model %s responds with the following:"     */
    JC_MSG_TOOL_CALL_ARG,   /* "Calling the tool %s, with %s."             */
    JC_MSG_TOOL_CALL,       /* "Calling the tool %s."                      */
    JC_MSG_TOOL_OK,         /* "The tool %s finished successfully. "       */
    JC_MSG_TOOL_FAIL,       /* "The tool %s failed. "                      */
    JC_MSG_SESSION_TOKENS,  /* "This session used %s input tokens, and ..." */
    /* M566: SCOPE-NEUTRAL WORDING IS LOAD-BEARING HERE. Two callers print
     * this entry -- the TUI's end-of-session summary and the headless path's
     * PER-TURN cost line -- so a translation that says "this session" would
     * be a lie on one of them. Translate it as "the cost was ...", never
     * "diese Sitzung kostete ...". The adjacent SESSION_TOKENS has one caller
     * and may name the session freely. */
    JC_MSG_SESSION_COST,    /* "The cost was %.4f dollars."                */
    JC_MSG_PRIV_PROMPT_ACC, /* "Run this with elevated privilege? Press ..." */
    JC_MSG_KINETIC_PROMPT_ACC, /* "Allow this physical actuation? Press ..." */
    /* M565: an unrecognised keypress at an approval prompt is NOT AN ANSWER.
     * It used to deny -- safe, but it resolved a fence with a byte the user
     * never meant, and told the model "denied by the user" for a decision
     * nobody made. Now the prompt is re-shown with this notice. */
    JC_MSG_UNRECOGNISED_KEY,
    /* M570: announced when repeated denials of the same call END the run. The
     * operator's report is the whole specification: "And the 0 does not abort,
     * so I kept pressing it until this happened" -- seven prompts for one
     * change, each one read aloud in full. */
    JC_MSG_DENIED_STOP,
    /* M571: a REFUSAL is not a malfunction. The operator heard "The tool
     * edit_file failed. denied" after answering the prompt themselves -- their
     * own decision reported back as a fault. JC_MSG_TOOL_FAIL stays for real
     * failures; this is for anything jc_fail_classify calls JC_FAIL_DENIED,
     * which covers both a human's no and a fence's. */
    /* M572: printed after the FIRST refusal in a turn, so the way out is
     * discoverable exactly when it becomes relevant -- and nowhere else, since
     * the approval prompt is the most-repeated string in a session (M562) and
     * cannot afford a sixth advertised option.
     *
     * M573: it now states THE RULE as well as the escape, because the operator
     * asked the question that showed nobody knew it -- "the user, or agent has
     * to know about the three refusals rule". It carries %d rather than a
     * hard-coded 3, so a changed JC_DENY_STOP_AT cannot turn it into a lie.
     *
     * PHRASED AS A RULE, NOT A COUNTDOWN, deliberately: "two more refusals"
     * needs plural agreement in every translation, and a fact stated once is
     * less chatty for a listener than a number after every refusal. */
    JC_MSG_DENY_HINT,
    /* M578: the word that carries "this is a suggestion, not your text" when
     * dim cannot. Kept SHORT because it precedes the suggestion on one line. */
    JC_MSG_SUGGESTION,
    JC_MSG_TOOL_REFUSED,   /* "That key does nothing here. Try again:"    */
    JC_MSG__COUNT
};

/* Resolve the UI language. Precedence: $JICHI_LANG (a UI-specific override, so
 * the UI can stay English while answers are Japanese) > the config `language`
 * (M135's answer language, free-form: "Japanese", "Deutsch", "ja", ...) >
 * $LANG's language prefix ("de_DE.UTF-8" => de) > English. Unrecognized
 * values fall through to the next source. When `utf8_ok` is 0 (the terminal
 * locale is not UTF-8) any non-English result collapses to English, since
 * every non-English catalog contains UTF-8 bytes. Pure (env values are passed
 * in, not read); unit-tested. */
enum jc_msg_lang jc_msg_lang_resolve(const char *config_language,
                                     const char *jichi_lang_env,
                                     const char *lang_env,
                                     int utf8_ok);

/* Does `s` NAME a language with a catalog? 1 and *out set, or 0 and *out
 * untouched. Accepts a bare name ("de", "deutsch") or a locale value
 * ("de_DE.UTF-8"), matching jc_msg_lang_resolve's own rules.
 *
 * M567: resolve() cannot answer this, because it folds "no match" and "English"
 * into the same JC_MSGL_EN -- correct for choosing a catalog, useless for
 * `doctor`, which must tell "the locale asks for English" apart from "the
 * locale asks for nothing". Both cases end in an English-speaking screen
 * reader, so both matter, but they need different advice. */
int jc_msg_lang_match(const char *s, enum jc_msg_lang *out);

/* Set / get the process-wide catalog language (default English). Called once
 * at TUI startup and again when /language changes it; not thread-relevant
 * (the TUI is single-threaded; forked children inherit a COW copy). */
void jc_msg_set_lang(enum jc_msg_lang lang);
enum jc_msg_lang jc_msg_get_lang(void);

/* The message `id` in the current language; English when the entry is
 * untranslated. Never NULL. */
const char *jc_msg(enum jc_msg_id id);

/* The raw catalog entry for one language -- NULL when untranslated, with NO
 * English fallback. For tests and introspection: jc_msg() papers over a hole
 * by design (the phased i18n policy), so completeness can only be checked
 * here (M380). */
const char *jc_msg_raw(enum jc_msg_lang lang, enum jc_msg_id id);

#ifdef __cplusplus
}
#endif
#endif /* JC_MSG_H */
