/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_parallel.c - the spawn_parallel tool.
 *
 * Runs several subagents concurrently in a fork-based worker pool sized to the
 * CPU. Read-only tasks run against the live workspace; "write" tasks each run
 * in an isolated git worktree (from the snapshot shadow repo) and the parent
 * merges their file changes back file-level, first-wins, conflicts reported.
 * Each child returns {answer,tokens,error} as JSON over a pipe; the parent
 * gathers with select (honouring the abort flag) and reaps every child. The
 * pure cores (sizing, change parsing, the merge claim) live in jc_parallel.c.
 * See docs/PARALLEL.md. */

#include "jc_delegreport.h"
#include "tool_util.h"
#include "jc_app.h"
#include "jc_eventlog.h"   /* M800: jc_eventlog_set_task (child attribution) */
#include "jc_control.h"    /* M801: jc_control_poll_during_call in the pool loop */
#include "jc_agent.h"
#include "jc_provider.h"
#include "jc_sysmsg.h"
#include "jc_message.h"
#include "jc_snapshot.h"
#include "jc_envelope.h"
#include "jc_parallel.h"
#include "jc_workerpool.h"
#include "jc_index.h"
#include "jc_path.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_platform.h"
#include "jc_proc.h"   /* jc_pipe_cloexec (M472) */
#include "jc_uuid.h"   /* M807: a run id for child transcripts when none is set */
#include "jc_toolloop.h" /* M808: jc_fail_class_name for the end record's last call */
#include "jc_tool.h"  /* M816: jc_tool_registry_find for effect classification */
#include "jc_fault.h"   /* M927 (R04-F1): the failed-copy arm */
#include "jc_landlock.h"  /* M831 (O9): Landlock write fence on a write child's shell */
#include "jc_toolout.h"   /* M857 (N41): the child's claim-ticket store, granted */
#include "jc_bg.h"        /* M961 (F1): app->bg->spool_dir */
#include "jc_log.h"       /* M831: WARN when the shell fence cannot be applied */
#include "jc_utf8.h"      /* M889 (N51): cuts at a character, not a byte */
#include "jc_scopededit.h"  /* D6 (M975): the scoped edit check */
#include "jc_dispatch.h"  /* M1006: the dirty-tree dispatch probe */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>   /* M852: stat/S_ISDIR for the fence checks */
#include "jc_select.h"  /* M834: <sys/select.h> where it exists */
#include <sys/time.h> /* struct timeval: MiNTLib's <sys/select.h> only forward-declares it (M723) */
#include <fcntl.h>    /* M811: FD_CLOEXEC on the batch's base lock fd */
#include "jc_fcntl.h" /* M1099 (D-12): JC_O_BINARY on the merge's O_NONBLOCK read */
#include <dirent.h>   /* M812: scan ~/.jichi.d/worktrees/par-* for orphan bases */
#include <time.h>     /* M861: time() for the run's budget at the report */

#define JC_PAR_MAX_TASKS  32
#define JC_PAR_CEILING     8
#define JC_PAR_ANSWER_CAP  16384

/* M888 (the demos drive's N49 (b); the operator's decision of 2026-10-03, "Name all,
 * hold large ones"): a write child's NEW directories -- for each path it added, the
 * highest ancestor the live tree does not have -- are named in the batch result with
 * their file counts and sizes, and one over these bounds is held out of the merge,
 * the task kept. Run 0102 merged a 15 MB, 189-file `oracle_scratch/` a worker made in
 * its worktree; run 0085 merged a 963-file demo copy. */
#define JC_PAR_NEWDIR_MAX        16
#define JC_PAR_NEWDIR_HOLD_FILES 100
#define JC_PAR_NEWDIR_HOLD_BYTES (1024L * 1024L)

struct newdir {
    char root[256];   /* relative to the tree, no trailing slash */
    long files;
    long bytes;
    int  held;
};

/* The new-directory root of `rel` -- its highest ancestor directory missing from the
 * live tree `live` -- into `out`, or "" when every ancestor is there. A cut at a '/'
 * cannot split a UTF-8 sequence. */
static void newdir_root(const char *live, const char *rel, char *out, jc_size cap)
{
    char buf[JC_PATH_MAX];
    jc_size i;
    jc_size n = (jc_size)strlen(rel);

    out[0] = '\0';
    for (i = 0; i < n; i++) {
        if (rel[i] == '/') {
            jc_snprintf(buf, sizeof buf, "%s/%.*s", live, (int)i, rel);
            if (!jc_is_dir(buf)) {
                if (i < cap) {
                    jc_snprintf(out, cap, "%.*s", (int)i, rel);
                }
                return;
            }
        }
    }
}

/* Group a task's added paths by their new-directory root; count and size each
 * group from the child's worktree; mark the ones over the bounds held. Returns the
 * number of groups; `*more` is set when there were more than `cap`. */
static int newdirs_scan(const char *live, const char *wt,
                        const struct jc_vec *changes, struct newdir *nd, int cap,
                        int *more)
{
    int n = 0;
    int j;
    jc_size k;

    *more = 0;
    for (k = 0; k < changes->len; k++) {
        const struct jc_change *c =
            (const struct jc_change *)jc_vec_at((struct jc_vec *)changes, k);
        char r[256];
        char full[JC_PATH_MAX];
        struct stat st;

        if (c->status != 'A' || c->path == NULL) {
            continue;
        }
        newdir_root(live, c->path, r, sizeof r);
        if (r[0] == '\0') {
            continue;
        }
        for (j = 0; j < n && strcmp(nd[j].root, r) != 0; j++) {
            ;
        }
        if (j == n) {
            if (n == cap) {
                *more = 1;
                continue;
            }
            memset(&nd[n], 0, sizeof nd[n]);
            jc_snprintf(nd[n].root, sizeof nd[n].root, "%s", r);
            n++;
        }
        nd[j].files++;
        jc_snprintf(full, sizeof full, "%s/%s", wt, c->path);
        if (stat(full, &st) == 0) {
            nd[j].bytes += (long)st.st_size;
        }
    }
    for (j = 0; j < n; j++) {
        nd[j].held = (nd[j].files > JC_PAR_NEWDIR_HOLD_FILES ||
                      nd[j].bytes > JC_PAR_NEWDIR_HOLD_BYTES) ? 1 : 0;
    }
    return n;
}

/* Is `path` under a held new directory? */
static int newdir_held_path(const struct newdir *nd, int n, const char *path)
{
    int j;
    for (j = 0; j < n; j++) {
        jc_size l = (jc_size)strlen(nd[j].root);
        if (nd[j].held && strncmp(path, nd[j].root, l) == 0 && path[l] == '/') {
            return 1;
        }
    }
    return 0;
}

/* "oracle_scratch/ (189 files, 15.0 MB, HELD); tmp/ (1 file, 12 bytes)" */
static void newdirs_render(const struct newdir *nd, int n, int more, char *out,
                           jc_size cap)
{
    int j;
    out[0] = '\0';
    for (j = 0; j < n; j++) {
        char sz[40];
        jc_size used = (jc_size)strlen(out);
        if (nd[j].bytes >= 1024L * 1024L) {
            jc_snprintf(sz, sizeof sz, "%.1f MB",
                        (double)nd[j].bytes / (1024.0 * 1024.0));
        } else if (nd[j].bytes >= 1024L) {
            jc_snprintf(sz, sizeof sz, "%.1f KB", (double)nd[j].bytes / 1024.0);
        } else {
            jc_snprintf(sz, sizeof sz, "%ld byte%s", nd[j].bytes,
                        nd[j].bytes == 1 ? "" : "s");
        }
        jc_snprintf(out + used, cap - used, "%s%s/ (%ld file%s, %s%s)",
                    j > 0 ? "; " : "", nd[j].root, nd[j].files,
                    nd[j].files == 1 ? "" : "s", sz, nd[j].held ? ", HELD" : "");
    }
    if (more) {
        jc_size used = (jc_size)strlen(out);
        jc_snprintf(out + used, cap - used, "; and more");
    }
}

/* def-332 (M1044): a worker's root report, by name -- the drive's report
 * convention, a root REPORT.md or REPORT-<label>.md with NO '/' anywhere. A
 * report BELOW the root (reports/x.md) is ordinary code and merges as such.
 * REPORT.md and REPORT-*.md only; REPORT.md.txt or REPORTS.md are not.
 * (The name is matched against snapshot_lint's root rule, check 13, the
 * stray-root scan: it ALLOWS bare REPORT.md at the root (M1005) but would flag
 * a per-task REPORT-<label>.md as stray; check 16 is the unpublished-names
 * scan and never mentions reports.) Both forms are kept out of the merge here
 * because a report is evidence, not code. */
static int jc_par_root_report(const char *path)
{
    jc_size n, p;
    int pre;

    if (path == NULL || path[0] == '\0' ||
        strchr(path, '/') != NULL) {
        return 0;
    }
    n = (jc_size)strlen(path);
    if (strcmp(path, "REPORT.md") == 0) {
        return 1;
    }
    p = n - 3;   /* room for ".md" */
    if (n <= 7 || p < 7 || path[n - 3] != '.' || path[n - 2] != 'm' ||
        path[n - 1] != 'd') {
        return 0;
    }
    pre = strncmp(path, "REPORT-", 7) == 0;
    return pre ? (int)p : 0;
}

/* def-332 (M1044): the child's root report, stored redacted with the run's
 * records beside its own transcript (t<N>.jsonl in the same dir). `path` is
 * the child's copy (<wt>/<report>), `store` receives the record path, written
 * when the return is 1. 0 on any read, mkdir or write failure: the caller
 * then names the report in the result and says the store FAILED (a lost
 * report is the defect this fixes, so a failed store must not be silent).
 * The report body is NUL-terminated and capped (jc_read_file refuses over
 * JC_READ_FILE_MAX). The redaction output is SIZED FROM THE INPUT, not a
 * fixed buffer: jc_redact_secrets/jc_redact_apply replace each secret of
 * >= JC_REDACT_MIN bytes with the 3-char "***", so the redacted string is
 * NEVER longer than the input, and rlen + 1 is therefore always enough. A
 * fixed cap (the pre-M1044-b 8192-byte buffer) silently cut any report
 * longer than 8191 bytes on the redaction-active path (def-332). raw and
 * red both live in the per-call arena (jc_app_tool_scratch), the same one
 * apply_change uses. */
static int jc_par_store_report(struct jc_app *app, const char *path,
                               int task_no, const char *run_id,
                               char *store, jc_size scap)
{
    char dir[JC_PATH_MAX];
    char *raw = NULL;
    jc_size rlen;

    jc_snprintf(dir, sizeof dir, "%s/.jichi.d/attempts/%s",
                jc_home_dir(), run_id);
    jc_snprintf(store, scap, "%s/report-t%d.md", dir, task_no);
    if (jc_mkdir_p(dir) != JC_OK) {
        return 0;
    }
    if (jc_read_file(path, &raw, NULL, jc_app_tool_scratch(app)) != JC_OK ||
        raw == NULL) {
        return 0;
    }
    rlen = (jc_size)strlen(raw);
    if (jc_redact_active()) {
        char *red = (char *)jc_arena_alloc(jc_app_tool_scratch(app), rlen + 1);
        if (red == NULL) {
            return 0;
        }
        jc_redact_secrets(raw, red, rlen + 1);
        raw = red;
        rlen = (jc_size)strlen(red);
    }
    return (jc_write_file(store, raw, rlen) == JC_OK) ? 1 : 0;
}

#define JC_PAR_TASK_TIMEOUT_DEFAULT 300   /* per-child watchdog (s); 0 cfg => this */
#define JC_PAR_TERM_GRACE_MS        300   /* SIGTERM -> SIGKILL grace window */

/* One subtask: resolved plan (parent) + runtime state (pool) + parsed result. */
struct ptask {
    const char               *task;       /* prompt                          */
    const char               *model_sel;  /* resolved selector, or NULL      */
    const struct jc_agentdef *def;        /* agent profile, or NULL          */
    int                       write;      /* edit in an isolated worktree    */
    const char               *from;       /* M816: resume off this kept ref  */
    char                     *task_owned; /* M816: malloc'd task text with the
                                           * predecessor note prepended, or NULL;
                                           * `task` points into it when set     */
    int                       invalid;    /* skipped before fork             */
    char                      wt[1200];   /* worktree path ("" if none)      */
    char                      tmpdir[1300]; /* M852: the child's private $TMPDIR, in
                                           * its worktree's git admin dir ("" if
                                           * none): outside the worktree, so never
                                           * merged; inside the shadow repo the
                                           * fence grants; removed with the
                                           * worktree by `git worktree remove`. */
    int                       fence_hints; /* M852: shell commands that hit a
                                           * permission error under the fence */
    char                      note[400];  /* a fallback/skip note, or ""     */
    char                      newdirs[600]; /* M888: its new directories, named */
    int                       no_merge;     /* M890: "merge": false -- kept, never
                                             * merged, at any ending */
    int                       newdir_held;  /* M888: how many were held        */

    pid_t                     pid;
    int                       fd;         /* pipe read end, -1 when closed   */
    int                       launched;
    int                       done;
    long                      launch_ms;  /* monotonic stamp at fork (watchdog) */
    double                    tok_budget;  /* per-child token slice (0 => none) */
    int                       tool_budget; /* per-child tool-call slice (-1 none) */
    int                       max_calls;   /* M861: the task's own max_tool_calls
                                            * (0 = none), bounded by the slice */
    int                       tused;       /* M861: the calls its budget counted,
                                            * its delegates' included */
    int                       npinned;     /* M863: states kept before discarding
                                            * git commands, and their refs */
    char                      pinned[JC_PAR_PINNED_MAX];
    struct jc_sb              out;        /* unconsumed child pipe bytes     */
    const char               *model_name; /* resolved, for the board         */
    char                      cur_tool[48];/* last tool the child ran         */
    int                       finished;   /* a "done" message was parsed     */
    int                       merged;      /* M803: content is in the parent tree
                                            * (skip preservation at teardown)    */
    const char               *preserve_why; /* M803: reason if NOT merged, or NULL */

    char                     *answer;     /* parsed (malloc), or NULL        */
    char                     *error;      /* parsed (malloc), or NULL        */
    double                    tokens;
    int                       tool_calls; /* parsed from the child (budget)  */
    /* M437: the child's own report, parsed from its "done" message. */
    char                      stop[16];
    char                      ftool[JC_DELEG_TOOL_MAX];
    char                      fmsg[JC_DELEG_MSG_MAX];
    int                       fcls;
    int                       looped;     /* M869: which loop stop ended it, or 0 */
    int                       fcount;     /* M876: failures of fcls in its run    */
    int                       fstop;      /* M885: that call was killed by the
                                           * child's own stop, not by a limit */
    char                      rerr[JC_DELEG_MSG_MAX]; /* M876: its model failure */
    int                       rerrn;
    long                      rerr0;
    long                      rerr1;
    int                       repeated;   /* M808: tool-loop repeat count, for the
                                           * preserved end record's `repeated` line */
    char                     *last;       /* M854: the child's last text when its
                                           * run did not end OK (malloc), or NULL */
    int                       term;       /* M854: the child saw SIGTERM        */
    int                       overflow;   /* M854: a context-window 400 ended it */
    long                      band;       /* M860: this child's seq band */
    int                       cut;        /* M858: its last reply was cut at the
                                           * output ceiling (the ceiling), or 0 */
    const char               *ending;     /* M854: how a write child that did not
                                           * end on its own ended, or NULL; such
                                           * a child merges only after a green
                                           * worktree verify */
    /* M437: the paths this write child actually changed, bounded and rendered as
     * it goes -- the one report field the fork pool can fill and the synchronous
     * spawn_subagent cannot, because a worktree gives a per-delegate baseline
     * that a nested in-process run has no equivalent of. */
    char                      changed[JC_DELEG_MSG_MAX];
};

/* Child-side callback state: serializes progress to the result pipe. */
struct child_cb {
    int    wfd;
    double total;
    int    tools;   /* tool calls this child made (merged into the budget) */
};

/* M860 (N26, CTL-5): this process's batch counter and next seq band. A batch's
 * id is "<base>-b<k>" -- its base dir, transcripts, manifest, end records and
 * task_run all carry it -- and its children's seq bands continue where the last
 * batch's stopped. Before M860 every batch of a session shared the session id:
 * two batches gave their children the same task_run and the same seq (N26), and
 * a batch started INSIDE a write child reused the lead's live base, collided
 * with its worktree, and deleted its lock and manifest, so a later run salvaged
 * the lead's live worktree as orphaned (CTL-5). Per process: a forked child that
 * starts a nested batch counts in its own copy, under its own base (s_task_id),
 * so the ids stay distinct.
 *
 * s_task_id is a child's own id, "<batch>-t<task>" -- the string its task_run
 * carries -- set by run_child in the forked child. It is kept here rather than
 * read back from the telemetry sink, which is NULL when telemetry is off
 * (`--log -`): a nested batch then fell back to the envelope's run id, which the
 * lead shares, and took the id of the lead's next batch -- both batches wrote
 * one transcript file. */
static int s_batch_no = 0;
static long s_next_band = 1;
static char s_task_id[72] = "";

/* M863 (N44 (a)): a write child's keep-before-discard state, set by run_child in
 * the forked child: the batch's base (copied from s_batch_base, which the parent
 * sets before forking), its private temp dir (the temporary index goes there,
 * outside the worktree), the last state kept ("<head> <tree>"), and the refs kept
 * (sent to the parent in the done line). */
static char s_batch_base[64] = "";
static char s_child_base[64] = "";
static char s_child_tmpdir[1300] = "";
static char s_pin_last[140] = "";
static char s_pinned[JC_PAR_PINNED_MAX] = "";
static int s_npinned = 0;

static void write_all(int fd, const char *s, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, s + off, n - off);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        off += (size_t)w;
    }
}

/* Write one compact JSON object + '\n' to the pipe (newline-framed). */
static void child_emit(int wfd, cJSON *o)
{
    char *s = jc_json_print(o);
    if (s != NULL) {
        write_all(wfd, s, strlen(s));
        write_all(wfd, "\n", 1);
        free(s);
    }
    cJSON_Delete(o);
}

/* M442 added the tool-call id to the callback. A fork-pool child does NOT forward
 * it: the parent renders a per-child board line and an aggregated report, never a
 * paired timeline, so an id would be bytes over the pipe that nothing reads. Named
 * and voided rather than silently accepted, so the omission is a decision. */
static void child_on_tool(void *u, const char *name, const char *args,
                          const char *id)
{
    struct child_cb *cc = (struct child_cb *)u;
    cJSON *o = cJSON_CreateObject();
    (void)args;
    (void)id;
    cc->tools++;
    cJSON_AddStringToObject(o, "t", "tool");
    cJSON_AddStringToObject(o, "name", name != NULL ? name : "?");
    child_emit(cc->wfd, o);
}

static void child_on_usage(void *u, double in_tok, double out_tok,
                           double cache_read, double cache_write)
{
    struct child_cb *cc = (struct child_cb *)u;
    cJSON *o = cJSON_CreateObject();
    (void)cache_read;   /* progress board reports total tokens only */
    (void)cache_write;
    cc->total += in_tok + out_tok;
    cJSON_AddStringToObject(o, "t", "tok");
    cJSON_AddNumberToObject(o, "n", cc->total);
    child_emit(cc->wfd, o);
}

/* In a forked child: run the subagent, streaming {tool}/{tok} progress lines to
 * the pipe, then a final {done,answer|error,tokens} line. Never returns to the
 * pool; the caller _exit()s. */
