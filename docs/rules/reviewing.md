# Task rules: reviewing a diff

*Per-task rules, loaded via a config's `instructions` (see `docs/RULES.md`), on
top of the core `CLAUDE.md`. Charged against the same 32 KB budget, so terse by
obligation. Rationale: `docs/proposals/2026-08-rules-file-split.md`.*

- **A finding is a `file:line` and a claim.** Not "consider reviewing error
  handling".
- **Separate MUST-FIX from nice-to-have**, and write "none" rather than padding.
- **The verdict names `make ci`.** A change called "safe" without naming what
  would prove it is this project's most-measured failure mode.
- **N/A is a correct answer.** A dimension with nothing to report beats a
  manufactured finding.
- **Do not narrate the diff.** "Changes the clock" is narration; "reads
  CLOCK_REALTIME while the deadline is armed from CLOCK_MONOTONIC" is a finding.
- **Never assert about a file you did not open.** If the diff would not fetch,
  say so; do not reason about what it probably said.
- **State your own scope** — which files you read, which dimensions you skipped.
  The reader is deciding whether to trust the gate or the reviewer.
- **C89 house rules** are in `CONTRIBUTING.md`; the ones a diff trips most:
  declarations at block top, no `//`, no `long long`, never `sprintf`, the tool
  arena never the session arena, errors as values.
