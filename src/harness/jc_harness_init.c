/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_init.c - `jichi-harness init` (M1053, H2): lay out the drive's
 * (or a rehearsal's) runtime directory, the C89 port of the drive's
 * setup-dir.sh (docs/internal/drive/2026-10-23-harness/harness/setup-dir.sh,
 * read line by line; its comments name the incidents).
 *
 * Design note: docs/internal/drive/2026-10-23-harness/notes/M1053.md. The
 * numbered behaviours (1-14) and the CHANGED lines (4, 6, 11, 13) are the
 * contract; this file implements them in order. `init` is the FIRST harness
 * command that runs git (the M1049 "never runs git" line for H0 is lifted
 * here, and only here): every git child goes through jc_proc_capture, the
 * same spawn primitive jichi's own git tools use (src/tools/jc_tool_git.c),
 * with the M2 neutralisation flags (`-c core.fsmonitor=`,
 * `-c log.showSignature=false`, `-c commit.gpgsign=false`,
 * `-c core.hooksPath=/dev/null`), the secret-env scrub that primitive
 * carries, and -- for the mirror's fetch -- `-c credential.helper=` so the
 * operator's credential cache is never consulted (setup-dir.sh lines 102-103).
 *
 * The never-delete rule (setup-dir.sh line 18): a part that already exists
 * is reported ("left as it is") and left alone; init only creates. A
 * refusal (exit 2) happens BEFORE any part is written, so a refused init
 * changes nothing. A git step that fails MID-layout is reported (exit 1):
 * a layout that cannot finish is not a crash, it is a line -- the next run
 * completes what this one left undone and changes nothing else.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO (the note's CHANGED lines, kept
 * out on purpose):
 *   4.  no DIR/harness/ archive: the C port IS the harness;
 *   6.  no rehearsal container-prefix / unit-name rewrite: Mode A has no
 *       containers (def-336, lands with Mode D / P4);
 *   11/13: no pre-push hooks (key scan, size check, the mirror wrapper, the
 *       records keyscan) and no credential helper installed: H3 arms the
 *       repos in C (def-337); init lays them out.
 */
/* realpath is an XSI extension; _XOPEN_SOURCE 600 makes glibc declare it in
 * every build mode (fortify or not) -- the jc_path.c precedent, before any
 * system header is pulled in. This TU therefore carries no local decl, and
 * none is redundant under an optimised (fortified) build. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "jc_platform.h"   /* jc_status, jc_mkdir_p, jc_read_file, ... */
#include "jc_mem.h"        /* struct jc_arena */
#include "jc_str.h"        /* struct jc_sb */
#include "jc_vec.h"        /* struct jc_vec (jc_proc_capture's env slot) */
#include "jc_snprintf.h"   /* jc_snprintf, never sprintf */
#include "jc_proc.h"       /* jc_proc_capture: the git children */
#include "jc_json.h"       /* jc_json_parse (identity re-read) */
#include "jc_utf8.h"       /* jc_utf8_trunc_len: boundary-safe cuts (M889) */
#include "cJSON.h"
#include "jc_harness_config.h"
#include "jc_harness_init.h"

/* One report line: `init (<mode>): <part>` (the script's `say`). */
#define INIT_PREFIX "init (%s): "

/* A git child's output is small (for-each-ref lines, rev-parse); the fetch
 * runs with -q and writes nothing on success. 256 KB is a floor, not a fit. */
#define GIT_OUT_CAP (256 * 1024)

/* Paths hang off the absolute dir; give the joins room (JC_PATH_MAX + 256). */
#define PATH_CAP 4352

/* The records README, the reference's step-7 text (setup-dir.sh line 113). */
static const char recs_readme[] =
    "# Records of the jichi-harness drive of 2026-10-23\n"
    "\n"
    "One directory per run under runs/, written by the harness (loop.sh); supervision-log.md\n"
    "and harness.log beside them. The plan and the documentation: branch drive/2026-10-23-harness/plan,\n"
    "docs/internal/drive/2026-10-23-harness/.\n";

static void
say(const char *mode, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf(INIT_PREFIX, mode);
    vprintf(fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
    fflush(stdout);
}

/* A named refusal on stderr (D13 item 3: say what is missing and what it
 * holds). The caller returns 2. */
static void
refuse(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: init: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* The M2-neutral git child (src/tools/jc_tool_git.c): `git` + the four -c
 * flags, an optional `-C <dir>`, an optional cleared credential helper, an
 * optional env vector (the records commit passes its identity this way --
 * git's AUTHOR and COMMITTER identity env takes precedence over every config
 * level, per docs/internal/COMMIT_IDENTITY.md), and the subcommand's args
 * (at most 16; every call below is 8 or fewer). Returns jc_proc_capture's
 * code: the child's exit status, -1 spawn failure, -2 timeout.
 * `env` entries are "KEY=VALUE" strings owned by the caller. */
static int
git_run_env(struct jc_sb *out, const char *dir, int cred_clear,
            const struct jc_vec *env,
            const char *const args[], int nargs)
{
    char *argv[32];
    int k = 0;
    int i;

    if (nargs > 16) {
        return -1;
    }
    argv[k++] = (char *)"git";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.fsmonitor=";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"log.showSignature=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"commit.gpgsign=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.hooksPath=/dev/null";
    if (dir != NULL) {
        argv[k++] = (char *)"-C";
        argv[k++] = (char *)dir;
    }
    if (cred_clear) {
        argv[k++] = (char *)"-c";
        argv[k++] = (char *)"credential.helper=";
    }
    for (i = 0; i < nargs; i++) {
        argv[k++] = (char *)args[i];
    }
    argv[k] = NULL;
    return jc_proc_capture(argv, env, NULL, out, GIT_OUT_CAP, 30, NULL);
}

/* The common no-env form. */
static int
git_run(struct jc_sb *out, const char *dir, int cred_clear,
        const char *const args[], int nargs)
{
    return git_run_env(out, dir, cred_clear, NULL, args, nargs);
}

/* A git step that MUST succeed: report the failure to stderr with its
 * captured output (the first 200 bytes) and hand back 1. */
static int
git_must(struct jc_sb *out, int rc, const char *what)
{
    char cut[216];
    jc_size ncut = 0;
    if (rc == 0) {
        return 0;
    }
    cut[0] = '\0';
    if (out->data != NULL) {
        ncut = jc_utf8_trunc_len(out->data, 200);
        jc_snprintf(cut, sizeof cut, "%.*s", (int)ncut, out->data);
    }
    fprintf(stderr, "jichi-harness: init: git %s failed (rc=%d): %s\n",
            what, rc, cut);
    return 1;
}

/* `od -An -N12 -tx1 /dev/urandom`, the reference's exact draw (setup-dir.sh
 * lines 70-71): 12 random bytes as 24 lowercase hex digits, whitespace
 * stripped. */
static int
draw_canary(char *hex)
{
    char *argv[6];
    struct jc_sb out;
    const char *p;
    int rc;
    int n = 0;

    jc_sb_init(&out);
    argv[0] = (char *)"od";
    argv[1] = (char *)"-An";
    argv[2] = (char *)"-N12";
    argv[3] = (char *)"-tx1";
    argv[4] = (char *)"/dev/urandom";
    argv[5] = NULL;
    rc = jc_proc_capture(argv, NULL, NULL, &out, 256, 10, NULL);
    if (rc == 0 && out.data != NULL) {
        for (p = out.data; *p != '\0'; p++) {
            if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) {
                if (n < 24) {
                    hex[n++] = *p;
                }
            }
        }
    }
    jc_sb_free(&out);
    if (n != 24) {
        return -1;
    }
    hex[n] = '\0';
    return 0;
}

/* Copy one file's bytes (the pinned jichi): slurp, write, chmod 755. */
static int
pin_binary(const char *src, const char *dst, struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    jc_status st;

    st = jc_read_file(src, &text, &len, a);
    if (st != JC_OK) {
        return -1;
    }
    st = jc_write_file(dst, text, len);
    if (st != JC_OK) {
        return -1;
    }
    if (chmod(dst, 0755) != 0) {
        return -1;
    }
    return 0;
}

/* The `build: <stamp>` line of `<bin> --version --verbose`, or NULL when the
 * binary answers no such line (a tarball build says nothing it cannot back).
 * The capture stays in the arena; the caller takes the pointer. */
static char *
build_stamp(const char *bin, struct jc_arena *a)
{
    char *argv[4];
    struct jc_sb out;
    const char *p;
    const char *nxt;
    char *line = NULL;
    jc_size len;
    char *s;
    int rc;

    jc_sb_init(&out);
    argv[0] = (char *)bin;
    argv[1] = (char *)"--version";
    argv[2] = (char *)"--verbose";
    argv[3] = NULL;
    rc = jc_proc_capture(argv, NULL, NULL, &out, 4096, 10, NULL);
    if (rc == 0 && out.data != NULL) {
        for (p = out.data; *p != '\0'; ) {
            /* The 'build: ' check runs BEFORE the advance (the review's S1):
             * a FINAL line without a trailing newline is still parsed
             * (strcspn stops at the NUL), and the advance itself must not
             * read past it -- `strchr(p, '\n') + 1` is undefined when no
             * newline remains. */
            if (strncmp(p, "build: ", 7) == 0) {
                len = strcspn(p + 7, "\n");
                s = (char *)jc_arena_calloc(a, len + 1);
                if (s != NULL) {
                    memcpy(s, p + 7, len);
                    s[len] = '\0';
                    line = s;
                }
                break;
            }
            nxt = strchr(p, '\n');
            if (nxt == NULL) {
                break;
            }
            p = nxt + 1;
        }
    }
    jc_sb_free(&out);
    return line;
}

/* Remove every `*.sample` under <hooks> (setup-dir.sh line 87). These are
 * git's own hook templates -- files, not a layout part; removing them does
 * not violate the never-delete rule, which protects the parts init creates.
 * The reference deletes them so a cloned team cannot inherit a planted
 * template. */
static void
remove_hook_samples(const char *hooks_dir)
{
    DIR *d;
    struct dirent *e;

    d = opendir(hooks_dir);
    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        char path[PATH_CAP];
        if (len > 7 && strcmp(e->d_name + len - 7, ".sample") == 0) {
            if (jc_snprintf(path, sizeof path, "%s/%s",
                            hooks_dir, e->d_name) < (int)sizeof path) {
                (void)unlink(path);
            }
        }
    }
    closedir(d);
}

/* The `jichi` next to the running harness (the --jichi-bin default, the
 * note's command shape: init works with no flag in a normal install). */
static int
default_jichi_bin(char *out, jc_size cap)
{
    char self[PATH_CAP];
    ssize_t n;
    char *slash;

    n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0 || n >= (ssize_t)sizeof self) {
        return -1;
    }
    self[n] = '\0';
    slash = strrchr(self, '/');
    if (slash == NULL) {
        return -1;
    }
    *slash = '\0';
    if (jc_snprintf(out, cap, "%s/jichi", self) >= (int)cap) {
        return -1;
    }
    return 0;
}

