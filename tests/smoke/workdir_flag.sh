#!/bin/sh
# smoke: the -C <dir> / --cwd workdir flag (M1043-b, plan 2026-10-03 4.3,
# card 79, def-329's 4.3 half; M1043-b-c moved the chdir). jichi chdirs to
# the named directory AFTER parse_args and BEFORE every dispatch -- the
# subcommand chain AND the run modes -- so -C applies to everything the way
# git/make/tar's -C does, and every workspace-relative thing -- the project
# config, the path fence, the repo map, skills/agents/commands, sessions,
# .jichi/ state -- sees that directory, not the launcher's. The model's
# shell then runs in the target, so a child's `pwd` is the target even
# though the process started elsewhere. A missing directory is refused at
# startup for every invocation.
#
# RED-FIRST history. On the original unfixed tree legs 1, 2 and 4 were red
# (-C an unknown option; leg 3 green both sides then). M1043-b landed the
# chdir, but AFTER the subcommand dispatch chain -- the supervisor measured
# `jichi -C <target> init` scaffolding into the LAUNCHER. Legs 5 and 6 are
# the M1043-b-c red half: on the tree that still has the post-dispatch
# chdir, leg 5 (init from the launcher with -C target) lands AGENTS.md in
# the launcher and leaves the target empty, and leg 6 (-C missing dir with
# init) exits 0 -- both red. Moving the chdir before every dispatch makes
# both green. Leg 3 (a missing directory is refused) stays as is: its
# `[ ! -e "$launch/.jichi" ]` assertion is now trivially stronger, because
# the refusal is universal, not run-path-only.
# The jichi call always takes < /dev/null.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)

# Two scratch workspaces: $wsA holds the "project" (a marker file + the
# project config that discovery must find), $launch is an EMPTY directory
# the jichi process starts in. smoke_home's JC_CONFIG null-file is un-set
# inside each subshell on purpose -- it would force explicit config mode
# and defeat the very discovery the flag is there to enable. The isolated
# HOME still covers ~/.jichi.
wsA=$(smoke_tmp)
launch=$(smoke_tmp)
mkdir -p "$wsA/.jichi"
printf '%s\n' 'MARKER-CONTENT-abc123' > "$wsA/marker.txt"

# --- 1 (RED on unfixed): the PROJECT CONFIG is discovered from the target --
# Run from the EMPTY launch dir with no --config: the only way the run
# finds a model at all is that -C moved the process to $wsA before
# discovery read $wsA/.jichi/config.json. The config is written AFTER the
# mock announces its port, into $wsA/.jichi/, so no --config is ever
# passed. A one-shot `trust` (a subcommand that also chdirs, and is not
# trusted by default) applies the models key; discovery is what leg 1
# proves. Unfixed: `error: unknown option '-C'` on every line below, the
# model is never reached.
cat > "$tmp/m1.mm" <<EOF
wire openai
rule
  count 1
  text WORKDIR_CONFIG_REACHED
EOF
mm_start_unbounded "$tmp/m1.mm" "$tmp/cap1"
# The port is known now; the project config is written into the TARGET's
# .jichi/ -- discovery (not --config) must find it from $wsA.
write_config "$wsA/.jichi/config.json" "$MM_PORT" '' ''
(cd "$launch" && {
    unset JC_CONFIG
    with_deadline 90 "$BIN" -C "$wsA" trust < /dev/null > "$tmp/trust1" 2>&1
    trc=$?
    with_deadline 90 "$BIN" -C "$wsA" -q --no-session --auto \
        -p "hi" < /dev/null > "$tmp/out1" 2>&1
    rc=$?
    [ "$trc" = "0" ] || rc=$trc
    exit "$rc"
}); runrc=$?
mm_stop
if [ "$runrc" = "0" ] && grep -q -E 'WORKDIR_CONFIG_REACHED' "$tmp/out1" &&
   ! grep -q -E 'unknown option' "$tmp/out1"
