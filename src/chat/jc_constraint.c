/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_constraint.c - pure constraint core (see jc_constraint.h). No I/O. */

#include "jc_constraint.h"
#include "jc_snprintf.h"

#include <string.h>

/* ---- small char helpers (case-insensitive, word-boundary) ---------------- */

static int lc(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int is_word_ch(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

/* Case-insensitive substring search with WORD boundaries around the match, so
 * "test" hits "cargo test" / "go test" but not "latest" / "contest". `ndl` may
 * hold internal spaces (multi-word); only the outer edges are boundary-checked. */
static int contains_word(const char *hay, const char *ndl)
{
    jc_size hn, nn, i, j;
    if (hay == NULL || ndl == NULL) return 0;
    hn = (jc_size)strlen(hay);
    nn = (jc_size)strlen(ndl);
    if (nn == 0 || hn < nn) return 0;
    for (i = 0; i + nn <= hn; i++) {
        int ok = 1;
        for (j = 0; j < nn; j++) {
            if (lc((unsigned char)hay[i + j]) != lc((unsigned char)ndl[j])) {
                ok = 0;
                break;
            }
        }
        if (!ok) continue;
        if (i > 0 && is_word_ch((unsigned char)hay[i - 1])) continue;
        if (i + nn < hn && is_word_ch((unsigned char)hay[i + nn])) continue;
        return 1;
    }
    return 0;
}

/* ---- canonical command keys + their command tokens ----------------------- */

struct cmd_key {
    const char *key;
    const char *tokens[10]; /* NULL-terminated */
    const char *text;       /* canonical prompt phrasing */
};

static const struct cmd_key CMD_KEYS[] = {
    { "build",
      { "build", "make", "cmake", "ninja", "gcc", "g++", "clang",
        "compile", "meson", NULL },
      "do not run build commands (make / cmake / compile / ...)" },
    { "test",
      { "test", "ctest", "pytest", "jest", "check", NULL, NULL, NULL, NULL, NULL },
      "do not run tests" },
    { "commit",
      { "commit", NULL },
      "do not commit" },
    { "push",
      { "push", NULL },
      "do not push" },
    { "deploy",
      { "deploy", NULL },
      "do not deploy" },
    { "install",
      { "install", NULL },
      "do not install packages" },
    { "privilege",
      { "sudo", "sudoedit", "doas", "pkexec", "su", "run0", NULL },
      "do not run privileged commands (sudo / doas / pkexec / su / run0)" }
};
static const int N_CMD_KEYS = (int)(sizeof(CMD_KEYS) / sizeof(CMD_KEYS[0]));

static const struct cmd_key *find_key(const char *key)
{
    int i;
    if (key == NULL) return NULL;
    for (i = 0; i < N_CMD_KEYS; i++) {
        if (strcmp(CMD_KEYS[i].key, key) == 0) return &CMD_KEYS[i];
    }
    /* M155: a user writes `deny-cmd sudo` (or doas/pkexec/su/run0), not the
     * internal key name "privilege" -- resolve those launcher words to it so
     * the constraint actually binds (before M155 `deny-cmd sudo` was inert). */
    if (strcmp(key, "sudo") == 0 || strcmp(key, "sudoedit") == 0 ||
        strcmp(key, "doas") == 0 || strcmp(key, "pkexec") == 0 ||
        strcmp(key, "su") == 0 || strcmp(key, "run0") == 0 ||
        strcmp(key, "privileged") == 0) {
        for (i = 0; i < N_CMD_KEYS; i++) {
            if (strcmp(CMD_KEYS[i].key, "privilege") == 0) {
                return &CMD_KEYS[i];
            }
        }
    }
    return NULL;
}

int jc_constraint_cmd_hits(const char *key, const char *command)
{
    const struct cmd_key *k = find_key(key);
    int i;
    if (k == NULL || command == NULL) return 0;
    for (i = 0; k->tokens[i] != NULL; i++) {
        if (contains_word(command, k->tokens[i])) return 1;
    }
    return 0;
}

/* ---- canonical text ------------------------------------------------------ */

static const char *cmd_text(const char *key)
{
    const struct cmd_key *k = find_key(key);
    return (k != NULL) ? k->text : "do not run this command";
}

/* Build a constraint's display/prompt text (canonical + stable, so the injected
 * block is cache-friendly regardless of the user's exact wording). */
static void constraint_text(enum jc_constraint_kind kind, const char *subj,
                            char *buf, jc_size cap)
{
    switch (kind) {
    case JC_CONSTRAINT_DENY_TOOL:
        jc_snprintf(buf, cap, "do not use the tool `%s`",
                    subj != NULL ? subj : "?");
        break;
    case JC_CONSTRAINT_DENY_CMD:
        jc_snprintf(buf, cap, "%s", cmd_text(subj));
        break;
    case JC_CONSTRAINT_READ_ONLY:
        jc_snprintf(buf, cap, "read-only: do not edit files or make changes");
        break;
    case JC_CONSTRAINT_NOTE:
    default:
        jc_snprintf(buf, cap, "%s", subj != NULL ? subj : "");
        break;
    }
}

int jc_constraint_has(const struct jc_constraint *cs, int n,
                      const struct jc_constraint *c)
{
    int i;
    if (cs == NULL || c == NULL) return 0;
    for (i = 0; i < n; i++) {
        if (cs[i].kind != c->kind) continue;
        if (cs[i].kind == JC_CONSTRAINT_READ_ONLY) return 1;
        if (cs[i].subject == NULL || c->subject == NULL) {
            if (cs[i].subject == c->subject) return 1;
            continue;
        }
        if (strcmp(cs[i].subject, c->subject) == 0) return 1;
    }
    return 0;
}

/* Append a constraint (deduped) with canonical text. */
static void emit(struct jc_constraint *out, int *n, int max, struct jc_arena *a,
                 enum jc_constraint_kind kind, const char *subj,
                 const char *note_text)
{
    struct jc_constraint c;
    char buf[256];
    if (*n >= max) return;
    c.kind = kind;
    c.subject = (subj != NULL) ? jc_arena_strdup(a, subj) : NULL;
    if (kind == JC_CONSTRAINT_NOTE) {
        c.text = jc_arena_strdup(a, note_text != NULL ? note_text : "");
    } else {
        constraint_text(kind, subj, buf, sizeof buf);
        c.text = jc_arena_strdup(a, buf);
    }
    /* M169: AUTHORED by default. jc_constraint_scan re-stamps its own emissions
     * as INFERRED on the way out, so the store parser (which also uses emit)
     * keeps producing persisted constraints without a second code path. */
    c.origin = JC_CONSTRAINT_AUTHORED;
    if (jc_constraint_has(out, *n, &c)) return;
    out[*n] = c;
    (*n)++;
}

/* ---- scan ---------------------------------------------------------------- */

static int is_pronoun_after(const char *s)
{
    /* s points just after a negation cue; skip spaces, then check for a
     * conversational pronoun ("don't you think", "why don't we") which is NOT a
     * constraint. */
    static const char *P[] = { "you", "we", "i ", "they", "he ", "she ",
                               "it ", NULL };
    int i;
    while (*s == ' ') s++;
    for (i = 0; P[i] != NULL; i++) {
        jc_size pl = (jc_size)strlen(P[i]);
        if (strncmp(s, P[i], pl) == 0) return 1;
    }
    return 0;
}

/* Word match tolerating a plural/3rd-person 's' ("tests"/"builds"/"pushes"). */
static int mentions(const char *win, const char *base)
{
    char plural[32];
    jc_size bl;
    if (contains_word(win, base)) return 1;
    bl = (jc_size)strlen(base);
    if (bl + 3 >= sizeof(plural)) return 0;
    memcpy(plural, base, bl);
    plural[bl] = 's';
    plural[bl + 1] = '\0';
    if (contains_word(win, plural)) return 1; /* tests / builds / commits */
    /* "-es" plural for a trailing s/h (pushes, deploys already covered by 's') */
    if (bl > 0 && (base[bl - 1] == 's' || base[bl - 1] == 'h')) {
        plural[bl] = 'e';
        plural[bl + 1] = 's';
        plural[bl + 2] = '\0';
        if (contains_word(win, plural)) return 1; /* pushes */
    }
    return 0;
}

/* Does the window contain a verb of *running* something? (M167)
 *
 * Inflections are spelled out rather than stemmed: a prefix match on "run"
 * would fire on "runtime", and the list is short enough to read. "use" is
 * deliberately absent -- "do not use the test fixture" is about a file. */
static int has_run_verb(const char *win)
{
    static const char *V[] = {
        "run", "runs", "running", "rerun", "reruns", "rerunning",
        "execute", "executes", "executing", "invoke", "invokes", "invoking",
        "call", "calls", "calling", "start", "starts", "starting",
        "launch", "launches", "launching", "perform", "performs", "performing",
        NULL
    };
    int i;
    for (i = 0; V[i] != NULL; i++) {
        if (contains_word(win, V[i])) return 1;
    }
    return 0;
}

/* Offset of `ndl` in `hay` as a whole word at or after `from`, else -1. */
static long word_offset(const char *hay, const char *ndl, long from)
{
    long hn = (long)strlen(hay);
    long nn = (long)strlen(ndl);
    long i;
    if (nn == 0) return -1L;
    for (i = from; i + nn <= hn; i++) {
        long j;
        int ok = 1;
        for (j = 0; j < nn; j++) {
            if (lc((unsigned char)hay[i + j]) != lc((unsigned char)ndl[j])) {
                ok = 0;
                break;
            }
        }
        if (!ok) continue;
        if (i > 0 && is_word_ch((unsigned char)hay[i - 1])) continue;
        if (i + nn < hn && is_word_ch((unsigned char)hay[i + nn])) continue;
        return i;
    }
    return -1L;
}

/* Fill `forms` with `base` and its plural spellings (same rule as `mentions`),
 * NULL-terminated. `store` backs the generated strings. */
static void word_forms(const char *base, const char *forms[4], char store[3][40])
{
    jc_size bl = (jc_size)strlen(base);
    int nf = 0;
    forms[nf++] = base;
    if (bl + 3 < sizeof(store[0])) {
        jc_snprintf(store[0], sizeof store[0], "%ss", base);
        forms[nf++] = store[0];
        if (bl > 0 && (base[bl - 1] == 's' || base[bl - 1] == 'h')) {
            jc_snprintf(store[1], sizeof store[1], "%ses", base);
            forms[nf++] = store[1];
        }
    }
    forms[nf] = NULL;
}

/* Is `base` used in VERB position in the window? (M167)
 *
 * True when an occurrence of the word is preceded only by the start of the
 * window, an intensifier, or a coordinator -- i.e. it heads the prohibition
 * ("do not test") or is another item in a list of prohibited actions
 * ("never commit or push", "do not commit, push, or deploy"). False when a
 * determiner or another verb precedes it, which is the noun reading
 * ("do not change the test file"). */
static int in_verb_position(const char *win, const char *base)
{
    static const char *LEAD[] = { "ever", "just", "again", "please", "actually",
                                  "blindly", "automatically", "to", "or", "and",
                                  "nor", "then", "either", "neither", NULL };
    const char *forms[4];
    char store[3][40];
    int f;
    word_forms(base, forms, store);
    for (f = 0; forms[f] != NULL; f++) {
        long at = -1L;
        while ((at = word_offset(win, forms[f], at + 1)) >= 0) {
            long p = at - 1;
            long end;
            int i;
            while (p >= 0 && win[p] == ' ') p--;
            if (p < 0) return 1;                  /* heads the window */
            /* A list separator is itself the coordination signal: in
             * "commit, push, or deploy" the item before `push` is a comma, not
             * a word we could look up. */
            if (win[p] == ',' || win[p] == ';') return 1;
            end = p;
            while (p >= 0 && is_word_ch((unsigned char)win[p])) p--;
            /* the preceding word is win[p+1 .. end] */
            for (i = 0; LEAD[i] != NULL; i++) {
                long ln = (long)strlen(LEAD[i]);
                if (end - p == ln &&
                    word_offset(win + p + 1, LEAD[i], 0) == 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Is "read-only" / "read only" used as an INSTRUCTION rather than a description?
 *
 * M168. Requires a stative/imperative cue within a few words before the phrase
 * ("keep this read-only", "stay read-only", "work read-only", "in read-only
 * mode"), or the phrase opening the message ("read-only please"). A bare
 * adjectival mention -- "Oracle files (read-only, outside the edit scope)" -- is
 * describing something, and must not silently put the whole run in read-only. */
static int mentions_read_only_as_instruction(const char *low)
{
    /* Cues that make the phrase an instruction. "is" is deliberately ABSENT:
     * "the corpus is mounted read-only" states a fact about someone else's
     * filesystem, and treating that as an order is the bug this guards. */
    static const char *CUE[] = {
        "keep", "keeps", "kept", "stay", "stays", "remain", "remains", "be",
        "in", "treat", "work", "strictly", "please", "mode",
        "this", "everything", "session", "purely", NULL
    };
    /* A determiner or object noun AFTER the phrase means it is modifying that
     * thing, not commanding the run: "read only the header", "read-only files". */
    static const char *OBJ[] = {
        "the", "a", "an", "this", "that", "these", "those", "its", "my", "our",
        "first", "last", "lines", "line", "file", "files", "part", "parts",
        "section", "sections", "header", "headers", "bytes", "copy", "copies",
        "mount", "mounts", "filesystem", "tree", "dir", "directory", NULL
    };
    static const char *FORMS[] = { "read-only", "read only", NULL };
    int f;
    for (f = 0; FORMS[f] != NULL; f++) {
        long at = -1L;
        while ((at = word_offset(low, FORMS[f], at + 1)) >= 0) {
            long p = at - 1;
            int words = 0;
            long after = at + (long)strlen(FORMS[f]);
            int objected = 0;
            {   /* peek at the word following the phrase */
                long q = after;
                int i;
                while (low[q] == ' ' || low[q] == '\t') q++;
                for (i = 0; OBJ[i] != NULL; i++) {
                    if (word_offset(low + q, OBJ[i], 0) == 0) {
                        objected = 1;
                        break;
                    }
                }
            }
            if (objected) {
                continue;              /* adjectival / object use: not an order */
            }
            if (at == 0) {
                return 1;                  /* opens the message */
            }
            /* walk back up to three words looking for a cue */
            while (p >= 0 && words < 3) {
                long end;
                int i;
                while (p >= 0 && !is_word_ch((unsigned char)low[p])) {
                    /* a sentence boundary ends the search: a cue in the previous
                     * sentence is not modifying this phrase */
                    if (low[p] == '.' || low[p] == '!' || low[p] == '?' ||
                        low[p] == '\n') {
                        return 0;
                    }
                    p--;
                }
                if (p < 0) {
                    break;
                }
                end = p;
                while (p >= 0 && is_word_ch((unsigned char)low[p])) {
                    p--;
                }
                for (i = 0; CUE[i] != NULL; i++) {
                    long cl = (long)strlen(CUE[i]);
                    if (end - p == cl &&
                        word_offset(low + p + 1, CUE[i], 0) == 0) {
                        return 1;
                    }
                }
                words++;
            }
        }
    }
    return 0;
}

/* ---- scope of an edit prohibition (M207) ---------------------------------- */

/* Phrases that unambiguously ORDER the agent not to edit. Each is tested for
 * SCOPE by edit_prohibition_is_global below; matching alone is not enough. */
static const char *EDIT_ORDERS[] = {
    "do not edit", "don't edit", "dont edit", "no edits",
    "do not make any change", "don't make any change",
    "do not make changes", "don't make changes",
    NULL
};

/* Determiners/quantifiers that may sit between the verb and its object and do
 * not themselves narrow the order ("do not edit ANY files"). */
static const char *EDIT_DET[] = {
    "any", "all", "the", "my", "our", "its", "further", "additional", "more",
    "other", NULL
};

/* Objects broad enough that prohibiting them prohibits editing outright. */
static const char *EDIT_GLOBAL_OBJ[] = {
    "file", "files", "anything", "everything", "code", "source", "sources",
    "thing", "things", "change", "changes", "content", "contents", "src",
    "tree", "repo", "repository", "workspace", NULL
};

/* A word after the object that re-narrows it to specific targets: "do not edit
 * any files IN src/", "do not edit anything EXCEPT the tests". */
static const char *EDIT_PREP[] = {
    "in", "inside", "under", "outside", "within", "except", "besides",
    "apart", "beyond", "to", "of", "from", "named", "called", "matching",
    "below", "above", "into", "unless", "other", NULL
};

static const char *skip_blanks(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

/* Copy the word at `s` into `buf` (lowercased by the caller already) and return
 * the pointer just past it. An empty word leaves buf[0] == '\0'. */
static const char *take_word(const char *s, char *buf, jc_size cap)
{
    jc_size n = 0;
    while (is_word_ch((unsigned char)*s)) {
        if (n + 1 < cap) buf[n++] = *s;
        s++;
    }
    buf[n] = '\0';
    return s;
}

static int word_in(const char *w, const char *const *list)
{
    int i;
    if (w[0] == '\0') return 0;
    for (i = 0; list[i] != NULL; i++) {
        if (strcmp(w, list[i]) == 0) return 1;
    }
    return 0;
}

/* True when an edit prohibition is UNSCOPED -- a blanket "make no changes"
 * rather than an order about one named target. `after` points just past the
 * matched phrase.
 *
 * M207: "do not edit" was adopted as blanket read-only with no further test, on
 * the reasoning recorded at the call site that the phrase "can only be an
 * order". It IS an order -- but orders have objects, and the object carries the
 * scope. A precise brief containing "Leave the pipeline unchanged; do NOT edit
 * the pipeline" turned a 1.5M-token `--auto` drive read-only: all 21 of its edit
 * and shell calls were refused below the verdict while the model kept reading
 * and complying, which is the same failure M168 fixed one axis of (descriptive
 * vs imperative) with the other axis (scoped vs unscoped) left open.
 *
 * Defaulting to "scoped" is the safe direction. A missed blanket prohibition
 * only forgoes a mechanical backstop for an instruction the prompt still states
 * in full; a false blanket one silently makes the run unable to do its task,
 * and in headless `--auto` there is no `/constraints clear` to lift it. */
static int edit_prohibition_is_global(const char *after)
{
    char w[32];
    const char *s = skip_blanks(after);

    /* "no edits" / "do not make any change" + an immediate 's' plural. */
    if (*s == 's') s++;
    s = skip_blanks(s);

    /* A prohibition that simply ends ("...do not edit.") is blanket. */
    if (*s == '\0' || *s == '.' || *s == ';' || *s == '!' || *s == '?') {
        return 1;
    }

    /* Skip determiners: "any", "all", "the", ... */
    for (;;) {
        const char *next = take_word(s, w, sizeof w);
        if (!word_in(w, EDIT_DET)) break;
        s = skip_blanks(next);
    }

    (void)take_word(s, w, sizeof w);
    if (w[0] == '\0') {
        return 1; /* determiners then nothing: "do not edit any." */
    }
    if (!word_in(w, EDIT_GLOBAL_OBJ)) {
        return 0; /* a NAMED object ("the pipeline", "parser.zig") => scoped */
    }
    /* A broad object, but a following preposition re-narrows it. */
    {
        const char *rest = skip_blanks(take_word(s, w, sizeof w));
        char w2[32];
        (void)take_word(rest, w2, sizeof w2);
        if (word_in(w2, EDIT_PREP)) {
            return 0;
        }
    }
    return 1;
}

/* True when ANY occurrence of any edit order in `low` is unscoped. */
static int has_global_edit_prohibition(const char *low)
{
    int p;
    for (p = 0; EDIT_ORDERS[p] != NULL; p++) {
        long at = -1L;
        jc_size plen = (jc_size)strlen(EDIT_ORDERS[p]);
        while ((at = word_offset(low, EDIT_ORDERS[p], at + 1)) >= 0) {
            if (edit_prohibition_is_global(low + at + plen)) {
                return 1;
            }
        }
    }
    return 0;
}

/* True when the window prohibits *doing* `base`, not merely mentioning it.
 *
 * M167: the bare-noun test was over-broad. "Do not change the test file" put
 * the word "test" in the window, so the scanner adopted `deny-tool run_tests` +
 * `deny-cmd test` and blocked the whole suite -- a prohibition the user never
 * expressed, persisted to .jichi/constraints.md, and silently inherited by every
 * later run in that directory. It neither protected the file the user actually
 * named nor allowed what they actually permitted. A target now counts only when
 * it heads the window (used as a verb) or a running verb appears alongside it.
 *
 * Not applied to the privilege group below: "sudo" / "root" / "privileged" are
 * naturally nouns and adjectives ("never run as root", "do not use sudo"), so
 * the bare mention is the right signal there. */
static int acts_on(const char *win, const char *base)
{
    return in_verb_position(win, base) || has_run_verb(win);
}

/* Look for target keywords in `window` and emit the matching constraints. */
static void scan_window(const char *window, struct jc_constraint *out, int *n,
                        int max, struct jc_arena *a)
{
    if ((mentions(window, "build") && acts_on(window, "build")) ||
        (mentions(window, "compile") && acts_on(window, "compile")) ||
        (mentions(window, "make") && acts_on(window, "make"))) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "build", NULL);
    }
    if (mentions(window, "test") && acts_on(window, "test")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_TOOL, "run_tests", NULL);
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "test", NULL);
    }
    if (mentions(window, "commit") && acts_on(window, "commit")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_TOOL, "git_commit", NULL);
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "commit", NULL);
    }
    if (mentions(window, "push") && acts_on(window, "push")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "push", NULL);
    }
    if (mentions(window, "deploy") && acts_on(window, "deploy")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "deploy", NULL);
    }
    if (mentions(window, "install") && acts_on(window, "install")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "install", NULL);
    }
    if (mentions(window, "sudo") || mentions(window, "privileged") ||
        mentions(window, "doas") || mentions(window, "pkexec") ||
        mentions(window, "root")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "privilege", NULL);
    }
}

