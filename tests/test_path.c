/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_path.c - workspace path containment + canonicalization (M24). */

#include "jc_test.h"
#include "jc_path.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static void test_under_root(void)
{
    /* Exact match and children are contained. */
    JC_CHECK(jc_path_under_root("/work", "/work") == 1);
    JC_CHECK(jc_path_under_root("/work", "/work/sub/file.c") == 1);
    JC_CHECK(jc_path_under_root("/work/", "/work/sub") == 1); /* trailing slash */

    /* Sibling-prefix is NOT contained ("/work" must not match "/workother"). */
    JC_CHECK(jc_path_under_root("/work", "/workother") == 0);
    JC_CHECK(jc_path_under_root("/work", "/wor") == 0);
    JC_CHECK(jc_path_under_root("/work", "/") == 0);

    /* Root "/" contains every absolute path. */
    JC_CHECK(jc_path_under_root("/", "/anything/here") == 1);

    /* Empty / NULL root denies everything (fail closed). */
    JC_CHECK(jc_path_under_root("", "/x") == 0);
    JC_CHECK(jc_path_under_root(NULL, "/x") == 0);
    JC_CHECK(jc_path_under_root("/work", NULL) == 0);
}

static void test_resolve(void)
{
    char out[JC_PATH_MAX];
    char cwd[JC_PATH_MAX];

    /* "." resolves to an absolute path (the cwd). */
    JC_CHECK(jc_path_resolve(".", out, sizeof(out)) == JC_OK);
    JC_CHECK(out[0] == '/');

    /* A not-yet-existing leaf still resolves (parent canonicalized). */
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        char target[JC_PATH_MAX];
        /* Build "<cwd>/no_such_file_xyz" without sprintf. */
        {
            size_t cl = strlen(cwd);
            const char *leaf = "/no_such_file_xyz_123";
            if (cl + strlen(leaf) + 1 < sizeof(target)) {
                memcpy(target, cwd, cl);
                memcpy(target + cl, leaf, strlen(leaf) + 1);
                JC_CHECK(jc_path_resolve(target, out, sizeof(out)) == JC_OK);
                JC_CHECK(jc_path_under_root(cwd, out) == 1);
            }
        }

        /* M638: a target whose PARENT does not exist yet resolves too -- the
         * deepest existing ancestor is canonicalized and the missing tail
         * re-appended. Until M638 this returned JC_ERR_NOTFOUND, the fence
         * failed closed, and write_file into a fresh directory was refused
         * as "path outside workspace" (the footer-in-anger note: the model
         * then built a Python file by fifty shell appends). */
        {
            size_t cl = strlen(cwd);
            const char *bad_parent = "/no_such_dir_abc_123/file.txt";
            if (cl + strlen(bad_parent) + 1 < sizeof(target)) {
                memcpy(target, cwd, cl);
                memcpy(target + cl, bad_parent, strlen(bad_parent) + 1);
                JC_CHECK(jc_path_resolve(target, out, sizeof(out)) == JC_OK);
                JC_CHECK(jc_path_under_root(cwd, out) == 1);
                JC_CHECK(strstr(out, "/no_such_dir_abc_123/file.txt") != NULL);
            }
        }
        /* Three missing levels, the same way. */
        {
            size_t cl = strlen(cwd);
            const char *deep = "/no_such_a_123/no_such_b/no_such_c/f.txt";
            if (cl + strlen(deep) + 1 < sizeof(target)) {
                memcpy(target, cwd, cl);
                memcpy(target + cl, deep, strlen(deep) + 1);
                JC_CHECK(jc_path_resolve(target, out, sizeof(out)) == JC_OK);
                JC_CHECK(jc_path_under_root(cwd, out) == 1);
                JC_CHECK(strstr(out, "/no_such_a_123/no_such_b/no_such_c/f.txt")
                         != NULL);
            }
        }
        /* But a missing tail is re-appended VERBATIM, so it may not carry a
         * `..` (or `.`) -- that would let "<root>/nope/../../etc/x" read as
         * inside the root while the kernel walked it out. Fail closed. */
        {
            size_t cl = strlen(cwd);
            const char *climb = "/no_such_dir_abc_123/../../../../etc/x";
            const char *dot = "/no_such_dir_abc_123/./x";
            if (cl + strlen(climb) + 1 < sizeof(target)) {
                memcpy(target, cwd, cl);
                memcpy(target + cl, climb, strlen(climb) + 1);
                JC_CHECK(jc_path_resolve(target, out, sizeof(out)) ==
                         JC_ERR_NOTFOUND);
                memcpy(target + cl, dot, strlen(dot) + 1);
                JC_CHECK(jc_path_resolve(target, out, sizeof(out)) ==
                         JC_ERR_NOTFOUND);
            }
        }
    }


    /* A tiny capacity is reported, not overflowed. */
    JC_CHECK(jc_path_resolve(".", out, 2) == JC_ERR_TOOBIG);

    /* Bad arguments. */
    JC_CHECK(jc_path_resolve(NULL, out, sizeof(out)) == JC_ERR_INVALID);
    JC_CHECK(jc_path_resolve("", out, sizeof(out)) == JC_ERR_INVALID);
}

