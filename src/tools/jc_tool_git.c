/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_git.c - git tools.
 *
 * Read-only: git_status / git_diff / git_log / git_blame.
 * Mutating (M39): git_add / git_commit / git_branch / git_stash -- so an agent
 * can record its own work as reviewable commits (the read-only tools could only
 * observe). The mutating ones are readonly=0, so they go through the normal
 * jc_perm gate (ASK in chat, auto-approved in AUTO, hidden in PLAN's read-only
 * fence) exactly like edit_file/run_terminal_command.
 *
 * All run plain `git -C <cwd> ...` against the USER's repository (not the
 * snapshot shadow repo), argv-style via fork/exec/pipe so model-supplied paths
 * are never shell-interpreted. Output (stdout+stderr) is byte-capped.
 */

#include "jc_toolcaps.h"
#include "jc_proc.h"
#include "jc_spawn.h"   /* M832 */
#include "tool_util.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>


/* Run `git -C cwd <args>` capturing stdout+stderr into `out` (capped at `cap`).
 * Returns the child exit code, or -1 if it could not be run.
 *
 * M2 (the 2026-10-16 plan; drive-readiness handoff of 2026-10-16, in docs/internal/, section 8.3): jichi's git children inherit the user environment, so a
 * malicious repo (or a drive team writing /work/.git/config) can plant
 * core.fsmonitor (an EXTERNAL process), alias.*.command / ext-diff, and hooks,
 * and every `git` here would run them. Measured: a planted fsmonitor fired with
 * the inherited env. The base neutralisation is injected at this ONE spawn site
 * so every git child -- read or mutating -- carries it, and tests/smoke/
 * git_neutralisation.sh holds it red-first:
 *   -c core.fsmonitor=        empty value WINS over the planted config, so the
 *                             fsmonitor script is never exec'd (the measured hole)
 *   -c log.showSignature=false keep a planted gpg program out of log/show/blame
 *                             (it stops the READ path from verifying signatures
 *                             and invoking gpg.program; it does NOT stop commit)
 *   -c commit.gpgsign=false    the COMMIT path: a planted commit.gpgsign=true +
 *                             gpg.program would make jichi's git_commit EXECUTE
 *                             that program, so force signing off
 *   -c core.hooksPath=/dev/null  disable the hooks path.
 *
 * The plan scoped hooksPath to the read-only calls, but the hook vector actually
 * fires on the MUTATING ones: a planted hooks/pre-commit runs when jichi's
 * git_commit records a change, which is the higher-value hole. All four -c
 * flags are safe on every git child jichi runs (they do not affect add/commit/
 * checkout/stash/worktree), so they are applied uniformly here. The plan's
 * read-only --no-ext-diff --no-textconv are diff-only (git rejects them on
 * status/ls-files), so they are added at the git_diff call site only -- where
 * an external diff program or textconv could be invoked. */
static int git_capture(const char *cwd, const char *const *args, int nargs,
                       struct jc_sb *out, jc_size cap)
{
    /* 32 wide: "git" + the 8 M2 neutral flags + "-C" + cwd (11) + up to 20 args
     * + NUL. The 20-arg copy budget is EXACTLY the pre-M2 one (argv was 24 wide
     * then), so the neutralisation adds no truncation of the caller's command. */
    char *argv[32];
    int fds[2];
    pid_t pid;
    int status = 0;
    int k = 0;
    int i;

    argv[k++] = (char *)"git";
    /* M2 base neutralisation: see the comment above this function. */
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.fsmonitor=";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"log.showSignature=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"commit.gpgsign=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.hooksPath=/dev/null";
    argv[k++] = (char *)"-C";
    argv[k++] = (char *)cwd;
    for (i = 0; i < nargs && k < 31; i++) {
        argv[k++] = (char *)args[i];
    }
    argv[k] = NULL;