int jc_constraint_scan(const char *msg, struct jc_constraint *out, int max,
                       struct jc_arena *a)
{
    static const char *NEG[] = {
        "do not", "don't", "dont", "never", "must not", "mustn't",
        "refrain from", "avoid ", NULL
    };
    char *low;
    jc_size len, i;
    int n = 0;

    if (msg == NULL || out == NULL || a == NULL) return 0;
    len = (jc_size)strlen(msg);
    low = (char *)jc_arena_alloc(a, len + 1);
    if (low == NULL) return 0;
    for (i = 0; i < len; i++) low[i] = (char)lc((unsigned char)msg[i]);
    low[len] = '\0';

    /* Explicit read-only phrasings (a state, independent of the negation loop).
     *
     * M168: "read-only" alone is not enough. A bare mention is very often
     * DESCRIPTIVE -- "Oracle files (read-only, outside the edit scope)" describes
     * some inputs; it does not ask the agent to stop editing. Adopting read-only
     * from that silently turned a real 1.56M-token `--auto` drive into a
     * read-only run that could not do its task, and the outcome looked like a
     * lazy model rather than a misparse. So the phrase counts only when a
     * stative/imperative cue sits near it ("keep this read-only", "stay
     * read-only", "read-only mode", "work read-only").
     *
     * M207: the explicit *instructions* used to need no cue at all, on the
     * reasoning that "do not edit" can only be an order. That is true and beside
     * the point -- an order has an object, and a NAMED object scopes it. "do NOT
     * edit the pipeline" cost another 1.5M-token drive the same way. They now go
     * through has_global_edit_prohibition, which adopts the blanket constraint
     * only when the object is broad. */
    if (mentions_read_only_as_instruction(low) ||
        has_global_edit_prohibition(low)) {
        emit(out, &n, max, a, JC_CONSTRAINT_READ_ONLY, NULL, NULL);
    }