/* Remove everything test_symlink_escape creates under `base`, and say whether
 * `base` itself went (0) or not (-1). M727: the name carries the pid, and
 * FreeMiNT's pids repeat from boot to boot (the suite is pid 5), while the
 * final cleanup here had never removed `sym_dir` -- so every run left `base`
 * behind (125 of them in one Linux /tmp), the next boot's mkdir failed, and
 * the test returned without a word. That hid a stack overflow in its `loop`
 * case through every hand run in one guest directory; the rig's fresh disk
 * found it. So the fixture is cleared before it is made, a mkdir that still
 * fails is a failure, and the cleanup must leave nothing. */
static int clear_symlink_fixture(const char *base)
{
    static const char *const leaves[] = {
        "/inside/real.txt", "/inside", "/sym_dir", "/escape",
        "/dangle_out", "/dangle_in", "/loop"
    };
    char p[700];
    size_t i;

    for (i = 0; i < sizeof leaves / sizeof leaves[0]; i++) {
        jc_snprintf(p, sizeof p, "%s%s", base, leaves[i]);
        (void)remove(p);
    }
    return remove(base) == 0 ? 0 : -1;
}

/* Build a per-process scratch dir under /tmp and verify a symlink that points
 * outside the root is detected (resolves out, so not contained). */
