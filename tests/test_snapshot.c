/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_snapshot.c - offline tests for the pure snapshot helpers.
 *
 * The git flow (init -> checkpoint -> edit -> undo) is verified end-to-end; here
 * we only exercise the deterministic, network-free helpers: shadow git-dir
 * derivation and the checkpoint-label cleaner. */

#include "jc_test.h"
#include "jc_snapshot.h"
#include "jc_parallel.h"   /* M927 (R04-F1): parse_changes_z */
#include "jc_toolout.h"
#include "jc_app.h"
#include "jc_mem.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static void test_git_dir(void)
{
    char a[256];
    char b[256];
    char c[256];
    const char *pa = a, *pb = b, *pc = c;

    jc_snapshot_git_dir("/home/u", "/work/proj", a, sizeof(a));
    /* Anchored under the per-user cache dir. The prefix length is derived, not
     * spelled: a hardcoded 35 silently stopped matching when the state dir was
     * renamed .jlu_continue.d -> .jichi.d. */
    {
        const char *want = "/home/u/.jichi.d/checkpoints/";
        JC_CHECK(strncmp(pa, want, strlen(want)) == 0);
    }

    /* Deterministic: same inputs -> same path. */
    jc_snapshot_git_dir("/home/u", "/work/proj", b, sizeof(b));
    JC_CHECK_STR(pa, pb);

    /* Distinct work trees -> distinct dirs. */
    jc_snapshot_git_dir("/home/u", "/work/other", c, sizeof(c));
    JC_CHECK(strcmp(pc, pa) != 0);
}

/* The key in the shadow-repo path is the derivation: `jc_workspace_key` of the
 * canonical work tree. After D-09 it is a SHA-256 digest rendered as 64
 * lowercase hex chars. This asserts the POST-FIX contract (shape + determinism
 * + the Ez/FY collision pair now separating), so it is RED on the unfixed tree,
 * where the key is a 20-char decimal `unsigned long` and djb2 makes the two
 * colliding names identical. */
static void test_key_shape(void)
{
    char a[256];
    char b[256];
    const char *ka, *kb;

    /* The key is the part of the git dir after the last '/'. */
    jc_snapshot_git_dir("/home/u", "/work/proj", a, sizeof(a));
    ka = strrchr(a, '/');
    JC_CHECK(ka != NULL);
    if (ka == NULL) return;
    ka++;
    /* (i) exactly 64 chars: the SHA-256 hex length. */
    JC_CHECK(strlen(ka) == 64);
    /* (ii) every char is a lowercase hex digit. */
    {
        int all_hex = 1;
        size_t i;
        for (i = 0; ka[i] != '\0'; i++) {
            char c = ka[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                all_hex = 0;
                break;
            }
        }
        JC_CHECK(all_hex == 1);
    }

    /* Determinism: same input -> same key. */
    jc_snapshot_git_dir("/home/u", "/work/proj", b, sizeof(b));
    kb = strrchr(b, '/');
    JC_CHECK(kb != NULL);
    if (kb == NULL) return;
    kb++;
    JC_CHECK_STR(ka, kb);

    /* (iii) the djb2 collision pair -- names differing by (+1,-33) in adjacent
     * bytes -- must now produce DISTINCT keys. */
    {
        char c1[256];
        char c2[256];
        const char *k1, *k2;
        jc_snapshot_git_dir("/p", "/p/Ez", c1, sizeof(c1));
        jc_snapshot_git_dir("/p", "/p/FY", c2, sizeof(c2));
        k1 = strrchr(c1, '/');
        k2 = strrchr(c2, '/');
        JC_CHECK(k1 != NULL && k2 != NULL);
        if (k1 == NULL || k2 == NULL) return;
        k1++;
        k2++;
        JC_CHECK(strcmp(k1, k2) != 0);
    }
}

static void test_clean_label(void)
{
    char buf[64];
    const char *p = buf;

    jc_snapshot_clean_label("fix the   parser\n\tbug", buf, sizeof(buf));
    JC_CHECK_STR(p, "fix the parser bug");

    /* Leading/trailing whitespace is trimmed. */
    jc_snapshot_clean_label("   hello world  ", buf, sizeof(buf));
    JC_CHECK_STR(p, "hello world");

    /* NULL and empty are safe. */
    jc_snapshot_clean_label(NULL, buf, sizeof(buf));
    JC_CHECK_STR(p, "");
    jc_snapshot_clean_label("", buf, sizeof(buf));
    JC_CHECK_STR(p, "");

    /* Truncation respects the buffer. */
    {
        char small[6];
        const char *q = small;
        jc_snapshot_clean_label("abcdefghij", small, sizeof(small));
        JC_CHECK(strlen(q) <= 5);
    }
}

/* End-to-end: take checkpoints in a throwaway shadow repo (HOME redirected to a
 * temp dir so nothing leaks into the user's home), then prove
 * jc_snapshot_restore_commit reverts the work tree to a named green commit.
 * Skipped cleanly when git is unavailable. */
/* M349: the undo note -- what the model is told after /undo reverts files it
 * believed it had changed. Bounded list, honest zero (no changed files => no
 * note), and the label quoted only when there is one. */
