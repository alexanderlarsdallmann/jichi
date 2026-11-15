#!/bin/sh
# smoke lint: the `make cpp-check` UNIVERSE must include the harness TUs (SV:0039).
#
# THE DEFECT. `make ci`'s C++ stage -- `$(MAKE) clean && $(MAKE) CC=g++ jichi
# jichi-harness test` (Makefile:1753) -- died on 2026-11-08 23:55 at the host gate
# (ci-88df8eec.log):
#
#     src/harness/jc_harness_notify.c:767:28: error: invalid conversion from
#     'void*' to 'char*' [-fpermissive]
#       767 |         char *line = malloc(strlen(cfg.host_notify_command) + 1);
#
# C89 converts void * implicitly; C++ does not, and only the gate's `CC=g++
# jichi-harness` line ever reaches harness code. `make cpp-check` -- the standing
# second-front-end sieve -- CANNOT see it: its file list
# (CPPCHECK_SRC = $(LIB_SRC) $(MAIN_OBJ:.o=.c) $(TEST_SRC)) expands to 330 files
# and 0 of the 16 src/harness/*.c. A sieve that is green while the gate's own
# C++ build is red is not a sieve. Every H9-H14 port adds harness translation
# units, so a universe that excludes src/harness will keep rotting.
#
# THIS LINT HOLDS THE UNIVERSE. Two checks, both red on 88df8eec's tree:
#
#   1. The universe, enumerated twice (the project's own doctrine): Route A is
#      `make cpp-check`'s actual file list, expanded by a tiny `make -f` shim
#      that includes the project Makefile and evaluates $(CPPCHECK_SRC); Route B
#      is the WILDCARD ITSELF, `ls src/harness/*.c`. Route A must contain every
#      file Route B names, and both floors must hold (Route A >= 346, Route B >=
#      16, the post-fix counts at this head). A Route A that dropped the harness
#      (the M188/M636/M871 lesson: a list that needs updating per file is a list
#      that stops being updated) is the perturbation this check is teeth for.
#
#   2. The gate's C++ stage, as a standing container check: a targeted
#      `g++ -x c++ -std=c++17 -fsyntax-only` over EVERY src/harness/*.c, reusing
#      exactly the -D defines `make -n -B src/harness/jc_harness_notify.o` prints
#      plus the tree's POSIX/include paths. This is the host's :767 line as a
#      standing check: any future implicit void*->char* (or const conversion) in
#      any of the 16 harness TUs is caught HERE, in the container, before the gate.
#
# WHY A LINT AND NOT THE GATE. The M188 decision holds: cpp-check stays OUT of
# `make ci` -- the gate's own C++ stage (line 1753) is the compile, and cpp-check
# is a parse-only sieve. What this lint adds is the container's standing check
# over the harness, so the universe cannot silently shrink again between gates.
#
# POSIT X. Strict POSIX sh (smoke_lint enforces). No `local` in the loop: the
# 16-TU loop keeps its per-iteration values in uniquely-named variables.
#
# DEGRADE. The universe and the -D defines both come from `make`; a rig whose
# `make` cannot parse this GNU Makefile (FreeBSD bmake, see smoke_make) gets
# SKIP lines, not false reds. The floors and the compile are where the strength
# lives, on the host, where `make ci` is the gate.
. "$(dirname "$0")/_smoke.sh"

t_plan 26  # 1 universe check + one C++17 parse per src/harness/*.c (25 TUs at this head)
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
cd "$SMOKE_ROOT" || exit 1
_mk=$(smoke_make)

FLOOR_A=349   # CPPCHECK_SRC file count at this head, harness included (LIB_SRC + main + tests + 18)
FLOOR_B=18    # floor for the src/harness/*.c TU count (the live count is 25 at this head: 16 at 6e5468fa; watchgate + watchunits at M1153; +1 gateunits at M1159 wave 4)

