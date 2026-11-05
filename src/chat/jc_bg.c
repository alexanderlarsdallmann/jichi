/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_bg.c - background command execution (see jc_bg.h).
 *
 * Process discipline mirrors the fork pool in jc_tool_parallel.c (pipe, fork,
 * select/non-blocking drain, SIGTERM->SIGKILL reap), and the malloc/free-of-OS-
 * handles lifecycle mirrors jc_user_tool_mgr.
 */

#include "jc_bg.h"
#include "jc_platform.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_proc.h"
#include "jc_spawn.h"   /* M832 */
#include "jc_fault.h"   /* M984: FAULT pause for the spool rewrite race */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "jc_fcntl.h"   /* JC_O_BINARY (M837) */
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* def-324: error message buffer for spool-open failures.
 * Single-threaded manager, so no locking needed. */
static char s_spool_err[512] = "";

/* M940: remove stale spools (bg-<digits>.out) left by a session that died
 * without a clean jc_bg_mgr_free. The dir holds nothing but our own files, so
 * the exact shape is the ownership test. Best-effort: a failed unlink is
 * skipped (the next sweep, or the owning session's start, still reclaims it
 * via O_TRUNC). */
static void sweep_stale_spools(const char *dir)
{
    DIR *d;
    struct dirent *ent;
    static char path[256];
    if (dir == NULL || dir[0] == '\0') {
        return;
    }
    d = opendir(dir);
    if (d == NULL) {
        return;
    }
    while ((ent = readdir(d)) != NULL) {
        const char *nm = ent->d_name;
        jc_size i, len;
        struct stat st;
        int all_digits = 1;
        /* Never unlink a spool a LIVE child is still writing: another jichi
         * of the same user may be running (two terminals, the e2e tier) and bg
         * ids restart at 1 each session, so bg-1.out collides. On Linux an open
         * file keeps st_nlink == 1 (only hard links raise it), so the liveness
         * test is the age: a fresh spool is one being written; an old one is
         * residue of a session that died between write and unlink (the
         * reap-time unlink is the normal path; this sweep is the fallback).
         * 24 h outlives any realistic run; it only costs the residue's
         * eventual cleanup, which the next session's sweep repeats. */
        jc_snprintf(path, sizeof(path), "%s/%s", dir, nm);
        if (stat(path, &st) != 0 || st.st_mtime > time(NULL) - 86400L) {
            continue;
        }
        if (strncmp(nm, "bg-", 3) != 0) {
            continue;
        }
        len = strlen(nm);
        if (len < 3 + 1 + 4) { /* bg-<one digit>.out at shortest */
            continue;
        }
        if (strcmp(nm + len - 4, ".out") != 0) {
            continue;
        }
        /* Shape: bg-<digits>-<digits>.out (id-pid, one hyphen). Both runs must
         * be non-empty digits. */
        all_digits = 0;
        {
            jc_size a = 0, b = 0;
            int hyphen = 0;
            for (i = 3; i + 4 < len; i++) {
                if (nm[i] == '-') {
                    hyphen = 1;
                    continue;
                }
                if (nm[i] < '0' || nm[i] > '9') {
                    break;
                }
                if (hyphen) {
                    b++;
                } else {
                    a++;
                }
            }
            if (i + 4 == len && hyphen == 1 && a > 0 && b > 0) {
                all_digits = 1;
            }
        }
        if (!all_digits) {
            continue;
        }
        jc_snprintf(path, sizeof(path), "%s/%s", dir, nm);
        unlink(path);
    }
    closedir(d);
}

