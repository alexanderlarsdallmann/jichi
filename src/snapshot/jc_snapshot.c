/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_snapshot.c - git-backed workspace snapshots & undo (see jc_snapshot.h).
 *
 * A shadow git repository (its own GIT_DIR, work tree = the workspace) records
 * checkpoints without touching the user's own .git. git is spawned argv-style
 * via fork/exec/pipe so workspace paths need no shell quoting. POSIX-only. */

#include "jc_snapshot.h"
#include "jc_proc.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "jc_message.h"
#include "jc_rewind.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* Beyond this many files in a non-git workspace, snapshots disable themselves
 * (a `git add -A` would be too heavy). Git repos are trusted to bound the set
 * via .gitignore and skip the check. */
#define JC_SNAPSHOT_MAX_FILES 20000

static unsigned long djb2(const char *s)
{
    unsigned long h = 5381u;
    while (*s != '\0') {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    return h;
}

unsigned long jc_workspace_key(const char *work_tree)
{
    return djb2(work_tree != NULL ? work_tree : "");
}

void jc_snapshot_git_dir(const char *home, const char *work_tree,
                         char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi.d/checkpoints/%lu",
                home != NULL ? home : ".",
                jc_workspace_key(work_tree));
}

enum jc_store_state jc_snapshot_store_state(const char *work_tree,
                                            int work_tree_exists,
                                            int parent_exists)
{
    if (work_tree == NULL || work_tree[0] == '\0') {
        return JC_STORE_UNKNOWN;
    }
    if (work_tree_exists) {
        return JC_STORE_LIVE;
    }
    /* Absent. Which kind of absent is the whole question, and the answer is a
     * guess: if the parent is there too, someone deleted a project; if the parent
     * is also gone, the likeliest cause is a filesystem that is not mounted, and
     * removing that repo would destroy the only copy of a live project's history. */
    return parent_exists ? JC_STORE_ORPHANED : JC_STORE_UNREACHABLE;
}

void jc_snapshot_clean_label(const char *s, char *buf, jc_size cap)
{
    jc_size o = 0;
    int prev_space = 0;

    if (cap == 0) {
        return;
    }
    if (s == NULL) {
        buf[0] = '\0';
        return;
    }
    while (*s != '\0' && o + 1 < cap) {
        unsigned char c = (unsigned char)*s;
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (o > 0 && !prev_space) {
                buf[o++] = ' ';
                prev_space = 1;
            }
        } else {
            buf[o++] = (char)c;
            prev_space = 0;
        }
        s++;
    }
    /* Drop a trailing space produced by collapsing. */
    if (o > 0 && buf[o - 1] == ' ') {
        o--;
    }
    buf[o] = '\0';
}

/* Run argv (NUL-terminated). If `out` is non-NULL, child stdout is captured
 * into it; otherwise stdout is discarded. stderr is always discarded. Returns
 * the child's exit code, or -1 if it could not be run. */
static int run_argv(char *const argv[], struct jc_sb *out)
{
    int fds[2];
    pid_t pid;
    int status = 0;

    if (out != NULL && jc_pipe_cloexec(fds) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        if (out != NULL) {
            close(fds[0]);
            close(fds[1]);
        }
        return -1;
    }
    if (pid == 0) {
        int dn;
        if (out != NULL) {
            dup2(fds[1], STDOUT_FILENO);
            close(fds[0]);
            close(fds[1]);
        } else {
            dn = open("/dev/null", O_WRONLY);
            if (dn >= 0) {
                dup2(dn, STDOUT_FILENO);
            }
        }
        dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) {
            dup2(dn, STDERR_FILENO);
        }
        jc_proc_child_close_fds(); /* M472: and not our fds */
        jc_proc_child_sigreset(); /* M461 */
        execvp(argv[0], argv);
        _exit(127);
    }
    if (out != NULL) {
        char buf[512];
        ssize_t n;
        close(fds[1]);
        while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
            jc_sb_append_n(out, buf, (jc_size)n);
        }
        close(fds[0]);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/* Run `git --git-dir=GD --work-tree=WT <a0..a4>` (NULL-terminated subcommand
 * args, up to 5). Captures stdout into `out` when non-NULL. */
static int git_run(struct jc_snapshot_mgr *m, const char *a0, const char *a1,
                   const char *a2, const char *a3, const char *a4,
                   struct jc_sb *out)
{
    char *argv[13];
    int n = 0;
    argv[n++] = (char *)"git";
    argv[n++] = (char *)"--git-dir";
    argv[n++] = m->git_dir;
    argv[n++] = (char *)"--work-tree";
    argv[n++] = m->work_tree;
    if (a0 != NULL) { argv[n++] = (char *)a0; }
    if (a1 != NULL) { argv[n++] = (char *)a1; }
    if (a2 != NULL) { argv[n++] = (char *)a2; }
    if (a3 != NULL) { argv[n++] = (char *)a3; }
    if (a4 != NULL) { argv[n++] = (char *)a4; }
    argv[n] = NULL;
    return run_argv(argv, out);
}

/* Like git_run but takes an arbitrary-length subcommand arg array (for
 * commands with more than five args, e.g. commit-tree -p X -m Y). */
static int git_runv(struct jc_snapshot_mgr *m, const char *const *extra,
                    int n, struct jc_sb *out)
{
    char *argv[24];
    int k = 0;
    int i;
    argv[k++] = (char *)"git";
    argv[k++] = (char *)"--git-dir";
    argv[k++] = m->git_dir;
    argv[k++] = (char *)"--work-tree";
    argv[k++] = m->work_tree;
    for (i = 0; i < n && k < 23; i++) {
        argv[k++] = (char *)extra[i];
    }
    argv[k] = NULL;
    return run_argv(argv, out);
}