    if (jc_pipe_cloexec(fds) != 0) return -1;
    /* M832: through jc_spawn. M2: the git child scrubs the registered secret
     * env (the plan's scrub_env=1). It was "no scrub, as ever" before M2, on the
     * assumption git is first-party and never runs user code -- but a git child
     * invokes programs NAMED IN THE REPO CONFIG (core.fsmonitor, ext-diff,
     * alias.*.command, hooks), so a planted one would otherwise inherit the
     * API keys. The -c neutralisation above stops those programs running; the
     * scrub is the second layer if one still got through. */
    {
        struct jc_spawn_req sr;
        memset(&sr, 0, sizeof sr);
        sr.argv = argv;
        sr.scrub_env = 1;
        /* M844 (ACP-1): never jichi's own stdin -- over ACP it is the client's
         * JSON-RPC stream, and a command that reads stdin ate its next message
         * (a session/cancel). tests/smoke/acp_child_stdin.sh holds this. */
        sr.fd_stdin = JC_SPAWN_DEVNULL;
        sr.fd_stdout = fds[1];
        sr.fd_stderr = fds[1];
        if (jc_spawn(&sr, &pid) != JC_OK) {
            close(fds[0]);
            close(fds[1]);
            return -1;
        }
    }
    {
        char buf[1024];
        ssize_t n;
        int truncated = 0;
        close(fds[1]);
        while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
            if (out->len < cap) {
                jc_size room = cap - out->len;
                jc_sb_append_n(out, buf,
                               ((jc_size)n < room) ? (jc_size)n : room);
                if ((jc_size)n >= room) truncated = 1;
            } else {
                truncated = 1;
            }
        }
        close(fds[0]);
        if (truncated) jc_sb_append(out, "\n... [output truncated]");
    }
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/* M684: which of PATHS does git track?  Writes 1 or 0 into OUT[i] for each
 * input path; returns 0 when git answered and -1 when it could not be run at
 * all, so a caller can tell "none are tracked" from "nobody asked".
 *
 * WHY THE JOURNAL WANTS THIS. `--strict-green` downgrades a run that touched a
 * file outside its edit scope. Measured at M662 over 91 run journals it would
 * have downgraded 16 of 35 successful runs -- 46% -- and the classification of
 * the 1,158 flagged paths says why: 85% of them are the WORK'S OWN OUTPUT (a
 * downloaded PDF, a `.o`, jichi's own state) and only 15% is source. The line
 * that separates the two is version control, and nothing recorded it.
 *
 * SHAPE. The paths are passed to `git ls-files` as operands, so the output is
 * at most the tracked subset rather than the whole index -- a repository with
 * 2,000 tracked files does not cost 60 KB of pipe to ask about two paths.
 * Batched, because git_capture's argv is bounded.
 *
 * A PATH THIS CANNOT PARSE IS REPORTED UNTRACKED, which is the conservative
 * direction: `git ls-files` quotes a name containing a newline or a quote, and
 * a misread there must not promote an artifact to "a gate file". The cost of
 * being wrong is asymmetric, so the fallback is chosen rather than left to
 * whichever way the parser happens to fail. */
int jc_git_tracked_paths(const char *cwd, const char *const *paths,
                         int npaths, int *out)
{
    int i;
    int base;
    const int per = 16;         /* git_capture's argv is 24 wide, minus git -C */

    if (out == NULL || npaths <= 0) return 0;
    for (i = 0; i < npaths; i++) out[i] = 0;
    if (cwd == NULL || paths == NULL) return -1;
    /* NB: this returns a BOOLEAN (1 = git works here), not a status code.
     * Written as `!= 0` it means the opposite of what it reads like, which is
     * how the first version of this function reported "no git" on every
     * repository it was given. */
    if (!jc_tool_git_available(cwd)) return -1;

    for (base = 0; base < npaths; base += per) {
        const char *a[2 + 16];
        struct jc_sb sb;
        int n = npaths - base;
        int k = 0;
        int rc;

        if (n > per) n = per;
        a[k++] = "ls-files";
        a[k++] = "--";
        for (i = 0; i < n; i++) a[k++] = paths[base + i];

        jc_sb_init(&sb);
        rc = git_capture(cwd, a, k, &sb, 65536);
        if (rc != 0) {
            jc_sb_free(&sb);
            return -1;
        }
        /* One tracked path per line. Compare whole lines: a prefix match would
         * call `docs/a.md` tracked because `docs/a.md.bak` is. */
        if (sb.data != NULL) {
            const char *line = sb.data;
            while (*line != '\0') {
                const char *nl = strchr(line, '\n');
                jc_size len = (nl != NULL) ? (jc_size)(nl - line)
                                           : (jc_size)strlen(line);
                for (i = 0; i < n; i++) {
                    const char *cand = paths[base + i];
                    if (cand != NULL && strlen(cand) == len &&
                        strncmp(cand, line, len) == 0) {
                        out[base + i] = 1;
                    }
                }
                if (nl == NULL) break;
                line = nl + 1;
            }
        }
        jc_sb_free(&sb);
    }
    return 0;
}

