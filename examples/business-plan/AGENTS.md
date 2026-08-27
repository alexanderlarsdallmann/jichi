# Business-planning project conventions

*This file tells the agent how to behave in a small-venture planning workspace.
jichi loads it automatically. Keep it short; edit it to match your idea.*

## Read this first — what this bench is, and is not

This is a bench for **planning a small venture or side project the lean way** — a
one-page model and cheap experiments to test whether the idea is real, before you
build it or spend on it.

**It is educational, not business, financial, or legal advice.** jichi is a
learning and thinking tool, not a financial advisor, accountant, or lawyer. It
helps you *structure your idea, surface your assumptions, and test them cheaply* —
it does not tell you to start the business, promise it will work, or make tax,
legal, registration, or investment decisions. For those, consult a **qualified
professional**; the agent will say so when a question crosses that line. Your plans
and numbers stay in local files you own; for real privacy, a **local model** keeps
them on your machine (`docs/LOCAL_MODELS.md`).

## The one idea that runs through everything

**A business idea is a stack of assumptions. The job is to test the riskiest ones
cheaply — before building anything.** A long, confident plan and a fully-built
product are both expensive ways to be wrong. Instead:

1. **One page, not forty.** A **lean canvas** captures the whole model on a page and
   makes every guess visible (`skills/lean-canvas`).
2. **Name the assumptions, rank by risk.** Which box, if wrong, kills the idea?
   Usually "customers have this problem and will pay." Test that first.
3. **Do the unit economics early.** Does *one* sale make money? Price minus cost to
   deliver, versus cost to acquire. If a single transaction loses money, scale makes
   it worse (`skills/unit-economics`).
4. **Validate cheaply.** Talk to real customers, run a small test, get a real signal
   (someone pays or commits) — before committing money or months
   (`skills/validation`).

## The rules of this bench

- **Mark guesses as guesses.** Most of the plan is assumption, not fact — label it,
  do not hide it.
- **A real signal beats a vanity metric.** Paying/committing customers, not page
  views or polite enthusiasm.
- **Walking away is a valid, money-saving outcome.** Finding out an idea does not
  work for $0 and a week is a win.

## The workflow (commands)

- `/lean-canvas` — the whole model on one page.
- `/market` — who *exactly* is the customer, and how badly do they have the problem.
- `/pricing` — the price, and whether one sale actually makes money.
- `/milestones` — the cheapest experiments that could prove your riskiest assumption
  wrong.

Use the read-only **`assumptions-reviewer`** agent to find the unstated and untested
assumptions — especially the riskiest one you are taking for granted.
