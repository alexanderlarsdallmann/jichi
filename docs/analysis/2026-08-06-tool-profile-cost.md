# `toolProfile: core` on a graded attempt — measured (M310)

**Date:** 2026-08-06 · **Model:** `jlu/gemma-4-31b-it` (HRZ, `contextLength` 32000)
· **Host:** the small reference box · **Design note:**
[`proposals/2026-08-tool-profile-cost.md`](../proposals/2026-08-tool-profile-cost.md)

M309 removed the project rules file from `attempt` and recorded the next lever without
pulling it: *"tool definitions are ~50% of each call, so `toolProfile: core` is where
the remaining cost lives."* This is that measurement.

---

## 1. Static: what each profile sends

No model, no network. Reproducible by anyone with the same config:

```
$ jichi context --tool-profile full
  tool definitions  ~2989  (16 tools)
$ jichi context --tool-profile core
  tool definitions  ~1193  (7 tools, core profile)
```

**−60% of the tool-definition prefix** (−1796 tokens per call), and this is the ceiling
on what the live half can save.

**Before M310 both lines read `~2989 (16 tools)`.** The report sized the *unfenced*
tool array while the agent loop applied the core fence, so the gauge was wrong in
exactly the configuration a user adopts to fix the problem the gauge diagnoses. That is
the defect this milestone fixed first; the numbers above are from the fixed build.

## 2. Live: `attempt` under each profile

Real `jichi attempt` runs in throwaway worktrees, metrics telemetry on, rules skipped
(the M309 default). `in`/`out` are the model's own `prompt_tokens`/`completion_tokens`,
not estimates.

### `00-hello` (1 point) — three runs each

| Profile | Calls | Input | Output | Total | Result |
|---|---|---|---|---|---|
| full | 6 | 65,425 | 287 | **65,712** | PASS |
| full | 7 | 76,677 | 437 | **77,114** | PASS |
| full | 6 | 65,329 | 296 | **65,625** | PASS |
| core | 4 | 29,004 | 129 | **29,133** | PASS |
| core | 4 | 29,004 | 192 | **29,196** | PASS |
| core | 4 | 29,004 | 193 | **29,197** | PASS |

### `06-make-the-test-pass` (3 points) — one run each

| Profile | Calls | Input | Output | Total | Result |
|---|---|---|---|---|---|
| full | 8 | 90,475 | 386 | **90,861** | PASS |
| core | 6 | 46,102 | 350 | **46,452** | PASS |

**Headline: ~55% fewer tokens and 2 fewer model calls, and both tasks still pass.**

### The per-call prefix, from the same telemetry

`model_call` events carry `sys_tok` / `tools_tok`, so the M309 estimate is checkable
rather than remembered:

| Profile | system | tools | tools as share of prefix |
|---|---|---|---|
| full | 3952 | 4459 | **53%** |
| core | 3952 | 970 | **20%** |

M309's "~50%" was right. Note `tools_tok` 4459 here vs 2989 in the static reading:
`attempt` runs with a different registry than a bare `context` in this workspace, which
is why the live figure is the one that governs.

## 3. The surprise worth recording

**`core` was deterministic; `full` was not.** Three `core` runs produced *identical*
input totals — 29,004 tokens, to the token — with the same 4 calls. The three `full`
runs took 6, 7 and 6 calls (65.4k / 76.7k / 65.3k).

I did not predict this and it is the more interesting result. With 16 tools the small
model sometimes takes an extra exploratory step; with 7 it walks the same path every
time. So on a small model the lean profile is not only cheaper, it is **more
repeatable** — which matters more for a *graded* run than the token count does, because
variance in the tool budget is what turns a pass into a FAIL at a fixed
`--budget-tokens`.

Caveat, stated because n=3 invites over-reading: three runs is enough to show `full`
varies and `core` did not vary *here*. It is not enough to claim `core` is always
deterministic.

## 4. What `core` costs

Seven built-ins: `read_file`, `write_file`, `edit_file`, `apply_patch`, `list_files`,
`search_code`, `run_terminal_command`. Everything conditional is dropped — including
the assignments feature's **`hint` and `ask_for_help`**, the tiered-learner machinery
`attempt` exists to exercise and whose use its own report counts ("N hints used").

**This was not visible in the runs above:** `assignments` is off in the measured
config, so neither arm advertised those tools and both reported "0 hints used". The
capability loss is real but was proven separately and mechanically, in
`tests/smoke/tool_profile.sh`: with `assignments: true`, `full` advertises `hint` and
`core` does not.

`doctor` already warns that `core` drops configured MCP/user/LSP tools. It says nothing
about the assignment tools, because they are built-ins.

## 5. Recommendation

- **Do not make `core` the default for `attempt`.** The M309 change was free — nothing
  the exercise measures depended on the host's contributor guide. This one costs a
  capability, and a change that costs a capability belongs to the operator, not to a
  default someone discovers by having a feature stop working.
- **Do recommend it for a learner on a modest budget**, in the tutorial and the plain
  pages: `jichi attempt --tool-profile core <spec>` roughly halves the cost of a graded
  attempt on a small model, at the price of the hint ladder.
- **`toolProfile: auto` already does the right thing for the small-context case** —
  below 12,000 effective tokens it resolves to `core` by itself. The 32k HRZ model sits
  above that threshold, which is why this had to be asked for explicitly.

## 6. What is still unmeasured

- **A `core` run that needs a hint.** Both tasks passed without one, so the capability
  cost has no measured behavioural consequence yet. The honest next probe is a task
  whose reference path uses the hint ladder.
- **`sys_tok` 3952 with rules skipped.** That is still the largest single component of
  each call. What is in it — persona, craft section, assignment spec, tool-use
  preamble — is not broken down anywhere, and `/context`'s "of which" line reports the
  *sections* it knows about, whose remainder is the base persona. Worth a slice.
- ~~**The `context` subcommand reports `rules ~0` always.**~~ **Fixed in M311**, and it
  was bigger than expected: on this repository the subcommand reported a **~750**-token
  system prompt where the real figure is **~15,200** (rules ~10,400, repo map ~4,000) —
  11% of the window instead of **57%**, before a single message. The feared cost (moving
  asset loading ahead of dispatch) turned out to be avoidable: `run_context` loads its
  own assets, as `run_sysmsg` already did, through one shared helper.

  **This changes the reading of §1 above.** The static figures (~2989 vs ~1193) were
  measured against a system prompt the same tool believed was ~750 tokens. The
  *comparison* stands — both arms ran seconds apart in the same build — but the
  share-of-window impression does not. Corrected, for an **interactive** turn in this
  repository: tool definitions are ~3,100 of ~18,300, i.e. **17%**, where the old report
  implied ~44%.

  That is not in tension with §2's **53%**: those runs are `attempt`, which skips the
  rules file (M309), so its prefix is ~4,000 of system prompt against ~4,500 of tools.
  Rules dominate an interactive turn and are absent from a graded one — which is the
  whole reason M309 removed them. The `sys_tok`/`tools_tok` telemetry was never affected
  by this bug; it comes from the agent loop, which was always right.
