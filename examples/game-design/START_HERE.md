# Start here — design your first game (the right size)

*A numbered, first-session walkthrough. If you have never designed a game, do
exactly these steps once; you will end with a small, clear, buildable design —
and, more importantly, the habit of designing before building.*

## Before you begin

You need almost nothing: **jichi** (you have it) and **a model for jichi to think
with** (copy `config.example.json`'s `models` block into your config and fill in
your model — a local one is free and private; see `docs/LOCAL_MODELS.md`; then
`jichi doctor` to confirm). No engine, no compiler — this is design, not code.

A tip that will save you months: **think small.** Your first design should be a
game you could build in a week or two, not the game of your dreams. The dream
game is a "later" list.

## Your first design, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`). You are in the TUI —
   jichi's interactive prompt.

2. **Find the core fun before anything else.** Tell jichi a rough idea, then
   start the design:

   ```
   /gdd
   ```

   jichi (the `game-designer` guide) will *refuse to list features* until you can
   say, in one sentence, what the player *does* moment-to-moment that is fun.
   This is the most important sentence in game design. Work on it with jichi
   until it is sharp. It writes `GDD.md`.

3. **Spec your core mechanic.** Type:

   ```
   /mechanic
   ```

   Name the one action at the heart of the game, and jichi helps you make it
   concrete — inputs, the game's response, the numbers, and *why it is fun*. If
   there is no "why it is fun", that is a discovery, not a failure: change it now,
   on paper, for free.

4. **Cut it down to something you can finish.** Type:

   ```
   /scope
   ```

   jichi will help you find the smallest version that still has the core fun —
   usually much smaller than you first imagined. **Cutting is the skill.** What
   you cut goes on a "later" list, not in the bin.

5. **Get a real opinion.** Before you fall in love with your design, ask the
   reviewer:

   ```
   Spawn the mechanics-reviewer agent to review my design.
   ```

   It reads (never edits) and tells you the hard truths — no core fun, scope too
   big, a mechanic with no reason to exist, a balance break visible on paper.
   A clean review here is worth a month of building the wrong thing.

6. **Test it — even on paper.** You do not need code to playtest. Act out the core
   loop with dice, cards, or a friend. Then record what *actually* happened:

   ```
   /playtest-notes
   ```

   Write what you *observed*, not what you hoped. If the fun you designed did not
   show up, that is the single most valuable thing you can learn — and you learned
   it before writing a line of code.

That is the loop: **core fun → mechanic → scope → review → playtest.** When the
design is small, clear, and playtests as fun, *then* you open the `godot` pack and
build it. Design first is why your game gets finished.

## If you get stuck

- You cannot state the core fun in one sentence → that is normal, and it is the
  whole exercise. Keep going with jichi; a fuzzy core is a design not yet ready.
- Your design keeps growing → run `/scope` again. There is no shame in a tiny
  first game; there is a lot of pain in an unfinished big one.
- Read the skills when a specific problem bites: `skills/game-loop` (is my loop
  fun?), `skills/balancing` (is a choice meaningless?), `skills/mda-framework`
  (why doesn't it *feel* the way I wanted?).