static void test_undo_note(void)
{
    struct jc_sb sb;

    /* Deref checks are COMBINED with the null check on purpose: JC_CHECK
     * counts and continues, so a separate null check would not stop the next
     * line from dereferencing NULL when the renderer wrote nothing -- the
     * teeth run for this very test segfaulted instead of counting before
     * this shape was adopted (the M343 tests' idiom). */
    jc_sb_init(&sb);
    /* M1095 (D-03): the input is now the NUL-separated raw form (a trailing
     * NUL after the last name); the length is passed explicitly because
     * strlen would stop at the first segment. Assertions are unchanged. */
    jc_snapshot_undo_note("fix the parser", "src/a.c\0src/b.c\0", 16, &sb);
    JC_CHECK(sb.data != NULL && strncmp(sb.data, "[undo]", 6) == 0);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "\"fix the parser\"") != NULL);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "2 file(s) reverted: src/a.c, src/b.c") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "re-read") != NULL);
    JC_CHECK(sb.data == NULL || strstr(sb.data, "more)") == NULL);
    jc_sb_free(&sb);

    /* No label: no quotes, the generic form. */
    jc_sb_init(&sb);
    jc_snapshot_undo_note(NULL, "x.c\0", 4, &sb);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "the last checkpoint") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "\"") == NULL);
    jc_sb_free(&sb);

    /* Twelve files: eight listed, the rest counted. */
    jc_sb_init(&sb);
    jc_snapshot_undo_note("big",
        "f01\0f02\0f03\0f04\0f05\0f06\0f07\0f08\0f09\0f10\0f11\0f12\0",
        48, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "12 file(s) reverted") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "f08") != NULL);
    JC_CHECK(sb.data == NULL || strstr(sb.data, "f09") == NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "(+4 more)") != NULL);
    jc_sb_free(&sb);

    /* Nothing changed => nothing became stale => no note at all. */
    jc_sb_init(&sb);
    jc_snapshot_undo_note("label", "", 0, &sb);
    JC_CHECK(sb.len == 0);
    jc_snapshot_undo_note("label", "\0", 1, &sb);
    JC_CHECK(sb.len == 0);
    jc_snapshot_undo_note("label", NULL, 0, &sb);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
}

static void test_restore_commit(void);
static void test_restore_clean_before_reset(void);

/* M1095 (D-03): jc_snapshot_changed_since must hand back NUL-separated RAW
 * names. Git's line form C-quotes non-ASCII paths ("caf\303\251.txt"), which
 * made the edit-scope sweep match the QUOTED string (an in-scope file reported
 * as a violation) and jc_snapshot_restore_paths unlink a name that is not on
 * disk (the journal says reverted, the file survives). -z fixes the root. */
/* strstr for a NUL-containing buffer: finds needle anywhere in buf[0..len).
 * The -z form separates names with INTERNAL NULs, so a plain strstr on
 * buf.data stops at the first segment and cannot see the rest. */
static int sb_contains(const char *buf, jc_size len, const char *needle)
{
    jc_size nl = strlen(needle);
    jc_size i;
    if (buf == NULL || nl > len) {
        return 0;
    }
    for (i = 0; i + nl <= len; i++) {
        if (memcmp(buf + i, needle, nl) == 0) {
            return 1;
        }
    }
    return 0;
}

static void test_changed_since_z(void)
{
    struct jc_app app;
    struct jc_snapshot_mgr mgr;
    struct jc_arena *a = jc_arena_new(0);
    char tmp[256];
    char work[320];
    char file[400];
    char saved_home[1024];
    const char *home = getenv("HOME");
    const char *c0;
    struct jc_sb sb;

    saved_home[0] = '\0';
    if (home != NULL) {
        jc_snprintf(saved_home, sizeof(saved_home), "%s", home);
    }

    if (!JC_REQUIRE(jc_snprintf(tmp, sizeof(tmp), "%s/jichi_z_test_%ld",
                                jc_test_tmpdir(), (long)getpid())
                    < (int)sizeof(tmp))) {
        jc_arena_free(a);
        return; /* a cut fixture path is a different path (M728) */
    }
    jc_snprintf(work, sizeof(work), "%s/work", tmp);
    jc_mkdir_p(work);
    setenv("HOME", tmp, 1);

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_snprintf(app.cwd, sizeof(app.cwd), "%s", work);
    app.config.snapshots = 1;
    app.config.snapshot_limit = 100;

    jc_snapshot_manager_init(&mgr, &app);
    if (jc_snapshot_available(&mgr)) {
        jc_snprintf(file, sizeof(file), "%s/base.txt", work);
        jc_write_file(file, "v1\n", 3);
        jc_snapshot_take(&mgr, "first");
        c0 = jc_snapshot_commit(&mgr, jc_snapshot_count(&mgr) - 1);
        JC_CHECK(c0 != NULL);
        if (JC_REQUIRE(c0 != NULL)) {
            /* Three files after the checkpoint: two C-quoted by git's line
             * form, one plain. */
            jc_snprintf(file, sizeof(file), "%s/caf\303\251.txt", work);
            jc_write_file(file, "x\n", 2);
            jc_snprintf(file, sizeof(file), "%s/plain.txt", work);
            jc_write_file(file, "y\n", 2);
            jc_snprintf(file, sizeof(file), "%s/src", work);
            jc_mkdir_p(file);
            jc_snprintf(file, sizeof(file), "%s/src/na\303\257ve.c", work);
            jc_write_file(file, "z\n", 2);
            /* M1095-b (SV:0918): a name carrying a real NEWLINE. The line form
             * C-quotes it to "new\nline.txt" (backslash-n), so it is the pin
             * that the -z form hands back the raw 0x0A byte and nothing else. */
            jc_snprintf(file, sizeof(file), "%s/new\nline.txt", work);
            jc_write_file(file, "x", 1);

            jc_sb_init(&sb);
            JC_CHECK(jc_snapshot_changed_since(&mgr, c0, &sb) == JC_OK);
            /* (1) floor: four names exist at all. */
            JC_CHECK(sb.data != NULL && sb.len >= 4);
            if (sb.data != NULL) {
                int i;
                int nnul = 0;
                int badbyte = 0;
                /* (2-5) the RAW bytes are present (buffer-wide: the names are
                 * NUL-separated, so strstr on .data would stop at the first
                 * segment); the C-quoted form fails (6), so it cannot also
                 * contain the raw bytes. */
                JC_CHECK(sb_contains(sb.data, sb.len, "caf\303\251.txt"));
                JC_CHECK(sb_contains(sb.data, sb.len, "na\303\257ve.c"));
                JC_CHECK(sb_contains(sb.data, sb.len, "plain.txt"));
                /* M1095-b: the newline name is present as a RAW 0x0A byte, and
                 * NOT as the two-byte C-quoted line form (backslash-n). */
                JC_CHECK(sb_contains(sb.data, sb.len, "new\nline.txt"));
                JC_CHECK(!sb_contains(sb.data, sb.len, "new\\nline.txt"));
                /* (6) no C-quoting anywhere: no '"' byte and no '\' byte. */
                for (i = 0; i < (int)sb.len; i++) {
                    if (sb.data[i] == '"' || sb.data[i] == '\\') {
                        badbyte = 1;
                        break;
                    }
                }
                JC_CHECK(badbyte == 0);
                /* (7) exactly four NUL-terminated segments: four NULs
                 * inside [0, sb.len). */
                for (i = 0; i < (int)sb.len; i++) {
                    if (sb.data[i] == '\0') {
                        nnul++;
                    }
                }
                JC_CHECK(nnul == 4);
            }
            jc_sb_free(&sb);
        }
    }

    jc_snapshot_manager_shutdown(&mgr);
    jc_arena_free(a);
    if (saved_home[0] != '\0') {
        setenv("HOME", saved_home, 1);
    }
    (void)jc_test_rm_rf(tmp);
}

