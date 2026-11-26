/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_land.c - `jichi-harness land` (M1202, H10, wave 1): the C89 port
 * of the 2026-10-27 hardening-host drive's land.sh (read line by line: :18 the
 * two redactor rules, :19 the die, :20-21 b1+b2, :22 the s8 re-derive,
 * :23-24 b3+b4, :25 b5, :26 b6, :27-28 b7, :29 b8, :30-31 b9, :32 the b9
 * refusal, :33 b10, :34 b11, :35-36 b12, :37 b14(i), :38 b14(ii), :39 b15,
 * :40 the b14 summary, :41 b15, :42 b16). The reference's every refusal
 * string is kept BYTE-FOR-BYTE; the usage refusal is rc 2, every policy
 * refusal rc 1, success rc 0 (b16). The port runs HOST-SIDE, like the
 * reference (its two pre-b2 refusals -- the harness-path readback and the
 * stored-URL readback -- are fail-closed additions the reference does not
 * carry).
 *
 * The git children run through jc_hp_git_run (the M1056 idiom: the M2 -c
 * neutralisation, the GIT_CONFIG_=/dev/null slots, the 120 s cap). The
 * credential-wired ones (b7 ls-remote, b9's fetch fallback, b11 push, b14's
 * push-repo fetch) rebuild the credential -c flags + identity env
 * IMMEDIATELY before the spawn (the records cred_child idiom), wired against
 * the config's gitTokenFile (D4: never a hand-exported token; the token is a
 * reference read at the moment git asks, through the port's OWN credential
 * helper). b14(ii) (the main checkout's `fetch -q origin master`) is a read
 * child (no credential wiring: it only refreshes a remote-tracking ref).
 *
 * The redactor (b13) applies land.sh:18's two substitutions -- (i) a
 * credential in a URL, `://user@` -> `://<r>@`; (ii) any 40+ char
 * alphanumeric_- run -> `<r>` -- to EVERY captured git output before it is
 * printed or appended to the run log (in memory, no sed subshell; CHANGED).
 * The refusals (b1-b9, b12) print clean on stderr; the git output is the
 * redacted channel (stdout + --land-log).
 *
 * The paths are ALL caller-named (the five flags --push-repo / --ci-log-dir /
 * --gate-home / --main-checkout / --land-log); the config names only the
 * gitTokenFile reference + the team-branch prefix (drive.branchPrefix). No
 * new config fields in this wave (wave 2 adds the orchestrator half, D1-D3).
 * Every path from the caller is non-NULL or the verb refuses BEFORE touching
 * git (b16's `set -u` C equivalent). */

#include "jc_harness_land.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>     /* readlink (/proc/self/exe, the harness's own path) */

#include "jc_harness_config.h"  /* the config load (gitTokenFile, the prefix) */
#include "jc_harness_push.h"    /* jc_hp_git_run / jc_hp_cred_flags / _env */
#include "jc_mem.h"             /* struct jc_arena, jc_arena_calloc */
#include "jc_platform.h"        /* jc_status, jc_size, jc_read_file, jc_file_size */
#include "jc_snprintf.h"        /* jc_snprintf, never sprintf */

#define LL_CAP_PATH  (4 * 1024)   /* a path the cells name + slack */
#define LL_CAP_LINE  4096         /* one captured git line / summary */
#define LL_CAP_SHA   128          /* a full 40-hex id + slack */
#define LL_CAP_SMALL 512          /* an m8 / m12 / short reading + slack */

/* The run log's open handle (NULL = --land-log absent: the append is a
 * no-op, the driver's fixture always names it). */
static FILE *g_ll_log = NULL;

/* land.sh:19 die(): `land: refused: <text>` on stderr, rc 1. */
static int
ll_refuse(const char *text)
{
    fprintf(stderr, "land: refused: %s\n", text);
    return 1;
}

/* Append `text` + newline to the run log (b11/b14's captured git output,
 * redacted; the b10/b14 summary lines). Never fatal (the driver's tooth is
 * the stdout + the re-read, not the log). The --land-log append is
 * deliberately UNBOUNDED per invocation: the reference appends land.sh's
 * full output to its log, uncapped, and the port keeps that (there is no
 * log-size cap). */
static void
ll_log(const char *text)
{
    if (g_ll_log != NULL) {
        if (text[0] != '\0') {
            fprintf(g_ll_log, "%s\n", text);
            fflush(g_ll_log);
        }
    }
}

/* The b13 redactor: land.sh:18's two sed -E rules, in memory. (i) a
 * credential in a URL (`://user@`, user an `[^@/ ]*` run) -> `://<r>@`;
 * (ii) any 40+ char [A-Za-z0-9_-] run -> `<r>`. Applied to every captured
 * git output line before print/append; the refusals never pass it (they
 * print clean). `src` is read-only; `dst` is the caller's buffer.
 *
 * The port buffers a clean run's bytes until the run ends (sed's
 * substitution consumes the whole match, so a run's fate is known only at
 * its end): a run of 39 chars or fewer flushes BYTE-UNCHANGED (the clean
 * output the F6 teeth cell guards); a run of 40+ flushes as the 3-char
 * `<r>` sentinel. A plain `://` not followed by a `[^@/ ]*@` credential is
 * a host, not a credential, and is emitted verbatim like any other byte. */
void
jc_harness_land_redact(const char *src, char *dst, size_t cap)
{
    const char *s = src != NULL ? src : "";
    char runbuf[64];   /* a run's bytes, flushed at the run's end */
    size_t rbi = 0;
    size_t di = 0;

    dst[0] = '\0';
    if (cap == 0) {
        return;
    }
#define ll_flush_run() do {                                       \
        const size_t _n = (rbi < sizeof runbuf) ? rbi : 39;       \
        if (rbi >= 40) {                                          \
            if (di + 3 >= cap) {                                  \
                di = cap - 1;                                     \
            } else {                                              \
                dst[di++] = '<';                                  \
                dst[di++] = 'r';                                  \
                dst[di++] = '>';                                  \
            }                                                     \
        } else if (di + _n >= cap) {                              \
            di = cap - 1;                                         \
        } else {                                                  \
            memcpy(dst + di, runbuf, _n);                         \
            di += _n;                                             \
        }                                                         \
        rbi = 0;                                                  \
    } while (0)
    for (; *s != '\0'; s++) {
        char c = *s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            /* Accumulate the run's bytes; the flush at the run's end
             * decides: < 40 byte-unchanged, 40+ the `<r>` sentinel. */
            if (rbi < sizeof runbuf) {
                runbuf[rbi] = c;
            }
            rbi++;
            continue;
        }
        if (rbi != 0) {
            ll_flush_run();
        }
        if (c == ':' && s[1] == '/' && s[2] == '/') {
            /* (i): `://` + a `[^@/ ]*` run + `@` -> `://<r>@` (a
             * credential in the URL). A `://` NOT followed by such an `@`
             * is a plain URL (host, not credential): emit `:` verbatim and
             * re-process `//` as ordinary bytes next. */
            const char *u = s + 3;
            while (*u != '\0' && *u != '@' && *u != '/' && *u != ' ' &&
                   *u != '\t' && *u != '\n' && *u != '\r') {
                u++;
            }
            if (*u == '@') {
                if (di + 7 >= cap) {
                    di = cap - 1;
                } else {
                    dst[di++] = ':';
                    dst[di++] = '/';
                    dst[di++] = '/';
                    dst[di++] = '<';
                    dst[di++] = 'r';
                    dst[di++] = '>';
                    dst[di++] = '@';
                }
                s = u;
                continue;
            }
        }
        if (di + 1 >= cap) {
            di = cap - 1;
        } else {
            dst[di++] = c;
        }
    }
    if (rbi != 0) {
        ll_flush_run();
    }
#undef ll_flush_run
    if (di >= cap) {
        di = cap - 1;
    }
    dst[di] = '\0';
}

