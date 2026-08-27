# What the structure claims, and what the world does

*Analysis, 2026-08-27. The second seam survey of the day, after
[`2026-08-27-the-language-of-lessons.md`](2026-08-27-the-language-of-lessons.md)
closed the learning loop's seams (M596–M605). The operator's brief for this one:
"find the next seam to mend. It may have to do with hardening jichi, memory
control, and internal datastructures jichi uses to represent state, and task
information."*

## 0. Method, and a bias declared

Three read-only surveys ran in parallel over the tree, one per named concern —
process memory (arenas, buffers, fault injection), state and task structures
(`jc_app`, `jc_session`, `jc_todo`, `jc_envelope`, `jc_workflow`, the on-disk
formats), and hardening (every parser at a trust boundary, secrets, child
processes). Each returned ranked candidates with `file:line` evidence and a list
of what `DEFERRED.md` and the ROADMAP already knew, so nothing here re-proposes a
recorded decision.

A survey's claim is a claim. **Every finding in §2–§4 was then reproduced by
running it**, in a scratch workspace with the mock model, before a line of the fix
was written — and the reproduction *is* the smoke driver that now guards it, born
red on the M605 binary. Findings in §5–§7 are verified by reading the exact lines
and are marked so; none of them is fixed by inference.

The bias: I wrote M596–M605 earlier today, and one of the findings below (§4, the
harness pushed without the gate) is mine. The record says so where it applies.

## 1. The question that found them

Each structure in jichi makes a claim about the world: *this list is the plan*,
*this path is inside the workspace*, *this child cannot see the key*, *this buffer
holds one line*. The seams are where the claim and the effect were allowed to
disagree **silently** — no test, no warning, no journal event. The question that
finds them is not "is this code correct?" but "**what does this structure claim,
and what does the world do?**" Asked of the task list, the path resolver, the
secret registry and three peer buffers, it produced one answer four times: the
representation had a scope smaller than the promise written beside it.

The tests doctrine has a name for the shape — a mitigation "whose scope excludes
the highest-value asset" (`HARDENING.md` §1, written about M130). What is new is
that the same shape sat in **state**, not only in hardening.

## 2. The state seam: the plan lived in the process and nowhere else (M606)

**Claim.** `todowrite`: "Create or update the task list for the current work … Use
it to plan and track multi-step tasks." `SUBAGENTS.md`: "The task list is shown to
the human and outlives the subtask."

