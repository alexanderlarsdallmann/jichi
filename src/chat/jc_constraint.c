/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_constraint.c - pure constraint core (see jc_constraint.h). No I/O. */

#include "jc_constraint.h"
#include "jc_priv.h"
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

/* ---- what a command RUNS (M873, the demos drive's N40) ---------------------
 *
 * jc_constraint_cmd_hits matches a key's word anywhere in a command, and that is
 * right for a rule the operator wrote: a fence errs toward refusing, and
 * `sh -c 'pip install x'` or `./install.sh` must not slip past `deny-cmd
 * install`. For a rule jichi GUESSED it is the wrong direction. Every run of the
 * demos drive inferred "do not install packages" from "Never install anything",
 * and then told the model its `grep -n "install\|zigodot" build.zig` went against
 * it -- an accusation, in the call's own result, about a search. A guess's note
 * is only worth its precision, so an inferred rule is matched against what a
 * command RUNS:
 *
 *   - a segment's command word, after assignments and wrappers (`env`, `sudo`,
 *     `nohup`, `timeout 60`, ...): `make`, `pytest`, `./install.sh` (a script's
 *     extension dropped), `python3 -m pytest`; `sh -c '...'` is read inside;
 *   - the action of a program that takes one: the first word after its options
 *     (`pip install`, `git -C dir commit`, `cargo test`), and the word after a
 *     `run`, `build` or `exec` (`npm run build`, `zig build test`);
 *   - every target of a target runner (`make -C dir install`).
 *
 * A quoted word is never an action, a grep pattern is grep's argument, and a
 * file name is not a command: `grep -n install build.zig` runs grep. The bare
 * `test` and `[` are the shell's conditional, not a test run. What it misses --
 * an abbreviation (`npm i`), an option for an action (`cmake --install`), a
 * program not in the table -- goes unnoted, the safe direction for a guess: the
 * rule is still in the system prompt. */

#define RUN_WORDS 24

struct sh_word {
    const char *p;
    jc_size n;
    int quoted;
};

static int word_is(const struct sh_word *w, const char *s)
{
    return !w->quoted && w->n == (jc_size)strlen(s) &&
           strncmp(w->p, s, w->n) == 0;
}

static int word_in_list(const struct sh_word *w, const char *const *list)
{
    int i;
    for (i = 0; list[i] != NULL; i++) {
        if (word_is(w, list[i])) return 1;
    }
    return 0;
}

/* Is the word's basename one of `list`? `/bin/sh` is a shell, `/usr/bin/make`
 * a target runner. A quoted word is never one. */
static int base_in(const struct sh_word *w, const char *const *list)
{
    jc_size start = 0;
    jc_size i;
    int k;
    if (w->quoted) return 0;
    for (i = 0; i < w->n; i++) {
        if (w->p[i] == '/') start = i + 1;
    }
    for (k = 0; list[k] != NULL; k++) {
        jc_size ln = (jc_size)strlen(list[k]);
        if (w->n - start == ln && strncmp(w->p + start, list[k], ln) == 0) {
            return 1;
        }
    }
    return 0;
}

static int name_in(const char *name, const char *const *list)
{
    int k;
    for (k = 0; list[k] != NULL; k++) {
        if (strcmp(name, list[k]) == 0) return 1;
    }
    return 0;
}

static int word_is_option(const struct sh_word *w)
{
    return !w->quoted && w->n > 1 && w->p[0] == '-';
}

/* NAME=value, as a shell reads it before a command. */
static int word_is_assignment(const struct sh_word *w)
{
    jc_size i;
    if (w->n == 0 || !((w->p[0] >= 'a' && w->p[0] <= 'z') ||
                       (w->p[0] >= 'A' && w->p[0] <= 'Z') || w->p[0] == '_')) {
        return 0;
    }
    for (i = 1; i < w->n; i++) {
        if (w->p[i] == '=') return 1;
        if (!is_word_ch((unsigned char)w->p[i]) && w->p[i] != '_') return 0;
    }
    return 0;
}

static int word_all_digits(const struct sh_word *w)
{
    jc_size i;
    if (w->n == 0) return 0;
    for (i = 0; i < w->n; i++) {
        if (w->p[i] < '0' || w->p[i] > '9') return 0;
    }
    return 1;
}

/* The command word's NAME: its basename, less one script extension, written
 * into buf as given. *pathed says it named a path ("./test"), which the
 * shell's own `test` never does. */
static void command_name(const struct sh_word *w, char *buf, jc_size cap,
                         int *pathed)
{
    static const char *const EXT[] = {
        ".sh", ".bash", ".py", ".pl", ".rb", ".js", NULL
    };
    jc_size start = 0;
    jc_size n;
    jc_size i;
    int e;
    *pathed = 0;
    for (i = 0; i < w->n; i++) {
        if (w->p[i] == '/') {
            start = i + 1;
            *pathed = 1;
        }
    }
    n = w->n - start;
    if (n >= cap) n = cap - 1;
    memcpy(buf, w->p + start, n);
    buf[n] = '\0';
    for (e = 0; EXT[e] != NULL; e++) {
        jc_size el = (jc_size)strlen(EXT[e]);
        if (n > el && strcmp(buf + n - el, EXT[e]) == 0) {
            buf[n - el] = '\0';
            break;
        }
    }
}

/* Programs whose first word after their options is an action. */
static const char *const DISPATCHERS[] = {
    "git", "hg", "svn", "jj", "fossil",
    "cargo", "go", "zig", "swift", "dotnet", "stack", "cabal", "dune", "mix",
    "gradle", "gradlew", "mvn", "mvnw", "bazel", "bazelisk", "buck", "buck2",
    "meson", "nix", "nix-env", "guix", "opam", "rustup",
    "npm", "npx", "pnpm", "pnpx", "yarn", "bun", "bunx", "deno",
    "pip", "pip3", "pipx", "uv", "poetry", "pipenv", "pdm", "conda", "mamba",
    "micromamba", "gem", "bundle", "composer",
    "apt", "apt-get", "aptitude", "dnf", "yum", "zypper", "apk", "brew", "port",
    "pkg", "snap", "flatpak",
    "docker", "podman", "kubectl", "helm", "firebase", "fly", "flyctl",
    "vercel", "netlify", "heroku",
    NULL
};

/* Programs whose every word after their options is a target. */
static const char *const TARGET_RUNNERS[] = {
    "make", "gmake", "bmake", "ninja", "just", "rake", NULL
};

/* Options that take the next word as their value, wherever they stand before
 * an action or among targets. */
static const char *const VALUE_OPTS[] = {
    "-C", "-c", "-f", "-o", "-W", "-I", "-u", "-g", "-n", "-p", "-s", "-k",
    "--prefix", "--cwd", "--manifest-path", "--git-dir", "--work-tree",
    "--filter", "--workspace", "-w", "--project", "--directory", "--file",
    NULL
};

/* Words that run what follows them: skipped, with their options, to reach the
 * command (sudo is here too -- the privilege key asks jc_priv instead). */
static const char *const WRAPPERS[] = {
    "nohup", "command", "exec", "time", "nice", "ionice", "stdbuf", "setsid",
    "builtin", "xargs", "timeout", "chrt", "taskset", "unbuffer",
    "sudo", "doas", "pkexec", "run0", "sudoedit", NULL
};

static const char *const ENV_WORD[] = { "env", NULL };
static const char *const TIMEOUT_WORD[] = { "timeout", NULL };

static const char *const SHELLS[] = {
    "sh", "bash", "dash", "zsh", "ksh", NULL
};

static const char *const INTERPRETERS[] = {
    "python", "python2", "python3", "perl", "ruby", "node", NULL
};

static int name_hits_key(const struct cmd_key *k, const char *name)
{
    int i;
    for (i = 0; k->tokens[i] != NULL; i++) {
        if (strcmp(name, k->tokens[i]) == 0) return 1;
    }
    return 0;
}

static int word_hits_key(const struct cmd_key *k, const struct sh_word *w)
{
    int i;
    if (w->quoted) return 0;
    for (i = 0; k->tokens[i] != NULL; i++) {
        if (word_is(w, k->tokens[i])) return 1;
    }
    return 0;
}

static int runs_line(const struct cmd_key *k, const char *cmd, int depth);

/* The text of a quoted word without its outer quotes, into buf. */
static void unquote_word(const struct sh_word *w, char *buf, jc_size cap)
{
    jc_size n = w->n;
    const char *p = w->p;
    if (n >= 2 && (p[0] == '\'' || p[0] == '"') && p[n - 1] == p[0]) {
        p++;
        n -= 2;
    }
    if (n >= cap) n = cap - 1;
    memcpy(buf, p, n);
    buf[n] = '\0';
}

/* Does one segment's word list run the key? */
static int runs_segment(const struct cmd_key *k, struct sh_word *w, int nw,
                        int depth)
{
    char name[64];
    int pathed;
    int i = 0;
    int guard = 0;

    for (;;) {
        if (i >= nw || ++guard > 32) return 0;
        if (word_is_assignment(&w[i])) {
            i++;
            continue;
        }
        if (base_in(&w[i], ENV_WORD)) {
            i++;
            while (i < nw && (word_is_option(&w[i]) ||
                              word_is_assignment(&w[i]))) {
                i++;
            }
            continue;
        }
        if (base_in(&w[i], WRAPPERS)) {
            int tmo = base_in(&w[i], TIMEOUT_WORD);
            i++;
            while (i < nw && word_is_option(&w[i])) {
                if (word_in_list(&w[i], VALUE_OPTS)) i++;
                i++;
            }
            if (tmo && i < nw) i++;            /* its duration */
            continue;
        }
        if (base_in(&w[i], SHELLS)) {
            i++;
            while (i < nw && word_is_option(&w[i])) {
                if (word_is(&w[i], "-c") && i + 1 < nw) {
                    char inner[1024];
                    unquote_word(&w[i + 1], inner, sizeof inner);
                    return depth < 4 && runs_line(k, inner, depth + 1);
                }
                i++;
            }
            continue;                            /* `sh script.sh` */
        }
        if (base_in(&w[i], INTERPRETERS)) {
            i++;
            /* past its options to the script -- or the module, for `python3
             * -m pytest`: the word after -m is read as the command, as a
             * script would be */
            while (i < nw && word_is_option(&w[i])) {
                if (word_is(&w[i], "-c")) return 0;   /* code, not a command */
                i++;
            }
            continue;
        }
        break;
    }
    if (w[i].quoted) return 0;
    command_name(&w[i], name, sizeof name, &pathed);
    /* the shell's own conditional is not a test run */
    if (!pathed && (strcmp(name, "test") == 0 || strcmp(name, "[") == 0)) {
        return 0;
    }
    if (name_hits_key(k, name)) return 1;

    if (name_in(name, TARGET_RUNNERS)) {
        int j;
        for (j = i + 1; j < nw; j++) {
            if (word_is_option(&w[j])) {
                if (word_in_list(&w[j], VALUE_OPTS) ||
                    ((word_is(&w[j], "-j") || word_is(&w[j], "-l")) &&
                     j + 1 < nw && word_all_digits(&w[j + 1]))) {
                    j++;
                }
                continue;
            }
            if (word_is_assignment(&w[j])) continue;
            if (word_hits_key(k, &w[j])) return 1;
        }
        return 0;
    }
    {
        int j = i + 1;
        int level;
        if (!name_in(name, DISPATCHERS)) return 0;
        /* the action, and one level below it: `npm run build`, `zig build
         * test`, `uv pip install`, `bundle exec rake` */
        for (level = 0; level < 2; level++) {
            while (j < nw && word_is_option(&w[j])) {
                if (word_in_list(&w[j], VALUE_OPTS)) j++;
                j++;
            }
            if (j >= nw || w[j].quoted) return 0;
            if (word_hits_key(k, &w[j])) return 1;
            if (!(word_is(&w[j], "run") || word_is(&w[j], "run-script") ||
                  word_is(&w[j], "build") || word_is(&w[j], "exec") ||
                  word_in_list(&w[j], DISPATCHERS))) {
                return 0;
            }
            j++;
        }
    }
    return 0;
}

