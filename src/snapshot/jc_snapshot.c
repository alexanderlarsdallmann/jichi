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
#include "jc_spawn.h"   /* M832 */
#include "jc_app.h"
#include "jc_str.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "jc_message.h"
#include "jc_rewind.h"
#include "jc_sha256.h"   /* M886: the worktree fingerprint */

#include <stdio.h>
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

/* M1097 (D-09): the workspace key is the SHA-256 digest of the raw work-tree
 * path string, rendered as 64 lowercase hex chars. djb2 (h*33+c) collides on
 * names that differ by (+1,-33) in adjacent bytes -- /p/Ez and /p/FY produced
 * the same 20-digit decimal key, so two workspaces shared one shadow repo and
 * an `undo` in one restored the other's tree. A cryptographic digest cannot be
 * collided on purpose. The path is used RAW, not canonicalised: the key must be
 * stable across machines, and a realpath divergence is the belt's (not the
 * key's) job to catch (jichi.worktree, written when the shadow repo is created
 * and refused on mismatch, see jc_snapshot_manager_init). */
void jc_workspace_key(const char *work_tree, char *out, jc_size cap)
{
    if (cap == 0) {
        return;
    }
    if (work_tree == NULL) {
        out[0] = '\0';
        return;
    }
    if (cap < 65) {
        jc_snprintf(out, cap, "%s", "");
        return;
    }
    jc_sha256_hex(work_tree, strlen(work_tree), out);
}