/* Trim a trailing CR/LF (and anything after) in place. */
static void chomp(char *s)
{
    while (*s != '\0' && *s != '\n' && *s != '\r') {
        s++;
    }
    *s = '\0';
}

/* Write a single line into the shadow repo's info/exclude. */
static void write_exclude(struct jc_snapshot_mgr *m)
{
    char path[1100];
    jc_snprintf(path, sizeof(path), "%s/info/exclude", m->git_dir);
    /* Never snapshot the user's real git metadata. */
    jc_write_file(path, ".git/\n", 6);
}

/* Bounded recursive file count, short-circuiting once *n exceeds `limit`.
 * Skips dotfiles/dotdirs and a few notoriously huge build dirs; does not follow
 * symlinks. */
static void count_walk(const char *dir, long limit, long *n)
{
    DIR *d;
    struct dirent *e;

    if (*n > limit) {
        return;
    }
    d = opendir(dir);
    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL && *n <= limit) {
        const char *nm = e->d_name;
        char path[2048];
        struct stat st;
        if (nm[0] == '.') {
            continue;
        }
        if (strcmp(nm, "node_modules") == 0 || strcmp(nm, "target") == 0 ||
            strcmp(nm, "build") == 0 || strcmp(nm, "dist") == 0 ||
            strcmp(nm, "vendor") == 0) {
            continue;
        }
        jc_snprintf(path, sizeof(path), "%s/%s", dir, nm);
        if (lstat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            count_walk(path, limit, n);
        } else {
            (*n)++;
        }
    }
    closedir(d);
}

/* Refuse to snapshot a huge, un-git-managed tree (e.g. an accidental run in
 * $HOME). A workspace with a .git or .gitignore is trusted to bound `git add`. */
static int workspace_too_big(const char *root)
{
    char probe[1100];
    long n = 0;

    jc_snprintf(probe, sizeof(probe), "%s/.git", root);
    if (jc_file_exists(probe)) {
        return 0;
    }
    jc_snprintf(probe, sizeof(probe), "%s/.gitignore", root);
    if (jc_file_exists(probe)) {
        return 0;
    }
    count_walk(root, JC_SNAPSHOT_MAX_FILES, &n);
    return n > JC_SNAPSHOT_MAX_FILES;
}

/* Rebuild the shadow repo's history to the most recent `keep` checkpoints:
 * re-create those commits with the same trees via commit-tree (so content and
 * `reset --hard` are unaffected), repoint the branch, then expire the reflog
 * and gc to reclaim the dropped objects. Non-destructive to the work tree.
 * Bails out (leaving the repo intact) on any error. */
static void do_prune(struct jc_snapshot_mgr *m, int keep)
{
    struct jc_sb log;
    struct jc_sb branch;
    char bname[256];
    char nbuf[16];
    char parent[64];
    char newtip[64];
    int have_parent = 0;
    int ok = 1;
    char *p;

    /* Current branch (shadow repo is never detached, but guard anyway). */
    jc_sb_init(&branch);
    if (git_run(m, "rev-parse", "--abbrev-ref", "HEAD", NULL, NULL,
                &branch) != 0 || branch.data == NULL) {
        jc_sb_free(&branch);
        return;
    }
    chomp(branch.data);
    jc_snprintf(bname, sizeof(bname), "%s", branch.data);
    jc_sb_free(&branch);
    if (strcmp(bname, "HEAD") == 0 || bname[0] == '\0') {
        return;
    }

    /* The most recent `keep` commits, oldest first. */
    jc_snprintf(nbuf, sizeof(nbuf), "%d", keep);
    jc_sb_init(&log);
    {
        const char *a[5];
        a[0] = "log"; a[1] = "--reverse"; a[2] = "--format=%H%x09%s";
        a[3] = "-n"; a[4] = nbuf;
        if (git_runv(m, a, 5, &log) != 0 || log.data == NULL) {
            jc_sb_free(&log);
            return;
        }
    }

    newtip[0] = '\0';
    p = log.data;
    while (p != NULL && *p != '\0' && ok) {
        char *line = p;
        char *nl = strchr(p, '\n');
        char *tab;
        struct jc_sb tree;
        const char *subject;
        char treeref[80];

        if (nl != NULL) { *nl = '\0'; p = nl + 1; } else { p = NULL; }
        tab = strchr(line, '\t');
        if (tab == NULL) {
            continue;
        }
        *tab = '\0';
        subject = tab + 1;

        /* Resolve this commit's tree. */
        jc_sb_init(&tree);
        jc_snprintf(treeref, sizeof(treeref), "%s^{tree}", line);
        if (git_run(m, "rev-parse", treeref, NULL, NULL, NULL, &tree) != 0 ||
            tree.data == NULL) {
            jc_sb_free(&tree);
            ok = 0;
            break;
        }
        chomp(tree.data);

        /* Re-create the commit with the same tree, chained to the previous. */
        {
            struct jc_sb cs;
            const char *a[6];
            int na = 0;
            a[na++] = "commit-tree";
            a[na++] = tree.data;
            if (have_parent) { a[na++] = "-p"; a[na++] = parent; }
            a[na++] = "-m";
            a[na++] = (subject[0] != '\0') ? subject : "checkpoint";
            jc_sb_init(&cs);
            if (git_runv(m, a, na, &cs) != 0 || cs.data == NULL) {
                jc_sb_free(&cs);
                jc_sb_free(&tree);
                ok = 0;
                break;
            }
            chomp(cs.data);
            jc_snprintf(parent, sizeof(parent), "%s", cs.data);
            jc_snprintf(newtip, sizeof(newtip), "%s", cs.data);
            have_parent = 1;
            jc_sb_free(&cs);
        }
        jc_sb_free(&tree);
    }
    jc_sb_free(&log);

    if (!ok || newtip[0] == '\0') {
        jc_logf(JC_LOG_WARN, "snapshots: prune aborted; history left intact");
        return;
    }

    /* Repoint the branch at the rebuilt tip, then reclaim the orphans. */
    {
        char ref[300];
        jc_snprintf(ref, sizeof(ref), "refs/heads/%s", bname);
        if (git_run(m, "update-ref", ref, newtip, NULL, NULL, NULL) != 0) {
            jc_logf(JC_LOG_WARN, "snapshots: prune update-ref failed");
            return;
        }
    }
    git_run(m, "reflog", "expire", "--expire=now", "--all", NULL, NULL);
    git_run(m, "gc", "--prune=now", "--quiet", NULL, NULL, NULL);
    jc_logf(JC_LOG_INFO, "snapshots: pruned history to %d checkpoints", keep);
}

