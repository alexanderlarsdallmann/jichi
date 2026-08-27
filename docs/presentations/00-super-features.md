---
marp: true
title: jichi — super features
theme: default
paginate: true
---

<!-- _class: lead -->

# jichi

### An AI coding agent in **C89**, one small binary

A ground-up rewrite of the Continue CLI: chat, an agentic tool loop, RAG,
autonomy, MCP, LSP, a TUI — in portable ANSI C, no runtime.

<!-- Opener: the pitch is "a full modern coding agent that fits in ~1.2 MB
(size-optimized; 1.7 MB as built by default, measured 2026-08-24) and runs
anywhere POSIX + libcurl exist." -->

---

# Ten things that surprise people

1. It's **C89**. Zero warnings under `-std=c89 -pedantic -Wall -Wextra`.
2. The binary is **~1.2 MB** (size-optimized); a headless turn lives in tens of MB RSS.
3. It **edits your code** — resilient/fuzzy multi-file patches with diffs.
4. It runs **autonomously** with guardrails — pausable/steerable mid-run.
5. It does **RAG** — hybrid BM25 + embeddings + rerank over your repo *and* docs.
6. It speaks **MCP** (client) *and* **ACP** (server, for editors like Zed).
7. It has **LSP** navigation *and* refactors (rename/format/code-actions).
8. It generates **images and speech**, and **plays/records** audio.
9. It won't `sudo` or drive a **motor** unbidden — every attempt audited.
10. It **checkpoints** every change, and runs on a **Pi or a phone** (Termux).

---

# The agent loop, in one breath

```
history + system + tools
  → provider.build_request()      (Anthropic or OpenAI, streamed)
  → HTTP/SSE                      (libcurl)
  → execute tool calls           (read/edit/run/search/git/…)
  → append results, loop         until a final answer
```

One loop, two providers, ~43 built-in tools, and it never branches on provider.

<!-- The whole thing is one function, jc_agent_run_turn; everything else is a
tool or a provider vtable. -->

---

# Guardrails you can trust it with

- **Modes:** chat (asks), plan (read-only), auto (autonomous).
- **Path fence:** file tools can't escape the workspace; reads may extend to
  named reference roots, writes never.
- **Autonomy envelope:** token/wall-clock/tool-call budgets, an edit-scope fence,
  a **verify gate** (run your tests), and a JSONL audit journal — with opt-in
  **auto-revert** of shell-introduced changes outside the edit scope.
- **Multi-file edits keep their promise:** `apply_patch` validates all-or-nothing
  and, if a write fails partway, reverts the already-written files and reports
  every file's state.
- **Snapshots:** a shadow git repo checkpoints before the first edit — `/undo`
  and `/rewind` restore files *and* conversation.

> Budget exhausted mid-task? It verifies once and keeps passing work; it only
> rolls back a *red* tree. Partial progress isn't thrown away.

---

# It scales *up* too

- **Subagents** — delegate a scoped subtask (own history, model, tool fence),
  now two levels deep by default with a per-depth budget taper.
- **Parallel agents** — a fork pool, each task in an isolated git worktree, merged
  file-level first-wins.
- **Daemon** — a warm process keeps config/MCP/LSP/index hot and serves requests
  over a socket, with a **bounded worker pool** + per-request watchdog.
- **Loops & fleets** — a supervisor drains a task queue (tmux/systemd/cron); a
  coordinator fans work across peer instances over SSH + MCP.

<!-- This is what makes the zigodot rewrite feasible: fan out, isolate, merge. -->

---

# Real dogfood: the zigodot rewrite

jichi is driving a **large, autonomous Godot → Zig port** — the north-star test.

- Long `--auto` runs under the envelope, on a real codebase.
- Telemetry + per-session timelines show where tokens/cost actually go.
- The **learning loop** feeds its own logs back as durable lessons so it stops
  repeating mistakes.
- Every war story that taught us something lives in `docs/ANECDOTES.md`.

The hard parts (context overflow, cacheless economics, isolation bugs) were found
by *using* it, not theorizing.

---

# Meets you where you work

- **Terminal:** a real TUI — markdown + syntax highlighting, live diffs, one-key
  approvals, `/cost`, `/context`, `/undo`.
- **Headless:** `jichi -p "…"`; `--output json`/`jsonl` for automation.
- **Editors:** Emacs, Vim/Neovim, nano, and any **ACP** editor (Zed).
- **Remote:** SSH + tmux for long autonomous runs on a GPU/CI box.
- **Your language:** `"language": "日本語"` and it answers in it — the approval
  prompt follows (en/de/es/ja/zh); onboarding docs in all five.

One binary, every surface, the same contract underneath.

---

# Local, private, cheap

- Point it at **any OpenAI-compatible endpoint** — LM Studio, llama.cpp, LocalAI,
  vLLM. No vendor lock-in; keyless local servers just work.
- **Prompt caching** (both providers) + cache-aware cost accounting.
- **Media generation** against a local **LocalAI** binary — verified image gen on
  a consumer GPU with no Docker.
- Runs where a container can't: embedded, low-RAM, air-gapped-ish.

---

# Footprint, measured (2026-07-28)

Same host (Linux x86-64, 32 cores), committed scripts (`tests/measure/`):

| | jichi 0.9.0 | opencode 1.17.7 | Claude Code 2.1.220 |
|---|---|---|---|
| executable on disk | **1.4 MB** | 150 MB | 263 MB |
| `--version` peak RSS | **10 MB** | 184 MB | 247 MB |
| TUI idle, 60 s (median RSS) | **13 MB, flat** | 583 MB (peak 900) | 224 MB |
| CPU burned while idle | **0.0 s** | 6.0 s | 0.8 s |
| 200-turn session slope | **~10 KB/turn** (soak, mock model) | — | — |

<!-- Honesty notes, say them out loud: the tools do different amounts of
startup work by design (runtimes, update checks); "—" means we can't drive
the others against a mock comparably, so we don't pretend to. Environment
snapshot, not a benchmark of record: docs/analysis/2026-07-28-footprint-
comparison.md has method + caveats. -->

---

<!-- _class: lead -->

# Why C89?

Because "portable, tiny, dependency-light, and still here in ten years" is a
feature — for embedded targets, for teaching, and for trust.

**Dependencies:** libcurl — linked, not vendored. No third-party source in the tree at all. That's it.

---

<!-- _class: lead -->

# Try it

```sh
make && ./jichi setup            # guided setup
./jichi -p "explain this repo"   # headless
./jichi                          # the TUI
```

Docs: `README.md`, `docs/ROADMAP.md` (themed index), `docs/AGENTS_GUIDE.md`.