void jc_bg_mgr_init(struct jc_bg_mgr *m)
{
    int i;
    m->spool_dir[0] = '\0';
    for (i = 0; i < JC_BG_MAX; i++) {
        m->procs[i].id = 0;
        m->procs[i].pid = 0;
        m->procs[i].spool[0] = '\0';
        m->procs[i].spool_pos = 0;
        m->procs[i].state = JC_BG_FREE;
        m->procs[i].exit_code = -1;
        m->procs[i].read_off = 0;
        m->procs[i].overflowed = 0;
        m->procs[i].cmd[0] = '\0';
        m->procs[i].tail_len = 0;
        m->procs[i].tail_done = 0;
        jc_sb_init(&m->procs[i].buf);
    }
    m->next_id = 1;
    /* M940: the spool directory, created once up front. A failed mkdir is not
     * fatal here (jc_bg_start reports it as a spawn error when it first bites);
     * leaving spool_dir empty keeps the "no dir" path tested. It is deliberately
     * under ~/.jichi.d, OUTSIDE the workspace and the snapshot/rollback blast
     * radius (ANECDOTES #1: observability lives outside the tree it watches). */
    {
        char dir[160];
        jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/bgspool", jc_home_dir());
        /* 0700 (M488): a spool holds whatever a background command printed --
         * build output, logs, and in principle secrets the command echoed. */
        if (jc_mkdir_p_private(dir) == JC_OK) {
            jc_snprintf(m->spool_dir, sizeof(m->spool_dir), "%s", dir);
        }
    }
    /* A session that died without a clean jc_bg_mgr_free (kill -9, crash)
     * leaves its spools behind; this is the boot-side half of that cleanup.
     * Only bg-N.out is ours: the directory is private to the bg registry, so
     * anything with that exact shape is a stale spool. */
    sweep_stale_spools(m->spool_dir);
}

static struct jc_bg_proc *find(struct jc_bg_mgr *m, int id)
{
    int i;
    for (i = 0; i < JC_BG_MAX; i++) {
        if (m->procs[i].state != JC_BG_FREE && m->procs[i].id == id) {
            return &m->procs[i];
        }
    }
    return NULL;
}

/* Reap if the child has exited; sets state + exit_code. */
static void reap(struct jc_bg_proc *p);

/* M940: pull whatever the child has written to its spool file into its buf.
 * The file stands in for the old pipe: the child's writes never block on us,
 * and we read it at tool-call boundaries, keeping the head of the output under
 * the JC_BG_BUF_MAX cap (M26) exactly as before. Reopening per drain is cheap
 * (a short lseek + read) and keeps no fd alive across tool calls -- the arena
 * cannot close one, and a leaked fd is how M26's registry got malloc/free. */