/* Prune when history has grown past 2x the retention limit (hysteresis: prune
 * back to the limit so it doesn't run every startup once at the cap). */
static void prune_if_needed(struct jc_snapshot_mgr *m)
{
    int limit = m->app->config.snapshot_limit;
    struct jc_sb out;
    long total;

    if (limit <= 0) {
        return; /* unlimited */
    }
    jc_sb_init(&out);
    if (git_run(m, "rev-list", "--count", "HEAD", NULL, NULL, &out) != 0 ||
        out.data == NULL) {
        jc_sb_free(&out); /* e.g. no commits yet */
        return;
    }
    total = atol(out.data);
    jc_sb_free(&out);
    if (total > (long)limit * 2) {
        do_prune(m, limit);
    }
}

jc_status jc_snapshot_manager_init(struct jc_snapshot_mgr *m,
                                   struct jc_app *app)
{
    char *probe[3];
    char head[1100];

    memset(m, 0, sizeof(*m));
    m->app = app;
    /* One config key governs every destructive restore in this file (M337b),
     * and M338 resolves its unset state HERE rather than at parse time: an
     * interactive destroy defaults ON. The envelope resolves the same key the
     * other way for its rollback, which is the whole point of the tri-state. */
    m->preserve = (app != NULL)
        ? (app->config.preserve_discarded != 0 ? 1 : 0)
        : 0;
    jc_vec_init(&m->checkpoints, sizeof(struct jc_snapshot));
    m->available = 0;

    if (!app->config.snapshots) {
        return JC_OK;
    }

    /* git present? */
    probe[0] = (char *)"git";
    probe[1] = (char *)"--version";
    probe[2] = NULL;
    if (run_argv(probe, NULL) != 0) {
        jc_logf(JC_LOG_DEBUG, "snapshots: git not available; disabled");
        return JC_OK;
    }

    jc_snprintf(m->work_tree, sizeof(m->work_tree), "%s", app->cwd);

    if (workspace_too_big(m->work_tree)) {
        jc_logf(JC_LOG_WARN, "snapshots: workspace has >%d files and no git; "
                "disabled (set \"snapshots\": false to silence)",
                JC_SNAPSHOT_MAX_FILES);
        return JC_OK;
    }

    jc_snapshot_git_dir(jc_home_dir(), m->work_tree, m->git_dir,
                        sizeof(m->git_dir));
    if (jc_mkdir_p(m->git_dir) != JC_OK) {
        jc_logf(JC_LOG_WARN, "snapshots: cannot create %s; disabled",
                m->git_dir);
        return JC_OK;
    }

    jc_snprintf(head, sizeof(head), "%s/HEAD", m->git_dir);
    if (!jc_file_exists(head)) {
        if (git_run(m, "init", "-q", NULL, NULL, NULL, NULL) != 0) {
            jc_logf(JC_LOG_WARN, "snapshots: git init failed; disabled");
            return JC_OK;
        }
        git_run(m, "config", "user.email", "jichi@localhost", NULL, NULL, NULL);
        git_run(m, "config", "user.name", "jichi", NULL, NULL, NULL);
        git_run(m, "config", "commit.gpgsign", "false", NULL, NULL, NULL);
    }
    write_exclude(m);

    m->available = 1;
    jc_logf(JC_LOG_DEBUG, "snapshots: shadow repo at %s", m->git_dir);
    prune_if_needed(m);     /* bound the shadow repo's history + disk */
    jc_snapshot_refresh(m); /* persist the stack across sessions/--resume */
    return JC_OK;
}

void jc_snapshot_manager_shutdown(struct jc_snapshot_mgr *m)
{
    if (m == NULL) {
        return;
    }
    jc_vec_free(&m->checkpoints);
    m->available = 0;
}

int jc_snapshot_available(const struct jc_snapshot_mgr *m)
{
    return m != NULL && m->available;
}

