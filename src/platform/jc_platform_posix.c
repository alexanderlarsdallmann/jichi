/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_platform_posix.c - POSIX implementations of jc_platform.h helpers.
 *
 * This translation unit (and only a handful of others) is compiled with
 * -D_POSIX_C_SOURCE so that opendir/mkdir/stat prototypes are visible. The
 * rest of jichi stays strict C89.
 */

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
/* Darwin only (M817). This file is compiled with -D_POSIX_C_SOURCE, which puts
 * the macOS SDK headers into strict POSIX mode -- and there the BSD typedefs
 * u_int, u_char and u_short are not declared. <sys/sysctl.h> below (needed for
 * the HW_MEMSIZE branch of jc_mem_total_mb) pulls in <sys/ucred.h> and
 * <sys/proc.h>, both of which use exactly those typedefs, so the build stops with
 * "unknown type name 'u_int'" before it reaches any jichi code.
 *
 * _DARWIN_C_SOURCE asks for the Darwin surface *in addition to* POSIX rather than
 * instead of it, which is precisely what this file needs. A feature-test macro
 * only takes effect before the first system header, hence the position.
 *
 * Found by Dima Mellnik on the first verified macOS build of this tree (arm64,
 * macOS 26.5, Apple clang 21; VERIFY_A_PLATFORM.md §4). No effect on any other
 * platform -- the guard is __APPLE__-only. */
#define _DARWIN_C_SOURCE 1
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
/* C-16 (M1134): mkdtemp is POSIX-2008, and the build-wide
 * -D_POSIX_C_SOURCE=200112L does NOT declare it -- a 3-line TU compiled
 * -std=c89 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200112L warns "implicit
 * declaration of mkdtemp" (measured in the M1134 design note; the same fact is
 * recorded from the test side at tests/fuzz/jc_fuzz_targets_fs.c:61).
 * The project's established solution is a per-TU _XOPEN_SOURCE guard before
 * the first system header (src/util/jc_path.c:12, src/index/jc_walk.c:9 use
 * 600 for realpath). 600 is NOT enough for mkdtemp: glibc gates it behind
 * __USE_XOPEN2K8, which _XOPEN_SOURCE sets only at >= 700 (verified this run --
 * a probe compiled with -D_POSIX_C_SOURCE=200112L and _XOPEN_SOURCE 600 still
 * warns "implicit declaration of mkdtemp", while 700 compiles clean). It goes
 * beside the __APPLE__ _DARWIN_C_SOURCE guard above: both feature-test macros
 * only take effect before the first system header, hence both precede
 * <errno.h>. 700 also declares realpath, so the jc_path.c/jc_walk.c precedent
 * is preserved, not narrowed. */

#include <errno.h>
#include "jc_platform.h"
#include "jc_fault.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include "jc_fcntl.h"   /* <fcntl.h> + JC_O_BINARY (M837) */
#include "jc_log.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>
#include <signal.h>
#include <sys/statvfs.h>
#include <sys/time.h>   /* struct timeval: jc_sleep_ms's select() sleep (M834); portability_lint 28 */
#ifdef JC_NO_NANOSLEEP
#include "jc_select.h"   /* M834: jc_sleep_ms sleeps with select() here */
#endif
#include <pwd.h>   /* getpwuid: ask the system for home, don't guess /tmp (M472) */
#if defined(__APPLE__)
#include <sys/sysctl.h>

#endif

/* M726: FreeMiNT gives a program a FIXED stack of `_stksize` bytes -- 64 KB by
 * MiNTLib's default -- and it does not grow. The unit suite ran past it
 * (tests/test_vision.c keeps a 64 KB buffer on the stack) and overwrote the
 * environment next to it: a bus error in getenv, then in setenv, reading
 * 0x78787878, which is that buffer's 'x' fill. 512 KB is measured to carry the
 * whole suite in the guest and fits MiNT's default 1,024 KB initial allocation;
 * a 4 MB stack did not start ("insufficient memory"). MiNT's crt0 reads this
 * symbol, and a definition here overrides MiNTLib's for every program that
 * links the platform layer: jichi, jichi-convert and run_tests. */
#ifdef __MINT__
long _stksize = 512L * 1024L;
#endif

const char *jc_status_str(jc_status s)
{
    switch (s) {
        case JC_OK:           return "ok";
        case JC_ERR_OOM:      return "out of memory";
        case JC_ERR_IO:       return "i/o error";
        case JC_ERR_PARSE:    return "parse error";
        case JC_ERR_HTTP:     return "http error";
        case JC_ERR_PROVIDER: return "provider error";
        case JC_ERR_NOTFOUND: return "not found";
        case JC_ERR_INVALID:  return "invalid argument";
        case JC_ERR_ABORTED:  return "aborted";
        case JC_ERR_TIMEOUT:  return "model stalled (timed out)";
        case JC_ERR_DENIED:   return "refused by safety fence";
        case JC_ERR_TOOBIG:   return "input too large";
        case JC_ERR_UNREADABLE: return "present but unreadable";
        default:              return "unknown error";
    }
}

/* Where jichi's state lives. EVERY private sink is rooted here -- the config,
 * ~/.jichi.env (the API key), sessions, telemetry, the run journals, the audit
 * log -- so getting this wrong relocates all of them at once, silently (M472).
 *
 * It used to be `getenv("HOME")` or else the literal "/tmp". /tmp is
 * world-writable and sticky, and the sticky bit stops another user DELETING your
 * files, not CREATING them at a path you have not used yet. So with HOME unset,
 * a local attacker could pre-create /tmp/.jichi.env, or make /tmp/.jichi.d a
 * symlink into a directory they own -- and M132's 0600/0700 could not help,
 * because jc_make_private applies a mode to whatever the path RESOLVES to, which
 * would be the attacker's file. The environments where HOME is unset are the
 * unattended ones AUTONOMOUS_LOOPS.md recommends: some containers, some cron
 * configurations, a unit that cleaned its environment.
 *
 * Three steps, each removing a way to be wrong:
 *   1. HOME, as before.
 *   2. Ask the SYSTEM where home is (getpwuid) rather than guessing. In practice
 *      this resolves every real HOME-unset case, which is what makes step 3 a
 *      backstop rather than a policy.
 *   3. A uid-scoped /tmp directory created 0700 -- and if that path is not a
 *      directory we own with no group/other bits, a pid-scoped one that is
 *      CHECKED the same way (a pid is predictable, so it too may have been
 *      pre-planted; C-16/M1134 removed the unchecked original). If BOTH are
 *      unusable, a fresh unique directory from mkdtemp (created 0700 and owned
 *      by us by construction, no check-then-use window); if even that fails,
 *      the pid-scoped path is returned with a second, louder warning -- never
 *      NULL, and it does not exit: a getter that kills the process is a worse
 *      contract than a getter that degrades loudly, and `doctor` reports the
 *      situation so an unattended run can gate on it. That trades persistence
 *      across runs (already lost in a degraded environment) for never writing
 *      a key into a directory somebody else controls.
 *
 * NOT cached, deliberately, and this cost a segfault to learn: an earlier cut
 * resolved once into a static because "49 call sites should not each re-derive
 * it". tests/test_index.c:178 calls setenv("HOME", ...) mid-run and expects the
 * next call to see it -- with the cache in place the planted cache directory was
 * never found and a NULL reached strcmp. HOME is genuinely mutable within a
 * process, so the getter must read it every time. The fallback paths recompute
 * deterministically, which costs a getpwuid only when HOME is unset; only the
 * one-time notice needs state. */

/* C-16 (M1134): is this path a directory WE own with no group/other bits?
 * The single rule both /tmp fallback candidates of jc_home_dir are judged by --
 * factored out so the pid-scoped leg cannot drift into a weaker check (that was
 * the defect: it mkdir'd and used the path without ever asking). A pre-planted
 * directory, a symlink (lstat, not stat), a file, another user's directory, or
 * one with any group/other bit answers 0. */