/* M831 (O9): confine a forked WRITE child's shell to its worktree with Landlock.
 * A write child edits in an isolated worktree and its FILE tools are path-fenced to
 * it (M820), but its SHELL can write anywhere the process can -- a variable, a
 * `cd ..`, or a script it writes then runs all reach the live tree, so a lexical
 * guard on the command TEXT is not a fence. Landlock is an irrevocable, inherited
 * kernel write restriction; applied here post-fork, before the agent loop, it binds
 * the child and every command the child starts, shell included. The allow-set is
 * exactly what a fenced write child legitimately writes: its worktree (app->root,
 * canonicalised above), the shadow repo it links to (so `git` in the worktree
 * works), /dev/null (so `2>/dev/null` works), and any operator-listed extra dirs
 * (shellFenceAllow). Reads and execution are never restricted. A broad /tmp grant is
 * DELIBERATELY not in the set: /tmp is world-writable and, more to the point, it is
 * where a project could sit -- granting it would reopen the escape the fence closes.
 * The child's own worktree is its scratch (its shell starts there); a command that
 * needs an external temp dir is refused, the model reads that as a tool error, and
 * the operator widens the set with shellFenceAllow. M852: the child's own temp dir
 * ($TMPDIR, in its worktree's git admin dir, inside the shadow repo) is writable,
 * so a compiler's or mktemp's temp file no longer needs a grant. On a kernel without
 * Landlock the child runs UNFENCED -- never silently: a WARN line and a telemetry
 * event record it, and `doctor` reports the static capability (jc_landlock_abi).
 * With Landlock present, a fence that still cannot be applied REFUSES the task
 * (M852; M831 ran it unfenced). Returns 1 applied, 0 unfenced (disabled, or no
 * Landlock), -1 refused -- `why` then holds the task's error. */
/* M852 (N36): a private temp dir for a write child, in its worktree's git admin
 * directory (`<shadow repo>/worktrees/<id>/tmp`), read from the worktree's `.git`
 * file ("gitdir: <admin>"). Three reasons for that home, each measured or read:
 * M831's fence already grants the shadow repo, so nothing widens; it is OUTSIDE the
 * worktree, so `git add -A` never commits a temp file and the merge never brings
 * one back (inside the worktree it would -- run 0085 merged a 963-file copy made
 * there); and `git worktree remove` deletes the admin dir with git's own recursive
 * remove, which does not follow a symlink a child planted. Before M852 a fenced
 * child had no writable temp dir at all, and `cc -c` aborted with "Cannot create
 * temporary file in /tmp" (measured). Leaves `out` empty when the admin dir cannot
 * be found or the dir cannot be made; the caller then sets no TMPDIR. */
static void worktree_tmpdir(const char *wt, char *out, jc_size cap)
{
    char gitfile[1300];
    char line[1200];
    FILE *f;
    jc_size n;

    out[0] = '\0';
    jc_snprintf(gitfile, sizeof gitfile, "%s/.git", wt);
    f = fopen(gitfile, "rb");   /* binary: portability_lint 37 */
    if (f == NULL) {
        return;
    }
    if (fgets(line, (int)sizeof line, f) == NULL) {
        fclose(f);
        return;
    }
    fclose(f);
    n = (jc_size)strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                     line[n - 1] == ' ')) {
        line[--n] = '\0';
    }
    if (strncmp(line, "gitdir: ", 8) != 0 || line[8] != '/') {
        return;
    }
    jc_snprintf(out, cap, "%s/tmp", line + 8);
    if (jc_mkdir_p_private(out) != JC_OK) {
        out[0] = '\0';
    }
}

static int apply_shell_fence(struct jc_app *app, int task_no, char *why,
                             jc_size whycap)
{
    /* M857: 32, not 8 -- at 8 a shellFenceAllow entry past the sixth was dropped
     * without a word (the loop below stops at the array's end) */
    const char *dirs[32];
    char toolout[512];               /* M857 (N41): this child's ticket store */
    const char *files[1];
    int n_dirs = 0;
    int failed = -1;
    jc_status st;
    cJSON *ev;

    if (why != NULL && whycap > 0) {
        why[0] = '\0';
    }

    if (app->config.shell_fence == 0) {
        return 0;   /* explicitly disabled ("shellFence": false): no fence at all */
    }

    dirs[n_dirs++] = app->root;   /* the worktree: edits, index, the .git file */
    if (app->snapshots != NULL && app->snapshots->git_dir[0] != '\0') {
        dirs[n_dirs++] = app->snapshots->git_dir;   /* objects + this worktree's admin */
    }
    /* M857 (N41): jichi's own claim-ticket store (D3, M339) for THIS process --
     * `$HOME/.jichi.d/tool-output/<pid>`. The fence binds the whole forked child,
     * jichi's own writes included, and the store is outside the worktree: on the
     * demos drive's first fenced run every mid-turn trim lost its ticket (29
     * "could not preserve" warnings in run 0089, none before the fence). Created
     * here, before the fence, so the rule can open it; granted alone, so the
     * shell still cannot write the store's parent or any other process's. The
     * store keeps its place, so the path the model is given, and the path
     * fence's read exemption for it (jc_app.c), are unchanged. */
    if (jc_toolout_dir(app, toolout, sizeof toolout) != NULL &&
        jc_mkdir_p(toolout) == JC_OK) {
        dirs[n_dirs++] = toolout;
    }
    /* M961 (F1): the background spool directory, so a fenced child's background
     * commands can write their spool files. The child's `app->bg` is a COW copy
     * of the parent's bg manager (M831's paragraph 3); `spool_dir` is
     * `~/.jichi.d/bgspool`, created by `jc_bg_mgr_init` BEFORE any fork, so the
     * path exists when the fence is applied. Grant ONLY this dir (not the parent
     * `.jichi.d` or any sibling) so a fenced child cannot write elsewhere in
     * `.jichi.d`, exactly mirroring the `toolout` grant's rationale (the parent's
     * bg manager creates `<spool_dir>/bg-<id>-<pid>.out`, and the child's
     * `jc_bg_start` opens it in the forked write child). */
    if (app->bg != NULL && app->bg->spool_dir[0] != '\0') {
        if (jc_mkdir_p(app->bg->spool_dir) == JC_OK) {
            dirs[n_dirs++] = app->bg->spool_dir;
        }
    }
    {
        jc_size i;
        for (i = 0; i < app->config.shell_fence_allow.len &&
                    n_dirs < (int)(sizeof dirs / sizeof dirs[0]); i++) {
            const char *p = *(char **)jc_vec_at(&app->config.shell_fence_allow, i);
            if (p != NULL && p[0] != '\0') {
                dirs[n_dirs++] = p;
            }
        }
    }
    files[0] = "/dev/null";

    st = jc_landlock_write_fence_ex(dirs, n_dirs, files, 1, &failed);
    /* Stamp depth/turn/run via the shared helper so this event joins to the run
     * that produced it (M583 seam D4); a raw jc_eventlog_begin here would carry
     * none of the three (telemetry_events_lint check 9). */
    ev = jc_app_telem_begin(app, "shell_fence");
    if (ev != NULL) {
        cJSON_AddNumberToObject(ev, "abi", jc_landlock_abi());
        cJSON_AddNumberToObject(ev, "dirs", n_dirs);
        if (st == JC_OK) {
            cJSON_AddStringToObject(ev, "state", "applied");
        } else {
            cJSON_AddStringToObject(ev, "state",
                st == JC_ERR_DENIED ? "unavailable" : "error");
        }
        jc_app_telem_end(app, ev);
    }
    app->shell_fenced = (st == JC_OK) ? 1 : 0;   /* M852: the permission-error hint is live */
    if (st == JC_ERR_DENIED) {
        jc_logf(JC_LOG_WARN, "shell fence unavailable (no Landlock on this kernel); parallel write task %d runs unfenced -- its shell can write outside its worktree. See `doctor`.", task_no);
        return 0;
    }
    if (st != JC_OK) {
        /* M852 (N36), the operator's decision of 2026-10-01: Landlock IS here, and the
         * fence still could not be applied -- refuse the task. "A fence that half
         * applied is not run" (the M831 proposal's 3.3); M831 as built ran the child
         * unfenced. Name the directory when the kernel refused a rule for one (a
         * regular file listed as a directory is the reproducible case). A MISSING
         * directory never gets here: it is skipped, granting nothing, and the batch
         * result says so once at the top (the decision's second half). */
        struct stat fst;
        const char *bad = (failed >= 0 && failed < n_dirs) ? dirs[failed] : NULL;
        if (why != NULL && whycap > 0) {
            if (bad != NULL && stat(bad, &fst) == 0 && !S_ISDIR(fst.st_mode)) {
                jc_snprintf(why, whycap, "refused: the shell fence could not be "
                            "applied -- \"%s\" is listed as a directory to "
                            "grant but is not one (shellFenceAllow)", bad);
            } else if (bad != NULL) {
                jc_snprintf(why, whycap, "refused: the shell fence could not be "
                            "applied -- the kernel refused a rule for \"%s\"",
                            bad);
            } else {
                jc_snprintf(why, whycap, "refused: the shell fence could not be "
                            "applied (Landlock setup failed)");
            }
        }
        jc_logf(JC_LOG_WARN, "parallel write task %d %s", task_no,
                (why != NULL && why[0] != '\0') ? why : "refused");
        return -1;
    }
    return 1;
}

/* M865: does any file the task MODIFIED (not one it created) change an existing
 * test assertion? Names the files in `held` (bounded). Only test-looking paths are
 * diffed -- M88's predicate rejects the rest anyway. M875: `*outside` counts the
 * held files that are not among the metric gate's declared pins. */
static int assertion_hold(struct jc_app *app, const char *wt, const char *base,
                          const struct jc_vec *changes, char *held, jc_size cap,
                          int *outside)
{
    jc_size k;
    int n = 0;

    held[0] = '\0';
    for (k = 0; k < changes->len; k++) {
        const struct jc_change *c =
            (const struct jc_change *)jc_vec_at((struct jc_vec *)changes, k);
        struct jc_sb d;
        if (c->status != 'M' || c->path == NULL ||
            (strstr(c->path, "test") == NULL && strstr(c->path, "Test") == NULL &&
             strstr(c->path, "spec") == NULL && strstr(c->path, "Spec") == NULL)) {
            continue;
        }
        jc_sb_init(&d);
        if (jc_snapshot_worktree_diff(app->snapshots, wt, base, c->path, &d) == JC_OK &&
            d.data != NULL && jc_parallel_diff_edits_assertion(c->path, d.data)) {
            if (strlen(held) + strlen(c->path) + 3 < cap) {
                if (held[0] != '\0') {
                    strcat(held, ", ");
                }
                strcat(held, c->path);
            }
            n++;
            if (!jc_parallel_path_is_pin(c->path,
                                         &app->config.parallel_metric_pins)) {
                (*outside)++;
            }
        }
        jc_sb_free(&d);
    }
    return n;
}

int jc_parallel_pre_discard(struct jc_app *app, const char *args_json,
                            char *ref_out, jc_size cap)
{
    cJSON *aj;
    const char *cmd;
    char what[16];
    char idx[1400];
    char record[600];
    jc_status st;
    int r = 0;

    if (ref_out != NULL && cap > 0) {
        ref_out[0] = '\0';
    }
    if (app == NULL || !app->parallel_write || app->snapshots == NULL ||
        args_json == NULL) {
        return 0;
    }
    aj = jc_json_parse(args_json);
    if (aj == NULL) {
        return 0;
    }
    cmd = jc_json_get_str(aj, "command", NULL);
    if (!jc_cmd_git_discards(cmd, what, sizeof what)) {
        cJSON_Delete(aj);
        return 0;
    }
    /* the temporary index: the child's private temp dir, outside its worktree
     * (else `add -A` would keep the index file itself); else beside the store */
    if (s_child_tmpdir[0] != '\0') {
        jc_snprintf(idx, sizeof idx, "%s/jichi-pin-%ld.idx", s_child_tmpdir,
                    (long)getpid());
    } else {
        char d[JC_PATH_MAX];
        if (jc_toolout_dir(app, d, sizeof d) == NULL) {
            cJSON_Delete(aj);
            return -1;
        }
        (void)jc_mkdir_p(d);
        jc_snprintf(idx, sizeof idx, "%s/pin.idx", d);
    }
    jc_snprintf(record, sizeof record,
                "pre-discard: before `%.300s`\ntask-run: %s\nbase: %s\n",
                cmd, s_task_id, s_child_base);
    st = jc_snapshot_worktree_pin(app->snapshots, app->root, idx, s_child_base,
                                  "pre-discard", record, s_pin_last,
                                  sizeof s_pin_last, ref_out, cap);
    if (st == JC_OK) {
        r = 1;
        s_npinned++;
        if (strlen(s_pinned) + strlen(ref_out) + 2 < sizeof s_pinned) {
            if (s_pinned[0] != '\0') {
                strcat(s_pinned, " ");
            }
            strcat(s_pinned, ref_out);
        }
    } else if (st != JC_ERR_NOTFOUND) {
        r = -1;
        jc_logf(JC_LOG_WARN, "could not keep the worktree before `git %s` -- "
                "the command runs, and what it discards is not kept", what);
    }
    {
        cJSON *o = jc_app_telem_begin(app, "pre_discard");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "sub", what);
            cJSON_AddStringToObject(o, "kept",
                r == 1 ? "yes" : (r == 0 ? "nothing to lose" : "failed"));
        }
        jc_eventlog_end(app->telemetry, o);
    }
    cJSON_Delete(aj);
    return r;
}

