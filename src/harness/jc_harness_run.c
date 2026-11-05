/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_run.c - `jichi-harness run` (M1057, H4): one lead run in Mode A,
 * the C89 port of the drive's run-lead.sh (read line by line; its comments
 * name the incidents). Mode A (the plan §7.1): the harness and the agent run
 * as the same user, so the container half of the reference is DROPPED and the
 * record and key-delivery half is kept, re-expressed over the jichi
 * primitives H0 linked (the spawn, the env scrub).
 *
 * Design note (the contract -- the fourteen numbered behaviours and the nine
 * driver cells R1-R9): docs/internal/drive/2026-10-23-harness/notes/M1057.md.
 * Reference: docs/internal/drive/2026-10-23-harness/harness/run-lead.sh.
 *
 * THE KEY IS READ FROM THE FILE AND HANDED THROUGH THE ENVIRONMENT (behaviour
 * 2): modelKeyFile is read at the moment the run starts (one line,
 * whitespace-trimmed), JICHI_API_KEY and JLU_API_KEY are UNSET from the
 * harness's OWN process environment first (the file is the only source -- an
 * ambient key the operator's shell carries would otherwise reach the child,
 * and the driver's R2 canary is distinct from the file's), and the key is
 * passed "JICHI_API_KEY=<key>" in the jc_spawn env vector, which is applied
 * AFTER the scrub so the explicit entry wins. It is never setenv'd in the
 * parent, never on a command line, and no error message ever prints it.
 *
 * THE RECORDS CHANNEL IS OUTSIDE THE WORKSPACE (behaviour 4): the run's
 * records live in <dir>/runs/<N>/, never in the workdir jichi chdirs into.
 * jichi writes journal.jsonl and telemetry.jsonl itself (plain paths, passed
 * as --journal / --log -- the reference's named pipes are DROPPED, Mode A);
 * the harness appends jichi's stdout to stream.jsonl and its stderr to
 * stderr.log.
 *
 * NOTES RETENTION (behaviour 13, cell R9): the inbox notes are READ into the
 * prompt at assembly, but MOVED into runs/<N>/notes/ ONLY after the child
 * exits and its stream carries a type:"text" event (a model call delivered
 * content). A run that died before its first model call (a 500) leaves them
 * in the inbox for the next run.
 *
 * MOVED == READ (M1063, SV:0904.2): the assembly lists the inbox's .md
 * names, sorts them BYTE-WISE ascending (the reference's sorted glob,
 * run-lead.sh 93) and reads each note into the prompt IN THAT ORDER; that
 * name-sorted array IS the assembly-time list, and the post-run move takes
 * it and moves EXACTLY those names. A note that appears in the inbox after
 * assembly (a supervisor drop mid-run) is therefore never read and never
 * moved: it stays for the next run's prompt. (The pre-fix move re-listed
 * the inbox at exit and consumed every .md present -- run 0077's 0725b note
 * was filed under a run that never read it, and the next run's handoff then
 * listed it as given to that one, SV:0905.)
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO (the note's IS-NOT / CHANGED lines,
 * kept out on purpose): the handoff body (M1058 fills "What the previous run
 * left you"); the post-run record collection (def-355); a container
 * (def-336); a gateway-up probe (def-357); a --deadline or a --budget-tokens
 * (behaviour 7: caps off, fences on).
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
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>  /* pid_t (portability_lint 29: declared under X/Open
                          level in <unistd.h> only, M723) */
#include <sys/wait.h>
#include <sys/time.h>   /* struct timeval (portability_lint 28: MiNTLib's
                         <sys/select.h> forward-declares it only, M723) */
#include "jc_select.h"   /* M834: select()/fd_set, never <sys/select.h> direct */
#include "jc_fcntl.h"    /* JC_O_BINARY (portability_lint 38: file opens) */

#include "jc_platform.h"   /* jc_status, jc_mkdir_p, jc_read_file, ... */
#include "jc_mem.h"        /* struct jc_arena */
#include "jc_str.h"        /* struct jc_sb */
#include "jc_vec.h"        /* struct jc_vec (the spawn's env slot) */
#include "jc_snprintf.h"   /* jc_snprintf, never sprintf */
#include "jc_proc.h"       /* jc_proc_capture (git), jc_proc_waitpid */
#include "jc_json.h"       /* jc_json_parse (the tenant's extra fields) */
#include "jc_spawn.h"      /* jc_spawn, jc_spawn_req, JC_SPAWN_DEVNULL */
#include <signal.h>        /* M1059 behaviour 9: the SIGINT handler */
#include "jc_base64.h"     /* jc_base64_encode (--prompt-b64) */
#include "jc_sha256.h"     /* jc_sha256_hex (the config's meta line) */
#include "cJSON.h"
#include "jc_harness_config.h"
#include "jc_harness_run.h"
#include "jc_harness_handoff.h"  /* the handoff section (M1058) */

/* Paths hang off the absolute dir; give the joins room (JC_PATH_MAX + 256),
 * the same bound init and mirror use. */
#define PATH_CAP 4352

/* A git child's output is small (one rev-parse line). 256 KB is a floor, not
 * a fit (init's GIT_OUT_CAP). */
#define GIT_OUT_CAP (256 * 1024)

/* `--version --verbose` writes a handful of lines; 4 KB is a floor (init's
 * build_stamp capture uses 4096). */
#define VERSION_OUT_CAP 4096

/* The reference's fence values (run-lead.sh 33, 39; the operator's 2026-09-25
 * 23:15 decision): caps off, fences on. */
#define RUN_MAX_TOOL_CALLS "50000"
#define RUN_VERIFY_TIMEOUT "30m"

/* The fixed key name the model receives under (the reference's env var, the
 * M242 setup-wizard default; def-356 records that a per-tenant apiKeyEnv name
 * is the drive's concern, not the run's). */
#define RUN_KEY_NAME "JICHI_API_KEY"

/* M1059 behaviour 9 (CHANGED 1's support, SV:1838.2): the run's own SIGINT
 * handler. When the drive's loop SIGINTs the run's process group (the port of
 * the reference's `podman kill --signal INT`, which reached only jichi), the
 * run must FORWARD that signal to the jichi group and set a flag -- and do
 * NOTHING else. It does not unblock, does not re-raise, and does not touch the
 * run's meta. The run then stays inside its waitpid for jichi (jc_proc_waitpid
 * retries the EINTR the signal interrupts); jichi exits 130 to the forwarded
 * signal (src/main.c on_sigint, M146) and writes its journal, so the run's
 * normal path writes end= / rc=130 / team_head_after= and returns 130 as its
 * own code. A re-raise (the default handler's 130) would die at once and never
 * write the meta -- the bug the reference's split exists to avoid.
 *
 * Neither global is touched ONLY from this handler (CLAUDE.md: a signal
 * handler touches only a volatile sig_atomic_t flag and the one POSIX-safe
 * kill the note names). g_run_sigint_seen is WRITTEN only from this handler
 * and READ by main in the one post-spawn re-check that closes the
 * install-to-spawn window (SV:2317.2: a signal that arrives after the handler
 * is installed but before the pgid is known is recorded, not forwarded).
 * g_run_jichi_pgid is WRITTEN by main -- the child's pid just after a
 * successful spawn, and 0 again after the waitpid -- and READ from this
 * handler; it is set before the waitpid so a signal after the spawn is
 * forwarded, and the one re-check covers the signal between the install and
 * that write. Until the spawn succeeds it is the 0 sentinel, so a pre-spawn
 * signal never kill(-1) (the whole system) or kill(-0) (the run's own group).
 */
static volatile sig_atomic_t g_run_sigint_seen;
static pid_t g_run_jichi_pgid;

static void
run_on_sigint(int sig)
{
    (void)sig;
    if (g_run_jichi_pgid > 0) {
        kill(-g_run_jichi_pgid, SIGINT);   /* the run's whole group, jichi with it */
    }
    g_run_sigint_seen = 1;
}

