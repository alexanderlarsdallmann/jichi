/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_snapshot.h - git-backed workspace snapshots & undo.
 *
 * Checkpoints the workspace before the agent's first file-changing action in a
 * turn, so `/undo` can revert that turn's edits. Uses a shadow git repository
 * under ~/.jichi.d/checkpoints/<key>/ whose work tree is the workspace,
 * keeping the user's own .git untouched. See docs/SNAPSHOTS.md.
 *
 * The path-derivation and label helpers are pure and unit-tested; the git flow
 * is verified end-to-end.
 */
#ifndef JC_SNAPSHOT_H
#define JC_SNAPSHOT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_str.h"

struct jc_app;     /* jc_app.h     */
struct jc_history; /* jc_message.h */

/* Most checkpoints surfaced to the stack / CLI listing (newest kept). */
#define JC_SNAPSHOT_MAX_LIST 200

/* One checkpoint: a shadow-repo commit plus a human label. Strings are
 * arena-owned (live for the session). */
/* One checkpoint. Fixed-size fields ON PURPOSE (M180): the stack lives in a
 * jc_vec that rotates at JC_SNAPSHOT_MAX_LIST, so a marathon session's
 * checkpoint store is truly bounded -- the previous arena-strdup'd strings
 * accumulated for the life of the process. Accessors return pointers into
 * the vec element: valid until the next take/refresh, and every caller
 * copies or consumes immediately (verified at M180). */
struct jc_snapshot {
    char commit[48];  /* shadow-repo commit sha (40 hex + NUL)            */
    char label[168];  /* short description of the turn (pre-truncated)    */
};

struct jc_snapshot_mgr {
    struct jc_app *app;
    char           git_dir[1024];   /* shadow repo (GIT_DIR)        */
    char           work_tree[1024]; /* the workspace (GIT_WORK_TREE) */
    int            available;       /* git present + repo ready      */
    struct jc_vec  checkpoints;     /* of struct jc_snapshot (stack) */
    /* M337b PRESERVE-BEFORE-DESTROY, extended to every destructive restore.
     *
     * M336 put preservation at the ONE call site that had destroyed work
     * (the envelope's rollback). There are five more -- /undo, /rewind, the
     * `undo` and `rewind` subcommands, and --revert-out-of-scope -- and they
     * funnel through exactly two chokepoints in this file: `restore_to` and
     * `jc_snapshot_restore_paths`. Preserving THERE rather than at each caller
     * is what makes a future sixth caller safe by default; a call site can be
     * forgotten, a chokepoint cannot.
     *
     * `preserve` is config `preserveDiscarded`, read once in manager_init, so
     * one key governs the whole layer and cannot disagree with itself.
     * `skip_once` is how the envelope opts out of the generic preservation for
     * one restore because it has already written a RICHER record (run id,
     * outcome, verifier, token and call counts) that this layer must not know
     * about -- jc_snapshot has no business reading a jc_envelope. */
    int            preserve;
    int            preserve_skip_once;
    int            preserve_n;
    /* The sha the last destructive restore preserved, or "" if none did. The
     * LAYER preserves and the SURFACE reports: a jc_logf at the natural level
     * for "your work was saved" is INFO, which is below the default WARN and so
     * says nothing to the user who just typed `undo`; and WARN is a lie about a
     * successful save. So the surfaces that print an undo result read this and
     * add a line. Which also keeps the wording in the surface's own voice --
     * `/undo` in the TUI and `jichi undo` in a script want different text. */
    char           preserve_last[64];
};

/* M338: should a preserved-discarded state be kept? Pure, so the policy can be
 * tested without a git repository.
 *
 * `rank` is 0-based newest-first; `age_days` is how old the state is. Keep when
 * EITHER the state is within `keep_days` OR it is among the newest `keep_n`.
 *
 * Age plus a count floor, deliberately, and not a size cap (proposal section
 * 10.4): age alone deletes the only copy from a workspace nobody has touched in
 * a month -- which is exactly when you are least able to reproduce it -- and a
 * size cap would require jichi to decide which of two discarded states is worth
 * more, which it cannot know. Age and count need no judgement.
 *
 * keep_days < 0 or keep_n < 0 mean "unlimited" for that half, so a caller can
 * disable either without a second predicate. */
