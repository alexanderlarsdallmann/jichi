/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_platform.h - portability layer and core types for jichi.
 *
 * Everything in jichi is written to the C89 (ANSI C / C90) standard.
 * This header centralises the few places where C89 forces a choice:
 *   - fixed-width integer typedefs (C89 has no <stdint.h>)
 *   - a boolean type (C89 has no <stdbool.h>)
 *   - the project-wide status/result enum (we never use exceptions)
 *   - thin wrappers around POSIX file/path helpers
 *
 * POSIX-only functionality is implemented in jc_platform_posix.c, which is
 * compiled with -D_POSIX_C_SOURCE so the prototypes are visible while the
 * rest of the tree stays strict C89.
 */
#ifndef JC_PLATFORM_H
#define JC_PLATFORM_H


#ifdef __cplusplus
extern "C" {
#endif
#include <limits.h>
#include <stddef.h>

/* ----- fixed-width integer typedefs ------------------------------------- *
 * C89 only guarantees: char >= 8 bits, short/int >= 16 bits, long >= 32
 * bits. We deliberately avoid any 64-bit integer requirement; token counts
 * and costs are carried as double, sizes as size_t / long.
 */
typedef unsigned char jc_u8;
typedef long          jc_i32;
typedef unsigned long jc_u32;
typedef size_t        jc_size;

/* ----- boolean ---------------------------------------------------------- */
typedef int jc_bool;
#define JC_TRUE  1
#define JC_FALSE 0

/* ----- printf-format attribute (E-04) ----------------------------------- *
 * Lets the compiler check jichi's own printf-style formatters with the same
 * -Wformat it applies to the libc ones. __attribute__((format(printf, f, a)))
 * is a GNU/Clang extension, so it is guarded by __GNUC__ (clang defines it);
 * a C89 compiler without attributes sees an empty macro and the declarations
 * read unchanged. f = the 1-based position of the format argument, a = the
 * position of the first variadic argument. Only true `...` functions may
 * carry it; a va_list overload cannot be checked by format(printf,...).
 */
#ifdef __GNUC__
#define JC_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define JC_PRINTF(f, a)
#endif

/* ----- status codes ----------------------------------------------------- *
 * Returned by every fallible function. Outputs are delivered via pointers.
 * Tool *execution* errors are NOT represented here: they are values handed
 * back to the model (see jc_tool_result), matching the original CLI.
 */
typedef enum {
    JC_OK = 0,
    JC_ERR_OOM,       /* allocation failed                     */
    JC_ERR_IO,        /* filesystem / read / write error       */
    JC_ERR_PARSE,     /* malformed JSON / config / SSE         */
    JC_ERR_HTTP,      /* transport-level failure               */
    JC_ERR_PROVIDER,  /* provider returned an error response   */
    JC_ERR_NOTFOUND,  /* file / key / item not present         */
    JC_ERR_INVALID,   /* invalid argument / state              */
    JC_ERR_ABORTED,   /* user interrupt (Ctrl-C)               */
    JC_ERR_TIMEOUT,   /* model stalled: connected then timed out (M22) */
    JC_ERR_DENIED,    /* refused by a safety fence (path fence) (M24)  */
    JC_ERR_TOOBIG,    /* input exceeds a hard resource bound (M24)     */
    JC_ERR_UNREADABLE /* file is PRESENT but could not be read (M1100) */
} jc_status;

/* Human-readable name for a status code (for logging / errors). */
const char *jc_status_str(jc_status s);

/* ----- platform helpers (implemented in jc_platform_posix.c) ------------ */

/* Forward declaration; defined in jc_mem.h. */
struct jc_arena;
/* Forward declaration; defined in jc_vec.h. */
struct jc_vec;

/* Returns $HOME (or a sensible fallback). Never NULL. Not owned by caller. */
const char *jc_home_dir(void);

/* Install a signal handler that PERSISTS across deliveries.
 *
 * Built on sigaction() with sa_flags = 0 (POSIX.1, visible under
 * -D_POSIX_C_SOURCE=200112L): no SA_RESETHAND, no SA_NODEFER (the handler
 * keeps itself blocked while it runs), no SA_RESTART (the existing loops
 * expect EINTR, which is why every read/poll here checks the abort flag).
 *
 * WHY IT EXISTS (M918, the review's R03-F1). jichi installed its SIGINT and
 * SIGTERM handlers with signal(), and under this build's flags glibc's
 * signal() is __sysv_signal -- SA_RESETHAND|SA_NODEFER. So the handler
 * reset to SIG_DFL after ONE delivery: the first Ctrl-C aborted the current
 * turn, the second KILLED the process (exit 130) with no teardown -- no
 * session save, no bg-mgr/MCP/LSP shutdown, no lease release. Probe with the
 * build's exact flags: after signal(), sa_flags = 0xc4000000; after one
 * delivery the disposition is SIG_DFL. A BSD-semantics libc keeps the handler
 * (the defect is invisible there), which is why no gate had caught it -- the
 * whole-tree review drove the real binary in a pty.
 *
 * Use this for every handler jichi installs. The daemon's per-connection
 * child and the handler's own SIGTERM "graceful once" re-arm still set a
 * DISPOSITION (SIG_DFL) directly -- that is a reset, not an install, and is
 * the one place signal() is right. */
jc_status jc_signal_install(int sig, void (*handler)(int));

/* Reset inherited signal dispositions at start-up:
 *   - SIGCHLD to SIG_DFL (so waitpid reaps its children)
 *   - unblocks SIGINT, SIGTERM, SIGHUP, and (if defined) SIGCHLD.
 * A caller must guard with #ifdef SIGCHLD; DJGPP defines no SIGCHLD (M834). */
void jc_signal_reset_inherited(void);

/* The POSIX shell to spawn for `sh -c` work, resolved once and cached.
 *
 * M461: jichi hardcoded "/bin/sh" at sixteen sites. Android has no /bin -- its
 * shell is /system/bin/sh -- so on an Android 4.4.2 tablet every shell-backed
 * feature failed at once: run_terminal_command, the verify gate, user tools,
 * hooks, notify, sound, ACP's command delegate and `!`cmd`` expansion. 53 unit
 * checks across 12 files. The hardware plan PREDICTED this and nothing had ever
 * measured it, because every other platform in the matrix -- glibc, musl,
 * uClibc, Guix, FreeBSD, OpenBSD -- keeps /bin/sh, and Termux fakes it with an
 * LD_PRELOAD that rewrites execve.
 *
 * Resolution order, first executable wins:
 *   1. $JICHI_SHELL      -- explicit operator control, and the test hook
 *   2. /bin/sh           -- every FHS system; unchanged behaviour there
 *   3. $PREFIX/bin/sh    -- Termux, WITHOUT depending on termux-exec's preload
 *   4. /system/bin/sh    -- Android
 *   5. /bin/sh           -- when nothing is resolvable, fail as before
 *
 * Step 3 is ordered ahead of 4 deliberately: on Termux /system/bin/sh exists
 * and would "work", but it is not Termux's shell and would lose that
 * environment's PATH and utilities -- a silent downgrade of the M459 row. */
const char *jc_shell_path(void);

/* mkdir -p semantics. Returns JC_OK if the directory exists afterwards. */
jc_status jc_mkdir_p(const char *path);

/* Hard upper bound on a single jc_read_file slurp (bytes). Above this the read
 * is refused with JC_ERR_TOOBIG rather than allocating an arena block that could
 * exhaust memory. Far above any plausible source file (M24). */
#define JC_READ_FILE_MAX (64L * 1024L * 1024L)

/* Read an entire file into a NUL-terminated buffer allocated from `a`.
 * *len (if non-NULL) receives the byte length excluding the terminator.
 * Returns JC_ERR_TOOBIG if the file exceeds JC_READ_FILE_MAX. */
jc_status jc_read_file(const char *path, char **out, jc_size *len,
                       struct jc_arena *a);

/* M1109 (D-35): read at most `max` bytes of `path` -- the LEADING `max`
 * (from offset 0) when `head` is non-zero, the TRAILING `max` (from
 * offset size-max) otherwise -- into a NUL-terminated buffer allocated
 * from `a`. *len (if non-NULL) receives the number of bytes read
 * (min(size, max)). For files at or under `max` this reads the whole
 * file, exactly as jc_read_file does; for larger files it reads the
 * `max` bytes the caller keeps and nothing else.
 *
 * This exists for the prompt-asset cut sites, which read a file and then
 * cut it to a cap: a read-then-cut via jc_read_file allocates the WHOLE
 * file (up to JC_READ_FILE_MAX) onto the arena, and the arena is the
 * SESSION's -- the retention survives the cut for the process lifetime.
 * D-35 measured it: seven sparse 60 MB prompt-asset files (16 KB on
 * disk) raised peak RSS from 12.9 MB to 442 MB. Reading at most the cap
 * means the cut sites allocate cap+1, not the file size.
 *
 * The guard set mirrors jc_read_file's: jc_is_dir -> JC_ERR_IO,
 * JC_FAULT_HIT(JC_FAULT_READ) -> JC_ERR_IO, a failed open ->
 * JC_ERR_NOTFOUND, a failed size query -> JC_ERR_IO. There is no
 * JC_ERR_TOOBIG here: `max` IS the bound, a large file is not an error,
 * and callers that need TOOBIG semantics (and the 64 MB cap) keep using
 * jc_read_file. Like jc_read_file, *out is written only on JC_OK. */
jc_status jc_read_file_bounded(const char *path, char **out, jc_size *len,
                               struct jc_arena *a, jc_size max, int head);

/* D-16 / M1100: read a file whose ABSENCE is a legitimate answer -- the
 * "proceed only on a genuine does-not-exist" shape. jc_read_file maps EVERY
 * open failure (including a path that exists but cannot be opened) to
 * JC_ERR_NOTFOUND, so a caller that appends to `path` after it silently
 * REPLACED a present-but-unreadable file (the lead's LOG-3 run 0106: a
 * 67109888-byte .jichi/memory.md became 8 bytes holding only the new note;
 * the 64 MiB boundary itself reads fine -- the trigger is OVER
 * JC_READ_FILE_MAX). This helper splits the two:
 *   JC_OK             -- present and read; *out is NUL-terminated on `a`,
 *                        *len (if non-NULL) its byte length.
 *   JC_ERR_NOTFOUND   -- genuinely absent (lstat says ENOENT/ENOTDIR, or the
 *                        file vanished between lstat and open).
 *   <READ error>      -- PRESENT but unreadable: the read error is passed
 *                        through UNCHANGED (JC_ERR_TOOBIG over
 *                        JC_READ_FILE_MAX stays TOOBIG, as do JC_ERR_IO and
 *                        JC_ERR_OOM); *out is NULL and *len (if non-NULL) is
 *                        0 on EVERY error return -- the helper resets both
 *                        itself, so a caller need not pre-zero them.
 * The M533 board rule (jc_board_save refuses to overwrite a board it could
 * not read) is the precedent; callers must refuse -- never write -- when
 * this returns anything other than JC_OK or JC_ERR_NOTFOUND.
 *
 * M1100-b (review nit 1 on 161ceb10): the helper LSTATS the leaf, so a symlink
 * -- live or broken -- is NEVER JC_ERR_NOTFOUND and is never read through
 * (it reports JC_ERR_IO). A DELETED symlink falls through to NOTFOUND because
 * its lstat fails ENOENT like a genuinely absent path. A writing caller that
 * got JC_ERR_NOTFOUND may create a regular file at the path and nothing else --
 * never the target on the other side of a link (the D-12 write-through class).
 * Before M1100-b the helper stat()ed, so a symlink to an ABSENT target read
 * NOTFOUND and a trusting writer would have created the target through the
 * link; it was safe only because both callers run jc_state_file_check (D-15)
 * first. */
jc_status jc_read_existing(const char *path, char **out, jc_size *len,
                           struct jc_arena *a);

/* Write `len` bytes to `path`, creating/truncating it. */
jc_status jc_write_file(const char *path, const char *data, jc_size len);

/* M146: atomic replace -- write a same-directory temp (O_CREAT|O_EXCL,
 * 0600; mkstemp is XSI, not strict POSIX-200112), then rename() over
 * `path`, so a concurrent reader sees the old file or the new one, never a
 * torn half-write. The 0600 mode CARRIES to the final file; use this only
 * for jichi's own private sinks (sessions, calibration), where owner-only is
 * correct -- workspace files keep jc_write_file, whose in-place write
 * preserves the target's mode/symlink/hard-link semantics (M141 rationale). */
jc_status jc_write_file_atomic(const char *path, const char *data,
                               jc_size len);

/* M1099 (D-12): the mode-carrying variant of jc_write_file_atomic. Same
 * atomic tmp+rename, but fchmod()s the temp to `mode` (before the rename)
 * instead of leaving 0600 on it: the 0600-mode variant is M62 -- jichi's own
 * private sinks -- and this one is for a merged WORKSPACE file, which keeps
 * the child's mode, not 0600 (the parallel merge, D-12). */
jc_status jc_write_file_atomic_mode(const char *path, const char *data,
                                    jc_size len, int mode);

/* M1104-c (SV:0257, the M1104 side effect): the write that KEEPS a HARD LINK
 * when it rewrites jichi's own workspace files (memory.md, AGENTS.md/
 * CLAUDE.md). lstat the target: a regular file with st_nlink > 1 is written
 * IN PLACE (the M141 / vim backupcopy=auto convention -- rename over the
 * first name would orphan the second), else the M1104 mode-preserving atomic
 * replace (jc_write_file_atomic_mode with the target's lstat mode, 0644 if
 * absent). Documented trade: a failed in-place write truncates the shared
 * inode. Callers run jc_state_file_check (D-15) first. */
jc_status jc_write_file_preserve_link(const char *path, const char *data,
                                      jc_size len);

/* M1083 (D-08): the durable variant of jc_write_file_atomic. Same atomic
 * tmp+rename, but before the rename it fsync(fd)s the written data, and after
 * it fsyncs the containing directory, so the bytes and the new name both
 * survive a power loss / OOM-kill rather than resting in the page cache. The
 * plain writer stays deliberately unsynced (telemetry and other recreatable
 * sinks pay the FILE_HANDLING "no fsync" decision); only a caller that must
 * not lose the data -- the session save, once per tool round -- routes here.
 * A directory-fsync failure is not fatal: the file data is already durable. */
jc_status jc_write_file_atomic_durable(const char *path, const char *data,
                                       jc_size len);

/* Make `path` executable: add the execute bit wherever the matching read bit is
 * already set (so it respects umask, like `chmod +x`). Returns JC_OK on success,
 * JC_ERR_IO otherwise. Used for generated start-scripts (M48). */
jc_status jc_make_executable(const char *path);

/* Restrict `path` to owner-only access: 0600 for a file, 0700 for a directory
 * (M132). Used for on-disk sinks that hold conversation/prompt content or
 * telemetry -- sessions, the event log, the envelope journal, calibration --
 * so another local user on a shared host can't read them. Best-effort: a
 * chmod failure is not fatal (returns JC_ERR_IO but callers ignore it). */
jc_status jc_make_private(const char *path);

/* --- verifying privacy as an EFFECT, not as an intention (M528) ------------
 *
 * `jc_make_private` and a `umask` around `bind()` both *ask* for owner-only
 * access. Neither proves it: a chmod's return value says the call succeeded, not
 * that the resulting mode is what you wanted, and `src/main.c`'s daemon comment
 * names the case that cannot be assumed away -- "there are platforms that ignore
 * the umask for sockets". A socket the kernel created 0755 on such a platform is
 * a shell prompt for every local user, and nothing in the code would have said
 * so. jichi's own `doctor` already applies this discipline to the key file
 * ("created 0664, tightened to 0600, read back -- verified rather than trusting
 * chmod's return value"); these two make it reusable.
 *
 * The verdict is PURE -- it takes the three numbers and returns the answer -- so
 * every branch is unit-testable without a filesystem, a mount option or a
 * platform that misbehaves. */
enum jc_priv_verdict {
    JC_PRIV_OK = 0,      /* owned by euid, no group or other bits            */
    JC_PRIV_NOT_OWNER,   /* someone else owns it                            */
    JC_PRIV_TOO_OPEN,    /* group and/or other bits are set                 */
    JC_PRIV_NO_STAT      /* could not be examined at all                    */
};

/* Pure: the verdict for a path with this owner and mode, as seen by `euid`.
 * `mode` is a stat(2) st_mode; only the permission bits are consulted. */
enum jc_priv_verdict jc_priv_verdict_of(unsigned long st_uid,
                                        unsigned long st_mode,
                                        unsigned long euid);

/* One-line English for a verdict, for a diagnostic. Never NULL. */
const char *jc_priv_verdict_str(enum jc_priv_verdict v);

/* lstat `path` (never following a symlink -- the thing on the path is what an
 * attacker controls) and return the verdict for the CURRENT euid. */
enum jc_priv_verdict jc_path_private_check(const char *path);

/* Is `dir` safe to hold a private endpoint -- i.e. can no other user REPLACE
 * what we put there? True when the directory is not writable by group or other,
 * or is writable but sticky (/tmp). Pure, same reason as above. */
int jc_dir_holds_private(unsigned long st_mode);

/* Create DIR and its parents, and apply 0700 ONLY to what this call created
 * (M488). Use this instead of `jc_mkdir_p(d); jc_make_private(d);`.
 *
 * THE DEFECT IT REPLACES. That pair re-permissions the directory whether or not
 * jichi made it, and two of its four call sites take the path from the user
 * (`--log`, `--control`). Run as root -- every container, most CI --
 * `--log /tmp/jichi.jsonl` turned /tmp into 0700 root-only for the whole machine:
 * measured 1777 before, 700 after. Non-root was inert only BY ACCIDENT, because
 * chmod fails with EPERM on a directory you do not own and the return was
 * discarded; on a directory the user DOES own it reproduces at any privilege.
 *
 * M132's guarantee is unchanged for everything jichi owns: a sink directory it
 * creates is still 0700, and the FILE is still 0600 regardless. What changes is
 * that a directory jichi did not create is not jichi's to re-permission.
 *
 * Rejected alternatives, recorded because each changes M132 differently:
 * refusing outright when the owner differs (turns a working --log into an error
 * for a legitimate shared directory), and chmod-ing only when the mode is wider
 * than 0700 (still re-permissions /tmp, just conditionally). */
jc_status jc_mkdir_p_private(const char *dir);

/* Non-zero if a regular file or directory exists at `path`. */
int jc_file_exists(const char *path);

/* M760: an exclusive advisory lock on `path` (created 0600 when absent), waiting
 * up to `wait_ms` for another process to let go. Returns a descriptor for
 * jc_lock_release; JC_LOCK_BUSY when the lock is still held after the wait; or
 * JC_LOCK_NONE when no lock can be taken here at all (the file cannot be opened,
 * or the filesystem has no record locks -- ENOLCK on some NFS mounts).
 *
 * POSIX fcntl(F_SETLK) over the whole file, polled rather than F_SETLKW, so a
 * holder that is stopped (SIGSTOP, a debugger) delays the caller by `wait_ms`
 * instead of wedging it. The kernel drops the lock when its holder exits, so a
 * crash leaves none behind -- which is why it guards a short critical section
 * and is never itself a record anyone reads. The file is never removed:
 * unlinking a lock file reopens the race it closes, because a waiter holding
 * the old inode and a newcomer creating a new one would both hold "the" lock.
 * Per process, like every fcntl lock: closing any other descriptor on the same
 * file drops it too, so only this pair of functions opens a lock file. */
#define JC_LOCK_BUSY (-1)
#define JC_LOCK_NONE (-2)
int jc_lock_take(const char *path, long wait_ms);
void jc_lock_release(int fd);

/* M503: the file's permission bits (st_mode & 07777), or -1 when it cannot be
 * stat'd. Exists so `doctor` can READ BACK a mode it just set instead of
 * trusting chmod's return value -- on a filesystem that ignores POSIX modes
 * (an MSYS2 `noacl` mount, some network mounts) chmod reports success and
 * changes nothing, and nothing in jichi could tell the difference. */
long jc_file_mode(const char *path);

/* Seconds since an unspecified epoch as a double (for timing / mtime). */
double jc_now_seconds(void);

/* Monotonic milliseconds since an unspecified epoch, as a double (for measuring
 * latencies/durations as deltas). Not affected by wall-clock adjustments. Falls
 * back to wall-clock time if a monotonic clock is unavailable. */
double jc_now_millis(void);

/* Sleep for `ms` milliseconds, waking early if *abort becomes non-zero
 * (abort may be NULL). Returns 1 if interrupted by the abort flag, else 0. */
int jc_sleep_ms(long ms, volatile int *abort);

/* Modification time of `path` in seconds, or -1.0 on error. */
double jc_file_mtime(const char *path);

/* Non-zero if `path` is a directory. */
int jc_is_dir(const char *path);

/* Non-zero if `path` is a REGULAR file (not a directory, FIFO, socket or
 * device). M198: callers that read a path they DISCOVERED by scanning a
 * directory jichi owns -- the session store, the index/repo-map walks, asset
 * discovery -- must check this first. Opening a FIFO with no writer blocks
 * forever, so a stray `pipe.json` in the session store used to hang
 * /sessions, /resume and `--continue` startup with no output and no timeout.
 *
 * Deliberately NOT enforced inside jc_read_file: a path the user NAMED
 * (--config, read_file, @path) may legitimately be a pipe, e.g.
 * `--config <(jq ...)` which resolves to /dev/fd/N. Scanned paths are garbage
 * when they are not regular files; named paths are the user's business.
 * See docs/proposals/2026-07-robustness-edge-cases.md (#1). */
int jc_is_regular_file(const char *path);

/* Size of `path` in bytes, or -1 on error. */
long jc_file_size(const char *path);

/* Append the names (not full paths) of entries in `dir` to `names`, which
 * must be an initialised jc_vec of (char *) allocated from `a`. Skips "."
 * and "..". */
jc_status jc_list_dir(const char *dir, struct jc_vec *names,
                      struct jc_arena *a);

/* Number of online CPU cores; at least 1 (1 if it cannot be determined). */
int jc_cpu_count(void);
/* Whether that count was actually DETECTED, as opposed to fallen back to (M669).
 *
 * jc_cpu_count() returns 1 for two unrelated reasons -- a genuine one-core
 * machine, and a platform where `_SC_NPROCESSORS_ONLN` is not declared under
 * this tree's feature-test macros (the BSDs hide it behind __BSD_VISIBLE while
 * we compile -D_POSIX_C_SOURCE=200112L). A caller cannot tell those apart, and
 * the difference is not cosmetic: `maxParallelAgents` defaults to the core
 * count, so on FreeBSD and NetBSD `spawn_parallel` silently runs ONE child on
 * an eight-core machine. Returns 0 when the number is a fallback rather than a
 * measurement, so a diagnostic can state a fact instead of a suspicion. */
int jc_cpu_count_known(void);

/* Total physical RAM in mebibytes, or 0 if it cannot be determined. */
unsigned long jc_mem_total_mb(void);

/* Resource tier for auto-tuning defaults, from total RAM (MiB) + CPU count.
 * Centralizes the lean/lite thresholds that were duplicated as a bare
 * `mb < 1024` in several places. Unknown RAM (0) => NORMAL (never downgrade on
 * a failed probe). Pure; unit-tested. */
/* "Linux 6.8.0 (x86_64)" from uname, for reporting. Returns 0 (and empties
 * buf) when uname fails. The NAME is for telling a human what they are on, and
 * for guessing which command to suggest -- never for deciding whether a
 * feature works. Use a capability probe for that. */
int jc_platform_describe(char *buf, jc_size cap);

/* uname's sysname is exactly "Linux". For suggesting OS-appropriate commands
 * (aplay vs afplay), not for capability decisions. */
int jc_platform_is_linux(void);

/* Does docs/PLATFORMS.md carry a Verified row for this kernel? Linux plus the
 * three BSDs, each of which runs the FULL gate (M465/M480/M481).
 *
 * This exists because `doctor` told a FreeBSD user "jichi has never been
 * compiled on this platform" months after FreeBSD started passing 1,068 smoke
 * checks there -- the product asserting a verdict its own docs had retired, in
 * the one place a support conversation starts. tests/smoke/doctor.sh had even
 * written the staleness down and deferred the call to PLATFORMS.md; PLATFORMS.md
 * then made it, and nothing carried the answer back here.
 *
 * The list is compiled in, so it CAN rot -- which is why
 * tests/smoke/portability_lint.sh check 7c pins it against PLATFORMS.md's
 * Verified table in both directions. A name here that the page does not verify,
 * or a verified kernel missing here, fails the build.
 *
 * Names, not capabilities: this decides what to SAY, never what to attempt.
 *
 * M695: three values, not two. docs/PLATFORMS.md distinguishes Verified (the
 * full gate ran) from Partly verified (it compiles and SOME gate ran green,
 * with the incomplete part named), and a product that knows only "verified or
 * never compiled" must call the middle tier never-compiled. It did, to three
 * measured platforms, one of which has run a live agentic task. */
#define JC_PLATFORM_ROW_UNKNOWN   0
#define JC_PLATFORM_ROW_PARTLY    1
#define JC_PLATFORM_ROW_VERIFIED  2

int jc_platform_row_verdict(void);

/* The table without the uname() call, so it is testable on any host. */
int jc_platform_row_verdict_for(const char *sysname);

/* Back-compat shorthand: verdict == JC_PLATFORM_ROW_VERIFIED. */
int jc_platform_verified_row(void);

/* Can the RSS watchdog behind `memBudgetMb` actually work here? Probes
 * /proc/self/stat, the file it reads. A shipped config key that silently does
 * nothing is worse than one that refuses (M326q). */
int jc_have_proc_rss(void);

enum jc_resource_tier { JC_RES_NORMAL = 0, JC_RES_LITE = 1, JC_RES_MINIMAL = 2 };
enum jc_resource_tier jc_resource_tier(unsigned long mb, int cpu);

/* Free space (MiB) on the filesystem holding `path`, or 0 if unknown. */
unsigned long jc_disk_free_mb(const char *path);

/* Free space AND free inodes on the filesystem holding `path`, measured in
 * `double` so the result can be reported, thresholded and compared without the
 * two traps that make `jc_disk_free_mb` above unfit for a guard:
 *
 *   1. It scales the block size to MiB by INTEGER division first
 *      (`f_bavail * (bsize/1024) / 1024`), so on any filesystem with a fragment
 *      size below 1024 -- ZFS uses 512 -- `bsize/1024` is 0 and the whole
 *      product collapses to 0 MiB of "free" space on a disk that is anything but
 *      full. A guard reading that would refuse to run on every ZFS host.
 *   2. It returns the SAME 0 when `statvfs` itself fails, so "the disk is full"
 *      and "I could not measure the disk" are indistinguishable -- a guard must
 *      never stop work on an unknown, and it cannot tell unknown from full here.
 *
 * This function keeps the two apart: it returns JC_ERR_IO when the measurement
 * could not be taken (leaving *free_mb / *free_files at -1.0, a value no real
 * filesystem produces), and JC_OK with a true reading otherwise -- including a
 * genuine 0.0 for a full disk. `double`'s 52-bit mantissa represents every byte
 * count exactly past 4 PiB, so no fragment size truncates. Free inodes come from
 * f_favail (the non-root count, like f_bavail); on a filesystem with no inode
 * limit statvfs reports a huge or zero f_files -- callers treat 0 as "no inode
 * pressure to measure", never as exhaustion. Either out-pointer may be NULL. */
jc_status jc_disk_space(const char *path, double *free_mb, double *free_files);

/* 1 if the file system under `path` keeps long file names, 0 if it keeps only
 * 8.3 names (M838). Only DOS has the distinction: plain DOS cuts every name to
 * eight characters and a three-character extension, and cannot hold a name
 * beginning with a dot, until an LFN driver such as DOSLFN is loaded. The C
 * library is asked where it can say (DJGPP's _use_lfn, probed by the Makefile as
 * JC_HAVE_USE_LFN); everywhere else the answer is 1 and nothing is asked. */
int jc_long_names_ok(const char *path);

/* Local time, checked before it is believed (M839).
 *
 * A C library can hand back a local time that is not a local time at all:
 * DJGPP's, when TZ is unset, empty, or a name without an offset such as "UTC",
 * returned 2026-08-02 05:08 for 2026-08-10 00:00 UTC in one run and a 37-day
 * error in another, depending on what was in memory. FreeDOS sets TZ empty by
 * default, so every local date jichi showed there was garbage.
 *
 * jc_tm_offset_plausible says whether `local` could be `utc` in a real time
 * zone: the two differ by at most 26 hours (UTC-12 to UTC+14 is the range in
 * use) and by a whole number of 15-minute steps (every zone in use today:
 * +5:45 and +12:45 included). Pure, so the rule is unit-tested anywhere.
 *
 * jc_localtime_checked fills `*out` with local time and returns 1 when that
 * time is plausible; otherwise it fills `*out` with UTC and returns 0, so a
 * caller can say which it shows; it returns -1, with `*out` zeroed, when not
 * even UTC can be computed. `t` is seconds since the epoch.
 *
 * That fallback is right for a timestamp that came from elsewhere, and wrong
 * for "now" on DJGPP: there a broken zone shifts time() ITSELF by the same
 * garbage offset, so localtime(time()) is the system clock and gmtime(time())
 * is weeks off (measured: 2026-10-23 for 2026-09-29). jc_tz_repair, called
 * first in main(), fixes the process instead: where local time is not
 * plausible it sets TZ=UTC0 and re-reads it, and returns 1; on a healthy
 * system it changes nothing and returns 0. It must run before any time is
 * taken, or a timestamp from before it and one from after it disagree by the
 * garbage offset. */
struct tm;
int jc_tm_offset_plausible(const struct tm *local, const struct tm *utc);
int jc_localtime_checked(long t, struct tm *out);
int jc_tz_repair(void);

/* Format the current local wall-clock time into `buf` using strftime pattern
 * `fmt` (NULL => "%X", the locale's time representation). Respects LC_TIME (set
 * once at startup via setlocale(LC_TIME, "")). Where the local time zone is
 * unusable (jc_localtime_checked returns 0) the time shown is UTC. */
void jc_now_timestr(const char *fmt, char *buf, jc_size cap);

/* The OS locale's numeric thousands separator (first byte), or 0 if none.
 * Queried WITHOUT leaving LC_NUMERIC changed, so cJSON/printf number I/O stays
 * in the "C" locale (a switched LC_NUMERIC would emit "1,5" and corrupt JSON). */
char jc_locale_group_sep(void);

/* Does the environment declare a UTF-8 locale? Reads LC_ALL, then LC_CTYPE,
 * then LANG -- the POSIX precedence -- and looks for a UTF-8 charset name.
 *
 * M566: this was a `static` in src/tui/jc_tui.c, where it answered two
 * questions: may we print box-drawing glyphs, and is a non-English message
 * catalog safe to serve (jc_msg_lang_resolve's `utf8_ok`). The headless
 * front-end needs the second one, and duplicating an eight-line predicate is
 * how two front-ends come to disagree about the same environment -- which is
 * the defect M566 exists to fix, so the fix must not introduce it. */
int jc_locale_is_utf8(void);

#ifdef __cplusplus
}
#endif
#endif /* JC_PLATFORM_H */
