# zigodot × jichi — usage review

*Read-only analysis. No file in the zigodot project was modified. Every
recommendation that would touch zigodot is tagged and requires explicit approval
before it is applied.*

**Subject:** `~/development/miscellaneous/zigodot` — a
Zig reimplementation of the Godot engine (~76 Zig files, ~19k LOC), driven with
jichi for ~4 days.

**Evidence base:** `zigodot/local/config.json`, `AGENTS.md`, `.jichi/**`; telemetry
in `~/.jichi.d/telemetry/*.jsonl` filtered to the zigodot workspace (967
model calls, ~96.65M input tokens, 25 sessions, 2026-06-26 → 06-30); `jichi
telemetry --workspace`, `learn analyze`, `doctor`; the LiteLLM proxy's
`/v1/models`. Numbers below are quoted from that telemetry.

---

## Bottom line

The setup is **strong and well-tailored**: per-project config with fast/strong
routing, a real `verify`/`testCommand` (`zig build test`), `editScope`, `zls` +
`clangd` LSP, two hooks, a concise `AGENTS.md`, maintained `memory.md` +
`glossary.md`, and **7 agents / 8 commands / 6 skills** purpose-built for the
Godot→Zig porting workflow. `doctor` is 19/20 (only the benign `pathFence:false`
warning, covered by `editScope`).

The highest-value improvements are **not** in the model/context config (which is
well-matched) but in **tool ergonomics, observability, caching, and closing the
learning loop**.

---

## Findings

