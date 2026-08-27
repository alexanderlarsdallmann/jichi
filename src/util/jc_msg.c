/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_msg.c - runtime UI message catalog (M137; see jc_msg.h).
 *
 * Non-ASCII text is UTF-8 written as hex escapes (the source file stays
 * ASCII, like the TUI glyphs); the rendered string is in the comment beside
 * each entry. When editing a translation, keep the [y]/[n]/[a]/[e]/[v] keys
 * literally -- they are the accepted keypresses and are never localized.
 */
#include "jc_msg.h"

#include <stddef.h>

static enum jc_msg_lang g_lang = JC_MSGL_EN;

static const char *const msg_en[JC_MSG__COUNT] = {
    "working",
    "Allow? [y]es  [n]o  [a]lways  [e]dit  [v]iew",
    "allowed",
    "allowed (always this session)",
    "allowed (edited)",
    "denied",
    "queued for the next step",
    "queue full, line dropped",
    /* M569: NOT "press Enter to queue it". This notice is printed from
     * queue_hold_end, and the next two statements free the buffer -- so at the
     * moment a user hears it, Enter cannot queue that text and never could.
     * The operator asked the question that found it: "is Enter the key to be
     * pressed, or do other keys have an effect, as well?" Enter is the only key
     * that QUEUES; Backspace, Ctrl-U (clear the line) and Ctrl-K (drop what is
     * already queued) also act. Advice that cannot be followed is worse than
     * none, because a listener cannot see that the text is already gone. */
    "not sent, and Enter is what queues a line. Retype:",
    "queued input dropped",
    /* M552: "a for always" is AMBIGUOUS WITH ENGLISH GRAMMAR -- a listener
      * cannot tell the letter A from the indefinite article, so it is swallowed,
      * and `e` is a weak sound on its own. Both were reported by ear: "the
      * single vowels a, and e are difficult to make out when the options are
      * read." The consonants y/n/v survived; only the vowels failed.
      *
      * "as in" fixes it by making the WORD identify the letter: even if `a`
      * blurs, "as in always" says which letter it was. The keys are the words'
      * own initials, which is why the bracket form worked visually -- so the
      * cue costs no new vocabulary. This is the NATO-alphabet principle, and
      * the reason it CANNOT be translated is below. */
    /* M554: "as in" on ALL FIVE keys measured 81 COLUMNS and wrapped an
      * 80-column terminal -- a measured harm to both audiences, since some
      * readers re-announce a wrapped line and visual alignment breaks. The cue
      * is now on the two keys that actually needed it and no others, which is
      * 75 columns and is what the evidence supported all along: the operator
      * reported "the single vowels a, and e are difficult to make out", and
      * y/n/v are consonants that survived. Applying it uniformly was my choice
      * for tidiness, and tidiness bought a wrapped line.
      *
      * The pattern break is arguably a FEATURE for a listener -- "as in" marks
      * exactly the two letters that need help -- but that is a guess, and the
      * next listening test is what settles it. What is not a guess is the
      * width. */
    ("Allow? Press y for yes, n for no, a as in always, "
        "e as in edit, v for view."),
    /* ---- M557: the chrome sentences (see jc_msg.h) ------------------- */
    "%s input tokens used, and %s output tokens used.",
    "%s of those input tokens came from cache.",
    "Model %s responds with the following:",
    "Calling the tool %s, with %s.",
    "Calling the tool %s.",
    "The tool %s finished successfully. ",
    "The tool %s failed. ",
    "This session used %s input tokens, and %s output tokens.",
    "The cost was %.4f dollars.",
    ("Run this with elevated privilege? "
        "Press y as in yes, n as in no."),
    ("Allow this physical actuation? "
        "Press y as in yes, n as in no."),
    "That key does nothing here. Try again.",
    "Denied repeatedly, so this run is stopping. Nothing was changed.",
    "This run stops after %d refusals in a row. Control C stops it now.",
    "Suggestion, press Tab to accept:",
    "The tool %s was refused. "
};

