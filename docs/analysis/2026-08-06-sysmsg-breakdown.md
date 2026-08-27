# Where the system prompt's tokens go — measured (M312)

**Date:** 2026-08-06 · **Model:** `jlu/gemma-4-31b-it` (HRZ, 32k) · **Design note:**
[`proposals/2026-08-sysmsg-breakdown.md`](../proposals/2026-08-sysmsg-breakdown.md)
· **Predecessor:** [tool-profile cost (M310)](2026-08-06-tool-profile-cost.md)

M310 and M311 left the system prompt as the largest measured component of a call and
the only one the report could not explain. M312 made the builder report what it built.
The first thing the new report said was worth the milestone.

---

## 1. This repository, interactively

```
  system prompt     ~15196
    rules           ~10441
    repo map         ~3979
    craft             ~419
    persona           ~183
    safety            ~137
    environment        ~33
  tool definitions   ~3095  (16 tools)
```

The sections account for the whole prompt (the few tokens of slack are per-line integer
division, one per line at most). The old form named six sub-parts and left *"the base
persona + section headers"* unaccounted.

## 2. A graded attempt, with rules already skipped (M309)

```
  system prompt      ~626
    craft            ~329
    persona          ~144
    safety           ~108
    environment       ~44
```

**So the craft section (M299) is the largest part of a rules-free prompt** — bigger than
the persona it accompanies. That is a fair price for asking the agent to design before
it implements, and it is now visible to anyone who wants to weigh it (`craft: false`).

## 3. The finding: an `attempt`'s prompt is mostly a map of the wrong repository

The live telemetry said `sys_tok` **3952** for a graded attempt, while the report above
says ~626. The gap is the **repository map**: `attempt` runs in a git worktree of the
host project, so the map indexes *jichi's own 85k lines* — for an exercise that touches
two files.

Nothing in the system said so before this breakdown existed. Measured, `00-hello`:

| Config | Calls | Input | `sys_tok` | Result |
|---|---|---|---|---|
| `--tool-profile core` | 4 | 29,004 | 3,952 | PASS |
| `core` + `repoMap: false` | 4 | **9,232** | **827** | PASS |

**The system prompt fell 3,952 → 827 and the whole run 29k → 9.4k, same 4 calls, still
passing.**

### The cost of `00-hello`, over four milestones

| | Tokens | What changed |
|---|---|---|
| M308 (as found) | ~128k | — (and it FAILed at a 30k budget) |
| M309 | ~66k | `attempt` stops loading the host's rules file |
| M310 | ~29k | `--tool-profile core` |
| M312 | **~9.4k** | `repoMap: false` |

**13.6×**, for a task that writes one line into one file. Every step was found by making
a gauge honest first, and none of them changed what the exercise measures.

## 4. Why this is a recommendation and not a new default

The second task complicates it, which is why it was run:

| `06-make-the-test-pass` | Calls | Input | Result |
|---|---|---|---|
| `core` | 6 | 46,102 | PASS |
| `core` + `repoMap: false` | **9** | 28,351 | PASS |

Cheaper overall (−38%), but **50% more model calls**. The map buys navigation
efficiency: without it the model explores with `list_files`/`search_code` instead. On
`00-hello`, which navigates nothing, the map is pure waste. On `06` it saved three calls
and still cost more than it saved. **A task needing more navigation could flip that** —
and some curriculum tasks (20–22, on jichi's own arena lifetimes) are explicitly about
reading this repository.

So, unlike the rules file, the repo map is *sometimes* the thing being paid for. It is
also already the first section M73 truncates, and it is regenerable. The honest position:

- **Recommend `repoMap: false` for graded attempts on small self-contained tasks**, with
  the trade-off stated, exactly as for `--tool-profile core`.
- **Do not make it an `attempt` default.** M309's rules change was free — nothing graded
  depended on the host's contributor guide. This one is not free, and the evidence for
  the trade-off is two tasks, one of which moved in both directions at once.

## 5. What the breakdown does not tell you

- **Nothing about tool definitions per tool.** The 970–3,095 token tool array is one
  number; sizing individual tools is a different report.
- **Nothing about the history.** That is compaction's job and it has its own numbers.
- **It measures bytes, converted to tokens by the byte/4 heuristic × the M77
  calibration.** Same estimate as the compaction trigger, deliberately — but an estimate.
  The `sys_tok` figures in §3 come from the model's own `prompt_tokens` accounting and are
  the ones to trust when they disagree.