jc_status jc_snapshot_preserve(struct jc_snapshot_mgr *m, const char *label,
                               const char *ref, char *sha_out, jc_size cap)
{
    struct jc_sb sha;
    char *p;
    const char *extra[4];

    if (sha_out != NULL && cap > 0) {
        sha_out[0] = '\0';
    }
    if (!jc_snapshot_available(m) || ref == NULL || ref[0] == '\0') {
        return JC_OK;
    }

    git_run(m, "add", "-A", NULL, NULL, NULL, NULL);
    if (git_run(m, "commit", "-q", "--allow-empty", "-m",
                (label != NULL && label[0] != '\0') ? label : "discarded",
                NULL) != 0) {
        jc_logf(JC_LOG_WARN, "snapshots: could not commit the state about to be "
                "discarded; it will be lost");
        return JC_ERR_IO;
    }
    jc_sb_init(&sha);
    if (git_run(m, "rev-parse", "HEAD", NULL, NULL, NULL, &sha) != 0
            || sha.data == NULL) {
        jc_sb_free(&sha);
        return JC_ERR_IO;
    }
    p = sha.data;
    while (*p != '\0' && *p != '\n' && *p != '\r') {
        p++;
    }
    *p = '\0';

    /* The ref before the reset, always: after `restore_commit` moves the branch
     * back this commit is unreachable, and the ref is the only handle on it. */
    extra[0] = "update-ref";
    extra[1] = ref;
    extra[2] = sha.data;
    if (git_runv(m, extra, 3, NULL) != 0) {
        jc_logf(JC_LOG_WARN, "snapshots: could not pin %s; the discarded state "
                "may be garbage-collected", ref);
        jc_sb_free(&sha);
        return JC_ERR_IO;
    }
    if (sha_out != NULL && cap > 0) {
        jc_snprintf(sha_out, cap, "%s", sha.data);
    }
    jc_logf(JC_LOG_DEBUG, "snapshots: preserved %s at %s", ref, sha.data);
    jc_sb_free(&sha);
    return JC_OK;
}

jc_status jc_snapshot_take(struct jc_snapshot_mgr *m, const char *label)
{
    struct jc_sb sha;
    struct jc_snapshot snap;
    char clean[256];
    char *p;

    if (!jc_snapshot_available(m)) {
        return JC_OK;
    }

    git_run(m, "add", "-A", NULL, NULL, NULL, NULL);
    if (git_run(m, "commit", "-q", "--allow-empty", "-m",
                (label != NULL && label[0] != '\0') ? label : "checkpoint",
                NULL) != 0) {
        jc_logf(JC_LOG_WARN, "snapshots: commit failed");
        return JC_ERR_IO;
    }

    jc_sb_init(&sha);
    if (git_run(m, "rev-parse", "HEAD", NULL, NULL, NULL, &sha) != 0 ||
        sha.data == NULL) {
        jc_sb_free(&sha);
        return JC_ERR_IO;
    }
    /* Trim trailing newline from rev-parse output. */
    p = sha.data;
    while (*p != '\0' && *p != '\n' && *p != '\r') {
        p++;
    }
    *p = '\0';

    jc_snapshot_clean_label(label, clean, sizeof(clean));
    jc_snprintf(snap.commit, sizeof(snap.commit), "%s", sha.data);
    jc_snprintf(snap.label, sizeof(snap.label), "%s", clean);
    jc_sb_free(&sha);
    /* Bound the in-session stack the same way the startup load already is:
     * past JC_SNAPSHOT_MAX_LIST, drop the oldest (M180). The shadow repo
     * keeps the history; only the in-memory index rotates. */
    if (m->checkpoints.len >= JC_SNAPSHOT_MAX_LIST) {
        memmove(m->checkpoints.data,
                (char *)m->checkpoints.data + m->checkpoints.elem,
                (size_t)(m->checkpoints.len - 1) * m->checkpoints.elem);
        m->checkpoints.len--;
    }
    jc_vec_push(&m->checkpoints, &snap);
    jc_logf(JC_LOG_DEBUG, "snapshots: checkpoint %s (%s)", snap.commit,
            snap.label);
    return JC_OK;
}

int jc_snapshot_count(const struct jc_snapshot_mgr *m)
{
    if (m == NULL) {
        return 0;
    }
    return (int)m->checkpoints.len;
}

const char *jc_snapshot_label(const struct jc_snapshot_mgr *m, int i)
{
    struct jc_snapshot *s;
    if (m == NULL || i < 0 || (jc_size)i >= m->checkpoints.len) {
        return NULL;
    }
    s = (struct jc_snapshot *)jc_vec_at((struct jc_vec *)&m->checkpoints,
                                        (jc_size)i);
    return s->label;
}

const char *jc_snapshot_commit(const struct jc_snapshot_mgr *m, int i)
{
    struct jc_snapshot *s;
    if (m == NULL || i < 0 || (jc_size)i >= m->checkpoints.len) {
        return NULL;
    }
    s = (struct jc_snapshot *)jc_vec_at((struct jc_vec *)&m->checkpoints,
                                        (jc_size)i);
    return s->commit;
}

/* Restore the work tree to `commit`: reset --hard + clean (so files created
 * after the checkpoint, untracked and non-ignored, are removed too). */
/* M337b: commit and pin the tree that the caller is about to destroy.
 *
 * `why` becomes part of the ref path, so `jichi attempts` says which mechanism
 * discarded a state ("undo", "revert") without needing a field for it. The unix
 * time is in the name because these refs outlive the process: a bare counter
 * would have the next session's first undo silently overwrite this one's.
 *
 * Best-effort throughout. A restore the user asked for must not fail because
 * preservation did, which is the same rule the envelope's rollback follows. */
