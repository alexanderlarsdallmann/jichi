#!/bin/sh
# Grader for task 79: the same fix made twice -- the vector kept honest by
# holding an INDEX, and a linked list whose nodes have stable addresses and an
# O(1) splice -- both under AddressSanitizer, because the defect here is one
# ASan sees on the spot: a pointer held across a realloc.
#
# WHAT IS AND IS NOT CHECKED. Both fixes must exist and both must be ASan- and
# LeakSanitizer-clean under a probe that grows the array far past any reserve
# (200,000 adds), re-reads the captain, walks the list, moves a node between
# lists and checks its ADDRESS did not change, removes nodes while walking with
# `next` saved first, and frees everything. What it cannot check: which of the
# two the learner's own program should use. That is the brief's question, and
# the answer depends on what their use-case is buying -- locality or stable
# addresses -- which no probe can know.
cd "$(dirname "$0")" || exit 1
trap 'rm -rf vecprobe listprobe _vec.c _list.c _err.txt' EXIT
cc --version >/dev/null 2>&1 || { echo "CANNOT RUN: a C compiler (cc) is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 77; }
CC=${CC:-cc}
SAN="-std=c89 -pedantic -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

# --- fix 1: the vector, holding an index -------------------------------------------
cat > _vec.c <<'PRB'
#include "team_vec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANY 200000L

int main(void)
{
    struct team_vec *t = team_vec_new();
    struct player *p;
    long i;
    char name[32];

    if (t == NULL) return 21;
    if (team_vec_add(t, "ada") || team_vec_add(t, "grace") ||
        team_vec_add(t, "linus") || team_vec_add(t, "dennis")) { team_vec_free(t); return 22; }
    if (team_vec_set_captain(t, 1) != 0) { team_vec_free(t); return 23; }
    p = team_vec_captain(t);
    if (p == NULL || strcmp(p->name, "grace") != 0) { team_vec_free(t); return 24; }
    p->goals = 7;

    /* Grow the array far past any reserve a reasonable implementation would
       pre-allocate: the buffer WILL move. */
    for (i = 0; i < MANY; i++) {
        sprintf(name, "p%ld", i);
        if (team_vec_add(t, name) != 0) { team_vec_free(t); return 22; }
    }
    if (team_vec_count(t) != (size_t)(MANY + 4)) { team_vec_free(t); return 25; }

    /* The captain must still be grace, with her 7 goals, read from wherever
       the array lives NOW. A stored pointer reads freed memory here (ASan). */
    p = team_vec_captain(t);
    if (p == NULL) { team_vec_free(t); return 26; }
    if (strcmp(p->name, "grace") != 0 || p->goals != 7) { team_vec_free(t); return 27; }
    if (team_vec_at(t, 1) != p) { team_vec_free(t); return 28; }
    if (team_vec_at(t, MANY + 4) != NULL) { team_vec_free(t); return 29; }
    if (team_vec_set_captain(t, MANY + 4) == 0) { team_vec_free(t); return 30; }
    team_vec_free(t);
    return 0;
}
PRB

# --- fix 2: the list, stable addresses and an O(1) move ---------------------------------
cat > _list.c <<'PRB'
#include "team_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANY 200000L

int main(void)
{
    struct team_list *a = team_list_new();
    struct team_list *b = team_list_new();
    struct team_node *cap, *n, *next;
    struct player *p;
    long i, seen;
    char name[32];

    if (a == NULL || b == NULL) return 41;
    if (team_list_add(a, "ada") == NULL) { team_list_free(a); team_list_free(b); return 42; }
    cap = team_list_add(a, "grace");
    if (cap == NULL) { team_list_free(a); team_list_free(b); return 42; }
    if (team_list_add(a, "linus") == NULL) { team_list_free(a); team_list_free(b); return 42; }
    team_node_player(cap)->goals = 7;

    /* 1. stable addresses: the node survives 200,000 later adds unchanged. */
    for (i = 0; i < MANY; i++) {
        sprintf(name, "p%ld", i);
        if (team_list_add(a, name) == NULL) { team_list_free(a); team_list_free(b); return 42; }
    }
    if (team_list_count(a) != (size_t)(MANY + 3)) { team_list_free(a); team_list_free(b); return 43; }
    p = team_node_player(cap);
    if (strcmp(p->name, "grace") != 0 || p->goals != 7) { team_list_free(a); team_list_free(b); return 44; }

    /* 2. order of walking is order of adding. */
    n = team_list_first(a);
    if (n == NULL || strcmp(team_node_player(n)->name, "ada") != 0) { team_list_free(a); team_list_free(b); return 45; }
    n = team_node_next(n);
    if (n != cap) { team_list_free(a); team_list_free(b); return 45; }

    /* 3. move is a splice: the SAME node, the same address, now in b. */
    if (team_list_move(a, b, cap) != 0) { team_list_free(a); team_list_free(b); return 46; }
    if (team_list_count(a) != (size_t)(MANY + 2) || team_list_count(b) != 1) { team_list_free(a); team_list_free(b); return 47; }
    if (team_list_first(b) != cap) { team_list_free(a); team_list_free(b); return 48; }
    if (team_node_player(cap)->goals != 7) { team_list_free(a); team_list_free(b); return 48; }
    n = team_list_first(a);
    if (n == NULL || strcmp(team_node_player(n)->name, "ada") != 0) { team_list_free(a); team_list_free(b); return 45; }
    n = team_node_next(n);
    if (n == NULL || strcmp(team_node_player(n)->name, "linus") != 0) { team_list_free(a); team_list_free(b); return 45; }
    /* a node not in `from` is refused */
    if (team_list_move(a, b, cap) == 0) { team_list_free(a); team_list_free(b); return 49; }

    /* 4. remove every other node while walking, `next` saved FIRST. */
    seen = 0;
    for (n = team_list_first(a); n != NULL; n = next) {
        next = team_node_next(n);
        if ((seen++ & 1) == 0 && team_list_remove(a, n) != 0) { team_list_free(a); team_list_free(b); return 50; }
    }
    if (team_list_count(a) != (size_t)((MANY + 2) / 2)) { team_list_free(a); team_list_free(b); return 51; }
    for (n = team_list_first(a), seen = 0; n != NULL; n = team_node_next(n)) seen++;
    if (seen != (MANY + 2) / 2) { team_list_free(a); team_list_free(b); return 51; }
    if (team_list_remove(a, cap) == 0) { team_list_free(a); team_list_free(b); return 52; }  /* cap is in b */

    team_list_free(a);
    team_list_free(b);       /* frees cap too: LeakSanitizer judges this */
    return 0;
}
PRB

run_probe() { # $1 probe src, $2 impl, $3 out
    $CC $SAN -o "$3" "$1" "$2" 2>/dev/null || return 100
    "./$3" 2>_err.txt
    rc=$?
    if grep -qE "(AddressSanitizer|LeakSanitizer|runtime error)" _err.txt; then
        return 101
    fi
    return $rc
}

run_probe _vec.c team_vec.c vecprobe; rc=$?
case $rc in
  0)   : ;;
  100) echo "FAIL: team_vec.c does not compile as C89 under -Wall -Wextra"; exit 1 ;;
  101) echo "FAIL (fix 1, the vector): the sanitizer stopped the run -- a pointer held across a realloc, most likely. Its summary:"
       grep -E "ERROR:|SUMMARY:" _err.txt | sed 's/^/    /' | head -3; exit 1 ;;
  21|22) echo "FAIL (fix 1): team_vec_new/add failed on a machine that has memory"; exit 1 ;;
  23) echo "FAIL (fix 1): team_vec_set_captain refused a valid index"; exit 1 ;;
  24) echo "FAIL (fix 1): the captain read back wrong before any growth"; exit 1 ;;
  25) echo "FAIL (fix 1): team_vec_count is wrong after 200,004 adds"; exit 1 ;;
  26) echo "FAIL (fix 1): after the array grew, team_vec_captain returned NULL"; exit 1 ;;
  27) echo "FAIL (fix 1): after the array grew, the captain is not grace with 7 goals -- remember WHICH player (an index), not WHERE they were"; exit 1 ;;
  28) echo "FAIL (fix 1): team_vec_captain and team_vec_at(1) disagree about where grace is"; exit 1 ;;
  29) echo "FAIL (fix 1): team_vec_at past the end must return NULL"; exit 1 ;;
  30) echo "FAIL (fix 1): team_vec_set_captain accepted an index past the end"; exit 1 ;;
  *)  echo "FAIL (fix 1): the probe exited $rc -- it was killed"; sed 's/^/    /' _err.txt | head -6; exit 1 ;;
