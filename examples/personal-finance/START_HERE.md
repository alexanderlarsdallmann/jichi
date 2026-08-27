# Start here — build your first budget (privately)

*A numbered, first-session walkthrough. If you have never made a budget, do
exactly these steps once. You will end with a simple plan that balances, and the
methods to keep it — all on your own machine.*

## Read this first

This bench **teaches budgeting methods**; it is not financial advice. It helps you
apply methods to *your* numbers — the decisions stay yours, and for anything
bigger than budgeting (debt, taxes, investing), see a qualified professional.

And your money is **private**. Keep everything in the plain files here; do not
paste account numbers, logins, or whole bank statements into the chat — rounded
figures are all a budget needs. For real privacy, use a **local model** so nothing
leaves your computer (see `docs/LOCAL_MODELS.md`). That is the recommended setup
for this bench.

## Before you begin

You need **jichi** (you have it) and **a model** (copy `config.example.json`'s
`models` block into your config; a **local** one is best here for privacy; then
`jichi doctor` to confirm). Have your rough monthly numbers handy: what you take
home, and roughly what your regular bills are.

## Your first budget, step by step

1. **Open jichi here** (`cd` into this folder, run `jichi`). You are in the TUI.

2. **Build the budget.** Type:

   ```
   /budget
   ```

   The `budget-coach` will walk you through it in the right order: income first,
   then your fixed bills, then a simple **method** for the rest — it will explain
   **50/30/20** (needs / wants / savings) and **envelopes**, and let *you* pick.
   It puts a small **emergency-fund** line near the top of savings and explains
   why. It writes `budget.md`, and it checks that everything adds up. No judgement
   about any number — just a plan that balances.

3. **Check the math.** Before you trust it, ask:

   ```
   Spawn the budget-reviewer agent to check my budget.
   ```

   It reads (never edits) and flags *arithmetic and structure* problems — a plan
   that spends more than it earns, categories that do not sum, a missing
   emergency-fund line. It checks the math, not your choices.

4. **Track what actually happens.** A budget is a plan; tracking is the truth.
   Over the month, log spending:

   ```
   /track
   ```

   Keep it low-friction — rounded amounts are fine. (Only put in figures you are
   comfortable having in a local file.)

5. **Review and adjust.** After a few weeks:

   ```
   /review-spending
   ```

   See where you were on plan and where you drifted — stated as facts, not faults.
   The honest question is: should the *spending* change, or was the *budget*
   unrealistic? Either is a valid fix.

6. **Set a goal.** When you are ready, plan a savings target — a first emergency
   buffer is the classic starting one:

   ```
   /savings-goal
   ```

   It turns the goal into a monthly amount and an honest timeline from your budget.

That is the loop: **budget → check → track → review → goal.** Start small, keep it
simple, keep it private. A budget you actually keep — however modest — is the win.

## If you get stuck

- The budget won't balance → that is the point of the exercise; the coach will
  help find the smallest realistic change. It is information, not failure.
- The emergency-fund number feels huge → aim for a small first milestone, not the
  full amount. Read `skills/emergency-fund`.
- 50/30/20 doesn't fit your income → it is a starting frame, not a law; adapt it.
  Read `skills/50-30-20`. On a tight income, needs can exceed 50% — that is real.
- Anything beyond budgeting (debt spirals, investing, taxes) → this bench will
  point you to a professional, and it means it.
