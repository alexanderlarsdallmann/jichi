---
name: game-testing
description: What you can test automatically in a game (pure logic) versus what only playtesting shows (feel) — and how to structure code so more is testable.
---
# Testing a game

"How do you test a game? Isn't it all just playing it?" — a fair question, and the
answer is the key to a debuggable game: **split what a computer can check from what
only a human can feel.**

**Two kinds of "does it work":**

- **Logic** — the rules: does damage subtract correctly, does the win condition
  trigger at the right score, does the state machine go idle→run→jump→fall and back,
  does the save/load round-trip? These are *deterministic* and **unit-testable** —
  a computer can check them, fast, every time.
- **Feel** — is the jump satisfying, is the difficulty fair, is the level fun? These
  are *subjective* and only **playtesting** (a person playing) can answer them. No
  test asserts "fun."

**The lever: separate logic from rendering.** The reason most game code seems
untestable is that the rules are tangled with the engine (a damage calculation
buried inside a collision callback that also plays a sound and shakes the camera).
Pull the rule into a **pure function** — `apply_damage(hp, amount) -> new_hp`,
`next_state(state, input) -> state` — that takes values and returns values, no
engine calls. Now you can unit-test it in isolation, and the engine code just wires
it up. The more of your game's *rules* live in pure functions, the more of your
game you can test automatically — and the fewer bugs hide in the parts you can only
reach by playing.

**In practice:**

- Write unit tests for the pure logic (Godot has GUT/GdUnit; Lua has busted; Python
  has pytest). Run them headless (`godot --headless`, or the language's runner) as
  a `testCommand` so a bad change is caught before you even open the game.
- For everything else, **playtest deliberately** and record what you observe (the
  `game-design` bench's playtest notes) — that is the "test" for feel.

The goal is not 100% automated tests; it is that your *rules* are checked by a
machine so your *playtesting* time is spent on feel, not on catching arithmetic
bugs by hand.
