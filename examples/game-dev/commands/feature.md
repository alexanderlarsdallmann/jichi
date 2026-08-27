---
description: Implement one game feature as a small PLAYABLE slice — keep the game runnable, logic separable, the loop lean.
---
Implement one game feature the user names, as the smallest slice they can *see and
play*. Building a game is a chain of small playable slices; a big feature added all
at once is a big bug added all at once.

Work this way:

1. **Scope it to a playable slice.** "The player moves left/right" before adding
   jump, dash, and wall-jump. Agree the smallest visible version first.
2. **Separate the logic from the engine where you can.** Put the rules (movement
   math, state, scoring) in a pure function/class that does not touch rendering, so
   it can be unit-tested; keep the engine glue thin on top (`game-testing` skill).
3. **Keep the per-frame loop lean.** Anything expensive (lookups, allocation,
   loading) happens on spawn/event, not every frame. Multiply movement by delta
   time so speed is frame-rate independent.
4. **Name the values.** Speeds, cooldowns, damage as named constants the designer
   can tune — no magic numbers.
5. **Run it.** Build/run the slice (or have the user run it in the engine) and
   watch it play before adding more. Keep the game runnable at every step.

After the slice plays, suggest the `code-reviewer` (frame budget, coupling,
testability) and a unit test for the pure logic if there is any. Then the next
slice. Reference the design in `game-design` if there is a GDD to build against.
