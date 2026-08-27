# Game-design project conventions

*This file tells the agent how to behave in a game-**design** workspace. jichi
loads it automatically. Keep it short; edit it to match your project.*

## What this bench is (and is not)

This is a bench for **designing** a game — deciding what makes it fun and writing
it down — **not** for coding one. The output here is **documents**: a Game Design
Document (`GDD.md`), one spec per mechanic under `mechanics/`, and playtest notes
under `playtests/`. You build the game *later*, from these documents (the `godot`
pack is the code half). Design before code is the whole point: it is far cheaper
to change a sentence than a codebase.

There is no toolchain to install for design. If you want to *prototype* a mechanic
to feel it out, the fastest tools are **paper, dice, and grey boxes** — a design
you can test on paper today beats a perfect spec you build in a month.

## The rules of this bench

1. **Core fun first.** Before any feature list, state in one sentence what the
   player *does*, moment-to-moment, that is enjoyable. No core, no game.
2. **Design the core loop before content.** The 3–10 second cycle the player
   repeats is where fun lives. If it is boring with grey boxes, art will not
   save it.
3. **One mechanic at a time, and every mechanic earns its place.** Each spec says
   what the player does, what the game does back, and *why it is fun or hard*. No
   answer to "why fun?" → it is decoration; cut it.
4. **Fight scope, always.** A first game should be small enough to finish. Cutting
   is the skill. A small finished game teaches more than a huge unfinished one.
5. **Design to be playtested, not admired.** A design is a hypothesis about fun;
   the only test is a person playing it. Get to a testable version fast.
6. **Concrete, not poetic.** Numbers where numbers matter (jump height, enemy
   speed, costs). A builder must be able to build it without guessing.

## The workflow (commands)

- `/gdd` — draft or grow the Game Design Document.
- `/mechanic` — spec a single mechanic, concretely.
- `/scope` — cut the design down to something you can actually finish.
- `/playtest-notes` — record what a playtest *actually* showed.

Before you trust a design, have the read-only **`mechanics-reviewer`** agent read
it — it hunts the beginner-killers: no core fun, scope too big to build, a
mechanic with no "why", a balance problem visible on paper.