then
    t_ok "the project config is discovered from the -C target (no --config, launcher dir empty)"
else
    t_fail "-C did not relocate config discovery (runrc=$runrc): $(head_bytes 200 "$tmp/out1")"
fi

# --- 2 (RED on unfixed): the RUN'S WORKSPACE is the target --
# The model's shell writes `pwd` and a marker read to files; both must
# show $wsA / the marker content even though the process started in the
# empty launch dir. Unfixed: unknown option, no child ever runs.
cat > "$tmp/m2.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"pwd > '$tmp/pwd2'; cat marker.txt > '$tmp/mark2'; exit 0"}
rule
  text WORKDIR_PWD_DONE
EOF
mm_start_unbounded "$tmp/m2.mm" "$tmp/cap2"
# The project config must name THIS mock's port, and a change re-untrusts
# the config (it is sha256-trusted), so trust once more before the run.
write_config "$wsA/.jichi/config.json" "$MM_PORT" '' ''
(cd "$launch" && {
    unset JC_CONFIG
    with_deadline 90 "$BIN" -C "$wsA" trust < /dev/null > "$tmp/trust2" 2>&1
    trc=$?
    with_deadline 90 "$BIN" -C "$wsA" -q --no-session --auto \
        -p "hi" < /dev/null > "$tmp/out2" 2>&1
    rc=$?
    [ "$trc" = "0" ] || rc=$trc
    exit "$rc"
}); runrc=$?
mm_stop
if [ "$runrc" = "0" ] && [ -s "$tmp/pwd2" ] && [ "$(cat "$tmp/pwd2")" = "$wsA" ] &&
   grep -q -E 'MARKER-CONTENT-abc123' "$tmp/mark2" 2>/dev/null
then
    t_ok "the model's shell runs in the -C target (pwd and marker read from there)"
else
    t_fail "the run's workspace is not the -C target (runrc=$runrc, pwd=$(cat "$tmp/pwd2" 2>/dev/null)): $(head_bytes 200 "$tmp/out2")"
fi

# --- 3 (control, green both sides): a NON-EXISTENT target is refused --
# On fixed: `error: -C <dir>: <strerror>`, exit 1. On unfixed:
# `error: unknown option '-C'`, exit nonzero. Both name -C and exit
# nonzero, and neither must have touched the launch dir (no .jichi) --
# the refusal is before any run state is written.
missing="$wsA/definitely-not-a-dir"
(cd "$launch" && {
    unset JC_CONFIG
    with_deadline 90 "$BIN" -C "$missing" -q --no-session --auto \
        -p "hi" < /dev/null > "$tmp/out3" 2>&1
    exit $?
}); runrc=$?
if [ "$runrc" != "0" ] && grep -q -E -- '-C' "$tmp/out3" &&
   [ ! -e "$launch/.jichi" ]
then
    t_ok "a missing -C directory is refused (exit $runrc, nothing written in the launcher dir)"
else
    t_fail "the missing-directory refusal is wrong (runrc=$runrc): $(head_bytes 200 "$tmp/out3")"
fi

# --- 4 (RED on unfixed): --help names -C / --cwd --
"$BIN" --help < /dev/null > "$tmp/help4" 2>&1
if grep -q -E -- '-C <dir>, --cwd <dir>' "$tmp/help4"; then
    t_ok "--help names -C / --cwd beside --config"
else
    t_fail "--help does not name -C / --cwd:"
    head_bytes 400 "$tmp/help4"
fi

