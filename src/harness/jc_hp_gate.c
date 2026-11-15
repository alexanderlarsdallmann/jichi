/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gate.c - the `gate run <SHA8> [BATCH]` orchestrator (M1159, batch H9b
 * wave 4): the C89 port of the 2026-10-27 hardening-host drive's gate-run.sh,
 * CORE PATH (the contract: briefs-2026-11-11-M1159-B.md, M1159.md D1-D6,
 * D8-D11, D14; the driver's G-cells pin the bytes).
 *
 * The port runs the reference's steps in its order: the keyless SETUP
 * (D8: the orchestrator's OWN git children read the push repo), the `gated`
 * word (D9), then the FIVE STAGE CHILDREN (D1/D2: the merged stage bodies
 * re-run as their own dispatch forms through a `sh -c` self-exec, records
 * redirected like the reference's `sh <stage>.sh ... > <file> 2>&1`), the
 * RED / INVALID / GREEN-LAND-REFUSED verdicts (D4's table), each delivered
 * in the D6 ORDER: state word -> say -> note.
 *
 * say (gate-run.sh:26, b12 CHANGED): append `<now> <text>` to
 * <G>/<s8>/run.log AND print `<now> <text>` to stdout (the unit's journal).
 * verdict (gate-run.sh:27, b13): append `<s8> <word> <now>` to the state
 * file, NEVER truncating (G18: a second run appends lines 3-4).
 * note (b13): jc_harness_notify IN-PROCESS (the H8 delivery: the inbox note,
 * the digest.md line, the hook); a non-zero rc is named, never fatal.
 *
 * The stage children are NOT keyless at the shell level (D11: the reference
 * scrubs for the STAGES' OWN children only, not the orchestrator's); the
 * setup's git children ARE keyless (D8, the 16-name scrub + GIT_CONFIG_*,
 * the jc_hp_git_run idiom). Tool errors are VALUES: a stage-child rc feeds
 * D10's table (rc 1 -> the stage's INVALID marker, rc 2 -> invalid too:
 * the child's own usage refusal is a gate-tool fault, the rc 2 rule).
 * No cap: the children run with NO timeout (D14: the ci's 48 minutes are
 * the point).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "jc_fcntl.h"             /* JC_O_BINARY */
#include "jc_hp_gate.h"
#include "jc_harness_config.h"    /* the config loader + the notify entry */
#include "jc_harness_notify.h"    /* jc_harness_notify (the note delivery) */
#include "jc_harness_push.h"      /* jc_hp_git_run (the D8 keyless git child) */
#include "jc_mem.h"               /* struct jc_arena, jc_arena_strdup */
#include "jc_platform.h"          /* jc_status, jc_size, jc_mkdir_p */
#include "jc_proc.h"              /* jc_proc_capture (the D1 sh -c self-exec) */
#include "jc_snprintf.h"          /* jc_snprintf, never sprintf */
#include "jc_str.h"               /* struct jc_sb, the record files */
#include "jc_vec.h"               /* struct jc_vec */

/* The path bounds: the driver's paths are the mkdtemp shape (~160); these
 * caps bound the assembled homes, the say/note text, and the records.
 * 4352 matches the notify's own path cap (NOTIFY_PATH_CAP). */
#define GF_PATH_MAX 4352
#define GF_TEXT_MAX 2048          /* one say / note field (the joins are <= 640) */
#define GF_NOTE_MAX 16384         /* the whole note text (well under the
                                    notify's 128KB TEXT cap) */
#define GF_REC_MAX  (512 * 1024)  /* a stage record (the driver's records are
                                    small; the sweep.txt + ci.txt are bounded
                                    by their own bodies' line caps) */
#define GF_LINE_MAX 8192

/* The 16 keyless names (the sweep/ci idiom, M1154 T6: the 16 names the
 * stage bodies scrub, INCLUDING ZD_TOKEN_FILE -- the one name jc_proc's
 * built-in scrub lacks). The D8 setup git children receive them set EMPTY
 * (plus the GIT_CONFIG_ /dev/null slots the push idiom adds). */
static const char *const gf_keys[] = {
    "JICHI_API_KEY", "JLU_API_KEY", "ANTHROPIC_API_KEY", "OPENAI_API_KEY",
    "OPENROUTER_API_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY", "GROQ_API_KEY",
    "MISTRAL_API_KEY", "DEEPSEEK_API_KEY", "TOGETHER_API_KEY",
    "FIREWORKS_API_KEY", "TAVILY_API_KEY", "COHERE_API_KEY", "XAI_API_KEY",
    "ZD_TOKEN_FILE"
};
#define GF_NKEYS (int)(sizeof gf_keys / sizeof gf_keys[0])

/* --- the run's state (everything the helpers below need) ------------------ */

struct gf_run {
    const char *cfg_path;
    const char *s8;            /* the SHA8 as given */
    const char *batch;         /* <b>, "?" when absent */
    char full[64];             /* the resolved commit id (40 hex + NUL) */
    char master[64];           /* the origin/master id (40 hex + NUL) */
    char push_repo[GF_PATH_MAX];
    char gate_home[GF_PATH_MAX];
    char state[GF_PATH_MAX];   /* the state file path */
    char G[GF_PATH_MAX];       /* <gate-home>/<s8> */
    char runlog[GF_PATH_MAX];  /* <G>/run.log */
    char ci_log_dir[GF_PATH_MAX];  /* <gate-home>/ci (flat, D3) */
    char ledger_addr[512];
    char ledger_accept[GF_PATH_MAX];
    /* The forwarded stage surfaces (NULL = absent: the child's own flag-
     * else-config default, D2). */
    const char *records;
    const char *bin, *build_run, *mutant_run, *unit_run_san, *unit_run_o2,
        *container_run, *redfirst_run, *redfirst_unit_run, *ci_run,
        *ci_log_dir_flag, *ci_smoke_keep_going;
    struct jc_arena *a;
    /* The GREEN-note inputs (the brief's five), stored as each step computes
     * them so the terminal (HOLD / land / repin, D4.7-9) can see them: n
     * (the commit count, the reference's :42 rev-list child), sweepline
     * (the sweep record's `sweep: ran` line, :61), smokel / okl (the ci log's
     * last `smoke: OK` / `ci: OK` line, :76/:81), rfnote (the redfirst's
     * :68 form, already joined at the redfirst step). */
    int n;
    char sweepline[GF_TEXT_MAX];
    char smokel[GF_TEXT_MAX];
    char okl[GF_TEXT_MAX];
    char rfnote[GF_NOTE_MAX];
};

/* Forward: the record's last non-empty line (defined with the stage-child
 * helpers below; the repin-line strip above needs it). */
static void gf_last_line(const char *text, char *out, size_t cap);

/* Forward: the D4.7-9 terminal's helpers (defined below). */
static int gf_subst_s(char *cmd, size_t cap, const char *s8);
static int gf_capture_cmd(const struct gf_run *r, const char *cmd,
                          char *out_text, size_t cap, int *out_rc);
static void gf_repin_line(const char *text, char *out, size_t cap);
static int gf_hcn_count(const char *text);
static void gf_log_append(const struct gf_run *r, const char *text);

/* Forward: one single-quoted shell word (defined with the stage-child
 * helpers below; gf_git quotes its words with it, M1178). */
static int gf_cmd_word(char *cmd, size_t cap, const char *w);

/* A commit token: 7 to 40 lowercase hex characters (the watcher's alphabet,
 * jc_hp_watchgate.c). <SHA8> is pasted into shell command lines and joined
 * onto the gate home as a directory name, so anything else is refused at the
 * door (M1178). 1 iff `s` is one. */
static int
gf_is_commit_token(const char *s)
{
    size_t i;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
            return 0;
        }
    }
    return i >= 7 && i <= 40;
}

static long
gf_utc_from_tm(const struct tm *tmv)
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
    return (days * 86400L + tmv->tm_hour * 3600L
            + tmv->tm_min * 60L + tmv->tm_sec);
}

/* gate-run.sh:26 `date '+%F %T %z'`: the now field of a say line and a state
 * entry (the driver's _gstateline pins the %F %T %z shape, 4th field).
 * %F %T %z are GNU specifiers this C89 build rejects, so the same bytes are
 * assembled from ISO %Y-%m-%d %H:%M:%S plus the local offset (the
 * drive's stamp idiom, jc_harness_drive.c:117 -- the hand-rolled timegm
 * of the LOCAL wall clock minus t; glibc's timegm is not exposed under
 * the build's global _POSIX_C_SOURCE). */
static void
gf_now(char *out, size_t cap)
{
    time_t t = time(NULL);
    struct tm tmv;
    struct tm tmc;
    long off;
    long mag;
    int oh, om;
    size_t n;

    if (localtime_r(&t, &tmv) == NULL) {
        out[0] = '\0';
        return;
    }
    tmc = tmv;
    off = gf_utc_from_tm(&tmc) - (long)t;
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
    if (n == 0) {
        out[0] = '\0';
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
    } else {
        out[n] = '\0';
    }
}

/* The body's own usage line (the dispatch's usage text is main.c's; this
 * says the ledger-address requirement, G3 greps for --ledger-address). */
static void
usage_refused_gate_run(void)
{
    fprintf(stderr,
            "jichi-harness: gate run: <SHA8> [BATCH] with "
            "--ledger-address <addr> (or host.ledgerAddress)\n");
}

/* A fatal, NAMED fault: stderr + return 1. (A tool error is a value -- the
 * stage-child rcs are judged below; only a fault the body cannot recover
 * from reaches here.) */
static int
gf_die(const struct gf_run *r, const char *what)
{
    (void)r;
    fprintf(stderr, "jichi-harness: gate run: %s\n", what);
    return 1;
}

/* The harness's own executable path (D1: readlink /proc/self/exe, the
 * jc_hp_watchunits.c:94 idiom -- one owner per file, this file carries its
 * own copy). 1 on success. */
static int
gf_self_path(char *out, size_t cap)
{
    char buf[GF_PATH_MAX];
    ssize_t n;

    n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0 || n >= (ssize_t)sizeof buf) {
        return 0;
    }
    buf[n] = '\0';
    if (jc_snprintf(out, cap, "%s", buf) >= (int)cap) {
        return 0;
    }
    return 1;
}

/* --- the D6 machinery ------------------------------------------------------ */

/* The state file's parent must exist (the driver's --state paths are in an
 * existing tmp, the default is <gate-home>, which the body mkdir -p's). */
