# Module 1 — Reading before writing

*Stage 1 (守（しゅ） Shu — Follow the form) · ~3–4 h · assignments:
[`01-find-the-setting`](../assignments/01-find-the-setting.md) (1 pt),
[`02-where-is-it-defined`](../assignments/02-where-is-it-defined.md) (1 pt) ·
map: [CURRICULUM.md](../CURRICULUM.md)*

The most common beginner failure with an agent is asking it to *change* code
neither of you has read. This module is about the reading half of the craft —
and about learning, concretely, **what the agent can and cannot see**.

## The work

**1. Ask about something it can see.** Open a session in your bench and ask a
question about a real file — not from memory, from the file:

```
read docs/assignments/01-find-the-setting/config.txt and tell me
what settings it defines
```

Watch the tool line: `▸ read_file …`. That line is the difference between an
answer *grounded* in your workspace and an answer that merely sounds right.
An agent that answers without reading is reciting; make yours read, and make
it **quote the lines** it based the answer on. (`@file` references inline a
file into your message directly — [REFERENCES.md](../REFERENCES.md).)

**2. Make it search, not guess.** Ask where something is defined, and insist
on evidence:

```
search this directory for widget_init and show me every hit,
then tell me which one is the definition
```

`▸ search_code` is the agent's grep. The classification — definition vs.
declaration vs. comment — is *your* judgment to check, and exactly what
assignment 02 grades.

**3. Learn the modes.** `/mode plan` puts the agent in a read-only stance: it
can look at anything, change nothing — the right mode for every question in
this module ([AGENT_MODES.md](../AGENT_MODES.md)). `/mode chat` (the default)
asks before each change. Feel the difference now, while the stakes are zero.

**4. Ask precisely.** The two assignments are graded on an answer file the
agent writes for you. Notice how much the *phrasing* of your request matters:
"the effective value of retries" and "the value of retries" are different
questions to a config file with history in it. Precision in, precision out.

## The assignments

Work both; grade from the bench root.

| Spec | Practices | Pts |
|---|---|---|
| [`01-find-the-setting`](../assignments/01-find-the-setting.md) | grounded answers; live vs. commented-out | 1 |
| [`02-where-is-it-defined`](../assignments/02-where-is-it-defined.md) | driving search; definition vs. mention | 1 |

## The gate

`jichi assignments` shows both Module 1 rows `passed`.

## Reflection

*(toward [JOURNEY.md](../JOURNEY.md)'s Shu marker)* — After this module you
should be able to say, before pressing enter, *which tool the agent will use*
to answer you. If its move still surprises you, ask more questions; the
predicting is the learning.

> **If you are stuck alone:** `/hint` first — the ladders in this set are
> written for exactly the stuck you are in. Then `/tutor <your question>`
> (a read-only helper that nudges, never solves). Then re-read the task's
> fixture files yourself, slowly. The answer to every Module 1 task is
> literally inside them.

---

[◀ Prev](00-a-working-bench.md) · [▲ Curriculum map](../CURRICULUM.md) · [Next ▶](02-the-smallest-change.md)