/* THE FOUR TABLES BELOW STOP AFTER THE ELEVENTH ENTRY, ON PURPOSE.
 *
 * C89 positional initialisation leaves the remaining slots NULL, and
 * `jc_msg()` already falls back to English for a NULL or empty entry -- the
 * documented "phased translation" contract. So an untranslated chrome sentence
 * is a NULL, a runtime fallback to English, and a number the build prints,
 * rather than a copy of the English text sitting in the German table pretending
 * to be German.
 *
 * That distinction matters because of what the operator said about the German
 * entries that DO exist: "eingereiht/verworfen is not idiomatic, and not the
 * correct tense depending on the context." Text that looks translated and is
 * not is worse than an honest gap, because nobody knows to fix it.
 *
 * M380 added a completeness check here after a forgotten translation compiled
 * as NULL in silence. That guard is kept and made countable rather than
 * absolute: `tests/test_msg.c` asserts the number of untranslated entries per
 * language against a recorded figure and PRINTS it, so a deliberate gap is
 * visible and an accidental one still fails the build. The count is the
 * translation to-do list, and it is stages A4 (German, with a native speaker)
 * and A6 (Japanese, with the review protocol). */

static const char *const msg_de[JC_MSG__COUNT] = {
    "arbeite",
    "Erlauben? [y] ja  [n] nein  [a] immer  [e] bearbeiten  [v] ansehen",
    "erlaubt",
    "erlaubt (immer in dieser Sitzung)",
    "erlaubt (bearbeitet)",
    "abgelehnt",
    /* für den nächsten Schritt eingereiht */
    ("f\xc3\xbcr den n\xc3\xa4"
        "chsten Schritt eingereiht"),
    /* Warteschlange voll, Zeile verworfen */
    "Warteschlange voll, Zeile verworfen",
    /* nicht gesendet. Enter reiht eine Zeile ein. Neu eingeben:
     * M569: same correction as the English -- the old wording told the user to
     * press Enter at the moment the text had already been discarded. */
    ("nicht gesendet. Enter reiht eine Zeile ein. "
        "Neu eingeben:"),
    /* eingereihte Eingabe verworfen */
    "eingereihte Eingabe verworfen",
    /* M568: THE DIGITS, WHICH DISSOLVED THE PROBLEM THIS ENTRY USED TO
     * APOLOGISE FOR. M552's comment here explained at length why no "wie" cue
     * was possible: the English entry says "a as in always" and that works
     * only because the keys are the ENGLISH words' initials, so "a wie immer"
     * is simply false -- `a` is not the first letter of `immer`. Four
     * milestones worked around that. M564 made 1/0/8/3/5 accepted in every
     * language alongside y/n/a/e/v, and a digit needs no cue at all.
     *
     * Approved by ear by the operator, a native speaker: "The German phrases
     * read okay, so we are going to use them, for now, and may change them,
     * later." 57 columns against M554's 78-column budget -- the letter form it
     * replaces would have needed ~108 with DIN 5009 letter names.
     *
     * The letters still WORK in German; this only changes what is announced. */
    "Erlauben? 1 ja, 0 nein, 8 immer, 3 bearbeiten, 5 ansehen.",
    /* %s Eingabe- und %s Ausgabetoken verbraucht. */
    "%s Eingabe- und %s Ausgabetoken verbraucht.",
    /* %s davon aus dem Cache. */
    "%s davon aus dem Cache.",
    /* Modell %s antwortet Folgendes: */
    "Modell %s antwortet Folgendes:",
    /* Rufe das Werkzeug %s auf, mit %s. */
    "Rufe das Werkzeug %s auf, mit %s.",
    /* Rufe das Werkzeug %s auf. */
    "Rufe das Werkzeug %s auf.",
    /* Das Werkzeug %s war erfolgreich. -- the trailing space is load-bearing:
     * the tool's own output follows it on the same line. */
    "Das Werkzeug %s war erfolgreich. ",
    /* Das Werkzeug %s ist fehlgeschlagen. (trailing space: output follows) */
    "Das Werkzeug %s ist fehlgeschlagen. ",
    /* Diese Sitzung verbrauchte %s Eingabe- und %s Ausgabetoken. -- the first
     * draft named the tokens twice and measured 83 columns, over budget;
     * test_width refused it before it landed. This is the elided-compound
     * form, 76 with substitutions. */
    "Diese Sitzung verbrauchte %s Eingabe- und %s Ausgabetoken.",
    /* Die Kosten betrugen %.4f Dollar. -- DELIBERATELY SCOPE-NEUTRAL. Two
     * callers print this: the TUI per session and the headless path per turn
     * (M566). "Diese Sitzung kostete ..." would be a lie on one of them. */
    "Die Kosten betrugen %.4f Dollar.",
    /* Mit erhöhten Rechten ausführen? 1 ja, 0 nein. */
    "Mit erh\xc3\xb6hten Rechten ausf\xc3\xbchren? 1 ja, 0 nein.",
    /* Diese physische Bewegung erlauben? 1 ja, 0 nein. */
    "Diese physische Bewegung erlauben? 1 ja, 0 nein.",
    /* Diese Taste hat hier keine Funktion. Bitte erneut versuchen. */
    "Diese Taste hat hier keine Funktion. Bitte erneut versuchen.",
    /* Mehrfach abgelehnt, daher wird dieser Lauf beendet. Nichts wurde
     * geändert. */
    ("Mehrfach abgelehnt, daher wird dieser Lauf beendet. "
        "Nichts wurde ge\xc3\xa4ndert."),
    /* Dieser Lauf endet nach %d Ablehnungen in Folge. Strg C beendet ihn
     * sofort. (No hex escape needed here any more: the M572 wording had
     * "dr\xc3\xbccken" and hit the split rule; this one has no umlaut.) */
    ("Dieser Lauf endet nach %d Ablehnungen in Folge. "
        "Strg C beendet ihn sofort."),
    /* Vorschlag, mit Tab übernehmen: */
    /* Split before "bernehmen": \xbcb reads as one 3-digit escape. Same rule,
     * same author, second time this session -- and the compiler caught it both
     * times, which is the argument for a toolchain guard over a remembered
     * one. */
    ("Vorschlag, mit Tab \xc3\xbc" "bernehmen:"),
    /* Das Werkzeug %s wurde abgelehnt. (trailing space: output follows) */
    "Das Werkzeug %s wurde abgelehnt. "
};