static void run_child(struct jc_app *app, struct ptask *t, int task_no, int wfd,
                      const char *run_id)
{
    struct jc_history h;
    struct jc_provider *prov;
    struct jc_model_cfg *mc;
    struct jc_model_cfg *active;
    const char *sysmsg;
    char *answer = NULL;
    double before;
    double used;
    int cc_tools = 0;
    int include_mutating;
    int found = 1;
    cJSON *res;
    char *s;
    jc_status st;
    struct jc_eventlog transcript_sink;   /* M807: this child's reasoning record */

    /* M860: this child's own id, the base of any batch it starts -- kept here,
     * not read back from telemetry, which may be off (`--log -`). M864: set first,
     * so that every line this child logs carries the UTC time and that id. */
    if (run_id != NULL && run_id[0] != '\0') {
        jc_snprintf(s_task_id, sizeof s_task_id, "%s-t%d", run_id, task_no);
        jc_log_set_prefix(s_task_id);
    }
    if (t->write && t->wt[0] != '\0') {
        if (chdir(t->wt) == 0) {
            char canon[JC_PATH_MAX];
            jc_snprintf(app->cwd, sizeof(app->cwd), "%s", t->wt);
            app->parallel_write = 1;   /* M855: the stash hint */
            /* Move the path-fence root to the worktree too: otherwise a
             * write_file into it resolves outside app->root (still the real
             * workspace) and the fence -- on by default in --auto, the very
             * mode write tasks are for -- denies every write, silently
             * dropping all changes so nothing merges back. Safe in the forked
             * child (COW; the parent's app->root is untouched). */
            if (jc_path_resolve(t->wt, canon, sizeof(canon)) == JC_OK) {
                jc_snprintf(app->root, sizeof(app->root), "%s", canon);
            } else {
                jc_snprintf(app->root, sizeof(app->root), "%s", t->wt);
            }
        }
    }
    /* Per-child budget reservation (M62 #4): cap this child's slice of the
     * run's remaining budget so N siblings can't collectively N-times overspend
     * (the parent only reconciles real usage after the pool exits). Writes here
     * touch only the child's COW copy of the envelope, so the parent is
     * unaffected. */
    if (app->env != NULL) {
        if (t->tok_budget > 0.0) {
            app->env->budget_tokens = app->env->tokens_used + t->tok_budget;
        }
        if (t->tool_budget >= 0) {
            app->env->max_tool_calls = app->env->tool_calls + t->tool_budget;
        }
    }

    active = jc_config_model_at(&app->config, app->config.active);
    mc = jc_subagent_resolve_model(&app->config, t->model_sel, &found);
    if (mc == NULL || mc == active) {
        prov = app->provider;
    } else {
        prov = jc_provider_create(mc);
        if (prov == NULL) {
            prov = app->provider;
        }
    }
    /* M800: attribute this child's telemetry -- name the model it actually calls
     * (mc, not app->config.model, which still mirrors the lead's), stamp the task
     * index, and band the event seq so concurrent children never collide. This
     * fork's app is a COW copy, so none of it reaches the lead. */
    if (mc != NULL) {
        app->telem_model_name = mc->name;
        app->telem_model_id = mc->model;
    }
    jc_eventlog_set_task_run(app->telemetry, run_id, (long)task_no, t->band); /* M860 */
    /* M863: this child's keep-before-discard state starts empty. */
    jc_snprintf(s_child_base, sizeof s_child_base, "%s", s_batch_base);
    jc_snprintf(s_child_tmpdir, sizeof s_child_tmpdir, "%s", t->tmpdir);
    s_pin_last[0] = '\0';
    s_pinned[0] = '\0';
    s_npinned = 0;
    /* M807: a WRITE child records its reasoning to its own transcript, unless
     * sessions are off (--no-session persists nothing). One JSONL line per model
     * reply and per tool call, born redacted (jc_eventlog_add_text scrubs), 0600
     * in a 0700 dir, flushed per line so a SIGKILL loses at most the line being
     * written. This is what a successor reads; P3 builds the end record from its
     * tail. The dir is named by the run so gc can remove it with the run's refs. */
    jc_eventlog_disable(&transcript_sink);
    if (t->write && !app->no_session && run_id != NULL && run_id[0] != '\0') {
        char tdir[JC_PATH_MAX];
        char tpath[JC_PATH_MAX];
        jc_snprintf(tdir, sizeof(tdir), "%s/.jichi.d/attempts/%s",
                    jc_home_dir(), run_id);
        if (jc_mkdir_p_private(tdir) == JC_OK) {   /* creates it 0700, M488 */
            jc_snprintf(tpath, sizeof(tpath), "%s/t%d.jsonl", tdir, task_no);
            if (jc_eventlog_open(&transcript_sink, tpath,
                    (app->telemetry != NULL) ? app->telemetry->sid : "",
                    JC_EVENTLOG_FULL) == JC_OK) {
                jc_eventlog_set_task_run(&transcript_sink, run_id, (long)task_no,
                                         t->band);   /* M860 */
                jc_make_private(tpath);   /* 0600 */
                app->transcript = &transcript_sink;
            }
        }
    }
    include_mutating = (t->write && !app->readonly) ? 1 : 0;
    /* M596: a profile body is the identity paragraph, and the sections jichi
     * enforces at depth (untrusted rule, constraints, edit scope -- M434) are
     * appended after it. Before M596 the bare profile text WAS the whole prompt,
     * so a profiled delegate was fenced by rules it had never seen. */
    sysmsg = jc_sysmsg_build_sub_as(app,
                                    (t->def != NULL) ? t->def->system_prompt : NULL,
                                    NULL);
    /* M820 (N6a, threadwork's weekend drive): a WRITE child edits in its own
     * isolated worktree, but its SHELL is not confined to it (O9), and a task text
     * can name a path in the parent's live workspace -- on the drive one worker's
     * task said "/work" eight times and it made 53 write-class shell commands there.
     * Tell the child plainly where it is. This is the note the drive's team put in
     * its profiles, after which no worker touched the live tree. */
    if (t->write && t->wt[0] != '\0' && sysmsg != NULL) {
        struct jc_sb sm;
        char *full;
        jc_sb_init(&sm);
        jc_sb_append(&sm, sysmsg);
        jc_sb_append(&sm,
            "\n\n## Your workspace\n"
            "You run in your OWN isolated git worktree, and your shell starts "
            "there. The parent's workspace and your siblings' worktrees are NOT "
            "yours: do not `cd` into them or write to them. Work only on paths "
            "inside your worktree -- relative paths, or the directory you started "
            "in. If a file tool refuses a path as outside your workspace, that path "
            "is not yours: stop and say so in your answer rather than reaching for "
            "the shell to get around it.");
        /* sysmsg is ARENA memory (jc_sysmsg_build_sub_as -> jc_arena_strdup), so it
         * must not be free()d; copy the combined text into the same scratch arena,
         * which the turn frees. */
        full = (sm.data != NULL)
             ? jc_arena_strdup(jc_app_scratch(app), sm.data) : NULL;
        if (full != NULL) {
            sysmsg = full;
        }
        jc_sb_free(&sm);
    }

    /* M831 (O9): confine this write child's shell to its worktree. Applied AFTER the
     * transcript is opened (its fd survives -- Landlock checks a write at open, and
     * an already-open descriptor keeps working) and telemetry is inherited, but
     * BEFORE the agent loop, so every command the child runs is bound. Only a WRITE
     * child with a worktree is fenced: a read-only child has no worktree to grant. */
    /* M852 (N36): the child's private temp dir, for every write child with a
     * worktree, fenced or not, so a build behaves the same on every kernel.
     * Exported before the fence and the agent loop, so every command inherits it.
     */
    if (t->write && t->wt[0] != '\0' && t->tmpdir[0] != '\0') {
        (void)setenv("TMPDIR", t->tmpdir, 1);
        /* M887 (N49 (a)): the file tools reach it too -- this exact dir, never
         * an inherited TMPDIR the shell fence does not grant. */
        jc_snprintf(app->child_tmpdir, sizeof app->child_tmpdir, "%s",
                    t->tmpdir);
    }
    if (t->write && t->wt[0] != '\0') {
        char fwhy[1500];
        if (apply_shell_fence(app, task_no, fwhy, sizeof fwhy) < 0) {
            /* M852: refused before the agent loop -- nothing ran. */
            if (app->transcript != NULL) {
                jc_eventlog_close(&transcript_sink);
                app->transcript = NULL;
            }
            res = cJSON_CreateObject();
            cJSON_AddStringToObject(res, "t", "done");
            cJSON_AddStringToObject(res, "error", fwhy);
            cJSON_AddNumberToObject(res, "tokens", 0.0);
            cJSON_AddNumberToObject(res, "tools", 0.0);
            cJSON_AddStringToObject(res, "stop", "error");
            s = jc_json_print(res);
            if (s != NULL) {
                write_all(wfd, s, strlen(s));
                write_all(wfd, "\n", 1);
                free(s);
            }
            cJSON_Delete(res);
            return;
        }
    }

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, t->task);
    before = (app->env != NULL) ? app->env->tokens_used : 0.0;
    {
        struct child_cb cc;
        struct jc_agent_callbacks cb;
        cc.wfd = wfd;
        cc.total = 0.0;
        cc.tools = 0;
        memset(&cb, 0, sizeof(cb));
        cb.on_tool_start = child_on_tool;
        cb.on_usage = child_on_usage;
        cb.user = &cc;
        /* M861 (N28): the task's own tool-call budget, binding in this child
         * whether or not the run has an envelope; its note names the smaller of
         * the key and the envelope's slice, the budget this task actually has.
         * This fork's app is a COW copy, so none of it reaches the lead. */
        app->task_tool_max = t->max_calls;
        app->task_tool_eff = (t->tool_budget >= 0 && t->tool_budget < t->max_calls)
                             ? t->tool_budget : t->max_calls;
        app->task_tool_used = 0;
        app->task_budget_noted = 0;
        app->task_tool_depth = app->agent_depth + 1;
        app->agent_depth++;
        st = jc_agent_run_subagent(app, &h, prov, sysmsg, include_mutating,
                                   app->config.max_subagent_iters,
                                   (t->def != NULL && t->def->tools.len > 0)
                                       ? &t->def->tools : NULL,
                                   &cb, &answer);
        app->agent_depth--;
        /* Prefer the streamed running total; fall back to the envelope delta. */
        used = (cc.total > 0.0) ? cc.total
             : ((app->env != NULL) ? app->env->tokens_used - before : 0.0);
        cc_tools = cc.tools;
    }
    /* M807: flush and close the transcript (each line was already flushed; this
     * releases the FILE*). The child _exit()s next, so this is tidiness. */
    if (app->transcript != NULL) {
        jc_eventlog_close(&transcript_sink);
        app->transcript = NULL;
    }

    res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "t", "done");
    if (st == JC_OK && answer != NULL && answer[0] != '\0') {
        cJSON_AddStringToObject(res, "answer", answer);
    } else if (st == JC_ERR_ABORTED) {
        cJSON_AddStringToObject(res, "error", "interrupted");
    } else if (st != JC_OK) {
        cJSON_AddStringToObject(res, "error", "run failed");
    } else {
        cJSON_AddStringToObject(res, "error", "no final answer");
    }
    cJSON_AddNumberToObject(res, "tokens", used);
    cJSON_AddNumberToObject(res, "tools", (double)cc_tools);
    /* M437: the child's stop reason and last failing call. A capped parallel child
     * previously got NO note at all -- spawn_subagent had carried one since M62,
     * so a truncated answer from the fork pool was indistinguishable from a
     * complete one, which is the worse of the two defects this closes. Sent as
     * separate fields, not a rendered string, so the PARENT renders every child
     * through the same jc_delegreport_render as the synchronous tool. */
    cJSON_AddStringToObject(res, "stop", jc_delegreport_stop_name(
        jc_delegreport_stop_from(st == JC_OK, st == JC_ERR_ABORTED,
                                 app->last_run_capped,
                                 app->last_run_budget_stopped,
                                 answer != NULL && answer[0] != '\0')));
    if (app->last_fail_tool[0] != '\0') {
        cJSON_AddStringToObject(res, "ftool", app->last_fail_tool);
        cJSON_AddStringToObject(res, "fmsg", app->last_fail_msg);
        cJSON_AddNumberToObject(res, "fcls", (double)app->last_fail_cls);
        /* M885: that call was killed by this run's stop, not by a limit */
        if (app->last_fail_stopped) {
            cJSON_AddNumberToObject(res, "fstop", 1.0);
        }
        /* M876 (N47 (c)): how often that class failed in the run */
        if (app->last_fail_cls >= 0 && app->last_fail_cls < JC_FAIL_CLASS_COUNT) {
            cJSON_AddNumberToObject(res, "fcount",
                (double)app->fail_cls_count[app->last_fail_cls]);
        }
    }
    /* M876 (N47 (a)): the run's own failure, when its model calls failed */
    if (st != JC_OK && st != JC_ERR_ABORTED && app->last_model_error[0] != '\0') {
        cJSON_AddStringToObject(res, "rerr", app->last_model_error);
        cJSON_AddNumberToObject(res, "rerrn", (double)app->last_model_attempts);
        cJSON_AddNumberToObject(res, "rerr0", (double)app->last_model_t0);
        cJSON_AddNumberToObject(res, "rerr1", (double)app->last_model_t1);
    }
    /* M869: which loop stop ended it, so the parent names it (not "iteration cap"). */
    if (app->last_run_looped > 0) {
        cJSON_AddNumberToObject(res, "looped", (double)app->last_run_looped);
    }
    /* M808: the tool-loop repeat count, for the end record's `repeated` line. */
    if (app->last_fail_repeat > 0) {
        cJSON_AddNumberToObject(res, "repeated", (double)app->last_fail_repeat);
    }
    /* M852: how many shell commands hit a permission error under the fence. */
    if (app->fence_hints > 0) {
        cJSON_AddNumberToObject(res, "fhints", (double)app->fence_hints);
    }
    /* M854 (N33, N24c): how a run that did not end on its own ended, for the
     * parent's decision (jc_parallel_ending): its last text, which the parent
     * never sees otherwise when the run did not end OK; a SIGTERM (M146's
     * term_flag); a context-window refusal. */
    if (st != JC_OK && answer != NULL && answer[0] != '\0') {
        cJSON_AddStringToObject(res, "last", answer);
    }
    if (app->term_flag) {
        cJSON_AddNumberToObject(res, "term", 1.0);
    }
    if (app->ctx_overflow) {
        cJSON_AddNumberToObject(res, "overflow", 1.0);
    }
    /* M863 (N44 (a)): the states this child kept before discarding commands. */
    if (s_npinned > 0) {
        cJSON_AddNumberToObject(res, "npinned", (double)s_npinned);
        cJSON_AddStringToObject(res, "pinned", s_pinned);
    }
    /* M861: the calls the task's budget counted. A delegate it started ran with
     * no callbacks of this child's (spawn_subagent forwards none here), so
     * `tools` above leaves its calls out, while the budget counts them. */
    if (app->task_tool_max > 0) {
        cJSON_AddNumberToObject(res, "tused", (double)app->task_tool_used);
    }
    /* M858 (N42): the run's last reply was cut at the output ceiling; the value is
     * that ceiling, for the note (the provider marked it: M334's finish_reason,
     * or the usage reaching max_tokens). */
    if (app->last_response_truncated) {
        const struct jc_model_cfg *um = (mc != NULL) ? mc : active;
        long cap = (um != NULL)
            ? jc_config_effective_max_tokens(um->max_tokens, um->context_limit) : 1;
        cJSON_AddNumberToObject(res, "cut", (double)(cap > 0 ? cap : 1));
    }
    s = jc_json_print(res);
    if (s != NULL) {
        write_all(wfd, s, strlen(s));
        write_all(wfd, "\n", 1);
        free(s);
    }
    cJSON_Delete(res);
    jc_history_free(&h);
}

/* Copy (A/M) or delete (D) one path from worktree `wt` into the live workspace
 * (app->cwd). Returns 1 on success. */
static int apply_change(struct jc_app *app, const char *wt, char status,
                        const char *rel)
{
    char dst[2048];

    jc_snprintf(dst, sizeof(dst), "%s/%s", app->cwd, rel);
    if (status == 'D') {
        remove(dst);
        return 1;
    } else {
        char src[2048];
        char dstdir[2048];
        char *slash;
        char target[2048];
        struct stat st;
        struct stat fst;
        ssize_t n;
        int mode;
        char *content = NULL;
        jc_size len = 0;

        jc_snprintf(src, sizeof(src), "%s/%s", wt, rel);
        /* M1099 (D-12): lstat the child's side BEFORE touching it. The old
         * path was jc_read_file(src) + jc_write_file(dst), and both fopen()
         * FOLLOW symlinks -- so a child that left a link at the path was
         * merged as a COPY OF ITS TARGET (the outside file's content), and a
         * link planted at the live path was written THROUGH (the outside
         * target changed). lstat sees what is actually on the path. */
        if (lstat(src, &st) != 0) {
            return 0;
        }
        if (S_ISLNK(st.st_mode)) {
            /* Re-create the child's link AS A link, byte-exact. readlink's
             * length excludes the terminator; a link whose target is too long
             * for our buffer is refused (a conflict), never truncated. */
            n = readlink(src, target, sizeof(target) - 1);
            if (n < 0) {
                return 0;
            }
            if ((size_t)n >= sizeof(target) - 1) {
                return 0;
            }
            target[n] = '\0';
            /* A link the live tree already has is REPLACED (rename over it
             * writes nothing through it); a regular file is replaced too.
             * dst is the merge's own path, owned by the child's change. */
            if (lstat(dst, &fst) == 0) {
                remove(dst);
            }
            /* M1099-c (SV:1144.2): a link in a NEW directory must land. The
             * S_ISREG arm creates the parent via dstdir/jc_mkdir_p; the link
             * arm did not, so symlink() failed with ENOENT and the M927
             * conflict arm preserved the worktree instead of creating the
             * directory and the link. Same shape as the S_ISREG arm below. */
            jc_snprintf(dstdir, sizeof(dstdir), "%s", dst);
            slash = strrchr(dstdir, '/');
            if (slash != NULL) {
                *slash = '\0';
                jc_mkdir_p(dstdir);
            }
            if (symlink(target, dst) != 0) {
                return 0;
            }
            return 1;
        }
        if (S_ISREG(st.st_mode)) {
            int fd;
            char *chunk;
            /* Open O_NONBLOCK so a FIFO/regular swap cannot stall us, then
             * fstat and require the SAME inode we lstat'd (S_ISREG and
             * st_dev/st_ino): a TOCTOU-free read. The children are all
             * reaped before the merge (jc_tool_parallel.c:1497), so the
             * inode compare is belt -- but it is cheap. */
            fd = open(src, O_RDONLY | O_NONBLOCK | JC_O_BINARY);
            if (fd < 0) {
                return 0;
            }
            if (fstat(fd, &fst) != 0 || !S_ISREG(fst.st_mode) ||
                fst.st_dev != st.st_dev || fst.st_ino != st.st_ino) {
                close(fd);
                return 0;
            }
            /* M1099-c (SV:1144.1): a file larger than JC_READ_FILE_MAX is a
             * conflict, never a partial copy -- the same rule jc_read_file's
             * cap always carried. Checked BEFORE the allocation below, which
             * sizes itself to the file. */
            if (fst.st_size < 0 || (jc_size)fst.st_size > JC_READ_FILE_MAX) {
                close(fd);
                return 0;
            }
            /* The child's mode, fstat-verified by the match above. */
            mode = (int)(st.st_mode & 0777);
            /* Accumulate onto the per-tool-call arena (M610's reasoning:
             * apply_change is called once per path; the arena is reset before
             * the next tool call, nothing here outlives it). M1099-c
             * (SV:1144.1): allocate the file's ACTUAL size (fst.st_size + 1
             * for the NUL), not JC_READ_FILE_MAX + 1 -- one merge of N files
             * held N x 64 MiB on the tool-scratch arena; on no-overcommit
             * and 32-bit rows that is spurious M927 conflicts by arithmetic.
             * The read loop below bounds over-reads to the same size. */
            content = (char *)jc_arena_alloc(jc_app_tool_scratch(app),
                                              (jc_size)fst.st_size + 1);
            if (content == NULL) {
                close(fd);
                return 0;
            }
            /* M1099-c (review nit 1): the read chunk is arena-allocated, not
             * a 64 KiB stack frame -- the merge path is absent from the unit
             * suite that make ci runs under ulimit -s 512, so the stack cost
             * is unmeasured against MiNT's 512 KB _stksize (DEFERRED.md:1327
             * is the class); content is already on the arena, the chunk
             * follows it. */
            chunk = (char *)jc_arena_alloc(jc_app_tool_scratch(app), 64 * 1024);
            if (chunk == NULL) {
                close(fd);
                return 0;
            }
            len = 0;
            for (;;) {
                n = read(fd, chunk, 64 * 1024);
                if (n == 0) {
                    break;
                }
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    close(fd);
                    return 0;
                }
                /* M1099-c (SV:1144.1 belt): an over-read is a conflict. A
                 * regular file read cannot exceed its fstat size; a size
                 * that changes under us is exactly what the fstat match
                 * was for. */
                if (len + (jc_size)n > (jc_size)fst.st_size) {
                    close(fd);
                    return 0;
                }
                memcpy(content + len, chunk, (size_t)n);
                len += (jc_size)n;
            }
            close(fd);
            content[len] = '\0';
            /* The M927 conflict arm: a failed copy is a conflict that
             * preserves the worktree, never a silent drop. */
            if (JC_FAULT_HIT(JC_FAULT_COPY)) {
                return 0;
            }
            jc_snprintf(dstdir, sizeof(dstdir), "%s", dst);
            slash = strrchr(dstdir, '/');
            if (slash != NULL) {
                *slash = '\0';
                jc_mkdir_p(dstdir);
            }
            /* M1099 (D-12): the dst is written through a mode-carrying atomic
             * writer (rename replaces the live path -- a link there is
             * replaced, nothing written THROUGH it) and carries the child's
             * mode, not the 0600 the private sinks use. */
            return jc_write_file_atomic_mode(dst, content, len, mode) == JC_OK
                       ? 1 : 0;
        }
        /* Anything else -- S_ISFIFO, S_ISSOCK, S_ISBLK, S_ISCHR, S_ISDIR -- is
         * refused WITHOUT opening it: the M927 conflict arm at the call site
         * preserves the worktree and names it. Never a silent drop, never a
         * read() that could block. */
        return 0;
    }
}

/* Emit one indented board line for task `idx`, only if a front-end wants it. */
static void board_line(struct jc_app *app, int idx, const char *model,
                       int state, const char *detail, double tokens)
{
    char s[400];
    if (app->cb == NULL || app->cb->on_status == NULL) {
        return; /* headless / no status sink: board is purely additive */
    }
    jc_parallel_board_line(s, sizeof(s), idx, model, state, detail, tokens);
    app->cb->on_status(app->cb->user, s);
}

/* Consume complete '\n'-framed messages from t->out, updating live state and
 * the board; a "done" message records the answer/error/tokens. */
static void process_lines(struct jc_app *app, struct ptask *t, int idx)
{
    for (;;) {
        char *nl;
        char *line;
        jc_size linelen;
        jc_size rest;
        struct jc_pmsg m;

        if (t->out.data == NULL) {
            break;
        }
        nl = (char *)memchr(t->out.data, '\n', t->out.len);
        if (nl == NULL) {
            break;
        }
        linelen = (jc_size)(nl - t->out.data);
        line = (char *)malloc(linelen + 1);
        if (line == NULL) {
            break;
        }
        memcpy(line, t->out.data, linelen);
        line[linelen] = '\0';
        rest = t->out.len - linelen - 1;
        memmove(t->out.data, t->out.data + linelen + 1, rest);
        t->out.len = rest;
        t->out.data[rest] = '\0';

        switch (jc_parallel_parse_msg(line, &m)) {
        case JC_PMSG_TOOL: {
            jc_snprintf(t->cur_tool, sizeof(t->cur_tool), "%s", m.tool);
            board_line(app, idx, t->model_name, JC_BOARD_RUN, m.tool,
                       t->tokens);
            break;
        }
        case JC_PMSG_TOK:
            t->tokens = m.tokens;
            break;
        case JC_PMSG_DONE: {
            t->answer = m.answer;
            t->error = m.error;
            m.answer = NULL;
            m.error = NULL;
            t->tokens = m.tokens;
            t->tool_calls = m.tool_calls;
            /* M437: carry the child's report to the aggregation below. */
            jc_snprintf(t->stop, sizeof t->stop, "%s", m.stop);
            jc_snprintf(t->ftool, sizeof t->ftool, "%s", m.ftool);
            jc_snprintf(t->fmsg, sizeof t->fmsg, "%s", m.fmsg);
            t->fcls = m.fcls;
            t->repeated = m.repeated;   /* M808 */
            t->looped = m.looped;       /* M869 */
            t->fcount = m.fcount;       /* M876 */
            t->fstop = m.fstop;         /* M885 */
            jc_snprintf(t->rerr, sizeof t->rerr, "%s", m.rerr);
            t->rerrn = m.rerrn;
            t->rerr0 = m.rerr0;
            t->rerr1 = m.rerr1;
            t->fence_hints = m.fence_hints;   /* M852 */
            t->last = m.last;                 /* M854 */
            m.last = NULL;
            t->term = m.term;
            t->overflow = m.overflow;
            t->cut = m.cut;                   /* M858 */
            t->tused = m.tused;               /* M861 */
            t->npinned = m.npinned;           /* M863 */
            jc_snprintf(t->pinned, sizeof t->pinned, "%s", m.pinned);
            t->finished = 1;
            board_line(app, idx, t->model_name,
                       t->error != NULL ? JC_BOARD_FAIL : JC_BOARD_DONE,
                       t->error != NULL ? t->error : "", t->tokens);
            break;
        }
        default:
            break;
        }
        free(m.answer);
        free(m.error);
        free(m.last);    /* M854 */
        free(line);
    }
}

