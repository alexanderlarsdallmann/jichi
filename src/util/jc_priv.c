/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_priv.c - privileged-launcher detection (see jc_priv.h). */

#include "jc_priv.h"
#include "jc_platform.h"

#include <string.h>

static int pv_lc(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int pv_space(int c)
{
    return c == ' ' || c == '\t';
}

const char *jc_priv_kind_name(enum jc_priv_kind k)
{
    switch (k) {
    case JC_PRIV_SUDO:   return "sudo";
    case JC_PRIV_DOAS:   return "doas";
    case JC_PRIV_PKEXEC: return "pkexec";
    case JC_PRIV_SU:     return "su";
    case JC_PRIV_RUN0:   return "run0";
    default:             return "";
    }
}

/* A launcher bareword -> kind, or NONE. Compares the [p, end) slice exactly
 * (case-insensitively) against the launcher set. */
static enum jc_priv_kind launcher_kind(const char *p, jc_size n)
{
    struct { const char *name; enum jc_priv_kind kind; } tab[] = {
        { "sudo", JC_PRIV_SUDO }, { "sudoedit", JC_PRIV_SUDO },
        { "doas", JC_PRIV_DOAS }, { "pkexec", JC_PRIV_PKEXEC },
        { "su", JC_PRIV_SU },     { "run0", JC_PRIV_RUN0 }
    };
    unsigned int i;
    jc_size k;
    jc_size last = 0;
    int found = 0;
    /* B-05: a launcher named by path is matched by its FINAL path component,
     * so `/usr/bin/sudo` is the `sudo` launcher. A bare word (no `/`) is
     * compared exactly as before. */
    for (k = 0; k < n; k++) {
        if (p[k] == '/') { last = k + 1; found = 1; }
    }
    if (found) {
        p += last;
        n -= last;
    }
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        jc_size ln = (jc_size)strlen(tab[i].name);
        jc_size j;
        if (ln != n) continue;
        for (j = 0; j < n; j++) {
            if (pv_lc((unsigned char)p[j]) != tab[i].name[j]) break;
        }
        if (j == n) return tab[i].kind;
    }
    return JC_PRIV_NONE;
}

/* Transparent wrappers whose argument is itself a command to run, so a
 * launcher after one is still segment-leading (`nohup sudo x`). The set that
 * may carry its own options (or, for `timeout`, a leading duration) before the
 * command is `nice`, `ionice`, `stdbuf`, `timeout` -- see
 * skip_wrapper_args. */
static int is_wrapper(const char *p, jc_size n)
{
    static const char *const w[] = {
        "nohup", "command", "exec", "time", "timeout", "nice", "ionice",
        "stdbuf", "setsid", "nohup", "builtin", 0
    };
    int i;
    for (i = 0; w[i] != NULL; i++) {
        jc_size ln = (jc_size)strlen(w[i]);
        jc_size j;
        if (ln != n) continue;
        for (j = 0; j < n; j++) {
            if (pv_lc((unsigned char)p[j]) != w[i][j]) break;
        }
        if (j == n) return 1;
    }
    return 0;
}

/* Does the bareword at p look like a VAR=value assignment? (an identifier
 * followed by '=' before any space) */
static int is_assignment(const char *p)
{
    jc_size i = 0;
    if (!((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z') ||
          p[0] == '_')) {
        return 0;
    }
    while (p[i] != '\0' && !pv_space((unsigned char)p[i])) {
        if (p[i] == '=') return 1;
        if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') ||
              (p[i] >= '0' && p[i] <= '9') || p[i] == '_')) {
            return 0; /* not an identifier char before '=' */
        }
        i++;
    }
    return 0;
}

/* Advance past the bareword at *pp and any following whitespace. */
static void skip_token_and_ws(const char **pp)
{
    const char *p = *pp;
    while (*p != '\0' && !pv_space((unsigned char)*p) &&
           *p != '&' && *p != '|' && *p != ';' && *p != '\n' &&
           *p != '(' && *p != ')' && *p != '{' && *p != '}' && *p != '`') {
        p++;
    }
    while (pv_space((unsigned char)*p)) p++;
    *pp = p;
}

