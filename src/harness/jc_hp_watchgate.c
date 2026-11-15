/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_watchgate.c - the `watch gate` subcommand body (M1153, batch H9a,
 * UC3 part 1): ONE tick of the gate watcher, the C89 port of the 2026-10-27
 * hardening-host drive's gate-watch.sh (read line by line: :28 say, :29
 * verdict, :31-44 READY/state, :53-79 withdrawal, :89-98 master checks,
 * :102-109 busy/smoke waits, :110-114 dry/start).
 *
 * A tick:
 *   1. reads the LAST line of the drive's LOG of the form
 *      `BATCH <n> READY at <commit>` (a file read, no git in team/);
 *   2. does nothing if that commit is on master already, has a standing
 *      verdict in the state file, or a later LOG SENTENCE says WITHDRAWN and
 *      names it and names no other commit (the 13:47 rule, behaviour 7);
 *   3. waits (exit 0) until the commit is in push.git AND on the remote's
 *      team branch (the push timer has carried it through the key scan);
 *   4. refuses (exit 1) if it cannot name origin/master;
 *   5. waits while another gate or ci SERVICE named by a commit runs (DD5:
 *      BOTH drives' prefixes) or a team smoke runs in team/;
 *   6. starts the transient unit jhp-gate-<sha8> running the config's gateRun.
 *
 * The three host halves the container cannot run (systemctl is-active,
 * list-units, systemd-run) and pgrep are SHIMMABLE: the config's host block
 * (host.unitState / host.unitList / host.unitRun) or a --unit-state /
 * --unit-list / --unit-run / --pgrep flag names them, each falling back to
 * the reference's argv (the pageCommand idiom). The gate verdicts are
 * delivered through host.notifyCommand with the verdict TEXT as the argument
 * (the CHANGED host half; a failure is logged, never fatal).
 *
 * The withdrawal engine (behaviour 7) is a PURE function over a line buffer
 * (jc_gate_withdrawn): no I/O, unit-tested by tests/test_watchgate.c as its
 * own red-first table (the a-f / date / decimal / A-shape / J-shape / genuine
 * sole withdrawal cases).
 *
 * rc (behaviour 16): 0 for wait / silence / withdrawn / on-master / not-ff /
 * busy / smoke / no-READY; 1 for no-origin-master / no-gateRun / unit-run
 * failed; 2 for usage (no host block). A tick that waits is a SUCCESS: the
 * next tick is the retry (the reference's every wait-arm is `exit 0`; the
 * 10-minute cadence IS the polling loop, and there is no in-tick sleep). */

#include "jc_harness_config.h"
#include "jc_harness_push.h"   /* jc_hp_git_run (the shared git child, M1056) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>    /* pid_t's home (portability_lint 29, M723) */

#include "jc_platform.h"  /* jc_status, jc_size */
#include "jc_mem.h"       /* struct jc_arena, jc_arena_strdup, jc_arena_alloc */
#include "jc_str.h"       /* struct jc_sb, jc_sb_init / _free (capture out) */
#include "jc_vec.h"       /* struct jc_vec (the neutral git env) */
#include "jc_snprintf.h"  /* jc_snprintf, never sprintf */
#include "jc_proc.h"      /* jc_proc_capture, jc_proc_waitpid */
#include "jc_spawn.h"     /* jc_spawn, jc_spawn_req, JC_SPAWN_DEVNULL */
#include "jc_fcntl.h"     /* JC_O_APPEND (portability_lint 38) */
#include "jc_hp_watchgate.h" /* the pure withdrawal engine's prototypes */

/* Paths hang off the config's dir and the host pushHome; give the joins room,
 * the same bound jc_harness_push.c / jc_hp_units.c's PATH_CAP uses. */
#define WGATE_PATH_CAP 4352
/* The cap on a shimmed command's captured output (the unit list, pgrep's
 * pids). It is NOT a cap on the LOG or the state file: those are read whole
 * (M1178 -- a LOG past this size read as absent, so its READY was never
 * gated, and the live drive's LOG had reached 474,487 B). */
#define WGATE_LOG_CAP  (1024 * 1024)
/* A commit is 7-40 hex; the sha8 unit names and the state words are short. */
#define WGATE_SHA_CAP  64
/* The busy-set say line (space-joined .service names) and a shimmable
 * command string: keep room. */
#define WGATE_BUSY_CAP 4096
#define WGATE_CMD_CAP  4096
/* The argv slots a shimmable command + its appended args may use. */
#define WGATE_MAX_ARGV 40

/* --- the pure withdrawal engine (behaviour 7, unit-tested by
 * tests/test_watchgate.c through jc_hp_watchgate.h) ------------------------ */

/* A lowercase hex character (the commit-token alphabet). */
static int
wgate_is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/* An alphanumeric character (a commit token's boundary is NOT one). */
static int
wgate_is_alnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
}

/* Does the run s[0,n) contain at least one letter a-f? (the test that keeps a
 * pure-digit run like the date 20261105 from being a commit token). */
static int
wgate_run_has_abcdef(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (s[i] >= 'a' && s[i] <= 'f') {
            return 1;
        }
    }
    return 0;
}

/* Does the SENTENCE `t` WITHDRAW the READY commit c7 (the commit's first
 * seven hex)? 1 iff all three hold (behaviour 7, the 13:47 rule):
 *   (a) it contains WITHDRAWN (case-sensitive substring);
 *   (b) it names the READY commit: some commit token whose first seven hex
 *       equal c7 (the reference's `substr(x,1,7) == c`, any length);
 *   (c) it names NO OTHER commit token: no other token of 7-40 hex that
 *       contains at least one letter a-f (the reference's `length(x) <= 40
 *       && x ~ /[a-f]/`). A token of 41+ hex is no commit at all (not
 *       "other"). Pure: no I/O. A "commit token" is a maximal run of 7-or-
 *       more lowercase hex bounded by non-alphanumerics (the sentence is
 *       padded with a space on each side so a token at either edge still has
 *       its boundary -- the awk `u = " " t " "` idiom). */
int
jc_gate_withdrawn_sentence(const char *t, const char *c7)
{
    size_t tl;
    char u[65536];
    size_t ul;
    size_t i;
    int self = 0;
    int other = 0;

    if (t == NULL || c7 == NULL) {
        return 0;
    }
    /* (a) WITHDRAWN, case-sensitive (the awk index(t, "WITHDRAWN")). */
    if (strstr(t, "WITHDRAWN") == NULL) {
        return 0;
    }
    tl = strlen(t);
    if (tl + 2 >= sizeof u) {
        return 0;   /* a pathologically long sentence: not a withdrawal */
    }
    u[0] = ' ';
    memcpy(u + 1, t, tl);
    u[tl + 1] = ' ';
    u[tl + 2] = '\0';
    ul = tl + 2;

    for (i = 1; i < ul - 1; i++) {
        if (!wgate_is_hex(u[i])) {
            continue;
        }
        {
            size_t start = i;
            size_t end = i;
            while (end < ul - 1 && wgate_is_hex(u[end])) {
                end++;
            }
            {
                /* u[start-1] and u[end] are the run's boundaries. A token is
                 * a 7+-hex run bounded by non-alphanumerics on BOTH sides. */
                size_t len = end - start;
                if (!wgate_is_alnum(u[start - 1]) &&
                    !wgate_is_alnum(u[end]) && len >= 7) {
                    if (memcmp(u + start, c7, 7) == 0) {
                        self = 1;                 /* names the READY commit */
                    } else if (len <= 40 &&
                               wgate_run_has_abcdef(u + start, len)) {
                        other = 1;                /* names another commit */
                    }
                }
            }
            i = end - 1;   /* the loop's ++i advances past the run */
        }
    }
    /* An ambiguous sentence does not withdraw: the cost of a missed
     * withdrawal is one gate run the lead can still replace; the cost of a
     * false one is a drive that waits, silently, for a verdict that never
     * comes. */
    return (self && !other) ? 1 : 0;
}