    /* Negation cue -> scan a window after it for forbidden targets. */
    for (i = 0; i < len; i++) {
        int c;
        for (c = 0; NEG[c] != NULL; c++) {
            jc_size cl = (jc_size)strlen(NEG[c]);
            if (i + cl > len) continue;
            if (strncmp(low + i, NEG[c], cl) != 0) continue;
            /* boundary before the cue (avoid matching inside a word) */
            if (i > 0 && is_word_ch((unsigned char)low[i - 1])) continue;
            if (is_pronoun_after(low + i + cl)) continue; /* conversational */
            {
                char win[96];
                jc_size wl = len - (i + cl);
                if (wl > sizeof(win) - 1) wl = sizeof(win) - 1;
                memcpy(win, low + i + cl, wl);
                win[wl] = '\0';
                scan_window(win, out, &n, max, a);
            }
            break; /* one cue per position */
        }
    }
    /* M169: everything this function produced was GUESSED from prose, so mark it
     * session-scoped. jc_constraint_serialize skips these, which is what keeps a
     * misparse from governing every later run in the workspace. */
    {
        int k;
        for (k = 0; k < n; k++) {
            out[k].origin = JC_CONSTRAINT_INFERRED;
        }
    }
    return n;
}

/* ---- enforcement --------------------------------------------------------- */