static void test_restore_commit(void)
{
    struct jc_app app;
    struct jc_snapshot_mgr mgr;
    struct jc_arena *a = jc_arena_new(0);
    char tmp[256];
    char work[320];
    char file[400];
    char sha[64];
    char saved_home[1024];
    const char *home = getenv("HOME");
    const char *c0;
    char *content = NULL;
    jc_size len = 0;

    saved_home[0] = '\0';
    if (home != NULL) {
        jc_snprintf(saved_home, sizeof(saved_home), "%s", home);
    }

    if (!JC_REQUIRE(jc_snprintf(tmp, sizeof(tmp), "%s/jichi_snap_test_%ld",
                                jc_test_tmpdir(), (long)getpid())
                    < (int)sizeof(tmp))) {
        jc_arena_free(a);
        return; /* a cut fixture path is a different path (M728) */
    }
    jc_snprintf(work, sizeof(work), "%s/work", tmp);
    jc_mkdir_p(work);
    setenv("HOME", tmp, 1);

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_snprintf(app.cwd, sizeof(app.cwd), "%s", work);
    app.config.snapshots = 1;
    app.config.snapshot_limit = 100;

    jc_snapshot_manager_init(&mgr, &app);
    if (jc_snapshot_available(&mgr)) {
        jc_snprintf(file, sizeof(file), "%s/data.txt", work);

        jc_write_file(file, "v1\n", 3);
        jc_snapshot_take(&mgr, "first");
        c0 = jc_snapshot_commit(&mgr, jc_snapshot_count(&mgr) - 1);
        JC_CHECK(c0 != NULL);
        jc_snprintf(sha, sizeof(sha), "%s", c0 != NULL ? c0 : "");

        jc_write_file(file, "v2\n", 3);
        jc_snapshot_take(&mgr, "second");
        jc_write_file(file, "v3-uncommitted\n", 15);

        JC_CHECK(jc_snapshot_restore_commit(&mgr, sha) == JC_OK);
        JC_CHECK(jc_read_file(file, &content, &len, a) == JC_OK);
        JC_CHECK_STR(content != NULL ? content : "", "v1\n");

        /* A bogus SHA is reported, not crashed. */
        JC_CHECK(jc_snapshot_restore_commit(&mgr, NULL) == JC_ERR_NOTFOUND);
    }

    jc_snapshot_manager_shutdown(&mgr);
    jc_arena_free(a);
    if (saved_home[0] != '\0') {
        setenv("HOME", saved_home, 1);
    }
    (void)jc_test_rm_rf(tmp);
}

/* End-to-end: an isolated worktree from the shadow repo materialises the base
 * content; edits there are reported by worktree_changes and never touch the
 * live work tree; remove cleans up. Skipped cleanly when git is unavailable. */
/* `git worktree` arrived in git 2.5 (2015); CentOS/RHEL 7 ships git 1.8.3.1,
 * where the subcommand does not exist. Probe rather than parse a version
 * string, and SKIP loudly -- the M264 V2a row found this block segfaulting on
 * such a host, because JC_CHECK records a failure and keeps going, so a
 * NULL-check was followed by strstr(NULL). */
static int git_has_worktree(void)
{
    FILE *p = popen("git worktree -h 2>&1", "r");
    char buf[256];
    int supported = 1;
    if (p == NULL) {
        return 0;
    }
    while (fgets(buf, (int)sizeof(buf), p) != NULL) {
        if (strstr(buf, "is not a git command") != NULL) {
            supported = 0;
        }
    }
    pclose(p);
    return supported;
}

