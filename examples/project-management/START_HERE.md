# Start here — run your first project without dropping it

*A numbered, first-session walkthrough. If you have never run a project on purpose
— only drifted through them — do exactly these steps once. You will end with a
charter, a board, and the habits that make projects actually finish.*

## Before you begin

You need almost nothing: **jichi** (you have it) and **a model for jichi to think
with** (copy `config.example.json`'s `models` block into your config and fill in
your model — a local one is free and private; see `docs/LOCAL_MODELS.md`; then
`jichi doctor` to confirm). No tools, no accounts — this bench is plain markdown.

Pick a **real, small project** to run through this — a course assignment, a thing
you keep meaning to build, a room to declutter. Small and real beats big and
hypothetical. You will thank yourself for choosing small.

## Your first managed project, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`). You are in the TUI.

2. **Charter it — decide when it's *done* before you start.** Type:

   ```
   /charter
   ```

   jichi (the `project-manager` guide) will push you to state, in one page: what
   the project is, *why* it matters to you, and — the line that matters most —
   what **done** looks like as a checkable condition. If you cannot say when it is
   finished, you have found the reason projects like this never end. Fix that
   here. It writes `CHARTER.md`.

3. **Make a board.** Type:

   ```
   /kanban
   ```

   jichi sets up `BOARD.md` — `Todo`, `Doing`, `Done` — and helps you break the
   project into small, concrete cards. Notice the **WIP limit** on `Doing`: keep
   it to one card. This feels too strict. It is exactly right. (Read
   `skills/wip-limits` to see why.)

4. **Do the work — and check in with yourself.** Work a card. When you sit down
   (or stand up) next, do a solo standup:

   ```
   /standup
   ```

   Answer honestly: what did I *finish*, what's next, what's blocking me? This is
   the accountability a team gives you, done with the record instead. If a card
   has been "in progress" for three standups, that is a blocker — name it.

5. **Guard against drift.** When the project starts to sprawl (it will), ask:

   ```
   Spawn the scope-reviewer agent to review my project.
   ```

   It reads (never edits) and flags the killers — no definition of done, scope
   creep, an overloaded board, a stall. Ten minutes here saves weeks of drift.

6. **Finish, then reflect.** When you hit your definition of done — **stop**
   (done is a decision, not a feeling) — and run:

   ```
   /retro
   ```

   What went well, what didn't, how far off your time estimate was, and one small
   thing to try next time. That last line is how your *next* project goes better.

That is the whole loop: **charter → board → standup → review → retro.** Do it once
on something small and real. The habits — define done, limit WIP, check in
honestly — are what carry into everything else you build.

## If you get stuck

- Your board keeps growing → good, that is normal; run the `scope-reviewer` and
  move off-goal cards to a `Later` list. Cutting is allowed.
- A card has sat in `Doing` forever → it is blocked or too big. Break it down, or
  name the blocker in your next standup.
- It feels like too much process → it might be. This bench is meant to be *light*;
  drop any ritual that has become the work instead of serving it.