int jc_tool_git_available(const char *cwd)
{
    const char *a[2];
    struct jc_sb sink;
    int rc;

    a[0] = "rev-parse";
    a[1] = "--is-inside-work-tree";
    jc_sb_init(&sink);
    rc = git_capture(cwd, a, 2, &sink, 256);
    jc_sb_free(&sink);
    return rc == 0;
}

/* ---- pure helpers (unit-tested) ----------------------------------------- */

int jc_git_clamp_max(int requested)
{
    if (requested < 1) return 20;
    if (requested > 100) return 100;
    return requested;
}

int jc_git_blame_range(int start, int end, char *buf, int cap)
{
    if (start <= 0) {
        buf[0] = '\0';
        return 0;
    }
    if (end >= start) {
        jc_snprintf(buf, (jc_size)cap, "%d,%d", start, end);
    } else {
        jc_snprintf(buf, (jc_size)cap, "%d,", start);
    }
    return 1;
}

/* ---- shared result finish ----------------------------------------------- */

static void git_finish(struct jc_tool_result *out, struct jc_sb *sb, int rc)
{
    if (rc == -1) {
        jc_sb_free(sb);
        tu_err(out, "error: failed to run git");
        return;
    }
    if (sb->len == 0) jc_sb_append(sb, "(no output)");
    out->content = jc_sb_finish(sb);
    out->is_error = (rc != 0);
    jc_sb_free(sb);
}

/* ---- tools -------------------------------------------------------------- */

static cJSON *status_schema(void) { return tu_schema_begin(); }

static jc_status status_run(const cJSON *args, struct jc_tool_result *out,
                            struct jc_app *app)
{
    const char *a[3];
    struct jc_sb sb;
    (void)args;
    a[0] = "status";
    a[1] = "--short";
    a[2] = "--branch";
    jc_sb_init(&sb);
    git_finish(out, &sb, git_capture(app->cwd, a, 3, &sb,
                              jc_config_cap(app->config.git_max_bytes,
                                            JC_CAP_GIT_DEFAULT)));
    return JC_OK;
}

static cJSON *diff_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "Optional file/dir to limit the diff to.", 0);
    tu_schema_bool(s, "staged",
        "Show staged changes (git diff --staged) instead of the working tree.",
        0);
    return s;
}

static jc_status diff_run(const cJSON *args, struct jc_tool_result *out,
                          struct jc_app *app)
{
    const char *a[7];   /* M2: +2 for the diff-only flags; 7 holds the max below */
    const char *path = tu_arg_str(args, "path");
    int staged = tu_arg_bool(args, "staged", 0);
    struct jc_sb sb;
    int n = 0;

    /* M2: diff-only flags -- never on status/ls-files, where git rejects them.
     * --no-ext-diff keeps a planted external diff program out of this read. */
    a[n++] = "diff";
    a[n++] = "--no-ext-diff";
    a[n++] = "--no-textconv";
    if (staged) a[n++] = "--staged";
    if (path != NULL && path[0] != '\0') {
        a[n++] = "--";
        a[n++] = path;
    }
    jc_sb_init(&sb);
    git_finish(out, &sb, git_capture(app->cwd, a, n, &sb,
                              jc_config_cap(app->config.git_max_bytes,
                                            JC_CAP_GIT_DEFAULT)));
    return JC_OK;
}

static cJSON *log_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "Optional file/dir to limit history to.", 0);
    tu_schema_int(s, "max", "Number of commits to show (default 20, max 100).",
                  0);
    return s;
}

