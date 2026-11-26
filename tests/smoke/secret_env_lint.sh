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

t_plan 5
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

# --- 3: jichi's own key names AND the three harness token-file names, by name -------
# M1170 (batch H9c, SV:0818): the three names are not apiKeyEnv values (so
# checks 1 and 2 never see them) -- they are the reference scripts' ambient
# token-file exports (push.sh/land.sh/push-backlog.sh) and the port's D13
# twins (include/jc_harness_config.h). The fix is one rename from vacuous
# unless the lint earns each name by exact match.
c3missing=""
for _c3n in JICHI_API_KEY JLU_API_KEY ZD_TOKEN_FILE \
            JICHI_HARNESS_GIT_TOKEN_FILE JICHI_HARNESS_MODEL_KEY_FILE; do
    grep -qx "$_c3n" "$tmp/builtin" || c3missing="$c3missing $_c3n"
done
if [ -z "$c3missing" ]; then
    t_ok "JICHI_API_KEY, JLU_API_KEY, ZD_TOKEN_FILE, JICHI_HARNESS_GIT_TOKEN_FILE and JICHI_HARNESS_MODEL_KEY_FILE are built-in scrub entries"
else
    t_fail "not all five built-in names are present by exact match; missing:${c3missing} (list: $(tr '\n' ' ' < "$tmp/builtin"))"
fi

# --- 4: armed before the first forking subcommand ---------------------------------
# The CALL, not the statement: since M724 its return is checked (a name the
# registry cannot keep makes jichi refuse to start), so no `;` follows it.
arm=$(grep -n 'jc_proc_secret_env_add(km->api_key_env)' "$MAIN" | head -n 1 | cut -d: -f1)
disp=$(grep -n 'strcmp(args.pos\[0\], "brief-check") == 0' "$MAIN" | head -n 1 | cut -d: -f1)
if [ -n "$arm" ] && [ -n "$disp" ] && [ "$arm" -lt "$disp" ]; then
    t_ok "the secret registry is armed (main.c:$arm) above the brief-check dispatch (main.c:$disp)"
else
    t_fail "arming at main.c:${arm:-?} is not above the brief-check dispatch at main.c:${disp:-?}"
fi

# --- 5: no registration of a secret drops its answer ------------------------------
# jc_redact_register returns -1 when it cannot keep the value (out of memory),
# and then that secret is NOT scrubbed from the logs. Every caller says so in a
# warning -- except one did not: the bare token after "Bearer " in an MCP
# credential header was registered with the answer thrown away, so that one
# failure would have been silent (found by the 0.15.0 release audit).
# Universe, stated: every call of jc_redact_register( in src/**/*.c, the
# definition itself apart. Floored at today's exact count (7); the floor is the
# check that the scrape still scrapes. A call "drops its answer" when it stands
# as a statement of its own, with or without a (void) cast.
find "$SMOKE_ROOT/src" -type f -name '*.c' -exec grep -n 'jc_redact_register(' {} /dev/null \; 2>/dev/null \
    | grep -v ':int  *jc_redact_register(' | sed "s|^$SMOKE_ROOT/||" | sort > "$tmp/regsites"
nreg=$(grep -c . "$tmp/regsites")
grep -E ':[0-9]+:[[:space:]]*(\(void\)[[:space:]]*)?jc_redact_register\(' "$tmp/regsites" > "$tmp/regdropped"
ndrop=$(grep -c . "$tmp/regdropped")
if [ "$nreg" -lt 7 ]; then
    t_fail "only $nreg call(s) of jc_redact_register found in src/ (floor 7) -- fix the extraction, not the floor"
elif [ "$ndrop" -eq 0 ]; then
    t_ok "all $nreg registrations of a secret look at the answer (none stands as a bare statement)"
else
    t_fail "$ndrop of $nreg registration(s) of a secret throw the answer away -- a failed one would leave that secret unscrubbed without a word: $(cut -d: -f1,2 "$tmp/regdropped" | tr '\n' ' ')"
fi
t_done
