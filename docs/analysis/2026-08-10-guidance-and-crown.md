# How jichi guides, what carries the guidance, and where the crown is — 2026-08-10

Five questions asked together, answered from the code (M201: read before
believing). This note grounds M360 and records the finite-state-machine and
procedural-generation determinations.

## 1. How does guidance actually flow — questions and grounded recommendations?

jichi guides two audiences through the software process, and the mechanisms
are different:

**Toward the human**, the guidance is *interrogative at the edges, measured in
the middle*: `setup` asks role/journey questions and answers them with a
preset recipe (M48; the accept-defaults shortcut IS a recommendation);
`doctor` never recommends without measuring first — every WARN names the exact
config key that fixes it, and `--live` sends one real request before judging
tool-calling (M167c); `learn analyze` mines the agent's own telemetry into
lessons a human reviews before anything is applied (propose-only, M70); the
exit summary after `setup` names what was declined with the keys to add later
(M357 added pricing exactly when telemetry is armed). The curriculum and
tutorials are the long-form of the same stance: measured claims, checkable
steps, ungraded judgment.

**Toward the model**, guidance is *layered by strength* (the M356 ladder):
the craft prompt asks for design-before-implementation and for questions
"only about what the code cannot answer" (M299); tool descriptions carry
trigger shapes and counter-cases with the capability itself (M356); the
notice family delivers grounded, in-the-moment corrections — the budget bell
(M347), the flight plan (M355), the context gauge (M358), repair notes
(M353), drift notes (M350), the verify-gate declaration (M343) — every one
rendered from the run's own numbers so the advice cannot disagree with the
mechanism. `ask_user` is the model's channel *back* to the human, and M359
put its unanswered questions into the run's record.

**Is this dependent on AGENTS.md or skills?** No — they strengthen it, they
do not carry it. The floor is built in: craft prompt + tool descriptions +
doctor + the notice family all work in an empty project. AGENTS.md is
standing *advice* (read every turn, obeyed statistically); skills are named
recipes the model can pull (and enforce a tool fence only via
`restrict-tools` in a subagent, W2); scaffolded commands are *demonstrated
structure*, the strongest signal (`/investigate`). The dependency order is:
built-in floor < AGENTS.md policy < skill recipes < scaffolded demonstration
< the explicit prompt.

## 2. The small-model stack — and how limited tool calling is mitigated

This is jichi's most distinctive engineering thread. Inventory, by failure
mode:

- **Window too small for the toolset**: `toolProfile` core (M74) advertises 8
  tools instead of ~16, auto-selected under `--lite` or a sub-12k context;
  the subagent spawn tools are dropped entirely (no fan-out on a tiny
  window). System-prompt fitting caps rules + repo map (M73); compaction is
  chunked to the *summarizer's* window (M30); calibration learns each
  model's real bytes-per-token within its first turn (M77); the context
  gauge tells the model itself (M358).
- **Malformed tool calls**: conservative validated repair (`jc_jsonrepair`,
  M148) with a per-call honesty note (M353); unrepairable args get an error
  echoing the tool's own schema; a call narrated as prose instead of invoked
  is detected and nudged once per turn (M147); alias resolution accepts the
  names models actually emit (`glob`, `todo_write` — M219/M324) while fences
  stay exact (M285 documents the asymmetry); an unknown name gets a
  did-you-mean from the shared distance predicate (M345).
- **Wire fragility**: the placeholder-skip invariant (M166 — an empty
  trailing assistant turn makes a small model end its turn without calling
  anything); `doctor --live` classifies native/text/none tool calling and
  words its advice request-first (M167c); the bench (tests/bench) exists
  because a small strict model surfaces malformed requests a frontier model
  silently absorbs.
- **Capability ceilings**: fast-first routing escalates to `strong` on
  verify-fail/tool-error/stall (top-level only); the small-local preset sets
  the whole lean profile in one word (M150).

The remaining measured gap: in-turn repeat failures (the three-seams work);
the re-measure is scheduled for 2026-08-14 and deliberately not preempted.

## 3. The crown — jichi's most important features

Judged by what would be hardest to rebuild and what the rest leans on:
(1) the **autonomy envelope** — budgets, edit scope, verify gate with
fix-forward and green-checkpoint banking, journal, rollback decisions: it is
what makes unattended runs *boundable*; (2) the **observability triad** and
its offline readers (`telemetry`/`runs`/`audit`) — nothing else in the
ecosystem treats the agent's own behavior as a measurable workload; (3) the
**safety stack** (modes, path fence + reference roots, privileged/kinetic
gates, snapshots/undo/rewind); (4) the **compaction + calibration** stack
that makes small models viable at all; (5) the **learning loop**, which turns
(2) into durable lessons.

**The polish chosen (M360)**: where 1, 2 and the small-model thread meet, the
weakest surviving message is the tool-fence refusal. A model calling a tool
hidden by the core profile or a subagent fence is told "Tool not permitted by
this agent's allowed-tools." — a cause with **no way forward**, the exact
message class M342 showed amplifies retry loops. The policy-deny and
read-only refusals share the defect. M360 makes every fence refusal name the
way forward (the bounded list of tools that ARE available; what to do
instead), rendered by a pure, unit-tested helper.

## 4. Finite-state machines — determination

jichi is full of *implicit* state machines: the agent loop's
stream→tools→verify→compact cycle, the envelope outcome lattice
(green/red × completed/budget/interrupted × kept/rolled-back), SSE framing,
the TUI editor, control-channel pause/resume, mockmodel's rule engine.

**Recommendation: no FSM framework — keep the house idiom of pure decision
tables with exhaustive tests, and adopt it exactly where a transition bug has
actually bitten.** The precedents are already in the tree: M304's
`jc_perm_mode_rank` (an explicit safety-order table with all nine ordered
pairs tested, adopted because the enum's numeric order INVERTED the
property); M343's `verify_kind` enum + baseline verdict table; M80's
`jc_env_budget_rollback_decision`. Each is an FSM transition function in
plain C89 — enum in, enum out, exhaustively testable — without a framework's
indirection. A generic FSM engine would freeze the agent loop's transitions
behind a table exactly where its flexibility (hooks, notices, gates slotting
in between states) is the point, and C89 offers no cheap dispatch sugar that
would pay for itself. Where the next transition bug appears, the fix is
another named pure table + exhaustive pairwise test, recorded in
DECISIONS.md — not an engine.

## 5. Procedural generation — determination

jichi already ships the kinds of procgen that pay off in software
development, all deterministic: the scaffold packs (compiled-in generation of
project assets), `setup`'s config/start-script builders, mockmodel's scripted
SSE responses (generating provider behavior for tests), the smoke drivers'
fixture builders (M358's eight distinct pressure subjects), and the
`jc_assign` bench specs. The useful common property: **generation is seeded
by explicit inputs and byte-reproducible**, which is why it can sit under
tests.

**Recommendation**: the one genuinely promising addition is a *seeded*
fuzz-lite harness for the pure cores (`jc_patch`, `jc_utf8`, `jc_jsonrepair`,
`jc_glob_match`) — a tiny C89 LCG, fixed seeds committed with the tests, so
"random" inputs are reproducible and a failure is a permanent regression
case. Deferred, not built now: the pure cores' current failure findings come
from real workloads (the honest source), and a fuzz harness deserves its own
milestone with its own teeth. LLM-driven generation (test data, fixtures) is
already available through the agent itself and needs no C support. Recorded
in DEFERRED.md.