/* The first line of a captured `ls-remote` output (the sha + tab + ref), or
 * "" when the output is empty / the ref absent. */
static void
ll_first_line(const char *out, char *dst, size_t cap)
{
    const char *p = out != NULL ? out : "";
    const char *nl;
    size_t n;
    dst[0] = '\0';
    if (cap == 0) {
        return;
    }
    nl = strchr(p, '\n');
    n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, p, n);
    dst[n] = '\0';
}

/* The last line of a captured text (trailing newlines / CRs stripped, `tail
 * -1`'s shape: a newline-terminated file's last line is its last CONTENT
 * line, not the empty string after the final \n), or "" when empty. */
static void
ll_last_line(const char *out, char *dst, size_t cap)
{
    const char *p = out != NULL ? out : "";
    const char *nl;
    size_t n, e;
    dst[0] = '\0';
    if (cap == 0) {
        return;
    }
    n = strlen(p);
    while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r')) {
        n--;
    }
    nl = p;
    for (e = 0; e < n; e++) {
        if (p[e] == '\n') {
            nl = p + e + 1;
        }
    }
    n = (size_t)(p + n - nl);   /* the last line, WITHIN the stripped window */
    if (n == 0) {
        return;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, nl, n);
    dst[n] = '\0';
}

/* The first field (up to the first space or tab) of a line, e.g. the sha of
 * an ls-remote line or the count of a rev-list --count line. */
static void
ll_first_field(const char *line, char *dst, size_t cap)
{
    size_t i;
    dst[0] = '\0';
    if (cap == 0 || line == NULL) {
        return;
    }
    i = 0;
    while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' &&
           line[i] != '\n' && line[i] != '\r') {
        if (i + 1 >= cap) {
            break;
        }
        dst[i] = line[i];
        i++;
    }
    dst[i] = '\0';
}