static int jc_dir_usable(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        return 0;
    }
    if (st.st_uid != getuid()) {
        return 0;
    }
    if ((st.st_mode & (mode_t)077) != 0) {
        return 0;
    }
    return 1;
}

const char *jc_home_dir(void)
{
    static char fallback[1024];
    static int warned = 0;
    const char *h;
    struct passwd *pw;

    h = getenv("HOME");
    if (h != NULL && h[0] != '\0') {
        return h; /* the pointer into environ, exactly as before */
    }

    pw = getpwuid(getuid());
#ifdef JC_FAULT
    /* C-16 (M1134): the no-password-entry probe. Every bench has an entry for
     * its own uid, so the /tmp fallback below is unreachable without dropping
     * privileges; the env (inert in a release build) makes the whole branch
     * testable deterministically. */
    if (jc_fault_home_nopasswd()) {
        pw = NULL;
    }
#endif
    if (pw != NULL && pw->pw_dir != NULL && pw->pw_dir[0] != '\0') {
        jc_snprintf(fallback, sizeof(fallback), "%s", pw->pw_dir);
        if (!warned) {
            warned = 1;
            fprintf(stderr,
                    "jichi: HOME is not set; using this account's home directory "
                    "(%s) from the password database.\n"
                    "       Set HOME explicitly -- an unattended run whose HOME "
                    "moves also moves its config, sessions and audit log.\n",
                    fallback);
        }
        return fallback;
    }

    /* Neither HOME nor a password entry. Do not write secrets into /tmp itself.
     * C-16 (M1134): both candidates are judged by the SAME four-predicate check
     * (jc_dir_usable); the pid-scoped one was once used unchecked, on the false
     * assumption that a pid-scoped name could not have been pre-planted -- pids
     * are predictable. */
    jc_snprintf(fallback, sizeof(fallback), "/tmp/jichi-%lu",
                (unsigned long)getuid());
    (void)mkdir(fallback, 0700);
    if (!jc_dir_usable(fallback)) {
        /* Pre-planted, a symlink, a file, or shared: another user's directory,
         * or one with any group/other bit. The pid-scoped candidate next -- and
         * it is CHECKED too, because a hostile local user may have planted
         * /tmp/jichi-<uid>-<pid> (or a range of them) ahead of time; state does
         * not survive the run, which is the correct trade against handing over
         * the key file. */
        jc_snprintf(fallback, sizeof(fallback), "/tmp/jichi-%lu-%lu",
                    (unsigned long)getuid(), (unsigned long)getpid());
        (void)mkdir(fallback, 0700);
        if (jc_dir_usable(fallback)) {
            if (warned) {
                return fallback;
            }
            warned = 1;
            fprintf(stderr,
                    "jichi: HOME is not set and no password entry was found, and the "
                    "per-user fallback directory\n"
                    "       was unusable (wrong owner, wrong mode, or not a "
                    "directory -- it may have been pre-created\n"
                    "       by another user). Using %s for this run only; nothing "
                    "will persist.\n"
                    "       Set HOME. See `jichi doctor`.\n", fallback);
            return fallback;
        }
        /* The pid-scoped path failed the check too: it is planted. Fall back to
         * a fresh UNIQUE directory. mkdtemp replaces the trailing X's and
         * creates the directory 0700 owned by us, by construction -- no
         * check-then-use window. Nothing it names persists across runs. */
        {
            char tmpl[64];
            jc_snprintf(tmpl, sizeof(tmpl), "/tmp/jichi-XXXXXX");
            if (mkdtemp(tmpl) != NULL) {
                /* mkdtemp wrote the real name into tmpl; copy it into the
                 * static fallback the other legs return, and return THAT --
                 * a getter must not hand back a pointer to a local. */
                jc_snprintf(fallback, sizeof(fallback), "%s", tmpl);
                if (warned) {
                    return fallback;
                }
                warned = 1;
                fprintf(stderr,
                        "jichi: HOME is not set and no password entry was found, and "
                        "neither the per-user\n"
                        "       fallback directory nor its pid-scoped name could be "
                        "verified (wrong owner, wrong mode,\n"
                        "       or not a directory -- a hostile local user may have "
                        "pre-created them). Using\n"
                        "       the fresh directory %s for this run only; nothing "
                        "will persist.\n"
                        "       Set HOME. See `jichi doctor`.\n", fallback);
                return fallback;
            }
        }
        /* Even /tmp is unwritable: mkdtemp failed. DEGRADE LOUDLY, never NULL --
         * jc_home_dir has 70+ call sites, none of which copes with NULL, and
         * M472's contract is that a getter must not kill the process. Return the
         * (non-existent) pid-scoped path so every caller's mkdir/write fails
         * loudly, plus a second, louder warning. */
        if (warned) {
            return fallback;
        }
        warned = 1;
        fprintf(stderr,
                "jichi: HOME is not set and no password entry was found, and the "
                "per-user fallback directory\n"
                "       was unusable (wrong owner, wrong mode, or not a directory "
                "-- it may have been pre-created\n"
                "       by another user), and its pid-scoped name was unusable "
                "too, and even a fresh unique\n"
                "       directory could not be created (a mkdtemp template of "
                "the form /tmp/jichi-XXXXXX\n"
                "       failed): the temp filesystem itself appears "
                "unwritable or full.\n");
        fprintf(stderr,
                "       WARNING: no state can persist at all -- every attempt to "
                "write it will fail. Using\n"
                "       %s, which does not exist. Set HOME or repair /tmp. See "
                "`jichi doctor`.\n", fallback);
        return fallback;
    }
    if (warned) {
        return fallback;
    }
    warned = 1;
    fprintf(stderr,
            "jichi: HOME is not set and no password entry was found; using %s "
            "(created 0700).\n"
            "       Set HOME -- config, sessions and the audit log all live "
            "under it. See `jichi doctor`.\n", fallback);
    return fallback;
}

/* See the note in jc_platform.h. Resolved once; the cache also means the
 * sixteen call sites do not each pay four access() calls per spawn. */
const char *jc_shell_path(void)
{
    static char cached[1024]; /* jc_path.h is a layer up; 1024 matches this file */
    static int resolved = 0;
    const char *env;

    if (resolved) {
        return cached;
    }
    resolved = 1;

    env = getenv("JICHI_SHELL");
    if (env != NULL && *env != '\0' && access(env, X_OK) == 0) {
        jc_snprintf(cached, sizeof(cached), "%s", env);
        return cached;
    }
    if (access("/bin/sh", X_OK) == 0) {
        jc_snprintf(cached, sizeof(cached), "%s", "/bin/sh");
        return cached;
    }
    env = getenv("PREFIX");
    if (env != NULL && *env != '\0') {
        char cand[1024];
        jc_snprintf(cand, sizeof(cand), "%s/bin/sh", env);
        if (access(cand, X_OK) == 0) {
            jc_snprintf(cached, sizeof(cached), "%s", cand);
            return cached;
        }
    }
    if (access("/system/bin/sh", X_OK) == 0) {
        jc_snprintf(cached, sizeof(cached), "%s", "/system/bin/sh");
        return cached;
    }
    jc_snprintf(cached, sizeof(cached), "%s", "/bin/sh");
    return cached;
}

jc_status jc_signal_install(int sig, void (*handler)(int))
{
    struct sigaction sa;
    int rc;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    rc = sigemptyset(&sa.sa_mask);
    if (rc != 0) {
        return JC_ERR_INVALID;
    }
    if (sigaction(sig, &sa, (struct sigaction *)0) != 0) {
        return JC_ERR_INVALID;
    }
    return JC_OK;
}

void jc_signal_reset_inherited(void)
{
    struct sigaction sa;
    sigset_t set;

    /* Reset SIGCHLD to default (so waitpid reaps its children). */
#ifdef SIGCHLD
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
        /* A real failure leaves the inherited disposition in place for the
         * whole run -- the E-01 defect (a failed verifier read as passing)
         * is live and otherwise invisible in an unattended run. The
         * checked reap still says unknown, never a false green; the log line
         * is what names the cause. EINTR is the only realistic case (a
         * signal landing between start-up and this call) and is retriable in
         * principle, but the next disposition install is what actually
         * matters -- so log once and move on. */
        jc_logf(JC_LOG_WARN, "jc_signal_reset_inherited: resetting SIGCHLD "
                             "to default failed (errno %d); inherited signal "
                             "state may persist for this run",
                errno);
    }