static int block(const char *text, char *reason, jc_size cap)
{
    jc_snprintf(reason, cap, "blocked by an active constraint: %s",
                text != NULL ? text : "(constraint)");
    return 1;
}

int jc_constraint_blocks(const struct jc_constraint *cs, int n,
                         const char *tool_name, const char *command,
                         int tool_readonly, char *reason, jc_size cap)
{
    return jc_constraint_blocks_ex(cs, n, tool_name, command, tool_readonly, 0,
                                   reason, cap);
}

int jc_constraint_blocks_ex(const struct jc_constraint *cs, int n,
                            const char *tool_name, const char *command,
                            int tool_readonly, int explicit_write_allowed,
                            char *reason, jc_size cap)
{
    int i;
    if (cs == NULL || tool_name == NULL || reason == NULL || cap == 0) return 0;
    for (i = 0; i < n; i++) {
        const struct jc_constraint *c = &cs[i];
        switch (c->kind) {
        case JC_CONSTRAINT_DENY_TOOL:
            if (c->subject != NULL && strcmp(tool_name, c->subject) == 0) {
                return block(c->text, reason, cap);
            }
            break;
        case JC_CONSTRAINT_READ_ONLY:
            if (tool_readonly) break;
            /* An operator typed a flag naming this exact path. A read-only they
             * WROTE still wins -- two explicit declarations in conflict is theirs
             * to resolve, and silently picking one would hide it. A read-only we
             * GUESSED from prose does not: see the header. */
            if (explicit_write_allowed &&
                c->origin == JC_CONSTRAINT_INFERRED) {
                break;
            }
            return block(c->text, reason, cap);
        case JC_CONSTRAINT_DENY_CMD:
            /* The dedicated test tool is a "test" run regardless of its args. */
            if (c->subject != NULL && strcmp(c->subject, "test") == 0 &&
                strcmp(tool_name, "run_tests") == 0) {
                return block(c->text, reason, cap);
            }
            /* A shell command whose text hits the forbidden key. */
            if (command != NULL &&
                (strcmp(tool_name, "run_terminal_command") == 0 ||
                 strcmp(tool_name, "run_tests") == 0) &&
                jc_constraint_cmd_hits(c->subject, command)) {
                return block(c->text, reason, cap);
            }
            break;
        case JC_CONSTRAINT_NOTE:
        default:
            break; /* advisory only */
        }
    }
    return 0;
}

