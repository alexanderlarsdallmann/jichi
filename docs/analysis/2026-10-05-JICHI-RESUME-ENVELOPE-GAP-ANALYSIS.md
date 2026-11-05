# jichi gap analysis — resuming a session loses its run bounds (data flow)

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

A bounded `--auto` run's safety envelope — the edit scope, the token/wall-clock
budgets, the verify gate, and `--strict-scope` — is built **only from the
current invocation's CLI flags** (`src/main.c:9227` `envelope_arm`, called at
`17391`; `intro_arm_env`, `main.c:9365`, whose comment at `9357` states the
invariant plainly: "this invocation arms none" when the flags are absent). The **session file holds none
of it** — it serializes messages, tool calls and todos
(`src/session/jc_session.c`), with no `editScope`/`budget`/`deadline`/`verify`/
`strictScope` keys. So a run you *resume* with `--continue` (the register's
mechanism for "run indefinitely," and the one compaction exists to sustain)
comes back with `app->env == NULL`: **no edit scope, no budgets, no verify
gate, no strict-scope** — while the *conversation* that was operating under
those bounds is restored. The boundary is real and deliberate (the envelope is
a per-invocation contract, not state), but it is **not documented as such, and
not warned about** at resume: nothing tells the operator that their resumed run
is unscoped, and no test drives a scoped run, resumes it, and checks the
bounds are gone.

## 1. What exists — and why it is a strength

- **The envelope is real and enforced.** Edit scope (`jc_env_path_in_scope`,
  `jc_envelope.c:218-234`), budgets (`jc_env_over_budget`, `:241-260`), a
  verify gate with rollback (`jc_env_should_verify_now`, `:494`), and
  `--strict-scope` (the shell refusal, `jc_agent.c:3508-3522`). The
  `SESSION_RUNBOOK.md` treats them as first-class ("Keep the fences";
  "`editScope` already fences it out of `src/`").
- **The per-invocation nature is *consistent*.** `intro_arm_env`
  (`main.c:9365`; its "this invocation arms none" comment at `9357`) and the
  arming block (`17388-17391`) build the envelope
  purely from `args`/`app` for *this* invocation; introspection subcommands
  (`sysmsg`/`context`) get a journal-less arm or none. There is exactly one
  source of bounds: the CLI. That is clean and predictable — the confusion is
  not in the envelope, it is in the *resumption* silently dropping it.
- **The session persistence is real.** `--continue`/`--resume` restores the
  conversation, the todos, the `[undo]` notes (M349 saves immediately "so a
  later `--continue` cannot revive the stale beliefs"). State *is* durable.

The net: bounds and state are each sound. The gap is the **seam between
them**: state survives a resume, bounds do not, and nothing says so.

## 2. The one thing definitely lacking

**Documentation of the boundary, and a warning/test that a resumed run is
unbounded.** Concretely:

- **Nothing tells the operator their resume dropped the scope.** Enabling
  `--edit-scope` + `--auto` + a verify gate, hitting a budget or context
  limit, and resuming with `--continue` yields a run whose *conversation*
  says "this run is scoped to X" (the M387 scope-reach paragraph is in the
  history) but whose *actual* bounds are `NULL`. The resumed run can write
  out of scope, run unbounded, and skip the verify gate — with no startup
  line saying "resumed without an envelope; re-pass --edit-scope / --budget
  to re-arm."
- **No test drives it.** I found no driver that (a) arms an envelope, (b)
  resumes the session, and (c) asserts `app->env == NULL` (or that the
  resume *warns*). The envelope tests arm fresh flags; the session/continue
  tests restore state; the two never meet in a test.

## 3. Why "definitely lacking" (alternatives and their status)

- *"The envelope is per-invocation by design, so a resume re-arming it is the
  operator's job."* Correct as a *mechanism* — but "the operator's job to know
  to re-pass the flags" is precisely the class of silent boundary the register
  has been closing everywhere else: the shell-escape is *stated* to the model
  (M387) and in `/fences`; the ignored-file hole is *stated* in SNAPSHOTS.md;
  the one-shot repo map is *stated* in REPOMAP.md. The resume-drops-the-bounds
  case is the one that is *not stated anywhere* — it is the operator's job with
  no note. The register's own pattern (a boundary you have not written down is
  one you will assume is somewhere else, GATE_INTEGRITY.md) applies: the
  natural assumption is "my resumed run still has its scope," and that
  assumption is wrong.
- *"Compaction / `--continue` are for *long conversations*, and a scoped
  `--auto` run is meant to finish in one invocation."* Fair in the happy
  case. But the register *built* compaction to "keep a session running
  indefinitely," and a bounded run that hits its budget at turn 40 and is
  resumed to *continue* is exactly that case — now unbounded. The tension
  (state made durable to resume, bounds not made durable) is the gap, and it
  bites precisely on the resume-the-bounded-run flow.