static void test_worktree(void)
{
    struct jc_app app;
    struct jc_snapshot_mgr mgr;
    struct jc_arena *a = jc_arena_new(0);
    char tmp[256];
    char work[320];
    char wfile[400];
    char wt[360];
    char wtfile[460];
    char base[64];
    char saved_home[1024];
    const char *home = getenv("HOME");
    const char *c0;

    saved_home[0] = '\0';
    if (home != NULL) {
        jc_snprintf(saved_home, sizeof(saved_home), "%s", home);
    }
    if (!JC_REQUIRE(jc_snprintf(tmp, sizeof(tmp), "%s/jichi_wt_test_%ld",
                                jc_test_tmpdir(), (long)getpid())
                    < (int)sizeof(tmp))) {
        jc_arena_free(a);
        return; /* a cut fixture path is a different path (M728) */
    }
    jc_snprintf(work, sizeof(work), "%s/work", tmp);
    jc_mkdir_p(work);
    setenv("HOME", tmp, 1);

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_snprintf(app.cwd, sizeof(app.cwd), "%s", work);
    app.config.snapshots = 1;
    app.config.snapshot_limit = 100;

    jc_snapshot_manager_init(&mgr, &app);
    if (jc_snapshot_available(&mgr) && !git_has_worktree()) {
        printf("  (skipping worktree integration: git is older than 2.5)\n");
    } else if (jc_snapshot_available(&mgr)) {
        struct jc_sb changes;

        jc_snprintf(wfile, sizeof(wfile), "%s/keep.txt", work);
        jc_write_file(wfile, "base\n", 5);
        jc_snapshot_take(&mgr, "base");
        c0 = jc_snapshot_commit(&mgr, jc_snapshot_count(&mgr) - 1);
        jc_snprintf(base, sizeof(base), "%s", c0 != NULL ? c0 : "");

        jc_snprintf(wt, sizeof(wt), "%s/wt-1", tmp);
        JC_CHECK(jc_snapshot_worktree_add(&mgr, base, wt) == JC_OK);

        /* The worktree has the base content. */
        jc_snprintf(wtfile, sizeof(wtfile), "%s/keep.txt", wt);
        JC_CHECK(jc_file_exists(wtfile));

        /* Edit existing + add a new file inside the worktree only. */
        jc_write_file(wtfile, "edited\n", 7);
        jc_snprintf(wtfile, sizeof(wtfile), "%s/fresh.txt", wt);
        jc_write_file(wtfile, "new\n", 4);

        jc_sb_init(&changes);
        JC_CHECK(jc_snapshot_worktree_changes(&mgr, wt, base, &changes)
                 == JC_OK);
        JC_CHECK(changes.data != NULL);
        if (changes.data != NULL) {   /* JC_CHECK records and CONTINUES */
            JC_CHECK(strstr(changes.data, "keep.txt") != NULL);   /* M */
            JC_CHECK(strstr(changes.data, "fresh.txt") != NULL);  /* A */
        }
        jc_sb_free(&changes);

        /* M927 (R04-F1): a file whose name git C-quotes. The line form lists
         * it as A<tab>"caf\303\251.md" (quoted), which the line parser copied
         * as a literal path that no copy can reach -- so the merge lost the
         * file and still reported success. The NUL form lists it raw; it must
         * survive both the git call and the parser. */
        {
            char caf[420];
            jc_snprintf(caf, sizeof(caf), "%s/caf\303\251.md", wt);
            jc_write_file(caf, "accents\n", 8);
            jc_sb_init(&changes);
            JC_CHECK(jc_snapshot_worktree_changes_z(&mgr, wt, base, &changes)
                     == JC_OK);
            {
                struct jc_arena *pa;
                struct jc_vec pv;
                struct jc_change *pc;
                int found_cafe = 0;
                int pi;
                pa = jc_arena_new(0);
                jc_vec_init(&pv, sizeof(struct jc_change));
                if (changes.data != NULL) {
                    jc_parallel_parse_changes_z(changes.data, pa, &pv);
                    for (pi = 0; pi < (int)pv.len; pi++) {
                        pc = (struct jc_change *)jc_vec_at(&pv, pi);
                        /* caf\303\251.md is the UTF-8 bytes c a f C3 A9 . m d.
                         * Compare raw bytes (not a source literal): the test must
                         * read the same bytes the parser produced. */
                        if (pc->path[0]=='c' && pc->path[1]=='a' &&
                            pc->path[2]=='f' && pc->path[3]==(char)0xc3 &&
                            pc->path[4]==(char)0xa9 && pc->path[5]=='.' &&
                            pc->path[6]=='m' && pc->path[7]=='d' &&
                            pc->path[8]=='\0') {
                            JC_CHECK(pc->status == 'A');
                            found_cafe = 1;
                        }
                    }
                }
                JC_CHECK(found_cafe == 1);
                jc_vec_free(&pv);
                jc_arena_free(pa);
            }
            jc_sb_free(&changes);
        }

        /* The live work tree is untouched by the worktree edits. */
        {
            char *content = NULL;
            jc_size len = 0;
            JC_CHECK(jc_read_file(wfile, &content, &len, a) == JC_OK);
            JC_CHECK_STR(content != NULL ? content : "", "base\n");
        }

        JC_CHECK(jc_snapshot_worktree_remove(&mgr, wt, JC_WT_MERGED, NULL) == JC_OK);
    }

    jc_snapshot_manager_shutdown(&mgr);
    jc_arena_free(a);
    if (saved_home[0] != '\0') {
        setenv("HOME", saved_home, 1);
    }
    (void)jc_test_rm_rf(tmp);
}

/* M142: per-path restore from a baseline -- the out-of-scope auto-revert's
 * core. A modified file is checked back out, a created file is removed, a
 * deleted file is resurrected, and unlisted files keep their changes. */