int jc_snapshot_retain(long age_days, int rank, int keep_days, int keep_n);

/* The commit the most recent destructive restore preserved, or NULL if that
 * restore preserved nothing (feature off, or nothing to save). */
const char *jc_snapshot_preserved_last(const struct jc_snapshot_mgr *m);

/* Pure: derive the shadow-repo git dir for a workspace under `home`. */
/* M431e: the per-workspace store key -- djb2 of the canonical work tree. Public
 * because more than one store is keyed by workspace (the checkpoint repo here, the
 * M431e run lease under ~/.jichi.d/leases/), and two derivations of one key would
 * drift into two different numbers for one project. Pure; unit-tested. */
unsigned long jc_workspace_key(const char *work_tree);

void jc_snapshot_git_dir(const char *home, const char *work_tree,
                         char *buf, jc_size cap);

/* Pure: copy `s` into `buf` collapsing whitespace and truncating to a short
 * single-line label (for checkpoint display). */
/* M335: what a shadow-store directory is, judged from its work tree.
 *
 * Measured 2026-08-09: 21 shadow repositories under ~/.jichi.d/checkpoints
 * totalling 61 MB, of which 10 pointed at work trees that no longer existed
 * (/tmp scratch dirs, deleted git worktrees). `do_prune` bounds the commits
 * INSIDE a live repo; nothing had ever removed a repo, so half the store was
 * garbage jichi could neither report nor clear.
 *
 * The distinction that matters, and the one that cannot be made perfectly: a work
 * tree on an UNMOUNTED VOLUME is indistinguishable from a deleted one -- both are
 * simply absent. So this reports three states rather than two, using the parent
 * directory as a (stated, imperfect) discriminator: a project deleted from a
 * directory that still exists is ORPHANED; a path whose parent is ALSO missing
 * looks like an unmounted mount point and is UNREACHABLE, which `gc` lists and
 * refuses to remove. A heuristic, not a proof -- see
 * docs/proposals/2026-08-work-preservation.md section 8.2. Pure; unit-tested. */
/* M336 (PRESERVE-BEFORE-DESTROY): commit the CURRENT work tree and pin it under
 * `ref`, without touching the undo stack.
 *
 * Measured 2026-08-09: 6 of 46 runs discard work by rolling back, and 4 of 4
 * `verify_failed` runs do. One such rollback destroyed 711,628 tokens of tests
 * because the rollback path computes the NAMES of the files it is about to discard,
 * logs them, and throws the CONTENT away -- while a shadow repository whose whole
 * purpose is holding tree states sits unasked (ANECDOTES #48).
 *
 * Deliberately NOT jc_snapshot_take: that pushes onto the checkpoint stack, which
 * would make `undo` walk into states the user never chose and would consume the
 * `snapshotLimit` protecting the ones they did. The commit does advance the branch,
 * and the restore that follows resets it back -- orphaning the commit, which is
 * precisely why the ref is written FIRST: the ref is the only thing keeping it
 * alive, and it is in place before anything destructive happens.
 *
 * `sha_out` (may be NULL) receives the commit id. Returns JC_OK when snapshots are
 * unavailable, having done nothing -- preservation must never be the reason a
 * rollback fails. */
jc_status jc_snapshot_preserve(struct jc_snapshot_mgr *m, const char *label,
                               const char *ref, char *sha_out, jc_size cap);

enum jc_store_state {
    JC_STORE_LIVE = 0,      /* the work tree exists                            */
    JC_STORE_ORPHANED,      /* gone, but its parent exists: deleted project     */
    JC_STORE_UNREACHABLE,   /* gone AND parent gone: possibly an unmounted disk */
    JC_STORE_UNKNOWN        /* no work tree recorded at all                     */
};

enum jc_store_state jc_snapshot_store_state(const char *work_tree,
                                            int work_tree_exists,
                                            int parent_exists);

void jc_snapshot_clean_label(const char *s, char *buf, jc_size cap);

/* Initialise the manager for app->cwd: probe git, create/configure the shadow
 * repo. Sets m->available. Disabled (available=0) if config snapshots are off
 * or git is missing; never fails the caller. */
jc_status jc_snapshot_manager_init(struct jc_snapshot_mgr *m,
                                   struct jc_app *app);

void jc_snapshot_manager_shutdown(struct jc_snapshot_mgr *m);