/* A6: the caller's `args[8]` bound, enforced fail-closed. 0 on success;
 * -1 (the message already printed once to stderr) when storing `item`
 * would exceed the 8-element array -- the verb turns it into rc 2 (an
 * internal error, never a policy refusal). */
static int
ll_arg(const char *args[], int *an, const char *item)
{
    if (*an >= 8) {
        fprintf(stderr, "land: internal: git argv overflow\n");
        return -1;
    }
    args[*an] = item;
    (*an)++;
    return 0;
}

/* A6: the an=0 reset of a fresh argv. The bound itself is enforced by
 * ll_arg at EVERY append (a store that would exceed args[8] refuses), so
 * an an=0 reset is safe: no path can build past args[8]. */
#define ll_argv() do { an = 0; } while (0)

/* A git read child in `workdir` (no credential wiring): jc_hp_git_run's
 * read form; returns its code, leaves the combined output in out. */
static int
ll_git_read(struct jc_arena *a, const char *workdir, const char *args[],
            int n_args, int max_out, char *out)
{
    return jc_hp_git_run(a, workdir, NULL, 0, NULL, 0, args, n_args,
                         max_out, out, NULL);
}

/* A credential-wired git child in `workdir` (b7 ls-remote, b9 fetch, b11
 * push, b14 push-repo fetch): the records cred_child idiom -- jc_hp_cred_
 * flags + jc_hp_cred_env rebuilt on the call's own storage IMMEDIATELY
 * before the spawn, the harness's own path (readlink /proc/self/exe) for
 * the helper, the remote's stored URL for the identity. Returns
 * jc_hp_git_run's code, leaves the combined output in out. */
static int
ll_git_cred(struct jc_arena *a, const char *workdir, const char *harness,
            const char *remote_url, const char *token_file,
            const char *args[], int n_args, int max_out, char *out)
{
    char *cflags[4];
    char env0[512];
    char env1[512];
    char env2[512];
    char *envp[4];
    const char *envc[4];
    int nc;
    int rc;

    nc = jc_hp_cred_flags(a, harness, cflags);
    if (nc < 3) {
        return -1;
    }
    env0[0] = '\0';
    env1[0] = '\0';
    env2[0] = '\0';
    envp[0] = env0;
    envp[1] = env1;
    envp[2] = env2;
    envp[3] = NULL;
    jc_hp_cred_env(token_file, NULL, remote_url, envp);
    if (env0[0] == '\0' || env1[0] == '\0' || env2[0] == '\0') {
        return -1;
    }
    envc[0] = env0;
    envc[1] = env1;
    envc[2] = env2;
    envc[3] = NULL;
    rc = jc_hp_git_run(a, workdir, cflags, 3, envc, 3, args, n_args,
                       max_out, out, NULL);
    return rc;
}

/* The harness's own executable path (readlink /proc/self/exe, the M1053
 * idiom): the credential helper's argv needs it. 1 on success, 0 on fail. */
static int
ll_self_exe(char *out, size_t cap)
{
    char buf[LL_CAP_PATH];
    ssize_t n;
    n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n < 0 || (size_t)n + 1 > cap) {
        return 0;
    }
    buf[n] = '\0';
    memcpy(out, buf, (size_t)n + 1);
    return 1;
}

/* Read the stored `remote.origin.url` of `workdir` (the credential identity
 * the helper must match; a read child, credential-less). */
static int
ll_stored_url(struct jc_arena *a, const char *workdir, char *out, size_t cap)
{
    const char *args[3];
    char raw[LL_CAP_SMALL];
    const char *sp;
    size_t n;
    int rc;
    /* `git remote get-url origin` prints the URL ALONE (the `remote get-url`
     * without a remote name prints `origin <url>` -- the LAST field is the
     * URL either way, so read the last field explicitly). */
    args[0] = (char *)"remote";
    args[1] = (char *)"get-url";
    args[2] = (char *)"origin";
    rc = ll_git_read(a, workdir, args, 3, LL_CAP_SMALL, raw);
    if (rc != 0 || raw[0] == '\0') {
        return 0;
    }
    sp = strrchr(raw, ' ');
    if (sp == NULL) {
        sp = raw;
    } else {
        sp++;
    }
    n = strlen(sp);
    while (n > 0 && (sp[n - 1] == '\n' || sp[n - 1] == '\r' ||
                     sp[n - 1] == ' ' || sp[n - 1] == '\t')) {
        n--;
    }
    if (n == 0 || n + 1 > cap) {
        return 0;
    }
    memcpy(out, sp, n);
    out[n] = '\0';
    return 1;
}