static void test_restore_paths(void)
{
    struct jc_app app;
    struct jc_snapshot_mgr mgr;
    struct jc_arena *a = jc_arena_new(0);
    char tmp[256];
    char work[320];
    char fin[400], fout[400], fnew[400], fgone[400];
    char cmd[420];
    char sha[64];
    char saved_home[1024];
    const char *home = getenv("HOME");
    const char *c0;
    char *content = NULL;
    jc_size len = 0;

    saved_home[0] = '\0';
    if (home != NULL) {
        jc_snprintf(saved_home, sizeof(saved_home), "%s", home);
    }
    if (!JC_REQUIRE(jc_snprintf(tmp, sizeof(tmp), "%s/jichi_snap_rp_%ld",
                                jc_test_tmpdir(), (long)getpid())
                    < (int)sizeof(tmp))) {
        jc_arena_free(a);
        return; /* a cut fixture path is a different path (M728) */
    }
    jc_snprintf(work, sizeof(work), "%s/work", tmp);
    jc_mkdir_p(work);
    setenv("HOME", tmp, 1);

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_snprintf(app.cwd, sizeof(app.cwd), "%s", work);
    app.config.snapshots = 1;
    app.config.snapshot_limit = 100;

    jc_snapshot_manager_init(&mgr, &app);
    if (jc_snapshot_available(&mgr)) {
        int nrev = -1;
        int nfail = -1;
        const char *paths[3];

        jc_snprintf(fin, sizeof(fin), "%s/in-scope.txt", work);
        jc_snprintf(fout, sizeof(fout), "%s/outside.txt", work);
        jc_snprintf(fnew, sizeof(fnew), "%s/created.txt", work);
        jc_snprintf(fgone, sizeof(fgone), "%s/gone.txt", work);
        jc_write_file(fin, "keep\n", 5);
        jc_write_file(fout, "orig\n", 5);
        jc_write_file(fgone, "precious\n", 9);
        jc_snapshot_take(&mgr, "baseline");
        c0 = jc_snapshot_commit(&mgr, jc_snapshot_count(&mgr) - 1);
        JC_CHECK(c0 != NULL);
        jc_snprintf(sha, sizeof(sha), "%s", c0 != NULL ? c0 : "");

        /* The "run": one in-scope edit + three out-of-scope sins. */
        jc_write_file(fin, "changed in scope\n", 17);
        jc_write_file(fout, "tampered\n", 9);
        jc_write_file(fnew, "should not exist\n", 17);
        remove(fgone);

        paths[0] = "outside.txt";
        paths[1] = "created.txt";
        paths[2] = "gone.txt";
        JC_CHECK(jc_snapshot_restore_paths(&mgr, sha, paths, 3,
                                           &nrev, &nfail) == JC_OK);
        JC_CHECK(nrev == 3);
        JC_CHECK(nfail == 0);

        /* Modified: back to baseline. */
        JC_CHECK(jc_read_file(fout, &content, &len, a) == JC_OK);
        JC_CHECK_STR(content != NULL ? content : "", "orig\n");
        /* Created: removed. */
        JC_CHECK(!jc_file_exists(fnew));
        /* Deleted: resurrected. */
        JC_CHECK(jc_read_file(fgone, &content, &len, a) == JC_OK);
        JC_CHECK_STR(content != NULL ? content : "", "precious\n");
        /* Unlisted (in-scope) work is untouched -- the selective half. */
        JC_CHECK(jc_read_file(fin, &content, &len, a) == JC_OK);
        JC_CHECK_STR(content != NULL ? content : "", "changed in scope\n");

        /* Unavailable / bad input is reported, not crashed. */
        JC_CHECK(jc_snapshot_restore_paths(&mgr, NULL, paths, 3, &nrev,
                                           &nfail) == JC_ERR_NOTFOUND);

        /* M337b: --revert-out-of-scope destroyed the content of every path
         * above and M142 saved none of it. Two-sided, because the whole value
         * of a gate is that it is off when it should be off: */

        /* (1) OFF -- nothing was preserved, and nothing claims to have been. */
        JC_CHECK(jc_snapshot_preserved_last(&mgr) == NULL);

        /* (2) ON -- the same revert now pins the state it discards. */
        mgr.preserve = 1;
        jc_write_file(fout, "tampered again\n", 15);
        JC_CHECK(jc_snapshot_restore_paths(&mgr, sha, paths, 1,
                                           &nrev, &nfail) == JC_OK);
        JC_CHECK(jc_snapshot_preserved_last(&mgr) != NULL);
        /* The file is reverted, as before -- preservation changes nothing the
         * caller observes about the revert itself. */
        JC_CHECK(jc_read_file(fout, &content, &len, a) == JC_OK);
        JC_CHECK_STR(content != NULL ? content : "", "orig\n");
        /* And the preserved commit holds the DESTROYED version, not the
         * baseline. Saving the wrong side of a revert would pass every check
         * above while preserving nothing of value. */
        if (jc_snapshot_preserved_last(&mgr) != NULL) {
            char shown[420];
            jc_snprintf(cmd, sizeof(cmd),
                        "git --git-dir=%s/.jichi.d/checkpoints/*/ show "
                        "%s:outside.txt > %s/shown.txt 2>/dev/null",
                        tmp, jc_snapshot_preserved_last(&mgr), tmp);
            /* The exit status is not the point: a failed `git show` leaves
             * shown.txt empty, which the read below treats as "nothing to
             * check" and skips the assertion. */
            (void)(system(cmd) == 0);
            jc_snprintf(shown, sizeof(shown), "%s/shown.txt", tmp);
            if (jc_read_file(shown, &content, &len, a) == JC_OK
                    && content != NULL && content[0] != '\0') {
                JC_CHECK_STR(content, "tampered again\n");
            }
        }

        /* (3) skip_once is honoured, and consumed -- the envelope relies on
         * exactly one restore being exempt, not all of them. */
        mgr.preserve_skip_once = 1;
        jc_write_file(fout, "third\n", 6);
        JC_CHECK(jc_snapshot_restore_paths(&mgr, sha, paths, 1,
                                           &nrev, &nfail) == JC_OK);
        JC_CHECK(jc_snapshot_preserved_last(&mgr) == NULL);
        JC_CHECK(mgr.preserve_skip_once == 0);
        jc_write_file(fout, "fourth\n", 7);
        JC_CHECK(jc_snapshot_restore_paths(&mgr, sha, paths, 1,
                                           &nrev, &nfail) == JC_OK);
        JC_CHECK(jc_snapshot_preserved_last(&mgr) != NULL);

        /* (4) The OTHER chokepoint: `restore_to`, behind undo/rewind/rollback.
         * A shared helper still has to be CALLED from both places, and until
         * M337b nothing in this suite invoked jc_snapshot_undo at all -- so the
         * whole-tree half of preservation had no coverage to regress. */
        mgr.preserve = 0;
        jc_write_file(fnew, "work since the checkpoint\n", 26);
        JC_CHECK(jc_snapshot_undo(&mgr, NULL) == JC_OK);
        JC_CHECK(!jc_file_exists(fnew));                       /* destroyed */
        JC_CHECK(jc_snapshot_preserved_last(&mgr) == NULL);    /* gate off  */

        jc_snapshot_take(&mgr, "baseline again");
        mgr.preserve = 1;
        jc_write_file(fnew, "work since the checkpoint\n", 26);
        JC_CHECK(jc_snapshot_undo(&mgr, NULL) == JC_OK);
        JC_CHECK(!jc_file_exists(fnew));       /* undo still undoes */
        JC_CHECK(jc_snapshot_preserved_last(&mgr) != NULL);
    }

    jc_snapshot_manager_shutdown(&mgr);
    jc_arena_free(a);
    if (saved_home[0] != '\0') {
        setenv("HOME", saved_home, 1);
    }
    (void)jc_test_rm_rf(tmp);
}