int jc_snapshot_available(const struct jc_snapshot_mgr *m);

/* Take a checkpoint of the current work tree and push it on the stack. No-op
 * (JC_OK) when unavailable. `label` describes the turn about to run. */
jc_status jc_snapshot_take(struct jc_snapshot_mgr *m, const char *label);

/* Number of checkpoints on the session stack. */
int jc_snapshot_count(const struct jc_snapshot_mgr *m);

/* Label of checkpoint `i` (0 = oldest), or NULL if out of range. */
const char *jc_snapshot_label(const struct jc_snapshot_mgr *m, int i);

/* Commit SHA of checkpoint `i` (0 = oldest), or NULL if out of range. Used by
 * the autonomy envelope to remember a known-good ("green") checkpoint. */
const char *jc_snapshot_commit(const struct jc_snapshot_mgr *m, int i);

/* Restore the work tree to an explicit shadow-repo commit (reset --hard +
 * clean -fd), without touching the checkpoint stack. For the envelope's
 * rollback-to-green. Returns JC_OK, or JC_ERR_* if unavailable / git fails. */
jc_status jc_snapshot_restore_commit(struct jc_snapshot_mgr *m,
                                     const char *commit);

/* M142: restore individual `paths` (work-tree-relative, as
 * jc_snapshot_changed_since lists them) to their content at `commit`,
 * leaving every other file untouched. A path that did not exist at `commit`
 * (created since) is removed from the work tree -- that IS its restore; a
 * path deleted since is checked back out. Counts per-path successes into
 * *reverted and failures into *failed (either may be NULL). Returns
 * JC_ERR_NOTFOUND when snapshots are unavailable, else JC_OK (per-path
 * failures are reported via *failed, not the status). */
jc_status jc_snapshot_restore_paths(struct jc_snapshot_mgr *m,
                                    const char *commit,
                                    const char *const *paths, int n,
                                    int *reverted, int *failed);

/* Worktree isolation for the parallel agent swarm (spawn_parallel). All ride
 * the shadow repo, so the user's own .git is untouched. */

/* M337: append one line per preserved-discarded state (M336) to `out`, as
 *   <sha>\t<ref>\t<iso-date>\t<subject>
 * newest first. Reads the shadow repo's refs/jichi/discarded refs, so it shows
 * exactly what `--preserve-discarded` pinned and nothing else. Returns JC_OK with
 * `out` untouched when snapshots are unavailable or nothing was preserved. */
jc_status jc_snapshot_discarded_list(struct jc_snapshot_mgr *m,
                                     struct jc_sb *out);

/* Create a detached worktree at `path` materialising `base_commit`
 * (`git worktree add --detach`). The dir is created by git. */
jc_status jc_snapshot_worktree_add(struct jc_snapshot_mgr *m,
                                   const char *base_commit, const char *path);

/* Stage everything in worktree `wt_path` and append its changes vs
 * `base_commit` to `out`, one `<status>\t<path>` line each
 * (`git -C <wt> add -A` then `diff --name-status --no-renames`). Parse with
 * jc_parallel_parse_changes. */
jc_status jc_snapshot_worktree_changes(struct jc_snapshot_mgr *m,
                                       const char *wt_path,
                                       const char *base_commit,
                                       struct jc_sb *out);

/* Remove the worktree at `path` (`git worktree remove --force` + prune). */
jc_status jc_snapshot_worktree_remove(struct jc_snapshot_mgr *m,
                                      const char *path);

/* Restore the work tree to the most recent checkpoint and pop it. Returns
 * JC_ERR_NOTFOUND if the stack is empty, JC_ERR_* on git failure. On success,
 * *label_out (if non-NULL) points at the reverted checkpoint's label. */
jc_status jc_snapshot_undo(struct jc_snapshot_mgr *m, const char **label_out);

/* M349: the files an undo would revert RIGHT NOW -- the working tree diffed
 * against the newest checkpoint, in jc_snapshot_changed_since's
 * newline-separated format. Read it BEFORE jc_snapshot_undo: the undo pops
 * the checkpoint this diffs against. JC_ERR_NOTFOUND when there is nothing
 * to undo. */
jc_status jc_snapshot_undo_changes(struct jc_snapshot_mgr *m,
                                   struct jc_sb *out);

