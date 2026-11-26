#!/bin/sh
# smoke: the onboarding pack (M1201, G6) -- a fuller onboarding pack, red-first.
#
# `jichi init onboarding` scaffolds five files (src/scaffold/jc_scaffold.c, the
# ONBOARDING_FILES table). This driver pins their CURRENT five-file shape --
# two read-only agents, one write agent, one command, one skill -- plus the
# "5 written, 0 skipped, 0 failed." summary the verb prints for it (cell 1),
# and then asserts the four behaviour changes B1-B4 that G6 will make to the
# pack's prose.
#
# It is RED-FIRST. Cells 2-7 are RED against the pack as it is today: the
# current skill is the five-step checklist (no `jichi describe`, no
# `check_model`, no limits rules, no `setup --list`/`init --list`), the
# `/onboard` command never says what it could not verify, and no agent says
# "Do not commit unless asked." They turn green when B1-B4 land. Cells 1, 8
# and 9 are keeping pins (they stay green through the change): cell 1 pins the
# five-file shape, cell 8 pins that the compiled `jichi guide` (a separate
# surface generated from marked page sections, not from the pack) does not
# drift when the pack's prose changes, and cell 9 pins that the rewrite does
# not loosen the pack's propose-only shape (the analyst stays `readonly: true`
# and the command still says "Recommend only").
#
# Every cell is offline: no key, no network, no model. The binary is built
# once (`make jichi -s` in this worktree, where the sources live) and then run
# as `jichi init onboarding` in a throwaway workspace. The scaffolded files are
# extracted by path and grepped IN PLACE with /usr/bin/grep -F -- their text is
# never quoted here, so a reflow of the pack's prose does not rot this driver.
# A failing cell prints WHICH string was missing.
#
# NOT CHECKED: the guide text itself (guide_lint.sh owns it); the pack's i18n
# (there is none -- the packs are compiled-in English); `setup --onboard` (a
# model call, out of scope for an offline driver).
. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
tmp=$(smoke_tmp)
ws="$tmp/ws"
mkdir -p "$ws"

# /usr/bin/grep, fixed-string, for every assertion (the tier's POSIX-clean grep).
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# Build the binary (and the C tools cell 8's guide_lint needs) once. A full
# smoke run has already built jichi + smoke-tools (make smoke depends on $(BIN)
# and $(TT_BIN)), so this is a no-op there; run standalone it guarantees the
# ./jichi and tests/tools/mockmodel exist before any cell runs. A build failure
# is not its own check -- it surfaces as cell 1 (init cannot run) or cell 8
# (guide_lint cannot start mockmodel) -- so the plan stays 9.
_mk=$(smoke_make)
(cd "$SMOKE_ROOT" && $_mk jichi -s) >/dev/null 2>&1
(cd "$SMOKE_ROOT" && $_mk smoke-tools -s) >/dev/null 2>&1

# The five files the onboarding pack scaffolds. Cell 1 pins this exact set.
f_analyst="$ws/.jichi/agents/project-analyst.md"
f_tutorial="$ws/.jichi/agents/tutorial-writer.md"
f_fetcher="$ws/.jichi/agents/data-fetcher.md"
f_cmd="$ws/.jichi/commands/onboard.md"
f_skill="$ws/.jichi/skills/onboarding-checklist/SKILL.md"

# Run the verb once; cells 1-7 and 9 read from these scaffolded files + its
# output. Cell 8 (the guide) is a separate surface, run below.
bin_exists=0
[ -x "$BIN" ] && bin_exists=1
(cd "$ws" && "$BIN" init onboarding < /dev/null) > "$tmp/init_out" 2>&1
init_rc=$?

# --- 1. (keeping pin) init wrote the five files + the "5 written" summary. ---
n_exist=0
missing=""
for f in "$f_analyst" "$f_tutorial" "$f_fetcher" "$f_cmd" "$f_skill"; do
    if [ -f "$f" ]; then n_exist=$((n_exist + 1)); else missing="$missing $(basename "$f")"; fi
done
summary=0
$G -q -F '5 written, 0 skipped, 0 failed' "$tmp/init_out" && summary=1
if [ "$bin_exists" -eq 1 ] && [ "$init_rc" -eq 0 ] && [ "$n_exist" -eq 5 ] \
   && [ "$summary" -eq 1 ]; then
    t_ok "init onboarding wrote the five files and printed '5 written, 0 skipped, 0 failed' (the current pack shape)"
else
    t_fail "init onboarding: bin=$bin_exists rc=$init_rc files=$n_exist/5 summary=$summary missing:[$missing ]-- the five-file pack shape is not as pinned"
