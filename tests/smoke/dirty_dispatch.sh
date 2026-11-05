#!/bin/sh
# M1006 (board 84): the configurable dirty-tree dispatch guard.
#
# A WRITE dispatch (spawn_subagent with write tools) checks the LIVE workspace
# for uncommitted changes BEFORE any fork, and refuses / warns / proceeds per
# `strictness.dirtyDispatch` (default warn). The guard's universe, pinned here:
#   * only WRITE subagents are checked (include_mutating); READ-ONLY dispatches
#     (readonly:true) are never checked in any mode -- no event, no refusal.
#   * the probe reads the live workspace (git -C '<root>'), never a snapshot.
#   * off skips the whole check -- no probe, no telemetry event.
#
# The tool is FORCED by a mock `tool spawn_subagent {...}` rule (the subagent's
# own reply is a plain `text` rule), so the model never has to decide. The
# prompt carries a per-leg marker (DDELG / DDRO / DDEF / DDTR) so the right
# forced call fires -- the markers are otherwise absent, so they cannot be
# mistaken for tool output.
#
# Deterministic markers (each provably in a fixed place):
#   * WHERE THE GUARD SPEAKS: in a headless `-q` run the model's FINAL reply is
#     printed to stdout, but a TOOL RESULT (the refusal or the warn note) is
#     folded into the LEAD's NEXT request -- not printed. So the guard's text is
#     grepped from the lead's LAST request (the one that carries the tool
#     result), captured by mockmodel as req.N. This is the provably-present
#     place; the final answer is the mock's `DDFINAL` and carries nothing.
#   * "sub-agent inside jichi" is the SUBAGENT's system prompt only (PROMPT_SUB,
#     jc_sysmsg.c) -- never in a lead request. So "any capture carries it" is
#     the FORKED proof; "none carries it" is the no-fork proof.
#   * "commit, then dispatch" (the operator's fix phrase, verbatim) and
#     "dirty.txt" (the untracked file the dirty legs make) appear only in the
#     guard's own text -- never in the prompt or the mock replies.
#   * the `nomatch "commit, then dispatch"` / `nomatch "DDFINAL"` predicates stop
#     the forced-call rules once the guard has spoken or the subagent answered,
#     so every leg terminates on the text rule -- bounded in the fixed tree, and
#     bounded by --max-requests in the unfixed tree (where nothing ever speaks).
#   * the telemetry JSONL carries a `dirty_dispatch` event with mode/dirty/
#     npaths/outcome for every CHECKED dispatch; `jichi telemetry` renders the
#     reader line (the M167 "counted by hand" gap this milestone closes).
#
# RED FIRST: on the unfixed tree (no guard) the checks CANNOT pass -- there is
# no check to refuse, no guard text in the last request, no dirty_dispatch
# event, no reader line. The first commit adds this driver and shows those red
# lines; the fix commit makes it green.
#
# M1006-a legs 11-14 (the supervisor's three refinements):
#   11/12. a PRESENT-but-unknown strictness value is LOUD (split two ways):
#       11 the config-load notice (a headless `doctor` run with that config)
#           names the key, the unknown value and the mode in effect;
#       12 `jichi doctor` reports the EFFECTIVE mode in the report, with the
#           fell-through reason when the value was a typo. `doctor` is the
#           headless surface by design: -q sets the log level to NONE before
#           config load, so a `-q` run (run_leg) cannot carry the notice, but
#           doctor prints it. The main `-q` typo leg (10) still falls through
#           to warn and proceeds -- the typo must not change the fence.
#   13. a git FAILURE in a git workspace is UNKNOWN, not clean: corrupting
#       .git/index makes `git status` exit 128 while the workspace is still a
#       git repo (proven: `rev-parse --is-inside-work-tree` still succeeds;
#       chmod 000 .git is NOT the case -- it makes rev-parse fail too, reading
#       as non-git). The dispatch PROCEEDS in every mode with an unknown note;
#       telemetry says dirty="unknown", outcome="proceeded_unknown".
#   14. a workspace path with a SINGLE QUOTE: the probe spawns git with an argv
#       array (no shell), so `git -C <root>` gets the path intact and the guard
#       fires. The pre-fix shell-built string broke (sh: unterminated quote,
#       rc=2) and read as not-dirty -- the guard did NOT fire.
. "$(dirname "$0")/_smoke.sh"

