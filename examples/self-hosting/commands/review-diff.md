---
description: Review the working-tree diff with jichi's C89 reviewer and arena auditor (read-only).
---
Review the current change to jichi's source. Make **two** independent passes and
report both, clearly separated:

1. Spawn the `c89-reviewer` agent on the working diff — C89 / pedantic +
   house-rule findings.
2. Spawn the `arena-auditor` agent on the same diff — memory-lifetime findings.

Then consolidate into **MUST-FIX** vs *nice-to-have*, each item with `file:line`.
End with a one-line verdict: is this ready to run `make ci` on, or are there
must-fixes first?

Remember: you are a *second reviewer*, not the gate. The gate is `make ci`
(gcc + clang `-Werror`, ASan/UBSan, valgrind, smoke, e2e). Never imply a change
is safe because the review is clean — say the review is clean *and* CI must
still pass.

$ARGUMENTS