- *"An operator who resumes deliberately re-passes the flags."* For a
  *deliberate* re-arm, yes. The gap is the *accidental* resume — the one where
  the operator thinks `--continue` picks up where the scoped run left off, and
  the run's own history (the M387 scope paragraph, the journal's
  "edit_scope: N" event) *asserts* the scope is in force when it is not.
- *"Make the envelope durable too (persist editScope/budgets into the
  session)."* Rejected as the *fix* — it would resurrect a *stale* budget
  (a resume should not inherit the remaining tokens of a run that hit its
  cap), and a stale scope (the operator may have changed it). The honest fix
  is to *not* silently drop the bounds but *say so and re-arm explicitly*.

## 4. Evidence

- **`src/main.c:9227`** — `static void envelope_arm(struct cli_args *a,
  struct jc_app *app, ...)`; **`:17388-17391`** — the arming block calls it
  with the current `args`/`app` ("M444: the arming moved into envelope_arm").
  (Verified.)
- **`src/main.c:9357,9365`** — `intro_arm_env` (def at 9365): "arm a
  JOURNAL-LESS envelope for an introspection subcommand, or return 0 if
  **this invocation arms none**" (comment 9357) — `if (a == NULL ||
  !envelope_is_armed(a, app)) return 0;` (9370). The bounds come
  from *this* invocation's args; a resume with no such flags arms none.
  (Verified.)
- **`src/session/jc_session.c`** — the session serializes `content`, `role`,
  `toolCalls` (with `arguments`), `status` (todos), `isError`, `truncated`
  (`:182-271`). **No `editScope`, `budget`, `deadline`, `verify`, or
  `strictScope` key** (verified: grep of the session writer finds none of the
  bound keys). (Verified.)
- **`grep -rn "continue\|resume" src/main.c | grep -i "env"`** — empty
  (verified): the resume path does not re-arm the envelope from the session;
  it only restores the conversation.
- **`docs/SESSION_RUNBOOK.md:240-259`** — the envelope is treated as the
  run's contract ("Keep the fences, drop the budgets"; "`editScope` already
  fences it out of `src/`"); but **no line addresses what happens to the
  envelope on resume** (verified: grep for `envelope|scope|budget|verify`
  against `resume|continue` in the runbook and session docs finds no
  resume-scoping statement).
- **`src/chat/jc_agent.c:5323-5324`** — the run's *journal* records
  `"edit_scope": N` — so the journal (and the history) *assert* a scope was in
  force, while a resume drops it (the dissonance the gap is about). (Verified.)
- **No resume-drops-bounds driver** — `grep -rln "continue\|resume" tests/smoke/ | xargs grep -ln "edit.scope\|budget\|verify"`
  finds no test that resumes a scoped run and checks the bounds (verified by
  search).

## 5. Implications

- **The resumed run's history lies to the operator.** The M387 scope-reach
  paragraph, the `edit_scope: N` journal event, and any budget-notice the
  original run emitted are all *in the restored conversation*. They describe
  bounds that the resumed run does **not** have. An operator (or a model
  re-reading the history) reasonably concludes the scope is still in force;
  it is not.
- **It compounds the other bounded-run controls.** The verify gate (rollback on red),
  the budgets (the envelope's "hard caps"), and `--strict-scope` (the shell
  refusal) all vanish on resume. A scoped, verified, budgeted `--auto` run
  that is resumed becomes unscoped, unverified, and unbudgeted — the exact
  run that *most* needs its bounds is the one that loses them.
- **It is the register's own "run indefinitely" feature biting back.**
  Compaction and `--continue` exist so a session outlives a context window;
  the cost is that the *safety contract* does not outlive the *invocation*.
  The register has made state durable and left bounds per-invocation, without
  saying the two have different lifetimes.

## 6. Recommendation

1. **Warn at resume if the prior run was bounded.** On `--continue`/`--resume`,
   if the restored session's journal/history records an envelope
   (`edit_scope > 0`, budgets, a verify gate, or strict-scope) *and* the
   current invocation did not re-arm one, print a one-line startup notice:
   "resumed session was scoped (edit-scope, verify gate, budgets); the
   envelope is per-invocation and is NOT restored — re-pass --edit-scope /
   --verify / --budget / --strict-scope to re-arm." This is the M387
   state-the-reach move applied to the resume seam, and it makes the boundary
   visible at the moment it would otherwise be silently crossed.
2. **A red-first driver.** Arm `--edit-scope src/**` + a verify gate, run a
   turn, capture the session, then `--continue` it *without* the flags and
   assert: (a) `app->env == NULL` (the bounds are gone — pinning the real
   behavior), and (b) the resume emitted the step-1 notice. Before the notice:
   red (no warning). After: green.
3. **One line in SESSION_RUNBOOK.md** (or a RESUME doc) next to "Keep the
   fences": the envelope is **per-invocation, not resumed** — a `--continue`d
   run re-arms only from the flags on that invocation.

Rejected, in the register's spirit:

* *Persist the envelope into the session.* It would resurrect a *stale*
  budget (remaining tokens from a capped run) and a *stale* scope — both
  dangerous to restore silently. The bounds are a *contract with the operator
  who typed them*, not state; the fix is to say when the contract lapses, not
  to fake that it survived.
* *Auto-re-arm a default envelope on resume.* Inventing bounds the operator
  did not pass (a default scope, a default verify gate) is worse than
  warning — it would make the resumed run *seem* bounded by flags that were
  never chosen. Warn, don't guess.

## 7. Limitations and honest counterarguments

1. **The per-invocation design is *correct*, not a bug.** I am not proposing
   to change it. The gap is that a correct, deliberate boundary is (a) not
   documented and (b) not warned about at the seam where it silently applies —
   the resume. The "definitely lacking" thing is the *visibility* of a
   sound design.
2. **Many resumes are interactive, not bounded runs.** A plain `--continue`
   of a chat session has no envelope to drop; the gap only bites when the
   *prior* run was bounded and the *resume* is not. I scope the finding to
   that case (a resumed `--auto`/scoped run), which is where the dissonance
   is worst.
3. **I did not run a scoped-then-resumed session** (read-only). The "the
   session file has no bound keys" claim is verified by reading
   `jc_session.c`; the "a resume leaves `app->env == NULL`" claim is verified
   by the arming being CLI-only (`main.c:9227,17391,9365`) and the empty
   `continue|resume … env` grep. Step 2's driver executes it.
4. **The journal already records `edit_scope` (jc_agent.c:5323), so the
   information *is* available** at resume time — the fix (step 1) does not
   need new state; it needs to *read the prior run's journal* and compare it
   against the current invocation's flags. I note this because it makes the
   warning cheap and the "can't detect it" objection moot.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the envelope is built only from the current invocation's flags
    grep -n "static void envelope_arm" src/main.c          # 9227
    grep -n "envelope_arm(&args" src/main.c                # 17388-17391
    sed -n '9357,9370p' src/main.c                         # intro_arm_env: "this invocation arms none"; the armed check

    # the session file holds conversation, NOT bounds
    grep -n "cJSON_Add.*ToObject\|AddStringToObject\|AddBoolToObject" src/session/jc_session.c | head
    grep -n "editScope\|budget\|deadline\|verify\|strictScope" src/session/jc_session.c   # (expect empty)

    # the resume path does not re-arm the envelope
    grep -rn "continue\|resume" src/main.c | grep -i "env"   # (expect empty)

    # the run's journal DOES assert the scope was in force (the dissonance)
    grep -n "edit_scope" src/chat/jc_agent.c                 # 5323

    # the runbook treats the envelope as the contract but says nothing of resume
    grep -n "editScope\|budget\|Keep the fences" docs/SESSION_RUNBOOK.md | head
    grep -rn -i "resume\|continue" docs/SESSION_RUNBOOK.md | grep -iE "envelope|scope|budget|verify"   # (expect empty)

    # no test resumes a scoped run and checks the bounds
    grep -rln "continue\|resume" tests/smoke/ | xargs grep -ln "edit.scope\|budget\|verify"   # (expect none)

**Verdict line for the register this could seed:** jichi's bounded `--auto`
run carries a real, enforced safety envelope — edit scope (jc_envelope.c:218),
budgets (jc_env_over_budget), a verify gate with rollback, and --strict-scope
(jc_agent.c:3508) — and the SESSION_RUNBOOK treats it as the run's contract
("Keep the fences"). The envelope is built **only from the current
invocation's CLI flags** (envelope_arm, main.c:9227,17391; intro_arm_env,
main.c:9357,9365: "this invocation arms none"), and the **session file holds none
of it** (jc_session.c serializes messages/tool calls/todos with no
editScope/budget/deadline/verify/strictScope key) — so a run resumed with
--continue, the register's own "run indefinitely" mechanism, comes back with
app->env == NULL: no scope, no budgets, no verify gate, no strict-scope. The
dissonance is that the resumed conversation *still asserts* the bounds — the
M387 scope-reach paragraph is in the history and the journal records
"edit_scope: N" (jc_agent.c:5323) — while the run that carries them has them
no longer. The boundary is deliberate and sound (bounds are a contract with
the operator who typed them, not state to resurrect); what it lacks is
*visibility* at the seam where it silently applies: no documentation that the
envelope is per-invocation-and-not-resumed, no startup warning when a prior
bounded run is resumed unbounded, and no test that drives the flow. Definitely
lacking: (1) a one-line resume notice when the prior run's journal shows
bounds the current invocation did not re-arm; (2) a red-first driver that
arms a scope, resumes without the flags, and asserts app->env == NULL *and*
the notice. Revisit when: resuming a bounded run says so, and a test proves
the bounds lapse at resume — at which point the "run indefinitely" feature and the "bounded
run" contract stop quietly disagreeing about what survives a --continue.