/* b3: the ci-<s8>.log.meta's LAST line must end in ` rc=0` (land.sh:23,
 * `tail -1 | grep -q ' rc=0$'`). The file's absence -> not ok (the refusal).
 *
 * tail -1 reads the file's LAST LINE: for a file ending in exactly one
 * '\n' that is the final content line; for a file ending in '\n\n' it is
 * the EMPTY line between the two newlines (a blank line is a line). The
 * port mirrors that: strip exactly one trailing '\n' (F3a), then take the
 * segment after the last remaining '\n' WITHOUT stripping anything else
 * from it, and require the segment to end in the 5 bytes ` rc=0`. */
static int
ll_ci_meta_ok(const char *ci_log_dir, const char *s8, struct jc_arena *a)
{
    char path[LL_CAP_PATH];
    char *raw = NULL;
    jc_size len = 0;
    char last[LL_CAP_SMALL];
    const char *nl;
    const char *start;
    size_t n;
    jc_size e;
    if (jc_snprintf(path, sizeof path, "%s/ci-%s.log.meta", ci_log_dir, s8)
        < 0 || jc_snprintf(path, sizeof path, "%s/ci-%s.log.meta",
                           ci_log_dir, s8) >= (int)sizeof path) {
        return 0;
    }
    if (jc_read_file(path, &raw, &len, a) != JC_OK) {
        return 0;
    }
    /* Strip exactly one trailing '\n' (tail -1 on a newline-terminated file
     * yields its last content line; on a '\n\n' tail it yields the blank
     * line, which the segment scan below reads as empty -> refuse). */
    if (len > 0 && raw[len - 1] == '\n') {
        len--;
    }
    last[0] = '\0';
    if (len == 0) {
        return 0;
    }
    nl = NULL;
    for (e = len; e > 0; e--) {
        if (raw[e - 1] == '\n') {
            nl = raw + e - 1;
            break;
        }
    }
    start = (nl == NULL) ? raw : nl + 1;
    n = (size_t)(raw + len - start);
    if (n >= sizeof last) {
        n = sizeof last - 1;
    }
    memcpy(last, start, n);
    last[n] = '\0';
    /* `tail -1 | grep -q ' rc=0$'`: the last line ENDS in ` rc=0` (the
     * orchestrator's byte shape `make ci rc=0` / ` rc=1`). */
    return (strlen(last) >= 5
            && strncmp(last + strlen(last) - 5, " rc=0", 5) == 0);
}

/* b4: the ci-<s8>.log must carry a `^ci: OK` line (land.sh:24,
 * `grep -q '^ci: OK'`). grep matches a FINAL line without a trailing
 * newline too, so the loop examines it (F3b: today's loop breaks when it
 * finds no further newline and skips that last line). */
static int
ll_ci_ok_line(const char *ci_log_dir, const char *s8, struct jc_arena *a)
{
    char path[LL_CAP_PATH];
    char *raw = NULL;
    jc_size len = 0;
    const char *p;
    const char *nl;
    int ok = 0;
    if (jc_snprintf(path, sizeof path, "%s/ci-%s.log", ci_log_dir, s8) < 0
        || jc_snprintf(path, sizeof path, "%s/ci-%s.log", ci_log_dir, s8)
           >= (int)sizeof path) {
        return 0;
    }
    if (jc_read_file(path, &raw, &len, a) != JC_OK) {
        return 0;
    }
    p = raw;
    while (*p != '\0') {
        nl = strchr(p, '\n');
        if (nl != NULL) {
            if ((nl - p) >= 6 && strncmp(p, "ci: OK", 6) == 0) {
                ok = 1;
                break;
            }
            p = nl + 1;
        } else {
            /* The final line, WITHOUT a trailing newline: grep matches it
             * (shorter than 6 bytes, it cannot start with `ci: OK`). */
            if (strlen(p) >= 6 && strncmp(p, "ci: OK", 6) == 0) {
                ok = 1;
            }
            break;
        }
    }
    return ok;
}

/* b5: the gate-home's <s8>/sweep.verdict must read EXACTLY `green` (the
 * shell's `$(cat ...)` strips TRAILING NEWLINES only -- F2: it does not
 * strip spaces, tabs or '\\r', so a verdict of `green ` (trailing space)
 * REFUSES while `green\n\n` still accepts). */
static int
ll_sweep_green(const char *gate_home, const char *s8, struct jc_arena *a)
{
    char path[LL_CAP_PATH];
    char *raw = NULL;
    jc_size len = 0;
    char v[LL_CAP_SMALL];
    jc_size i;
    jc_size k = 0;
    if (jc_snprintf(path, sizeof path, "%s/%s/sweep.verdict", gate_home, s8)
        < 0 || jc_snprintf(path, sizeof path, "%s/%s/sweep.verdict",
                           gate_home, s8) >= (int)sizeof path) {
        return 0;
    }
    if (jc_read_file(path, &raw, &len, a) != JC_OK) {
        return 0;
    }
    while (len > 0 && raw[len - 1] == '\n') {
        len--;   /* $(cat) removes trailing newlines only */
    }
    for (i = 0; i < len && k + 1 < sizeof v; i++) {
        v[k++] = raw[i];
    }
    v[k] = '\0';
    return (strcmp(v, "green") == 0);
}