static void preserve_before_destroy(struct jc_snapshot_mgr *m, const char *why)
{
    char ref[220];
    char label[300];
    char sha[64];

    if (m == NULL) {
        return;
    }
    m->preserve_last[0] = '\0';   /* each restore reports only its own */
    if (!m->preserve) {
        return;
    }
    if (m->preserve_skip_once) {
        /* The caller wrote a richer record for this same state already. */
        m->preserve_skip_once = 0;
        return;
    }
    m->preserve_n++;
    jc_snprintf(ref, sizeof(ref), "refs/jichi/discarded/%s/%lu-%d", why,
                (unsigned long)jc_now_seconds(), m->preserve_n);
    jc_snprintf(label, sizeof(label),
                "discarded by %s\n\nmechanism: %s\nworkspace: %s",
                why, why, m->work_tree);
    if (jc_snapshot_preserve(m, label, ref, sha, sizeof(sha)) == JC_OK
            && sha[0] != '\0') {
        jc_snprintf(m->preserve_last, sizeof(m->preserve_last), "%s", sha);
        jc_logf(JC_LOG_INFO, "snapshots: preserved the state %s is discarding "
                "at %.12s", why, sha);
    }
}

int jc_snapshot_retain(long age_days, int rank, int keep_days, int keep_n)
{
    if (keep_days < 0 || keep_n < 0) {
        return 1;               /* either half unlimited keeps everything */
    }
    if (rank < keep_n) {
        return 1;               /* the count floor: newest K always survive */
    }
    return age_days <= (long)keep_days;
}

const char *jc_snapshot_preserved_last(const struct jc_snapshot_mgr *m)
{
    if (m == NULL || m->preserve_last[0] == '\0') {
        return NULL;
    }
    return m->preserve_last;
}

static jc_status restore_to(struct jc_snapshot_mgr *m, const char *commit)
{
    /* Every whole-tree restore -- undo, rewind, the envelope's rollback -- is
     * below this line. `reset --hard` + `clean -fd` is the destruction. */
    preserve_before_destroy(m, "undo");
    if (git_run(m, "reset", "--hard", commit, NULL, NULL, NULL) != 0) {
        jc_logf(JC_LOG_WARN, "snapshots: reset failed");
        return JC_ERR_IO;
    }
    git_run(m, "clean", "-fd", "-q", NULL, NULL, NULL);
    return JC_OK;
}

jc_status jc_snapshot_restore_commit(struct jc_snapshot_mgr *m,
                                     const char *commit)
{
    if (!jc_snapshot_available(m) || commit == NULL || commit[0] == '\0') {
        return JC_ERR_NOTFOUND;
    }
    return restore_to(m, commit);
}

jc_status jc_snapshot_restore_paths(struct jc_snapshot_mgr *m,
                                    const char *commit,
                                    const char *const *paths, int n,
                                    int *reverted, int *failed)
{
    int i;
    int ok = 0;
    int bad = 0;

    if (reverted != NULL) {
        *reverted = 0;
    }
    if (failed != NULL) {
        *failed = 0;
    }
    if (!jc_snapshot_available(m) || commit == NULL || commit[0] == '\0' ||
        paths == NULL) {
        return JC_ERR_NOTFOUND;
    }
    /* --revert-out-of-scope discards the content of every path below, and M142
     * shipped without preserving any of it. One state covers the whole set:
     * the tree here still holds all N versions, so a per-path ref would be
     * more refs for strictly less information (design decision D7). */
    if (n > 0) {
        preserve_before_destroy(m, "revert");
    }
    for (i = 0; i < n; i++) {
        const char *p = paths[i];
        struct jc_sb spec;
        int existed;
        if (p == NULL || p[0] == '\0') {
            continue;
        }
        /* Did the path exist at the baseline? (checkout can only restore
         * what the commit contains; a created file's restore is removal.) */
        jc_sb_init(&spec);
        jc_sb_append(&spec, commit);
        jc_sb_append(&spec, ":");
        jc_sb_append(&spec, p);
        existed = (git_run(m, "cat-file", "-e",
                           spec.data != NULL ? spec.data : "", NULL, NULL,
                           NULL) == 0);
        jc_sb_free(&spec);
        if (existed) {
            if (git_run(m, "checkout", commit, "--", p, NULL, NULL) == 0) {
                ok++;
            } else {
                bad++;
            }
        } else {
            char full[2048];
            jc_snprintf(full, sizeof(full), "%s/%s", m->work_tree, p);
            if (unlink(full) == 0 || !jc_file_exists(full)) {
                ok++; /* removed, or already gone: the creation is undone */
            } else {
                bad++;
            }
        }
    }
    if (reverted != NULL) {
        *reverted = ok;
    }
    if (failed != NULL) {
        *failed = bad;
    }
    return JC_OK;
}

jc_status jc_snapshot_discarded_list(struct jc_snapshot_mgr *m,
                                     struct jc_sb *out)
{
    const char *extra[6];

    if (!jc_snapshot_available(m) || out == NULL) {
        return JC_OK;
    }
    extra[0] = "for-each-ref";
    extra[1] = "--sort=-committerdate";
    extra[2] = "--format=%(objectname)\t%(refname:short)\t"
               "%(committerdate:iso-strict)\t%(subject)";
    extra[3] = "refs/jichi/discarded";
    if (git_runv(m, extra, 4, out) != 0) {
        return JC_ERR_IO;
    }
    return JC_OK;
}