static void drain(struct jc_bg_proc *p)
{
    char chunk[4096];
    int fd;
    long got = 0;
    if (p->spool[0] == '\0') {
        return;
    }
    fd = open(p->spool, O_RDONLY | JC_O_BINARY);
    if (fd < 0) {
        return;
    }
    if (lseek(fd, (off_t)p->spool_pos, SEEK_SET) != (off_t)p->spool_pos) {
        close(fd);
        return;
    }
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n > 0) {
            got += (long)n;
            if (p->buf.len < (jc_size)JC_BG_BUF_MAX) {
                jc_size room = (jc_size)JC_BG_BUF_MAX - p->buf.len;
                jc_size take = ((jc_size)n < room) ? (jc_size)n : room;
                jc_sb_append_n(&p->buf, chunk, take);
            }
            /* else: cap reached, keep the head (this chunk is not appended).
             * L-20 (M963): mark overflow ONLY when bytes were really dropped
             * -- more exist on disk than the head retains. The pre-fix test
             * `n >= room` also fired when the cap's edge landed exactly on a
             * read boundary (a total of precisely JC_BG_BUF_MAX), where
             * nothing was dropped; that flag then flushed the ring behind a
             * head that already held its same last JC_BG_TAIL_MAX bytes (the
             * lines delivered twice) and read "output truncated" for a
             * complete output. Checked after the append: `take` may be the
             * whole chunk (nothing dropped) or less (the rest dropped). */
            if (p->spool_pos + got > (long)p->buf.len) {
                p->overflowed = 1;
            }
            /* def-326: the ring holds the last JC_BG_TAIL_MAX bytes of
             * EVERYTHING drained, so at exit the tail (a build log's summary
             * line) can be delivered instead of dropped by the head-only
             * JC_BG_BUF_MAX cap. `n` is at most the 4096-byte chunk, which is
             * below JC_BG_TAIL_MAX, so a single read can never fill the ring
             * on its own; there are two cases by how the chunk sizes against
             * the ring's current contents. */
            {
                jc_size cn = (jc_size)n;
                if (p->tail_len + cn <= JC_BG_TAIL_MAX) {
                    /* everything fits: append the chunk at the end */
                    memcpy(p->tail + p->tail_len, chunk, cn);
                    p->tail_len += cn;
                } else {
                    /* the ring overflows: shift the oldest bytes out, then
                     * append the whole chunk */
                    jc_size drop = p->tail_len + cn - JC_BG_TAIL_MAX;
                    memmove(p->tail, p->tail + drop, p->tail_len - drop);
                    p->tail_len -= drop;
                    memcpy(p->tail + p->tail_len, chunk, cn);
                    p->tail_len += cn;
                }
            }
            continue;
        }
        if (n == 0) {
            break; /* EOF: the child has written everything it will */
        }
        if (errno == EINTR) continue;
        break; /* I/O error: treat as end-of-data, do not spin */
    }
    p->spool_pos += got;
    close(fd);

    /* def-324: cap the on-disk spool file. After reading new bytes,
     * if the file exceeds JC_BG_SPOOL_MAX, rewrite the last JC_BG_SPOOL_MAX
     * bytes to the front and truncate. The parent's spool_pos tracks bytes
     * consumed; after the rewrite, adjust it so the next drain reads from
     * the correct position (the new file contains the last cap bytes, and
     * spool_pos should indicate how many of those have been consumed).
     *
     * GATE: the destructive on-disk rewrite runs ONLY when the child has stopped
     * writing. While p->state == JC_BG_RUNNING, drain() still performs its
     * non-destructive read (the p->buf head and the def-326 p->tail ring,
     * unchanged) but SKIPS the rewrite entirely. When p->state is not RUNNING
     * (JC_BG_EXITED -- reap() sets it before its final drain at line 385),
     * it performs the rewrite exactly as below.
     *
     * This gate makes the comment's claim TRUE: rewrite only after the child
     * stopped writing, eliminating the race where bytes the child appends
     * between the size-read and O_TRUNC are destroyed. The in-memory caps
     * (JC_BG_BUF_MAX head, JC_BG_TAIL_MAX ring) already bound the running case.
     *
     * Formula: new_spool_pos = max(0, old_spool_pos - (old_size - cap)).
     * This subtracts the bytes we threw away from the consumed count.
     * Example: old_size=20, cap=8, tail_start=12, old_spool_pos=4.
     * Bytes 0-11 are thrown away, so spool_pos = max(0, 4 - 12) = 0.
     * Next drain reads from offset 0 (bytes 12-19, now at 0-7).
     * If old_spool_pos=15, spool_pos = max(0, 15 - 12) = 3.
     * Next drain reads from offset 3 (original bytes 15-19, now at 3-7).
     *
     * L-27: the operator (2026-10-19 08:00) accepted this gate openly giving
     * up the running-time disk bound: the spool file is UNBOUNDED for the
     * whole run, not only between drains, until the child exits (a 5-minute
     * `yes` child fills the disk). The bound returns with a loss-free
     * redesign (DEFERRED, L-22: RLIMIT_FSIZE in the child before exec, or a
     * periodic drain). The deterministic tooth: JICHI_FAULT_BGREWRITE_PAUSE_MS
     * sleeps in the read->O_TRUNC window; with the gate removed a mid-run drain
     * rewrites while the child appends and loses the tail (L27_VERDICT). */
    {
        long old_size;
        long cap = (long)JC_BG_SPOOL_MAX;
        int rfd;
        int wfd;

        if (p->spool[0] == '\0') {
            return;
        }

        /* Open for reading to get the file size. */
        rfd = open(p->spool, O_RDONLY | JC_O_BINARY);
        if (rfd < 0) {
            return;
        }
        old_size = (long)lseek(rfd, 0, SEEK_END);
        close(rfd);
        if (old_size <= cap) {
            return; /* no need to cap */
        }

        /* GATE: skip the on-disk rewrite if the child is still running.
         * While p->state == JC_BG_RUNNING, drain() still performs its
         * non-destructive read but SKIPS the destructive rewrite. */
        if (p->state == JC_BG_RUNNING) {
            return;
        }

        /* Read the last cap bytes into a buffer. */
        {
            char *buf = NULL;
            long to_read = cap;
            long read_pos = old_size - cap;
            long read_total = 0;

            /* Use the existing chunk buffer if it's big enough, else malloc. */
            if (to_read <= (long)sizeof(chunk)) {
                buf = chunk;
            } else {
                buf = (char *)malloc((size_t)to_read);
                if (buf == NULL) {
                    return; /* best effort: leave the file as-is */
                }
            }

            /* Open for reading from the position where we'll start reading. */
            rfd = open(p->spool, O_RDONLY | JC_O_BINARY);
            if (rfd >= 0) {
                if (lseek(rfd, read_pos, SEEK_SET) == read_pos) {
                    while (read_total < to_read) {
                        long remaining = to_read - read_total;
                        long n = read(rfd, buf + read_total,
                                      (size_t)(remaining > (long)sizeof(chunk) ?
                                               (long)sizeof(chunk) : remaining));
                        if (n <= 0) {
                            if (errno == EINTR) continue;
                            break;
                        }
                        read_total += n;
                    }
                }
                close(rfd);
            }

            /* L-27 tooth: sleep in the read->O_TRUNC window (M984), immediately
             * before the destructive open and after the last-cap-bytes read
             * that decides what the file will become. With the gate removed,
             * a mid-run drain rewrites here while the child appends: the
             * child's bytes written during the pause are truncated away, up to
             * the tail. With the gate the pause is reached only at reap, when
             * the child has stopped, so nothing is destroyed. Compiled to
             * nothing without JC_FAULT; disabled (0 ms) unless
             * JICHI_FAULT_BGREWRITE_PAUSE_MS is set (M503: a FAULT site
             * defaults to off). */
            jc_fault_bgrewrite_pause();

            /* M996 (L-27 follow-up): rewrite the kept tail to a TEMPORARY file
             * in the spool's own directory, then rename() it over the spool.
             * The pre-M996 rewrite opened the spool O_TRUNC and wrote in
             * place: on a short write (ENOSPC, EIO) the original had already
             * been destroyed, so the very tail the cap exists to keep was
             * gone; and the unchecked ftruncate() then extended the file with
             * NULs up to a stale size (the warning the -O2 fortified build
             * names is a symptom, the two failures above are the finding).
             * temp+rename has neither: a failed temp write leaves the spool
             * byte-for-byte as it was, and a successful rename makes the
             * spool EXACTLY the temp file's size (no truncate call at all,
             * so no NUL padding), atomically -- a concurrent reader sees
             * either the old file or the new one. The temp is in the same
             * directory, so the rename is atomic (same filesystem). A crash
             * between the temp write and the rename leaves one .tmp file in
             * bgspool -- sweep_stale_spools' ownership shape is the exact
             * bg-<pid>-<start>.out name, so a .tmp is not swept by it, and
             * O_EXCL would make the next drain's open fail forever (every
             * later rewrite of that spool would then be skipped). The temp
             * exists only between the open and the rename, so a .tmp found
             * on the next drain is always a crash orphan -- unlink it first
             * and proceed. A FAILED rewrite (short write, rename error)
             * keeps the oversized-but-intact file rather than a truncated
             * one: that is the whole point of the temp. */
            {
                char tmp[160];
                int tlen;

                tlen = jc_snprintf(tmp, sizeof(tmp), "%s.tmp%ld",
                                   p->spool, (long)getpid());
                if (tlen < 0 || (jc_size)tlen >= sizeof(tmp)) {
                    /* name unformable: best-effort, keep the file as-is.
                     * buf is the stack chunk unless malloc'd (to_read >
                     * sizeof(chunk)); freeing the chunk would be the same
                     * bug class as M197/M198/M199. */
                    if (buf != chunk && buf != NULL) {
                        free(buf);
                    }
                    return;
                }
                (void)unlink(tmp); /* crash orphan from a prior run, if any */
                wfd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | JC_O_BINARY, 0600);
                if (wfd >= 0) {
                    long written = 0;
                    long limit;
                    int write_ok = 1;

                    /* M996 FAULT site: cap the temp's first successful write to
                     * a short count (0 = the open "succeeds" but the first write
                     * reports failure), so the driver can prove a short write
                     * keeps the original spool intact. Returns -1 when disabled
                     * (the write loop's own condition is the honest one). */
                    limit = jc_fault_bgwrite_short_at();
                    while (written < read_total) {
                        long remaining = read_total - written;
                        long nmax = (remaining > (long)sizeof(chunk)) ?
                                    (long)sizeof(chunk) : remaining;
                        if (limit >= 0 && (long)written + nmax > limit) {
                            nmax = (long)limit - written;
                        }
                        if (nmax <= 0) {
                            break; /* the fault said: stop here (short write) */
                        }
                        {
                            long n = write(wfd, buf + written, (size_t)nmax);
                            if (n <= 0) {
                                write_ok = 0;
                                break;
                            }
                            written += n;
                        }
                    }
                    (void)close(wfd);
                    if (write_ok && written == read_total) {
                        if (rename(tmp, p->spool) == 0) {
                            /* The spool now holds exactly read_total bytes;
                             * spool_pos tracks it as before the rewrite. */
                            if (p->spool_pos > old_size - cap) {
                                p->spool_pos = p->spool_pos - (old_size - cap);
                            } else {
                                p->spool_pos = 0;
                            }
                        } else {
                            (void)unlink(tmp); /* rename failed: drop the temp, keep the spool */
                        }
                    } else {
                        (void)unlink(tmp); /* short/failed write: the original spool stands */
                    }
                    /* M996 FAULT window: pause after the rewrite attempt --
                     * successful or short -- so a test can observe the spool
                     * before reap() unlinks it (the normal path has no such
                     * window: reap's drain and unlink are back-to-back).
                     * One call on every rewrite path, so the probe's first
                     * jc_bg_read poll lands deterministically in it. */
                    jc_fault_bgwrite_pause();
                }
                /* else: the temp open failed (EEXIST after a crash is the only
                 * expected shape); the spool is left as-is, the rewrite is
                 * best-effort, and the next reap-time drain retries. */
            }

            /* Free if we allocated. */
            if (buf != chunk && buf != NULL) {
                free(buf);
            }
        }
    }
}

