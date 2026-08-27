#!/bin/sh
# lint: every API-key variable name the tree SHIPS as a default is on the
# built-in scrub list, and the secret registry is armed before any subcommand
# can fork a child (M608).
#
# Universe, stated: the distinct `"apiKeyEnv": "<NAME>"` values in src/ (the
# defaults the wizard, the scaffolder and the advice text write) and examples/
# (the configs the product ships). docs/ is deliberately OUT: its examples name
# illustrative variables (`ACME_API_KEY`, `MY_KEY`) that no one is meant to have.
# Extraction floored at today's exact counts (3 names, 31 mentions); the floor is
# the check that the scrape still scrapes.
#
# Why a lint: M130's built-in list carried thirteen third-party names and neither
# of jichi's own -- JICHI_API_KEY (the wizard's default since M242) and
# JLU_API_KEY (the HRZ onboarding name) -- so the "stray export can't leak"
# promise held for every provider's key except the one this project's users have.
# An audit would have found that once; this finds the next name forever.
#
# Check 4 pins the ORDER in main.c: the registry must be armed (the
# jc_proc_secret_env_add loop over the models) on a line ABOVE the brief-check
# dispatch, the earliest subcommand that forks a verifier. Before M608 the arming
# sat ~1,100 lines below it and brief-check's child saw the key
# (tests/smoke/secret_env_subcommands.sh is the effect; this is the shape).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
PROC="$SMOKE_ROOT/src/util/jc_proc.c"
MAIN="$SMOKE_ROOT/src/main.c"
tmp=$(smoke_tmp)

# --- 1: extraction floor ---------------------------------------------------------
find "$SMOKE_ROOT/src" "$SMOKE_ROOT/examples" -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.json' -o -name '*.jsonc' -o -name '*.md' -o -name '*.sh' \) \
    -exec grep -h -o '"apiKeyEnv" *: *"[A-Za-z_0-9]*"' {} + 2>/dev/null \
    | sed 's/.*: *"//; s/"$//' | sort > "$tmp/mentions"
sort -u "$tmp/mentions" > "$tmp/names"
nm=$(grep -c . "$tmp/mentions"); nn=$(grep -c . "$tmp/names")
if [ "$nn" -ge 3 ] && [ "$nm" -ge 31 ]; then
    t_ok "extracted $nn distinct apiKeyEnv names from $nm mentions (floors 3 / 31): $(tr '\n' ' ' < "$tmp/names")"
else
    t_fail "extraction too thin ($nn names, $nm mentions; floors 3 / 31) -- the scrape broke"
fi

# --- 2: every shipped name is on the built-in list --------------------------------
sed -n '/^static const char \*const g_secret_env_builtin\[\] = {/,/^};/p' "$PROC" \
    | grep -o '"[A-Z_0-9]*"' | tr -d '"' | sort -u > "$tmp/builtin"
nb=$(grep -c . "$tmp/builtin")
missing=""
while IFS= read -r n; do
    [ -n "$n" ] || continue
    grep -qx "$n" "$tmp/builtin" || missing="$missing $n"
done < "$tmp/names"
if [ "$nb" -ge 15 ] && [ -z "$missing" ]; then
    t_ok "all $nn shipped names are among the $nb built-in scrub entries"
else
    t_fail "built-in list ($nb entries) misses shipped apiKeyEnv name(s):${missing:- (none; floor 15 failed)}"
fi

# --- 3: jichi's own two names, by name ----------------------------------------------
if grep -qx JICHI_API_KEY "$tmp/builtin" && grep -qx JLU_API_KEY "$tmp/builtin"; then
    t_ok "JICHI_API_KEY and JLU_API_KEY are built-in scrub entries"
else
    t_fail "jichi's own key names are not both built in: $(tr '\n' ' ' < "$tmp/builtin")"
fi

# --- 4: armed before the first forking subcommand ---------------------------------
arm=$(grep -n 'jc_proc_secret_env_add(km->api_key_env);' "$MAIN" | head -n 1 | cut -d: -f1)
disp=$(grep -n 'strcmp(args.pos\[0\], "brief-check") == 0' "$MAIN" | head -n 1 | cut -d: -f1)
if [ -n "$arm" ] && [ -n "$disp" ] && [ "$arm" -lt "$disp" ]; then
    t_ok "the secret registry is armed (main.c:$arm) above the brief-check dispatch (main.c:$disp)"
else
    t_fail "arming at main.c:${arm:-?} is not above the brief-check dispatch at main.c:${disp:-?}"
fi
t_done
