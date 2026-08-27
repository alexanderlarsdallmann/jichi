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
 * entry owns a pid + a non-blocking read pipe, so the registry is malloc/free
 * managed (the arena cannot SIGKILL a pid or close an fd). Background children
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

enum jc_bg_state { JC_BG_RUNNING = 0, JC_BG_EXITED, JC_BG_KILLED, JC_BG_FREE };

struct jc_bg_proc {
    int            id;        /* 1-based handle (0 => free slot)           */
    long           pid;       /* child pgid/pid                            */
    int            fd;        /* non-blocking read end of its pipe, or -1  */
    int            state;     /* enum jc_bg_state                          */
    int            exit_code; /* valid once EXITED/KILLED                  */
    struct jc_sb   buf;       /* accumulated output (capped)               */
    jc_size        read_off;  /* bytes already returned by read_background */
    int            overflowed;/* output hit the cap and was truncated      */
    char           cmd[256];  /* the command, for status display           */
};

struct jc_bg_mgr {
    struct jc_bg_proc procs[JC_BG_MAX];
    int               next_id;
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

#ifdef __cplusplus
}
#endif
#endif /* JC_BG_H */