/* Is a bare single-letter dash option the kind that takes a SEPARATE value
 * token (so `nice -n 5`, `ionice -c 2`, `stdbuf -o 8192` need the following
 * token consumed too)? The value-taking letters for these wrappers are n (nice,
 * ionice), c (ionice) and o/i/e (stdbuf); each appears only as a value-taking
 * option on its wrapper, so matching them is safe. `nice -n5` (value attached,
 * length 3) is not a separate-value option. */
static int wrapper_opt_takes_sep_value(const char *op, jc_size on)
{
    int c;
    if (on != 2 || op[0] != '-') return 0;
    c = pv_lc((unsigned char)op[1]);
    return c == 'n' || c == 'c' || c == 'o' || c == 'i' || c == 'e';
}

/* B-05: after a wrapper word, skip the wrapper's OWN argument tokens -- its
 * options (`-n`, `-c`, `-o`, ...) and the value tokens they carry, or (for
 * `timeout`) the leading duration -- the way the `env` branch already skips
 * its arguments, so a launcher that follows a wrapper option is still
 * resolved. `*pp` points just past the wrapper word; on return it points at
 * the command word (a launcher/wrapper/assignment), or at the end. A token
 * that launcher_kind or is_wrapper would match is never consumed: the loop
 * stops on it and lets the caller re-resolve. Only option/value tokens are
 * skipped -- a plain wrapper's command word (`time sleep ...`) is left for the
 * caller. Bounded by the caller's guard. */
static void skip_wrapper_args(const char **pp, int *guard, int is_timeout)
{
    const char *p = *pp;
    int duration_taken = 0;
    for (;;) {
        const char *op;
        jc_size on = 0;
        while (pv_space((unsigned char)*p)) p++;
        if (*p == '\0') break;
        if (++(*guard) > 64) break; /* pathological input */
        op = p;
        while (op[on] != '\0' && !pv_space((unsigned char)op[on]) &&
               op[on] != '&' && op[on] != '|' && op[on] != ';' &&
               op[on] != '\n' && op[on] != '(' && op[on] != ')' &&
               op[on] != '{' && op[on] != '}' && op[on] != '`') {
            on++;
        }
        if (on == 0) break;
        /* A command word (assignment, launcher, or wrapper) ends the option
         * run; leave it for the caller to resolve. */
        if (is_assignment(op) || launcher_kind(op, on) != JC_PRIV_NONE ||
            is_wrapper(op, on)) {
            break;
        }
        if (op[0] == '-') {
            /* The wrapper's own option: skip it, and if it takes a separate
             * value, skip that value too (unless the value is a command word). */
            skip_token_and_ws(&p);
            if (wrapper_opt_takes_sep_value(op, on)) {
                const char *v;
                jc_size vn = 0;
                while (pv_space((unsigned char)*p)) p++;
                if (*p != '\0' && ++(*guard) <= 64) {
                    v = p;
                    while (v[vn] != '\0' && !pv_space((unsigned char)v[vn]) &&
                           v[vn] != '&' && v[vn] != '|' && v[vn] != ';' &&
                           v[vn] != '\n' && v[vn] != '(' && v[vn] != ')' &&
                           v[vn] != '{' && v[vn] != '}' && v[vn] != '`') {
                        vn++;
                    }
                    if (vn != 0 &&
                        !(is_assignment(v) ||
                          launcher_kind(v, vn) != JC_PRIV_NONE ||
                          is_wrapper(v, vn))) {
                        skip_token_and_ws(&p);
                    }
                }
            }
            continue;
        }
        /* A non-dash token. For `timeout`, the FIRST one is the duration:
         * consume it, then stop so the next token (the command) is resolved.
         * For any other wrapper this IS the command: stop. */
        if (is_timeout && !duration_taken) {
            duration_taken = 1;
            skip_token_and_ws(&p);
        }
        break;
    }
    *pp = p;
}

/* M1091-b (SV:2258): when the wrapper is `command`, does the IMMEDIATELY
 * following argument make it a shell NAME LOOKUP? `command -v NAME` (and `-V`)
 * prints where NAME would resolve; it does NOT run NAME, so nothing privileged
 * happens and the detector must return NONE -- not skip `-v` and flag NAME.
 * Only the bare two-char `-v`/`-V` token qualifies (case-insensitive); a
 * combined option like `-pv` or a different letter does not make `command` a
 * lookup. `p` points at the first argument token after the wrapper word. */