static int
gf_verdict(const struct gf_run *r, const char *word)
{
    char now[64];
    char line[2048];
    int n;
    FILE *f;

    gf_now(now, sizeof now);
    n = jc_snprintf(line, sizeof line, "%s %s %s\n", r->s8, word, now);
    if (n < 0 || (size_t)n >= sizeof line) {
        return -1;
    }
    f = fopen(r->state, "ab");
    if (f == NULL) {
        return -1;
    }
    if (fputs(line, f) == EOF) {
        (void)fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        return -1;
    }
    return 0;
}

/* say (gate-run.sh:26, b12 CHANGED): append `<now> <text>` to run.log AND
 * print `<now> <text>` to stdout. */
static int
gf_say(const struct gf_run *r, const char *text)
{
    char now[64];
    FILE *f;

    gf_now(now, sizeof now);
    f = fopen(r->runlog, "ab");
    if (f == NULL) {
        return -1;
    }
    if (fputs(now, f) != EOF && fputc(' ', f) != EOF &&
        fputs(text, f) != EOF && fputc('\n', f) != EOF &&
        fflush(f) == 0) {
        (void)fclose(f);
    } else {
        (void)fclose(f);
        fprintf(stderr, "jichi-harness: gate run: could not write the say "
                        "line to %s\n", r->runlog);
        return -1;
    }
    printf("%s %s\n", now, text);
    fflush(stdout);
    return 0;
}

/* note (b13): jc_harness_notify IN-PROCESS. A non-zero rc is NAMED (the
 * D6 order: after the say), never fatal: the verdict stands. */
static void
gf_note(const struct gf_run *r, const char *text, const char *attach,
        const char *word)
{
    int nr;

    nr = jc_harness_notify(r->cfg_path, NULL, r->s8, text, attach, r->a);
    if (nr != 0) {
        char line[GF_TEXT_MAX];

        (void)jc_snprintf(line, sizeof line,
                          "notify: delivery failed rc=%d (the verdict "
                          "stands: %s)", nr, word);
        (void)gf_say(r, line);
    }
}

/* red <kind> <reason> (gate-run.sh:29-31, D4.10): verdict red-<kind>;
 * say VERDICT red (<kind>): <reason>; note the D4.10 byte shape; exit 1. */
static int
gf_red(const struct gf_run *r, const char *kind, const char *reason)
{
    char sayline[GF_TEXT_MAX];
    char noteline[GF_NOTE_MAX];
    char word[64];

    (void)jc_snprintf(word, sizeof word, "red-%s", kind);
    if (gf_verdict(r, word) != 0) {
        return gf_die(r, "could not write the state file");
    }
    (void)jc_snprintf(sayline, sizeof sayline,
                      "VERDICT red (%s): %s", kind, reason);
    if (gf_say(r, sayline) != 0) {
        return 1;
    }
    (void)jc_snprintf(noteline, sizeof noteline,
                      "RED (%s) on BATCH %s READY at %s. %s "
                      "Nothing was landed; master is unchanged. Fix it, then "
                      "write a new BATCH READY line.",
                      kind, r->batch, r->s8, reason);
    gf_note(r, noteline, NULL, "red");
    return 1;
}

/* invalid <stage> (D4.11, b11 CHANGED): verdict invalid-<stage>;
 * say VERDICT invalid (<stage>); note the record's LAST THREE lines, each
 * cut 200, newline->';' (G8's byte shape: the one-line build-failed record
 * gives `build failed: INVALID;`); exit 0 (the batch is not judged). */
static int
gf_invalid(const struct gf_run *r, const char *stage,
           const char *record_text)
{
    char sayline[GF_TEXT_MAX];
    char noteline[GF_NOTE_MAX];
    char lines[GF_NOTE_MAX];
    char word[64];

    (void)jc_snprintf(word, sizeof word, "invalid-%s", stage);
    if (gf_verdict(r, word) != 0) {
        return gf_die(r, "could not write the state file");
    }
    (void)jc_snprintf(sayline, sizeof sayline,
                      "VERDICT invalid (%s)", stage);
    if (gf_say(r, sayline) != 0) {
        return 0;
    }
    {
        /* The record's LAST THREE non-empty lines (the markers + context),
         * each cut 200, each APPENDED WITH ITS OWN ';' -- the reference's
         * `tr '\n' ';'` on a newline-terminated stream yields a ';' after
         * the LAST line too (the driver's G8 grep: `build failed:
         * INVALID; Change nothing...`). A sliding window keeps the tail. */
        struct {
            const char *start;
            size_t len;
        } ls[3];
        int k = 0;
        size_t off;
        size_t len;
        int j;

        len = strlen(record_text != NULL ? record_text : "");
        off = 0;
        while (off < len) {
            size_t start = off;
            size_t end = start;

            while (end < len && record_text[end] != '\n') {
                end++;
            }
            if (end > start) {
                ls[k].start = record_text + start;
                ls[k].len = end - start;
                k++;
                if (k == 3) {
                    k = 2;
                    ls[0] = ls[1];
                    ls[1] = ls[2];
                }
            }
            off = end;
            if (end < len) {
                off++;
            }
        }
        lines[0] = '\0';
        for (j = 0; j < k; j++) {
            size_t takej = ls[j].len < 200 ? ls[j].len : 200;
            char *q = lines + strlen(lines);

            if (takej > 0) {
                memcpy(q, ls[j].start, takej);
                q[takej] = '\0';
            }
            q[takej] = ';';
            q[takej + 1] = '\0';
        }
    }
    (void)jc_snprintf(noteline, sizeof noteline,
                      "INVALID (%s) on BATCH %s READY at %s. %s "
                      "Change nothing, wait: the gate's own tool faulted, "
                      "the batch is not judged. (A re-gate is the watcher's "
                      "regate arm, or fix the tool and let the next tick "
                      "take it.)",
                      stage, r->batch, r->s8, lines);
    gf_note(r, noteline, NULL, "invalid");
    return 0;
}

/* --- the D4.7-9 HOLD / land / repin helpers (gate-run.sh:84-104) --------- */

/* Replace every literal `<s>` in `cmd` with `s8` (the READY sha8, as given):
 * the shimmable land / repin commands' substitution token (the driver's
 * repin stubs echo `repin: done <s>, doctors clean`, the reference's
 * repin.sh prints its `$old -> $s` summary -- s8 is the port's $s).
 * The token is fixed-shape (3 bytes -> 8), so the cap is sized by the
 * caller for the worst case. -1 on a truncation. */
static int
gf_subst_s(char *cmd, size_t cap, const char *s8)
{
    char out[16384];
    size_t i = 0;
    size_t o = 0;

    /* Worst case: every `<s>` (3 bytes) expands to strlen(s8) -- a safe
     * upper bound on 512 tokens; the loop below is bounds-checked too. */
    if (strlen(cmd) + 512 * (strlen(s8) + 1) > sizeof out) {
        return -1;
    }
    for (i = 0; cmd[i] != '\0'; ) {
        if (cmd[i] == '<' && strncmp(cmd + i, "<s>", 3) == 0) {
            o += (size_t)jc_snprintf(out + o, sizeof out - o, "%s", s8);
            if (o >= sizeof out) {
                return -1;
            }
            i += 3;
        } else {
            if (o + 1 >= sizeof out) {
                return -1;
            }
            out[o++] = cmd[i];
            i++;
        }
    }
    if (o + 1 > cap) {
        return -1;
    }
    memcpy(cmd, out, o);
    cmd[o] = '\0';
    return (int)o;
}

/* Run one shimmable command (the reference's land.sh / repin.sh, D4.8/9):
 * `sh -c '<cmd> <s8>'` with stdin /dev/null (the reference's
 * `< /dev/null`), combined stdout+stderr CAPTURED (the reference's
 * `lo=$( ... 2>&1)`) with the trailing newline trimmed (the command
 * substitution does that). The child inherits the process environment
 * (D11: the host's land.sh is a command that manages its own env; the
 * reference runs it with gate-run.sh's env). The `<s>` token is
 * substituted with the READY sha8 (see gf_subst_s). 1 on success (the
 * combined output in *out_text, the child's rc in *out_rc), 0 on a
 * spawn/assembly fault (the caller names it; out_rc stays 0). */
static int
gf_capture_cmd(const struct gf_run *r, const char *cmd, char *out_text,
               size_t cap, int *out_rc)
{
    char c[16384];
    char *argv[4];
    struct jc_vec envv;
    struct jc_sb capture;
    int rc;
    size_t len;

    if (jc_snprintf(c, sizeof c, "%s", cmd) < 0 ||
        strlen(cmd) + strlen(r->s8) + 2 >= sizeof c ||
        jc_snprintf(c + strlen(c), sizeof c - strlen(c), " %s", r->s8) < 0) {
        return 0;
    }
    if (gf_subst_s(c, sizeof c, r->s8) < 0) {
        return 0;
    }
    argv[0] = (char *)"sh";
    argv[1] = (char *)"-c";
    argv[2] = c;
    argv[3] = NULL;
    /* An EMPTY env vec: jc_spawn scrubs the secret names and otherwise
     * inherits (the D11 shape; a NULL vec would inherit without the
     * built-in scrub, and the stage children above deliberately pass
     * NULL to keep the reference's env -- these are the reference's
     * own land.sh/repin.sh, not the stage bodies' children). */
    jc_vec_init(&envv, sizeof(char *));
    jc_sb_init(&capture);
    /* cap = out_text's size: the child's combined output is APPENDED up to
     * the cap (a cap of 0 would capture nothing -- M929's NULL-out is the
     * discard path; a bound of 16KB holds every stub and the host's
     * land.sh / repin.sh summaries). */
    rc = (int)jc_proc_capture(argv, &envv, NULL, &capture, (jc_size)cap,
                              0L, NULL);
    jc_vec_free(&envv);
    if (rc == -1 || rc == -2) {
        jc_sb_free(&capture);
        return 0;
    }
    len = capture.len;
    while (len > 0 && (capture.data[len - 1] == '\n' ||
                       capture.data[len - 1] == '\r')) {
        len--;
    }
    if (len > cap - 1) {
        len = cap - 1;
    }
    if (capture.data != NULL && len > 0) {
        memcpy(out_text, capture.data, len);
    }
    out_text[len] = '\0';
    jc_sb_free(&capture);
    *out_rc = rc;
    return 1;
}

/* The reference's :104 strip: `tail -1 | sed 's/^[^r]*repin: //' | cut
 * -c1-80`. The last line of `text` (NUL-terminated in `last`), with, from
 * its start, a PREFIX of non-`r` characters followed by the literal
 * `repin: ` removed when the line carries one (sed's `[^r]*` cannot span an
 * r, so the only candidate start is the FIRST r of the line -- a line with
 * no r, or whose first r does not open `repin: `, is untouched). cut 80
 * then trims the tail. */