#endif

    /* Unblock SIGINT, SIGTERM, SIGHUP and (if defined) SIGCHLD. An inherited
     * signal MASK is the same class of problem as an ignored disposition. */
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
#ifdef SIGCHLD
    sigaddset(&set, SIGCHLD);
#endif
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

jc_status jc_mkdir_p(const char *path)
{
    char buf[1024];
    jc_size n;
    jc_size i;

    n = strlen(path);
    if (n == 0 || n >= sizeof(buf)) {
        return JC_ERR_INVALID;
    }
    memcpy(buf, path, n + 1);

    /* Create each intermediate component. */
    for (i = 1; i < n; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0) {
                /* EEXIST is fine; anything else is reported lazily below. */
            }
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0755) != 0) {
        struct stat st;
        if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) {
            return JC_OK;
        }
        /* Tolerate the case where the leaf already exists as a dir. */
        return JC_OK;
    }
    return JC_OK;
}

/* M1138 (D-05): ftell reported 0, but 0 is UNKNOWN, not necessarily EMPTY.
 * procfs pseudo-files (/proc/cpuinfo, /proc/self/status, ...) report a size of
 * 0 and a successful seek-to-end while holding real bytes, and -- crucially --
 * they are stat-identical to a genuine empty regular file (both are S_ISREG
 * with st_size 0), so NO stat can tell the two apart. The only reliable
 * discriminator is to actually try to read: a 1-byte probe read returns 0 for
 * a genuinely empty file and 1 for a content-bearing pseudo-file.
 *
 * `f` is open at the position left by the caller's successful fseek(SEEK_END).
 * Read at most `bound` bytes and, on JC_OK, set *out (arena-owned, NUL-
 * terminated) and *len (bytes read, 0..bound). The file is NOT closed here --
 * the caller owns the fclose. Returns an error status on a failed seek, an
 * ferror (an EIO short read is checked here, NOT returned as success), or an
 * arena OOM.
 *
 * A genuinely empty file (probe returns 0) takes the 1-byte shape: exactly what
 * the size==0 path did before M1138 (allocate 1 byte, read 0, JC_OK/len 0) --
 * byte-identical, and no `bound`-sized allocation is made for it. */
static jc_status jc_read_unknown_size(FILE *f, char **out, jc_size *len,
                                      struct jc_arena *a, jc_size bound)
{
    char *buf;
    jc_size total;
    size_t step;
    char probe[1];
    size_t pg;

    if (fseek(f, 0L, SEEK_SET) != 0) {
        return JC_ERR_IO;
    }
    pg = fread(probe, 1, 1, f);
    if (ferror(f)) {
        return JC_ERR_IO;
    }
    if (pg == 0) {
        buf = (char *)jc_arena_alloc(a, 1);
        if (buf == NULL) {
            return JC_ERR_OOM;
        }
        buf[0] = '\0';
        *out = buf;
        if (len != NULL) {
            *len = 0;
        }
        return JC_OK;
    }
    /* The probe consumed a byte and proved there IS content: back to the start
     * and read up to `bound` bytes in `bound - total` steps to EOF. */
    if (fseek(f, 0L, SEEK_SET) != 0) {
        return JC_ERR_IO;
    }
    buf = (char *)jc_arena_alloc(a, bound + 1);
    if (buf == NULL) {
        return JC_ERR_OOM;
    }
    total = 0;
    while (total < bound) {
        step = fread(buf + total, 1, bound - total, f);
        if (ferror(f)) {
            return JC_ERR_IO;
        }
        total += (jc_size)step;
        if (step == 0) {
            break; /* clean EOF */
        }
    }
    buf[total] = '\0';
    *out = buf;
    if (len != NULL) {
        *len = total;
    }
    return JC_OK;
}

jc_status jc_read_file(const char *path, char **out, jc_size *len,
                       struct jc_arena *a)
{
    FILE *f;
    long size;
    char *buf;
    size_t got;

    /* M198: a directory is not a readable file, but fopen("rb") on one SUCCEEDS
     * on Linux -- ftell then reports a size and fread fails, so this used to
     * return JC_OK with an empty string: a silent wrong answer rather than an
     * error. Rejecting a directory here cannot affect a pipe, so unlike the
     * S_ISREG check (which belongs at the scanning callers -- see
     * jc_is_regular_file) it is safe at this chokepoint. */
    if (jc_is_dir(path)) {
        return JC_ERR_IO;
    }
    if (JC_FAULT_HIT(JC_FAULT_READ)) {
        return JC_ERR_IO; /* M198: simulated read failure */
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return JC_ERR_NOTFOUND;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return JC_ERR_IO;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return JC_ERR_IO;
    }
    if (size == 0) {
        /* M1138 (D-05): procfs reports ftell 0 for a file that HAS bytes, and
         * the seek to end succeeds -- so 0 means the size is UNKNOWN, and it is
         * stat-identical to a genuine empty regular file. The helper probe-reads
         * and takes the genuine-empty shape (JC_OK, len 0, 1-byte buffer) or
         * the bounded loop (at most JC_READ_FILE_MAX bytes) -- see it. */
        jc_status s;
        s = jc_read_unknown_size(f, out, len, a, (jc_size)JC_READ_FILE_MAX);
        fclose(f);
        return s;
    }
    /* Hard cap: refuse to slurp a pathological file into the arena (a 2GB file
     * would otherwise OOM the process). 64 MB is far above any source file. */
    if (size > JC_READ_FILE_MAX) {
        fclose(f);
        return JC_ERR_TOOBIG;
    }
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return JC_ERR_IO;
    }
    buf = (char *)jc_arena_alloc(a, (jc_size)size + 1);
    if (buf == NULL) {
        fclose(f);
        return JC_ERR_OOM;
    }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    if (len != NULL) {
        *len = (jc_size)got;
    }
    return JC_OK;
}

/* M1109 (D-35): a bounded variant of jc_read_file for the prompt-asset cut
 * sites -- rules' add_file (JC_RULES_MAX, keeps the HEAD), glossary's
 * append_file (JC_GLOSSARY_MAX, keeps the TAIL of the combined buffer) and
 * the command @-reference append_file (JC_CMD_FILE_MAX, keeps the HEAD).
 * Each of them used to jc_read_file the WHOLE file -- up to the 64 MB
 * JC_READ_FILE_MAX -- onto the SESSION arena and only then cut it to its
 * cap. The cut bounds the output, not the retention: the arena block holds
 * the whole file for the process lifetime, which is exactly D-35's
 * mechanism (seven sparse 60 MB prompt-asset files raised peak RSS from
 * 12.9 MB to 442 MB, readers slice). This helper reads at most `max` bytes
 * of a file the caller will cut to `max` anyway -- the leading `max` when
 * `head`, the trailing `max` otherwise -- so the whole file is never
 * allocated onto `a`.
 *
 * It is ADDITIVE: jc_read_file is unchanged, and its other callers (which
 * rely on the JC_ERR_TOOBIG / JC_ERR_NOTFOUND semantics and the 64 MB cap,
 * e.g. the M1100 jc_read_existing chain) are untouched. There is no TOOBIG
 * here: `max` IS the bound. The guard set mirrors jc_read_file's:
 * jc_is_dir -> JC_ERR_IO (M198), JC_FAULT_HIT(JC_FAULT_READ) -> JC_ERR_IO,
 * open failure -> JC_ERR_NOTFOUND, a failed size query -> JC_ERR_IO. Like
 * jc_read_file, *out is written only on JC_OK, and *len receives the number
 * of bytes read (min(size, max)). */
jc_status jc_read_file_bounded(const char *path, char **out, jc_size *len,
                               struct jc_arena *a, jc_size max, int head)
{
    FILE *f;
    long size;
    jc_size want;
    char *buf;
    size_t got;

