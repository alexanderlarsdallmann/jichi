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
    JC_ERR_TOOBIG     /* input exceeds a hard resource bound (M24)     */
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

/* mkdir -p semantics. Returns JC_OK if the directory exists afterwards. */
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
 * Names, not capabilities: this decides what to SAY, never what to attempt. */
int jc_platform_verified_row(void);

/* Can the RSS watchdog behind `memBudgetMb` actually work here? Probes
 * /proc/self/stat, the file it reads. A shipped config key that silently does
 * nothing is worse than one that refuses (M326q). */
int jc_have_proc_rss(void);

enum jc_resource_tier { JC_RES_NORMAL = 0, JC_RES_LITE = 1, JC_RES_MINIMAL = 2 };
enum jc_resource_tier jc_resource_tier(unsigned long mb, int cpu);

/* Free space (MiB) on the filesystem holding `path`, or 0 if unknown. */
unsigned long jc_disk_free_mb(const char *path);

/* Format the current local wall-clock time into `buf` using strftime pattern
 * `fmt` (NULL => "%X", the locale's time representation). Respects LC_TIME (set
 * once at startup via setlocale(LC_TIME, "")). */
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
