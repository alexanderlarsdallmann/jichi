/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_mirror.c - `jichi-harness mirror fetch` and `mirror head`
 * (M1055, H3b): the C89 port of the drive's mirror.sh mirror_fetch +
 * mirror_head. The push is M1056 / H3c and is NOT built here.
 *
 * Design note (the contract, measured on git 2.53.0): docs/internal/drive/
 * 2026-10-23-harness/notes/M1055.md. Reference: docs/internal/drive/
 * 2026-10-23-harness/harness/mirror.sh, mirror_fetch + mirror_head only.
 *
 * THE MIRROR IS THE FETCH CLIENT. The team's clone is the source. In Mode A
 * there is no container, so the source is read by a local git child; the
 * surface to neutralise is that child (behaviours 3, 4, 5 of the note) and
 * the mirror itself (the five integrity checks, the note's "mirror-integrity
 * precheck"). The checks run git-free where the note says so: the bare
 * check reads the HEAD file, the config check memcmps the bytes against the
 * stored copy, the alternates check stats the file, the hooks check lists
 * the directory, the ownership check stats the mirror directory. A failed
 * check is exit 2 naming the check, and NO fetch happens.
 *
 * Exit classes (the note, line 118-122): precheck failure = exit 2, the
 * check named; a team-branches fetch that fails at the TRANSPORT (a bad
 * source) = exit 1 with the captured git output (first 200 bytes, M1053's
 * git_must idiom); a non-fast-forward REJECTION (the refspec carries no `+`)
 * = the output captured and reported as the script's line, TOLERATED (the
 * other refspecs in the same fetch still applied -- cell W2); a failed
 * kept-attempt fetch = a report line, not fatal (the script's `|| log`
 * shape). A successful command = exit 0.
 *
 * THE INIT WIRING (the note's "Inputs"): init (M1053, jc_harness_init.c) is
 * the one that creates the mirror; it now also copies the mirror's `config`
 * bytes to <absdir>/mirror.git-config.harness-copy when the mirror is
 * written, so this command's check 3 can memcmp against the exact bytes the
 * harness wrote. A mirror whose stored copy is missing is refused (check 3
 * cannot run), naming the check.
 */

#include "jc_harness_mirror.h"

#include "jc_harness_config.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_proc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* getuid */
#include <dirent.h>
#include <sys/stat.h>

/* Paths hang off the config's dir / the mirror dir; give the joins room
 * (JC_PATH_MAX + 256), the same bound init uses. */
#define PATH_CAP 4352

/* The mirror's config is tiny (a [core] block + the origin remote); the
 * fetch and rev-parse children write at most a few KB. 256 KB is a floor,
 * not a fit (init's GIT_OUT_CAP). */
#define MIRROR_OUT_CAP (256 * 1024)

/* A fetch child: generous but bounded; a local-path fetch over a drive
 * clone is seconds. */
#define MIRROR_FETCH_TIMEOUT 60L

/* The first 200 bytes of a failed git child's output (M1053's git_must
 * idiom). */
#define MIRROR_ERR_CUT 200

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

/* One report line to stdout (`mirror fetch: ...`), flushed like init's say. */
static void
say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("mirror fetch: ", stdout);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
    fflush(stdout);
}

/* A bounded copy of the first MIRROR_ERR_CUT bytes of a string. */
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
    if (n > MIRROR_ERR_CUT) {
        n = MIRROR_ERR_CUT;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

/* Load the harness config. A refused load (unreadable file, malformed JSON,
 * a missing "dir") is a USAGE-class refusal (exit 2): the command cannot
 * derive the team path or the checkpoints root without a config. */
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
               "fetch cannot name the team's branches");
        return 2;
    }
    return 0;
}

/* Build <mirror's parent>/mirror.git-config.harness-copy into dst (cap
 * PATH_CAP) -- the exact stored-copy path the note names, beside the
 * mirror, in the harness's dir (init's absdir, which is the mirror's
 * parent: init creates the mirror at <absdir>/mirror.git). Returns 0 on a
 * path that is too long. */
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