/* The records-commit identity, from the config's drive.gitName /
 * drive.gitEmail (the drive.env ZD_GIT_NAME / ZD_GIT_EMAIL the script read
 * at line 115). Re-reads the config file the loader already validated: the
 * loader's struct carries no identity fields. Returns 0 when at least one
 * was set (the unset one stays NULL), -1 on a read/parse failure. The
 * caller passes the name/email through `git -c`, so a commit is never
 * stamped from a planted global gitconfig -- and where the config sets
 * neither, git's own resolution (per COMMIT_IDENTITY.md: the drive's agents
 * get the identity from their ENVIRONMENT) applies. */
static int
read_identity(const char *config_path, char *name, jc_size name_cap,
              char *email, jc_size email_cap, struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    cJSON *root = NULL;
    const cJSON *drive;
    const cJSON *item;
    const char *v;
    int st = 0;

    name[0] = '\0';
    email[0] = '\0';
    if (config_path == NULL) {
        return 0;
    }
    if (jc_read_file(config_path, &text, &len, a) != JC_OK) {
        return -1;
    }
    root = jc_json_parse(text);
    if (root == NULL) {
        return -1;   /* the loader just said this is valid JSON */
    }
    drive = cJSON_GetObjectItem(root, "drive");
    if (cJSON_IsObject(drive)) {
        item = cJSON_GetObjectItem(drive, "gitName");
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            v = item->valuestring;
            if (jc_snprintf(name, name_cap, "%s", v) < (int)name_cap) {
                st = 1;
            }
        }
        item = cJSON_GetObjectItem(drive, "gitEmail");
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            v = item->valuestring;
            if (jc_snprintf(email, email_cap, "%s", v) < (int)email_cap) {
                st = 1;
            }
        }
    }
    cJSON_Delete(root);
    return st;
}