static const char *const msg_es[JC_MSG__COUNT] = {
    "trabajando",
    /* ¿Permitir? [y] sí  [n] no  [a] siempre  [e] editar  [v] ver
     * (multi-line entries are parenthesized: clang's -Wstring-concatenation
     * treats bare adjacent literals in an array initializer as a suspected
     * missing comma) */
    ("\xc2\xbfPermitir? [y] s\xc3\xad  [n] no  [a] siempre  [e] editar  "
        "[v] ver"),
    "permitido",
    /* permitido (siempre en esta sesión) */
    "permitido (siempre en esta sesi\xc3\xb3n)",
    "permitido (editado)",
    "denegado",
    /* en cola para el siguiente paso */
    "en cola para el siguiente paso",
    /* cola llena, línea descartada */
    "cola llena, l\xc3\xadnea descartada",
    /* sin enviar, pulsa Enter para encolarlo */
    "sin enviar, pulsa Enter para encolarlo",
    /* entrada en cola descartada */
    "entrada en cola descartada",
    /* ¿Permitir? y sí  n no  a siempre  e editar  v ver */
    "\xc2\xbfPermitir? y s\xc3\xad  n no  a siempre  e editar  v ver"
};

static const char *const msg_ja[JC_MSG__COUNT] = {
    /* 作業中 */
    "\xe4\xbd\x9c\xe6\xa5\xad\xe4\xb8\xad",
    /* 許可しますか? [y] はい  [n] いいえ  [a] 常に  [e] 編集  [v] 表示 */
    ("\xe8\xa8\xb1\xe5\x8f\xaf\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x99\xe3\x81\x8b"
        "? [y] \xe3\x81\xaf\xe3\x81\x84  [n] \xe3\x81\x84\xe3\x81\x84"
        "\xe3\x81\x88  [a] \xe5\xb8\xb8\xe3\x81\xab  [e] \xe7\xb7\xa8"
        "\xe9\x9b\x86  [v] \xe8\xa1\xa8\xe7\xa4\xba"),
    /* 許可しました */
    "\xe8\xa8\xb1\xe5\x8f\xaf\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f",
    /* 許可しました（このセッションでは常に） */
    ("\xe8\xa8\xb1\xe5\x8f\xaf\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f"
        "\xef\xbc\x88\xe3\x81\x93\xe3\x81\xae\xe3\x82\xbb\xe3\x83\x83"
        "\xe3\x82\xb7\xe3\x83\xa7\xe3\x83\xb3\xe3\x81\xa7\xe3\x81\xaf"
        "\xe5\xb8\xb8\xe3\x81\xab\xef\xbc\x89"),
    /* 許可しました（編集済み） */
    ("\xe8\xa8\xb1\xe5\x8f\xaf\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f"
        "\xef\xbc\x88\xe7\xb7\xa8\xe9\x9b\x86\xe6\xb8\x88\xe3\x81\xbf"
        "\xef\xbc\x89"),
    /* 拒否しました */
    "\xe6\x8b\x92\xe5\x90\xa6\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f",
    /* 次のステップで送信します */
    ("\xe6\xac\xa1\xe3\x81\xae\xe3\x82\xb9\xe3\x83\x86\xe3\x83\x83"
        "\xe3\x83\x97\xe3\x81\xa7\xe9\x80\x81\xe4\xbf\xa1\xe3\x81\x97"
        "\xe3\x81\xbe\xe3\x81\x99"),
    /* キューが一杯です。行を破棄しました */
    ("\xe3\x82\xad\xe3\x83\xa5\xe3\x83\xbc\xe3\x81\x8c\xe4\xb8\x80"
        "\xe6\x9d\xaf\xe3\x81\xa7\xe3\x81\x99\xe3\x80\x82\xe8\xa1\x8c"
        "\xe3\x82\x92\xe7\xa0\xb4\xe6\xa3\x84\xe3\x81\x97\xe3\x81\xbe"
        "\xe3\x81\x97\xe3\x81\x9f"),
    /* 未送信です。Enter でキューに追加 */
    ("\xe6\x9c\xaa\xe9\x80\x81\xe4\xbf\xa1\xe3\x81\xa7\xe3\x81\x99"
        "\xe3\x80\x82"
        "Enter \xe3\x81\xa7\xe3\x82\xad\xe3\x83\xa5\xe3\x83\xbc\xe3\x81\xab"
        "\xe8\xbf\xbd\xe5\x8a\xa0"),
    /* キューの入力を破棄しました */
    ("\xe3\x82\xad\xe3\x83\xa5\xe3\x83\xbc\xe3\x81\xae\xe5\x85\xa5"
        "\xe5\x8a\x9b\xe3\x82\x92\xe7\xa0\xb4\xe6\xa3\x84\xe3\x81\x97"
        "\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f"),
    /* 許可しますか? y はい  n いいえ  a 常に  e 編集  v 表示 */
    ("\xe8\xa8\xb1\xe5\x8f\xaf\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x99\xe3\x81\x8b"
        "? y \xe3\x81\xaf\xe3\x81\x84  n \xe3\x81\x84\xe3\x81\x84"
        "\xe3\x81\x88  a \xe5\xb8\xb8\xe3\x81\xab  e \xe7\xb7\xa8"
        "\xe9\x9b\x86  v \xe8\xa1\xa8\xe7\xa4\xba")
};