fi

# --- 2. (RED, B1) the skill names `jichi describe`. ---
if $G -q -F 'jichi describe' "$f_skill"; then
    t_ok "the onboarding skill names 'jichi describe'"
else
    t_fail "the onboarding skill does not name 'jichi describe' (B1: read the interface contract first)"
fi

# --- 3. (RED, B1) the skill names `check_model` (the G3 live check). ---
if $G -q -F 'check_model' "$f_skill"; then
    t_ok "the onboarding skill names 'check_model'"
else
    t_fail "the onboarding skill does not name 'check_model' (B1: the G3 live check the run offers)"
fi

# --- 4. (RED, B1) the skill carries the three limits rules. ---
m_cred=0; m_out=0; m_commit=0
$G -q -F -i 'credentials' "$f_skill" && m_cred=1
$G -q -F -i 'outside the project' "$f_skill" && m_out=1
$G -q -F -i 'never commit' "$f_skill" && m_commit=1
if [ "$m_cred" -eq 1 ] && [ "$m_out" -eq 1 ] && [ "$m_commit" -eq 1 ]; then
    t_ok "the onboarding skill carries the three limits rules (credentials / outside the project / never commit)"
else
    t_fail "the onboarding skill is missing limits rule(s): 'credentials'=$m_cred 'outside the project'=$m_out 'never commit'=$m_commit (B1: the limits topic)"
fi

# --- 5. (RED, B1) the skill names `setup --list` and `init --list`. ---
m_slist=0; m_ilist=0
$G -q -F 'setup --list' "$f_skill" && m_slist=1
$G -q -F 'init --list' "$f_skill" && m_ilist=1
if [ "$m_slist" -eq 1 ] && [ "$m_ilist" -eq 1 ]; then
    t_ok "the onboarding skill names 'setup --list' and 'init --list'"
else
    t_fail "the onboarding skill is missing: 'setup --list'=$m_slist 'init --list'=$m_ilist (B1: the exact commands the trial's agent never found)"
fi

# --- 6. (RED, B2) the /onboard command says to end by reporting what it
#        changed and what it could not verify. ---
if $G -q -F 'could not verify' "$f_cmd"; then
    t_ok "the /onboard command says to end by reporting what it could not verify"
else
    t_fail "the /onboard command does not say to end by reporting what it could not verify (B2): 'could not verify' absent from commands/onboard.md"
fi

# --- 7. (RED, B3) each of the three agents carries "Do not commit unless asked." ---
n_commit=0
for f in "$f_analyst" "$f_tutorial" "$f_fetcher"; do
    $G -q -F 'Do not commit unless asked.' "$f" && n_commit=$((n_commit + 1))
done
if [ "$n_commit" -eq 3 ]; then
    t_ok "all three onboarding agents carry 'Do not commit unless asked.'"
else
    t_fail "only $n_commit of 3 onboarding agents carry 'Do not commit unless asked.' (B3: the pack must stay uniform)"
fi

# --- 8. (keeping pin, B4) guide_lint stays green. The compiled guide is a
#        SEPARATE surface (generated from marked page sections, not the pack);
#        this pins that a pack change does not drift it. Run from the repo root
#        with this driver's TMPDIR. ---
gl_rc=$(cd "$SMOKE_ROOT" && TMPDIR="$tmp" sh "$SMOKE_ROOT/tests/smoke/guide_lint.sh" > "$tmp/gl_out" 2>&1; echo $?)
if [ "$gl_rc" -eq 0 ]; then
    t_ok "guide_lint stays green (the compiled guide is a separate surface; the pack change did not drift it)"
else
    t_fail "guide_lint exited $gl_rc -- the pack change drifted the compiled guide (B4 keeping pin): $( $G -m 3 -F 'not ok' "$tmp/gl_out" 2>/dev/null | tr '\n' ' ')"
fi

# --- 9. (keeping pin) propose-only kept: the analyst is still `readonly: true`
#        and the command still says "Recommend only". ---
m_ro=0; m_rec=0
$G -q -F 'readonly: true' "$f_analyst" && m_ro=1
$G -q -F 'Recommend only' "$f_cmd" && m_rec=1
if [ "$m_ro" -eq 1 ] && [ "$m_rec" -eq 1 ]; then
    t_ok "propose-only kept: the analyst is still 'readonly: true' and the command still says 'Recommend only'"
else
    t_fail "propose-only loosened by the rewrite: analyst 'readonly: true'=$m_ro, command 'Recommend only'=$m_rec"
fi

t_done
