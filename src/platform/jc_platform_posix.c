/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_platform_posix.c - POSIX implementations of jc_platform.h helpers.
 *
 * This translation unit (and only a handful of others) is compiled with
 * -D_POSIX_C_SOURCE so that opendir/mkdir/stat prototypes are visible. The
 * rest of jichi stays strict C89.
 */

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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>
#include <sys/statvfs.h>
#include <pwd.h>   /* getpwuid: ask the system for home, don't guess /tmp (M472) */
#if defined(__APPLE__)
#include <sys/sysctl.h>
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
 *      directory we own with no group/other bits, a pid-scoped one that cannot
 *      have been pre-planted. That trades persistence across runs (already lost
 *      in a degraded environment) for never writing a key into a directory
 *      somebody else controls. It does NOT exit: a getter that kills the process
 *      is a worse contract than a getter that degrades loudly, and `doctor`
 *      reports the situation so an unattended run can gate on it.
 *
 * NOT cached, deliberately, and this cost a segfault to learn: an earlier cut
 * resolved once into a static because "49 call sites should not each re-derive
 * it". tests/test_index.c:178 calls setenv("HOME", ...) mid-run and expects the
 * next call to see it -- with the cache in place the planted cache directory was
 * never found and a NULL reached strcmp. HOME is genuinely mutable within a
 * process, so the getter must read it every time. The fallback paths recompute
 * deterministically, which costs a getpwuid only when HOME is unset; only the
 * one-time notice needs state. */
const char *jc_home_dir(void)
{
    static char fallback[1024];
    static int warned = 0;
    const char *h;
    struct passwd *pw;
    struct stat st;

    h = getenv("HOME");
    if (h != NULL && h[0] != '\0') {
        return h; /* the pointer into environ, exactly as before */
    }

    pw = getpwuid(getuid());
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

    /* Neither HOME nor a password entry. Do not write secrets into /tmp itself. */
    jc_snprintf(fallback, sizeof(fallback), "/tmp/jichi-%lu",
                (unsigned long)getuid());
    (void)mkdir(fallback, 0700);
    if (lstat(fallback, &st) != 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != getuid() || (st.st_mode & (mode_t)077) != 0) {
        /* Pre-planted, a symlink, or shared. A pid-scoped name cannot have been
         * waiting for us; state does not survive the run, which is the correct
         * trade against handing over the key file. */
        jc_snprintf(fallback, sizeof(fallback), "/tmp/jichi-%lu-%lu",
                    (unsigned long)getuid(), (unsigned long)getpid());
        (void)mkdir(fallback, 0700);
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

jc_status jc_write_file(const char *path, const char *data, jc_size len)
{
    FILE *f;
    size_t put;
    f = fopen(path, "wb");
    if (f == NULL) {
        return JC_ERR_IO;
    }
    put = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || put != len) {
        return JC_ERR_IO;
    }
    return JC_OK;
}

jc_status jc_write_file_atomic(const char *path, const char *data, jc_size len)
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
    if (JC_FAULT_HIT(JC_FAULT_WRITE)) {
        return JC_ERR_IO; /* M198: simulated write failure */
    }
    jc_snprintf(tmp, sizeof(tmp), "%s.tmp%ld", path, (long)getpid());
    fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        remove(tmp);
        fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
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
    if (fclose(f) != 0 || put != len) {
        remove(tmp);
        return JC_ERR_IO;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return JC_ERR_IO;
    }
    return JC_OK;
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
        struct timespec ts;
        long slice = remaining > 50 ? 50 : remaining;
        if (abort != NULL && *abort) {
            return 1;
        }
        ts.tv_sec = slice / 1000;
        ts.tv_nsec = (slice % 1000) * 1000000L;
        nanosleep(&ts, NULL);
        remaining -= slice;
    }
    if (abort != NULL && *abort) {
        return 1;
    }
    return 0;
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
    while ((ent = readdir(d)) != NULL) {
        char *name;
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

void jc_now_timestr(const char *fmt, char *buf, jc_size cap)
{
    time_t t;
    struct tm lt;
    if (buf == NULL || cap == 0) {
        return;
    }
    t = time(NULL);
    /* localtime_r needs _POSIX_C_SOURCE (set for this TU). Respects LC_TIME. */
    localtime_r(&t, &lt);
    if (strftime(buf, (size_t)cap, (fmt != NULL) ? fmt : "%X", &lt) == 0) {
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

int jc_platform_describe(char *buf, jc_size cap)
{
    struct utsname u;
    if (buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (uname(&u) != 0) {
        return 0;
    }
    jc_snprintf(buf, cap, "%s %s (%s)", u.sysname, u.release, u.machine);
    return 1;
}

int jc_platform_is_linux(void)
{
    struct utsname u;
    return (uname(&u) == 0 && strcmp(u.sysname, "Linux") == 0);
}

/* Kept in one array so portability_lint check 7c can extract it and compare
 * with docs/PLATFORMS.md. Keep the literals on their own lines for that reason. */
int jc_platform_verified_row(void)
{
    static const char *verified[] = {
        "Linux",
        "FreeBSD",
        "NetBSD",
        "OpenBSD",
        NULL
    };
    struct utsname u;
    int i;
    if (uname(&u) != 0) {
        return 0;
    }
    for (i = 0; verified[i] != NULL; i++) {
        if (strcmp(u.sysname, verified[i]) == 0) {
            return 1;
        }
    }
    return 0;
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
    f = fopen("/proc/self/stat", "r");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}
