#!/bin/sh
# smoke lint: a user-typed numeric CLI argument must not be read by atoi/atol (M923).
#
# WHY. atoi/atol stop at the first non-digit and report nothing: "abc" and "1k"
# both read as a number with no signal. On a FENCE flag that is the fence
# silently REMOVED (`--max-tool-calls abc` -> 0 = uncapped, measured by the
# 2026-10-08 review R02-F3); on a DATA flag it is a mass delete
# (`prune --keep abc` -> 0 = keep nothing, R02-F1 high -- and on 2026-10-09 an
# unfixed probe of exactly that case deleted a real 605-session store, so the
# driver's prune check runs in a sandboxed HOME and asserts the file survives).
# The whole-string parser is jc_cli_parse_nonneg_int / _bounded_int
# (include/jc_cli.h); every parse_args numeric flag and the undo/rewind N go
# through it. This lint holds that boundary, because the next numeric flag will
# be added by someone who has not read that story.
#
# THE UNIVERSE (M305): numeric arguments the USER types, read from argv.
#   checked  -- src/main.c has NO atoi(/atol( on a value taken from argv (the
#               parse_args pattern `*(argv[++i])`), floored at today's 0.
#   checked  -- the parse_args numeric-flag sites use the strict helper,
#               floored at today's exact count (10 flags + undo + rewind N).
#   checked  -- a planted positive (`x = atoi(argv[++i]);`) is caught by the
#               very pattern used, so the check cannot silently match nothing.
#   NOT checked -- atoi/atol on values jichi itself produced (a menu readline
#               line, git's rev-list count, a board id, an attempt's age):
#               those errors are visible or clamped in a display path, not a
#               fence removal or a delete; a follow-up class, recorded in the
#               M923 ROADMAP entry. NOT checked -- the already-strict duration
#               parsers (jc_env_parse_duration), which are a different family.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
MAIN="$SMOKE_ROOT/src/main.c"

# 1. No atoi/atol on an argv-taken value anywhere in src/main.c. The pattern is
#    the argv shape itself, `atoi(argv[` / `atol(argv[`, so a planted positive
#    written the way the defect used to be must fire it (check 3).
bad=$(grep -cE '(atoi|atol)\(argv\[' "$MAIN" || true)
[ "$bad" = 0 ] \
    && t_ok "src/main.c reads no user-typed argv number with atoi/atol (0 sites)" \
    || t_fail "src/main.c still has atoi/atol on argv in $bad place(s) -- the M923 class is back"

# 2. The parse_args numeric flags + the undo/rewind N go through the strict
#    helper. Floored at today's exact count: 10 parse_args flags, 2 subcommand
#    N sites = 12.
strict=$(grep -cE 'jc_cli_parse_(bounded|nonneg)_int' "$MAIN" || true)
[ "$strict" = 12 ] \
    && t_ok "src/main.c routes its user-number sites through the strict helper (12)" \
    || t_fail "src/main.c uses the strict helper in $strict of 12 user-number sites -- the count moved; restate it or find the atoi that slipped back"

# 3. The planted positive must fire: prove the check 1 pattern can match, using
#    the SAME pattern check 1 runs (a different pattern here would not prove
#    anything about check 1).
probe=$(smoke_tmp)/probe.c
printf 'int x = atoi(argv[++i]);\n' > "$probe"
if grep -qE '(atoi|atol)\(argv\[' "$probe"; then
    t_ok "planted atoi(argv[++i]) is caught by the check-1 pattern (the instrument can fire)"
else
    t_fail "planted atoi(argv[++i]) is NOT caught by the check-1 pattern -- check 1 is a green that matches nothing"
fi

t_done
