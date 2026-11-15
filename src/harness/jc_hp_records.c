/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_records.c - the `push records` subcommand body (B1, M1147, batch
 * H7). One file per push subcommand (a1's restructure), so the parallel
 * pool's per-file merge can land all five bodies.
 *
 * This body is b1's finished records push (re-applied from b1's kept work,
 * 91ec96e), adapted to the exported M1056 primitives a1 moved into
 * jc_harness_push.c's M1147 section:
 *   - the range: b1's `build_exclusion` + `list_pushed_bc` is now the
 *     exported jc_hp_records_scan (M1056's exact range rule, OIDs only);
 *   - the credential-wired children (the remote fetch, the push): b1's
 *     `cred_child` now builds the flags via the exported jc_hp_cred_flags
 *     (the arena-stable SET flag, the SV:1939 dangling-setflag fix -- the
 *     flag now survives every child between build and spawn) and the
 *     identity env via the exported jc_hp_cred_env, and spawns through the
 *     exported jc_hp_git_run. b1's decision 2 (rebuild the credential
 *     wiring per child, immediately before the spawn) stands unchanged;
 *   - the secrets: b1's static `load_secret` (which printed its own
 *     label-named line) is now the exported jc_hp_load_secret (prints
 *     nothing) -- the which-secret refusal line is printed here;
 *   - the blob check: b1's `bytes_contains` is now the exported
 *     jc_hp_bytes_contains (the explicit needle length replaces the
 *     NUL-terminated needle; the secrets contain no NUL, so identical);
 *   - the blob-version list: b1's own `struct blob_list` / `blob_push` /
 *     `parse_blobs` (a copy of M1056's) is deleted (SV:0038) -- the key scan
 *     parses its batch-check capture into M1056's EXPORTED shared `struct
 *     jc_hp_blob_list` with the exported `jc_hp_parse_blobs` (the copy's
 *     `strcpy(p, path)` was the 00:27 sprintf_lint check-2 red; M1056's
 *     allowlisted copy is the only one now).
 * The statics that a1 did NOT export (the config load, the empty env
 * files, the self-exe path, the stored-URL parse, the refusal/log helpers)
 * are duplicated here, kept in behaviour with their originals in
 * jc_harness_push.c; and the read child that the exports cannot express
 * (the batch-check child needs a stdin feed, which jc_hp_git_run has) runs
 * through a small M2-neutral child of M1056's git_child shape. (The blob
 * read was shared via the jc_hp_blob_read export, SV:2239.)
 *
 * The C89 port of push.sh steps 5-8 (the records half; def-360 retired),
 * with the reference's containerised upload-pack indirection dropped (the
 * CHANGED note: the port reads <dir>/recs DIRECTLY with the M2-neutral git
 * child; the URL guard + key scan are the real fences). The differences
 * from `mirror push`:
 *   - the records repo is a PLAIN NON-BARE repo (positional), not a bare
 *     mirror; the stored config copy is <recs-parent>/<basename>.git-
 *     config.harness-copy (the basename follows the recs dir, so a clone
 *     has its own copy);
 *   - KEY SCAN ONLY -- no size check (push.sh L20-21: "the records are
 *     large by design");
 *   - the range is objects reachable from refs/heads/<prefix>/records and
 *     from none of the remote's current tips (ls-remote of the stored URL)
 *     nor recs' tracking refs (M1056's exact two exclusions, via
 *     jc_hp_records_scan);
 *   - the URL guard is stored-URL-parse + push-by-stored-URL, not a
 *     live==stored byte refusal (b1's measurement decision 1: the driver's
 *     clone cells have a live origin = the clone source while the stored
 *     copy names the real remote; the invariant that survives is that the
 *     push goes by the stored ABSOLUTE URL, never the mutable `origin`
 *     name);
 *   - the remote-fetch failure is rc-NEUTRAL (the orphan records branch
 *     may not exist on the remote yet); the drive-records fetch failure and
 *     the push failure set rc=1 but the command runs to the end (push.sh
 *     L82-83); the log lines go to stdout (the host timer's journal is the
 *     durable log).
 *
 * Exit classes (B1.4/1.5): 0 landed, 1 key-scan refusal OR push failure,
 * 2 precheck/usage/URL-guard refusal.
 * ===================================================================== */

#include "jc_harness_push.h"
#include "jc_harness_config.h"

#include "jc_mem.h"
#include "jc_platform.h"
#include "jc_proc.h"
#include "jc_snprintf.h"
#include "jc_str.h"    /* struct jc_sb, jc_sb_init / jc_sb_free */
#include "jc_vec.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>   /* readlink */

/* Paths hang off the config's dir / the recs dir; the same bound M1056
 * (jc_harness_push.c), the fetch, and the drive use. */
#define PATH_CAP 4352

/* Capture caps (M1056's). The key scan reads a blob RAW (git cat-file
 * blob) and a blob can be large (the records are large by design), so the
 * blob capture cap is generous; the object listing is bounded by the
 * history size. A cap here is a floor for the driver's fixtures, not a
 * measured fit. */
#define PUSH_OUT_CAP   (8 * 1024 * 1024)   /* rev-list / batch-check */
#define PUSH_SMALL_CAP (256 * 1024)        /* rev-parse */
#define PUSH_BLOB_CAP  (64 * 1024 * 1024)  /* git cat-file blob <sha> */
#define PUSH_TIMEOUT   120L                /* M1056's cap (jc_hp_git_run keeps it) */

/* The first 200 bytes of a failed git child's output (M1053's git_must idiom). */
#define PUSH_ERR_CUT 200

/* The minimum a secret must be, after whitespace trim (behaviour 3; the
 * exported jc_hp_load_secret enforces the same bound). */
#define SECRET_MIN_LEN 12

/* stderr refusal for the records command (names the command, never a
 * secret). The `say`/`refuse` helpers are M1056's (the "mirror push:" and
 * "jichi-harness: mirror:" prefixes) and stay as-is in their own file. */
static void
rrefuse(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: push records: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

/* stdout log line, the reference's `push: ...` shape (B1.5). The log goes
 * to stdout (the host timer's journal is the durable log; push-tick B2
 * captures the last line). */
static void
rsay(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("push: ", stdout);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
    fflush(stdout);
}

/* The first 200 bytes of a failed git child's output (M1056's cut_copy
 * shape, for refusal lines). */
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

/* The harness's own executable path (readlink /proc/self/exe, the M1053
 * idiom). Returns 1 on success, 0 on failure. (M1056's self_exe shape.) */
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

/* The config load, the records flavor: the harness config at config_path,
 * its refusals exit 2 (M1056's load_cfg shape, the `push records` prefix).
 * a1's M1147 section does not export the config load, so this file's own
 * thin copy stands here. */
static int
load_cfg(const char *config_path, struct jc_arena *a,
         struct jc_harness_config *cfg)
{
    jc_status st;
    if (config_path == NULL) {
        rrefuse("needs --config <file> (the harness configuration)");
        return 2;
    }
    st = jc_harness_config_load(config_path, cfg, a);
    if (st != JC_OK) {
        const char *msg = jc_harness_config_error(cfg);
        rrefuse("%s", (msg != NULL) ? msg : "could not read the harness config");
        return 2;
    }
    if (cfg->drive_branch_prefix == NULL ||
        cfg->drive_branch_prefix[0] == '\0') {
        rrefuse("the config has no drive.branchPrefix (the refset's $BR); "
                "push cannot name the team's branches");
        return 2;
    }
    return 0;
}

/* Two empty files the harness owns, in the config's dir, for
 * GIT_CONFIG_GLOBAL / GIT_CONFIG_SYSTEM (behaviour 8's isolation, M1056's
 * make_empty_env_files shape; the drive's recs children use the same
 * idiom). The child reads an empty global and an empty system, so a plant
 * in the agent's ~/.gitconfig is not seen by the records git children. */
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

/* The `remote.origin.url` value parsed from the stored config copy: the
 * first `url =` line under the `[remote "origin"]` block (the driver's awk
 * shape). Arena-allocated; NULL when the stored copy has no such line.
 * (M1056's stored_origin_url shape, verbatim behaviour -- the URL guard's
 * reference, b1's measurement decision 1.) */
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

/* Build <recs-parent>/<recs-basename>.git-config.harness-copy (the stored
 * config copy init writes next to recs/; the URL guard's reference, B1.1).
 * The FILE NAME follows the recs directory's basename, so a driver clone
 * (recs2, recs5) has its own <recs2>.git-config.harness-copy -- the driver
 * writes one per clone. Returns 1 on success, 0 when the joined path is too
 * long. (b1's static, unchanged: M1056's build_stored_copy hard-codes
 * "mirror.git-config.harness-copy"; the records copy is named for the
 * recs dir.) */
static int
build_recs_stored_copy(const char *recs, char *dst, size_t cap)
{
    const char *slash;
    slash = strrchr(recs, '/');
    if (slash != NULL && slash != recs) {
        jc_size pl = (jc_size)(slash - recs);
        char parent[PATH_CAP];
        if (pl + 1 >= sizeof parent) {
            return 0;
        }
        memcpy(parent, recs, pl);
        parent[pl] = '\0';
        if (jc_snprintf(dst, cap, "%s/%s.git-config.harness-copy", parent,
                        slash + 1) >= (int)cap) {
            return 0;
        }
    } else {
        if (jc_snprintf(dst, cap, "%s.git-config.harness-copy", recs)
            >= (int)cap) {
            return 0;
        }
    }
    return 1;
}

/* The first line of a capture, arena-allocated (M1056's first_line shape
 * over a plain text buffer). */
static char *
first_line(const char *text, struct jc_arena *a)
{
    const char *nl;
    jc_size len;
    char *s;
    if (text == NULL) {
        return NULL;
    }
    nl = strchr(text, '\n');
    len = (nl != NULL) ? (jc_size)(nl - text) : strlen(text);
    if (len == 0) {
        return NULL;
    }
    s = (char *)jc_arena_calloc(a, len + 1);
    if (s == NULL) {
        return NULL;
    }
    memcpy(s, text, len);
    s[len] = '\0';
    return s;
}

/* The blob-version list the key scan parses its batch-check capture into is
 * M1056's EXPORTED `struct jc_hp_blob_list` + `jc_hp_parse_blobs`
 * (jc_harness_push.h (c), M1147; SV:0038). b1's re-apply of this file
 * duplicated M1056's blob_list / blob_push / parse_blobs (the exports handed
 * the range over as OIDs only, so this file re-listed the range and parsed it
 * with its own copies); the duplicate's `strcpy(p, path)` (its own blob_push)
 * was the 00:27 sprintf_lint check-2 red -- M1056's strcpy is allowlisted,
 * the copy's was not. The three duplicates are deleted; this file now parses
 * its capture with the ONE shared parser, so the blob-version machinery
 * lives in M1056's file alone and the two copies can no longer drift. */

/* One M2-neutral git read child into a jc_sb, M1056's git_child shape
 * (the five M2 -c flags, -C workdir, the two empty GIT_CONFIG_* slots, an
 * optional stdin feed, M1056's 120 s cap). The only children it serves
 * are the ones the exports cannot express -- the batch-check child (needs
 * a stdin feed; jc_hp_git_run has none) and the blob read (needs the true
 * byte length of a binary capture; jc_hp_git_run's caller buffer is
 * NUL-terminated text). No credential flags: a read child never consults
 * the credential helper (M1056's git_m2 split). Returns
 * jc_proc_capture's code: the child's exit status, -1 on a build / spawn
 * failure, -2 on timeout. */
static int
git_sb(struct jc_sb *out, const char *workdir,
       const char *e_global, const char *e_system,
       const char *args[], int nargs,
       const char *stdin_data, jc_size cap)
{
    char *argv[64];
    struct jc_vec envv;
    int k = 0;
    int i;
    int rc;
    if (nargs > 40) {
        return -1;
    }
    argv[k++] = (char *)"git";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.fsmonitor=";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"log.showSignature=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"commit.gpgsign=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.hooksPath=/dev/null";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.alternateRefsCommand=";
    argv[k++] = (char *)"-C";
    argv[k++] = (char *)workdir;
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
    rc = (int)jc_proc_capture(argv, &envv, stdin_data, out, cap,
                              PUSH_TIMEOUT, NULL);
    jc_vec_free(&envv);
    return rc;
}

/* One M2-neutral git read child, a1's exported jc_hp_git_run with no
 * credential flags / env (the tip rev-parse): the M2 -c flags, the
 * GIT_CONFIG_GLOBAL/SYSTEM=/dev/null slots, the caller's text buffer.
 * Returns jc_hp_git_run's code. */
static int
git_read(struct jc_arena *a, const char *workdir,
         const char *args[], int nargs,
         int max_out, char *out, char *err_line)
{
    return jc_hp_git_run(a, workdir, NULL, 0, NULL, 0, args, nargs,
                         max_out, out, err_line);
}

/* One credential-wired git child (the remote fetch, the push), built
 * IMMEDIATELY before the spawn and carrying the credential helper wiring
 * exactly M1056's (the reset-then-set helper, useHttpPath, and the
 * token-file / rule / url identity env). b1's decision 2 stands: the
 * flags + env are rebuilt on the call's own storage for EVERY
 * credential-wired child, never cached between them. Adapted to a1's
 * exports: the flags come from jc_hp_cred_flags (the SET flag is
 * arena-stable -- the SV:1939 dangling-setflag fix -- so it now survives
 * every child between build and spawn) and the identity env from
 * jc_hp_cred_env (the caller's three 512-byte buffers, which outlive every
 * child); the spawn runs through jc_hp_git_run. The rule is `none` for a
 * file/local URL, `oauth2` for a scheme URL (jc_hp_cred_env derives it).
 * Returns jc_hp_git_run's code: the child's exit status, -1 on a
 * build / spawn failure, -2 on timeout. `out` (the caller's capture buffer,
 * jc_sb_init'd) receives the child's combined stdout+stderr, truncated to
 * cap (b1's cred_child took the same `struct jc_sb *out`; the fetch's
 * output is unused, the push's first line feeds the refusal line). */
static int
cred_child(struct jc_sb *out, struct jc_arena *a, const char *workdir,
           const char *harness_path,
           const char *push_url, const char *git_token_file,
           const char *args[], int nargs, jc_size cap)
{
    char *cflags[4];
    char token_env[512];
    char rule_env[512];
    char url_env[512];
    char *penv[4];
    const char *penv_c[4];
    char *buf;
    char err_line[512];
    int nc;
    int rc;

    nc = jc_hp_cred_flags(a, harness_path, cflags);
    if (nc < 0) {
        return -1;
    }
    penv[0] = token_env;
    penv[1] = rule_env;
    penv[2] = url_env;
    penv[3] = NULL;
    jc_hp_cred_env(git_token_file, NULL, push_url, penv);
    if (token_env[0] == '\0' || rule_env[0] == '\0' || url_env[0] == '\0') {
        /* jc_hp_cred_env left a buffer empty on overflow: the caller must
         * refuse (the buffer contract). */
        return -1;
    }
    penv_c[0] = penv[0];
    penv_c[1] = penv[1];
    penv_c[2] = penv[2];
    penv_c[3] = NULL;
    buf = (char *)jc_arena_calloc(a, cap + 1);
    if (buf == NULL) {
        return -1;
    }
    rc = jc_hp_git_run(a, workdir, cflags, nc, penv_c, 3, args, nargs,
                       (int)cap, buf, err_line);
    /* jc_hp_git_run's caller buffer is NUL-terminated TEXT (truncated to
     * max_out); copy the same bytes into out's heap-backed buffer (via
     * jc_sb_append_n, NOT by aliasing `buf` -- buf is arena-owned and the
     * caller frees out with jc_sb_free, which would free() an arena pointer).
     * The fetch's output is unused, the push's first line feeds the refusal. */
    if (out != NULL && rc != 0) {
        jc_size blen = strlen(buf);
        if (blen > 0) {
            jc_sb_append_n(out, buf, blen);
        }
    }
    return rc;
}

/* The records key scan (B1.2): KEY SCAN ONLY -- the SAME range rule as
 * M1056's (via the exported jc_hp_records_scan: the remote's current tips
 * by ls-remote of the stored URL + the recs' tracking refs, over a SINGLE
 * pushed tip refs/heads/<prefix>/records) but with NO size check. The
 * scan lists the blobs the push adds (a batch-check feed of the range,
 * M1056's shape), reads each blob RAW, and refuses exit 1 naming the PATH
 * (never the content) when a secret is present; any machinery failure
 * refuses (fail-closed). Returns 0 when clean, 1 when refused. */
static int
records_key_scan(const char *recs, const char *stored_url, const char *br,
                 const char *records_tip, const char *model_key,
                 int model_key_len, const char *git_token,
                 int git_token_len, const char *e_global,
                 const char *e_system, struct jc_arena *a,
                 const char *git_token_file, const char *harness_path)
{
    char **shas = NULL;
    int n = 0;
    struct jc_sb rev;
    struct jc_sb bc;
    char fmtarg[96];
    struct jc_hp_blob_list bl;
    int i;
    int rc;
    int read = 0;   /* M1155 (SV:0438): the BLOBS actually read, printed as the
                     * file-version count -- NOT `n` (the export's named OID
                     * list, which also carries the range's trees/commit). The
                     * mirror push's idiom (n = bl.n) does not apply here: `bl`
                     * is the SUPERSET blob map, and the loop iterates the
                     * export's `n`, so a counter beside the matched>=0 read is
                     * the safe count. */

    jc_snprintf(fmtarg, sizeof fmtarg,
                "--batch-check=%%(objectname) %%(objecttype) %%(objectsize) "
                "%%(rest)");

    /* The range (jc_hp_records_scan: build_exclusion + list_pushed over the
     * single pushed tip) -- the authoritative OIDs the push adds. */
    if (jc_hp_records_scan(a, recs, records_tip, stored_url,
                           e_global, e_system, git_token_file,
                           harness_path, &shas, &n) != 0) {
        rrefuse("the key scan could not list the remote's current tips "
                "(`git ls-remote` of the stored URL %s failed); the records "
                "push is refused (fail-closed: it cannot list what the push "
                "adds)", stored_url);
        return 1;
    }
    if (n == 0) {
        rsay("the key scan: 0 file version(s) checked, 0 with a secret");
        return 0;
    }

    /* The range's blob versions, with paths: the same rev-list range, piped
     * through cat-file --batch-check (M1056's list_pushed_bc shape -- the
     * export hands over OIDs only, so this file re-lists the range with its
     * paths, reading them straight from the recs repo) and parsed into M1056's
     * EXPORTED shared blob list (jc_hp_parse_blobs, SV:0038 -- no longer a
     * local duplicate). A failure refuses (fail-closed). */
    {
        const char *rev_args[64];
        int an = 0;
        jc_sb_init(&rev);
        rev_args[an++] = (char *)"rev-list";
        rev_args[an++] = (char *)"--objects";
        rev_args[an++] = (char *)records_tip;
        /* The remote's current tips are NOT re-listed here: the range is the
         * export's (jc_hp_records_scan already applied the two exclusions).
         * For the batch-check path map we list the same reachable objects
         * and keep only the blobs the export named; a rev-list of the tip
         * alone is a SUPERSET of the export's range (it may include objects
         * the exclusions drop), so the path map below is keyed by OID and a
         * blob is only checked when the export named it -- the superset
         * never widens the scan. */
        rev_args[an] = NULL;
        rc = git_sb(&rev, recs, e_global, e_system, rev_args, an, NULL,
                    PUSH_OUT_CAP);
        if (rc != 0) {
            rrefuse("the key scan cannot list what the records push adds "
                    "(`git rev-list --objects` of %s/records failed); the "
                    "records push is refused (fail-closed)", br);
            jc_sb_free(&rev);
            return 1;
        }
        jc_sb_init(&bc);
        {
            const char *bargv[2];
            bargv[0] = (char *)"cat-file";
            bargv[1] = (char *)fmtarg;
            rc = git_sb(&bc, recs, e_global, e_system, bargv, 2, rev.data,
                        PUSH_OUT_CAP);
            if (rc != 0) {
                rrefuse("the key scan could not read the pushed file versions; the "
                        "records push is refused (fail-closed)");
                jc_sb_free(&rev);
                jc_sb_free(&bc);
                return 1;
            }
        }
        memset(&bl, 0, sizeof bl);
        bl.a = a;
        if (jc_hp_parse_blobs(bc.data, &bl) != 0) {
            rrefuse("the key scan could not read the pushed file versions; the "
                    "records push is refused (fail-closed)");
            jc_sb_free(&rev);
            jc_sb_free(&bc);
            return 1;
        }
        jc_sb_free(&rev);
        jc_sb_free(&bc);
    }

    /* Read each blob the export named and check it for a secret. */
    for (i = 0; i < n; i++) {
        const char *oid = shas[i];
        int matched = -1;
        int m;
        for (m = 0; m < bl.n; m++) {
            if (strcmp(bl.shas[m], oid) == 0) {
                matched = m;
                break;
            }
        }
        if (matched < 0) {
            /* A commit / tree OID the export named (not a blob version):
             * nothing to scan for a secret. */
            continue;
        }
        read = read + 1;   /* M1155 (SV:0438): a blob version was read below;
                            * this is the count the file-version line carries. */
        {
            struct jc_sb blob;
            int brc;
            /* ONE per-blob read (jc_hp_blob_read, jc_harness_push.h -- the
             * SHARED helper the M1056 `mirror push` key scan also calls,
             * SV:2239, so the two copies cannot drift apart again): 0 = read
             * (the bytes may be EMPTY), 1 = a genuine cat-file failure
             * (brc != 0, reported in brc for the refusal's
             * `(cat-file rc=%d: %s)` clause). Refuse ONLY on that genuine
             * failure (rc != 0). A 0-byte blob is read CLEAN, not as a read
             * error: git cat-file of an empty object succeeds (rc 0) and
             * emits no bytes, so the capture is data==NULL / len==0
             * (jc_sb_init's untouched state, no append ever ran) -- that
             * empty result is a LEGITIMATE read, not a failure. A 0-byte
             * object cannot hold a secret, so it is clean; the earlier
             * `|| blob.data == NULL || blob.len == 0` clause refused it
             * fail-closed, making every in-loop records push die on the
             * drive's own snapshot commits (runs/<N>/.started, a 0-byte
             * file -> git's EMPTY blob e69de29...). The fingerprint was the
             * refusal's own text: "could not be read (cat-file rc=0: )" --
             * rc 0 with empty detail, the read SUCCEEDED.
             * jc_hp_bytes_contains is safe over a NULL/0-byte haystack
             * (returns 0 for the needle >= 12 bytes), so the check below
             * still runs. */
            if (jc_hp_blob_read(&blob, recs, e_global, e_system,
                                bl.shas[matched], &brc) != 0) {
                char cut[224];
                cut_copy(cut, sizeof cut, blob.data);
                rrefuse("the key scan: the records file version %s (%s) could "
                        "not be read (cat-file rc=%d: %s); the records push is "
                        "refused (fail-closed)", bl.shas[matched],
                        bl.paths[matched], brc, cut);
                jc_sb_free(&blob);
                return 1;
            }
            if (jc_hp_bytes_contains(blob.data, (int)blob.len, model_key,
                                     model_key_len) ||
                jc_hp_bytes_contains(blob.data, (int)blob.len, git_token,
                                     git_token_len)) {
                jc_sb_free(&blob);
                rsay("the key scan: %d file version(s) checked, 1 with a secret "
                     "(refused)", read);
                rrefuse("the key scan: the records file version %s contains a "
                        "secret (the model key or the git token); the records "
                        "push is refused", bl.paths[matched]);
                return 1;
            }
            jc_sb_free(&blob);
        }
    }
    rsay("the key scan: %d file version(s) checked, 0 with a secret", read);
    return 0;
}

/* `push records`: the records push (B1). config_path non-NULL (the
 * dispatch requires --config); positional the records dir; rest/n_rest the
 * positionals after it (none expected). */
int
jc_harness_push_records_run(const char *config_path, const char *positional,
                            struct jc_arena *a, const char **rest,
                            int n_rest)
{
    struct jc_harness_config cfg;
    char e_global[PATH_CAP];
    char e_system[PATH_CAP];
    char stored[PATH_CAP];
    char *stored_url;
    char *model_key = NULL;
    char *git_token = NULL;
    int model_key_len = 0;
    int git_token_len = 0;
    char harness_path[PATH_CAP];
    char refspec[512];
    char records_ref[512];
    char records_tip[41];
    int rc = 0;   /* the soft-failure accumulator (the reference's rc) */
    int have_tip = 0;
    int load;

    (void)rest;
    if (positional == NULL || positional[0] == '\0') {
        rrefuse("needs the records directory: "
                "jichi-harness push records --config <f> <recs-dir>");
        return 2;
    }
    if (n_rest != 0) {
        rrefuse("takes exactly one positional (the records directory): "
                "jichi-harness push records --config <f> <recs-dir>");
        return 2;
    }
    if (config_path == NULL) {
        rrefuse("--config <path> is required");
        return 2;
    }
    load = load_cfg(config_path, a, &cfg);
    if (load != 0) {
        return load;   /* load_cfg's refusals are exit 2 */
    }
    if (!jc_is_dir(positional)) {
        rrefuse("the records directory does not exist or is not a directory: "
                "%s", positional);
        return 2;
    }
    if (cfg.dir == NULL || cfg.dir[0] == '\0') {
        rrefuse("the config has no `dir`, so the harness's state (the empty "
                "GIT_CONFIG files) cannot be located");
        return 2;
    }
    /* The stored config copy (B1.1): <recs-parent>/<basename>.git-config.
     * harness-copy, the URL guard's reference and the push destination. */
    if (!build_recs_stored_copy(positional, stored, sizeof stored)) {
        rrefuse("the records path is too long to build the stored-copy path");
        return 2;
    }
    if (!make_empty_env_files(cfg.dir, a, e_global, sizeof e_global,
                              e_system, sizeof e_system)) {
        rrefuse("could not create the empty GIT_CONFIG_GLOBAL/SYSTEM files "
                "in the harness's dir %s", cfg.dir);
        return 1;
    }
    /* The URL guard (B1.1, M1056's stored_origin_url pattern, adapted to a
     * NON-BARE read repo): the push DESTINATION is the stored config copy's
     * `remote.origin.url`, pinned and never the (mutable) live origin name.
     * The stored copy must exist and name a URL, else exit 2. Unlike M1056's
     * mirror guard -- where the mirror IS the push client and a live
     * `set-url`/`pushurl` plant would redirect the push, so the guard
     * compares the live effective URL to the stored one -- the records port
     * reads the recs repo DIRECTLY and pushes by the stored ABSOLUTE URL, so
     * the recs repo's live origin is never the destination and comparing it
     * adds no fence (the CHANGED note: the reference's push also runs from a
     * SEPARATE repo, recs-push.git, and never reads recs/'s live origin). A
     * recs repo made by `git clone` (the driver's cells 2/5) has a live
     * origin = the clone source while the stored copy names the real remote;
     * refusing on that mismatch would break exactly those cells, so the guard
     * here is the stored-URL parse + push-by-stored-URL, not a live==stored
     * refusal (b1's measurement decision 1). */
    stored_url = stored_origin_url(stored, a);
    if (stored_url == NULL || stored_url[0] == '\0') {
        rrefuse("could not parse `remote.origin.url` from the stored config "
                "copy at %s -- the push destination is unknown; the records "
                "push is refused", stored);
        return 2;
    }
    /* The two secrets, fail-closed (each must exist, be readable, be >= 12
     * bytes after trim). A refusal names WHICH secret could not be loaded.
     * (b1's load_secret printed its own line; the export jc_hp_load_secret
     * prints nothing, so the which-secret line is printed here.) */
    if (jc_hp_load_secret(a, cfg.model_key_file, &model_key,
                          &model_key_len) != 0) {
        rrefuse("the model key could not be loaded (%s); nothing can be "
                "checked (fail-closed); the records push is refused",
                model_key);
        return 1;
    }
    if (jc_hp_load_secret(a, cfg.git_token_file, &git_token,
                          &git_token_len) != 0) {
        rrefuse("the git token could not be loaded (%s); nothing can be "
                "checked (fail-closed); the records push is refused",
                git_token);
        return 1;
    }
    /* The harness's own path, resolved BEFORE the git children: the remote
     * fetch, the key-scan ls-remote, and the push all name the credential
     * helper by its absolute path (M1056's P1 fix). */
    if (!self_exe(harness_path, sizeof harness_path)) {
        rrefuse("could not resolve the harness's own path (readlink "
                "/proc/self/exe failed); the records push cannot name the "
                "credential helper; refused");
        return 1;
    }
    /* The single records refspec: refs/heads/<prefix>/records -> the same
     * name on the remote (the reference's L80 refspec, never --force). */
    if (jc_snprintf(refspec, sizeof refspec,
                    "refs/heads/%s/records:refs/heads/%s/records",
                    cfg.drive_branch_prefix, cfg.drive_branch_prefix)
        >= (int)sizeof refspec) {
        rrefuse("the branch prefix is too long to build the records refspec");
        return 2;
    }
    if (jc_snprintf(records_ref, sizeof records_ref,
                    "refs/heads/%s/records", cfg.drive_branch_prefix)
        >= (int)sizeof records_ref) {
        rrefuse("the branch prefix is too long to build the records ref");
        return 2;
    }

    /* Step 5 (push.sh L75-76): the remote fetch of the records ref,
     * rc-NEUTRAL -- the orphan branch may not exist on the remote yet. A
     * failure is logged and does NOT set rc (the command runs to the end). */
    {
        struct jc_sb out;
        char fetchref[512];
        const char *argv[6];
        int an = 0;
        int g;
        if (jc_snprintf(fetchref, sizeof fetchref,
                        "+refs/heads/%s/records:refs/remotes/origin/%s/records",
                        cfg.drive_branch_prefix, cfg.drive_branch_prefix)
            >= (int)sizeof fetchref) {
            rrefuse("the branch prefix is too long to build the records "
                    "fetch refspec");
            return 2;
        }
        jc_sb_init(&out);
        argv[an++] = (char *)"fetch";
        argv[an++] = (char *)"-q";
        argv[an++] = (char *)"--no-tags";
        argv[an++] = (char *)stored_url;
        argv[an++] = fetchref;
        argv[an] = NULL;
        g = cred_child(&out, a, positional, harness_path,
                       stored_url, cfg.git_token_file, argv, an,
                       PUSH_SMALL_CAP);
        if (g < 0) {
            g = 128;   /* a build/spawn failure: the fetch is rc-NEUTRAL
                         * anyway (the log below), but it must not read as a
                         * clean 0. */
        }
        if (g != 0) {
            rsay("no records branch on the remote yet (or the fetch failed, "
                 "rc=%d)", g);
        }
        jc_sb_free(&out);
    }

    /* Step 6 (push.sh L77-79): the drive-records fetch -- reading the local
     * records branch (the CHANGED note: the port reads <dir>/recs DIRECTLY
     * with the M2-neutral git child). A failure (the records branch is not
     * in the records repo) sets rc=1 but the command runs to the end. */
    {
        const char *argv[3];
        char *out = (char *)jc_arena_calloc(a, PUSH_SMALL_CAP);
        char err_line[512];
        char *line;
        int g;
        if (out == NULL) {
            rrefuse("out of memory (the records tip capture buffer)");
            return 1;
        }
        argv[0] = (char *)"rev-parse";
        argv[1] = records_ref;
        argv[2] = NULL;
        g = git_read(a, positional, argv, 2, PUSH_SMALL_CAP, out, err_line);
        line = (g == 0) ? first_line(out, a) : NULL;
        if (line != NULL && strlen(line) == 40) {
            memcpy(records_tip, line, 40);
            records_tip[40] = '\0';
            have_tip = 1;
        }
        if (!have_tip) {
            rc = 1;
            rsay("the fetch from the drive's records failed (rc=%d)", g);
        }
    }

    /* The key scan (B1.2, KEY SCAN ONLY -- no size check): a refusal is a
     * HARD stop, exit 1, before any push (the reference's pre-push hook,
     * the port's pre-push C gate). */
    if (have_tip) {
        if (records_key_scan(positional, stored_url, cfg.drive_branch_prefix,
                             records_tip, model_key, model_key_len,
                             git_token, git_token_len, e_global,
                             e_system, a, cfg.git_token_file, harness_path)
            != 0) {
            return 1;
        }
    }

    /* Step 7 (push.sh L80-81): the push, by the stored absolute URL (never
     * the `origin` name), refspec records:records, NEVER --force, with the
     * credential wiring. A failure sets rc=1 but the command runs to the
     * end. */
    if (have_tip) {
        struct jc_sb out;
        const char *args[6];
        int an = 0;
        int g;
        jc_sb_init(&out);
        args[an++] = (char *)"push";
        args[an++] = (char *)"-q";
        args[an++] = (char *)stored_url;
        args[an++] = refspec;
        args[an] = NULL;
        g = cred_child(&out, a, positional, harness_path,
                       stored_url, cfg.git_token_file, args, an,
                       PUSH_SMALL_CAP);
        if (g != 0) {
            rc = 1;
            rsay("the records push failed (rc=%d)", g);
        } else {
            rsay("the records push of %s/records to %s landed",
                 cfg.drive_branch_prefix, stored_url);
        }
        jc_sb_free(&out);
    }

    /* Step 8 (push.sh L82-83): the final rc line; the command always runs
     * to the end and exits rc. */
    rsay("end rc=%d", rc);
    return rc;
}