/* Check 1 (git-free): <mirror>/HEAD exists and is a symref (a "ref: "
 * line). init --bare writes HEAD as a ref, so a harness mirror's HEAD is
 * always of this shape; a detached or missing HEAD is not a bare harness
 * mirror. (A non-bare dir has no <mirror>/HEAD at all, so this same check
 * catches it -- the team clone's HEAD lives in <mirror>/.git/HEAD.) */
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
    /* The first non-empty line must be a symref. */
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

/* Check 3 (git-free): the mirror's config is byte-identical to the copy the
 * harness stored when it created the mirror. A missing stored copy means the
 * mirror was not created by init (or the copy was lost) -- check 3 cannot
 * run, so the command is refused, naming the check. */
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
               "re-writes; treat the mirror as altered (this is a "
               "check-then-use guard against the agent's own commands, "
               "not a fence against a same-uid racer; the complete fence "
               "is Mode B, def-330)",
               (unsigned long)live_len, (unsigned long)stored_len);
        return 2;
    }
    return 0;
}

/* Check 5 (git-free): no `objects/info/alternates` under the mirror. The
 * note's vector (supervisor point 1, measured on 2.53): git runs the
 * mirror's `core.alternateRefsCommand` while walking the refs of an
 * alternate object store, and the alternates file names where to walk. A
 * mirror the harness wrote has none; the refusal is the guard against the
 * agent (the empty `-c core.alternateRefsCommand=` on the fetch child is
 * the second layer, against a git that changes which side the traversal
 * runs on). */
static int
check_no_alternates(const char *mirror)
{
    char path[PATH_CAP];
    jc_snprintf(path, sizeof path, "%s/objects/info/alternates", mirror);
    if (jc_file_exists(path)) {
        refuse("check 5 (alternates): the mirror has an "
               "`objects/info/alternates` file at %s -- a mirror the "
               "harness wrote has none (walking its named alternates is "
               "P4 territory, recorded in DEFERRED.md); the fetch would "
               "run `core.alternateRefsCommand` over them, so it is "
               "refused", path);
        return 2;
    }
    return 0;
}

/* Check 4 (git-free, a directory listing rather than a git call): `hooks/`
 * holds nothing the harness did not tolerate. The harness puts NO hooks
 * there (CHANGED 11: init lays the repos out WITHOUT arming them, def-337).
 * NOTE (supervisor 2026-10-24 21:40): init removes the `.sample` templates
 * from the TEAM clone ONLY -- it does NOT touch this mirror's hooks/ -- so
 * a fresh init mirror's hooks/ still holds all 14 of git's own `*.sample`
 * files. Check 4 therefore TOLERATES those git templates (git never runs a
 * `.sample`; the fetch's `-c core.hooksPath=/dev/null` would not either)
 * and REFUSES any other regular file there (a plant, e.g. a
 * `hooks.reference-transaction` or a bare `post-receive`). Directories
 * (which git does not ship) are skipped, not refused. */
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
        /* A missing hooks/ is not a harness mirror (git init --bare makes
         * one); check 1 usually caught it already, but name THIS check. */
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
            continue;   /* a vanished entry: not a plant */
        }
        if (!S_ISREG(st.st_mode)) {
            continue;   /* only regular files run as hooks */
        }
        dot = strrchr(name, '.');
        if (dot != NULL && strcmp(dot, ".sample") == 0) {
            continue;   /* git's own template; the fetch's
                           -c core.hooksPath=/dev/null never runs it, so a
                           reappearing .sample is tolerated -- but ANY other
                           regular file is refused (the note's check 4) */
        }
        /* refuse() BEFORE closedir(d) (SV:1200.1): name = e->d_name lives in
         * the DIR buffer; closing first read the freed buffer in the report
         * (ASan: heap-use-after-free READ). */
        refuse("check 4 (hooks): the mirror's hooks/ holds a regular "
               "file %s/%s that the harness did not put there -- "
               "init lays the mirror out without arming it (def-337), "
               "so a hook there is a plant", mirror, name);
        closedir(d);
        return 2;
    }
    closedir(d);
    return 0;
}