void jc_snapshot_git_dir(const char *home, const char *work_tree,
                         char *buf, jc_size cap)
{
    char key[65];
    jc_workspace_key(work_tree, key, sizeof(key));
    jc_snprintf(buf, cap, "%s/.jichi.d/checkpoints/%s",
                home != NULL ? home : ".", key);
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

/* M2 (the 2026-10-16 plan; drive-readiness handoff of 2026-10-16, in docs/internal/, section 8.3): the git-config neutralisation, injected once at this spawn
 * point so EVERY git child the snapshot machinery runs carries it (it flows
 * through here: git_run, git_runv, and every direct argv block). jichi's git
 * children inherit the user environment, so a malicious repo -- or a drive team
 * writing /work/.git/config -- can plant core.fsmonitor (an external process),
 * alias.*.command / ext-diff, and hooks, and these `git` calls would run them.
 * Measured (throwaway repo): a planted fsmonitor fired with the inherited env;
 * `-c core.fsmonitor=` beats it, and also beats a GIT_CONFIG_* env mechanism
 * inherited from a parent (both confirmed).
 *   -c core.fsmonitor=         empty value WINS over the planted config/env, so
 *                              the fsmonitor script is never exec'd
 *   -c log.showSignature=false keep a planted gpg program out of log/show/blame
 *                              (the READ path only; it does NOT stop commit)
 *   -c commit.gpgsign=false    the COMMIT path: a planted commit.gpgsign=true +
 *                              gpg.program would make jichi's git commit
 *                              EXECUTE that program, so force signing off
 *   -c core.hooksPath=/dev/null disable the hooks path (the shadow repo never
 *                              wanted a hook to run anyway).
 * The plan's read-only --no-ext-diff --no-textconv are DIFF-ONLY flags (git
 * rejects them on status/ls-files), so they cannot be injected here; the one
 * snapshot call that renders the child's WORKTREE content (jc_snapshot_
 * worktree_diff, a path the merge back through -- the other snapshot reads hit
 * jichi's own shadow repo, not attacker content) already carries them. The
 * one non-git child that reaches this helper (the `rm -rf` worktree teardown)
 * is not gated by the `argv[0]=="git"` check below. */
static char *s_git_neutral[] = {
    (char *)"-c", (char *)"core.fsmonitor=",
    (char *)"-c", (char *)"log.showSignature=false",
    (char *)"-c", (char *)"commit.gpgsign=false",
    (char *)"-c", (char *)"core.hooksPath=/dev/null",
};
#define JC_GIT_NEUTRAL_N 8   /* two `-c KEY=VAL` pairs x 4: argv entries        */

/* Run argv (NUL-terminated). If `out` is non-NULL, child stdout is captured
 * into it; otherwise stdout is discarded. stderr is always discarded. Returns
 * the child's exit code, or -1 if it could not be run. M863: `env` (a vec of
 * "KEY=VALUE", or NULL) is set in the child -- GIT_INDEX_FILE for a pin.
 *
 * M2: a git child is REBUILT into a local nargv with the neutralisation flags
 * inserted after argv[0] -- the caller's own array is read, not written, so
 * none of the call sites needs to grow for the flags. nargv is 32 wide, which
 * bounds every git child here to 23 source args (none comes close; the widest
 * is the worktree fingerprint's ls-files with 9); a longer argv is a caller
 * bug and is refused rather than truncated. */
static int run_argv_env(char *const argv[], const struct jc_vec *env,
                        struct jc_sb *out)
{
    char *nargv[32];         /* M2 rebuild buffer; the source argv is untouched */
    int fds[2];
    pid_t pid;
    int status = 0;
    int nsrc = 0;
    int is_git = 0;
    int i;

    for (i = 0; argv[i] != NULL; i++) nsrc++;
    is_git = (argv[0] != NULL && strcmp(argv[0], "git") == 0);
    /* nargv is 32 wide. A git child needs git + JC_GIT_NEUTRAL_N(8) + the rest
     * + NUL at index nsrc + JC_GIT_NEUTRAL_N, so nsrc <= 23 keeps it < 32;
     * a non-git child needs nsrc + 1 (nsrc <= 31). Refuse a caller bug (a
     * command this long does not occur) rather than overflow nargv. */
    if (is_git ? (nsrc >= 24) : (nsrc >= 31)) return -1;
    if (is_git) {
        nargv[0] = argv[0];
        for (i = 0; i < JC_GIT_NEUTRAL_N; i++)
            nargv[1 + i] = s_git_neutral[i];
        for (i = 1; i < nsrc; i++)
            nargv[1 + JC_GIT_NEUTRAL_N + (i - 1)] = argv[i];
        nargv[nsrc + JC_GIT_NEUTRAL_N] = NULL;
    } else {
        for (i = 0; i < nsrc; i++) nargv[i] = argv[i];
        nargv[nsrc] = NULL;
    }

    if (out != NULL && jc_pipe_cloexec(fds) != 0) {
        return -1;
    }
    /* M832: through jc_spawn. M2: git is first-party (no fsmonitor/ext-diff
     * trust) but its CONFIG still comes from the user's repo, so this child
     * scrubs the secret env (the plan's scrub_env=1) AND carries the neutral
     * flags above. The explicit `env` (GIT_INDEX_FILE) is applied after the
     * scrub, so it still reaches the child. */
    {
        struct jc_spawn_req sr;
        memset(&sr, 0, sizeof sr);
        sr.argv = nargv;
        /* M844 (ACP-1): never jichi's own stdin -- over ACP it is the client's
         * JSON-RPC stream, and a command that reads stdin ate its next message
         * (a session/cancel). tests/smoke/acp_child_stdin.sh holds this. */
        sr.fd_stdin = JC_SPAWN_DEVNULL;
        sr.fd_stdout = (out != NULL) ? fds[1] : JC_SPAWN_DEVNULL;
        sr.fd_stderr = JC_SPAWN_DEVNULL;
        sr.env = env;
        if (is_git) {
            sr.scrub_env = 1;
        }
        if (jc_spawn(&sr, &pid) != JC_OK) {
            if (out != NULL) {
                close(fds[0]);
                close(fds[1]);
            }
            return -1;
        }
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

static int run_argv(char *const argv[], struct jc_sb *out)
{
    return run_argv_env(argv, NULL, out);
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
    /* The shadow repository holds a FULL CONTENT COPY of the workspace.
     * M1096/D-06: create the checkpoints/<key> base owner-only so the objects
     * git init lays down under it are unreachable on a traversable home,
     * whatever their own umask-derived modes. Tightens only the leaf jichi
     * created; an existing base is left as found. */
    if (jc_mkdir_p_private(m->git_dir) != JC_OK) {
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
        /* M1097 (D-09): the belt. Record the work tree this shadow repo belongs
         * to, at the one point where we KNOW it is ours (a fresh init). With the
         * key now SHA-256 of the raw path, two different paths no longer collide,
         * but the SAME logical project reached by a different absolute path
         * (realpath divergence, a symlinked or renamed home) still gets a fresh,
         * distinct key -- and the belt is what says "this store was pinned to a
         * different tree" if one is ever presented. Written on fresh init only. */
        git_run(m, "config", "jichi.worktree", m->work_tree, NULL, NULL, NULL);
    } else {
        /* The shadow repo already exists. Read the belt back. If it positively
         * pins a work tree OTHER than m->work_tree, this store belongs to a
         * different tree: using it would let an `undo` here restore a sibling's
         * state (the D-09 destructive half), so refuse -- available stays 0. A
         * missing belt is a pre-M1097 repo: trust it and set the belt now (a
         * one-time repair); the rejection fires only on a positive mismatch.
         * Old djb2-keyed numeric stores are orphaned by effect: their key no
         * longer matches the new derivation, so they are simply never reached. */
        struct jc_sb belt;
        char pinned[1024];
        int present = 0;

        jc_snprintf(pinned, sizeof(pinned), "%s", "");
        jc_sb_init(&belt);
        if (git_run(m, "config", "jichi.worktree", NULL, NULL, NULL, &belt) == 0
                && belt.data != NULL && belt.data[0] != '\0') {
            chomp(belt.data);
            if (belt.data[0] != '\0') {
                present = 1;
                jc_snprintf(pinned, sizeof(pinned), "%s", belt.data);
            }
        }
        jc_sb_free(&belt);
        if (present) {
            if (strcmp(pinned, m->work_tree) != 0) {
                jc_logf(JC_LOG_WARN,
                        "snapshots: checkpoint store %s is pinned to a "
                        "different work tree (%s); disabled for %s",
                        m->git_dir, pinned, m->work_tree);
                m->available = 0;
                return JC_OK;
            }
        } else {
            /* Belt absent: a pre-M1097 store. Trust it and pin it now. */
            git_run(m, "config", "jichi.worktree", m->work_tree, NULL, NULL, NULL);
        }
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
     * below this line. `reset --hard` + `clean -fd` is the destruction.
     *
     * M977 (D-02): run clean BEFORE reset. The current .gitignore may ignore
     * files that were tracked at the checkpoint target; if clean runs AFTER
     * reset, it reads the RESTORED .gitignore and deletes those files -- which
     * the dry-run preview (run before reset, under the current rules) never
     * listed. Running clean first makes it read the same rules as the preview,
     * so a file ignored now is never touched. A clean failure is logged, not
     * swallowed, and does not abort the restore. */
    preserve_before_destroy(m, "undo");
    {
        int clean_status = git_run(m, "clean", "-fd", "-q", NULL, NULL, NULL);
        if (clean_status != 0) {
            jc_logf(JC_LOG_WARN,
                    "snapshots: clean failed (status %d)", clean_status);
        }
    }
    if (git_run(m, "reset", "--hard", commit, NULL, NULL, NULL) != 0) {
        jc_logf(JC_LOG_WARN, "snapshots: reset failed");
        return JC_ERR_IO;
    }
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

/* M856 (N30): the base a kept state was made from, as its end record names it
 * (M808's `base:` line), verified to be an ancestor of the kept commit; "" when
 * the record names none (a stash entry, a state kept before M808) or the named
 * commit is not an ancestor. A kept commit's PARENT is not its base:
 * preserve_worktree commits on top of the worktree's HEAD, so when the child
 * committed as it went, the parent is the child's own last commit, and a diff
 * against it carries only what was left uncommitted. Run 0022 of the demos drive
 * resumed two kept states that way and both successors started without the
 * work, told it was applied. */
static void kept_base(struct jc_snapshot_mgr *m, const char *ref, char *base,
                      jc_size cap)
{
    const char *show[4];
    const char *anc[4];
    struct jc_sb body;
    const char *p;

    base[0] = '\0';
    if (cap < 8) {
        return;
    }
    jc_sb_init(&body);
    show[0] = "show"; show[1] = "-s"; show[2] = "--format=%B"; show[3] = ref;
    if (git_runv(m, show, 4, &body) == 0 && body.data != NULL &&
        (p = strstr(body.data, "\nbase:")) != NULL) {
        jc_size k = 0;
        p += 6;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (k + 1 < cap && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) {
            base[k++] = *p++;
        }
        base[k] = '\0';
        if (k < 7) {
            base[0] = '\0';
        }
    }
    jc_sb_free(&body);
    if (base[0] != '\0') {
        anc[0] = "merge-base"; anc[1] = "--is-ancestor"; anc[2] = base; anc[3] = ref;
        if (git_runv(m, anc, 4, NULL) != 0) {
            base[0] = '\0';
        }
    }
}

jc_status jc_snapshot_kept_changes(struct jc_snapshot_mgr *m, const char *ref,
                                   struct jc_sb *out)
{
    const char *extra[5];
    char base[72];

    if (!jc_snapshot_available(m) || ref == NULL || out == NULL) {
        return JC_ERR_NOTFOUND;
    }
    kept_base(m, ref, base, sizeof base);
    if (base[0] != '\0') {
        extra[0] = "diff"; extra[1] = "--name-only"; extra[2] = base; extra[3] = ref;
        return git_runv(m, extra, 4, out) == 0 ? JC_OK : JC_ERR_IO;
    }
    extra[0] = "show"; extra[1] = "--name-only"; extra[2] = "--format="; extra[3] = ref;
    return git_runv(m, extra, 4, out) == 0 ? JC_OK : JC_ERR_IO;
}

jc_status jc_snapshot_discarded_show(struct jc_snapshot_mgr *m, const char *ref,
                                     struct jc_sb *out)
{
    const char *extra[5];
    char base[72];

    if (!jc_snapshot_available(m) || ref == NULL || out == NULL) {
        return JC_ERR_NOTFOUND;
    }
    /* M856 (N30): the end record, then what the kept state changed against the
     * BASE it started from -- the child's own commits and what it left
     * uncommitted. M815 showed the diffstat against the kept commit's parent, on
     * the belief that the parent IS the base; for a child that committed, it is
     * the child's last commit, and the stat showed only the residue (often
     * nothing). A state with no recorded base keeps M815's form. */
    kept_base(m, ref, base, sizeof base);
    if (base[0] != '\0') {
        extra[0] = "show"; extra[1] = "-s"; extra[2] = "--format=%B"; extra[3] = ref;
        if (git_runv(m, extra, 4, out) != 0) {
            return JC_ERR_IO;
        }
        extra[0] = "diff"; extra[1] = "--stat"; extra[2] = base; extra[3] = ref;
        if (git_runv(m, extra, 4, out) != 0) {
            return JC_ERR_IO;
        }
        return JC_OK;
    }
    extra[0] = "show";
    extra[1] = "--stat";
    extra[2] = "--format=%B";
    extra[3] = ref;
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

jc_status jc_snapshot_resume_apply(struct jc_snapshot_mgr *m, const char *wt_path,
                                   const char *ref, struct jc_sb *conflicts)
{
    char *verify[8];
    char *cp[7];
    char *ab[6];
    char rev[300];
    char base[72];
    char range[300];
    int n;

    if (!jc_snapshot_available(m) || wt_path == NULL || ref == NULL ||
        ref[0] == '\0') {
        return JC_ERR_NOTFOUND;
    }
    /* The ref must resolve to a real commit in the object store the worktree
     * shares -- a bad ref is a clean NOTFOUND, not a cherry-pick error message. */
    jc_snprintf(rev, sizeof rev, "%s^{commit}", ref);
    n = 0;
    verify[n++] = (char *)"git"; verify[n++] = (char *)"-C";
    verify[n++] = (char *)wt_path; verify[n++] = (char *)"rev-parse";
    verify[n++] = (char *)"--verify"; verify[n++] = (char *)"-q";
    verify[n++] = rev; verify[n] = NULL;
    if (run_argv(verify, NULL) != 0) {
        return JC_ERR_NOTFOUND;
    }
    /* 3-way apply the kept state onto the worktree's current HEAD. M856 (N30):
     * replay the whole range from the base the predecessor started from -- its own
     * commits, then the kept commit with whatever it left uncommitted. M816 picked
     * the kept commit alone, i.e. its diff against its PARENT, believing the
     * parent to be that base; for a child that committed as it went, the parent
     * is its last commit, the pick carried only the residue, an empty residue
     * picked cleanly, and the successor started from the bare base. A state with
     * no recorded base keeps the single pick. A clean apply or an auto-resolved
     * merge exits 0; an unresolved conflict does not. --no-commit leaves the
     * result staged, so the pool's flow proceeds as for a from-scratch task. */
    kept_base(m, ref, base, sizeof base);
    if (base[0] != '\0') {
        jc_snprintf(range, sizeof range, "%s..%s", base, ref);
    } else {
        jc_snprintf(range, sizeof range, "%s", ref);
    }
    n = 0;
    cp[n++] = (char *)"git"; cp[n++] = (char *)"-C"; cp[n++] = (char *)wt_path;
    cp[n++] = (char *)"cherry-pick"; cp[n++] = (char *)"--no-commit";
    cp[n++] = range; cp[n] = NULL;
    if (run_argv(cp, NULL) == 0) {
        return JC_OK;
    }
    /* Conflict (or an empty/redundant pick): name the unmerged paths, then abort
     * so the worktree is restored to the clean base -- the caller will not launch
     * this task, and teardown removes the (base-identical) tree without preserving
     * a bogus state. */
    if (conflicts != NULL) {
        char *diff[8];
        int k = 0;
        diff[k++] = (char *)"git"; diff[k++] = (char *)"-C";
        diff[k++] = (char *)wt_path; diff[k++] = (char *)"diff";
        diff[k++] = (char *)"--name-only"; diff[k++] = (char *)"--diff-filter=U";
        diff[k] = NULL;
        run_argv(diff, conflicts);
    }
    n = 0;
    ab[n++] = (char *)"git"; ab[n++] = (char *)"-C"; ab[n++] = (char *)wt_path;
    ab[n++] = (char *)"cherry-pick"; ab[n++] = (char *)"--abort"; ab[n] = NULL;
    run_argv(ab, NULL);
    return JC_ERR_INVALID;
}

/* M927 (R04-F1): shared stage + diff builder. `z` selects the output form:
 * the line form (`<status>\t<path>\n`, C-quoted) is a stable telemetry wire
 * value (M415 files_changed); the NUL form (`-z`, `<status>\0<path>\0`,
 * never quoted) is the only form a merge can parse without losing files whose
 * names carry a newline, tab, quote or backslash -- git still C-quotes those
 * with quotepath=false (measured), so the flag is not a fix. */
static jc_status worktree_changes_impl(struct jc_snapshot_mgr *m,
                                       const char *wt_path,
                                       const char *base_commit,
                                       struct jc_sb *out, int z)
{
    char *stage[6];
    char *diff[10];
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
    if (z) {
        diff[n++] = (char *)"-z";
    }
    diff[n++] = (char *)base_commit;
    diff[n] = NULL;
    return run_argv(diff, out) == 0 ? JC_OK : JC_ERR_IO;
}

jc_status jc_snapshot_worktree_changes(struct jc_snapshot_mgr *m,
                                       const char *wt_path,
                                       const char *base_commit,
                                       struct jc_sb *out)
{
    return worktree_changes_impl(m, wt_path, base_commit, out, 0);
}

jc_status jc_snapshot_worktree_changes_z(struct jc_snapshot_mgr *m,
                                         const char *wt_path,
                                         const char *base_commit,
                                         struct jc_sb *out)
{
    return worktree_changes_impl(m, wt_path, base_commit, out, 1);
}

/* M803: a linked worktree shares this manager's object store but has its own
 * (detached) HEAD, so its content is committed with `git -C <wt>` and the commit
 * is then pinned in the shadow repo. The commit becomes a child of the worktree's
 * HEAD, so a child's own commits are captured too. Returns JC_ERR_IO on any step
 * failing, on which the caller keeps the tree. */
const char *const JC_WT_MERGED = "merged";

static jc_status preserve_worktree(struct jc_snapshot_mgr *m, const char *path,
                                   const char *why, const char *record)
{
    char *add[6];
    char *ci[9];
    char *rp[6];
    const char *ref_extra[4];
    struct jc_sb sha;
    char ref[256];
    char *p;
    int n;

    n = 0;
    add[n++] = (char *)"git"; add[n++] = (char *)"-C"; add[n++] = (char *)path;
    add[n++] = (char *)"add"; add[n++] = (char *)"-A"; add[n] = NULL;
    run_argv(add, NULL);

    n = 0;
    ci[n++] = (char *)"git"; ci[n++] = (char *)"-C"; ci[n++] = (char *)path;
    ci[n++] = (char *)"commit"; ci[n++] = (char *)"-q";
    ci[n++] = (char *)"--allow-empty"; ci[n++] = (char *)"-m";
    ci[n++] = (char *)((record != NULL && record[0] != '\0') ? record : why);
    ci[n] = NULL;
    if (run_argv(ci, NULL) != 0) {
        return JC_ERR_IO;
    }

    jc_sb_init(&sha);
    n = 0;
    rp[n++] = (char *)"git"; rp[n++] = (char *)"-C"; rp[n++] = (char *)path;
    rp[n++] = (char *)"rev-parse"; rp[n++] = (char *)"HEAD"; rp[n] = NULL;
    if (run_argv(rp, &sha) != 0 || sha.data == NULL) {
        jc_sb_free(&sha);
        return JC_ERR_IO;
    }
    p = sha.data;
    while (*p != '\0' && *p != '\n' && *p != '\r') {
        p++;
    }
    *p = '\0';

    m->preserve_n++;
    jc_snprintf(ref, sizeof(ref), "refs/jichi/discarded/%s/%lu-%d", why,
                (unsigned long)jc_now_seconds(), m->preserve_n);
    ref_extra[0] = "update-ref";
    ref_extra[1] = ref;
    ref_extra[2] = sha.data;
    if (git_runv(m, ref_extra, 3, NULL) != 0) {
        jc_sb_free(&sha);
        return JC_ERR_IO;
    }
    jc_snprintf(m->preserve_last, sizeof(m->preserve_last), "%s", sha.data);
    jc_snprintf(m->preserve_last_ref, sizeof(m->preserve_last_ref), "%s", ref);
    jc_logf(JC_LOG_INFO, "snapshots: preserved a %s worktree at %.12s (%s)",
            why, sha.data, ref);
    jc_sb_free(&sha);
    return JC_OK;
}

/* M886 (the demos drive's N50): see jc_snapshot.h. Each item is fed with its NUL,
 * so the boundaries between paths and digests cannot run together. */
#define JC_FP_FILE_MAX (8L * 1024L * 1024L)

static void fp_feed(struct jc_sha256 *c, const char *s)
{
    jc_sha256_update(c, s, (jc_size)strlen(s) + 1);
}

static void fp_hex(const unsigned char *dg, char *out)
{
    static const char hexd[] = "0123456789abcdef";
    int i;
    for (i = 0; i < JC_SHA256_DIGEST_LEN; i++) {
        out[2 * i] = hexd[(dg[i] >> 4) & 0xF];
        out[2 * i + 1] = hexd[dg[i] & 0xF];
    }
    out[2 * JC_SHA256_DIGEST_LEN] = '\0';
}

jc_status jc_snapshot_worktree_fingerprint(const char *root, char *out,
                                           jc_size cap)
{
    char *argv[10];
    struct jc_sb sb;
    struct jc_sha256 c;
    unsigned char dg[JC_SHA256_DIGEST_LEN];
    jc_size i;
    int n = 0;

    if (out == NULL || cap < 2 * JC_SHA256_DIGEST_LEN + 1) {
        return JC_ERR_INVALID;
    }
    out[0] = '\0';
    if (root == NULL || root[0] == '\0') {
        return JC_ERR_INVALID;
    }
    argv[n++] = (char *)"git"; argv[n++] = (char *)"-C"; argv[n++] = (char *)root;
    argv[n++] = (char *)"ls-files"; argv[n++] = (char *)"-z"; argv[n++] = (char *)"-m";
    argv[n++] = (char *)"-d"; argv[n++] = (char *)"-o";
    argv[n++] = (char *)"--exclude-standard"; argv[n] = NULL;
    jc_sb_init(&sb);
    if (run_argv(argv, &sb) != 0) {
        jc_sb_free(&sb);
        return JC_ERR_IO;
    }
    jc_sha256_init(&c);
    i = 0;
    while (sb.data != NULL && i < sb.len) {
        const char *rel = sb.data + i;
        jc_size rl = (jc_size)strlen(rel);
        char full[2048];
        char item[160];
        struct stat st;

        i += rl + 1;
        if (rl == 0) {
            continue;
        }
        fp_feed(&c, rel);
        jc_snprintf(full, sizeof full, "%s/%s", root, rel);
        if (stat(full, &st) != 0) {
            fp_feed(&c, "D");
            continue;
        }
        if (!S_ISREG(st.st_mode) || (long)st.st_size > JC_FP_FILE_MAX) {
            jc_snprintf(item, sizeof item, "S %ld %ld", (long)st.st_size,
                        (long)st.st_mtime);
            fp_feed(&c, item);
            continue;
        }
        {
            FILE *f = fopen(full, "rb");
            struct jc_sha256 fc;
            unsigned char fd[JC_SHA256_DIGEST_LEN];
            char buf[16384];
            size_t got;
            if (f == NULL) {
                fp_feed(&c, "U");
                continue;
            }
            jc_sha256_init(&fc);
            while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
                jc_sha256_update(&fc, buf, (jc_size)got);
            }
            fclose(f);
            jc_sha256_final(&fc, fd);
            item[0] = 'F';
            item[1] = ' ';
            fp_hex(fd, item + 2);
            fp_feed(&c, item);
        }
    }
    jc_sb_free(&sb);
    /* M893: and HEAD. A clean tree before and a clean tree after a commit list
     * the same (no) files, so without it a commit was invisible -- and M893 asks
     * "is this still the tree the run started from?" */
    {
        char *hv[8];
        struct jc_sb hs;
        char head[80];
        jc_size k = 0;
        int hn = 0;
        hv[hn++] = (char *)"git"; hv[hn++] = (char *)"-C"; hv[hn++] = (char *)root;
        hv[hn++] = (char *)"rev-parse"; hv[hn++] = (char *)"--verify";
        hv[hn++] = (char *)"-q"; hv[hn++] = (char *)"HEAD"; hv[hn] = NULL;
        jc_sb_init(&hs);
        head[0] = 'H';
        head[1] = ' ';
        head[2] = '-';
        head[3] = '\0';
        if (run_argv(hv, &hs) == 0 && hs.data != NULL) {
            while (k < hs.len && k + 3 < sizeof head && hs.data[k] != '\n' &&
                   hs.data[k] != '\0') {
                head[2 + k] = hs.data[k];
                k++;
            }
            if (k > 0) {
                head[2 + k] = '\0';
            }
        }
        jc_sb_free(&hs);
        fp_feed(&c, head);
    }
    jc_sha256_final(&c, dg);
    fp_hex(dg, out);
    return JC_OK;
}

/* M863: the first line of a command's captured output, into `buf`. */
static void first_word_line(const struct jc_sb *sb, char *buf, jc_size cap)
{
    jc_size i = 0;
    buf[0] = '\0';
    if (sb->data == NULL) {
        return;
    }
    while (i + 1 < cap && sb->data[i] != '\0' && sb->data[i] != '\n' &&
           sb->data[i] != '\r') {
        buf[i] = sb->data[i];
        i++;
    }
    buf[i] = '\0';
}

jc_status jc_snapshot_worktree_pin(struct jc_snapshot_mgr *m,
                                   const char *wt_path, const char *index_path,
                                   const char *base, const char *why,
                                   const char *record, char *last,
                                   jc_size last_cap, char *ref_out,
                                   jc_size ref_cap)
{
    char *argv[12];
    const char *ref_extra[4];
    char envs[1300];
    char *envp;
    struct jc_vec env;
    struct jc_sb sb;
    char head[64];
    char htree[64];
    char tree[64];
    char commit[64];
    char state[140];
    char ref[256];
    jc_status st = JC_ERR_IO;
    int n;

    if (ref_out != NULL && ref_cap > 0) {
        ref_out[0] = '\0';
    }
    if (!jc_snapshot_available(m) || wt_path == NULL || index_path == NULL ||
        why == NULL) {
        return JC_ERR_NOTFOUND;
    }
    jc_snprintf(envs, sizeof envs, "GIT_INDEX_FILE=%s", index_path);
    envp = envs;
    jc_vec_init(&env, sizeof(char *));
    jc_vec_push(&env, &envp);
    remove(index_path);
    jc_sb_init(&sb);

    /* HEAD, and HEAD's tree */
    n = 0;
    argv[n++] = (char *)"git"; argv[n++] = (char *)"-C"; argv[n++] = (char *)wt_path;
    argv[n++] = (char *)"rev-parse"; argv[n++] = (char *)"HEAD"; argv[n] = NULL;
    if (run_argv(argv, &sb) != 0) {
        goto done;
    }
    first_word_line(&sb, head, sizeof head);
    jc_sb_free(&sb);
    jc_sb_init(&sb);
    argv[4] = (char *)"HEAD^{tree}";
    if (run_argv(argv, &sb) != 0) {
        goto done;
    }
    first_word_line(&sb, htree, sizeof htree);
    jc_sb_free(&sb);
    jc_sb_init(&sb);

    /* the files -- tracked, and untracked but not ignored -- in a TEMPORARY
     * index, so the worktree's own index, HEAD and files stay as they are: the
     * command that follows must do exactly what it would have done */
    n = 0;
    argv[n++] = (char *)"git"; argv[n++] = (char *)"-C"; argv[n++] = (char *)wt_path;
    argv[n++] = (char *)"read-tree"; argv[n++] = (char *)"HEAD"; argv[n] = NULL;
    if (run_argv_env(argv, &env, NULL) != 0) {
        goto done;
    }
    argv[3] = (char *)"add"; argv[4] = (char *)"-A";
    if (run_argv_env(argv, &env, NULL) != 0) {
        goto done;
    }
    argv[3] = (char *)"write-tree"; argv[4] = NULL;
    if (run_argv_env(argv, &env, &sb) != 0) {
        goto done;
    }
    first_word_line(&sb, tree, sizeof tree);
    jc_sb_free(&sb);
    jc_sb_init(&sb);
    if (head[0] == '\0' || htree[0] == '\0' || tree[0] == '\0') {
        goto done;
    }

    /* nothing to lose: the files are HEAD's, and HEAD is where the task began */
    if (strcmp(tree, htree) == 0 &&
        (base == NULL || base[0] == '\0' || strcmp(head, base) == 0)) {
        st = JC_ERR_NOTFOUND;
        goto done;
    }
    /* the same state as the last pin */
    jc_snprintf(state, sizeof state, "%s %s", head, tree);
    if (last != NULL && strcmp(last, state) == 0) {
        st = JC_ERR_NOTFOUND;
        goto done;
    }

    /* the commit: those files, on HEAD -- so HEAD's own commits are kept too */
    n = 0;
    argv[n++] = (char *)"git"; argv[n++] = (char *)"-C"; argv[n++] = (char *)wt_path;
    argv[n++] = (char *)"commit-tree"; argv[n++] = tree; argv[n++] = (char *)"-p";
    argv[n++] = head; argv[n++] = (char *)"-m";
    argv[n++] = (char *)((record != NULL && record[0] != '\0') ? record : why);
    argv[n] = NULL;
    if (run_argv(argv, &sb) != 0) {
        goto done;
    }
    first_word_line(&sb, commit, sizeof commit);
    if (commit[0] == '\0') {
        goto done;
    }
    /* the pid as well: concurrent children of one batch each count in their
     * own copy of preserve_n, and two pins in one second would share a name --
     * the second update-ref overwriting the first. `jichi gc` ages a ref by its
     * commit's date, not its name. */
    m->preserve_n++;
    jc_snprintf(ref, sizeof(ref), "refs/jichi/discarded/%s/%lu-%ld-%d", why,
                (unsigned long)jc_now_seconds(), (long)getpid(), m->preserve_n);
    ref_extra[0] = "update-ref";
    ref_extra[1] = ref;
    ref_extra[2] = commit;
    if (git_runv(m, ref_extra, 3, NULL) != 0) {
        goto done;
    }
    if (ref_out != NULL && ref_cap > 0) {
        jc_snprintf(ref_out, ref_cap, "%s", ref);
    }
    if (last != NULL && last_cap > 0) {
        jc_snprintf(last, last_cap, "%s", state);
    }
    jc_logf(JC_LOG_INFO, "snapshots: kept a live worktree at %.12s (%s)",
            commit, ref);
    st = JC_OK;
done:
    remove(index_path);
    jc_sb_free(&sb);
    jc_vec_free(&env);
    return st;
}

jc_status jc_snapshot_worktree_diff(struct jc_snapshot_mgr *m,
                                    const char *wt_path, const char *base,
                                    const char *path, struct jc_sb *out)
{
    char *argv[12];
    int n = 0;

    if (!jc_snapshot_available(m) || wt_path == NULL || base == NULL ||
        path == NULL || out == NULL) {
        return JC_ERR_NOTFOUND;
    }
    argv[n++] = (char *)"git"; argv[n++] = (char *)"-C"; argv[n++] = (char *)wt_path;
    argv[n++] = (char *)"diff"; argv[n++] = (char *)"--no-color";
    argv[n++] = (char *)"--no-ext-diff"; argv[n++] = (char *)"--unified=0";
    argv[n++] = (char *)base; argv[n++] = (char *)"--"; argv[n++] = (char *)path;
    argv[n] = NULL;
    return (run_argv(argv, out) == 0) ? JC_OK : JC_ERR_IO;
}

int jc_snapshot_stash_keep(struct jc_snapshot_mgr *m, const char *tag,
                           struct jc_sb *refs)
{
    const char *ls[4];
    const char *del[3];
    struct jc_sb out;
    char *p;
    int n = 0;
    int failed = 0;

    if (!jc_snapshot_available(m) || !m->preserve || tag == NULL) {
        return 0;
    }
    jc_sb_init(&out);
    /* `log -g`, not `stash list`: it needs no work tree, and every git this
     * tree supports has it. Newest first; no ref, no output (and an error). */
    ls[0] = "log"; ls[1] = "-g"; ls[2] = "--format=%H"; ls[3] = "refs/stash";
    if (git_runv(m, ls, 4, &out) != 0 || out.data == NULL) {
        jc_sb_free(&out);
        return 0;
    }
    p = out.data;
    while (*p != '\0') {
        char *eol = strchr(p, '\n');
        jc_size len = (eol != NULL) ? (jc_size)(eol - p) : (jc_size)strlen(p);
        if (len >= 40 && len < 72) {
            char sha[72];
            char ref[256];
            const char *ur[3];
            memcpy(sha, p, len);
            sha[len] = '\0';
            m->preserve_n++;
            jc_snprintf(ref, sizeof ref, "refs/jichi/discarded/%s/%lu-%d", tag,
                        (unsigned long)jc_now_seconds(), m->preserve_n);
            ur[0] = "update-ref"; ur[1] = ref; ur[2] = sha;
            if (git_runv(m, ur, 3, NULL) != 0) {
                failed = 1;
            } else {
                n++;
                if (refs != NULL) {
                    jc_sb_append(refs, ref);
                    jc_sb_append(refs, "\n");
                }
            }
        }
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }
    jc_sb_free(&out);
    if (n > 0 && !failed) {
        /* the same as `git stash clear`, without needing a work tree */
        del[0] = "update-ref"; del[1] = "-d"; del[2] = "refs/stash";
        (void)git_runv(m, del, 3, NULL);
    }
    return n;
}

jc_status jc_snapshot_worktree_remove(struct jc_snapshot_mgr *m,
                                      const char *path,
                                      const char *why, const char *record)
{
    char *rm[8];
    char *prune[6];
    int n;

    if (!jc_snapshot_available(m) || path == NULL) {
        return JC_ERR_NOTFOUND;
    }
    /* M803: preserve the child's work before removing, unless it was merged in
     * full or preservation is off. If preserving FAILS, keep the tree -- salvage
     * (a later step) or the operator recovers it; housekeeping never destroys the
     * only copy. */
    if (why != JC_WT_MERGED && m->preserve && jc_file_exists(path)) {
        if (preserve_worktree(m, path, why, record) != JC_OK) {
            jc_logf(JC_LOG_WARN, "snapshots: could not preserve the %s worktree "
                    "at %s -- keeping it rather than destroying the only copy",
                    why != NULL ? why : "?", path);
            return JC_ERR_IO;
        }
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
                           jc_size names_len, struct jc_sb *out)
{
    jc_size n = 0;
    jc_size listed = 0;
    jc_size i = 0;
    jc_size len;

    if (out == NULL || names == NULL) {
        return;
    }
    /* M1095 (D-03): `names` is the NUL-separated raw form (jc_snapshot_undo_changes),
     * with a trailing NUL after the last name. strlen is wrong for it -- it would
     * see only the first segment -- so the caller passes the buffer length
     * explicitly. names_len 0 is an empty list. */
    len = names_len;
    /* Count the reverted files first: an undo that changed nothing made
     * nothing stale, and a note about it would be noise. */
    while (i < len) {
        jc_size ls = i;
        while (i < len && names[i] != '\0') {
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
        while (i < len && names[i] != '\0') {
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
     * appear, then list just the names changed since `commit`. M1095 (D-03):
     * with -z the output is NUL-separated raw paths, one per segment, with a
     * trailing NUL after the last name (an empty list is an empty buffer) --
     * git never quotes in this form. The line form C-quotes non-ASCII names
     * (café.txt -> "caf\303\251.txt"), which the edit-scope glob matched as a
     * violation and the per-path revert unlinked as a name that is not on disk.
     * Every consumer splits on NUL. */
    git_run(m, "add", "-A", NULL, NULL, NULL, NULL);
    git_run(m, "diff", "--cached", "--name-only", commit, "-z", out);
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