static jc_status log_run(const cJSON *args, struct jc_tool_result *out,
                         struct jc_app *app)
{
    const char *a[6];
    const char *path = tu_arg_str(args, "path");
    int max = jc_git_clamp_max(tu_arg_int(args, "max", 20));
    char nbuf[16];
    struct jc_sb sb;
    int n = 0;

    jc_snprintf(nbuf, sizeof(nbuf), "%d", max);
    a[n++] = "log";
    a[n++] = "--oneline";
    a[n++] = "-n";
    a[n++] = nbuf;
    if (path != NULL && path[0] != '\0') {
        a[n++] = "--";
        a[n++] = path;
    }
    jc_sb_init(&sb);
    git_finish(out, &sb, git_capture(app->cwd, a, n, &sb,
                              jc_config_cap(app->config.git_max_bytes,
                                            JC_CAP_GIT_DEFAULT)));
    return JC_OK;
}

static cJSON *blame_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "The file to blame.", 1);
    tu_schema_int(s, "start", "Optional first line of a range.", 0);
    tu_schema_int(s, "end", "Optional last line of a range.", 0);
    return s;
}

static jc_status blame_run(const cJSON *args, struct jc_tool_result *out,
                           struct jc_app *app)
{
    const char *a[6];
    const char *path = tu_arg_str(args, "path");
    int start = tu_arg_int(args, "start", 0);
    int end = tu_arg_int(args, "end", 0);
    char lbuf[40];
    struct jc_sb sb;
    int n = 0;

    if (path == NULL || path[0] == '\0') {
        tu_err(out, "error: 'path' is required");
        return JC_OK;
    }
    a[n++] = "blame";
    if (jc_git_blame_range(start, end, lbuf, sizeof(lbuf))) {
        a[n++] = "-L";
        a[n++] = lbuf;
    }
    a[n++] = "--";
    a[n++] = path;
    jc_sb_init(&sb);
    git_finish(out, &sb, git_capture(app->cwd, a, n, &sb,
                              jc_config_cap(app->config.git_max_bytes,
                                            JC_CAP_GIT_DEFAULT)));
    return JC_OK;
}

/* ---- mutating tools (M39) ----------------------------------------------- */

/* Finish a mutating action: an exec failure is a tool error; a non-zero git
 * exit returns git's captured output as an error (e.g. "nothing to commit");
 * success returns git's output, or `ok_when_empty` when git printed nothing. */
static void git_finish_mut(struct jc_tool_result *out, struct jc_sb *sb, int rc,
                           const char *ok_when_empty)
{
    if (rc == -1) {
        jc_sb_free(sb);
        tu_err(out, "error: failed to run git");
        return;
    }
    if (rc != 0) {
        if (sb->len == 0) {
            jc_sb_append(sb, "git command failed");
        }
        out->content = jc_sb_finish(sb);
        out->is_error = 1;
        jc_sb_free(sb);
        return;
    }
    if (sb->len == 0) {
        jc_sb_append(sb, ok_when_empty);
    }
    out->content = jc_sb_finish(sb);
    out->is_error = 0;
    jc_sb_free(sb);
}

/* True when `s` is NULL or only ASCII whitespace. */
static int git_blank(const char *s)
{
    if (s == NULL) {
        return 1;
    }
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    return *s == '\0';
}

static cJSON *add_schema(void)
{
    cJSON *s = tu_schema_begin();
    cJSON *props = cJSON_GetObjectItem(s, "properties");
    cJSON *arr = cJSON_CreateObject();
    cJSON *items = cJSON_CreateObject();
    cJSON_AddStringToObject(arr, "type", "array");
    cJSON_AddStringToObject(arr, "description",
        "Files/dirs to stage (relative to the repo). Omit and set all:true to "
        "stage every change.");
    cJSON_AddStringToObject(items, "type", "string");
    cJSON_AddItemToObject(arr, "items", items);
    cJSON_AddItemToObject(props, "paths", arr);
    tu_schema_bool(s, "all", "Stage all changes, like `git add -A` (default "
                            "false).", 0);
    return s;
}