    if (jc_is_dir(path)) {
        return JC_ERR_IO;
    }
    if (JC_FAULT_HIT(JC_FAULT_READ)) {
        return JC_ERR_IO; /* simulated read failure, as in jc_read_file */
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return JC_ERR_NOTFOUND;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return JC_ERR_IO;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return JC_ERR_IO;
    }
    if (size == 0) {
        /* M1138 (D-05): UNKNOWN size, not zero (procfs); stat-identical to a
         * genuine empty file, so the helper probe-reads. The bound is `max`
         * (the caller's cut), not JC_READ_FILE_MAX. The pre-existing
         * head/tail fseek below is a no-op at size 0 either way, so it is
         * skipped here. */
        jc_status s;
        s = jc_read_unknown_size(f, out, len, a, max);
        fclose(f);
        return s;
    }
    /* size >= 0 here, so casting to jc_size keeps the comparison unsigned
     * on both sides (no -Wsign-compare) and min(size, max) is unchanged. */
    want = ((jc_size)size > max) ? max : (jc_size)size;
    if (head) {
        if (fseek(f, 0L, SEEK_SET) != 0) {
            fclose(f);
            return JC_ERR_IO;
        }
    } else {
        if (fseek(f, (long)(size - (long)want), SEEK_SET) != 0) {
            fclose(f);
            return JC_ERR_IO;
        }
    }
    buf = (char *)jc_arena_alloc(a, want + 1);
    if (buf == NULL) {
        fclose(f);
        return JC_ERR_OOM;
    }
    /* want == 0 (an empty file) reads 0 bytes; buf[0] = '\0' still stands
     * and the caller gets JC_OK with *len == 0, as with jc_read_file. */
    got = fread(buf, 1, (size_t)want, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    if (len != NULL) {
        *len = (jc_size)got;
    }
    return JC_OK;
}

jc_status jc_read_existing(const char *path, char **out, jc_size *len,
                           struct jc_arena *a)
{
    struct stat st;
    jc_status stt;

    /* *out and *len are reset at the top and jc_read_file only ever writes
     * them on JC_OK, so on EVERY error return *out is NULL and *len is 0: the
     * caller decides by the STATUS (NOTFOUND => create fresh; anything else =>
     * present but unreadable, refuse), and a failed read cannot hand it a stale
     * pointer or length. M1100-b (review of 49e02556): *len was reset by the
     * CALLERS (jc_memory_add's `elen = 0`, the teeth's own pre-set) but never by
     * the function -- the teeth's `len == 0` arm asserted the test's pre-set,
     * not the helper's contract. */
    if (out != NULL) {
        *out = NULL;
    }
    if (len != NULL) {
        *len = 0;
    }
    if (path == NULL || path[0] == '\0') {
        return JC_ERR_INVALID;
    }
    /* M1100-b (review nit 1 on 161ceb10): LSTAT the LEAF, never stat. The old
     * stat() followed the link, so a symlink whose target is ABSENT reported
     * ENOENT -> JC_ERR_NOTFOUND and a writer that trusts the tri-state alone
     * would fopen("wb") through the link and create the TARGET -- the D-12
     * write-through class, re-disguised. lstat sees the link itself, so a
     * symlink (live or broken) is PRESENT: it can never read NOTFOUND and is
     * never read through. */
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            /* Genuinely absent. A DELETED symlink falls through to this arm
             * too: once unlinked, lstat fails ENOENT exactly like a truly
             * absent path (there is no link left to see), so "the path is gone"
             * is the honest answer and a caller may create a regular file here. */
            return JC_ERR_NOTFOUND;
        }
        /* lstat itself failed for any OTHER reason (a parent EACCES, ELOOP on
         * a cycle, ...): UNKNOWABLE -- the leaf may be present. Fail closed as
         * JC_ERR_IO so the caller refuses rather than assuming absence. Note
         * the direction: a parent EACCES today reports NOTFOUND via stat (the
         * stat follows into the unreadable parent and ENOENTs); lstat reports
         * IO instead -- REFUSE, the safe direction. */
        return JC_ERR_IO;
    }
    if (S_ISLNK(st.st_mode)) {
        /* A symlink, live or broken: PRESENT, so JC_ERR_IO, never NOTFOUND, and
         * never read through -- a writer must not create the target on the
         * other side of the link (the D-12 write-through class). The D-15
         * jc_state_file_check pre-verify at BOTH callers (jc_memory.c:269,
         * jc_learn.c:783) still lstats the leaf and fires FIRST with its own
         * more specific warning, so the behaviour at the current callers is
         * UNCHANGED -- this is the belt INSIDE the helper. */
        return JC_ERR_IO;
    }
    if (!S_ISREG(st.st_mode)) {
        /* Directory / FIFO / device: refuse explicitly (JC_ERR_IO), not by
         * jc_read_file's own accident (it handles a directory itself). */
        return JC_ERR_IO;
    }
    stt = jc_read_file(path, out, len, a);
    if (stt == JC_OK) {
        return JC_OK;
    }
    /* Present but unreadable: pass the READ error through UNCHANGED --
     * JC_ERR_TOOBIG over JC_READ_FILE_MAX stays TOOBIG, as do JC_ERR_IO
     * and JC_ERR_OOM. (A JC_ERR_NOTFOUND from jc_read_file is the TOCTOU
     * where the file vanished between lstat and open: it was absent at the
     * moment of the read, so report it as genuinely absent -- safe for a
     * caller to write.) */
    return stt;
}

jc_status jc_write_file(const char *path, const char *data, jc_size len)
{
    FILE *f;
    size_t put;
    f = fopen(path, "wb");
    if (f == NULL) {
        return JC_ERR_IO;
    }
    /* D-04/M979: inject a half-done in-place write (truncation) at the
     * truncate point. The one-shot sibling (JICHI_FAULT_WRITE_ONCE_AT) faults
     * a SINGLE call, so a recovery written after it can succeed -- M269's
     * reasoning: a recovery is exactly what we need to observe. */
    if (JC_FAULT_HIT(JC_FAULT_WRITE) || jc_fault_write_once_at()) {
        fclose(f);
        return JC_ERR_IO;
    }
    put = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || put != len) {
        return JC_ERR_IO;
    }
    return JC_OK;
}

/* D-08: the shared body of jc_write_file_atomic, its durable variant and
 * its mode-carrying variant. `durable` selects the fsync half -- with it off
 * the flow is byte-for-byte the pre-D-08 writer (open tmp -> fwrite -> fclose
 * -> rename, no fsync), so the 15+ non-durable callers (telemetry,
 * calibration, config) are unchanged. `set_mode` (M1099, D-12) selects a
 * fchmod of the temp to `mode` between the fclose and the rename -- the two
 * public 0600 writers below pass set_mode = 0 and are byte- and
 * mode-identical to before this parameter existed. */
