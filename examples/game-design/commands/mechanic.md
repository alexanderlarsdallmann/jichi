---
description: Spec a single mechanic — what the player does, what the game does back, and why it is fun or hard.
---
Specify **one** game mechanic clearly enough that a builder could implement it and
a reviewer could judge it. One mechanic per file under `mechanics/`.

For the mechanic the user names, write:

1. **Name + one-line summary.**
2. **What the player does** — the input, the action, the timing.
3. **What the game does in response** — the rule, the numbers (speeds, costs,
   cooldowns, damage), the resulting state.
4. **Why it is fun or hard** — the decision or skill it gives the player. If you
   cannot answer this, the mechanic is decoration; say so and reconsider it.
5. **Edge cases** — what happens at the boundaries (nothing left, everything at
   once, the player does the unexpected).

Be concrete: "the player double-jumps" is not a spec; "pressing jump within 0.2s
of leaving a platform gives a second, weaker jump (60% height)" is. End by noting
how you would playtest it — the fastest way to see if it is actually fun.