/* Reap if the child has exited; sets state + exit_code. */
static void reap(struct jc_bg_proc *p)
{
    int status;
    pid_t r;
    if (p->state != JC_BG_RUNNING) {
        return;
    }
    r = waitpid((pid_t)p->pid, &status, WNOHANG);
    if (r == (pid_t)p->pid) {
        if (WIFEXITED(status)) {
            p->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            p->exit_code = 128 + WTERMSIG(status);
        } else {
            p->exit_code = -1;
        }
        p->state = JC_BG_EXITED;
        /* M940: the child is gone, so its spool file is safe to remove.
         * drain() once more first: the shell may have flushed its last bytes
         * between our poll and this waitpid. */
        drain(p);
        if (p->spool[0] != '\0') {
            unlink(p->spool);
            p->spool[0] = '\0';
        }
    }
}

int jc_bg_start(struct jc_bg_mgr *m, const char *command)
{
    int i;
    int sfd;
    struct jc_bg_proc *p = NULL;
    pid_t pid;

    if (command == NULL || command[0] == '\0') {
        return -1;
    }
    /* Prefer a free slot; otherwise reclaim a finished (exited/killed) one so a
     * long session isn't capped at JC_BG_MAX *lifetime* background commands. A
     * still-RUNNING slot is never reused. */
    for (i = 0; i < JC_BG_MAX; i++) {
        if (m->procs[i].state == JC_BG_FREE) {
            p = &m->procs[i];
            break;
        }
    }
    if (p == NULL) {
        for (i = 0; i < JC_BG_MAX; i++) {
            if (m->procs[i].state == JC_BG_EXITED ||
                m->procs[i].state == JC_BG_KILLED) {
                p = &m->procs[i];
                jc_sb_free(&p->buf); /* release the prior capture before reuse */
                break;
            }
        }
    }
    if (p == NULL) {
        return 0; /* all slots are running */
    }
    /* M940: the child's stdout+stderr go to a spool FILE, not a pipe. A pipe's
     * reader only runs at tool-call boundaries, so a chatty child (a gate
     * prints ~2,600 lines) fills the 64 KiB and blocks in write(2) for minutes
     * at a time -- the bg-output stall, observed 2026-10-12. A file never
     * waits on us; the output also survives jichi dying. The id is allocated
     * BEFORE the file so the name is per-start (ids only ever increase).
     *
     * def-324: open with O_APPEND so child writes always land at EOF.
     * The file's end is the source of truth for what the child has written,
     * and the parent's lseek-to-spool_pos reads are unchanged (drain always
     * reads from the last consumed position to EOF; a cap-rewrite may move
     * bytes, which is the point). O_TRUNC still clears at open (the dir is
     * private, the sweep removes stale ones, and a reused slot's name is
     * id-unique); O_APPEND makes every child write land at EOF. */
    if (m->spool_dir[0] == '\0') {
        return -1;
    }
    p->id = m->next_id++;
    /* bg ids restart at 1 each session; the parent pid keeps two concurrent
     * jichi of one HOME from truncating each other's live spool (review
     * finding, 2026-10-12). */
    jc_snprintf(p->spool, sizeof(p->spool), "%s/bg-%ld-%ld.out", m->spool_dir,
                (long)p->id, (long)getpid());
    sfd = open(p->spool, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | JC_O_BINARY, 0600);
    if (sfd < 0) {
        /* def-324: set error message with path, errno, and fence hint.
         * Under 512 chars, per jc_snprintf constraint. The path is still in
         * p->spool at this point; clear it only AFTER formatting the error. */
        jc_snprintf(s_spool_err, sizeof(s_spool_err),
                    "spool open failed: %s: %s (errno=%d) "
                    "if this run is fenced (spawn_parallel write child), "
                    "the shell fence may not grant $HOME/.jichi.d/bgspool "
                    "-- see M961", p->spool, strerror(errno), errno);
        p->spool[0] = '\0';
        return -1;
    }
    /* CLOEXEC (O_CLOEXEC is outside this tree's 200112 baseline): the child
     * keeps only the dup2'd copy, M472's contract. */
    fcntl(sfd, F_SETFD, FD_CLOEXEC);
    /* M832: through jc_spawn -- own process group (so kill reaches
     * grandchildren), both std streams to the spool, keys scrubbed, on either
     * backend. The header's "same fd for stdout and stderr" contract (M26's
     * "2>&1" wiring) is unchanged; O_CLOEXEC keeps the child to the dup2'd copy. */
    {
        struct jc_spawn_req sr;
        memset(&sr, 0, sizeof sr);
        sr.shell_cmd = command;
        /* M844 (ACP-1): never jichi's own stdin -- over ACP it is the client's
         * JSON-RPC stream, and a command that reads stdin ate its next message
         * (a session/cancel). tests/smoke/acp_child_stdin.sh holds this. */
        sr.fd_stdin = JC_SPAWN_DEVNULL;
        sr.fd_stdout = sfd;
        sr.fd_stderr = sfd;
        sr.new_pgroup = 1;
        sr.scrub_env = 1;
        if (jc_spawn(&sr, &pid) != JC_OK) {
            close(sfd);
            unlink(p->spool);
            p->spool[0] = '\0';
            return -1;
        }
    }
    close(sfd); /* the child holds its copy; we re-open per drain */
    p->pid = (long)pid;
    p->spool_pos = 0;
    p->state = JC_BG_RUNNING;
    p->exit_code = -1;
    p->read_off = 0;
    p->overflowed = 0;
    p->tail_len = 0;
    p->tail_done = 0;
    jc_sb_init(&p->buf);
    jc_snprintf(p->cmd, sizeof(p->cmd), "%s", command);
    return p->id;
}