static jc_status jc_write_file_atomic_impl(const char *path, const char *data,
                                           jc_size len, int durable,
                                           int set_mode, int mode)
{
    char tmp[1200];
    int fd;
    FILE *f;
    size_t put;

    if (path == NULL || strlen(path) + 32 >= sizeof(tmp)) {
        return JC_ERR_INVALID;
    }
    /* Same-directory temp so the final rename() is atomic on that fs. The
     * pid suffix is unique enough (jichi is single-threaded and writes its
     * private sinks sequentially); O_EXCL refuses a live collision, and a
     * leftover from a crashed run is removed and retried once. mkstemp is
     * XSI, not strict POSIX-200112, so it is deliberately not used. */
    /* D-18/M1104: honor the ONE-SHOT write knob (JICHI_FAULT_WRITE_ONCE_AT)
     * exactly as the in-place jc_write_file does (its M979 site). Before the
     * fix the four workspace rewrites (memory.md / rules file) were in-place
     * jc_write_file, so a failed write truncated the pre-existing content; the
     * one-shot knob was the means to inject precisely that failure on the
     * PRIMARY write. Those writes are now atomic mode-preserving replaces, so
     * the knob must reach this impl or the driver cannot reproduce a failed
     * primary (the monotone JC_FAULT_HIT alone faults the later write too,
     * which is the blanket-fail shape, not failed-primary-not-recovery). */
    if (JC_FAULT_HIT(JC_FAULT_WRITE) || jc_fault_write_once_at()) {
        return JC_ERR_IO; /* M198: simulated write failure */
    }
    jc_snprintf(tmp, sizeof(tmp), "%s.tmp%ld", path, (long)getpid());
    fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY | JC_O_BINARY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        remove(tmp);
        fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY | JC_O_BINARY, S_IRUSR | S_IWUSR);
    }
    if (fd < 0) {
        return JC_ERR_IO;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) {
        close(fd);
        remove(tmp);
        return JC_ERR_IO;
    }
    put = fwrite(data, 1, len, f);
    if (put != len) {
        fclose(f);
        remove(tmp);
        return JC_ERR_IO;
    }
    if (durable) {
        /* D-08: flush the data to stable storage BEFORE the rename makes it
         * visible. A power loss in the fclose->rename window leaves the bytes
         * in the page cache, which ext4's rename heuristic may but may not
         * save (F-resources D4; D-files 3.2). fsync(fd) is the durable
         * promise the shared writer deliberately never makes. */
        if (fsync(fileno(f)) != 0) {
            fclose(f);
            remove(tmp);
            return JC_ERR_IO;
        }
    }
    if (set_mode) {
        /* M1099 (D-12): apply the caller's mode to the temp BEFORE the rename
         * makes it the file, so the final file carries it atomically. The
         * fchmod is on `fd`, which is still open (fclose below closes it), and
         * is declared by <sys/stat.h> under strict POSIX-2001 (verified on
         * this tree's build); a failure is a real I/O error. */
        if (fchmod(fd, (mode_t)mode) != 0) {
            fclose(f);
            remove(tmp);
            return JC_ERR_IO;
        }
    }
    if (fclose(f) != 0) {
        remove(tmp);
        return JC_ERR_IO;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return JC_ERR_IO;
    }
    if (durable) {
        /* D-08: fsync the DIRECTORY so the rename itself (the new inode's
         * link) is durable -- a crash between rename and the dir fsync can
         * lose the new name while the old one is already gone. Best-effort:
         * the file's data is already durable, so a dir-fsync failure (a
         * read-only mount, some network fs) does not undo the write. */
        const char *slash = strrchr(path, '/');
        char dpath[1200];
        jc_size dl;
        int dfd;
        if (slash != NULL) {
            dl = (jc_size)(slash - path);
            if (dl >= sizeof(dpath)) dl = sizeof(dpath) - 1;
            memcpy(dpath, path, dl);
            dpath[dl] = '\0';
        } else {
            dpath[0] = '.';
            dpath[1] = '\0';
        }
        {
            /* M1083 (D-08): open the directory itself so the fsync below
             * commits the rename. O_DIRECTORY keeps open() refusing a file.
             * JC_O_BINARY is 0 on POSIX, but the lint (portability check 38)
             * scans this open()'s text for the flag, as it does for every
             * other open() in this file -- so it is in the call, not in
             * dflags. */
            int dflags = O_RDONLY;
#ifdef O_DIRECTORY
            dflags |= O_DIRECTORY;
#endif
            dfd = open(dpath, dflags | JC_O_BINARY);
            if (dfd >= 0) {
                fsync(dfd);
                close(dfd);
            }
        }
    }
    return JC_OK;
}

jc_status jc_write_file_atomic(const char *path, const char *data, jc_size len)
{
    return jc_write_file_atomic_impl(path, data, len, 0, 0, 0);
}

jc_status jc_write_file_atomic_durable(const char *path, const char *data,
                                       jc_size len)
{
    return jc_write_file_atomic_impl(path, data, len, 1, 0, 0);
}

jc_status jc_write_file_atomic_mode(const char *path, const char *data,
                                    jc_size len, int mode)
{
    return jc_write_file_atomic_impl(path, data, len, 0, 1, mode);
}

/* M1104-c: the target's CURRENT permission bits, or 0644 for a genuinely
 * absent target (what fopen("wb") under the default umask produced when it
 * created such a file pre-M1099). lstat -- the callers (the four workspace
 * rewrites) run jc_state_file_check (D-15) first, so a symlink here is the
 * TOCTOU case, and lstat reports the LINK's mode, never the target's. An
 * lstat failure is not an error: the file is (now) absent, so 0644 is the
 * honest answer. */
static int jc_preserve_link_target_mode(const char *path)
{
    struct stat st;
    if (lstat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        return (int)(st.st_mode & 0777);
    }
    return 0644;
}

jc_status jc_write_file_preserve_link(const char *path, const char *data,
                                      jc_size len)
{
    struct stat st;

    if (lstat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink > 1) {
        /* M1104-c: the target is a HARD LINK (a second name, e.g. AGENTS.md
         * linked to CLAUDE.md to share one rules file between two tools). An
         * atomic tmp+rename would replace the inode and silently ORPHAN the
         * other name -- the M1104 side effect measured as SV:0257. In-place
         * (the M141 / vim backupcopy=auto convention) keeps the inode, so
         * every name sees the new bytes at once. The accepted trade: a failed
         * in-place write TRUNCATES the shared inode -- the exact M141 trade
         * for link integrity. The four callers run jc_state_file_check first
         * (a regular file, never a symlink). The fault knob stays honoured
         * (it lives inside jc_write_file, byte-for-byte the M979/D-04 shape),
         * so the FAULT=1 driver still reproduces a failed primary. */
        return jc_write_file(path, data, len);
    }
    /* Absent, symlink, or a regular file with exactly one link: the M1104
     * mode-preserving atomic replace. */
    return jc_write_file_atomic_mode(path, data, len,
                                     jc_preserve_link_target_mode(path));
}

/* --- M528: privacy as a verified effect (see jc_platform.h) --------------- */

enum jc_priv_verdict jc_priv_verdict_of(unsigned long st_uid,
                                        unsigned long st_mode,
                                        unsigned long euid)
{
    /* Ownership first, and it outranks the mode: a 0600 endpoint owned by
     * somebody else is THEIR private endpoint. Saying "too open" about it would
     * describe the wrong problem and suggest the wrong fix (a chmod that will
     * fail with EPERM) instead of the right one (you are looking at the wrong
     * path, or somebody planted it). */
    if (st_uid != euid) {
        return JC_PRIV_NOT_OWNER;
    }
    /* Any group or other bit at all. Not "world-writable" -- readable is enough
     * for a socket, because connect(2) needs only write permission and a
     * readable-by-group socket on a shared host is a group-wide shell. */
    if ((st_mode & 077UL) != 0UL) {
        return JC_PRIV_TOO_OPEN;
    }
    return JC_PRIV_OK;
}

const char *jc_priv_verdict_str(enum jc_priv_verdict v)
{
    switch (v) {
    case JC_PRIV_OK:        return "owner-only (0600/0700)";
    case JC_PRIV_NOT_OWNER: return "owned by another user";
    case JC_PRIV_TOO_OPEN:  return "group/other bits set -- must be 0600";
    default:                break;
    }
    return "could not be examined";
}

enum jc_priv_verdict jc_path_private_check(const char *path)
{
    struct stat st;
    if (path == NULL || path[0] == '\0') {
        return JC_PRIV_NO_STAT;
    }
    /* lstat, never stat: the thing AT the path is what another user can control,
     * and following a symlink would report the target's mode while the socket a
     * client connects to is the link. */
    if (lstat(path, &st) != 0) {
        return JC_PRIV_NO_STAT;
    }
    return jc_priv_verdict_of((unsigned long)st.st_uid,
                              (unsigned long)st.st_mode,
                              (unsigned long)geteuid());
}

int jc_dir_holds_private(unsigned long st_mode)
{
    /* Writable by group or other is only safe with the sticky bit, which stops
     * a user from unlinking a file they do not own -- that is exactly what makes
     * /tmp (1777) a legitimate home for a socket while 0777 is not. */
    if ((st_mode & 022UL) != 0UL) {
        return (st_mode & 01000UL) != 0UL ? 1 : 0;
    }
    return 1;
}