/* Split `t` into sentences on the reference's awk delimiter [.!?]+[ \t]+
 * (one-or-more of . ! ? followed by one-or-more space/tab) and apply
 * jc_gate_withdrawn_sentence to each part (the awk `split` parts). A line
 * with no such boundary is one sentence (the last part). Pure. */
int
jc_gate_withdrawn(const char *t, const char *c7)
{
    if (t == NULL || c7 == NULL) {
        return 0;
    }
    return jc_gate_withdrawn_n(t, strlen(t), c7);
}

/* jc_gate_withdrawn_n: see the header (M1178). The body reads t[0, n) only. */
int
jc_gate_withdrawn_n(const char *t, size_t n, const char *c7)
{
    size_t tl;
    size_t prev;
    size_t i;

    if (t == NULL || c7 == NULL) {
        return 0;
    }
    tl = n;
    if (tl >= 65536) {
        return 0;   /* a pathologically long line: not a withdrawal */
    }
    prev = 0;
    i = 0;
    while (i < tl) {
        if (t[i] == '.' || t[i] == '!' || t[i] == '?') {
            size_t k = i;
            while (k < tl && (t[k] == '.' || t[k] == '!' || t[k] == '?')) {
                k++;
            }
            if (k < tl && (t[k] == ' ' || t[k] == '\t')) {
                size_t w = k;
                size_t slen = i - prev;
                char part[65536];
                if (slen > 0 && slen < sizeof part) {
                    memcpy(part, t + prev, slen);
                    part[slen] = '\0';
                    if (jc_gate_withdrawn_sentence(part, c7)) {
                        return 1;
                    }
                }
                while (w < tl && (t[w] == ' ' || t[w] == '\t')) {
                    w++;
                }
                prev = w;
                i = w;
                continue;
            }
        }
        i++;
    }
    /* The trailing part (after the last boundary), if any. */
    if (tl > prev) {
        size_t slen = tl - prev;
        char part[65536];
        if (slen < sizeof part) {
            memcpy(part, t + prev, slen);
            part[slen] = '\0';
            if (jc_gate_withdrawn_sentence(part, c7)) {
                return 1;
            }
        }
    }
    return 0;
}

/* --- the busy filter (behaviour 11, DD5) -------------------------------- */

/* Is `unit` a gate or ci SERVICE named by a commit? (the 01:58 self-match's
 * teeth: the watcher's own jhp-gate-watch.service/.timer is NOT one --
 * counting it made every READY wait forever). DD5: BOTH drives' prefixes
 * (jhd- and jhp-). Matches ^j(hd|hp)-(gate|ci)-[0-9a-f]{7,40}\.service$ */
static int
wgate_is_busy_unit(const char *unit)
{
    static const char kinds[2][5] = { "gate", "ci" };
    size_t k;

    if (unit == NULL) {
        return 0;
    }
    for (k = 0; k < 2; k++) {
        const char *s = unit;
        const char *kind = kinds[k];
        size_t klen = strlen(kind);
        size_t hp;   /* the hex run's start */
        size_t he;   /* the hex run's end */
        if (strncmp(s, "jhd-", 4) != 0 && strncmp(s, "jhp-", 4) != 0) {
            continue;
        }
        /* The kind is compared by ITS OWN length: "gate" is 4, "ci" is 2 --
         * a fixed 4-char compare (the first cut) matched "gate" but never
         * "ci", so a jhp-ci-<sha>.service gate was read as NOT busy. */
        if (strncmp(s + 4, kind, klen) != 0 || s[4 + klen] != '-') {
            continue;
        }
        hp = 4 + klen + 1;
        he = hp;
        while (wgate_is_hex(s[he])) {
            he++;
        }
        if (he - hp < 7 || he - hp > 40) {
            continue;
        }
        if (strcmp(s + he, ".service") == 0) {
            return 1;
        }
    }
    return 0;
}

/* --- small I/O helpers -------------------------------------------------- */

/* timegm (glibc's UTC mktime), but the build's _POSIX_C_SOURCE does not
 * expose it, so this hand-rolls it -- the SAME proleptic-Gregorian
 * days-from-civil algorithm notify_ts / jc_hp_tick.c use. */
static time_t
wgate_utc_from_tm(const struct tm *tmv)
{
    long y = tmv->tm_year + 1900;
    long m = tmv->tm_mon + 1;
    long d = tmv->tm_mday;
    long era, yoe, doy, doe, days;
    y -= (m <= 2) ? 1 : 0;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = era * 146097 + doe - 719468;
    return (time_t)(days * 86400L + tmv->tm_hour * 3600L
                    + tmv->tm_min * 60L + tmv->tm_sec);
}

/* The reference's timestamp shape: `date '+%F %T %z'` (gate-watch.sh :28),
 * e.g. `2026-11-05 10:23:45 +0100`. `%F %T %z` are GNU strftime specifiers
 * this C89 build rejects, so the same bytes are assembled from the ISO
 * `%Y-%m-%d %H:%M:%S` plus the LOCAL offset (the notify_ts idiom). A clock
 * that cannot format is not a reason to stop the tick: "unknown", never
 * crash. The driver greps the say lines by the text, not the stamp. */
static void
wgate_ts(char *out, size_t cap)
{
    time_t t = time(NULL);
    struct tm tmv;
    struct tm tmc;
    long off;
    long mag;
    int oh, om;
    size_t n;

    {
        const struct tm *lt = localtime(&t);
        if (lt == NULL) {
            memset(&tmv, 0, sizeof tmv);
            tmv.tm_year = 70; tmv.tm_mon = 0; tmv.tm_mday = 1;
            tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
            off = 0;
        } else {
            tmv = *lt;
            tmc = tmv;
            off = (long)wgate_utc_from_tm(&tmc) - (long)t;
        }
    }
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
    if (n == 0) {
        jc_snprintf(out, cap, "unknown");
        return;
    }
    mag = (off < 0) ? -off : off;
    oh = (int)(mag / 3600);
    om = (int)((mag % 3600) / 60);
    if (oh < 100 && cap > n + 7) {
        out[n] = ' ';
        out[n + 1] = (off < 0) ? '-' : '+';
        out[n + 2] = (char)('0' + oh / 10);
        out[n + 3] = (char)('0' + oh % 10);
        out[n + 4] = (char)('0' + om / 10);
        out[n + 5] = (char)('0' + om % 10);
        out[n + 6] = '\0';
    }
}