/* jc_harness_land_run: the `land` verb (b1-b16). See jc_harness_land.h for
 * the rc contract. The arena `a` is the caller's (it outlives every child). */
int
jc_harness_land_run(const char *config_path,
                    const char *push_repo, const char *ci_log_dir,
                    const char *gate_home, const char *main_checkout,
                    const char *land_log,
                    const char *sha8, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    const char *harness;
    const char *remote_url;
    const char *token_file;
    const char *prefix;
    char s8[LL_CAP_SMALL];
    char full[LL_CAP_SHA];
    char out[LL_CAP_LINE];
    char red[LL_CAP_LINE];
    char first[LL_CAP_SMALL];
    char last[LL_CAP_SMALL];
    char team_ref[LL_CAP_PATH];
    char team_ref_c[LL_CAP_PATH];
    char m8[LL_CAP_SMALL];
    char m12[LL_CAP_SMALL];
    char p8[LL_CAP_SMALL];
    char j8[LL_CAP_SMALL];
    char count[LL_CAP_SMALL];
    char summary[LL_CAP_LINE];
    const char *args[8];
    int an = 0;
    int rc;
    int i;
    /* The harness's own path + the remote's stored URL, hoisted to the verb's
     * scope so `harness` / `remote_url` (pointers into them) are valid across
     * every git child (a nested-block copy would dangle once the block ends,
     * -Wdangling-pointer under WERROR=1). */
    char self[LL_CAP_PATH];
    char stored[LL_CAP_PATH];

    /* b1: one argument, the SHA8. The usage refusal (rc 2, the note's exact
     * text) -- a tool error is a value, never an abort (CHANGED). */
    if (sha8 == NULL || sha8[0] == '\0') {
        fprintf(stderr, "usage: jichi-harness land <SHA8>\n");
        return 2;
    }
    /* Every path from the caller is non-NULL or the verb refuses BEFORE
     * touching git (b16's `set -u` C equivalent). */
    if (config_path == NULL || push_repo == NULL || ci_log_dir == NULL ||
        gate_home == NULL || main_checkout == NULL) {
        fprintf(stderr, "jichi-harness: land: --push-repo, --ci-log-dir, "
                        "--gate-home and --main-checkout are required "
                        "(with --config)\n");
        return 2;
    }
    if (a == NULL) {
        return 2;
    }

    /* The config: the gitTokenFile reference (D4) + the team-branch prefix
     * (b6). A load refusal is rc 2 (the gate dispatch's idiom). */
    memset(&cfg, 0, sizeof cfg);
    rc = (int)jc_harness_config_load(config_path, &cfg, a);
    if (rc != JC_OK) {
        fprintf(stderr, "jichi-harness: land: %s\n",
                jc_harness_config_error(&cfg) != NULL
                    ? jc_harness_config_error(&cfg)
                    : "could not load the config");
        return 2;
    }
    token_file = (cfg.git_token_file != NULL) ? cfg.git_token_file : "";
    prefix = (cfg.drive_branch_prefix != NULL)
                 ? cfg.drive_branch_prefix
                 : "drive";

    /* The run log (b11/b14's captured output + the b10/b14 summary lines):
     * --land-log named -> open it (append); absent -> the append is a
     * no-op. A failed open is not fatal (the tooth is stdout + the
     * re-read). */
    if (land_log != NULL && land_log[0] != '\0') {
        g_ll_log = fopen(land_log, "ab");
    } else {
        g_ll_log = NULL;
    }

    /* The harness's own path (the credential helper's argv) + the remote's
     * stored URL (the credential identity), resolved BEFORE the children. */
    harness = NULL;
    if (ll_self_exe(self, sizeof self)) {
        harness = self;
    }
    if (harness == NULL) {
        ll_log("land: could not resolve the harness's own path (readlink "
               "/proc/self/exe failed); the credential-wired children cannot "
               "name the helper; refused");
        fprintf(stderr,
                "land: could not resolve the harness's own path (readlink "
                "/proc/self/exe failed); the credential-wired children "
                "cannot name the helper; refused\n");
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }
    remote_url = NULL;
    if (ll_stored_url(a, push_repo, stored, sizeof stored)) {
        remote_url = stored;
    }
    if (remote_url == NULL) {
        ll_log("land: could not read the push repo's stored "
               "remote.origin.url; the credential-wired children are "
               "refused; refused");
        fprintf(stderr,
                "land: could not read the push repo's stored "
                "remote.origin.url; the credential-wired children are "
                "refused; refused\n");
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }

    /* b2: the short sha is resolved in the push repository (a credential-
     * less READ child). The refusal names the RAW argument as passed
     * (F1: land.sh:21 dies with `$s` BEFORE the :22 s8 re-derive, so a
     * 40-hex argument is named in its 40 chars, not its first 8). */
    ll_argv();
    if (ll_arg(args, &an, (char *)"rev-parse") != 0 ||
        ll_arg(args, &an, (char *)"--verify") != 0 ||
        ll_arg(args, &an, (char *)"-q") != 0) {
        return 2;   /* internal: git argv overflow (the message is printed) */
    }
    jc_snprintf(team_ref, sizeof team_ref, "%s^{commit}", sha8);
    if (ll_arg(args, &an, team_ref) != 0) {
        return 2;
    }
    rc = ll_git_read(a, push_repo, args, an, LL_CAP_SHA, out);
    if (rc != 0 || out[0] == '\0') {
        char msg[544];   /* the raw argument (up to 512) + the fixed suffix */
        jc_snprintf(msg, sizeof msg, "%s is not in push.git", sha8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }
    ll_first_field(out, full, sizeof full);
    jc_snprintf(s8, sizeof s8, "%.8s", full);

    /* b3 + b4: the ci record (both BEFORE any network call, land.sh:23-24). */
    if (!ll_ci_meta_ok(ci_log_dir, s8, a)) {
        char msg[LL_CAP_SMALL];
        jc_snprintf(msg, sizeof msg, "no make ci record with rc=0 for %s", s8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }
    if (!ll_ci_ok_line(ci_log_dir, s8, a)) {
        char msg[LL_CAP_SMALL];
        jc_snprintf(msg, sizeof msg,
                    "no 'ci: OK' line in ci-%s.log", s8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }

    /* b5: the green sweep verdict (the gate home's <s8>/sweep.verdict). */
    if (!ll_sweep_green(gate_home, s8, a)) {
        char msg[LL_CAP_SMALL];
        jc_snprintf(msg, sizeof msg, "no green sweep verdict for %s", s8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }

    /* b6: the sha is on the remote's team branch (the config's
     * driveBranchPrefix + /main, a credential-less read child). */
    ll_argv();
    if (ll_arg(args, &an, (char *)"merge-base") != 0 ||
        ll_arg(args, &an, (char *)"--is-ancestor") != 0 ||
        ll_arg(args, &an, full) != 0) {
        return 2;
    }
    jc_snprintf(team_ref, sizeof team_ref,
                "refs/remotes/origin/%s/main", prefix);
    if (ll_arg(args, &an, team_ref) != 0) {
        return 2;
    }
    rc = ll_git_read(a, push_repo, args, an, LL_CAP_SMALL, out);
    if (rc != 0) {
        char msg[LL_CAP_SMALL];
        jc_snprintf(msg, sizeof msg,
                    "%s is not on the remote's team branch", s8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }

    /* b7: ls-remote reads master NOW (a CREDENTIAL-WIRED child: master on a
     * protected remote needs the token). */
    ll_argv();
    if (ll_arg(args, &an, (char *)"ls-remote") != 0 ||
        ll_arg(args, &an, (char *)"origin") != 0 ||
        ll_arg(args, &an, (char *)"refs/heads/master") != 0) {
        return 2;
    }
    rc = ll_git_cred(a, push_repo, harness, remote_url, token_file,
                     args, an, LL_CAP_LINE, out);
    ll_first_line(out, first, sizeof first);
    if (rc != 0 || first[0] == '\0') {
        ll_log("ls-remote could not read master");
        (void)ll_refuse("ls-remote could not read master");
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }
    /* The master sha (M) from the ls-remote line's first field. */
    ll_first_field(first, out, sizeof out);
    if (strcmp(out, full) == 0) {
        /* b8: master is already the sha (the idempotency guard). */
        char msg[LL_CAP_SMALL];
        jc_snprintf(msg, sizeof msg, "master is %s already", s8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }
    /* M = the master sha (out holds it now). */
    for (i = 0; i < 40 && out[i] != '\0'; i++) {
        team_ref_c[i] = out[i];
    }
    team_ref_c[i] = '\0';
    jc_snprintf(m8, sizeof m8, "%.8s", team_ref_c);
    jc_snprintf(m12, sizeof m12, "%.12s", team_ref_c);

    /* b9: the ff check as ls-remote reads it. (i) cat-file -e M^{commit}
     * (local, credential-less); on failure (ii) a CREDENTIAL-WIRED fetch
     * with the FORCED refspec (the moved master lands in the local ref the
     * ancestry test reads); (iii) merge-base --is-ancestor M full. */
    ll_argv();
    if (ll_arg(args, &an, (char *)"cat-file") != 0 ||
        ll_arg(args, &an, (char *)"-e") != 0) {
        return 2;
    }
    jc_snprintf(team_ref, sizeof team_ref, "%s^{commit}", team_ref_c);
    if (ll_arg(args, &an, team_ref) != 0) {
        return 2;
    }
    rc = ll_git_read(a, push_repo, args, an, 1, out);
    if (rc != 0) {
        ll_argv();
        if (ll_arg(args, &an, (char *)"fetch") != 0 ||
            ll_arg(args, &an, (char *)"-q") != 0 ||
            ll_arg(args, &an, (char *)"--no-tags") != 0 ||
            ll_arg(args, &an, (char *)"origin") != 0 ||
            ll_arg(args, &an,
                   (char *)"+refs/heads/master:refs/remotes/origin/master")
            != 0) {
            return 2;
        }
        rc = ll_git_cred(a, push_repo, harness, remote_url, token_file,
                         args, an, LL_CAP_LINE, out);
        if (out[0] != '\0') {
            jc_harness_land_redact(out, red, sizeof red);
            ll_log(red);
            /* The fetch's redacted output also goes to stdout (land.sh's
             * `2>&1 | red`); a non-zero fetch is not fatal (the ancestry
             * below is the verdict). */
        }
    }
    ll_argv();
    if (ll_arg(args, &an, (char *)"merge-base") != 0 ||
        ll_arg(args, &an, (char *)"--is-ancestor") != 0 ||
        ll_arg(args, &an, team_ref_c) != 0 ||
        ll_arg(args, &an, full) != 0) {
        return 2;
    }
    rc = ll_git_read(a, push_repo, args, an, 1, out);
    if (rc != 0) {
        char msg[LL_CAP_SMALL];
        jc_snprintf(msg, sizeof msg,
                    "%s is not a fast-forward of master %s (ls-remote)",
                    s8, m8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }

    /* b10: the success line (a read child: rev-list --count M..full).
     * A1: the shell's $(...) prints the EMPTY word when the count child
     * fails, so the line then carries "( commits)" -- the port keeps the
     * empty word (no "0" substitute). */
    ll_argv();
    if (ll_arg(args, &an, (char *)"rev-list") != 0 ||
        ll_arg(args, &an, (char *)"--count") != 0) {
        return 2;
    }
    jc_snprintf(team_ref, sizeof team_ref, "%s..%s", team_ref_c, full);
    if (ll_arg(args, &an, team_ref) != 0) {
        return 2;
    }
    rc = ll_git_read(a, push_repo, args, an, LL_CAP_SMALL, out);
    if (rc == 0 && out[0] != '\0') {
        ll_first_field(out, count, sizeof count);
    } else {
        count[0] = '\0';
    }
    jc_snprintf(summary, sizeof summary,
                "land: ls-remote master %s; %s is a fast-forward (%s "
                "commits)", m12, s8, count);
    printf("%s\n", summary);
    ll_log(summary);

    /* b11: the push -- NEVER --force, master only (a CREDENTIAL-WIRED child,
     * captured output redacted). The push's own exit status does NOT decide
     * the verdict (the re-read, b12, does); a push rc != 0 is reported as a
     * refusal with the redacted tail. */
    ll_argv();
    if (ll_arg(args, &an, (char *)"push") != 0 ||
        ll_arg(args, &an, (char *)"origin") != 0) {
        return 2;
    }
    jc_snprintf(team_ref, sizeof team_ref, "%s:refs/heads/master", full);
    if (ll_arg(args, &an, team_ref) != 0) {
        return 2;
    }
    rc = ll_git_cred(a, push_repo, harness, remote_url, token_file,
                     args, an, LL_CAP_LINE, out);
    if (out[0] != '\0') {
        jc_harness_land_redact(out, red, sizeof red);
        printf("%s\n", red);
        ll_log(red);
    }
    if (rc != 0) {
        /* The push refused: report the redacted tail as the refusal, and let
         * the re-read (b12) judge (it will not equal full). */
        ll_last_line(red, last, sizeof last);
        ll_log(last);
    }

    /* b12: the re-read after the push (ls-remote again; compare to full). */
    ll_argv();
    if (ll_arg(args, &an, (char *)"ls-remote") != 0 ||
        ll_arg(args, &an, (char *)"origin") != 0 ||
        ll_arg(args, &an, (char *)"refs/heads/master") != 0) {
        return 2;
    }
    rc = ll_git_cred(a, push_repo, harness, remote_url, token_file,
                     args, an, LL_CAP_LINE, out);
    ll_first_line(out, first, sizeof first);
    if (rc != 0 || first[0] == '\0') {
        char msg[LL_CAP_SMALL];
        jc_snprintf(msg, sizeof msg,
                    "after the push, ls-remote reads master %s, not %s",
                    "", s8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }
    ll_first_field(first, out, sizeof out);
    jc_snprintf(m12, sizeof m12, "%.12s", out);
    if (strcmp(out, full) != 0) {
        char msg[LL_CAP_SMALL];
        jc_snprintf(msg, sizeof msg,
                    "after the push, ls-remote reads master %s, not %s",
                    m12, s8);
        (void)ll_refuse(msg);
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }

    /* b14: the origin/master refresh, both clones. (i) re-fetch master in
     * the push repository (credential-wired, forced refspec, redacted);
     * (ii) fetch `origin master` in the MAIN checkout (a read child, no
     * credential flags -- it only refreshes a remote-tracking ref). */
    ll_argv();
    if (ll_arg(args, &an, (char *)"fetch") != 0 ||
        ll_arg(args, &an, (char *)"-q") != 0 ||
        ll_arg(args, &an, (char *)"--no-tags") != 0 ||
        ll_arg(args, &an, (char *)"origin") != 0 ||
        ll_arg(args, &an,
               (char *)"+refs/heads/master:refs/remotes/origin/master")
        != 0) {
        return 2;
    }
    rc = ll_git_cred(a, push_repo, harness, remote_url, token_file,
                     args, an, LL_CAP_LINE, out);
    if (out[0] != '\0') {
        jc_harness_land_redact(out, red, sizeof red);
        ll_log(red);
    }
    ll_argv();
    if (ll_arg(args, &an, (char *)"fetch") != 0 ||
        ll_arg(args, &an, (char *)"-q") != 0 ||
        ll_arg(args, &an, (char *)"origin") != 0 ||
        ll_arg(args, &an, (char *)"master") != 0) {
        return 2;
    }
    rc = ll_git_read(a, main_checkout, args, an, LL_CAP_LINE, out);
    if (out[0] != '\0') {
        jc_harness_land_redact(out, red, sizeof red);
        ll_log(red);
    }

    /* b14's summary line: the three readings. */
    ll_argv();
    if (ll_arg(args, &an, (char *)"rev-parse") != 0 ||
        ll_arg(args, &an, (char *)"--short=8") != 0 ||
        ll_arg(args, &an, (char *)"refs/remotes/origin/master") != 0) {
        return 2;
    }
    rc = ll_git_read(a, push_repo, args, an, LL_CAP_SMALL, out);
    p8[0] = '\0';
    if (rc == 0 && out[0] != '\0') {
        ll_first_field(out, p8, sizeof p8);
    }
    ll_argv();
    if (ll_arg(args, &an, (char *)"rev-parse") != 0 ||
        ll_arg(args, &an, (char *)"--verify") != 0 ||
        ll_arg(args, &an, (char *)"-q") != 0 ||
        ll_arg(args, &an, (char *)"refs/remotes/origin/master") != 0) {
        return 2;
    }
    rc = ll_git_read(a, main_checkout, args, an, LL_CAP_SHA, out);
    j8[0] = '\0';
    if (rc == 0 && out[0] != '\0') {
        ll_first_field(out, j8, sizeof j8);
        if (strlen(j8) > 8) {   /* land.sh:40's `printf '%.8s' "$jm"` */
            j8[8] = '\0';
        }
    }
    jc_snprintf(summary, sizeof summary,
                "land: master is %s (ls-remote); push.git origin/master %s; "
                "main checkout origin/master %s",
                m12, (p8[0] != '\0') ? p8 : "", (j8[0] != '\0') ? j8 : "");
    printf("%s\n", summary);
    ll_log(summary);

    /* b15: the refresh must land (the main checkout's origin/master reads
     * the full sha). */
    ll_argv();
    if (ll_arg(args, &an, (char *)"rev-parse") != 0 ||
        ll_arg(args, &an, (char *)"--verify") != 0 ||
        ll_arg(args, &an, (char *)"-q") != 0 ||
        ll_arg(args, &an, (char *)"refs/remotes/origin/master") != 0) {
        return 2;
    }
    rc = ll_git_read(a, main_checkout, args, an, LL_CAP_SHA, out);
    if (rc != 0 || out[0] == '\0') {
        printf("land: the main checkout's origin/master was not "
               "refreshed\n");
        ll_log("the main checkout's origin/master was not refreshed");
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }
    ll_first_field(out, first, sizeof first);
    if (strcmp(first, full) != 0) {
        printf("land: the main checkout's origin/master was not "
               "refreshed\n");
        ll_log("the main checkout's origin/master was not refreshed");
        if (g_ll_log != NULL) {
            fclose(g_ll_log);
            g_ll_log = NULL;
        }
        return 1;
    }

    /* b16: rc 0 only after b15 passes. */
    if (g_ll_log != NULL) {
        fclose(g_ll_log);
        g_ll_log = NULL;
    }
    return 0;
}
