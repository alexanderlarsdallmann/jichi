/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_landlock.c - a Linux Landlock write fence (see jc_landlock.h).
 *
 * Everything the kernel headers would give us is defined locally instead: the
 * access-right bits, the two attribute structs (packed by byte order), and the
 * syscall numbers. So the file needs no <linux/...> header and no Makefile probe
 * -- it compiles on any Linux (32-bit and 64-bit), and the RUNTIME ABI query decides
 * whether the fence can be applied. On a non-Linux kernel the whole body is compiled
 * out and the query returns 0.
 *
 * M1018 E-02 fix: the kernel expects __u64 handled_access_fs in little-endian byte
 * order. On 32-bit Linux, copying from unsigned long (4 bytes) leaves bytes [4..7)
 * uninitialized. landlock_pack_u64 zeros the 8-byte buffer first, then packs each
 * byte explicitly using shifts, ensuring the high 4 bytes are zero-extended. */

/* syscall() and O_CLOEXEC live behind _GNU_SOURCE, not strict POSIX. This TU is
 * Linux-only, and glibc treats _GNU_SOURCE as the superset of the -D_POSIX_C_SOURCE
 * the build sets, so requesting it here is safe. Must precede the first include. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "jc_landlock.h"

#include <ctype.h>

#if defined(__linux__)

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/prctl.h>

/* M1018 E-02: byte-packing helper. Packs sizeof(v) bytes of an unsigned long into
 * 8 bytes in little-endian order, leaving the rest of the zeroed buffer alone.
 * On 64-bit platforms this is byte-identical to an 8-byte memcpy; on 32-bit it
 * packs only 4 bytes, and the memset(0,8) ensures bytes [4..7] remain zero.
 * C89-clean (8*i with int i). The implementation follows after the #defines. */

/* Syscall numbers (Landlock, since Linux 5.13). Since Linux 5.1 a new syscall has
 * ONE number on every architecture but alpha, so these hold on x86_64/aarch64/... */
#ifndef SYS_landlock_create_ruleset
#define SYS_landlock_create_ruleset 444
#endif
#ifndef SYS_landlock_add_rule
#define SYS_landlock_add_rule 445
#endif
#ifndef SYS_landlock_restrict_self
#define SYS_landlock_restrict_self 446
#endif
#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

#define JC_LL_CREATE_RULESET_VERSION (1U)   /* flags: query the ABI version    */
#define JC_LL_RULE_PATH_BENEATH      (1U)   /* enum landlock_rule_type          */

/* Access-right bits. All are <= bit 14, so the mask fits an `unsigned long`. The
 * kernel's __u64 field is 8 bytes on ALL Linux platforms (32-bit and 64-bit). The
 * helper landlock_pack_u64 packs bytes in little-endian order, zero-extending on
 * 32-bit. (C89 has no `long long`; the tree forbids it. `UL` is C89.) */
#define JC_LL_EXECUTE     (1UL << 0)
#define JC_LL_WRITE_FILE  (1UL << 1)
#define JC_LL_READ_FILE   (1UL << 2)
#define JC_LL_READ_DIR    (1UL << 3)
#define JC_LL_REMOVE_DIR  (1UL << 4)
#define JC_LL_REMOVE_FILE (1UL << 5)
#define JC_LL_MAKE_CHAR   (1UL << 6)
#define JC_LL_MAKE_DIR    (1UL << 7)
#define JC_LL_MAKE_REG    (1UL << 8)
#define JC_LL_MAKE_SOCK   (1UL << 9)
#define JC_LL_MAKE_FIFO   (1UL << 10)
#define JC_LL_MAKE_BLOCK  (1UL << 11)
#define JC_LL_MAKE_SYM    (1UL << 12)
#define JC_LL_REFER       (1UL << 13)   /* ABI 2 */
#define JC_LL_TRUNCATE    (1UL << 14)   /* ABI 3 */

#ifndef O_PATH
#define O_PATH 010000000
#endif

/* M1018 E-02: byte-packing implementation (C89-clean, 8*i with int i). */
void jc_landlock_pack_u64(unsigned char *dst8, unsigned long v)
{
    int i;
    memset(dst8, 0, 8);   /* zero first: ensures bytes [sizeof v .. 8) are zero on 32-bit */
    for (i = 0; i < (int)sizeof v; i++) {
        dst8[i] = (unsigned char)((v >> (8 * i)) & 0xff);
    }
}

int jc_landlock_abi(void)
{
    long v = syscall(SYS_landlock_create_ruleset, (void *)0, (size_t)0,
                     JC_LL_CREATE_RULESET_VERSION);
    if (v < 0) {
        return 0;   /* ENOSYS / EOPNOTSUPP / disabled -- unavailable */
    }
    return (int)v;
}

/* Add one path-beneath rule granting `access` under the file/dir at `path`. The
 * attr is packed in the kernel (8-byte access, 4-byte fd = 12 bytes); C89 cannot
 * express that, so build the 12-byte buffer by hand. A path that will not open is
 * skipped: it grants nothing, so it cannot widen the fence.
 *
 * M1018 E-02: the allowed_access field (__u64) must be zero-padded on 32-bit.
 * We zero the entire attr buffer first, then use the helper to pack the 64-bit
 * access value, and finally place the fd at offset 8. */
