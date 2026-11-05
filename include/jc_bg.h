/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_bg.h - background command execution (M26).
 *
 * Claude Code parity for long-running commands (dev servers, watchers, builds):
 * run_terminal_command with run_in_background:true starts a detached process and
 * returns a shell id immediately; read_background_output (BashOutput) returns
 * new output since the last read plus the running/exited status; kill_background
 * (KillShell) terminates it.
 *
 * A bounded registry of processes lives on jc_app (struct jc_bg_mgr *bg). Each
 * entry owns a pid + an output spool file, so the registry is malloc/free
 * managed (the arena cannot SIGKILL a pid or unlink a file). The spool file is
 * why the child can never block on us: a pipe's reader only ran at tool-call
 * boundaries, so a chatty child stalled until the next one (the bg-output
 * stall, M940) -- a file's appends do not wait on a reader at all. Children
 * are drained opportunistically at tool-call boundaries and reaped at session
 * exit -- they intentionally survive an aborted turn (the point of detaching).
 *
 * Only the local fork path is supported (the ACP terminal delegate is a single
 * blocking call with no poll/kill surface); when an editor terminal is active,
 * run_in_background reports that it is unavailable.
 *
 * See docs/BACKGROUND.md.
 */
#ifndef JC_BG_H
#define JC_BG_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

#define JC_BG_MAX 8              /* concurrent background processes        */
#define JC_BG_BUF_MAX (256*1024) /* captured output cap per process (bytes)*/
/* def-326: a build/test log's verdict is at its TAIL, so when the output
 * over-runs JC_BG_BUF_MAX the last JC_BG_TAIL_MAX bytes are kept in a ring and
 * delivered on the final read, instead of being dropped. 8 KiB covers a summary
 * line and its context generously while bounding the over-cap view. */
#define JC_BG_TAIL_MAX (8*1024)
/* def-324 (M962), superseded in its running-time effect by L-27/M984
 * (operator decision 2026-10-19 08:00): the on-disk spool file is NOT capped
 * while the child runs -- it is unbounded for the whole run until the child
 * exits (the running-time disk bound is deferred, L-22, to a loss-free
 * redesign: RLIMIT_FSIZE in the child or a periodic drain). The cap binds at
 * REAP: the final drain rewrites the file to its last 512 KiB (= 2x
 * JC_BG_BUF_MAX (256 KiB head) + room for the 8 KiB tail ring, so the on-disk
 * file can never hold less than what the in-memory view retains) and reap
 * unlinks it. The pre-L-27 rewrite ran on every drain that saw the file
 * oversized, which raced a still-writing child and destroyed its output. */
#define JC_BG_SPOOL_MAX (512*1024)

enum jc_bg_state { JC_BG_RUNNING = 0, JC_BG_EXITED, JC_BG_KILLED, JC_BG_FREE };

struct jc_bg_proc {
    int            id;        /* 1-based handle (0 => free slot)           */
    long           pid;       /* child pgid/pid                            */
    char           spool[128];/* its output spool file, '' when none (M940)*/
    long           spool_pos; /* bytes already consumed from the spool     */
    int            state;     /* enum jc_bg_state                          */
    int            exit_code; /* valid once EXITED/KILLED                  */
    struct jc_sb   buf;       /* accumulated output (capped)               */
    jc_size        read_off;  /* bytes already returned by read_background */
    int            overflowed;/* output hit the cap and was truncated      */
    char           cmd[256];  /* the command, for status display           */
    char           tail[JC_BG_TAIL_MAX]; /* def-326: last N bytes   */
    jc_size        tail_len;  /* bytes currently held in the tail ring     */
    int            tail_done; /* the tail has been delivered to a reader   */
};

struct jc_bg_mgr {
    struct jc_bg_proc procs[JC_BG_MAX];
    int               next_id;
    char              spool_dir[128]; /* ~/.jichi.d/bgspool, '' if absent */
};

void jc_bg_mgr_init(struct jc_bg_mgr *m);

/* SIGTERM -> SIGKILL -> reap every live child, close fds, free buffers. */
void jc_bg_mgr_free(struct jc_bg_mgr *m);

/* Start `command` (via /bin/sh -c) detached in its own process group. On
 * success returns the new id (>0) and the slot is RUNNING; returns 0 when the
 * registry is full and -1 on a spawn error. */
int jc_bg_start(struct jc_bg_mgr *m, const char *command);

/* Non-blocking: drain any pending output from every live child and reap those
 * that have exited. Safe to call often (tool-call boundaries). */
void jc_bg_poll(struct jc_bg_mgr *m);

/* Append output produced since the caller's last read for `id` to `out`, plus a
 * status line. Returns JC_OK, or JC_ERR_NOTFOUND for an unknown id. */
jc_status jc_bg_read(struct jc_bg_mgr *m, int id, struct jc_sb *out);

/* Terminate `id` (SIGTERM, brief grace, then SIGKILL) and reap it. Returns
 * JC_OK or JC_ERR_NOTFOUND. */
jc_status jc_bg_kill(struct jc_bg_mgr *m, int id);

/* def-324: return the error message from the last spool-open failure,
 * or "" if none. Single-threaded manager, so no locking needed. */
const char *jc_bg_spool_error(void);

#ifdef __cplusplus
}
#endif
#endif /* JC_BG_H */