static jc_status add_run(const cJSON *args, struct jc_tool_result *out,
                         struct jc_app *app)
{
    const char *a[24];
    int n = 0, npath = 0, all = tu_arg_bool(args, "all", 0);
    cJSON *paths = cJSON_GetObjectItem((cJSON *)args, "paths");
    struct jc_sb sb;

    a[n++] = "add";
    if (all) {
        a[n++] = "-A";
    } else if (cJSON_IsArray(paths) && cJSON_GetArraySize(paths) > 0) {
        cJSON *it;
        a[n++] = "--";
        cJSON_ArrayForEach(it, paths) {
            if (cJSON_IsString(it) && it->valuestring != NULL && n < 23) {
                a[n++] = it->valuestring;
                npath++;
            }
        }
        if (npath == 0) {
            tu_err(out, "error: 'paths' had no valid string entries");
            return JC_OK;
        }
    } else {
        tu_err(out, "error: provide 'paths' (an array) or set all:true");
        return JC_OK;
    }
    jc_sb_init(&sb);
    git_finish_mut(out, &sb, git_capture(app->cwd, a, n, &sb,
                       jc_config_cap(app->config.git_max_bytes, JC_CAP_GIT_DEFAULT)),
                   "Staged changes.");
    return JC_OK;
}

static cJSON *commit_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "message", "The commit message.", 1);
    tu_schema_bool(s, "all",
        "Also stage modified/deleted tracked files first (git commit -a). "
        "Untracked files still need git_add. Default false.", 0);
    return s;
}

static jc_status commit_run(const cJSON *args, struct jc_tool_result *out,
                            struct jc_app *app)
{
    const char *a[6];
    const char *msg = tu_arg_str(args, "message");
    int n = 0, all = tu_arg_bool(args, "all", 0);
    struct jc_sb sb;

    if (git_blank(msg)) {
        tu_err(out, "error: 'message' is required and must not be empty");
        return JC_OK;
    }
    a[n++] = "commit";
    if (all) {
        a[n++] = "-a";
    }
    a[n++] = "-m";
    a[n++] = msg;
    jc_sb_init(&sb);
    git_finish_mut(out, &sb, git_capture(app->cwd, a, n, &sb,
                       jc_config_cap(app->config.git_max_bytes, JC_CAP_GIT_DEFAULT)),
                   "Committed.");
    return JC_OK;
}

static cJSON *branch_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "name", "Branch to switch to (or create).", 1);
    tu_schema_bool(s, "create",
        "Create the branch from the current HEAD and switch to it "
        "(git checkout -b). Default false (switch to an existing branch).", 0);
    return s;
}

/* branch_run -- switch to or create a branch via git checkout.
 *
 * The model may send a branch name that begins with `-`. A git branch name
 * legitimately starting with `-` does not exist in practice (git check-ref-format
 * rejects leading `-` for new refs), so REFUSING is the smaller correct fix.
 *
 * An alternative would be to insert `--` before the name (as the other git tools
 * do). That alone is NOT sufficient: `git checkout -b -- -x` would still try to
 * CREATE a branch with a leading dash, which git refuses via check-ref-format.
 * The guard covers both create and switch cases with a single check.
 *
 * See docs/analysis/2026-10-17-hardening-03-processes-and-fences.md B-03
 * (already recorded as R04-F8, 2026-10-08 review, still open). */
static jc_status branch_run(const cJSON *args, struct jc_tool_result *out,
                            struct jc_app *app)
{
    const char *a[4];
    const char *name = tu_arg_str(args, "name");
    int n = 0, create = tu_arg_bool(args, "create", 0);
    struct jc_sb sb;

    if (git_blank(name)) {
        tu_err(out, "error: 'name' is required");
        return JC_OK;
    }
    if (name[0] == '-') {
        tu_err(out, "error: 'name' must not begin with a dash (it would be read as a git option); branch names in this run do not start with '-'");
        return JC_OK;
    }
    a[n++] = "checkout";
    if (create) {
        a[n++] = "-b";
    }
    a[n++] = name;
    jc_sb_init(&sb);
    git_finish_mut(out, &sb, git_capture(app->cwd, a, n, &sb,
                       jc_config_cap(app->config.git_max_bytes, JC_CAP_GIT_DEFAULT)),
                   create ? "Created and switched to the branch."
                          : "Switched branch.");
    return JC_OK;
}