/* Split `cmd` into segments at unquoted ; & | newline ( ) { } ` and $( , and
 * ask each. */
static int runs_line(const struct cmd_key *k, const char *cmd, int depth)
{
    struct sh_word w[RUN_WORDS];
    int nw = 0;
    const char *p = cmd;
    const char *ws = NULL;      /* the word being read */
    int quoted = 0;
    int in_sq = 0;
    int in_dq = 0;

    for (;; p++) {
        char c = *p;
        int brk = 0;
        if (in_sq) {
            if (c == '\0') break;
            if (c == '\'') in_sq = 0;
            continue;
        }
        if (in_dq) {
            if (c == '\0') break;
            if (c == '\\' && p[1] != '\0') {
                p++;
                continue;
            }
            if (c == '"') in_dq = 0;
            continue;
        }
        if (c == '\'' || c == '"') {
            if (ws == NULL) ws = p;
            quoted = 1;
            if (c == '\'') in_sq = 1; else in_dq = 1;
            continue;
        }
        if (c == '\\' && p[1] != '\0') {
            if (ws == NULL) ws = p;
            p++;
            continue;
        }
        /* `{` and `}` group commands only as words of their own -- `{ make;
         * }` -- never inside one: `${DIR}`, `-I{}` */
        brk = (c == '\0' || c == ';' || c == '&' || c == '|' || c == '\n' ||
               c == '(' || c == ')' || c == '`' || (c == '$' && p[1] == '(') ||
               ((c == '{' || c == '}') && ws == NULL &&
                (p[1] == '\0' || p[1] == ' ' || p[1] == '\t' || p[1] == ';' ||
                 p[1] == '\n')));
        if (brk || c == ' ' || c == '\t' || c == '\r') {
            if (ws != NULL) {
                if (nw < RUN_WORDS) {
                    w[nw].p = ws;
                    w[nw].n = (jc_size)(p - ws);
                    w[nw].quoted = quoted;
                    nw++;
                }
                ws = NULL;
                quoted = 0;
            }
            if (brk) {
                if (nw > 0 && runs_segment(k, w, nw, depth)) return 1;
                nw = 0;
                if (c == '$') p++;
                if (c == '\0') break;
            }
            continue;
        }
        if (ws == NULL) ws = p;
    }
    /* an unclosed quote: whatever was read before it still counts */
    if (ws != NULL && nw < RUN_WORDS) {
        w[nw].p = ws;
        w[nw].n = (jc_size)strlen(ws);
        w[nw].quoted = 1;
        nw++;
    }
    return nw > 0 && runs_segment(k, w, nw, depth);
}

int jc_constraint_cmd_runs(const char *key, const char *command)
{
    const struct cmd_key *k = find_key(key);
    if (k == NULL || command == NULL) return 0;
    /* The privilege key's tokens ARE launchers: jc_priv already finds one
     * that begins a segment (M152), and nothing here should second-guess it. */
    if (strcmp(k->key, "privilege") == 0) {
        return jc_priv_detect(command, NULL) != JC_PRIV_NONE;
    }
    return runs_line(k, command, 0);
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
    c.source = NULL;   /* M872: the scan sets it on what it emitted */
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

/* True when a "do not" cue is DESCRIPTIVE rather than an instruction, judged by
 * what stands in front of it. "the tests do not compile" is a statement about
 * the tests; "do not compile anything" is an order. The difference is a subject
 * before the cue, so this looks BEHIND it -- the mirror of is_pronoun_after,
 * which looks ahead for "why don't we".
 *
 * WHY THIS EXISTS (2026-09-21, found by driving jichi on a real project). A
 * prompt opened "Two tests at the end of analyzer.zig do not compile:" -- a
 * statement of the state the agent was being asked to FIX -- and `compile` is a
 * token of the `build` command key, so the run inferred "do not run build
 * commands" and enforced it. Measured with the same scripted tool call: 0
 * refused without that sentence, 1 refused with it. The agent then could not
 * run `zig build test` to check its own work, looped, and the run ended
 * verify_failed after 42 tool calls and 2,215,762 tokens. Telling a coding
 * agent that something does not compile is the most ordinary thing there is to
 * say to one.
 *
 * SCOPED TO THE "do not" FAMILY on purpose. "we never push to master" is
 * declarative too, and it is also a policy worth honouring; "never" and "must
 * not" are left alone. Only the third-person "X do not Y" shape is silenced.
 *
 * A word before the cue means descriptive, EXCEPT the discourse markers that
 * legitimately open an imperative clause ("please do not ...", "but do not
 * ..."). A clause boundary or the start of the message means imperative. */
static int is_descriptive_before(const char *low, jc_size pos)
{
    static const char *OPENERS[] = {
        "please", "but", "and", "or", "nor", "then", "also", "so",
        "however", "again", "now", "instead", "just", NULL
    };
    jc_size e, b;
    int i;

    if (pos == 0) {
        return 0;                       /* start of the message: an order */
    }
    e = pos;
    while (e > 0 && low[e - 1] == ' ') {
        e--;
    }
    if (e == 0) {
        return 0;
    }
    if (!is_word_ch((unsigned char)low[e - 1])) {
        return 0;                       /* . ; : , ! ? ( - * newline: new clause */
    }
    b = e;
    while (b > 0 && is_word_ch((unsigned char)low[b - 1])) {
        b--;
    }
    for (i = 0; OPENERS[i] != NULL; i++) {
        jc_size ol = (jc_size)strlen(OPENERS[i]);
        if (ol == e - b && strncmp(low + b, OPENERS[i], ol) == 0) {
            return 0;                   /* "please do not ...": still an order */
        }
    }
    return 1;                           /* a subject stands in front: a statement */
}

/* contains_word, except that a match inside a FILE NAME does not count.
 *
 * M743: '.' is a word boundary, so "root" in "src/root.zig" was the whole word
 * "root": "Do NOT modify `src/root.zig`." inferred "do not run privileged
 * commands" in seven stored prompts, and "Do NOT edit root.zig, build.zig" also
 * inferred "do not run build commands". A match followed by '.' and a letter
 * or digit -- an extension -- is a file name. A sentence's own full stop
 * never gets here: the window ends before it. Every misparse of this kind in
 * the corpus had an extension; a bare directory ("root/") is left to the verb
 * tests, which is where the evidence stops. Only the scanner uses this;
 * command matching keeps contains_word. */
static int contains_word_not_path(const char *hay, const char *ndl)
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
        if (i + nn + 1 < hn && hay[i + nn] == '.' &&
            is_word_ch((unsigned char)hay[i + nn + 1])) {
            continue;
        }
        return 1;
    }
    return 0;
}