/* ---- render -------------------------------------------------------------- */

void jc_constraint_render(const struct jc_constraint *cs, int n,
                          struct jc_sb *sb)
{
    int i;
    if (cs == NULL || sb == NULL || n <= 0) return;
    jc_sb_append(sb,
        "\n# Active constraints (ENFORCED -- a violating tool call is REFUSED)\n");
    for (i = 0; i < n; i++) {
        jc_sb_append(sb, "- ");
        jc_sb_append(sb, cs[i].text != NULL ? cs[i].text : "(constraint)");
        jc_sb_append(sb, "\n");
    }
    jc_sb_append(sb,
        "These are hard limits set by the user. Do NOT attempt to work around "
        "them (e.g. via a different tool or a shell alias).\n");
}

/* ---- store parse / serialize -------------------------------------------- */

/* Trim leading spaces + an optional "- " bullet; return the start. */
static const char *skip_bullet(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '-' && s[1] == ' ') s += 2;
    while (*s == ' ') s++;
    return s;
}

int jc_constraint_parse(const char *text, struct jc_constraint *out, int max,
                        struct jc_arena *a)
{
    const char *p = text;
    int n = 0;
    if (text == NULL || out == NULL || a == NULL) return 0;
    while (*p != '\0' && n < max) {
        const char *eol = strchr(p, '\n');
        jc_size ll = (eol != NULL) ? (jc_size)(eol - p) : (jc_size)strlen(p);
        char line[512];
        const char *s;
        if (ll > sizeof(line) - 1) ll = sizeof(line) - 1;
        memcpy(line, p, ll);
        line[ll] = '\0';
        p = (eol != NULL) ? eol + 1 : p + strlen(p);

        s = skip_bullet(line);
        if (s[0] == '\0' || s[0] == '#') continue;

        if (strncmp(s, "deny-tool", 9) == 0 && (s[9] == ' ' || s[9] == '\t')) {
            const char *v = s + 9;
            while (*v == ' ' || *v == '\t') v++;
            if (*v != '\0') {
                char subj[128];
                jc_size k = 0;
                while (v[k] != '\0' && v[k] != ' ' && v[k] != ';' &&
                       k < sizeof(subj) - 1) { subj[k] = v[k]; k++; }
                subj[k] = '\0';
                emit(out, &n, max, a, JC_CONSTRAINT_DENY_TOOL, subj, NULL);
            }
        } else if (strncmp(s, "deny-cmd", 8) == 0 &&
                   (s[8] == ' ' || s[8] == '\t')) {
            const char *v = s + 8;
            while (*v == ' ' || *v == '\t') v++;
            if (*v != '\0') {
                char subj[128];
                jc_size k = 0;
                while (v[k] != '\0' && v[k] != ' ' && v[k] != ';' &&
                       k < sizeof(subj) - 1) { subj[k] = v[k]; k++; }
                subj[k] = '\0';
                emit(out, &n, max, a, JC_CONSTRAINT_DENY_CMD, subj, NULL);
            }
        } else if (strcmp(s, "read-only") == 0 || strcmp(s, "read only") == 0) {
            emit(out, &n, max, a, JC_CONSTRAINT_READ_ONLY, NULL, NULL);
        } else if (strncmp(s, "note", 4) == 0 && (s[4] == ' ' || s[4] == '\t')) {
            const char *v = s + 4;
            while (*v == ' ' || *v == '\t') v++;
            emit(out, &n, max, a, JC_CONSTRAINT_NOTE, NULL, v);
        }
        /* Unknown directive lines are ignored (forward-compatible). */
    }
    return n;
}