static const char *const msg_zh[JC_MSG__COUNT] = {
    /* 处理中 */
    "\xe5\xa4\x84\xe7\x90\x86\xe4\xb8\xad",
    /* 允许吗? [y] 是  [n] 否  [a] 总是  [e] 编辑  [v] 查看 */
    ("\xe5\x85\x81\xe8\xae\xb8\xe5\x90\x97? [y] \xe6\x98\xaf  [n] "
        "\xe5\x90\xa6  [a] \xe6\x80\xbb\xe6\x98\xaf  [e] \xe7\xbc\x96"
        "\xe8\xbe\x91  [v] \xe6\x9f\xa5\xe7\x9c\x8b"),
    /* 已允许 */
    "\xe5\xb7\xb2\xe5\x85\x81\xe8\xae\xb8",
    /* 已允许（本会话中始终） */
    ("\xe5\xb7\xb2\xe5\x85\x81\xe8\xae\xb8\xef\xbc\x88\xe6\x9c\xac\xe4\xbc\x9a"
        "\xe8\xaf\x9d\xe4\xb8\xad\xe5\xa7\x8b\xe7\xbb\x88\xef\xbc\x89"),
    /* 已允许（已编辑） */
    ("\xe5\xb7\xb2\xe5\x85\x81\xe8\xae\xb8\xef\xbc\x88\xe5\xb7\xb2\xe7\xbc\x96"
        "\xe8\xbe\x91\xef\xbc\x89"),
    /* 已拒绝 */
    "\xe5\xb7\xb2\xe6\x8b\x92\xe7\xbb\x9d",
    /* 已排入队列，下一步发送 */
    ("\xe5\xb7\xb2\xe6\x8e\x92\xe5\x85\xa5\xe9\x98\x9f\xe5\x88\x97"
        "\xef\xbc\x8c\xe4\xb8\x8b\xe4\xb8\x80\xe6\xad\xa5\xe5\x8f\x91"
        "\xe9\x80\x81"),
    /* 队列已满，已丢弃该行 */
    ("\xe9\x98\x9f\xe5\x88\x97\xe5\xb7\xb2\xe6\xbb\xa1\xef\xbc\x8c"
        "\xe5\xb7\xb2\xe4\xb8\xa2\xe5\xbc\x83\xe8\xaf\xa5\xe8\xa1\x8c"),
    /* 未发送，按 Enter 加入队列 */
    ("\xe6\x9c\xaa\xe5\x8f\x91\xe9\x80\x81\xef\xbc\x8c\xe6\x8c\x89 Enter "
        "\xe5\x8a\xa0\xe5\x85\xa5\xe9\x98\x9f\xe5\x88\x97"),
    /* 已丢弃排队的输入 */
    ("\xe5\xb7\xb2\xe4\xb8\xa2\xe5\xbc\x83\xe6\x8e\x92\xe9\x98\x9f"
        "\xe7\x9a\x84\xe8\xbe\x93\xe5\x85\xa5"),
    /* 允许吗? y 是  n 否  a 总是  e 编辑  v 查看 */
    ("\xe5\x85\x81\xe8\xae\xb8\xe5\x90\x97? y \xe6\x98\xaf  n "
        "\xe5\x90\xa6  a \xe6\x80\xbb\xe6\x98\xaf  e \xe7\xbc\x96"
        "\xe8\xbe\x91  v \xe6\x9f\xa5\xe7\x9c\x8b")
};