/* M349: render the note a live conversation gets after an undo, so the MODEL
 * learns what the human already saw. `/undo` reverts the FILES and keeps the
 * conversation (the M34c split; /rewind is the both-halves form), so every
 * earlier tool result describing the reverted files is stale the moment the
 * restore runs -- and a model that is not told builds its next edit on
 * phantom state. `names` is newline-separated (jc_snapshot_undo_changes); at
 * most 8 are listed, then "+K more". Appends NOTHING when no file changed:
 * nothing became stale, so there is nothing to say. /rewind deliberately gets
 * no note -- truncation removes the beliefs along with the work. Pure;
 * unit-tested. */
void jc_snapshot_undo_note(const char *label, const char *names,
                           struct jc_sb *out);

/* (Re)load the checkpoint stack from the shadow repo's git history (oldest
 * first), capped at the most recent JC_SNAPSHOT_MAX_LIST. Called at init so the
 * stack persists across sessions/--resume; also used by the CLI. No-op when
 * unavailable. */
void jc_snapshot_refresh(struct jc_snapshot_mgr *m);

/* Restore the work tree to the n-th most recent checkpoint (n=1 is the latest)
 * without popping. For the `undo` CLI subcommand. Returns JC_ERR_NOTFOUND when
 * n is out of range, JC_ERR_* on git failure; *label_out (if non-NULL) gets the
 * restored checkpoint's label. */
jc_status jc_snapshot_restore_index(struct jc_snapshot_mgr *m, int n,
                                    const char **label_out);

/* Without changing anything, append to `out` a human-readable preview of what
 * restoring the n-th most recent checkpoint would do: the tracked changes a
 * reset would revert (`git diff --stat`) and the untracked files a clean would
 * remove (`git clean -nd`). For `undo --dry-run`. Returns JC_ERR_NOTFOUND when
 * n is out of range; *label_out (if non-NULL) gets the checkpoint's label. */
jc_status jc_snapshot_preview_index(struct jc_snapshot_mgr *m, int n,
                                    struct jc_sb *out, const char **label_out);

/* Append the ONE-LINE blast radius of restoring the n-th checkpoint: the
 * `--shortstat` summary of the tracked changes it will revert, plus a count of
 * the untracked files it will remove. Call it BEFORE the restore -- afterwards
 * there is nothing left to measure. `undo` prints it on success, because until
 * M537 a revert of 768 files and a revert of none printed the same line. */
jc_status jc_snapshot_scope_index(struct jc_snapshot_mgr *m, int n,
                                  struct jc_sb *out);

/* Append to `out` a full patch of the work tree's changes since the n-th most
 * recent checkpoint (n=1 is the latest), including new files. Stages into the
 * shadow index only (`git add -A` against the shadow git-dir) so the user's own
 * .git/index is untouched, then `git diff --cached <commit>`. For self-review.
 * Returns JC_ERR_NOTFOUND when n is out of range or snapshots are unavailable. */
jc_status jc_snapshot_diff(struct jc_snapshot_mgr *m, int n, struct jc_sb *out);

/* List the paths changed in the work tree since `commit` (a shadow-repo SHA),
 * newline-separated, into `out` -- `git add -A` (shadow index) + `git diff
 * --cached --name-only <commit>`. Used by the M83 edit-scope out-of-scope guard
 * to see every file a run touched (including shell-introduced changes the
 * file-write fence can't see). Returns JC_ERR_NOTFOUND when unavailable. */
jc_status jc_snapshot_changed_since(struct jc_snapshot_mgr *m,
                                    const char *commit, struct jc_sb *out);

/* Conversational rewind support (M34c): compute the history truncation length to
 * rewind to the n-th most recent checkpoint (n=1 is the latest) -- the index of
 * the user message that triggered that checkpoint's turn, found by matching
 * checkpoint labels to user messages in order (jc_rewind_match). Truncating
 * `hist` to this length removes that turn and everything after it, so the
 * conversation lines up with the restored file state. Returns -1 when n is out
 * of range or no matching user message is found. Pure w.r.t. the filesystem. */
int jc_snapshot_rewind_cut(const struct jc_snapshot_mgr *m,
                           struct jc_history *hist, int n);

#ifdef __cplusplus
}
#endif
#endif /* JC_SNAPSHOT_H */