/* Behaviour 12's fetch count: the lines of `git -C <mirror> for-each-ref
 * refs/remotes`. */
static int
count_remote_refs(struct jc_sb *out)
{
    const char *p;
    int n = 0;

    if (out->data == NULL) {
        return 0;
    }
    for (p = out->data; *p != '\0'; p++) {
        if (*p == '\n') {
            n++;
        }
    }
    if (out->data[0] != '\0' && out->data[out->len - 1] != '\n') {
        n++;
    }
    return n;
}

int
jc_harness_init_run(const char *config_path, const char *mode,
                    const char *dir, const char *from,
                    const char *jichi_bin, struct jc_arena *a)
{
    char absdir[PATH_CAP];
    char prefix[160] = "";
    char id_name[160];
    char id_email[160];
    char path[PATH_CAP];
    char path2[PATH_CAP];
    char standin[PATH_CAP];
    char remote[PATH_CAP];
    char bin_path[PATH_CAP];
    char hooks[PATH_CAP];
    char team_dir[PATH_CAP];
    char mirror_dir[PATH_CAP];
    char recs_dir[PATH_CAP];
    char planbr[160] = "harness-plan/plan";
    static const char *plain[] = {
        "runs", "inbox", "backups", "bin", "verify",
        "home/.cache", "home/.jichi.d/tool-output"
    };
    char jichi_default[PATH_CAP];
    char canary_env[64];
    char canary_tok[64];
    char keyfile_line[PATH_CAP + 32];
    const char *keyfile = NULL;
    const char *cfg_prefix = NULL;
    struct jc_harness_config cfg;
    struct jc_sb out;
    char *stamp = NULL;
    int is_drive = 0;
    int rc;
    int i;

    jc_sb_init(&out);

    /* --- behaviours 1: the arguments, the refusals BEFORE any layout ------- */

    if (mode == NULL || (strcmp(mode, "drive") != 0 &&
                         strcmp(mode, "rehearsal") != 0)) {
        refuse("MODE must be drive or rehearsal (got '%s')",
               (mode != NULL) ? mode : "(none)");
        fprintf(stderr,
                "usage: jichi-harness init [--config <file>] "
                "<drive|rehearsal> DIR [--from <repo>] [--jichi-bin <path>]\n");
        jc_sb_free(&out);
        return 2;
    }
    is_drive = (strcmp(mode, "drive") == 0);

    if (dir == NULL || dir[0] == '\0') {
        refuse("DIR is missing (the drive's or rehearsal's runtime "
               "directory, made absolute before the layout)");
        jc_sb_free(&out);
        return 2;
    }
    if (from == NULL || from[0] == '\0') {
        refuse("--from <repo> is required: the source of the team clone, "
               "and the rehearsal's local stand-in's content");
        jc_sb_free(&out);
        return 2;
    }
    /* The pinned jichi: the flag, or the jichi next to the running harness. */
    if (jichi_bin != NULL && jichi_bin[0] != '\0') {
        if (!jc_file_exists(jichi_bin)) {
            refuse("the jichi binary to pin, %s (--jichi-bin), is missing or "
                   "unreadable", jichi_bin);
            jc_sb_free(&out);
            return 2;
        }
    } else if (default_jichi_bin(jichi_default, sizeof jichi_default) == 0 &&
               jc_file_exists(jichi_default)) {
        jichi_bin = jichi_default;
    } else {
        refuse("no jichi binary to pin: --jichi-bin is not given and there is "
               "no jichi next to the running harness binary");
        jc_sb_free(&out);
        return 2;
    }

    /* The config: the drive's identity (branch prefix, records-commit
     * name/email, the model-key reference). */
    memset(&cfg, 0, sizeof cfg);
    if (config_path == NULL) {
        if (is_drive) {
            /* A drive's key.env must reference the key file; with no config
             * (or env) there is nothing to reference. */
            refuse("the model key file (JICHI_HARNESS_MODEL_KEY_FILE) is not "
                   "set: a drive's key.env has nothing to reference "
                   "(give --config or the env setting)");
            jc_sb_free(&out);
            return 2;
        }
    } else {
        rc = (jc_harness_config_load(config_path, &cfg, a) == JC_OK) ? 0 : -1;
        if (rc != 0) {
            const char *msg = jc_harness_config_error(&cfg);
            refuse("the harness config could not be loaded: %s",
                   (msg != NULL) ? msg : "(no message)");
            jc_sb_free(&out);
            return 2;
        }
        cfg_prefix = cfg.drive_branch_prefix;
        keyfile = cfg.model_key_file;
    }
    if (is_drive && keyfile == NULL) {
        refuse("the model key file (JICHI_HARNESS_MODEL_KEY_FILE) is not set: "
               "a drive's key.env has nothing to reference");
        jc_sb_free(&out);
        return 2;
    }
    if (cfg_prefix != NULL) {
        (void)jc_snprintf(prefix, sizeof prefix, "%s", cfg_prefix);
    }
    if (prefix[0] == '\0') {
        refuse("the branch prefix is not set (the config's "
               "drive.branchPrefix): the team, the mirror and the records "
               "branches hang off it");
        jc_sb_free(&out);
        return 2;
    }
    /* S3 (the review): the prefix must fit the derived branch names
     * (<prefix>/plan, <prefix>/main, <prefix>/records) BEFORE any part of
     * the layout is written. 8 is the longest suffix ('/records'); the
     * buffer holds the name plus its NUL. A too-long prefix is a
     * usage-class refusal (exit 2), not a silent truncation to the wrong
     * branch names. */
    if (strlen(prefix) + 8 >= (size_t)sizeof planbr) {
        refuse("drive.branchPrefix is too long (%d characters): the derived "
               "branch names (<prefix>/plan, <prefix>/main, "
               "<prefix>/records) cannot fit the %d-byte branch-name "
               "buffer, and the layout would carry the wrong names",
               (int)strlen(prefix), (int)sizeof planbr);
        jc_sb_free(&out);
        return 2;
    }
    /* 0 (no config, or an identity-free one) is fine; only < 0 fails. */
    if (read_identity(config_path, id_name, sizeof id_name,
                      id_email, sizeof id_email, a) < 0) {
        refuse("the harness config could not be re-read for the records "
               "commit's identity (drive.gitName / drive.gitEmail)");
        jc_sb_free(&out);
        return 1;
    }
    if (jc_snprintf(planbr, sizeof planbr, "%s/plan", prefix)
        >= (int)sizeof planbr) {
        refuse("drive.branchPrefix is too long: <prefix>/plan does not fit "
               "the branch-name buffer");
        jc_sb_free(&out);
        return 1;
    }

    /* --- behaviour 2: DIR is made absolute before anything is laid out ----- */
    rc = (int)jc_mkdir_p(dir);
    if (rc != 0) {
        refuse("could not create DIR %s (mkdir -p failed)", dir);
        jc_sb_free(&out);
        return 1;
    }
    if (realpath(dir, absdir) == NULL) {
        refuse("could not make DIR %s absolute (realpath failed)", dir);
        jc_sb_free(&out);
        return 1;
    }
    (void)jc_snprintf(remote, sizeof remote, "%s",
                      is_drive ? from : "");

    /* --- behaviour 3: the plain directories (the shell fence's grants) ----- */
    {
        for (i = 0; i < (int)(sizeof plain / sizeof plain[0]); i++) {
            if (jc_snprintf(path, sizeof path, "%s/%s",
                            absdir, plain[i]) >= (int)sizeof path) {
                refuse("DIR %s with %s is too long to lay out", absdir, plain[i]);
                jc_sb_free(&out);
                return 1;
            }
            if (jc_mkdir_p(path) != JC_OK) {
                refuse("could not create %s (mkdir -p failed)", path);
                jc_sb_free(&out);
                return 1;
            }
        }
    }
    /* Behaviour 4 (CHANGED): no harness/ archive -- the C port IS the
     * harness; there are no scripts to install. Nothing to do, on purpose. */

    /* --- behaviour 5: the pinned jichi -------------------------------------- */
    if (jc_snprintf(bin_path, sizeof bin_path, "%s/bin/jichi", absdir)
        >= (int)sizeof bin_path) {
        refuse("DIR %s is too long to lay out", absdir);
        jc_sb_free(&out);
        return 1;
    }
    {
        int pin_was = jc_file_exists(bin_path);
        /* S2 (the review): the reference pins with [ -x ] (setup-dir.sh
         * line 61), not [ -e ]: a pin that exists but is NOT executable is
         * re-pinned, not left as it is. */
        int pin_ok = pin_was && access(bin_path, X_OK) == 0;
        if (pin_ok) {
            say(mode, "bin/jichi exists: left as it is");
        } else {
            if (pin_binary(jichi_bin, bin_path, a) != 0) {
                refuse("could not pin the jichi binary: %s -> %s",
                       jichi_bin, bin_path);
                jc_sb_free(&out);
                return 1;
            }
            stamp = build_stamp(bin_path, a);
            if (pin_was) {
                say(mode, "bin/jichi: re-pinned (it existed but was not "
                           "executable): %s",
                    (stamp != NULL) ? stamp : "(no build stamp)");
            } else {
                say(mode, "bin/jichi: %s",
                    (stamp != NULL) ? stamp : "(no build stamp)");
            }
        }
    }

    /* --- behaviours 7-9: the secrets, then the rehearsal's stand-in --------- */
    if (is_drive) {
        /* The key file is READ WHEN SOURCED, never copied (line 67). */
        if (jc_snprintf(path, sizeof path, "%s/key.env", absdir)
            >= (int)sizeof path) {
            refuse("DIR %s is too long to lay out", absdir);
            jc_sb_free(&out);
            return 1;
        }
        if (jc_file_exists(path)) {
            say(mode, "key.env exists: left as it is");
        } else {
            if (jc_snprintf(keyfile_line, sizeof keyfile_line,
                            "JICHI_API_KEY=$(tr -d ' \\t\\n\\r' < %s)\n",
                            keyfile) >= (int)sizeof keyfile_line) {
                refuse("the model key file's path is too long to reference");
                jc_sb_free(&out);
                return 1;
            }
            if (jc_write_file(path, keyfile_line,
                              (jc_size)strlen(keyfile_line)) != JC_OK) {
                refuse("could not write %s", path);
                jc_sb_free(&out);
                return 1;
            }
            (void)chmod(path, 0600);
            say(mode, "key.env: the model key file is read when sourced (no copy)");
        }
        /* The drive's remote is the push target (the config's server the
         * tenant pushes to); with --from given, that is what the mirror and
         * the records clone point at. */
        (void)jc_snprintf(remote, sizeof remote, "%s", from);
    } else {
        /* Random canaries, never a fixed string (lines 70-71). */
        if (jc_snprintf(path, sizeof path, "%s/canary.env", absdir)
            >= (int)sizeof path) {
            refuse("DIR %s is too long to lay out", absdir);
            jc_sb_free(&out);
            return 1;
        }
        if (jc_file_exists(path)) {
            say(mode, "canary.env exists: left as it is");
        } else {
            if (draw_canary(canary_env) != 0) {
                refuse("could not draw the canary (od -An -N12 -tx1 "
                       "/dev/urandom failed)");
                jc_sb_free(&out);
                return 1;
            }
            (void)jc_snprintf(keyfile_line, sizeof keyfile_line,
                              "JICHI_API_KEY=CANARY-%s\n", canary_env);
            if (jc_write_file(path, keyfile_line,
                              (jc_size)strlen(keyfile_line)) != JC_OK) {
                refuse("could not write %s", path);
                jc_sb_free(&out);
                return 1;
            }
            say(mode, "canary.env: a random model-key canary (never a fixed string)");
        }
        if (jc_snprintf(path2, sizeof path2, "%s/canary.token", absdir)
            >= (int)sizeof path2) {
            refuse("DIR %s is too long to lay out", absdir);
            jc_sb_free(&out);
            return 1;
        }
        if (jc_file_exists(path2)) {
            say(mode, "canary.token exists: left as it is");
        } else {
            if (draw_canary(canary_tok) != 0) {
                refuse("could not draw the canary (od -An -N12 -tx1 "
                       "/dev/urandom failed)");
                jc_sb_free(&out);
                return 1;
            }
            (void)jc_snprintf(keyfile_line, sizeof keyfile_line,
                              "tokcanary-%s\n", canary_tok);
            if (jc_write_file(path2, keyfile_line,
                              (jc_size)strlen(keyfile_line)) != JC_OK) {
                refuse("could not write %s", path2);
                jc_sb_free(&out);
                return 1;
            }
            say(mode, "canary.token: a random token canary (never a fixed string)");
        }
        /* The rehearsal remote: a local bare stand-in, never GitLab (72-76).
         * Created only if absent; its content, the source's plan branch. */
        if (jc_snprintf(standin, sizeof standin, "%s/standin.git", absdir)
            >= (int)sizeof standin) {
            refuse("DIR %s is too long to lay out", absdir);
            jc_sb_free(&out);
            return 1;
        }
        if (!jc_is_dir(standin)) {
            const char *a0[5];
            a0[0] = (char *)"init";
            a0[1] = (char *)"-q";
            a0[2] = (char *)"--bare";
            a0[3] = (char *)standin;
            a0[4] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, NULL, 0, a0, 4), "init --bare standin.git") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            {
                char refspec[480];
                const char *a1[5];
                (void)jc_snprintf(refspec, sizeof refspec,
                                  "%s:refs/heads/%s", planbr, planbr);
                a1[0] = (char *)"push";
                a1[1] = (char *)"-q";
                a1[2] = (char *)standin;
                a1[3] = (char *)refspec;
                a1[4] = NULL;
                jc_sb_clear(&out);
                /* The push runs in the SOURCE repo (-C from), into the
                 * stand-in's path. */
                if (git_must(&out, git_run(&out, from, 0, a1, 4),
                             "push the source's plan branch into standin.git") != 0) {
                    jc_sb_free(&out);
                    return 1;
                }
            }
            /* The stand-in IS the rehearsal remote (REMOTE=$DIR/standin.git,
             * the script's line 76): the source is pushed INTO it by path, and
             * nothing points back at it with an origin -- it is a sink, not a
             * client. (The port's earlier `remote add origin <source>` was a
             * deviation from the reference; removed to match lines 72-76.) */
            say(mode, "standin.git: the source's plan branch, local (no real remote)");
        }
        (void)jc_snprintf(remote, sizeof remote, "%s", standin);
    }
    /* Behaviour 6 (CHANGED): no container-prefix / unit-name rewrite --
     * Mode A has no containers. Nothing to do, on purpose (def-336). */

    /* --- behaviour 10: the team's clone ------------------------------------- */
    if (jc_snprintf(team_dir, sizeof team_dir, "%s/team", absdir)
        >= (int)sizeof team_dir) {
        refuse("DIR %s is too long to lay out", absdir);
        jc_sb_free(&out);
        return 1;
    }
    if (jc_snprintf(path, sizeof path, "%s/.git", team_dir)
        >= (int)sizeof path) {
        refuse("DIR %s is too long to lay out", absdir);
        jc_sb_free(&out);
        return 1;
    }
    if (jc_is_dir(path)) {
        say(mode, "team/ exists: left as it is");
    } else {
        {
            char mainbr[160];
            char short_head[80] = "?";
            const char *a0[8];
            struct jc_sb head;
            const char *cargs[4];
            if (jc_snprintf(mainbr, sizeof mainbr, "%s/main", prefix)
                >= (int)sizeof mainbr) {
                refuse("drive.branchPrefix is too long: <prefix>/main does "
                       "not fit the branch-name buffer");
                jc_sb_free(&out);
                return 1;
            }
            a0[0] = (char *)"clone";
            a0[1] = (char *)"-q";
            a0[2] = (char *)"--no-local";
            a0[3] = (char *)"--branch";
            a0[4] = (char *)planbr;
            a0[5] = (char *)from;
            a0[6] = (char *)team_dir;
            a0[7] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, NULL, 0, a0, 7),
                         "clone the team from the source's plan branch") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            {
                const char *sargs[4];
                sargs[0] = (char *)"switch";
                sargs[1] = (char *)"-q";
                sargs[2] = (char *)"-c";
                sargs[3] = (char *)mainbr;
                jc_sb_clear(&out);
                if (git_must(&out, git_run(&out, team_dir, 0, sargs, 4),
                             "switch the team to the drive branch") != 0) {
                    jc_sb_free(&out);
                    return 1;
                }
            }
            {
                const char *uargs[4];
                uargs[0] = (char *)"remote";
                uargs[1] = (char *)"set-url";
                uargs[2] = (char *)"origin";
                uargs[3] = (char *)"/nonexistent-the-harness-pushes";
                jc_sb_clear(&out);
                if (git_must(&out, git_run(&out, team_dir, 0, uargs, 4),
                             "point the team's origin nowhere") != 0) {
                    jc_sb_free(&out);
                    return 1;
                }
            }
            if (jc_snprintf(hooks, sizeof hooks, "%s/.git/hooks", team_dir)
                >= (int)sizeof hooks) {
                refuse("DIR %s is too long to lay out", absdir);
                jc_sb_free(&out);
                return 1;
            }
            remove_hook_samples(hooks);
            jc_sb_init(&head);
            cargs[0] = (char *)"rev-parse";
            cargs[1] = (char *)"--short";
            cargs[2] = (char *)"HEAD";
            cargs[3] = NULL;
            if (git_run(&head, team_dir, 0, cargs, 3) == 0
                && head.data != NULL && head.data[0] != '\0') {
                (void)jc_snprintf(short_head, sizeof short_head,
                                  "%.*s",
                                  (int)jc_utf8_trunc_len(head.data, 40),
                                  head.data);
            }
            jc_sb_free(&head);
            say(mode, "team/ on %s at %s (no sample hooks; origin leads nowhere)",
                mainbr, short_head);
        }
    }

    /* --- behaviours 11-12: the mirror (CHANGED 11: no hooks installed) ------ */
    if (jc_snprintf(mirror_dir, sizeof mirror_dir, "%s/mirror.git", absdir)
        >= (int)sizeof mirror_dir) {
        refuse("DIR %s is too long to lay out", absdir);
        jc_sb_free(&out);
        return 1;
    }
    if (jc_is_dir(mirror_dir)) {
        say(mode, "mirror.git exists: left as it is");
    } else {
        {
            const char *a0[5];
            const char *a1[5];
            const char *a2[4];
            const char *a3[4];
            const char *a4[3];
            struct jc_sb refs;
            int nrefs;
            a0[0] = (char *)"init";
            a0[1] = (char *)"-q";
            a0[2] = (char *)"--bare";
            a0[3] = (char *)mirror_dir;
            a0[4] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, NULL, 0, a0, 4),
                         "init --bare mirror.git") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            a1[0] = (char *)"remote";
            a1[1] = (char *)"add";
            a1[2] = (char *)"origin";
            a1[3] = (char *)remote;
            a1[4] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, mirror_dir, 0, a1, 4),
                         "set the mirror's origin") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            a2[0] = (char *)"config";
            a2[1] = (char *)"remote.origin.fetch";
            a2[2] = (char *)"+refs/heads/*:refs/remotes/origin/*";
            a2[3] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, mirror_dir, 0, a2, 3),
                         "set the mirror's fetch refspec") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            /* The initial fetch, reported not fatal (lines 102-104). The
             * cleared credential helper stays: the fetch must not consult
             * the operator's credential cache. */
            a3[0] = (char *)"fetch";
            a3[1] = (char *)"-q";
            a3[2] = (char *)"origin";
            jc_sb_clear(&out);
            rc = git_run(&out, mirror_dir, 1, a3, 3);
            if (rc == 0) {
                jc_sb_init(&refs);
                a4[0] = (char *)"for-each-ref";
                a4[1] = (char *)"refs/remotes";
                if (git_run(&refs, mirror_dir, 0, a4, 2) == 0) {
                    nrefs = count_remote_refs(&refs);
                } else {
                    nrefs = -1;
                }
                jc_sb_free(&refs);
                if (nrefs < 0) {
                    say(mode, "mirror.git: the for-each-ref count FAILED");
                } else {
                    say(mode, "mirror.git fetched %d remote refs", nrefs);
                }
            } else {
                say(mode, "mirror.git: the fetch from origin FAILED");
            }
            /* M1055 (H3b): store the EXACT bytes of the mirror's config, as
             * init just wrote them (the core block + the origin remote + the
             * fetch refspec + whatever the initial fetch added), so `mirror
             * fetch`'s check 3 can memcmp the live config against the copy
             * the harness wrote. Written HERE, at creation, not re-read
             * later -- a later re-read would bless a plant. A mirror whose
             * stored copy is missing is refused by `mirror fetch` (check 3
             * cannot run); init is idempotent, so on a re-run where the
             * mirror already exists this is skipped ("left as it is"), and
             * an existing mirror with a lost stored copy is init's to notice
             * (it says so) rather than silently re-snapshotted. */
            {
                char cfgsrc[PATH_CAP];
                char cfgdst[PATH_CAP];
                char *cfgbytes = NULL;
                jc_size cfglen = 0;
                jc_status cfs;
                jc_snprintf(cfgsrc, sizeof cfgsrc, "%s/config", mirror_dir);
                jc_snprintf(cfgdst, sizeof cfgdst,
                            "%s/mirror.git-config.harness-copy", absdir);
                cfs = jc_read_file(cfgsrc, &cfgbytes, &cfglen, a);
                if (cfs != JC_OK) {
                    say(mode, "mirror.git: the stored config copy FAILED "
                        "(could not read %s)", cfgsrc);
                } else {
                    cfs = jc_write_file(cfgdst, cfgbytes, cfglen);
                    if (cfs != JC_OK) {
                        say(mode, "mirror.git: the stored config copy FAILED "
                            "(could not write %s)", cfgdst);
                    } else {
                        say(mode, "mirror.git: the config bytes are stored at "
                            "mirror.git-config.harness-copy (for `mirror "
                            "fetch`'s integrity check)");
                    }
                }
            }
        }
    }

    /* --- behaviour 13: the records clone (CHANGED: no keyscan hook) --------- */
    if (jc_snprintf(recs_dir, sizeof recs_dir, "%s/recs", absdir)
        >= (int)sizeof recs_dir) {
        refuse("DIR %s is too long to lay out", absdir);
        jc_sb_free(&out);
        return 1;
    }
    if (jc_snprintf(path, sizeof path, "%s/.git", recs_dir)
        >= (int)sizeof path) {
        refuse("DIR %s is too long to lay out", absdir);
        jc_sb_free(&out);
        return 1;
    }
    if (jc_is_dir(path)) {
        say(mode, "recs/ exists: left as it is");
    } else {
        {
            char recbr[160];
            char idn1[384];
            char idn2[384];
            char ide1[384];
            char ide2[384];
            struct jc_vec idenv;
            char *idenv_s[4];
            const char *a0[4];
            const char *a1[5];
            const char *a2[4];
            const char *a3[4];
            const char *a4[5];
            if (jc_snprintf(recbr, sizeof recbr, "%s/records", prefix)
                >= (int)sizeof recbr) {
                refuse("drive.branchPrefix is too long: <prefix>/records "
                       "does not fit the branch-name buffer");
                jc_sb_free(&out);
                return 1;
            }
            a0[0] = (char *)"init";
            a0[1] = (char *)"-q";
            a0[2] = (char *)recs_dir;
            a0[3] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, NULL, 0, a0, 3),
                         "init recs/") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            a1[0] = (char *)"checkout";
            a1[1] = (char *)"-q";
            a1[2] = (char *)"--orphan";
            a1[3] = (char *)recbr;
            a1[4] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, recs_dir, 0, a1, 4),
                         "start the records' orphan branch") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            if (jc_snprintf(path2, sizeof path2, "%s/README.md", recs_dir)
                >= (int)sizeof path2) {
                refuse("DIR %s is too long to lay out", absdir);
                jc_sb_free(&out);
                return 1;
            }
            if (jc_write_file(path2, recs_readme,
                              (jc_size)strlen(recs_readme)) != JC_OK) {
                refuse("could not write %s", path2);
                jc_sb_free(&out);
                return 1;
            }
            a2[0] = (char *)"add";
            a2[1] = (char *)"README.md";
            a2[2] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, recs_dir, 0, a2, 2),
                         "stage the records README") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            /* The records commit, stamped with the identity the config
             * carries (the script's ZD_GIT_NAME / ZD_GIT_EMAIL, line 115).
             * Where the config sets one, it rides as git's AUTHOR/COMMITTER
             * ENV (which beats every config level, COMMIT_IDENTITY.md);
             * where it sets neither, the commit falls to the operator's own
             * git resolution -- never a placeholder baked into the program.
             * The M2 neutralisation (core.hooksPath=/dev/null, ...) still
             * applies: a planted hook cannot stamp this commit. */
            jc_vec_init(&idenv, sizeof(char *));
            idenv_s[0] = NULL; idenv_s[1] = NULL;
            idenv_s[2] = NULL; idenv_s[3] = NULL;
            if (id_name[0] != '\0') {
                if (jc_snprintf(idn1, sizeof idn1,
                                "GIT_AUTHOR_NAME=%s", id_name)
                    >= (int)sizeof idn1 ||
                    jc_snprintf(idn2, sizeof idn2,
                                "GIT_COMMITTER_NAME=%s", id_name)
                    >= (int)sizeof idn2) {
                    refuse("the config's records git name is too long");
                    jc_vec_free(&idenv);
                    jc_sb_free(&out);
                    return 1;
                }
                idenv_s[0] = idn1;
                idenv_s[1] = idn2;
            }
            if (id_email[0] != '\0') {
                if (jc_snprintf(ide1, sizeof ide1,
                                "GIT_AUTHOR_EMAIL=%s", id_email)
                    >= (int)sizeof ide1 ||
                    jc_snprintf(ide2, sizeof ide2,
                                "GIT_COMMITTER_EMAIL=%s", id_email)
                    >= (int)sizeof ide2) {
                    refuse("the config's records git email is too long");
                    jc_vec_free(&idenv);
                    jc_sb_free(&out);
                    return 1;
                }
                idenv_s[2] = ide1;
                idenv_s[3] = ide2;
            }
            {
                int i;
                for (i = 0; i < 4; i++) {
                    if (idenv_s[i] != NULL) {
                        if (jc_vec_push(&idenv, &idenv_s[i]) != JC_OK) {
                            refuse("out of memory building the commit's "
                                   "identity env");
                            jc_vec_free(&idenv);
                            jc_sb_free(&out);
                            return 1;
                        }
                    }
                }
            }
            a3[0] = (char *)"commit";
            a3[1] = (char *)"-q";
            a3[2] = (char *)"-m";
            a3[3] = (char *)"records: the branch";
            jc_sb_clear(&out);
            if (git_must(&out,
                         git_run_env(&out, recs_dir, 0,
                                     &idenv, a3, 4),
                         "commit the records README") != 0) {
                jc_vec_free(&idenv);
                jc_sb_free(&out);
                return 1;
            }
            jc_vec_free(&idenv);
            a4[0] = (char *)"remote";
            a4[1] = (char *)"add";
            a4[2] = (char *)"origin";
            a4[3] = (char *)remote;
            a4[4] = NULL;
            jc_sb_clear(&out);
            if (git_must(&out, git_run(&out, recs_dir, 0, a4, 4),
                         "set the records' origin") != 0) {
                jc_sb_free(&out);
                return 1;
            }
            say(mode, "recs/ on %s (orphan), origin %s", recbr, remote);
        }
    }

    say(mode, "done: %s", absdir);
    jc_sb_free(&out);
    return 0;
}