jc_status jc_snapshot_worktree_add(struct jc_snapshot_mgr *m,
                                   const char *base_commit, const char *path)
{
    char *argv[10];
    int n = 0;

    if (!jc_snapshot_available(m) || base_commit == NULL || path == NULL) {
        return JC_ERR_NOTFOUND;
    }
    argv[n++] = (char *)"git";
    argv[n++] = (char *)"--git-dir";
    argv[n++] = m->git_dir;
    argv[n++] = (char *)"worktree";
    argv[n++] = (char *)"add";
    argv[n++] = (char *)"--detach";
    argv[n++] = (char *)path;
    argv[n++] = (char *)base_commit;
    argv[n] = NULL;
    return run_argv(argv, NULL) == 0 ? JC_OK : JC_ERR_IO;
}

jc_status jc_snapshot_worktree_changes(struct jc_snapshot_mgr *m,
                                       const char *wt_path,
                                       const char *base_commit,
                                       struct jc_sb *out)
{
    char *stage[6];
    char *diff[9];
    int n;

    if (!jc_snapshot_available(m) || wt_path == NULL || base_commit == NULL) {
        return JC_ERR_NOTFOUND;
    }
    /* Stage all changes so new files appear in the diff vs the base commit. */
    n = 0;
    stage[n++] = (char *)"git";
    stage[n++] = (char *)"-C";
    stage[n++] = (char *)wt_path;
    stage[n++] = (char *)"add";
    stage[n++] = (char *)"-A";
    stage[n] = NULL;
    run_argv(stage, NULL);

    n = 0;
    diff[n++] = (char *)"git";
    diff[n++] = (char *)"-C";
    diff[n++] = (char *)wt_path;
    diff[n++] = (char *)"diff";
    diff[n++] = (char *)"--name-status";
    diff[n++] = (char *)"--no-renames";
    diff[n++] = (char *)base_commit;
    diff[n] = NULL;
    return run_argv(diff, out) == 0 ? JC_OK : JC_ERR_IO;
}

jc_status jc_snapshot_worktree_remove(struct jc_snapshot_mgr *m,
                                      const char *path)
{
    char *rm[8];
    char *prune[6];
    int n;

    if (!jc_snapshot_available(m) || path == NULL) {
        return JC_ERR_NOTFOUND;
    }
    n = 0;
    rm[n++] = (char *)"git";
    rm[n++] = (char *)"--git-dir";
    rm[n++] = m->git_dir;
    rm[n++] = (char *)"worktree";
    rm[n++] = (char *)"remove";
    rm[n++] = (char *)"--force";
    rm[n++] = (char *)path;
    rm[n] = NULL;
    run_argv(rm, NULL);

    /* `git worktree remove` only exists since git 2.17; on an older git the
     * call above fails and the directory survives -- and `worktree prune`
     * only clears admin data for worktrees whose directory is already GONE,
     * so the pre-2.17 leak was silent (found by the V2f stretch row, M272).
     * The version-independent procedure is: delete the tree, then prune.
     * Guarded to the manager's own worktree area so a stray path can never
     * reach a recursive delete. */
    if (jc_file_exists(path) &&
        strstr(path, "/.jichi.d/worktrees/") != NULL) {
        char *rmrf[4];
        n = 0;
        rmrf[n++] = (char *)"rm";
        rmrf[n++] = (char *)"-rf";
        rmrf[n++] = (char *)path;
        rmrf[n] = NULL;
        run_argv(rmrf, NULL);
    }

    n = 0;
    prune[n++] = (char *)"git";
    prune[n++] = (char *)"--git-dir";
    prune[n++] = m->git_dir;
    prune[n++] = (char *)"worktree";
    prune[n++] = (char *)"prune";
    prune[n] = NULL;
    run_argv(prune, NULL);
    return JC_OK;
}

jc_status jc_snapshot_undo_changes(struct jc_snapshot_mgr *m,
                                   struct jc_sb *out)
{
    struct jc_snapshot *top;

    if (!jc_snapshot_available(m) || m->checkpoints.len == 0) {
        return JC_ERR_NOTFOUND;
    }
    top = (struct jc_snapshot *)jc_vec_at(&m->checkpoints,
                                          m->checkpoints.len - 1);
    return jc_snapshot_changed_since(m, top->commit, out);
}

void jc_snapshot_undo_note(const char *label, const char *names,
                           struct jc_sb *out)
{
    jc_size n = 0;
    jc_size listed = 0;
    jc_size i = 0;
    jc_size len;

    if (out == NULL || names == NULL) {
        return;
    }
    len = (jc_size)strlen(names);
    /* Count the reverted files first: an undo that changed nothing made
     * nothing stale, and a note about it would be noise. */
    while (i < len) {
        jc_size ls = i;
        while (i < len && names[i] != '\n') {
            i++;
        }
        if (i > ls) {
            n++;
        }
        if (i < len) {
            i++;
        }
    }
    if (n == 0) {
        return;
    }
    jc_sb_append(out, "[undo] the operator restored the workspace to ");
    if (label != NULL && label[0] != '\0') {
        jc_sb_append(out, "the checkpoint \"");
        jc_sb_append(out, label);
        jc_sb_append(out, "\"");
    } else {
        jc_sb_append(out, "the last checkpoint");
    }
    jc_sb_append_fmt(out, "; %lu file(s) reverted: ", (unsigned long)n);
    i = 0;
    while (i < len && listed < 8) {
        jc_size ls = i;
        while (i < len && names[i] != '\n') {
            i++;
        }
        if (i > ls) {
            if (listed > 0) {
                jc_sb_append(out, ", ");
            }
            jc_sb_append_n(out, names + ls, i - ls);
            listed++;
        }
        if (i < len) {
            i++;
        }
    }
    if (n > listed) {
        jc_sb_append_fmt(out, " (+%lu more)", (unsigned long)(n - listed));
    }
    jc_sb_append(out,
        ". Tool results earlier in this conversation describe the PRE-undo "
        "state of these files -- re-read them before relying on or editing "
        "them.");
}

