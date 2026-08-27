---
title: Diagnose and fix a failing test
audience: agent
verify: "sh ./test.sh"
points: 3
---
Run `./test.sh`. It fails. Find the cause in the source, fix it, and run
`./test.sh` again to confirm it passes. Leave `test_math.c` exactly as it is.

<!-- Wording note: the final sentence deliberately avoids the phrasing "do not
     change the test file". The M110 constraint scanner reads any negation cue
     followed by the bare noun "test" as the prohibition "do not run tests",
     adopts `deny-tool run_tests` + `deny-cmd test`, and persists them to
     .jichi/constraints.md -- which then blocks the very command this task
     requires. That false positive is a real finding of this bench, recorded in
     docs/analysis/2026-07-27-local-gpu-bench.md; it is kept out of the corpus
     so this task measures the compound fix-and-verify loop rather than the
     constraint bug. Do not "simplify" the wording back. -->