void jc_bg_poll(struct jc_bg_mgr *m)
{
    int i;
    for (i = 0; i < JC_BG_MAX; i++) {
        struct jc_bg_proc *p = &m->procs[i];
        if (p->state == JC_BG_RUNNING) {
            drain(p);
            reap(p);
        }
    }
}

jc_status jc_bg_read(struct jc_bg_mgr *m, int id, struct jc_sb *out)
{
    struct jc_bg_proc *p = find(m, id);
    if (p == NULL) {
        return JC_ERR_NOTFOUND;
    }
    drain(p);
    reap(p);
    if (p->buf.len > p->read_off) {
        jc_sb_append_n(out, p->buf.data + p->read_off,
                       p->buf.len - p->read_off);
        p->read_off = p->buf.len;
    }
    /* def-326: at exit the ring holds the last JC_BG_TAIL_MAX bytes of
     * the WHOLE run, so a chatty command's verdict (the final summary line) is
     * delivered here even though the head-only buffer capped long ago.
     * `overflowed` is the flush gate: without it the total output fits the
     * buffer and the ring only duplicates its last bytes. L-27's gate on the
     * on-disk REWRITE does not touch this: `overflowed` is set in the
     * non-destructive read phase, which still runs while the child is
     * running, so a truncated run overflows as before. */
    if (p->state != JC_BG_RUNNING && p->overflowed &&
        !p->tail_done && p->tail_len > 0) {
        long mid;
        p->tail_done = 1;
        /* Bytes neither the buffer's head nor the ring's tail retain. */
        mid = (long)p->spool_pos - (long)p->buf.len - (long)p->tail_len;
        if (mid > 0) {
            jc_sb_append_fmt(out, "\n[... %ld middle bytes not shown ...]", mid);
        }
        /* L-20 (M963): mid can be NEGATIVE -- in the band
         * JC_BG_BUF_MAX <= total < JC_BG_BUF_MAX + JC_BG_TAIL_MAX the head's
         * last -mid bytes ARE the ring's first -mid bytes (the 8 KiB tail
         * overlaps the 256 KiB head's end). Flushing the whole ring there
         * re-delivered those -mid bytes after the head, so a reader saw the
         * same lines twice with no marker between. Flush only the part of
         * the ring the head does not already hold. At mid == 0 the whole
         * ring is new (total exactly cap + TAIL_MAX); overflowed is only
         * set when bytes were really dropped, so mid < 0 here means the
         * overlap, not a complete output. */
        if (mid < 0) {
            jc_size skip = (jc_size)(-mid);
            if (skip > p->tail_len) {
                skip = p->tail_len;
            }
            jc_sb_append_n(out, p->tail + skip, p->tail_len - skip);
        } else {
            jc_sb_append_n(out, p->tail, p->tail_len);
        }
    }
    if (out->len == 0) {
        jc_sb_append(out, "(no new output)");
    }
    if (p->state == JC_BG_RUNNING) {
        jc_sb_append_fmt(out, "\n[background %d: running, pid %ld]", id, p->pid);
    } else {
        jc_sb_append_fmt(out, "\n[background %d: exited, status %d]",
                         id, p->exit_code);
    }
    if (p->overflowed) {
        jc_sb_append(out, " [output truncated]");
    }
    return JC_OK;
}