/* A named refusal on stderr (behaviour 1's exit-2 class). The message names
 * the setting or the path, NEVER a secret value (behaviour 14 / cell R2). */
static void
refuse(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: run: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

/* The UTC wall clock as YYYY-MM-DDTHH:MM:SSZ (the `date -Is` shape of the
 * reference's meta lines, with a Z the harness can always stand behind). */
static void
now_iso(char *out, size_t cap)
{
    time_t t = time(NULL);
    struct tm tmv;
    if (gmtime_r(&t, &tmv) != NULL) {
        if (strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tmv) == 0) {
            out[0] = '\0';
        }
        return;
    }
    /* The clock is unusable; a deterministic value keeps meta.txt shaped. */
    tmv.tm_year = 70; tmv.tm_mon = 0; tmv.tm_mday = 1;
    tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
    if (strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tmv) == 0) {
        out[0] = '\0';
    }
}

/* The `jichi` next to the running harness (behaviour 6's default; the
 * jc_harness_init.c idiom, which run shares the binary with). */
static int
default_jichi_bin(char *out, size_t cap)
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

/* Behaviour 2: read modelKeyFile, fail closed (returns 0) when it is missing,
 * unreadable or holds no key. The key lands ONLY in `out` -- the harness never
 * setenv's it and no message prints it. One line, whitespace-trimmed (the
 * reference's `tr -d ' \t\r\n'` idiom). */
static int
read_key_file(const char *path, char *out, size_t cap, struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    const char *p;
    const char *end;
    int got = 0;

    out[0] = '\0';
    if (jc_read_file(path, &text, &len, a) != JC_OK) {
        refuse("the model key could not be loaded (modelKeyFile %s is "
               "missing or unreadable)", path);
        return 0;
    }
    /* The first non-blank line wins. */
    end = text + len;
    for (p = text; p < end && !got; ) {
        const char *a0 = p;
        const char *b0;
        while (p < end && *p != '\n') {
            p++;
        }
        b0 = p;
        while (a0 < b0 && (*a0 == ' ' || *a0 == '\t' || *a0 == '\r')) {
            a0++;
        }
        while (b0 > a0 && (b0[-1] == ' ' || b0[-1] == '\t' || b0[-1] == '\r')) {
            b0--;
        }
        if (b0 > a0) {
            size_t n = (size_t)(b0 - a0);
            if (n + 1 > cap) {
                n = cap - 1;
            }
            memcpy(out, a0, n);
            out[n] = '\0';
            got = 1;
        }
        if (p < end) {
            p++;   /* skip the newline */
        }
    }
    if (!got) {
        refuse("the model key could not be loaded (modelKeyFile %s holds no "
               "key)", path);
        return 0;
    }
    return 1;
}

/* The fields the shared loader does not carry for this command: the tenant's
 * promptFile and bin, and the drive's gitName / gitEmail (the commit identity
 * the child carries, the reference's ZD_GIT_NAME / ZD_GIT_EMAIL). Re-reads the
 * config file the loader already validated (the init read_identity idiom).
 * Returns 1 when the named tenant was found (the unset fields stay NULL). */
static int
read_run_extras(const char *config_path, const char *tenant,
                char **prompt_file, char **bin, char **git_name,
                char **git_email, struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    cJSON *root;
    cJSON *arr;
    cJSON *item;
    int found = 0;

    *prompt_file = NULL;
    *bin = NULL;
    *git_name = NULL;
    *git_email = NULL;
    if (jc_read_file(config_path, &text, &len, a) != JC_OK) {
        return 0;   /* the loader just said this is valid JSON */
    }
    root = jc_json_parse(text);
    if (root == NULL) {
        return 0;
    }
    arr = cJSON_GetObjectItem(root, "tenants");
    if (cJSON_IsArray(arr)) {
        cJSON_ArrayForEach(item, arr) {
            const cJSON *nm;
            if (!cJSON_IsObject(item)) {
                continue;
            }
            nm = cJSON_GetObjectItem(item, "name");
            if (!cJSON_IsString(nm) || nm->valuestring == NULL) {
                continue;
            }
            if (strcmp(nm->valuestring, tenant) == 0) {
                const cJSON *v;
                found = 1;
                v = cJSON_GetObjectItem(item, "promptFile");
                if (cJSON_IsString(v) && v->valuestring != NULL &&
                    v->valuestring[0] != '\0') {
                    *prompt_file = jc_arena_strdup(a, v->valuestring);
                }
                v = cJSON_GetObjectItem(item, "bin");
                if (cJSON_IsString(v) && v->valuestring != NULL &&
                    v->valuestring[0] != '\0') {
                    *bin = jc_arena_strdup(a, v->valuestring);
                }
            }
        }
    }
    {
        const cJSON *drive = cJSON_GetObjectItem(root, "drive");
        const cJSON *v;
        if (cJSON_IsObject(drive)) {
            v = cJSON_GetObjectItem(drive, "gitName");
            if (cJSON_IsString(v) && v->valuestring != NULL &&
                v->valuestring[0] != '\0') {
                *git_name = jc_arena_strdup(a, v->valuestring);
            }
            v = cJSON_GetObjectItem(drive, "gitEmail");
            if (cJSON_IsString(v) && v->valuestring != NULL &&
                v->valuestring[0] != '\0') {
                *git_email = jc_arena_strdup(a, v->valuestring);
            }
        }
    }
    cJSON_Delete(root);
    return found;
}

/* The lead-model guard (SV:1818.1, the reference's run-lead.sh 44-52, the
 * strict twin of its raw `grep -Eq '"name"...:$LEAD_MODEL"'`): if
 * drive.leadModel is set, is it a model the tenant's jichi config NAMES?
 * The config is jichi's schema, so it is parsed as JSON (the harness links
 * src/json): a name is "named" iff some entry of the top-level "models"
 * array is an object whose "name" string equals it.
 *
 * Returns 1 when the name is set (pass --model, as today) or when it is not
 * set at all (no lead model, run on the config's routing); 0 when it is set
 * and NOT named (the WARN+OMIT below: the run proceeds on the config's
 * routing). The behaviour is the script's WARN + OMIT, NOT a refusal (the
 * note's b1 last sentence, edited in meaning): nothing is refused, no
 * record is written, the exit code is unchanged, and the line printed to
 * the harness's stderr names ONLY the model -- never a key, never a URL. */
static int
lead_model_named(const char *jichi_config, const char *lead_model,
                 struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    cJSON *root;
    cJSON *arr;
    cJSON *item;
    int named = 0;

    if (jc_read_file(jichi_config, &text, &len, a) != JC_OK) {
        return 0;   /* unreadable: the run is refused before this guard */
    }
    root = jc_json_parse(text);
    if (root == NULL) {
        return 0;
    }
    arr = cJSON_GetObjectItem(root, "models");
    if (cJSON_IsArray(arr)) {
        cJSON_ArrayForEach(item, arr) {
            const cJSON *nm;
            if (!cJSON_IsObject(item)) {
                continue;
            }
            nm = cJSON_GetObjectItem(item, "name");
            if (cJSON_IsString(nm) && nm->valuestring != NULL &&
                strcmp(nm->valuestring, lead_model) == 0) {
                named = 1;
                break;
            }
        }
    }
    cJSON_Delete(root);
    return named;
}

/* Two empty files the harness owns, in the drive root, for
 * GIT_CONFIG_GLOBAL / GIT_CONFIG_SYSTEM (behaviour 11's M2 neutralisation,
 * the mirror's make_empty_env_files idiom). */
