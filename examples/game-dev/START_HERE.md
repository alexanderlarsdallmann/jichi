# Start here — build a game that stays runnable

*A numbered, first-session walkthrough. Do these steps once on a tiny game idea.
You will end with a small game that actually runs and plays — and the habits (keep
it runnable, small slices, testable logic) that let a game grow without collapsing.*

## Read this first

Build **small**. Your first game should be something you can finish in days, not
the dream game (that is the `game-design` bench's `/scope` lesson, and it applies
double once you are coding). A tiny game that runs and is a little fun beats a huge
one that never works. Keep the game runnable at every step — a game that does not
run cannot be played, and playing is how you know it works.

## Before you begin

You need:

1. **A game engine** installed. Default here is **Godot** (godot.org) with
   GDScript; LÖVE (Lua) or Pygame (Python) work too. Know how you run your engine.
2. **jichi** (you have it) and **a coding-capable model** (copy
   `config.example.json`'s `models` block into your config; a local one is free —
   `docs/LOCAL_MODELS.md`; then `jichi doctor`).
3. Ideally a **design** to build against — even one sentence of core fun and one
   mechanic from the `game-design` bench.

## Your first game, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`) and make sure the
   engine runs at all:

   ```
   /build
   ```

   jichi confirms your engine and helps you get an empty/starter project *running*
   — a window opens, even if it is blank. Proving the run path works first is worth
   more than any code; you will run the game constantly from here.

2. **Add the smallest playable thing.** Type:

   ```
   /feature
   ```

   Name the tiniest slice — usually "the player moves." jichi implements it,
   keeping the movement *logic* separable and the per-frame loop lean, then you run
   it (`/build`) and watch the player move. That is a playable slice. Then the next
   one (jump, an obstacle, a goal) — one at a time, running each.

3. **When something breaks — and it will — reproduce first.** Type:

   ```
   /bug
   ```

   jichi helps you make the bug happen reliably, find the *cause* (not paper over
   the symptom), fix it, and — if the logic is pure — write a unit test so it never
   comes back. "Reproduce before fix" is the whole discipline of debugging a
   stateful, real-time thing.

4. **Keep the code honest.** Before you pile on features, ask:

   ```
   Spawn the code-reviewer agent to review my game code.
   ```

   It reads (never edits) and flags the game-killers — expensive work every frame,
   logic tangled with rendering, movement that runs at a different speed on a
   different machine, magic numbers a designer cannot tune.

5. **Cut a version and tell players.** When a slice is worth sharing:

   ```
   /release-notes
   ```

   Short, player-facing notes — what is new, what is fixed — in the language of
   someone who plays the game, not the code.

That is the loop: **build → feature → bug → review → release.** Run the game after
every change. Read the skills when a specific question bites: `entity-component`
(how do I organize my objects?), `game-testing` (what can I even test?),
`input-handling` (why does my jump fire twice?).

## If you get stuck

- The game won't run → fix that before anything else (`/build`); a broken build
  blocks all playtesting.
- It stutters as you add things → something expensive is running every frame; run
  the `code-reviewer` (`skills/game-testing` on the frame budget).
- It plays at a different speed on another machine → you are not multiplying
  movement by delta time (frame-rate independence).
- Your object classes are becoming an unmanageable tree → switch to composition
  (`skills/entity-component`).
