---
name: kanban
description: How to run a simple kanban board in plain markdown — visualize the work, limit it, and pull the next thing only when you have room.
---
# Kanban, in plain markdown

Kanban is a dead-simple way to manage work: **make it visible, limit how much is
in progress, and pull the next thing only when you finish something.** You do not
need software — three markdown headings are a real kanban board.

```
## Todo
- [ ] write the intro section
- [ ] add the example
## Doing        (WIP limit: 1)
- [ ] fix the parser bug
## Done
- [x] set up the project
```

**The three ideas:**

1. **Visualize the work.** Everything you might do is a card in `Todo`; the board
   is the truth about where things stand. If it is not on the board, it is not
   real work — it is a worry.
2. **Limit work in progress (WIP).** Cap how many cards are in `Doing` at once
   (for a solo project, 1 is ideal). This is the whole trick — see the
   `wip-limits` skill for why it works.
3. **Pull, don't push.** You do not *schedule* the next card; you *pull* it into
   `Doing` only when a slot opens (something reaches `Done`). Finish, then start.

**Why it fits a solo learner:** it is honest (the board cannot lie about what is
half-done), it is light (no tool, no ceremony), and it fights the number-one solo
failure — starting everything and finishing nothing. Move cards as you work; a
board that matches reality is worth more than a pretty one that does not.