static int
make_empty_git_files(const char *dir, char *g_out, size_t g_cap,
                     char *s_out, size_t s_cap)
{
    char gpath[PATH_CAP];
    char spath[PATH_CAP];
    const char zero[] = "";
    if (jc_snprintf(gpath, sizeof gpath, "%s/.harness-empty-global", dir)
        >= (int)sizeof gpath) {
        return 0;
    }
    if (jc_snprintf(spath, sizeof spath, "%s/.harness-empty-system", dir)
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

/* The M2-neutral `git rev-parse --short HEAD` in the workdir (behaviour 11):
 * the four -c flags plus the empty GIT_CONFIG files, so a hook or a planted
 * global config the agent carries cannot affect this run's stamp. The short
 * hash lands in `out`; 0 (unknown) when it cannot be read. */
static int
team_head(const char *workdir, const char *e_global, const char *e_system,
          char *out, size_t cap)
{
    /* argv[15], not [14] (SV:1158.1): 14 slots are filled (git, the four -c
     * flag/value pairs, -C, the workdir, rev-parse, --short, HEAD) and the
     * terminating NULL is the 15th store; [14] wrote one past the end
     * (UBSan: index 14 out of bounds for type 'char *[14]') -- M1057's. */
    char *argv[15];
    char *env[3];
    struct jc_vec envv;
    struct jc_sb scratch;
    int k = 0;
    int i;
    int rc;

    out[0] = '\0';
    jc_sb_init(&scratch);
    argv[k++] = (char *)"git";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.fsmonitor=";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"log.showSignature=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"commit.gpgsign=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.hooksPath=/dev/null";
    argv[k++] = (char *)"-C"; argv[k++] = (char *)workdir;
    argv[k++] = (char *)"rev-parse";
    argv[k++] = (char *)"--short";
    argv[k++] = (char *)"HEAD";
    argv[k] = NULL;

    env[0] = (char *)e_global;
    env[1] = (char *)e_system;
    env[2] = (char *)"";
    jc_vec_init(&envv, sizeof(char *));
    for (i = 0; i < 2; i++) {
        if (jc_vec_push(&envv, &env[i]) != JC_OK) {
            jc_vec_free(&envv);
            jc_sb_free(&scratch);
            return 0;
        }
    }
    rc = (int)jc_proc_capture(argv, &envv, NULL, &scratch, GIT_OUT_CAP,
                              30, NULL);
    jc_vec_free(&envv);

    if (rc == 0 && scratch.data != NULL) {
        const char *nl = strchr(scratch.data, '\n');
        size_t n = (nl != NULL) ? (size_t)(nl - scratch.data)
                                : strlen(scratch.data);
        if (n > 0 && n + 1 < cap) {
            memcpy(out, scratch.data, n);
            out[n] = '\0';
        }
    }
    jc_sb_free(&scratch);
    return out[0] != '\0';
}

/* The two lines of `<bin> --version --verbose` (the reference's lines 64-65,
 * the init build_stamp idiom): the FIRST line into `version` and the `build: `
 * value into `*stamp` (NULL when the binary answers no such line -- a tarball
 * build says nothing it cannot back). The captures stay in the arena. */
static int
read_jichi_version(const char *bin, char *version, size_t vcap,
                   char **stamp, struct jc_arena *a)
{
    char *argv[4];
    struct jc_sb out;
    const char *p;
    const char *nl;
    int rc;

    version[0] = '\0';
    *stamp = NULL;
    jc_sb_init(&out);
    argv[0] = (char *)bin;
    argv[1] = (char *)"--version";
    argv[2] = (char *)"--verbose";
    argv[3] = NULL;
    rc = (int)jc_proc_capture(argv, NULL, NULL, &out, VERSION_OUT_CAP, 10,
                              NULL);
    if (rc == 0 && out.data != NULL) {
        p = out.data;
        nl = strchr(p, '\n');
        if (nl == NULL) {
            nl = p + strlen(p);
        }
        {
            size_t n = (size_t)(nl - p);
            if (n > 0 && n + 1 < vcap) {
                memcpy(version, p, n);
                version[n] = '\0';
            }
        }
        for (p = (nl > p) ? nl + 1 : p; *p != '\0'; ) {
            if (strncmp(p, "build: ", 7) == 0) {
                size_t blen = strcspn(p + 7, "\n");
                char *s = (char *)jc_arena_calloc(a, blen + 1);
                if (s != NULL) {
                    memcpy(s, p + 7, blen);
                    s[blen] = '\0';
                    *stamp = s;
                }
                break;
            }
            nl = strchr(p, '\n');
            if (nl == NULL) {
                break;
            }
            p = nl + 1;
        }
    }
    jc_sb_free(&out);
    return version[0] != '\0';
}

/* Behaviour 13: did the child's stream carry a type:"text" event (a model
 * call delivered content)? A 500 emits an error and a done, never a text
 * line, so its absence is the "the first call did not succeed" gate. jichi
 * serializes the events unformatted (CJSON_PrintUnformatted), so the compact
 * needle is the one to look for (memmem is not in the C89/POSIX-2001 set:
 * search by hand). */
static int
stream_has_text(const char *stream_path)
{
    char *text = NULL;
    jc_size len = 0;
    static const char needle[] = "\"type\":\"text\"";
    int found = 0;
    jc_size i;
    jc_size nlen = sizeof needle - 1;
    struct jc_arena *ra = jc_arena_new(0);

    if (ra == NULL) {
        return 0;
    }
    if (jc_read_file(stream_path, &text, &len, ra) != JC_OK) {
        jc_arena_free(ra);
        return 0;
    }
    for (i = 0; i + nlen <= len; i++) {
        if (memcmp(text + i, needle, nlen) == 0) {
            found = 1;
            break;
        }
    }
    jc_arena_free(ra);
    return found;
}

/* Behaviour 13 (a): move the notes this run ACTUALLY READ into the prompt
 * (the assembly-time list, name-sorted) into <dest> (the first model call
 * succeeded, so those notes are consumed). M1063 (SV:0904.2): the move no
 * longer re-lists the inbox -- the pre-fix code moved EVERY .md present
 * when the child exited, so a note dropped after assembly was filed under
 * a run that never read it. It moves EXACTLY the list's names; an empty
 * list (no assembly, or no notes) moves nothing. rename first (one
 * filesystem), falling back to copy-then-unlink. A vanished note is
 * skipped, not an error. */
static void
move_inbox_notes(const char *inbox, const char *dest,
                 char *const *names, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        const char *name;
        size_t nlen;
        char src[PATH_CAP];
        char dst[PATH_CAP];

        name = names[i];
        if (name == NULL) {
            continue;
        }
        nlen = strlen(name);
        if (nlen <= 3 || strcmp(name + nlen - 3, ".md") != 0) {
            continue;
        }
        if (jc_snprintf(src, sizeof src, "%s/%s", inbox, name)
            >= (int)sizeof src) {
            continue;
        }
        if (jc_snprintf(dst, sizeof dst, "%s/%s", dest, name)
            >= (int)sizeof dst) {
            continue;
        }
        if (!jc_file_exists(src)) {
            continue;
        }
        if (rename(src, dst) != 0) {
            char *text = NULL;
            jc_size len = 0;
            struct jc_arena *ra = jc_arena_new(0);
            if (ra != NULL && jc_read_file(src, &text, &len, ra) == JC_OK) {
                if (jc_write_file(dst, text, len) == JC_OK) {
                    (void)unlink(src);
                }
            }
            jc_arena_free(ra);
        }
    }
}

/* Append `line\n` to the run's meta.txt (behaviour 10/12's end=/rc= shape).
 * A meta that cannot be opened is not fatal to the run's exit class: the
 * reference appends unconditionally and a records gap is the loop's (H5's)
 * report, not the run's. */
static void
meta_append(const char *meta_path, const char *line)
{
    FILE *f = fopen(meta_path, "ab");
    if (f != NULL) {
        fputs(line, f);
        fputc('\n', f);
        fclose(f);
    }
}

/* Read one pipe end (non-blocking) appending its bytes to `fd`, until the
 * pipe is quiet or has reached EOF. Returns 0 when the read returned EOF (or
 * a hard error: the writer is gone) and 1 while more data can still arrive
 * (EAGAIN). The caller selects on the fd before calling, so a 1 here means
 * "round-robin again", not "poll in a tight loop". */
static int
drain_fd(int pfd, int fd)
{
    char chunk[8192];
    int r;
    ssize_t w;
    for (;;) {
        r = (int)read(pfd, chunk, sizeof chunk);
        if (r > 0) {
            w = write(fd, chunk, (size_t)r);
            (void)w; /* a short/failed append is advisory (behaviour 4): the loop re-reads */
            continue;
        }
        if (r < 0 && errno == EAGAIN) {
            return 1;
        }
        return 0;   /* EOF (r == 0) or a hard error: the writer is done */
    }
}

int
jc_harness_run(const char *config_path, const char *tenant,
               const char *run, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    struct jc_harness_tenant *t = NULL;
    const char *dir;
    const char *keyfile;
    const char *workdir;
    const char *home;
    const char *lead_model;
    const char *end_text;
    char key[1024];
    char prompt_file[PATH_CAP];
    char bin_path[PATH_CAP];
    char jichi_default[PATH_CAP];
    char run_dir[PATH_CAP];
    char records_dir[PATH_CAP];
    char ctl_dir[PATH_CAP];
    char notes_dir[PATH_CAP];
    char inbox_dir[PATH_CAP];
    char stream_path[PATH_CAP];
    char stderr_path[PATH_CAP];
    char journal_path[PATH_CAP];
    char telemetry_path[PATH_CAP];
    char prompt_path[PATH_CAP];
    char meta_path[PATH_CAP];
    char config_copy_path[PATH_CAP];
    char started_path[PATH_CAP];
    char e_global[PATH_CAP];
    char e_system[PATH_CAP];
    /* The handoff (M1058): the previous run's records, built before the
     * spawn (the value is known for the meta's handoff= line, behaviour 12). */
    char prev_run_dir[PATH_CAP];
    char prev_meta_path[PATH_CAP];
    char prev_notes_dir[PATH_CAP];
    char handoff_path[PATH_CAP];
    char handoff_err_path[PATH_CAP];
    char handoff_buf[JC_HARNESS_HANDOFF_OUT_CAP];
    char handoff_err[256];
    int handoff_len = 0;
    char prev_name[64];
    int prev_num;
    /* M1063 (SV:0904.2): the assembly-time list of the inbox notes this run
     * READS into the prompt (the .md names, byte-wise ascending -- the
     * reference's sorted glob, run-lead.sh 93). Filled by the prompt
     * assembly; the post-run move takes it and moves EXACTLY those names,
     * so a note dropped into the inbox after assembly is neither read nor
     * moved. notes_read_count stays 0 when the assembly never runs (a
     * refusal returns before it), so an empty list moves nothing. The array
     * is stack-local (in scope until the move below); the strings it points
     * at are arena-allocated into the run's arena `a`, which outlives this
     * function -- so neither needs a free here.
     * The cap bounds a run's inbox notes; a list past it defers the extras
     * to the next run (they are left out of BOTH the prompt and the move,
     * so nothing is ever consumed undelivered). */
    char *note_names[512];
    int notes_read_count = 0;
    char *prompt_file_a = NULL;
    char *bin_a = NULL;
    char *git_name = NULL;
    char *git_email = NULL;
    char *cfgbytes = NULL;
    jc_size cfglen = 0;
    char cfgsha[JC_SHA256_HEX_LEN + 1];
    char version[64];
    char *stamp = NULL;
    char head[80] = "";
    char start_iso[48];
    char end_iso[48];
    char *b64;
    jc_size b64cap;
    int i;
    int rc;
    int child_rc;

    /* Behaviour 2, at the very start, before any read or spawn: the file is
     * the ONLY source. Unset the two ambient key names from the harness's
     * OWN process environment first (the driver's R2 canary must not reach
     * the model). The key is later passed in the spawn's env vector, which
     * is applied AFTER the scrub, so it wins -- and only for that one child.
     * The harness never setenv's it. */
    unsetenv(RUN_KEY_NAME);
    unsetenv("JLU_API_KEY");

    /* --- behaviour 1: the pre-run refusals, all BEFORE any write or spawn --- */

    if (config_path == NULL || config_path[0] == '\0') {
        refuse("needs --config <file> (the harness configuration)");
        return 2;
    }
    if (tenant == NULL || tenant[0] == '\0') {
        refuse("needs --tenant <name> (one of the config's tenants)");
        return 2;
    }
    if (run == NULL || run[0] == '\0') {
        refuse("needs --run <N> (the zero-padded run number)");
        return 2;
    }
    memset(&cfg, 0, sizeof cfg);
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        const char *msg = jc_harness_config_error(&cfg);
        refuse("the harness config could not be loaded: %s",
               (msg != NULL) ? msg : "(no message)");
        return 2;
    }
    for (i = 0; i < cfg.n_tenants; i++) {
        if (strcmp(cfg.tenants[i].name, tenant) == 0) {
            t = &cfg.tenants[i];
            break;
        }
    }
    if (t == NULL) {
        refuse("no tenant named %s (the config has %d tenant(s))",
               tenant, cfg.n_tenants);
        return 2;
    }
    workdir = t->workdir;
    home = t->home;
    if (workdir == NULL || workdir[0] == '\0') {
        refuse("tenant %s has no workdir (a run needs a place to chdir)",
               tenant);
        return 2;
    }
    if (home == NULL || home[0] == '\0') {
        refuse("tenant %s has no home (the child's isolated state root)",
               tenant);
        return 2;
    }
    dir = cfg.dir;
    if (dir == NULL || dir[0] == '\0') {
        refuse("the drive dir is not set (the config's dir)");
        return 2;
    }
    keyfile = cfg.model_key_file;
    if (keyfile == NULL || keyfile[0] == '\0') {
        refuse("the model key file (JICHI_HARNESS_MODEL_KEY_FILE or the "
               "config's modelKeyFile) is not set: a run has no key to hand "
               "to the model");
        return 2;
    }
    /* The tenant's extra fields the shared loader does not carry. */
    if (!read_run_extras(config_path, tenant, &prompt_file_a, &bin_a,
                         &git_name, &git_email, a)) {
        refuse("the harness config could not be re-read for tenant %s "
               "(promptFile / bin / the commit identity)", tenant);
        return 2;
    }
    prompt_file[0] = '\0';
    if (prompt_file_a != NULL) {
        if (jc_snprintf(prompt_file, sizeof prompt_file, "%s", prompt_file_a)
            >= (int)sizeof prompt_file) {
            refuse("tenant %s's promptFile path is too long", tenant);
            return 2;
        }
    }
    if (prompt_file[0] == '\0') {
        refuse("tenant %s has no promptFile (the base prompt text)", tenant);
        return 2;
    }
    {
        char *pt = NULL;
        jc_size pl = 0;
        if (jc_read_file(prompt_file, &pt, &pl, a) != JC_OK) {
            refuse("the prompt file could not be read (%s)", prompt_file);
            return 2;
        }
    }
    /* The pinned jichi: the tenant's bin, or the jichi next to the harness. */
    bin_path[0] = '\0';
    if (bin_a != NULL) {
        if (jc_snprintf(bin_path, sizeof bin_path, "%s", bin_a)
            >= (int)sizeof bin_path) {
            refuse("tenant %s's bin path is too long", tenant);
            return 2;
        }
    }
    if (bin_path[0] == '\0') {
        if (default_jichi_bin(jichi_default, sizeof jichi_default) == 0) {
            if (jc_snprintf(bin_path, sizeof bin_path, "%s", jichi_default)
                >= (int)sizeof bin_path) {
                refuse("the default jichi path is too long");
                return 2;
            }
        } else {
            refuse("no jichi binary: tenant %s has no bin and there is no "
                   "jichi next to the running harness", tenant);
            return 2;
        }
    }
    if (!jc_file_exists(bin_path)) {
        refuse("the jichi binary %s is missing or unreadable", bin_path);
        return 2;
    }
    /* The key file is read before any write (behaviour 2's fail-closed;
     * cell R3's refusal precedes the layout). */
    if (!read_key_file(keyfile, key, sizeof key, a)) {
        return 2;
    }
    /* The tenant's jichi config bytes: sha256 for meta, a copy for records. */
    if (t->jichi_config == NULL || t->jichi_config[0] == '\0') {
        refuse("tenant %s has no jichiConfig (the jichi config to run with)",
               tenant);
        return 2;
    }
    if (jc_read_file(t->jichi_config, &cfgbytes, &cfglen, a) != JC_OK) {
        refuse("the tenant's jichi config could not be read (%s)",
               t->jichi_config);
        return 2;
    }
    jc_sha256_hex(cfgbytes, cfglen, cfgsha);

    /* The pinned build's version and stamp (behaviour 6's read; the driver's
     * R1 asserts a `build:` stamp of 8+ hex digits, and the prompt's pinned
     * build line carries it too -- so it is read BEFORE the prompt). */
    if (!read_jichi_version(bin_path, version, sizeof version, &stamp, a)
        && stamp == NULL) {
        /* The binary answered no --version line at all: name it, fail closed. */
        refuse("cannot read the pinned build from %s (--version answered "
               "nothing)", bin_path);
        return 1;
    }
    if (stamp == NULL) {
        stamp = (char *)jc_arena_strdup(a, "(no build stamp)");
    }
    lead_model = cfg.drive_lead_model;
    end_text = cfg.drive_end;

    /* --- the lead-model guard (SV:1818.1): WARN + OMIT, not a refusal ------ */
    /* A drive.leadModel the tenant's jichi config does not NAME is not passed
     * as --model: it would reach the gateway as a raw id (the incident the
     * reference's comment names). The script's behaviour (run-lead.sh 49-52):
     * warn on the harness's stderr (the model named, never a key or a URL),
     * clear the name, and run on the config's routing. The guard runs BEFORE
     * the spawn and BEFORE any record write and does not change the exit
     * code. When the name is omitted, the meta's lead_model= line and the
     * argv below both fall back to "(the config's routing)" / no --model, as
     * the reference's cleared variable does. */
    if (lead_model != NULL && lead_model[0] != '\0' &&
        !lead_model_named(t->jichi_config, lead_model, a)) {
        fprintf(stderr,
                "jichi-harness: run: lead-model %s is not a model of the "
                "tenant's jichi config: ignored\n", lead_model);
        fflush(stderr);
        lead_model = NULL;
    }

    /* --- behaviour 4: the records layout, OUTSIDE the workdir -------------- */
    if (jc_snprintf(run_dir, sizeof run_dir, "%s/runs/%s", dir, run)
        >= (int)sizeof run_dir) {
        refuse("the drive dir with run %s is too long to lay out", run);
        return 1;
    }
    if (jc_mkdir_p(run_dir) != JC_OK) {
        refuse("could not create %s (mkdir -p failed)", run_dir);
        return 1;
    }
    /* The Mode-A records path (SV:1818.3): the drive dir's runs directory,
     * ABSOLUTE, stated in the "## This run" prompt and in meta.txt. In the
     * reference the records were mounted read-only at /records/runs; in Mode
     * A the agent's shell reads and writes the real path, so the standing
     * prompt and the handoffs' /records/runs references are stated once here,
     * where they actually point. */
    {
        char runs_dir[PATH_CAP];
        if (jc_snprintf(runs_dir, sizeof runs_dir, "%s/runs", dir)
            >= (int)sizeof runs_dir) {
            refuse("the drive dir is too long to lay out");
            return 1;
        }
        if (realpath(runs_dir, records_dir) == NULL) {
            refuse("could not make the records dir %s absolute (realpath "
                   "failed)", runs_dir);
            return 1;
        }
    }
    if (jc_snprintf(ctl_dir, sizeof ctl_dir, "%s/ctl", run_dir)
        >= (int)sizeof ctl_dir) {
        refuse("the drive dir with run %s is too long to lay out", run);
        return 1;
    }
    if (jc_mkdir_p(ctl_dir) != JC_OK) {
        refuse("could not create %s (mkdir -p failed)", ctl_dir);
        return 1;
    }
    if (jc_snprintf(notes_dir, sizeof notes_dir, "%s/notes", run_dir)
        >= (int)sizeof notes_dir) {
        refuse("the drive dir with run %s is too long to lay out", run);
        return 1;
    }
    if (jc_mkdir_p(notes_dir) != JC_OK) {
        refuse("could not create %s (mkdir -p failed)", notes_dir);
        return 1;
    }
    if (jc_snprintf(inbox_dir, sizeof inbox_dir, "%s/inbox", dir)
        >= (int)sizeof inbox_dir) {
        refuse("the drive dir is too long to lay out");
        return 1;
    }
    if (jc_mkdir_p(inbox_dir) != JC_OK) {
        refuse("could not create %s (mkdir -p failed)", inbox_dir);
        return 1;
    }
    if (jc_snprintf(stream_path, sizeof stream_path, "%s/stream.jsonl",
                    run_dir) >= (int)sizeof stream_path ||
        jc_snprintf(stderr_path, sizeof stderr_path, "%s/stderr.log",
                    run_dir) >= (int)sizeof stderr_path ||
        jc_snprintf(journal_path, sizeof journal_path, "%s/journal.jsonl",
                    run_dir) >= (int)sizeof journal_path ||
        jc_snprintf(telemetry_path, sizeof telemetry_path,
                    "%s/telemetry.jsonl", run_dir)
            >= (int)sizeof telemetry_path ||
        jc_snprintf(prompt_path, sizeof prompt_path, "%s/prompt.md", run_dir)
            >= (int)sizeof prompt_path ||
        jc_snprintf(meta_path, sizeof meta_path, "%s/meta.txt", run_dir)
            >= (int)sizeof meta_path ||
        jc_snprintf(config_copy_path, sizeof config_copy_path,
                    "%s/config.json", run_dir)
            >= (int)sizeof config_copy_path ||
        jc_snprintf(started_path, sizeof started_path, "%s/.started", run_dir)
            >= (int)sizeof started_path ||
        jc_snprintf(handoff_path, sizeof handoff_path, "%s/handoff.md",
                    run_dir) >= (int)sizeof handoff_path ||
        jc_snprintf(handoff_err_path, sizeof handoff_err_path,
                    "%s/handoff.err", run_dir) >= (int)sizeof handoff_err_path) {
        refuse("the drive dir with run %s is too long to lay out", run);
        return 1;
    }

    /* --- behaviour 5: the prompt, assembled in order ----------------------- */
    {
        struct jc_sb sb;
        char *pt = NULL;
        jc_size pl = 0;
        DIR *d;
        struct dirent *e;
        int first_note = 1;
        /* M1063: the inbox's .md names, byte-wise ascending (the reference's
         * sorted glob, run-lead.sh 93). Populated below by a list-then-sort
         * pass and READ in that order; note_names/notes_read_count (the
         * function scope) ARE the assembly-time list the move takes. */
        char *name_list[512];
        int name_list_n = 0;
        jc_size si;
        jc_size sj;

        jc_sb_init(&sb);
        if (jc_read_file(prompt_file, &pt, &pl, a) != JC_OK) {
            jc_sb_free(&sb);
            refuse("the prompt file could not be read (%s)", prompt_file);
            return 2;
        }
        jc_sb_append_n(&sb, pt, pl);
        now_iso(start_iso, sizeof start_iso);
        jc_sb_append_fmt(&sb, "\n## This run\n\nRun %s, started %s.",
                         run, start_iso);
        if (end_text != NULL && end_text[0] != '\0') {
            jc_sb_append_fmt(&sb, " The drive ends %s.", end_text);
        }
        jc_sb_append_fmt(&sb,
                         "\nThe pinned build (for the Jichi-Build trailer): %s.",
                         stamp);
        /* The Mode-A records path (SV:1818.3): the standing prompt and the
         * handoffs refer to /records/runs, which does not exist in Mode A
         * (the reference's read-only mounts are dropped, behaviour 3), so
         * state once where the records actually are. The line ends with a
         * newline (SV:1937.1): the reference's "## This run" block ends with
         * `\n` (run-lead.sh printf) and handoff.md BEGINS with `\n`
         * (handoff.py's `"\n" + build()`), so the two newlines between the
         * records sentence and `## What the previous run left you` are the
         * reference's BLANK line -- without this trailing newline the
         * header fuses onto the records sentence. */
        jc_sb_append_fmt(&sb,
                         "\nThe records for this run (journal, stream, the "
                         "meta, the previous runs' records) are in %s/.\n",
                         records_dir);
        /* The handoff section (M1058): built from the PREVIOUS run's own
         * records (its stream.jsonl, meta.txt, filed notes) and embedded
         * here. The previous run number is run-1 (the reference's
         * loop.sh arithmetic, leading-zero-stripped: a leading zero is
         * octal in shell, so 0001-1 = 0, not 1); a run of 0 or a
         * non-numeric run name has NO previous run (a first run). When the
         * section is non-empty it is embedded verbatim (its own header
         * leads it); when empty -- a first run, or a drop -- nothing is
         * embedded, so a first run's prompt has NO handoff header at all
         * (the M1057 "(none -- the first run)" stub is gone). The section
         * is also written to runs/<N>/handoff.md (always -- empty is legal,
         * the reference's ": >" shape) and the filed reason to
         * runs/<N>/handoff.err (always, empty on success). */
        prev_run_dir[0] = '\0';
        prev_meta_path[0] = '\0';
        prev_notes_dir[0] = '\0';
        prev_name[0] = '\0';
        prev_num = 0;
        handoff_buf[0] = '\0';
        handoff_err[0] = '\0';
        {
            int all_digits = 1;
            int run_len = (int)strlen(run);
            int p = 0;
            for (p = 0; p < run_len; p++) {
                if (run[p] < '0' || run[p] > '9') {
                    all_digits = 0;
                    break;
                }
            }
            if (all_digits && run_len > 0 && run_len < (int)sizeof prev_name) {
                prev_num = atoi(run);
            }
        }
        if (prev_num > 0) {
            jc_snprintf(prev_name, sizeof prev_name, "%04d", prev_num - 1);
            jc_snprintf(prev_run_dir, sizeof prev_run_dir, "%s/runs/%s",
                        dir, prev_name);
            jc_snprintf(prev_meta_path, sizeof prev_meta_path,
                        "%s/meta.txt", prev_run_dir);
            jc_snprintf(prev_notes_dir, sizeof prev_notes_dir, "%s/notes",
                        prev_run_dir);
        }
        /* The builder is invoked ONLY when the previous run's record dir
         * actually exists (the reference's `[ -d "$D/runs/$PREV" ]` guard,
         * run-lead.sh 81): for a genuine first run (0001 -> 0000, no dir)
         * the builder is skipped, no reason is filed, and both handoff.md
         * and handoff.err are written EMPTY (handoff_len 0, handoff_err
         * empty) with meta handoff=none -- the contract's no-previous-run
         * case (M1058 note, behaviour 2, the note at the top of it). For
         * 0002+ (the previous dir exists) the builder runs and keeps its
         * fail-open reason on a drop (the reference's class, behaviour 1);
         * a missing stream on an EXISTING run is still filed, unchanged. */
        if (prev_run_dir[0] != '\0' && jc_is_dir(prev_run_dir)) {
            jc_harness_build_handoff(prev_run_dir, prev_meta_path,
                                     prev_notes_dir, records_dir,
                                     handoff_buf, sizeof handoff_buf,
                                     handoff_err, sizeof handoff_err,
                                     &handoff_len);
        }
        /* handoff.md and handoff.err are ALWAYS written (the reference's
         * ": >" + "2>" shape): empty is legal. */
        if (jc_write_file(handoff_path, handoff_buf,
                          (jc_size)handoff_len) != JC_OK) {
            jc_sb_free(&sb);
            refuse("could not write the handoff record (%s)", handoff_path);
            return 1;
        }
        if (jc_write_file(handoff_err_path, handoff_err,
                          (jc_size)strlen(handoff_err)) != JC_OK) {
            jc_sb_free(&sb);
            refuse("could not write the handoff record (%s)",
                   handoff_err_path);
            return 1;
        }
        if (handoff_len > 0) {
            jc_sb_append_n(&sb, handoff_buf, (jc_size)handoff_len);
        }
        /* Behaviour 13 (a): the supervisor notes are READ into the prompt
         * (a read, not a move); the move happens only on a successful first
         * model call, after the child exits. M1063 (SV:0904.2): the inbox is
         * listed first, its .md names SORTED byte-wise ascending (the
         * reference's sorted glob, run-lead.sh 93 -- a readdir order is
         * unspecified, so the pre-fix read could reach the prompt out of
         * order), and each note is then read IN THAT ORDER. The sorted names
         * become note_names / notes_read_count (the function scope) -- the
         * assembly-time list the move takes. A missing or unreadable note is
         * skipped from BOTH the array and the prompt, so it is never moved.
         * The name_list copy is the pre-sort staging; note_names holds the
         * sorted result the move reads. */
        d = opendir(inbox_dir);
        if (d != NULL) {
            while ((e = readdir(d)) != NULL) {
                size_t nlen = strlen(e->d_name);
                char *dup;
                if (nlen <= 3 || strcmp(e->d_name + nlen - 3, ".md") != 0) {
                    continue;
                }
                if (name_list_n >= 512) {
                    break;   /* past the cap: the extras stay for the next run */
                }
                dup = jc_arena_strdup(a, e->d_name);
                if (dup == NULL) {
                    continue;
                }
                name_list[name_list_n++] = dup;
            }
            closedir(d);
            /* Insertion sort, byte-wise ascending (the handoff's own idiom,
             * hh_list_notes; an inbox holds a handful of names). */
            for (si = 1; si < (jc_size)name_list_n; si++) {
                char *key = name_list[si];
                for (sj = si; sj > 0 && strcmp(name_list[sj - 1], key) > 0;
                     sj--) {
                    name_list[sj] = name_list[sj - 1];
                }
                name_list[sj] = key;
            }
        }
        for (si = 0; si < (jc_size)name_list_n; si++) {
            const char *nm = name_list[si];
            size_t nlen = strlen(nm);
            char src[PATH_CAP];
            char base[PATH_CAP];
            char *nt = NULL;
            jc_size nl = 0;
            if (nlen <= 3 || strcmp(nm + nlen - 3, ".md") != 0) {
                continue;
            }
            if (jc_snprintf(src, sizeof src, "%s/%s", inbox_dir, nm)
                >= (int)sizeof src) {
                continue;
            }
            /* The basename WITHOUT the .md (the note's "### <basename>"
             * shape); nm may not be mutated. */
            jc_snprintf(base, sizeof base, "%.*s", (int)(nlen - 3), nm);
            if (jc_read_file(src, &nt, &nl, a) != JC_OK) {
                continue;   /* unreadable: skipped from array AND prompt */
            }
            /* Read into the prompt now; remember the name so the move (on a
             * successful first call) consumes exactly this one. */
            if (notes_read_count < 512) {
                note_names[notes_read_count++] = (char *)nm;
            }
            if (first_note) {
                jc_sb_append(&sb, "\n## Notes from the manager\n");
                first_note = 0;
            }
            jc_sb_append_fmt(&sb, "\n### %s\n\n", base);
            jc_sb_append_n(&sb, nt, nl);
        }
        /* -- the base64 for --prompt-b64 -- */
        b64cap = jc_base64_encoded_len(sb.len) + 1;
        b64 = (char *)jc_arena_calloc(a, b64cap);
        if (b64 == NULL) {
            jc_sb_free(&sb);
            refuse("out of memory encoding the prompt");
            return 1;
        }
        if (jc_base64_encode((const unsigned char *)sb.data, sb.len, b64,
                             b64cap) != JC_OK) {
            jc_sb_free(&sb);
            refuse("could not base64-encode the prompt");
            return 1;
        }
        if (jc_write_file(prompt_path, sb.data, sb.len) != JC_OK) {
            jc_sb_free(&sb);
            refuse("could not write the assembled prompt (%s)", prompt_path);
            return 1;
        }
        jc_sb_free(&sb);
    }

    /* --- behaviour 10: meta.txt, written BEFORE the spawn ------------------ */
    /* The M2-neutral team_head, read in the workdir BEFORE the spawn
     * (behaviour 11: a hook planted during the run cannot affect this stamp). */
    if (!make_empty_git_files(dir, e_global, sizeof e_global,
                              e_system, sizeof e_system)) {
        refuse("could not create the empty GIT_CONFIG files in the drive "
               "root %s", dir);
        return 1;
    }
    if (!team_head(workdir, e_global, e_system, head, sizeof head)) {
        head[0] = '\0';   /* meta says team_head=unknown (the reference's own
                            2>/dev/null fallback, run-lead.sh line 122) */
    }
    {
        char lines[9][256];
        char buf[4096];
        int n = 0;
        int j;
        size_t off = 0;

        jc_snprintf(lines[n++], 256, "run=%s", run);
        jc_snprintf(lines[n++], 256, "start=%s", start_iso);
        jc_snprintf(lines[n++], 256, "jichi=%s build %s", version, stamp);
        jc_snprintf(lines[n++], 256, "config=%s sha256=%s",
                    t->jichi_config, cfgsha);
        jc_snprintf(lines[n++], 256, "records_dir=%s", records_dir);
        jc_snprintf(lines[n++], 256, "team_head=%s",
                    (head[0] != '\0') ? head : "unknown");
        jc_snprintf(lines[n++], 256, "lead_model=%s",
                    (lead_model != NULL && lead_model[0] != '\0')
                        ? lead_model : "(the config's routing)");
        if (handoff_len > 0) {
            /* The reference's "handoff=from run $PREV, $(wc -c) bytes"
             * (run-lead.sh 125): the section is present, so name the run it
             * came from and its size. */
            jc_snprintf(lines[n++], 256,
                        "handoff=from run %s, %d bytes", prev_name,
                        handoff_len);
        } else {
            /* A first run (no previous run) or a drop (the section came
             * back empty): the fixed line (the reference's else branch). */
            jc_snprintf(lines[n++], 256, "handoff=none");
        }
        jc_snprintf(lines[n++], 256,
                    "limits: max-tool-calls=%s verify-timeout=%s; "
                    "no deadline, no token budget",
                    RUN_MAX_TOOL_CALLS, RUN_VERIFY_TIMEOUT);
        buf[0] = '\0';
        for (j = 0; j < n; j++) {
            size_t l = strlen(lines[j]);
            if (off + l + 1 >= sizeof buf) {
                refuse("the run's meta lines do not fit the buffer");
                return 1;
            }
            memcpy(buf + off, lines[j], l);
            off += l;
            buf[off++] = '\n';
            buf[off] = '\0';
        }
        if (jc_write_file(meta_path, buf, off) != JC_OK) {
            refuse("could not write the run's meta (%s)", meta_path);
            return 1;
        }
    }
    /* The exact tenant jichi config, kept with its records (the reference's
     * `cp -p "$CONFIG"`). */
    if (jc_write_file(config_copy_path, cfgbytes, cfglen) != JC_OK) {
        refuse("could not keep a copy of the tenant's jichi config (%s)",
               config_copy_path);
        return 1;
    }
    /* The .started marker (behaviour 10; bounds the post-run collection
     * M1058+ adds, def-355). */
    {
        FILE *f = fopen(started_path, "ab");
        if (f != NULL) {
            fclose(f);
        }
    }

    /* --- behaviours 3/6/7/8/9: the spawn ---------------------------------- */
    {
        int sfd;
        int efd;
        int sfp[2] = { -1, -1 };
        int efp[2] = { -1, -1 };
        pid_t pid = -1;
        int status = 0;
        int out_open;
        int err_open;
        char env_home[PATH_CAP + 16];
        char env_tmp[PATH_CAP + 16];
        char env_key[1100];
        char env_path[4096];
        char env_gan[448];
        char env_gae[448];
        char env_gcn[448];
        char env_gce[448];
        char *envs[12];
        int ne = 0;
        int i2;
        const char *ambient_path = getenv("PATH");
        struct jc_vec envv;
        char *argv[28];
        char ctl_sock[PATH_CAP];
        int k = 0;
        struct timeval tv;
        fd_set set;
        int ready;
        struct sigaction sa;
        struct sigaction sa_old;

        /* M1059 behaviour 9: install the SIGINT handler BEFORE the spawn.
         * It forwards a group SIGINT to the jichi group and sets a flag; it
         * never re-raises (SV:1838.2). The pgid global stays 0 (a sentinel)
         * until the spawn has a pid, so a pre-spawn signal cannot kill(-1).
         * sa_old is pre-seeded with the default disposition so a restore is
         * always valid even if the install itself fails. */
        memset(&sa, 0, sizeof sa);
        memset(&sa_old, 0, sizeof sa_old);
        sa_old.sa_handler = SIG_DFL;
        sigemptyset(&sa_old.sa_mask);
        sa.sa_handler = run_on_sigint;
        sigemptyset(&sa.sa_mask);
        (void)sigaction(SIGINT, &sa, &sa_old);

        /* Open the two record files the harness appends (jichi's stdout and
         * stderr); the harness owns them, jichi writes its own
         * journal/telemetry. */
        sfd = open(stream_path, O_WRONLY | O_CREAT | O_APPEND | JC_O_BINARY, 0644);
        efd = open(stderr_path, O_WRONLY | O_CREAT | O_APPEND | JC_O_BINARY, 0644);
        if (sfd < 0 || efd < 0) {
            if (sfd >= 0) {
                close(sfd);
            }
            if (efd >= 0) {
                close(efd);
            }
            refuse("could not open the run's record files (%s / %s)",
                   stream_path, stderr_path);
            now_iso(end_iso, sizeof end_iso);
            {
                char tail[160];
                jc_snprintf(tail, sizeof tail, "end=%s rc=1", end_iso);
                meta_append(meta_path, tail);
            }
            return 1;
        }

        /* The spawn's env vector (behaviour 8 + 2): applied after the scrub,
         * so the explicit JICHI_API_KEY wins. The parent NEVER setenv's it. */
        jc_snprintf(env_home, sizeof env_home, "HOME=%s", home);
        jc_snprintf(env_tmp, sizeof env_tmp, "TMPDIR=%s", run_dir);
        jc_snprintf(env_key, sizeof env_key, "%s=%s", RUN_KEY_NAME, key);
        jc_snprintf(env_path, sizeof env_path, "PATH=%s",
                    (ambient_path != NULL && ambient_path[0] != '\0')
                        ? ambient_path : "/usr/local/bin:/usr/bin:/bin");
        if (git_name != NULL && git_name[0] != '\0') {
            jc_snprintf(env_gan, sizeof env_gan, "GIT_AUTHOR_NAME=%s",
                        git_name);
            jc_snprintf(env_gcn, sizeof env_gcn, "GIT_COMMITTER_NAME=%s",
                        git_name);
            envs[ne++] = env_gan;
            envs[ne++] = env_gcn;
        }
        if (git_email != NULL && git_email[0] != '\0') {
            jc_snprintf(env_gae, sizeof env_gae, "GIT_AUTHOR_EMAIL=%s",
                        git_email);
            jc_snprintf(env_gce, sizeof env_gce, "GIT_COMMITTER_EMAIL=%s",
                        git_email);
            envs[ne++] = env_gae;
            envs[ne++] = env_gce;
        }
        envs[ne++] = env_home;
        envs[ne++] = env_tmp;
        envs[ne++] = env_path;
        envs[ne++] = env_key;
        envs[ne] = NULL;

        jc_vec_init(&envv, sizeof(char *));
        for (i2 = 0; envs[i2] != NULL; i2++) {
            if (jc_vec_push(&envv, &envs[i2]) != JC_OK) {
                jc_vec_free(&envv);
                close(sfd);
                close(efd);
                refuse("out of memory building the child's environment");
                return 1;
            }
        }

        /* The spawn argv (behaviours 6/7): caps off (no --deadline, no
         * --budget-tokens), fences on (--max-tool-calls, --verify-timeout).
         * journal/telemetry are plain PATHS jichi opens (behaviour 4: never
         * a named pipe). */
        argv[k++] = (char *)bin_path;
        argv[k++] = (char *)"--config";
        argv[k++] = (char *)t->jichi_config;
        argv[k++] = (char *)"--auto";
        argv[k++] = (char *)"--no-rollback";
        if (lead_model != NULL && lead_model[0] != '\0') {
            argv[k++] = (char *)"--model";
            argv[k++] = (char *)lead_model;
        }
        argv[k++] = (char *)"--journal";
        argv[k++] = (char *)journal_path;
        argv[k++] = (char *)"--log";
        argv[k++] = (char *)telemetry_path;
        argv[k++] = (char *)"--log-level";
        argv[k++] = (char *)"full";
        argv[k++] = (char *)"--output";
        argv[k++] = (char *)"jsonl";
        argv[k++] = (char *)"--heartbeat";
        argv[k++] = (char *)"60";
        if (jc_snprintf(ctl_sock, sizeof ctl_sock, "%s/lead.sock", ctl_dir)
            < (int)sizeof ctl_sock) {
            argv[k++] = (char *)"--control";
            argv[k++] = ctl_sock;
        }
        argv[k++] = (char *)"--max-tool-calls";
        argv[k++] = (char *)RUN_MAX_TOOL_CALLS;
        argv[k++] = (char *)"--verify-timeout";
        argv[k++] = (char *)RUN_VERIFY_TIMEOUT;
        argv[k++] = (char *)"--prompt-b64";
        argv[k++] = (char *)b64;
        argv[k] = NULL;

        if (jc_pipe_cloexec(sfp) != 0 || jc_pipe_cloexec(efp) != 0) {
            if (sfp[0] >= 0) {
                close(sfp[0]);
                close(sfp[1]);
            }
            if (efp[0] >= 0) {
                close(efp[0]);
                close(efp[1]);
            }
            close(sfd);
            close(efd);
            jc_vec_free(&envv);
            refuse("could not create the record pipes");
            return 1;
        }
        /* The parent's read ends (sfp[0], efp[0]) become NON-BLOCKING so the
         * drain below can round-robin the two pipes without one full pipe
         * deadlocking against the other; the child's write ends (sfp[1],
         * efp[1]) are dup2'd by the child, which drops the flag (jc_proc.h
         * M472). */
        (void)fcntl(sfp[0], F_SETFL, O_NONBLOCK);
        (void)fcntl(efp[0], F_SETFL, O_NONBLOCK);
        out_open = 1;
        err_open = 1;

        {
            struct jc_spawn_req req;
            memset(&req, 0, sizeof req);
            req.argv = argv;
            req.fd_stdin = JC_SPAWN_DEVNULL;   /* behaviour 9: never a TTY */
            req.fd_stdout = sfp[1];
            req.fd_stderr = efp[1];
            req.new_pgroup = 1;                /* M461: kill(-pid) takes all */
            req.scrub_env = 1;                 /* M130: drop the registered */
            req.env = &envv;                   /*   names, then re-add ours  */
            req.cwd = workdir;                 /* Mode A: chdir into the team */

            rc = (jc_spawn(&req, &pid) == JC_OK) ? 0 : -1;
            if (rc == 0) {
                /* new_pgroup=1: the jichi child leads its own group and its
                 * pgid IS its pid. Record it for the handler's forward
                 * (behaviour 9); a pre-spawn signal saw the 0 sentinel. */
                g_run_jichi_pgid = pid;
                if (g_run_sigint_seen) kill(-pid, SIGINT);
            }
            if (rc != 0) {
                sigaction(SIGINT, &sa_old, NULL);   /* restore; no child was made */
                close(sfp[0]);
                close(sfp[1]);
                close(efp[0]);
                close(efp[1]);
                close(sfd);
                close(efd);
                jc_vec_free(&envv);
                refuse("the spawn of %s failed (no child could be "
                       "started)", bin_path);
                now_iso(end_iso, sizeof end_iso);
                {
                    char tail[160];
                    jc_snprintf(tail, sizeof tail, "end=%s rc=1", end_iso);
                    meta_append(meta_path, tail);
                }
                return 1;
            }
        }
        jc_vec_free(&envv);
        close(sfp[1]);
        close(efp[1]);

        /* The harness appends the child's stdout/stderr to its record files
         * while it waits: stdout -> stream.jsonl, stderr -> stderr.log.
         * select rounds over the two non-blocking read ends, so neither
         * blocks the other. A pipe is retired only on a real EOF, NOT on
         * EAGAIN (EAGAIN means quiet, not closed -- retiring on it would
         * lose bytes and end the loop while the child still writes). */
        for (;;) {
            int hi = 0;
            if (!out_open && !err_open) {
                break;
            }
            FD_ZERO(&set);
            if (out_open) {
                FD_SET(sfp[0], &set);
                if (sfp[0] > hi) {
                    hi = sfp[0];
                }
            }
            if (err_open) {
                FD_SET(efp[0], &set);
                if (efp[0] > hi) {
                    hi = efp[0];
                }
            }
            tv.tv_sec = 0;
            tv.tv_usec = 100000;   /* a short bound: the loop re-checks state */
            ready = select(hi + 1, &set, NULL, NULL, &tv);
            if (ready < 0 && errno != EINTR) {
                break;   /* select itself failed: drain to the end is gone */
            }
            if (out_open && drain_fd(sfp[0], sfd) == 0) {
                out_open = 0;
                close(sfp[0]);
            }
            if (err_open && drain_fd(efp[0], efd) == 0) {
                err_open = 0;
                close(efp[0]);
            }
        }
        close(sfd);
        close(efd);

        /* Reap the child (behaviour 12: the child's exit code IS the run's
         * code; a signal death is non-zero, never masked to 0). A SIGINT that
         * interrupted this wait was forwarded to the jichi group (behaviour 9)
         * and jc_proc_waitpid retried the EINTR; the run only returns once
         * jichi is actually dead, so its meta is written on the normal path
         * below (the reference's split, SV:1838.2). */
        child_rc = 1;
        if (jc_proc_waitpid(pid, &status) == 0) {
            if (WIFEXITED(status)) {
                child_rc = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                child_rc = 128 + WTERMSIG(status);
            }
        }
        /* The child is dead: restore the SIGINT disposition the run found and
         * clear the pgid so a later signal (the loop's deadline check does not
         * fire again, but a manual Ctrl-C on the run itself would) cannot
         * forward to a reaped group. */
        sigaction(SIGINT, &sa_old, NULL);
        g_run_jichi_pgid = 0;
    }

    /* --- behaviours 10/12: end= and rc= appended AFTER the child exits ----- */
    now_iso(end_iso, sizeof end_iso);
    {
        char tail[160];
        jc_snprintf(tail, sizeof tail, "end=%s", end_iso);
        meta_append(meta_path, tail);
        jc_snprintf(tail, sizeof tail, "rc=%d", child_rc);
        meta_append(meta_path, tail);
    }
    /* The team head AFTER the run (CHANGED 1): the run writes it so its own
     * handoff's ended() can say "It ended at commit `<sha>`". The reference's
     * loop.sh wrote it from the mirror; here the run re-reads the workdir's
     * head after the child exits. Unreadable -> "unknown". */
    {
        char head_after[80];
        char tail[160];
        if (team_head(workdir, e_global, e_system, head_after,
                      sizeof head_after)) {
            jc_snprintf(tail, sizeof tail, "team_head_after=%s", head_after);
        } else {
            jc_snprintf(tail, sizeof tail, "team_head_after=unknown");
        }
        meta_append(meta_path, tail);
    }

    /* --- behaviour 13: move the notes only on a successful first call ------ */
    if (stream_has_text(stream_path)) {
        move_inbox_notes(inbox_dir, notes_dir, note_names, notes_read_count);
    }

    return child_rc;
}
