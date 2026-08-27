/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_prefix.c - the prompt-cache prefix sentinel (M365).
 *
 * The watch must be slow to accuse: ONE changed build is normal life (a
 * memory write, a mode switch, midnight moving the date line) -- only a hash
 * that changes on JC_PREFIX_CHURN_STREAK consecutive builds is churn,
 * because no legitimate cause fires every turn. And it fires ONCE: churn
 * persists, and a bell per turn buries the finding (the M323 rule). */

#include "jc_test.h"
#include "jc_prefix.h"

#include <string.h>

void test_prefix_watch(void)
{
    struct jc_prefix_watch w;

    /* --- the hash ---------------------------------------------------------- */
    JC_CHECK(jc_prefix_hash("a") != jc_prefix_hash("b"));
    JC_CHECK(jc_prefix_hash("prompt") == jc_prefix_hash("prompt"));
    JC_CHECK(jc_prefix_hash(NULL) == jc_prefix_hash(""));

    /* --- prime + stable: never fires --------------------------------------- */
    memset(&w, 0, sizeof(w));
    JC_CHECK(jc_prefix_watch_track(&w, 100UL) == 0); /* prime            */
    JC_CHECK(jc_prefix_watch_track(&w, 100UL) == 0); /* stable           */
    JC_CHECK(jc_prefix_watch_track(&w, 100UL) == 0);
    JC_CHECK(w.streak == 0);

    /* --- one change, then stable: normal life, no fire --------------------- */
    JC_CHECK(jc_prefix_watch_track(&w, 200UL) == 0); /* streak 1         */
    JC_CHECK(jc_prefix_watch_track(&w, 200UL) == 0); /* stable: resets   */
    JC_CHECK(w.streak == 0);

    /* --- two changes, then stable: still no fire ---------------------------- */
    JC_CHECK(jc_prefix_watch_track(&w, 300UL) == 0); /* streak 1         */
    JC_CHECK(jc_prefix_watch_track(&w, 400UL) == 0); /* streak 2         */
    JC_CHECK(jc_prefix_watch_track(&w, 400UL) == 0); /* reset            */

    /* --- three consecutive changes: fires exactly once ---------------------- */
    JC_CHECK(jc_prefix_watch_track(&w, 500UL) == 0); /* streak 1         */
    JC_CHECK(jc_prefix_watch_track(&w, 600UL) == 0); /* streak 2         */
    JC_CHECK(jc_prefix_watch_track(&w, 700UL) == 1); /* streak 3: FIRE   */
    JC_CHECK(jc_prefix_watch_track(&w, 800UL) == 0); /* warned: silent   */
    JC_CHECK(jc_prefix_watch_track(&w, 900UL) == 0);

    /* --- a hash that happens to be 0 is a value, not "unprimed" ------------- */
    memset(&w, 0, sizeof(w));
    JC_CHECK(jc_prefix_watch_track(&w, 0UL) == 0);   /* prime with 0     */
    JC_CHECK(jc_prefix_watch_track(&w, 0UL) == 0);   /* stable           */
    JC_CHECK(w.streak == 0);
    JC_CHECK(jc_prefix_watch_track(&w, 1UL) == 0);   /* change: streak 1 */
    JC_CHECK(w.streak == 1);

    /* --- NULL watch: no crash ------------------------------------------------ */
    JC_CHECK(jc_prefix_watch_track(NULL, 1UL) == 0);
}