# Guarded /usr/bin/grep for portability (systems without merged /usr).
G=/usr/bin/grep
[ -x "$G" ] || G=grep

command -v git >/dev/null 2>&1 || t_skip "git is not installed -- the guard needs it"

t_plan 14
smoke_home
tmp=$(smoke_tmp)

# --- one mock for every leg ----------------------------------------------------
cat > "$tmp/s.mm" <<'EOF'
wire openai
rule
  match "DDRO"
  nomatch "commit, then dispatch"
  nomatch "DDFINAL"
  tool spawn_subagent {"task":"do the readonly work","readonly":true}
rule
  match "DDELG"
  nomatch "commit, then dispatch"
  nomatch "DDFINAL"
  tool spawn_subagent {"task":"do the write work"}
rule
  match "DDEF"
  nomatch "commit, then dispatch"
  nomatch "DDFINAL"
  tool spawn_subagent {"task":"do the write work"}
rule
  match "DDTR"
  nomatch "commit, then dispatch"
  nomatch "DDFINAL"
  tool spawn_subagent {"task":"do the write work"}
rule
  match "DDUT"
  nomatch "commit, then dispatch"
  nomatch "DDFINAL"
  tool spawn_subagent {"task":"do the write work"}
rule
  match "DDQT"
  nomatch "commit, then dispatch"
  nomatch "DDFINAL"
  tool spawn_subagent {"task":"do the write work"}
rule
  text DDFINAL
EOF

# make_git REPO DIRTY -- a real repo, optionally dirty (one untracked file).
make_git() {
    _mg_d=$1; _mg_dirty=$2
    git init -q "$_mg_d"
    git -C "$_mg_d" config user.email t@t
    git -C "$_mg_d" config user.name t
    printf 'committed\n' > "$_mg_d/base.txt"
    (cd "$_mg_d" && git add base.txt && git commit -q -m init)
    if [ "$_mg_dirty" = 1 ]; then
        printf 'uncommitted work\n' > "$_mg_d/dirty.txt"   # untracked -> porcelain
    fi
}

# write_strictness CFG MODE -- inject the strictness block after `{`.
# (temp file + mv: BSD sed -i REQUIRES a backup suffix, so the tier bans
# `sed -i` -- see posix_utils_lint check 3.)
write_strictness() {
    _ws_c=$1; _ws_m=$2
    sed "1s/{/{\"strictness\":{\"dirtyDispatch\":\"$_ws_m\"},/" "$_ws_c" > "$_ws_c.tmp" \
        && mv "$_ws_c.tmp" "$_ws_c"
}

# run_leg CAP CFG DIRTY MODE PROMPT -- runs one forced subagent call.
# MODE is the strictness value to write; the two sentinels:
#   off    -> writes "off" (the explicit escape: no check, no event)
#   absent -> writes NO strictness block (so the product default applies)
run_leg() {
    _rl_cap=$1; _rl_cfg=$2; _rl_dirty=$3; _rl_mode=$4; _rl_prompt=$5
    _rl_ws=$(smoke_tmp)
    make_git "$_rl_ws" "$_rl_dirty"
    rm -rf "$_rl_cap"; mkdir -p "$_rl_cap"
    mm_start "$tmp/s.mm" "$_rl_cap" 30      # 30-request cap: a bounded loop
    write_config "$_rl_cfg" "$MM_PORT" '' ''
    case "$_rl_mode" in
        off)    write_strictness "$_rl_cfg" off ;;
        absent) : ;;                         # no key -> default (warn)
        *)      write_strictness "$_rl_cfg" "$_rl_mode" ;;
    esac
    ( cd "$_rl_ws" && with_deadline 90 "$BIN" --config "$_rl_cfg" -q --auto \
        --log "$_rl_cap/telemetry.jsonl" \
        -p "$_rl_prompt" < /dev/null > "$_rl_cap/out" 2>&1 )
    mm_stop
    RL_WS=$_rl_ws; RL_OUT=$_rl_cap/out; RL_TEL=$_rl_cap/telemetry.jsonl
    RL_CAP=$_rl_cap
}

