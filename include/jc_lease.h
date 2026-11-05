/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lease.h - the per-workspace run lease (M431e).
 *
 * THE HAZARD. jichi has no lock of any kind: a `flock|lockf|LOCK_EX|pthread_mutex`
 * sweep over src/ and include/ returns a skip-list file extension, a scaffold
 * template string, and O_EXCL in the atomic-replace helper. What mutual exclusion
 * exists lives in examples/ (an atomic rename(2) task claim, and an in-process
 * threading.Lock in the web bridge) -- outside the binary, and useless across
 * processes.
 *
 * Meanwhile the autonomy envelope assumes ONE ACTOR PER TREE, and
 * `revertOutOfScope` makes that assumption load-bearing: the M83 end-of-turn sweep
 * diffs the whole tree against a run-start baseline and cannot tell a sibling run's
 * edits -- or a human's mid-run merge -- from an out-of-scope write by the model it
 * is policing. So a second run can cause the first to revert work nobody asked it
 * to touch. The collision surfaces are concrete: the shared shadow git repo under
 * ~/.jichi.d/checkpoints/<key> (two runs' notions of "green"), .jichi/memory.md and
 * .jichi/board.json (read-modify-WHOLE-FILE-write), and the global
 * calibration.json (last-writer-wins by its own comment). Atomicity keeps each
 * file valid; it does not prevent a lost update.
 *
 * WHAT THIS IS, AND IS NOT. An ADVISORY lease: a file naming the current holder,
 * consulted when an envelope arms. It does NOT fix the one-actor assumption -- it
 * makes violating it LOUD. Default is to warn and proceed, because two read-only
 * runs over one tree are routine (it is how an operator inspects a running job) and
 * refusing them to prevent a write-write hazard would be a cure worse than the
 * disease. `--lease fail` is there for a supervisor that wants serialisation.
 *
 * NOT flock: it dies with the process, which sounds like a feature until a crashed
 * run leaves no evidence it ever held the tree -- and "who was in here?" is exactly
 * the forensic question a supervisor asks. A file plus a liveness check answers it;
 * a released lock cannot.
 *
 * M760: a lock does guard the TAKE, and only the take. Read-then-write let two
 * runs started together both read "free" and both take the lease quietly. Taking,
 * and releasing, now happen under jc_lock_take on <key>.lock beside the record:
 * the record is still the evidence; the lock only makes reading it and replacing
 * it one step, and dying with its holder is exactly right for that.
 */
#ifndef JC_LEASE_H
#define JC_LEASE_H

#include "jc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What to do when another live run already holds this workspace. */
enum jc_lease_mode {
    JC_LEASE_WARN = 0,  /* say so and proceed (default)                     */
    JC_LEASE_FAIL,      /* refuse to start                                  */
    JC_LEASE_OFF        /* neither take nor consult a lease                 */
};

enum jc_lease_verdict {
    JC_LEASE_TAKE = 0,  /* free, or the holder is gone: take it quietly     */
    JC_LEASE_WARN_TAKE, /* held by a live run: say so, take it anyway       */
    JC_LEASE_REFUSE     /* held by a live run and the mode says stop        */
};

/* One holder's record. `run` is the envelope run id, so a warning names the id a
 * supervisor can find in ~/.jichi.d/runs/<run>.jsonl. */
struct jc_lease_info {
    char run[40];
    long pid;
    long started;       /* unix seconds */
    char mode[16];      /* the agent mode the holder runs in */
    /* M759: where the holder works and how to reach it, for `jichi ps`. Both
     * may be empty (an older record, or a run without --control); a reader
     * that does not know them ignores them, as for any added field. */
    char root[1024];    /* the workspace root */
    char control[1024]; /* its control socket, or "" */
};

/* Parse `--lease` / config. Returns 1 and sets *out, or 0 for an unknown word
 * (the caller reports it; a silently ignored posture flag is the M429 lesson).
 * Pure. */
int jc_lease_mode_parse(const char *s, enum jc_lease_mode *out);

/* The name of a mode, for messages and `doctor`. Pure. */
const char *jc_lease_mode_name(enum jc_lease_mode m);

/* The whole policy, in one place so a caller cannot invent a fourth outcome.
 * `held` is "a lease file was read"; `holder_alive` is whether its pid still
 * exists. A stale lease (held, not alive) is TAKEn quietly -- a crashed run must
 * not block the next one forever, which is the failure mode a lockfile is famous
 * for. Pure; unit-tested. */
enum jc_lease_verdict jc_lease_decide(int held, int holder_alive,
                                      enum jc_lease_mode m);

/* ~/.jichi.d/leases/<jc_workspace_key(work_tree)>[.<run>].json. Keyed by the
 * SAME derivation as the checkpoint repo, so one project is one number
 * everywhere. M1097 (D-09): that key is now the 64-hex SHA-256 digest of the
 * work-tree path (was a decimal djb2), so the filename is <64-hex>.json.
 *
 * M1101 (D-17): ONE RECORD PER HOLDER. The holder's run id is folded into the
 * name -- <key>.<run>.json -- so a second run that warns and proceeds writes
 * its OWN file beside the first run's instead of over it. `run` that is NULL or
 * empty yields the LEGACY bare name <key>.json (the pre-D-17 shape, still read
 * by the scan so a hand-planted or old-format record is honoured). Pure. */
void jc_lease_path(const char *home, const char *work_tree, const char *run,
                   char *buf, jc_size cap);

/* M1101 (D-17): walk the leases directory and count this workspace's records.
 *
 * Reads every <key>.<...>.json and the legacy <key>.json for THIS work_tree
 * (key = jc_workspace_key(work_tree), one derivation); other workspaces' files
 * and the <key>.lock are ignored. A record whose pid is alive is LIVE; one
 * whose pid is gone is STALE (taken-over material, left on disk -- jichi ps
 * counts it as evidence and removes nothing, so this never cleans it). Fills
 * *most_live with the most recently started LIVE record -- the one a warning
 * or a refusal should name -- on ties (same started) the lexicographically
 * NEWEST name wins, so the answer is deterministic. *n_live / *n_stale may be
 * NULL. Returns 1 when the directory was read (even if it held nothing), 0
 * when `home` is NULL or the directory is absent/unreadable. I/O. */
int jc_lease_scan(const char *home, const char *work_tree,
                  struct jc_lease_info *most_live, int *n_live, int *n_stale);

/* Render / parse the record. Pure; unit-tested as a round trip. `jc_lease_parse`
 * returns 1 on success, 0 on malformed input -- which is treated as "no lease"
 * rather than an error, since a truncated file must not wedge every later run. */
char *jc_lease_render(const struct jc_lease_info *in);
int jc_lease_parse(const char *json, struct jc_lease_info *out);

/* Is that pid still around? kill(pid, 0), with EPERM counted as alive (another
 * user's process is still a process). I/O. */
int jc_lease_pid_alive(long pid);

/* Consult and (unless refused or OFF) take the lease. Returns the verdict; fills
 * *holder when one was read, so the caller can name it. Never fails the run on
 * its own -- an unwritable lease directory warns and proceeds, because a
 * diagnostic must not become an outage. */
enum jc_lease_verdict jc_lease_acquire(const char *home, const char *work_tree,
                                       const struct jc_lease_info *me,
                                       enum jc_lease_mode mode,
                                       struct jc_lease_info *holder);

/* Drop ours. M1101 (D-17): the NAME is the identity -- it removes
 * <key>.<my_run>.json, which can only be our record (one file per holder), so it
 * cannot touch a sibling's: a run that warned-and-proceeded past someone else's
 * lease keeps ITS file on the way out. */
void jc_lease_release(const char *home, const char *work_tree,
                      const char *my_run);

#ifdef __cplusplus
}
#endif
#endif /* JC_LEASE_H */