Each is labelled **FACT** (measured) or **UNDETERMINED** (the data needed to root-
cause it isn't in the current logs — see the observability note).

### F1 — The model invents non-existent `todo` tool names *(FACT; jichi-side fix)*
jichi registers exactly `todowrite` and `todoread` (`src/tools/jc_tool_todo.c`).
The model (qwen3-coder-next) frequently calls names that don't exist, and every
one fails:

| called name | calls | failures |
|---|---|---|
| `todowrite` (registered) | 35 | 0 |
| `todoread` (registered) | 1 | 0 |
| `todoadd` | 20 | **20** |
| `todo_write` | 3 | **3** |
| `todo_read` | 1 | **1** |

~24 wasted tool calls. The fix belongs in **jichi**, not zigodot: accept
the common aliases (`todoadd`/`todo_write` → `todowrite`, `todo_read` →
`todoread`). Helps every project; models reach for these names by habit.

### F2 — `apply_patch` fails ~72% of the time *(FACT count; reason UNDETERMINED)*
`apply_patch`: **43 calls, 31 failures (~72%)**. This is the single biggest tool
reliability problem (by contrast `run_terminal_command` 555/29-fail ≈ 95% ok,
`run_tests` 109/3 ≈ 97%, `read_file` 116/7 ≈ 94%).

The **cause is not recoverable** from current data: zigodot's telemetry is
metrics-tier (no tool I/O text) and the failing calls were headless/`--no-session`
runs (only 1 of 43 `apply_patch` calls is in a saved session, and it succeeded).
The plausible cause is `old_string`-not-matching on churning Zig code, but that is
a **hypothesis, not a measured fact** — do not treat it as diagnosed. See O1.

### F3 — The proxy delivers no prompt caching; `promptCache` is moot here *(FACT)*
Cache hit-rate is **0%** across all 967 calls (~96.65M input tokens, ~103k
avg/call). Crucially, jichi's OpenAI provider parses
`usage.prompt_tokens_details.cached_tokens` **unconditionally**
(`src/provider/jc_provider_openai.c`, not gated on the `promptCache` setting) — so
if the backend reported any cached portion, jichi would have recorded it. It never
did in 967 calls. Conclusion: the LiteLLM gateway (`api.hrz.uni-giessen.de`) and
its backends **do not deliver/report prefix-cache hits**, so enabling
`promptCache` would change nothing. This is *not* a config gap to fix — it's a
backend limitation. (jichi's caching machinery is correct; there's just nothing to
cache against here.) Revisit only if the endpoint later gains prompt caching.

### F4 — The learning loop has never run, despite textbook material *(FACT)*
`learn analyze` surfaces clear **edit redo-loops** — `src/editor/resource_manager.zig`
×5, `src/editor/script_editor.zig` ×4, `docs/ROADMAP.md` ×3–4 in single sessions
(fix/break/fix) — and there are 2 `verify_fail` route escalations (qwen3 emitted
code that failed `zig build test`). Yet there is **no `.jichi/lessons.draft.md`** and
no committed lessons/skills from this experience. This is exactly what the
propose-only mentor loop (`learn analyze` → `/learn` → review → `learn apply`) is
for.

### F5 — Context/model config is well-matched *(FACT — corrects an earlier guess)*
`contextLimit: 128000` with `routing.fast = qwen3-coder-next`. The proxy
(`api.hrz.uni-giessen.de` is a **LiteLLM gateway**, not raw vLLM) routes that
alias to a genuinely large-context backend: qwen3-coder-next handled **865 calls
at ~103k avg input with only 6 errors**. So `contextLimit: 128000` is appropriate
and there is **no system-prompt-overflow problem here**. (An earlier hypothesis
that the strong tier overflowed on escalation was **wrong**: the 4 fast HTTP-400
rejects at turns 3/9/10/11 are all on qwen3-coder-next, not gemma, and qwen3
demonstrably handles far larger prompts — so they are not context overflow. Their
cause is UNDETERMINED, see O1. Turns 15/19 show ~10003 ms entries = stall-timeout
retries, a separate, expected mechanism.) The only small-context models are the
local summarize/embed ones, and summarization is chunked (M30), so that's fine.

### O1 — Observability gap: zigodot can't diagnose its own tool/model failures *(FACT; meta-finding)*
F2 and the 400s are undiagnosable because runs log **metrics-tier** telemetry
(counts, no I/O text) and run headless (no session transcript). The single
highest-leverage change for *everything else* is to **capture full-tier telemetry
to a path outside the workspace** for a short window, so the next `apply_patch`
failures and 400s carry their error text and become root-causable.

### Minor / FYI (no change recommended)
- `pathFence:false` + `editScope` set — a valid M54 combination; doctor's warning
  is expected. Leave as-is.
- No `output-styles/` — low value for code work.
- `maxToolIters: 256` is high; only matters if a redo-loop turn thrashes — revisit
  only if F4's loops persist.
- External Godot reference tree is an operational dependency, already noted in
  `AGENTS.md`.

---

## Recommendations (ranked; nothing applied without approval)

1. **[jichi change] Add `todo` tool-name aliases** (`todoadd`/`todo_write`
   → `todowrite`, `todo_read` → `todoread`) in `jc_tool_todo.c` / tool dispatch.
   Eliminates F1's ~24 guaranteed failures and helps all projects. Small, testable.
2. **[zigodot config → needs approval] Turn on full-tier telemetry briefly** (a
   `logging` block / `--log-level full --log <path outside zigodot>`), run a normal
   day, then re-analyse F2/400s with real error text. Closes O1. Lowest-risk,
   highest-diagnostic-yield.
3. **[no action — evidence-based] `promptCache` is moot here.** Verified the
   proxy returns no `cached_tokens` in 967 calls and jichi parses that field
   unconditionally, so enabling it changes nothing (F3). Skip unless the backend
   later gains caching.
4. **[zigodot asset, propose-only by design] Run the learning loop**: `learn
   analyze` → `/learn` (writes only `.jichi/lessons.draft.md`) → you review → `learn
   apply` to commit memory notes + any new skill capturing the redo-loop gotchas.
   Optionally set `learnOnStop`. Targets F4. Nothing lands until you approve the
   draft.
5. **[depends on #2] Diagnose `apply_patch`** once full-tier logs exist; if it's
   `old_string` mismatch, the fix may be a jichi robustness tweak (fuzzy-edit
   behaviour) and/or an `AGENTS.md` note — decide with the evidence, not before.

---

## Verification (when changes are approved)

- Aliases (#1): unit test in jichi + re-check that `todoadd`/`todo_write`
  ok-rate goes to 100% in a follow-up telemetry slice.
- Full-tier (#2): confirm `apply_patch`/400 events now carry error text.
- Cache (#3): non-zero `cache_read` in telemetry / the TUI `cached=N` line.
- Learning (#4): `.jichi/lessons.draft.md` exists for review; after `apply`,
  `/memory` + `/skills` show the new entries; redo-loop recurrence drops.
- After any zigodot config edit: `jichi doctor` stays ≤ the existing one warning;
  `zig build test` untouched. **No zigodot file changes without explicit approval.**

---

## Execution & outcomes (applied with approval)

- **F1 (jichi):** `jc_tool_registry_find` now resolves todo aliases
  (`todoadd`/`todo_write` → `todowrite`, `todo_read` → `todoread`) via the pure
  `jc_tool_canonical_name`. Committed, CI green.
- **#2 (zigodot config):** upgraded the *existing* `logging` block (it was
  `metrics`, the silent cause of O1) to `level:"full"` with a path **outside**
  the project. (A first attempt added a duplicate top-level `logging` key that
  the existing one overrode — caught by JSON validation and fixed.)
- **#3 (cache):** confirmed moot — no action.
- **Context windows (admin-confirmed, by message on 2026-06-30):** the LiteLLM admin
  reports real windows of
  ~150k (`qwen3-coder-next`) and ~256k (`gemma-4-31b-it`) — both far above the
  ~103k observed usage, so F5 holds and the 400s are definitively not overflow.
  Declared per-model `contextLength` (150000/256000) as accurate metadata; kept
  the conservative top-level `contextLimit: 128000` (which overrides per-model in
  jichi and already keeps real usage safely under qwen3's 150k). No behavioral
  change — room to raise the cap later if more history headroom is wanted.
  **Provenance added 2026-08-09:** the date matters because the proxy does not publish
  these numbers — a locally-registered model can carry no declared context window at all
  (docs/analysis/2026-08-09-hrz-gateway-findings.md §1). So the only record of
  the real window is a message, which means it can go stale without anything noticing.
  That is the argument for asking the gateway to publish it rather than for us to keep
  copying it: a number that exists only in a conversation has no way to be wrong out
  loud.
- **#4 (learning loop):** added `mentor`/`learn` scaffold to `zigodot/.jichi/` and
  ran `/learn`. It exposed two real issues and one win:
  - **Loop bug (zigodot asset):** the scaffolded `learn.md` ran bare
    `jichi learn analyze`, which fails because zigodot uses a `./jichi`
    wrapper (no PATH install) — so the mentor got no evidence. Fixed the local
    copy to `` !`./jichi learn analyze --workspace .` `` (the `--workspace .` also
    feeds the mentor the aggregated 4-day history, not one recent log).
  - **Win:** with that fixed, the mentor root-caused the redo-loops to **two
    real zigodot defects** (verified read-only): `TextBuffer.delete_char`
    (`script_editor.zig:163`) bounds-checks the column against the *line count*
    and never checks `current_line` → possible panic; and `ResourceCache`
    (`resource_manager.zig:293`) uses `std.HashMap([]const u8, …, AutoHasher)`
    which hashes the slice header, not contents → the cache always misses
    (should be `std.StringHashMap`). The drafted `.jichi/lessons.draft.md` is
    **normalized + awaiting human review**; `learn apply` was NOT run.
  - **M72 recurrence (jichi):** the mentor model emitted shifted heading levels
    (`#`/`##`); hardened `jc_learn_parse_draft` to classify headings by content,
    not `#`-count, so the loop tolerates this. Committed, CI green.

### Open items for the user
- **Review `zigodot/.jichi/lessons.draft.md`**, then `./jichi learn apply` (writes
  `.jichi/memory.md` + skills — shapes every session).
- **Fix the two code defects** in zigodot (`delete_char` bounds; `ResourceCache`
  → `StringHashMap`) — likely root causes of the observed redo-loops.
- **jichi scaffold hardening (done — commit `579ce80`):** the shipped `learn.md`
  now resolves the binary wrapper-agnostically (`./jichi`-or-PATH) with
  `--workspace .`, so future `init`s work in wrapper-based setups out of the box.