**World.** `struct jc_todo_list todos` was a field of `jc_app` — initialised at
process start, freed at exit, and in between touched by exactly one writer
(`todowrite`) and two readers (`todoread`, the TUI's status indicator). Not
mentioned once in `jc_session.c`, `jc_sysmsg.c`, `jc_compact.c` or `jc_agent.c`.
Nothing saved it; nothing restored it; nothing cleared it when the TUI's `/resume`
or ACP's `session/load` swapped conversations; `/clear` cleared the history and left
the list.

**Measured.** In the three telemetry logs on the development machine, `todowrite`
is **348 of ~408 tool events** — the tool the models call most, writing the least
durable state in the process. `tests/smoke/todo_resume.sh`, written before the fix:

```
not ok 2 - no todos in the session file: {"sessionId":"009a30c4-…","jichi":"0.9.0", …
not ok 3 - after --continue, todoread answered '(todo list is empty)' while the history holds a two-item list
not ok 4 - restored state missing: 'in-progress  write the codec' occurs 1 time(s) in request 2 (need 2)
```

The model's belief (its own `todowrite` result, still in the history) and the
tool's state disagreed, and nothing said so — the M350 drift shape, for the plan
instead of the files. `BOARD.md` had written the gap down as a property:
"session-ephemeral todo list (`todowrite`) … single-turn planning". A documented
limitation that contradicts the tool's own description is not a decision; it is
two claims.

**Mend (M606).** The session owns the list: `struct jc_session.todos`, with
`app->todos` a pointer to the live session's copy, set by whoever makes a session
live and cleared when ACP drops one. The codec carries it (`"todos"`, wire status
words, lenient on read), the fork deep-copies it, `/clear` drops it with the
history, and the list has a `gen` so a `todowrite` alone marks the session dirty.
The store gained the shape version it never had (`"v":2`), read leniently: a file
from a newer jichi loads with one warning, because a conversation is not
regenerable the way a calibration file is. 4 smoke checks, 3 unit tests, and a
fourth check on `session_fields_lint.sh` — whose struct-`jc_message` scrape could
never have seen a session-level field, so the three codec sites are now named.

**Rejected.** Rebuilding the list from the last `todowrite` in the history: mid-turn
elision and compaction rewrite exactly those arguments; a plan is state, not a
message. Refusing a newer store version: see above. Rendering the list into the
system prompt: the in-process `todoread` already answers correctly within a
process; the seam was the process boundary, and a second copy in the prompt is a
second thing to keep consistent.

## 3. The fence seam: the verdict was about a name (M607)

**Claim.** `jc_app_path_denied_ex`: a write path outside the workspace is refused
under `--auto`. `pathfence.sh` proves it for an absolute outside path;
`test_path.c` and the fuzz corpus prove it for a symlink to an *existing* outside
target.

**World.** `jc_path_resolve` canonicalizes with `realpath()`. For a target that does
not exist yet — every fresh write — `realpath()` fails, so the parent is
canonicalized and the **leaf is re-appended verbatim**
(`src/util/jc_path.c:164–213`). A leaf that is a symlink to a *not-yet-existing*
path is exactly that case: the verdict is "inside", and `fopen("wb")` then follows
the link and creates the target outside. A link to an existing outside file was
always caught, which is why every existing test planted an existing target.

**Measured.** `tests/smoke/pathfence_dangling.sh`, written before the fix, against
M605:

```
not ok 1 - fence ON but the link's outside target was created: /tmp/jichi_smoke.PCK94r/escaped.txt
not ok 3 - no refusal in the tool result
ok 4 - --no-path-fence: the same write followed the link (the fence was the guard)
```

The control matters: with the fence off the write lands in the same place, so the
fence — not a missing directory — is what decided nothing. This is the one fence
that is supposed to hold under `--edit-scope --strict-scope`, where the shell is
forbidden; a cloned repository (or the model's own `ln -s`) is enough to plant the
link.

## 4. The secrets seam: the registry was armed after the children forked (M608)

**Claim.** M130: the configured key variable is scrubbed from every child's
environment, and a built-in list drops the well-known provider names even when
unconfigured, "so a stray export in the parent's shell can't leak either."

**World.** Two holes, one shape. (1) The registry (`jc_proc_secret_env_add`) is
armed at `src/main.c:~14570`, *after* the subcommand dispatch chain; `brief-check
--verify CMD` forks the gate at `:14210` — the scrub runs against an empty
registry, and only the built-in list applies. M444 fixed the same ordering for the
envelope's arming block; this is the sibling. (2) The built-in list names thirteen
third-party variables and neither of jichi's own — `JICHI_API_KEY`, which the setup
wizard and the scaffolder write, nor `JLU_API_KEY`, the HRZ onboarding name.

**Measured.** `tests/smoke/secret_env_subcommands.sh`, written before the fix:

```
not ok 1 - brief-check's verifier saw JICHI_API_KEY (count=1) -- forked before arming
not ok 2 - the shell tool's child saw jichi's own key names (count=2, rc=0)
ok 3 - control: the configured key variable is scrubbed from the shell tool's child
```

The control shows the mechanism working where it was armed — the defect is scope,
not mechanism, which is `HARDENING.md` §1's own sentence about M130 coming true a
second time inside the fix it describes.

A related finding of my own making: `tests/bench/mentor_ab/blind.sh` (M605) used
GNU-only `\|` alternation, which `posix_utils_lint` catches — so M605 was pushed
without the gate having run on the tracked harness. Fixed in M606 and recorded
there.

## 5. Memory control at the boundaries (M609 did the LSP framer; the rest is in DEFERRED.md)

The SSE parser caps a field at 4 MB (`JC_SSE_FIELD_MAX`) and says so. Three other
peers do not:

| Peer | Buffer | Bound |
|---|---|---|
| MCP stdio server | `rbuf` (`src/mcp/jc_mcp_stdio.c:240`) | a 120 s deadline — time, not bytes |
| ACP editor | `inbuf` (`src/acp/jc_acp.c:159,185`) | none; no deadline either |
| LSP server | header block (`src/lsp/jc_lsp_proto.c:35`) | the *body* is capped at 64 MB; a header that never ends is not |

A peer that writes without a newline grows the buffer until the kernel kills the
process (`exit 137`), which the ROADMAP shows being misattributed once already.
Beside them, `find_content_length` (`jc_lsp_proto.c:64`) accumulates `v*10+d` in
a `jc_size` with no bound — unsigned wrap, not memory-unsafe, but a wrapped length
desyncs the stream permanently — and both the MCP and the LSP shutdown do
`kill(SIGTERM); waitpid(pid, …, 0)`, a blocking wait that a TERM-trapping server
turns into a hang at exit, while `jc_worker_reap_grace` exists for exactly this and
is used by the parallel pool and the daemon.

## 6. The lint's universe (M610)

`arena_lint.sh` enforces the three-arena discipline over `src/tools`, `src/lsp`,
`src/chat`, `src/provider`, `src/net`, `src/session`, `src/index` and one file of
`src/tui` — 75 files, floored at 50. Thirteen `src/` directories are outside it;
`src/util/jc_calib.c` allocates into the session arena per calibration update and
`src/util/jc_learn.c` re-loads skills onto it per `learn apply` (both bounded today,
neither watched). The allowlist is keyed by stripped text, so two byte-identical
lines in one file share an entry — `jc_app.c:147` and `:199` do. The lint matches
`app->arena` only, so `jc_tool_parallel.c:309` reading each merged file onto the
per-*turn* arena (correct arena: per tool call) can never be flagged. `CLAUDE.md`'s
own rules — floor at today's exact count, audit the universe a second way — apply
to this lint as to any other. Separately, `jc_workflow_parse` drops stage 17,
item 65 and any unknown stage type with a bare `continue` and no count
(`src/util/jc_workflow.c:69,90`): a 70-item fan-out runs 64 and reports success,
which is the "no silent caps" rule violated in the structure that *represents the
task*.

## 7. Recorded, not built

- **The envelope is not resumable.** No `jc_env_save`/`load` exists; `tokens_used`,
  `tool_calls`, `green_commit`, `wrote` die with the process, and a `--continue`
  of a killed `--auto` run starts its budget from zero against a half-edited tree.
  Whether that should be persistence or an explicit refusal is a design decision
  for the operator; `DEFERRED.md` gets the row.
- **Implicit state machines on `jc_app`.** `last_run_capped`,
  `last_run_budget_stopped`, `last_fail_*`, `turn_capped`, `last_response_truncated`
  share a "read right after the run returns" contract kept by comments and by
  save/restore around every nested `jc_tool_execute`; M437 records the class
  already producing a cross-depth misattribution. A returned outcome struct is the
  shape; it is a refactor, not a seam, and it goes to `DEFERRED.md`.
- **Fault injection reaches block allocation only.** `JC_FAULT_ALLOC` sits in
  `block_new`; `jc_strdup`, `jc_sb_reserve` and `jc_vec` growth — which own the
  whole history and every tool result — have no OOM coverage. `DEFERRED.md`.
- **`app->read_files` is uncapped** but deduplicated, so bounded by distinct paths;
  and reset on resume, so the first edit of each believed file after `--continue`
  costs one refused round-trip. Noted; not worth a milestone yet.

## 8. Status

| Seam | Milestone | Born-red evidence | State |
|---|---|---|---|
| task list lost across resume / kept across session switch | M606 | `todo_resume.sh` 3/4 red on M605 | **done** |
| dangling-symlink write escapes the fence | M607 | `pathfence_dangling.sh` 2/4 red | **done** |
| secret registry armed after subcommands; own key names not built in | M608 | `secret_env_subcommands.sh` 2/3 red | **done** |
| LSP framer header cap + Content-Length overflow | M609 | `test_framer` 2 cases red | **done** |
| MCP/ACP line buffers, blocking shutdown | — | needs a memory-pressure harness | **DEFERRED.md** |
| arena_lint universe/floor/key; workflow silent caps; parallel per-turn arena | M610 | scaffold-inject teeth; workflow drop counts red | **done** |

## 9. Limits

Two pairs of eyes did not read this. The surveys were mine (through three
sub-agents I prompted), the reproductions were mine, and the fixes are mine; the
gate is `make ci`, which does not read prose. The telemetry count in §2 is from
one machine's three logs. §5's exploitability depends on a peer's write rate, which
I did not measure; the buffers are unbounded by construction, and that is the
claim.

## 10. Dream, daydream, and RAG (added 2026-08-27, at the operator's request)

A later question in the same session: find the seams in `dream` and `daydream`,
recommend mends, and answer whether either could be used for RAG integration.

### 10.1 Dream (M102) — three seams, mended in M611

`dream` (`run_dream`, `src/main.c`) is the `learn analyze` reflection, made
autonomous and written to a dated propose-only draft under `~/.jichi.d/dreams/`;
the daemon runs it on idle (`--idle-dream`). The seams, all the same shape as
§1's — a structure claiming more than it delivered:

1. **Silent clobber.** `dream-<epoch-seconds>.md` + a truncating write meant two
   dreams in one second (manual racing the daemon, or a loop) overwrote a draft
   whose whole point is preservation. → collision-proof filename (M611 S1).
2. **No retention.** `prune` scoped sessions only; the daemon grew the directory
   unboundedly. → `prune` scopes dreams by the same selectors (M611 S2).
3. **Identical-dream spam.** No embedded timestamp, so an idle daemon over
   unchanging telemetry wrote a byte-identical dated duplicate each stretch. →
   a dream records a delta or nothing (M611 S3).

The fourth — **dream is inert and duplicates `learn analyze`** (its artifact is
read by no tool; it points a human at `/learn`, which re-derives the same analysis
from the same telemetry) — is left as a **design decision**, not a patch: making
the dream the thing `/learn` consumes changes what the mentor reads and is the
operator's call.

### 10.2 Daydream (M107) — stays parked, and I agree

`daydream` is not code; it is a postponed design (`SELF_IMPROVEMENT.md:468`): spend
idle/latency time on cheap, read-only, cancellable speculation. The doc's own
recommendation — skip unless a concrete need appears; if ever built, the non-model
slice only (idle `testCommand` pre-run / retrieval pre-warm), never speculative
model calls — still holds, and is stronger now: on the cacheless free-`jlu/*`
backend a speculative model call is pure spend at a low hit rate. The one honest
asymmetry: idle reflection exists **server-side** (the daemon's `--idle-dream`,
running the *heavy* dream) but not **client-side** (the interactive TUI, which is
what M107 was about), and never as the *cheap latency-hiding* speculation the
design proposed. Naming it is worth more than building it.

### 10.3 Could dream / daydream feed RAG?

Grounded in the retrieval stack (`jc_index_build` → `~/.jichi.d/index/<key>/`,
incremental by mtime, cancellable; query embeds once via the `embed` role;
**no lexical fallback** — without an `embed` endpoint the whole path is dark):

- **Dream cannot itself do RAG.** Its defining invariant is offline / model-free /
  network-free (`main.c`, the daemon path is "no model call"); embedding requires
  an `/embeddings` endpoint. Wiring retrieval into `dream` breaks the one property
  that makes it safe to run unattended. So: no.
- **Daydream is the right host, for the non-model half only.** The postponed
  design *already names* "pre-warm the retrieval index for the last turn's files"
  as its plug point, and `jc_index_build` is already incremental **and**
  cancellable (`abort` param) — a near-perfect fit for an idle, keypress-abortable
  pre-warm. But it is **not free**: incremental indexing of changed files still
  spends `embed`-role calls, and degrades to a no-op with no `embed` endpoint.
  So the honest slice is: *opt-in, idle-time `jc_index_build` refresh in a
  cancellable child, only when an `embed` role is configured, never a speculative
  chat/query call* — and measured against the retrieval latency it actually hides
  before shipping.
- **Indexing the `dreams/` corpus** as a docs source (`jc_docs_run` over the same
  retrieval core) is *possible* but low-value: dreams are a handful of short
  reflections a human reviews directly; they are not a corpus that needs semantic
  search, and indexing them would spend embeddings on text `/learn` already reads.

**Postscript (M612).** Mapping the retrieval stack for this answer surfaced an
adjacent seam of the same shape as §2/the dreams seam: the index cache
(`~/.jichi.d/index/<key>/`) is keyed by workspace root and had **no retention** --
one directory per distinct workspace, forever. `prune` now sweeps it by the same
`--keep`/`--older-than` selectors it applies to sessions and dreams, protecting
the current workspace's own index (an index is a rebuildable cache, so
over-pruning costs a re-embed, never data). `tests/smoke/prune_index.sh`, born
red. The RAG conclusion is unchanged; the growth path it noted is closed.

**Net:** the RAG opportunity is not in dream (which must stay offline) but in
reviving daydream's non-model pre-warm slice — and only if a measured latency
justifies it. Recorded, not built; daydream stays postponed.
