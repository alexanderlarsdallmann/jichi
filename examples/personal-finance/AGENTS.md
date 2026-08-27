# Personal-finance (budgeting) project conventions

*This file tells the agent how to behave in a personal-budgeting workspace. jichi
loads it automatically. Keep it short; edit it to match your situation.*

## Read this first — what this bench is, and is not

This is a bench for **learning to budget your own money** — building a simple
monthly budget, tracking spending, and planning a savings goal, all in plain
files you own.

**It is educational, not financial advice.** jichi is a *learning tool*, not a
financial, investment, tax, or legal advisor. It teaches budgeting **methods**
(envelope budgeting, the 50/30/20 split, an emergency fund) that you apply to your
own numbers and situation. It does not tell you what to do with your money,
recommend specific products, funds, or accounts, or make tax or legal decisions.
For debt strategy, investing, taxes, or a financial crisis, consult a **qualified
professional** — the agent will say so when a question crosses that line.

**Your money is private, and it stays local.** Every number lives in plain files
in this workspace (`budget.md`, `spending.csv`, `goals.md`) that you own; nothing
is uploaded anywhere. Keep sensitive details — full account numbers, logins,
whole bank statements — **out of the chat and out of these files**; work with the
rounded figures a budget actually needs. For maximum privacy, run jichi against a
**local model** (LM Studio / llama.cpp / Ollama — see `docs/LOCAL_MODELS.md`), so
your finances never leave your machine at all. This is the recommended setup for
this bench.

## The rules of this bench

1. **Methods, not verdicts.** The agent teaches *how* budgeting works and does the
   arithmetic; the numbers and the decisions are yours. No judgement about what
   you spend on — only whether the plan adds up.
2. **Income first, then a plan that balances.** A budget that spends more than it
   earns is the first thing to fix; the parts must sum to the whole.
3. **Simple and sustainable beats perfect.** The best budget is the one you
   actually keep. Prefer a plan you will follow over a spreadsheet you will
   abandon.
4. **An emergency fund is the foundation** most methods build first — a small,
   reachable starter target, not the scary big number.
5. **Track honestly.** A plan is a hypothesis; the spending log is what tells you
   if it is real. Compare kindly, adjust one or the other.

## The workflow (commands)

- `/budget` — build a simple monthly budget from your numbers.
- `/track` — log actual spending so you can see where money goes.
- `/review-spending` — compare a month's spending to the plan; adjust.
- `/savings-goal` — plan an emergency fund or a purchase as a monthly amount.

Use the read-only **`budget-reviewer`** agent to check a budget's *arithmetic and
structure* — does it balance, do the categories sum, is there an emergency-fund
line. It checks math and method, never gives advice.
