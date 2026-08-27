# The teaching seams: assignments, hints, grading

*Analysis, 2026-08-27, at the operator's request ("find the seams in the
assignments, hints, grading features"). Third seam survey of this series, after
[`2026-08-27-the-language-of-lessons.md`](2026-08-27-the-language-of-lessons.md)
(the learning loop, M596–M605) and
[`2026-08-27-what-the-structure-claims.md`](2026-08-27-what-the-structure-claims.md)
(state/hardening, M606–M612). Same method: one deep read-only survey plus
first-hand verification of every load-bearing claim; the question is unchanged —
**what does this feature claim, and what does the world do?***

## 0. The subsystem in one paragraph

One pure spec core (`jc_assign.h/.c`: parse, render by audience, score, verdict),
one shared grading mechanic (`assign_grade_core`, built at M529 because "a grade
that differs between the CLI and the wire is not a grade"), two learner-owned
record sinks (`.jichi/progress.jsonl` — attempts with verdicts; `.jichi/hints.jsonl`
— hint pulls, deliberately separate so a hint can never read as a failed attempt),
a graded hint ladder (spec frontmatter → CLI `hint`, the `hint` tool, TUI `/hint`),
and a worktree sandbox for `attempt` (M410 TAINTED verdict) and `improve
--attempt` (the self-improvement pass-rate). The lineage is deep and mostly
honest: M289/M409 (ladder truncation said aloud), M410 (moved goalposts),
M412 (`--expect-fail`), M502 (a broken harness is not a grade), M529 (one
mechanic), M533/M536 (records outlive the sandbox).

## 1. The seams, ranked

### A. Records that quietly lie

- **A1 — every shipped brief is truncated mid-sentence.** The hint-availability
  note is staged through `char note[192]` (`jc_assign.c:220`) but the
  student/junior message is 206 bytes; `jc_snprintf` truncates silently.
  Observed, not inferred: `jichi assign docs/assignments/00-hello.md` ends
  `"…or delega"`. All **79** shipped specs are `audience: student`, and this is
  the exact text `attempt` hands the solver (`main.c:9839`). The render tests
  only matched substrings, so all passed. → **M613**.
- **A2 — the TUI `/hint` records nothing.** `jc_tui.c:3843` reveals the rung and
  increments the counter; the only `jc_progress_hint_append` callers are the CLI
  (`main.c:9005`) and the tool (`jc_tool_hint.c:79`). The TUI `/assignment` load
  arms `assignment_spec`/`assignment_dir` with a comment saying it exists so "a
  rung the tutor reveals is recorded exactly like one the CLI reveals" — the
  claim is in the comment, the call is absent. And the TUI is the ladder's real
  path: M319/M320 measured 24 model runs with **zero** `hint` tool calls; humans
  use `/hint`. `CURRICULUM.md:292` / `ASSIGNMENTS.md:307` promise "recorded,
  never penalised". → **M614**.
- **A3 — the TUI `/grade` is a fourth grading implementation, and the only one
  that records without asking.** `assignment_grade` (`jc_tui.c:2394`)
  re-implements setup+verify+score without the M502 `AGF_CANNOT_RUN` guard,
  then appends to progress.jsonl unconditionally (`:2450`) — while `main.c:9013`
  says "ONE grading mechanic, three callers". A `/grade` whose verify is
  unreachable writes `FAIL 0%` into the permanent record. → **M614**.
- **A4 — CLI records write to `"."`, readers read `app->cwd`** (`main.c:9005,
  9216` vs `:7922`): graded from a subdirectory, the row lands where no reader
  looks. Possibly deliberate; needs one decision and one documented sentence.
  → C3 sweep.

### B. Verdicts that conflate "harness broken" with "learner failed"

- **B1 — M502's reachability probe is blind for the beginner tier.** It fires
  only when the verify program contains `/` (`main.c:9076`). Measured: 68
  shipped verifies are `sh <path>` (guarded); **12 are `grep`/`[`** — assignments
  00–05 and plain-language p1–p3, exactly the tier least able to tell a broken
  grader from its own mistake. From the wrong directory those grade
  `FAIL / score: 0%` as a real grade. Inspecting arguments is refused on purpose
  (`jc_assign.h:66`), so the mend needs a different signal. → **M617** (design).
- **B2 — `attempt` has no cannot-run class.** Any nonzero verify exit is FAIL;
  a failed `chdir(wt)` leaves `after=-1` → "FAIL"; `--record` writes
  `passed:false`. And `attempt` never runs `spec.setup` (grade does), so a
  setup-dependent spec grades differently per path. → **M615**.
- **B3 — TAINTED sees two of the write paths.** `tu_report_test_edit` is called
  only by `edit_file`/`apply_patch`; `write_file` (and shell `sed -i`) never bump
  `test_edits` — the sole input to the M410 verdict — though the write chokepoint
  already flags test-looking paths (`jc_app.c:442`). Overwriting the gate
  wholesale yields a clean PASS. Worse: **`improve --attempt` arms no envelope at
  all** (verified: no `jc_env_init` in `main.c:9460–9605`), so a moved-goalpost
  green counts as "fixed" in the pass-rate the loop exists to track. → **M615**.
- **B4 — `pct` can read 100 while `passed` is false** (`jc_assign_score`,
  `jc_assign.c:142`): informative but confusing in `best_pct`. → C3 (likely a
  documented sentence, not a code change).

### C. Residue, bounds, fences

- **C1 — attempt/improve/workflow worktrees are never swept.**
  `att-<pid>`/`imp-<pid>`/`wf-<pid>` under `~/.jichi.d/worktrees/` are removed
  only on the clean path; `--keep-worktree`, SIGKILL, or a deadline kill leaves
  them forever. `checkpoints gc` never touches `worktrees/`;
  `jc_snapshot.c:890` already clears shadow-repo admin data for directories that
  are GONE, so a sweeper's job is only the directories. Same growth family as
  dreams (M611) and the index cache (M612). → **M616**.
- **C2 — the tutor stance is prose, not a fence.** `assignment_tutor` swaps the
  system prompt ("NEVER write the solution", `jc_sysmsg.c:893`);
  `ASSIGNMENTS.md:297` promises "it will decline even if asked". The write
  toolset stays live and the `hint` tool has no tutor check
  (`jc_tool_hint.c:34`), so the model can burn the human's ladder, writing
  hints.jsonl rows attributed to the learner. → **M617** (hint gate mechanical;
  the write-fence question is a recorded decision).
- **C3 — sweep-up:** `hints_skipped` (M409) surfaces on 2 of 5 hint surfaces;
  `ASSIGNMENTS.md:216` overstates tool gating ("and an assignment is active" —
  registration is `assignments: true` alone, `jc_tool.c:519`); record readers
  skip ≥4096-byte lines and parse failures silently (documented as learner-owned
  tolerance — verify the doc says so); the `agent` audience render branch is dead
  across all 79 shipped specs; **no lint pins a new spec into
  `curriculum_graders.py`** (hand-enumerated universe). → **M618**.

## 2. Already recorded, not re-proposed

M319/M320 (models don't pull hints — measured), the M326 rows, the shell-`cd`
worktree residual (ANECDOTES #12, restated at `main.c:9801`), the parked
gradebook/cohort rows, and the closed M409/M410/M412 lineage.

## 3. Status

| Seam | Milestone | Born-red evidence | State |
|---|---|---|---|
| A1 truncated brief | M613 | render test pins the final sentence (red: 1 failure) | **done** |
| A2 `/hint` unrecorded + A3 `/grade` fourth impl | M614 | learner_flow 9→12; teeth red both ways | **done** |
| B2 attempt cannot-run/setup + B3 TAINTED gaps, improve unmetered | M615 | 3 drivers born red (9 checks); 4 teeth | **done** |
| C1 worktree sweeper | M616 | driver born red; liveness + name-filter teeth | **done** |
| B1 beginner-tier reachability + C2 tutor fences | M617 | `grade_wrongdir.sh` + tutor unit case, born red | **done** |
| C3 sweep-up | M618 | universe lint (teeth); render note born red; A4/B4 as documentation | **done** |

## 4. Limits

One reader (me), no second pair of eyes; the gate is `make ci`. The verify and
audience populations were counted over this checkout's `docs/assignments/` and
`docs/i18n/*/assignments/`. A2/A3 rest on reading the TUI source and the callers
of the two record sinks; the smoke drivers that exist (`hint_record.sh` 9 checks,
`grade_expect_fail.sh` 8) cover the CLI and tool paths and assert nothing about
the TUI ones — that hole is itself part of seam A2/A3.