static const char *const *table_for(enum jc_msg_lang l)
{
    switch (l) {
    case JC_MSGL_DE: return msg_de;
    case JC_MSGL_ES: return msg_es;
    case JC_MSGL_JA: return msg_ja;
    case JC_MSGL_ZH: return msg_zh;
    default:         return msg_en;
    }
}

/* ---- language resolution ------------------------------------------------ */

/* Case-insensitive ASCII string equality (multibyte UTF-8 alias bytes are
 * outside a-z/A-Z, so they compare exactly). */
static int ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Match one name (a code, an English name, or a native name) to a catalog
 * language. Returns -1 when unrecognized. Native-name aliases are the same
 * UTF-8 the docs' i18n directory uses. */
static int match_name(const char *s)
{
    static const char *const en_alias[] = { "en", "english", 0 };
    static const char *const de_alias[] = { "de", "german", "deutsch", 0 };
    static const char *const es_alias[] = {
        "es", "spanish", "espanol",
        "espa\xc3\xb1ol", /* español */
        0
    };
    static const char *const ja_alias[] = {
        "ja", "japanese", "nihongo",
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", /* 日本語 */
        0
    };
    static const char *const zh_alias[] = {
        "zh", "zh-cn", "zh-tw", "zh-hans", "chinese", "mandarin",
        "\xe4\xb8\xad\xe6\x96\x87", /* 中文 */
        0
    };
    static const struct {
        const char *const *alias;
        int lang;
    } tab[] = {
        { en_alias, JC_MSGL_EN },
        { de_alias, JC_MSGL_DE },
        { es_alias, JC_MSGL_ES },
        { ja_alias, JC_MSGL_JA },
        { zh_alias, JC_MSGL_ZH }
    };
    unsigned int i;
    int j;

    if (s == NULL || s[0] == '\0') {
        return -1;
    }
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        for (j = 0; tab[i].alias[j] != NULL; j++) {
            if (ieq(s, tab[i].alias[j])) {
                return tab[i].lang;
            }
        }
    }
    return -1;
}