static void test_symlink_escape(void)
{
    char base[256];
    char canon[JC_PATH_MAX];
    char inside_dir[512];
    char inside_file[600];
    char link_path[512];
    char via_link[600];
    long pid = (long)getpid();
    FILE *f;

    /* base = /tmp/jichi_path_test_<pid> */
    {
        const char *pfx = jc_test_tmp("jichi_path_test_");
        size_t pl = strlen(pfx);
        char num[32];
        int nn = 0;
        long v = pid;
        /* itoa without sprintf */
        if (v == 0) { num[nn++] = '0'; }
        while (v > 0 && nn < 30) { num[nn++] = (char)('0' + (v % 10)); v /= 10; }
        if (!JC_REQUIRE(pl + (size_t)nn < sizeof base)) {
            return; /* the fixture path does not fit (M728) */
        }
        {
            int i;
            memcpy(base, pfx, pl);
            for (i = 0; i < nn; i++) {
                base[pl + i] = num[nn - 1 - i];
            }
            base[pl + nn] = '\0';
        }
    }

    (void)clear_symlink_fixture(base); /* a previous run's, under this pid */
    if (!JC_REQUIRE(mkdir(base, 0700) == 0)) {
        return; /* red, not a silent skip (M727) */
    }

    /* base/inside (a real subdir) + a file in it. */
    {
        size_t bl = strlen(base);
        memcpy(inside_dir, base, bl);
        memcpy(inside_dir + bl, "/inside", 8);
        mkdir(inside_dir, 0700);
        memcpy(inside_file, inside_dir, strlen(inside_dir));
        memcpy(inside_file + strlen(inside_dir), "/real.txt", 10);
        f = fopen(inside_file, "wb");
        if (f != NULL) { fputs("hi", f); fclose(f); }
    }

    /* base/sym_dir -> base/inside (a symlink to a real subdir). */
    {
        size_t bl = strlen(base);
        char sym_dir[512];
        memcpy(sym_dir, base, bl);
        memcpy(sym_dir + bl, "/sym_dir", 9);
        if (symlink(inside_dir, sym_dir) != 0) {
            /* Cleanup and bail if symlinks failed */
            (void)clear_symlink_fixture(base);
            return;
        }

        /* A not-yet-existing file inside the symlinked dir should resolve via the symlink. */
        {
            char sym_file[600];
            memcpy(sym_file, sym_dir, strlen(sym_dir));
            /* 14, not 13: "/new_file.txt" is 13 characters PLUS the
             * terminator, and the sibling memcpys here all count the NUL.
             * The miscount left sym_file unterminated, so realpath read
             * past the buffer to a stray NUL 1.5 KB up the stack -- latent
             * since M204, visible only to ASan, which only fires when
             * `make ci` actually runs (ANECDOTES: it exists, it is
             * correct, and it had not been run). */
            memcpy(sym_file + strlen(sym_dir), "/new_file.txt", 14);
            
            JC_CHECK(jc_path_resolve(sym_file, canon, sizeof(canon)) == JC_OK);
            /* The result should be the canonical path to the file in 'inside' */
            JC_CHECK(jc_path_under_root(canon, canon) == 1);
            JC_CHECK(strstr(canon, "/inside/new_file.txt") != NULL);
        }
    }

    /* base/escape -> /tmp (a symlink that climbs out of base). */
    {
        size_t bl = strlen(base);
        memcpy(link_path, base, bl);
        memcpy(link_path + bl, "/escape", 8);
        if (symlink(jc_test_tmpdir(), link_path) != 0) {
            /* cleanup and bail (symlinks unsupported here) */
            (void)clear_symlink_fixture(base);
            return;
        }
        memcpy(via_link, link_path, strlen(link_path));
        memcpy(via_link + strlen(link_path), "/foo", 5);
    }

    JC_CHECK(jc_path_resolve(base, canon, sizeof(canon)) == JC_OK);

    /* A genuine file inside the root is contained. */
    JC_CHECK(jc_path_in_root(canon, inside_file) == 1);

    /* A path through the escaping symlink resolves to /tmp/foo -> NOT contained.*/
    JC_CHECK(jc_path_in_root(canon, via_link) == 0);
    /* M638: nor when two missing levels follow the link. */
    {
        char deep[700];
        size_t vl = strlen(via_link);
        memcpy(deep, via_link, vl);
        memcpy(deep + vl, "/nope/f.txt", 12);
        JC_CHECK(jc_path_in_root(canon, deep) == 0);
        /* and a missing tail under the symlinked INSIDE dir is inside */
        vl = strlen(via_link) - 7 + 8; /* base + "/sym_dir" */
        memcpy(deep, base, strlen(base));
        memcpy(deep + strlen(base), "/sym_dir/nope/f.txt", 20);
        JC_CHECK(jc_path_in_root(canon, deep) == 1);
        (void)vl;
    }

    /* M607: DANGLING links -- the leaf exists as a symlink, its target does not
     * exist yet. realpath() fails on such a leaf, and the resolver used to fall
     * to the parent branch and re-append the leaf verbatim: "inside", while
     * fopen("wb") would follow the link. Three shapes:
     *   dangle_out -> <tmpdir>/no_such_<pid>   (absolute, outside)  => denied
     *   dangle_in  -> inside/not_yet.txt        (relative, inside)   => allowed
     *   loop       -> loop                      (a cycle)            => denied
     * The relative-inside case guards the fix from over-reaching: a link a
     * project legitimately keeps (a symlinked build output not written yet)
     * must still resolve to where it points, inside. */
    {
        size_t bl = strlen(base);
        char dangle_out[600];
        char dangle_in[600];
        char loop[600];
        char out_target[600];
        char probe[JC_PATH_MAX];
        memcpy(dangle_out, base, bl); memcpy(dangle_out + bl, "/dangle_out", 12);
        memcpy(dangle_in, base, bl);  memcpy(dangle_in + bl, "/dangle_in", 11);
        memcpy(loop, base, bl);       memcpy(loop + bl, "/loop", 6);
        {
            const char *td = jc_test_tmpdir();
            size_t tl = strlen(td);
            memcpy(out_target, td, tl);
            memcpy(out_target + tl, "/jichi_no_such_target_m607", 27);
        }
        (void)remove(out_target);
        if (symlink(out_target, dangle_out) == 0 &&
            symlink("inside/not_yet.txt", dangle_in) == 0 &&
            symlink("loop", loop) == 0) {
            JC_CHECK(jc_path_in_root(canon, dangle_out) == 0);   /* the escape */
            JC_CHECK(jc_path_in_root(canon, dangle_in) == 1);    /* still fine */
            /* M638: a MISSING TAIL below a dangling out-link follows the link
             * (the deepest existing ancestor is the link itself, and it is
             * resolved through its target), so the verdict is outside. */
            {
                char below[700];
                size_t dl = strlen(dangle_out);
                memcpy(below, dangle_out, dl);
                memcpy(below + dl, "/newdir/f.txt", 14);
                JC_CHECK(jc_path_in_root(canon, below) == 0);
                dl = strlen(dangle_in);
                memcpy(below, dangle_in, dl);
                memcpy(below + dl, "/newdir/f.txt", 14);
                JC_CHECK(jc_path_in_root(canon, below) == 1);
            }
            JC_CHECK(jc_path_resolve(dangle_in, probe, sizeof(probe)) == JC_OK);
            JC_CHECK(strstr(probe, "/inside/not_yet.txt") != NULL);
            JC_CHECK(jc_path_resolve(loop, probe, sizeof(probe)) ==
                     JC_ERR_NOTFOUND);                             /* bounded */
            JC_CHECK(jc_path_in_root(canon, loop) == 0);         /* fail closed */
        }
        remove(dangle_out);
        remove(dangle_in);
        remove(loop);
    }

    /* Cleanup, and nothing may be left: a leftover `base` is what skipped
     * the next run on FreeMiNT (see clear_symlink_fixture). */
    JC_CHECK(clear_symlink_fixture(base) == 0);
}