# cap_forked CAP -- did a SUBAGENT fork? The subagent's OWN request carries the
# PROMPT_SUB system text ("You are a focused sub-agent ..."), which no lead
# request carries (the lead's prompt is "You are jichi"). So "any capture
# request carries it" == forked; "none" == the no-fork proof.
cap_forked() { [ -n "$(cat "$1"/req.* 2>/dev/null | $G -l 'focused sub-agent' 2>/dev/null)" ]; }

# cap_has CAP STR -- the guard's text lands in a request (a refusal is folded
# into the lead's next request; a warn note, prepended to the subagent result,
# is too). Grepped across the whole capture: provably present in one of the
# requests, never in the lead's printed final answer (the mock's DDFINAL).
cap_has() { cat "$1"/req.* 2>/dev/null | $G -q "$2"; }

# 1: dirty + refuse -> NO subagent forked (the no-fork proof).
run_leg "$tmp/c1" "$tmp/c1.json" 1 refuse "DDELG dispatch the delegate"
if cap_forked "$RL_CAP"; then
    t_fail "refuse: a subagent forked (PROMPT_SUB in a capture) -- the guard did not refuse"
else
    t_ok "refuse: the write subagent never forked (no sub-agent request in the capture)"
fi

# 2: the refusal names the dirty path and the fix, "commit, then dispatch".
# (folded into the lead's next request; grep across the capture)
if cap_has "$RL_CAP" 'commit, then dispatch' && cap_has "$RL_CAP" 'dirty.txt'; then
    t_ok "refuse: the policy refusal names the dirty path and 'commit, then dispatch'"
else
    t_fail "refuse: capture missing the refusal (forked=$(cap_forked "$RL_CAP" && echo y || echo n))"
fi

# 3: dirty + warn -> the subagent RUNS and the result carries the warning
# naming the dirty path. A warn note does NOT say "commit, then dispatch" the
# way a refusal does, so forked + the named path is the marker.
run_leg "$tmp/c2" "$tmp/c2.json" 1 warn "DDELG dispatch the delegate"
if cap_forked "$RL_CAP" && cap_has "$RL_CAP" 'dirty.txt' \
   && $G -q '"event":"dirty_dispatch"' "$RL_TEL"; then
    t_ok "warn: the write subagent ran, and the result carries the dirty warning (no refusal)"
else
    t_fail "warn: forked=$(cap_forked "$RL_CAP" && echo y || echo n)"
fi

# 4: dirty + off -> the subagent runs, NO warning line, NO event.
run_leg "$tmp/c3" "$tmp/c3.json" 1 off "DDELG dispatch the delegate"
if cap_forked "$RL_CAP" && ! cap_has "$RL_CAP" 'dirty.txt' \
   && ! $G -q '"event":"dirty_dispatch"' "$RL_TEL"; then
    t_ok "off: the write subagent ran, unchecked (no warning, no dirty_dispatch event)"
else
    t_fail "off: forked=$(cap_forked "$RL_CAP" && echo y || echo n) events=$(cat "$RL_TEL" 2>/dev/null | $G -c '"event":"dirty_dispatch"')"
fi

# 5: clean + refuse -> the subagent RUNS (the guard must not refuse healthy work).
run_leg "$tmp/c4" "$tmp/c4.json" 0 refuse "DDELG dispatch the delegate"
if cap_forked "$RL_CAP" && ! cap_has "$RL_CAP" 'commit, then dispatch' \
   && ! cap_has "$RL_CAP" 'dirty.txt'; then
    t_ok "clean+refuse: the guard did not refuse a clean tree; the subagent ran"