# ---- 1: the universe, enumerated twice -------------------------------------
# Route A: the file list `make cpp-check` actually compiles. There is no
# `make print` in this tree, so expand $(CPPCHECK_SRC) with a 4-line shim that
# includes the project Makefile (so LIB_SRC/MAIN_OBJ/TEST_SRC/wildcard resolve
# to the REAL values, not an empty parse) and echoes the variable.
shim="$tmp/cppcheck_src_shim.mk"
printf 'include %s\nprint:\n\t@echo $(CPPCHECK_SRC)\n' "$SMOKE_ROOT/Makefile" > "$shim"
ra_raw=$($_mk -f "$shim" print 2>/dev/null)
ra=$(printf '%s\n' "$ra_raw" | tr ' ' '\n' | "$G" -E '\.c$' | sort -u)
# Route B: the wildcard itself -- the harness TUs the universe must contain.
rb=$(ls src/harness/*.c 2>/dev/null | sort -u)
na=$(printf '%s\n' "$ra" | "$G" -c . )
[ -n "$na" ] || na=0
nb=$(printf '%s\n' "$rb" | "$G" -c . )
[ -n "$nb" ] || nb=0
if [ -z "$ra_raw" ]; then
    t_skip_one "no GNU make could evaluate \$CPPCHECK_SRC -- the universe \
(346 floor) and the harness containment cannot be enumerated here"
else
    printf '%s\n' "$ra" > "$tmp/ra.txt"
    printf '%s\n' "$rb" > "$tmp/rb.txt"
    missing=$(comm -13 "$tmp/ra.txt" "$tmp/rb.txt")
    if [ -z "$missing" ] && [ "$na" -ge "$FLOOR_A" ] && [ "$nb" -ge "$FLOOR_B" ]; then
        t_ok "universe holds the harness: Route A (make shim) $na files (floor $FLOOR_A), Route B (ls) $nb harness TUs (floor $FLOOR_B); all present"
    else
        t_fail "the cpp-check universe lost the harness TUs (SV:0039). Route A = $na files (floor $FLOOR_A), Route B = $nb harness TUs (floor $FLOOR_B).
Missing from CPPCHECK_SRC (every line is src/harness/*.c the sieve skips):
$(printf '%s\n' "$missing" | sed 's/^/    /')
A file list that needs updating per TU stops being updated -- the gate's own
C++ stage is the only place the harness was ever compiled, and it is not the
standing check. Re-include \$(wildcard src/harness/*.c) in CPPCHECK_SRC."
    fi
fi

# ---- 2: the gate's C++ stage, over every harness TU -------------------------
# Reuse EXACTLY the -D defines the build would use, from `make -n -B`:
# -D_POSIX_C_SOURCE=200112L -D_FILE_OFFSET_BITS=64 plus the JC_HAVE_* the stamp
# build defines (VSNPRINTF, MALLOC_TRIM, SET_DUMPABLE, POSIX_SPAWN, FCHDIR,
# CURL when present). -Iinclude -Isrc/json complete the tree's POSIX/include
# set. -fsyntax-only: parse, do not emit -- this is a sieve, and the gate's
# LINK is what the ci C++ stage already proves.
_ddefs=$($_mk -n -B src/harness/jc_harness_notify.o 2>/dev/null | tr ' ' '\n' \
          | "$G" -E '^-D' | tr '\n' ' ')
if ! command -v g++ >/dev/null 2>&1 || [ -z "$_ddefs" ]; then
    for _i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
        t_skip_one "g++ or its -D define set (make -n -B) is unavailable -- the \
C++ stage cannot be re-run here as a parse"
    done
else
    for _f in src/harness/*.c; do
        _b=${_f##*/}
        _err=$(g++ -x c++ -std=c++17 -fsyntax-only $_ddefs -Iinclude -Isrc/json \
                 "$_f" 2>&1)
        _rc=$?
        if [ "$_rc" -eq 0 ]; then
            t_ok "$_b parses as C++17 (the gate's C++ stage, as a standing check)"
        else
            t_fail "$_b does not parse as C++17 -- the gate's C++ stage would die here (SV:0039, the :767 void*->char* family):
$(printf '%s\n' "$_err" | "$G" -E 'error:' | head -4 | sed 's/^/    /')"
        fi
    done
fi

t_done