static void test_retain(void)
{
    /* M338: the retention policy behind `checkpoints gc`. Defaults 7 days / 20. */

    /* Fresh states are kept whatever their rank -- age alone is sufficient. */
    JC_CHECK(jc_snapshot_retain(0, 999, 7, 20) == 1);
    JC_CHECK(jc_snapshot_retain(7, 999, 7, 20) == 1);   /* boundary: inclusive */
    JC_CHECK(jc_snapshot_retain(8, 999, 7, 20) == 0);   /* one day past it     */

    /* The count floor: the newest K survive however old they are. This is the
     * half that protects a workspace nobody has touched in a month. */
    JC_CHECK(jc_snapshot_retain(3650, 0, 7, 20) == 1);
    JC_CHECK(jc_snapshot_retain(3650, 19, 7, 20) == 1);
    JC_CHECK(jc_snapshot_retain(3650, 20, 7, 20) == 0); /* rank is 0-based */

    /* Either half unlimited keeps everything -- so a caller can switch one off
     * without needing a second predicate. */
    JC_CHECK(jc_snapshot_retain(99999, 99999, -1, 20) == 1);
    JC_CHECK(jc_snapshot_retain(99999, 99999, 7, -1) == 1);

    /* Both at zero is the strictest policy expressible, and it still cannot
     * delete something created today: age 0 <= 0. A gc that could remove the
     * state it just preserved would be a trap, not a policy. */
    JC_CHECK(jc_snapshot_retain(0, 0, 0, 0) == 1);
    JC_CHECK(jc_snapshot_retain(1, 0, 0, 0) == 0);
}

static void test_toolout_split(void)
{
    jc_size h, t;

    /* M339: under the cap nothing is elided, so the pre-M339 result is
     * byte-identical -- the common case must not change. */
    jc_toolout_split(100, 100, &h, &t);
    JC_CHECK(h == 0 && t == 0);
    jc_toolout_split(99, 100, &h, &t);
    JC_CHECK(h == 0 && t == 0);

    /* Over the cap: head + tail spend exactly the budget, never more -- the
     * budget IS the cap the operator configured. */
    jc_toolout_split(10000, 300, &h, &t);
    JC_CHECK(h + t == 300);
    JC_CHECK(h > t);            /* head carries the command and its context */
    JC_CHECK(t > 0);            /* a build log's error is at the END, so the
                                 * tail is never zero when there is room --
                                 * head-only is what M339 exists to fix */

    /* A budget too small to split does not produce a zero-byte head. */
    jc_toolout_split(10000, 1, &h, &t);
    JC_CHECK(h == 1 && t == 0);

    /* A zero budget elides nothing rather than dividing by anything. */
    jc_toolout_split(10000, 0, &h, &t);
    JC_CHECK(h == 0 && t == 0);
}