/* Word match tolerating a plural/3rd-person 's' ("tests"/"builds"/"pushes"). */
static int mentions(const char *win, const char *base)
{
    char plural[32];
    jc_size bl;
    if (contains_word_not_path(win, base)) return 1;
    bl = (jc_size)strlen(base);
    if (bl + 3 >= sizeof(plural)) return 0;
    memcpy(plural, base, bl);
    plural[bl] = 's';
    plural[bl + 1] = '\0';
    if (contains_word_not_path(win, plural)) return 1; /* tests / builds */
    /* "-es" plural for a trailing s/h (pushes, deploys already covered by 's') */
    if (bl > 0 && (base[bl - 1] == 's' || base[bl - 1] == 'h')) {
        plural[bl] = 'e';
        plural[bl + 1] = 's';
        plural[bl + 2] = '\0';
        if (contains_word_not_path(win, plural)) return 1; /* pushes */
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

/* M872 (N45 (a)): a cue at `pos` the request QUOTES or ASKS about -- defined
 * with the quote mask below, after sentence_len(). */
static int mentioned_not_said(const char *low, jc_size len, const char *mask,
                              jc_size pos);
/* M878: a read-only phrase STATED of someone else -- defined below. */
static int read_only_said_of_other(const char *low, long at);

/* True when the word ending just before offset `at` is an article.
 *
 * M743: "Confirm the upstream Godot path remains the read-only reference" put a
 * run in read-only, because "remains" is a cue two words back. An article in
 * front makes the phrase an adjective of the noun after it, whatever cue stands
 * before the article; the stored prompts say "the read-only reference" fifteen
 * times, and not once as an order. */
static int article_before(const char *low, long at)
{
    static const char *ART[] = { "the", "a", "an", NULL };
    long p = at - 1;
    long end;
    int i;
    while (p >= 0 && (low[p] == ' ' || low[p] == '\t')) p--;
    if (p < 0 || !is_word_ch((unsigned char)low[p])) return 0;
    end = p;
    while (p >= 0 && is_word_ch((unsigned char)low[p])) p--;
    for (i = 0; ART[i] != NULL; i++) {
        long al = (long)strlen(ART[i]);
        if (end - p == al && strncmp(low + p + 1, ART[i], (size_t)al) == 0) {
            return 1;
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
 * describing something, and must not silently put the whole run in read-only.
 *
 * Returns the offset of the instruction, or -1 (M872: the offset is where the
 * rule's source sentence is read from). An occurrence the request quotes or asks
 * about is skipped ("Should I keep this read-only?"). */
static long read_only_instruction_at(const char *low, jc_size len,
                                     const char *mask)
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
        "mount", "mounts", "filesystem", "tree", "dir", "directory",
        /* M878: a role -- "read-only children have no shell" is about them */
        "child", "children", "worker", "workers", "agent", "agents", "subagent",
        "subagents", "reviewer", "reviewers", "peer", "peers", NULL
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
            if (objected || article_before(low, at)) {
                continue;              /* adjectival / object use: not an order */
            }
            if (mentioned_not_said(low, len, mask, (jc_size)at)) {
                continue;              /* M872: quoted, or asked about */
            }
            if (at == 0) {
                return at;                 /* opens the message */
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
                        return -1L;
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
                        /* M878: a cue says how, not who -- a statement about
                         * someone else is not an order to the reader */
                        if (read_only_said_of_other(low, at)) {
                            break;
                        }
                        return at;
                    }
                }
                if (CUE[i] != NULL) {
                    break;               /* a cue matched, and it was a statement */
                }
                words++;
            }
        }
    }
    return -1L;
}

/* ---- scope of an edit prohibition (M207) ---------------------------------- */

/* Phrases that unambiguously ORDER the agent not to edit. Each is tested for
 * SCOPE by edit_prohibition_is_global below; matching alone is not enough.
 *
 * M743: "edit" was the only verb, so "Do not modify any file." -- the sentence
 * every read-only task of the 2026-09-24 drive ended with, 16 of them -- inferred
 * nothing, and the night's zero advisories measured this list rather than the
 * work. "modify", "change" and "touch" join it; each occurs in the stored
 * prompts, and each goes through the same object test. The plural "make any
 * changes" is spelled out because word_offset() wants a boundary after the
 * phrase, so "make any change" never matched it. */
static const char *EDIT_ORDERS[] = {
    "do not edit", "don't edit", "dont edit", "no edits",
    "do not modify", "don't modify", "dont modify",
    "do not change", "don't change", "dont change",
    "do not touch", "don't touch", "dont touch",
    "do not make any change", "don't make any change",
    "do not make any changes", "don't make any changes",
    "do not make changes", "don't make changes",
    NULL
};

/* Determiners/quantifiers that may sit between the verb and its object and do
 * not themselves narrow the order ("do not edit ANY files").
 *
 * M743: "other" is no longer one. It narrows: "do not modify any other file"
 * means every file but the one the task names, and skipping it as a determiner
 * made that sentence -- in a brief whose job was to edit one file -- read-only. */
static const char *EDIT_DET[] = {
    "any", "all", "the", "my", "our", "its", "further", "additional", "more",
    NULL
};

/* Objects broad enough that prohibiting them prohibits editing outright.
 *
 * M743: "code", "source", "sources" and "src" are gone. They name a KIND of
 * file, and a kind is a scope: "Do NOT edit any source; your only file output
 * is the design doc named below" is a brief that writes a file. With "src"
 * here, "Do not touch src/gdscript/codegen.zig." would have been read-only the
 * moment "touch" became a verb. */
static const char *EDIT_GLOBAL_OBJ[] = {
    "file", "files", "anything", "everything",
    "thing", "things", "change", "changes", "content", "contents",
    "tree", "repo", "repository", "workspace", NULL
};

/* A word after the object that re-narrows it to specific targets: "do not edit
 * any files IN src/", "do not edit anything EXCEPT the tests", and (M743) "do
 * not touch anything ELSE", "anything BUT the tests". */
static const char *EDIT_PREP[] = {
    "in", "inside", "under", "outside", "within", "except", "besides",
    "apart", "beyond", "to", "of", "from", "named", "called", "matching",
    "below", "above", "into", "unless", "other", "else", "but", NULL
};

/* M743: a preposition that INTRODUCES the object rather than following it:
 * "Make NO edits TO any file." Without this the object read was "to", which is
 * not a broad object, so a sentence that forbids editing every file was taken
 * as scoped. */
static const char *EDIT_OBJ_INTRO[] = { "to", "in", "on", NULL };

/* M743: verbs that may be listed after the order, before its one object:
 * "Do not edit, create, or modify any file." */
static const char *EDIT_COORD_VERB[] = {
    "edit", "create", "modify", "change", "touch", "write", "delete", "remove",
    "rename", "move", "add", "overwrite", "update", NULL
};
static const char *EDIT_CONJ[] = { "or", "and", "nor", NULL };

static const char *skip_blanks(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

/* skip_blanks, and Markdown emphasis with them: "*any* file" (M743). */
static const char *skip_blanks_emph(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' ||
           *s == '*' || *s == '_') {
        s++;
    }
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
 * and in headless `--auto` there is no `/constraints clear` to lift it.
 *
 * M743: this used to skip one 's' after the blanks, meant for a plural glued to
 * the phrase ("no edit|s"). word_offset() never matches a phrase with a letter
 * after it, so no such 's' ever arrived here; what the skip ate was the first
 * letter of the NEXT word, and "do not edit source" was judged on "ource". */
static int edit_prohibition_is_global(const char *after)
{
    char w[32];
    const char *s = skip_blanks(after);

    /* M743: a list of verbs before the one object -- "Do not edit, create, or
     * modify any file." This used to be judged global because the comma read
     * as "nothing follows", which was right for that sentence and wrong for
     * "do not edit, create, or modify the parser". A comma NOT followed by a
     * verb still ends the order ("no edits, just a report"), as before. */
    if (*s == ',') {
        const char *t = s;
        int verbs = 0;
        for (;;) {
            const char *next;
            while (*t == ',' || *t == ' ' || *t == '\t' || *t == '\n' ||
                   *t == '\r') {
                t++;
            }
            next = take_word(t, w, sizeof w);
            if (word_in(w, EDIT_COORD_VERB)) {
                verbs++;
            } else if (!word_in(w, EDIT_CONJ)) {
                break;
            }
            t = next;
        }
        if (verbs == 0) {
            return 1;
        }
        s = t;
    }

    /* A prohibition that simply ends ("...do not edit.") is blanket. */
    if (*s == '\0' || *s == '.' || *s == ';' || *s == '!' || *s == '?') {
        return 1;
    }

    /* "no edits TO any file": step over the preposition to the object. */
    {
        const char *next = take_word(s, w, sizeof w);
        if (word_in(w, EDIT_OBJ_INTRO)) {
            s = skip_blanks(next);
        }
    }

    /* Skip determiners: "any", "all", "the", ... and Markdown emphasis, which
     * decorates an object without naming one ("do not edit *any* file"). */
    for (;;) {
        const char *next;
        s = skip_blanks_emph(s);
        next = take_word(s, w, sizeof w);
        if (!word_in(w, EDIT_DET)) break;
        s = skip_blanks(next);
    }

    if (*s == '\0' || *s == '.' || *s == ';' || *s == '!' || *s == '?') {
        return 1; /* determiners then nothing: "do not edit any." */
    }
    /* M743: an object that is not a word -- a backtick, quote, bracket or path
     * -- is a NAMED one, so its empty word falls to "scoped" just below. The
     * empty word used to mean "nothing follows" and return blanket: "Do NOT
     * modify `src/root.zig`." fences one file, and it made the run read-only
     * the moment "modify" was a verb; "do not edit `x`" had done so all along. */
    (void)take_word(s, w, sizeof w);
    if (!word_in(w, EDIT_GLOBAL_OBJ)) {
        return 0; /* a NAMED object ("the pipeline", "parser.zig") => scoped */
    }
    /* A broad object, but a following preposition re-narrows it. */
    {
        const char *rest = skip_blanks_emph(take_word(s, w, sizeof w));
        char w2[32];
        (void)take_word(rest, w2, sizeof w2);
        if (word_in(w2, EDIT_PREP)) {
            return 0;
        }
    }
    return 1;
}

/* True when ANY occurrence of any edit order in `low` is unscoped.
 *
 * M743: a "do not" order with a subject in front of it is a statement, as the
 * negation scan has held since 2026-09-21 -- "these commands do not modify any
 * file" describes the commands. "edit" was rarely said that way; "modify",
 * "change" and "touch" are said that way all the time. "no edits" has no
 * subject to have, so it is not asked.
 *
 * Returns the offset of the order, or -1. M872: an order the request quotes or
 * asks about is skipped ("should the brief say "do not edit any file"?"). */
static long global_edit_prohibition_at(const char *low, jc_size len,
                                       const char *mask)
{
    int p;
    for (p = 0; EDIT_ORDERS[p] != NULL; p++) {
        long at = -1L;
        jc_size plen = (jc_size)strlen(EDIT_ORDERS[p]);
        while ((at = word_offset(low, EDIT_ORDERS[p], at + 1)) >= 0) {
            if (EDIT_ORDERS[p][0] == 'd' &&
                is_descriptive_before(low, (jc_size)at)) {
                continue;
            }
            if (mentioned_not_said(low, len, mask, (jc_size)at)) {
                continue;
            }
            if (edit_prohibition_is_global(low + at + plen)) {
                return at;
            }
        }
    }
    return -1L;
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

/* D2 (M735): how far a negation reaches -- to the end of its SENTENCE.
 *
 * M730 ran this scanner over 289 stored prompts: nine inferred constraints, six
 * of them wrong, and every wrong one joined a negation to a word in the NEXT
 * sentence. "Do not touch build.zig. Verify by running" became "do not run build
 * commands"; "never runs. Wire exactly three ... into build" the same; "never the
 * body of a `pub fn` nothing calls. A subsystem is proven when a test" became "do
 * not run tests". Each forbade the gate its own task named. The window used to be
 * the next 95 characters, wherever they fell.
 *
 * A sentence ends at '.', '!' or '?' -- after any closing quote, bracket or
 * emphasis mark -- when white space or the end of the text follows, so the dot in
 * "build.zig" or "3.5" does not end one. It also ends at a blank line, and at a
 * newline that opens a list item, since each item is a sentence of its own. A
 * single newline does not: a hard-wrapped sentence goes on. An abbreviation
 * ("e.g. the build") ends the scope early; that errs toward inferring less, the
 * safe direction for a guess. */
static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int opens_list_item(const char *s, jc_size avail)
{
    jc_size k = 0;
    while (k < avail && (s[k] == ' ' || s[k] == '\t')) k++;
    if (k + 1 < avail && (s[k] == '-' || s[k] == '*' || s[k] == '+') &&
        (s[k + 1] == ' ' || s[k + 1] == '\t')) {
        return 1;
    }
    if (k < avail && s[k] >= '0' && s[k] <= '9') {
        while (k < avail && s[k] >= '0' && s[k] <= '9') k++;
        return k + 1 < avail && (s[k] == '.' || s[k] == ')') && s[k + 1] == ' ';
    }
    return 0;
}

static jc_size sentence_len(const char *s, jc_size avail)
{
    jc_size k;
    for (k = 0; k < avail; k++) {
        char c = s[k];
        if (c == '.' || c == '!' || c == '?') {
            jc_size e = k + 1;
            while (e < avail && (s[e] == '"' || s[e] == '\'' || s[e] == ')' ||
                                 s[e] == ']' || s[e] == '*' || s[e] == '`' ||
                                 s[e] == '_')) {
                e++;
            }
            if (e >= avail || is_blank(s[e])) {
                return k;
            }
        } else if (c == '\n') {
            jc_size e = k + 1;
            while (e < avail && (s[e] == ' ' || s[e] == '\t' || s[e] == '\r')) {
                e++;
            }
            if (e >= avail || s[e] == '\n' ||
                opens_list_item(s + k + 1, avail - k - 1)) {
                return k;
            }
        }
    }
    return avail;
}

/* M743: a negation governs its CLAUSE, which can end before its sentence does.
 * "Do NOT add recursion/defaults/typed-params -- just make ONE user call"
 * inferred "do not run build commands" from the clause after the dash, and "do
 * NOT hardcode an external absolute path -- the test must be portable), run it
 * through runConformanceTest" inferred "do not run tests" in a brief that asked
 * for one. A semicolon ends the reach, and so does a dash: " -- " with a blank
 * on each side (so a "--flag" does not), or an em or en dash, spaced or not.
 * A comma does not: "do not build, test, or commit" is three prohibitions. */
static jc_size clause_len(const char *s, jc_size avail)
{
    jc_size end = sentence_len(s, avail);
    jc_size k;
    for (k = 0; k < end; k++) {
        unsigned char c = (unsigned char)s[k];
        if (c == ';') {
            return k;
        }
        /* L-21: a colon also ends the clause. The sentence "Never end a run
         * with a dirty tree: commit the work..." has "commit" after the colon,
         * and the negation "Never" must not cross that boundary. */
        if (c == ':') {
            return k;
        }
        if (c == '-' && k > 0 && is_blank(s[k - 1]) && k + 1 < avail &&
            s[k + 1] == '-' && (k + 2 >= avail || is_blank(s[k + 2]))) {
            return k;
        }
        if (c == 0xE2 && k + 2 < avail && (unsigned char)s[k + 1] == 0x80 &&
            ((unsigned char)s[k + 2] == 0x93 ||
             (unsigned char)s[k + 2] == 0x94)) {
            return k;
        }
    }
    return end;
}

/* M743: "make" is an English verb far more often than it is the build tool.
 * "Do not make any changes." inferred "do not run build commands" beside the
 * read-only rule the same sentence gives, and "just make if/else correct" did
 * the same. So the tool counts when it is RUN: a run verb right before it, or
 * an opening backtick or quote ("do not run make", "never invoke `make`").
 *
 * L-19: when "make" is inside backticks and followed by a specific target
 * (like `make ci`), it should NOT be treated as the build tool - the named
 * target must not widen to a class. The same applies when "make" is NOT quoted
 * but a run verb precedes it and the next non-blank token is a word (a target
 * like "ci" or "build") - "do not run make ci" names the target "ci", not the
 * build tool. A bare "make" (or one followed by punctuation) is the tool.
 *
 * Note: "do not run make changes" names "changes" as the target. This is a
 * false-negative of the same kind M743 accepted (a guess errs toward saying
 * nothing). */
static int make_as_tool(const char *win)
{
    long at = -1L;
    while ((at = word_offset(win, "make", at + 1)) >= 0) {
        long p = at - 1;
        long end;
        char w[16];
        if (p >= 0 && (win[p] == '`' || win[p] == '\'' || win[p] == '"')) {
            /* L-19: when make is inside backticks, check if it's followed
             * by another word (a specific target). If so, don't treat it as
             * the build tool. */
            long q = at + 4;  /* past "make" */
            while (q < (long)strlen(win) && (win[q] == ' ' || win[q] == '\t')) q++;
            /* If there's a word boundary and another word, it's a specific target */
            if (q < (long)strlen(win) && is_word_ch((unsigned char)win[q])) {
                /* "make ci" or similar: specific target, not the build tool */
                continue;
            }
            return 1;
        }
        /* M968: check if "make" is preceded by a run verb and followed by a word.
         * In that case, "make" names the target, not the build tool. */
        while (p >= 0 && win[p] == ' ') p--;
        if (p >= 0 && is_word_ch((unsigned char)win[p])) {
            end = p;
            while (p >= 0 && is_word_ch((unsigned char)win[p])) p--;
            if (end - p > 0 && end - p <= (long)sizeof w) {
                memcpy(w, win + p + 1, (size_t)(end - p));
                w[end - p] = '\0';
                if (has_run_verb(w)) {
                    /* There is a run verb before "make". Now check if "make" is
                     * followed by another word (a target like "ci" or "build"). */
                    long q = at + 4;  /* past "make" */
                    while (q < (long)strlen(win) && (win[q] == ' ' || win[q] == '\t')) q++;
                    /* If followed by a word, this is a named target, not the tool */
                    if (q < (long)strlen(win) && is_word_ch((unsigned char)win[q])) {
                        continue;
                    }
                    /* "make" followed by punctuation or end: still the tool */
                    return 1;
                }
            }
        }
        /* fall through: check if preceded by run verb alone (no target word) */
        while (p >= 0 && win[p] == ' ') p--;
        if (p < 0) continue;
        end = p;
        while (p >= 0 && is_word_ch((unsigned char)win[p])) p--;
        if (end - p <= 0 || end - p >= (long)sizeof w) continue;
        memcpy(w, win + p + 1, (size_t)(end - p));
        w[end - p] = '\0';
        if (has_run_verb(w)) return 1;
    }
    return 0;
}

/* ---- what the request quotes, and what it asks (M872, N45 (a)) ------------
 *
 * A "do not <verb>" inside quotation marks is MENTIONED, not said. The demos
 * drive's run 0091 carried its supervisor's question `should a task text say
 * "write REPORT.md, do not commit it", or name a per-task file in /tmp?`, and
 * jichi told a lead agent whose job is to commit merges, for the whole session,
 * not to commit. A question asks; it does not order -- "Do not commit?" did the
 * same. So a cue inside a quoted span infers nothing, and neither does a cue in
 * a sentence that ends in '?'.
 *
 * What is quoted: a fenced code block (``` or ~~~), an inline code span, and a
 * pair of double quotes, single quotes, or their typographic forms. A pair must
 * close within its paragraph, so a lone mark -- an inch sign, a stray quote --
 * quotes nothing. A straight quote opens only where a word may begin and closes
 * only where one may end, so the apostrophe in "don't" or "users'" does neither.
 *
 * The costs, taken on purpose, since a guess errs toward saying nothing: a rule
 * the request quotes because it means it ("the policy: "never push to main"")
 * infers nothing, and nor does an order put as a polite question ("can you make
 * sure you don't push?"). The request still says both; only the restatement and
 * the per-call note are lost. */

/* A paragraph break starts at s[k]: a newline, blanks, another newline. */
static int para_break_at(const char *s, jc_size len, jc_size k)
{
    jc_size e;
    if (s[k] != '\n') return 0;
    e = k + 1;
    while (e < len && (s[e] == ' ' || s[e] == '\t' || s[e] == '\r')) e++;
    return e < len && s[e] == '\n';
}

/* The byte before the mark at s[k], looking past one backslash: a prompt that
 * carries JSON-escaped text writes \"do not commit\" for a quoted phrase. 0 at
 * the start of the text. */
static int byte_before(const char *s, jc_size k)
{
    if (k == 0) return 0;
    if (s[k - 1] == '\\') return (k >= 2) ? (unsigned char)s[k - 2] : 0;
    return (unsigned char)s[k - 1];
}

/* A straight quote at s[k] may open a span: a word may begin before it (start,
 * blank, an opening bracket or a joining mark) and something follows it. A
 * single quote must be followed by a letter or digit, which an apostrophe
 * after a word never is in the position it opens from. */
static int may_open(const char *s, jc_size len, jc_size k, int single)
{
    int b = byte_before(s, k);
    unsigned char a = (k + 1 < len) ? (unsigned char)s[k + 1] : 0;
    if (b != 0 && !is_blank((char)b) && strchr("([{<:=/*_-", b) == NULL) {
        return 0;
    }
    if (a == 0 || is_blank((char)a)) return 0;
    return single ? is_word_ch(a) : 1;
}

/* ...and may close one: a word ends before it, and a blank, punctuation or the
 * end of the text follows. */
static int may_close(const char *s, jc_size len, jc_size k, jc_size w)
{
    int b = byte_before(s, k);
    unsigned char a = (k + w < len) ? (unsigned char)s[k + w] : 0;
    if (b == 0 || is_blank((char)b)) return 0;
    return a == 0 || is_blank((char)a) || strchr(".,;:!?)]}>*_'\"-\\", a) != NULL;
}

/* A typographic mark E2 80 xx at s[k]? */
static int typo_at(const char *s, jc_size len, jc_size k, unsigned char x)
{
    return k + 2 < len && (unsigned char)s[k] == 0xE2 &&
           (unsigned char)s[k + 1] == 0x80 && (unsigned char)s[k + 2] == x;
}

/* The closing mark for the span opened at s[i] (`kind`: '"', '\'', 'D' for a
 * typographic double, 'S' for a typographic single), searched up to the end
 * of the paragraph; its offset in *at and width in *w, or 0 when it is never
 * closed. */
static int find_close(const char *s, jc_size len, const char *m, jc_size i,
                      int kind, jc_size *at, jc_size *w)
{
    jc_size j = i + ((kind == 'D' || kind == 'S') ? 3 : 1);
    for (; j < len; j++) {
        if (m[j] || para_break_at(s, len, j)) return 0;
        if (kind == '"' && s[j] == '"' && may_close(s, len, j, 1)) {
            *at = j;
            *w = 1;
            return 1;
        }
        if (kind == '\'' && s[j] == '\'' && may_close(s, len, j, 1)) {
            *at = j;     /* may_close() wants no letter after: not "don't" */
            *w = 1;
            return 1;
        }
        if (kind == 'D' && typo_at(s, len, j, 0x9D)) {
            *at = j;
            *w = 3;
            return 1;
        }
        /* U+2019 is the closing single quote AND the typographic apostrophe
         * ("don\xe2\x80\x99t"): it closes only with no letter after it. */
        if (kind == 'S' && typo_at(s, len, j, 0x99) &&
            !(j + 3 < len && is_word_ch((unsigned char)s[j + 3]))) {
            *at = j;
            *w = 3;
            return 1;
        }
    }
    return 0;
}

/* mask[k] != 0 when s[k] is inside a quoted span (its marks included); NULL
 * when the arena is out of memory, which the callers read as "nothing quoted". */
static char *quote_mask(const char *s, jc_size len, struct jc_arena *a)
{
    char *m = (char *)jc_arena_alloc(a, len + 1);
    jc_size i;
    int fence = 0;

    if (m == NULL) return NULL;
    memset(m, 0, len + 1);

    /* Fenced code blocks, a line at a time: a line opening with ``` or ~~~
     * (up to three blanks in) opens or closes one, and is masked with it. */
    i = 0;
    while (i < len) {
        jc_size eol = i;
        jc_size k = i;
        int marker;
        while (eol < len && s[eol] != '\n') eol++;
        while (k < eol && k - i < 3 && s[k] == ' ') k++;
        marker = k + 3 <= eol &&
                 ((s[k] == '`' && s[k + 1] == '`' && s[k + 2] == '`') ||
                  (s[k] == '~' && s[k + 1] == '~' && s[k + 2] == '~'));
        if (marker || fence) memset(m + i, 1, eol - i);
        if (marker) fence = !fence;
        i = eol + 1;
    }

    /* Spans within a paragraph. */
    i = 0;
    while (i < len) {
        jc_size at = 0;
        jc_size w = 0;
        int kind = 0;
        if (m[i]) {
            i++;
            continue;
        }
        if (s[i] == '`') {
            /* An inline code span: a run of N backticks closed by the next run
             * of exactly N. */
            jc_size r = 0;
            jc_size j;
            while (i + r < len && s[i + r] == '`') r++;
            for (j = i + r; j < len; j++) {
                jc_size r2 = 0;
                if (m[j] || para_break_at(s, len, j)) break;
                if (s[j] != '`') continue;
                while (j + r2 < len && s[j + r2] == '`') r2++;
                if (r2 == r) {
                    memset(m + i, 1, j + r2 - i);
                    break;
                }
                j += r2 - 1;
            }
            i += r;
            continue;
        }
        if (s[i] == '"' && may_open(s, len, i, 0)) kind = '"';
        else if (s[i] == '\'' && may_open(s, len, i, 1)) kind = '\'';
        else if (typo_at(s, len, i, 0x9C)) kind = 'D';
        else if (typo_at(s, len, i, 0x98)) kind = 'S';
        if (kind != 0 && find_close(s, len, m, i, kind, &at, &w)) {
            memset(m + i, 1, at + w - i);
            i = at + w;
            continue;
        }
        i++;
    }
    return m;
}

/* Does the sentence holding s[pos] end in a question mark? */
static int asks(const char *low, jc_size len, jc_size pos)
{
    jc_size e = pos + sentence_len(low + pos, len - pos);
    return e < len && low[e] == '?';
}

static int mentioned_not_said(const char *low, jc_size len, const char *mask,
                              jc_size pos)
{
    if (mask != NULL && pos < len && mask[pos]) return 1;
    return asks(low, len, pos);
}

/* ---- who a read-only phrase speaks of (M878) ------------------------------ */

/* Words before a clause's first word that say nothing about who is meant. */
static const char *const RO_OPEN[] = {
    "please", "but", "and", "or", "so", "then", "also", "now", "just", "again",
    "instead", "however", "first", "finally", "here", "still", "otherwise",
    "therefore", "meanwhile", NULL
};
/* Verbs whose doer is in the mode: what follows is what it acts on. */
static const char *const RO_SELF[] = {
    "stay", "remain", "be", "work", "operate", "act", "proceed", "continue",
    "behave", "do", "go", "review", "read", "inspect", "analyze", "analyse",
    "audit", "investigate", "explore", "look", "check", "report", "answer",
    "examine", "study", "browse", "search", "survey", "evaluate", "assess",
    "only", "strictly", "purely", NULL
};
/* Verbs that PUT their object in the mode. */
static const char *const RO_PUT[] = {
    "keep", "treat", "run", "use", "leave", "make", "start", "open", "mount",
    "consider", "assume", "launch", "spawn", "set", "mark", NULL
};
/* Verbs that take a clause, and the words that open it ("make sure", "check
 * that", "remember to"); "ensure" takes one with or without them. */
static const char *const RO_COMP[] = {
    "make", "check", "verify", "confirm", "ensure", "note", "remember", "see",
    NULL
};
static const char *const RO_THAT[] = {
    "sure", "that", "whether", "if", "to", NULL
};
/* A subject that is the reader. "your" and "our" are not here: "your children
 * run in read-only mode" is about the children -- they take a scope noun. */
static const char *const RO_READER[] = {
    "you", "we", "us", "let", "lets", "everything", "nothing", NULL
};
/* What may stand between such a subject and its verb ("you must keep ..."). */
static const char *const RO_MODAL[] = {
    "must", "should", "shall", "will", "would", "can", "could", "may", "might",
    "need", "needs", "have", "has", "are", "to", "always", "only", "just",
    "also", "still", "please", "s", "us", NULL
};
static const char *const RO_PREP[] = {
    "in", "as", "for", "during", "while", "throughout", "under", "with", "from",
    "until", NULL
};
/* The reader's own scope: a determiner, then one of the nouns. */
static const char *const RO_SCOPE_DET[] = {
    "this", "the", "your", "our", "all", NULL
};
static const char *const RO_SCOPE[] = {
    "session", "run", "task", "job", "workspace", "repo", "repository", "tree",
    "worktree", "checkout", "project", "directory", "work", "pass", "review",
    "analysis", "turn", "conversation", "code", "codebase", "files", NULL
};
/* What may stand between a PUT verb and the phrase while the object stays the
 * reader's own: "keep it strictly read-only", "run in read-only mode". */
static const char *const RO_BETWEEN[] = {
    "this", "it", "things", "yourself", "yourselves", "us", "all", "in", "as",
    "to", "mode", "strictly", "purely", "only", "entirely", "completely",
    "fully", "always", NULL
};

/* The next word at or after `p` and before `end`, past any marks, into `w`;
 * the pointer past it, or NULL when nothing but marks stands before `end`. */
static const char *word_before(const char *p, const char *end, char *w,
                               jc_size cap)
{
    while (p < end && !is_word_ch((unsigned char)*p)) {
        p++;
    }
    if (p >= end) {
        w[0] = '\0';
        return NULL;
    }
    return take_word(p, w, cap);
}

/* Is the line ending at the newline low[nl] blank? */
static int ro_blank_line(const char *low, long nl)
{
    long k = nl;
    while (k > 0 && (low[k - 1] == ' ' || low[k - 1] == '\t' ||
                     low[k - 1] == '\r')) {
        k--;
    }
    return k == 0 || low[k - 1] == '\n';
}

/* Is the line ending at the newline low[nl] a markdown heading? */
static int ro_heading_line(const char *low, long nl)
{
    long s = nl;
    int hashes = 0;
    while (s > 0 && low[s - 1] != '\n') {
        s--;
    }
    while (s < nl && (low[s] == ' ' || low[s] == '\t')) {
        s++;
    }
    while (s < nl && low[s] == '#') {
        s++;
        hashes++;
    }
    return hashes >= 1 && hashes <= 6 &&
           (s == nl || low[s] == ' ' || low[s] == '\t');
}

/* Do the words from `p` to `end` -- what a PUT verb puts in the mode -- name
 * only the reader's own scope? "everything" is all of it; "the session" or
 * "your work" is the reader's; "the children" or "the corpus" is not. */
static int ro_object_is_reader(const char *p, const char *end)
{
    char o[24];
    const char *n;
    while ((n = word_before(p, end, o, sizeof o)) != NULL) {
        if (strcmp(o, "everything") == 0 || strcmp(o, "anything") == 0) {
            return 1;
        }
        if (word_in(o, RO_SCOPE_DET)) {
            char o2[24];
            const char *n2 = word_before(n, end, o2, sizeof o2);
            if (n2 != NULL && word_in(o2, RO_SCOPE)) {
                p = n2;
                continue;
            }
        }
        if (!word_in(o, RO_BETWEEN)) {
            return 0;                    /* an object of its own */
        }
        p = n;
    }
    return 1;
}

/* The start of the clause holding offset `at`: back to a sentence's end, a
 * blank line, a heading, a list item or a table cell, or a ';', ':', ',', '('
 * or a dash. *soft is set when it was a ',', '(' or a dash, the marks an aside
 * sits behind; with `hard_only` those are walked past. */
static long ro_clause_start(const char *low, long at, int hard_only, int *soft)
{
    long b = at;
    *soft = 0;
    while (b > 0) {
        unsigned char c = (unsigned char)low[b - 1];
        int dash = (c == '-' && b >= 2 && low[b - 2] == '-') ||
                   ((c == 0x93 || c == 0x94) && b >= 3 &&
                    (unsigned char)low[b - 3] == 0xe2 &&
                    (unsigned char)low[b - 2] == 0x80);   /* U+2013, U+2014 */
        if (c == ',' || c == '(' || dash) {
            if (!hard_only) {
                *soft = 1;
                break;
            }
        } else if (c == ';' || c == ':' || c == '!' || c == '?' || c == ')' ||
                   c == '|') {
            break;                       /* '|': a table's cell */
        } else if (c == '.' && is_blank(low[b])) {
            break;
        } else if (c == '\n' && (ro_blank_line(low, b - 1) ||
                                 ro_heading_line(low, b - 1) ||
                                 opens_list_item(low + b, strlen(low + b)))) {
            break;
        }
        b--;
    }
    return b;
}

/* Is the clause from low[b] to the phrase at low[stop] about SOMEONE ELSE?
 * Judged on its first word, past marks and discourse words. */
static int ro_judge(const char *low, long b, long stop)
{
    const char *end = low + stop;
    const char *p = skip_blanks_emph(low + b);
    const char *next;
    char w[24];
    int depth;
    for (depth = 0; depth < 2; depth++) {
        for (;;) {
            next = word_before(p, end, w, sizeof w);
            if (next == NULL) {
                return 0;                /* the phrase opens its clause */
            }
            if (!word_in(w, RO_OPEN)) {
                break;
            }
            p = next;
        }
        if (depth == 0 && word_in(w, RO_COMP)) {
            /* "make sure ...", "check that ...": judge the clause it takes */
            char w2[24];
            const char *q = next;
            const char *n2;
            int took = 0;
            while ((n2 = word_before(q, end, w2, sizeof w2)) != NULL &&
                   word_in(w2, RO_THAT)) {
                q = n2;
                took = 1;
            }
            if (took || strcmp(w, "ensure") == 0) {
                p = q;
                continue;
            }
        }
        break;
    }
    if (word_in(w, RO_READER)) {
        /* "you must keep the children read-only": the verb still decides */
        char v[24];
        const char *q = next;
        const char *nv;
        while ((nv = word_before(q, end, v, sizeof v)) != NULL &&
               word_in(v, RO_MODAL)) {
            q = nv;
        }
        if (nv != NULL && word_in(v, RO_PUT)) {
            return !ro_object_is_reader(nv, end);
        }
        return 0;
    }
    if (word_in(w, RO_SCOPE_DET)) {
        char w2[24];
        const char *n2 = word_before(next, end, w2, sizeof w2);
        return !(n2 != NULL && word_in(w2, RO_SCOPE));  /* "this session is" */
    }
    if (word_in(w, RO_PUT)) {
        return !ro_object_is_reader(next, end);
    }
    if (word_in(w, RO_SELF) || word_in(w, RO_PREP)) {
        return 0;
    }
    return 1;                            /* a subject of its own: a statement */
}

/* M878 (the demos drive's N45, continued): is the clause holding a read-only
 * phrase at `at` about SOMEONE ELSE?
 *
 * A supervisor's note drafted for the drive's lead said that a child "runs in
 * read-only mode" and quoted jichi's refusal "tool disabled in read-only mode";
 * `constraints scan` adopted a session-wide read-only rule from it, for a lead
 * whose job is to merge. M872 skips what is quoted or asked; this skips what is
 * STATED of a third party, or ordered FOR one. The cue words before the phrase
 * ("in", "stays") say how, not who, so the clause decides:
 *
 *  - Its first word. An order starts with a verb ("Stay read-only"), speaks to
 *    the reader ("You must stay read-only"), names the reader's own scope ("This
 *    session is read-only", "Everything stays read-only"), or opens with a
 *    preposition or the phrase itself ("Mode: read-only"). Any other start names
 *    someone else -- "The child runs in read-only mode", "Your children run in
 *    read-only mode", "It answers ..." -- and is a statement.
 *  - A verb that PUTS its object in the mode ("run", "keep", "start", "treat")
 *    orders the reader only when that object is the reader's own scope: "Run
 *    everything in read-only mode" and "Keep this read-only" are orders; "Run
 *    the children in read-only mode" is about the children, and "Keep the corpus
 *    read-only" is narrower than a session-wide rule. "You must keep ..." is
 *    judged the same way.
 *  - "Make sure", "check that", "ensure" and their kind take a clause, judged on
 *    its own first word: "Make sure the child runs in read-only mode" is about
 *    the child; "Make sure you stay read-only" is an order.
 *  - An aside -- the phrase alone behind a ',', '(' or a dash, as in "figures
 *    from this machine, read-only, ..." or "plan mode (read-only)" -- is judged
 *    by the clause it interrupts.
 *
 * The clause runs from the last boundary before the phrase: a sentence's end, a
 * blank line, a heading, a list item or a table cell, or a ';', ',', ':' or a
 * dash, so "When the child runs, keep it read-only" is judged on "keep". Marks
 * and discourse words before the first word are skipped, so a blockquote ("> The
 * child runs ...") is judged on "the child". A miss costs a restatement of an
 * instruction the request still makes; a false order told a lead to change
 * nothing for its whole session -- where this cannot tell, it reads a statement. */
static int read_only_said_of_other(const char *low, long at)
{
    int soft;
    long b = ro_clause_start(low, at, 0, &soft);
    if (soft) {
        const char *p = low + b;
        while (p < low + at && !is_word_ch((unsigned char)*p)) {
            p++;
        }
        if (p >= low + at) {             /* an aside: what it interrupts decides */
            long stop = b - 1;
            b = ro_clause_start(low, stop, 1, &soft);
            return ro_judge(low, b, stop);
        }
    }
    return ro_judge(low, b, at);
}

/* A closing mark that may stand between a sentence's terminator and the blank
 * after it (the set sentence_len() skips). */
static int closing_mark(char c)
{
    return c == '"' || c == '\'' || c == ')' || c == ']' || c == '*' ||
           c == '`' || c == '_';
}

/* M872 (N45 (b)): the sentence holding offset `pos`, as the request wrote it.
 *
 * It starts after the previous sentence's end -- '.', '!' or '?' with a blank
 * after it (closing marks allowed between), a blank line, or a newline that
 * opens a list item, whose marker is dropped -- and ends where sentence_len()
 * ends the cue's scope, terminator included. Blanks, newlines among them,
 * collapse to one space. A sentence longer than JC_CONSTRAINT_SOURCE_MAX is
 * quoted as a window that keeps the cue, cut at blanks, with "..." on each cut
 * side. NULL only when the arena is out of memory. */
static char *source_sentence(const char *msg, jc_size len, jc_size pos,
                             struct jc_arena *a)
{
    char buf[JC_CONSTRAINT_SOURCE_MAX + 8];
    jc_size start = 0;
    jc_size end;
    jc_size from;
    jc_size to;
    jc_size k;
    jc_size bl = 0;
    int blank = 0;
    int wrote = 0;

    for (k = pos; k > 0; k--) {
        char c = msg[k - 1];
        if (c == '\n') {
            jc_size b = k - 1;
            while (b > 0 && (msg[b - 1] == ' ' || msg[b - 1] == '\t' ||
                             msg[b - 1] == '\r')) {
                b--;
            }
            if (b == 0 || msg[b - 1] == '\n' ||
                opens_list_item(msg + k, len - k)) {
                start = k;
                break;
            }
        } else if (c == '.' || c == '!' || c == '?') {
            jc_size e = k;
            while (e < pos && closing_mark(msg[e])) e++;
            if (e < pos && is_blank(msg[e])) {
                start = e;
                break;
            }
        }
    }
    while (start < pos && is_blank(msg[start])) start++;
    if (opens_list_item(msg + start, pos - start)) {
        while (start < pos && !is_blank(msg[start])) start++;
        while (start < pos && is_blank(msg[start])) start++;
    }

    end = pos + sentence_len(msg + pos, len - pos);
    if (end < len && (msg[end] == '.' || msg[end] == '!' || msg[end] == '?')) {
        end++;
        while (end < len && closing_mark(msg[end])) end++;
    }

    from = start;
    to = end;
    if (end - start > JC_CONSTRAINT_SOURCE_MAX) {
        jc_size lead = JC_CONSTRAINT_SOURCE_MAX / 3;
        if (pos > start + lead) {
            from = pos - lead;
            while (from > start && !is_blank(msg[from - 1])) from--;
            if (from == start) from = pos - lead;
        }
        to = from + JC_CONSTRAINT_SOURCE_MAX - 8;
        if (to >= end) {
            to = end;
        } else {
            jc_size t = to;
            while (t > pos && !is_blank(msg[t])) t--;
            if (t > pos) to = t;
        }
        while (from < pos && ((unsigned char)msg[from] & 0xC0) == 0x80) from++;
        while (to > pos && to < end && ((unsigned char)msg[to] & 0xC0) == 0x80) {
            to--;
        }
    }

    if (from > start) {
        memcpy(buf, "...", 3);
        bl = 3;
    }
    for (k = from; k < to && bl + 4 < sizeof buf; k++) {
        if (is_blank(msg[k])) {
            blank = 1;
            continue;
        }
        if (blank && wrote) buf[bl++] = ' ';
        blank = 0;
        wrote = 1;
        buf[bl++] = msg[k];
    }
    if (to < end && bl + 3 < sizeof buf) {
        memcpy(buf + bl, "...", 3);
        bl += 3;
    }
    buf[bl] = '\0';
    return jc_arena_strdup(a, buf);
}

/* M1069 (G2): does `w` spell a FILE name -- a '.' with word characters on
 * both sides and at least one letter in the extension ("report.md")? "3.5"
 * is a number, and a leading or trailing dot is not a name. `w` is lowercased
 * (the scan window is), so the letter test is 'a'..'z'. */
static int is_file_name(const char *w)
{
    int i, j;
    for (i = 0; w[i] != '\0'; i++) {
        int letter;
        if (w[i] != '.' || i == 0 || w[i + 1] == '\0') continue;
        if (!is_word_ch((unsigned char)w[i - 1])) continue;
        letter = 0;
        for (j = i + 1; w[j] != '\0' && is_word_ch((unsigned char)w[j]); j++) {
            if (w[j] >= 'a' && w[j] <= 'z') letter = 1;
        }
        if (letter) return 1;
    }
    return 0;
}

/* M1069 (G2): does the prohibition NAME its object as a file?
 *
 * True when an occurrence of `base` in VERB position -- the occurrence that
 * established the prohibition, the same test as in_verb_position -- is
 * followed by a determiner/pronoun (a, an, the, this, that, these, those, it,
 * its, my, our, your, his, her, their) and then a word carrying a FILE
 * EXTENSION ("commit a REPORT.md"). The prohibition is of THAT object, not of
 * the class, so the blanket rule for `base` is not emitted.
 *
 * A determiner without a file name is not enough: "do not commit the work"
 * and "never commit or push my work" (the word before "my" is "push", not
 * "commit") still emit. And a file name without a determiner -- "commit
 * REPORT.md" -- is heard as before: the measured misparse (LOG-3's M1069,
 * I1b) had the determiner + name shape. The build/test arms pass through the
 * same test: in "do not run the build, nor the tests" neither target word is
 * in verb position (a determiner precedes it), so the guards emit unchanged.
 *
 * Not applied to the privilege arm: "root"'s scope-noun reading is G1's job,
 * and "sudo" / "doas" / "pkexec" / "privileged" are named launchers, not
 * command classes. */
static int named_file_after(const char *win, const char *base)
{
    static const char *DET[] = { "a", "an", "the", "this", "that", "these",
                                 "those", "it", "its", "my", "our", "your",
                                 "his", "her", "their", NULL };
    static const char *LEAD[] = { "ever", "just", "again", "please", "actually",
                                  "blindly", "automatically", "to", "or", "and",
                                  "nor", "then", "either", "neither", NULL };
    const char *forms[4];
    char store[3][40];
    char w[40];
    int f, i;

    word_forms(base, forms, store);
    for (f = 0; forms[f] != NULL; f++) {
        long at = -1L;
        while ((at = word_offset(win, forms[f], at + 1)) >= 0) {
            long p = at - 1;
            long end;
            int verb = 0;
            const char *s;
            const char *e;
            while (p >= 0 && win[p] == ' ') p--;
            if (p < 0) {
                verb = 1;                          /* heads the window */
            } else if (win[p] == ',' || win[p] == ';') {
                verb = 1;                          /* a coordinated item */
            } else {
                end = p;
                while (p >= 0 && is_word_ch((unsigned char)win[p])) p--;
                for (i = 0; LEAD[i] != NULL; i++) {
                    long ln = (long)strlen(LEAD[i]);
                    if (end - p == ln &&
                        word_offset(win + p + 1, LEAD[i], 0) == 0) {
                        verb = 1;
                        break;
                    }
                }
            }
            if (!verb) continue;
            /* the word after the occurrence: a determiner, else no naming */
            s = win + at + (long)strlen(forms[f]);
            while (*s == ' ' || *s == '\t') s++;
            if (!is_word_ch((unsigned char)*s)) continue;
            e = take_word(s, w, sizeof w);
            if (!word_in(w, DET)) continue;
            /* the word after the determiner: a run of word chars AND dots,
             * so "report.md" arrives whole (take_word would stop at '.') */
            s = skip_blanks(e);
            {
                jc_size j = 0;
                while (s[j] != '\0' &&
                       (is_word_ch((unsigned char)s[j]) || s[j] == '.') &&
                       j + 1 < sizeof w) {
                    w[j] = s[j];
                    j++;
                }
                w[j] = '\0';
            }
            if (is_file_name(w)) return 1;
        }
    }
    return 0;
}

/* M1069-b (G3 rework, SV:1359): does a "never" clause DESCRIBE its subject
 * rather than forbid an action?
 *
 * A description has BOTH halves, and only then is the window skipped:
 *
 *   (1) the word that FOLLOWS "never" is a finite form of a running verb
 *       (run/runs/running/.../use/uses) -- the clause's own action, the
 *       subject's verb; and
 *
 *   (2) the word that PRECEDES "never" (walking back over any run of modal
 *       auxiliaries) is a SUBJECT NOUN of a third party -- not the reader
 *       ("You must never ..."), not a clause-start, not an opener
 *       ("please never", "and never").
 *
 * The old G3 tested (1) only, plus "no target within three words" -- and the
 * target-distance clause dropped four real ORDERS the operator measured, whose
 * targets sit four or more words out or are only implied:
 *   - "Never run any command with sudo."              (privilege)
 *   - "Never run any of these commands as root."      (privilege)
 *   - "Never run anything that needs root."           (privilege)
 *   - "You must never run the full unit test suite here."  (run_tests + test)
 * and the skip happened BEFORE scan_window, so even the mention-only sudo arm
 * was starved. The privilege losses are the safety-relevant class. So the
 * target-distance half of the old test is gone: (1) now only says "is this a
 * clause whose verb is the subject's own action", and (2) decides description
 * vs order.
 *
 * The SV:1359 shape for (2): an order starts the clause, or follows the
 * READER ("You must never ..."), or follows an opener/conjunction ("please
 * never", "and never"); a description has a SUBJECT NOUN of a third party
 * ("The branches never run", "The job never uses", "The hooks never run at
 * commit time"). Keeping (1) is what preserves a reported-speech order whose
 * preceding word is a bare verb: "it says never push anything to the remote"
 * -- "push" is not a finite run-verb, so (1) fails and the push order stands
 * (the word before "never" is "says", which on its own would have been read as
 * a third-party subject and silently dropped the ban).
 *
 * `after` is the offset just PAST the "never" cue, so the backward walk first
 * skips over the word "never" itself, then over any run of modal/auxiliary
 * words (must, can, ... up to three -- "You must never" reaches "You"), and
 * classifies the word reached. The two description rows below, still wrong in
 * both OLD and NEW per SV:1359, are pinned by test_scan_m1069b:
 *   - "The job never uses sudo."      -> n == 0
 *   - "The hooks never run at commit time."  -> n == 0
 *
 * M1069-c (SV:1459), the deontic half, documented at the DEONTIC list below
 * and pinned by test_scan_m1069c: must/should/shall/may are ALL treated as
 * deontic by the drive's decision -- a norm whose SUBJECT INCLUDES THE WORK
 * binds the agent ("Developers should never run ...", and the same for
 * "The CI may never run the integration tests": "may" here is not an
 * epistemic guess, it is the CI's stated policy, and the agent doing the
 * work is among the CI's subject). "may" is intentionally NOT removed from
 * DEONTIC; the may row is pinned green in test_scan_m1069d.
 *
 * M1069-d, the reporting-verb half: a REPORTING VERB directly
 * before "never" is CROSSED like a modal -- the verb itself is never what
 * classifies, its SUBJECT is. "it says never run the tests" reports an order
 * the prompt itself states, and "it" is a third-party subject, not the
 * reader, so the window is kept and the ban stands (before M1069-d the walk
 * stopped at "says", classified the VERB as a subject noun, and the whole
 * order was silently dropped). The walk bound grows from three to four hops
 * -- at most one reporting word plus the existing three modals -- so it
 * stays bounded. Pinned by test_scan_m1069d:
 *   - "it says never run the tests."       -> run_tests + test
 *   - "The CI may never run the ... tests." -> test (deontic may, kept)
 *   - "The spec states never commit ..."   -> commit (a green guard kept by
 *     gate (1), NOT the reported crossing: "commit" is not a FIN verb, so gate
 *     (1) keeps the window before the backward walk ever runs -- it exercises
 *     none of the M1069-d reporting-verb mechanism) */
static int never_describes_subject(const char *low, jc_size after)
{
    static const char *MODAL[] = { "must", "can", "could", "should", "would",
        "will", "shall", "may", "might", "do", "does", "did", NULL };
    /* M1069-d: a reporting verb standing directly before "never"
     * ("it says never ...", "the spec states never ...") is crossed, not
     * classified: the reporting verb's SUBJECT is what classifies the clause,
     * and that subject -- "it", "the spec", "the note" -- is a third party,
     * never a READER word, so a reported order is kept and a reported
     * description is skipped. The verb itself ("says") would otherwise be
     * read as a subject noun and the order dropped. */
    static const char *REPORTING[] = { "says", "say", "stated", "states",
        "state", "specifies", "specify", "notes", "note", "writes",
        "write", "requires", "require", NULL };
    /* M1069-b: the READER is being ORDERED, not described. This is the key
     * difference from is_descriptive_before, which treats "you" as a
     * descriptive subject -- "You must never run ..." orders you. */
    static const char *READER[] = { "you", "we", "us", "let", "your", "yours",
        "our", "ours", NULL };
    /* Reuses is_descriptive_before's OPENERS: a clause opened by one of these
     * ("please never", "and never", "but never") is still an order. */
    static const char *OPENERS[] = { "please", "but", "and", "or", "nor", "so",
        "then", "however", "also", "now", "instead", "just", "again", NULL };
    /* (1): the clause's own action -- a finite form of a running verb. */
    static const char *FIN[] = { "run", "runs", "running", "rerun",
        "execute", "executed", "executes", "call", "calls", "start",
        "starts", "launch", "use", "uses", NULL };
    long p;
    char w[40];
    int modals, m, f, deontic, reported;

    /* M1069-c (SV:1459): set when the backward modal walk crosses one of the
     * DEONTIC modals (must/should/shall/may) -- a NORM, not a mere auxiliary.
     * A deontic between a third-party subject and "never" is an order the
     * agent is among, so the final classification keeps the window. */
    deontic = 0;
    /* M1069-d: set when the walk crosses a REPORTING VERB. Gate (1) only
     * passes for a FIN verb ("run", "use", ...), so reaching the reporter
     * here means the reported clause's action is such a verb; the reporting
     * subject ("it", "the spec") is a third party, not a READER word, yet the
     * reported order ("it says never run the tests") is one the agent must
     * still respect -- a flag that, like the deontic, forces the final
     * classification to keep the window. */
    reported = 0;

    /* (1) the first word after "never" must be a finite run-verb. */
    {
        const char *s = skip_blanks(low + after);
        if (!is_word_ch((unsigned char)*s)) return 0;
        s = take_word(s, w, sizeof w);
        for (f = 0; FIN[f] != NULL; f++) {
            if (strcmp(w, FIN[f]) == 0) break;
        }
        if (FIN[f] == NULL) return 0;  /* "never push...", "never commit...": an order */
    }

    /* (2) walk backward over the "never" cue itself -- `after` is just past it. */
    p = (long)after - 1;
    while (p >= 0 && is_word_ch((unsigned char)low[p])) p--;
    /* Step into the word before "never" (if any). */
    while (p >= 0 && (low[p] == ' ' || low[p] == '\t')) p--;

    /* Walk backward over a run of modal/auxiliary words (at most three, plus
     * one reporting verb -- M1069-d -- so the total stays bounded at four).
     * "You must never ..." skips "must" to reach the subject "You";
     * "it says never ..." skips "says" to reach the reporting subject "it". */
    for (modals = 0; modals < 4; modals++) {
        if (p < 0 || !is_word_ch((unsigned char)low[p])) return 0;
        {
            long b = p;
            int is_modal = 0, is_deontic = 0, is_reporting = 0;
            /* M1069-c (SV:1459): the DEONTIC set -- a norm, not a mere
             * auxiliary. A deontic between a third-party subject and "never"
             * (e.g. "Developers should never run ...") is an order the agent
             * is among, not a description of the subject's habits, so the
             * final classification must keep the window. would/could/can/will/
             * do/does/did are NOT deontic: they read as ordinary auxiliaries
             * or epistemic possibility, not as binding norms. */
            static const char *DEONTIC[] = { "must", "should", "shall", "may",
                NULL };
            while (b > 0 && is_word_ch((unsigned char)low[b - 1])) b--;
            for (m = 0; MODAL[m] != NULL; m++) {
                if ((long)strlen(MODAL[m]) == p - b + 1 &&
                    strncmp(low + b, MODAL[m], (size_t)(p - b + 1)) == 0) {
                    is_modal = 1;
                    for (f = 0; DEONTIC[f] != NULL; f++) {
                        if (strncmp(low + b, DEONTIC[f],
                                    (size_t)(p - b + 1)) == 0) {
                            is_deontic = 1;
                            break;
                        }
                    }
                    break;
                }
            }
            /* M1069-d: a REPORTING VERB is crossed like a modal -- the verb
             * ("says") is not the subject; its SUBJECT ("it", "the spec") is
             * what classifies. Crossing it sets `reported`, which, like a
             * deontic, forces the final classification to keep the window:
             * a reported order ("it says never run the tests") binds the
             * agent even though the reporter is a third party. */
            if (!is_modal) {
                for (m = 0; REPORTING[m] != NULL; m++) {
                    if ((long)strlen(REPORTING[m]) == p - b + 1 &&
                        strncmp(low + b, REPORTING[m],
                                (size_t)(p - b + 1)) == 0) {
                        is_reporting = 1;
                        break;
                    }
                }
            }
            if (!is_modal && !is_reporting) break;
            if (is_deontic) deontic = 1;
            if (is_reporting) reported = 1;
            p = b - 1;
            while (p >= 0 && (low[p] == ' ' || low[p] == '\t')) p--;
        }
    }

    /* No word reached, or the reached word sits at the start of the message:
     * "never" begins the clause -> an ORDER. (A reached word that is a
     * list-item digit, not a letter, is also a clause start.) */
    if (p < 0 || !is_word_ch((unsigned char)low[p])) return 0;

    /* Step 2: extract the word reached (case-insensitive; `low` is the
     * lowercased buffer) and classify it. */
    {
        long b = p;
        jc_size wl;
        while (b > 0 && is_word_ch((unsigned char)low[b - 1])) b--;
        wl = (jc_size)(p - b + 1);
        if (wl >= sizeof w) wl = sizeof w - 1;
        memcpy(w, low + b, wl);
        w[wl] = '\0';
        for (m = 0; READER[m] != NULL; m++) {
            if (strcmp(w, READER[m]) == 0) return 0; /* reader: an order */
        }
        for (m = 0; OPENERS[m] != NULL; m++) {
            if (strcmp(w, OPENERS[m]) == 0) return 0; /* opener: an order */
        }
    }
    /* Any other word is a third-party subject noun -- a description, skip.
     * M1069-c (SV:1459): EXCEPT when the modal walk crossed a DEONTIC
     * (must/should/shall/may). Then the clause is a NORM the agent is among,
     * not a description of the subject's habits -- "Developers should never
     * run the deploy script by hand." must be kept and scanned. The plain
     * present with no deontic ("The CI never runs ...") stays a description.
     * M1069-d: EXCEPT TOO when the walk crossed a REPORTING VERB ("says",
     * "states", ...). The reported subject ("it", "the spec") is a third
     * party, but the clause RELAYS AN ORDER to the agent, not a description
     * of the reporter's habits -- "it says never run the tests" must be kept
     * and scanned, so the reported flag forces keep exactly as the deontic
     * does. */
    if (deontic || reported) return 0; /* a norm / a reported order: keep */
    return 1;
}

/* Look for target keywords in `window` and emit the matching constraints. */
static void scan_window(const char *window, struct jc_constraint *out, int *n,
                        int max, struct jc_arena *a)
{
    if ((mentions(window, "build") && acts_on(window, "build")) ||
        (mentions(window, "compile") && acts_on(window, "compile")) ||
        (mentions(window, "make") && make_as_tool(window))) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "build", NULL);
    }
    if (mentions(window, "test") && acts_on(window, "test")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_TOOL, "run_tests", NULL);
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "test", NULL);
    }
    if (mentions(window, "commit") && acts_on(window, "commit") &&
        !named_file_after(window, "commit")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_TOOL, "git_commit", NULL);
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "commit", NULL);
    }
    if (mentions(window, "push") && acts_on(window, "push") &&
        !named_file_after(window, "push")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "push", NULL);
    }
    if (mentions(window, "deploy") && acts_on(window, "deploy") &&
        !named_file_after(window, "deploy")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "deploy", NULL);
    }
    if (mentions(window, "install") && acts_on(window, "install") &&
        !named_file_after(window, "install")) {
        emit(out, n, max, a, JC_CONSTRAINT_DENY_CMD, "install", NULL);
    }
    /* M1069 (G1): "root" is the one privilege word that is also an ordinary
     * SCOPE NOUN -- "a report at the repository root", "the repository root"
     * -- so a bare mention of it must pass the same acts_on test the other
     * arms use. "sudo" / "doas" / "pkexec" / "privileged" have no such
     * reading: naming the launcher IS naming the privilege, and they stay
     * mention-only. "do not run as root" and "never run with root
     * privileges" still infer (the run verb); "do not use sudo" still
     * infers (the mention). */
    if (mentions(window, "sudo") || mentions(window, "privileged") ||
        mentions(window, "doas") || mentions(window, "pkexec") ||
        (mentions(window, "root") && acts_on(window, "root"))) {
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
    char *mask;
    jc_size len, i;
    long at;
    int n = 0;

    if (msg == NULL || out == NULL || a == NULL) return 0;
    len = (jc_size)strlen(msg);
    low = (char *)jc_arena_alloc(a, len + 1);
    if (low == NULL) return 0;
    for (i = 0; i < len; i++) low[i] = (char)lc((unsigned char)msg[i]);
    low[len] = '\0';
    /* M872 (N45 (a)): what the request quotes. Offsets in `low`, `mask` and
     * `msg` are the same byte for byte, which is what lets a rule's source
     * sentence be cut from the request as written. */
    mask = quote_mask(low, len, a);

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
     * through global_edit_prohibition_at, which adopts the blanket constraint
     * only when the object is broad. */
    at = read_only_instruction_at(low, len, mask);
    if (at < 0) {
        at = global_edit_prohibition_at(low, len, mask);
    }
    if (at >= 0) {
        int before = n;
        emit(out, &n, max, a, JC_CONSTRAINT_READ_ONLY, NULL, NULL);
        if (n > before) {
            out[before].source = source_sentence(msg, len, (jc_size)at, a);
        }
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
            /* M743: and after it. A cue glued to a letter is inside a word
             * ("nevertheless", "do note that"), and one glued to a hyphen is a
             * compound adjective: "forces never-gated dead code to COMPILE/RUN"
             * inferred "do not run build commands" in five stored prompts --
             * M433's own example, "never-compiled", still firing. */
            if (NEG[c][cl - 1] != ' ' && i + cl < len &&
                (is_word_ch((unsigned char)low[i + cl]) || low[i + cl] == '-')) {
                continue;
            }
            if (is_pronoun_after(low + i + cl)) continue; /* conversational */
            /* "the tests do not compile" states a fact; it does not forbid
             * compiling. Only the "do not" family -- never/must not are
             * normative even in the third person. */
            if (c <= 2 && is_descriptive_before(low, i)) continue;
            /* M743: "do not JUST build blindly" asks for more than building;
             * "not only", "not merely", "not simply" the same. None forbids. */
            {
                char nw[16];
                (void)take_word(skip_blanks(low + i + cl), nw, sizeof nw);
                if (strcmp(nw, "just") == 0 || strcmp(nw, "only") == 0 ||
                    strcmp(nw, "merely") == 0 || strcmp(nw, "simply") == 0) {
                    continue;
                }
            }
            /* M872 (N45 (a)): a cue the request quotes, or one in a question,
             * is mentioned, not said. */
            if (mentioned_not_said(low, len, mask, i)) continue;
            /* M1069 (G3): a "never" clause whose first word is a finite
             * present form of the clause's own action describes its subject
             * -- "The branches never run at runtime" -- and forbids nothing.
             * "never commit or push my work" (first word not in the list) and
             * "never run as root" / "never run tests" / "never run sudo"
             * (a target within three words) are still prohibitions. */
            if (c == 3 && never_describes_subject(low, i + cl)) continue;
            {
                char win[96];
                int before = n;
                /* D2 (M735): the rest of this sentence, not the next 95 bytes;
                 * M743: of this clause. */
                jc_size wl = clause_len(low + i + cl, len - (i + cl));
                if (wl > sizeof(win) - 1) wl = sizeof(win) - 1;
                memcpy(win, low + i + cl, wl);
                win[wl] = '\0';
                scan_window(win, out, &n, max, a);
                /* M872 (N45 (b)): every rule this cue produced carries the
                 * sentence it was read from. A rule an earlier cue already
                 * produced was deduped by emit() and keeps its first source. */
                if (n > before) {
                    char *src = source_sentence(msg, len, i, a);
                    int k;
                    for (k = before; k < n; k++) {
                        out[k].source = src;
                    }
                }
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
            /* A shell command that hits the forbidden key. M873 (N40): a rule
             * jichi INFERRED is matched against what the command runs, so a
             * search for the word is not said to break it; a rule the operator
             * WROTE keeps the word match, which errs toward refusing. */
            if (command != NULL &&
                (strcmp(tool_name, "run_terminal_command") == 0 ||
                 strcmp(tool_name, "run_tests") == 0) &&
                (c->origin == JC_CONSTRAINT_INFERRED
                     ? jc_constraint_cmd_runs(c->subject, command)
                     : jc_constraint_cmd_hits(c->subject, command))) {
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

enum jc_constraint_verdict jc_constraint_judge(const struct jc_constraint *cs,
                                               int n, const char *tool_name,
                                               const char *command,
                                               int tool_readonly,
                                               int explicit_write_allowed,
                                               char *reason, jc_size cap,
                                               int *rule)
{
    int i;

    if (rule != NULL) *rule = -1;
    if (cs == NULL || tool_name == NULL || reason == NULL || cap == 0) {
        return JC_CONSTRAINT_ALLOW;
    }
    reason[0] = '\0';
    /* The operator's own rules first: a guess sitting beside a policy must not be
     * what decides a call the policy forbids. */
    for (i = 0; i < n; i++) {
        if (cs[i].origin == JC_CONSTRAINT_INFERRED) continue;
        if (jc_constraint_blocks_ex(&cs[i], 1, tool_name, command,
                                    tool_readonly, explicit_write_allowed,
                                    reason, cap)) {
            if (rule != NULL) *rule = i;
            return JC_CONSTRAINT_REFUSE;
        }
    }
    for (i = 0; i < n; i++) {
        if (cs[i].origin != JC_CONSTRAINT_INFERRED) continue;
        if (jc_constraint_blocks_ex(&cs[i], 1, tool_name, command,
                                    tool_readonly, explicit_write_allowed,
                                    reason, cap)) {
            jc_snprintf(reason, cap, "%s",
                        cs[i].text != NULL ? cs[i].text : "(constraint)");
            if (rule != NULL) *rule = i;
            return JC_CONSTRAINT_ADVISE;
        }
    }
    reason[0] = '\0';
    return JC_CONSTRAINT_ALLOW;
}

/* ---- render -------------------------------------------------------------- */

void jc_constraint_render(const struct jc_constraint *cs, int n,
                          struct jc_sb *sb)
{
    int i;
    int nbind = 0;
    int nadv = 0;

    if (cs == NULL || sb == NULL || n <= 0) return;
    for (i = 0; i < n; i++) {
        if (cs[i].origin == JC_CONSTRAINT_INFERRED) nadv++; else nbind++;
    }
    if (nbind > 0) {
        jc_sb_append(sb,
            "\n# Active constraints (ENFORCED -- a violating tool call is "
            "REFUSED)\n");
        for (i = 0; i < n; i++) {
            if (cs[i].origin == JC_CONSTRAINT_INFERRED) continue;
            jc_sb_append(sb, "- ");
            jc_sb_append(sb, cs[i].text != NULL ? cs[i].text : "(constraint)");
            jc_sb_append(sb, "\n");
        }
        jc_sb_append(sb,
            "These are hard limits set by the user. Do NOT attempt to work "
            "around them (e.g. via a different tool or a shell alias).\n");
    }
    /* M734: a guess is presented as a guess. The model is the only reader that
     * can weigh one against the task, so it is told where the rule came from and
     * what to do when the two disagree -- report it, rather than obey a misparse
     * or quietly ignore a real instruction. */
    if (nadv > 0) {
        jc_sb_append(sb,
            "\n# Inferred from the request (ADVISORY -- not enforced)\n");
        for (i = 0; i < n; i++) {
            if (cs[i].origin != JC_CONSTRAINT_INFERRED) continue;
            jc_sb_append(sb, "- ");
            jc_sb_append(sb, cs[i].text != NULL ? cs[i].text : "(constraint)");
            /* M872 (N45 (b)): and the words it was read from, so the one
             * reader that can weigh a guess against the task sees what the
             * guess was made of. */
            if (cs[i].source != NULL && cs[i].source[0] != '\0') {
                jc_sb_append(sb, " (read from: \"");
                jc_sb_append(sb, cs[i].source);
                jc_sb_append(sb, "\")");
            }
            jc_sb_append(sb, "\n");
        }
        jc_sb_append(sb,
            "jichi read these from the wording of the request, and a reading of "
            "wording can mistake a description for an order. Follow them unless "
            "the task itself needs otherwise; if you act against one, say so in "
            "your final answer.\n");
    }
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

/* Append `s` at buf[*len] when it fits with its terminator; 0 when it does not. */
static int join_put(char *buf, jc_size cap, jc_size *len, const char *s)
{
    jc_size sl = (jc_size)strlen(s);
    if (*len + sl + 1 > cap) return 0;
    memcpy(buf + *len, s, sl);
    *len += sl;
    buf[*len] = '\0';
    return 1;
}

void jc_constraint_join_sourced(const struct jc_constraint *cs, int from, int n,
                                char *buf, jc_size cap)
{
    jc_size len = 0;
    const char *sep = "";
    int i;
    if (buf == NULL || cap == 0) return;
    buf[0] = '\0';
    if (cs == NULL) return;
    if (from < 0) from = 0;
    for (i = from; i < n; i++) {
        const char *t = cs[i].text;
        const char *src = cs[i].source;
        jc_size mark = len;
        int ends_run;
        if (t == NULL || t[0] == '\0') continue;
        if (src != NULL && src[0] == '\0') src = NULL;
        ends_run = src != NULL &&
                   (i + 1 >= n || cs[i + 1].source == NULL ||
                    strcmp(cs[i + 1].source, src) != 0);
        if (!join_put(buf, cap, &len, sep) || !join_put(buf, cap, &len, t) ||
            (ends_run && (!join_put(buf, cap, &len, " (from \"") ||
                          !join_put(buf, cap, &len, src) ||
                          !join_put(buf, cap, &len, "\")")))) {
            /* Out of room: keep the rules that fit whole, and say some did
             * not -- as join_text does, never a rule cut mid-quote. */
            static const char more[] = ", ...";
            jc_size room;
            jc_size mn = sizeof(more) - 1;
            len = mark;
            room = cap - 1 - len;
            if (mn > room) mn = room;
            memcpy(buf + len, more, mn);
            buf[len + mn] = '\0';
            return;
        }
        sep = ends_run ? "; " : ", ";
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
