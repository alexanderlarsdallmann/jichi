/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_board.c - the kanban board pure ops (jc_board, #7). */

#include "jc_test.h"
#include "jc_board.h"
#include "cJSON.h"
#include "jc_str.h"
#include "jc_mem.h"
#include "jc_snprintf.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

void test_board(void)
{
    /* state parsing + words. */
    JC_CHECK(jc_board_state_from_str("todo") == 0);
    JC_CHECK(jc_board_state_from_str("pending") == 0);
    JC_CHECK(jc_board_state_from_str("doing") == 1);
    JC_CHECK(jc_board_state_from_str("in_progress") == 1);
    JC_CHECK(jc_board_state_from_str("done") == 2);
    JC_CHECK(jc_board_state_from_str("bogus") == -1);
    JC_CHECK(strcmp(jc_board_state_word(0), "todo") == 0);
    JC_CHECK(strcmp(jc_board_state_word(1), "doing") == 0);
    JC_CHECK(strcmp(jc_board_state_word(2), "done") == 0);

    /* add / move / remove + ids. */
    {
        struct jc_board b;
        int id1, id2, id3;
        struct jc_sb sb;
        jc_board_init(&b);
        id1 = jc_board_add(&b, "design api", "design", NULL);
        id2 = jc_board_add(&b, "implement", "implementation", "the hard part");
        id3 = jc_board_add(&b, "tests", "testing", NULL);
        JC_CHECK(id1 == 1 && id2 == 2 && id3 == 3);
        JC_CHECK(b.cards.len == 3);
        /* add with no title is rejected. */
        JC_CHECK(jc_board_add(&b, "", "x", NULL) == 0);
        JC_CHECK(jc_board_add(&b, NULL, "x", NULL) == 0);

        /* move id2 -> doing; unknown id fails. */
        JC_CHECK(jc_board_move(&b, id2, 1) == 1);
        JC_CHECK(jc_board_move(&b, 999, 1) == 0);
        JC_CHECK(jc_board_move(&b, id2, 5) == 0); /* bad state */

        /* render shows columns + the moved card under DOING. */
        jc_board_set_active_phase(&b, "implementation");
        jc_sb_init(&sb);
        jc_board_render(&b, &sb);
        JC_CHECK(strstr(sb.data, "Active phase: implementation") != NULL);
        JC_CHECK(strstr(sb.data, "DOING") != NULL);
        JC_CHECK(strstr(sb.data, "implement") != NULL);
        JC_CHECK(strstr(sb.data, "the hard part") != NULL);
        jc_sb_free(&sb);

        /* focus block surfaces the in-progress card. */
        jc_sb_init(&sb);
        jc_board_render_focus(&b, &sb);
        JC_CHECK(strstr(sb.data, "In progress:") != NULL);
        JC_CHECK(strstr(sb.data, "implement") != NULL);
        jc_sb_free(&sb);

        /* M932 (the branch's M899, N37): what the board tool returns. One card done, so the
         * open render must count it and not show its title. */
        JC_CHECK(jc_board_move(&b, id3, 2) == 1);
        jc_sb_init(&sb);
        jc_board_render_counts(&b, &sb);
        JC_CHECK(strcmp(sb.data, "Board: 1 todo, 1 doing, 1 done; active "
                                 "phase: implementation\n") == 0);
        jc_sb_free(&sb);
        jc_sb_init(&sb);
        JC_CHECK(jc_board_render_card(&b, id2, &sb) == 1);
        JC_CHECK(strcmp(sb.data, "[2] implement  (doing, implementation) -- "
                                 "the hard part\n") == 0);
        jc_sb_free(&sb);
        jc_sb_init(&sb);
        JC_CHECK(jc_board_render_card(&b, 999, &sb) == 0);
        JC_CHECK(sb.len == 0);
        jc_sb_free(&sb);
        jc_sb_init(&sb);
        jc_board_render_open(&b, &sb);
        JC_CHECK(strstr(sb.data, "design api") != NULL);   /* todo, shown */
        JC_CHECK(strstr(sb.data, "implement") != NULL);    /* doing, shown */
        JC_CHECK(strstr(sb.data, "tests") == NULL);        /* done, counted */
        JC_CHECK(strstr(sb.data, "## DONE\n  1 card (") != NULL);
        jc_sb_free(&sb);
        JC_CHECK(jc_board_move(&b, id3, 0) == 1);

        /* M932 (the branch's M900): the JSON view adds the counts; the file's shape does not. */
        {
            cJSON *v = jc_board_to_json(&b, 1);
            cJSON *f = jc_board_to_json(&b, 0);
            cJSON *cnt = cJSON_GetObjectItem(v, "counts");
            cJSON *c1 = cJSON_GetArrayItem(cJSON_GetObjectItem(v, "cards"), 1);
            JC_CHECK(cnt != NULL);
            JC_CHECK(cJSON_GetObjectItem(cnt, "todo")->valuedouble == 2.0);
            JC_CHECK(cJSON_GetObjectItem(cnt, "doing")->valuedouble == 1.0);
            JC_CHECK(cJSON_GetObjectItem(cnt, "done")->valuedouble == 0.0);
            JC_CHECK(strcmp(cJSON_GetObjectItem(c1, "state")->valuestring,
                            "doing") == 0);
            JC_CHECK(strcmp(cJSON_GetObjectItem(c1, "note")->valuestring,
                            "the hard part") == 0);
            JC_CHECK(cJSON_GetObjectItem(f, "counts") == NULL);
            JC_CHECK(strcmp(cJSON_GetObjectItem(f, "phase")->valuestring,
                            "implementation") == 0);
            cJSON_Delete(v);
            cJSON_Delete(f);
        }
        {
            struct jc_board e0;
            cJSON *v;
            jc_board_init(&e0);
            v = jc_board_to_json(&e0, 1);
            JC_CHECK(cJSON_IsNull(cJSON_GetObjectItem(v, "phase")));
            JC_CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(v, "cards")) == 0);
            cJSON_Delete(v);
            v = jc_board_to_json(&e0, 0);
            JC_CHECK(cJSON_GetObjectItem(v, "phase") == NULL);
            cJSON_Delete(v);
            jc_board_free(&e0);
        }

        /* remove the middle card; the others survive with intact titles. */
        JC_CHECK(jc_board_remove(&b, id2) == 1);
        JC_CHECK(jc_board_remove(&b, id2) == 0); /* already gone */
        JC_CHECK(b.cards.len == 2);
        {
            struct jc_board_card *c0 =
                (struct jc_board_card *)jc_vec_at(&b.cards, 0);
            struct jc_board_card *c1 =
                (struct jc_board_card *)jc_vec_at(&b.cards, 1);
            JC_CHECK(strcmp(c0->title, "design api") == 0);
            JC_CHECK(strcmp(c1->title, "tests") == 0);
        }
        jc_board_free(&b);
    }

    /* M761: jc_board_edit's contract, in one process. The cross-process property
     * -- another writer's card survives -- is board_concurrent.sh's. Here: an
     * edit reads the FILE, not the struct it is handed (a card planted on disk
     * behind its back is kept, and the id after it is used); a refused edit
     * returns 0 and leaves the file alone; and an edit into a board that cannot
     * be read returns JC_BOARD_SAVE_FAILED, never 1, with the file untouched. */
    {
        char dir[512];
        char bpath[600];
        struct jc_board b;
        struct jc_board_edit e;
        char *text = NULL;
        struct jc_arena *ta = jc_arena_new(0);

        jc_snprintf(dir, sizeof(dir), "%s", jc_test_tmp("jichi_board_edit_test"));
        (void)jc_test_rm_rf(dir);
        jc_snprintf(bpath, sizeof(bpath), "%s/.jichi", dir);
        jc_mkdir_p(bpath);
        jc_snprintf(bpath, sizeof(bpath), "%s/.jichi/board.json", dir);
        {
            const char *planted =
                "{\"cards\":[{\"id\":7,\"title\":\"planted\",\"state\":\"todo\"}]}";
            jc_write_file(bpath, planted, (jc_size)strlen(planted));
        }

        jc_board_init(&b);   /* empty in memory: the edit must not trust it */
        memset(&e, 0, sizeof(e));
        e.kind = JC_BOARD_ADD;
        e.title = "added";
        JC_CHECK(jc_board_edit(&b, dir, &e) == 1);
        JC_CHECK(e.id == 8);
        JC_CHECK(b.cards.len == 2);

        memset(&e, 0, sizeof(e));
        e.kind = JC_BOARD_MOVE;
        e.id = 99;
        e.state = 2;
        JC_CHECK(jc_board_edit(&b, dir, &e) == 0);

        jc_write_file(bpath, "{ not json", (jc_size)strlen("{ not json"));
        memset(&e, 0, sizeof(e));
        e.kind = JC_BOARD_ADD;
        e.title = "into damage";
        JC_CHECK(jc_board_edit(&b, dir, &e) == JC_BOARD_SAVE_FAILED);
        JC_CHECK(ta != NULL && jc_read_file(bpath, &text, NULL, ta) == JC_OK &&
                 text != NULL && strcmp(text, "{ not json") == 0);

        /* ...and once the file is repaired, the next edit is not refused: the
         * reload starts from a clean flag, not the last load's. */
        jc_write_file(bpath, "{\"cards\":[]}", (jc_size)strlen("{\"cards\":[]}"));
        memset(&e, 0, sizeof(e));
        e.kind = JC_BOARD_ADD;
        e.title = "after the repair";
        JC_CHECK(jc_board_edit(&b, dir, &e) == 1);

        jc_board_free(&b);
        jc_arena_free(ta);
        (void)jc_test_rm_rf(dir);
    }

    /* M997-follow-up: board edit through symlinked .jichi must be BLOCKED.
     * Create an outside directory (pre-existing) and symlink .jichi to it.
     * jc_board_edit must refuse AND no board.lock or board.json may be created
     * in the outside directory. */
    {
        char dir[512];
        char outside[512];
        char bpath[600];
        char lpath[600];
        struct jc_board b;
        struct jc_board_edit e;
        int r;
        struct jc_arena *ta = jc_arena_new(0);

        /* Create the "outside" directory first - this is where the symlink points.
         * It must be a separate path under TMPDIR, NOT inside the workspace dir. */
        jc_snprintf(outside, sizeof(outside), "%s", jc_test_tmp("jichi_board_outside"));
        (void)jc_test_rm_rf(outside);
        if (mkdir(outside, 0755) != 0) {
            jc_arena_free(ta);
            return;
        }

        /* Create the workspace directory */
        jc_snprintf(dir, sizeof(dir), "%s", jc_test_tmp("jichi_board_symlink_test"));
        (void)jc_test_rm_rf(dir);
        if (mkdir(dir, 0755) != 0) {
            jc_test_rm_rf(outside);
            jc_arena_free(ta);
            return;
        }

        /* Create .jichi as a symlink to outside */
        {
            char jichi[600];
            jc_snprintf(jichi, sizeof(jichi), "%s/.jichi", dir);
            if (symlink(outside, jichi) != 0) {
                jc_test_rm_rf(outside);
                jc_test_rm_rf(dir);
                jc_arena_free(ta);
                return;
            }
        }

        jc_board_init(&b);
        memset(&e, 0, sizeof(e));
        e.kind = JC_BOARD_ADD;
        e.title = "added via symlink";
        r = jc_board_edit(&b, dir, &e);

        /* (1) The edit must be refused: JC_BOARD_SAVE_FAILED */
        JC_CHECK(r == JC_BOARD_SAVE_FAILED);

        /* (2) No board.lock must be created in outside dir */
        jc_snprintf(lpath, sizeof(lpath), "%s/board.lock", outside);
        if (access(lpath, F_OK) == 0) {
            JC_CHECK(0);  /* lock file should NOT exist */
        }

        /* (3) No board.json must be created in outside dir */
        jc_snprintf(bpath, sizeof(bpath), "%s/board.json", outside);
        if (access(bpath, F_OK) == 0) {
            JC_CHECK(0);  /* board.json should NOT exist */
        }

        /* Control: with a real .jichi dir, the same edit should succeed */
        {
            char dir2[512];
            char jichi2[600];
            char bpath2[600];
            int r2;
            jc_snprintf(dir2, sizeof(dir2), "%s", jc_test_tmp("jichi_board_real_test"));
            (void)jc_test_rm_rf(dir2);
            if (mkdir(dir2, 0755) != 0) {
                jc_test_rm_rf(outside);
                jc_test_rm_rf(dir);
                jc_arena_free(ta);
                return;
            }
            jc_snprintf(jichi2, sizeof(jichi2), "%s/.jichi", dir2);
            jc_mkdir_p(jichi2);
            jc_snprintf(bpath2, sizeof(bpath2), "%s/board.json", jichi2);
            jc_board_init(&b);
            memset(&e, 0, sizeof(e));
            e.kind = JC_BOARD_ADD;
            e.title = "added to real dir";
            r2 = jc_board_edit(&b, dir2, &e);
            /* Control: edit should succeed */
            JC_CHECK(r2 == 1);
            /* board.json should exist */
            JC_CHECK(access(bpath2, F_OK) == 0);
            jc_board_free(&b);
            jc_test_rm_rf(dir2);
        }

        jc_board_free(&b);
        jc_test_rm_rf(outside);
        jc_test_rm_rf(dir);
        jc_arena_free(ta);
    }

    /* M1003: jc_board_load through symlinked .jichi/board.json must be BLOCKED.
     * Create an outside directory with a real board.json, symlink it into
     * dir/.jichi/board.json, and verify that jc_board_load refuses (JC_ERR_DENIED,
     * load_failed=1, zero cards) while the outside file remains unchanged.
     * The control arm (real dir, real board.json) should load successfully. */
    {
        char dir[512];
        char outside[512];
        char bpath[600];
        struct jc_board b;
        jc_status st;
        struct jc_arena *ta = jc_arena_new(0);
        char *outside_content = NULL;

        /* The outside directory holds the real board.json that the symlink points to. */
        jc_snprintf(outside, sizeof(outside), "%s", jc_test_tmp("jichi_board_load_outside"));
        (void)jc_test_rm_rf(outside);
        if (mkdir(outside, 0755) != 0) {
            jc_arena_free(ta);
            return;
        }

        /* Create the workspace directory */
        jc_snprintf(dir, sizeof(dir), "%s", jc_test_tmp("jichi_board_load_symlink_test"));
        (void)jc_test_rm_rf(dir);
        if (mkdir(dir, 0755) != 0) {
            jc_test_rm_rf(outside);
            jc_arena_free(ta);
            return;
        }

        /* Create the outside board.json with a valid card. */
        jc_snprintf(bpath, sizeof(bpath), "%s/board.json", outside);
        {
            const char *planted =
                "{\"cards\":[{\"id\":42,\"title\":\"outside card\",\"state\":\"todo\"}]}";
            jc_write_file(bpath, planted, (jc_size)strlen(planted));
        }

        /* Store the outside file content for later byte-for-byte comparison. */
        {
            if (jc_read_file(bpath, &outside_content, NULL, ta) != JC_OK ||
                outside_content == NULL) {
                outside_content = NULL;
            }
        }

        /* Create .jichi as a real directory, then symlink board.json inside it. */
        {
            char jichi[600];
            char linkpath[600];
            jc_snprintf(jichi, sizeof(jichi), "%s/.jichi", dir);
            if (mkdir(jichi, 0755) != 0) {
                jc_test_rm_rf(outside);
                jc_test_rm_rf(dir);
                jc_arena_free(ta);
                return;
            }
            jc_snprintf(linkpath, sizeof(linkpath), "%s/board.json", jichi);
            if (symlink(bpath, linkpath) != 0) {
                /* Symlink not supported on this platform; skip the symlink arm,
                 * but still run the control arm below. */
                (void)jc_test_rm_rf(jichi);
            } else {
                /* Symlink created successfully: test the blocked path. */
                jc_board_init(&b);
                st = jc_board_load(&b, dir);

                /* (1) Load must be refused: JC_ERR_DENIED */
                JC_CHECK(st == JC_ERR_DENIED);

                /* (2) load_failed must be set */
                JC_CHECK(b.load_failed == 1);

                /* (3) The board must have zero cards (noParse due to symlink) */
                JC_CHECK(b.cards.len == 0);

                jc_board_free(&b);

                /* (4) control: jc_board_load is read-only, so this cannot fail on its own; the teeth for the read refusal are check (3) (zero cards parsed through the link) and the JC_ERR_DENIED/load_failed assertions. Kept as a control so a future writer in the load path is caught. */
                {
                    char *current = NULL;
                    struct jc_arena *cta = jc_arena_new(0);
                    if (cta && jc_read_file(bpath, &current, NULL, cta) == JC_OK &&
                        current != NULL && outside_content != NULL) {
                        JC_CHECK(strcmp(current, outside_content) == 0);
                    }
                    if (cta) jc_arena_free(cta);
                }
            }
        }

        /* Control: with a real (non-symlinked) board.json, the load should succeed. */
        {
            char dir2[512];
            char jichi2[600];
            char bpath2[600];
            jc_snprintf(dir2, sizeof(dir2), "%s", jc_test_tmp("jichi_board_load_real_test"));
            (void)jc_test_rm_rf(dir2);
            if (mkdir(dir2, 0755) == 0) {
                jc_snprintf(jichi2, sizeof(jichi2), "%s/.jichi", dir2);
                if (mkdir(jichi2, 0755) == 0) {
                    jc_snprintf(bpath2, sizeof(bpath2), "%s/board.json", jichi2);
                    /* Copy the outside board.json to the real location. */
                    {
                        const char *planted =
                            "{\"cards\":[{\"id\":99,\"title\":\"real card\",\"state\":\"doing\",\"phase\":\"test\"}]}";
                        jc_write_file(bpath2, planted, (jc_size)strlen(planted));
                    }
                    jc_board_init(&b);
                    st = jc_board_load(&b, dir2);
                    /* Control: load should succeed with the card visible. */
                    JC_CHECK(st == JC_OK);
                    JC_CHECK(b.load_failed == 0);
                    JC_CHECK(b.cards.len == 1);
                    {
                        struct jc_board_card *c =
                            (struct jc_board_card *)jc_vec_at(&b.cards, 0);
                        JC_CHECK(strcmp(c->title, "real card") == 0);
                        JC_CHECK(c->id == 99);
                        JC_CHECK(c->state == 1); /* doing */
                        JC_CHECK(strcmp(c->phase, "test") == 0);
                    }
                    jc_board_free(&b);
                }
                (void)jc_test_rm_rf(dir2);
            }
        }

        jc_test_rm_rf(outside);
        jc_test_rm_rf(dir);
        jc_arena_free(ta);
    }
}
