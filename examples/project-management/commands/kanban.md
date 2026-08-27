---
description: Set up or update a plain-markdown kanban board — Todo / Doing / Done — with a WIP limit you actually keep.
---
Create or update `BOARD.md`, a plain-markdown kanban board that is the single
source of truth for where the project stands.

Structure it as three sections, cards as checklist items:

```
## Todo
- [ ] a concrete next action
## Doing   (WIP limit: 1-2)
- [ ] the thing I am working on right now
## Done
- [x] a finished card
```

When updating: move cards between sections, add new ones, mark done. Enforce the
rules as you go:

- **Keep `Doing` small** — one card is ideal, two or three the absolute max.
  Before adding to `Doing`, ask: can we finish something first? Starting many and
  finishing none is the solo trap.
- **Every card is a concrete next action** you could do in a sitting. If it is
  vague or too big, break it into smaller cards.
- **Every card traces to the charter.** A card that does not serve the stated
  goal is scope creep — put it under a `## Later` heading, do not silently work it.

End by showing the board and stating: what is in flight, and the single next
thing to pick up.