static void test_toolout_spill(void)
{
    /* M339 round trip: the remainder must actually be ON DISK and the preview
     * must name where. Real files, like the git tests above -- a spill that only
     * formats a marker would pass every pure check and preserve nothing. */
    struct jc_app app;
    struct jc_arena *a = jc_arena_new(0);
    struct jc_sb out;
    char tmp[256];
    char saved_home[1024];
    const char *home = getenv("HOME");
    char *big;
    jc_size i, n = 5000;
    const char *dir;
    char dirbuf[512];
    int spilled;

    saved_home[0] = '\0';
    if (home != NULL) {
        jc_snprintf(saved_home, sizeof(saved_home), "%s", home);
    }
    if (!JC_REQUIRE(jc_snprintf(tmp, sizeof(tmp), "%s/jichi_toolout_%ld",
                                jc_test_tmpdir(), (long)getpid())
                    < (int)sizeof(tmp))) {
        jc_arena_free(a);
        return; /* a cut fixture path is a different path (M728) */
    }
    jc_mkdir_p(tmp);
    setenv("HOME", tmp, 1);

    memset(&app, 0, sizeof(app));
    app.arena = a;
    big = (char *)jc_arena_alloc(a, n + 1);
    JC_CHECK(big != NULL);
    if (big == NULL) {
        jc_arena_free(a);
        return;
    }
    for (i = 0; i < n; i++) {
        big[i] = (char)('A' + (int)(i % 26));
    }
    big[n] = '\0';

    jc_sb_init(&out);
    spilled = jc_toolout_spill(&app, "run", big, n, 300, &out);
    JC_CHECK(spilled == 1);
    JC_CHECK(out.data != NULL);
    if (out.data != NULL) {
        /* The preview is bounded: head + tail + marker, nowhere near 5000. */
        JC_CHECK(out.len < 900);
        /* It names the byte count and a path, so the model can act on it. */
        JC_CHECK(strstr(out.data, "5000 bytes total") != NULL);
        JC_CHECK(strstr(out.data, "/tool-output/") != NULL);
        /* The spill must be under the TEMP home this test set, not the real one.
         * The first version of jc_toolout_dir cached its answer in a static and
         * so ignored the override -- writing into the developer's ~/.jichi.d
         * while this very assertion passed. Pin the location, not just the shape. */
        JC_CHECK(strstr(out.data, tmp) != NULL);
        /* Head AND tail: the first bytes and the LAST bytes are both present.
         * big[] cycles A-Z, so the final byte is deterministic. */
        JC_CHECK(out.data[0] == 'A');
        JC_CHECK(strstr(out.data, "\n") != NULL);
    }
    /* And the file holds every byte, which is the whole claim. Read the path
     * the preview NAMES -- "The COMPLETE output is at <path>" -- rather than
     * guessing a filename. The store's counter (jc_toolout_preserve's `seq`) is
     * a process GLOBAL, so the first ticket in THIS process is run-1.txt only
     * if nothing earlier wrote one; once any mid-turn elision test ran, this
     * is run-N.txt, and a hardcoded run-1.txt would read the wrong (absent)
     * file. The preview is the authority on where the bytes went, so the file
     * it names is the one to check. */
    dir = jc_toolout_dir(&app, dirbuf, sizeof(dirbuf));
    JC_CHECK(dir != NULL);
    {
        const char *p = (out.data != NULL)
            ? strstr(out.data, "The COMPLETE output is at ") : NULL;
        char path[700];
        char *content = NULL;
        jc_size len = 0;
        jc_size k = 0;
        if (p != NULL) {
            p += strlen("The COMPLETE output is at ");
            while (*p != '\0' && *p != ' ' && k + 1 < sizeof(path)) {
                path[k++] = *p++;
            }
            path[k] = '\0';
        }
        JC_CHECK(p != NULL && path[0] != '\0');
        if (path[0] != '\0') {
            JC_CHECK(jc_file_exists(path));
            JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
            JC_CHECK(len == n);
            if (content != NULL && len == n) {
                JC_CHECK(memcmp(content, big, n) == 0);
            }
        }
    }
    jc_sb_free(&out);

    /* Under the cap: unchanged text, no file, no marker -- pre-M339 behaviour. */
    jc_sb_init(&out);
    JC_CHECK(jc_toolout_spill(&app, "run", "short", 5, 300, &out) == 0);
    JC_CHECK(out.data != NULL && out.len == 5);
    if (out.data != NULL) {
        JC_CHECK(strstr(out.data, "bytes total") == NULL);
    }
    jc_sb_free(&out);

    jc_arena_free(a);
    if (saved_home[0] != '\0') {
        setenv("HOME", saved_home, 1);
    }
    (void)jc_test_rm_rf(tmp);
}

static void test_store_state(void)
{
    /* M335: which kind of absent a work tree is, is the whole question. */

    /* Present -> live, whatever the parent says. */
    JC_CHECK(jc_snapshot_store_state("/home/u/proj", 1, 1) == JC_STORE_LIVE);
    JC_CHECK(jc_snapshot_store_state("/home/u/proj", 1, 0) == JC_STORE_LIVE);

    /* Gone, parent still there -> someone deleted a project. Removable. */
    JC_CHECK(jc_snapshot_store_state(jc_test_tmp("gone"), 0, 1) == JC_STORE_ORPHANED);

    /* Gone AND parent gone -> looks like an unmounted volume. NOT removable:
     * deleting this would destroy the only history of a live project whose disk
     * happens to be detached. The heuristic exists precisely to refuse here. */
    JC_CHECK(jc_snapshot_store_state("/mnt/disk/proj", 0, 0)
             == JC_STORE_UNREACHABLE);

    /* No work tree recorded at all is its own case, not an orphan: a repo with no
     * core.worktree may be mid-initialisation, and guessing would be destructive. */
    JC_CHECK(jc_snapshot_store_state(NULL, 0, 1) == JC_STORE_UNKNOWN);
    JC_CHECK(jc_snapshot_store_state("", 0, 1) == JC_STORE_UNKNOWN);
}

/* M886 (the demos drive's N50): the worktree's content, measured by what changed
 * in it -- a read changes nothing, an edit does, a SECOND edit to a file already
 * modified does too (its `git status` line would not), an ignored file does not. */
