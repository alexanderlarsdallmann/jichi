# Design: language scaffolding packs (C++, Perl, R, Guile, Racket, Clojure, Haskell, Elixir, Erlang, Emacs Lisp)

**Status:** design + implementation (2026-07-10).

## Goal
`jichi init <lang>` should drop a project into a **best-practice** starting
config for its language: a domain `AGENTS.md`, a language-specific read-only
**reviewer**, a `config.example.json` (testCommand + LSP + formatter), and one
high-value language **skill** — reusing the generic agents/skills/commands the
`default` pack already ships. Extends the existing `c-cli`/`zig-cli`/`python-cli`/
`rust-cli`/`go-cli`/`web-ts` packs to the rest of the requested languages.

## What exists (reuse)
`src/scaffold/jc_scaffold.c` is compiled-in pack tables: a pack is a
`struct jc_scaffold_file[]` of `{relpath, lines[]}`, where `lines[]` is a
NULL-terminated array of per-line string chunks (each <509 chars, C89). A language
pack (see `c-cli`) lists the generic files (`FILE_REVIEWER`, `FILE_TEST_WRITER`,
`FILE_DEBUGGER`, `FILE_PLANNER`, docs agents, the `FILE_SK_*` skills, the `FILE_CMD_*`
commands, `FILE_MENTOR` + `learn`) PLUS four language extras:
`FILE_AGENTS_<L>`, `FILE_<L>_REVIEWER`, `FILE_CFG_<L>`, and one `FILE_SK_<lang-skill>`.
Then a `PACKS[]` entry registers it.

## The addition (per language)
For each of {cpp, perl, r, guile, racket, clojure, haskell, elixir, erlang, elisp}:
1. `FILE_AGENTS_<L>` — conventions + build/test/format/lint + do/don't.
2. `FILE_<L>_REVIEWER` — `agents/<lang>-reviewer.md`, `readonly: true`, focused on the
   language's real failure modes (e.g. C++ UB/lifetimes, Haskell space leaks/partial
   functions, Elixir/Erlang OTP + let-it-crash, elisp byte-compile/package hygiene).
3. `FILE_CFG_<L>` — the exact `config.example.json` MERGE-warning + `testCommand` +
   `lspServers` (+ `formatCommand` where a canonical formatter exists). elisp has NO
   standard LSP (byte-compile + checkdoc + flymake are the tools), documented as such.
4. `FILE_SK_<X>` — one language skill (the analogue of C's valgrind-triage:
   sanitizer/perlcritic/lintr/contract/clj-kondo/space-leak/dialyzer/byte-compile
   triage, or a REPL-driven-debugging skill for Guile which lacks a standard LSP).
5. `<L>_FILES[]` table (generics + the four extras) + a `PACKS[]` entry.

Content is authored per-language for accuracy (parallel drafting), then encoded to
C89 chunk arrays with `scratchpad/encode_chunks.py` (escapes `"`/`\\`, `\n` per
line, splits >460-char lines into adjacent literals). `test_scaffold.c` already
asserts every shipped asset parses (frontmatter + required keys) and the JSON
examples are valid; new `find_pack` checks are added for each new pack.

## Naming
Packs use the language name (not `-cli`, since several are not CLI-shaped): `cpp`,
`perl`, `r`, `guile`, `racket`, `clojure`, `haskell`, `elixir`, `erlang`, `elisp`.
(`c`/`zig`/`python` remain as the existing `c-cli`/`zig-cli`/`python-cli`.)

## pygimix
`~/development/folio_projects/pygimix` is a large Python
project (FOLIO migration tooling; ~158 `.py`, some very large files, pytest, a
`.venv`, an existing `AGENTS.md` + a Continue `.continue/config.yaml`). Deliverable:
a solid `local/config.json` (git-ignored) — the global models inherited, Python
`testCommand`/`lspServers` (pyright or pylsp), a `contextLimit` sized to the model,
prompt-cache + snapshots on, and `--tool-profile`/`repoMap` tuned for a big repo —
plus the `python-cli` `.jichi/` assets. Its existing Continue config can seed models
via `jichi-convert`.