else
    t_fail "clean+refuse: forked=$(cap_forked "$RL_CAP" && echo y || echo n)"
fi

# 6: dirty + READ-ONLY -> the universe: NOT checked, runs, NO event, no refusal.
run_leg "$tmp/c5" "$tmp/c5.json" 1 refuse "DDRO dispatch the readonly delegate"
if cap_forked "$RL_CAP" \
   && ! $G -q '"event":"dirty_dispatch"' "$RL_TEL" \
   && ! cap_has "$RL_CAP" 'commit, then dispatch'; then
    t_ok "read-only+dirty: the subagent ran and was NOT checked (no dirty_dispatch event, no refusal)"
else
    t_fail "read-only+dirty: forked=$(cap_forked "$RL_CAP" && echo y || echo n) events=$(cat "$RL_TEL" 2>/dev/null | $G -c '"event":"dirty_dispatch"')"
fi

# 7: telemetry read-back -- one log accumulates a refuse check and a warn check,
# both with the full field set.
_rl_ws=$(smoke_tmp)
make_git "$_rl_ws" 1
rm -rf "$tmp/c6"; mkdir -p "$tmp/c6"
mm_start "$tmp/s.mm" "$tmp/c6" 30
write_config "$tmp/c6.json" "$MM_PORT" '' ''
write_strictness "$tmp/c6.json" refuse
( cd "$_rl_ws" && with_deadline 90 "$BIN" --config "$tmp/c6.json" -q --auto \
    --log "$tmp/c6/telemetry.jsonl" \
    -p "DDELG dispatch the delegate" < /dev/null > "$tmp/c6/out.refuse" 2>&1 )
mm_stop
rm -rf "$tmp/c6b"; mkdir -p "$tmp/c6b"
mm_start "$tmp/s.mm" "$tmp/c6b" 30
write_config "$tmp/c6b.json" "$MM_PORT" '' ''
write_strictness "$tmp/c6b.json" warn
( cd "$_rl_ws" && with_deadline 90 "$BIN" --config "$tmp/c6b.json" -q --auto \
    --log "$tmp/c6/telemetry.jsonl" \
    -p "DDELG dispatch the delegate" < /dev/null > "$tmp/c6b/out.warn" 2>&1 )
mm_stop
TELE=$tmp/c6/telemetry.jsonl
if [ -f "$TELE" ] \
   && [ "$(cat "$TELE" | $G -c '"event":"dirty_dispatch"' 2>/dev/null)" -ge 2 ] \
   && cat "$TELE" | $G '"event":"dirty_dispatch"' | $G -q '"outcome":"refused"' \
   && cat "$TELE" | $G '"event":"dirty_dispatch"' | $G -q '"outcome":"proceeded"' \
   && cat "$TELE" | $G '"event":"dirty_dispatch"' | $G -q '"mode":"warn"' \
   && cat "$TELE" | $G '"event":"dirty_dispatch"' | $G -q '"mode":"refuse"' \
   && cat "$TELE" | $G '"event":"dirty_dispatch"' | $G -q '"npaths"' \
   && cat "$TELE" | $G '"event":"dirty_dispatch"' | $G -q '"dirty":"yes"'; then
    t_ok "telemetry: dirty_dispatch events carry mode/dirty/npaths/outcome (refused + proceeded)"
else
    t_fail "telemetry: events=$(cat "$TELE" 2>/dev/null | $G -c dirty_dispatch); $(cat "$TELE" 2>/dev/null | $G 'dirty_dispatch' | head_bytes 400)"
fi

# 8: the READER -- `jichi telemetry` on that log renders the guard line.
"$BIN" telemetry "$TELE" > "$tmp/tel_render" 2>&1
if $G -q 'checked, 1 refused' "$tmp/tel_render"; then
    t_ok "telemetry reader: 'jichi telemetry' renders the dirty-dispatch guard line"
