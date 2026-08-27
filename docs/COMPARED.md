# jichi compared: Continue, opencode, Claude Code

*What is the same, what differs, and what jichi does not have. Revised
2026-08-21 (M522) and again 2026-08-22 (M531), when an independent reading of both
trees corrected five claims on this page — three of them in jichi's favour. The measured basis is
[`analysis/2026-08-09-opencode-continue-comparison.md`](analysis/2026-08-09-opencode-continue-comparison.md);
this page is the usable summary, extended to Claude Code and corrected where the
first pass could not be reproduced.*

## The sourcing rule, before anything else

A comparison is only worth reading if you can tell where each claim came from. So
every claim here carries its source, and the sources are of exactly three kinds:

| Kind | What it can support | Used for |
|---|---|---|
| **Source read** — the tree is on this disk and was read | how it works | opencode (`~/development/opencode`, HEAD 2026-08-19, 2,687 `.ts` files), Continue (`~/development/continue`, HEAD 2026-07-20, 1,429 `.ts` files) |
| **Config surface** — the format is parsed by `jichi-convert`, so its keys are known exactly | what it is configured with | all three, via `src/convert/jc_convert_{continue,opencode,claude}.c` |
| **Published behaviour** — its own documentation | what it claims to do | Claude Code |

**Claude Code's implementation is not readable**, so nothing here describes how it
works internally. Where this page says something about Claude Code, it is about
its *config surface* or its *published behaviour*, and it says which. That
boundary is the difference between a comparison and a rumour.

**Continue is archived.** Its README states: *"The `continuedev/continue`
repository is no longer actively maintained and is read-only for all users."*
Read at HEAD 2026-07-20. jichi is therefore a from-scratch reimplementation of a
tool that has since stopped, which is worth knowing before treating any Continue
row here as a live comparison.

**Dates matter and are given.** A comparison of moving software is a photograph.
opencode moved 97 commits between the 2026-08-09 analysis and this revision —
predominantly model-catalogue updates and generated churn, by commit subject —
and Continue did not move at all. Re-read before trusting a year-old row.

## Lineage: what jichi owes to whom

Stated plainly, because a comparison that hides its debts is advocacy.

- **jichi is a from-scratch reimplementation of the Continue CLI** (`CLAUDE.md`).
  Not a fork and not a port — no Continue code is present — but the shape of the
  thing, the config vocabulary, and the initial feature list came from it. That is
  why `jichi-convert` reads Continue configs first-class.
- **The slash-command shape is opencode's.** `docs/COMMANDS.md` credits it in its
  opening paragraph: markdown prompt templates invoked as `/name`, "inspired by
  opencode's custom commands".
- **Auto-compaction mirrors Continue and Claude Code.** `docs/COMPACTION.md`:
  when the history gets large, fold the old part into a summary and carry on.
- **`AGENTS.md` as the rules file is opencode's and Claude Code's convention.**
  `docs/RULES.md` says so. jichi reads `AGENTS.md` first and `CLAUDE.md` as the
  fallback precisely to be compatible with both.

## A note on counting files, and why this page mostly does not

The 2026-08-09 analysis compared the tools partly by counting files that mention a
term — "MCP (opencode 221 files, Continue 116)". Re-measuring for this revision,
**those numbers could not be reproduced**, because the analysis did not record the
command that produced them. For MCP in opencode today:

```sh
# in the opencode checkout
grep -rl mcp . --include='*.ts' | wc -l     # 169
grep -ril mcp . | wc -l                     # 436
```

The old figure of 221 sits between the two. Nothing is wrong with either count —
they answer different questions — but a count whose command is unrecorded is not
evidence, and the analysis's own §1 already warns that "a grep for a name answers
a question about vocabulary, not about capability".

So this page states capabilities and cites where they were read. Where a count
appears, the command appears with it.

## Where all four converge

These exist in all of them, and the convergence is the interesting part: four
independent teams reached the same shapes.

- **MCP** for tool servers, **LSP** for language intelligence, **ACP** for editor
  integration.
- **Compaction** of long histories.
- **Prompt caching** as an explicit concern.
- **Subagents** and **headless / non-interactive** operation.
- **A markdown rules file** in the project root.
- **Shadow-git snapshots.** opencode's `packages/core/src/snapshot.ts` keeps a
  separate git dir whose work tree is the project, under
  `global.data/snapshot/<project>/<hash(worktree)>` — which is jichi's
  architecture, arrived at independently.