/* The tick's `say` (behaviour 14, the reference's :28 CHANGED): append
 * `<stamp> <msg>\n` to the --out file AND print `<msg>\n` to stdout (the
 * port prints every say always -- a tick's stdout is the journal of the
 * oneshot; the file append is the reference's invariant half). A --out that
 * cannot be written is logged to stderr, never fatal to the tick. */
static void
wgate_say(const char *out_path, const char *msg)
{
    char stamped[WGATE_PATH_CAP];
    char stamp[64];
    FILE *f;

    wgate_ts(stamp, sizeof stamp);
    if (jc_snprintf(stamped, sizeof stamped, "%s %s\n", stamp, msg) < 0 ||
        (size_t)jc_snprintf(stamped, sizeof stamped, "%s %s\n", stamp, msg)
            >= sizeof stamped) {
        /* the message is too long to stamp: the plain line only */
        fputs(msg, stdout);
        fputc('\n', stdout);
    } else {
        if (out_path != NULL) {
            f = fopen(out_path, "ab");
            if (f == NULL) {
                fprintf(stderr, "jichi-harness: watch: could not append to "
                        "the out file %s (no such directory?); the say is "
                        "stdout-only\n", out_path);
            } else {
                fputs(stamped, f);
                if (ferror(f)) {
                    fprintf(stderr, "jichi-harness: watch: could not write "
                            "to the out file %s\n", out_path);
                }
                fclose(f);
            }
        }
        fputs(msg, stdout);
        fputc('\n', stdout);
    }
    fflush(stdout);
}

/* The tick's `verdict` (behaviour 15, the reference's :29): append
 * `<sha8> <word> <stamp>\n` to the --state file. The file is NEVER
 * truncated. A write that cannot happen is logged to stderr, never fatal. */
static void
wgate_verdict(const char *state_path, const char *sha8, const char *word)
{
    char line[WGATE_SHA_CAP + 96];
    char stamp[64];
    FILE *f;

    wgate_ts(stamp, sizeof stamp);
    if (jc_snprintf(line, sizeof line, "%s %s %s\n", sha8, word, stamp) < 0) {
        return;
    }
    if (state_path != NULL) {
        f = fopen(state_path, "ab");
        if (f == NULL) {
            fprintf(stderr, "jichi-harness: watch: could not append to the "
                    "state file %s (no such directory?); the verdict word is "
                    "not written\n", state_path);
            return;
        }
        fputs(line, f);
        if (ferror(f)) {
            fprintf(stderr, "jichi-harness: watch: could not write to the "
                    "state file %s\n", state_path);
        }
        fclose(f);
    }
}

/* The basename of a path (the reference's $(basename "$f")); on a
 * NULL/empty input, "". */
static const char *
wgate_basename(const char *p)
{
    const char *slash;
    if (p == NULL || p[0] == '\0') {
        return "";
    }
    slash = strrchr(p, '/');
    return (slash != NULL) ? slash + 1 : p;
}

/* Tokenize a shimmable command string into argv (a mutable buffer the
 * caller owns, capped). The first token is argv[0] (PATH-searched by the
 * spawn). Returns the number of tokens; an empty cmd returns 0 (argv[0] is
 * NUL). */
/* Tokenize a shimmable command string into argv. The tokens must SURVIVE
 * every later split (the four shims are split up front and used at the end of
 * the tick), so they are copied into the ARENA `a`, not into a local buffer a
 * subsequent strtok would reuse. The first token is argv[0] (PATH-searched by
 * the spawn). Returns the number of tokens; an empty cmd returns 0. */
static int
wgate_split_cmd(struct jc_arena *a, char **argv, char *cmd)
{
    char *tok;
    char *save;
    int n = 0;

    argv[0] = NULL;
    if (cmd == NULL || cmd[0] == '\0') {
        return 0;
    }
    if ((size_t)strlen(cmd) >= WGATE_CMD_CAP) {
        return 0;
    }
    tok = strtok_r(cmd, " \t", &save);
    while (tok != NULL && n < WGATE_MAX_ARGV - 1) {
        argv[n++] = jc_arena_strdup(a, tok);
        tok = strtok_r(NULL, " \t", &save);
    }
    argv[n] = NULL;
    return n;
}

/* Read a WHOLE file into an arena-owned NUL-terminated buffer. Returns the
 * buffer, or NULL when the file is absent or unreadable, or the arena cannot
 * hold it (a value, not a crash). No size cap (M1178): a LOG is append-only
 * and only grows, so any cap is a date on which the watcher goes quiet. */