else
    t_fail "telemetry reader: $(head_bytes 400 "$tmp/tel_render")"
fi

# 9: the product DEFAULT is warn -- with NO strictness key at all the guard
# still checks: a write subagent on a dirty tree runs and carries the warning.
run_leg "$tmp/cd" "$tmp/cd.json" 1 absent "DDEF dispatch the delegate"
if cap_forked "$RL_CAP" && cap_has "$RL_CAP" 'dirty.txt' \
   && $G -q '"event":"dirty_dispatch"' "$tmp/cd/telemetry.jsonl"; then
    t_ok "default: with no strictness key the guard is warn (runs + warns + emits)"
else
    t_fail "default: forked=$(cap_forked "$RL_CAP" && echo y || echo n)"
fi

# 10: the lenient read -- a typo must not tighten or loosen the fence (M519):
# "refuse!" is prose, falls through to the default warn, NOT refuse. A real
# refuse would be NO-FORK; the typo mode ran the subagent (forked=y) and
# carried the warn note naming the path.
run_leg "$tmp/ct" "$tmp/ct.json" 1 'refuse!' "DDTR dispatch the delegate"
if cap_forked "$RL_CAP" && cap_has "$RL_CAP" 'dirty.txt'; then
    t_ok "lenient: a typo mode ('refuse!') falls to the default warn -- it ran and warned"
else
    t_fail "lenient: forked=$(cap_forked "$RL_CAP" && echo y || echo n)"
fi

# 11: the typo must be LOUD (M1006-a, refinement 1): the config-load NOTICE.
# A present-but-unknown strictness.dirtyDispatch value falls through to warn
# (the M519 rule -- the fence is not tightened) AND says so at load: a notice
# naming the key, the unknown value and the mode in effect. `doctor` is the
# headless command whose stderr is grepped by design -- a `-q` run (run_leg)
# sets the log level to NONE before config load and cannot carry the notice.
_ws11=$(smoke_tmp)
make_git "$_ws11" 0
rm -rf "$tmp/c11"; mkdir -p "$tmp/c11"
write_config "$tmp/c11.json" "$MM_PORT" '' ''
write_strictness "$tmp/c11.json" 'refuseX'
( cd "$_ws11" && with_deadline 60 "$BIN" doctor --config "$tmp/c11.json" \
    < /dev/null > "$tmp/c11/doc" 2>&1 )
if $G -q 'strictness.dirtyDispatch' "$tmp/c11/doc" \
   && $G -q 'refuseX' "$tmp/c11/doc" \
   && $G -q 'unknown' "$tmp/c11/doc" \
   && $G -q 'warn.*in effect' "$tmp/c11/doc"; then
    t_ok "typo is loud: the config-load notice names the key, the value and the mode in effect"
else
    t_fail "typo is loud (notice): $(head_bytes 400 "$tmp/c11/doc")"
fi

# 12: the typo must be LOUD (refinement 1, effect side): `jichi doctor`
# prints the EFFECTIVE mode in every mode, and when the value fell through it
# names the reason -- the operator checks the drive by effect, not by reading
# the config. Same typo config as leg 11.
if $G -q 'dirty-tree dispatch guard: warn' "$tmp/c11/doc" \
   && $G -q 'strictness.dirtyDispatch' "$tmp/c11/doc" \
   && $G -q 'refuseX' "$tmp/c11/doc"; then
    t_ok "doctor reports the effective mode and the fell-through reason (dirty-tree dispatch guard: warn)"
else
    t_fail "doctor effective-mode line: $(grep 'dispatch guard' "$tmp/c11/doc" | head -1; head_bytes 300 "$tmp/c11/doc")"
fi