jc_status jc_snapshot_undo(struct jc_snapshot_mgr *m, const char **label_out)
{
    struct jc_snapshot *top;
    jc_size last;
    jc_status st;

    if (label_out != NULL) {
        *label_out = NULL;
    }
    if (!jc_snapshot_available(m) || m->checkpoints.len == 0) {
        return JC_ERR_NOTFOUND;
    }

    last = m->checkpoints.len - 1;
    top = (struct jc_snapshot *)jc_vec_at(&m->checkpoints, last);
    st = restore_to(m, top->commit);
    if (st != JC_OK) {
        return st;
    }
    if (label_out != NULL) {
        *label_out = top->label;
    }
    m->checkpoints.len = last; /* pop */
    return JC_OK;
}

void jc_snapshot_refresh(struct jc_snapshot_mgr *m)
{
    struct jc_sb out;
    char *p;
    char nbuf[16];

    if (!jc_snapshot_available(m)) {
        return;
    }
    m->checkpoints.len = 0; /* clear cache, keep capacity */

    jc_snprintf(nbuf, sizeof(nbuf), "%d", JC_SNAPSHOT_MAX_LIST);
    jc_sb_init(&out);
    /* Oldest-first (so the vector's tail is newest, matching push/pop), capped
     * to the most recent JC_SNAPSHOT_MAX_LIST commits. */
    if (git_run(m, "log", "--reverse", "--format=%H%x09%s", "-n",
                nbuf, &out) != 0 || out.data == NULL) {
        jc_sb_free(&out); /* no commits yet */
        return;
    }

    p = out.data;
    while (p != NULL && *p != '\0') {
        char *line = p;
        char *nl = strchr(p, '\n');
        char *tab;
        if (nl != NULL) {
            *nl = '\0';
            p = nl + 1;
        } else {
            p = NULL;
        }
        tab = strchr(line, '\t');
        if (tab == NULL) {
            continue;
        }
        *tab = '\0';
        {
            struct jc_snapshot snap;
            jc_snprintf(snap.commit, sizeof(snap.commit), "%s", line);
            jc_snprintf(snap.label, sizeof(snap.label), "%s", tab + 1);
            jc_vec_push(&m->checkpoints, &snap);
        }
    }
    jc_sb_free(&out);
}

jc_status jc_snapshot_preview_index(struct jc_snapshot_mgr *m, int n,
                                    struct jc_sb *out, const char **label_out)
{
    struct jc_snapshot *s;
    jc_size idx;
    struct jc_sb part;

    if (label_out != NULL) {
        *label_out = NULL;
    }
    if (!jc_snapshot_available(m) || n < 1 ||
        (jc_size)n > m->checkpoints.len) {
        return JC_ERR_NOTFOUND;
    }
    idx = m->checkpoints.len - (jc_size)n;
    s = (struct jc_snapshot *)jc_vec_at(&m->checkpoints, idx);
    if (label_out != NULL) {
        *label_out = s->label;
    }

    /* Tracked changes that `reset --hard` would revert. */
    jc_sb_append(out, "Tracked changes that would be reverted:\n");
    jc_sb_init(&part);
    git_run(m, "diff", "--stat", s->commit, NULL, NULL, &part);
    if (part.data != NULL && part.data[0] != '\0') {
        jc_sb_append(out, part.data);
    } else {
        jc_sb_append(out, "  (none)\n");
    }
    jc_sb_free(&part);

    /* Untracked files that `clean -fd` would remove. */
    jc_sb_append(out, "Untracked files that would be removed:\n");
    jc_sb_init(&part);
    git_run(m, "clean", "-nd", NULL, NULL, NULL, &part);
    if (part.data != NULL && part.data[0] != '\0') {
        jc_sb_append(out, part.data);
    } else {
        jc_sb_append(out, "  (none)\n");
    }
    jc_sb_free(&part);
    return JC_OK;
}

/* M537: the one-line blast radius of restoring the n-th checkpoint.
 *
 * WHY THIS EXISTS. `undo --dry-run` has printed a full preview since M337 --
 * every tracked file a reset would revert, every untracked file a clean would
 * remove. The DESTRUCTIVE path printed the checkpoint's label and nothing else,
 * so a revert of 768 files and a revert of none looked identical on screen, and
 * the only way to learn which had happened was to go and run `git status`
 * yourself. That has the safety backwards: the reversible path was the informed
 * one and the irreversible path was the silent one.
 *
 * M337b already made this argument one step further along -- "a save the user is
 * not told about is a store nobody knows to read" -- and the same sentence holds
 * earlier in the sequence: a destruction whose SIZE you are not told is damage
 * nobody knows to look for. Found the hard way, by an agent that ran `undo` as
 * an existence probe and was told only "reverted to checkpoint 1"; see
 * docs/ANECDOTES.md #66.
 *
 * Must be called BEFORE the restore -- afterwards the evidence is gone, which is
 * the whole difficulty and the reason this is a separate call rather than a
 * return value of the restore. */
jc_status jc_snapshot_scope_index(struct jc_snapshot_mgr *m, int n,
                                  struct jc_sb *out)
{
    struct jc_snapshot *s;
    jc_size idx;
    struct jc_sb part;
    int untracked = 0;

