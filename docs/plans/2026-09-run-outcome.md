# Five things, and the one design change underneath four of them

*Design, 2026-09-21 (M688–M691). Written after a session in which **five
milestones were spent on defects that were not in jichi** — they were in the
instruments that report on jichi. This page argues that four of the five items
below are symptoms of one missing abstraction, names it, and says how to build
it without a flag day.*

---

## 0. The evidence, measured before the design

Eight stop reasons (`done`, `interrupted`, `timeout`, `error`, `budget`,
`verify_failed`, `scope_tainted`, `max_iters`) are reported on **five surfaces**:

| surface | where |
|---|---|
| stdout | `run_headless` — the raw answer, and nothing else (M73) |
| exit code | `main.c`, the `env_active_run` block |
| reach footer | `jc_reach.c:reach_halves()` — "checked" / "not checked" |
| `[envelope]` verdict | `main.c`, via `jc_env_outcome_name()` |
| `--output json` | `main.c`, `stop = "…"` |

**Forty cells, each wired by hand.** M687 filled one that had been empty for
fifteen milestones — *after* M322 had written down exactly what was wrong with
it. So the question is not whether people are careful; it is whether the shape
lets a cell be left empty without anybody noticing.

**It does, and here is another one, measured 2026-09-21.** A run stopped by
`--max-tool-calls 4`, with a verifier and an edit scope armed, printed this:

```
[jichi] checked: verify did not conclude · 4 tool calls, 0 errors (0 refused by a fence) · ...
not checked: (nothing -- a verifier and an edit scope were armed)
[envelope] budget_exhausted (tokens 100, tool calls 4)
```

stdout was **empty**. Two false claims in one block:

1. **"not checked: (nothing)"** about a run that was cut off and answered
   nothing. The budget surface knows; the footer does not.
2. **"verify did not conclude" filed under *checked*.** A verifier that never
   reached a verdict is the definition of something that was *not* checked. It
   is on the wrong side of the colon.

The full audit (`done`, `max_iters`, `budget`, `verify_failed` reached with mock
fixtures; `scope_tainted` and `timeout` were not reachable with them, and that
is said here rather than guessed):

| condition | rc | stdout | footer: checked | footer: NOT checked | `[envelope]` |
|---|---|---|---|---|---|
| `done`, no verifier | 0 | answer | counts | "no verifier armed" ✓ | `verified ok` |
| `max_iters` | 0 | **empty** | verify green | names the cap ✓ *(M687)* | names the cap ✓ *(M687)* |
| `budget` | 1 | **empty** | "verify did not conclude" ✗ | **"(nothing)"** ✗ | `budget_exhausted` ✓ |
| `verify_failed` | 1 | answer | "verify RED (rolled back)" ✓ | "(nothing)" — acceptable | `verify_failed` ✓ |

---

## 1. M688 — one outcome, rendered five ways

**The change.** A single `struct jc_run_outcome` that carries *why the run
stopped* and *what that means for the answer*, filled once, and **read** by
every surface. Not a new reporting path: the surfaces keep their own wording and
their own audience. What changes is that they stop each deriving the fact
independently from a different pile of booleans.

**What it must make impossible**, in order of how much the session cost:

- a surface that does not handle a stop reason **compiling**;
- a stop reason that means "the answer is incomplete" being absent from the
  not-checked half;
- "did not conclude" being reported as a thing that was checked.

**Shape.** `jc_run_outcome` holds the stop reason as an enum, plus two derived
predicates the surfaces actually branch on — `answer_is_complete` and
`result_was_tested`. The enum gets a `switch` with no `default:` in each
renderer, so `-Wswitch` (already on, via `-Wall`) turns a new stop reason into a
build error at every site that must handle it. That is the whole mechanism: the
compiler holds the matrix, not a reviewer.

**Deliberately NOT in scope:** changing any exit code, any stop-reason string,
or the M73 stdout rule. This is a refactor that must be invisible to every
existing caller — its own test is that the surfaces say what they said before,
except in the cells the audit above proved empty.

**Test.** A driver that runs each reachable stop condition and asserts all five
surfaces agree, so the matrix is checked rather than described. It is the audit
above, made executable — which is the difference between a finding and a gate.

---

## 2. M689 — the two seams that driving found

From `DEFERRED.md` item 6, both measured while driving jichi on zigodot:

- **`doctor` validates the endpoint, not the model id.** It answers
  `✓ model server reachable` for a model the server does not list — and it
  already fetches `/v1/models` in the very next check, to read limits. The
  zigodot config named two retired ids and every non-live check passed. This is
  the first command a user runs, and it blessed a config that could not work.
  *Fail open* when the listing is unavailable: a gateway that will not answer
  must not turn into a red `doctor`.
- **The envelope cannot tell a reading shell command from a writing one.**
  `not checked: a shell command ran -- changes it made are not attributed` fires
  identically for fourteen `grep`/`find`/`ls` calls and for one `zig fmt`. jichi
  keeps a snapshot per snapshotted turn, so *"a shell command ran and the tree
  is byte-identical afterwards"* is a measurement it can make. A warning that
  fires on every `ls` is one readers learn to skip, and it is attached to the
  sentence that should never be skipped.

---

## 3. M690 — the drive log (done together with 4: the measurement proved they were one item)

**The gap.** `PLATFORM_TESTING.md` teaches *"does it work on this machine"*.
Nothing teaches *"does it work when somebody uses it"* — and that is where this
session's real defects came from: **8 headless runs on a foreign repository
yielded 4 seams**, none of them reachable by any gate in the tier, because
every gate in the tier is offline.

**Not a new tier.** A recipe and a log: pick a real task on a real second
project, drive it headless, and write down **what the caller was told** — the
answer, the exit code, and every line of the footer. The artifact is the row,
as with platform testing. The discipline it teaches is reading the run's own
report as evidence rather than as decoration.

---

## 4. M690 — ask the journals what they can already answer

Three `DEFERRED.md` rows are blocked on a corpus, not on code. One is nearly
free: **item 7** asks whether a capped one-shot usually answers anything, and
`stop_reason` has been in the run journal since M322. Count the completed
`--no-session` runs that ended `max_iters` and how many produced a non-empty
answer. If the answer is "almost always something", the exit code of 0 is
defensible and the row closes; if it is "usually nothing", the flip has a number
behind it. **Either way the row stops being an argument.**

The other two (`--strict-green`'s tracked-vs-untracked rule, the re-read ratio)
need the corpus M684 and M690 produce. They are named here so the next reader
knows what M690 is *for*.

---

## 5. M691 — cut the public snapshot

The public tree is at the **M668** state; this one is at M687 — nineteen
milestones, and they are the ones that added the documentation the release is
explicitly sold on: the honest anecdotes, the platform-testing tutorial, the
biography. `scripts/make-snapshot.sh` is the producer, `snapshot_lint` the gate,
and the gate got materially stronger this session — it had been reading only
part of the tree.

Done last on purpose: a snapshot is only worth cutting once the things it
publishes are finished.