/* Match a value that may be a locale string: try it whole, then its language
 * prefix ("de_DE.UTF-8" / "ja_JP" / "en_US@euro" => "de"/"ja"/"en"). */
static int match_value(const char *s)
{
    char pre[16];
    unsigned int n = 0;
    int m = match_name(s);

    if (m >= 0 || s == NULL) {
        return m;
    }
    while (s[n] != '\0' && s[n] != '_' && s[n] != '.' && s[n] != '@' &&
           n < sizeof(pre) - 1) {
        pre[n] = s[n];
        n++;
    }
    pre[n] = '\0';
    return (n > 0) ? match_name(pre) : -1;
}

int jc_msg_lang_match(const char *s, enum jc_msg_lang *out)
{
    int m = match_value(s);
    if (m < 0) {
        return 0;
    }
    if (out != NULL) {
        *out = (enum jc_msg_lang)m;
    }
    return 1;
}

enum jc_msg_lang jc_msg_lang_resolve(const char *config_language,
                                     const char *jichi_lang_env,
                                     const char *lang_env,
                                     int utf8_ok)
{
    int m = match_value(jichi_lang_env);
    if (m < 0) {
        m = match_value(config_language);
    }
    if (m < 0) {
        m = match_value(lang_env);
    }
    if (m < 0) {
        m = JC_MSGL_EN;
    }
    /* Every non-English catalog contains UTF-8 bytes; on a non-UTF-8 terminal
     * English beats mojibake (same policy as the TUI glyph fallback). */
    if (!utf8_ok && m != JC_MSGL_EN) {
        m = JC_MSGL_EN;
    }
    return (enum jc_msg_lang)m;
}

void jc_msg_set_lang(enum jc_msg_lang lang)
{
    g_lang = lang;
}

enum jc_msg_lang jc_msg_get_lang(void)
{
    return g_lang;
}

const char *jc_msg(enum jc_msg_id id)
{
    const char *const *t;
    const char *s;

    if ((int)id < 0 || (int)id >= (int)JC_MSG__COUNT) {
        return "";
    }
    t = table_for(g_lang);
    s = t[id];
    if (s == NULL || s[0] == '\0') {
        s = msg_en[id]; /* phased: an untranslated entry falls back */
    }
    return s;
}

const char *jc_msg_raw(enum jc_msg_lang lang, enum jc_msg_id id)
{
    /* The table entry as written, WITHOUT the English fallback -- so a test
     * can see a hole the fallback would paper over (M380). C89 positional
     * initialization makes a forgotten translation compile silently as NULL;
     * jc_msg() then serves English, which is right for the user and
     * invisible to any check routed through it. */
    if ((int)id < 0 || (int)id >= (int)JC_MSG__COUNT) {
        return NULL;
    }
    return table_for(lang)[id];
}