jc_status jc_bg_kill(struct jc_bg_mgr *m, int id)
{
    struct jc_bg_proc *p = find(m, id);
    int waited;
    if (p == NULL) {
        return JC_ERR_NOTFOUND;
    }
    if (p->state == JC_BG_RUNNING) {
        /* Signal the whole group; grandchildren (e.g. a dev server's workers)
         * go too. */
        kill(-(pid_t)p->pid, SIGTERM);
        for (waited = 0; waited < 10; waited++) {
            reap(p);
            if (p->state != JC_BG_RUNNING) {
                break;
            }
            jc_sleep_ms(50, NULL);
        }
        if (p->state == JC_BG_RUNNING) {
            kill(-(pid_t)p->pid, SIGKILL);
            waitpid((pid_t)p->pid, NULL, 0);
            p->state = JC_BG_KILLED;
        }
        /* else it exited on its own during the grace window: reap() already set
         * EXITED + the real exit_code -- don't clobber it with KILLED. */
    }
    drain(p); /* the child's remaining output, before the slot can be reused */
    return JC_OK;
}

void jc_bg_mgr_free(struct jc_bg_mgr *m)
{
    int i;
    for (i = 0; i < JC_BG_MAX; i++) {
        struct jc_bg_proc *p = &m->procs[i];
        if (p->state == JC_BG_RUNNING) {
            kill(-(pid_t)p->pid, SIGTERM);
        }
    }
    /* Brief grace, then hard-kill and reap any stragglers. */
    jc_sleep_ms(80, NULL);
    for (i = 0; i < JC_BG_MAX; i++) {
        struct jc_bg_proc *p = &m->procs[i];
        if (p->state == JC_BG_RUNNING) {
            reap(p);
            if (p->state == JC_BG_RUNNING) {
                kill(-(pid_t)p->pid, SIGKILL);
                waitpid((pid_t)p->pid, NULL, 0);
                p->state = JC_BG_KILLED;
            }
        }
        /* A killed child may have left a partial spool; a dead jichi leaves
         * the rest behind. Both are ours to remove, so the next session's
         * start (jc_bg_start opens O_TRUNC) never reads another run's bytes. */
        if (p->spool[0] != '\0') {
            unlink(p->spool);
            p->spool[0] = '\0';
        }
        jc_sb_free(&p->buf);
        p->state = JC_BG_FREE;
    }
}

const char *jc_bg_spool_error(void)
{
    return s_spool_err;
}
