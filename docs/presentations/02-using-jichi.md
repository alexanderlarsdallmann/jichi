---
marp: true
title: Using jichi
theme: default
paginate: true
---

<!-- _class: lead -->

# Using jichi

### From first run to autonomous task

---

# Get set up in one command

```sh
jichi setup                      # interactive wizard
# or, reuse your global config in a project:
jichi setup --from-global --preset developer
# or, adopt an unfamiliar repo (propose-only analysis + tutorial draft):
jichi setup --onboard
jichi --config local/config.json doctor   # validate everything
```

`doctor` checks libcurl, config, models, keys, reachability, git, MCP, LSP, and
your project assets.

---

# Three ways to run it

| Surface | Command | When |
|---|---|---|
| **TUI** | `jichi` | Interactive work, review, one-key approvals. |
| **Headless** | `jichi -p "…"` | Scripts, CI, automation, SSH. |
| **Structured** | `jichi -p "…" --output jsonl` | Another program/agent drives it. |

stdin is read when the prompt is `-`, so a whole file can be the prompt with no
`ARG_MAX` limit.

---

# Modes: how much leash?

- **`/chat`** — normal; it asks before mutating actions.
- **`/plan`** — read-only; investigate and propose, no edits.
- **`/auto`** — autonomous; runs its sandboxed tools without prompting.

```sh
jichi --plan -p "how would you add feature X?"    # a plan
jichi --auto -p "add feature X and make tests pass"  # do it
```

<!-- Plan mode is the safe default for exploring an unfamiliar or shared repo. -->

---

# The tools it has

- **Files:** `read_file`, `write_file`, `edit_file`, `apply_patch` (atomic
  multi-edit), `list_files`, `search_code`.
- **Run:** `run_terminal_command` (+ background), `run_tests`.
- **Knowledge:** `codebase_search`, `search_docs`, `fetch_url`, `web_search`.
- **Git:** status/diff/log/blame + add/commit/branch/stash.
- **Nav/refactor:** LSP `find_definition`/`references`/`symbols`, rename, format.
- **Delegate:** `spawn_subagent`, `spawn_parallel`.
- **Media & sound:** `generate_image`, `generate_audio`, `transcribe_audio`,
  `play_audio`, `record_audio`.

---

# Context you bring to a turn

`@`-references in a plain message pull context in:

```
review @src/parser.c against @diff and the @rss:https://…/releases.xml feed
explain @sym:jc_agent_run_turn and @folder:src/net
```

`@file @diff @url @rss @sym @docs @problems @folder @mcp @audio @img` — each
resolves to a bounded block appended to your message.

---

# When a session gets long

- **Auto-compaction** summarizes the old prefix to stay within the window;
  **mid-turn compaction** trims heavy tool output on a single runaway turn.
- **Token calibration** learns each model's real bytes-per-token so the estimates
  stop running optimistic.
- `/context` shows the live budget breakdown; `/cost` the running spend.

You mostly never think about it — that's the point.

---

# Autonomous runs, safely

```sh
jichi --auto \
  --verify "make test" \
  --budget-tokens 500k --deadline 30m \
  --edit-scope "src/**" \
  --journal run.jsonl --control \
  -p "fix the failing ring-buffer tests"
```

Pass → advance; fail → fix-forward N times, else roll back to the last green.
Everything is in the journal.

Steer it live: `jichi control <sock> status | inject "…" | pause | abort`.
Read it back: `jichi runs` and `jichi audit` (both `--output json`).

---

# In your editor

- **Emacs** (`jichi.el`), **Vim/Neovim** (`jichi.vim`), **nano** (`jichi-nano`) — all
  over the headless contract.
- **Zed / any ACP editor** — `jichi serve` (granular approval, streaming).

```vim
:JichiAsk how does the fence work?    " answer in a scratch split
:'<,'>JichiRegion tidy this           " transform a selection in place
:JichiTask add a test for edge case Y " agentic, confirms first
```

---

<!-- _class: lead -->

# Handy commands

`/model` `/mode` `/diff` `/undo` `/rewind` `/compact` `/context` `/cost`
`/skills` `/mcp` `/export` `/fork` `/sessions`

`export` a transcript for a PR or a class; `fork` to explore a branch without
losing your place.