/* M855: append each line of `lines` to `sb`, indented under a list item. */
static void append_indented(struct jc_sb *sb, const char *lines)
{
    const char *p = lines;
    while (p != NULL && *p != '\0') {
        const char *eol = strchr(p, '\n');
        jc_size len = (eol != NULL) ? (jc_size)(eol - p) : (jc_size)strlen(p);
        jc_sb_append(sb, "  - ");
        jc_sb_append_n(sb, p, len);
        jc_sb_append(sb, "\n");
        p = (eol != NULL) ? eol + 1 : NULL;
    }
}

static int eff_max_parallel(struct jc_app *app, int n_tasks)
{
    return jc_parallel_eff_max(n_tasks, app->config.max_parallel_agents,
                               jc_cpu_count(), JC_PAR_CEILING);
}

/* Reap a child that has already been sent SIGTERM: poll for it within a short
 * grace window, then SIGKILL and block, so a child that ignores or traps SIGTERM
 * can't wedge the parent (M62 #2). Delegates to the shared jc_workerpool
 * primitive (also used by the daemon worker pool). */
static void reap_grace(pid_t pid)
{
    jc_worker_reap_grace(pid, JC_PAR_TERM_GRACE_MS);
}

/* SIGTERM, then escalate to SIGKILL after a grace window, then reap one task. */
static void kill_one(struct ptask *t)
{
    if (!t->launched || t->done) {
        return;
    }
    if (t->fd >= 0) {
        close(t->fd);
        t->fd = -1;
    }
    if (t->pid > 0) {
        kill(t->pid, SIGTERM);
        reap_grace(t->pid);
    }
    t->done = 1;
}

/* Kill and reap any still-running children (on abort). SIGTERM them all first so
 * they shut down in parallel, then escalate+reap each (M62 #2). */
static void kill_live(struct ptask *tasks, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (tasks[i].launched && !tasks[i].done && tasks[i].pid > 0) {
            kill(tasks[i].pid, SIGTERM);
        }
    }
    for (i = 0; i < n; i++) {
        if (tasks[i].launched && !tasks[i].done) {
            if (tasks[i].fd >= 0) {
                close(tasks[i].fd);
                tasks[i].fd = -1;
            }
            reap_grace(tasks[i].pid);
            tasks[i].done = 1;
        }
    }
}

/* The fork pool: keep <= cap children live, gather each child's pipe with a
 * 200ms-sliced select (checking app->abort_flag), reap on EOF, refill. */
static void run_pool(struct jc_app *app, struct ptask *tasks, int ntasks,
                     int cap, long timeout_ms, int *aborted, const char *run_id)
{
    int next = 0;
    int live = 0;
    int done = 0;
    int i;

    *aborted = 0;
    while (done < ntasks) {
        fd_set rf;
        struct timeval tv;
        int maxfd = -1;
        int rc;

        /* M804: `pause` in the pool means "launch no new task" -- already-running
         * children keep going (so their pipes are read and their per-child watchdog
         * stays honest), but the pool starts nothing new until resume. This is the
         * one safe pool semantics for pause: FREEZING the select loop would stop the
         * parent reading the children's pipes, so a child would block on a full pipe
         * while its watchdog clock ran on and then be killed for a stall the pause
         * caused. Queuing pause to the round boundary (the alternative) would make it
         * a no-op during a long batch, which is the very case an operator pauses in.
         * A full mid-task freeze is out of scope by that pipe-starvation hazard. */
        while (live < cap && next < ntasks &&
               !(app->control != NULL && app->control->paused)) {
            struct ptask *t = &tasks[next];
            int fds[2];
            pid_t pid;

            if (t->invalid) {
                t->done = 1;
                done++;
                next++;
                continue;
            }
            if (jc_pipe_cloexec(fds) != 0) {
                t->error = jc_strdup("pipe failed");
                t->done = 1;
                done++;
                next++;
                continue;
            }
            pid = fork();
            if (pid < 0) {
                /* M325: say WHICH failure. A bare "fork failed" cannot tell
                 * EAGAIN (the process/thread limit -- lower maxParallelAgents,
                 * or raise the ulimit) from ENOMEM (out of memory -- fewer
                 * children, or a smaller model), and those want opposite
                 * responses. Seen for real: a workload where 3 of 6
                 * spawn_parallel failures were forks that could not happen, with
                 * nothing in the log to say why. */
                char forkerr[160];
                int e = errno;
                close(fds[0]);
                close(fds[1]);
                jc_snprintf(forkerr, sizeof(forkerr),
                            "fork failed: %s%s", strerror(e),
                            e == EAGAIN
                                ? " (process/thread limit -- lower "
                                  "maxParallelAgents or raise the ulimit)"
                                : (e == ENOMEM ? " (out of memory -- run fewer "
                                                 "children)" : ""));
                t->error = jc_strdup(forkerr);
                t->done = 1;
                done++;
                next++;
                continue;
            }
            if (pid == 0) {
                close(fds[0]);
                run_child(app, t, next + 1, fds[1], run_id);
                close(fds[1]);
                _exit(0);
            }
            close(fds[1]);
            t->pid = pid;
            t->fd = fds[0];
            t->launched = 1;
            t->launch_ms = (long)jc_now_millis();
            jc_sb_init(&t->out);
            /* Resolve the model name for the board and announce the launch. */
            {
                int f = 1;
                struct jc_model_cfg *mc =
                    jc_subagent_resolve_model(&app->config, t->model_sel, &f);
                t->model_name = (mc != NULL) ? mc->name : NULL;
            }
            board_line(app, next, t->model_name, JC_BOARD_RUN, "", 0.0);
            live++;
            next++;
        }

        if (live == 0) {
            break;
        }

        FD_ZERO(&rf);
        for (i = 0; i < ntasks; i++) {
            if (tasks[i].launched && !tasks[i].done && tasks[i].fd >= 0) {
                FD_SET(tasks[i].fd, &rf);
                if (tasks[i].fd > maxfd) {
                    maxfd = tasks[i].fd;
                }
            }
        }
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        rc = select(maxfd + 1, &rf, NULL, NULL, &tv);

        /* M801 (L1a): answer a supervisor's `status` while the batch runs.
         * M804 (L1b): and apply a mid-batch `abort` -- safe now that the teardown
         * after this returns preserves every child's worktree (P1) before removing
         * it, so killing the batch early loses no work. */
        jc_control_poll_during_call(app, 1);
        jc_control_apply_deferred_abort(app);

        /* M1047: liveness at every depth. While the children are alive and
         * streaming (the normal state for a multi-minute batch) the lead's jsonl
         * was SILENT: jc_app_batch_progress above fired only on a child's pipe
         * EOF (the finish tick). A supervisor's `stale` watchdog could not tell
         * a working child from a wedged one (run 0075: both children streaming,
         * lead silent 17 min). Call it on every 200 ms idle wake; the existing
         * once-per-heartbeat_secs throttle inside jc_app_batch_progress (and the
         * same batch_hb_last_ms the finish tick shares) makes this at most one
         * row per interval, and the `live` argument names how many children are
         * still running, so a row is self-describing. */
        jc_app_batch_progress(app, done, ntasks, live);

        if (app->abort_flag) {
            kill_live(tasks, ntasks);
            *aborted = 1;
            return;
        }
        /* Per-child watchdog (M62 #1): a child wedged in a tool or a stalled
         * stream must not hang the whole swarm. Kill any task past its deadline
         * and report it failed; siblings keep running. */
        if (timeout_ms > 0) {
            long now = (long)jc_now_millis();
            for (i = 0; i < ntasks; i++) {
                struct ptask *t = &tasks[i];
                if (t->launched && !t->done &&
                    now - t->launch_ms > timeout_ms) {
                    kill_one(t);
                    if (t->error == NULL && t->answer == NULL) {
                        /* M325: name the knob and the limit that fired. This was
                         * the dominant spawn_parallel failure in a measured
                         * workload -- 3 of 6 -- on a project whose SUCCESSFUL
                         * parallel calls ran 300-462 s against this 300 s
                         * default. "sub-agent timed out" alone sends an operator
                         * looking for a hung child instead of at the setting. */
                        char toerr[160];
                        jc_snprintf(toerr, sizeof(toerr),
                                    "sub-agent timed out after %lds "
                                    "(parallelTaskTimeout) -- raise it if the "
                                    "task legitimately takes longer",
                                    timeout_ms / 1000L);
                        t->error = jc_strdup(toerr);
                    }
                    board_line(app, i, t->model_name, JC_BOARD_TIMEOUT,
                               "timed out", t->tokens);
                    done++;
                    live--;
                }
            }
            if (done >= ntasks) {
                return;
            }
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            kill_live(tasks, ntasks);
            return;
        }
        if (rc == 0) {
            continue;
        }
        for (i = 0; i < ntasks; i++) {
            struct ptask *t = &tasks[i];
            char buf[4096];
            ssize_t n;
            if (!t->launched || t->done || t->fd < 0 ||
                !FD_ISSET(t->fd, &rf)) {
                continue;
            }
            n = read(t->fd, buf, sizeof(buf));
            if (n > 0) {
                jc_sb_append_n(&t->out, buf, (jc_size)n);
                process_lines(app, t, i);
            } else {
                close(t->fd);
                t->fd = -1;
                waitpid(t->pid, NULL, 0);
                t->done = 1;
                done++;
                live--;
                /* M165/M969: batch progress heartbeat for the parallel tool's wait
                 * loop. M1047: the finish tick names the live children too (0 here,
                 * the child just exited) and shares the same throttle as the idle
                 * liveness tick above, so a finish and a liveness tick in the same
                 * interval emit only one row. */
                jc_app_batch_progress(app, done, ntasks, live);
                if (!t->finished && t->answer == NULL && t->error == NULL) {
                    t->error = jc_strdup("sub-agent ended without a result");
                    board_line(app, i, t->model_name, JC_BOARD_FAIL,
                               "ended without a result", t->tokens);
                }
            }
        }
    }
}

static cJSON *parallel_schema(void)
{
    cJSON *s = tu_schema_begin();
    cJSON *props = cJSON_GetObjectItem(s, "properties");
    cJSON *req = cJSON_GetObjectItem(s, "required");
    cJSON *tasks = cJSON_CreateObject();
    cJSON *items = cJSON_CreateObject();
    cJSON *iprops = cJSON_CreateObject();
    cJSON *p;

    cJSON_AddStringToObject(tasks, "type", "array");
    cJSON_AddStringToObject(tasks, "description",
        "The subtasks to run concurrently (each an autonomous sub-agent). Use "
        "independent, self-contained tasks; for editing, set write:true and "
        "give each task a disjoint set of files.");
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description", "The subtask to complete.");
    cJSON_AddItemToObject(iprops, "task", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description",
        "Optional; omit it to use the current model. Otherwise a configured "
        "model's name or 1-based number, or a role one of them declares.");
    cJSON_AddItemToObject(iprops, "model", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description",
        "Optional named agent profile (.jichi/agents/*.md).");
    cJSON_AddItemToObject(iprops, "agent", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "boolean");
    cJSON_AddStringToObject(p, "description",
        "If true, the sub-agent may edit files; it runs in an isolated git "
        "worktree and its changes are merged back. Default false: it is offered "
        "read-only tools, with no shell.");
    cJSON_AddItemToObject(iprops, "write", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description",
        "Optional; resume a kept attempt. Pass a ref from a previous run's "
        "\"Kept\"/\"Salvaged\" list (or `jichi attempts`). Its file changes are "
        "3-way merged into this task's worktree and its already-done outside-tree "
        "actions are noted in the task; implies write:true. A merge conflict is "
        "returned as this task's error.");
    cJSON_AddItemToObject(iprops, "from", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "integer");
    cJSON_AddStringToObject(p, "description",
        "Optional; the most tool calls this task may make (at most its share of "
        "the run's tool-call cap, when one is set). At four-fifths of it the "
        "sub-agent is told to finish up; at it the task ends, and its work merges "
        "only after a green worktree verify.");
    cJSON_AddItemToObject(iprops, "max_tool_calls", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "boolean");
    cJSON_AddStringToObject(p, "description",
        "Optional; false keeps a write task's work and never merges it, at any "
        "ending and after any verify -- for a probe, whose changes are evidence, "
        "not a change. The result names the kept ref, to read or recover.");
    cJSON_AddItemToObject(iprops, "merge", p);
    cJSON_AddStringToObject(items, "type", "object");
    cJSON_AddItemToObject(items, "properties", iprops);
    cJSON_AddItemToObject(tasks, "items", items);

    cJSON_AddItemToObject(props, "tasks", tasks);
    cJSON_AddItemToArray(req, cJSON_CreateString("tasks"));
    return s;
}

/* M811 (P3): write the batch manifest to <base>/batch.json BEFORE any worktree
 * exists (manifest-first), so a crash between the two still records what the
 * trees were. Salvage (M812) reads it to name a dead run's orphaned tasks. */
static void write_batch_manifest(const char *base, const char *run_id,
                                 struct ptask *tasks, int ntasks)
{
    cJSON *o;
    cJSON *arr;
    char *s;
    int i;

    o = cJSON_CreateObject();
    if (o == NULL) {
        return;
    }
    cJSON_AddStringToObject(o, "run", (run_id != NULL) ? run_id : "");
    cJSON_AddNumberToObject(o, "pid", (double)getpid());
    cJSON_AddNumberToObject(o, "started", jc_now_seconds());
    arr = cJSON_CreateArray();
    for (i = 0; i < ntasks; i++) {
        cJSON *t = cJSON_CreateObject();
        if (t == NULL) {
            continue;
        }
        cJSON_AddNumberToObject(t, "n", (double)(i + 1));
        cJSON_AddStringToObject(t, "task",
            (tasks[i].task != NULL) ? tasks[i].task : "");
        if (tasks[i].model_name != NULL) {
            cJSON_AddStringToObject(t, "model", tasks[i].model_name);
        }
        cJSON_AddNumberToObject(t, "write", (double)(tasks[i].write ? 1 : 0));
        cJSON_AddItemToArray(arr, t);
    }
    cJSON_AddItemToObject(o, "tasks", arr);
    s = jc_json_print(o);
    cJSON_Delete(o);
    if (s != NULL) {
        char path[JC_PATH_MAX];
        jc_snprintf(path, sizeof path, "%s/batch.json", base);
        jc_write_file(path, s, (jc_size)strlen(s));
        jc_make_private(path);
        free(s);
    }
}

/* M812 (P3): the manifest's task text + model for 1-based task index `n`. */
static void manifest_task(cJSON *mani, int n, const char **task,
                          const char **model)
{
    cJSON *arr;
    int i;
    int sz;

    *task = NULL;
    *model = NULL;
    if (mani == NULL) {
        return;
    }
    arr = cJSON_GetObjectItem(mani, "tasks");
    if (!cJSON_IsArray(arr)) {
        return;
    }
    sz = cJSON_GetArraySize(arr);
    for (i = 0; i < sz; i++) {
        cJSON *t = cJSON_GetArrayItem(arr, i);
        if (cJSON_IsObject(t) && jc_json_clamp_int(jc_json_get_num(t, "n", 0.0)) == n) {
            *task = jc_json_get_str(t, "task", NULL);
            *model = jc_json_get_str(t, "model", NULL);
            return;
        }
    }
}

/* M812 (P3): salvage every ORPHAN base under ~/.jichi.d/worktrees -- a `par-<run>`
 * whose `.lock` is FREE, meaning the run that made it has died (the lock dies with
 * its holder, M811). Each such base's `wt-*` worktrees are preserved as
 * `parallel-orphaned` (the manifest names their tasks) and then removed, so a dead
 * parent's work is recoverable via `jichi attempts`/`recover` instead of lost.
 *
 * Idempotent: a completed salvage REMOVES the base, so a second pass finds nothing
 * (two salvages -> one ref); and the fcntl lock we hold keeps two passes from
 * racing the same base. A base whose lock is BUSY is a LIVE run's -- left untouched.
 * Appends one line per salvaged state to `report`; returns the count. */
static int salvage_orphan_bases(struct jc_app *app, struct jc_sb *report)
{
    char wtroot[JC_PATH_MAX];
    char names[64][64];
    int nnames = 0;
    int total = 0;
    int b;
    DIR *d;
    struct dirent *e;

    if (app == NULL || app->snapshots == NULL ||
        !jc_snapshot_available(app->snapshots)) {
        return 0;
    }
    jc_snprintf(wtroot, sizeof wtroot, "%s/.jichi.d/worktrees", jc_home_dir());
    d = opendir(wtroot);
    if (d == NULL) {
        return 0;
    }
    /* Collect names first; do not mutate the directory while reading it. */
    while ((e = readdir(d)) != NULL && nnames < 64) {
        if (strncmp(e->d_name, "par-", 4) == 0 &&
            (int)strlen(e->d_name) < 64) {
            jc_snprintf(names[nnames], sizeof names[nnames], "%s", e->d_name);
            nnames++;
        }
    }
    closedir(d);

    for (b = 0; b < nnames; b++) {
        char base[JC_PATH_MAX];
        char lpath[JC_PATH_MAX];
        char mpath[JC_PATH_MAX];
        char wts[JC_PAR_MAX_TASKS][32];
        int nwt = 0;
        int lock;
        int w;
        char *mbuf = NULL;
        jc_size mlen = 0;
        cJSON *mani = NULL;
        const char *run = "";
        DIR *bd;

        jc_snprintf(base, sizeof base, "%s/%s", wtroot, names[b]);
        jc_snprintf(lpath, sizeof lpath, "%s/.lock", base);
        lock = jc_lock_take(lpath, 0);
        if (lock < 0) {
            continue;   /* BUSY (a live run) or NONE (cannot tell): leave it */
        }
        jc_snprintf(mpath, sizeof mpath, "%s/batch.json", base);
        if (jc_read_file(mpath, &mbuf, &mlen, jc_app_scratch(app)) == JC_OK &&
            mbuf != NULL) {
            mani = cJSON_Parse(mbuf);
        }
        if (mani != NULL) {
            const char *r = jc_json_get_str(mani, "run", NULL);
            if (r != NULL) {
                run = r;
            }
        }
        bd = opendir(base);
        if (bd != NULL) {
            while ((e = readdir(bd)) != NULL && nwt < JC_PAR_MAX_TASKS) {
                if (strncmp(e->d_name, "wt-", 3) == 0 &&
                    (int)strlen(e->d_name) < 32) {
                    jc_snprintf(wts[nwt], sizeof wts[nwt], "%s", e->d_name);
                    nwt++;
                }
            }
            closedir(bd);
        }
        for (w = 0; w < nwt; w++) {
            char wpath[JC_PATH_MAX];
            int idx = atoi(wts[w] + 3);   /* "wt-N" -> N (0-based task index) */
            const char *task = NULL;
            const char *model = NULL;
            struct jc_sb rec;

            manifest_task(mani, idx + 1, &task, &model);
            jc_snprintf(wpath, sizeof wpath, "%s/%s", base, wts[w]);
            jc_sb_init(&rec);
            jc_sb_append_fmt(&rec,
                "discarded: parallel-orphaned task %d (a run that did not finish)\n\n",
                idx + 1);
            jc_sb_append_fmt(&rec, "task:       %.200s\n",
                             (task != NULL) ? task : "(unknown -- no manifest)");
            jc_sb_append(&rec, "reason:     parallel-orphaned\n");
            if (model != NULL) {
                jc_sb_append_fmt(&rec, "model:      %s\n", model);
            }
            if (run[0] != '\0') {
                jc_sb_append_fmt(&rec, "run:        %s    task-run: %s-t%d\n",
                                 run, run, idx + 1);
                jc_sb_append_fmt(&rec, "transcript: attempts/%s/t%d.jsonl\n",
                                 run, idx + 1);
            }
            if (jc_snapshot_worktree_remove(app->snapshots, wpath,
                    "parallel-orphaned", rec.data) == JC_OK) {
                total++;
                if (report != NULL) {
                    jc_sb_append_fmt(report,
                        "- orphaned task %d (run %.12s): %s\n", idx + 1, run,
                        app->snapshots->preserve_last_ref);
                }
            }
            jc_sb_free(&rec);
        }
        if (mani != NULL) {
            cJSON_Delete(mani);
        }
        jc_lock_release(lock);
        remove(mpath);
        remove(lpath);
        rmdir(base);
    }
    return total;
}

