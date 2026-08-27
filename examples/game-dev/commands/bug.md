---
description: Fix a game bug the disciplined way — reproduce it first, then fix the cause, then verify (a test where you can).
---
Fix a bug in the game, reproducing before fixing. Games are stateful and
real-time, so "I changed something and it seems better" is not a fix — you must be
able to *make the bug happen* and then *make it stop*.

1. **Reproduce it.** Pin down the exact steps and state that trigger it. A bug you
   cannot reproduce, you cannot know you fixed. If it is intermittent, find the
   condition (a specific timing, entity count, input sequence).
2. **Find the cause, not the symptom.** Read the code path involved; form a
   hypothesis about *why*; confirm it (a print, a breakpoint, isolating the logic).
   Do not paper over it with a special case that hides it elsewhere.
3. **Fix the cause.** The smallest change that removes the root cause.
4. **Verify.** Re-run the reproduction and confirm it is gone. If the bug is in
   pure logic (not rendering), write a **unit test** that reproduces it — so it can
   never silently come back (`game-testing` skill).
5. **Note it.** A one-line entry in `notes/bugs.md` (symptom → cause → fix) — the
   debugging record that makes you faster next time.

Games hide bugs in timing and state; the discipline of reproduce-first is what
turns "it feels flaky" into "it is fixed." Suggest the `code-reviewer` if the bug
points at a systemic issue (a per-frame trap, frame-rate dependence).
