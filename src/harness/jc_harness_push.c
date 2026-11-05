/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_push.c - `jichi-harness mirror push` (M1056, H3c): the C89 port of
 * the drive's mirror.sh mirror_push + pre-push-keyscan.sh + pre-push-sizecheck.sh.
 * The credential helper it registers is jc_harness_cred.c. See
 * jc_harness_push.h for the contract and the exit classes.
 *
 * THE MIRROR IS THE PUSH CLIENT. The pipeline, in order (the note's steps 1-4):
 *   1. the M1055 mirror-integrity precheck (five checks, git-free, exit 2
 *      naming the check) -- the SAME checks the fetch runs, not weakened;
 *      ALL FIVE (1, 2, 5, 4, 3) complete BEFORE any git child is spawned,
 *      so the stored-copy byte comparison (check 3) fires before the guard's
 *      get-url child (the note's step 1, "BEFORE any git call");
 *   2. the URL guard: the effective push URL (`git remote get-url --push
 *      origin`) must equal the stored copy's `remote.origin.url` byte-exactly
 *      (exit 2 naming both on a mismatch), BEFORE any ls-remote/push child;
 *      it is the FIRST git child of the pipeline. A mirror-local plant
 *      (set-url/pushurl) changes the LIVE config, so check 3 refuses it first
 *      and the guard is redundant-but-present for it; the guard's DISTINCT
 *      tooth is a plant check 3 cannot see -- a stored copy rewritten so the
 *      stored URL no longer equals the effective URL while live == stored
 *      (the driver's P2c cell exercises the guard with check 3 satisfied);
 *   3. the C key scan (fail-closed on a secret it cannot load or an object it
 *      cannot read; exit 1 naming a PATH, never a secret) over the exact range
 *      the push adds, then the C size check (8 MiB per file version, 20 MiB
 *      per push; exit 1 with the reference's report);
 *   4. the push by the stored absolute URL (never the `origin` name), never
 *      --force, never a master refspec, with the config-level isolation and the
 *      reset-then-set credential helper.
 *
 * The range the scans count (the note's behaviour 4, the 01:37 item 1
 * tracking-refs gap): every object reachable from a pushed tip and from NONE
 * of (a) the remote's CURRENT tips -- obtained by `git ls-remote <stored URL>`
 * (a push by URL never advances the mirror's refs/remotes/origin, so the
 * tracking refs are stale by construction and must NOT be the exclusion), or
 * (b) the mirror's tracking refs. A ref new to the remote counts everything,
 * so the first push scans the whole history.
 */

#include "jc_harness_push.h"

#include "jc_harness_config.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_proc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* getuid, readlink */
#include <dirent.h>
#include <sys/stat.h>

/* Paths hang off the config's dir / the mirror dir; give the joins room, the
 * same bound the fetch (jc_harness_mirror.c) and init use. */
#define PATH_CAP 4352

/* Capture caps. The key scan reads a blob RAW (git cat-file blob) and a blob
 * can be up to the per-file limit (and the reference's incident was 9.7 MB),
 * so the blob capture cap is generous; the object listing and ls-remote are
 * bounded by the history size. A cap here is a floor for the driver's
 * fixtures, not a measured fit (init's GIT_OUT_CAP, mirror's MIRROR_OUT_CAP). */
#define PUSH_OUT_CAP   (8 * 1024 * 1024)   /* rev-list / batch-check / ls-remote */
#define PUSH_SMALL_CAP (256 * 1024)        /* get-url / for-each-ref / cat-file -s */
#define PUSH_BLOB_CAP  (64 * 1024 * 1024)  /* git cat-file blob <sha> */
#define PUSH_TIMEOUT   120L

/* The first 200 bytes of a failed git child's output (M1053's git_must idiom). */
#define PUSH_ERR_CUT 200

/* The reference's measured limits (behaviour 7), in MiB (2^20). */
#define PUSH_MAX_FILE_MB 8UL
#define PUSH_MAX_TOTAL_MB 20UL

/* The minimum a secret must be, after whitespace trim (behaviour 3). */
#define SECRET_MIN_LEN 12

/* The max number of tips the port carries ($BR/main + the $BR/work
 * wildcard refs), and the max exclusion set it builds from the remote +
 * tracking tips. #ifndef-guarded so a measuring build (def-350's argv-
 * boundary cell) can lower them with -DPUSH_MAX_TIPS/-DPUSH_MAX_EXCL and
 * land list_pushed exactly at its an bound; every build that passes no
 * defines gets the 24/48 above, byte-identical. */
#ifndef PUSH_MAX_TIPS
#define PUSH_MAX_TIPS 24
#endif
#ifndef PUSH_MAX_EXCL
#define PUSH_MAX_EXCL 48
#endif

/* def-350: report the cap VALUES this build was compiled with. The argv-
 * boundary build (make smoke-push-boundary) lowers them to 1/1 and announces
 * them in `--version` via jc_harness_main.c's print_version; the driver's
 * gate 2 greps the values, not just the marker, so a hand-built
 * -DJC_PUSH_BOUNDARY at 24/48 cannot pass it. Every default build answers
 * 24/48 here and prints nothing (the caller is #ifdef'd on
 * JC_PUSH_BOUNDARY). */
void
jc_harness_push_caps(int *tips, int *excl)
{
    *tips = PUSH_MAX_TIPS;
    *excl = PUSH_MAX_EXCL;
}

/* ---------------------------------------------------------------------------
 * Duplicated from jc_harness_mirror.c (M1055), where these are STATIC and so
 * NOT exported (the fetch file's header declares only the two command entry
 * points, no precheck/git helper). Per the task: the push must run the SAME
 * mirror-integrity precheck and must not weaken it; rather than refactor the
 * fetch file, the minimum (the five checks, the stored-copy path, the config
 * load, the empty env files, the refusal helpers) is duplicated here, kept in
 * behaviour with the fetch. If M1055's precheck changes, this copy must
 * follow.
 * ------------------------------------------------------------------------- */

static void
refuse(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: mirror: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

static void
say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("mirror push: ", stdout);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
    fflush(stdout);
}

static void
cut_copy(char *dst, size_t cap, const char *s)
{
    size_t n;
    if (dst == NULL || cap == 0) {
        return;
    }
    if (s == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(s);
    if (n > (size_t)PUSH_ERR_CUT) {
        n = (size_t)PUSH_ERR_CUT;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static int
load_cfg(const char *config_path, struct jc_arena *a,
         struct jc_harness_config *cfg)
{
    jc_status st;
    if (config_path == NULL) {
        refuse("needs --config <file> (the harness configuration)");
        return 2;
    }
    st = jc_harness_config_load(config_path, cfg, a);
    if (st != JC_OK) {
        const char *msg = jc_harness_config_error(cfg);
        refuse("%s", (msg != NULL) ? msg : "could not read the harness config");
        return 2;
    }
    if (cfg->drive_branch_prefix == NULL || cfg->drive_branch_prefix[0] == '\0') {
        refuse("the config has no drive.branchPrefix (the refset's $BR); "
               "push cannot name the team's branches");
        return 2;
    }
    return 0;
}

/* Build <mirror's parent>/mirror.git-config.harness-copy into dst (the exact
 * stored-copy path the note names, beside the mirror, in the harness's dir).
 * Returns 0 on a path that is too long. (Duplicated from the fetch.) */
static int
build_stored_copy(const char *mirror, const char *base_hint,
                  char *dst, size_t cap)
{
    const char *slash;
    (void)base_hint;
    slash = strrchr(mirror, '/');
    if (slash != NULL && slash != mirror) {
        size_t blen = (size_t)(slash - mirror);
        char tmp[PATH_CAP];
        if (blen + 40 >= sizeof tmp) {
            return 0;
        }
        memcpy(tmp, mirror, blen);
        tmp[blen] = '\0';
        jc_snprintf(dst, cap, "%s/mirror.git-config.harness-copy", tmp);
    } else if (base_hint != NULL && base_hint[0] != '\0') {
        jc_snprintf(dst, cap, "%s/mirror.git-config.harness-copy", base_hint);
    } else {
        jc_snprintf(dst, cap, "%s", "mirror.git-config.harness-copy");
    }
    return 1;
}

/* Check 1 (git-free): the mirror's HEAD file exists and is a symref. */
static int
check_bare(const char *mirror, struct jc_arena *a)
{
    char path[PATH_CAP];
    char *text = NULL;
    jc_size len = 0;
    const char *p;
    jc_status st;
    int ok;

    jc_snprintf(path, sizeof path, "%s/HEAD", mirror);
    st = jc_read_file(path, &text, &len, a);
    if (st != JC_OK) {
        refuse("check 1 (bare): the mirror's HEAD file could not be read "
               "at %s -- a bare repository the harness wrote has one; the "
               "mirror was not created by `jichi-harness init`, or it is "
               "not a bare repository", path);
        return 2;
    }
    p = text;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    ok = strncmp(p, "ref: ", 5) == 0;
    if (!ok) {
        refuse("check 1 (bare): the mirror's HEAD is not a symref (a "
               "`ref: ...` line) -- a bare repository the harness wrote has "
               "one");
        return 2;
    }
    return 0;
}

/* Check 2 (git-free): the mirror directory is owned by the harness's uid. */
static int
check_ownership(const char *mirror)
{
    struct stat st;
    if (stat(mirror, &st) != 0) {
        refuse("check 2 (ownership): the mirror directory could not be "
               "stat'ed at %s", mirror);
        return 2;
    }
    if (!S_ISDIR(st.st_mode)) {
        refuse("check 2 (ownership): %s is not a directory -- a mirror is "
               "a bare repository directory", mirror);
        return 2;
    }
    if (st.st_uid != getuid()) {
        refuse("check 2 (ownership): the mirror is owned by uid %ld, not "
               "the harness's uid %ld -- a mirror the harness wrote is "
               "owned by it", (long)st.st_uid, (long)getuid());
        return 2;
    }
    return 0;
}

/* Check 3 (git-free): the mirror's config is byte-identical to the stored
 * copy. A missing stored copy means check 3 cannot run: refused. */
static int
check_config_bytes(const char *mirror, const char *stored_copy,
                   struct jc_arena *a)
{
    char path[PATH_CAP];
    char *live = NULL;
    char *stored = NULL;
    jc_size live_len = 0;
    jc_size stored_len = 0;
    jc_status st;

    jc_snprintf(path, sizeof path, "%s/config", mirror);
    st = jc_read_file(path, &live, &live_len, a);
    if (st != JC_OK) {
        refuse("check 3 (config bytes): the mirror's config could not be "
               "read at %s", path);
        return 2;
    }
    st = jc_read_file(stored_copy, &stored, &stored_len, a);
    if (st != JC_OK) {
        refuse("check 3 (config bytes): the harness's stored copy of the "
               "mirror config is missing at %s -- the mirror was not "
               "created by `jichi-harness init` (init stores the bytes it "
               "wrote), so check 3 cannot run; re-run init, or the mirror "
               "is not one this harness owns", stored_copy);
        return 2;
    }
    if (live_len != stored_len ||
        (live_len != 0 && memcmp(live, stored, live_len) != 0)) {
        refuse("check 3 (config bytes): the mirror's config DIFFERS from "
               "the copy the harness wrote (live %lu bytes vs stored %lu "
               "bytes) -- a mirror config is not something the agent "
               "re-writes; treat the mirror as altered",
               (unsigned long)live_len, (unsigned long)stored_len);
        return 2;
    }
    return 0;
}

/* Check 5 (git-free): no `objects/info/alternates` under the mirror. */
static int
check_no_alternates(const char *mirror)
{
    char path[PATH_CAP];
    jc_snprintf(path, sizeof path, "%s/objects/info/alternates", mirror);
    if (jc_file_exists(path)) {
        refuse("check 5 (alternates): the mirror has an "
               "`objects/info/alternates` file at %s -- a mirror the "
               "harness wrote has none; the push would run "
               "`core.alternateRefsCommand` over them, so it is refused",
               path);
        return 2;
    }
    return 0;
}

/* Check 4 (git-free): `hooks/` holds nothing the harness did not tolerate
 * (git's own `*.sample` templates are tolerated; any other regular file is a
 * plant and is refused). */
static int
check_hooks(const char *mirror)
{
    char path[PATH_CAP];
    DIR *d;
    struct dirent *e;
    struct stat st;

    jc_snprintf(path, sizeof path, "%s/hooks", mirror);
    d = opendir(path);
    if (d == NULL) {
        refuse("check 4 (hooks): the mirror has no hooks/ directory at %s "
               "-- a bare repository git wrote has one", path);
        return 2;
    }
    for (e = readdir(d); e != NULL; e = readdir(d)) {
        const char *name = e->d_name;
        const char *dot;
        char full[PATH_CAP];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        jc_snprintf(full, sizeof full, "%s/%s", path, name);
        if (stat(full, &st) != 0) {
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        dot = strrchr(name, '.');
        if (dot != NULL && strcmp(dot, ".sample") == 0) {
            continue;
        }
        /* refuse() BEFORE closedir(d) (SV:1200.1): name = e->d_name lives in
         * the DIR buffer; closing first read the freed buffer in the report
         * (ASan: heap-use-after-free READ). */
        refuse("check 4 (hooks): the mirror's hooks/ holds a regular "
               "file %s/%s that the harness did not put there -- init lays "
               "the mirror out without arming it, so a hook there is a "
               "plant", mirror, name);
        closedir(d);
        return 2;
    }
    closedir(d);
    return 0;
}

/* The five M1055 mirror-integrity checks are called individually by the
 * entry point: ALL FIVE (1, 2, 5, 4, 3) run first (git-free) BEFORE any git
 * child is spawned (the note's step 1, "BEFORE any git call"), then the URL
 * guard (the FIRST git child, its get-url) -- so a mirror-local
 * set-url/pushurl plant is refused by check 3 on config bytes before the
 * guard's child runs (the guard is redundant-but-present for it), and the
 * guard's own distinct tooth is a plant check 3 cannot see: a stored copy
 * rewritten so the stored URL no longer equals the effective URL while live
 * == stored bytes (the driver's P2c cell measures the guard with check 3
 * satisfied). The checks themselves are the fetch's, duplicated and not
 * weakened. */

/* Two empty files the harness owns, in the harness's dir, for
 * GIT_CONFIG_GLOBAL / GIT_CONFIG_SYSTEM (behaviour 8's isolation). The
 * g_out/s_out buffers receive the "KEY=PATH" env-slot strings. */
static int
make_empty_env_files(const char *base, struct jc_arena *a,
                     char *g_out, size_t g_cap, char *s_out, size_t s_cap)
{
    char gpath[PATH_CAP];
    char spath[PATH_CAP];
    const char zero[] = "";
    (void)a;
    if (jc_snprintf(gpath, sizeof gpath, "%s/.harness-empty-global", base)
        >= (int)sizeof gpath) {
        return 0;
    }
    if (jc_snprintf(spath, sizeof spath, "%s/.harness-empty-system", base)
        >= (int)sizeof spath) {
        return 0;
    }
    if (jc_write_file(gpath, zero, 0) != JC_OK) {
        return 0;
    }
    if (jc_write_file(spath, zero, 0) != JC_OK) {
        return 0;
    }
    if (jc_snprintf(g_out, g_cap, "GIT_CONFIG_GLOBAL=%s", gpath)
        >= (int)g_cap) {
        return 0;
    }
    if (jc_snprintf(s_out, s_cap, "GIT_CONFIG_SYSTEM=%s", spath)
        >= (int)s_cap) {
        return 0;
    }
    return 1;
}

/* End of the duplicated fetch minimum. */

/* ---------------------------------------------------------------------------
 * The git children, the scan, and the pipeline.
 * ------------------------------------------------------------------------- */

/* The harness's own executable path (readlink /proc/self/exe, the M1053
 * idiom). Returns 1 on success, 0 on failure. */
static int
self_exe(char *out, size_t cap)
{
    char buf[PATH_CAP];
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

/* One git child in the mirror: the M2 neutralisation (fsmonitor, gpgsign,
 * hooksPath, alternateRefsCommand) + the `-c <flag>` of `extra_c` (in order)
 * + -C <mirror> + the subcommand of `args`. The env carries the two empty
 * GIT_CONFIG_* files plus the `extra_env` slots (in order). Returns
 * jc_proc_capture's code: the child's exit status, -1 on spawn failure, -2 on
 * timeout. The output is appended to `out` (bounded by `cap`). */
static int
git_child(struct jc_sb *out, const char *mirror,
          const char *e_global, const char *e_system,
          char **extra_c, int n_extra_c,
          char **extra_env, int n_extra_env,
          const char *const args[], int nargs,
          const char *stdin_data, jc_size cap)
{
    char *argv[64];
    struct jc_vec envv;
    int k = 0;
    int i;
    int j;

    if (nargs > 40 || n_extra_c > 10) {
        return -1;
    }
    argv[k++] = (char *)"git";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.fsmonitor=";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"log.showSignature=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"commit.gpgsign=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.hooksPath=/dev/null";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.alternateRefsCommand=";
    for (i = 0; i < n_extra_c; i++) {
        argv[k++] = (char *)"-c";
        argv[k++] = extra_c[i];
    }
    argv[k++] = (char *)"-C";
    argv[k++] = (char *)mirror;
    for (i = 0; i < nargs; i++) {
        argv[k++] = (char *)args[i];
    }
    argv[k] = NULL;

    jc_vec_init(&envv, sizeof(char *));
    if (jc_vec_push(&envv, &e_global) != JC_OK ||
        jc_vec_push(&envv, &e_system) != JC_OK) {
        jc_vec_free(&envv);
        return -1;
    }
    for (j = 0; j < n_extra_env; j++) {
        if (jc_vec_push(&envv, &extra_env[j]) != JC_OK) {
            jc_vec_free(&envv);
            return -1;
        }
    }
    i = (int)jc_proc_capture(argv, &envv, stdin_data, out, cap,
                             PUSH_TIMEOUT, NULL);
    jc_vec_free(&envv);
    return i;
}

/* A git child run by the mirror (get-url, ls-remote, rev-list, cat-file,
 * for-each-ref): no credential flags on the read children. */
static int
git_m2(struct jc_sb *out, const char *mirror, const char *e_global,
       const char *e_system, const char *const args[], int nargs,
       const char *stdin_data, jc_size cap)
{
    return git_child(out, mirror, e_global, e_system, NULL, 0, NULL, 0,
                     args, nargs, stdin_data, cap);
}

/* The credential -c flags, ONE helper for BOTH git children (the P1 fix: the
 * ls-remote of the key-scan exclusion and the push must carry the IDENTICAL
 * credential flags, or the ls-remote against GitLab over HTTPS has no helper
 * and fails). Fills `argv_out` with, in order: -c credential.helper= (the
 * RESET, so a planted repo-level helper is not appended), -c
 * credential.helper=<harness> credential-helper (the two-word helper spec, the
 * SET, which git runs as `<harness> credential-helper <action>`), and -c
 * credential.useHttpPath=true (the helper's path identity, behaviour 11's
 * host guard). Returns the flag count (3), or -1 when the buffers are too
 * small. */
static int
credential_args(char *argv_out[], int cap, const char *harness)
{
    char setflag[PATH_CAP + 32];
    if (cap < 3 || harness == NULL || harness[0] == '\0') {
        return -1;
    }
    if (jc_snprintf(setflag, sizeof setflag,
                    "credential.helper=%s credential-helper", harness)
        >= (int)sizeof setflag) {
        return -1;
    }
    argv_out[0] = (char *)"credential.helper=";
    argv_out[1] = setflag;
    argv_out[2] = (char *)"credential.useHttpPath=true";
    return 3;
}

/* The three credential identity env vars, ONE helper for BOTH git children
 * (the P1 fix). Fills `env_out` with, in order, the NAME=value strings
 * JICHI_HARNESS_GIT_TOKEN_FILE (the token file's PATH, not the token),
 * JICHI_HARNESS_CRED_RULE and JICHI_HARNESS_CRED_URL (the stored remote
 * URL). The three pre-built strings are passed in (the caller sized them
 * against their own overflow). Returns the count (3), or -1 when the buffers
 * are too small. */
static int
credential_env(char *env_out[], int cap, const char *e_token,
               const char *e_rule, const char *e_url)
{
    if (cap < 3 || e_token == NULL || e_rule == NULL || e_url == NULL) {
        return -1;
    }
    env_out[0] = (char *)e_token;
    env_out[1] = (char *)e_rule;
    env_out[2] = (char *)e_url;
    return 3;
}

/* The first NUL-terminated line of a capture (a trailing newline trimmed),
 * or NULL when the capture is empty. */
static char *
first_line(const struct jc_sb *out, struct jc_arena *a)
{
    char *s;
    const char *p;
    const char *nl;
    jc_size len;
    if (out == NULL || out->data == NULL) {
        return NULL;
    }
    p = out->data;
    nl = strchr(p, '\n');
    len = (nl != NULL) ? (jc_size)(nl - p) : strlen(p);
    if (len == 0) {
        return NULL;
    }
    s = (char *)jc_arena_calloc(a, len + 1);
    if (s == NULL) {
        return NULL;
    }
    memcpy(s, p, len);
    s[len] = '\0';
    return s;
}

/* The `remote.origin.url` value parsed from the stored config copy: the first
 * `url =` line under the `[remote "origin"]` block (the driver's awk shape).
 * Arena-allocated; NULL when the stored copy has no such line. */
static char *
stored_origin_url(const char *stored_copy, struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    const char *p;
    int in_remote = 0;
    char *out;

    if (jc_read_file(stored_copy, &text, &len, a) != JC_OK) {
        return NULL;
    }
    p = text;
    while (*p != '\0') {
        const char *line = p;
        const char *nl;
        jc_size linelen;
        const char *q;
        int matched = 0;
        nl = strchr(p, '\n');
        linelen = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
        {
            jc_size i = linelen;
            if (i > 0 && line[i - 1] == '\r') {
                i--;
            }
            linelen = i;
        }
        if (linelen >= 17 && strncmp(line, "[remote \"origin\"]", 17) == 0) {
            in_remote = 1;
        } else if (linelen > 0 && line[0] == '[' && in_remote) {
            in_remote = 0;   /* left the [remote "origin"] block */
        }
        if (in_remote) {
            q = line;
            while (q < line + linelen && (*q == ' ' || *q == '\t')) {
                q++;
            }
            if (linelen - (jc_size)(q - line) >= 5 &&
                strncmp(q, "url =", 5) == 0) {
                q += 5;
                while (q < line + linelen && (*q == ' ' || *q == '\t')) {
                    q++;
                }
                {
                    jc_size vlen = (jc_size)(line + linelen - q);
                    out = (char *)jc_arena_calloc(a, vlen + 1);
                    if (out != NULL) {
                        if (vlen > 0) {
                            memcpy(out, q, vlen);
                        }
                        out[vlen] = '\0';
                        matched = 1;
                    }
                }
                if (matched) {
                    return out;
                }
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return NULL;
}

/* Load one secret (the model key or the git token) by reference: the file the
 * config names. Returns 0 and sets *secret (arena, whitespace-trimmed, at
 * least SECRET_MIN_LEN) on success; -1 when the file is missing/unreadable
 * (the refusal names the label, never the value) or -2 when the trimmed value
 * is shorter than the minimum ("could not be loaded"). */
static int
load_secret(const char *label, const char *path, char **secret,
            struct jc_arena *a)
{
    char *raw = NULL;
    jc_size rlen = 0;
    char *trim;
    jc_size i;
    jc_size k = 0;
    jc_status st;
    if (path == NULL || path[0] == '\0') {
        refuse("the %s could not be loaded: the config names no file for it; "
               "the key scan cannot run (fail-closed); the push is refused",
               label);
        return -1;
    }
    st = jc_read_file(path, &raw, &rlen, a);
    if (st != JC_OK) {
        refuse("the %s could not be loaded: the file at %s is missing or "
               "not readable, so nothing can be checked (fail-closed); the "
               "push is refused", label, path);
        return -1;
    }
    trim = (char *)jc_arena_calloc(a, rlen + 1);
    if (trim == NULL) {
        refuse("the %s could not be loaded (out of memory)", label);
        return -1;
    }
    for (i = 0; i < rlen; i++) {
        char c = raw[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            trim[k++] = c;
        }
    }
    trim[k] = '\0';
    if (k < (jc_size)SECRET_MIN_LEN) {
        refuse("the %s could not be loaded: shorter than %d bytes after "
               "trimming whitespace, so nothing can be checked (fail-closed); "
               "the push is refused", label, (int)SECRET_MIN_LEN);
        return -2;
    }
    *secret = trim;
    return 0;
}

/* A NUL-safe substring search over raw bytes (the secrets contain no NUL, so
 * the needle is NUL-terminated; a blob may hold NULs). */
static int
bytes_contains(const char *hay, jc_size haylen, const char *needle)
{
    jc_size ndlen;
    jc_size i;
    ndlen = (jc_size)strlen(needle);
    if (ndlen == 0) {
        return 1;
    }
    if (haylen < ndlen) {
        return 0;
    }
    for (i = 0; i + ndlen <= haylen; i++) {
        if (memcmp(hay + i, needle, ndlen) == 0) {
            return 1;
        }
    }
    return 0;
}

/* A growing list of (sha, path, size) blob versions, parsed from a
 * `cat-file --batch-check='%(objectname) %(objecttype) %(objectsize)
 * %(rest)'` capture. */
struct blob_list {
    char **shas;
    char **paths;
    unsigned long *sizes;
    int n;
    int cap;
    struct jc_arena *a;
};

static int
blob_push(struct blob_list *bl, const char *sha, const char *path,
          unsigned long size)
{
    char *s;
    char *p;
    if (bl->n == bl->cap) {
        int ncap;
        char **ns;
        char **np;
        unsigned long *nz;
        int i;
        ncap = (bl->cap == 0) ? 32 : bl->cap * 2;
        ns = (char **)jc_arena_calloc(bl->a, (jc_size)ncap * sizeof(char *));
        np = (char **)jc_arena_calloc(bl->a, (jc_size)ncap * sizeof(char *));
        nz = (unsigned long *)jc_arena_calloc(bl->a,
                    (jc_size)ncap * sizeof(unsigned long));
        if (ns == NULL || np == NULL || nz == NULL) {
            return -1;
        }
        for (i = 0; i < bl->n; i++) {
            ns[i] = bl->shas[i];
            np[i] = bl->paths[i];
            nz[i] = bl->sizes[i];
        }
        bl->shas = ns;
        bl->paths = np;
        bl->sizes = nz;
        bl->cap = ncap;
    }
    s = (char *)jc_arena_calloc(bl->a, 41);
    p = (char *)jc_arena_calloc(bl->a, (jc_size)strlen(path) + 1);
    if (s == NULL || p == NULL) {
        return -1;
    }
    memcpy(s, sha, 40);
    s[40] = '\0';
    strcpy(p, path);
    bl->shas[bl->n] = s;
    bl->paths[bl->n] = p;
    bl->sizes[bl->n] = size;
    bl->n++;
    return 0;
}

/* Parse the batch-check capture into `bl`. Returns 0 on success, -1 on an
 * arena failure. `text` is NUL-terminated; each line is
 * `<sha> <type> <size> <path...>` (the path is everything after the third
 * field, and may itself hold spaces). */
static int
parse_blobs(const char *text, struct blob_list *bl)
{
    const char *p;
    int rc = 0;
    if (text == NULL) {
        return 0;
    }
    p = text;
    while (*p != '\0') {
        const char *nl;
        char line[1024];
        char *c1;
        char *c2;
        char *c3;
        jc_size linelen;
        nl = strchr(p, '\n');
        linelen = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
        if (linelen > sizeof line - 1) {
            linelen = sizeof line - 1;
        }
        memcpy(line, p, linelen);
        line[linelen] = '\0';
        c1 = strchr(line, ' ');
        if (c1 != NULL) {
            *c1 = '\0';
        }
        c2 = (c1 != NULL) ? strchr(c1 + 1, ' ') : NULL;
        if (c2 != NULL) {
            *c2 = '\0';
        }
        c3 = (c2 != NULL) ? strchr(c2 + 1, ' ') : NULL;
        if (c1 != NULL && c2 != NULL && strcmp(c1 + 1, "blob") == 0) {
            const char *path;
            const char *s;
            unsigned long v;
            int digits;
            unsigned long size;
            s = c2 + 1;
            v = 0;
            digits = 0;
            while (c3 != NULL && s < c3) {
                char c;
                c = *s;
                if (c >= '0' && c <= '9') {
                    v = v * 10UL + (unsigned long)(c - '0');
                    digits = 1;
                } else {
                    break;
                }
                s++;
            }
            size = (digits != 0) ? v : 0;
            path = (c3 != NULL) ? c3 + 1 : "";
            if (blob_push(bl, line, path, size) != 0) {
                rc = -1;
                break;
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return rc;
}
/* The pushed refspecs (behaviour 1). Returns the refspec COUNT (2 or 3):
 * the two always-on refspecs are $BR/main and the $BR/work wildcard, plus
 * -- when `push_kept` is true (the config's `pushKept` gate, the port of
 * mirror.sh's PUSH_KEPT=1, DEFAULT OFF) -- the kept-attempts refspec
 * the refspec refs/kept-wildcard to refs/heads/$BR/kept-wildcard. No `+`
 * anywhere (a rewritten ref is not
 * pushed). Built internally from the prefix, so the literal refs/heads/master
 * is never emitted (behaviour 12's architectural guard). */
static int
build_refspecs(const char *br, int push_kept, char rs[][512], size_t cap)
{
    int n = 0;
    jc_snprintf(rs[n], cap, "refs/heads/%s/main:refs/heads/%s/main", br, br);
    n++;
    jc_snprintf(rs[n], cap, "refs/heads/%s/work/*:refs/heads/%s/work/*", br, br);
    n++;
    if (push_kept) {
        jc_snprintf(rs[n], cap, "refs/kept/*:refs/heads/%s/kept/*", br);
        n++;
    }
    return n;
}

/* A deduplicated set of SHAs, COPIED into the arena the set owns (the caller
 * passes its 40-char strings on the stack, which the set must not outlive by
 * pointer). `add` appends `sha` when non-empty and 40 chars and not already
 * present; returns 0 when it fit, -1 when the set is full. */
struct sha_set {
    char *items[PUSH_MAX_EXCL];
    int n;
    struct jc_arena *a;
};

static int
set_add(struct sha_set *ss, const char *sha)
{
    int i;
    char *copy;
    if (sha == NULL || sha[0] == '\0' || sha[40] != '\0') {
        return 0;   /* not exactly a 40-char sha: skip (tag, empty line, ...) */
    }
    for (i = 0; i < ss->n; i++) {
        if (strcmp(ss->items[i], sha) == 0) {
            return 0;
        }
    }
    if (ss->n >= PUSH_MAX_EXCL) {
        return -1;
    }
    copy = (char *)jc_arena_calloc(ss->a, 41);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, sha, 40);
    copy[40] = '\0';
    ss->items[ss->n++] = copy;
    return 0;
}

/* Count the UNIQUE commit OIDs of a `cat-file --batch-check='%(objectname)
 * %(objecttype) %(objectsize) %(rest)'` capture. A commit's line is
 * `<oid> commit <size>` (the oid is 40 hex, then a space, then the literal
 * type `commit` -- a tree's line is `<oid> tree <size>` and a blob's is
 * `<oid> blob <size> <path>`, so the type token, not just the first 40 hex
 * chars, decides). The SAME commit reachable from two pushed tips appears on
 * two lines, so the OIDs are deduplicated through `seen`. Returns the number
 * of COMMITS in the range (the report must count commits, P4: the old
 * `count_commit_lines` counted EVERY batch-check line -- a file + its commit
 * -- as a "commit"). The caller owns and passes `seen` (it is the caller's
 * scratch set, distinct from the exclusion set). */
static int
count_commit_oids(const char *text, struct sha_set *seen)
{
    int n = 0;
    const char *p;
    if (text == NULL) {
        return 0;
    }
    p = text;
    while (*p != '\0') {
        const char *nl;
        int hex;
        jc_size i;
        nl = strchr(p, '\n');
        /* a commit line: 40 hex, a space, then the literal type `commit`. */
        hex = 1;
        for (i = 0; i < 40; i++) {
            char c = p[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) {
                hex = 0;
                break;
            }
        }
        if (hex && p[40] == ' ' && strncmp(p + 41, "commit", 6) == 0) {
            char oid[41];
            int n_before;
            memcpy(oid, p, 40);
            oid[40] = '\0';
            n_before = seen->n;
            set_add(seen, oid);
            /* set_add appends only when the oid was NOT already present, so
             * a fresh commit grows seen->n by one; a tip-shared commit
             * (reachable from two pushed tips) does not. */
            if (seen->n == n_before + 1) {
                n++;
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return n;
}

/* The `where the bytes are` report: the three biggest top-2-level directory
 * prefixes. */
struct dir_bytes {
    char name[256];
    unsigned long bytes;
};

static int
dir_cmp(const void *x, const void *y)
{
    const struct dir_bytes *a = (const struct dir_bytes *)x;
    const struct dir_bytes *b = (const struct dir_bytes *)y;
    if (a->bytes < b->bytes) {
        return 1;
    }
    if (a->bytes > b->bytes) {
        return -1;
    }
    return 0;
}

/* The directory prefix (up to two levels) of `path`, for the report. */
static void
dir_prefix(const char *path, char *out, size_t cap)
{
    const char *slash;
    jc_size plen;
    if (path == NULL || path[0] == '\0') {
        jc_snprintf(out, cap, "%s", ".");
        return;
    }
    slash = strchr(path, '/');
    if (slash == NULL) {
        jc_snprintf(out, cap, "%s", ".");
        return;
    }
    plen = (jc_size)(slash - path);
    if (plen >= cap) {
        plen = cap - 1;
    }
    memcpy(out, path, plen);
    out[plen] = '\0';
    /* a second level: the next component after the first slash */
    {
        const char *rest = slash + 1;
        const char *slash2 = strchr(rest, '/');
        if (slash2 != NULL && slash2 != rest) {
            jc_size p2 = (jc_size)(slash2 - rest);
            if (plen + 1 + p2 + 1 < cap) {
                out[plen] = '/';
                if (p2 >= cap - plen - 1) {
                    p2 = cap - plen - 2;
                }
                memcpy(out + plen + 1, rest, p2);
                out[plen + 1 + p2] = '\0';
            }
        }
    }
}

/* Find the index of `name` in `dirs` (0..ndirs-1), or -1. */
static int
dir_find(struct dir_bytes *dirs, int ndirs, const char *name)
{
    int i;
    for (i = 0; i < ndirs; i++) {
        if (strcmp(dirs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Format `b` bytes as `%.1f` MB into out (out is at least 32 bytes). */
static void
fmt_mb(unsigned long b, char *out, size_t cap)
{
    double v = (double)b / 1048576.0;
    jc_snprintf(out, cap, "%.1f", v);
}

/* Resolve the pushed tips ($BR/main + the $BR/work wildcard). Returns the
 * tip count in *ntips (up to PUSH_MAX_TIPS), 0 on none, -1 on a git
 * failure. */
static int
resolve_tips(char **tips, int *ntips, const char *mirror, const char *br,
             const char *e_global, const char *e_system, struct jc_arena *a)
{
    char pat_main[512];
    char pat_work[512];
    const char *argv[5];
    struct jc_sb out;
    const char *p;
    int rc;

    *ntips = 0;
    jc_snprintf(pat_main, sizeof pat_main, "refs/heads/%s/main", br);
    jc_snprintf(pat_work, sizeof pat_work, "refs/heads/%s/work", br);
    jc_sb_init(&out);
    argv[0] = (char *)"for-each-ref";
    argv[1] = (char *)pat_main;
    argv[2] = (char *)pat_work;
    argv[3] = (char *)"--format=%(objectname)";
    argv[4] = NULL;
    rc = git_m2(&out, mirror, e_global, e_system, argv, 4, NULL,
                PUSH_SMALL_CAP);
    if (rc != 0) {
        jc_sb_free(&out);
        return -1;
    }
    p = out.data;
    if (p != NULL) {
        while (*p != '\0') {
            const char *nl;
            jc_size ll;
            char sha[41];
            nl = strchr(p, '\n');
            ll = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
            if (ll == 40 && *ntips < PUSH_MAX_TIPS) {
                memcpy(sha, p, 40);
                sha[40] = '\0';
                tips[*ntips] = (char *)jc_arena_calloc(a, 41);
                if (tips[*ntips] != NULL) {
                    memcpy(tips[*ntips], sha, 41);
                    (*ntips)++;
                }
            }
            if (nl == NULL) {
                break;
            }
            p = nl + 1;
        }
    }
    jc_sb_free(&out);
    return 0;
}

/* Fill `excl` with the remote's current tips (by ls-remote of the stored
 * URL; the 01:37 item 1 exclusion -- a push by URL never advances the
 * mirror's refs/remotes/origin, so the tracking refs are stale by
 * construction) plus the mirror's tracking refs as a belt-and-suspenders
 * superset. Returns 0 on success, -1 on a git failure (the caller refuses,
 * fail-closed). The ls-remote child for an HTTPS URL is built like the push
 * child (the reset-then-set credential flags + the identity env); for a
 * file/local URL it is a plain read child. */
static int
build_exclusion(struct sha_set *excl, const char *mirror,
                const char *stored_url, const char *e_global,
                const char *e_system, char *env_url, struct jc_arena *a,
                const char *git_token_file, const char *harness_path)
{
    const char *argv[4];
    char *xflags[3];
    char *xenv[3];
    int nx = 0;
    int ne = 0;
    int rc;
    struct jc_sb out;
    const char *p;
    char token_env[512];
    char rule_env[512];

    memset(excl, 0, sizeof *excl);
    excl->a = a;
    /* The ls-remote child carries the SAME credential flags + identity env as
     * the push child (the P1 fix: against GitLab over HTTPS the ls-remote
     * consults the credential helper through the reset-then-set flags, so it
     * must get the full SET, not just the RESET -- pre-fix it carried only the
     * RESET + useHttpPath + CRED_URL, failed, and the key scan refused
     * fail-closed, so every real push was refused). The rule is `none` for a
     * file/local URL (a local ls-remote consults no helper) and `oauth2` for
     * a scheme URL. */
    {
        int is_file;
        is_file = (stored_url != NULL && strstr(stored_url, "://") == NULL)
                  || (stored_url != NULL &&
                      strncmp(stored_url, "file://", 7) == 0);
        nx = credential_args(xflags, 3, harness_path);
        if (nx < 0) {
            return -1;
        }
        if (jc_snprintf(token_env, sizeof token_env,
                        "JICHI_HARNESS_GIT_TOKEN_FILE=%s",
                        git_token_file != NULL ? git_token_file : "")
            >= (int)sizeof token_env) {
            return -1;
        }
        if (jc_snprintf(rule_env, sizeof rule_env,
                        "JICHI_HARNESS_CRED_RULE=%s",
                        is_file ? "none" : "oauth2")
            >= (int)sizeof rule_env) {
            return -1;
        }
        if (jc_snprintf(env_url, 512,
                        "JICHI_HARNESS_CRED_URL=%s", stored_url)
            >= 512) {
            return -1;
        }
        ne = credential_env(xenv, 3, token_env, rule_env, env_url);
        if (ne < 0) {
            return -1;
        }
    }

    jc_sb_init(&out);
    argv[0] = (char *)"ls-remote";
    argv[1] = (char *)stored_url;
    argv[2] = NULL;
    rc = git_child(&out, mirror, e_global, e_system, xflags, nx, xenv, ne,
                   argv, 2, NULL, PUSH_OUT_CAP);
    if (rc != 0) {
        jc_sb_free(&out);
        return -1;
    }
    /* Copy the ls-remote listing to the arena: the existence check below
     * reuses `out` as its scratch buffer while we still parse this. */
    p = out.data;
    {
        char *lscopy;
        if (p != NULL && out.len > 0) {
            lscopy = (char *)jc_arena_calloc(a, out.len + 1);
            if (lscopy == NULL) {
                jc_sb_free(&out);
                return -1;
            }
            memcpy(lscopy, p, out.len);
            lscopy[out.len] = '\0';
            p = lscopy;
        }
    }
    if (p != NULL) {
        while (*p != '\0') {
            const char *nl;
            const char *tab;
            jc_size ll;
            char sha[41];
            char rev[64];
            const char *cargv[4];
            char *exists;
            nl = strchr(p, '\n');
            ll = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
            tab = (ll != 0) ? (const char *)memchr(p, '\t', ll) : NULL;
            if (tab != NULL && (jc_size)(tab - p) == 40) {
                memcpy(sha, p, 40);
                sha[40] = '\0';
                /* Exclude only a tip the mirror actually HAS as a commit
                 * (the reference: `git cat-file -e "$rsha^{commit}"`). A
                 * tip pushed directly to the remote (not fetched) is absent
                 * here; excluding it would be a "bad object" and fail the
                 * rev-list. */
                jc_snprintf(rev, sizeof rev, "%s^{commit}", sha);
                exists = rev;
                cargv[0] = (char *)"cat-file";
                cargv[1] = (char *)"-e";
                cargv[2] = exists;
                cargv[3] = NULL;
                jc_sb_free(&out);
                jc_sb_init(&out);
                if (git_m2(&out, mirror, e_global, e_system, cargv, 3, NULL,
                           PUSH_SMALL_CAP) == 0) {
                    if (set_add(excl, sha) != 0) {
                        jc_sb_free(&out);
                        return -1;
                    }
                    jc_sb_free(&out);
                }
                jc_sb_init(&out);
            }
            if (nl == NULL) {
                break;
            }
            p = nl + 1;
        }
    }
    jc_sb_free(&out);

    /* The mirror's tracking refs (the second exclusion, --not
     * --remotes=origin in the rev-list; here for the same belt-and-
     * suspenders reason the reference gives: an object already on a tracking
     * ref is not new). */
    jc_sb_init(&out);
    argv[0] = (char *)"for-each-ref";
    argv[1] = (char *)"refs/remotes";
    argv[2] = (char *)"--format=%(objectname)";
    argv[3] = NULL;
    rc = git_m2(&out, mirror, e_global, e_system, argv, 3, NULL,
                PUSH_SMALL_CAP);
    if (rc == 0) {
        p = out.data;
        if (p != NULL) {
            while (*p != '\0') {
                const char *nl;
                jc_size ll;
                char sha[41];
                nl = strchr(p, '\n');
                ll = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
                if (ll == 40) {
                    memcpy(sha, p, 40);
                    sha[40] = '\0';
                    if (set_add(excl, sha) != 0) {
                        jc_sb_free(&out);
                        return -1;
                    }
                }
                if (nl == NULL) {
                    break;
                }
                p = nl + 1;
            }
        }
    }
    jc_sb_free(&out);
    return 0;
}

/* List the file versions the push adds: `git rev-list --objects <tips>
 * ^<excl> --not --remotes=origin` (the reference's exact form: the `^`
 * exclusions are the remote's CURRENT tips from the ls-remote; `--not
 * --remotes=origin` adds the tracking refs). Returns the capture (the caller
 * frees it); `*ok` is 1 when the rev-list ran (the capture may be empty when
 * nothing is new), 0 on a git failure (fail-closed). */
static struct jc_sb
list_pushed(char **tips, int ntips, const struct sha_set *excl,
            const char *mirror, const char *e_global, const char *e_system,
            int *ok)
{
    char rev_args[PUSH_MAX_TIPS + PUSH_MAX_EXCL + 2][72];
    const char *argv[PUSH_MAX_TIPS + PUSH_MAX_EXCL + 5];
    struct jc_sb out;
    int an = 0;
    int i;
    int rc;

    jc_sb_init(&out);
    argv[an++] = (char *)"rev-list";
    argv[an++] = (char *)"--objects";
    for (i = 0; i < ntips; i++) {
        jc_snprintf(rev_args[i], sizeof rev_args[i], "%s", tips[i]);
        argv[an++] = rev_args[i];
    }
    for (i = 0; i < excl->n; i++) {
        jc_snprintf(rev_args[ntips + i], sizeof rev_args[ntips + i], "^%s",
                    excl->items[i]);
        argv[an++] = rev_args[ntips + i];
    }
    argv[an++] = (char *)"--not";
    argv[an++] = (char *)"--remotes=origin";
    /* The rev-list argument bound, checked BEFORE the NULL terminator is
     * written (its slot is the argv's +5 entry). The legal maximum is
     * an == PUSH_MAX_TIPS + PUSH_MAX_EXCL + 4 (2 fixed + the tips + the
     * ^exclusions + "--not" "--remotes=origin"); if a caller ever exceeds it,
     * the argument list overflowed its bound -- fail-closed, the push is
     * refused. (out is already jc_sb_init'd and still empty here; no free.) */
    if (an > PUSH_MAX_TIPS + PUSH_MAX_EXCL + 4) {
        *ok = 0;
        return out;
    }
    argv[an] = NULL;
    rc = git_m2(&out, mirror, e_global, e_system, argv, an, NULL,
                PUSH_OUT_CAP);
    *ok = (rc == 0);
    if (rc != 0) {
        jc_sb_free(&out);
        jc_sb_init(&out);
    }
    return out;
}

/* The same range as list_pushed, but piped (as the reference does) through
 * `git cat-file --batch-check='%(objectname) %(objecttype) %(objectsize)
 * %(rest)'` so the blob list carries its OBJECT SIZE (the size check reads
 * that column, never re-reading the blob). The batch-check child's stdin is
 * the raw rev-list capture; returns the batch-check output, `*ok` 1 when the
 * range ran (the output may be empty when nothing is new), 0 on a failure. */
static struct jc_sb
list_pushed_bc(char **tips, int ntips, const struct sha_set *excl,
               const char *mirror, const char *e_global,
               const char *e_system, int *ok)
{
    struct jc_sb raw;
    struct jc_sb bc;
    const char *bargv[3];
    const char fmt[] = "%(objectname) %(objecttype) %(objectsize) %(rest)";
    int rc;
    int raw_ok;

    raw = list_pushed(tips, ntips, excl, mirror, e_global, e_system, &raw_ok);
    jc_sb_init(&bc);
    if (!raw_ok) {
        jc_sb_free(&raw);
        *ok = 0;
        return bc;
    }
    *ok = 1;
    if (raw.data == NULL || raw.len == 0) {
        /* The push adds nothing: the range is empty and the scan is silent. */
        jc_sb_free(&raw);
        return bc;
    }
    {
        char fmtarg[128];
        /* cat-file --batch-check=<format> takes one argument. */
        jc_snprintf(fmtarg, sizeof fmtarg, "--batch-check=%s", fmt);
        bargv[0] = (char *)"cat-file";
        bargv[1] = fmtarg;
        bargv[2] = NULL;
    }
    rc = git_m2(&bc, mirror, e_global, e_system, bargv, 2, raw.data,
                PUSH_OUT_CAP);
    if (rc != 0) {
        jc_sb_free(&bc);
        jc_sb_init(&bc);
    }
    jc_sb_free(&raw);
    return bc;
}

/* The KEY SCAN (behaviours 3-6): load both secrets fail-closed, resolve the
 * pushed tips, build the exclusion, list the blob versions the push adds, and
 * read each RAW for both canary secrets (a NUL-safe substring search). A hit
 * refuses, exit 1, naming the PATH never the content; any machinery failure
 * refuses (fail-closed). Returns 0 when clean, 1 when refused. */
static int
key_scan(const char *mirror, const char *stored_url, const char *br,
         const char *model_key, const char *git_token,
         const char *e_global, const char *e_system, struct jc_arena *a,
         const char *git_token_file, const char *harness_path)
{
    char *tips[PUSH_MAX_TIPS];
    int ntips = 0;
    struct sha_set excl;
    struct sha_set cseen;
    struct jc_sb out;
    char env_url[512];
    struct blob_list bl;
    int key_ok;
    int commits;
    int n;
    int i;
    int bad = 0;

    if (resolve_tips(tips, &ntips, mirror, br, e_global, e_system, a) != 0) {
        refuse("the key scan could not list the pushed tips in the mirror "
               "(`git for-each-ref` of %s/main and %s/work failed); the push "
               "is refused (fail-closed)", br, br);
        return 1;
    }
    if (ntips == 0) {
        refuse("the key scan: no pushed tip resolved in the mirror; the "
               "push is refused (fail-closed: a scan that cannot run is "
               "never a pass, behaviour 14)");
        return 1;
    }
    if (build_exclusion(&excl, mirror, stored_url, e_global, e_system,
                        env_url, a, git_token_file, harness_path) != 0) {
        refuse("the key scan could not list the remote's current tips "
               "(`git ls-remote` of the stored URL failed); the push is "
               "refused (fail-closed: it cannot list what the push adds)");
        return 1;
    }
    out = list_pushed_bc(tips, ntips, &excl, mirror, e_global, e_system,
                         &key_ok);
    if (key_ok == 0) {
        refuse("the key scan cannot list what the push adds "
               "(`git rev-list --objects` failed); the push is refused "
               "(fail-closed)");
        return 1;
    }
    memset(&bl, 0, sizeof bl);
    bl.a = a;
    /* The COMMIT count (P4: commits, not the every-object line count the
     * rev-list emits): the unique commit OIDs of the batch-check capture
     * (a commit line is `<oid> commit <size>`; a blob is `<oid> blob ...`;
     * the same commit reachable from two tips is deduplicated). */
    memset(&cseen, 0, sizeof cseen);
    cseen.a = a;
    commits = count_commit_oids(out.data, &cseen);
    if (parse_blobs(out.data, &bl) != 0) {
        refuse("the key scan could not read the pushed file versions; the "
               "push is refused (fail-closed)");
        jc_sb_free(&out);
        return 1;
    }
    n = bl.n;
    for (i = 0; i < n; i++) {
        struct jc_sb blob;
        const char *cargv[4];
        int brc;
        jc_sb_init(&blob);
        cargv[0] = (char *)"cat-file";
        cargv[1] = (char *)"blob";
        cargv[2] = bl.shas[i];
        cargv[3] = NULL;
        brc = git_m2(&blob, mirror, e_global, e_system, cargv, 3, NULL,
                     PUSH_BLOB_CAP);
        if (brc != 0 || blob.data == NULL || blob.len == 0) {
            char cut[224];
            cut_copy(cut, sizeof cut, blob.data);
            refuse("the key scan: the pushed file version %s (%s) could not "
                   "be read (cat-file rc=%d: %s); the push is refused "
                   "(fail-closed)", bl.shas[i], bl.paths[i], brc, cut);
            jc_sb_free(&blob);
            jc_sb_free(&out);
            return 1;
        }
        if (bytes_contains(blob.data, blob.len, model_key) ||
            bytes_contains(blob.data, blob.len, git_token)) {
            say("the key scan: %d file version(s) in %d commit(s) checked, "
                "1 with a secret (refused)", n, commits);
            refuse("the key scan: the pushed file version %s contains a "
                   "secret (the model key or the git token); the push is "
                   "refused", bl.paths[i]);
            bad++;
            jc_sb_free(&blob);
            jc_sb_free(&out);
            return 1;
        }
        jc_sb_free(&blob);
    }
    jc_sb_free(&out);
    say("the key scan: %d file version(s) in %d commit(s) checked, 0 with a "
        "secret", n, commits);
    (void)bad;
    return 0;
}

/* The SIZE CHECK (behaviour 7): the same range as the key scan, the blob
 * object sizes (the listing's own `%(objectsize)` field, not a re-read).
 * Refuses above 8 MiB per file version or 20 MiB per push, exit 1, with the
 * reference's report shape (the over-limit files up to 8, the three biggest
 * directories, the 'the commits stay in the mirror' reminder). Returns 0 when
 * within the limits, 1 when refused. */
static int
size_check(const char *mirror, const char *stored_url, const char *br,
           const char *e_global, const char *e_system, struct jc_arena *a,
           const char *git_token_file, const char *harness_path)
{
    char *tips[PUSH_MAX_TIPS];
    int ntips = 0;
    struct sha_set excl;
    struct jc_sb out;
    char env_url[512];
    struct blob_list bl;
    int size_ok;
    int n;
    int i;
    unsigned long total;
    unsigned long max_file_bytes;
    char bigmb[32];
    struct dir_bytes dirs[32];
    int ndirs = 0;

    if (resolve_tips(tips, &ntips, mirror, br, e_global, e_system, a) != 0) {
        refuse("the size check could not list the pushed tips; the "
               "push is refused (fail-closed)");
        return 1;
    }
    if (ntips == 0) {
        refuse("the size check: no pushed tip resolved in the mirror; the "
               "push is refused (fail-closed: a scan that cannot run is "
               "never a pass, behaviour 14)");
        return 1;
    }
    if (build_exclusion(&excl, mirror, stored_url, e_global, e_system,
                        env_url, a, git_token_file, harness_path) != 0) {
        /* The size check runs in the same process call as the key scan, so
         * the stored url is available. */
        refuse("the size check could not list the remote's current tips; "
               "the push is refused (fail-closed)");
        return 1;
    }
    out = list_pushed_bc(tips, ntips, &excl, mirror, e_global, e_system,
                         &size_ok);
    if (size_ok == 0) {
        refuse("the size check cannot list what the push adds; the push is "
               "refused (fail-closed)");
        return 1;
    }
    memset(&bl, 0, sizeof bl);
    bl.a = a;
    if (parse_blobs(out.data, &bl) != 0) {
        refuse("the size check could not read the pushed file versions; the "
               "push is refused (fail-closed)");
        jc_sb_free(&out);
        return 1;
    }
    n = bl.n;
    total = 0;
    max_file_bytes = 0;
    memset(dirs, 0, sizeof dirs);
    for (i = 0; i < n; i++) {
        char prefix[256];
        int di;
        total += bl.sizes[i];
        if (bl.sizes[i] > max_file_bytes) {
            max_file_bytes = bl.sizes[i];
        }
        dir_prefix(bl.paths[i], prefix, sizeof prefix);
        di = dir_find(dirs, ndirs, prefix);
        if (di < 0 && ndirs < 32) {
            jc_snprintf(dirs[ndirs].name, sizeof dirs[ndirs].name, "%s",
                        prefix);
            ndirs++;
            di = ndirs - 1;
        }
        if (di >= 0) {
            dirs[di].bytes += bl.sizes[i];
        }
    }
    jc_sb_free(&out);
    {
        double tmb = (double)total / 1048576.0;
        char over_files[8][512];
        int ofn = 0;
        int j;
        /* collect the over-limit file versions (up to 8) with their sizes */
        for (i = 0; i < n && ofn < 8; i++) {
            char mb[32];
            if (bl.sizes[i] > (PUSH_MAX_FILE_MB * 1048576UL)) {
                fmt_mb(bl.sizes[i], mb, sizeof mb);
                jc_snprintf(over_files[ofn], sizeof over_files[ofn],
                            "%s  (%s MB)", bl.paths[i], mb);
                ofn++;
            }
        }
        (void)ofn;
        /* the three biggest directories */
        for (i = 0; i < ndirs; i++) {
            for (j = i + 1; j < ndirs; j++) {
                if (dir_cmp(&dirs[i], &dirs[j]) > 0) {
                    struct dir_bytes t;
                    t = dirs[i];
                    dirs[i] = dirs[j];
                    dirs[j] = t;
                }
            }
        }
        if (total > (PUSH_MAX_TOTAL_MB * 1048576UL)) {
            char mb[32];
            fmt_mb(total, mb, sizeof mb);
            say("the size check: %d file version(s), %s MB in all -- over "
                "the limit of %lu MB per push. The largest: %s; %s; %s. "
                "The over-limit file version(s): %s",
                n, mb, (unsigned long)PUSH_MAX_TOTAL_MB,
                (ndirs > 0) ? dirs[0].name : "-",
                (ndirs > 1) ? dirs[1].name : "-",
                (ndirs > 2) ? dirs[2].name : "-",
                (ofn > 0) ? over_files[0] : "-");
            say("the push was refused: the commits stay in the mirror; what "
                "helps is a branch without those commits, or a raised limit "
                "for one push by hand");
            return 1;
        }
        if (max_file_bytes > (PUSH_MAX_FILE_MB * 1048576UL)) {
            char mb[32];
            fmt_mb(max_file_bytes, mb, sizeof mb);
            say("the size check: a file version is %s MB -- over the limit "
                "of %lu MB per file version: %s", mb,
                (unsigned long)PUSH_MAX_FILE_MB,
                (ofn > 0) ? over_files[0] : "-");
            say("the push was refused: the commits stay in the mirror; what "
                "helps is a branch without those commits, or a raised limit "
                "for one push by hand");
            return 1;
        }
        fmt_mb(max_file_bytes, bigmb, sizeof bigmb);
        say("the size check: %d file version(s), %.1f MB in all, the "
            "biggest file version %s MB -- within the limits of %lu MB per "
            "file version and %lu MB per push",
            n, tmb, bigmb,
            (unsigned long)PUSH_MAX_FILE_MB,
            (unsigned long)PUSH_MAX_TOTAL_MB);
        (void)tmb;
        return 0;
    }
}

int
jc_harness_mirror_push_run(const char *config_path,
                           const char *mirror_dir, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    char e_global[PATH_CAP];
    char e_system[PATH_CAP];
    char stored[PATH_CAP];
    char *stored_url;
    char *model_key = NULL;
    char *git_token = NULL;
    char harness_path[PATH_CAP];
    char *cflags[6];
    char *penv[6];
    char rule_env[512];
    char token_env[512];
    char url_env[512];
    int nc = 0;
    int ne = 0;
    int rc;
    int push_kept;   /* the `pushKept` gate (default off): the kept refspec */

    if (mirror_dir == NULL || mirror_dir[0] == '\0') {
        refuse("needs the mirror directory: "
               "jichi-harness mirror push --config <f> <mirror-dir>");
        return 2;
    }
    rc = load_cfg(config_path, a, &cfg);
    if (rc != 0) {
        return rc;
    }
    push_kept = cfg.push_kept;
    if (!jc_is_dir(mirror_dir)) {
        refuse("the mirror directory does not exist or is not a directory: "
               "%s", mirror_dir);
        return 2;
    }
    if (cfg.dir == NULL || cfg.dir[0] == '\0') {
        refuse("the config has no `dir`, so the harness's state (the empty "
               "GIT_CONFIG files) cannot be located");
        return 2;
    }
    /* Step 1: the git-free M1055 checks 1, 2, 5 and 4 first -- a bare repo,
     * owned by the harness, no alternates, no armed hooks -- then check 3
     * (the stored-copy config-BYTES comparison), so ALL FIVE precheck checks
     * (1, 2, 5, 4, 3) complete BEFORE any git child is spawned (the note's
     * step 1, "BEFORE any git call"). A mirror-local `remote set-url` /
     * `pushurl` plant changes the LIVE config bytes, so check 3 refuses it
     * here, before the guard's get-url child runs; the guard below is
     * redundant-but-present for such plants, and its DISTINCT tooth is a
     * plant check 3 cannot see -- a stored copy rewritten so the stored URL
     * no longer equals the effective URL while live == stored (the driver's
     * P2c cell measures the guard with check 3 satisfied). */
    rc = check_bare(mirror_dir, a);
    if (rc != 0) {
        return rc;
    }
    rc = check_ownership(mirror_dir);
    if (rc != 0) {
        return rc;
    }
    rc = check_no_alternates(mirror_dir);
    if (rc != 0) {
        return rc;
    }
    rc = check_hooks(mirror_dir);
    if (rc != 0) {
        return rc;
    }
    if (!make_empty_env_files(cfg.dir, a, e_global, sizeof e_global,
                              e_system, sizeof e_system)) {
        refuse("could not create the empty GIT_CONFIG_GLOBAL/SYSTEM files "
               "in the harness's dir %s", cfg.dir);
        return 1;
    }
    if (!build_stored_copy(mirror_dir, cfg.dir, stored, sizeof stored)) {
        refuse("the mirror path is too long to build the stored-copy path");
        return 2;
    }
    rc = check_config_bytes(mirror_dir, stored, a);
    if (rc != 0) {
        return rc;
    }
    stored_url = stored_origin_url(stored, a);
    if (stored_url == NULL || stored_url[0] == '\0') {
        refuse("could not parse `remote.origin.url` from the stored config "
               "copy at %s -- the push destination is unknown", stored);
        return 2;
    }
    /* Step 2: the URL guard, the FIRST git child of the pipeline (and
     * BEFORE any ls-remote/push child). The effective push URL must equal
     * the stored URL byte-exactly (a mismatch names both values, exit 2). */
    {
        struct jc_sb out;
        const char *argv[4];
        char *eff;
        jc_sb_init(&out);
        argv[0] = (char *)"remote";
        argv[1] = (char *)"get-url";
        argv[2] = (char *)"--push";
        argv[3] = (char *)"origin";
        rc = git_m2(&out, mirror_dir, e_global, e_system, argv, 4, NULL,
                    PUSH_SMALL_CAP);
        eff = (rc == 0) ? first_line(&out, a) : NULL;
        jc_sb_free(&out);
        if (eff == NULL) {
            refuse("the URL guard could not read the mirror's effective push "
                   "URL (`git remote get-url --push origin`); the push is "
                   "refused");
            return 2;
        }
        if (strcmp(eff, stored_url) != 0) {
            refuse("the URL guard: the mirror's effective push URL is %s, "
                   "but the stored copy (the one init wrote) says %s -- the "
                   "mirror's remote was altered; the push is refused", eff,
                   stored_url);
            return 2;
        }
    }
    /* The two secrets, fail-closed (each must exist, be readable, be >= 12
     * bytes after trim). A refusal names WHICH secret could not be loaded. */
    if (load_secret("model key", cfg.model_key_file, &model_key, a) != 0) {
        return 1;
    }
    if (load_secret("git token", cfg.git_token_file, &git_token, a) != 0) {
        return 1;
    }
    /* The harness's own path, resolved BEFORE the scans: the key scan's and
     * size check's ls-remote exclusion children need the credential helper's
     * absolute path (the P1 fix: the ls-remote child is built like the push
     * child -- the same reset-then-set flags + the same identity env). */
    if (!self_exe(harness_path, sizeof harness_path)) {
        refuse("could not resolve the harness's own path (readlink "
               "/proc/self/exe failed); the push cannot name the credential "
               "helper; refused");
        return 1;
    }
    /* The C key scan, then the C size check, over the same range (the port
     * runs them as pre-push gates, not hooks the agent could edit). */
    if (key_scan(mirror_dir, stored_url, cfg.drive_branch_prefix, model_key,
                 git_token, e_global, e_system, a, cfg.git_token_file,
                 harness_path) != 0) {
        return 1;
    }
    if (size_check(mirror_dir, stored_url, cfg.drive_branch_prefix, e_global,
                   e_system, a, cfg.git_token_file, harness_path) != 0) {
        return 1;
    }
    /* Step 4: the push, by the stored absolute URL (never the `origin`
     * name), never --force, never a master refspec. The M2 set +
     * -c core.hooksPath=/dev/null + -c credential.helper= (RESET) +
     * -c credential.helper=<ABS> credential-helper (the two-word helper,
     * SET after the reset so it wins) + -c credential.useHttpPath=true, and
     * the env the helper needs (the token FILE, the rule, the url). */
    {
        char rs[3][512];
        int nrs;
        struct jc_sb out;
        const char *args[16];
        int an = 0;
        int i;

        /* ONE helper builds the credential flags + env for BOTH git children
         * (the P1 fix: the push and the key-scan exclusion's ls-remote carry
         * the IDENTICAL credential args/env, so an HTTPS remote consults the
         * harness's credential helper on the ls-remote too): -c
         * credential.helper= (the RESET), -c credential.helper=<ABS-JICHI-
         * HARNESS> credential-helper (the two-word spec, SET after the reset
         * so it wins), -c credential.useHttpPath=true (the helper's path
         * identity, behaviour 11's host guard), and the env JICHI_HARNESS_
         * GIT_TOKEN_FILE + JICHI_HARNESS_CRED_RULE + JICHI_HARNESS_CRED_URL.
         * The rule is `none` for a file/local URL (a local push consults no
         * helper) and `oauth2` for a scheme URL. */
        {
            int is_file;
            is_file = (stored_url != NULL && strstr(stored_url, "://") == NULL)
                      || (stored_url != NULL &&
                          strncmp(stored_url, "file://", 7) == 0);
            nc = credential_args(cflags, 6, harness_path);
            if (nc < 0) {
                refuse("the credential -c flags could not be built (the "
                       "harness path is too long); the push is refused");
                return 1;
            }
            if (jc_snprintf(rule_env, sizeof rule_env,
                            "JICHI_HARNESS_CRED_RULE=%s",
                            is_file ? "none" : "oauth2")
                >= (int)sizeof rule_env) {
                refuse("the credential rule env is too long; the push is "
                       "refused");
                return 1;
            }
            if (jc_snprintf(token_env, sizeof token_env,
                            "JICHI_HARNESS_GIT_TOKEN_FILE=%s",
                            cfg.git_token_file != NULL ? cfg.git_token_file : "")
                >= (int)sizeof token_env) {
                refuse("the token-file env is too long; the push is refused");
                return 1;
            }
            if (jc_snprintf(url_env, sizeof url_env,
                            "JICHI_HARNESS_CRED_URL=%s", stored_url)
                >= (int)sizeof url_env) {
                refuse("the credential url env is too long; the push is "
                       "refused");
                return 1;
            }
            ne = credential_env(penv, 6, token_env, rule_env, url_env);
            if (ne < 0) {
                refuse("the credential env could not be built; the push is "
                       "refused");
                return 1;
            }
        }

        nrs = build_refspecs(cfg.drive_branch_prefix, push_kept, rs,
                             sizeof rs[0]);
        jc_sb_init(&out);
        args[an++] = (char *)"push";
        args[an++] = (char *)"-q";
        args[an++] = (char *)stored_url;
        for (i = 0; i < nrs; i++) {
            args[an++] = rs[i];
        }
        args[an] = NULL;
        rc = git_child(&out, mirror_dir, e_global, e_system, cflags, nc,
                       penv, ne, args, an, NULL, PUSH_SMALL_CAP);
        if (rc == 0) {
            say("pushed %s/main and the %s/work refset to %s",
                cfg.drive_branch_prefix, cfg.drive_branch_prefix,
                stored_url);
            jc_sb_free(&out);
            return 0;
        }
        {
            char cut[224];
            cut_copy(cut, sizeof cut, out.data);
            if (rc == 1) {
                /* git refused (a non-FF, a bad remote): print git's own
                 * rejection, NOT just the first 200 bytes -- the rejection
                 * line (`! [rejected] ... (fetch first)`) can sit past byte
                 * 200 after a long repo path, and the driver greps it.
                 * `out` carries the child's full stdout+stderr (merged);
                 * find the FIRST line that mentions a refusal marker and
                 * print it (bounded to 160 bytes), so the driver's grep
                 * for 'fetch first' / 'non-fast-forward' / 'rejected'
                 * matches even under a 200-byte head-truncation. Do NOT
                 * suppress the line; the driver quotes it to the log. */
                const char *data = out.data;
                jc_size len = out.len;
                jc_size pos = 0;
                int found = 0;
                fputs("mirror push: git refused the push:\n", stderr);
                if (data != NULL && len > 0) {
                    while (pos < len && !found) {
                        jc_size eol = len;
                        jc_size i;
                        for (i = pos; i < len; i++) {
                            if (data[i] == '\n') {
                                eol = i;
                                break;
                            }
                        }
                        /* NUL-terminate a scratch copy for the substring
                         * tests (the buffer is not NUL-terminated at
                         * eol, and we must not scan past len). */
                        {
                            char scratch[256];
                            jc_size n = eol - pos;
                            if (n >= 256) {
                                n = 255;
                            }
                            memcpy(scratch, data + pos, n);
                            scratch[n] = '\0';
                            if (strstr(scratch, "rejected") != NULL ||
                                strstr(scratch, "fetch first") != NULL ||
                                strstr(scratch, "non-fast-forward") != NULL ||
                                strstr(scratch, "error:") != NULL) {
                                jc_size print_n = (eol - pos > 160)
                                    ? 160 : (eol - pos);
                                fwrite(data + pos, 1, print_n, stderr);
                                fputc('\n', stderr);
                                found = 1;
                            }
                        }
                        pos = eol + 1;
                    }
                    if (!found) {
                        /* No refusal marker on any line -- print the
                         * original bounded cut so the operator still sees
                         * SOMETHING (the 200-byte head). */
                        fputs(cut, stderr);
                        if (cut[200] != '\0') {
                            fputc('\n', stderr);
                        }
                    }
                }
                fflush(stderr);
                jc_sb_free(&out);
                return 1;
            }
            jc_sb_free(&out);
            refuse("the push failed (git rc=%d: %s); the remote was not "
                   "updated", rc, cut);
            return 1;
        }
    }
    /* Unreachable: every git_child rc path above returns. */
    return 1;
}