static int add_beneath(int ruleset_fd, const char *path, unsigned long access)
{
    unsigned char attr[12];
    int fd;
    long rc;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0) {
        return 0;   /* missing/unreadable -> skip (cannot widen) */
    }
    /* Zero the buffer first, then pack access and fd by byte order. */
    memset(attr, 0, sizeof attr);
    jc_landlock_pack_u64(attr, access);        /* allowed_access at offset 0 (8 bytes) */
    memcpy(attr + 8, &fd, sizeof fd);          /* parent_fd at offset 8 (4 bytes) */
    rc = syscall(SYS_landlock_add_rule, ruleset_fd,
                 (int)JC_LL_RULE_PATH_BENEATH, (void *)attr, 0U);
    close(fd);
    return rc == 0 ? 0 : -1;
}

jc_status jc_landlock_write_fence_ex(const char *const *dirs, int n_dirs,
                                     const char *const *files, int n_files,
                                     int *failed_dir)
{
    unsigned char rsattr[8];
    unsigned long handled;
    int abi;
    int ruleset_fd;
    int i;
    long rc;

    if (failed_dir != NULL) {
        *failed_dir = -1;
    }
    abi = jc_landlock_abi();
    if (abi < 1) {
        return JC_ERR_DENIED;   /* Landlock unavailable -- caller runs unfenced */
    }

    /* Handle only the WRITE-ish rights the running kernel supports; leave READ and
     * EXECUTE unhandled, so they stay free. */
    handled = JC_LL_WRITE_FILE | JC_LL_REMOVE_DIR | JC_LL_REMOVE_FILE |
              JC_LL_MAKE_CHAR | JC_LL_MAKE_DIR | JC_LL_MAKE_REG |
              JC_LL_MAKE_SOCK | JC_LL_MAKE_FIFO | JC_LL_MAKE_BLOCK |
              JC_LL_MAKE_SYM;
    if (abi >= 2) {
        handled |= JC_LL_REFER;
    }
    if (abi >= 3) {
        handled |= JC_LL_TRUNCATE;
    }

    /* struct landlock_ruleset_attr { __u64 handled_access_fs; ... }: set the one
     * field and pass its size; the kernel accepts a smaller size than it knows.
     * M1018 E-02: on 32-bit Linux, unsigned long is 4 bytes but the kernel expects
     * 8 bytes (__u64). We zero rsattr first, then use the helper to pack the value
     * in little-endian byte order, ensuring high 4 bytes are zero-extended. */
    memset(rsattr, 0, sizeof rsattr);
    jc_landlock_pack_u64(rsattr, handled);
    ruleset_fd = (int)syscall(SYS_landlock_create_ruleset, (void *)rsattr,
                              sizeof rsattr, 0U);
    if (ruleset_fd < 0) {
        return JC_ERR_IO;
    }

    for (i = 0; i < n_dirs; i++) {
        if (add_beneath(ruleset_fd, dirs[i], handled) != 0) {
            /* M852: say WHICH directory the kernel refused a rule for (a
             * regular file listed as a directory is the reproducible case:
             * directory-only rights on a non-directory are EINVAL). The
             * process is not restricted yet -- restrict_self is below. */
            if (failed_dir != NULL) {
                *failed_dir = i;
            }
            close(ruleset_fd);
            return JC_ERR_IO;
        }
    }
    for (i = 0; i < n_files; i++) {
        if (add_beneath(ruleset_fd, files[i], JC_LL_WRITE_FILE) != 0) {
            close(ruleset_fd);
            return JC_ERR_IO;
        }
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        close(ruleset_fd);
        return JC_ERR_IO;
    }
    rc = syscall(SYS_landlock_restrict_self, ruleset_fd, 0U);
    close(ruleset_fd);
    return rc == 0 ? JC_OK : JC_ERR_IO;
}

#else /* !__linux__ */

int jc_landlock_abi(void)
{
    return 0;
}

jc_status jc_landlock_write_fence_ex(const char *const *dirs, int n_dirs,
                                     const char *const *files, int n_files,
                                     int *failed_dir)
{
    (void)dirs; (void)n_dirs; (void)files; (void)n_files;
    if (failed_dir != NULL) {
        *failed_dir = -1;
    }
    return JC_ERR_DENIED;
}

#endif

jc_status jc_landlock_write_fence(const char *const *dirs, int n_dirs,
                                  const char *const *files, int n_files)
{
    return jc_landlock_write_fence_ex(dirs, n_dirs, files, n_files, NULL);
}

/* M852 (N36): does a fenced shell command's output look like a refused write?
 * Landlock answers a write outside the fence with EACCES, which each tool prints
 * in its own words; these are the spellings measured or known: libc's strerror
 * ("Permission denied" -- cc's "Cannot create temporary file ...: Permission
 * denied", mktemp, sh's redirect), Zig's error name ("AccessDenied"), and EPERM's
 * ("Operation not permitted", which Landlock gives for some renames). Matched
 * case-insensitively, as text: a hint is advice, never a verdict -- an ordinary
 * permission error on a READ matches too, which is why the hint says "if". */
int jc_landlock_permission_error(const char *output)
{
    static const char *const marks[] = {
        "permission denied", "accessdenied", "access denied",
        "operation not permitted"
    };
    const char *p;
    jc_size i;

    if (output == NULL) {
        return 0;
    }
    for (p = output; *p != '\0'; p++) {
        for (i = 0; i < sizeof marks / sizeof marks[0]; i++) {
            const char *m = marks[i];
            const char *q = p;
            while (*m != '\0' && *q != '\0' &&
                   tolower((unsigned char)*q) == (unsigned char)*m) {
                m++;
                q++;
            }
            if (*m == '\0') {
                return 1;
            }
        }
    }
    return 0;
}