/* Check 2 (git-free): the mirror directory is owned by the harness's uid.
 * (The note's "mode 700" is not met by a `git init --bare` mirror -- that
 * is umask 022, mode 755, and init's own M1053 driver asserts the layout
 * init produces, so hard-refusing a 755 mirror would make every
 * init-created mirror fail; the ownership stat is the concrete,
 * init-consistent half of the note's check 2, and is what "owned by the
 * harness's uid" means. A same-uid racer is out of scope -- def-330.) */
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

/* The precheck: all five checks, in the note's order, BEFORE any git call
 * in the mirror. Returns 0 when the mirror is one the harness wrote and
 * still holds. */
static int
precheck(const char *mirror, const char *cfg_dir, struct jc_arena *a)
{
    char stored[PATH_CAP];
    int rc;
    if (!build_stored_copy(mirror, cfg_dir, stored, sizeof stored)) {
        refuse("the mirror path is too long to build the stored-copy path");
        return 2;
    }
    rc = check_bare(mirror, a);
    if (rc != 0) {
        return rc;
    }
    rc = check_ownership(mirror);
    if (rc != 0) {
        return rc;
    }
    rc = check_config_bytes(mirror, stored, a);
    if (rc != 0) {
        return rc;
    }
    rc = check_no_alternates(mirror);
    if (rc != 0) {
        return rc;
    }
    rc = check_hooks(mirror);
    if (rc != 0) {
        return rc;
    }
    return 0;
}

/* Run a git child in `repo` with the M2 neutralisation (behaviour 3), the
 * empty GIT_CONFIG_GLOBAL/SYSTEM files (behaviour 4, the env vector), and
 * the empty `core.alternateRefsCommand=` (behaviour 5). `args`/`nargs` are
 * the git subcommand and its options; at most 8 (every call below is 5 or
 * fewer). Returns jc_proc_capture's code: the child's exit status, -1 on
 * spawn failure, -2 on timeout. */
static int
git_m2(struct jc_sb *out, const char *repo, const char *e_global,
       const char *e_system, const char *const args[], int nargs)
{
    char *argv[20];
    char *env[3];
    struct jc_vec envv;
    int k = 0;
    int i;

    if (nargs > 8) {
        return -1;
    }
    argv[k++] = (char *)"git";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.fsmonitor=";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"log.showSignature=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"commit.gpgsign=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.hooksPath=/dev/null";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.alternateRefsCommand=";
    argv[k++] = (char *)"-C";
    argv[k++] = (char *)repo;
    for (i = 0; i < nargs; i++) {
        argv[k++] = (char *)args[i];
    }
    argv[k] = NULL;

    /* The env slot: two empty files the harness owns (behaviour 4). The
     * child reads an empty global and an empty system, so a plant in the
     * agent's ~/.gitconfig (uploadpack.packObjectsHook and the rest of the
     * global level) is not seen by the fetch child or the serve child it
     * spawns. */
    env[0] = (char *)e_global;
    env[1] = (char *)e_system;
    env[2] = (char *)"";
    jc_vec_init(&envv, sizeof(char *));
    {
        int j;
        for (j = 0; j < 2; j++) {
            if (jc_vec_push(&envv, &env[j]) != JC_OK) {
                jc_vec_free(&envv);
                return -1;
            }
        }
    }
    (void)env[2];
    i = (int)jc_proc_capture(argv, &envv, NULL, out, MIRROR_OUT_CAP,
                             MIRROR_FETCH_TIMEOUT, NULL);
    jc_vec_free(&envv);
    return i;
}

