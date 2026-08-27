---
name: game-loop
description: How to find and design the core loop — the short cycle a player repeats — because if the loop isn't fun, nothing else matters.
---
# The core loop

Every game is, at heart, a short cycle the player repeats over and over. In a
platformer: *see a gap → time a jump → land (or fall) → see the next gap.* In a
match-3: *scan the board → spot a match → swap → watch it clear → scan again.*
That cycle is the **core loop**, and it is where fun lives or dies.

**Why it matters most:** content, art, story, and menus all sit *on top of* the
loop. If the loop is boring, more of them just gives you more boredom. If the
loop is fun, a game made of grey boxes is already fun. So design the loop first
and prove it before building anything else.

**How to find yours:**

1. Describe what the player does in the shortest repeating unit — usually 3–10
   seconds. If you cannot describe it in one sentence, you do not have a loop yet.
2. Name the **decision** or **skill** in each cycle. A good loop asks the player
   to *decide* or *do* something with a real outcome. No decision, no engagement.
3. Name the **feedback** — how the player knows if they did well. Immediate, clear
   feedback is most of what makes a loop feel good.
4. Check the **stakes** — a small risk/reward each cycle keeps it alive; none
   makes it a chore.

**The test:** could you prototype the loop on paper or with grey boxes in an
afternoon, and would it *still* be a little bit fun? If yes, you have something to
build. If no, fix the loop before you touch anything else.