jc_status jc_make_executable(const char *path)
{
    struct stat st;
    mode_t mode;
    if (stat(path, &st) != 0) {
        return JC_ERR_IO;
    }
    mode = st.st_mode;
    /* Mirror `chmod +x`: add execute where the matching read bit is set. */
    if (mode & S_IRUSR) mode |= S_IXUSR;
    if (mode & S_IRGRP) mode |= S_IXGRP;
    if (mode & S_IROTH) mode |= S_IXOTH;
    return (chmod(path, mode) == 0) ? JC_OK : JC_ERR_IO;
}

/* See jc_platform.h: 0700 for what WE created, nothing for what was already
 * there. The existence test races a concurrent mkdir, and the loss in that race
 * is that a directory somebody else created in the same microsecond does not get
 * tightened -- which is precisely the outcome this function exists to produce. */
jc_status jc_mkdir_p_private(const char *dir)
{
    struct stat st;
    int existed;

    if (dir == NULL || *dir == '\0') {
        return JC_ERR_INVALID;
    }
    existed = (stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    if (jc_mkdir_p(dir) != JC_OK) {
        return JC_ERR_IO;
    }
    if (!existed) {
        jc_make_private(dir);
    }
    return JC_OK;
}

jc_status jc_make_private(const char *path)
{
    struct stat st;
    mode_t mode;
    if (stat(path, &st) != 0) {
        return JC_ERR_IO;
    }
    mode = S_ISDIR(st.st_mode) ? (mode_t)0700 : (mode_t)0600;
    if (JC_FAULT_HIT(JC_FAULT_CHMOD)) {
        /* M503: the filesystem that says yes and does nothing. Returning JC_OK
         * is the point -- callers already ignore the status, and the defect is
         * that SUCCESS is indistinguishable from a no-op. */
        return JC_OK;
    }
    return (chmod(path, mode) == 0) ? JC_OK : JC_ERR_IO;
}

int jc_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}
long jc_file_mode(const char *path)
{
    struct stat st;
    if (path == NULL || stat(path, &st) != 0) {
        return -1;
    }
    return (long)(st.st_mode & 07777);
}


double jc_now_seconds(void)
{
    return (double)time(NULL);
}

double jc_now_millis(void)
{
    /* CLOCK_MONOTONIC alone was NOT enough of a guard (M326u): <time.h>
     * declares the macro on every glibc, including those where the function
     * lives in librt (glibc < 2.17). The code compiled there and the LINK
     * failed. The Makefile now probes for the symbol and adds -lrt if that is
     * what it takes; JC_NO_CLOCK_GETTIME is defined only when the probe has
     * proved it unreachable either way, and then the time() fallback stands. */
#if defined(CLOCK_MONOTONIC) && !defined(JC_NO_CLOCK_GETTIME)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
    }
#endif
    return (double)time(NULL) * 1000.0; /* coarse fallback */
}

int jc_sleep_ms(long ms, volatile int *abort)
{
    /* Sleep in small slices so an abort flag is honoured promptly. */
    long remaining = ms;
    while (remaining > 0) {
#ifdef JC_NO_NANOSLEEP
        struct timeval tv;
#else
        struct timespec ts;
#endif
        long slice = remaining > 50 ? 50 : remaining;
        if (abort != NULL && *abort) {
            return 1;
        }
#ifdef JC_NO_NANOSLEEP
        /* M834 (def-266): DJGPP has neither nanosleep nor struct timespec -- POSIX
         * makes nanosleep optional (the Timers option) -- and the Makefile's
         * HAVE_NANOSLEEP probe says so. select() with no descriptors and a timeout
         * is the portable sleep, and every row this tree builds on has it. */
        tv.tv_sec = slice / 1000;
        tv.tv_usec = (slice % 1000) * 1000L;
        (void)select(0, NULL, NULL, NULL, &tv);
#else
        ts.tv_sec = slice / 1000;
        ts.tv_nsec = (slice % 1000) * 1000000L;
        nanosleep(&ts, NULL);
#endif
        remaining -= slice;
    }
    if (abort != NULL && *abort) {
        return 1;
    }
    return 0;
}

/* See jc_platform.h (M760). */
int jc_lock_take(const char *path, long wait_ms)
{
    struct flock fl;
    long waited = 0;
    int fd;

    if (path == NULL) {
        return JC_LOCK_NONE;
    }
    fd = open(path, O_RDWR | O_CREAT | JC_O_BINARY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return JC_LOCK_NONE;
    }
    for (;;) {
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0; /* to the end of the file, however long it grows */
        if (fcntl(fd, F_SETLK, &fl) == 0) {
            return fd;
        }
        /* EACCES and EAGAIN both mean "held" (POSIX allows either); EINTR is
         * a signal, not an answer. Anything else is a filesystem that cannot
         * lock, and waiting would not change it. */
        if (errno != EACCES && errno != EAGAIN && errno != EINTR) {
            close(fd);
            return JC_LOCK_NONE;
        }
        if (waited >= wait_ms) {
            close(fd);
            return JC_LOCK_BUSY;
        }
        jc_sleep_ms(10, NULL);
        waited += 10;
    }
}

void jc_lock_release(int fd)
{
    if (fd >= 0) {
        close(fd); /* drops the lock: fcntl locks belong to the (process, file) */
    }
}

double jc_file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1.0;
    }
    return (double)st.st_mtime;
}

int jc_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int jc_is_regular_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

long jc_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1L;
    }
    return (long)st.st_size;
}

jc_status jc_list_dir(const char *dir, struct jc_vec *names,
                      struct jc_arena *a)
{
    DIR *d;
    struct dirent *ent;

    d = opendir(dir);
    if (d == NULL) {
        /* M482: ABSENT and UNREADABLE are different answers, and this returned
         * NOTFOUND for both. Every caller then treated a permissions failure,
         * EMFILE, or a path that is not a directory as "there is nothing here" --
         * for the session store that meant a real fault presented as "(no saved
         * sessions)" with exit 0. All 27 callers test `!= JC_OK`, so splitting the
         * status is compatible; those that want the distinction now have it. */
        return (errno == ENOENT || errno == ENOTDIR)
               ? JC_ERR_NOTFOUND : JC_ERR_IO;
    }
    /* M1140 (D-10): POSIX says readdir leaves errno UNCHANGED on success and
     * sets it only on failure, so a stale errno from an earlier call would make
     * the end-of-listing check below read a clean end as a failure. Clear it
     * here so the only thing that can set it during the loop is a failing
     * readdir (the loop body's arena/vec failures return early). */
    errno = 0;
    while ((ent = readdir(d)) != NULL) {
        char *name;
#ifdef JC_FAULT
        /* M1140 (D-10): model a MID-LISTING readdir failure -- a NULL return
         * with errno set (EIO), the shape a 32-bit build without 64-bit offsets
         * produces on a 64-bit-inode filesystem. Placed before the dot-skip so
         * it aborts a listing that already has entries, not at a clean end. */
        if (JC_FAULT_HIT(JC_FAULT_LISTDIR)) {
            errno = EIO;
            break;
        }
#endif
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        name = jc_arena_strdup(a, ent->d_name);
        if (name == NULL) {
            closedir(d);
            return JC_ERR_OOM;
        }
        if (jc_vec_push(names, &name) != JC_OK) {
            closedir(d);
            return JC_ERR_OOM;
        }
    }
    /* M1140 (D-10): POSIX says readdir sets errno only on failure, so a NULL
     * return here is either a clean end (errno still 0, from the pre-loop clear
     * -- the healthy path is byte-identical to the old `closedir; return JC_OK`)
     * or a failure (errno != 0). Without this check a mid-listing readdir error
     * (a 32-bit build without 64-bit offsets reading a 64-bit-inode filesystem)
     * is indistinguishable from end-of-directory and a short listing is
     * returned with success -- the silent-truncation half of the finding. */
    if (errno != 0) {
        closedir(d);
        return JC_ERR_IO;
    }
    closedir(d);
    return JC_OK;
}

