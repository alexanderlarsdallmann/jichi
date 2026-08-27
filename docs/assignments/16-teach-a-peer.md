---
title: Author an assignment for a peer
audience: student
phase: documentation
difficulty: advanced
points: 4
verify: "sh docs/assignments/16-teach-a-peer/test.sh"
hints:
  - Pick something you struggled with in Stage 1 and design the task that would have taught it to you faster. Small is right -- one skill, one check.
  - Write the reference solution FIRST, then the check that accepts it, then break the solution and watch the check catch it. Only then write the brief and the ladder.
  - "A good rung answers the question the learner is actually stuck on, one level less abstract than the one above it: rung 1 re-frames the problem, rung 2 names the approach, rung 3 walks one concrete step. If rung 1 gives away the answer, you have written a solution in three fonts."
---
The last skill of the curriculum before the capstone: **change seats**.
Author a complete, mechanically gradeable assignment for a peer — brief,
rubric, hint ladder, a strict check, and the reference solution that proves
the check honest. Writing a good hint ladder will humble you faster than any
bug: you must remember what not-knowing felt like
([JOURNEY.md](../../JOURNEY.md), Ri).

Build your task under `docs/assignments/16-teach-a-peer/task/`:

| File | What it is |
|---|---|
| `task/spec.md` | the assignment: frontmatter (`title:`, `audience:`, `points:`, `verify:`, a `hints:` ladder of **at least two rungs**) + a task body containing a `## Rubric` section |
| `task/…` | whatever starting fixtures your task needs |
| `task/check.sh` | the mechanical floor: exits 0 only when your task is correctly solved |
| `task/solution.sh` | applies your reference solution to the fixtures (this is grader material — a peer would receive everything *except* this) |

Both scripts must begin with `cd "$(dirname "$0")"` and work relative to
their own directory: the meta-grader runs the solve half on a **throwaway
copy** of `task/`, so grading never mutates your shipped task (and grading
twice keeps meaning something).

The meta-grader holds your work to the discipline this whole curriculum was
held to (`tests/e2e/curriculum_graders.py` and the bench's
`check_graders.py` enforce it on ours) — your check must be **two-sided**:

1. the spec parses and carries the ladder and rubric;
2. `sh task/check.sh` **fails as shipped** — your task ships unsolved, and
   your check can tell;
3. after `sh task/solution.sh`, `sh task/check.sh` **passes** — your check
   accepts your own reference solution.

Design advice the grader cannot enforce but a peer will feel immediately:
tune the ladder to a level (that tuning *is* the teaching); make the rubric
say what *good* looks like, not just what *done* looks like; and give the
task a reason to exist — "practise X because it bit me in task Y" is a
better brief than a puzzle from nowhere. When it is done, inflict it on a
peer — or on a fresh session of your own agent (`attempt` mode) — and watch
where they stall. That watching is the module.

Grade with `jichi grade docs/assignments/16-teach-a-peer.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/16-teach-a-peer.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/16-teach-a-peer.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
