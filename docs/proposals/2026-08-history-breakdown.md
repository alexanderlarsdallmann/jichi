# Breaking down the history (M315)

*Design note written before implementation, per the M299 craft rule.*

---

## The last unexplained line

`/context` has three lines. Two now explain themselves:

```
  system prompt     ~15196     <- M312: sixteen named sections, summing exactly
  tool definitions   ~3095     <- M313/M314: per-tool sizes, and calls
  history           ~12000  (46 messages)   <- one number
```

The history is the part that *grows*, the part compaction exists for, and — on a long
`--auto` run — the part that dominates. "46 messages" says nothing about which of them.

## What the user actually wants to know

Not "how many messages". The three answerable questions, in order of usefulness:

1. **Which tool's output is filling the window?** A `search_code` with no `context` limit,
   a `read_file` of a 4,000-line file, a `run_terminal_command` that dumped a build log —
   these are the recurring causes, and each has a fix the user controls.
2. **How is it split between what I said, what the model said, and tool output?** A history
   that is 90% tool results is a different problem from one that is 90% assistant prose.
3. **Is one enormous message the whole story?** Often it is, and then the fix is that one
   message, not a policy.

So: a role summary, a per-tool aggregate, and the largest single messages.

## Surface: `context history [<id|prefix>]`

The `context` subcommand has **no live conversation** — that is why its history line reads
0. So a per-message breakdown there must read a **saved session**, defaulting to the most
recent for this workspace and resolving an explicit id/prefix, exactly as `export` and
`--continue` do.

That is not a workaround, it is the better surface: the question "what filled my window on
that run?" is asked *after* the run, often by someone reviewing an unattended one, and a
saved session is the artifact that survives it. Sessions are saved after every turn, so the
lag on a live session is one turn.

Rejected: **a TUI `/context history`.** The TUI's `/context` takes no arguments today, and
adding sub-arguments to a slash command is a separate (small) piece of work whose value is
mostly covered by the above. Deferred deliberately, and recorded — see
[`DEFERRED.md`](../DEFERRED.md).

Rejected: **more lines in the default `/context`.** Same reasoning as M313: the report is
consulted for orientation, and per-message detail belongs in a view you ask for. The
consistency now has a rule — *a line stays a line when the detail is a list whose length
grows with the workload*, which is true of tools and messages and false of the sixteen
fixed prompt sections.

## Shape

```
History: session 4f3a2b1c, 46 messages, ~12000 tokens

  by role
    tool results     ~9100   (76%, 21 messages)
    assistant         ~2100  (17%, 18 messages)
    user               ~800   (7%,  7 messages)

  tool output, largest first
    search_code       ~5200   (43%, 6 calls)
    read_file         ~2600   (21%, 9 calls)
    run_terminal_c…    ~900    (7%, 4 calls)
    ...

  largest single messages
    ~3100  tool result  search_code   (message 22)
    ~1400  tool result  read_file     (message 14)
    ~900   assistant                  (message 31)
```

Four choices:

1. **The three blocks sum to the reported total**, per role, exactly — the M312 invariant,
   with a unit test. The per-tool block is a *subset* (tool results only) and says so with
   its own percentage base, because a percentage that silently changes denominator between
   blocks is how a report becomes untrustworthy.
2. **Per-message sizing uses compaction's own `msg_tokens`**, exposed as
   `jc_compact_estimate_message`. Currently it is `static`, so any breakdown written
   without exposing it would be a *second* definition of message size — which is the drift
   M311, M312 and M313 each had to undo. One definition, or the sum will not match the
   line it explains.
3. **Tool results are attributed by name**, resolved through `tool_call_id` against the
   assistant messages that requested them. A result whose call is missing from the history
   (compaction dropped the prefix, or the session predates the id) is charged to
   `(unknown)` rather than dropped — a visible bucket, not a silent shortfall.
4. **Message indices are printed**, so "message 22" can be found in
   `export --output json`. A size with no way to locate the thing is a complaint, not a
   diagnosis.

## What it will not do

- **No advice.** Same rule as M314: the report says `search_code` is 43% of the history.
  It does not tell you to narrow your searches. (The `search_code` tool already takes a
  `context` argument; `doctor` is where advice lives.)
- **No prediction of what compaction would do.** Mid-turn compaction (M76) elides the
  oldest large tool result and the trigger is a live-state decision; a static "this would
  be elided next" would be wrong as soon as the next turn changed the estimate.
- **No content.** Sizes and names only. `export` prints transcripts, and pointing at it is
  cheaper than a second, worse transcript renderer.