static int command_lookup_flag(const char *p)
{
    char q;
    if (p[0] != '-') return 0;
    if (pv_lc((unsigned char)p[1]) != 'v') return 0;
    q = p[2];
    return q == '\0' || q == ' ' || q == '\t' || q == '&' || q == '|' ||
           q == ';' || q == '\n' || q == '(' || q == ')' || q == '{' ||
           q == '}' || q == '`';
}

/* At a segment start, resolve the leading command word, skipping env
 * assignments, an `env ...` prefix, and transparent wrappers. Returns the
 * launcher kind if that word is a privilege launcher, else NONE. */
static enum jc_priv_kind check_segment(const char *p, const char **out_tok)
{
    int guard = 0;
    while (pv_space((unsigned char)*p)) p++;
    for (;;) {
        const char *tok;
        jc_size n;
        if (*p == '\0') return JC_PRIV_NONE;
        if (++guard > 64) return JC_PRIV_NONE; /* pathological input */

        /* VAR=value assignments before the command. */
        if (is_assignment(p)) {
            skip_token_and_ws(&p);
            continue;
        }
        /* Measure the bareword. */
        tok = p;
        n = 0;
        while (tok[n] != '\0' && !pv_space((unsigned char)tok[n]) &&
               tok[n] != '&' && tok[n] != '|' && tok[n] != ';' &&
               tok[n] != '\n' && tok[n] != '(' && tok[n] != ')' &&
               tok[n] != '{' && tok[n] != '}' && tok[n] != '`') {
            n++;
        }
        if (n == 0) return JC_PRIV_NONE;

        /* `env` prefix: skip env and its VAR=val / -opt args, then re-resolve. */
        if (n == 3 && pv_lc((unsigned char)tok[0]) == 'e' &&
            pv_lc((unsigned char)tok[1]) == 'n' &&
            pv_lc((unsigned char)tok[2]) == 'v') {
            skip_token_and_ws(&p);
            while (*p != '\0') {
                while (pv_space((unsigned char)*p)) p++;
                if (is_assignment(p) || *p == '-') {
                    skip_token_and_ws(&p);
                    continue;
                }
                break;
            }
            continue;
        }
        if (is_wrapper(tok, n)) {
            /* Skip the wrapper word, then its own option/value tokens. For
             * `timeout` the first positional token is the duration. */
            int is_command = (n == 7 && pv_lc((unsigned char)tok[0]) == 'c' &&
                              pv_lc((unsigned char)tok[1]) == 'o' &&
                              pv_lc((unsigned char)tok[2]) == 'm' &&
                              pv_lc((unsigned char)tok[3]) == 'm' &&
                              pv_lc((unsigned char)tok[4]) == 'a' &&
                              pv_lc((unsigned char)tok[5]) == 'n' &&
                              pv_lc((unsigned char)tok[6]) == 'd');
            int is_timeout = (n == 7 && pv_lc((unsigned char)tok[0]) == 't' &&
                              pv_lc((unsigned char)tok[1]) == 'i' &&
                              pv_lc((unsigned char)tok[2]) == 'm' &&
                              pv_lc((unsigned char)tok[3]) == 'e' &&
                              pv_lc((unsigned char)tok[4]) == 'o' &&
                              pv_lc((unsigned char)tok[5]) == 'u' &&
                              pv_lc((unsigned char)tok[6]) == 't');
            skip_token_and_ws(&p);
            /* M1091-b (SV:2258): `command -v NAME` / `command -V NAME` is a
             * name LOOKUP, not a launch -- return NONE for the segment instead
             * of skipping `-v` and flagging NAME. No other wrapper gets this
             * treatment. (`builtin command -v NAME` reaches the same check on
             * its `command` pass.) */
            if (is_command && command_lookup_flag(p)) {
                return JC_PRIV_NONE;
            }
            skip_wrapper_args(&p, &guard, is_timeout);
            continue;
        }
        {
            enum jc_priv_kind k = launcher_kind(tok, n);
            if (k != JC_PRIV_NONE && out_tok != NULL) {
                *out_tok = tok;
            }
            return k;
        }
    }
}

/* Does the command contain an unquoted chaining operator (so a prefix
 * allowlist could be evaded by appending `; sudo rm -rf`)? */