/* M192: pure lexical normalization for identity comparison. No filesystem
 * access, so everything here is offline. */
static void test_normalize(void)
{
    char out[64];

    /* A relative path is joined against cwd -- the case that matters, since the
     * model spells the same file both ways within one session. */
    JC_CHECK(jc_path_normalize("/work", "src/vm.zig", out, sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/work/src/vm.zig") == 0);
    /* ...and both spellings must converge on the same string. */
    JC_CHECK(jc_path_normalize("/work", "/work/src/vm.zig", out,
                               sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/work/src/vm.zig") == 0);

    /* An absolute path keeps its leading slash and is otherwise unchanged. */
    JC_CHECK(jc_path_normalize(NULL, "/a/b/c", out, sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/a/b/c") == 0);

    /* Redundant separators and "." segments collapse. */
    JC_CHECK(jc_path_normalize("/work", ".//src///vm.zig", out,
                               sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/work/src/vm.zig") == 0);
    JC_CHECK(jc_path_normalize(NULL, "/a//./b/", out, sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/a/b") == 0);
    /* A trailing slash is not significant, but a bare root survives. */
    JC_CHECK(jc_path_normalize(NULL, "/", out, sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/") == 0);
    /* A trailing slash on cwd doesn't double up. */
    JC_CHECK(jc_path_normalize("/work/", "src/x", out, sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/work/src/x") == 0);

    /* ".." is REFUSED, not collapsed: "a/link/../b" is not "a/b" when link is a
     * symlink, and a caller comparing for identity would merge two distinct
     * files. Callers fall back to the raw spelling (a missed dedup). */
    JC_CHECK(jc_path_normalize("/work", "src/../src/vm.zig", out,
                               sizeof(out)) == JC_ERR_INVALID);
    JC_CHECK(jc_path_normalize(NULL, "/a/../b", out, sizeof(out)) ==
             JC_ERR_INVALID);
    JC_CHECK(jc_path_normalize(NULL, "..", out, sizeof(out)) == JC_ERR_INVALID);
    JC_CHECK(jc_path_normalize(NULL, "/a/..", out, sizeof(out)) ==
             JC_ERR_INVALID);
    /* ...but a filename that merely BEGINS with dots is fine. */
    JC_CHECK(jc_path_normalize("/work", "..hidden", out, sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/work/..hidden") == 0);
    JC_CHECK(jc_path_normalize("/work", "src/...", out, sizeof(out)) == JC_OK);
    JC_CHECK(strcmp(out, "/work/src/...") == 0);

    /* A relative path with no usable cwd cannot be normalized. */
    JC_CHECK(jc_path_normalize(NULL, "src/x", out, sizeof(out)) ==
             JC_ERR_INVALID);
    JC_CHECK(jc_path_normalize("relative/cwd", "src/x", out, sizeof(out)) ==
             JC_ERR_INVALID);

    /* Bad arguments and overflow report rather than truncate. */
    JC_CHECK(jc_path_normalize("/work", NULL, out, sizeof(out)) ==
             JC_ERR_INVALID);
    JC_CHECK(jc_path_normalize("/work", "", out, sizeof(out)) == JC_ERR_INVALID);
    JC_CHECK(jc_path_normalize("/work", "src/x", NULL, sizeof(out)) ==
             JC_ERR_INVALID);
    JC_CHECK(jc_path_normalize("/work", "src/x", out, 0) == JC_ERR_INVALID);
    {
        char tiny[8];
        JC_CHECK(jc_path_normalize("/work", "src/verylongname", tiny,
                                   sizeof(tiny)) == JC_ERR_TOOBIG);
    }
}

void test_path(void)
{
    test_under_root();
    test_resolve();
    test_symlink_escape();
    test_normalize();
}
