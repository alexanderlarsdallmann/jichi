/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_backlog.c - the `push backlog` subcommand body (B3, M1147, batch
 * H7): a refused backlog of the TEAM branch pushed in FAST-FORWARD steps,
 * oldest TIP first, FROM THE HOST-ONLY push.git. The C89 port of the
 * 2026-10-27 hardening-host drive's push-backlog.sh, behaviour B3.
 *
 * One file per push subcommand (the a1 restructure moved the W1 entry points
 * out of the shared jc_harness_push.c, one owner each). The body was first
 * landed in batch H7 / W2's tree, beside M1056's statics, then dropped at the
 * W2 merge (first-writer-kept behind the stopped b2); it is RE-APPLIED here
 * from its kept ref (3a4633c, commit "M1147 (H7, B3): push backlog -- the
 * stepwise fast-forward push"), adapted to code against the M1147 primitives
 * EXPORTED from jc_harness_push.c instead of the statics it once sat next to:
 *
 *   jc_hp_git_run          (the M2 git child; its env carries the two
 *                          GIT_CONFIG_* clearing slots pointing at /dev/null
 *                          -- the file-free isolation idiom -- plus the
 *                          caller's credential env; supersedes b3's local
 *                          backlog_git, whose slots pointed at two empty
 *                          files under cfg.dir)
 *   jc_hp_cred_flags       (the SV:1939 arena-fixed credential -c flags; the
 *                          dangling-setflag FIX is now the shared export, so
 *                          b3's local backlog_cred_args is DELETED in favour
 *                          of it -- it was byte-identical to what the export
 *                          wraps)
 *   jc_hp_cred_env         (the three credential identity env vars, into
 *                          caller-supplied 512-byte buffers)
 *   jc_hp_load_secret      (one secret by reference, trimmed, fail-closed)
 *   jc_hp_bytes_contains   (the NUL-safe substring search for the key scan)
 *   jc_hp_tip_delta_scan   (M1056's range rule pointed at the TIP: the OIDs
 *                          reachable from the TIP and from NONE of the
 *                          remote's current main tip + push.git's tracking
 *                          refs -- the step's key-scan + size-check range,
 *                          Reading B)
 *
 * What b3's body KEEPS and re-applies verbatim (the reference's shape):
 *   - the usage refusal (no TIP -> exit 2, `usage: push backlog TIP...`);
 *   - the two ancestor checks (is-not-on-local-main, not-a-FF-of-remote);
 *   - the START-OF-COMMAND fetch of push.git's tracking ref (documented
 *     CHANGED vs the reference: the reference RELIES ON the sibling push.sh to
 *     have kept that tracking ref in step; the port is standalone and refreshes
 *     it itself, so a remote that advanced elsewhere cannot look like a
 *     fast-forward);
 *   - the stepwise loop, oldest TIP first, BOTH M1056 checks ON (key scan +
 *     size, 8 MiB per file / 20 MiB per step) over the TIP's delta, then the
 *     push by the MIRROR'S STORED URL (never the `origin` name), NEVER
 *     --force;
 *   - the per-step RE-FETCH after the push (push.git's tracking ref must say
 *     what the remote holds now, so the next step's ancestor check reads the
 *     landed tip);
 *   - the stop-at-first-refusal / `done` log lines, and the over-limit SPLIT
 *     (the first valid step lands, the second is refused, NO `done`).
 *   NO lock (the reference has none; B3.6).
 *
 * What b3's body CHANGES in this re-apply (the restructure, not the behaviour):
 *   - b3's local `backlog_cred_args` (the arena-SET-flag fix) is DELETED:
 *     `jc_hp_cred_flags` is now the SV:1939 arena-fixed export, the ONE
 *     helper (not a second copy).
 *   - b3's local `backlog_git` (the M2 git child) is DELETED: `jc_hp_git_run`
 *     is the export; its GIT_CONFIG_* slots point at /dev/null instead of the
 *     two empty files b3's make_empty_env_files created under cfg.dir.
 *   - b3's `backlog_checks` wrapped M1056's `build_exclusion` +
 *     `list_pushed_bc` (which carries the `%(objectsize)` column the size
 *     check read without re-reading the blob). The re-exported
 *     `jc_hp_tip_delta_scan` returns only a PLAIN OID list (the hp_range_shas
 *     parse, no size column), so the size check here MEASURES each blob's size
 *     via `git cat-file -s` (and reads its content via `git cat-file blob`,
 *     the SAME bytes M1056's key scan reads) instead of reading a batch-check
 *     column. The check's DECISION is unchanged: a blob > 8 MiB, or the sum of
 *     the step's blob versions > 20 MiB, refuses; a blob carrying either
 *     secret refuses. `backlog_checks` is therefore KEPT as a LOCAL wrapper
 *     that CALLS `jc_hp_tip_delta_scan` for the range (the export's shape
 *     cannot express the step's size delta on its own), then classifies +
 *     reads the blobs.
 */

#include "jc_harness_push.h"
#include "jc_harness_config.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_vec.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>       /* the log stamp (the reference's date '+%F %T %z') */
#include <unistd.h>     /* readlink /proc/self/exe (the harness's own path) */

#define BACKLOG_PATH_CAP   4352   /* local mirror of jc_harness_push.c PATH_CAP */
#define BACKLOG_SMALL_CAP  (256 * 1024)    /* is-ancestor / rev-parse / -t / -s */
#define BACKLOG_BLOB_CAP   (64 * 1024 * 1024)  /* git cat-file blob (content) */
#define BACKLOG_FILE_MB    8UL   /* the size check's per-file-version limit */
#define BACKLOG_TOTAL_MB   20UL  /* the size check's per-step limit */
#define BACKLOG_SECRET_MIN 12    /* jc_hp_load_secret's trimmed minimum */

/* A named refusal on stderr (the `jichi-harness: mirror: ...` shape the
 * sibling bodies use); the caller returns the exit class. */
static void
backlog_refuse(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: mirror: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

/* The log stamp, the reference's `date '+%F %T %z'` without a date child:
 * strftime for the local wall clock, then %z's +HHMM offset appended
 * (the driver's runlog only needs the line text, not the stamp, so a
 * best-effort stamp is fine; a failed localtime falls back to a bare date). */
static void
backlog_stamp(char *out, size_t cap)
{
    time_t t;
    struct tm tmv;
    int off_min;
    size_t n;

    out[0] = '\0';
    if (cap < 2) {
        return;
    }
    t = time(NULL);
    if (localtime_r(&t, &tmv) == NULL) {
        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = 70; tmv.tm_mon = 0; tmv.tm_mday = 1;
        tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
        off_min = 0;
    } else {
        time_t utc = mktime(&tmv);   /* mktime: the local wall clock's epoch */
        double dd = difftime(t, utc);
        off_min = (int)(dd / 60.0);  /* local is UTC + off_min */
    }
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
    if (n == 0) {
        out[0] = '\0';
        return;
    }
    if (cap > n + 7) {
        int mag = (off_min < 0) ? -off_min : off_min;
        out[n] = ' ';
        out[n + 1] = (off_min < 0) ? '-' : '+';
        out[n + 2] = (char)('0' + mag / 3600 / 10);
        out[n + 3] = (char)('0' + mag / 3600 % 10);
        out[n + 4] = (char)('0' + mag / 60 % 10);
        out[n + 5] = (char)('0' + mag % 10);
        out[n + 6] = '\0';
    }
}

/* Append `<stamp> backlog: <line>` to push.log (the reference's log(): every
 * line APPENDED, the `date` stamp then the text). A log that cannot be opened
 * is dropped, never fatal to the command's exit class (the reference does not
 * `set -e`; a missing LOG dir would just lose the line). */
static void
backlog_log(const char *log_path, const char *line)
{
    char stamped[640];
    char stamp[40];
    FILE *f;

    if (log_path == NULL || line == NULL) {
        return;
    }
    backlog_stamp(stamp, sizeof stamp);
    if (jc_snprintf(stamped, sizeof stamped, "%s backlog: %s", stamp, line)
        >= (int)sizeof stamped) {
        return;   /* a line that cannot be stamped cannot be logged */
    }
    f = fopen(log_path, "ab");
    if (f == NULL) {
        return;
    }
    fputs(stamped, f);
    fputc('\n', f);
    fclose(f);
}

/* One git child in the push.git store, via the exported jc_hp_git_run. The
 * GIT_CONFIG_* isolation slots point at /dev/null (jc_hp_git_run's own
 * file-free idiom, added there); when `n_cred` / `n_env` are non-zero the
 * credential -c flags + identity env ride along (the push / fetch children),
 * and the read children pass NULL / 0 / NULL / 0. `out` (>= BACKLOG_SMALL_CAP
 * or the caller's cap) receives the combined output, truncated; `err_line`
 * (>= 512) receives the first non-empty line. Returns the child's exit status,
 * -1 on spawn failure, -2 on timeout. The args array is declared INCOMPLETE
 * (`const char *args[]`) so the caller may pass small argv's without tripping
 * -Wstringop-overflow against jc_hp_git_run's [64] bound (the M1056 idiom). */
static int
backlog_git(const char *pushgit, char *cred_flags[4], int n_cred,
            const char *cred_env[4], int n_env,
            const char *args[], int n_args,
            int cap, char *out, char *err_line)
{
    return jc_hp_git_run(NULL, pushgit, cred_flags, n_cred,
                         cred_env, n_env, args, n_args, cap, out, err_line);
}

/* `git -C pushgit rev-parse <ref>`: the full 40-char SHA (the first line of
 * the capture), or -1 when the ref is absent / git failed. `buf` receives the
 * SHA (the caller sized it >= 41). */
static int
backlog_rev_parse_sha(char *buf, const char *pushgit, const char *ref)
{
    const char *argv[2];
    char out[BACKLOG_SMALL_CAP];
    char err[512];
    int rc;
    const char *p;
    const char *nl;
    size_t ll;

    argv[0] = (char *)"rev-parse";
    argv[1] = (char *)ref;
    rc = backlog_git(pushgit, NULL, 0, NULL, 0, argv, 2,
                     BACKLOG_SMALL_CAP, out, err);
    if (rc != 0) {
        return -1;
    }
    p = out;
    nl = strchr(p, '\n');
    ll = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
    if (ll != 40) {
        return -1;
    }
    memcpy(buf, p, 40);
    buf[40] = '\0';
    return 0;
}

/* `git -C pushgit rev-parse --short <ref>`: the abbreviated SHA (the
 * reference's L30 form) for the step's `the remote's <prefix>/main is
 * <short>` line. `buf` receives the first line (the caller sized it >= 48).
 * Returns 1 on success, 0 when the ref is absent. */
static int
backlog_rev_parse_short(char *buf, size_t cap, const char *pushgit,
                        const char *ref)
{
    const char *argv[3];
    char out[BACKLOG_SMALL_CAP];
    char err[512];
    int rc;
    const char *p;
    const char *nl;
    size_t ll;

    argv[0] = (char *)"rev-parse";
    argv[1] = (char *)"--short";
    argv[2] = (char *)ref;
    rc = backlog_git(pushgit, NULL, 0, NULL, 0, argv, 3,
                     BACKLOG_SMALL_CAP, out, err);
    if (rc != 0) {
        return 0;
    }
    p = out;
    nl = strchr(p, '\n');
    ll = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
    if (ll == 0 || ll >= cap) {
        return 0;
    }
    memcpy(buf, p, ll);
    buf[ll] = '\0';
    return 1;
}

/* `git -C pushgit cat-file -t <oid>`: the object type as a string (the first
 * line, e.g. "blob" / "commit" / "tree"), or -1 on a git failure. */
static int
backlog_obj_type(const char *pushgit, const char *oid,
                 char *out, int cap)
{
    const char *argv[3];
    char r[BACKLOG_SMALL_CAP];
    char err[512];
    int rc;
    const char *p;
    const char *nl;
    int ll;

    argv[0] = (char *)"cat-file";
    argv[1] = (char *)"-t";
    argv[2] = (char *)oid;
    rc = backlog_git(pushgit, NULL, 0, NULL, 0, argv, 3,
                     BACKLOG_SMALL_CAP, r, err);
    if (rc != 0) {
        return -1;
    }
    p = r;
    nl = strchr(p, '\n');
    ll = (nl != NULL) ? (int)(nl - p) : (int)strlen(p);
    if (ll <= 0 || ll >= cap) {
        return -1;
    }
    memcpy(out, p, (jc_size)ll);
    out[ll] = '\0';
    return 0;
}

/* `git -C pushgit cat-file -s <oid>`: the object size in bytes (the first
 * line, decimal), or -1 on a git failure / non-numeric capture. */
static int
backlog_obj_size(const char *pushgit, const char *oid,
                 unsigned long *out)
{
    const char *argv[3];
    char r[BACKLOG_SMALL_CAP];
    char err[512];
    int rc;
    const char *p;
    unsigned long v;
    int digits;

    argv[0] = (char *)"cat-file";
    argv[1] = (char *)"-s";
    argv[2] = (char *)oid;
    rc = backlog_git(pushgit, NULL, 0, NULL, 0, argv, 3,
                     BACKLOG_SMALL_CAP, r, err);
    if (rc != 0) {
        return -1;
    }
    v = 0;
    digits = 0;
    p = r;
    while (*p >= '0' && *p <= '9') {
        v = v * 10UL + (unsigned long)(*p - '0');
        digits = 1;
        p++;
    }
    if (digits == 0) {
        return -1;
    }
    *out = v;
    return 0;
}

/* One step's TWO CHECKS ON (key scan, then size check), M1056's range rule
 * pointed at the TIP (NOT the mirror's own tip): every object reachable from
 * the TIP and from NONE of (a) the remote's current main tip (the single
 * ^-exclusion) nor (b) push.git's tracking refs (the `--not --remotes=origin`
 * in jc_hp_tip_delta_scan's rev-list) -- i.e. the step's delta, measured in
 * push.git's store.
 *
 * The range comes from the exported jc_hp_tip_delta_scan (Reading B: the
 * step's checks run over the TIP's delta, not a mirror-push call). The export
 * returns only a PLAIN OID list (no %(objectsize) column), so the size check
 * here MEASURES each blob's size via `git cat-file -s` (and the key scan READS
 * its content via `git cat-file blob` -- the SAME bytes M1056's key scan
 * reads). A blob > BACKLOG_FILE_MB, or the sum of the step's blob versions >
 * BACKLOG_TOTAL_MB, refuses; a blob carrying either secret refuses. Returns 0
 * when both pass, 1 when a check refused (the step must not push). */
static int
backlog_checks(const char *pushgit, const char *tip_sha40,
               const char *remote_main_sha40,
               const char *e_global, const char *e_system,
               const char *model_key, int model_key_len,
               const char *git_token, int git_token_len,
               struct jc_arena *a)
{
    char **shas = NULL;
    int nshas = 0;
    int i;
    int ok;
    unsigned long total;

    if (tip_sha40 == NULL || strlen(tip_sha40) != 40) {
        return 1;   /* a TIP that is not a 40-char sha cannot be scanned */
    }
    ok = jc_hp_tip_delta_scan(a, pushgit, tip_sha40, remote_main_sha40,
                              e_global, e_system, &shas, &nshas);
    if (ok != 0 || shas == NULL) {
        return 1;   /* a scan that cannot run is never a pass (fail-closed) */
    }
    total = 0;
    for (i = 0; i < nshas; i++) {
        char type[16];
        unsigned long size;
        if (backlog_obj_type(pushgit, shas[i], type, sizeof type) != 0) {
            return 1;   /* a delta object we cannot classify: fail-closed */
        }
        if (strcmp(type, "blob") != 0) {
            continue;   /* commits + trees carry no content / size */
        }
        if (backlog_obj_size(pushgit, shas[i], &size) != 0) {
            return 1;   /* a blob we cannot size: fail-closed */
        }
        if (size > (BACKLOG_FILE_MB * 1048576UL)) {
            return 1;   /* the per-file-version limit (8 MiB) */
        }
        total += size;
        if (total > (BACKLOG_TOTAL_MB * 1048576UL)) {
            return 1;   /* the per-step limit (20 MiB) -- the split */
        }
        /* The KEY SCAN: read the blob's content (the SAME bytes M1056's key
         * scan reads) and search for both secrets. A blob that cannot be read
         * is fail-closed. (size <= 8 MiB here, so the read is bounded by
         * BACKLOG_BLOB_CAP; we never read the content of a blob that already
         * tripped the per-file limit.) */
        {
            char *content;
            const char *argv[3];
            char err[512];
            int cc;
            content = (char *)jc_arena_calloc(a, (jc_size)BACKLOG_BLOB_CAP);
            if (content == NULL) {
                return 1;
            }
            argv[0] = (char *)"cat-file";
            argv[1] = (char *)"blob";
            argv[2] = (char *)shas[i];
            cc = backlog_git(pushgit, NULL, 0, NULL, 0, argv, 3,
                             BACKLOG_BLOB_CAP, content, err);
            if (cc != 0) {
                return 1;   /* a blob the key scan cannot read */
            }
            if ((model_key_len > 0 &&
                 jc_hp_bytes_contains(content, (int)size, model_key,
                                      model_key_len)) ||
                (git_token_len > 0 &&
                 jc_hp_bytes_contains(content, (int)size, git_token,
                                      git_token_len))) {
                return 1;   /* a secret in the step's delta */
            }
        }
    }
    return 0;
}

/* The mirror's stored origin URL (the step's push destination): parse
 * `remote.origin.url` from the stored config copy (
 * <mirror's parent>/mirror.git-config.harness-copy). Returns an
 * arena-allocated string, or NULL when the copy is absent / unparseable.
 * (The mirror's parent is <cfg.dir>; the stored copy sits beside the mirror
 * in the harness's dir, the exact path init / mirror write it at.) */
static char *
backlog_stored_origin_url(const char *mirror, const char *base_hint,
                          struct jc_arena *a)
{
    char stored[BACKLOG_PATH_CAP];
    char *text = NULL;
    jc_size len = 0;
    const char *p;
    int in_remote = 0;
    char *out;

    {
        const char *slash;
        slash = strrchr(mirror, '/');
        if (slash != NULL && slash != mirror) {
            size_t blen = (size_t)(slash - mirror);
            char tmp[BACKLOG_PATH_CAP];
            if (blen + 40 >= sizeof tmp) {
                return NULL;
            }
            memcpy(tmp, mirror, blen);
            tmp[blen] = '\0';
            jc_snprintf(stored, sizeof stored,
                        "%s/mirror.git-config.harness-copy", tmp);
        } else if (base_hint != NULL && base_hint[0] != '\0') {
            jc_snprintf(stored, sizeof stored,
                        "%s/mirror.git-config.harness-copy", base_hint);
        } else {
            jc_snprintf(stored, sizeof stored,
                        "%s", "mirror.git-config.harness-copy");
        }
    }
    if (jc_read_file(stored, &text, &len, a) != JC_OK) {
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

/* The harness's own executable path (readlink /proc/self/exe, the M1053
 * idiom): the credential.helper flag needs its absolute path. */
static int
backlog_self_exe(char *out, size_t cap)
{
    char buf[BACKLOG_PATH_CAP];
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

/* `push backlog TIP...`: a refused backlog in fast-forward steps (B3).
 * positional the first TIP; rest/n_rest the remaining ones. */
int
jc_harness_push_backlog_run(const char *config_path, const char *positional,
                            struct jc_arena *a, const char **rest, int n_rest)
{
    struct jc_harness_config cfg;
    jc_status st;
    char pushgit[BACKLOG_PATH_CAP];
    char log_path[BACKLOG_PATH_CAP];
    char e_global[BACKLOG_PATH_CAP];
    char e_system[BACKLOG_PATH_CAP];
    char mirror[BACKLOG_PATH_CAP];
    char *stored_url = NULL;
    char *model_key = NULL;
    char *git_token = NULL;
    int model_key_len = 0;
    int git_token_len = 0;
    char harness_path[BACKLOG_PATH_CAP];
    char *cflags[4];
    char env0[512], env1[512], env2[512];
    const char *penv[4];
    char *envout[4];
    int nc = 0;
    int ne = 0;
    struct jc_vec tips;
    int i;

    /* (1) Usage: at least one TIP (positional the first), else a usage
     * refusal on stderr, exit 2 (push-backlog.sh L19). The reference's
     * `usage: push-backlog.sh TIP...` becomes the verb's own line:
     * `usage: push backlog TIP...`. */
    if (positional == NULL || positional[0] == '\0') {
        fprintf(stderr, "usage: push backlog TIP...\n");
        return 2;
    }
    if (n_rest < 0) {
        n_rest = 0;
    }
    /* The TIP list: EVERY positional, oldest first (the reference's
     * `for t in "$@"` is unbounded; the M1147 port's fixed tips[8] silently
     * dropped a 9th+ TIP and still reported done -- the M1151 defect b).
     * A jc_vec of `const char *`, reserved for 1 + n_rest up front; the
     * vectors own the SLOTS only, the strings are the caller's argv. */
    jc_vec_init(&tips, sizeof (const char *));
    if (jc_vec_reserve(&tips, (jc_size)(1 + n_rest)) != JC_OK) {
        backlog_refuse("out of memory sizing the TIP list");
        jc_vec_free(&tips);
        return 1;
    }
    if (jc_vec_push(&tips, &positional) != JC_OK) {
        backlog_refuse("out of memory holding the TIP list");
        jc_vec_free(&tips);
        return 1;
    }
    for (i = 0; i < n_rest; i++) {
        if (jc_vec_push(&tips, &rest[i]) != JC_OK) {
            backlog_refuse("out of memory holding the TIP list");
            jc_vec_free(&tips);
            return 1;
        }
    }

    st = jc_harness_config_load(config_path, &cfg, a);
    if (st != JC_OK) {
        const char *msg = jc_harness_config_error(&cfg);
        backlog_refuse("%s",
                       (msg != NULL) ? msg : "could not read the harness config");
        jc_vec_free(&tips);
        return 2;
    }
    if (cfg.drive_branch_prefix == NULL || cfg.drive_branch_prefix[0] == '\0') {
        backlog_refuse("the config has no drive.branchPrefix (the refset's "
                       "$BR); push cannot name the team's branches");
        jc_vec_free(&tips);
        return 2;
    }
    /* The host block names pushHome; a missing one (a container config) is a
     * named refusal (B1's rule: the host commands are host-side by
     * construction and must not guess a dir). */
    if (!cfg.host_dir_present ||
        cfg.host_push_home == NULL || cfg.host_push_home[0] == '\0') {
        backlog_refuse("the config has no host block (host.pushHome) -- "
                       "`push backlog` is a host command and cannot run from "
                       "a container config");
        jc_vec_free(&tips);
        return 2;
    }
    if (cfg.dir == NULL || cfg.dir[0] == '\0') {
        backlog_refuse("the config has no `dir`, so the harness's state "
                       "cannot be located");
        jc_vec_free(&tips);
        return 2;
    }
    if (jc_snprintf(pushgit, sizeof pushgit, "%s/push.git",
                    cfg.host_push_home) >= (int)sizeof pushgit) {
        backlog_refuse("pushHome's path is too long to build push.git's path");
        jc_vec_free(&tips);
        return 2;
    }
    if (!jc_is_dir(pushgit)) {
        backlog_refuse("push.git does not exist or is not a directory: %s "
                       "-- the host's push home was not set up by "
                       "`jichi-harness init`", pushgit);
        jc_vec_free(&tips);
        return 2;
    }
    if (jc_snprintf(log_path, sizeof log_path, "%s/push.log",
                    cfg.host_push_home) >= (int)sizeof log_path) {
        backlog_refuse("pushHome's path is too long to build push.log's path");
        jc_vec_free(&tips);
        return 2;
    }
    /* The MIRROR (the harness's own refset client, <dir>/mirror.git) and its
     * stored URL (the team push's destination). The step's push/fetch/scan
     * run -C push.git but by the MIRROR's stored URL -- the URL the team push
     * lands on (standin.git in the rehearsal fixture). */
    if (jc_snprintf(mirror, sizeof mirror, "%s/mirror.git", cfg.dir)
        >= (int)sizeof mirror) {
        backlog_refuse("the mirror's path is too long");
        jc_vec_free(&tips);
        return 2;
    }
    stored_url = backlog_stored_origin_url(mirror, cfg.dir, a);
    if (stored_url == NULL || stored_url[0] == '\0') {
        backlog_refuse("could not parse `remote.origin.url` from the stored "
                       "config copy beside %s -- the step's push destination "
                       "is unknown", mirror);
        jc_vec_free(&tips);
        return 2;
    }
    /* The two GIT_CONFIG_* clearing slots for jc_hp_tip_delta_scan's git
     * children: point them at /dev/null (the same empty-config isolation
     * jc_hp_git_run adds to its own children). */
    jc_snprintf(e_global, sizeof e_global, "%s",
                "GIT_CONFIG_GLOBAL=/dev/null");
    jc_snprintf(e_system, sizeof e_system, "%s",
                "GIT_CONFIG_SYSTEM=/dev/null");
    /* The two secrets, fail-closed (the step's key scan needs both). */
    if (jc_hp_load_secret(a, cfg.model_key_file, &model_key, &model_key_len)
        != 0) {
        backlog_refuse("the model key could not be loaded: %s is missing, "
                       "unreadable, or shorter than %d bytes after trimming "
                       "(fail-closed: the key scan cannot run); refused",
                       (cfg.model_key_file != NULL) ? cfg.model_key_file
                                                     : "the config names no file",
                       BACKLOG_SECRET_MIN);
        jc_vec_free(&tips);
        return 1;
    }
    if (jc_hp_load_secret(a, cfg.git_token_file, &git_token, &git_token_len)
        != 0) {
        backlog_refuse("the git token could not be loaded: %s is missing, "
                       "unreadable, or shorter than %d bytes after trimming "
                       "(fail-closed: the key scan cannot run); refused",
                       (cfg.git_token_file != NULL) ? cfg.git_token_file
                                                     : "the config names no file",
                       BACKLOG_SECRET_MIN);
        jc_vec_free(&tips);
        return 1;
    }
    /* The harness's own path, resolved BEFORE the steps: the push/fetch
     * children need the credential helper's absolute path. */
    if (!backlog_self_exe(harness_path, sizeof harness_path)) {
        backlog_refuse("could not resolve the harness's own path (readlink "
                       "/proc/self/exe failed); the step's push cannot name "
                       "the credential helper; refused");
        jc_vec_free(&tips);
        return 1;
    }
    /* The credential -c flags + env, ONE set for the push / fetch children
     * (M1056's P1 fix). jc_hp_cred_flags is the SV:1939 arena-fixed export
     * (b3's local backlog_cred_args is DELETED in favour of it); jc_hp_cred_
     * env formats the three identity env vars into caller buffers so they
     * outlive every git child. The rule (none / oauth2) is derived from the
     * URL inside jc_hp_cred_env. */
    nc = jc_hp_cred_flags(a, harness_path, cflags);
    if (nc != 3) {
        backlog_refuse("the credential -c flags could not be built (the "
                       "harness path is too long); refused");
        jc_vec_free(&tips);
        return 1;
    }
    env0[0] = '\0';
    env1[0] = '\0';
    env2[0] = '\0';
    envout[0] = env0;
    envout[1] = env1;
    envout[2] = env2;
    envout[3] = NULL;
    jc_hp_cred_env(cfg.git_token_file, NULL, stored_url, envout);
    penv[0] = env0;
    penv[1] = env1;
    penv[2] = env2;
    penv[3] = NULL;
    if (env0[0] == '\0' || env1[0] == '\0' || env2[0] == '\0') {
        backlog_refuse("the credential env could not be built (a value is "
                       "too long); refused");
        jc_vec_free(&tips);
        return 1;
    }
    ne = 3;

    /* The start-of-command fetch (CHANGED vs the reference, documented): the
     * reference's push-backlog.sh reads push.git's
     * refs/remotes/origin/<prefix>/main as its "remote's <prefix>/main" for
     * the not-a-FF check, and RELIES ON the sibling push.sh (push.sh L53-55)
     * to have kept that tracking ref in step with the real remote at the
     * start of the run. The port's `push backlog` is STANDALONE (no push.sh
     * has run first), so it does the equivalent refresh itself before the
     * stepwise loop: fetch the stored URL's <prefix> branch ref into
     * push.git's tracking ref, force-updating (the `+`, like the per-step
     * re-fetch below, because the remote may have moved a tip). This is what
     * makes the not-a-FF check read the remote's CURRENT tip rather than a
     * possibly-stale tracking ref -- without it a remote that advanced
     * elsewhere would look like a fast-forward and the step would push a
     * diverged line backwards. A failure here cannot verify the remote, so
     * the command stops (rc=1), exactly like the reference's equivalent
     * (push.sh L53-55). For a remote that has not moved (the common case, and
     * cells 2/4/5/6 of the smoke) it is a no-op. */
    {
        char rs[640];
        const char *args[6];
        int an = 0;
        int frc;
        char out[BACKLOG_SMALL_CAP];
        char err[512];
        if (jc_snprintf(rs, sizeof rs,
                        "+refs/heads/%s/main:refs/remotes/origin/%s/main",
                        cfg.drive_branch_prefix, cfg.drive_branch_prefix)
            >= (int)sizeof rs) {
            backlog_refuse("the branch prefix is too long to build the "
                           "refresh refspec");
            jc_vec_free(&tips);
            return 2;
        }
        args[an++] = "fetch";
        args[an++] = "-q";
        args[an++] = "--no-tags";
        args[an++] = stored_url;
        args[an++] = rs;
        args[an] = NULL;
        frc = backlog_git(pushgit, cflags, nc, penv, ne, args, an,
                          BACKLOG_SMALL_CAP, out, err);
        if (frc != 0) {
            char line[700];
            if (jc_snprintf(line, sizeof line,
                            "the fetch of the remote's team refs failed "
                            "(rc=%d): stop", frc) < (int)sizeof line) {
                backlog_log(log_path, line);
            }
            jc_vec_free(&tips);
            return 1;
        }
    }

    /* The stepwise loop, oldest TIP first (the reference's L20-31). NO lock
     * (B3.6: the reference has none; keep it). The TIP list is the vector
     * built above -- EVERY positional, uncapped (the M1151 defect b fix;
     * only the count bound went, the order and every step are unchanged). */
    for (i = 0; i < (int)tips.len; i++) {
        const char *t = *(const char **)jc_vec_at(&tips, (jc_size)i);
        char mainref[600];
        char trackref[600];
        char tip_sha40[41];
        char remote_sha40[41];
        int remote_valid = 0;

        /* (2) Resolve the TIP to a 40-char SHA (so the checks + the push name
         * an unambiguous object), then is the TIP on push.git's local
         * <prefix>/main? `git -C pushgit merge-base --is-ancestor <t>
         * refs/heads/<prefix>/main`; fail -> the not-on-main refusal, exit 1,
         * BEFORE any push. */
        if (jc_snprintf(mainref, sizeof mainref, "refs/heads/%s/main",
                        cfg.drive_branch_prefix) >= (int)sizeof mainref) {
            backlog_refuse("the branch prefix is too long to build main's ref");
            jc_vec_free(&tips);
            return 2;
        }
        if (backlog_rev_parse_sha(tip_sha40, pushgit, t) != 0) {
            char line[700];
            if (jc_snprintf(line, sizeof line,
                            "%s is not on push.git's %s/main: stop",
                            t, cfg.drive_branch_prefix)
                < (int)sizeof line) {
                backlog_log(log_path, line);
            }
            jc_vec_free(&tips);
            return 1;
        }
        {
            const char *argv4[4];
            int arc;
            char out[BACKLOG_SMALL_CAP];
            char err[512];
            argv4[0] = "merge-base";
            argv4[1] = "--is-ancestor";
            argv4[2] = tip_sha40;
            argv4[3] = mainref;
            arc = backlog_git(pushgit, NULL, 0, NULL, 0, argv4, 4,
                              BACKLOG_SMALL_CAP, out, err);
            if (arc != 0) {
                char line[700];
                if (jc_snprintf(line, sizeof line,
                                "%s is not on push.git's %s/main: stop",
                                t, cfg.drive_branch_prefix)
                    < (int)sizeof line) {
                    backlog_log(log_path, line);
                }
                jc_vec_free(&tips);
                return 1;
            }
        }
        /* (3) Is the TIP a fast-forward of the remote's <prefix>/main?
         * `git -C pushgit merge-base --is-ancestor
         * refs/remotes/origin/<prefix>/main <t>`; fail -> the not-a-FF
         * refusal, exit 1. */
        if (jc_snprintf(trackref, sizeof trackref,
                        "refs/remotes/origin/%s/main",
                        cfg.drive_branch_prefix)
            >= (int)sizeof trackref) {
            backlog_refuse("the branch prefix is too long to build the "
                           "tracking ref");
            jc_vec_free(&tips);
            return 2;
        }
        {
            const char *argv4[4];
            int arc;
            char out[BACKLOG_SMALL_CAP];
            char err[512];
            argv4[0] = "merge-base";
            argv4[1] = "--is-ancestor";
            argv4[2] = trackref;
            argv4[3] = tip_sha40;
            arc = backlog_git(pushgit, NULL, 0, NULL, 0, argv4, 4,
                              BACKLOG_SMALL_CAP, out, err);
            if (arc != 0) {
                char line[700];
                if (jc_snprintf(line, sizeof line,
                                "%s is not a fast-forward of the "
                                "remote's %s/main: stop", t,
                                cfg.drive_branch_prefix)
                    < (int)sizeof line) {
                    backlog_log(log_path, line);
                }
                jc_vec_free(&tips);
                return 1;
            }
        }
        /* The remote's current <prefix>/main tip (the step's delta lower
         * bound for jc_hp_tip_delta_scan's single ^-exclusion); absent when
         * the remote has no main yet (a new remote -- the scan then excludes
         * nothing but the tracking refs). */
        remote_valid = (backlog_rev_parse_sha(remote_sha40, pushgit,
                                              trackref) == 0);
        /* (4) Log the step, then BOTH CHECKS ON (the step's push must go
         * through the mirror-push machinery -- key scan + size check over the
         * TIP's delta, M1056's 8 MiB per file / 20 MiB per step) BEFORE the
         * push. */
        {
            char line[700];
            if (jc_snprintf(line, sizeof line, "push %s", t)
                < (int)sizeof line) {
                backlog_log(log_path, line);
            }
        }
        {
            int ckc = backlog_checks(pushgit, tip_sha40,
                                     remote_valid ? remote_sha40 : NULL,
                                     e_global, e_system,
                                     model_key, model_key_len,
                                     git_token, git_token_len, a);
            if (ckc != 0) {
                char line[700];
                if (jc_snprintf(line, sizeof line,
                                "the push of %s failed (rc=1): stop", t)
                    < (int)sizeof line) {
                    backlog_log(log_path, line);
                }
                jc_vec_free(&tips);
                return 1;
            }
        }
        /* The step's push, by the stored URL, never --force. */
        {
            char refspec[600];
            const char *args[4];
            int an = 0;
            int prc;
            char out[BACKLOG_SMALL_CAP];
            char err[512];
            if (jc_snprintf(refspec, sizeof refspec, "%s:refs/heads/%s/main",
                            tip_sha40, cfg.drive_branch_prefix)
                >= (int)sizeof refspec) {
                backlog_refuse("the branch prefix is too long to build the "
                               "push refspec");
                jc_vec_free(&tips);
                return 2;
            }
            args[an++] = "push";
            args[an++] = "-q";
            args[an++] = stored_url;
            args[an++] = refspec;
            prc = backlog_git(pushgit, cflags, nc, penv, ne, args, an,
                              BACKLOG_SMALL_CAP, out, err);
            if (prc != 0) {
                char line[700];
                if (jc_snprintf(line, sizeof line,
                                "the push of %s failed (rc=%d): stop", t,
                                prc) < (int)sizeof line) {
                    backlog_log(log_path, line);
                }
                jc_vec_free(&tips);
                return 1;
            }
        }
        /* (5) The re-fetch after the step (push.git's tracking ref must say
         * what the remote holds now, so the next step's ancestor check reads
         * the landed tip). */
        {
            char refspec[640];
            const char *args[5];
            int an = 0;
            int frc;
            char out[BACKLOG_SMALL_CAP];
            char err[512];
            if (jc_snprintf(refspec, sizeof refspec,
                            "+refs/heads/%s/main:refs/remotes/origin/%s/main",
                            cfg.drive_branch_prefix, cfg.drive_branch_prefix)
                >= (int)sizeof refspec) {
                backlog_refuse("the branch prefix is too long to build the "
                               "re-fetch refspec");
                jc_vec_free(&tips);
                return 2;
            }
            args[an++] = "fetch";
            args[an++] = "-q";
            args[an++] = "--no-tags";
            args[an++] = stored_url;
            args[an++] = refspec;
            frc = backlog_git(pushgit, cflags, nc, penv, ne, args, an,
                              BACKLOG_SMALL_CAP, out, err);
            if (frc != 0) {
                char line[700];
                if (jc_snprintf(line, sizeof line,
                                "the fetch after %s failed (rc=%d): stop",
                                t, frc) < (int)sizeof line) {
                    backlog_log(log_path, line);
                }
                jc_vec_free(&tips);
                return 1;
            }
        }
        {
            char shortsha[48];
            char line[700];
            if (backlog_rev_parse_short(shortsha, sizeof shortsha, pushgit,
                                        trackref)) {
                if (jc_snprintf(line, sizeof line,
                                "the remote's %s/main is %s",
                                cfg.drive_branch_prefix, shortsha)
                    < (int)sizeof line) {
                    backlog_log(log_path, line);
                }
            }
        }
    }
    /* (6) All steps done. */
    backlog_log(log_path, "done");
    jc_vec_free(&tips);
    return 0;
}