/* M816 (P5): does a call of `tool` leave an effect OUTSIDE the resume worktree?
 * The 3-way merge replays a predecessor's in-tree file edits, so those must NOT be
 * listed in the resume note (the successor already has them). Everything else a
 * mutating tool does -- a shell command's side effects, network I/O, git-ref
 * writes, installs -- does NOT ride the merge, so the successor must be told it
 * already happened (R2, effect-classified). Read-only tools have no effect at all.
 * The registry is the source of truth for readonly; the in-tree editors are named
 * because their whole effect is the diff the merge carries. */
static int effect_is_outside_tree(struct jc_app *app, const char *tool)
{
    static const char *const in_tree[] = {
        "write_file", "create_file", "edit_file", "str_replace", "multi_edit",
        "format_file", "rename_symbol", "apply_code_action"
    };
    const struct jc_tool *td;
    size_t i;

    if (tool == NULL || tool[0] == '\0') {
        return 0;
    }
    td = (app->tools != NULL) ? jc_tool_registry_find(app->tools, tool) : NULL;
    if (td != NULL && td->readonly) {
        return 0;   /* a look-up changes nothing */
    }
    for (i = 0; i < sizeof in_tree / sizeof in_tree[0]; i++) {
        if (strcmp(tool, in_tree[i]) == 0) {
            return 0;   /* an in-tree edit rides the merge */
        }
    }
    return 1;
}

/* M816 (P5): compose the note prepended to a `from:` task -- the predecessor's
 * kept change is already in the successor's starting tree, and (effect-classified)
 * the outside-tree actions it already performed, so the successor does not repeat
 * them. Reads the kept ref's end record for the `transcript:` pointer, then walks
 * that JSONL for the tool calls with outside-tree effects. Appends to `out`; always
 * emits at least the "changes applied" line so the successor knows it is resuming.
 * Best-effort: a missing transcript (a --no-session predecessor) yields the header
 * line plus a note that the effect list is unavailable. */
static void build_resume_note(struct jc_app *app, const char *ref,
                              struct jc_sb *out)
{
    struct jc_sb rec;
    char *tbuf = NULL;
    jc_size tlen = 0;
    char rel[256];
    char tpath[JC_PATH_MAX];
    const char *ln;
    const char *tmark;
    int neffects = 0;

    /* M856 (N30): name what was applied, and never claim a change that was not:
     * before M856 this said "ALREADY APPLIED" when the pick had carried nothing. */
    {
        struct jc_sb chg;
        int nfiles = 0;
        const char *q;
        jc_sb_init(&chg);
        if (jc_snapshot_kept_changes(app->snapshots, ref, &chg) == JC_OK &&
            chg.data != NULL) {
            for (q = chg.data; *q != '\0'; q++) {
                if (*q == '\n') {
                    nfiles++;
                }
            }
        }
        if (nfiles > 0) {
            jc_sb_append_fmt(out,
                "[resume] You are continuing a previous attempt. Its file changes -- "
                "its own commits and what it left uncommitted, %d file(s) -- are "
                "ALREADY APPLIED to your starting tree (ref %.80s); do not re-make "
                "them. Build on them: re-run any builds, tests and gates yourself.\n",
                nfiles, ref);
        } else {
            jc_sb_append_fmt(out,
                "[resume] You are continuing a previous attempt (ref %.80s). Its kept "
                "state changed no files against the base it started from, so nothing "
                "was applied: your starting tree is the current tree. Check what it "
                "reported before you build on it.\n", ref);
        }
        jc_sb_free(&chg);
    }

    rel[0] = '\0';
    jc_sb_init(&rec);
    if (jc_snapshot_discarded_show(app->snapshots, ref, &rec) == JC_OK &&
        rec.data != NULL) {
        /* Pull `transcript: attempts/<run>/tK.jsonl` out of the end record. */
        tmark = strstr(rec.data, "transcript:");
        if (tmark != NULL) {
            const char *p = tmark + strlen("transcript:");
            size_t k = 0;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            while (*p != '\0' && *p != '\n' && *p != '\r' &&
                   k + 1 < sizeof rel) {
                rel[k++] = *p++;
            }
            rel[k] = '\0';
        }
    }
    jc_sb_free(&rec);

    if (rel[0] == '\0') {
        jc_sb_append(out,
            "[resume] (no transcript for this attempt; outside-tree effects "
            "unknown -- assume none were made.)\n");
        return;
    }
    jc_snprintf(tpath, sizeof tpath, "%s/.jichi.d/%s", jc_home_dir(), rel);
    if (jc_read_file(tpath, &tbuf, &tlen, jc_app_tool_scratch(app)) != JC_OK ||
        tbuf == NULL) {
        jc_sb_append(out,
            "[resume] (transcript unreadable; outside-tree effects unknown.)\n");
        return;
    }
    /* One JSON object per line: list each tool_call whose effect is outside the
     * tree, so the successor does not repeat it. tbuf is writable arena memory, so
     * NUL the newline in place to parse each line, then step past it. */
    ln = tbuf;
    while (ln != NULL && *ln != '\0') {
        char *nl = strchr((char *)ln, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        if (*ln != '\0') {
            cJSON *o = cJSON_Parse(ln);
            if (o != NULL) {
                const char *ev = jc_json_get_str(o, "event", NULL);
                if (ev != NULL && strcmp(ev, "tool_call") == 0) {
                    const char *nm = jc_json_get_str(o, "name", NULL);
                    if (effect_is_outside_tree(app, nm)) {
                        const char *ar = jc_json_get_str(o, "args", NULL);
                        if (neffects == 0) {
                            jc_sb_append(out,
                                "[resume] It already performed these "
                                "OUTSIDE-tree actions (done -- do not "
                                "repeat):\n");
                        }
                        jc_sb_append_fmt(out, "  - %s: %.160s\n",
                                         nm != NULL ? nm : "?",
                                         ar != NULL ? ar : "");
                        neffects++;
                    }
                }
                cJSON_Delete(o);
            }
        }
        ln = (nl != NULL) ? nl + 1 : NULL;
    }
    if (neffects == 0) {
        jc_sb_append(out,
            "[resume] It made no outside-tree actions; only file edits, which "
            "are already applied.\n");
    }
}

/* ---- the metric gate (M875) --------------------------------------------------
 * The demos drive's §9 (b), built beside M865's hold at the operator's decision of
 * 2026-10-02 ("so either option is configurable"). In "metric" mode, a write child
 * whose moved test assertions are ALL in declared pin files merges when the
 * declared metric, measured in its worktree, is not worse than the base's run of
 * the same command; otherwise it is kept. The base is measured once per batch and
 * only when a child needs it, in a fresh worktree of the batch's base (the live
 * tree stops being the base at the first merge). An unknown measurement holds.
 * Design and the rejected alternatives: docs/proposals/2026-10-metric-gate.md. */
struct metric_base {
    int tried;
    int known;
    double v;
    char why[200];
};

static int metric_gate_on(const struct jc_app *app)
{
    return app->config.parallel_test_edits == JC_TESTEDITS_METRIC &&
           app->config.parallel_metric_cmd != NULL &&
           app->config.parallel_metric_pins.len > 0;
}

/* Run the declared metric in `dir`: 1 with *v set, or 0 with `why` saying what
 * failed -- never a number guessed from a failed run. */
static int metric_measure(struct jc_app *app, const char *dir, double *v,
                          char *why, jc_size cap)
{
    struct jc_sb out;
    int code;
    long tmo = app->config.parallel_metric_timeout > 0
                   ? app->config.parallel_metric_timeout
                   : ((app->env != NULL) ? app->env->verify_timeout : 0);
    const char *label = app->config.parallel_metric_label;

    jc_sb_init(&out);
    code = jc_env_run_verify(app->config.parallel_metric_cmd, dir, &out,
                             &app->abort_flag, tmo);
    if (code == -2) {
        jc_snprintf(why, cap, "the metric command timed out or was stopped");
    } else if (code == -1) {
        jc_snprintf(why, cap, "the metric command could not be run");
    } else if (code != 0) {
        jc_snprintf(why, cap, "the metric command exited %d", code);
    } else if (!jc_parallel_metric_parse(out.data != NULL ? out.data : "",
                                         label, v)) {
        if (label != NULL) {
            jc_snprintf(why, cap, "no number after \"%s\" in its output", label);
        } else {
            jc_snprintf(why, cap, "no number in its output");
        }
    } else {
        jc_sb_free(&out);
        return 1;
    }
    jc_sb_free(&out);
    return 0;
}

static void metric_base_measure(struct jc_app *app, const char *base,
                                const char *wtbase, struct metric_base *mb)
{
    char path[JC_PATH_MAX];
    mb->tried = 1;
    jc_snprintf(path, sizeof path, "%s/wt-metric-base", wtbase);
    if (jc_snapshot_worktree_add(app->snapshots, base, path) != JC_OK) {
        jc_snprintf(mb->why, sizeof mb->why,
                    "a worktree of the base could not be made");
        return;
    }
    mb->known = metric_measure(app, path, &mb->v, mb->why, sizeof mb->why);
    /* pristine base content: nothing to preserve */
    (void)jc_snapshot_worktree_remove(app->snapshots, path, JC_WT_MERGED, NULL);
}

static void metric_record(struct jc_app *app, int task, const char *verdict,
                          const struct metric_base *mb, int vknown, double v)
{
    cJSON *o = jc_app_telem_begin(app, "metric_gate");
    cJSON *j;
    if (o != NULL) {
        cJSON_AddNumberToObject(o, "task", (double)task);
        cJSON_AddStringToObject(o, "verdict", verdict);
        if (mb->known) cJSON_AddNumberToObject(o, "base", mb->v);
        if (vknown) cJSON_AddNumberToObject(o, "value", v);
    }
    jc_eventlog_end(app->telemetry, o);
    j = jc_env_journal_begin(app->env, "metric_gate");
    if (j != NULL) {
        cJSON_AddNumberToObject(j, "task", (double)task);
        cJSON_AddStringToObject(j, "verdict", verdict);
        if (mb->known) cJSON_AddNumberToObject(j, "base", mb->v);
        if (vknown) cJSON_AddNumberToObject(j, "value", v);
    }
    jc_env_journal_end(app->env, j);
}

/* Decide a child whose moved assertions are all in pin files: 1 merges it (the
 * note says what the metric read), 0 keeps it (note and reason set). */
static int metric_gate(struct jc_app *app, int i, struct ptask *t,
                       const char *base, const char *wtbase,
                       struct metric_base *mb, const char *held)
{
    double v = 0.0;
    int vknown = 0;
    char why[200];
    char bs[48];
    char vs[48];
    const char *better = app->config.parallel_metric_higher ? "higher" : "lower";

    if (!mb->tried) {
        board_line(app, i, t->model_name, JC_BOARD_RUN, "metric (base)",
                   t->tokens);
        metric_base_measure(app, base, wtbase, mb);
    }
    if (!mb->known) {
        t->preserve_why = "parallel-test-edit";
        jc_snprintf(t->note, sizeof t->note,
            "[not merged: this task changed an existing test assertion in a pin "
            "file (%s), and the metric could not be measured on the base: %s -- "
            "its work is kept, see Kept]", held, mb->why);
        metric_record(app, i + 1, "unknown", mb, 0, 0.0);
        return 0;
    }
    board_line(app, i, t->model_name, JC_BOARD_RUN, "metric (worktree)",
               t->tokens);
    vknown = metric_measure(app, t->wt, &v, why, sizeof why);
    if (!vknown) {
        t->preserve_why = "parallel-test-edit";
        jc_snprintf(t->note, sizeof t->note,
            "[not merged: this task changed an existing test assertion in a pin "
            "file (%s), and the metric could not be measured in its worktree: %s "
            "-- its work is kept, see Kept]", held, why);
        metric_record(app, i + 1, "unknown", mb, 0, 0.0);
        return 0;
    }
    jc_parallel_metric_fmt(mb->v, bs, sizeof bs);
    jc_parallel_metric_fmt(v, vs, sizeof vs);
    if (jc_parallel_metric_worse(mb->v, v, app->config.parallel_metric_higher)) {
        t->preserve_why = "parallel-metric";
        jc_snprintf(t->note, sizeof t->note,
            "[not merged: this task changed an existing test assertion in a pin "
            "file (%s), and the metric fell: base %s, this task %s (%s is "
            "better) -- its work is kept, see Kept]", held, bs, vs, better);
        metric_record(app, i + 1, "held", mb, 1, v);
        return 0;
    }
    jc_snprintf(t->note, sizeof t->note,
        "[merged: this task changed an existing test assertion in a pin file "
        "(%s), and the metric did not fall: base %s, this task %s (%s is "
        "better)]", held, bs, vs, better);
    metric_record(app, i + 1, "merged", mb, 1, v);
    return 1;
}

/* M1006-b: free each task's per-task heap members (the resolution loop's
 * `error` strings for invalid tasks, plus everything the pool fills in). The
 * normal end-of-function path and the M1006 refuse early-return both call it:
 * before M1006-b the refuse return skipped this and leaked the invalid tasks'
 * `error` strings (bounded, one per refused batch). */
static void ptask_free_members(struct ptask *tasks, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (tasks[i].launched) {
            jc_sb_free(&tasks[i].out);
        }
        if (tasks[i].answer != NULL) {
            free(tasks[i].answer);
        }
        if (tasks[i].error != NULL) {
            free(tasks[i].error);
        }
        free(tasks[i].last);             /* M854 */
        if (tasks[i].task_owned != NULL) {
            free(tasks[i].task_owned);   /* M816 */
        }
    }
}

static jc_status parallel_run(const cJSON *args, struct jc_tool_result *out,
                              struct jc_app *app)
{
    cJSON *tasks_json = cJSON_GetObjectItem(args, "tasks");
    cJSON *e;
    struct ptask tasks[JC_PAR_MAX_TASKS];
    int ntasks = 0;
    int truncated = 0;
    int any_write = 0;
    int have_base = 0;
    int cap;
    int aborted = 0;
    int i;
    int applied = 0;
    int n_quarantined = 0;   /* M144: red-verify write children not merged */
    int on_start = 0;        /* M893: this batch's base is the run's starting tree */
    int n_ok = 0;
    char base[64];
    char wtbase[1100];
    char fence_note[900];   /* M852: shellFenceAllow entries that grant nothing */
    char dirty_note[900];   /* M1006: a write dispatch started from a dirty tree */
    int dd_unknown_note = 0;/* M1006-a: the probe FAILED (unknown, not clean)   */
    char run_id[96];   /* M807/M808: this batch's id (transcripts + end records);
                        * M860: "<base>-b<k>", one per batch */
    int  base_lock = -3; /* M811: fcntl lock fd on <base>/.lock; <0 = none held */
    struct jc_sb agg;
    struct jc_sb kept;   /* M809: one line per preserved state, for the result top */
    int nkept = 0;       /* M809 */
    struct jc_sb salvaged; /* M812: one line per orphan state recovered on start */
    int nsalvaged = 0;     /* M812 */
    struct jc_sb stash_earlier; /* M855: stash entries set aside at the start */
    struct jc_sb stash_left;    /* M855: stash entries the children left       */
    int nstash_earlier = 0;     /* M855 */
    int nstash_left = 0;        /* M855 */
    struct jc_sb conflicts;
    struct jc_sb reports;    /* def-332 (M1044): stored root reports, by task */
    struct jc_sb vfails;     /* M144: per-task verifier output tails */
    struct metric_base mbase; /* M875: the metric gate's base, measured once */
    struct jc_vec seen;
    double tok_sum = 0.0;
    int tool_sum = 0;

    if (!jc_subagent_can_spawn(app->agent_depth,
                               app->config.max_subagent_depth)) {
        tu_err(out, "error: sub-agent depth limit reached");
        return JC_OK;
    }
    if (!cJSON_IsArray(tasks_json)) {
        tu_err(out, "error: 'tasks' must be an array of {task, ...} objects");
        return JC_OK;
    }

    /* Resolve each task up front so bad models/profiles are reported, not run. */
    memset(tasks, 0, sizeof(tasks));
    for (e = tasks_json->child; e != NULL; e = e->next) {
        struct ptask *t;
        const char *task_str;
        const char *arg_model;
        const char *agent_name;
        const struct jc_agentdef *def = NULL;
        const char *model_sel;
        int ro_ignored;
        int found = 1;

        if (ntasks >= JC_PAR_MAX_TASKS) {
            truncated = 1;
            break;
        }
        t = &tasks[ntasks];
        t->fd = -1;
        t->tokens = 0.0;
        t->tool_budget = -1; /* no cap unless the envelope reserves one below */

        task_str = (cJSON_IsObject(e)) ? jc_json_get_str(e, "task", NULL) : NULL;
        if (task_str == NULL || task_str[0] == '\0') {
            t->invalid = 1;
            t->error = jc_strdup("missing 'task'");
            t->task = "(invalid)";
            ntasks++;
            continue;
        }
        t->task = task_str; /* owned by args' cJSON tree (lives this call) */
        arg_model = jc_json_get_str(e, "model", NULL);
        agent_name = jc_json_get_str(e, "agent", NULL);
        t->write = (cJSON_IsTrue(cJSON_GetObjectItem(e, "write"))) ? 1 : 0;
        /* M890 (the demos drive's N48; the operator's decision of 2026-10-03, "Build
         * `"merge": false`"): a probe's work is evidence, not a change. Read leniently
         * -- a model writes it, and `"false"` must not merge a probe (N17). */
        t->no_merge = jc_json_get_bool_lenient(e, "merge", 1) ? 0 : 1;
        /* M816 (P5): `from:"<ref>"` resumes a kept attempt. Resuming means
         * building on a prior worktree's changes, so it implies write -- the
         * kept diff is applied into an isolated worktree the child then extends. */
        t->from = jc_json_get_str(e, "from", NULL);
        if (t->from != NULL && t->from[0] != '\0') {
            t->write = 1;
        } else {
            t->from = NULL;
        }
        /* M861 (N28): the lead's tool-call budget for this task, read leniently
         * (a number, or a string of digits). Any other value refuses the task:
         * a lead must never believe a budget binds that does not -- the strict
         * read of `write` made that mistake silently (N17). */
        t->max_calls = 0;
        if (jc_parallel_parse_max_calls(cJSON_GetObjectItem(e, "max_tool_calls"),
                                        &t->max_calls) < 0) {
            t->invalid = 1;
            t->error = jc_strdup("max_tool_calls must be a whole number of tool "
                                 "calls, 1 or more");
            ntasks++;
            continue;
        }

        if (agent_name != NULL && agent_name[0] != '\0') {
            def = jc_agentdef_find(&app->agents, agent_name);
            if (def == NULL) {
                t->invalid = 1;
                t->error = jc_strdup("no such agent profile");
                ntasks++;
                continue;
            }
        }
        jc_agentdef_merge(def, arg_model, 0, 0, &model_sel, &ro_ignored);
        jc_subagent_resolve_model(&app->config, model_sel, &found);
        if (!found) {
            t->invalid = 1;
            t->error = jc_strdup("no model matches the selector");
            ntasks++;
            continue;
        }
        t->def = def;
        t->model_sel = model_sel;
        if (t->write) {
            any_write = 1;
        }
        ntasks++;
    }