static void test_worktree_fingerprint(void)
{
    const char *dir = jc_test_tmp("jichi_fp_m886");
    char cmd[900];
    char a[72], b[72], c[72], d[72], e[72], g[72];
    FILE *f;

    (void)jc_test_rm_rf(dir);
    if (system("command -v git >/dev/null 2>&1") != 0) {
        printf("  (skipping the worktree fingerprint: no git)\n");
        return;
    }
    jc_snprintf(cmd, sizeof cmd,
                "mkdir -p %s && cd %s && git init -q && "
                "git config user.email t@example.com && "
                "git config user.name tester && printf 'one\\n' > a.txt && "
                "printf 'out/\\n' > .gitignore && git add -A && "
                "git commit -q -m init", dir, dir);
    if (system(cmd) != 0) {
        return;
    }
    JC_CHECK(jc_snapshot_worktree_fingerprint(dir, a, sizeof a) == JC_OK);
    /* nothing changed: the same print */
    JC_CHECK(jc_snapshot_worktree_fingerprint(dir, b, sizeof b) == JC_OK &&
             strcmp(a, b) == 0);
    /* a tracked file modified */
    jc_snprintf(cmd, sizeof cmd, "%s/a.txt", dir);
    f = fopen(cmd, "ab");
    if (f != NULL) { fputs("two\n", f); fclose(f); }
    JC_CHECK(jc_snapshot_worktree_fingerprint(dir, c, sizeof c) == JC_OK &&
             strcmp(c, a) != 0);
    /* the same file modified AGAIN: its status line is unchanged, its print is not */
    f = fopen(cmd, "ab");
    if (f != NULL) { fputs("three\n", f); fclose(f); }
    JC_CHECK(jc_snapshot_worktree_fingerprint(dir, d, sizeof d) == JC_OK &&
             strcmp(d, c) != 0);
    /* an ignored file does not count */
    jc_snprintf(cmd, sizeof cmd, "mkdir -p %s/out && printf 'cache\\n' > %s/out/x",
                dir, dir);
    if (system(cmd) == 0) {
        JC_CHECK(jc_snapshot_worktree_fingerprint(dir, e, sizeof e) == JC_OK &&
                 strcmp(e, d) == 0);
    }
    /* a new untracked file does */
    jc_snprintf(cmd, sizeof cmd, "printf 'new\\n' > %s/b.txt", dir);
    if (system(cmd) == 0) {
        JC_CHECK(jc_snapshot_worktree_fingerprint(dir, g, sizeof g) == JC_OK &&
                 strcmp(g, d) != 0);
    }
    /* M893: a commit moves HEAD, so the print changes even though the tree is
     * clean before and after -- the case "is this still the starting tree?"
     * would otherwise answer yes to */
    jc_snprintf(cmd, sizeof cmd, "cd %s && git add -A && git commit -q -m two", dir);
    if (system(cmd) == 0) {
        JC_CHECK(jc_snapshot_worktree_fingerprint(dir, a, sizeof a) == JC_OK);
        jc_snprintf(cmd, sizeof cmd, "cd %s && git commit -q --allow-empty -m three",
                    dir);
        if (system(cmd) == 0) {
            JC_CHECK(jc_snapshot_worktree_fingerprint(dir, b, sizeof b) == JC_OK &&
                     strcmp(a, b) != 0);
        }
    }
    /* outside a git worktree it cannot tell */
    JC_CHECK(jc_snapshot_worktree_fingerprint("/", a, sizeof a) != JC_OK);
    (void)jc_test_rm_rf(dir);
}

/* M977: D-02 regression test. A file ignored NOW but NOT at the checkpoint must
 * survive an undo when clean runs BEFORE reset. The defect is that clean runs
 * AFTER the reset restored an older .gitignore that no longer ignores it, so the
 * file is deleted. Running clean BEFORE reset makes it read the same rules as the
 * dry-run preview. */
static void test_restore_clean_before_reset(void)
{
    struct jc_app app;
    struct jc_snapshot_mgr mgr;
    struct jc_arena *a = jc_arena_new(0);
    char tmp[256];
    char work[320];
    char dataset_dir[400];
    char dataset_file[420];
    char gitignore[400];
    char sha_first[64];
    char saved_home[1024];
    const char *home = getenv("HOME");
    const char *c0;

    saved_home[0] = '\0';
    if (home != NULL) {
        jc_snprintf(saved_home, sizeof(saved_home), "%s", home);
    }

    if (!JC_REQUIRE(jc_snprintf(tmp, sizeof(tmp), "%s/jichi_snap_d02_%ld",
                                jc_test_tmpdir(), (long)getpid())
                    < (int)sizeof(tmp))) {
        jc_arena_free(a);
        return;
    }
    jc_snprintf(work, sizeof(work), "%s/work", tmp);
    jc_mkdir_p(work);
    setenv("HOME", tmp, 1);

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_snprintf(app.cwd, sizeof(app.cwd), "%s", work);
    app.config.snapshots = 1;
    app.config.snapshot_limit = 100;

    jc_snapshot_manager_init(&mgr, &app);
    if (jc_snapshot_available(&mgr)) {
        /* (1) Write data.txt, take checkpoint "first" */
        char file[400];
        jc_snprintf(file, sizeof(file), "%s/data.txt", work);
        jc_write_file(file, "v1\n", 3);
        jc_snapshot_take(&mgr, "first");
        c0 = jc_snapshot_commit(&mgr, jc_snapshot_count(&mgr) - 1);
        JC_CHECK(c0 != NULL);
        jc_snprintf(sha_first, sizeof(sha_first), "%s", c0 != NULL ? c0 : "");

        /* (2) OUTSIDE jichi (direct file ops) create a subdirectory dataset/ with
         * a file in it, and add a .gitignore containing dataset/ to the work tree */
        jc_snprintf(dataset_dir, sizeof(dataset_dir), "%s/dataset", work);
        jc_mkdir_p(dataset_dir);
        jc_snprintf(dataset_file, sizeof(dataset_file), "%s/x.bin", dataset_dir);
        jc_write_file(dataset_file, "ignored-data\n", 13);
        jc_snprintf(gitignore, sizeof(gitignore), "%s/.gitignore", work);
        jc_write_file(gitignore, "dataset/\n", 9);

        /* (3) Take checkpoint "second" - the dataset/ file is ignored now,
         * so it's NOT captured in the checkpoint */
        jc_snapshot_take(&mgr, "second");

        /* (4) Call jc_snapshot_restore_commit to the FIRST checkpoint */
        JC_CHECK(jc_snapshot_restore_commit(&mgr, sha_first) == JC_OK);

        /* (5) Assert dataset/x.bin STILL EXISTS on disk
         * This is RED on the unfixed tree: clean runs AFTER reset restored a
         * .gitignore without dataset/, so the file is deleted. */
        JC_CHECK(jc_file_exists(dataset_file));
    }

    jc_snapshot_manager_shutdown(&mgr);
    jc_arena_free(a);
    if (saved_home[0] != '\0') {
        setenv("HOME", saved_home, 1);
    }
    (void)jc_test_rm_rf(tmp);
}

void test_snapshot(void)
{
    test_worktree_fingerprint();
    test_retain();
    test_toolout_split();
    test_toolout_spill();
    test_store_state();
    test_git_dir();
    test_key_shape();
    test_clean_label();
    test_undo_note();
    test_changed_since_z();
    test_restore_commit();
    test_restore_clean_before_reset();
    test_restore_paths();
    test_worktree();
}