- **The permission model, in the same words.** opencode has
  `Effect = "allow" | "deny" | "ask"` and `Reply = "once" | "always" | "reject"`.
  That is `jc_approval` and the TUI's `y/n/a` prompt. Neither copied the other.

Convergent design is usually evidence the design was right. It is also the honest
answer to "what makes jichi special?" for most of the feature list: nothing does.

## Where the others are ahead

From the analysis, still true on re-reading:

- **opencode has a run-level iteration cap**, which the first grep pass missed
  because it searched for `budget` and found only the model *reasoning* parameter.
  But this row over-corrected in the other direction and is now narrowed: read at
  M531, the run bound is per-agent **`steps`**, which defaults to unbounded, and
  `maxToolCalls` is **codemode's interpreter limit**, not a run bound. Neither
  opencode nor Continue bounds a run by tokens, cost or wall-clock; Continue's
  agent loop is a literal `while (true)`. Accuracy in both directions: the first
  pass under-credited them, this page then over-credited them.
- **Ecosystem and reach.** All three run on Node/TypeScript with the package
  ecosystem that implies: more providers, more integrations, more contributors,
  and a plugin story jichi does not have.
- **Managed tool output** was opencode's, and jichi took it — see *Outcomes*
  below.
- **A test that tests the tests.** This is the correction that most changes the
  story, because jichi treats its test-integrity apparatus as its strongest
  claim. opencode's `packages/opencode/test/server/httpapi-exercise/` **derives
  its own universe from the live API** — `routeKeys(OpenApi.fromApi(PublicApi))`
  — reports `missing`/`extra`, gates on both in CI, detects scenarios pointing at
  dead routes, and fails any scenario claiming to exercise the model without
  wiring the fake one. That is jichi's "audit the universe, not the result"
  doctrine, arrived at independently, and on that one surface its gate is harder
  than jichi's. What remains jichi's alone is the *breadth* — 41 lints, most of
  them policing **claims** rather than code — and `tests/teeth.sh`, the
  red-before-green ritual mechanised with vacuity detection in both directions.

## Where jichi differs by design

Different, not better — each of these buys something and costs something.

| Axis | jichi | The others |
|---|---|---|
| Language | **C89**, POSIX-only, zero warnings under `-std=c89 -pedantic -Wall -Wextra` | TypeScript on Node |
| Dependencies | **libcurl and nothing else**, linked not vendored; no third-party source in the tree | npm dependency graphs |
| Footprint | one binary; see [`analysis/2026-07-28-footprint-comparison.md`](analysis/2026-07-28-footprint-comparison.md) | a Node runtime plus `node_modules` |
| Teaching | a graded curriculum, 80 specs, assignments with **two-sided** graders proved through the product's own `grade`, a hint ladder, a progress record | nothing of this kind. The nearest relative is Continue's `manual-testing-sandbox/next-edit/` — 10 difficulty-graded exercise/solution pairs, with no frontmatter, no verify, no pass/fail, and nothing in the codebase referencing them |
| Project record | 65 anecdotes, 269 decision rows each naming what was rejected, 39 deferrals, 58 analyses | **rarer than unique.** opencode ships a *Dead Ends* table (`perf/test-suite.md`), an 843-line dated ADR log (`specs/v2/schema-changelog.md`), a deferral register with reasons (`specs/v2/todo.md`) and a controlled vocabulary with 119 invariants (`CONTEXT.md`). Continue ships ~49 lines of decisions and a 30-line PR checklist |
| Cost visibility | *published* per-tool output cost measurements (`docs/TOOL_OUTPUT_COST.md`) | **convergent as a mechanism** — opencode persists per-session cost and input/output/reasoning/cache tokens in SQLite; Continue computes per-request cost. The difference is that jichi published the measurement, not that it has the counter |

The C89 choice is the one with real consequences in both directions: it is why
jichi runs on machines the others cannot reach (`docs/PLATFORMS.md`,
`docs/LOW_MEMORY.md`), and it is why jichi will never have their provider
coverage. It is also, as `docs/CHOOSING_A_MODEL.md` notes, a
model-compatibility decision — almost any model ever trained has seen C89.

## Claude Code: what can honestly be said

Only two things, and both are marked as such.

**Its config surface**, exactly, because `jichi-convert` parses it
(`src/convert/jc_convert_claude.c`): `.claude/settings.json` carrying `model`,
`mcpServers`, `permissions`, `hooks` and `env`; `CLAUDE.md` as the rules file; and
`.claude/agents/*.md` plus `.claude/commands/*.md` whose **frontmatter is
compatible with jichi's**, which is why the converter carries those two verbatim.