esac

run_probe _list.c team_list.c listprobe; rc=$?
case $rc in
  0)   : ;;
  100) echo "FAIL: team_list.c does not compile as C89 under -Wall -Wextra"; exit 1 ;;
  101) echo "FAIL (fix 2, the list): the sanitizer stopped the run -- a node freed and then read, or a node never freed. Its summary:"
       grep -E "ERROR:|SUMMARY:" _err.txt | sed 's/^/    /' | head -3; exit 1 ;;
  41|42) echo "FAIL (fix 2): team_list_new/add returned NULL on a machine that has memory -- the stub, still"; exit 1 ;;
  43) echo "FAIL (fix 2): team_list_count is wrong after 200,003 adds"; exit 1 ;;
  44) echo "FAIL (fix 2): a node's player changed under it after later adds -- a node's address must be stable for its lifetime"; exit 1 ;;
  45) echo "FAIL (fix 2): walking the list does not give the players in the order they were added"; exit 1 ;;
  46) echo "FAIL (fix 2): team_list_move refused a node that is in 'from'"; exit 1 ;;
  47) echo "FAIL (fix 2): the counts are wrong after a move -- one list loses a node, the other gains it"; exit 1 ;;
  48) echo "FAIL (fix 2): after the move the node in 'to' is not the SAME node -- a move is a splice of the existing node, never a copy; its address and its data must survive"; exit 1 ;;
  49) echo "FAIL (fix 2): team_list_move accepted a node that is not in 'from'"; exit 1 ;;
  50) echo "FAIL (fix 2): team_list_remove refused a node that is in the list"; exit 1 ;;
  51) echo "FAIL (fix 2): after removing every other node the count or the walk is wrong"; exit 1 ;;
  52) echo "FAIL (fix 2): team_list_remove accepted a node that is in ANOTHER list"; exit 1 ;;
  *)  echo "FAIL (fix 2): the probe exited $rc -- it was killed"; sed 's/^/    /' _err.txt | head -6; exit 1 ;;
esac

echo "PASS: the vector survives 200,000 adds by holding an index, and the list keeps its addresses through adds, an O(1) move and removal -- both clean under ASan/LSan"
exit 0