static int has_unquoted_chain(const char *s)
{
    int in_sq = 0, in_dq = 0;
    const char *p;
    for (p = s; *p != '\0'; p++) {
        char c = *p;
        if (in_sq) { if (c == '\'') in_sq = 0; continue; }
        if (in_dq) {
            if (c == '\\' && p[1] != '\0') { p++; continue; }
            if (c == '"') in_dq = 0;
            continue;
        }
        if (c == '\'') { in_sq = 1; continue; }
        if (c == '"') { in_dq = 1; continue; }
        if (c == ';' || c == '&' || c == '|' || c == '\n' || c == '`') {
            return 1;
        }
        if (c == '$' && p[1] == '(') return 1;
    }
    return 0;
}

int jc_priv_allowlisted(const char *command, const char *const *entries, int n)
{
    const char *c;
    int i;

    if (command == NULL || entries == NULL || n <= 0) {
        return 0;
    }
    c = command;
    while (*c == ' ' || *c == '\t') c++;
    /* A chained command can never be safely matched by a single prefix. */
    if (has_unquoted_chain(c)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        const char *e = entries[i];
        jc_size el;
        if (e == NULL) continue;
        while (*e == ' ' || *e == '\t') e++;
        el = (jc_size)strlen(e);
        while (el > 0 && (e[el - 1] == ' ' || e[el - 1] == '\t')) el--;
        if (el == 0) continue;
        if (strncmp(c, e, el) == 0 &&
            (c[el] == '\0' || c[el] == ' ' || c[el] == '\t')) {
            return 1;
        }
    }
    return 0;
}

enum jc_priv_kind jc_priv_detect(const char *command, const char **out_tok)
{
    const char *p;
    int in_sq = 0;
    int in_dq = 0;
    int seg_start = 1;

    if (out_tok != NULL) {
        *out_tok = NULL;
    }
    if (command == NULL) {
        return JC_PRIV_NONE;
    }
    for (p = command; *p != '\0'; p++) {
        char c = *p;
        if (in_sq) {
            if (c == '\'') in_sq = 0;
            continue;
        }
        if (in_dq) {
            if (c == '\\' && p[1] != '\0') { p++; continue; }
            if (c == '"') in_dq = 0;
            continue;
        }
        if (c == '\'') { in_sq = 1; seg_start = 0; continue; }
        if (c == '"') { in_dq = 1; seg_start = 0; continue; }

        /* Segment openers: operators, subshell/group/substitution starts. */
        if (c == '&' || c == '|' || c == ';' || c == '\n' ||
            c == '(' || c == '{' || c == '`') {
            seg_start = 1;
            continue;
        }
        if (c == '$' && p[1] == '(') {
            seg_start = 1;
            p++; /* skip '(' too */
            continue;
        }
        if (pv_space((unsigned char)c)) {
            continue;
        }
        if (seg_start) {
            enum jc_priv_kind k = check_segment(p, out_tok);
            if (k != JC_PRIV_NONE) {
                return k;
            }
            seg_start = 0;
        }
    }
    return JC_PRIV_NONE;
}

/* M821 (N9): does the NUL-separated environment block `block` (len bytes, as read
 * from /proc/<pid>/environ) contain an entry whose NAME -- the text before the
 * first '=' of an entry -- is exactly `name`? Compares the NAME only, never the
 * value: doctor uses this to report that a PID-1 init in a container holds the
 * key variable, without ever touching the secret. Pure. */
int jc_env_block_has_name(const char *block, jc_size len, const char *name)
{
    jc_size i = 0;
    jc_size nlen;

    if (block == NULL || name == NULL || name[0] == '\0' || len == 0) {
        return 0;
    }
    nlen = (jc_size)strlen(name);
    while (i < len) {
        jc_size start = i;
        jc_size eq = len;   /* len => no '=' in this entry */
        while (i < len && block[i] != '\0') {
            if (eq == len && block[i] == '=') {
                eq = i;
            }
            i++;
        }
        /* [start, i) is one entry; its NAME is [start, eq). */
        if (eq != len && (eq - start) == nlen &&
            memcmp(block + start, name, nlen) == 0) {
            return 1;
        }
        i++;   /* step over the NUL separator */
    }
    return 0;
}