void jc_constraint_serialize(const struct jc_constraint *cs, int n,
                             struct jc_sb *sb)
{
    int i;
    if (sb == NULL) return;
    jc_sb_append(sb,
        "# jichi constraints -- enforced every turn (M110). One per line.\n"
        "# deny-cmd <build|test|commit|push|deploy|install> | "
        "deny-tool <name> | read-only | note <text>\n");
    if (cs == NULL) return;
    for (i = 0; i < n; i++) {
        /* M169: never write a prompt-inferred constraint to the durable store.
         * It is enforced for this session; persisting a guess is what turned a
         * one-turn misparse into a permanent ban (docs/ANECDOTES.md #21). */
        if (cs[i].origin == JC_CONSTRAINT_INFERRED) {
            continue;
        }
        switch (cs[i].kind) {
        case JC_CONSTRAINT_DENY_TOOL:
            jc_sb_append(sb, "deny-tool ");
            jc_sb_append(sb, cs[i].subject != NULL ? cs[i].subject : "?");
            jc_sb_append(sb, "\n");
            break;
        case JC_CONSTRAINT_DENY_CMD:
            jc_sb_append(sb, "deny-cmd ");
            jc_sb_append(sb, cs[i].subject != NULL ? cs[i].subject : "?");
            jc_sb_append(sb, "\n");
            break;
        case JC_CONSTRAINT_READ_ONLY:
            jc_sb_append(sb, "read-only\n");
            break;
        case JC_CONSTRAINT_NOTE:
        default:
            jc_sb_append(sb, "note ");
            jc_sb_append(sb, cs[i].text != NULL ? cs[i].text : "");
            jc_sb_append(sb, "\n");
            break;
        }
    }
}

