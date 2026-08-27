# Project-management project conventions

*This file tells the agent how to behave in a solo project-management workspace.
jichi loads it automatically. Keep it short; edit it to match your project.*

## What this bench is

A bench for **running a project by yourself** — a study project, a side project,
a course assignment, anything — and actually *finishing* it. It is for the
self-learner who can do the work but drifts, over-commits, or never ships.

**No external tools.** Everything is plain markdown files in this workspace: a
`CHARTER.md`, a `BOARD.md` kanban, standup notes, a `RETRO.md`. No Jira, no app,
no account — nothing to sign up for, nothing to sync, all yours and all local.
jichi drives the files; the project management happens in *your* head and habits,
which is what this bench is really training.

**Keep it light.** Process serves the project, not the other way round. Just
enough structure to stay honest and reach done — and no more. When a ritual
starts to feel like the work, cut it.

## The rules of this bench

1. **Charter before work.** Know *what*, *why*, and — most important — what
   **done** means, before you start. A project with no defined finish never ends.
2. **One board, one truth.** `BOARD.md` (Todo / Doing / Done) is where the project
   stands. If it is not on the board, it is not real work.
3. **Limit work in progress.** Keep `Doing` to one card (two or three at most).
   Finish before you start. This is the single most valuable habit here.
4. **Every card is a concrete next action** that traces to the charter. Vague or
   off-goal cards are how projects drift.
5. **Check in with yourself.** A solo standup (did / next / blockers) is the
   accountability a team would give you. Be honest; a stall named early is cheap.
6. **Retro to improve.** At the end, note what went well, what didn't, and one
   thing to try next time. That is how the next project goes better.

## The workflow (commands)

- `/charter` — write the one-page charter (what, why, definition of done).
- `/kanban` — set up / update the markdown kanban board. (Named `/kanban`, not
  `/board`: jichi has a built-in `/board` for its own phase board, and a
  project command of that name would be shadowed and never run.)
- `/standup` — a solo check-in: did / next / blockers.
- `/retro` — what went well, what didn't, what to change.

Before you trust that a project is on track, have the read-only
**`scope-reviewer`** agent read it — it flags the things that sink solo projects:
no definition of done, scope creep, an overloaded board, a hidden stall.
