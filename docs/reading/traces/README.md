# The recorded runs

Each directory here is **one run of jichi, written down**. The
[追跡（ついせき）*Tsuiseki*](../TSUISEKI.md) chapters quote these artifacts, and
`tests/smoke/reading_trace.sh` re-takes them on every `make smoke` and diffs
the result, so a chapter cannot keep describing behaviour the code has left
behind.

## Layout

```
tool-round/
  trace.sh          the run: the prompt, the starting workspace, the round-trip bound
  replies.mm        the model: a reply table for tests/tools/mockmodel
  expected/         what the run produced, normalized -- the committed record
capture.sh          takes a trace and writes its artifacts (shared by all traces)
```

## Taking a trace

```sh
# in the jichi checkout
make && make smoke-tools
sh docs/reading/traces/capture.sh tool-round          # -> prints a temp dir
sh docs/reading/traces/capture.sh tool-round /tmp/x   # -> writes /tmp/x
```

A trace directory given as a path is used as-is, so you can copy one to
`/tmp`, change the fixture, and run it there without touching the repository:

```sh
# in the jichi checkout
cp -r docs/reading/traces/tool-round /tmp/vary && rm -rf /tmp/vary/expected
sh docs/reading/traces/capture.sh /tmp/vary /tmp/vary-out
```

## What is real here, and what is not

The honest reading of these artifacts, stated once so no chapter has to keep
apologising:

- **Real:** everything jichi did. The request bodies are the bytes a server
  received, the event stream is the run's own `--output jsonl`, the workspace
  is what was on disk when the process exited, and the exit status is the exit
  status.
- **A fixture:** everything the *model* did. `replies.mm` decides the replies
  in advance, so token counts, tool-call ids, and the wording of every answer
  are the mock's, not a model's. `tests/tools/mm_core.h` documents the
  grammar; a chapter that draws a conclusion about model behaviour from one of
  these traces is overreaching, and should be corrected.
- **Substituted:** six values that differ between two honest runs — the
  workspace path, today's date, the binary's version, the mock's port, the
  per-session `prompt_cache_key`, and `Content-Length`. Each becomes a
  `<PLACEHOLDER>`; `capture.sh`'s header explains each one and how the list
  was measured (two captures, two directory names, one diff) rather than
  reasoned about.
- **Derived:** `shape` is generated from the request bodies by `capture.sh`,
  not captured. Its sizes are measured **after** substitution, so they
  reproduce exactly on any machine while being a few bytes off what the socket
  saw. The `growth` lines are exact regardless: the system message is
  byte-identical across one run's requests, so it cancels.

## Re-taking a trace on purpose

When a change to jichi legitimately changes a trace, the diff is the point:
read it, decide it is intended, and only then re-take the record.

```sh
# in the jichi checkout
sh docs/reading/traces/capture.sh tool-round /tmp/new
diff -r docs/reading/traces/tool-round/expected /tmp/new   # read this
cp /tmp/new/* docs/reading/traces/tool-round/expected/     # then this
```

Then re-read the chapter that quotes it — `reading_quotes_lint.sh` will catch
a quoted line that moved, but nothing can catch a paragraph whose argument
the new numbers have quietly undermined.

## Adding a trace

1. `mkdir docs/reading/traces/<name>` with a `trace.sh` (set `PROMPT`,
   `MAX_REQUESTS`, define `seed_workspace`) and a `replies.mm`.
2. Take it twice, into differently-named directories, and diff them. Anything
   that differs is a value `capture.sh` does not yet normalize — add it to the
   substitution list, with a comment naming what varies and why.
3. Commit `expected/`, and add the trace to `tests/smoke/reading_trace.sh` so
   the gate covers it.
4. Write the chapter last. The artifacts are the source; prose written before
   them is a guess.
