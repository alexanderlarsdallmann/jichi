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
 * Returns the child exit code, or -1 if it could not be run. */
static int git_capture(const char *cwd, const char *const *args, int nargs,
                       struct jc_sb *out, jc_size cap)
{
    char *argv[24];
    int fds[2];
    pid_t pid;
    int status = 0;
    int k = 0;
    int i;

    argv[k++] = (char *)"git";
    argv[k++] = (char *)"-C";
    argv[k++] = (char *)cwd;
    for (i = 0; i < nargs && k < 23; i++) {
        argv[k++] = (char *)args[i];
    }
    argv[k] = NULL;

    if (jc_pipe_cloexec(fds) != 0) return -1;
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        jc_proc_child_close_fds(); /* M472: and not our fds */
        jc_proc_child_sigreset(); /* M461 */
        execvp("git", argv);
        _exit(127);
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
    const char *a[5];
    const char *path = tu_arg_str(args, "path");
    int staged = tu_arg_bool(args, "staged", 0);
    struct jc_sb sb;
    int n = 0;

    a[n++] = "diff";
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
    0 /* main_agent_only (M436) */
};

static const struct jc_tool DIFF_TOOL = {
    "git_diff",
    "Show changes as a unified diff (git diff). Optionally limit to a 'path', "
    "or pass staged:true for the staged diff. Read-only.",
    diff_schema, 1, diff_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool LOG_TOOL = {
    "git_log",
    "Show recent commit history one line each (git log --oneline). Optionally "
    "limit to a 'path' or set 'max' (default 20). Read-only.",
    log_schema, 1, log_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool BLAME_TOOL = {
    "git_blame",
    "Show, for each line of a file, the commit that last changed it "
    "(git blame). Optionally restrict to a 'start'/'end' line range. Read-only.",
    blame_schema, 1, blame_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool ADD_TOOL = {
    "git_add",
    "Stage files for the next commit (git add). Pass 'paths' (an array) or "
    "all:true to stage every change. Mutating.",
    add_schema, 0, add_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool COMMIT_TOOL = {
    "git_commit",
    "Record the staged changes as a commit (git commit -m). 'message' is "
    "required; set all:true to also stage modified tracked files first. "
    "Returns the new commit's summary. Mutating.",
    commit_schema, 0, commit_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool BRANCH_TOOL = {
    "git_branch",
    "Switch branches, or create one with create:true (git checkout [-b] "
    "<name>). Mutating.",
    branch_schema, 0, branch_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool STASH_TOOL = {
    "git_stash",
    "Save the working tree to a stash, or restore it with pop:true "
    "(git stash push/pop). Mutating.",
    stash_schema, 0, stash_run, NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_git_status(void) { return &STATUS_TOOL; }
const struct jc_tool *jc_tool_git_diff(void)   { return &DIFF_TOOL; }
const struct jc_tool *jc_tool_git_log(void)    { return &LOG_TOOL; }
const struct jc_tool *jc_tool_git_blame(void)  { return &BLAME_TOOL; }
const struct jc_tool *jc_tool_git_add(void)    { return &ADD_TOOL; }
const struct jc_tool *jc_tool_git_commit(void) { return &COMMIT_TOOL; }
const struct jc_tool *jc_tool_git_branch(void) { return &BRANCH_TOOL; }
const struct jc_tool *jc_tool_git_stash(void)  { return &STASH_TOOL; }