static void
gf_repin_line(const char *text, char *out, size_t cap)
{
    char last[GF_LINE_MAX];
    const char *p;
    size_t i;
    size_t take;

    gf_last_line(text, last, sizeof last);
    out[0] = '\0';
    if (cap == 0) {
        return;
    }
    p = last;
    for (i = 0; p[i] != '\0' && p[i] != 'r'; i++) {
        ;
    }
    if (p[i] == 'r' && strncmp(p + i, "repin: ", 7) == 0) {
        i += 7;
    }
    take = strlen(p + i);
    if (take > cap - 1) {
        take = cap - 1;
    }
    memcpy(out, p + i, take);
    out[take] = '\0';
}

/* The reference's :102 hcn sed (sed -n, s-pipe-tail-1 shape): on the LAST
 * line of `text` that names `src/harness files changed`, take the digits
 * after the first `:` following the non-colon run (the pattern's
 * `[^:]*:`); 0 when absent or when that line names no digits. */
static int
gf_hcn_count(const char *text)
{
    const char *p = text;
    int hc = 0;

    if (text == NULL) {
        return 0;
    }
    while ((p = strstr(p, "src/harness files changed")) != NULL) {
        const char *c;
        int d = 0;
        int any = 0;

        c = p + strlen("src/harness files changed");
        while (*c != '\0' && *c != ':') {
            c++;
        }
        if (*c == ':') {
            c++;
            /* The reference's pattern is `[^:]*: <digit>`: the count
             * follows the colon + a SPACE (repin.sh:71 `changed $old..$s:
             * $hc`). Skip any whitespace, then the digits. */
            while (*c == ' ' || *c == '\t') {
                c++;
            }
            while (*c >= '0' && *c <= '9') {
                d = d * 10 + (*c - '0');
                any = 1;
                c++;
            }
            if (any) {
                hc = d;
            }
        }
        p++;
    }
    return hc;
}

/* Read a file into a caller-arena'd buffer (NUL-terminated). 1 on success
 * (a missing file is a FAILURE for the records -- the stage's rc then
 * decides: the D10 split reads the record only when the child ran). */
static int
gf_read_file(const char *path, char *out, size_t cap)
{
    FILE *f;
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) {
        out[0] = '\0';
        return 0;
    }
    n = fread(out, 1, cap - 1, f);
    (void)fclose(f);
    out[n] = '\0';
    return 1;
}

/* --- the D8 setup: the orchestrator's OWN keyless git children ------------ */

/* Append `text` verbatim to run.log (the reference's :90/:100
 * `printf '%s\n' "$lo" >> "$G/run.log"`): the land / repin children's
 * captured output lands in the run's log. Faults are NAMED (like the
 * say's), never fatal. */
static void
gf_log_append(const struct gf_run *r, const char *text)
{
    FILE *f;

    f = fopen(r->runlog, "ab");
    if (f == NULL) {
        fprintf(stderr, "jichi-harness: gate run: could not append to %s\n",
                r->runlog);
        return;
    }
    if (fputs(text, f) != EOF && fputc('\n', f) != EOF) {
        (void)fflush(f);
    }
    (void)fclose(f);
}

/* One git child in the push repo (the gl_git / gv_git shape): jc_hp_git_run
 * with no credential wiring -- the 16-name keyless env (the sweep idiom:
 * the child is a sh -c through jc_proc, so the keys are set EMPTY in the
 * child env; jc_hp_git_run adds the two GIT_CONFIG_=/dev/null slots). */
static int
gf_git(const struct gf_run *r, char *args[], int n_args, char *out, int cap)
{
    char envv[GF_NKEYS][128];
    char *cargv[8];
    char *cenv[GF_NKEYS + 4];
    char joined[GF_PATH_MAX + 512];
    int i, k, rc;

    for (i = 0; i < GF_NKEYS; i++) {
        if (jc_snprintf(envv[i], sizeof envv[i], "%s=", gf_keys[i]) < 0) {
            return -1;
        }
        cenv[i] = envv[i];
    }
    cenv[GF_NKEYS] = (char *)"GIT_CONFIG_GLOBAL=/dev/null";
    cenv[GF_NKEYS + 1] = (char *)"GIT_CONFIG_SYSTEM=/dev/null";
    cenv[GF_NKEYS + 2] = NULL;
    k = 0;
    cargv[k++] = (char *)"sh";
    cargv[k++] = (char *)"-c";
    /* `cd <repo> && git <args...>` in the repo: the reference's PLAIN git
     * argv with the sweep/ci idiom's 16-name SCRUB (D8). EVERY word is
     * single-quoted by gf_cmd_word (M1178): the words were pasted bare into
     * `sh -c`, so a <SHA8> carrying a `;` ran the rest of it on the host,
     * and a push repo path holding a `'` broke the `cd`. A truncation is
     * -1 here too (the bare paste checked only for an encoding error). */
    {
        int j;
        size_t used;
        int gn;

        gn = jc_snprintf(joined, sizeof joined, "cd");
        if (gn < 0 || (size_t)gn >= sizeof joined) {
            return -1;
        }
        if (gf_cmd_word(joined, sizeof joined, r->push_repo) != 0) {
            return -1;
        }
        used = strlen(joined);
        gn = jc_snprintf(joined + used, sizeof joined - used, " && git");
        if (gn < 0 || (size_t)gn >= sizeof joined - used) {
            return -1;
        }
        for (j = 0; j < n_args; j++) {
            if (gf_cmd_word(joined, sizeof joined, args[j]) != 0) {
                return -1;
            }
        }
        cargv[k++] = joined;
        cargv[k] = NULL;
    }
    {
        struct jc_vec envv_vec;
        struct jc_sb capture;
        int e;

        jc_vec_init(&envv_vec, sizeof(char *));
        for (i = 0; cenv[i] != NULL; i++) {
            e = (int)jc_vec_push(&envv_vec, &cenv[i]);
            if (e != 0) {
                jc_vec_free(&envv_vec);
                return -1;
            }
        }
        jc_sb_init(&capture);
        rc = (int)jc_proc_capture(cargv, &envv_vec, NULL, &capture,
                                  (jc_size)cap, 120L, NULL);
        jc_vec_free(&envv_vec);
        if (out != NULL && cap > 0) {
            int cn = (int)capture.len;

            if (cn > cap - 1) {
                cn = cap - 1;
            }
            if (capture.data != NULL && cn > 0) {
                memcpy(out, capture.data, (jc_size)cn);
            }
            out[cn] = '\0';
        }
        jc_sb_free(&capture);
    }
    /* Trim the git child's trailing newline (a rev-parse line). */
    if (out != NULL && cap > 0) {
        int ol = (int)strlen(out);

        while (ol > 0 && (out[ol - 1] == '\n' || out[ol - 1] == '\r')) {
            out[--ol] = '\0';
        }
    }
    (void)rc;
    return rc;
}

/* --- the D1/D2 stage children --------------------------------------------- */

/* Append a leading space + word, SINGLE-QUOTED (M1159 B-fix C1): the
 * stage child is handed to `sh -c`, so a pasted UNQUOTED value is re-parsed
 * by the shell -- a `--container-run` value whose text carries a `;` ends
 * the child's command and the rest runs on the host (supervisor-measured,
 * the G8 note read `INVALID (sweep) ... container
 * resolve_timeout_connect rc=0 5 ok, 0 not ok --push-repo /tmp/...`).
 * Single-quoting every word -- a literal `'` as `\'\'` -- makes the
 * shell hand each word back EXACTLY (the supervisor-verified variant).
 * -1 on a truncation (the append would lose bytes). */
static int
gf_cmd_word(char *cmd, size_t cap, const char *w)
{
    size_t base;
    size_t i;
    int gn;

    base = strlen(cmd);
    if (base + 1 >= cap) {
        return -1;
    }
    gn = jc_snprintf(cmd + base, cap - base, " '");
    if (gn < 0 || (size_t)gn > cap - base) {
        return -1;
    }
    base += (size_t)gn;
    for (i = 0; w[i] != '\0'; i++) {
        if (w[i] == '\'') {
            gn = jc_snprintf(cmd + base, cap - base, "'\\''");
        } else {
            gn = jc_snprintf(cmd + base, cap - base, "%c",
                             (int)(unsigned char)w[i]);
        }
        if (gn < 0 || (size_t)gn > cap - base) {
            return -1;
        }
        base += (size_t)gn;
    }
    gn = jc_snprintf(cmd + base, cap - base, "'");
    if (gn < 0 || (size_t)gn > cap - base) {
        return -1;
    }
    return 0;
}

/* Run one stage as its OWN dispatch form through a `sh -c` self-exec (D1):
 * `<self> gate run --config <cfg> --stage <name> <positionals>
 * <forwarded flags> > <record> 2>&1` -- the reference's exact shape, every
 * word single-quoted (C1, above). The child's combined output goes to the
 * record file (sh does the redirect); the child's rc is RETURNED (a value:
 * it feeds D10's table). The `<flags>` / `<values>` arrays are
 * NULL-terminated at the same index (a NULL value drops the flag, D2). 1 on
 * spawn success (rc in *out_rc), 0 on a spawn fault (the caller promotes
 * it). The cmd buffer is sized for the QUOTED worst case: every word is
 * wrapped in quotes, and a value's `'` triples, so the realistic ~1.4k
 * command stays far under the 3*GF_PATH_MAX + 8192 cap (the flags/values
 * are the operator's commands + the config's paths). */