/* The first line of a `rev-parse --short` capture (the short hash), NUL
 * trimmed, or NULL when the ref did not resolve. */
static char *
first_line(struct jc_sb *out, struct jc_arena *a)
{
    char *s;
    const char *p;
    const char *nl;
    jc_size len;
    if (out->data == NULL) {
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

/* The team-branches fetch: `git -C <mirror> fetch -q --no-tags <team clone>
 * "refs/heads/$BR/main:refs/heads/$BR/main" plus the work wildcard refspec
 * (refs/heads/$BR/work, star -> refs/heads/$BR/work, star). The SOURCE is
 * the team's clone -- the note's measured behaviour 1 (`git -C $MIRROR
 * fetch -q --no-tags <absolute path to the team clone> ...`), where the
 * script's container-served team stands. No `+` anywhere (a ref rewritten
 * in the team is not carried over -- cell W2), no --prune (a ref deleted
 * inside stays in the mirror -- cell W1).
 *
 * Measured on git 2.53 with -q (the spec's flag): a non-FF REJECTION exits
 * 1 with NO output (the `! [rejected] ... (non-fast-forward)` line is
 * progress and -q suppresses it), while a TRANSPORT failure (a bad source
 * path) exits 1 WITH output (`fatal: ... does not appear to be a git
 * repository`). So the two rc=1 classes are told apart by whether git said
 * anything: silent rc=1 is the tolerated non-FF (the mirror keeps the old
 * tip, the other refspecs still applied), loud rc=1 is a transport failure
 * (exit 1, output captured). Returns the process exit code: 0 on success or
 * a tolerated non-FF; 1 on a transport failure (the captured output is
 * reported). */
static int
fetch_team_branches(const char *mirror, const char *team_path,
                    const char *br,
                    const char *e_global, const char *e_system,
                    struct jc_arena *a)
{
    const char *argv[10];
    struct jc_sb out;
    char cut[224];
    int rc;
    int n;

    if (team_path == NULL || team_path[0] == '\0') {
        refuse("the config has no `dir`, so the team's clone (the fetch "
               "source) cannot be located");
        return 2;
    }
    (void)a;
    jc_sb_init(&out);
    n = 0;
    argv[n++] = (char *)"fetch";
    argv[n++] = (char *)"-q";
    argv[n++] = (char *)"--no-tags";
    argv[n++] = (char *)team_path;
    {
        char rs1[512];
        char rs2[512];
        jc_snprintf(rs1, sizeof rs1,
                    "refs/heads/%s/main:refs/heads/%s/main", br, br);
        jc_snprintf(rs2, sizeof rs2,
                    "refs/heads/%s/work/*:refs/heads/%s/work/*", br, br);
        argv[n++] = (char *)rs1;
        argv[n++] = (char *)rs2;
    }
    argv[n] = NULL;

    rc = git_m2(&out, mirror, e_global, e_system, argv, n);
    if (rc == -1 || rc == -2) {
        cut_copy(cut, sizeof cut, out.data);
        refuse("the team-branches fetch could not run (rc=%d): %s", rc, cut);
        jc_sb_free(&out);
        return 1;
    }
    if (rc == 0) {
        jc_sb_free(&out);
        return 0;
    }
    /* rc != 0, measured shape (2.53, -q): */
    cut_copy(cut, sizeof cut, out.data);
    if (rc == 1 && (out.data == NULL || out.data[0] == '\0')) {
        /* A SILENT rc=1: the no-`+` discipline rejecting a non-fast-forward
         * ref. The expected case it exists for: report it as the script's
         * line and TOLERATE it (the other refspecs in the same fetch still
         * applied, so the mirror is left with whatever the fetch applied). */
        say("the fetch of the team's branches: a ref was not carried over "
            "(non-fast-forward, refused -- the refset has no `+`)");
        jc_sb_free(&out);
        return 0;
    }
    /* A LOUD rc=1 (or any other non-zero): the fetch failed at the
     * transport (a bad source) or otherwise did not do its job: exit 1,
     * the captured git output named (the first 200 bytes). */
    refuse("the fetch of the team's branches failed (rc=%d): %s", rc, cut);
    jc_sb_free(&out);
    return 1;
}

/* The kept-attempt fetch for one checkpoint store: `git -C <mirror> fetch
 * -q --no-tags <store>`, refspec refs/jichi/discarded, star ->
 * refs/kept/<k>, star (the store's discarded attempts into the mirror's
 * kept refs). The store is a jichi shadow repo (a git repo directory);
 * the M2 set + isolation + the empty alternateRefsCommand flag are on the
 * child because the store's config is agent-writable too (the note's
 * kept-attempt paragraph). A failure is REPORTED, not fatal (the script's
 * `|| log` shape). */
static void
fetch_kept(const char *mirror, const char *store, const char *k,
           const char *e_global, const char *e_system)
{
    char rs[512];
    const char *argv[7];
    struct jc_sb out;
    char cut[224];
    int n = 0;
    int rc;

    jc_snprintf(rs, sizeof rs, "refs/jichi/discarded/*:refs/kept/%s/*", k);
    argv[n++] = (char *)"fetch";
    argv[n++] = (char *)"-q";
    argv[n++] = (char *)"--no-tags";
    argv[n++] = (char *)store;
    argv[n++] = (char *)rs;
    argv[n] = NULL;

    jc_sb_init(&out);
    rc = git_m2(&out, mirror, e_global, e_system, argv, n);
    cut_copy(cut, sizeof cut, out.data);
    if (rc != 0) {
        say("the fetch of the kept attempts of %s failed (rc=%d): %s",
            k, rc, cut);
    }
    jc_sb_free(&out);
}

/* The first `worktree = <path>` line of a checkpoint's config, read as a
 * FILE (the note: "the container wrote it"; the script's sed shape
 * `^[[:space:]]*worktree[[:space:]]*=[[:space:]]*`, kept exactly). The
 * value is arena-allocated; NULL when there is no such line. */
static char *
checkpoint_worktree(const char *store_cfg, struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    const char *p;
    const char *line;
    char *val;
    jc_size vlen;
    if (jc_read_file(store_cfg, &text, &len, a) != JC_OK) {
        return NULL;
    }
    p = text;
    while (*p != '\0') {
        line = p;
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        /* A line is the bytes from `line` up to (not including) the NUL or
         * newline at p. */
        {
            jc_size linelen = (jc_size)(p - line);
            jc_size i = 0;
            /* Skip leading blanks ([[:space:]]* = spaces and tabs here). */
            while (i < linelen && (line[i] == ' ' || line[i] == '\t')) {
                i++;
            }
            if (linelen - i >= 8 && strncmp(line + i, "worktree", 8) == 0) {
                i += 8;
                while (i < linelen && (line[i] == ' ' || line[i] == '\t')) {
                    i++;
                }
                if (i < linelen && line[i] == '=') {
                    i++;
                    while (i < linelen && (line[i] == ' ' || line[i] == '\t')) {
                        i++;
                    }
                    /* The value runs to the end of the line. */
                    vlen = linelen - i;
                    val = (char *)jc_arena_calloc(a, vlen + 1);
                    if (val != NULL) {
                        if (vlen > 0) {
                            memcpy(val, line + i, vlen);
                        }
                        val[vlen] = '\0';
                        return val;
                    }
                }
            }
        }
        if (*p == '\0') {
            break;
        }
        p++;   /* skip the newline */
    }
    return NULL;
}

/* The checkpoints loop (the script's `for s in $D/home/.jichi.d/
 * checkpoints/STAR/`): for each store whose config's `worktree =` line is
 * BYTE-EXACTLY the team's workdir, fetch its refs/jichi/discarded, star
 * into refs/kept/<k>, star. A store whose worktree is NOT the team's is
 * skipped, not errored (the 2026-10-18 incident: a lead's scratch-probe
 * store with a worktree that exists only in its own container). */
static void
fetch_kept_all(const char *mirror, const char *checkpoints,
               const char *team_path, const char *e_global,
               const char *e_system, struct jc_arena *a)
{
    DIR *d;
    struct dirent *e;
    d = opendir(checkpoints);
    if (d == NULL) {
        return;   /* no checkpoints: nothing kept, nothing to say */
    }
    for (e = readdir(d); e != NULL; e = readdir(d)) {
        const char *k = e->d_name;
        char store[PATH_CAP];
        char store_cfg[PATH_CAP];
        char *wt;
        if (strcmp(k, ".") == 0 || strcmp(k, "..") == 0) {
            continue;
        }
        jc_snprintf(store, sizeof store, "%s/%s", checkpoints, k);
        if (!jc_is_dir(store)) {
            continue;
        }
        jc_snprintf(store_cfg, sizeof store_cfg, "%s/config", store);
        if (!jc_file_exists(store_cfg)) {
            continue;
        }
        wt = checkpoint_worktree(store_cfg, a);
        if (wt == NULL || team_path == NULL ||
            strcmp(wt, team_path) != 0) {
            /* Not the team's workdir: skipped, not errored (the
             * 2026-10-18 incident). */
            continue;
        }
        fetch_kept(mirror, store, k, e_global, e_system);
    }
    closedir(d);
}

/* Two empty files the harness owns, in the harness's dir, for
 * GIT_CONFIG_GLOBAL / GIT_CONFIG_SYSTEM (the note's behaviour 4). The
 * g_out/s_out buffers receive the `GIT_CONFIG_GLOBAL=/…` and
 * `GIT_CONFIG_SYSTEM=/…` env-slot strings (jc_spawn_build_envp wants
 * "KEY=VALUE"). Created empty, overwritten empty on every run. */
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

int
jc_harness_mirror_fetch_run(const char *config_path,
                            const char *mirror, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    char *e_global;
    char *e_system;
    char team[PATH_CAP];
    char checkpoints[PATH_CAP];
    int rc;

    if (mirror == NULL || mirror[0] == '\0') {
        refuse("needs the mirror directory: "
               "jichi-harness mirror fetch --config <f> <mirror-dir>");
        return 2;
    }
    rc = load_cfg(config_path, a, &cfg);
    if (rc != 0) {
        return rc;
    }
    if (!jc_is_dir(mirror)) {
        refuse("the mirror directory does not exist or is not a directory: "
               "%s", mirror);
        return 2;
    }
    /* The precheck: all five checks, git-free, BEFORE any git call in the
     * mirror. A failure is exit 2 naming the check, and no fetch happens. */
    rc = precheck(mirror, cfg.dir, a);
    if (rc != 0) {
        return rc;
    }
    if (cfg.dir == NULL || cfg.dir[0] == '\0') {
        refuse("the config has no `dir`, so the harness's state (the team "
               "clone, the checkpoints, the empty GIT_CONFIG files) cannot "
               "be located");
        return 2;
    }
    /* The empty env files (behaviour 4), in the harness's dir -- NOT in the
     * mirror (the precheck just verified the mirror is the harness's; a
     * fetch must not add files to it, and the fetch does not happen until
     * after the precheck). */
    e_global = (char *)jc_arena_calloc(a, PATH_CAP);
    e_system = (char *)jc_arena_calloc(a, PATH_CAP);
    if (e_global == NULL || e_system == NULL) {
        refuse("out of memory");
        return 1;
    }
    if (!make_empty_env_files(cfg.dir, a, e_global, PATH_CAP,
                              e_system, PATH_CAP)) {
        refuse("could not create the empty GIT_CONFIG_GLOBAL/SYSTEM files "
               "in the harness's dir %s", cfg.dir);
        return 1;
    }
    /* The team's clone (the fetch source). */
    if (jc_snprintf(team, sizeof team, "%s/team", cfg.dir)
        >= (int)sizeof team) {
        refuse("the config's dir is too long to build the team path");
        return 2;
    }
    /* The kept-attempt loop path is built unconditionally, BEFORE the team
     * fetch: it is NOT gated on the team fetch succeeding (the note's
     * 'Exit classes': mirror.sh's `|| log` never stops the checkpoint loop,
     * so a team-branches TRANSPORT failure still runs the kept-attempt
     * fetches; the team fetch's rc is the command's final exit code,
     * returned last). */
    if (jc_snprintf(checkpoints, sizeof checkpoints,
                    "%s/home/.jichi.d/checkpoints", cfg.dir)
        >= (int)sizeof checkpoints) {
        refuse("the config's dir is too long to build the checkpoints path");
        return 2;
    }
    /* The team-branches fetch (exit 1 on a transport failure, exit 0 on a
     * tolerated non-FF). The precheck (exit 2, no fetch) already ran above;
     * this rc is remembered and returned at the END, after the kept loop. */
    rc = fetch_team_branches(mirror, team,
                             cfg.drive_branch_prefix, e_global, e_system, a);
    /* The kept-attempt loop (report lines, not fatal). ALWAYS runs, even when
     * the team fetch failed at the transport (rc==1): that is the script's
     * order, and the port keeps it. */
    if (checkpoints[0] != '\0' && jc_is_dir(checkpoints)) {
        fetch_kept_all(mirror, checkpoints, team, e_global, e_system, a);
    }
    if (rc != 0) {
        /* The team fetch failed (transport); the kept attempts were still
         * attempted. The command's final exit code is the team fetch's rc. */
        say("the team's branches failed to fetch but the kept attempts were "
            "still attempted (final exit %d)", rc);
        return rc;
    }
    say("done");
    return 0;
}

int
jc_harness_mirror_head_run(const char *config_path,
                           const char *mirror, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    char e_global[PATH_CAP];
    char e_system[PATH_CAP];
    char ref[512];
    const char *argv[4];
    struct jc_sb out;
    char *head;
    int rc;
    int i = 0;

    if (mirror == NULL || mirror[0] == '\0') {
        refuse("needs the mirror directory: "
               "jichi-harness mirror head --config <f> <mirror-dir>");
        return 2;
    }
    rc = load_cfg(config_path, a, &cfg);
    if (rc != 0) {
        return rc;
    }
    if (!jc_is_dir(mirror)) {
        refuse("the mirror directory does not exist or is not a directory: "
               "%s", mirror);
        return 2;
    }
    if (cfg.dir == NULL || cfg.dir[0] == '\0') {
        refuse("the config has no `dir`, so the empty GIT_CONFIG files "
               "cannot be located");
        return 2;
    }
    if (!make_empty_env_files(cfg.dir, a, e_global, sizeof e_global,
                              e_system, sizeof e_system)) {
        refuse("could not create the empty GIT_CONFIG_GLOBAL/SYSTEM files "
               "in the harness's dir %s", cfg.dir);
        return 1;
    }
    jc_snprintf(ref, sizeof ref, "refs/heads/%s/main",
                cfg.drive_branch_prefix);
    argv[i++] = (char *)"rev-parse";
    argv[i++] = (char *)"--short";
    argv[i++] = (char *)ref;
    argv[i] = NULL;
    jc_sb_init(&out);
    rc = git_m2(&out, mirror, e_global, e_system, argv, 3);
    head = (rc == 0) ? first_line(&out, a) : NULL;
    if (head != NULL) {
        fputs(head, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
    /* mirror_head (the script): an unresolvable ref is the EMPTY string,
     * not an error -- the output is on stdout, stderr is discarded (the
     * script's 2>/dev/null), and the command exits 0. */
    jc_sb_free(&out);
    return 0;
}