    if (ntasks == 0) {
        tu_err(out, "error: no tasks provided");
        return JC_OK;
    }

    /* M1006: the dirty-tree dispatch guard, on the WRITE half of a batch.
     *
     * UNIVERSE: only write batches are checked. `any_write` is known now (the
     * task-resolution loop above set it), and this runs BEFORE the first fork
     * (the worktree allocation below), so a `refuse` stops the batch before any
     * child's base is taken. READ-ONLY DISPATCHES ARE NOT CHECKED in any mode:
     * a batch with no write task changes nothing -- no fork, no merge -- so it
     * is outside the guard's stated universe, and the check reads the LIVE
     * workspace (`git -C '<root>'`), never the shadow snapshot repo.
     *
     * `off` skips the whole check -- no probe, no telemetry event (the universe
     * rule, and the escape hatch for a deliberately-dirty tree). */
    dirty_note[0] = '\0';
    if (any_write) {
        int dd_mode = app->config.strictness.dirty_dispatch;
        int dd_state = JC_DISPATCH_CLEAN;
        int dd_npaths = 0;
        char dd_paths[512];
        if (dd_mode == JC_DIRTYDISPATCH_OFF) {
            /* unchecked: no probe, no event -- the escape hatch. */
        } else {
            cJSON *de;
            jc_dispatch_dirty_probe(app->root, &dd_state, &dd_npaths, dd_paths,
                                    sizeof dd_paths, 5);
            de = jc_app_telem_begin(app, "dirty_dispatch");
            if (de != NULL) {
                cJSON_AddStringToObject(de, "mode",
                                        dd_mode == JC_DIRTYDISPATCH_REFUSE
                                            ? "refuse" : "warn");
                /* The dirty field is a STRING: yes / no / unknown. A failed
                 * probe (UNKNOWN) is never "no" -- the classifier's
                 * else-branch says unknown (M519/M628). */
                cJSON_AddStringToObject(de, "dirty",
                                        dd_state == JC_DISPATCH_DIRTY
                                            ? "yes"
                                            : dd_state == JC_DISPATCH_UNKNOWN
                                            ? "unknown" : "no");
                cJSON_AddNumberToObject(de, "npaths", dd_npaths);
            }
            if (dd_state == JC_DISPATCH_DIRTY) {
                if (dd_mode == JC_DIRTYDISPATCH_REFUSE) {
                    char msg[1000];
                    if (de != NULL) {
                        cJSON_AddStringToObject(de, "outcome", "refused");
                    }
                    jc_eventlog_end(app->telemetry, de);
                    jc_snprintf(msg, sizeof msg,
                                "error: the workspace is dirty before a write "
                                "dispatch (uncommitted: %s). commit, then "
                                "dispatch -- a child forks a worktree off the "
                                "current HEAD and merges back, so the base must "
                                "be a committed tree, or its uncommitted work "
                                "is mixed in with the child's.",
                                dd_paths[0] != '\0' ? dd_paths : "(see git status)");
                    tu_err_policy(out, msg);
                    ptask_free_members(tasks, ntasks); /* M1006-b: no error leak */
                    return JC_OK;
                }
                if (de != NULL) {
                    cJSON_AddStringToObject(de, "outcome", "proceeded");
                }
                jc_eventlog_end(app->telemetry, de);
                jc_snprintf(dirty_note, sizeof dirty_note, "%s", dd_paths);
            } else if (dd_state == JC_DISPATCH_UNKNOWN) {
                /* M1006-a: the probe FAILED -- the tree state is unknown, not
                 * clean. The batch proceeds (an infra failure must not block
                 * work) with the unknown note; the outcome says so. */
                if (de != NULL) {
                    cJSON_AddStringToObject(de, "outcome", "proceeded_unknown");
                }
                jc_eventlog_end(app->telemetry, de);
                dd_unknown_note = 1;
            } else if (de != NULL) {
                cJSON_AddStringToObject(de, "outcome", "proceeded");
                jc_eventlog_end(app->telemetry, de);
            }
        }
    }

    /* Pre-warm the workspace index so read-only children share it (incremental;
     * a no-op when no embed model is configured or it is already current). */
    {
        struct jc_model_cfg *em = jc_app_model_for_role(app, JC_ROLE_EMBED);
        if (em != NULL) {
            jc_index_build(app->cwd, em, 0, NULL, 0, &app->index, NULL,
                           &app->abort_flag, &app->config.ignore_dirs);
        }
    }

    /* M812 (P3): before this batch makes its own base, salvage any ORPHAN bases a
     * DEAD parent left behind -- a par-<run> whose fcntl lock is free. Their
     * worktrees are preserved as `parallel-orphaned` and removed, so a dead run's
     * work is recovered rather than lost (and does not pile up). Runs before this
     * batch's own base exists, so it can never salvage itself. */
    jc_sb_init(&salvaged);
    nsalvaged = salvage_orphan_bases(app, &salvaged);
    /* M855 (N34): the shadow repo's stash stack is ONE stack for every child's
     * worktree (measured, git 2.43: one worktree's `git stash pop` applied
     * another's entry), and an entry outlives the batch that made it. Set aside
     * whatever is on it -- an earlier batch's, or one from before M855 -- so no
     * child of this batch pops it into its worktree. */
    jc_sb_init(&stash_earlier);
    jc_sb_init(&stash_left);
    if (any_write) {
        nstash_earlier = jc_snapshot_stash_keep(app->snapshots,
                                                "parallel-stash-earlier",
                                                &stash_earlier);
    }

    /* For write tasks, checkpoint the current tree and give each its own
     * worktree off that base. Degrade to read-only if snapshots are absent. */
    base[0] = '\0';
    /* M807/M808: one run id for this batch -- names the child transcripts
     * (~/.jichi.d/attempts/<run>/) and appears in each preserved end record.
     * Correlate with the telemetry sid (hence the session), then the envelope run
     * id, and only otherwise mint one; computed once so children and the teardown
     * agree. */
    {
        /* M860: the base -- a nested batch's is its child's own id, the
         * lead's the session -- then this process's batch number. */
        char rbase[72];
        if (s_task_id[0] != '\0') {
            jc_snprintf(rbase, sizeof rbase, "%s", s_task_id);
        } else if (app->telemetry != NULL && app->telemetry->sid[0] != '\0') {
            jc_snprintf(rbase, sizeof rbase, "%s", app->telemetry->sid);
        } else if (app->env != NULL && app->env->run_id != NULL &&
                   app->env->run_id[0] != '\0') {
            jc_snprintf(rbase, sizeof rbase, "%s", app->env->run_id);
        } else {
            jc_uuid_v4(rbase);
        }
        s_batch_no++;
        jc_snprintf(run_id, sizeof run_id, "%s-b%d", rbase, s_batch_no);
    }
    /* M860: this batch's children take the next seq bands, in task order. */
    for (i = 0; i < ntasks; i++) {
        tasks[i].band = s_next_band + (long)i;
    }
    s_next_band += (long)ntasks;
    if (any_write && jc_snapshot_available(app->snapshots)) {
        const char *c0;
        jc_snapshot_take(app->snapshots, "parallel base");
        c0 = jc_snapshot_commit(app->snapshots,
                                jc_snapshot_count(app->snapshots) - 1);
        if (c0 != NULL) {
            jc_snprintf(base, sizeof(base), "%s", c0);
            jc_snprintf(s_batch_base, sizeof s_batch_base, "%s", c0); /* M863 */
            have_base = 1;
        }
        /* M893 (N52 (b)): is the base still the tree the run started from? By
         * content where git can tell (HEAD included), else by "the lead has run
         * no call that could change a file". Only then is a worktree verify's
         * failure evidence about the starting tree. */
        if (app->env != NULL && app->env->pre_edit_fail[0] == '\0') {
            char sfp[72];
            if (app->env->start_fp_ok) {
                on_start = (jc_snapshot_worktree_fingerprint(app->root, sfp,
                                                             sizeof sfp) == JC_OK &&
                            strcmp(sfp, app->env->start_fp) == 0);
            } else {
                on_start = (app->env->changing_executed == 0);
            }
        }
    }
    /* M814 (P4b): an UNTHROTTLED disk check before the disk-heavy worktree
     * allocation, on the filesystem the worktrees live on (~/.jichi.d, which may
     * differ from the workspace). Below the floor, don't allocate -- the children
     * run read-only and the run stops at the next agent-loop boundary. */
    if (have_base && jc_diskguard_check(&app->diskguard, jc_home_dir(), 1,
                                        jc_now_millis())) {
        fprintf(stderr, "[jichi] %s -- not allocating parallel worktrees; the "
                "run will stop.\n", app->diskguard.why);
        have_base = 0;
        app->abort_flag = 1;
    }
    /* M852 (N36), the operator's decision of 2026-10-01, its second half: a
     * shellFenceAllow entry that is not an absolute path, or does not exist when
     * the batch starts, grants nothing -- add_beneath skips a path that will not
     * open, so the fence holds and that directory is simply not writable. That was
     * silent (measured: no line anywhere). Say it ONCE, at the top of the batch
     * result, and once in the log, rather than in every child. Only when the fence
     * will be applied: without Landlock the list is moot. A path that exists but is
     * not a directory is the child's case A (the kernel refuses its rule; the task
     * is refused, naming it). */
    fence_note[0] = '\0';
    if (have_base && app->config.shell_fence != 0 && jc_landlock_abi() >= 1) {
        jc_size fi;
        jc_size fn = 0;
        for (fi = 0; fi < app->config.shell_fence_allow.len; fi++) {
            const char *fp =
                *(char **)jc_vec_at(&app->config.shell_fence_allow, fi);
            struct stat fst;
            const char *what = NULL;
            if (fp == NULL || fp[0] == '\0') {
                continue;
            }
            if (fp[0] != '/') {
                what = "is not an absolute path";
            } else if (stat(fp, &fst) != 0) {
                what = "does not exist";
            }
            if (what != NULL && fn + 200 < sizeof fence_note) {
                fn += (jc_size)jc_snprintf(fence_note + fn,
                                           sizeof fence_note - fn,
                                           "%s\"%s\" %s", fn > 0 ? "; " : "",
                                           fp, what);
            }
        }
        if (fence_note[0] != '\0') {
            jc_logf(JC_LOG_WARN, "shellFenceAllow: %s -- such an entry grants "
                    "nothing, so a write child's shell cannot write there",
                    fence_note);
        }
    }
    if (have_base) {
        char lpath[JC_PATH_MAX];
        /* M811 (P3): name the base by RUN ID, not pid. In a container every lead
         * is the same pid, so a pid-named base let a DEAD run's orphan collide
         * with the next run's -- the confirmed G3 defect. A run id is unique per
         * invocation, so bases never collide. Hold an fcntl lock on <base>/.lock
         * for the whole batch: it dies with this process, so an orphan base left
         * by a dead parent has a FREE lock, which is how salvage (M812) tells a
         * dead run's trees from a live one's. CLOEXEC so the lock fd does not leak
         * into the shell commands the children run. The manifest is written first
         * (before the worktrees), so a crash between them still records the tasks. */
        jc_snprintf(wtbase, sizeof(wtbase), "%s/.jichi.d/worktrees/par-%s",
                    jc_home_dir(), run_id);
        jc_mkdir_p_private(wtbase);   /* creates it 0700, M488 */
        jc_snprintf(lpath, sizeof lpath, "%s/.lock", wtbase);
        base_lock = jc_lock_take(lpath, 0);
        if (base_lock >= 0) {
            (void)fcntl(base_lock, F_SETFD, FD_CLOEXEC);
        }
        write_batch_manifest(wtbase, run_id, tasks, ntasks);
        for (i = 0; i < ntasks; i++) {
            if (tasks[i].invalid || !tasks[i].write) {
                continue;
            }
            jc_snprintf(tasks[i].wt, sizeof(tasks[i].wt), "%s/wt-%d",
                        wtbase, i);
            if (jc_snapshot_worktree_add(app->snapshots, base, tasks[i].wt)
                    != JC_OK) {
                tasks[i].wt[0] = '\0';
                tasks[i].write = 0;
                jc_snprintf(tasks[i].note, sizeof(tasks[i].note),
                            "(worktree unavailable; ran read-only)");
                continue;
            }
            /* M852 (N36): the child's private temp dir -- see worktree_tmpdir. */
            worktree_tmpdir(tasks[i].wt, tasks[i].tmpdir,
                            sizeof tasks[i].tmpdir);
            /* M816 (P5): a `from:` task resumes a kept state -- 3-way apply that
             * state's change onto this fresh worktree, then prepend the effect-
             * classified predecessor note to the task text. A conflict (or a bad
             * ref) is a TOOL-ERROR VALUE the model reads, never a crash: mark the
             * task invalid so it is not launched, and drop write so teardown
             * removes the (now base-clean) worktree without preserving it. */
            if (tasks[i].from != NULL) {
                struct jc_sb cf;
                jc_status rs;
                jc_sb_init(&cf);
                rs = jc_snapshot_resume_apply(app->snapshots, tasks[i].wt,
                                              tasks[i].from, &cf);
                if (rs == JC_OK) {
                    struct jc_sb note;
                    jc_sb_init(&note);
                    build_resume_note(app, tasks[i].from, &note);
                    if (note.data != NULL && tasks[i].task != NULL) {
                        struct jc_sb full;
                        jc_sb_init(&full);
                        jc_sb_append(&full, note.data);
                        jc_sb_append(&full, "\n");
                        jc_sb_append(&full, tasks[i].task);
                        tasks[i].task_owned = jc_sb_finish(&full);
                        if (tasks[i].task_owned != NULL) {
                            tasks[i].task = tasks[i].task_owned;
                        }
                        jc_sb_free(&full);
                    }
                    jc_sb_free(&note);
                } else {
                    tasks[i].invalid = 1;
                    tasks[i].write = 0;
                    if (rs == JC_ERR_INVALID) {
                        char msg[512];
                        jc_snprintf(msg, sizeof msg,
                            "resume conflict applying %.80s onto the current "
                            "base -- conflicting paths:\n%.300s",
                            tasks[i].from,
                            cf.data != NULL ? cf.data : "(unknown)");
                        tasks[i].error = jc_strdup(msg);
                    } else {
                        char msg[256];
                        jc_snprintf(msg, sizeof msg,
                            "resume failed: no such kept ref %.120s",
                            tasks[i].from);
                        tasks[i].error = jc_strdup(msg);
                    }
                }
                jc_sb_free(&cf);
            }
        }
    } else if (any_write) {
        for (i = 0; i < ntasks; i++) {
            if (tasks[i].write) {
                tasks[i].write = 0;
                /* M816: a `from:` resume needs a worktree; with snapshots off it
                 * cannot run at all (a read-only fallback would silently drop the
                 * kept change), so report it as an error rather than degrade. */
                if (tasks[i].from != NULL) {
                    tasks[i].invalid = 1;
                    tasks[i].error = jc_strdup(
                        "resume requires snapshots, which are unavailable here");
                } else {
                    jc_snprintf(tasks[i].note, sizeof(tasks[i].note),
                                "(snapshots unavailable; ran read-only)");
                }
            }
        }
    }

    cap = eff_max_parallel(app, ntasks);

    /* Reserve each child a 1/ntasks slice of the run's remaining budget (M62
     * #4): an upper bound that holds however the pool refills, since the parent
     * only learns real usage after the pool exits. Children inherit start_time,
     * so the deadline is shared.
     *
     * M431: these slices are now ENFORCED. Until then this comment claimed the
     * children self-checked, and they could not: jc_env_over_budget sat behind
     * env_active(), which requires agent_depth == 0, while run_child starts a
     * child at depth 1 -- so every slice computed here was applied to the
     * child's COW envelope and never once consulted, leaving the per-child
     * watchdog as the only real bound. env_budget_applies() (jc_agent.c) is the
     * predicate that fixed it. */
    if (app->env != NULL && ntasks > 0) {
        double rem_tok = (app->env->budget_tokens > 0.0)
            ? app->env->budget_tokens - app->env->tokens_used : 0.0;
        int rem_tools = (app->env->max_tool_calls > 0)
            ? app->env->max_tool_calls - app->env->tool_calls : -1;
        for (i = 0; i < ntasks; i++) {
            if (app->env->budget_tokens > 0.0) {
                tasks[i].tok_budget = (rem_tok > 0.0) ? rem_tok / ntasks : 0.0;
            }
            if (rem_tools >= 0) {
                /* Distribute the remainder so the slices sum to exactly
                 * rem_tools (no N-times overspend) AND a near-exhausted budget
                 * (rem_tools < ntasks) still lets the first rem_tools children
                 * make a call each, instead of truncating every slice to 0 and
                 * handing each child a zero tool budget. */
                tasks[i].tool_budget =
                    rem_tools / ntasks + (i < rem_tools % ntasks ? 1 : 0);
            }
        }
    }

    {
        long ttimeout = (app->config.parallel_task_timeout > 0)
            ? app->config.parallel_task_timeout
            : JC_PAR_TASK_TIMEOUT_DEFAULT;
        run_pool(app, tasks, ntasks, cap, ttimeout * 1000L, &aborted, run_id);
    }