static int
gf_stage_child(const struct gf_run *r, const char *self,
               const char *stage, const char *pos[], int n_pos,
               const char *flags[], const char *values[],
               const char *record_path, int *out_rc)
{
    char cmd[GF_PATH_MAX * 3 + 8192];
    char *cargv[4];
    struct jc_vec envv;
    struct jc_sb capture;
    int rc;
    int i;

    cmd[0] = '\0';
    if (gf_cmd_word(cmd, sizeof cmd, self) != 0 ||
        gf_cmd_word(cmd, sizeof cmd, "gate") != 0 ||
        gf_cmd_word(cmd, sizeof cmd, "run") != 0 ||
        gf_cmd_word(cmd, sizeof cmd, "--config") != 0 ||
        gf_cmd_word(cmd, sizeof cmd, r->cfg_path) != 0 ||
        gf_cmd_word(cmd, sizeof cmd, "--stage") != 0 ||
        gf_cmd_word(cmd, sizeof cmd, stage) != 0) {
        return 0;
    }
    for (i = 0; i < n_pos; i++) {
        if (gf_cmd_word(cmd, sizeof cmd, pos[i]) != 0) {
            return 0;
        }
    }
    for (i = 0; flags[i] != NULL; i++) {
        if (values[i] != NULL) {
            if (gf_cmd_word(cmd, sizeof cmd, flags[i]) != 0 ||
                gf_cmd_word(cmd, sizeof cmd, values[i]) != 0) {
                return 0;
            }
        }
    }
    /* The redirect tail: the record path is the orchestrator's OWN path
     * (built from gate-home + sha8), not operator text -- quoted as the
     * original did (a single-quoted `>` would become a literal argument
     * and the record would never be written). */
    {
        int gn = jc_snprintf(cmd + strlen(cmd), sizeof cmd - strlen(cmd),
                             " > '%s' 2>&1", record_path);

        if (gn < 0 || (size_t)gn > sizeof cmd - strlen(cmd)) {
            return 0;
        }
    }
    cargv[0] = (char *)"sh";
    cargv[1] = (char *)"-c";
    cargv[2] = cmd;
    cargv[3] = NULL;

    jc_vec_init(&envv, sizeof(char *));
    jc_sb_init(&capture);
    /* NO timeout (D14: the ci's 48 minutes are the point; the stage's OWN
     * children are capped where the reference caps them -- the sweep's
     * 600s driver-loop idiom is the stage's, not the orchestrator's). */
    rc = (int)jc_proc_capture(cargv, &envv, NULL, &capture, 0, 0L, NULL);
    (void)capture;
    jc_sb_free(&capture);
    jc_vec_free(&envv);
    *out_rc = rc;
    if (rc == -1) {
        /* spawn fault: treat as the stage's INVALID (rc 1, D10) */
        *out_rc = 1;
        return 0;
    }
    return 1;
}

/* The record's LAST non-empty line (NUL-terminated copy, cut). "" on
 * absence. (The reference's `tail -1` on a newline-terminated record is the
 * line before the final newline.) */
