---
description: Record an already-implemented change in ROADMAP + CHANGELOG, house style (delegates to the doc-updater agent; writes only docs/ and CHANGELOG.md).
agent: doc-updater
---
Record the change described below (or, with no argument, what `git_diff` shows)
in the project docs, matching the existing form exactly: a `### M### — … — done`
entry in `docs/ROADMAP.md` (+ the "Where we stand" banner bump) and one
`## [Unreleased]` bullet in `CHANGELOG.md`. Describe the actual change; never
claim a test/CI result you did not see.

$ARGUMENTS
