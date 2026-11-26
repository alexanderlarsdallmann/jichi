# The next reading courses: a plan

*2026-11-17. The operator asked which of jichi's features deserve a reading course
alongside the C source, then decided: "Please, put them all on the plan, including
code search, and the protocol clients. We may skip the yaml parser, for now." None of
the courses below is written yet. Each gets its own design and review page in
`docs/plans/` when its writing starts, as [Tazuna's](2026-11-13-tazuna-the-harness-course.md)
did.*

## 1. Where the courses stand

Four guides read the code:

| Guide | What it reads |
|---|---|
| [Annai](../reading/ANNAI.md) | one request, from `main` to the wire and back |
| [Fukabori](../reading/FUKABORI.md) | one design decision per chapter |
| [Tsuiseki](../reading/TSUISEKI.md) | recorded runs, quoted byte for byte |
| [Tazuna](../reading/TAZUNA.md) | `jichi-harness` |

A fifth, [Kiroku](../reading/KIROKU.md), reads the project's record rather than the code.

Large parts of the program are read by none of them: jichi's own JSON parser, the
portability layer, the terminal interface, code search, the protocol clients, how
jichi starts other programs, and the fences around a model's actions. Most of these
have reference pages. None has a page that walks a learner through its source.

## 2. When a course can be written

A course is held to the code it reads. `tests/smoke/reading_quotes_lint.sh` checks
that every quoted block still matches its file line for line, and
`tests/smoke/reading_refs_lint.sh` checks that every `file.c:function` anchor still
resolves. A course written on code that is about to change therefore goes red on the
day the code changes, and has to be rewritten. So each course below names the change
it waits for, if there is one.

## 3. The courses, in the order planned

### 3.1 The JSON layer

- **Reads:** `src/json/cJSON.c`, `src/json/cJSON.h`, `src/json/jc_json.c`,
  `src/util/jc_jsonrepair.c`.
- **Why:** every request jichi sends and every reply it reads passes through this
  code. It is jichi's own implementation of the cJSON interface, not a copy of that
  library (M171).
- **What a reader learns:** a recursive-descent parser and a printer in C89; who owns
  a parsed tree, and why every `cJSON_Parse` is matched by a `cJSON_Delete`; printing
  a `double` so that it reads back as the same number, and where toolchains get that
  wrong (the 75 armeb failures that `PLATFORMS.md` records); repairing the malformed
  JSON a model sends; and reading a boolean according to who wrote it (M519, M530).
- **Pairs with:** [`DATA_STRUCTURES.md`](../DATA_STRUCTURES.md),
  [`ALGORITHMS.md`](../ALGORITHMS.md).
- **When:** now. The code is stable.

### 3.2 Portability: probes and fallbacks

- **Reads:** the Makefile's capability probes (`make info` prints their results),
  `src/platform/jc_platform_posix.c`, `src/platform/jc_snprintf.c`.
- **Why:** `PLATFORMS.md` has 34 rows, and every non-Linux row so far has found bugs
  that existed on every platform (the [platform-widening plan](2026-11-13-platform-widening.md),
  section 1).
- **What a reader learns:** probing for a capability instead of naming a platform; a
  fallback that has to behave exactly like the call it replaces (C89 has no
  `snprintf`); and the platform findings as short lessons, among them the success
  value of `uname()` (ANECDOTES #97), a `va_list` passed by value (M1172) and musl
  refusing to seek in `/proc` (M1162).
- **Pairs with:** [`BUILD.md`](../BUILD.md), [`PLATFORMS.md`](../PLATFORMS.md),
  [`platform-notes/`](../platform-notes/README.md),
  [`PLATFORM_RETEST.md`](../PLATFORM_RETEST.md).
- **When:** alongside the platform retest the operator ordered on the same day
  ([`DECISIONS.md`](../DECISIONS.md#dec-2026-11-17-platform-scope)). Each finding of
  the retest can become a chapter on the day it is made.

### 3.3 The terminal without curses

- **Reads:** `src/tui/jc_term.c`, `src/tui/jc_tui.c`, `src/util/jc_utf8.c`,
  `src/util/jc_mdrender.c`.
- **Why:** jichi draws its interface with a small fixed set of escape sequences of its
  own, with no curses and no terminfo
  ([`DECISIONS.md`](../DECISIONS.md#dec-2026-11-11-tui)).
- **What a reader learns:** entering raw mode with `termios` and leaving it again; the
  line editor; UTF-8 text and the columns it takes; and a signal
  handler that only sets a flag, with the work done in the main loop.
- **Pairs with:** [`INTERFACE_TUTORIAL.md`](../INTERFACE_TUTORIAL.md),
  [`TUI_RENDER.md`](../TUI_RENDER.md), [`TUI_PRINCIPLES.md`](../TUI_PRINCIPLES.md),
  [`CJK.md`](../CJK.md).
- **When:** any time.

### 3.4 Code search

- **Reads:** `src/index/jc_walk.c`, `jc_repomap.c`, `jc_lexical.c`, `jc_search.c`,
  `jc_retrieve.c`, `jc_index.c`, `jc_docs.c`.
- **Why:** before a model can change code it has to find it, and what jichi puts into
  a limited context decides what the model sees.
- **What a reader learns:** walking a tree while honouring ignore rules; building a map
  of a repository by a heuristic line scan rather than a parser; a lexical score
  ("BM25-lite") fused with embedding search by reciprocal rank fusion; and choosing what
  fits into a context of a given size.
- **Pairs with:** [`REPOMAP.md`](../REPOMAP.md), [`RAG.md`](../RAG.md),
  [`ALGORITHMS.md`](../ALGORITHMS.md).
- **When:** any time.

### 3.5 The protocol clients, and the editor server

- **Reads:** `src/mcp/` (`jc_mcp.c`, `jc_mcp_proto.c`, `jc_mcp_stdio.c`,
  `jc_mcp_http.c`), `src/lsp/` (`jc_lsp.c`, `jc_lsp_proto.c`) and `src/acp/`
  (`jc_acp.c`, `jc_acp_proto.c`).
- **Why:** besides the model, jichi speaks to tool servers (MCP), to language servers
  (LSP) and, as a server itself, to editors (ACP).
- **What a reader learns:** how each protocol frames its messages; matching replies to
  requests by their ids; keeping a long-lived child process and its pipes; and
  treating what a server sends as content from outside.
- **Pairs with:** [`MCP.md`](../MCP.md), [`LSP.md`](../LSP.md),
  [`ACP.md`](../ACP.md), [`EDITORS.md`](../EDITORS.md).
- **When:** after batches PI-2 and PI-5 of the prompt-injection plan have landed. PI-2
  adds a line to what an ACP session reports, and PI-5 counts MCP results among the
  untrusted channels.

### 3.6 Running other programs

- **Reads:** `src/util/jc_spawn.c`, `src/util/jc_proc.c`, `src/tools/jc_tool_run.c`,
  `src/chat/jc_bg.c`, `src/util/jc_landlock.c`.
- **Why:** the shell tool, verifiers, git, hooks and background jobs all start other
  programs, and this is the code where systems differ most: the GNU/Hurd's one unit
  failure (M461's pipeline-kill check), FreeDOS's missing `fork`, and the
  background-command tests that the 2026-11-12 retest saw fail on slow boards are all
  in it.
- **What a reader learns:** one function in front of two ways to start a process,
  `fork` and `exec` or `posix_spawn` (M832); pipes and close-on-exec; reading output up
  to a cap; a timeout that stops a whole process group; background jobs whose output
  goes to a file rather than a pipe (M940); and the Landlock fence on a writing
  child's shell (M831).
- **Pairs with:** [`BACKGROUND.md`](../BACKGROUND.md), [`PARALLEL.md`](../PARALLEL.md),
  [`HARDENING.md`](../HARDENING.md),
  [`plans/2026-09-29-running-programs-on-dos.md`](2026-09-29-running-programs-on-dos.md).
- **When:** after def-269's design for systems that run one program at a time and
  PI-4's shell fence have landed. The command cancellation this code gained in M1192
  is already in.

### 3.7 The fences around a model's actions

- **Reads:** `src/chat/jc_perm.c`, `src/chat/jc_scopededit.c`,
  `src/util/jc_landlock.c`, `src/util/jc_priv.c`, `src/util/jc_untrusted.c`,
  `src/config/jc_config_trust.c`.
- **Why:** a fence bounds what a run can touch, and it stays on even when every cap is
  off. This is where a misread boolean twice turned a fence off without a word (M519,
  M530).
- **What a reader learns:** a fence against a cap; reading a policy boolean as its
  writer meant it (M843); a fence the kernel enforces, and a fallback that is never
  silent (M831); keeping keys out of core dumps and out of other processes' reach
  (M770, M771); and marking content that came from outside.
- **Pairs with:** [`AUTONOMY.md`](../AUTONOMY.md), [`HARDENING.md`](../HARDENING.md),
  [Fukabori ch. 6](../reading/fukabori-06-the-autonomy-envelope.md), which covers
  checking results and rolling back rather than the fences.
- **When:** with the prompt-injection batches PI-1 to PI-7, which change exactly this
  code. Each chapter can be the learner page of the batch that changes its subject.

## 4. Not planned now

- **The YAML parser of `jichi-convert`** (`src/convert/jc_yaml.c`): small and
  self-contained, but read by few. Skipped for now (the operator, 2026-11-17).

## 5. How each course will be written

As Tazuna was, and held to the same rules ([its plan](2026-11-13-tazuna-the-harness-course.md),
sections 2 and 3): Annai's level and Fukabori's honesty; anchors as
`file.c:function`, never line numbers; every quoted block verbatim; every published
command run first, in the published form; nothing claimed that was not read in the
code or measured; incidents told from the tree's own records, cited by milestone. The
drafts are reviewed against the code before anything is published, and what the
review finds goes on the course's own plan page, with any defect the writing finds in
the code.

Each course gets its name, in the manner of the others, when its writing starts.