static cJSON *stash_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_bool(s, "pop",
        "Restore and remove the most recent stash (git stash pop). Default "
        "false (save the working tree to a new stash).", 0);
    tu_schema_string(s, "message", "Optional label when saving a stash.", 0);
    return s;
}

static jc_status stash_run(const cJSON *args, struct jc_tool_result *out,
                           struct jc_app *app)
{
    const char *a[5];
    const char *msg = tu_arg_str(args, "message");
    int n = 0, pop = tu_arg_bool(args, "pop", 0);
    struct jc_sb sb;

    a[n++] = "stash";
    if (pop) {
        a[n++] = "pop";
    } else if (!git_blank(msg)) {
        /* `save`, not `push -m`: push was only added in git 2.13, and V2f
         * (stretch, git 2.11) is a supported build target; save takes the
         * message as a plain argument on every git we run against. */
        a[n++] = "save";
        a[n++] = msg;
    }
    /* no message: bare `git stash` -- the default subcommand on all versions */
    jc_sb_init(&sb);
    git_finish_mut(out, &sb, git_capture(app->cwd, a, n, &sb,
                       jc_config_cap(app->config.git_max_bytes, JC_CAP_GIT_DEFAULT)),
                   pop ? "Popped the latest stash." : "Stashed changes.");
    return JC_OK;
}

static const struct jc_tool STATUS_TOOL = {
    "git_status",
    "Show the working-tree status (git status --short --branch): branch, and "
    "staged/unstaged/untracked files. Read-only.",
    status_schema, 1, status_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

static const struct jc_tool DIFF_TOOL = {
    "git_diff",
    "Show changes as a unified diff (git diff). Optionally limit to a 'path', "
    "or pass staged:true for the staged diff. Read-only.",
    diff_schema, 1, diff_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

static const struct jc_tool LOG_TOOL = {
    "git_log",
    "Show recent commit history one line each (git log --oneline). Optionally "
    "limit to a 'path' or set 'max' (default 20). Read-only.",
    log_schema, 1, log_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

static const struct jc_tool BLAME_TOOL = {
    "git_blame",
    "Show, for each line of a file, the commit that last changed it "
    "(git blame). Optionally restrict to a 'start'/'end' line range. Read-only.",
    blame_schema, 1, blame_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

static const struct jc_tool ADD_TOOL = {
    "git_add",
    "Stage files for the next commit (git add). Pass 'paths' (an array) or "
    "all:true to stage every change. Mutating.",
    add_schema, 0, add_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

static const struct jc_tool COMMIT_TOOL = {
    "git_commit",
    "Record the staged changes as a commit (git commit -m). 'message' is "
    "required; set all:true to also stage modified tracked files first. "
    "Returns the new commit's summary. Mutating.",
    commit_schema, 0, commit_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

static const struct jc_tool BRANCH_TOOL = {
    "git_branch",
    "Switch branches, or create one with create:true (git checkout [-b] "
    "<name>). Mutating.",
    branch_schema, 0, branch_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

static const struct jc_tool STASH_TOOL = {
    "git_stash",
    "Save the working tree to a stash, or restore it with pop:true "
    "(git stash push/pop). Mutating.",
    stash_schema, 0, stash_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

const struct jc_tool *jc_tool_git_status(void) { return &STATUS_TOOL; }
const struct jc_tool *jc_tool_git_diff(void)   { return &DIFF_TOOL; }
const struct jc_tool *jc_tool_git_log(void)    { return &LOG_TOOL; }
const struct jc_tool *jc_tool_git_blame(void)  { return &BLAME_TOOL; }
const struct jc_tool *jc_tool_git_add(void)    { return &ADD_TOOL; }
const struct jc_tool *jc_tool_git_commit(void) { return &COMMIT_TOOL; }
const struct jc_tool *jc_tool_git_branch(void) { return &BRANCH_TOOL; }
const struct jc_tool *jc_tool_git_stash(void)  { return &STASH_TOOL; }
