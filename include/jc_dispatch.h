/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_dispatch.h - the dirty-tree dispatch probe (M1006; M1006-a refinements).
 *
 * A WRITE dispatch -- `spawn_parallel` with write tasks, or `spawn_subagent`
 * with the mutating tools -- is the only dispatch that changes the workspace:
 * the parallel children fork worktrees off the current HEAD and 3-way merge
 * back, and an in-process write subagent edits the LIVE workspace directly.
 * When the tree is DIRTY at dispatch time the base is unidentifiable, unverified
 * work is built on, and no record survives to say so. The guard checks the live
 * workspace before any fork and acts on the result per a config mode
 * (strictness.dirtyDispatch: off | warn | refuse, default warn).
 *
 * The guard's UNIVERSE, stated here once because both call sites need it (the
 * M852 "say it once" principle):
 *
 *   * READ-ONLY DISPATCHES ARE NOT CHECKED, in any mode. A read-only reviewer
 *     of an uncommitted diff must stay possible -- no fork and no merge happen,
 *     so there is no dirty-base problem to guard against.
 *   * The probe reads the LIVE workspace, NEVER the shadow snapshot repo:
 *     `git -C <root>` (root = the canonical workspace, app->root), the same
 *     command `relay_git_facts` runs (src/main.c). A workspace jichi cannot
 *     read is not a dirty workspace, and the guard must not refuse work on a
 *     non-git dir.
 *   * `off` skips the check entirely -- no probe, no telemetry event.
 *
 * M1006-a: the probe now answers THREE states, not two. `git status` is run
 * with an argv array (NO shell -- a single quote in the workspace path broke
 * the old `git -C '<root>'` string; the argv form passes the path intact), and
 * its EXIT STATUS is read: exit 0 with output is dirty, exit 0 with no output
 * is clean, and a nonzero exit (or a spawn/read failure) is UNKNOWN -- the
 * classifier's else-branch says unknown (M519/M628), never clean. The one
 * exception: a workspace that is NOT a git repository (rev-parse fails) is
 * not unknown but NOT-DIRTY -- that distinction is the "a missing repo omits
 * the line rather than asserting a falsehood" rule.
 */
#ifndef JC_DISPATCH_H
#define JC_DISPATCH_H

#include <stddef.h>   /* size_t */

/* What the probe could determine about the live workspace. */
enum jc_dispatch_state {
    JC_DISPATCH_CLEAN = 0,  /* git ran, exit 0, no porcelain output      */
    JC_DISPATCH_DIRTY,      /* git ran, exit 0, porcelain reported paths */
    JC_DISPATCH_UNKNOWN     /* git did not run successfully IN A GIT
                             * WORKSPACE (corrupt .git/index, an error
                             * mid-status, a spawn failure): the tree state
                             * cannot be read. Callers PROCEED with a note
                             * (an infra failure must not block work) and
                             * telemetry says "unknown", never "no". */
};

/* Probe the live workspace at `root` for uncommitted changes, `git status
 * --porcelain` semantics (tracked modified/staged and untracked non-ignored
 * files). No shell: the command is spawned as argv
 * {"git", "-C", root, "status", "--porcelain"} -- the path with a single quote
 * (or a space, or any metacharacter) reaches git intact. The exit status is
 * checked; a failure that rev-parse --is-inside-work-tree shows to be a
 * NON-GIT workspace reads JC_DISPATCH_CLEAN-equivalent (not dirty); a failure
 * in a workspace that IS a git repository reads JC_DISPATCH_UNKNOWN.
 *
 *   *state    <- JC_DISPATCH_CLEAN / DIRTY / UNKNOWN
 *   *npaths   <- the number of dirty paths seen (0 unless DIRTY)
 *   paths     <- the first max_named dirty paths, comma-joined ("a, b");
 *                left "" unless DIRTY (or when `paths` is NULL)
 *   cap       <- size of `paths` (0 when `paths` is NULL)
 *   max_named <- how many paths to name (small -- 5 at both call sites)
 *
 * Bounded, pure C: no arena (the call sites pass their own buffers); the git
 * output is read into a bounded local buffer. `root` NULL/empty is CLEAN
 * (not dirty). Always returns 0. */
int jc_dispatch_dirty_probe(const char *root, int *state, int *npaths,
                            char *paths, size_t cap, int max_named);

#endif /* JC_DISPATCH_H */