static const char *
wgate_read_file(struct jc_arena *a, const char *path)
{
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    size_t rd;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)jc_arena_alloc(a, (jc_size)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* The LAST state word for `sha8`: the last line of the state file beginning
 * "<sha8> " (behaviour 3's read: grep "^<s8> " | tail -1 | awk '{print
 * $2}'), as an arena-owned copy; NULL when there is none or the file is
 * absent. */
static const char *
wgate_state_word(struct jc_arena *a, const char *state_path,
                 const char *sha8)
{
    const char *buf;
    char prefix[WGATE_SHA_CAP + 2];
    const char *p;
    const char *line_start;
    size_t plen;
    const char *best = NULL;

    if (state_path == NULL) {
        return NULL;
    }
    buf = wgate_read_file(a, state_path);
    if (buf == NULL) {
        return NULL;
    }
    plen = (size_t)jc_snprintf(prefix, sizeof prefix, "%s ", sha8);
    if (plen >= sizeof prefix) {
        return NULL;
    }
    p = buf;
    line_start = p;
    for (;;) {
        const char *nl;
        size_t ll = 0;
        nl = line_start;
        while (*nl != '\0' && *nl != '\n') {
            nl++;
            ll++;
        }
        if (ll >= plen && memcmp(line_start, prefix, plen) == 0) {
            const char *w = line_start + plen;
            size_t wl = 0;
            while (*w != '\0' && *w != ' ' && *w != '\t' && *w != '\n') {
                w++;
                wl++;
            }
            if (wl > 0 && wl < WGATE_SHA_CAP) {
                char word[WGATE_SHA_CAP];
                memcpy(word, line_start + plen, wl);
                word[wl] = '\0';
                best = jc_arena_strdup(a, word);
            }
        }
        if (*nl == '\0') {
            break;
        }
        line_start = nl + 1;
    }
    return best;
}

/* --- the shimmable-command spawns ---------------------------------------- */

/* The neutral git env (the backlog_git idiom) so a host git child cannot be
 * redirected by an ambient user config; a no-op for a systemctl / systemd-run
 * / pgrep child. */
static int
wgate_run_list(char *const argv_base[], int n_base, char *extra[],
               int n_extra, char *out, size_t outcap)
{
    char *argv[WGATE_MAX_ARGV];
    struct jc_vec envv;
    struct jc_sb cap;
    char *e_global = (char *)"GIT_CONFIG_GLOBAL=/dev/null";
    char *e_system = (char *)"GIT_CONFIG_SYSTEM=/dev/null";
    char *e_prompt = (char *)"GIT_TERMINAL_PROMPT=0";
    int k = 0;
    int i;
    int rc;

    if (argv_base == NULL || argv_base[0] == NULL) {
        if (out != NULL && outcap > 0) {
            out[0] = '\0';
        }
        return -1;
    }
    for (i = 0; i < n_base && k < WGATE_MAX_ARGV - 1; i++) {
        argv[k++] = argv_base[i];
    }
    if (extra != NULL) {
        for (i = 0; i < n_extra && k < WGATE_MAX_ARGV - 1; i++) {
            argv[k++] = extra[i];
        }
    }
    argv[k] = NULL;
    jc_vec_init(&envv, sizeof(char *));
    (void)jc_vec_push(&envv, &e_global);
    (void)jc_vec_push(&envv, &e_system);
    (void)jc_vec_push(&envv, &e_prompt);
    jc_sb_init(&cap);
    rc = jc_proc_capture(argv, &envv, NULL, &cap, (jc_size)WGATE_LOG_CAP,
                         120L, NULL);
    jc_vec_free(&envv);
    if (out != NULL && outcap > 0) {
        {
            size_t n = (cap.len < outcap - 1) ? cap.len : outcap - 1;
            if (cap.data != NULL && n > 0) {
                memcpy(out, cap.data, n);
            }
            out[n] = '\0';
        }
    }
    jc_sb_free(&cap);
    return rc;
}

/* Run a shimmable command and return only its exit status (output discarded).
 * -1 spawn, -2 timeout. */
static int
wgate_run_rc(char *const argv_base[], int n_base, char *extra[],
             int n_extra)
{
    return wgate_run_list(argv_base, n_base, extra, n_extra, NULL, 0);
}

/* Run a shimmable command with its stdout+stderr appended to `file` -- the
 * reference's `>> "$OUT" 2>&1` for the transient gate unit. Returns the
 * child's exit status (-1 spawn, -2 timeout). */
static int
wgate_run_to_file(char *const argv_base[], int n_base, char *extra[],
                  int n_extra, const char *file)
{
    char *argv[WGATE_MAX_ARGV];
    struct jc_spawn_req req;
    pid_t pid;
    int ofd;
    int k = 0;
    int i;
    int status;
    int rc;

    if (argv_base == NULL || argv_base[0] == NULL) {
        return -1;
    }
    for (i = 0; i < n_base && k < WGATE_MAX_ARGV - 1; i++) {
        argv[k++] = argv_base[i];
    }
    if (extra != NULL) {
        for (i = 0; i < n_extra && k < WGATE_MAX_ARGV - 1; i++) {
            argv[k++] = extra[i];
        }
    }
    argv[k] = NULL;
    ofd = open(file, O_WRONLY | O_CREAT | O_APPEND | JC_O_BINARY, 0644);
    if (ofd < 0) {
        fprintf(stderr, "jichi-harness: watch: could not open %s for the "
                        "gate unit's output; refused\n", file);
        return -1;
    }
    memset(&req, 0, sizeof req);
    req.argv = argv;
    req.fd_stdin = JC_SPAWN_DEVNULL;
    req.fd_stdout = ofd;
    req.fd_stderr = ofd;
    if (jc_spawn(&req, &pid) != JC_OK) {
        close(ofd);
        return -1;
    }
    close(ofd);
    rc = -1;
    if (jc_proc_waitpid(pid, &status) == 0) {
        if (WIFEXITED(status)) {
            rc = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            rc = 128 + WTERMSIG(status);
        }
    }
    return rc;
}

/* The READY-line parse (behaviour 1): `^\**BATCH <n> READY at [0-9a-f]{7,40}`
 * (the leading markdown bold is optional; <n> is one non-space token; the
 * commit is 7-40 lowercase hex). */
struct wgate_ready {
    int line_no;                      /* 1-based */
    char batch[WGATE_SHA_CAP];
    char commit[WGATE_SHA_CAP];       /* the hex from the line */
};

static int
wgate_parse_ready_line(const char *line, struct wgate_ready *out)
{
    const char *p = line;
    const char *bs;
    size_t bl;
    size_t hn;

    while (*p == '*') {
        p++;
    }
    if (strncmp(p, "BATCH ", 6) != 0) {
        return 0;
    }
    p += 6;
    bs = p;
    bl = 0;
    while (*p != '\0' && *p != ' ') {
        p++;
        bl++;
    }
    if (bl == 0 || bl >= sizeof out->batch) {
        return 0;
    }
    if (strncmp(p, " READY at ", 10) != 0) {
        return 0;
    }
    p += 10;
    hn = 0;
    while (wgate_is_hex(p[hn])) {
        hn++;
    }
    if (hn < 7 || hn > 40 || hn >= sizeof out->commit) {
        return 0;
    }
    memcpy(out->batch, bs, bl);
    out->batch[bl] = '\0';
    memcpy(out->commit, p, hn);
    out->commit[hn] = '\0';
    return 1;
}

/* Scan a LOG buffer for the LAST READY line; fill out (line_no 1-based). */
static int
wgate_find_last_ready(const char *logbuf, struct wgate_ready *out)
{
    const char *line_start = logbuf;
    int line_no = 0;
    int found = 0;

    for (;;) {
        const char *nl = line_start;
        size_t ll = 0;
        char line[WGATE_PATH_CAP];
        struct wgate_ready cand;
        while (*nl != '\0' && *nl != '\n') {
            nl++;
            ll++;
        }
        line_no++;
        if (ll > 0 && ll < sizeof line) {
            memcpy(line, line_start, ll);
            line[ll] = '\0';
            if (wgate_parse_ready_line(line, &cand)) {
                cand.line_no = line_no;
                *out = cand;
                found = 1;
            }
        }
        if (*nl == '\0') {
            break;
        }
        line_start = nl + 1;
    }
    return found;
}

/* git in push.git (the shared M1056 git child, neutral env): returns
 * jc_proc_capture's code; `out` receives combined stdout+stderr. */
static int
wgate_git(const char *push_repo, const char *args[], int n_args, char *out,
          size_t outcap)
{
    char outbuf[WGATE_PATH_CAP];
    int rc;

    rc = jc_hp_git_run(NULL, push_repo, NULL, 0, NULL, 0,
                       args, n_args,
                       (int)(sizeof outbuf), outbuf, NULL);
    if (out != NULL && outcap > 0) {
        size_t n = strlen(outbuf);
        if (n > outcap - 1) {
            n = outcap - 1;
        }
        memcpy(out, outbuf, n);
        out[n] = '\0';
    }
    return rc;
}

/* `git -C push.git rev-parse --verify -q <ref-or-sha>`: the full sha in out
 * (40 hex) when it resolves, "" when not (a value, not a crash). */
static int
wgate_git_full(const char *push_repo, const char *rev, char *out, size_t cap)
{
    const char *args[5];
    char rev_copy[WGATE_PATH_CAP];
    int rc;

    if ((size_t)jc_snprintf(rev_copy, sizeof rev_copy, "%s", rev)
            >= sizeof rev_copy) {
        out[0] = '\0';
        return -1;
    }
    args[0] = (char *)"rev-parse";
    args[1] = (char *)"--verify";
    args[2] = (char *)"-q";
    args[3] = rev_copy;
    args[4] = NULL;
    rc = wgate_git(push_repo, args, 4, out, cap);
    if (rc != 0 || out[0] == '\0') {
        out[0] = '\0';
        return 0;
    }
    /* trim the trailing newline */
    out[strcspn(out, "\r\n")] = '\0';
    return 1;
}

/* `git -C push.git merge-base --is-ancestor <a> <b>`: 1 when a is an
 * ancestor of b, 0 otherwise, -1 on a git error (a value). */
static int
wgate_git_is_ancestor(const char *push_repo, const char *a, const char *b)
{
    const char *args[5];
    char acopy[WGATE_PATH_CAP];
    char bcopy[WGATE_PATH_CAP];
    int rc;

    if ((size_t)jc_snprintf(acopy, sizeof acopy, "%s", a) >= sizeof acopy ||
        (size_t)jc_snprintf(bcopy, sizeof bcopy, "%s", b) >= sizeof bcopy) {
        return -1;
    }
    args[0] = (char *)"merge-base";
    args[1] = (char *)"--is-ancestor";
    args[2] = acopy;
    args[3] = bcopy;
    args[4] = NULL;
    rc = wgate_git(push_repo, args, 4, NULL, 0);
    if (rc == 0) {
        return 1;
    }
    if (rc == 1) {
        return 0;
    }
    return -1;
}

/* The digest's regate hint (behaviour 8): `append "<sha8> regate <time>" to
 * <state-basename>` -- the sha8 is the READY commit (the reference's line
 * 84 names $s, NOT the state path), and the target is the --state file's
 * BASENAME (the reference hard-codes gate/state; the port's CHANGED names
 * the file the --state knob actually wrote to). */
static int
wgate_digest_regate_hint(const char *sha8, const char *state_path,
                         char *out, size_t cap)
{
    if (jc_snprintf(out, cap,
                    "append \"%s regate <time>\" to %s",
                    sha8, wgate_basename(state_path)) >= (int)cap) {
        return 0;
    }
    return 1;
}

/* Append `text` (one line, no trailing newline) to the digest file. A write
 * that cannot happen is logged, never fatal (the reference's >> digest.md
 * 2>/dev/null). */
static void
wgate_digest_append(const char *digest_path, const char *text)
{
    FILE *f = fopen(digest_path, "ab");
    char line[WGATE_PATH_CAP];

    if ((size_t)jc_snprintf(line, sizeof line, "%s\n", text) >= sizeof line) {
        return;
    }
    if (f == NULL) {
        fprintf(stderr, "jichi-harness: watch gate: could not append to the "
                        "digest %s (no such directory?); skipped\n",
                digest_path);
        return;
    }
    fputs(line, f);
    if (ferror(f)) {
        fprintf(stderr, "jichi-harness: watch gate: could not write to the "
                        "digest %s\n", digest_path);
    }
    fclose(f);
}

/* Run the notify hook (behaviour 9) with ONE argument, the verdict TEXT. A
 * failure is logged, never fatal; an ABSENT hook runs nothing (the
 * pageCommand idiom). */
static void
wgate_notify(char *const nt_argv[], int n_nt, const char *text)
{
    char *extra[1];
    int nrc;

    if (n_nt == 0) {
        return;
    }
    extra[0] = (char *)text;
    nrc = wgate_run_rc(nt_argv, n_nt, extra, 1);
    if (nrc != 0) {
        fprintf(stderr, "jichi-harness: watch gate: the notify hook failed "
                        "(rc=%d); logged, never fatal\n", nrc);
    }
}

/* The `watch gate` tick. See the file header for the behaviours and the rc
 * table (behaviour 16): 0 for wait / silence / withdrawn / on-master /
 * not-ff / busy / smoke / no-READY; 1 for no-origin-master / no-gateRun /
 * unit-run-failed; 2 for usage (no host block). A tick that waits is a
 * SUCCESS: the next tick is the retry (the 10-minute cadence IS the polling
 * loop; the reference's every wait-arm is `exit 0`, no in-tick sleep). */
int
jc_hp_watchgate_run(const char *config_path, const char *log,
                    const char *state, const char *out, int dry,
                    const char *unit_state, const char *unit_list,
                    const char *unit_run, const char *pgrep,
                    struct jc_arena *a)
{
    struct jc_harness_config cfg;
    jc_status st;
    const char *push_repo;
    const char *gate_home;
    const char *gate_run;
    const char *notify_cmd;
    char repo[WGATE_PATH_CAP];
    char team_dir[WGATE_PATH_CAP];
    char digest_path[WGATE_PATH_CAP];
    char team_ref[WGATE_PATH_CAP];
    char master[WGATE_SHA_CAP];
    char full[WGATE_SHA_CAP];
    char c7[WGATE_SHA_CAP];
    char s8[WGATE_SHA_CAP];
    char m8[WGATE_SHA_CAP];
    char say[WGATE_PATH_CAP];
    char redtext[WGATE_PATH_CAP];
    char regate_hint[WGATE_PATH_CAP];
    char log_path[WGATE_PATH_CAP];
    const char *logbuf;
    const char *log_base;
    const char *state_word;
    struct wgate_ready ready;
    char *us_argv[WGATE_MAX_ARGV];
    char *ul_argv[WGATE_MAX_ARGV];
    char *ur_argv[WGATE_MAX_ARGV];
    char *pg_argv[WGATE_MAX_ARGV];
    char *nt_argv[WGATE_MAX_ARGV];
    int n_us, n_ul, n_ur, n_pg, n_nt;
    int rc;

    if (config_path == NULL) {
        fprintf(stderr, "jichi-harness: watch gate: --config <path> is "
                        "required\n");
        return 2;
    }
    st = jc_harness_config_load(config_path, &cfg, a);
    if (st != JC_OK) {
        const char *msg = jc_harness_config_error(&cfg);
        fprintf(stderr, "jichi-harness: watch gate: %s\n",
                (msg != NULL) ? msg : "could not read the harness config");
        return 2;
    }
    /* The gate watcher is host-side by construction (the push idiom): a
     * config with no usable host block refuses exit 2 naming `no host
     * block`. */
    if (cfg.host_dir_present == 0 ||
        cfg.host_dir == NULL || cfg.host_dir[0] == '\0') {
        fprintf(stderr, "jichi-harness: watch gate: no host block in the "
                        "config -- the gate watcher is host-side by "
                        "construction (the host object names pushHome, "
                        "gateHome, gateOnCalendar, gateRun)\n");
        return 2;
    }
    /* push.git: host.pushRepo, default <host.pushHome>/push.git. */
    if (cfg.host_push_repo != NULL && cfg.host_push_repo[0] != '\0') {
        push_repo = cfg.host_push_repo;
    } else if (cfg.host_push_home != NULL &&
               cfg.host_push_home[0] != '\0' &&
               (size_t)jc_snprintf(repo, sizeof repo, "%s/push.git",
                                   cfg.host_push_home) < sizeof repo) {
        push_repo = repo;
    } else {
        push_repo = NULL;
    }
    /* gate home: host.gateHome, default <cfg.dir>/gate. */
    if (cfg.host_gate_home != NULL && cfg.host_gate_home[0] != '\0') {
        gate_home = cfg.host_gate_home;
    } else if ((size_t)jc_snprintf(repo, sizeof repo, "%s/gate", cfg.dir)
                   < sizeof repo) {
        gate_home = repo;
    } else {
        gate_home = "";
    }
    gate_run = (cfg.host_gate_run != NULL && cfg.host_gate_run[0] != '\0')
                  ? cfg.host_gate_run : NULL;
    notify_cmd = cfg.host_notify_command;
    if ((size_t)jc_snprintf(team_dir, sizeof team_dir, "%s/team",
                            cfg.host_dir) >= sizeof team_dir) {
        return 1;
    }
    if (cfg.drive_branch_prefix == NULL || cfg.drive_branch_prefix[0] ==
            '\0' ||
        (size_t)jc_snprintf(team_ref, sizeof team_ref,
                            "refs/remotes/origin/%s/main",
                            cfg.drive_branch_prefix) >= sizeof team_ref) {
        return 1;
    }
    if ((size_t)jc_snprintf(digest_path, sizeof digest_path, "%s/digest.md",
                            gate_home) >= sizeof digest_path) {
        return 1;
    }
    /* The reference's :27 `mkdir -p "$G"`: the gate home (watch.out's and the
     * digest's parent) is created, not assumed -- a fresh host's drive dir has
     * no gate/ yet, and an absent parent makes the first tick's digest append
     * (and the state write) fail. The first cut dropped it; the container
     * driver pre-creates the dir, so only the switch-over's first host tick
     * would have lost its lines (the review's F4). */
    if (gate_home[0] != '\0' && jc_mkdir_p(gate_home) != JC_OK) {
        fprintf(stderr, "jichi-harness: watch gate: could not create the "
                        "gate home %s; the digest and state would have no "
                        "place to land\n", gate_home);
        return 1;
    }
    /* The shimmable commands: the CLI flag, then the config, then the
     * reference's argv (the pageCommand idiom). */
    {
        const char *us;
        const char *ul;
        const char *ur;
        const char *pg;
        char *us_c;
        char *ul_c;
        char *ur_c;
        char *pg_c;
        static const char DEF_US[] = "systemctl --user is-active -q";
        static const char DEF_UL[] =
            "systemctl --user list-units --state=active,activating "
            "--no-legend jhd-gate-* jhd-ci-* jhp-gate-* jhp-ci-*";
        static const char DEF_UR[] = "systemd-run --user --collect --nice=19";
        static const char DEF_PG[] = "pgrep -f tests/smoke/";
        us = (unit_state != NULL && unit_state[0] != '\0')
                ? unit_state : (cfg.host_unit_state != NULL
                                     ? cfg.host_unit_state : DEF_US);
        ul = (unit_list != NULL && unit_list[0] != '\0')
                ? unit_list : (cfg.host_unit_list != NULL
                                   ? cfg.host_unit_list : DEF_UL);
        ur = (unit_run != NULL && unit_run[0] != '\0')
                ? unit_run : (cfg.host_unit_run != NULL
                                  ? cfg.host_unit_run : DEF_UR);
        pg = (pgrep != NULL && pgrep[0] != '\0')
                ? pgrep : DEF_PG;
        /* strtok_r mutates its buffer: copy each command into the arena
         * first (us/ul/ur/pg name static defaults, config fields, or argv). */
        us_c = jc_arena_strdup(a, us);
        ul_c = jc_arena_strdup(a, ul);
        ur_c = jc_arena_strdup(a, ur);
        pg_c = jc_arena_strdup(a, pg);
        n_us = (us_c != NULL) ? wgate_split_cmd(a, us_argv, us_c) : 0;
        n_ul = (ul_c != NULL) ? wgate_split_cmd(a, ul_argv, ul_c) : 0;
        n_ur = (ur_c != NULL) ? wgate_split_cmd(a, ur_argv, ur_c) : 0;
        n_pg = (pg_c != NULL) ? wgate_split_cmd(a, pg_argv, pg_c) : 0;
    }
    if (notify_cmd != NULL) {
        char *nbuf = jc_arena_strdup(a, notify_cmd);
        n_nt = (nbuf != NULL) ? wgate_split_cmd(a, nt_argv, nbuf) : 0;
    } else {
        n_nt = 0;
    }

    /* 1. The READY line (behaviour 1). The --log default (M1156): when
     * --log is absent or empty, read <cfg.dir>/LOG.md (the config's own dir
     * field, the drive dir). The reference's GATE_LOG3 env is this flag and it
     * NEVER refuses on an absent log; the port did ("the dispatch always
     * supplies one" -- false), which made the generated `watch units` oneshot
     * (ExecStart with no --log) refuse every tick. cfg.dir is guaranteed
     * non-empty (the gate-home default above already joins it); if the join
     * would overflow the path cap that is a refusal, not a silent truncation.
     * log_base (the log's last path component, else the whole string) is the
     * leaf named in every say line the tick produces. */
    if (log == NULL || log[0] == '\0') {
        if ((size_t)jc_snprintf(log_path, sizeof log_path, "%s/LOG.md",
                                cfg.dir) >= sizeof log_path) {
            fprintf(stderr, "jichi-harness: watch gate: the default LOG "
                            "path <dir>/LOG.md is too long to name; refusing\n");
            return 1;
        }
        log = log_path;
    }
    log_base = wgate_basename(log);
    logbuf = wgate_read_file(a, log);
    if (logbuf == NULL) {
        return 0;   /* no LOG file: the quiet tick (a value, not a crash) */
    }
    if (!wgate_find_last_ready(logbuf, &ready)) {
        return 0;   /* no READY line: the normal quiet tick */
    }
    /* 2. Resolve the READY commit in push.git (the reference's :38): the
     * say lines carry the RESOLVED `%.8s` of the full sha, so a 7-char READY
     * line names its 8-char full form. `full` is the 40-char sha; the
     * wait-for-remote say (below) uses `%.8s` of the READY string when it
     * cannot resolve yet (the reference's :40, before :42). */
    if (push_repo == NULL) {
        wgate_say(out, "no push.git in the config (host.pushRepo or "
                       "host.pushHome); waiting for the push timer");
        return 0;
    }
    {
        size_t cl = strlen(ready.commit);
        size_t t7 = (cl < 7) ? cl : 7;
        size_t t8 = (cl < 8) ? cl : 8;
        memcpy(c7, ready.commit, t7);
        c7[t7] = '\0';
        memcpy(s8, ready.commit, t8);
        s8[t8] = '\0';
        full[0] = '\0';
        (void)wgate_git_full(push_repo, ready.commit, full, sizeof full);
    }

    /* 3. The wait-for-remote (behaviour 2, the reference's :39-41): the
     * READY commit must be IN push.git AND under the remote's team ref. */
    {
        int under;
        if (full[0] != '\0') {
            under = wgate_git_is_ancestor(push_repo, full, team_ref);
            if (under == -1) {
                under = 0;   /* a git error: wait (the push timer's problem) */
            }
        } else {
            under = 0;   /* not in push.git yet: the push timer has not run */
        }
        if (under == 0) {
            if (jc_snprintf(say, sizeof say,
                            "BATCH %s READY at %s (%s:%d): not on the "
                            "remote's team branch yet; waiting for the push "
                            "timer",
                            ready.batch, s8, log_base, ready.line_no) < 0) {
                return 1;
            }
            wgate_say(out, say);
            return 0;
        }
    }
    if (full[0] != '\0') {
        size_t fl = strlen(full);
        size_t t8 = (fl < 8) ? fl : 8;
        memcpy(s8, full, t8);
        s8[t8] = '\0';
    }

    /* 4. The state-word arms (behaviours 3, 4, 5, 6, the reference's
     * :43-52): read the LAST state word for the RESOLVED sha8. '' -> fall
     * through; started -> is-active check (exit 0 active / say+continue
     * dead); regate -> say+continue; any other word -> the verdict stands
     * (exit 0, say NOTHING). */
    {
        int regate = 0;
        int started_dead = 0;
        state_word = wgate_state_word(a, state, s8);
        if (state_word != NULL && strcmp(state_word, "started") == 0) {
            char unit[WGATE_SHA_CAP + 16];
            char *extra[1];
            int arc;
            if ((size_t)jc_snprintf(unit, sizeof unit, "jhp-gate-%s", s8)
                    >= sizeof unit) {
                return 1;
            }
            extra[0] = unit;
            arc = wgate_run_rc(us_argv, n_us, extra, 1);
            if (arc == 0) {
                return 0;   /* the unit is still active: say nothing */
            }
            started_dead = 1;
        } else if (state_word != NULL && strcmp(state_word, "regate") == 0) {
            regate = 1;
        } else if (state_word != NULL) {
            return 0;   /* gated already: its verdict stands (behaviour 6) */
        }
        if (started_dead) {
            if (jc_snprintf(say, sizeof say,
                            "the gate on %s died without a verdict "
                            "(jhp-gate-%s is not active): gating it again",
                            s8, s8) < 0) {
                return 1;
            }
            wgate_say(out, say);
        }
        if (regate) {
            if (jc_snprintf(say, sizeof say,
                            "BATCH %s READY at %s: gating it again "
                            "(gate/state says regate: the earlier verdict "
                            "was withdrawn)",
                            ready.batch, s8) < 0) {
                return 1;
            }
            wgate_say(out, say);
        }
    }
    /* 5. The withdrawal (behaviour 7, the reference's :53-88): a LATER LOG
     * line (after the READY line) with a SENTENCE that WITHDRAWS the READY
     * commit and names no other commit. */
    {
        const char *p = logbuf;
        int ln = 0;
        int withdrawn = 0;
        while (*p != '\0') {
            const char *nl = p;
            size_t ll = 0;
            while (*nl != '\0' && *nl != '\n') {
                nl++;
                ll++;
            }
            ln++;
            if (ln > ready.line_no && ll > 0 && ll < 65536) {
                /* The line's OWN length (M1178): `p` points into the LOG
                 * buffer, so a NUL-terminated read ran to its end. */
                if (jc_gate_withdrawn_n(p, ll, c7)) {
                    withdrawn = 1;
                    break;
                }
            }
            if (*nl == '\0') {
                break;
            }
            p = nl + 1;
        }
        if (withdrawn) {
            char wtext[WGATE_PATH_CAP];
            char dgtext[WGATE_PATH_CAP];
            if (jc_snprintf(say, sizeof say,
                            "BATCH %s READY at %s (%s:%d) is WITHDRAWN by "
                            "a later line: not gated",
                            ready.batch, s8, log_base, ready.line_no) < 0) {
                return 1;
            }
            wgate_say(out, say);
            if (dry) {
                return 0;   /* the say line is the only dry output */
            }
            wgate_verdict(state, s8, "withdrawn");
            if (wgate_digest_regate_hint(s8, state, regate_hint,
                                         sizeof regate_hint) &&
                (size_t)jc_snprintf(dgtext, sizeof dgtext,
                                    "BATCH %s READY at %s (%s:%d) read as "
                                    "WITHDRAWN by a later LOG sentence; not "
                                    "gated. If that is wrong, %s",
                                    ready.batch, s8, log_base, ready.line_no,
                                    regate_hint) < sizeof dgtext) {
                wgate_digest_append(digest_path, dgtext);
            }
            if ((size_t)jc_snprintf(wtext, sizeof wtext,
                                    "BATCH %s READY at %s read as "
                                    "WITHDRAWN by a later LOG sentence; not "
                                    "gated; the state word is %s withdrawn",
                                    ready.batch, s8, s8) < sizeof wtext) {
                wgate_notify(nt_argv, n_nt, wtext);
            }
            return 0;
        }
    }
    /* 6. The master checks (behaviours 10-11, the reference's :89-99):
     * origin/master must resolve; on-master (the reference's own sha8 for
     * master); not-ff (the verdict + the reference's RED notify text). */
    if (wgate_git_full(push_repo, "refs/remotes/origin/master", master,
                       sizeof master) != 1 || master[0] == '\0') {
        wgate_say(out, "no origin/master in push.git");
        return 1;
    }
    {
        int onm;
        int mff;
        size_t ml = strlen(master);
        size_t t8 = (ml < 8) ? ml : 8;
        memcpy(m8, master, t8);
        m8[t8] = '\0';
        onm = wgate_git_is_ancestor(push_repo, s8, master);
        mff = wgate_git_is_ancestor(push_repo, master, s8);
        if (onm == 1) {
            if (jc_snprintf(say, sizeof say,
                            "BATCH %s READY at %s: on master already (%s)",
                            ready.batch, s8, m8) < 0) {
                return 1;
            }
            wgate_say(out, say);
            wgate_verdict(state, s8, "on-master");
            return 0;
        }
        if (onm == -1 || mff == -1) {
            wgate_say(out, "no origin/master in push.git");
            return 1;
        }
        if (mff == 0) {
            if (jc_snprintf(say, sizeof say,
                            "BATCH %s READY at %s: NOT a fast-forward of "
                            "master %s",
                            ready.batch, s8, m8) < 0) {
                return 1;
            }
            wgate_say(out, say);
            if (dry) {
                return 0;
            }
            wgate_verdict(state, s8, "not-ff");
            if ((size_t)jc_snprintf(redtext, sizeof redtext,
                                    "RED (not gated): BATCH %s READY at %s "
                                    "is not a fast-forward of master %s. "
                                    "Rebase or merge master into the team "
                                    "branch, then write a new READY line.",
                                    ready.batch, s8, m8) < sizeof redtext) {
                wgate_notify(nt_argv, n_nt, redtext);
            }
            return 0;
        }
    }
    /* 7. The busy filter (behaviour 11, DD5): the unit list's lines that are
     * a gate/ci SERVICE named by a commit (the watcher's own units are NOT
     * one -- the 01:58 self-match's teeth). */
    {
        char units[WGATE_LOG_CAP];
        int lrc = wgate_run_list(ul_argv, n_ul, NULL, 0, units,
                                 sizeof units);
        if (lrc == 0) {
            char names[WGATE_BUSY_CAP];
            const char *p = units;
            names[0] = '\0';
            while (*p != '\0') {
                char unit[WGATE_PATH_CAP];
                size_t tl = 0;
                /* The unit is the line's first FIELD, not its first
                 * space-delimited token (M1178): `systemctl list-units
                 * --no-legend` starts every line with two spaces (or with
                 * a status glyph, U+25CF, and a space), so the token read
                 * here was empty and a running gate was never busy. The
                 * reference's awk '{print $1}' skipped the blanks. */
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                if ((unsigned char)p[0] == 0xE2 &&
                    (unsigned char)p[1] == 0x97 &&
                    (unsigned char)p[2] == 0x8F) {
                    p += 3;
                    while (*p == ' ' || *p == '\t') {
                        p++;
                    }
                }
                while (*p != '\0' && *p != ' ' && *p != '\t' &&
                       *p != '\n' && tl < sizeof unit - 1) {
                    unit[tl++] = *p;
                    p++;
                }
                unit[tl] = '\0';
                if (wgate_is_busy_unit(unit)) {
                    size_t off = strlen(names);
                    /* Each unit is followed by ONE space, the last one too:
                     * the reference joins them with `tr '\n' ' '`, so its
                     * say line reads `(jhp-gate-<s8>.service )`, and the
                     * shadow compares the say lines byte for byte (M1178;
                     * measured on the 2026-11-13 07:37 replay). The bound:
                     * names holds its prefix, and the unit (tl bytes), the
                     * space and the NUL fit in sizeof names. memcpy, not
                     * strcat (sprintf_lint's raw-call rule). */
                    if (off + tl + 1 < sizeof names) {
                        memcpy(names + off, unit, tl);
                        names[off + tl] = ' ';
                        names[off + tl + 1] = '\0';
                    }
                }
                while (*p != '\0' && *p != '\n') {
                    p++;
                }
                if (*p == '\n') {
                    p++;
                }
            }
            if (names[0] != '\0') {
                if (jc_snprintf(say, sizeof say,
                                "BATCH %s READY at %s: waiting, a gate runs "
                                "(%s)",
                                ready.batch, s8, names) < 0) {
                    return 1;
                }
                wgate_say(out, say);
                return 0;
            }
        }
        /* 6. The team-smoke wait (behaviour 12): the shim's pids, and for
         * each one /proc/<pid>/cwd == the team dir. */
        {
            char pids[WGATE_LOG_CAP];
            int gsrc = wgate_run_list(pg_argv, n_pg, NULL, 0, pids,
                                      sizeof pids);
            const char *p = pids;
            if (gsrc == 0) {
                while (*p != '\0') {
                    char pid[WGATE_SHA_CAP];
                    char ppath[WGATE_PATH_CAP];
                    char cwd[WGATE_PATH_CAP];
                    size_t pl = 0;
                    ssize_t n;
                    while (*p >= '0' && *p <= '9' &&
                           pl < sizeof pid - 1) {
                        pid[pl++] = *p;
                        p++;
                    }
                    pid[pl] = '\0';
                    while (*p != '\0' && *p != '\n') {
                        p++;
                    }
                    if (*p == '\n') {
                        p++;
                    }
                    if (pl == 0) {
                        continue;
                    }
                    if ((size_t)jc_snprintf(ppath, sizeof ppath,
                                            "/proc/%s/cwd", pid)
                            >= sizeof ppath) {
                        continue;
                    }
                    n = readlink(ppath, cwd, sizeof cwd - 1);
                    if (n <= 0) {
                        continue;
                    }
                    cwd[n] = '\0';
                    /* The reference's :106-108 waits when the cwd EQUALS the
                     * team dir OR is UNDER it (a smoke that cd's into
                     * <team>/examples still runs beside the drive). The first
                     * cut (==0 only) dropped that half. */
                    if (strcmp(cwd, team_dir) == 0 ||
                        (strncmp(cwd, team_dir, strlen(team_dir)) == 0 &&
                         cwd[strlen(team_dir)] == '/')) {
                        if (jc_snprintf(say, sizeof say,
                                        "BATCH %s READY at %s: waiting, a "
                                        "team smoke runs (pid %s)",
                                        ready.batch, s8, pid) < 0) {
                            return 1;
                        }
                        wgate_say(out, say);
                        return 0;
                    }
                }
            }
        }
    }
    /* 7. The start (behaviour 13): the unit-run shim gets the unit name as
     * the --unit= OPTION, the description as the -p Description= PROPERTY,
     * and the exec `<gateRun> <sha8> <batch>` as SEPARATE command words
     * (def-381, M1188 -- the reference's gate-watch.sh:113-114 token order;
     * the three faults were the unit/description as bare positionals and the
     * command as one space-joined word, SV:2327), its stdout+stderr appended
     * to the --out file. */
    if (gate_run == NULL || gate_run[0] == '\0') {
        wgate_say(out, "no host.gateRun in the config; cannot start the "
                       "gate");
        return 1;
    }
    if (dry) {
        if (jc_snprintf(say, sizeof say,
                        "DRY: would start jhp-gate-%s (gate-run.sh %s) for "
                        "BATCH %s (%s:%d)",
                        s8, s8, ready.batch, log_base, ready.line_no) < 0) {
            return 1;
        }
        wgate_say(out, say);
        return 0;
    }
    {
        char unitopt[WGATE_SHA_CAP + 24];
        char desc[WGATE_PATH_CAP];
        char *extra[WGATE_MAX_ARGV];
        char *cmd_argv[WGATE_MAX_ARGV];
        char *cmd_copy;
        int n_cmd;
        int n_extra = 0;
        int ci;
        if ((size_t)jc_snprintf(unitopt, sizeof unitopt,
                                "--unit=jhp-gate-%s", s8)
                >= sizeof unitopt ||
            (size_t)jc_snprintf(desc, sizeof desc,
                                "Description=jichi drive gate on %s "
                                "(BATCH %s)", s8, ready.batch) >= sizeof desc) {
            return 1;
        }
        /* The command is its OWN words (def-381's third fault): gateRun split
         * through the shared tokenizer, then the sha8 and the batch -- not one
         * space-joined element. gate_run is non-empty here (the refusal above). */
        cmd_copy = jc_arena_strdup(a, gate_run);
        n_cmd = (cmd_copy != NULL)
                    ? wgate_split_cmd(a, cmd_argv, cmd_copy)
                    : 0;
        if (n_cmd <= 0) {
            wgate_say(out, "host.gateRun is too long to tokenize; cannot "
                           "start the gate");
            return 1;
        }
        extra[n_extra++] = unitopt;
        extra[n_extra++] = "-p";
        extra[n_extra++] = desc;
        for (ci = 0; ci < n_cmd && n_extra < WGATE_MAX_ARGV; ci++) {
            extra[n_extra++] = cmd_argv[ci];
        }
        if (n_extra + 2 >= WGATE_MAX_ARGV) {
            return 1;
        }
        extra[n_extra++] = s8;
        extra[n_extra++] = ready.batch;
        wgate_verdict(state, s8, "started");
        if (jc_snprintf(say, sizeof say,
                        "BATCH %s READY at %s (%s:%d): starting "
                        "jhp-gate-%s",
                        ready.batch, s8, log_base, ready.line_no, s8) < 0) {
            return 1;
        }
        wgate_say(out, say);
        rc = wgate_run_to_file(ur_argv, n_ur, extra, n_extra, out);
        if (rc != 0) {
            if (jc_snprintf(say, sizeof say, "systemd-run failed (rc=%d)",
                            rc) < 0) {
                return 1;
            }
            wgate_say(out, say);
            return 1;   /* the state word `started` stays (behaviour 18) */
        }
    }
    return 0;
}
