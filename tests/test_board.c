/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_board.c - the kanban board pure ops (jc_board, #7). */

#include "jc_test.h"
#include "jc_board.h"
#include "jc_str.h"

#include <string.h>

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
}