    if (!jc_snapshot_available(m) || n < 1 ||
        (jc_size)n > m->checkpoints.len) {
        return JC_ERR_NOTFOUND;
    }
    idx = m->checkpoints.len - (jc_size)n;
    s = (struct jc_snapshot *)jc_vec_at(&m->checkpoints, idx);

    jc_sb_init(&part);
    git_run(m, "diff", "--shortstat", s->commit, NULL, NULL, &part);
    if (part.data != NULL && part.data[0] != '\0') {
        const char *q = part.data;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        jc_sb_append(out, q);
        /* --shortstat ends in a newline; the caller prints one line. */
        while (out->len > 0 && (out->data[out->len - 1] == '\n' ||
                                out->data[out->len - 1] == ' ')) {
            out->len--;
            out->data[out->len] = '\0';
        }
    } else {
        jc_sb_append(out, "no tracked file differs from that checkpoint");
    }
    jc_sb_free(&part);

    jc_sb_init(&part);
    git_run(m, "clean", "-nd", NULL, NULL, NULL, &part);
    if (part.data != NULL) {
        const char *q = part.data;
        while (*q != '\0') {
            if (*q == '\n') {
                untracked++;
            }
            q++;
        }
    }
    jc_sb_free(&part);
    if (untracked > 0) {
        char tail[64];
        jc_snprintf(tail, sizeof(tail), "; %d untracked file%s removed",
                    untracked, untracked == 1 ? "" : "s");
        jc_sb_append(out, tail);
    }
    return JC_OK;
}

jc_status jc_snapshot_diff(struct jc_snapshot_mgr *m, int n, struct jc_sb *out)
{
    struct jc_snapshot *s;
    jc_size idx;

    if (!jc_snapshot_available(m) || n < 1 ||
        (jc_size)n > m->checkpoints.len) {
        return JC_ERR_NOTFOUND;
    }
    idx = m->checkpoints.len - (jc_size)n;
    s = (struct jc_snapshot *)jc_vec_at(&m->checkpoints, idx);

    /* Stage into the shadow index so new files appear in the diff; the shadow
     * index is separate from the user's .git, so this has no user-visible
     * effect (the next checkpoint re-stages anyway). */
    git_run(m, "add", "-A", NULL, NULL, NULL, NULL);
    git_run(m, "diff", "--cached", s->commit, NULL, NULL, out);
    return JC_OK;
}

jc_status jc_snapshot_changed_since(struct jc_snapshot_mgr *m,
                                    const char *commit, struct jc_sb *out)
{
    if (!jc_snapshot_available(m) || commit == NULL || commit[0] == '\0') {
        return JC_ERR_NOTFOUND;
    }
    /* Stage into the shadow index (separate from the user's .git) so new files
     * appear, then list just the names changed since `commit`. */
    git_run(m, "add", "-A", NULL, NULL, NULL, NULL);
    git_run(m, "diff", "--cached", "--name-only", commit, NULL, out);
    return JC_OK;
}

int jc_snapshot_rewind_cut(const struct jc_snapshot_mgr *m,
                           struct jc_history *hist, int n)
{
    jc_size hlen;
    jc_size i;
    int nc;
    int nusers = 0;
    int stack_idx;
    int cut = -1;
    const char **users = NULL;
    int *hidx = NULL;
    const char **labels = NULL;
    int *out_user = NULL;
    int ci;

    if (m == NULL || hist == NULL) {
        return -1;
    }
    nc = jc_snapshot_count(m);
    if (n < 1 || n > nc) {
        return -1;
    }
    stack_idx = nc - n; /* n=1 (latest) -> highest stack index */

    hlen = jc_history_len(hist);
    users = (const char **)malloc((hlen > 0 ? hlen : 1) * sizeof(*users));
    hidx = (int *)malloc((hlen > 0 ? hlen : 1) * sizeof(*hidx));
    labels = (const char **)malloc((jc_size)nc * sizeof(*labels));
    out_user = (int *)malloc((jc_size)nc * sizeof(*out_user));
    if (users == NULL || hidx == NULL || labels == NULL || out_user == NULL) {
        free(users); free(hidx); free(labels); free(out_user);
        return -1;
    }

    for (i = 0; i < hlen; i++) {
        struct jc_message *mm = jc_history_get(hist, i);
        if (mm->role == JC_ROLE_USER) {
            users[nusers] = (mm->content != NULL) ? mm->content : "";
            hidx[nusers] = (int)i;
            nusers++;
        }
    }
    for (ci = 0; ci < nc; ci++) {
        labels[ci] = jc_snapshot_label(m, ci); /* oldest-first */
    }
    jc_rewind_match(users, nusers, labels, nc, out_user);
    if (out_user[stack_idx] >= 0) {
        cut = hidx[out_user[stack_idx]];
    }

    free(users); free(hidx); free(labels); free(out_user);
    return cut;
}

jc_status jc_snapshot_restore_index(struct jc_snapshot_mgr *m, int n,
                                    const char **label_out)
{
    struct jc_snapshot *s;
    jc_size idx;
    jc_status st;

    if (label_out != NULL) {
        *label_out = NULL;
    }
    if (!jc_snapshot_available(m) || n < 1 ||
        (jc_size)n > m->checkpoints.len) {
        return JC_ERR_NOTFOUND;
    }
    /* n=1 is the newest = the last element. */
    idx = m->checkpoints.len - (jc_size)n;
    s = (struct jc_snapshot *)jc_vec_at(&m->checkpoints, idx);
    st = restore_to(m, s->commit);
    if (st == JC_OK && label_out != NULL) {
        *label_out = s->label;
    }
    return st;
}
