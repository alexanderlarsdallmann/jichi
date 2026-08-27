# Repository map

jichi builds a **repository map** — a compact, deterministic index of the
project's source files and each file's top-level symbols — and injects it into
the system prompt so the agent knows the layout up front, instead of grep-guessing
its way around. It's the structural overview a human gets from skimming the file
tree and a few headers.

```
## Repository map
A high-level index of the project's source files and their top-level symbols, ...

src/chat/jc_agent.c: stream_once, run_agent_loop, jc_agent_run_turn, ...
src/util/jc_testparse.c: jc_testparse, jc_testparse_render, ...
include/jc_app.h: jc_app, jc_app_route_to, ...
...
```

## How it's built

- **Discovery** — a filesystem walk from the workspace root, skipping dot-dirs,
  `node_modules`/`target`/`build`/`dist`/`__pycache__`, and over-size files,
  keeping only files with a recognised source extension.
- **Symbols** — a fast, language-keyed **heuristic line scan** (not LSP, which
  would cost a server round-trip per file): top-level functions, types, classes.
  Languages: C/C++, Python, Go, Rust, JavaScript/TypeScript, Java, Ruby, shell,
  Racket (`.rkt`/`.rktl`), Scheme/Guile (`.scm`/`.ss`/`.sld`/`.sls`/`.sps`),
  Zig (`.zig`), Clojure (`.clj`/`.cljs`/`.cljc`), Elixir (`.ex`/`.exs`),
  Erlang (`.erl`/`.hrl`), and Haskell (`.hs`). Unknown extensions still list the
  file path.
- **Bounded & deterministic** — files are sorted by path; the rendered map is
  capped (default 12 KB; set `repoMapLimit`), with a per-file symbol cap and a
  `… (map truncated; N more files)` note when the budget is hit. Generated once
  at startup.

It's a *navigation aid*, not ground truth — heuristics miss and over-match. The
section tells the model as much: use `read_file` / `find_definition` /
`search_code` for detail.

## Surfaces

- **System prompt** — injected automatically (main agent) when there are source
  files. This costs tokens every turn (bounded by the cap); disable with
  `"repoMap": false`.
- **`map` CLI** — `jichi map` prints the map (no API key). Always builds,
  regardless of the `repoMap` prompt-injection setting.
- **TUI `/map`** — prints the current map.

## Configuration

```json
{
  "repoMap": true,        // inject the map (default true, but FALSE under --lite,
                          // which auto-enables below ~1 GB of RAM)
  "repoMapLimit": 12288   // byte budget for the rendered map (0 => built-in 12 KB)
}
```

## Limitations

- **Heuristic, not a parser** — multi-line prototypes may appear, some
  definitions are missed, and macro/string contents can occasionally false-match.
  Good enough to point the agent at the right file.
- **Top-level focus** — class/struct methods are generally not expanded (the
  type name is listed); the goal is a map, not a full outline (`list_symbols`
  via LSP gives a precise per-file outline).
- **Regenerated each startup** — no on-disk cache yet; the file-count and byte
  caps keep it cheap even on large trees.