void jc_constraint_join_text(const struct jc_constraint *cs, int from, int n,
                             char *buf, jc_size cap)
{
    jc_size len = 0;
    int i;
    if (buf == NULL || cap == 0) return;
    buf[0] = '\0';
    if (cs == NULL) return;
    if (from < 0) from = 0;
    for (i = from; i < n; i++) {
        const char *t = cs[i].text;
        jc_size tl;
        jc_size need;
        if (t == NULL || t[0] == '\0') continue;
        tl = (jc_size)strlen(t);
        need = tl + (len > 0 ? 2 : 0);
        if (len + need + 1 > cap) {
            /* Out of room: mark the truncation rather than lying by omission.
             * Terminate immediately after whatever fits -- writing the NUL at
             * cap-1 instead would leave the bytes between the marker and the
             * terminator uninitialised, which valgrind rightly flags when the
             * caller strlen()s the result. */
            static const char mark[] = ", ...";
            jc_size room = cap - 1 - len;      /* len <= cap-1 by invariant */
            jc_size mn = sizeof(mark) - 1;
            if (mn > room) {
                mn = room;
            }
            memcpy(buf + len, mark, mn);
            buf[len + mn] = '\0';
            return;
        }
        if (len > 0) {
            buf[len++] = ',';
            buf[len++] = ' ';
        }
        memcpy(buf + len, t, tl);
        len += tl;
        buf[len] = '\0';
    }
}


int jc_constraint_source_line(const char *msg, const struct jc_constraint *c,
                              struct jc_arena *a)
{
    const char *p;
    const char *nl;
    int line = 0;

    if (msg == NULL || c == NULL || a == NULL) {
        return 0;
    }
    p = msg;
    while (*p != '\0') {
        char buf[1024];
        jc_size len;
        struct jc_constraint got[JC_CONSTRAINT_MAX];
        int n;
        int i;

        line++;
        nl = strchr(p, '\n');
        len = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
        if (len >= sizeof(buf)) {
            len = sizeof(buf) - 1;
        }
        memcpy(buf, p, len);
        buf[len] = '\0';

        n = jc_constraint_scan(buf, got, JC_CONSTRAINT_MAX, a);
        for (i = 0; i < n; i++) {
            if (got[i].kind != c->kind) {
                continue;
            }
            /* Same kind and same subject (both NULL counts as same -- READ_ONLY and
             * NOTE carry no subject). */
            if (got[i].subject == NULL && c->subject == NULL) {
                return line;
            }
            if (got[i].subject != NULL && c->subject != NULL &&
                strcmp(got[i].subject, c->subject) == 0) {
                return line;
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return 0;   /* no single line reproduces it; say so rather than guess */
}