static void
gf_last_line(const char *text, char *out, size_t cap)
{
    const char *start;
    const char *end;
    const char *p;
    size_t take;

    out[0] = '\0';
    if (text == NULL || text[0] == '\0') {
        return;
    }
    /* M1159 B-fix C4: keep the line's END (the last non-newline byte, the
     * record's trailing newline trimmed) in `end`, then walk back to the
     * line's START and take end - start. The old code set start = p AFTER
     * the walk and computed p - start = 0, so EVERY say line was a bare
     * timestamp (the ledger say read "" although ledger.txt held
     * `ledger: 1 commit(s), 0 bad`). */
    end = text;
    while (*end != '\0') {
        end++;
    }
    while (end > text && (end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }
    p = end;
    while (p > text && p[-1] != '\n') {
        p--;
    }
    start = p;
    take = (size_t)(end - start);
    if (take >= cap) {
        take = cap - 1;
    }
    memcpy(out, start, take);
    out[take] = '\0';
}

/* Does `text` carry a `records:` line (D10's sweep split: rc 0 + NO
 * records line = a late fault -> invalid sweep)? */
static int
gf_has_records_line(const char *text)
{
    const char *p = text;

    if (p == NULL) {
        return 0;
    }
    while (*p != '\0') {
        if (strncmp(p, "records:", 8) == 0) {
            return 1;
        }
        p = strchr(p, '\n');
        if (p == NULL) {
            return 0;
        }
        p++;
    }
    return 0;
}

/* The gate-run.sh:59 grep set on sweep.txt: lines matching
 * `^  RED|^sweep:|rc=[1-9]|not ok|INVALID`, head -4, cut -c1-140,
 * newline->';' -- each line carries its own ';' INCLUDING the last
 * (the reference's `tr '\n' ';'` on a newline-terminated stream)
 * (the D4.4 join, the driver's G26 byte shape). */
static void
gf_sweep_join(const char *text, char *out, size_t cap)
{
    const char *p;
    size_t off;
    size_t len;
    int k = 0;

    out[0] = '\0';
    if (text == NULL) {
        return;
    }
    len = strlen(text);
    off = 0;
    while (off < len && k < 4) {
        size_t start = off;
        size_t end = start;
        int match = 0;

        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end > start) {
            const char *line = text + start;
            size_t ln = end - start;
            char lbuf[GF_LINE_MAX];

            if (ln >= sizeof lbuf) {
                ln = sizeof lbuf - 1;
            }
            memcpy(lbuf, line, ln);
            lbuf[ln] = '\0';
            (void)p;
            if (strncmp(lbuf, "  RED", 5) == 0 ||
                strncmp(lbuf, "sweep:", 6) == 0 ||
                strstr(lbuf, "rc=[1-9]") != NULL ||
                (lbuf[0] != ' ' && strstr(lbuf, "rc=") != NULL) ||
                strstr(lbuf, "not ok") != NULL ||
                strstr(lbuf, "INVALID") != NULL) {
                match = 1;
            }
            /* the real grep set: ^  RED | ^sweep: | rc=[1-9] | not ok |
             * INVALID. The rc=[1-9] class: an `rc=` followed by a digit
             * 1-9. */
            if (!match) {
                const char *rce = strstr(lbuf, "rc=");

                if (rce != NULL && rce[3] >= '1' && rce[3] <= '9') {
                    match = 1;
                }
            }
            if (match) {
                size_t take = ln < 140 ? ln : 140;
                char *q = out + strlen(out);
                int rem = (int)(cap - strlen(out));

                /* Each cut line appends its own ';' INCLUDING the last
                 * (the `tr '\n' ';'` shape): reserve one byte for it. */
                if (rem >= 2) {
                    if (take > (size_t)(rem - 2)) {
                        take = (size_t)(rem - 2);
                    }
                    if (take > 0) {
                        memcpy(q, lbuf, take);
                    }
                    q[take] = ';';
                    q[take + 1] = '\0';
                    k++;
                }
            }
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
}

/* The gate-run.sh:41 join on ledger.txt: the first three non-`ledger:`
 * lines, cut -c1-140, newline->';' -- each line carries its own ';'
 * INCLUDING the last (the reference's `tr '\n' ';'` on a
 * newline-terminated stream) (the D4.10 ledger reason's join, the
 * driver's G7 byte shape). */
static void
gf_ledger_join(const char *text, char *out, size_t cap)
{
    const char *p;
    size_t off;
    size_t len;
    int k = 0;

    out[0] = '\0';
    (void)p;
    if (text == NULL) {
        return;
    }
    len = strlen(text);
    off = 0;
    while (off < len && k < 3) {
        size_t start = off;
        size_t end = start;

        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end > start) {
            char lbuf[GF_LINE_MAX];
            size_t ln = end - start;

            if (ln >= sizeof lbuf) {
                ln = sizeof lbuf - 1;
            }
            memcpy(lbuf, text + start, ln);
            lbuf[ln] = '\0';
            if (strncmp(lbuf, "ledger:", 7) != 0) {
                size_t take = ln < 140 ? ln : 140;
                char *q = out + strlen(out);
                int rem = (int)(cap - strlen(out));

                /* Each cut line appends its own ';' INCLUDING the last
                 * (the `tr '\n' ';'` shape): reserve one byte for it. */
                if (rem >= 2) {
                    if (take > (size_t)(rem - 2)) {
                        take = (size_t)(rem - 2);
                    }
                    if (take > 0) {
                        memcpy(q, lbuf, take);
                    }
                    q[take] = ';';
                    q[take + 1] = '\0';
                    k++;
                }
            }
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
}

/* The gate-run.sh:79 grep set on the ci log: lines matching
 * `^smoke: FAILED --|^not ok|^\*\*\* \[|^ *FAIL `, head -4, cut -c1-160,
 * newline->';' -- each line carries its own ';' INCLUDING the last
 * (the reference's `tr '\n' ';'` on a newline-terminated stream)
 * (the D4.6 join, the driver's G15 byte shape). */
static void
gf_ci_join(const char *text, char *out, size_t cap)
{
    size_t off;
    size_t len;
    int k = 0;

    out[0] = '\0';
    if (text == NULL) {
        return;
    }
    len = strlen(text);
    off = 0;
    while (off < len && k < 4) {
        size_t start = off;
        size_t end = start;

        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end > start) {
            char lbuf[GF_LINE_MAX];
            size_t ln = end - start;
            int match = 0;

            if (ln >= sizeof lbuf) {
                ln = sizeof lbuf - 1;
            }
            memcpy(lbuf, text + start, ln);
            lbuf[ln] = '\0';
            if (strncmp(lbuf, "smoke: FAILED --", 16) == 0 ||
                strncmp(lbuf, "not ok", 6) == 0 ||
                strncmp(lbuf, "*** [", 4) == 0) {
                match = 1;
            }
            /* ^ *FAIL : optional leading spaces then FAIL */
            if (!match) {
                size_t i2 = 0;

                while (lbuf[i2] == ' ') {
                    i2++;
                }
                if (strncmp(lbuf + i2, "FAIL ", 5) == 0) {
                    match = 1;
                }
            }
            if (match) {
                size_t take = ln < 160 ? ln : 160;
                char *q = out + strlen(out);
                int rem = (int)(cap - strlen(out));

                /* Each cut line appends its own ';' INCLUDING the last
                 * (the `tr '\n' ';'` shape): reserve one byte for it. */
                if (rem >= 2) {
                    if (take > (size_t)(rem - 2)) {
                        take = (size_t)(rem - 2);
                    }
                    if (take > 0) {
                        memcpy(q, lbuf, take);
                    }
                    q[take] = ';';
                    q[take + 1] = '\0';
                    k++;
                }
            }
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
}

/* The green-land-refused arm's note (M1154 :261-285's named-refusal bytes,
 * the D4.8 / driver's G10 byte shape). */
static int
gf_terminal_refused(const struct gf_run *r)
{
    char noteline[GF_NOTE_MAX];

    if (gf_verdict(r, "green-land-refused") != 0) {
        return gf_die(r, "could not write the state file");
    }
    if (gf_say(r, "VERDICT green, the land step is absent") != 0) {
        return 1;
    }
    (void)jc_snprintf(noteline, sizeof noteline,
                      "GREEN on BATCH %s READY at %s, but no land step is "
                      "named (host.landRun absent: H10 is not built; the "
                      "reference's land.sh stands for the switch-over). "
                      "master is not moved.",
                      r->batch, r->s8);
    gf_note(r, noteline, NULL, "green-land-refused");
    return 1;
}

/* --- the entry ------------------------------------------------------------- */

int jc_hp_gate_full_run(const char *config_path, const char *sha8,
                        const char *batch,
                        const char *gate_home, const char *state,
                        const char *push_repo,
                        const char *ledger_address,
                        const char *ledger_accept,
                        const char *bin,
                        const char *build_run,
                        const char *mutant_run,
                        const char *unit_run_san,
                        const char *unit_run_o2,
                        const char *container_run,
                        const char *redfirst_run,
                        const char *redfirst_unit_run,
                        const char *records,
                        const char *ci_run, const char *ci_log_dir,
                        const char *ci_smoke_keep_going,
                        const char *land_run, const char *repin_run,
                        struct jc_arena *a)
{
    struct gf_run r;
    struct jc_harness_config cfg;
    int rc;
    char self[GF_PATH_MAX];
    int gn;

    memset(&r, 0, sizeof r);
    r.cfg_path = config_path;
    r.s8 = sha8;
    r.batch = (batch != NULL && batch[0] != '\0') ? batch : "?";
    r.push_repo[0] = '\0';
    r.gate_home[0] = '\0';
    r.ledger_addr[0] = '\0';
    r.a = a;

    /* <SHA8> at the door (M1178): it reaches `sh -c` command lines and the
     * record home's path, so only a commit token gets past here -- before
     * the config, the gate home or the state file is touched. */
    if (!gf_is_commit_token(sha8)) {
        fprintf(stderr, "jichi-harness: gate run: the <SHA8> must be "
                        "7-40 lowercase hex (got '%s')\n",
                sha8 != NULL ? sha8 : "");
        return 2;
    }

    /* The config (the dispatch's stage idiom: it loads once here; the
     * stage children load their OWN copy for their own defaults -- D1). */
    memset(&cfg, 0, sizeof cfg);
    rc = (int)jc_harness_config_load(config_path, &cfg, a);
    if (rc != JC_OK) {
        fprintf(stderr, "jichi-harness: gate run: %s\n",
                jc_harness_config_error(&cfg) != NULL
                    ? jc_harness_config_error(&cfg)
                    : "could not load the config");
        return 2;
    }

    /* The ledger address (b1's CHANGED REQUIRED): flag else config, a
     * usage refusal BEFORE any git read (G3). */
    {
        const char *addr = ledger_address;

        if ((addr == NULL || addr[0] == '\0') &&
            cfg.host_ledger_address != NULL) {
            addr = cfg.host_ledger_address;
        }
        if (addr == NULL || addr[0] == '\0') {
            fprintf(stderr,
                    "jichi-harness: gate run: --ledger-address <addr> is "
                    "required (or host.ledgerAddress in the config)\n");
            usage_refused_gate_run();
            return 2;
        }
        if (jc_snprintf(r.ledger_addr, sizeof r.ledger_addr, "%s", addr)
            < 0) {
            return gf_die(&r, "the ledger address is too long");
        }
    }

    if (ledger_accept != NULL && ledger_accept[0] != '\0') {
        gn = jc_snprintf(r.ledger_accept, sizeof r.ledger_accept, "%s",
                         ledger_accept);
        if (gn < 0 || (size_t)gn >= sizeof r.ledger_accept) {
            return gf_die(&r, "the ledger accept path is too long");
        }
    }

    /* The push repo (REQUIRED: flag else host.pushRepo). */
    {
        const char *pr = push_repo;

        if ((pr == NULL || pr[0] == '\0') && cfg.host_push_repo != NULL) {
            pr = cfg.host_push_repo;
        }
        if (pr == NULL || pr[0] == '\0') {
            fprintf(stderr,
                    "jichi-harness: gate run: --push-repo <path> is "
                    "required (or host.pushRepo in the config)\n");
            return 2;
        }
        gn = jc_snprintf(r.push_repo, sizeof r.push_repo, "%s", pr);
        if (gn < 0 || (size_t)gn >= sizeof r.push_repo) {
            return gf_die(&r, "the push repo path is too long");
        }
    }

    /* The gate home (D3: flag else host.gateHome else <dir>/gate, the
     * notify's existing default). */
    {
        const char *gh = gate_home;

        if ((gh == NULL || gh[0] == '\0') && cfg.host_gate_home != NULL) {
            gh = cfg.host_gate_home;
        }
        if (gh == NULL || gh[0] == '\0') {
            char dflt[GF_PATH_MAX];

            if (cfg.dir == NULL || cfg.dir[0] == '\0') {
                return gf_die(&r, "the gate home cannot be resolved "
                                  "(no --gate-home, no host.gateHome, no "
                                  "dir)");
            }
            gn = jc_snprintf(dflt, sizeof dflt, "%s/gate", cfg.dir);
            if (gn < 0 || (size_t)gn >= sizeof dflt) {
                return gf_die(&r, "the gate home path is too long");
            }
            gh = dflt;
        }
        gn = jc_snprintf(r.gate_home, sizeof r.gate_home, "%s", gh);
        if (gn < 0 || (size_t)gn >= sizeof r.gate_home) {
            return gf_die(&r, "the gate home path is too long");
        }
    }
    /* The state file (D6: flag else <gate-home>/state). */
    if (state != NULL && state[0] != '\0') {
        gn = jc_snprintf(r.state, sizeof r.state, "%s", state);
        if (gn < 0 || (size_t)gn >= sizeof r.state) {
            return gf_die(&r, "the state path is too long");
        }
    } else {
        gn = jc_snprintf(r.state, sizeof r.state, "%s/state", r.gate_home);
        if (gn < 0 || (size_t)gn >= sizeof r.state) {
            return gf_die(&r, "the state path is too long");
        }
    }
    /* G = <gate-home>/<s8> (mkdir -p) + the run.log. */
    gn = jc_snprintf(r.G, sizeof r.G, "%s/%s", r.gate_home, r.s8);
    if (gn < 0 || (size_t)gn >= sizeof r.G) {
        return gf_die(&r, "the record home is too long");
    }
    if (jc_mkdir_p(r.gate_home) != JC_OK || jc_mkdir_p(r.G) != JC_OK) {
        return gf_die(&r, "could not create the gate home / record home");
    }
    gn = jc_snprintf(r.runlog, sizeof r.runlog, "%s/run.log", r.G);
    if (gn < 0 || (size_t)gn >= sizeof r.runlog) {
        return gf_die(&r, "the run.log path is too long");
    }
    /* The ci log dir (flat, D3: <gate-home>/ci, the reference's $LOGDIR) --
     * the RESOLVED value forwarded to the ci child (the brief's G23 note). */
    if (ci_log_dir != NULL && ci_log_dir[0] != '\0') {
        r.ci_log_dir_flag = ci_log_dir;
        gn = jc_snprintf(r.ci_log_dir, sizeof r.ci_log_dir, "%s",
                         ci_log_dir);
    } else {
        r.ci_log_dir_flag = NULL;
        gn = jc_snprintf(r.ci_log_dir, sizeof r.ci_log_dir, "%s/ci",
                         r.gate_home);
    }
    if (gn < 0 || (size_t)gn >= sizeof r.ci_log_dir) {
        return gf_die(&r, "the ci log dir is too long");
    }

    /* The forwarded stage surfaces (D2: NULL = the child's own default). */
    r.bin = bin;
    r.build_run = build_run;
    r.mutant_run = mutant_run;
    r.unit_run_san = unit_run_san;
    r.unit_run_o2 = unit_run_o2;
    r.container_run = container_run;
    r.redfirst_run = redfirst_run;
    r.redfirst_unit_run = redfirst_unit_run;
    r.records = records;
    r.ci_run = ci_run;
    r.ci_smoke_keep_going = ci_smoke_keep_going;

    /* The HOLD arm is the reference's STEP 4 (gate-run.sh:84): it fires
     * AFTER the ci step, once ci is green -- the full pipeline (setup,
     * gated, ledger, sweep, verdict, redfirst, ci) runs first, and the
     * HOLD arm is the terminal's first check (D4.7, the brief's arm 1).
     * (Task C: the stub that fired here before the pipeline -- green-held
     * with no `gated` line -- is deleted.) */

    /* The D8 setup: the orchestrator's OWN keyless git children. */
    {
        char tmp[GF_PATH_MAX + 64];
        char *av[8];
        int n;

        rc = -1;
        n = 0;
        av[n++] = (char *)"rev-parse";
        av[n++] = (char *)"--verify";
        av[n++] = (char *)"-q";
        jc_snprintf(tmp, sizeof tmp, "%s^{commit}", r.s8);
        av[n++] = tmp;
        rc = gf_git(&r, av, n, r.full, (int)sizeof r.full);
        if (rc != 0) {
            return gf_red(&r, "setup",
                          "The commit is not in the host's push.git.");
        }
        n = 0;
        av[n++] = (char *)"rev-parse";
        av[n++] = (char *)"--verify";
        av[n++] = (char *)"-q";
        av[n++] = (char *)"refs/remotes/origin/master";
        rc = gf_git(&r, av, n, r.master, (int)sizeof r.master);
        if (rc != 0) {
            return gf_red(&r, "setup",
                          "push.git has no origin/master.");
        }
        n = 0;
        av[n++] = (char *)"merge-base";
        av[n++] = (char *)"--is-ancestor";
        av[n++] = r.master;
        av[n++] = r.full;
        rc = gf_git(&r, av, n, NULL, 0);
        if (rc != 0) {
            char reason[GF_TEXT_MAX];
            char m8[16];

            memcpy(m8, r.master, 8);
            m8[8] = '\0';
            jc_snprintf(reason, sizeof reason,
                        "It is not a fast-forward of master %s.", m8);
            return gf_red(&r, "not-ff", reason);
        }
    }

    /* D9: the `gated` word -- the FIRST line of a successful run. */
    if (gf_verdict(&r, "gated") != 0) {
        return gf_die(&r, "could not write the state file");
    }
    {
        char line[GF_TEXT_MAX];

        jc_snprintf(line, sizeof line,
                    "gate: BATCH %s at %s; master %s",
                    r.batch, r.full, r.master);
        if (gf_say(&r, line) != 0) {
            return 1;
        }
    }

    /* self (D1). */
    if (!gf_self_path(self, sizeof self)) {
        return gf_die(&r, "could not resolve the harness's own path");
    }

    /* 1. the ledger (the reference's step 1, gate-run.sh:39-41). */
    {
        const char *posp[3];
        const char *flags[4];
        const char *values[4];
        char rec[GF_PATH_MAX];
        char recbuf[GF_REC_MAX];
        int chrc = 0;

        posp[0] = r.push_repo;
        posp[1] = r.master;
        posp[2] = r.full;
        flags[0] = "--ledger-address";
        values[0] = r.ledger_addr;
        if (r.ledger_accept[0] != '\0') {
            flags[1] = "--ledger-accept";
            values[1] = r.ledger_accept;
            flags[2] = NULL;
            values[2] = NULL;
        } else {
            flags[1] = NULL;
            values[1] = NULL;
        }
        jc_snprintf(rec, sizeof rec, "%s/ledger.txt", r.G);
        if (!gf_stage_child(&r, self, "ledger", posp, 3, flags,
                            values, rec, &chrc) || chrc == -2) {
            chrc = chrc == -2 ? 1 : chrc;
        }
        /* rc 2 (a usage refusal of the child) -> invalid ledger (D10's
         * stage-child-rc-2 rule); rc 1 -> red ledger; rc 0 -> proceed. */
        if (chrc == 2) {
            (void)gf_read_file(rec, recbuf, sizeof recbuf);
            return gf_invalid(&r, "ledger", recbuf);
        }
        {
            char last[GF_TEXT_MAX];

            (void)gf_read_file(rec, recbuf, sizeof recbuf);
            gf_last_line(recbuf, last, sizeof last);
            if (gf_say(&r, last) != 0) {
                return 1;
            }
            if (chrc != 0) {
                char reason[GF_NOTE_MAX];
                char join[GF_TEXT_MAX];

                gf_ledger_join(recbuf, join, sizeof join);
                jc_snprintf(reason, sizeof reason,
                            "%s: commit identity or trailers broken "
                            "(author, committer and every Co-Authored-By on "
                            "the operator's address; Jichi-Build and "
                            "Jichi-Model present): %s",
                            last, join);
                return gf_red(&r, "ledger", reason);
            }
        }
    }

    /* n (gate-run.sh:42): `rev-list --count <master>..<full>` in the push
     * repo (a keyless git child, the D8 idiom) -- the GREEN note's input
     * (`<n> commits`), stored for the terminal (the brief's arm 1). The
     * reference runs it after the ledger; a fault is a value: n = 0. */
    {
        char *nav[5];
        char out[16];
        int nn = 0;
        char rng[96];
        int j = 0;

        nav[j++] = (char *)"rev-list";
        nav[j++] = (char *)"--count";
        jc_snprintf(rng, sizeof rng, "%s..%s", r.master, r.full);
        nav[j++] = rng;
        nav[j] = NULL;
        out[0] = '\0';
        if (gf_git(&r, nav, 3, out, (int)sizeof out) == 0) {
            /* Parse the count: a bare non-negative integer. A fault
             * (non-numeric / empty) is a value: n = 0. */
            int dig = 1;
            char *d;

            for (d = out; *d != '\0'; d++) {
                if (*d < '0' || *d > '9') {
                    dig = 0;
                    break;
                }
            }
            if (dig && out[0] != '\0') {
                nn = atoi(out);
            }
        }
        r.n = nn;
    }

    /* 2. the sweep (gate-run.sh:55-59) + the verdict. */
    {
        const char *posp[1];
        char work[GF_PATH_MAX];
        /* M1159 B-fix C2: the array is up to 8 flags + the NULL terminator
         * (flags[9]/values[9], NOT [8]: the old [8] overflowed at index 8,
         * the UBSan-measured index-8-out-of-bounds; on a plain build the
         * stray NULL landed on values[0] and DROPPED --work from the sweep
         * child's command). */
        const char *flags[9];
        const char *values[9];
        char rec[GF_PATH_MAX];
        char recbuf[GF_REC_MAX];
        int chrc = 0;
        int i;

        posp[0] = r.s8;
        jc_snprintf(work, sizeof work, "%s/sw", r.G);
        jc_snprintf(rec, sizeof rec, "%s/sweep.txt", r.G);
        gf_say(&r, "sweep: start");
        i = 0;
        flags[i] = "--work";
        values[i] = work;
        i++;
        /* D2: forward a flag ONLY when the user set it. */
        if (r.bin != NULL) {
            flags[i] = "--bin";
            values[i] = r.bin;
            i++;
        }
        if (r.build_run != NULL) {
            flags[i] = "--build-run";
            values[i] = r.build_run;
            i++;
        }
        if (r.mutant_run != NULL) {
            flags[i] = "--mutant-run";
            values[i] = r.mutant_run;
            i++;
        }
        if (r.unit_run_san != NULL) {
            flags[i] = "--unit-run-san";
            values[i] = r.unit_run_san;
            i++;
        }
        if (r.unit_run_o2 != NULL) {
            flags[i] = "--unit-run-o2";
            values[i] = r.unit_run_o2;
            i++;
        }
        if (r.container_run != NULL) {
            flags[i] = "--container-run";
            values[i] = r.container_run;
            i++;
        }
        {
            flags[i] = "--push-repo";
            values[i] = r.push_repo;
            i++;
        }
        flags[i] = NULL;
        values[i] = NULL;
        if (!gf_stage_child(&r, self, "sweep", posp, 1, flags,
                            values, rec, &chrc) || chrc == -2) {
            chrc = chrc == -2 ? 1 : chrc;
        }
        (void)gf_read_file(rec, recbuf, sizeof recbuf);
        /* D10: rc 1 (the early-fatal INVALID) OR rc 0 with NO records line
         * (a late fault) -> invalid sweep; rc 2 -> invalid sweep (the rule). */
        if (chrc == 1 || chrc == 2 ||
            (chrc == 0 && !gf_has_records_line(recbuf))) {
            return gf_invalid(&r, "sweep", recbuf);
        }
        /* The VERDICT child (on sweep.txt). */
        {
            const char *vpos[2];
            const char *vflags[2];
            const char *vvalues[2];
            char vrec[GF_PATH_MAX];
            char vbuf[GF_REC_MAX];
            int vrc = 0;

            vpos[0] = rec;
            vpos[1] = r.s8;
            jc_snprintf(vrec, sizeof vrec, "%s/verdict.txt", r.G);
            vflags[0] = "--push-repo";
            vvalues[0] = r.push_repo;
            vflags[1] = NULL;
            vvalues[1] = NULL;
            if (!gf_stage_child(&r, self, "verdict", vpos, 2, vflags,
                                vvalues, vrec, &vrc) || vrc == -2) {
                vrc = vrc == -2 ? 1 : vrc;
            }
            (void)gf_read_file(vrec, vbuf, sizeof vbuf);
            {
                char sv[GF_TEXT_MAX];

                gf_last_line(vbuf, sv, sizeof sv);
                if (vrc == 2) {
                    return gf_invalid(&r, "sweep", vbuf);
                }
                if (vrc == 0) {
                    /* green: echo green > <G>/<s8>/sweep.verdict;
                     * sweepline for C's GREEN note (the reference's :61). */
                    FILE *vf = fopen(vrec, "rb");
                    FILE *vout;
                    char vpath[GF_PATH_MAX];

                    (void)vf;
                    jc_snprintf(vpath, sizeof vpath, "%s/sweep.verdict",
                                r.G);
                    vout = fopen(vpath, "wb");
                    if (vout == NULL) {
                        return gf_die(&r, "could not write sweep.verdict");
                    }
                    if (fputs("green\n", vout) == EOF ||
                        fclose(vout) != 0) {
                        return gf_die(&r, "could not write sweep.verdict");
                    }
                    /* sweepline (gate-run.sh:61 `grep '^sweep: ran'
                     * sweep.txt`): the sweep record's count line -- the
                     * GREEN note's input (the brief's arm 1), stored for
                     * the terminal. The first such line (the reference's
                     * grep prints every one; the record carries one). */
                    {
                        const char *q = recbuf;

                        while (*q != '\0') {
                            if (strncmp(q, "sweep: ran", 10) == 0) {
                                const char *e = q;
                                size_t take;

                                while (*e != '\0' && *e != '\n') {
                                    e++;
                                }
                                take = (size_t)(e - q);
                                if (take >= sizeof r.sweepline) {
                                    take = sizeof r.sweepline - 1;
                                }
                                memcpy(r.sweepline, q, take);
                                r.sweepline[take] = '\0';
                                break;
                            }
                            q = strchr(q, '\n');
                            if (q == NULL) {
                                break;
                            }
                            q++;
                        }
                    }
                    {
                        char sayline[GF_TEXT_MAX];

                        /* gate-run.sh:58 `say "sweep: $sv"`: the verdict's
                         * own line (green or green (note...)), verbatim. */
                        jc_snprintf(sayline, sizeof sayline, "sweep: %s",
                                    sv);
                        if (gf_say(&r, sayline) != 0) {
                            return 1;
                        }
                    }
                } else {
                    /* rc 1 (or a fault): red sweep (D4.4, gate-run.sh:59).
                     * The say carries `sweep: <sv>` (the driver's G26 reads
                     * the reason from that say line); the note carries
                     * `The pre-gate sweep: <sv with its leading 'red: '
                     * removed>. Its lines: <join>`. */
                    char sayline[GF_TEXT_MAX];
                    char reason[GF_NOTE_MAX];
                    char why[GF_TEXT_MAX];
                    char join[GF_TEXT_MAX];

                    jc_snprintf(sayline, sizeof sayline, "sweep: %s", sv);
                    if (gf_say(&r, sayline) != 0) {
                        return 1;
                    }
                    why[0] = '\0';
                    if (strncmp(sv, "red:", 4) == 0) {
                        jc_snprintf(why, sizeof why, "%s", sv + 4);
                        if (why[0] == ' ') {
                            memmove(why, why + 1, strlen(why));
                        }
                    } else {
                        jc_snprintf(why, sizeof why, "%s", sv);
                    }
                    gf_sweep_join(recbuf, join, sizeof join);
                    jc_snprintf(reason, sizeof reason,
                                "The pre-gate sweep: %s. Its lines: %s",
                                why, join);
                    return gf_red(&r, "sweep", reason);
                }
            }
        }
    }

    /* 2b. the redfirst replay (gate-run.sh:64-68, informational). */
    {
        const char *posp[2];
        char work[GF_PATH_MAX];
        /* M1159 B-fix C2: up to 6 flags + the NULL terminator (flags[7]/
         * values[7], NOT [6] -- the redfirst half of the sweep's one-slot
         * overflow). */
        const char *flags[7];
        const char *values[7];
        char rec[GF_PATH_MAX];
        char recbuf[GF_REC_MAX];
        int chrc = 0;
        int i;

        posp[0] = r.master;
        posp[1] = r.full;
        jc_snprintf(work, sizeof work, "%s/rf", r.G);
        jc_snprintf(rec, sizeof rec, "%s/redfirst.txt", r.G);
        i = 0;
        flags[i] = "--work";
        values[i] = work;
        i++;
        flags[i] = "--out";
        values[i] = work;
        i++;
        if (r.records != NULL) {
            flags[i] = "--records";
            values[i] = r.records;
            i++;
        }
        if (r.redfirst_run != NULL) {
            flags[i] = "--redfirst-run";
            values[i] = r.redfirst_run;
            i++;
        }
        if (r.redfirst_unit_run != NULL) {
            flags[i] = "--redfirst-unit-run";
            values[i] = r.redfirst_unit_run;
            i++;
        }
        flags[i] = "--push-repo";
        values[i] = r.push_repo;
        i++;
        flags[i] = NULL;
        values[i] = NULL;
        if (!gf_stage_child(&r, self, "redfirst", posp, 2, flags,
                            values, rec, &chrc) || chrc == -2) {
            chrc = chrc == -2 ? 1 : chrc;
        }
        (void)gf_read_file(rec, recbuf, sizeof recbuf);
        if (chrc == 1 || chrc == 2) {
            return gf_invalid(&r, "redfirst", recbuf);
        }
        {
            char rf[GF_TEXT_MAX];
            char rfsay[GF_TEXT_MAX];
            char rfg[GF_TEXT_MAX];
            char rfnote[GF_NOTE_MAX];
            const char *p;
            size_t off;
            size_t len;

            /* rfnote's input (gate-run.sh:65 `rf=$(tail -1 redfirst.txt)`):
             * the record's LAST line -- in the port that is the
             * `records: .../rf/records` line the redfirst stage appends
             * (R7 pins it as the stage's tail). G9 and the driver's
             * _ggreenexp both recompute it as tail -1, so the note keeps
             * it verbatim. */
            gf_last_line(recbuf, rf, sizeof rf);
            /* The say (gate-run.sh:67 `say "$rf"`): the reference's
             * redfirst record ends with the summary, so its $rf IS the
             * summary -- the port's says the record's last `redfirst: `
             * line (G17 greps run.log for it), with the last line as the
             * fallback. */
            rfsay[0] = '\0';
            {
                const char *p2 = recbuf;

                while (*p2 != '\0') {
                    if (strncmp(p2, "redfirst: ", 10) == 0) {
                        const char *e = p2;
                        size_t take;

                        while (*e != '\0' && *e != '\n') {
                            e++;
                        }
                        take = (size_t)(e - p2);
                        if (take >= sizeof rfsay) {
                            take = sizeof rfsay - 1;
                        }
                        memcpy(rfsay, p2, take);
                        rfsay[take] = '\0';
                    }
                    p2 = strchr(p2, '\n');
                    if (p2 == NULL) {
                        break;
                    }
                    p2++;
                }
            }
            if (rfsay[0] == '\0') {
                memcpy(rfsay, rf, sizeof rf);
            }
            if (gf_say(&r, rfsay) != 0) {
                return 1;
            }
            /* rfg (gate-run.sh:66): the ^<sha8> (GREEN|NO SUMMARY|BUILD
             * FAILED) lines, cut -c1-120, tr '\n' ';' -- each line
             * carries its own ';' INCLUDING the last (the reference's
             * `tr '\n' ';'` on a newline-terminated stream). The
             * driver's _ggreenexp recomputes it from redfirst.txt, so
             * the byte shape is the reference's, joined in order. */
            len = strlen(recbuf);
            off = 0;
            rfg[0] = '\0';
            while (off < len) {
                size_t start = off;
                size_t end = start;
                char lbuf[GF_LINE_MAX];

                while (end < len && recbuf[end] != '\n') {
                    end++;
                }
                {
                    size_t ln = end - start;
                    int match = 0;

                    if (ln < 8 + 6 || ln >= sizeof lbuf) {
                        ln = (ln >= sizeof lbuf) ? sizeof lbuf - 1 : ln;
                    }
                    memcpy(lbuf, recbuf + start, ln);
                    lbuf[ln] = '\0';
                    if (ln >= 8) {
                        int hex8 = 1;
                        int j;

                        for (j = 0; j < 8; j++) {
                            char c = lbuf[j];
                            if (!((c >= '0' && c <= '9') ||
                                  (c >= 'a' && c <= 'f'))) {
                                hex8 = 0;
                                break;
                            }
                        }
                        if (hex8 && lbuf[8] == ' ') {
                            if (strncmp(lbuf + 9, "GREEN", 5) == 0 ||
                                strncmp(lbuf + 9, "NO SUMMARY", 10) == 0 ||
                                strncmp(lbuf + 9, "BUILD FAILED", 12) == 0) {
                                match = 1;
                            }
                        }
                    }
                    if (match) {
                        size_t take = ln < 120 ? ln : 120;
                        char *q = rfg + strlen(rfg);
                        size_t used = strlen(rfg);

                        /* Each cut line appends its own ';' INCLUDING the
                         * last (the `tr '\n' ';'` shape); rfg's cap
                         * leaves headroom for the note's tail. */
                        if (used + take + 2 <= sizeof rfg) {
                            memcpy(q, lbuf, take);
                            q[take] = ';';
                            q[take + 1] = '\0';
                        }
                    }
                }
                off = end;
                if (end < len) {
                    off++;
                }
            }
            (void)p;
            /* rfnote (gate-run.sh:68): $rf + the rfg half when rfg is
             * non-empty (the driver's _ggreenexp byte shape). */
            if (rfg[0] != '\0') {
                jc_snprintf(rfnote, sizeof rfnote,
                            "%s -- never observed failing at its own "
                            "commit, or did not build/finish there (a pin "
                            "of fixed behaviour, a build-only test fix, or "
                            "a test that cannot fire: say which in LOG-3): "
                            "%s",
                            rf, rfg);
            } else {
                jc_snprintf(rfnote, sizeof rfnote, "%s", rf);
            }
            r.rfnote[0] = '\0';
            memcpy(r.rfnote, rfnote, strlen(rfnote) < sizeof r.rfnote
                       ? strlen(rfnote) : sizeof r.rfnote - 1);
            /* rfnote (gate-run.sh:68) -- the GREEN note's input (the
             * brief's arm 1), stored for the terminal; the say above
             * already pinned the bare rf line (G17). */
        }
    }

    /* 3. make ci (gate-run.sh:71-81). */
    {
        const char *posp[1];
        char work[GF_PATH_MAX];
        const char *flags[7];
        const char *values[7];
        char rec[GF_PATH_MAX];
        char recbuf[GF_REC_MAX];
        int chrc = 0;
        int run_rc;
        int i;
        char logpath[GF_PATH_MAX];
        char logbuf[GF_REC_MAX];

        posp[0] = r.s8;
        jc_snprintf(work, sizeof work, "%s/ci", r.G);
        jc_snprintf(rec, sizeof rec, "%s/ci.txt", r.G);
        jc_snprintf(logpath, sizeof logpath, "%s/ci-%s.log", r.ci_log_dir,
                    r.s8);
        i = 0;
        flags[i] = "--work";
        values[i] = work;
        i++;
        if (r.records != NULL) {
            flags[i] = "--records";
            values[i] = r.records;
            i++;
        }
        if (r.ci_run != NULL) {
            flags[i] = "--ci-run";
            values[i] = r.ci_run;
            i++;
        }
        {
            /* The RESOLVED ci log dir (the brief's G23 note: always
             * forward it, so --gate-home lands the log flat under the
             * gate home). */
            flags[i] = "--ci-log-dir";
            values[i] = r.ci_log_dir;
            i++;
        }
        if (r.ci_smoke_keep_going != NULL) {
            flags[i] = "--ci-smoke-keep-going";
            values[i] = r.ci_smoke_keep_going;
            i++;
        }
        flags[i] = "--push-repo";
        values[i] = r.push_repo;
        i++;
        flags[i] = NULL;
        values[i] = NULL;
        gf_say(&r, "ci: start");
        if (!gf_stage_child(&r, self, "ci", posp, 1, flags, values,
                            rec, &chrc) || chrc == -2) {
            chrc = chrc == -2 ? 1 : chrc;
        }
        (void)gf_read_file(rec, recbuf, sizeof recbuf);
        (void)gf_read_file(logpath, logbuf, sizeof logbuf);
        /* okl (gate-run.sh:76): the log's LAST ^ci: OK line, and smokel
         * (:81) the last `smoke: OK` line -- both STORED in the run's
         * fields (the brief's GREEN-note inputs, the terminal's). */
        r.okl[0] = '\0';
        r.smokel[0] = '\0';
        {
            const char *p2 = logbuf;

            while (*p2 != '\0') {
                if (strncmp(p2, "ci: OK", 6) == 0) {
                    const char *e = p2;
                    size_t take;

                    while (*e != '\0' && *e != '\n') {
                        e++;
                    }
                    take = (size_t)(e - p2);
                    if (take >= sizeof r.okl) {
                        take = sizeof r.okl - 1;
                    }
                    memcpy(r.okl, p2, take);
                    r.okl[take] = '\0';
                }
                if (strncmp(p2, "smoke: OK", 9) == 0) {
                    const char *e = p2;
                    size_t take;

                    while (*e != '\0' && *e != '\n') {
                        e++;
                    }
                    take = (size_t)(e - p2);
                    if (take >= sizeof r.smokel) {
                        take = sizeof r.smokel - 1;
                    }
                    memcpy(r.smokel, p2, take);
                    r.smokel[take] = '\0';
                }
                p2 = strchr(p2, '\n');
                if (p2 == NULL) {
                    break;
                }
                p2++;
            }
        }
        /* The RUN rc (gate-run.sh:77's $cr): parsed from the record, NOT
         * chrc (the STAGE's rc, 0 for a completed-but-failing run). Last
         * `  ci: RED make ci rc=<r>.` line wins (the body's contract line,
         * the run's own rc), else the last `  ci: rc=<r>` line, else chrc
         * (the body died mid-run, its contract broke -- the only number we
         * have, so the say is unchanged from the chrc path). M1160-d. */
        {
            const char *p3;

            run_rc = chrc;
            p3 = recbuf;
            while (p3 != NULL) {
                if (memcmp(p3, "  ci: RED make ci rc=", 21) == 0) {
                    int rv = atoi(p3 + 21);

                    run_rc = rv;
                }
                if (memcmp(p3, "  ci: rc=", 9) == 0 &&
                    memcmp(p3, "  ci: RED", 9) != 0) {
                    int rv = atoi(p3 + 9);

                    run_rc = rv;
                }
                p3 = strchr(p3, '\n');
                if (p3 != NULL) {
                    p3++;
                }
            }
        }
        /* say (gate-run.sh:77): ci: rc=<cr> ${okl:-no 'ci: OK' line}. */
        {
            char sayline[GF_TEXT_MAX];

            if (r.okl[0] != '\0') {
                jc_snprintf(sayline, sizeof sayline, "ci: rc=%d %s",
                            run_rc, r.okl);
            } else {
                jc_snprintf(sayline, sizeof sayline,
                            "ci: rc=%d no 'ci: OK' line", run_rc);
            }
            if (gf_say(&r, sayline) != 0) {
                return 1;
            }
        }
        if (chrc == 1) {
            return gf_invalid(&r, "ci", recbuf);
        }
        if (chrc == 2) {
            return gf_invalid(&r, "ci", recbuf);
        }
        if (run_rc != 0 || r.okl[0] == '\0') {
            /* The body's `  ci: RED` line in the record: the run failed
             * (rc != 0 of the RUN, or no ci: OK) -- red ci (D4.6, G15):
             * make ci rc=<cr>. <:79 join> Log on the host: <log>. with the
             * EXCERPT ATTACH (the body wrote <ci-log-dir>/ci-<s8>.excerpt
             * on FAIL). M1160-d: the RUN rc (run_rc), not the STAGE rc
             * (chrc), matching gate-run.sh:78's `cr -ne 0 || -z okl`. */
            char reason[GF_NOTE_MAX];
            char join[GF_TEXT_MAX];
            char attach[GF_PATH_MAX];

            gf_ci_join(logbuf, join, sizeof join);
            jc_snprintf(reason, sizeof reason,
                        "make ci rc=%d. %s Log on the host: %s.",
                        run_rc, join, logpath);
            jc_snprintf(attach, sizeof attach, "%s/ci-%s.excerpt",
                        r.ci_log_dir, r.s8);
            {
                struct stat ast;
                int use_attach = (stat(attach, &ast) == 0);

                if (!use_attach) {
                    attach[0] = '\0';
                }
                if (gf_verdict(&r, "red-ci") != 0) {
                    return gf_die(&r, "could not write the state file");
                }
                {
                    char sayline[GF_TEXT_MAX];

                    jc_snprintf(sayline, sizeof sayline,
                                "VERDICT red (ci): %s", reason);
                    if (gf_say(&r, sayline) != 0) {
                        return 1;
                    }
                }
                {
                    char noteline[GF_NOTE_MAX];

                    jc_snprintf(noteline, sizeof noteline,
                                "RED (ci) on BATCH %s READY at %s. %s "
                                "Nothing was landed; master is unchanged. "
                                "Fix it, then write a new BATCH READY line.",
                                r.batch, r.s8, reason);
                    gf_note(&r, noteline,
                            attach[0] != '\0' ? attach : NULL, "red-ci");
                }
                return 1;
            }
        }
    }

    /* 4. Land, unless held (gate-run.sh:84-104, D4.7-9): the reference's
     * terminal, in its order -- the HOLD check (a pre-existing
     * <gate-home>/HOLD: green-held, exit 0, NO land or repin), then the
     * land step (a capture child; its output appended to run.log; rc != 0
     * -> green-land-failed exit 1; rc 0 -> landed), then the repin step
     * (a capture child; its rc NEVER fails the run: repin-rc<N>) and the
     * final note. The land / repin commands: flag else config (D4.8/9);
     * an ABSENT land step is the green-land-refused terminal (G10, the
     * Task B bytes, kept), and an ABSENT repin step is the NOT-re-pinned
     * half of the GREEN-and-LANDED note (D4.9's CHANGED, G14b). */
    {
        const char *land = land_run;
        const char *repin = repin_run;
        char hold[GF_PATH_MAX];
        struct stat hst;

        if ((land == NULL || land[0] == '\0') && cfg.host_land_run != NULL) {
            land = cfg.host_land_run;
        }
        if ((repin == NULL || repin[0] == '\0')
            && cfg.host_repin_run != NULL) {
            repin = cfg.host_repin_run;
        }
        if (land == NULL || land[0] == '\0') {
            land = NULL;
        }
        if (repin == NULL || repin[0] == '\0') {
            repin = NULL;
        }
        /* The HOLD arm (D4.7, the reference's step 4): fires AFTER the ci
         * step, once ci is green -- the full pipeline has run, so the
         * state carries `gated` first (G9) and sweep.verdict is written. */
        gn = jc_snprintf(hold, sizeof hold, "%s/HOLD", r.gate_home);
        if (gn >= 0 && (size_t)gn < sizeof hold &&
            stat(hold, &hst) == 0) {
            char sayline[GF_TEXT_MAX];
            char noteline[GF_NOTE_MAX];

            if (gf_verdict(&r, "green-held") != 0) {
                return gf_die(&r, "could not write the state file");
            }
            jc_snprintf(sayline, sizeof sayline, "VERDICT green, held (%s)",
                        hold);
            if (gf_say(&r, sayline) != 0) {
                return 1;
            }
            jc_snprintf(noteline, sizeof noteline,
                        "GREEN on BATCH %s READY at %s (%d commits; %s; %s; "
                        "%s; %s). Held for the operator (%s): master is not "
                        "moved yet. Go on with the next batch.",
                        r.batch, r.s8, r.n, r.sweepline, r.smokel, r.okl,
                        r.rfnote, hold);
            gf_note(&r, noteline, NULL, "green-held");
            return 0;
        }
        if (land == NULL) {
            /* The green-land-refused terminal (D4.8, G10): kept from
             * Task B -- the port will not guess a landing. */
            return gf_terminal_refused(&r);
        }
        /* The land step (D4.8): `lo=$(sh land.sh <s8> 2>&1)` -- a capture
         * child (its combined output AND rc), the output appended to
         * run.log (:90). */
        {
            char lo[GF_NOTE_MAX];
            int lr = 0;

            if (!gf_capture_cmd(&r, land, lo, sizeof lo, &lr)) {
                return gf_die(&r, "could not run the land step");
            }
            gf_log_append(&r, lo);
            if (lr != 0) {
                char last[GF_TEXT_MAX];
                char sayline[GF_TEXT_MAX];
                char noteline[GF_NOTE_MAX];

                if (gf_verdict(&r, "green-land-failed") != 0) {
                    return gf_die(&r, "could not write the state file");
                }
                jc_snprintf(sayline, sizeof sayline,
                            "VERDICT green, the landing FAILED rc=%d", lr);
                if (gf_say(&r, sayline) != 0) {
                    return 1;
                }
                gf_last_line(lo, last, sizeof last);
                jc_snprintf(noteline, sizeof noteline,
                            "GREEN on BATCH %s READY at %s, but the landing "
                            "failed (%.*s). The operator is told; master "
                            "may be unchanged. Go on with the next batch.",
                            r.batch, r.s8, 160, last);
                gf_note(&r, noteline, NULL, "green-land-failed");
                return 1;
            }
            if (gf_verdict(&r, "landed") != 0) {
                return gf_die(&r, "could not write the state file");
            }
            {
                char sayline[GF_TEXT_MAX];

                jc_snprintf(sayline, sizeof sayline,
                            "VERDICT green, LANDED: master is %s", r.s8);
                if (gf_say(&r, sayline) != 0) {
                    return 1;
                }
            }
            /* The repin step (D4.9): `ro=$(sh repin.sh <s8> 2>&1)` -- its
             * rc NEVER fails the run (b10, G14). */
            {
                char ro[GF_NOTE_MAX];
                int rr = 0;
                char word[64];

                if (repin == NULL) {
                    rr = 0;
                    ro[0] = '\0';
                } else if (!gf_capture_cmd(&r, repin, ro, sizeof ro,
                                           &rr)) {
                    return gf_die(&r, "could not run the repin step");
                }
                if (repin != NULL) {
                    gf_log_append(&r, ro);
                    jc_snprintf(word, sizeof word, "repin-rc%d", rr);
                    if (gf_verdict(&r, word) != 0) {
                        return gf_die(&r, "could not write the state file");
                    }
                }
                /* The final GREEN-and-LANDED note (:104): the five inputs
                 * + the repin's last line, sed-stripped (:104) -- or the
                 * NOT-re-pinned half when there was no repin step. */
                {
                    char rpline[256];
                    int hc;
                    char noteline[GF_NOTE_MAX];
                    int gn2;

                    rpline[0] = '\0';
                    if (repin != NULL) {
                        gf_repin_line(ro, rpline, sizeof rpline);
                    }
                    hc = gf_hcn_count(ro);
                    if (repin != NULL) {
                        /* The re-pinned half (:104): `bin/ re-pinned at
                         * <s> (<rpline>)` -- rpline is the repin's last
                         * line, sed-stripped of its prefix to `repin: `,
                         * cut 80 (the driver's G12: `done <s8>, doctors
                         * clean`). */
                        gn2 = jc_snprintf(noteline, sizeof noteline,
                                          "GREEN and LANDED: BATCH %s READY "
                                          "at %s is master now (%d commits; "
                                          "%s; %s; %s; %s). bin/ re-pinned "
                                          "at %s (%s); your NEXT run uses "
                                          "it. Branch the next batch from "
                                          "the team branch as before.",
                                          r.batch, r.s8, r.n, r.sweepline,
                                          r.smokel, r.okl, r.rfnote, r.s8,
                                          rpline);
                    } else {
                        /* The NOT-re-pinned half (D4.9's CHANGED, G14b):
                         * no repin step was run, so the same note with the
                         * re-pinned words replaced (no repin-rcN state
                         * word was written either). */
                        gn2 = jc_snprintf(noteline, sizeof noteline,
                                          "GREEN and LANDED: BATCH %s READY "
                                          "at %s is master now (%d commits; "
                                          "%s; %s; %s; %s). bin/ NOT "
                                          "re-pinned (host.repinRun absent: "
                                          "H11 is not built; the reference's "
                                          "repin.sh stands for the "
                                          "switch-over); your NEXT run uses "
                                          "it. Branch the next batch from "
                                          "the team branch as before.",
                                          r.batch, r.s8, r.n, r.sweepline,
                                          r.smokel, r.okl, r.rfnote);
                    }
                    if (gn2 < 0 || (size_t)gn2 >= sizeof noteline) {
                        return gf_die(&r, "the green note was truncated");
                    }
                    /* The <hcn> tail (:102-103): when the repin output
                     * names `src/harness files changed ...: N` with N > 0,
                     * append the restart warning (G13: `The landing
                     * changed 2 src/harness file(s): ...`). */
                    if (hc > 0) {
                        char tail[256];
                        int tlen;

                        tlen = jc_snprintf(tail, sizeof tail,
                                           " The landing changed %d "
                                           "src/harness file(s): the drive "
                                           "loop keeps its old harness code "
                                           "until the operator restarts it.",
                                           hc);
                        if (tlen < 0 ||
                            strlen(noteline) + (size_t)tlen + 1 >=
                                sizeof noteline) {
                            return gf_die(&r,
                                          "the green note was truncated");
                        }
                        memcpy(noteline + strlen(noteline), tail,
                               (size_t)tlen + 1);
                    }
                    gf_note(&r, noteline, NULL, "landed");
                    return 0;
                }
            }
        }
    }
}

