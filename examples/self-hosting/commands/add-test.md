---
description: Author a shown-red-first test for existing jichi code (delegates to the test-author agent; writes only under tests/).
agent: test-author
---
Author a test for the following. Write it under `tests/` only, wire it in
(`tests/jc_test.h` + `tests/test_main.c` for a unit test), and build it — it must
compile clean under `-std=c89 -pedantic -Wall -Wextra -Werror` (declarations at
the top of every block; no `//`; no `long long`) and pass.

Then **the shown-red step, which you must NOT perform by editing `src/`**: this
run's `editScope` is a positive allow-list of `tests/**`, `docs/**` and
`CHANGELOG.md`, so a temporary break in `src/` is out of scope and jichi reverts
it — measured 2026-08-03, when exactly that happened. Instead report, precisely:
the file and line to change, the exact before/after text of the one-line break,
and which of your new checks it should red. The human applies it, observes the
failure count, and restores. Say plainly that you have NOT observed the red.

If the test would need a `src/` change to compile at all, stop and say what.

$ARGUMENTS