# --- 5 (RED on the M1043-b tree, green on M1043-b-c): a SUBCOMMAND honours -C --
# From the EMPTY launch dir, `jichi -C $wsB init` must scaffold into the
# TARGET: $wsB/AGENTS.md exists, and NOTHING (no AGENTS.md, no .jichi) is
# written into the launcher. M1043-b's chdir sat after the subcommand
# dispatch chain, so init dispatched first and scaffolded the launcher. A
# fresh target $wsB (a brand-new dir) keeps the assertion about WHERE the
# scaffold lands, independent of init's exists/skip semantics. No mock
# model is needed: init is filesystem only.
wsB=$(smoke_tmp)
(cd "$launch" && {
    unset JC_CONFIG
    with_deadline 90 "$BIN" -C "$wsB" init < /dev/null \
        > "$tmp/out5" 2>&1
    exit $?
}); runrc=$?
if [ "$runrc" = "0" ] && [ -f "$wsB/AGENTS.md" ] &&
   [ ! -e "$launch/AGENTS.md" ] && [ ! -e "$launch/.jichi" ]
then
    t_ok "init under -C scaffolds into the target, nothing into the launcher"
else
    t_fail "init under -C did not act on the target (runrc=$runrc, launcher AGENTS.md: $( [ -e "$launch/AGENTS.md" ] && echo present || echo absent ), launcher .jichi: $( [ -e "$launch/.jichi" ] && echo present || echo absent ), target AGENTS.md: $( [ -f "$wsB/AGENTS.md" ] && echo present || echo absent )): $(head_bytes 200 "$tmp/out5")"
fi

# --- 6 (RED on the M1043-b tree, green on M1043-b-c): the missing-directory
# refusal is UNIVERSAL -- it fires for subcommands too, not just run modes.
# From the launch dir, `jichi -C <missing> init` must exit NONZERO and name
# -C; M1043-b's chdir sat after the dispatch, so init exited 0 first and
# the refusal never ran. Nothing may be written into the launcher.
missing6="$wsA/definitely-not-a-dir"
(cd "$launch" && {
    unset JC_CONFIG
    with_deadline 90 "$BIN" -C "$missing6" init < /dev/null \
        > "$tmp/out6" 2>&1
    exit $?
}); runrc=$?
if [ "$runrc" != "0" ] && grep -q -E -- '-C' "$tmp/out6" &&
   [ ! -e "$launch/AGENTS.md" ] && [ ! -e "$launch/.jichi" ]
then
    t_ok "a missing -C directory is refused for subcommands too (exit $runrc, launcher untouched)"
else
    t_fail "the -C refusal did not fire for init (runrc=$runrc): $(head_bytes 200 "$tmp/out6")"
fi

# --- 7 (M1043-b-d, GREEN on this tree -- a PIN, not red-first): a -C target
# that is an EXISTING FILE is refused, not just a missing path. This pins the
# ENOTDIR sub-case the qwen27b review of M1043-b-c flagged as unpinned: chdir
# onto a regular file fails with ENOTDIR and hits the SAME refusal branch as
# the ENOENT legs 3/6 (error: -C <dir>: <strerror>, exit 1). legs 5/6 were the
# red-first half, recorded; this leg exists so the file-target path stays
# covered. (Platform note: this box's strerror(ENOTDIR) is "Not a directory",
# so the assertion below is the full -C + Not a directory + launcher-untouched
# form; on a platform whose strerror differs the weaker -C + nonzero +
# launcher-untouched form is the floor.)
notadir="$tmp/notadir"
touch "$notadir"
(cd "$launch" && {
    unset JC_CONFIG
    with_deadline 90 "$BIN" -C "$notadir" -q --no-session --auto \
        -p "hi" < /dev/null > "$tmp/out7" 2>&1
    exit $?
}); runrc=$?
if [ "$runrc" != "0" ] && grep -q -E -- '-C' "$tmp/out7" &&
   grep -q -E 'Not a directory' "$tmp/out7" &&
   [ ! -e "$launch/.jichi" ] && [ ! -e "$launch/AGENTS.md" ]
then
    t_ok "a -C target that is a file is refused (exit $runrc, ENOTDIR named, launcher untouched)"
else
    t_fail "the file-target -C refusal is wrong (runrc=$runrc): $(head_bytes 200 "$tmp/out7")"
fi
t_done
