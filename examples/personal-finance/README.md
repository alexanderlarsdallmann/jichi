# examples/personal-finance — the fourth domain-scaffold slice

A ready-to-use **personal-budgeting bench** for jichi: the agents, commands,
skills, and conventions that turn an empty `.jichi/` into a guided workspace for
learning to budget your own money — in plain files you own, kept local and
private. It is the fourth pilot from the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
after `data-analysis`, `game-design`, and `project-management`, built in
`examples/` first so the beginner-support contract is exercised before it becomes
a compiled-in `init personal-finance` pack.

## Educational, not financial advice

This bench **teaches budgeting methods** (envelope budgeting, 50/30/20, an
emergency fund) that you apply to your own numbers. jichi is a *learning tool*,
not a financial, investment, tax, or legal advisor; it does not tell you what to
do with your money or recommend products. For debt strategy, investing, taxes, or
a crisis, consult a qualified professional. Your money is **private**: every
number stays in plain local files you own — nothing is uploaded — and a **local
model** (see `config.example.json`) keeps it on your machine entirely. This is
baked into the `AGENTS.md` and both agents, not a fine-print afterthought.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — the educational-not-advice line, the local-and-private rule, and methods-not-verdicts, all up front |
| `START_HERE.md` | a numbered first-session walkthrough — budget → check → track → review → goal |
| `agents/budget-coach.md` | a guide that teaches the *method* behind each step and does the arithmetic — no judgement, no advice |
| `agents/budget-reviewer.md` | a **read-only** reviewer that checks a budget's *arithmetic and structure* (does it balance, do categories sum, is there an emergency-fund line) — checks math, never gives advice — the pack's soul |
| `commands/{budget,track,review-spending,savings-goal}.md` | the loop as slash commands |
| `skills/{envelope-budgeting,50-30-20,emergency-fund}/SKILL.md` | the methods, each with the educational-not-advice framing |
| `config.example.json` | a template config recommending a **local** model for privacy — secret-free |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-budget && cd my-budget
mkdir -p .jichi
cp -r /path/to/jichi/examples/personal-finance/agents   .jichi/agents
cp -r /path/to/jichi/examples/personal-finance/commands .jichi/commands
cp -r /path/to/jichi/examples/personal-finance/skills   .jichi/skills
cp    /path/to/jichi/examples/personal-finance/AGENTS.md .
# then set up a (preferably local) model, and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and build your first budget.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_personal_finance.sh`](../../tests/smoke/example_personal_finance.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_personal_finance.sh
```

## Status

**Pilot, in `examples/`.** Once exercised, it graduates into a compiled-in `init
personal-finance` pack + a `budgeter` setup preset (proposal §7), alongside the
sibling `data-analysis`, `game-design`, and `project-management` slices.