**Three of those keys do not map**, and the converter reports them as notes rather
than guessing (`docs/CONVERT.md`):

- `permissions` — Claude's `Tool(spec)` pattern language is structurally unlike
  jichi's `permissions{allow,deny}` plus `editScope`;
- `hooks` — a different event model (`docs/HOOKS.md`);
- `env` — no jichi equivalent.

Anything else about Claude Code — how it compacts, how it decides, how it
performs — is not in this repository's reach and is therefore not in this page.

## The config-key matrix

What `jichi-convert` maps, and what it drops. This is the most reliable
comparison material available, because it is the part the code has to get right.

| Source concept | jichi equivalent | Notes |
|---|---|---|
| Continue `models[]`, opencode `provider/model`, Claude `model` | `models[]` | provider maps to `anthropic`/`openai`; anything else becomes OpenAI-compatible; keys become `apiKeyEnv`, never literals |
| opencode `model` / `small_model` | roles `[chat,edit,apply]` / `[summarize,autocomplete]` | |
| `mcpServers` (all three) | `mcpServers[]` | opencode's `command:[cmd,args…]` split; remote `headers{}` flattened |
| opencode `lsp{}` | `lspServers[]` | |
| Continue `docs[]` | `docs[]` | `startUrl` → `url` |
| opencode `instructions[]` | `instructions[]` | |
| opencode `permission{}` | `permissions{allow,deny}` | denying both `bash` and `edit` sets `mode: "plan"` |
| Continue `rules[]`, legacy `systemMessage`, `CLAUDE.md` | `AGENTS.md` | |
| Continue `prompts[]`/`customCommands[]`/`slashCommands[]`, opencode `command{}` | `.jichi/commands/<name>.md` | |
| opencode `agent{}` | `.jichi/agents/<name>.md` | tool allow-list → `readonly` + `tools:` |
| Hub `uses:` references | — | cannot be resolved offline; skipped with a warning |
| opencode `formatter{}`, per-model pricing | — | no equivalent; dropped |
| Claude `permissions` / `hooks` / `env` | — | reported as notes to port by hand |

## What jichi does not have

The credibility test for any comparison written by one of the participants. The
live register is [`DEFERRED.md`](DEFERRED.md) — parked items with the reason and,
where the reason had a checkable part, the check. Standing gaps as of M522
include instrumentation and cost work, the graded-attempt cost chain, gate
integrity items, and invariants known to be incomplete. `DEFERRED.md` is the
authority; this paragraph will go stale, that file will not.

Beyond the register, two structural absences are deliberate and documented:

- **No plugin ecosystem.** MCP is the extension surface.
- **No agent-to-agent negotiation, no shared mutable agent memory, no swarm
  consensus** ([`AGENT_COLLABORATION.md`](AGENT_COLLABORATION.md)) — agents
  interface through files, budgets, journals and verifiers, because an interface
  a human can read is one a human can audit.

## Outcomes: what the 2026-08-09 comparison actually changed

A comparison that recommends things and never reports back is a wish list. Both
recommendations, honestly:

- **§5.1 Managed tool output — taken.** Keep a bounded preview and tell the model
  where the remainder is, rather than truncating silently. Built in at **M440**;
  the reasoning and the measurements are in
  [`TOOL_OUTPUT_COST.md`](TOOL_OUTPUT_COST.md).
- **§5.2 Context Epoch — half taken, and the half that shipped is the smaller
  one.** The ask was a *named, durable* prefix baseline: the system context
  rendered once per epoch, stored, reused verbatim across restarts, replaced only
  at compaction. What exists is **detection**, not a baseline:
  `tests/smoke/prefix_churn.sh` (M365) warns when the system-prompt hash changes
  on three consecutive turns. So jichi can now notice that its cached prefix moved
  and still cannot answer "what exactly is the prefix, and when did it change"
  without reconstructing it from telemetry. Recorded as unfinished rather than
  quietly dropped.

## What this page cannot tell you

- **Nothing here is a benchmark.** No quality, speed or cost comparison between
  the tools has been run. Feature presence is not performance.
- **Claude Code's internals are unread**, by necessity.
- **opencode and Continue were read at one commit each**, months apart, by one
  reader who wrote one of the four tools. Take the convergence section as the most
  reliable part and the "ahead" section as the least.
- **A file count is not a capability**, and the one number this page quotes is
  quoted twice to show why.
