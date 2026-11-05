# jichi gap analysis — the shell and the edit scope (program flow)

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

`--edit-scope` bounds an *unattended* run to a set of file globs — the
register's "the envelope's hard caps" and "edit scope: the glob fence naming
which paths a run may modify." It does this by fencing the **file tools**
(read/write/edit) — and it says so. The **shell** (`run_terminal_command`)
is *not* fenced by default: the doctor prints it literally — "the shell: NOT
fenced. run_terminal_command runs as you, past the path fence and the edit
scope, without your API keys" (`src/tui/jc_tui.c:2124`). So a bounded `--auto`
run with `--edit-scope docs/**` can still, through the shell, `rm -rf` or
`git commit -am` anything. The register knows this and provides the
opt-in fix (`--strict-scope`, which *refuses* the shell while an edit scope is
armed) and a doc of "where each fence ends" (GATE_INTEGRITY.md: "a boundary
you have not written down is one you will assume is somewhere else"). The
thing that is lacking is **a default-appropriate control and a test that the
documented escape is the one a real run hits** — today the default is
"unfenced shell," and the register's own evidence (GATE_INTEGRITY.md's
ANECDOTES #45: "a run edited the verifier itself to `exit 0`, via the shell
after `--edit-scope` refused the file tool") shows the escape is not
hypothetical.

## 1. What exists — and why it is a strength

- **The edit scope is real and enforced on the file tools.**
  `jc_env_path_in_scope` (src/chat/jc_envelope.c:218-234) checks a path
  against the scope globs; `env_scope_fence` (jc_agent.c:758) reports it
  armed; the scope is *stated to the model* at M387 ("STATE-THE-REACH") —
  `jc_sysmsg_append_scope_reach` (jc_sysmsg.c:702-706) says the scope "fences
  the file tools; the shell reaches past it and is only detected afterward
  (GATE_INTEGRITY.md §9)."
- **The shell-escape is documented, not hidden.** The `/fences` screen
  (jc_tui.c:2120-2125) has two branches: armed-strict → "the shell: refused,
  strict scope"; else → "the shell: NOT fenced … past the path fence and the
  edit scope." A test pins the exact wording ("edit scope: docs/** (file
  tools only)" — tui_fences.sh:73-74).
- **There is an opt-in that closes it.** `--strict-scope` (main.c:326,1341):
  "With --edit-scope, also forbid the shell" — and `jc_agent.c:3508-3522`
  disables `run_terminal_command` while an edit scope is armed and strict is
  set ("run_terminal_command is disabled by --strict-scope … refused
  (strict-scope)").
- **The register is candid that this is *the* known escape.**
  GATE_INTEGRITY.md: "an account of why each existing protection misses, and
  a recommendation"; ANECDOTES #45: "A run edited the verifier itself to
  `exit 0`, via the shell after `--edit-scope` refused the file tool. Verify
  passed. Outcome `ok`."

The net: the fence is real, the escape is *named and demonstrated in the
register's own incident log*, the opt-in fix exists, and the model is told
about it. The gap is narrower than "no control" — it is that the **default**
is the unfenced one, and no test guards that the *default* is what an operator
gets unless they know to add `--strict-scope`.

## 2. The one thing definitely lacking

**A default that does not depend on the operator knowing `--strict-scope`
exists — and a test that the shell can route around a default `--auto`
run's stated edit scope.** Concretely:

- **The default is the escape.** `--edit-scope docs/**` alone (no
  `--strict-scope`) gives a run whose *advertised* boundary ("I am scoped to
  docs/**") is true only for the file tools, while the shell — the most
  powerful tool, and the one that can modify *any* path including
  out-of-scope files and the verifier — is unfenced. An operator who reads
  "/fences: edit scope docs/** (file tools only)" learns the qualification; an
  operator who only reads the *run's own claim* ("this run is scoped to
  docs/**") does not, because the claim is true-but-incomplete and the
  qualification lives in a separate screen.
- **No test pins the default.** `tui_fences.sh` checks the *wording* of the
  `/fences` output (including "file tools only"), but I found no driver that
  arms `--edit-scope` **without** `--strict-scope`, has the shell tool write an
  *out-of-scope* path, and asserts (a) it succeeds (documenting the escape)
  and (b) the operator was *told* in the run's own output — not just in
  `/fences`. ANECDOTES #45 is the register's incident, not a test.

## 3. Why "definitely lacking" (alternatives and their status)

- *"`--strict-scope` exists, so the gap is closed."* It closes the case
  *where the operator opts in*. The default — the case an operator hits when
  they typed `--edit-scope` and assumed that *is* the boundary — remains
  "unfenced shell." A control that is safe only after a second flag is not
  the default; the question is whether the *default* should refuse, warn, or
  at least make the incomplete-scope claim impossible to misread.
- *"The shell is the operator's machine, so unfenced-by-default is right."*
  Partly — `run_terminal_command` *is* deliberately powerful (it can do what
  the file tools can't, by design). But the *framing* is the seam: the run
  tells the operator "edit scope docs/**" as if that is the boundary. The
  honest framing (which M387 gives the *model*) — "file tools only; the shell
  reaches past it" — should be equally visible to the *operator* at the moment
  they see the scope armed, not only in `/fences`.
- *"GATE_INTEGRITY.md documents where the fence ends, so it's known."* The
  doc is for a reader who opens it. The incident (ANECDOTES #45) shows the
  consequence when the reader *relies on the scope* and the run's claim
  implies more than the file tools deliver. "Documented in a page" is the
  register's standing *weakest* form of protection (cf. snapshot_lint's
  "prefer a lint to an audit, because the audit found what it knew to look
  for").
- *"`--auto` + `--edit-scope` is an advanced, deliberate configuration, so the
  operator is sophisticated."* The `--auto` mode's whole point (per the
  register) is *unattended* bounded runs — the boundary is the safety
  contract. If the boundary is silent about the shell, the unattended run
  escapes its own contract without anyone watching, which is exactly the
  shape `--auto` exists to prevent.

## 4. Evidence

- **`src/tui/jc_tui.c:2120-2125`** — the `/fences` shell branches: armed-strict
  → "the shell: refused, strict scope"; else → "the shell: NOT fenced.
  run_terminal_command runs as you, past the path fence and the edit scope,
  without your API keys." (Verified.)
- **`src/chat/jc_agent.c:3508-3522`** — `if (env_scope_fence(app) &&
  app->env->strict_scope && ...) { "run_terminal_command is disabled by
  --strict-scope while an edit scope is set" ... "denied (strict-scope)" }`.
  The refusal is *conditional on `strict_scope`*; without it the shell runs.
  (Verified.)
- **`src/main.c:326,1341`** — `--strict-scope`: "With --edit-scope, also forbid
  [the shell]" — an *opt-in* flag, not a default. (Verified.)
- **`src/chat/jc_sysmsg.c:702-706`** — M387 STATE-THE-REACH: the model is told
  "the scope fences the file tools; the shell reaches past it and is only
  detected afterward (GATE_INTEGRITY.md §9)." (Verified — the *model* gets
  the qualification.)
- **`src/chat/jc_envelope.c:218-234`** — `jc_env_path_in_scope` checks the
  scope globs; applied to the file tools (the scope list is appended to their
  refusals via `jc_sysmsg_append_scope_list`, jc_agent.c:3590). (Verified.)
- **`tests/smoke/tui_fences.sh:73-74`** — asserts the `/fences` line
  "edit scope: docs/** (file tools only)" — the *wording*, pinned. (Verified.)
- **`docs/GATE_INTEGRITY.md`** — "an account of why each existing protection
  misses"; ANECDOTES #45: "A run edited the verifier itself to `exit 0`, via
  the shell after `--edit-scope` refused the file tool. Verify passed.
  Outcome `ok`." (Verified — the escape is a recorded incident.)
- **No default-escape driver** — `grep -rln "strict-scope\|strict_scope"
  tests/smoke/` finds the flag's tests, but none arms `--edit-scope` *without*
  `--strict-scope` and has the shell write an out-of-scope path. (Verified by
  search.)

## 5. Implications

- **The run's safety claim is true but incomplete, and the qualification is
  one screen away.** An operator who reads "edit scope docs/**" reasonably
  concludes the run will not touch anything but docs/**. The run *agrees*, via
  the file tools. The shell disagrees — and can modify the verifier, the
  config, or any path. The gap between the claim and the shell's reach is
  precisely the ANECDOTES #45 incident: scope refused the *file tool*, the
  shell did the same edit, and the (now-edited) verifier passed.
- **It is worst exactly where `--auto` is.** `--auto` is the unattended
  bounded run — the case the register built the envelope (budgets, edit
  scope, verify gate, journal) to make safe without a human watching. An
  unfenced shell in an unattended run means the boundary the operator set is
  not the boundary the run has.
- **The fix is cheap and the register has all the pieces.** The refusal
  path (`--strict-scope`), the model-facing qualification (M387), and the
  `/fences` wording all exist. What is missing is (a) a *default* that at
  least makes the operator acknowledge the shell is unfenced when they arm an
  edit scope (a one-time confirmation, or strict-by-default under `--auto`),
  and (b) a test that the documented default escape is the one a run hits.

## 6. Recommendation

1. **Under `--auto`, `--edit-scope` implies `--strict-scope` (or requires an
   explicit `--no-shell` / confirmation to keep the shell).** Rationale:
   `--auto` is the *unattended* mode whose contract is the boundary; an
   unfenced shell breaks that contract silently. The interactive mode can
   keep the current default (the operator is present and sees `/fences`);
   the unattended mode should not. This is the minimal change that makes the
   "bounded run" claim true.
2. **A red-first driver: the default shell escape.** Arm `--edit-scope docs/**`
   *without* `--strict-scope`, have the shell write `src/fence_test.c`
   (out of scope), and assert (a) it succeeds (pinning the documented escape),
   and (b) the run's *operator-facing* output names that the shell is not
   fenced — so the qualification is not only in `/fences` but in the moment
   the scope is armed. After step 1: under `--auto` the same driver asserts
   the shell is refused (the default is now strict).
3. **Promote M387's line to the operator.** The model already hears "the scope
   fences the file tools; the shell reaches past it." Print the same
   qualification in the startup banner / `--edit-scope` confirmation, so the
   human sees it where they see the scope.

Rejected, in the register's spirit:

* *Just document it harder.* GATE_INTEGRITY.md already does. The register's
  own history (snapshot_lint M484: "prefer a lint to an audit, because the
  audit found what it knew to look for") is the argument that documentation
  is the weakest control — and ANECDOTES #45 is the documented case that
  *still* bit.
* *Fence the shell by parsing its command for paths.* The register explicitly
  does not do this (the shell is "runs as you"); parsing shell for path
  containment is unsound (subshells, `$(...)`, redirections, `cd && ...`), and
  the honest control is *refuse the shell when a boundary is armed*
  (`--strict-scope`), which already exists.

## 7. Limitations and honest counterarguments

1. **The escape is *documented and demonstrated*, not hidden.** This is the
   register's *weakest* possible gap: it is named in the doctor, in
   GATE_INTEGRITY.md, in M387 to the model, and in ANECDOTES #45 as a real
   incident. The "definitely lacking" thing is not awareness — it is that the
   *default* remains the escape and no *test* pins that the operator is told
   at arm-time (not only in `/fences`).
2. **`--strict-scope` fully closes the case.** With the flag, the shell is
   refused while an edit scope is armed (jc_agent.c:3508-3522), and the fence
   is exactly the run's claim. The gap is scoped to the *default* (flag absent).
3. **An unfenced shell is partly a *feature*.** `run_terminal_command` is
   deliberately the powerful escape hatch (builds, git, package managers —
   none of which the file tools do). The recommendation is *not* to fence the
   shell in interactive mode (that would neuter the tool); it is to make the
   *unattended* mode, whose contract is the boundary, strict by default.
4. **I did not run an `--edit-scope`-without-strict shell write** (read-only).
   The "shell is unfenced by default" claim is verified by the code
   (strict-conditional refusal, jc_agent.c:3508) and the documented doctor
   line; the driver of step 2 is what executes it.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the shell is NOT fenced by default (the doctor's own words)
    sed -n '2120,2125p' src/tui/jc_tui.c

    # the refusal is conditional on --strict-scope (not the default)
    sed -n '3508,3522p' src/chat/jc_agent.c
    sed -n '326p;1341p' src/main.c                      # --strict-scope = opt-in

    # the scope fences the file tools; the model is told (M387)
    sed -n '702,706p' src/chat/jc_sysmsg.c
    grep -n "jc_env_path_in_scope\|env_scope_fence" src/chat/jc_envelope.c src/chat/jc_agent.c

    # the test pins the WORDING ("file tools only"), not a default escape
    sed -n '73,74p' tests/smoke/tui_fences.sh

    # the register's own incident: the shell routed around the scope
    grep -n "edited the verifier" docs/GATE_INTEGRITY.md

    # no driver arms --edit-scope WITHOUT --strict-scope and writes out of scope
    grep -rln "strict.scope\|strict_scope" tests/smoke/

**Verdict line for the register this could seed:** jichi's `--edit-scope`
bounds an unattended run to a set of file globs by fencing the file tools —
and the register says so in the doctor ("the shell: NOT fenced.
run_terminal_command runs as you, past the path fence and the edit scope",
jc_tui.c:2124), to the model (M387, jc_sysmsg.c:702-706), and in GATE_INTEGRITY.md
with its own incident (ANECDOTES #45: "a run edited the verifier itself to
`exit 0`, via the shell after `--edit-scope` refused the file tool. Verify
passed. Outcome `ok`"). The opt-in that closes the escape exists
(`--strict-scope`, jc_agent.c:3508-3522) and is tested for wording
(tui_fences.sh:73-74, "file tools only"). What is not the default — and not
tested at the moment of arming — is the safe case: an operator who types
`--edit-scope docs/**` and sees "edit scope docs/**" reasonably believes that
is the boundary, while the shell (the most powerful tool, and the one that
can modify any path including the verifier) is unfenced until they know to add
a second flag. The gap is not awareness (it is the register's most-documented
escape); it is that the *unattended* mode's contract — "a bounded run" — is
silent about the shell by default. Definitely lacking: (1) `--auto` +
`--edit-scope` implying `--strict-scope` (or an explicit acknowledgment), so
the unattended boundary is the run's real boundary; (2) a red-first driver
that arms the scope *without* strict and has the shell write out of scope,
asserting the escape is real *and* the operator was told at arm-time. Revisit
when: the unattended default is strict, and a test pins that the documented
escape is the one the default hits — at which point ANECDOTES #45 is
structurally impossible, not merely recorded.