int jc_cpu_count(void)
{
    /* _SC_NPROCESSORS_ONLN is NOT in POSIX.1-2001. It is a widely-copied
     * extension -- glibc, musl, bionic and Darwin all have it, which is why
     * four libcs and five years never noticed. FreeBSD has it too, but hides
     * it behind __BSD_VISIBLE, and this tree compiles with
     * -D_POSIX_C_SOURCE=200112L, so on FreeBSD 15.1 the identifier is simply
     * undeclared and the build stops here (M459, the first non-Linux row).
     *
     * Guarded rather than worked around. The alternatives were both worse:
     * widening the feature-test macros would pull in every other extension on
     * every platform to fix one symbol, and a `#ifdef __FreeBSD__` sysctl path
     * would add this tree's first BSD conditional -- PLATFORMS.md's whole point
     * about the BSD row is that there are none, and a portability defect is a
     * poor reason to start.
     *
     * The cost is stated rather than hidden: where the symbol is absent this
     * reports 1, so maxParallelAgents defaults to 1 and spawn_parallel runs
     * one child. That is a degraded default on an otherwise working system,
     * exactly the "simply degrades" the register predicted -- and now it is
     * measured instead of assumed. */
#ifdef _SC_NPROCESSORS_ONLN
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) {
        return 1;
    }
    return (int)n;
#else
    return 1;
#endif
}

int jc_cpu_count_known(void)
{
    /* The same two branches as above, answering the other question: was the 1
     * measured or assumed? Kept adjacent on purpose -- a future edit that adds
     * a third way of obtaining the count has to answer both, and separating
     * them is how the two would drift. */
#ifdef _SC_NPROCESSORS_ONLN
    return sysconf(_SC_NPROCESSORS_ONLN) >= 1;
#else
    return 0;
#endif
}

unsigned long jc_mem_total_mb(void)
{
#if defined(__APPLE__)
    /* Darwin: HW_MEMSIZE (bytes) is the canonical total-RAM query.
     *
     * `unsigned long`, NOT `unsigned long long` (M400). HW_MEMSIZE is an
     * int64_t, and C89 has no 64-bit type -- but every Darwin ABI jichi could
     * run on (x86_64 and arm64) is LP64, so `unsigned long` IS 64 bits there
     * and sizeof gives sysctl the 8-byte buffer it wants. On a hypothetical
     * ILP32 Darwin the buffer is too small, sysctl fails with ENOMEM, and the
     * _SC_PHYS_PAGES path below answers instead -- a safe degrade, not a wrong
     * number.
     *
     * This branch is why the M400 `long long` lint exists: it had used
     * `unsigned long long` and `1024ULL` since it was written, which is three
     * diagnostics under this project's own mandatory
     * `-std=c89 -pedantic -Wall -Wextra` (and a hard failure under WERROR=1) --
     * invisible because NO MACHINE HERE HAS EVER COMPILED IT. Code behind a
     * platform guard we cannot build gets no compiler feedback, so grep has to
     * stand in for the compiler. See docs/PLATFORMS.md. */
    {
        int mib[2];
        unsigned long bytes = 0;
        size_t len = sizeof(bytes);
        mib[0] = CTL_HW;
        mib[1] = HW_MEMSIZE;
        if (sysctl(mib, 2, &bytes, &len, NULL, 0) == 0 && bytes > 0) {
            return bytes / (1024UL * 1024UL);
        }
    }
#endif
#if defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
    {
        long pages = sysconf(_SC_PHYS_PAGES);
        long page = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && page > 0) {
            /* Divide before multiplying to keep the intermediate in a long. */
            return (unsigned long)pages * (unsigned long)(page / 1024) / 1024UL;
        }
    }
#endif
    return 0UL;
}

unsigned long jc_disk_free_mb(const char *path)
{
    struct statvfs vfs;
    unsigned long bsize;
    if (path == NULL || statvfs(path, &vfs) != 0) {
        return 0UL;
    }
    /* f_frsize is the fragment size; f_bavail = blocks available to non-root.
     * Divide the block size to MiB-scale first to avoid overflow on big disks. */
    bsize = (unsigned long)vfs.f_frsize;
    if (bsize == 0UL) {
        bsize = (unsigned long)vfs.f_bsize;
    }
    return (unsigned long)vfs.f_bavail * (bsize / 1024UL) / 1024UL;
}

int jc_long_names_ok(const char *path)
{
#ifdef JC_HAVE_USE_LFN
    return _use_lfn(path) != 0;
#else
    (void)path;
    return 1;
#endif
}

jc_status jc_disk_space(const char *path, double *free_mb, double *free_files)
{
    struct statvfs vfs;
    double frsize;
    /* Sentinel first: on any early return the caller sees -1.0 ("unknown"),
     * never a stale or zero reading it might mistake for a full disk. */
    if (free_mb != NULL) {
        *free_mb = -1.0;
    }
    if (free_files != NULL) {
        *free_files = -1.0;
    }
    if (path == NULL || statvfs(path, &vfs) != 0) {
        return JC_ERR_IO;   /* unknown -- distinct from a true 0.0 free */
    }
    frsize = (double)vfs.f_frsize;
    if (frsize == 0.0) {
        frsize = (double)vfs.f_bsize;
    }
    if (free_mb != NULL) {
        /* All-double: no integer division truncates the fragment size (the
         * ZFS-512 bug in jc_disk_free_mb), and the mantissa is exact well past
         * any real disk. f_bavail is the count available to a non-root user. */
        *free_mb = (double)vfs.f_bavail * frsize / (1024.0 * 1024.0);
    }
    if (free_files != NULL) {
        *free_files = (double)vfs.f_favail;
    }
    return JC_OK;
}

int jc_tm_offset_plausible(const struct tm *local, const struct tm *utc)
{
    long days;
    long off;

    if (local == NULL || utc == NULL) {
        return 0;
    }
    if (local->tm_year == utc->tm_year) {
        days = (long)local->tm_yday - (long)utc->tm_yday;
    } else if (local->tm_year == utc->tm_year + 1 && local->tm_yday == 0 &&
               utc->tm_mon == 11 && utc->tm_mday == 31) {
        days = 1;  /* already New Year's Day here, still 31 December in UTC */
    } else if (local->tm_year + 1 == utc->tm_year && utc->tm_yday == 0 &&
               local->tm_mon == 11 && local->tm_mday == 31) {
        days = -1;
    } else {
        return 0;
    }
    if (days > 1 || days < -1) {
        return 0;
    }
    off = days * 86400L + (long)(local->tm_hour - utc->tm_hour) * 3600L +
          (long)(local->tm_min - utc->tm_min) * 60L +
          (long)(local->tm_sec - utc->tm_sec);
    return off >= -26L * 3600L && off <= 26L * 3600L && off % 900L == 0;
}

int jc_localtime_checked(long t, struct tm *out)
{
    time_t tt = (time_t)t;
    struct tm utc;

    if (out == NULL) {
        return -1;
    }
    /* The _r forms need _POSIX_C_SOURCE (set for this TU); they also keep the
     * UTC copy from being overwritten by localtime's static buffer. */
    if (gmtime_r(&tt, &utc) == NULL) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    if (localtime_r(&tt, out) == NULL || !jc_tm_offset_plausible(out, &utc)) {
        *out = utc;
        return 0;
    }
    return 1;
}

int jc_tz_repair(void)
{
    struct tm now;

    if (jc_localtime_checked((long)time(NULL), &now) != 0) {
        return 0;  /* 1: the zone is usable; -1: there is no clock to repair */
    }
    if (setenv("TZ", "UTC0", 1) != 0) {
        return 0;
    }
    tzset();
    return 1;
}

void jc_now_timestr(const char *fmt, char *buf, jc_size cap)
{
    struct tm lt;
    if (buf == NULL || cap == 0) {
        return;
    }
    /* Respects LC_TIME; UTC where the local zone is unusable (M839). */
    if (jc_localtime_checked((long)time(NULL), &lt) < 0 ||
        strftime(buf, (size_t)cap, (fmt != NULL) ? fmt : "%X", &lt) == 0) {
        buf[0] = '\0';
    }
}

