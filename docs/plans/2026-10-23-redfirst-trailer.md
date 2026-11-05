# Machine-readable red-first claims (batch 14, item (f)) — design note

*The finding: a commit trailer naming the red-first test and the files its
tooth reverts — batch 14 item (f) of
`docs/internal/drive/2026-10-17-hardening/PLAN.md` (design note first; the
form settles here). Milestone: **unclaimed** — this is a note, and the 08:36
order builds (f) after the switch, when its slot comes. Written 2026-10-23
(run 0075).*

**Status:** form settled for discussion; **not built** (the operator's rule
of 2026-10-23: design when found, build when needed).

## Why this

The drive's red-first ritual — a new test shown failing before its fix,
`tests/teeth.sh`, `docs/TESTING_RUNBOOK.md` — is currently a **claim in
prose**: the log line and the report quote the failing line, and a reviewer
(qwen27b) reads them. Nothing machine-checkable ties a fix commit to the
test that was red without it, or to the hunk whose reversion makes that test
red. M1047's own F1 (the report's quoted red block described an earlier
driver iteration of check 5) is the class of drift this closes: the red
claim pointed at bytes that were no longer in the tree.

The teeth exist, but only as a **one-off ritual** (`teeth.sh` reverts, runs,
restores). The gap is the durable, per-commit record: which test is the
tooth, and which files does reverting the fix make it red.

## The shape (proposed)

One trailer block on the fix commit, parseable by the existing
`trailer_block_lint` machinery (which already parses `Co-Authored-By` /
`Jichi-Build` / `Jichi-Model`):

```
Red-First-Test: tests/smoke/parallel_liveness.sh
Red-First-Checks: 1 2 3
Red-First-Reverts: src/tools/jc_tool_parallel.c src/tools/jc_tool_subagent.c
Red-First-Red: b91c54ac          # the commit the test was proven red on (the test-only commit)
```

- **Red-First-Test** — the driver (or unit file) that was red. A lint
  (`red_first_lint`, new, beside `teeth.sh`) checks it exists and is named in
  `run.sh`.
- **Red-First-Checks** — the specific `t_ok`/`not ok` numbers the fix must
  keep red when reverted. Perturb-per-CHECK, not per driver: the lint reverts
  only `Red-First-Reverts` and asserts **exactly** those checks go red, so a
  tooth that reverts a file but no longer makes its claimed check red is
  caught (the M508/M510/M511 "audit the universe" class).
- **Red-First-Reverts** — the minimal file set whose reversion produces the
  red. For a multi-hunk fix, the set is the files, and the lint notes that
  whole-file reversion is a superset of the true minimal hunk set (the
  per-hunk teeth stay with `teeth.sh` and the report).
- **Red-First-Red** — the commit the red was observed on (the test-only
  commit that precedes the fix), so the red line is reproducible from history
  without the reviewer trusting the log's prose.

The lint runs in the **fast gate** (it reverts and rebuilds, ~one driver
per fix commit in the window), not in `make ci` alone, so a hollow red claim
fails before the batch is declared READY.

## The cost

A `teeth.sh`-style run per fix commit in the window. The lint bounds this to
the commits in the gate's window that carry a `Red-First-Test` trailer (most
commits — docs, registers, reviews — carry none and are skipped), so the
steady-state cost is one revert+rebuild+driver per code commit, not per
commit.

## Open questions for the operator

1. **Whole-file revert vs per-hunk revert.** The trailer names files (cheap,
   stable under rebase); `teeth.sh` reverts hunks (exact). Is the file-level
   claim adequate for the durable record, with the per-hunk teeth staying a
   one-off ritual, or should the trailer name a patch id (exact but brittle)?
2. **Which commits must carry it.** Every fix commit, or only the ones whose
   finding is in the hardening register (this drive's universe)? The drive's
   ritual already applies to the register; extending it to every `src/` fix
   commit widens the universe and the cost.
3. **The lint's window.** The fast gate sees the current batch's commits; a
   `make ci`-only lint sees the whole history since a baseline (the
   `trailer_block_lint` pattern). Red-first is a property of the commit's
   moment, so the fast gate is the honest place — but then a later commit
   that breaks the red (a refactor that makes the tooth pass without the fix)
   is caught only if the lint re-runs on the reverted state in `make ci` too.
4. **Interaction with `teeth.sh`.** Does the trailer *replace* the ritual, or
   is the ritual the one-off proof and the trailer the durable claim the lint
   re-verifies? (Recommended: the latter — the ritual proves the red once,
   the trailer records it, the lint re-verifies it cheaply.)
