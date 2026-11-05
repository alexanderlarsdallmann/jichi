/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_platform.c - file-type predicates and read-path edge cases (M198).
 *
 * These cover the boundary between "a path the user NAMED" and "a path jichi
 * DISCOVERED by scanning a directory it owns". A non-regular file is garbage in
 * the second case (and used to hang the process), but may be legitimate in the
 * first -- `--config <(jq ...)` resolves to a pipe -- so the S_ISREG check lives
 * at the scanning callers, not inside jc_read_file. See
 * docs/proposals/2026-07-robustness-edge-cases.md (#1).
 */

#include "jc_test.h"
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_vec.h"      /* M1140 (D-10): jc_list_dir's name vector */
#include "jc_fault.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#ifdef JC_FAULT
static void test_home_dir_pid_fallback(void); /* C-16 (M1134) */
static void test_list_dir_readdir_error(void); /* D-10 (M1140) */
#endif

void test_platform(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char dir[256];
    char reg[320];
    char sub[320];
    char fifo[320];
    char dangling[320];
    char *text;
    jc_size len;

    /* M461: whatever jc_shell_path() resolves to must be executable HERE.
     * On every FHS platform that is /bin/sh and this is trivially true; on
     * Android, where /bin does not exist, it is the entire point -- sixteen
     * hardcoded "/bin/sh" literals failed 53 checks across 12 files on an
     * Android 4.4.2 tablet before the resolver existed. The check is written
     * as "the shell we will spawn can be spawned", which is the property that
     * actually matters and is the one that varies by platform. */
    JC_CHECK(jc_shell_path() != NULL);
    JC_CHECK(access(jc_shell_path(), X_OK) == 0);

    /* M695: the platform VERDICT table, exercised on any host.
     *
     * Table-driven on purpose. The defect this guards was INVISIBLE on the
     * development platform: Linux answered correctly throughout, while
     * FreeBSD, NetBSD, OpenBSD, illumos, Cygwin and MSYS2 were told by
     * `doctor` and by the setup wizard that jichi had never been compiled
     * there -- on Cygwin, while running a binary it had just compiled there.
     * A test that can only see the row it runs on cannot see that, which is
     * why there was no test here at all before this one. */
    JC_CHECK(jc_platform_row_verdict_for("Linux") == JC_PLATFORM_ROW_VERIFIED);
    JC_CHECK(jc_platform_row_verdict_for("FreeBSD") == JC_PLATFORM_ROW_VERIFIED);
    JC_CHECK(jc_platform_row_verdict_for("NetBSD") == JC_PLATFORM_ROW_VERIFIED);
    JC_CHECK(jc_platform_row_verdict_for("OpenBSD") == JC_PLATFORM_ROW_VERIFIED);

    /* Partly verified is a MEASURED verdict, not a softer kind of unknown:
     * the matrix carries a row for each of these. */
    JC_CHECK(jc_platform_row_verdict_for("SunOS") == JC_PLATFORM_ROW_PARTLY);

    /* The Windows emulation layers append the host's build number, so these
     * match as PREFIXES -- an exact compare would go stale at the next Windows
     * release, silently, which is the failure mode this whole change is about.
     * The two concrete strings were measured on this bench 2026-09-21; the
     * third is a future build that must still resolve. */
    JC_CHECK(jc_platform_row_verdict_for("CYGWIN_NT-10.0-26200")
             == JC_PLATFORM_ROW_PARTLY);
    JC_CHECK(jc_platform_row_verdict_for("MSYS_NT-10.0-26200")
             == JC_PLATFORM_ROW_PARTLY);
    JC_CHECK(jc_platform_row_verdict_for("CYGWIN_NT-11.0-99999")
             == JC_PLATFORM_ROW_PARTLY);
    /* M817: macOS is partly verified since Dima Mellnik's first Darwin build.
     * `uname -s` is exactly "Darwin" (no build suffix), so it is an exact match. */
    JC_CHECK(jc_platform_row_verdict_for("Darwin") == JC_PLATFORM_ROW_PARTLY);
    /* M830: Haiku is partly verified since the first Haiku build (hrev60160).
     * `uname -s` is exactly "Haiku". */
    JC_CHECK(jc_platform_row_verdict_for("Haiku") == JC_PLATFORM_ROW_PARTLY);

    /* Unknown stays unknown, and a prefix must not swallow a different name:
     * without the trailing-dash rule "Linux" would match "Linuxish", and the exact
     * "Darwin" entry must not match "Darwinish". */
    JC_CHECK(jc_platform_row_verdict_for("Darwinish") == JC_PLATFORM_ROW_UNKNOWN);
    JC_CHECK(jc_platform_row_verdict_for("Linuxish") == JC_PLATFORM_ROW_UNKNOWN);
    JC_CHECK(jc_platform_row_verdict_for("SunOSish") == JC_PLATFORM_ROW_UNKNOWN);
    JC_CHECK(jc_platform_row_verdict_for("") == JC_PLATFORM_ROW_UNKNOWN);
    JC_CHECK(jc_platform_row_verdict_for(NULL) == JC_PLATFORM_ROW_UNKNOWN);

    /* The back-compat shorthand still means exactly "Verified", nothing wider. */
    JC_CHECK(jc_platform_verified_row()
             == (jc_platform_row_verdict() == JC_PLATFORM_ROW_VERIFIED));


    if (!JC_REQUIRE(jc_snprintf(dir, sizeof dir, "%s/jichi_plat_test_%ld",
                                jc_test_tmpdir(), (long)getpid())
                    < (int)sizeof dir)) {
        jc_arena_free(a);
        return; /* a cut fixture path is a different path (M728) */
    }
    (void)jc_test_rm_rf(dir);
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);

    jc_snprintf(reg, sizeof reg, "%s/plain.txt", dir);
    jc_snprintf(sub, sizeof sub, "%s/subdir", dir);
    jc_snprintf(fifo, sizeof fifo, "%s/pipe.json", dir);
    jc_snprintf(dangling, sizeof dangling, "%s/dangling", dir);

    JC_CHECK(jc_write_file(reg, "hello\n", 6) == JC_OK);
    JC_CHECK(jc_mkdir_p(sub) == JC_OK);

    /* --- jc_is_regular_file ------------------------------------------------ */
    JC_CHECK(jc_is_regular_file(reg) == 1);
    JC_CHECK(jc_is_regular_file(sub) == 0);          /* a directory is not */
    JC_CHECK(jc_is_regular_file(dangling) == 0);     /* missing is not */
    JC_CHECK(jc_is_regular_file("/nonexistent/x") == 0);
    /* NULL-ish and empty must not crash. */
    JC_CHECK(jc_is_regular_file("") == 0);

    /* A FIFO is the case that used to hang a scan: it exists, it is readable in
     * principle, and it is NOT a regular file -- which is the only cheap way to
     * know not to open it. */
    if (mkfifo(fifo, 0600) == 0) {
        JC_CHECK(jc_file_exists(fifo) == 1);   /* exists... */
        JC_CHECK(jc_is_regular_file(fifo) == 0); /* ...but must be skipped */
        /* Deliberately NOT calling jc_read_file(fifo): with no writer it would
         * block forever, which is precisely the defect. The predicate is the
         * contract; tests/smoke/degenerate_store.sh proves the scan honours it. */
        remove(fifo);
    }

    /* jc_is_dir stays the complement for directories. */
    JC_CHECK(jc_is_dir(sub) == 1);
    JC_CHECK(jc_is_dir(reg) == 0);

    /* --- jc_read_file: a directory is an ERROR, not an empty file ---------- */
    /* fopen("rb") on a directory SUCCEEDS on Linux; ftell reports a size and
     * fread fails, so this used to return JC_OK with "" -- a silent wrong
     * answer. Rejecting a directory cannot affect a pipe, so unlike S_ISREG it
     * belongs at this chokepoint. */
    text = NULL;
    len = 0;
    JC_CHECK(jc_read_file(sub, &text, &len, a) == JC_ERR_IO);

    /* A regular file still reads normally. */
    text = NULL;
    JC_CHECK(jc_read_file(reg, &text, &len, a) == JC_OK);
    JC_CHECK(text != NULL && strcmp(text, "hello\n") == 0);
    JC_CHECK(len == 6);

    /* A missing path is NOTFOUND, distinct from the directory's IO. */
    JC_CHECK(jc_read_file("/nonexistent/x", &text, &len, a) == JC_ERR_NOTFOUND);

    /* --- M1138 (D-05): procfs files read their CONTENT, not empty-with-OK -- */
    /* On procfs, fseek(SEEK_END) returns 0 and ftell reports 0 for a file with
     * real bytes, so jc_read_file used to allocate 1 byte, read 0, and return
     * JC_OK with an EMPTY buffer -- /proc/cpuinfo read back as len=0. The fix
     * takes the size==0 window with a 1-byte PROBE (stat cannot discriminate:
     * a procfs pseudo-file is S_ISREG with st_size 0, identical to a genuine
     * empty regular file, which is why the design note's stat gate shipped as
     * the probe instead): probe 0 reads JC_OK/len 0 exactly as before; probe 1
     * re-seeks and reads in a bounded fread loop to EOF.
     *
     * (4) No-regression control: a real 0-byte file is STILL JC_OK, len == 0.
     * GREEN both before and after the fix. */
    {
        char empty[320];
        jc_snprintf(empty, sizeof empty, "%s/empty.txt", dir);
        JC_CHECK(jc_write_file(empty, "", 0) == JC_OK);
        text = NULL;
        len = 0;
        JC_CHECK(jc_read_file(empty, &text, &len, a) == JC_OK);
        JC_CHECK(text != NULL && text[0] == '\0');
        JC_CHECK(len == 0);
    }

    /* (1)-(3) The procfs teeth, run only where /proc is openable (skip on a
     * host without it). /proc/self/status and /proc/cpuinfo are readable to
     * self regardless of the M770 PR_SET_DUMPABLE state, so they are the
     * correct teeth. We NEVER read /proc/self/environ: in a hardened build it
     * is JC_ERR_NOTFOUND and that is CORRECT (the key stays hidden) -- testing
     * it would "fix" a leak. */
    {
        struct stat pst;
        if (stat("/proc/self/status", &pst) == 0) {
            text = NULL;
            len = 0;
            JC_CHECK(jc_read_file("/proc/self/status", &text, &len, a) == JC_OK);
            JC_CHECK(len > 0);
            JC_CHECK(text != NULL && strstr(text, "Name:") != NULL);

            text = NULL;
            len = 0;
            JC_CHECK(jc_read_file("/proc/cpuinfo", &text, &len, a) == JC_OK);
            JC_CHECK(len > 0);

            text = NULL;
            len = 0;
            JC_CHECK(jc_read_file_bounded("/proc/self/status", &text, &len,
                                          a, 4096, 1) == JC_OK);
            JC_CHECK(len > 0);
            JC_CHECK(len <= 4096);
        }
    }

    /* --- jc_read_existing (D-16/M1100): the "proceed only on a genuine
     * does-not-exist" tri-state ------------------------------------------ */
    /* A small file reads: JC_OK + content (same as jc_read_file). */
    text = NULL;
    len = 0;
    JC_CHECK(jc_read_existing(reg, &text, &len, a) == JC_OK);
    JC_CHECK(text != NULL && strcmp(text, "hello\n") == 0);
    JC_CHECK(len == 6);

    /* An ABSENT path: JC_ERR_NOTFOUND -- the only status a caller may
    treat as "no file". (jc_read_file already reports NOTFOUND here too, so
    the split is not visible on this path; it becomes visible on the
    PRESENT-but-unreadable path below.) */
    text = NULL;
    len = 0;
    JC_CHECK(jc_read_existing("/nonexistent/x", &text, &len, a) == JC_ERR_NOTFOUND);

    /* PRESENT-but-unreadable: a file OVER JC_READ_FILE_MAX reports the READ
     * error UNCHANGED -- JC_ERR_TOOBIG stays TOOBIG -- and *out is untouched,
     * NOT collapsed to NOTFOUND the way a bare jc_read_file failure would
     * tempt a caller to do. This is the D-16 root: the old jc_memory_add
     * mapped every non-OK read to "missing file" and replaced the 67109888-
     * byte file with the one new note. Writing the 64 MiB + 1 byte fixture
     * is cheap on a local fs; it lives in the test's own mkdtemp dir (removed
     * below) and never in the repo tree. */
    {
        char big[360];
        FILE *bf;
        long i;
        jc_status st;
        jc_snprintf(big, sizeof big, "%s/over_cap.bin", dir);
        bf = fopen(big, "wb");
        if (bf != NULL) {
            /* JC_READ_FILE_MAX is 64 MiB; write 64 MiB + 1 byte in 64KiB
             * blocks (one byte at a time would be slow). */
            char block[65536];
            memset(block, 'x', sizeof block);
            for (i = 0; i < 1024; i++) { /* 1024 x 64KiB = 64 MiB */
                if (fwrite(block, 1, sizeof block, bf) != sizeof block) {
                    break;
                }
            }
            fputc('x', bf); /* +1 byte -> over the cap */
            fclose(bf);
        }
        /* Confirm the fixture is really over the cap (a truncated write would
         * make the TOOBIG check vacuous). */
        JC_CHECK(jc_file_size(big) == (long)JC_READ_FILE_MAX + 1L);
        text = NULL;
        len = 0;
        st = jc_read_existing(big, &text, &len, a);
        JC_CHECK(st == JC_ERR_TOOBIG); /* the READ error, passed through */
        JC_CHECK(text == NULL);        /* *out untouched: no content produced */
        JC_CHECK(len == 0);            /* *len untouched */
        (void)remove(big);
    }

    /* --- jc_read_existing (D-12 write-through / M1100-b, review nit 1): a
     * symlink at the leaf is PRESENT, never NOTFOUND, and never read through
     * ---------------------------------------------------------------------- */
    /* The helper LSTATS the leaf. A symlink -- live or broken -- must report
     * JC_ERR_IO, so a writing caller can never be handed JC_ERR_NOTFOUND for a
     * path that is actually a link and then fopen("wb") through it and create
     * the TARGET on the other side of the link (the D-12 write-through class,
     * re-disguised). On the stat() helper (161ceb10) this is exactly what
     * happened: a link to an ABSENT target followed the link and reported
     * ENOENT -> JC_ERR_NOTFOUND, and a link to a LIVE file read the target's
     * content -- teeth (1) and (2) below are RED against that, green once the
     * helper lstats. */
    {
        char link[320];
        char target[320];
        FILE *tf;
        jc_status st;

        jc_snprintf(link, sizeof link, "%s/link_absent", dir);
        jc_snprintf(target, sizeof target, "%s/sym_target", dir);

        /* (1) A symlink whose target is ABSENT: lstat sees the link itself, so
         * this is JC_ERR_IO -- NOT JC_ERR_NOTFOUND. A writer that trusts the
         * tri-state alone would create the target through the link (D-12). */
        (void)remove(link);
        JC_CHECK(symlink("no-such-target", link) == 0);
        text = NULL;
        len = 999999; /* poison: only the helper's own *len reset can zero it */
        st = jc_read_existing(link, &text, &len, a);
        JC_CHECK(st == JC_ERR_IO); /* the stat() helper gave NOTFOUND here */
        JC_CHECK(text == NULL);
        JC_CHECK(len == 0);
        (void)remove(link);

        /* (2) A symlink to a LIVE regular file: never read through. The helper
         * lstats the link and refuses (JC_ERR_IO); it must NOT return the
         * target's content. (The pre-fix stat() helper followed the link and
         * handed back the 12 bytes below.) */
        tf = fopen(target, "wb");
        JC_CHECK(tf != NULL);
        if (tf != NULL) {
            (void)fwrite("target-data\n", 1, 12, tf);
            (void)fclose(tf);
        }
        JC_CHECK(symlink(target, link) == 0);
        text = NULL;
        len = 0;
        st = jc_read_existing(link, &text, &len, a);
        JC_CHECK(st == JC_ERR_IO); /* never read through the link */
        JC_CHECK(text == NULL);
        JC_CHECK(len == 0);
        (void)remove(link);

        /* (3) Reading the target DIRECTLY still works: proves the refusal is
         * about the LINK, not the content. JC_OK, the 12 bytes, NUL-terminated. */
        text = NULL;
        len = 0;
        st = jc_read_existing(target, &text, &len, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(text != NULL && strcmp(text, "target-data\n") == 0);
        JC_CHECK(len == 12);
        (void)remove(target);
    }

    /* --- jc_write_file_preserve_link (M1104-c): a rewrite of jichi's own
     * workspace file KEEPS a hard link (M141 / vim backupcopy=auto) -------- */
    /* The four workspace rewrites (memory.md, AGENTS.md/CLAUDE.md) route
     * here. The property under test: a target that is a regular file with
     * st_nlink > 1 is written IN PLACE (same inode, both names see the new
     * bytes); everything else (single-link, absent, symlink) takes the M1104
     * mode-preserving atomic replace. The two-link case is the M1104 red: a
     * temp+rename would replace the inode and orphan the second name. */
    {
        char single[320];
        char linkA[320];
        char linkB[320];
        char absent[320];
        char sym[320];
        char sym_out[320];
        struct stat sa;
        struct stat sb;
        struct stat ss;
        jc_status st;
        jc_size n;

        /* (a) A SINGLE-LINK regular file: preserve_link goes ATOMIC (rename),
         * so the inode CHANGES but the content is updated and the mode is
         * preserved. This pins the "no over-refusal / atomic path still runs"
         * half -- the driver's check 3. */
        jc_snprintf(single, sizeof single, "%s/single.txt", dir);
        st = jc_write_file_preserve_link(single, "first\n", 6);
        JC_CHECK(st == JC_OK);
        JC_CHECK(chmod(single, 0640) == 0);
        JC_CHECK(stat(single, &sa) == 0);
        n = 0;
        st = jc_read_file(single, &text, &n, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(jc_write_file_preserve_link(single, "second\n", 7) == JC_OK);
        JC_CHECK(stat(single, &sb) == 0);
        JC_CHECK(sb.st_ino != sa.st_ino);           /* atomic: new inode */
        JC_CHECK((sb.st_mode & 0777) == 0640);      /* ...but mode preserved */
        n = 0;
        st = jc_read_file(single, &text, &n, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(text != NULL && strcmp(text, "second\n") == 0);

        /* (b) A TWO-LINK file: written IN PLACE. Both names see the new bytes,
         * SAME inode, link count still 2, original mode. This is the M1104
         * red -- atomic rename would orphan linkB (different inode). */
        jc_snprintf(linkA, sizeof linkA, "%s/linkA.txt", dir);
        jc_snprintf(linkB, sizeof linkB, "%s/linkB.txt", dir);
        st = jc_write_file_preserve_link(linkA, "old\n", 4);
        JC_CHECK(st == JC_OK);
        JC_CHECK(chmod(linkA, 0644) == 0);
        JC_CHECK(link(linkA, linkB) == 0);
        JC_CHECK(stat(linkA, &sa) == 0);
        JC_CHECK(sa.st_nlink == 2);
        st = jc_write_file_preserve_link(linkA, "new-two\n", 8);
        JC_CHECK(st == JC_OK);
        JC_CHECK(stat(linkA, &sa) == 0);
        JC_CHECK(stat(linkB, &sb) == 0);
        JC_CHECK(sa.st_ino == sb.st_ino);           /* IN PLACE: same inode */
        JC_CHECK(sa.st_nlink == 2);                  /* ...link count intact */
        JC_CHECK((sa.st_mode & 0777) == 0644);       /* ...mode intact */
        n = 0;
        st = jc_read_file(linkA, &text, &n, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(text != NULL && strcmp(text, "new-two\n") == 0);
        n = 0;
        st = jc_read_file(linkB, &text, &n, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(text != NULL && strcmp(text, "new-two\n") == 0); /* both see it */

        /* (c) An ABSENT path: created, content right, mode 0644 (the lstat
         * failure honest default, not the atomic 0600). */
        jc_snprintf(absent, sizeof absent, "%s/absent.txt", dir);
        st = jc_write_file_preserve_link(absent, "fresh\n", 6);
        JC_CHECK(st == JC_OK);
        JC_CHECK(stat(absent, &ss) == 0);
        JC_CHECK(ss.st_nlink == 1);
        JC_CHECK((ss.st_mode & 0777) == 0644);
        n = 0;
        st = jc_read_file(absent, &text, &n, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(text != NULL && strcmp(text, "fresh\n") == 0);

        /* (d) A SYMLINK path: must NOT write THROUGH the link. preserve_link
         * lstats the link (S_ISREG is false for a link), so it delegates to the
         * atomic replace, whose rename REPLACES the link with a regular file
         * and never touches the outside target (the D-12 write-through class).
         * The outside target is left byte-unchanged; the path is now a regular
         * single-link file carrying the new content. */
        jc_snprintf(sym, sizeof sym, "%s/sym.txt", dir);
        jc_snprintf(sym_out, sizeof sym_out, "%s/sym_outside.txt", dir);
        st = jc_write_file_preserve_link(sym_out, "outside\n", 8);
        JC_CHECK(st == JC_OK);
        JC_CHECK(symlink(sym_out, sym) == 0);
        n = 0;
        st = jc_read_file(sym_out, &text, &n, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(text != NULL && strcmp(text, "outside\n") == 0);
        st = jc_write_file_preserve_link(sym, "through\n", 8);
        JC_CHECK(st == JC_OK);
        JC_CHECK(lstat(sym, &ss) == 0);
        JC_CHECK(S_ISREG(ss.st_mode));              /* link replaced by regular */
        JC_CHECK(ss.st_nlink == 1);
        n = 0;
        st = jc_read_file(sym, &text, &n, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(text != NULL && strcmp(text, "through\n") == 0);
        /* The outside target is UNTOUCHED -- the write did not follow the link. */
        n = 0;
        st = jc_read_file(sym_out, &text, &n, a);
        JC_CHECK(st == JC_OK);
        JC_CHECK(text != NULL && strcmp(text, "outside\n") == 0);
    }

    (void)jc_test_rm_rf(dir);
#ifdef JC_FAULT
    /* C-16 (M1134): the HOME-unset /tmp fallback, exercised with the
     * JICHI_FAULT_HOME_NOPASSWD probe (every bench has a passwd entry for its
     * own uid, so the branch is otherwise unreachable without root). Only
     * reachable with the injector compiled in (make FAULT=1); the
     * smoke-faults tier runs this suite. */
    test_home_dir_pid_fallback();
    /* D-10 (M1140): jc_list_dir's errno check on a mid-listing readdir failure,
     * exercised with the JICHI_FAULT_LISTDIR probe (a host never fails readdir
     * honestly, so the branch is otherwise unreachable). Same FAULT=1-only
     * reachability as the C-16 probe above. */
    test_list_dir_readdir_error();
#endif
    jc_arena_free(a);
}

#ifdef JC_FAULT
/* C-16 (M1134), red-first. The finding: jc_home_dir's pid-scoped HOME
 * fallback (/tmp/jichi-<uid>-<pid>) was mkdir'd and USED without the
 * lstat/owner/mode check its uid-scoped sibling got -- on the false
 * assumption "a pid-scoped name cannot have been waiting for us". Pids are
 * predictable: a hostile local user can pre-create them (or a range of them)
 * at 0777, and jichi would root its key file and sessions in another user's
 * directory.
 *
 * THE RED LINE is leg (a) below: with both candidates pre-planted at 0777 the
 * unfixed tree returns the planted pid path unchecked, so
 *   JC_CHECK(strcmp(got, pid_dir) != 0)
 * fails (prints "FAIL ... strcmp(got, pid_dir) != 0"). The fixed tree
 * refuses it and returns a fresh 0700 mkdtemp directory instead.
 *
 * The mode 0777 plant (same uid) is the mode-check half of the finding and is
 * reachable without root; the owner half is asserted on the RETURNED directory
 * (leg b) and would need a uid-0 plant, which the smoke driver covers via the
 * real binary. */
static void test_home_dir_pid_fallback(void)
{
    char uid_dir[320];
    char pid_dir[320];
    char got[1024];
    char mkdtemp_root[1024];
    char home_saved[2048];
    struct stat st;
    mode_t uid_mode;
    mode_t pid_mode;
    int had_home;
    int uid_preexisted;
    int pid_preexisted;
    const char *r;

    had_home = (getenv("HOME") != NULL);
    if (had_home) {
        jc_snprintf(home_saved, sizeof home_saved, "%s", getenv("HOME"));
    }
    setenv("JICHI_FAULT_HOME_NOPASSWD", "1", 1);
    unsetenv("HOME");

    jc_snprintf(uid_dir, sizeof uid_dir, "/tmp/jichi-%lu",
                (unsigned long)getuid());
    jc_snprintf(pid_dir, sizeof pid_dir, "/tmp/jichi-%lu-%lu",
                (unsigned long)getuid(), (unsigned long)getpid());

    /* Record whatever pre-exists so cleanup restores, never destroys -- and
     * decide what is SAFE to plant. The smoke driver (tests/smoke/
     * state_root_pid_fallback.sh) carries this same guard: "it never destroys
     * a pre-existing directory". For each candidate:
     *   absent             -> preexisted = 0; this test creates and removes it.
     *   an OWN directory   -> preexisted = 1, its full mode recorded; leg (a)
     *                          chmods it 0777 (exactly the plant, as the
     *                          driver does) and cleanup restores the mode.
     *   anything else      -> a file, a symlink, or another account's
     *                          directory: RETURN with ZERO checks performed --
     *                          the unit-test equivalent of the driver's
     *                          t_skip "pre-existing ... not owned by ...;
     *                          unsafe to plant". On a host where the dir
     *                          pre-existed (e.g. a machine that has run jichi
     *                          once with HOME unset and no passwd entry),
     *                          without this guard leg (a) chmods a REAL state
     *                          root to 0777 mid-test, and legs (c)/(d) would
     *                          false-red on a fixed tree. */
    if (lstat(uid_dir, &st) == 0) {
        if (S_ISDIR(st.st_mode) && st.st_uid == getuid()) {
            uid_preexisted = 1;
            uid_mode = st.st_mode & (mode_t)07777;
        } else {
            /* Not safe to plant: skip before mutating anything, leaving the
             * pre-existing entry exactly as found (and the probe state
             * restored, since this path bypasses the cleanup tail). */
            unsetenv("JICHI_FAULT_HOME_NOPASSWD");
            if (had_home) {
                setenv("HOME", home_saved, 1);
            }
            return;
        }
    } else {
        uid_preexisted = 0;
    }
    if (lstat(pid_dir, &st) == 0) {
        if (S_ISDIR(st.st_mode) && st.st_uid == getuid()) {
            pid_preexisted = 1;
            pid_mode = st.st_mode & (mode_t)07777;
        } else {
            unsetenv("JICHI_FAULT_HOME_NOPASSWD");
            if (had_home) {
                setenv("HOME", home_saved, 1);
            }
            return;
        }
    } else {
        pid_preexisted = 0;
    }

    /* (a) THE RED LINE: both candidates planted 0777 (a pre-plant a local
     * attacker would make; mode half of the finding, same uid). jc_home_dir
     * must NOT return either planted path. The static result buffer is
     * copied to `got` BEFORE any later call can overwrite it. */
    (void)mkdir(uid_dir, 0777);
    (void)chmod(uid_dir, 0777);
    (void)mkdir(pid_dir, 0777);
    (void)chmod(pid_dir, 0777);
    r = jc_home_dir();
    JC_CHECK(r != NULL);
    jc_snprintf(got, sizeof got, "%s", r);
    /* Capture the mkdtemp dir BEFORE (c)/(d) overwrite `got`: it is jichi's
     * own fresh empty 0700 dir, and cleanup removes it by this name (the
     * original test leaked it on a clean-slate host). */
    jc_snprintf(mkdtemp_root, sizeof mkdtemp_root, "%s", got);
    JC_CHECK(strcmp(got, pid_dir) != 0); /* RED on the unfixed tree: it returns
                                             the pre-planted pid path */
    JC_CHECK(strcmp(got, uid_dir) != 0);
    JC_CHECK(strncmp(got, "/tmp/jichi-", 11) == 0); /* the mkdtemp dir */

    /* (b) whatever it returned, it must pass the check the pid leg was
     * missing: a directory, ours, with no group/other bits. */
    JC_CHECK(lstat(got, &st) == 0);
    JC_CHECK(S_ISDIR(st.st_mode));
    JC_CHECK(st.st_uid == getuid());
    JC_CHECK((st.st_mode & (mode_t)077) == 0);

    /* (c) regression: nothing planted -> the uid-scoped leg works as before
     * (the fix must not break the leg that was already correct).
     *
     * The premise is "nothing planted", and it is MADE TRUE by the gate
     * below: when uid_dir pre-existed, leg (a) left it at 0777 (it cannot be
     * rmdir'd -- it is a real directory this test does not own), so jc_home_dir
     * correctly routes to the pid path and the two assertions would measure
     * the host's premise, not a regression. (c) and (d) therefore run only on
     * a clean slate; the red line (a) and the rule (b) run either way. */
    if (!uid_preexisted) {
        /* Unplant what leg (a) created: uid_dir and pid_dir do not
         * pre-exist here, so both are this test's own empty 0777 dirs and
         * are removed (a pre-existed own dir is left in place, its mode
         * restored at the end -- and the gate above keeps these assertions
         * off it). Then the uid-scoped leg: jc_home_dir re-mkdirs uid_dir
         * fresh at 0700. */
        (void)rmdir(uid_dir);
        if (!pid_preexisted) {
            (void)rmdir(pid_dir);
        }
        r = jc_home_dir();
        JC_CHECK(r != NULL);
        jc_snprintf(got, sizeof got, "%s", r);
        JC_CHECK(strcmp(got, uid_dir) == 0);

        /* (d) the pid branch is not reached when the uid leg passes: uid dir
         * 0700 (jichi's own mkdir from (c) left it so -- re-assert) and the pid
         * path planted 0777. */
        JC_CHECK(chmod(uid_dir, 0700) == 0);
        (void)mkdir(pid_dir, 0777);
        (void)chmod(pid_dir, 0777);
        r = jc_home_dir();
        JC_CHECK(r != NULL);
        jc_snprintf(got, sizeof got, "%s", r);
        JC_CHECK(strcmp(got, uid_dir) == 0);
    }

    /* Cleanup: remove only what this test created; restore modes of anything
     * that pre-existed; disarm the probe; restore HOME. The (a) mkdtemp dir
     * (captured in mkdtemp_root) and the (c)/(d) uid dir are both jichi's own
     * empty 0700 dirs, removed by name; a pre-existed dir is ours and is left
     * in place, its recorded mode restored. */
    if (lstat(mkdtemp_root, &st) == 0 && S_ISDIR(st.st_mode)
            && st.st_uid == getuid()) {
        (void)rmdir(mkdtemp_root);
    }
    if (!pid_preexisted) {
        (void)rmdir(pid_dir);
    } else {
        (void)chmod(pid_dir, pid_mode);
    }
    if (!uid_preexisted) {
        (void)rmdir(uid_dir);
    } else {
        (void)chmod(uid_dir, uid_mode);
    }
    unsetenv("JICHI_FAULT_HOME_NOPASSWD");
    if (had_home) {
        setenv("HOME", home_saved, 1);
    }
}

/* D-10 (M1140), red-first. The finding: jc_list_dir looped
 * `while ((ent = readdir(d)) != NULL)` and returned JC_OK after closedir, so a
 * MID-LISTING readdir FAILURE -- a NULL return with errno set -- was
 * indistinguishable from a clean end-of-directory and produced a short listing
 * with success. That is the silent-truncation half of D-10: on a 32-bit build
 * without 64-bit offsets, readdir can fail mid-listing on a 64-bit-inode
 * filesystem, and the caller (a session scan, a rules walk, ...) would read a
 * partial directory and act as if it were complete.
 *
 * A host never fails readdir honestly, so the tooth is the JICHI_FAULT_LISTDIR
 * probe (M198's deterministic class, same FAULT=1-only reachability as the C-16
 * probe above): armed, the readdir past the threshold faults -- the loop sets
 * errno=EIO and exits, and the fixed jc_list_dir must return JC_ERR_IO.
 *
 * THE RED LINE is the `st == JC_ERR_IO` check below: with the probe armed and
 * the errno check REVERTED, jc_list_dir returns JC_OK (the short listing is
 * presented as success), so
 *   JC_CHECK(st == JC_ERR_IO)
 * fails (prints "FAIL ... st == JC_ERR_IO"). The fixed tree returns JC_ERR_IO
 * and drops the short name set. */
static void test_list_dir_readdir_error(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_vec names;
    char dir[256];
    char f1[320], f2[320], f3[320];
    jc_status st;

    jc_vec_init(&names, sizeof(char *));

    if (!JC_REQUIRE(jc_snprintf(dir, sizeof dir, "%s/jichi_listdir_%ld",
                                jc_test_tmpdir(), (long)getpid())
                    < (int)sizeof dir)) {
        jc_vec_free(&names);
        jc_arena_free(a);
        return; /* a cut fixture path is a different path (M728) */
    }
    (void)jc_test_rm_rf(dir);
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);
    jc_snprintf(f1, sizeof f1, "%s/a.txt", dir);
    jc_snprintf(f2, sizeof f2, "%s/b.txt", dir);
    jc_snprintf(f3, sizeof f3, "%s/c.txt", dir);
    JC_CHECK(jc_write_file(f1, "one\n", 4) == JC_OK);
    JC_CHECK(jc_write_file(f2, "two\n", 4) == JC_OK);
    JC_CHECK(jc_write_file(f3, "three\n", 6) == JC_OK);

    /* ARMED leg: the probe faults the 2nd readdir, so the listing is CUT
     * mid-stream. jc_fault_reset() zeroes the site's counter and forces
     * load() to re-read the env, so the arm below is what takes effect. */
    jc_fault_reset();
    setenv("JICHI_FAULT_LISTDIR_AFTER", "1", 1);
    st = jc_list_dir(dir, &names, a);
    unsetenv("JICHI_FAULT_LISTDIR_AFTER");

    /* THE RED LINE: a failed readdir must surface as JC_ERR_IO, not JC_OK. */
    JC_CHECK(st == JC_ERR_IO);
    /* ...and the names it DID deliver are a SHORT listing -- fewer than the 3
     * files. On this host the probe fires on the 2nd entry (after "."), and
     * neither "." nor ".." is captured, so the short vector holds 0 names. */
    JC_CHECK(names.len < 3);

    /* HEALTHY leg: unarm, reset, and the full listing comes back with JC_OK. */
    jc_vec_clear(&names);
    jc_fault_reset();
    st = jc_list_dir(dir, &names, a);
    JC_CHECK(st == JC_OK);
    JC_CHECK(names.len == 3);

    (void)jc_test_rm_rf(dir);
    jc_vec_free(&names);
    jc_arena_free(a);
}
#endif /* JC_FAULT */

/* M805 (P4a): jc_disk_space measures free space + inodes as a signal that a
 * later guard (P4b) can threshold. The one property a guard depends on -- and
 * the one jc_disk_free_mb cannot provide -- is that "could not measure" is
 * DISTINCT from "measured zero free". These branches assert exactly that
 * distinction, plus that the sentinel is written on every failure path so a
 * caller that forgets to check the status still cannot read a stale value as a
 * real one. (The double-vs-integer fragment-size fix cannot be exercised
 * without a filesystem whose fragment size is below 1024, e.g. ZFS at 512; the
 * rig rows on ZFS are where that is verified live -- see the memory note.) */
void test_disk_space(void)
{
    double mb, files;

    /* A path that exists (the current directory) is measurable: OK, and a real
     * filesystem reports a non-negative amount free. */
    mb = -2.0;
    files = -2.0;
    JC_CHECK(jc_disk_space(".", &mb, &files) == JC_OK);
    JC_CHECK(mb >= 0.0);
    JC_CHECK(files >= 0.0);

    /* A path that cannot be stat'd is UNKNOWN, not full: the status says so AND
     * both out-values are left at the -1.0 sentinel, which no filesystem yields.
     * A guard that (wrongly) ignored the status would still not see a "0 free". */
    mb = 7.0;
    files = 7.0;
    JC_CHECK(jc_disk_space("/nonexistent/deep/path/xyz", &mb, &files)
             == JC_ERR_IO);
    JC_CHECK(mb == -1.0);
    JC_CHECK(files == -1.0);

    /* A NULL path is the same unknown, never a crash. */
    mb = 7.0;
    JC_CHECK(jc_disk_space(NULL, &mb, NULL) == JC_ERR_IO);
    JC_CHECK(mb == -1.0);

    /* Both out-pointers NULL: the caller only wants the status. No write, no
     * crash; the measurement still succeeds for a real path. */
    JC_CHECK(jc_disk_space(".", NULL, NULL) == JC_OK);
}

/* M528: privacy verified as an EFFECT. The daemon socket's access control is its
 * file mode and nothing else, and the code that sets it says so -- while also
 * naming the case it cannot verify: "there are platforms that ignore the umask
 * for sockets". On such a platform the kernel creates a 0755 socket, every local
 * user gets a shell, and no code path notices. These are the branches of the
 * check that notices, pure so that a platform which misbehaves does not have to
 * be present to test them. */
/* M839: a local time is believed only at an offset a real time zone could have.
 * The offsets below are the ones measured on FreeDOS, where DJGPP's C library
 * returned garbage for a TZ that was unset or had no offset. */
static struct tm mk_tm(int year, int mon, int mday, int yday, int h, int mi, int s)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = mday;
    t.tm_yday = yday;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    return t;
}

void test_localtime_checked(void)
{
    struct tm u = mk_tm(2026, 8, 10, 221, 0, 0, 0);   /* 2026-08-10 00:00 UTC */
    struct tm l;
    const char *tz0 = getenv("TZ");
    char *saved = (tz0 != NULL) ? jc_strdup(tz0) : NULL;

    l = u;
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 1);                 /* UTC itself */
    l = mk_tm(2026, 8, 10, 221, 2, 0, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 1);                 /* +02:00 */
    l = mk_tm(2026, 8, 10, 221, 5, 45, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 1);                 /* +05:45 */
    l = mk_tm(2026, 8, 9, 220, 12, 0, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 1);                 /* -12:00 */
    l = mk_tm(2026, 8, 10, 221, 14, 0, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 1);                 /* +14:00 */
    l = mk_tm(2026, 8, 11, 222, 2, 15, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 0);                 /* +26:15 */
    l = mk_tm(2026, 8, 2, 213, 5, 8, 39);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 0);                 /* DJGPP, TZ unset */
    l = mk_tm(2026, 7, 4, 184, 0, 0, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 0);                 /* DJGPP, TZ=UTC */
    l = mk_tm(2026, 8, 10, 221, 0, 30, 7);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 0);                 /* not a 15-min step */

    /* Across New Year, where tm_yday alone would say 364 days apart. */
    u = mk_tm(2026, 12, 31, 364, 23, 0, 0);
    l = mk_tm(2027, 1, 1, 0, 1, 0, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 1);                 /* +02:00 */
    l = mk_tm(2027, 1, 2, 1, 1, 0, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 0);                 /* a day too far */
    u = mk_tm(2027, 1, 1, 0, 1, 0, 0);
    l = mk_tm(2026, 12, 31, 364, 20, 0, 0);
    JC_CHECK(jc_tm_offset_plausible(&l, &u) == 1);                 /* -05:00 */
    JC_CHECK(jc_tm_offset_plausible(NULL, &u) == 0);
    JC_CHECK(jc_tm_offset_plausible(&l, NULL) == 0);

    /* The checked call: where the C library honours TZ=UTC0, local time is UTC
     * and is believed. */
    setenv("TZ", "UTC0", 1);
    tzset();
    JC_CHECK(jc_localtime_checked(1786320000L, &l) == 1);
    JC_CHECK(l.tm_year == 126 && l.tm_mon == 7 && l.tm_mday == 10 && l.tm_hour == 0);
    JC_CHECK(jc_localtime_checked(1786320000L, NULL) == -1);
    /* ...and the start-up repair leaves a usable zone exactly as it found it. */
    JC_CHECK(jc_tz_repair() == 0);
    JC_CHECK(getenv("TZ") != NULL && strcmp(getenv("TZ"), "UTC0") == 0);
    if (saved != NULL) {
        setenv("TZ", saved, 1);
        free(saved);
    } else {
        unsetenv("TZ");
    }
    tzset();
}

void test_priv_verdict(void)
{
    /* 0600, ours: the only acceptable answer. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100600UL, 1000UL) == JC_PRIV_OK);
    /* A directory at 0700 is equally fine -- the check is on the low 9 bits. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0040700UL, 1000UL) == JC_PRIV_OK);
    /* Owner-only but with execute set is still owner-only. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100700UL, 1000UL) == JC_PRIV_OK);

    /* Any group or other bit is too open -- one bit is enough. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100640UL, 1000UL) == JC_PRIV_TOO_OPEN);
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100604UL, 1000UL) == JC_PRIV_TOO_OPEN);
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100601UL, 1000UL) == JC_PRIV_TOO_OPEN);
    /* The umask-ignored-on-bind case this exists for: 0755. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0140755UL, 1000UL) == JC_PRIV_TOO_OPEN);
    /* And the worst one, which a chmod's return value would not have caught. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0140777UL, 1000UL) == JC_PRIV_TOO_OPEN);

    /* Not ours outranks the mode: a 0600 file owned by someone else is THEIR
     * private file, and connecting to it is not a privacy question but an
     * identity one. Reported separately so the diagnostic can say which. */
    JC_CHECK(jc_priv_verdict_of(0UL, 0100600UL, 1000UL) == JC_PRIV_NOT_OWNER);
    JC_CHECK(jc_priv_verdict_of(1001UL, 0100777UL, 1000UL) == JC_PRIV_NOT_OWNER);
    /* Running as root, root's own 0600 socket is fine. */
    JC_CHECK(jc_priv_verdict_of(0UL, 0100600UL, 0UL) == JC_PRIV_OK);

    /* Every verdict has a human string, including the failure ones. */
    JC_CHECK(jc_priv_verdict_str(JC_PRIV_OK) != NULL);
    JC_CHECK(jc_priv_verdict_str(JC_PRIV_TOO_OPEN) != NULL);
    JC_CHECK(jc_priv_verdict_str(JC_PRIV_NOT_OWNER) != NULL);
    JC_CHECK(jc_priv_verdict_str(JC_PRIV_NO_STAT) != NULL);
    JC_CHECK(strstr(jc_priv_verdict_str(JC_PRIV_TOO_OPEN), "0600") != NULL);
}

/* And the directory that holds the endpoint: the mode question there is not
 * "can others read it" but "can others REPLACE what we put in it", which the
 * sticky bit answers for /tmp. */
void test_dir_holds_private(void)
{
    JC_CHECK(jc_dir_holds_private(0040700UL) == 1);   /* private dir      */
    JC_CHECK(jc_dir_holds_private(0040755UL) == 1);   /* readable, not writable */
    JC_CHECK(jc_dir_holds_private(0041777UL) == 1);   /* /tmp: sticky     */
    JC_CHECK(jc_dir_holds_private(0041733UL) == 1);   /* sticky, still ok */
    JC_CHECK(jc_dir_holds_private(0040777UL) == 0);   /* world-writable, NOT sticky */
    JC_CHECK(jc_dir_holds_private(0040770UL) == 0);   /* group-writable, not sticky */
    JC_CHECK(jc_dir_holds_private(0040707UL) == 0);   /* other-writable */
}