    /* M854 (N33, N24(c)): a write child that did not end on its own -- stopped
     * from outside, out of context, or capped -- answers with a note instead of
     * ending "without a result". Its work is committed in its worktree, and such
     * a child has often finished (N33: run 0087's card-204 worker committed its
     * fix, looped, and was stopped twelve minutes later; N24: two of four
     * overflowed children had committed finished work). The note says the text
     * is not an answer the child chose to give, and the merge loop below lets
     * such a child merge only after a green worktree verify. */
    for (i = 0; i < ntasks; i++) {
        struct ptask *t = &tasks[i];
        struct jc_sb nb;
        t->ending = jc_parallel_ending(t->write, t->wt[0] != '\0', t->finished,
                                       aborted, t->stop, t->term, t->overflow,
                                       t->cut);
        /* M869 (def-305): a loop stop is reported as one, not as the cap. */
        if (t->ending != NULL && t->looped > 0 && strcmp(t->stop, "max_iters") == 0) {
            const char *le = jc_parallel_loop_ending(t->looped);
            if (le != NULL) {
                t->ending = le;
            }
        }
        if (t->ending == NULL) {
            continue;
        }
        jc_sb_init(&nb);
        if (t->answer != NULL && t->answer[0] != '\0') {
            jc_sb_append(&nb, t->answer);
            jc_sb_append(&nb, "\n\n");
        } else if (t->last != NULL && t->last[0] != '\0') {
            jc_sb_append(&nb, t->last);
            jc_sb_append(&nb, "\n\n");
        }
        /* M861: a task stopped at its max_tool_calls is counted as its budget
         * counted, its delegates' calls included. */
        if (strcmp(t->stop, "task_budget") == 0 && t->tused > t->tool_calls) {
            jc_sb_append_fmt(&nb, "[jichi] This task %s after %d tool call(s), "
                             "its delegates' included", t->ending, t->tused);
        } else {
            jc_sb_append_fmt(&nb, "[jichi] This task %s after %d tool call(s)",
                             t->ending, t->tool_calls);
        }
        if (t->cut > 1 && strstr(t->ending, "output ceiling") != NULL) {
            jc_sb_append_fmt(&nb, " (the ceiling: %d output tokens)", t->cut);
        }
        /* M861 (N28): which budget, in the lead's own terms. */
        if (strcmp(t->stop, "task_budget") == 0 && t->max_calls > 0) {
            jc_sb_append_fmt(&nb, " (max_tool_calls: %d)", t->max_calls);
        } else if (strcmp(t->stop, "budget") == 0 && t->tool_budget >= 0 &&
                   t->tool_calls >= t->tool_budget) {
            jc_sb_append_fmt(&nb, " (its share of the run's tool calls: %d)",
                             t->tool_budget);
        }
        jc_sb_append(&nb, ". Any text above is its last message, not an answer it "
                     "chose to give; ");
        jc_sb_append(&nb, t->no_merge
            ? "its work is kept and not merged (\"merge\": false)."   /* M890 */
            : "its work merges only after a green worktree verify (M854).");
        free(t->answer);
        t->answer = jc_strdup(nb.data != NULL ? nb.data : "");
        free(t->error);
        t->error = NULL;
        jc_sb_free(&nb);
    }

    /* run_pool's streaming gather already set answer/error/tokens from each
     * child's "done" message (or an error on premature EOF); this is just a
     * final backstop. */
    for (i = 0; i < ntasks; i++) {
        if (tasks[i].launched && tasks[i].error == NULL &&
            tasks[i].answer == NULL) {
            tasks[i].error = jc_strdup("no result from sub-agent");
        }
        tok_sum += tasks[i].tokens;
        tool_sum += tasks[i].tool_calls;
        if (tasks[i].answer != NULL) {
            n_ok++;
        }
    }
    if (app->env != NULL) {
        /* Reconcile the children's real usage into the parent's budget: tokens
         * AND tool calls (M62 #3). Forked children meter into their COW copy of
         * the envelope, invisible to the parent, so without this merge a swarm
         * could evade max_tool_calls entirely. The parent's own struct was never
         * mutated by the children (COW writes stayed in their address spaces). */
        app->env->tokens_used += tok_sum;
        app->env->tool_calls += tool_sum;
    }
    /* M861: a batch started inside a task spends that task's own budget too. */
    app->task_tool_used += tool_sum;