# 13: a git FAILURE in a git workspace is UNKNOWN, not clean (refinement 2).
# The breakage: .git/index is corrupted (one printf of garbage). Proven by
# hand before wiring: `git status --porcelain` exits 128 ("index file smaller
# than expected") while `git rev-parse --is-inside-work-tree` STILL succeeds
# (rc 0) -- so the workspace reads as a git repo whose status cannot be
# determined: unknown, not the non-git "not dirty" case. (chmod 000 .git is
# NOT the case: it makes rev-parse fail too, which reads as non-git.) The
# dispatch PROCEEDS (req.2 is the inverted no-fork proof: forked=y), the
# result carries the unknown note, and telemetry says dirty="unknown" and
# outcome="proceeded_unknown" -- never dirty:false for a failed probe.
_ws13=$(smoke_tmp)
make_git "$_ws13" 1
printf 'garbage\n' > "$_ws13/.git/index"   # break git's status, keep its repo
rm -rf "$tmp/c13"; mkdir -p "$tmp/c13"
mm_start "$tmp/s.mm" "$tmp/c13" 30
write_config "$tmp/c13.json" "$MM_PORT" '' ''
write_strictness "$tmp/c13.json" refuse
( cd "$_ws13" && with_deadline 90 "$BIN" --config "$tmp/c13.json" -q --auto \
    --log "$tmp/c13/telemetry.jsonl" \
    -p "DDUT dispatch the delegate" < /dev/null > "$tmp/c13/out" 2>&1 )
mm_stop
if cap_forked "$tmp/c13" \
   && cap_has "$tmp/c13" 'state is unknown' \
   && $G -q '"dirty":"unknown"' "$tmp/c13/telemetry.jsonl" \
   && $G -q '"outcome":"proceeded_unknown"' "$tmp/c13/telemetry.jsonl"; then
    t_ok "git failure is unknown: the write subagent ran (proceeded) with the unknown note and dirty=\"unknown\"/proceeded_unknown"
else
    t_fail "git failure is unknown: forked=$(cap_forked "$tmp/c13" && echo y || echo n) note=$(cat "$tmp/c13"/req.* 2>/dev/null | $G -c 'state is unknown'); $(cat "$tmp/c13/telemetry.jsonl" 2>/dev/null | $G 'dirty_dispatch' | head_bytes 400)"
fi

# 14: a workspace path with a SINGLE QUOTE (refinement 3). The probe spawns
# git with an argv array, so `git -C <root>` gets the path intact. The pre-fix
# shell-built string broke (sh: unterminated quoted string, rc=2 -- proven by
# hand) and read as not-dirty, so the guard did NOT fire and the subagent ran:
# that is the red line. Fixed: dirty=yes, refuse fires, no fork, and the
# refusal names the path.
_ws14="$tmp/ws'q"
if [ -d "$_ws14" ]; then rm -rf "$_ws14"; fi
make_git "$_ws14" 1
rm -rf "$tmp/c14"; mkdir -p "$tmp/c14"
mm_start "$tmp/s.mm" "$tmp/c14" 30
write_config "$tmp/c14.json" "$MM_PORT" '' ''
write_strictness "$tmp/c14.json" refuse
( cd "$_ws14" && with_deadline 90 "$BIN" --config "$tmp/c14.json" -q --auto \
    --log "$tmp/c14/telemetry.jsonl" \
    -p "DDQT dispatch the delegate" < /dev/null > "$tmp/c14/out" 2>&1 )
mm_stop
if ! cap_forked "$tmp/c14" \
   && $G -q '"dirty":"yes"' "$tmp/c14/telemetry.jsonl" \
   && $G -q '"outcome":"refused"' "$tmp/c14/telemetry.jsonl" \
   && cap_has "$tmp/c14" 'dirty.txt'; then
    t_ok "quoted path: the argv probe saw the dirty tree and refuse fired (no fork, path named)"
else
    t_fail "quoted path: forked=$(cap_forked "$tmp/c14" && echo y || echo n) events=$(cat "$tmp/c14/telemetry.jsonl" 2>/dev/null | $G -c dirty_dispatch); $(cat "$tmp/c14/telemetry.jsonl" 2>/dev/null | $G 'dirty_dispatch' | head_bytes 400)"
fi

t_done
