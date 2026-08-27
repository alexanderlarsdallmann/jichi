# Game-development project conventions

*This file tells the agent how to behave in a game-**development** workspace. jichi
loads it automatically. Keep it short; edit it to match your engine and project.*

## What this bench is

A bench for **building** a game — the code half. The `game-design` bench is where
you designed the fun (the GDD, the mechanics); this is where you make it run. If
you have a design, build against it; if not, keep the scope tiny (see
`game-design`'s `/scope`). jichi's compiled-in **`godot`** pack is the
engine-specific companion for Godot users.

## What runs where

The code runs **inside a game engine** you have installed — this bench is
engine-agnostic, but the default assumption is **Godot (GDScript)**. jichi writes
and edits the game code and can run it through the shell; the game itself executes
in the engine. Confirm your engine and how you run it (`/build`), and read the
command jichi is about to run before approving it. If a run fails on a missing
engine or library, that is your setup, not jichi.

## The rules of this bench

1. **Keep it runnable, always.** A game that does not run cannot be playtested and
   teaches nothing. After every change the game still starts and plays — half-done
   features hide behind a flag, they do not break the build.
2. **Small, playable slices.** Add the smallest thing you can see and play, then the
   next. Test a slice by playing it.
3. **Separate logic from rendering.** Rules (movement, scoring, state, damage) as
   pure functions you can unit-test; the engine glue thin on top. This is the
   biggest lever for a testable, debuggable game (`game-testing`).
4. **Keep the per-frame loop lean.** No allocation, lookups, or loading in
   `_process` / the main update — do expensive work once, cache it. Multiply
   movement by delta time (frame-rate independence).
5. **Handle input as named actions**, read in one place, not raw keys scattered
   through gameplay (`input-handling`).
6. **Compose behaviours; avoid deep inheritance.** Small reusable components, not a
   collapsing class tree (`entity-component`).
7. **No magic numbers.** Speeds, cooldowns, damage as named, tunable constants.
8. **Reproduce a bug before fixing it**, and unit-test the fix where the logic is
   pure (`/bug`).

## The workflow (commands)

- `/feature` — implement one feature as a small playable slice.
- `/bug` — reproduce → fix the cause → verify (a test where you can).
- `/build` — build/run/export the game; keep the run path working.
- `/release-notes` — player-facing notes for a version.

Use the read-only **`code-reviewer`** agent before you trust a change — it flags
per-frame performance traps, logic tangled with rendering, untestable gameplay,
frame-rate dependence, and magic numbers.