    /* Merge write tasks' file changes: first-wins by path, conflicts reported.
     * Only successful (answered) write children are merged. */
    jc_vec_init(&seen, sizeof(char *));
    jc_sb_init(&conflicts);
    jc_sb_init(&reports);
    jc_sb_init(&vfails);
    memset(&mbase, 0, sizeof mbase);
    for (i = 0; i < ntasks; i++) {
        struct ptask *t = &tasks[i];
        struct jc_sb chg;
        struct jc_vec changes;
        jc_size k;
        int had_conflict = 0;   /* M803: any of this task's changes not merged */
        struct newdir nd[JC_PAR_NEWDIR_MAX];   /* M888: its new directories */
        int nnd = 0;
        int nd_more = 0;
        int n_held_paths = 0;                  /* M888: paths kept out */

        if (!t->write || t->wt[0] == '\0') {
            continue;
        }
        /* M890 (N48): "merge": false keeps the task's work, whatever its ending
         * and before any verify. Run 0104's two capped research probes merged
         * code after a green verify -- three files, then one of two -- and the
         * lead reverted both by hand; the verify asks whether the tests pass,
         * not whether the task was meant to change code. */
        if (t->no_merge) {
            cJSON *o;
            t->preserve_why = "parallel-no-merge";
            jc_snprintf(t->note, sizeof t->note,
                "[not merged: this task was set \"merge\": false -- its work is "
                "kept, see Kept; read it with `jichi attempts show <ref>`, or take "
                "it on purpose; the ref lives in jichi's own attempt store under "
                "`$HOME/.jichi.d`, not in this workspace, so `git show`/`git log` "
                "here will not resolve it]");
            o = jc_app_telem_begin(app, "no_merge_kept");
            if (o != NULL) {
                cJSON_AddNumberToObject(o, "task", (double)(i + 1));
            }
            jc_eventlog_end(app->telemetry, o);
            continue;
        }
        if (t->answer == NULL) {
            continue;
        }
        /* M854: a child that did not end on its own merges only after a green
         * worktree verify. With no verify -- parallelVerify off, or no verifier
         * -- its work is KEPT: the Kept list at the top names its ref and how to
         * recover it. Before M854 a capped child that had written text merged
         * here unverified; one that had not was kept. */
        if (t->ending != NULL &&
            (!app->config.parallel_verify ||
             jc_parallel_verify_cmd(app->env != NULL ? app->env->verify_cmd : NULL,
                                    app->config.verify,
                                    app->config.test_command) == NULL)) {
            t->preserve_why = "parallel-unverified";
            jc_snprintf(t->note, sizeof t->note,
                        "[not merged: this task did not end on its own and no "
                        "worktree verify ran -- its work is kept; see Kept]");
            continue;
        }
        /* Per-child verify gate (M144, opt-in `parallelVerify`): run the
         * configured verifier IN THE CHILD'S WORKTREE before its changes may
         * merge. A red child is quarantined -- reported, not merged -- so a
         * sibling's (or the parent's) tree never absorbs unverified breakage;
         * green children merge exactly as before. The worktree is the ideal
         * gate site: isolated cwd, the live tree untouched either way. */
        if (app->config.parallel_verify) {
            const char *vcmd = jc_parallel_verify_cmd(
                app->env != NULL ? app->env->verify_cmd : NULL,
                app->config.verify, app->config.test_command);
            if (vcmd != NULL) {
                struct jc_sb vout;
                int vcode;
                long vt = (app->env != NULL) ? app->env->verify_timeout : 0;
                board_line(app, i, t->model_name, JC_BOARD_RUN,
                           "verify (worktree)", t->tokens);
                jc_sb_init(&vout);
                vcode = jc_env_run_verify(vcmd, t->wt, &vout,
                                          &app->abort_flag, vt);
                if (vcode != 0) {
                    n_quarantined++;
                    jc_snprintf(t->note, sizeof(t->note),
                        "[worktree verify FAILED (exit %d) -- this task's "
                        "changes were NOT merged]", vcode);
                    board_line(app, i, t->model_name, JC_BOARD_FAIL,
                               "verify red: not merged", t->tokens);
                    /* So the caller can see WHY without re-running anything.
                     * M862 (N43): the lines that look like failures first, then
                     * the bounded tail -- a tail alone held zig's summary and
                     * the failed build command, not the failing tests' names --
                     * and the whole output kept as a claim ticket in this
                     * parent's store (unfenced: N41 does not apply), named. */
                    jc_sb_append_fmt(&vfails,
                        "### Task %d worktree verify (exit %d):\n", i + 1, vcode);
                    /* M893 (N52 (b)): the same command, on the starting tree plus
                     * this task's changes, which are not merged: if the end-of-run
                     * check later fails on this very line, the cause was here
                     * before the run's own changes. The first such line only. */
                    if (on_start && app->env != NULL &&
                        app->env->pre_edit_fail[0] == '\0' &&
                        app->env->verify_cmd != NULL &&
                        strcmp(vcmd, app->env->verify_cmd) == 0 &&
                        jc_env_fail_line(vout.data, app->env->pre_edit_fail,
                                         sizeof app->env->pre_edit_fail)) {
                        jc_snprintf(app->env->pre_edit_from,
                                    sizeof app->env->pre_edit_from,
                                    "task %d's worktree verify, on the run's "
                                    "starting tree plus that task's own changes "
                                    "(not merged),", i + 1);
                    }
                    jc_parallel_verify_excerpt(vout.data, vout.len, 500, &vfails);
                    if (vout.data != NULL && vout.len > 0) {
                        char vpath[JC_PATH_MAX];
                        if (jc_toolout_preserve(app, "verify", vout.data,
                                                vout.len, vpath, sizeof vpath)) {
                            jc_sb_append_fmt(&vfails,
                                "the whole output (%lu bytes): %s\n",
                                (unsigned long)vout.len, vpath);
                        }
                    }
                    {
                        cJSON *j = jc_env_journal_begin(app->env,
                                                        "parallel_verify");
                        if (j != NULL) {
                            cJSON_AddNumberToObject(j, "task", (double)(i + 1));
                            cJSON_AddNumberToObject(j, "exit", (double)vcode);
                        }
                        jc_env_journal_end(app->env, j);
                    }
                    jc_sb_free(&vout);
                    t->preserve_why = "parallel-verify-red"; /* M803: keep it */
                    continue; /* quarantined: gather no changes */
                }
                jc_sb_free(&vout);
                board_line(app, i, t->model_name, JC_BOARD_DONE,
                           "verify green", t->tokens);
            }
        }
        jc_sb_init(&chg);
        /* M927 (R04-F1): the NUL form -- a child whose file name git would
         * C-quote (non-ASCII, newline, tab, quote, backslash) used to be
         * listed as one quoted line, copied nowhere, and reported merged;
         * its worktree held the only copy. */
        if (jc_snapshot_worktree_changes_z(app->snapshots, t->wt, base, &chg)
                != JC_OK || chg.data == NULL) {
            jc_sb_free(&chg);
            t->merged = 1;   /* M803: answered, no changes -- nothing to preserve */
            continue;
        }
        jc_vec_init(&changes, sizeof(struct jc_change));
        /* M199: per-call arena. The parsed change list is consumed by the merge
         * loop below and never referenced again, so it must not sit on the
         * session arena. Safe here: the children are forked processes, so no
         * nested in-process agent run can reset this arena mid-call. */
        jc_parallel_parse_changes_z(chg.data, jc_app_tool_scratch(app), &changes);   /* M927 (R04-F1): NUL form */
        /* M865 (the demos drive's §9; the operator's decision of 2026-10-02:
         * "Hold the merge"): a child that changed an existing test assertion,
         * in a file it did not create, is KEPT, not merged -- a verify the child
         * can turn green by moving an expected value is not evidence (card 215:
         * setting the scoreboard pin to 43 would have greened its verify and
         * taken the tree from 47 to 43). Found from the diff, so a shell edit
         * counts too. The lead can still take it on purpose (see Kept). */
        {
            char held[200];
            int outside = 0;
            int nh = assertion_hold(app, t->wt, base, &changes, held, sizeof held,
                                    &outside);
            /* M875: in "metric" mode, a child whose moved assertions are all in
             * the declared pin files is decided by the metric. One moved outside
             * them is held below, as in "hold" mode: the metric does not
             * measure it. */
            if (nh > 0 && outside == 0 && metric_gate_on(app)) {
                if (metric_gate(app, i, t, base, wtbase, &mbase, held)) {
                    nh = 0;              /* merges below; its note says why */
                } else {
                    jc_vec_free(&changes);
                    jc_sb_free(&chg);
                    continue;
                }
            }
            if (nh > 0) {
                cJSON *o;
                t->preserve_why = "parallel-test-edit";
                jc_snprintf(t->note, sizeof t->note,
                    "[not merged: this task changed an existing test assertion "
                    "%s(%s); a green verify reached that way is not evidence -- "
                    "its work is kept, see Kept; take it on purpose if the "
                    "change is right]",
                    metric_gate_on(app) ? "outside the metric's pin files " : "",
                    held);
                o = jc_app_telem_begin(app, "test_edit_hold");
                if (o != NULL) {
                    cJSON_AddNumberToObject(o, "task", (double)(i + 1));
                    cJSON_AddNumberToObject(o, "files", (double)nh);
                }
                jc_eventlog_end(app->telemetry, o);
                jc_vec_free(&changes);
                jc_sb_free(&chg);
                continue;
            }
        }
        /* M888 (N49 (b)): name every new directory this task adds; hold the large
         * ones out of the merge below. Measured against the live tree as it stands
         * now, so a directory an earlier task of this batch created is not new. */
        nnd = newdirs_scan(app->root, t->wt, &changes, nd, JC_PAR_NEWDIR_MAX,
                           &nd_more);
        newdirs_render(nd, nnd, nd_more, t->newdirs, sizeof t->newdirs);
        {
            int j;
            long hf = 0;
            long hb = 0;
            for (j = 0; j < nnd; j++) {
                if (nd[j].held) {
                    t->newdir_held++;
                    hf += nd[j].files;
                    hb += nd[j].bytes;
                }
            }
            if (t->newdir_held > 0) {
                cJSON *o = jc_app_telem_begin(app, "new_dir_hold");
                if (o != NULL) {
                    cJSON_AddNumberToObject(o, "task", (double)(i + 1));
                    cJSON_AddNumberToObject(o, "dirs", (double)t->newdir_held);
                    cJSON_AddNumberToObject(o, "files", (double)hf);
                    cJSON_AddNumberToObject(o, "bytes", (double)hb);
                }
                jc_eventlog_end(app->telemetry, o);
            }
        }
        for (k = 0; k < changes.len; k++) {
            struct jc_change *c =
                (struct jc_change *)jc_vec_at(&changes, k);
            /* def-332 (M1044): the worker's root report is evidence, not a code
             * change. Kept out of the merge BEFORE the first-wins claim, so two
             * tasks that both write REPORT.md no longer drop one as a conflict
             * and force a clean task into a Kept parallel-conflict state. It is
             * stored redacted beside the child's own transcript and named in the
             * result (the ## Reports section below). A D (deleted) report is NOT
             * captured: deleting the report is a code event that merges as such. */
            if (jc_par_root_report(c->path) && c->status != 'D') {
                char rp[JC_PATH_MAX];
                char sp[JC_PATH_MAX];
                int stored;
                jc_snprintf(rp, sizeof rp, "%s/%s", t->wt, c->path);
                stored = jc_par_store_report(app, rp, i + 1, run_id,
                                             sp, sizeof sp);
                if (stored) {
                    jc_sb_append_fmt(&reports,
                        "[report] task %d: stored at %s (kept out of the merge)\n",
                        i + 1, sp);
                } else {
                    jc_sb_append_fmt(&reports,
                        "[report] task %d: kept out of the merge, but the store "
                        "FAILED (path not written)\n", i + 1);
                }
                continue;
            }
            /* M888: a path under a held new directory stays out of the merge. */
            if (newdir_held_path(nd, nnd, c->path)) {
                n_held_paths++;
                continue;
            }
            /* Merge-time edit-scope fence (M133): even though each write child
             * enforces --edit-scope on its file tools, it could still touch a
             * file outside the scope via the shell inside its worktree. The
             * parent refuses to merge any out-of-scope change back. */
            if (app->env != NULL && app->env->edit_scope.len > 0 &&
                !jc_env_path_in_scope(app->env, app->root, c->path)) {
                jc_sb_append_fmt(&conflicts,
                                 "  %s (task %d, out of edit-scope: not merged)\n",
                                 c->path, i + 1);
                had_conflict = 1;   /* M803 */
                continue;
            }
            if (jc_parallel_claim(&seen, c->path)) {
                if (apply_change(app, t->wt, c->status, c->path)) {
                    applied++;
                    /* D6 (M975): record this path as worker-owned at successful merge. */
                    {
                        char *content;
                        jc_size len;
                        struct stat dstst;
                        /* Re-read the merged content from disk */
                        char full_path[4096];
                        if (strlen(app->root) + 1 + strlen(c->path) < sizeof(full_path)) {
                            jc_snprintf(full_path, sizeof(full_path), "%s/%s", app->root, c->path);
                            /* M1099 (D-12): check the endpoint before reading --
                             * apply_change now re-creates a merged path as a
                             * SYMLINK (typechange T), and jc_read_file's fopen
                             * follows it: a link to a FIFO would block this
                             * re-read (and the whole lead). Reading the link's
                             * REGULAR target's content is acceptable scoped-edit
                             * tracking, so follow it only when the resolved
                             * endpoint is a regular file; skip a link to a
                             * pipe/other and any non-regular dst. lstat is
                             * first so the guard itself never opens; stat then
                             * resolves the link (metadata only, no open) before
                             * the read. */
                            if (lstat(full_path, &dstst) == 0) {
                                if (S_ISREG(dstst.st_mode)) {
                                    if (jc_read_file(full_path, &content, &len,
                                                      jc_app_tool_scratch(app)) == 0) {
                                        jc_scopededit_owner(app, c->path, i + 1,
                                                            content, len);
                                    }
                                } else if (S_ISLNK(dstst.st_mode)) {
                                    struct stat ts;
                                    if (stat(full_path, &ts) == 0 &&
                                        S_ISREG(ts.st_mode)) {
                                        if (jc_read_file(full_path, &content, &len,
                                                          jc_app_tool_scratch(app)) == 0) {
                                            jc_scopededit_owner(app, c->path, i + 1,
                                                                content, len);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    /* M437: record it for this child's report. Bounded by
                     * construction: once the buffer is nearly full an ellipsis is
                     * appended and nothing more is added, because a truncated list
                     * that does not SAY it is truncated reads as complete. */
                    if (t->changed[0] == '\0') {
                        jc_snprintf(t->changed, sizeof t->changed, "%s",
                                    c->path);
                    } else if (strlen(t->changed) + strlen(c->path) + 6
                                   < sizeof t->changed) {
                        jc_snprintf(t->changed + strlen(t->changed),
                                    sizeof t->changed - strlen(t->changed),
                                    ", %s", c->path);
                    } else if (strstr(t->changed, ", ...") == NULL &&
                               strlen(t->changed) + 6 < sizeof t->changed) {
                        jc_snprintf(t->changed + strlen(t->changed),
                                    sizeof t->changed - strlen(t->changed),
                                    ", ...");
                    }
                } else {
                    /* M927 (R04-F1): a copy that FAILED is not "applied".
                     * Before this it was recorded nowhere, so the task was
                     * marked merged and its worktree -- the only copy of the
                     * change -- destroyed. Now it is a conflict: the worktree
                     * is preserved (had_conflict, M803) and the result says
                     * so instead of claiming a merge. */
                    jc_sb_append_fmt(&conflicts,
                                     "  %s (task %d, copy failed: not merged)\n",
                                     c->path, i + 1);
                    had_conflict = 1;   /* M927 */
                }
            } else {
                jc_sb_append_fmt(&conflicts, "  %s (task %d)\n",
                                 c->path, i + 1);
                had_conflict = 1;   /* M803 */
            }
        }
        jc_vec_free(&changes);
        jc_sb_free(&chg);
        /* M803: a task whose every change merged is "merged in full" -- skip its
         * preservation at teardown. If any change conflicted, keep the worktree. */
        t->merged = !had_conflict && n_held_paths == 0;
        if (had_conflict && t->preserve_why == NULL) {
            t->preserve_why = "parallel-conflict";
        }
        if (n_held_paths > 0 && t->preserve_why == NULL) {
            t->preserve_why = "parallel-new-dir";   /* M888 */
        }
    }
    jc_vec_free(&seen);

    /* M855 (N34): keep what the children left on the shared stash stack, and
     * empty it. A stashed change is not in a child's worktree, so the M803
     * preservation below never sees it (run 0070: a stopped worker's kept ref was
     * empty, its change in the stash). An entry cannot be traced to one child:
     * its parent is the batch's shared base unless that child had committed. */
    if (any_write) {
        nstash_left = jc_snapshot_stash_keep(app->snapshots, "parallel-stash",
                                             &stash_left);
    }

    /* Remove all worktrees, then the (now-empty) per-run base directory. M803:
     * a child merged in full (or a read-only child, with nothing to lose) is
     * removed without preserving; every other worktree is preserved first, under
     * a reason that names how the child ended. A worktree whose preservation fails
     * is KEPT (rmdir below then fails on the non-empty base, leaving it too). */
    jc_sb_init(&kept);   /* M809: accumulate one line per state actually preserved */
    for (i = 0; i < ntasks; i++) {
        struct ptask *t = &tasks[i];
        const char *why;
        struct jc_sb rec;
        if (t->wt[0] == '\0') {
            continue;
        }
        if (!t->write || t->merged) {
            why = JC_WT_MERGED;
        } else if (t->preserve_why != NULL) {
            why = t->preserve_why;
        } else if (t->answer == NULL) {
            why = (t->error != NULL) ? "parallel-interrupted" : "parallel-unanswered";
        } else {
            why = "parallel-interrupted";
        }
        /* M808: compose the plan's §4.2 end record as the preserved commit message,
         * so a successor -- and P3's salvage -- reads WHY a state was kept (its
         * reason, the loop that stopped it, its transcript) and not just its diff.
         * A merged state is not preserved, so its record is built but unused. */
        jc_sb_init(&rec);
        jc_sb_append_fmt(&rec, "discarded: parallel task %d of %d, %s\n\n",
                         i + 1, ntasks, why);
        jc_sb_append_fmt(&rec, "task:       %.200s\n", t->task != NULL ? t->task : "");
        jc_sb_append_fmt(&rec, "reason:     %s\n", why);
        if (t->ending != NULL) {
            jc_sb_append_fmt(&rec, "ended:      %s\n", t->ending);  /* M854 */
        }
        if (t->model_name != NULL) {
            jc_sb_append_fmt(&rec, "model:      %s\n", t->model_name);
        }
        jc_sb_append_fmt(&rec, "run:        %s    task-run: %s-t%d\n",
                         run_id, run_id, i + 1);
        if (base[0] != '\0') {
            jc_sb_append_fmt(&rec, "base:       %s\n", base);
        }
        /* M827 (N16): tool_calls comes from the worker's own final report, which an
         * INTERRUPTED worker never sends -- so it reads 0, and a successor that trusts
         * "0 tool calls" may discard a state that holds work (its transcript and
         * diffstat show it ran). The rule for a missing value is UNKNOWN, never a
         * fabricated 0. tokens are metered live, so they stay. */
        if (t->finished) {
            jc_sb_append_fmt(&rec, "counters:   %d tool calls, %.0f tokens\n",
                             t->tool_calls, t->tokens);
        } else {
            jc_sb_append_fmt(&rec, "counters:   tool calls unknown (interrupted "
                             "before it reported), %.0f tokens\n", t->tokens);
        }
        if (t->ftool[0] != '\0') {
            jc_sb_append_fmt(&rec, "last call:  %s (%s)\n", t->ftool,
                             jc_fail_class_name((enum jc_fail_class)t->fcls));
        }
        if (t->repeated > 0) {
            jc_sb_append_fmt(&rec,
                "repeated:   the same failing call, %d times in a row\n",
                t->repeated);
        }
        if (t->changed[0] != '\0') {
            jc_sb_append_fmt(&rec, "files:      %s\n", t->changed);
        }
        jc_sb_append_fmt(&rec, "transcript: attempts/%s/t%d.jsonl\n",
                         run_id, i + 1);
        /* M809: clear the stash so a NON-preserving removal (a merged/read-only
         * child) leaves it empty -- then a non-empty ref after the call means THIS
         * task's state was actually preserved, and its ref goes in the Kept list. */
        if (app->snapshots != NULL) {
            app->snapshots->preserve_last_ref[0] = '\0';
        }
        jc_snapshot_worktree_remove(app->snapshots, t->wt, why, rec.data);
        if (app->snapshots != NULL &&
            app->snapshots->preserve_last_ref[0] != '\0') {
            jc_sb_append_fmt(&kept, "- task %d (%s): %s", i + 1, why,
                             app->snapshots->preserve_last_ref);
            if (t->repeated > 0) {
                jc_sb_append_fmt(&kept, " — looped %d×", t->repeated);
            }
            jc_sb_append(&kept, "\n");
            nkept++;
        }
        jc_sb_free(&rec);
    }
    if (have_base) {
        /* M811: this run tore down cleanly, so drop the lock and remove the
         * manifest + lock file, then the base dir. (A dead PARENT never reaches
         * here, so its manifest + free lock stay for salvage.) rmdir still fails
         * harmlessly if a preserved tree lingered. M860 (CTL-5): only a batch that
         * HELD the lock removes them. M811's comment said no other run reuses a
         * base's name; before M860 a nested batch did (one run id per session),
         * failed to take the lock, and then deleted the live lead's lock and
         * manifest. The per-batch id makes the names distinct; this keeps a batch
         * that did not take the lock from touching what it does not own. */
        char p[JC_PATH_MAX];
        int held = (base_lock >= 0);
        if (held) {
            jc_lock_release(base_lock);
            base_lock = -3;
            jc_snprintf(p, sizeof p, "%s/batch.json", wtbase);
            remove(p);
            jc_snprintf(p, sizeof p, "%s/.lock", wtbase);
            remove(p);
        }
        rmdir(wtbase);
    }

    /* Aggregate into one tool result. */
    jc_sb_init(&agg);
    jc_sb_append_fmt(&agg, "## Parallel agents: %d task(s), up to %d at once\n\n",
                     ntasks, cap);
    if (aborted) {
        jc_sb_append(&agg, "(interrupted; partial results below)\n\n");
    }
    /* M852 (N36): shellFenceAllow entries that grant nothing, said once. */
    if (fence_note[0] != '\0') {
        jc_sb_append_fmt(&agg, "[jichi] shellFenceAllow: %s -- such an entry "
                         "grants nothing, so a write child's shell cannot write "
                         "there. List absolute directories that exist before "
                         "the run.\n\n", fence_note);
    }
    /* M1006: the dispatch started from a DIRTY tree (mode `warn`). Said once,
     * at the top, the M852 fence_note shape -- the batch ran, but the base is
     * not a committed tree, so the child's merge and the lead's later `git add`
     * mix uncommitted work in. */
    if (dirty_note[0] != '\0') {
        jc_sb_append_fmt(&agg,
            "[jichi] the workspace was dirty when this write batch started "
            "(uncommitted: %s). The batch ran, but the children forked off the "
            "current HEAD, not a committed base -- commit, then dispatch, so a "
            "merge cannot mix in uncommitted work.\n\n", dirty_note);
    } else if (dd_unknown_note) {
        /* M1006-a: the probe FAILED -- the tree state is unknown, not clean.
         * The batch proceeded (an infra failure must not block work), but the
         * note says so, so the base is not silently assumed committed. */
        jc_sb_append(&agg,
            "[jichi] the workspace tree state is unknown when this write batch "
            "started: the dirty-check probe (git status) failed, so the tree "
            "could not be read. The batch proceeded, but verify the tree is "
            "committed before relying on this work.\n\n");
    }
    /* M809: the lead reads the kept states FIRST. A child whose work was not merged
     * had it preserved (M803) with a full end record (M808); list those states at
     * the TOP, each with the ref a successor hands to `jichi attempts show <ref>` --
     * a ref at the bottom of a long report is a ref nobody reads (plan §4.2).
     * The ref lives in jichi's own attempt store under `$HOME/.jichi.d`, not in this
     * workspace, so `git show`/`git log` here will not resolve it. */
    if (nkept > 0) {
        jc_sb_append_fmt(&agg,
            "**Kept %d unmerged state(s)** — recover with `jichi attempts show "
            "<ref>` (or `jichi recover <ref>`):\n%s\n"
            "the ref lives in jichi's own attempt store under `$HOME/.jichi.d`, not in this workspace, so `git show`/`git log` here will not resolve it",
            nkept, kept.data);
    }
    jc_sb_free(&kept);
    /* M812: a previous batch that did not finish left orphaned work; this batch
     * salvaged it on start. Say so at the top, so the lead (and operator) sees it
     * with the refs to recover -- not buried where nobody reads. */
    if (nsalvaged > 0) {
        jc_sb_append_fmt(&agg,
            "**Salvaged %d orphaned state(s)** from a batch that did not finish — "
            "recover with `jichi attempts show <ref>`:\n%s\n"
            "the ref lives in jichi's own attempt store under `$HOME/.jichi.d`, not in this workspace, so `git show`/`git log` here will not resolve it",
            nsalvaged, salvaged.data);
    }
    jc_sb_free(&salvaged);
    /* M855 (N34): the stash entries kept, at the top beside Kept. */
    if (nstash_left > 0 || nstash_earlier > 0) {
        jc_sb_append(&agg,
            "**Kept stash entries** — a write child's `git stash` goes to one stack "
            "shared by every child of the batch, so an entry cannot be traced to one "
            "child; inspect one with `jichi attempts show <ref>`:\n"
            "the ref lives in jichi's own attempt store under `$HOME/.jichi.d`, not in this workspace, so `git show`/`git log` here will not resolve it\n");
        if (nstash_left > 0) {
            jc_sb_append_fmt(&agg, "- %d left by this batch's children:\n",
                             nstash_left);
            append_indented(&agg, stash_left.data);
        }
        if (nstash_earlier > 0) {
            jc_sb_append_fmt(&agg, "- %d found before this batch started, set aside "
                             "so no child could pop them:\n", nstash_earlier);
            append_indented(&agg, stash_earlier.data);
        }
        jc_sb_append(&agg, "\n");
    }
    jc_sb_free(&stash_left);
    jc_sb_free(&stash_earlier);
    /* M824 (N7, threadwork's weekend drive): the DROPPED changes at the TOP, next
     * to Kept -- the first-wins merge skips a later writer's change to a file an
     * earlier writer also changed, and on the drive that silently dropped a second
     * worker's test registration in src/root.zig (kept in its worktree, but not
     * compiled until the lead happened to notice). A drop in the merge summary at
     * the bottom is a drop nobody reads. The lines are the same the merge section
     * repeats below, surfaced here because a shared registry (a test index, an
     * `__init__`, a `mod.rs`) loses real work this way. */
    if (conflicts.len > 0) {
        int ndrop = 0;
        jc_size ci;
        for (ci = 0; ci < conflicts.len; ci++) {
            if (conflicts.data[ci] == '\n') {
                ndrop++;
            }
        }
        jc_sb_append_fmt(&agg,
            "**Dropped %d change(s) (first writer kept)** — a later task changed a "
            "file an earlier task also changed, so its change was NOT merged. If any "
            "is a shared file (a test registry, an index, a module list), its lines "
            "are in the preserved worktree (see *Kept* above, `jichi attempts show "
            "<ref>`) but NOT in the tree — re-apply them, or resume that attempt with "
            "`spawn_parallel` `from:\"<ref>\"`:\n%s\n", ndrop, conflicts.data);
    }
    /* def-332 (M1044): the workers' root reports, kept out of the merge and
     * stored redacted with the run's records -- named here, where the Kept and
     * Dropped sections stand, so the lead reads its own workers' evidence with
     * the refs rather than buried at the bottom. */
    if (reports.len > 0) {
        jc_sb_append(&agg, "\n## Reports (kept out of the merge):\n");
        jc_sb_append(&agg, reports.data);
    }
    for (i = 0; i < ntasks; i++) {
        struct ptask *t = &tasks[i];
        /* M889 (the demos drive's N51): cut the title at a character, not a byte.
         * `%.70s` kept the first byte of run 0104's em dash, and the telemetry
         * line that carried this result was not valid UTF-8. */
        {
            const char *tt = (t->task != NULL) ? t->task : "";
            jc_size tl = (jc_size)strlen(tt);
            jc_size tcut = (tl > 70) ? jc_utf8_trunc_len(tt, 70) : tl;
            jc_sb_append_fmt(&agg, "### Task %d: %.*s%s\n", i + 1, (int)tcut, tt,
                             (tl > 70) ? "..." : "");
        }
        /* M891 (the demos drive's §15; the operator's decision of 2026-10-03, "Name
         * each task's model"): which model ran it, and when that was the lead's
         * own because neither the task nor its profile named one. Run 0096's two
         * probe batches named none and ran on the lead's 27B, 7-23 s a call where
         * the workers' model took 1.3 s; the result said nothing of it. */
        if (t->model_name != NULL) {
            jc_sb_append_fmt(&agg, "[jichi] model: %s%s\n", t->model_name,
                             (t->model_sel == NULL || t->model_sel[0] == '\0')
                                 ? " -- the lead's own: neither the task nor its "
                                   "profile named one"
                                 : "");
        }
        if (t->note[0] != '\0') {
            jc_sb_append_fmt(&agg, "%s\n", t->note);
        }
        if (t->answer != NULL) {
            if (strlen(t->answer) > JC_PAR_ANSWER_CAP) {
                /* M889 (N51): at a character, like the title. */
                jc_sb_append_n(&agg, t->answer,
                               jc_utf8_trunc_len(t->answer, JC_PAR_ANSWER_CAP));
                jc_sb_append(&agg, "\n... [answer truncated]\n");
            } else {
                jc_sb_append(&agg, t->answer);
                jc_sb_append(&agg, "\n");
            }
        } else {
            jc_sb_append_fmt(&agg, "(error: %s)\n",
                             t->error != NULL ? t->error : "unknown");
        }
        /* M863 (N44 (a)): a child that ran work-discarding git commands, and
         * the states jichi kept before them, for the lead and the operator. */
        if (t->npinned > 0) {
            jc_sb_append_fmt(&agg, "[jichi] This task ran %d git command(s) that "
                             "discard work or move HEAD; its worktree was kept "
                             "before each: %s\n", t->npinned, t->pinned);
        }
        /* M888 (N49 (b)): every new directory, named; a held one was kept out. */
        if (t->newdirs[0] != '\0') {
            jc_sb_append_fmt(&agg, "[jichi] This task added new directories: %s.%s\n",
                             t->newdirs,
                             t->newdir_held > 0
                                 ? " A HELD directory -- over 1 MB or 100 files -- was "
                                   "not merged; the task's work is kept (see Kept), "
                                   "the rest of it merged."
                                 : "");
        }
        /* M852 (N36): the fence's refusals are the kernel's, so a child's own
         * count of permission errors is the only place they show. */
        if (t->fence_hints > 0) {
            jc_sb_append_fmt(&agg, "[jichi] %d shell command(s) in this task "
                             "failed with a permission error under the shell "
                             "fence: a write outside the worktree, its temp dir "
                             "and shellFenceAllow is refused.\n",
                             t->fence_hints);
        }
        /* M437: the child's own report, rendered by the SAME function
         * spawn_subagent uses -- so the two delegation tools cannot describe one
         * outcome two ways. A child that reported nothing (an older message, or a
         * pre-fork skip) renders nothing, so this cannot invent a stop reason. */
        if (t->stop[0] != '\0') {
            struct jc_delegate_report cr;
            jc_delegreport_init(&cr);
            cr.stop = jc_delegreport_stop_parse(t->stop);
            cr.cut = (t->cut > 0);    /* M858 (N42) */
            /* M885 (N47, continued): a SIGTERM this batch did not send. When
             * the batch stops a child -- its own abort, a task timeout -- it
             * closes the child's pipe first, so that child never reports; a
             * group-wide signal aborts the batch too, hence !aborted. */
            cr.term = (t->term && !aborted) ? 1 : 0;
            cr.looped = t->looped;    /* M869 */
            /* M861: a stop on its share, while the run has budget left (the
             * children's usage is already reconciled into the envelope). */
            cr.share_only = (cr.stop == JC_DELEG_BUDGET &&
                             jc_env_over_budget(app->env, (long)time(NULL))
                                 == JC_BUDGET_NONE);
            cr.tokens = (t->tokens > 0.0) ? t->tokens : -1.0;
            cr.tool_calls = (t->tool_calls > 0) ? (long)t->tool_calls : -1;
            if (t->ftool[0] != '\0') {
                jc_snprintf(cr.fail_tool, sizeof cr.fail_tool, "%s", t->ftool);
                jc_snprintf(cr.fail_msg, sizeof cr.fail_msg, "%s", t->fmsg);
                cr.fail_cls = (enum jc_fail_class)t->fcls;
                cr.fail_count = t->fcount;    /* M876 */
                cr.fail_stopped = t->fstop;   /* M885 */
            }
            /* M876 (N47 (a)): the run's own model failure */
            jc_snprintf(cr.run_error, sizeof cr.run_error, "%s", t->rerr);
            cr.run_error_attempts = t->rerrn;
            cr.run_error_t0 = t->rerr0;
            cr.run_error_t1 = t->rerr1;
            /* The one field the fork pool CAN fill and the sync tool cannot: a
             * write child ran in its own worktree, whose changed files the parent
             * already parsed to decide the merge. */
            if (t->write && t->changed[0] != '\0') {
                jc_snprintf(cr.files_changed, sizeof cr.files_changed, "%s",
                            t->changed);
            }
            jc_delegreport_render(&cr, &agg);
            jc_sb_append(&agg, "\n");
        }
        jc_sb_append(&agg, "\n");
    }
    if (any_write) {
        jc_sb_append_fmt(&agg, "## Merge: %d file(s) applied", applied);
        if (n_quarantined > 0) {
            jc_sb_append_fmt(&agg, "; %d task(s) QUARANTINED by the verify "
                             "gate (changes not merged)", n_quarantined);
        }
        if (conflicts.len > 0) {
            jc_sb_append(&agg, ", conflicts skipped (first writer kept):\n");
            jc_sb_append(&agg, conflicts.data);
        } else {
            jc_sb_append(&agg, ", no conflicts.\n");
        }
        if (vfails.len > 0) {
            jc_sb_append(&agg, "\n");
            jc_sb_append(&agg, vfails.data);
        }
    }
    if (truncated) {
        jc_sb_append_fmt(&agg, "\n(note: only the first %d tasks were run)\n",
                         JC_PAR_MAX_TASKS);
    }
    jc_sb_free(&conflicts);
    jc_sb_free(&reports);
    jc_sb_free(&vfails);

    /* Free per-task parsed strings + pipe buffers. */
    ptask_free_members(tasks, ntasks);

    out->content = jc_sb_finish(&agg);
    out->is_error = (n_ok == 0) ? 1 : 0;
    jc_sb_free(&agg);
    return JC_OK;
}

static const struct jc_tool PARALLEL_TOOL = {
    "spawn_parallel",
    "Run several independent subtasks concurrently as sub-agents and return "
    "their aggregated answers. Trigger shape: three or more independent "
    "look-ups or reviews sharing no state -- one task per angle, each naming "
    "its files and its question. Read-only by default; write:true lets a "
    "task edit in an isolated git worktree (give write tasks disjoint "
    "files). Each child gets a slice of the remaining budget. Prefer this "
    "over sequential spawn_subagent for independent tasks, and plain tools "
    "for a single look-up.",
    parallel_schema,
    0, /* mutating */
    parallel_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

const struct jc_tool *jc_tool_parallel(void)
{
    return &PARALLEL_TOOL;
}
