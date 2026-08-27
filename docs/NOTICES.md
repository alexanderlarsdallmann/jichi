# The bracketed tags — the registry

jichi speaks to three parties in bracketed tags: **the model** receives
notices in its conversation (`[envelope] budget check: …`), **drivers** grep
for them in captured requests, and **prompts teach them** (the M355 flight
plan names the budget check so the model recognises it later). That makes
every tag a three-party protocol — and M368 showed what happens without a
registry: the flight plan's teaching sentence contained a driver's grep
needle, and the driver was red for thirteen milestones. This page is the
registry; `tests/smoke/notice_tags_lint.sh` checks it against the source
both ways, and checks that each **structural form** (the part a driver may
pin) exists in exactly one render site.

Tag shape, as the lint extracts it: a lowercase word (2+ chars, hyphens
allowed) opening a bracket and closed immediately by `]` or `:`, at a string
literal's start or after `\n`/`%s` inside one. `src/scaffold/` is excluded —
pack content is authored prose (it defines its own conventions, e.g. the
mentor skill's `[evidence: …]` request). Multi-word bracketed riders
(`[fuzzy match]`, `[stopped at its iteration limit]`, the elision markers,
compaction's `[Earlier conversation summarized …]`) are prose sentences, not
tags, and are documented where they live.

## Model-facing tags (these reach the model's context)

Rules for authors: a new notice gets a row here BEFORE it ships; a driver
pins the **structural form**, never the bare tag (M368); prompt prose may
name a tag but must never contain a structural form.

| Tag | Structural form (drivers pin this) | Carrier | Since |
| --- | --- | --- | --- |
| `[envelope]` | `[envelope] budget check:` | user-role note, once per run at ~80% of an armed budget | M347 |
| `[envelope]` | `[envelope] the verifier just PASSED` | user-role note at a hollow periodic green | M351 |
| `[context]` | `[context] this turn has used` | user-role note, once per loop at the first pressed mid-turn pass | M358 |
| `[undo]` | `[undo] the operator restored` | user-role note after a TUI `/undo` that reverted files | M349 |
| `[resume]` | `[resume] since this conversation last ran` | user-role note at resume when believed files drifted | M350 |
| `[operator]` | `[operator] ` (message prefix) | user-role prefix on control-channel injects and type-ahead | M159/M254 |
| `[delegate]` | `[delegate] stop=` | tool-result addendum on `spawn_subagent`; one per delegated run | M437 |
| `[delegate]` | `[delegate] last failing call:` | tool-result addendum naming the delegate's last failure + its class | M437 |
| `[delegate]` | `[delegate] files changed:` | tool-result addendum, `spawn_parallel` write children only | M437 |
| `[note:` | `[note: the arguments` | rider appended to a repaired call's own tool result | M353 |
| `[stopped:` | `\n[stopped: ` (four variants: timeout, output cap, memory, interrupt) | rider on a killed command's tool result — the variant names the true cause (M342) | M26/M342 |
| `[image:` | `[image: ` | session-store placeholder for turn-ephemeral image attachments | M29 |
| `[lsp:` | `[lsp: ` | diagnostics rider appended to edit-tool results | LSP (M28-era) |
| `[codebase]` / `[docs:` | section headers | inside the auto-context block on the user message | M61 |

## Operator-surface tags (stderr logs, TUI lines, CLI listings — never sent to the model)

`[aborted]` `[acp]` `[auto]` `[command]` `[compact]` `[constraint]`
`[control]` `[deny]` `[error:` `[fallback]` `[history]` `[hook]`
`[interrupted]` `[jichi]` `[kinetic]` `[mcp]` `[mem]` `[model]` `[nudge]`
`[prefix]` `[privileged]` `[provider]` `[reachable]` `[route]`
`[self-review]` `[session]` `[telemetry]` `[tool]` `[tools]`

These label diagnostics for the human; the model never sees them, so their
wording is free to change (stderr is not an interface — EMBEDDING.md).

## Known bracket-shaped prose (not tags; the lint knows these four)

| Literal | Why it is prose |
| --- | --- |
| `[json]` | CLI usage syntax: `mcp call <tool> [json]` — optional-argument brackets |
| `[brackets]` | the setup wizard's help sentence "take the default in [brackets]" |
| `[evidence:` | the mentor's provenance trailer on a memory note (`[evidence: …]`) — **authored by the model**, kept by `learn apply` since M600 and read back by `learn analyze`; a convention the draft parser keys on, not a notice jichi renders. It reaches the model only inside a remembered note |
| `[pins:` | the M600 pin trailer (`[pins: tests/smoke/x.sh]`) naming the test, lint or constraint that holds a note; same provenance and same reach as `[evidence:`. `jc_insights.c` matches the literal to count the pinned share, which is what put it in this lint's scrape |

See also: [AUTONOMY.md](AUTONOMY.md) (the envelope notices),
[COMPACTION.md](COMPACTION.md) (the context gauge and elision markers),
[SNAPSHOTS.md](SNAPSHOTS.md) (the undo note), [SCRIPTING.md](SCRIPTING.md)
(resume drift), [CONTROL.md](CONTROL.md) (operator injects).
