# Scheduling project conventions

*This file tells the agent how to behave in a time-management workspace. jichi
loads it automatically. Keep it short; edit it to match your life.*

## What this bench is

A bench for **managing your own time** — planning realistic weeks, blocking focused
work, tracking deadlines, and getting better at estimating how long things take.
It is for the self-learner who sets their own hours and deadlines, and needs a way
to actually keep them.

**Plain files, no app required.** Everything is a `PLAN.md` (goals and deadlines),
a weekly `schedule.md`, and running records of estimates vs actuals — all yours,
all local. This complements a calendar app but does not need one; the *habits* are
the point, not the tool. It also pairs with the `project-management` bench (that
one is about the work; this one is about the time).

## The three rules that make a schedule work

1. **Estimate, then measure.** You underestimate how long things take — everyone
   does (the planning fallacy). The cure is data: estimate a task, record the
   actual, and learn your personal fudge factor (usually 1.5-2x). Plan with *that*,
   not with optimism. This is the most valuable habit here (`skills/estimation`,
   `/estimate-review`).
2. **Leave buffer.** Never schedule 100% of your time. Keep ~20-30% unscheduled for
   overruns and interruptions — because there is always a surprise. A wall-to-wall
   plan is a fantasy that breaks on the first slip (`skills/buffer-planning`).
3. **Protect focused time.** Deep work needs real, uninterrupted blocks in your
   peak hours; scattered minutes get nothing hard done. Block it and defend it
   (`skills/time-blocking`).

## How it goes

- **Work backward from deadlines**, with margin before them, not against them.
- **Match work to energy** — hard work in your peak hours, admin in the troughs.
- **Plan the week, review the week** — a short plan on Monday, a short review on
  Friday (what slipped, how far off the estimates). The review is where you learn.
- **Keep it light.** A plan you follow at 80% beats a rigid one you abandon.

## The workflow (commands)

- `/schedule` — plan a realistic week, working backward from deadlines.
- `/timeblock` — block a day into protected focus sessions.
- `/deadline-check` — check upcoming deadlines against the plan, before it's late.
- `/estimate-review` — compare estimates to actuals; learn your real numbers.

Use the read-only **`schedule-reviewer`** agent to sanity-check a plan before you
commit to it — it flags an overpacked schedule, no buffer, optimistic estimates,
and deadlines the plan won't actually hit.