enum jc_resource_tier jc_resource_tier(unsigned long mb, int cpu)
{
    (void)cpu; /* reserved: CPU could refine the tier later */
    if (mb == 0UL) {
        return JC_RES_NORMAL; /* unknown probe: don't downgrade */
    }
    if (mb < 512UL) {
        return JC_RES_MINIMAL;
    }
    if (mb < 1024UL) {
        return JC_RES_LITE;
    }
    return JC_RES_NORMAL;
}

int jc_locale_is_utf8(void)
{
    const char *v = getenv("LC_ALL");
    if (v == NULL || v[0] == '\0') v = getenv("LC_CTYPE");
    if (v == NULL || v[0] == '\0') v = getenv("LANG");
    if (v == NULL) return 0;
    return strstr(v, "UTF-8") != NULL || strstr(v, "UTF8") != NULL ||
           strstr(v, "utf-8") != NULL || strstr(v, "utf8") != NULL;
}

char jc_locale_group_sep(void)
{
    const char *s;
    char c = 0;
    /* Query the locale's numeric thousands separator, then RESTORE the C
     * numeric locale so printf/cJSON keep using '.' as the decimal point. A
     * persistent LC_NUMERIC switch would emit "1,5" into JSON and corrupt it. */
    setlocale(LC_NUMERIC, "");
    s = localeconv()->thousands_sep;
    if (s != NULL && s[0] != '\0') {
        c = s[0];
    }
    setlocale(LC_NUMERIC, "C");
    return c;
}

/* `uname()` SUCCEEDS WITH ANY NON-NEGATIVE VALUE, and one platform uses that.
 * POSIX says only "upon successful completion, a non-negative value shall be
 * returned" -- it does not say zero. Linux, the BSDs and both Windows layers
 * return 0, so `!= 0` read as "failed" everywhere this project had ever run,
 * and it was wrong on the first SysV kernel it met: illumos returns a positive
 * value, so every one of these three call sites took the failure path on a
 * system where the call had worked perfectly.
 *
 * Measured, 2026-09-22, on OmniOS r151058 through `scripts/tier-v-illumos.sh`:
 * `doctor` printed `! host platform not recognised` -- the branch main.c takes
 * when this function returns 0 -- while the rig's own `uname -a` on the same
 * guest, seconds earlier, printed the system fine. The visible cost was that
 * M695's whole platform-verdict feature was DEAD on illumos:
 * `jc_platform_row_verdict` below returned UNKNOWN, so the `"SunOS"` entry in
 * `jc_sys_partly[]` could never be reached and `doctor` told an illumos user
 * jichi had never been compiled there. `tests/smoke/doctor.sh` is what failed,
 * and it failed CORRECTLY: PLATFORMS.md partly-verifies SunOS and doctor would
 * not say so.
 *
 * `< 0` is the test POSIX actually licenses. It is identical on every platform
 * already measured, which is exactly why nothing caught this until a row that
 * returns something else ran. */
int jc_platform_describe(char *buf, jc_size cap)
{
    struct utsname u;
    if (buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (uname(&u) < 0) {
        return 0;
    }
    jc_snprintf(buf, cap, "%s %s (%s)", u.sysname, u.release, u.machine);
    return 1;
}

int jc_platform_is_linux(void)
{
    struct utsname u;
    return (uname(&u) >= 0 && strcmp(u.sysname, "Linux") == 0);
}

/* THE VERDICT THE PRODUCT STATES, and it has three values because
 * docs/PLATFORMS.md has three for a platform anyone has measured: Verified,
 * Partly verified, and never compiled. Collapsing the middle one into "never"
 * is what made doctor and the setup wizard tell three measured platforms --
 * illumos/Solaris, Cygwin and MSYS2, one of them FULLY DRIVEN -- that jichi had
 * never been built there (M695).
 *
 * Two arrays, one literal per line, because tests/smoke/portability_lint.sh
 * check 7c extracts both and pins each against its own table on that page: a
 * name here the page does not carry, or a row on the page this file does not
 * know, fails the build. M486 did that for the Verified tier only, so the tier
 * below it was outside the check's universe by construction and drifted for
 * exactly the same reason, one row down.
 *
 * An entry ending in '-' is a PREFIX. The Windows emulation layers append the
 * host's build number, so uname -s reads CYGWIN_NT-10.0-26200 today and
 * something else after the next Windows release; a whole-string compare there
 * would go stale silently, which is the failure this whole function is about.
 * Every other entry is a complete sysname and must match exactly -- so a
 * hypothetical "Linuxwhatever" is NOT Linux.
 *
 * Names, not capabilities: this decides what to SAY, never what to attempt.
 * The capability questions have their own probes (jc_have_proc_rss). */
static const char *const jc_sys_verified[] = {
    "Linux",
    "FreeBSD",
    "NetBSD",
    "OpenBSD",
    NULL
};

static const char *const jc_sys_partly[] = {
    "SunOS",
    "CYGWIN_NT-",
    "MSYS_NT-",
    /* M817: first verified macOS build (Dima Mellnik); partly verified. */
    "Darwin",
    /* M830: first verified Haiku build (hrev60160, gcc 13.3.0); partly verified. */
    "Haiku",
    NULL
};

static int jc_sys_listed(const char *sysname, const char *const *list)
{
    int i;
    jc_size n;
    for (i = 0; list[i] != NULL; i++) {
        n = (jc_size)strlen(list[i]);
        if (n > 0 && list[i][n - 1] == '-') {
            if (strncmp(sysname, list[i], n) == 0) {
                return 1;
            }
        } else if (strcmp(sysname, list[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Split from the uname() call so the table can be tested on any host: the
 * mapping is the part that rots, and a test that can only exercise the row it
 * is running on is how this drifted in the first place (tests/test_platform.c). */
int jc_platform_row_verdict_for(const char *sysname)
{
    if (sysname == NULL || sysname[0] == '\0') {
        return JC_PLATFORM_ROW_UNKNOWN;
    }
    if (jc_sys_listed(sysname, jc_sys_verified)) {
        return JC_PLATFORM_ROW_VERIFIED;
    }
    if (jc_sys_listed(sysname, jc_sys_partly)) {
        return JC_PLATFORM_ROW_PARTLY;
    }
    return JC_PLATFORM_ROW_UNKNOWN;
}

int jc_platform_row_verdict(void)
{
    struct utsname u;
    if (uname(&u) < 0) {   /* non-negative is SUCCESS -- see jc_platform_describe */
        return JC_PLATFORM_ROW_UNKNOWN;
    }
    return jc_platform_row_verdict_for(u.sysname);
}

int jc_platform_verified_row(void)
{
    return jc_platform_row_verdict() == JC_PLATFORM_ROW_VERIFIED;
}

int jc_have_proc_rss(void)
{
    /* A CAPABILITY probe, not a name check (M326q). The RSS watchdog behind
     * `memBudgetMb` walks /proc/<pid>/stat; asking "is this Linux?" gets the
     * wrong answer in a container or chroot where /proc is not mounted, and on
     * any non-Linux system that does provide it. So try the exact file the
     * watchdog needs, on this process, and believe the result. */
    FILE *f;
    if (JC_FAULT_HIT(JC_FAULT_PROCFS)) {
        return 0;             /* a system without /proc, on demand */
    }
    f = fopen("/proc/self/stat", "rb");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

#ifdef JC_NOFORK
/* M769: the NOFORK measuring build's stand-ins (the Makefile's NOFORK note).
 * With -Dfork=jc_nofork_fork the system header's own declaration of fork() names
 * this function, so the prototype comes from <unistd.h> and <stdio.h> as before.
 * Each refuses the way a system without fork would: -1 or NULL, errno ENOSYS. */
pid_t jc_nofork_fork(void)
{
    errno = ENOSYS;
    return -1;
}

FILE *jc_nofork_popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}
#endif /* JC_NOFORK */
