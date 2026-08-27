# Keep the remainder: managed tool output instead of discarded truncation

*jichi's five output caps (`readMaxBytes`, `runMaxBytes`, `searchMaxBytes`, `gitMaxBytes`,
`fetchMaxBytes`) all do the same thing when they fire: append `... [output truncated]` and
**throw the rest away**. This proposes keeping it, in a file outside the workspace, and telling
the model where it is. Taken from `opencode`'s Managed Tool Output File
(docs/analysis/2026-08-09-opencode-continue-comparison.md §5.1), with its open question
answered.*

---

## 1. The open question, and the answer

The comparison recorded a genuine objection to importing this:

> does this help or hurt on a backend with no prompt caching? A path the model then reads is a
> second round trip; a truncated output the model re-runs is also a second round trip. Which is
> cheaper is measurable and unmeasured.

**The round trips are symmetric, so that is not the argument.** Truncated-then-re-run and
managed-then-read both cost one extra tool call and re-bill the history. The reason to do it
anyway is different, and stronger:

1. **Re-running is not always equivalent.** A build log, a test run, a `git log` over a moving
   branch: re-running produces *different* output, and for an expensive command (a 4-minute
   `zig build test`) it produces it slowly. The truncated tail of the run you actually did is
   not recoverable at any price.
2. **A file is addressable in slices; a command is not.** With the remainder on disk the model
   can `search_code` it for the failing assertion or read its last 40 lines. Re-running to see
   the tail means paying for the whole output again — the exact pathology
   `docs/TOOL_OUTPUT_COST.md` measures.
3. **It converts an unbounded cost into a bounded one.** The cap stays; what changes is that
   exceeding it is no longer lossy.

So: adopt it, and **not** on the grounds of saving round trips. Stated here because a feature
justified by the wrong mechanism gets measured against the wrong number later.

## 2. Decisions

**D1 — Outside the workspace.** `~/.jichi.d/tool-output/<session>/<n>.txt`. ANECDOTES #1 is a
log file kept inside the workspace that a rollback deleted; the same mistake here would have
the envelope's rollback destroy the evidence of the run it rolled back. This also keeps the
files out of `git status`, the repo map, and the index.

**D2 — Eviction designed before the feature, not after.** M338's finding was that
preserve-before-destroy shipped a store nothing could empty. This one gets: the session's
directory removed at teardown, plus an age sweep at startup for directories left by crashed
sessions. **A store with no eviction is not shipped again.**

**D3 — A write failure must not turn a successful tool into a failed one.** If the file cannot
be written, the result degrades to exactly today's behaviour — bounded output plus the
truncation marker — and the operator gets the diagnostic on stderr. The model is never told
about a path that does not exist. (opencode states this property explicitly and it is right.)

**D4 — The path fence must permit reading it, narrowly.** `~/.jichi.d/tool-output/` is outside
`app->root`, so `jc_app_read_file` would refuse it. Implemented by adding that one directory to
the read-side fence — the same mechanism as M54's `referenceRoots`, not a new exemption and not
`pathFence: false`. Reads only; nothing may write there but jichi.

**D5 — Head *and* tail in the preview.** A truncated build log keeps the compiler invocation and
loses the error; a tail-only preview loses the command. The preview is head + tail with a
marker naming the byte count and the path, which is also what makes the file *discoverable*
without a separate instruction.

**D6 — Shell paths first, not `read_file`.** `read_file` already takes `offset`/`limit`, so the
model has a cheap way to reach the rest of a file and the marginal value is small. The value is
concentrated where re-running is expensive and non-reproducible: `run_terminal_command`,
`run_tests`. Start there, measure, extend if it pays.

**Rejected: a `read_tool_output` tool.** It would need its own name in the tool array (~40
tokens on every call, on a backend with no caching) to do what `read_file` already does. The
path is a path; ordinary tools should read it. This is also opencode's own assertion — *reading
a managed output file is permitted without granting external-directory access* — and their
permission test asserts it.

**Rejected: keeping the remainder in history behind a summary.** That is compaction's job
(M76 already elides old tool output mid-turn). Putting bytes in the history to avoid a round
trip is the opposite of the measured lever.

## 3. What this does not fix

- It does not reduce the cost of the output the model *does* read. The caps do that.
- It does not help a model that never looks at the path. Prose in a tool result is advice
  (ANECDOTES #41); the file is there whether it is read or not, and the operator can read it
  even when the model does not.
- It does not make a nondeterministic command reproducible — it preserves *one* run's output,
  which is the point, but it is not a recording of the command.

## 4. Status

**Shipped as M339 for the shell paths (D6).** `src/util/jc_toolout.c`:
`jc_toolout_spill` writes the full capture to `~/.jichi.d/tool-output/<pid>/<tag>-<n>.txt`
(0600, via the atomic writer) and appends head + a marker naming the byte count and path +
tail. `run_terminal_command` now captures to `JC_TOOLOUT_SPILL_MAX` (1 MB) and displays
`runMaxBytes`, so the remainder exists to be kept. The path fence permits reads of that one
directory on the read side only (D4). `jc_toolout_cleanup` removes the session's directory at
teardown and sweeps directories older than 3 days left by crashed sessions (D2).

Proven two-sided: with `jc_write_file_atomic` forced to fail, 6 checks fail and **the tool
still returns its output** — which is D3, the property that a storage failure must not turn a
working tool into a failed one. Under the cap the result is byte-identical to pre-M339.

Not yet done: `search_code`, `git_*` and `fetch_url` still discard (D6 says measure first), and
nothing has yet measured whether models actually read the path. That measurement is the next
step and it needs driven runs, not a unit test.
