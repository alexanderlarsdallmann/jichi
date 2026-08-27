---
name: balancing
description: How to spot and fix balance problems — dominant strategies, difficulty spikes, meaningless choices — mostly on paper, before building.
---
# Balancing

A balanced game is one where the player's choices *matter* and the challenge is
fair. Much of balance can be reasoned about on paper, long before a build — and
should be, because it is cheap there and expensive later.

**The problems to hunt for:**

- **A dominant strategy.** One option is always best, so every other choice is
  pointless. (Two weapons, same damage, one with more range → nobody picks the
  other.) Fix: make each option best in *some* situation — a trade-off, not a
  ranking.
- **A meaningless choice.** The player picks between options that feel different
  but play identically. Either make them differ or cut one.
- **A difficulty spike or wall.** Challenge that jumps instead of ramping. Map
  the curve: does each step ask a little more than the last?
- **Runaway rewards.** A resource or score that snowballs so the leader always
  wins (or the player can never lose). Add a cost that scales, or a catch-up.
- **A dead resource.** Something the player always has too much of (so it is not
  a real constraint) or never enough of (so it is just frustrating).

**How to check on paper:**

1. For each choice, ask: *when would a smart player pick each option?* If one
   answer is "always" or "never", it is unbalanced.
2. For difficulty, list the challenges in order and rate each 1–5. Look for jumps.
3. For economies, do the arithmetic: can income outrun costs forever? Then it is
   not a constraint.

Then **playtest** — paper math finds the obvious breaks; real players find the
ones you cannot see. Change one number at a time so you know what fixed what.
