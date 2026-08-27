#!/bin/sh
# smoke: the legacy-path warning helps in the case that actually loses data
# (M326v).
#
# THE FIELD INCIDENT. A real ~/.jichi.d contained a second ~/.jichi.d/.jichi.d
# holding 5.5 GB of checkpoints that jichi neither reads nor mentions. The chain:
#
#   1. warn_legacy_paths fired only while `old exists && new does NOT`.
#   2. Running jichi once after upgrading CREATES the new directory -- so from
#      the first run onward the warning was permanently silent, and nobody was
#      told the old state was stranded.
#   3. Whoever noticed later reached for `mv OLD NEW`. Because NEW now exists
#      as a DIRECTORY, mv does not rename -- it moves OLD *inside* NEW.
#   4. Silence. More state lost than before, and now invisible.
#
# So the both-exist case must speak, must NOT print the `mv` that causes (3),
# and the resulting shape must be recognisable on sight. Three states, three
# behaviours -- an assertion on any one alone would pass on the broken code
# (the old-only branch is unchanged, which is exactly why it is pinned too).
. "$(dirname "$0")/_smoke.sh"

t_plan 6

# `models` is enough to reach warn_legacy_paths and needs no network: it reports
# configured models, and with no config there are none. --version exits earlier.
run_with_home() {
    _h=$1
    with_deadline 30 env HOME="$_h" "$BIN" models < /dev/null > "$_h/out" 2> "$_h/err"
}

# --- old only: the safe rename is still advised ------------------------------
a=$(smoke_tmp); mkdir -p "$a/.jlu_continue.d"
run_with_home "$a"
if grep -q "mv $a/.jlu_continue.d $a/.jichi.d" "$a/err"; then
    t_ok "old-only: the plain rename is advised (unchanged behaviour)"
else
    t_fail "old-only: lost the mv advice: $(head_bytes 200 "$a/err")"
fi

# --- both exist: speaks, and refuses to suggest the mv -----------------------
b=$(smoke_tmp); mkdir -p "$b/.jlu_continue.d" "$b/.jichi.d"
run_with_home "$b"
if grep -q 'both exist' "$b/err"; then
    t_ok "both-exist: says so (was silent before -- the data-losing case)"
else
    t_fail "both-exist: still silent: $(head_bytes 200 "$b/err")"
fi

# The teeth of the whole driver: the advice that causes the nesting must be
# absent in exactly this state. Anchored on the mv of THESE two paths, not on
# the word "mv" -- the warning text is allowed to discuss mv, and an unanchored
# grep would test the prose rather than the advice.
if grep -q "mv $b/.jlu_continue.d $b/.jichi.d" "$b/err"; then
    t_fail "both-exist: still prints the mv that nests the directory"
else
    t_ok "both-exist: does not print the mv that would nest it"
fi

if grep -q 'INSIDE' "$b/err"; then
    t_ok "both-exist: names the consequence, not just the fact"
else
    t_fail "both-exist: warns without saying what goes wrong"
fi

# --- the shape the mistake leaves behind is recognised ------------------------
c=$(smoke_tmp); mkdir -p "$c/.jichi.d/.jichi.d"
run_with_home "$c"
if grep -q 'nested inside the state directory' "$c/err"; then
    t_ok "a nested state directory is detected and named"
else
    t_fail "nested state directory not reported: $(head_bytes 200 "$c/err")"
fi

# --- and a clean HOME stays quiet --------------------------------------------
# Without this the three checks above are satisfied by a build that warns
# unconditionally, which would train every user to ignore the warning.
d=$(smoke_tmp); mkdir -p "$d/.jichi.d"
run_with_home "$d"
if grep -qE 'older location|nested inside' "$d/err"; then
    t_fail "clean HOME warns anyway: $(head_bytes 200 "$d/err")"
else
    t_ok "a clean HOME produces no migration warning"
fi

t_done
