# Start here — plan a week you can actually keep

*A numbered, first-session walkthrough. Do these steps once for the week ahead. You
will end with a realistic weekly plan, blocked days, and the start of the one habit
that fixes bad planning forever: measuring how long things really take.*

## The one thing to believe up front

**You underestimate how long things take.** So does everyone — it is called the
planning fallacy, and it is why plans fail. You cannot fix it by trying harder; you
fix it by *measuring*. So this bench is not about a perfect calendar — it is about
getting honest with your own time. Two more truths that follow: **leave buffer** (a
full-to-the-minute plan breaks on the first surprise), and **protect focused time**
(scattered minutes get nothing hard done).

## Before you begin

You need **jichi** (you have it) and **a model** (copy `config.example.json`'s
`models` block into your config; a local one is free and private —
`docs/LOCAL_MODELS.md`; then `jichi doctor`). Have in mind: your fixed commitments
this week, what you need to get done, and any **deadlines**.

## Your first realistic week, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`).

2. **Check your deadlines first.** Before planning, know what is coming:

   ```
   /deadline-check
   ```

   jichi lists your upcoming deadlines and how much work each still needs, and does
   the honest arithmetic: does the time before each deadline actually cover the
   work? Facing a tight deadline now, when you still have options, beats discovering
   it the night before. It writes `deadlines.md`.

3. **Plan the week — realistically.** Type:

   ```
   /schedule
   ```

   jichi (the `planner`) blocks your fixed points first, works backward from your
   deadlines, estimates each task **honestly** (using your history if you have it),
   and — crucially — **leaves ~20-30% of your time unscheduled** as buffer. It
   writes `schedule.md`. It will resist letting you pack every hour; that resistance
   is the point.

4. **Block a day.** Take tomorrow and make it concrete:

   ```
   /timeblock
   ```

   jichi assigns your deep-work tasks to real, protected time blocks in your peak
   hours, batches the shallow stuff, and schedules breaks. "Work on the essay"
   becomes "9:00-10:30, essay draft, phone away."

5. **Sanity-check before you commit.**

   ```
   Spawn the schedule-reviewer agent to review my plan.
   ```

   It reads (never edits) and flags what will break it — no buffer, an overpacked
   day, an optimistic estimate, a deadline the plan won't hit. Ten minutes here
   saves a collapsed week.

6. **At the end of the week, measure.** This is the step that makes you better —
   do not skip it:

   ```
   /estimate-review
   ```

   Compare what you *estimated* against what things *actually took*. Over a few
   weeks you will learn your personal fudge factor (probably 1.5-2x), and your plans
   will start holding. Being wrong about time is universal; *measuring* it is the
   skill.

That is the loop: **deadlines → plan → block → review-the-plan → measure.** Run it
each week. The plans get more realistic as your estimate data grows — which is the
whole point: a schedule built on your real numbers is one you can actually keep.

## If you get stuck

- Your plan collapsed by Wednesday → almost certainly no buffer, or optimistic
  estimates. Read `skills/buffer-planning` and `skills/estimation`; run
  `/estimate-review`.
- You keep missing deadlines → run `/deadline-check` at the *start* of every week,
  not the end. Early is cheap; late is a panic.
- You can't focus even in your blocks → the blocks are not protected (interruptions)
  or not in your peak hours (`skills/time-blocking`).
- The schedule feels like a cage → loosen it. A plan followed 80% beats a rigid one
  abandoned. This bench is meant to be light.
