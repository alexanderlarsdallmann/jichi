# Tsuiseki 4 — The call that was wrong, and the answer that lied

*[追跡（ついせき）*Tsuiseki* — the traced run](TSUISEKI.md) · chapter 4 of 4*

## Why this exists

This trace is [chapter 1](tsuiseki-01-a-tool-round.md)'s twin. Same prompt,
same workspace, same flags, same three round trips. One field differs, in the
model's second call, and from that one field follows: a tool that refuses, a
loop that carries on, a file that never changes, an exit status of 0, an empty
`stderr`, and a final sentence announcing success.

It is not a hypothetical. It is the first capture taken while writing chapter
1: the fixture was wrong, the run reported success anyway, and the only place
the mistake was visible was in the artifacts. That accident is why this series
records runs instead of describing them — so the run that taught the lesson
gets the last chapter.

Hold this question: **which artifact would have told you?**

## The one difference

Chapter 1's fixture:

```mm docs/reading/traces/tool-round/replies.mm
  tool edit_file {"path":"notes.txt","old_string":"buy milk","new_string":"buy oat milk"}
```

This chapter's:

```mm docs/reading/traces/wrong-args/replies.mm
  tool edit_file {"path":"notes.txt","old":"milk","new":"oat milk"}
```

`old` and `new` against `old_string` and `new_string`. Nothing else in the two
traces differs — deliberately, including the `read_file` call in round 1, which
is there so the read-before-edit guard is satisfied and this run has exactly
one fault in it.

```sh
# in the jichi checkout
sh docs/reading/traces/capture.sh wrong-args
```

## What the run did

Round 2's two events, the call and its result:

```jsonl docs/reading/traces/wrong-args/expected/stdout.jsonl
{"v":1,"type":"tool_call","name":"edit_file","args":"{\"path\":\"notes.txt\",\"old\":\"milk\",\"new\":\"oat milk\"}","id":"c1"}
{"v":1,"type":"tool_result","name":"edit_file","is_error":true,"id":"c1","preview":"error: 'path', 'old_string', and 'new_string' are required"}
```

`is_error` is `true` and the tool named exactly what it wanted. Then round 3,
which is the whole reason this chapter exists:

```jsonl docs/reading/traces/wrong-args/expected/stdout.jsonl
{"v":1,"type":"text","delta":"Done -- notes.txt now says oat milk."}
```

And the file:

```text docs/reading/traces/wrong-args/expected/workspace.after
== notes.txt ==
buy milk
feed the cat
```

`notes.txt` still says `buy milk`. `exit_status` is `0`. `stderr.txt` is empty.

## The finding: the two runs' summaries are the same

Every jichi run ends with one `done` event, and a supervisor reading a fleet of
runs will read that event and nothing else. Here is chapter 1's, from the run
that changed the file:

```jsonl docs/reading/traces/tool-round/expected/stdout.jsonl
{"v":1,"type":"done","text":"Changed 'buy milk' to 'buy oat milk' in notes.txt.","model":"mock","tokens":{"input":60,"output":15},"cost":0,"tool_calls":2,"aborted":false,"stop_reason":"done","work_kept":true,"starved":false,"peak_input":20,"cache":{"read":0,"write":0},"tools":{"read":1,"write":1,"shell":0,"other":0}}
```

And here is this run's, from the run that changed nothing:

```jsonl docs/reading/traces/wrong-args/expected/stdout.jsonl
{"v":1,"type":"done","text":"Done -- notes.txt now says oat milk.","model":"mock","tokens":{"input":60,"output":15},"cost":0,"tool_calls":2,"aborted":false,"stop_reason":"done","work_kept":true,"starved":false,"peak_input":20,"cache":{"read":0,"write":0},"tools":{"read":1,"write":1,"shell":0,"other":0}}
```

Diff them yourself:

```sh
# in the jichi checkout
diff <(tail -1 docs/reading/traces/tool-round/expected/stdout.jsonl) \
     <(tail -1 docs/reading/traces/wrong-args/expected/stdout.jsonl)
```

**Only the `text` field differs.** `stop_reason` is `done` in both.
`tool_calls` is 2 in both. `work_kept` is true in both. `aborted` is false in
both. The token counts and the cost agree. And `"tools":{"read":1,"write":1}`
is identical — because that counter counts a call in the **write category**,
which is what was attempted, not what succeeded.

So every machine-readable field of the summary agrees between a run that did
the work and a run that did nothing. Across the whole twelve-line stream only
four lines differ — the second call's arguments, its result, the closing
sentence, and the summary that repeats that sentence — plus one file on disk. That is the finding, and it
generalizes past this fixture: **a run's summary is a summary of what was
tried.**

## Why the loop did not stop

Open `src/tools/jc_tool_edit.c:edit_run` at the top of its body:

```c src/tools/jc_tool_edit.c:edit_run
    if (path == NULL || old_s == NULL || new_s == NULL) {
        tu_err(out, "error: 'path', 'old_string', and 'new_string' are required");
        return JC_OK;
    }
```

Read the last line twice. The tool returns **`JC_OK`** — success — while
writing an error into its result. That is not sloppiness, it is the contract:
`jc_status` answers *did the tool machinery work?* and the result answers *did
the thing happen?* The machinery worked perfectly. It received arguments it
could not use, said so, and handed the sentence back to the loop, which put it
in the history exactly as chapter 1's diff was put in the history, and asked
the model what to do next.

If it had been the other way round — a non-OK status propagating up — the run
would have ended at round 2 with an error, and no model would ever get the
chance to fix its own call. Which is the trade this design makes: **recoverable
by construction, un-alarming by construction.** You cannot have the first
without paying for the second somewhere, and the place jichi pays is here.

### And the error is not a field on the wire

`is_error` appears in the event stream, so it is tempting to assume the model
receives it. Check:

```sh
# in the jichi checkout
awk 'b { print; next } /^\r?$/ { b = 1 }' \
    docs/reading/traces/wrong-args/expected/req.3 > /tmp/body.json
tests/tools/jsonq .messages[5] /tmp/body.json
```

```
{"role":"tool","tool_call_id":"c1","content":"error: 'path', 'old_string', and 'new_string' are required"}
```

Three fields: role, id, content. **No error flag.** The model's entire
knowledge that the call failed is the word `error:` at the start of a string —
prose, in a channel that carries prose. `is_error` is local: it drives the UI's
colour, the counters, the loop's bookkeeping, the `jsonl` field you read above.

Two consequences worth carrying out of this chapter. First, the careful
wording of jichi's tool errors is not politeness — it is the *only* channel,
which is why chapter 1's ambiguity error names the colliding line numbers and
why this one lists the required arguments. Second, a model that skims will miss
it, and nothing in the protocol prevents that. The behaviour you are looking at
is the protocol working as designed.

`shape` shows what the exchange cost:

```text docs/reading/traces/wrong-args/expected/shape
growth req.1->req.2 = +241 bytes
growth req.2->req.3 = +297 bytes
```

Chapter 1's second growth was +353 bytes, this one is +297: the error is
*cheaper* than the diff it did not produce. A failed round trip costs a whole
round trip and slightly fewer bytes — the budget cannot tell you a run is going
wrong, because going wrong is not more expensive.

## What is the system here, and what is the instrument

Being precise about this matters more here than anywhere else in the guide,
because the temptation is to read a lying model into it.

- **The model did not lie.** There is no model. `replies.mm` decided round 3's
  sentence before round 2 existed, so the "claim of success" is a fixture
  emitting a fixed string, not an inference from a tool result. What the trace
  demonstrates is that **jichi does not check the claim** — that part is
  jichi's behaviour, faithfully recorded.
- **A real model usually recovers.** Given an error that names the required
  arguments, a competent model retries correctly on the next round. This
  fixture cannot, because a fixture cannot learn. Do not read this trace as
  evidence about how often models get schemas wrong; read it as evidence about
  what the *run* looks like when one does and the loop moves on.
- **`is_error:true` in the stream is real**, and it is the field a supervisor
  should be counting. Everything else in the summary is the same as a
  successful run.

## How you would actually catch this

Four checks, cheapest first, all visible in these artifacts:

1. **Count the errors, not the calls.** `grep -c '"is_error":true'` over the
   stream. Two runs with identical `done` events differ here, 1 against 0.
2. **Look at the workspace.** A run that claims an edit and leaves the file
   byte-identical has said something checkable and false.
3. **Ask for proof, not a report.** This is what `verify`/`--strict-green`
   exist for: a run whose own test suite passes has made a claim a machine
   checked. `docs/AUTONOMOUS_LOOPS.md` is the version of this argument for
   unattended runs, where nobody is reading the summary at all.
4. **Then read the answer** — last, and as a hypothesis. The sentence at the
   end of a run is generated from the same context as everything before it,
   which means it is exactly as reliable as the run and not one bit more.

The habit these add up to is the one the whole guide is for: the answer is the
*last* artifact you should trust, because it is the only one nothing checked.
`docs/ANECDOTES.md` is a file of instances, and the pattern in almost all of
them is a plausible summary over an unexamined trace.

## Prove it to yourself

**1. Diff the two traces.** Everything one field changed, in one command:

```sh
# in the jichi checkout
diff -r docs/reading/traces/tool-round/expected \
        docs/reading/traces/wrong-args/expected
```

Four files differ, and the four that do not are the interesting half.
**`req.1` and `req.2` are byte-identical** — up to and including the request
that carries the read result, these are the same run. `exit_status` (0) and
`stderr.txt` (empty) are identical too, which is the chapter's point in two
files. What differs is `req.3` (the assistant's wrong call, and the error that
came back), `shape` (that message is shorter), `stdout.jsonl`, and
`workspace.after`.

Note where the divergence *is not*: the model's mistake is invisible until the
request that reports it, because a tool call is only in the history after the
round that made it.

**2. Make it recover.** Copy the trace, give the model a third round in which
it tries again with the right field names, and watch the file actually change.
Write the whole reply table rather than appending to it — the reason is the
next paragraph:

```sh
# in the jichi checkout
cp -r docs/reading/traces/wrong-args /tmp/fixed && rm -rf /tmp/fixed/expected
sed 's/^MAX_REQUESTS=3$/MAX_REQUESTS=4/' /tmp/fixed/trace.sh > /tmp/fixed/t.new \
    && mv /tmp/fixed/t.new /tmp/fixed/trace.sh
cat > /tmp/fixed/replies.mm <<'MM'
wire openai
rule
  count 1
  tool read_file {"path":"notes.txt"}
rule
  count 2
  tool edit_file {"path":"notes.txt","old":"milk","new":"oat milk"}
rule
  count 3
  tool edit_file {"path":"notes.txt","old_string":"buy milk","new_string":"buy oat milk"}
rule
  count 4
  text Fixed it on the second attempt -- the field names are old_string/new_string.
rule
  status 500
  body {"error":"unexpected request"}
MM
sh docs/reading/traces/capture.sh /tmp/fixed /tmp/fixed-out
cat /tmp/fixed-out/workspace.after
grep -c '"is_error":true' /tmp/fixed-out/stdout.jsonl
```

`notes.txt` now says `buy oat milk`, `tool_calls` is 3, and `is_error:true`
still appears **once**. A recovered run contains its own failure, which is the
correct outcome and the reason counting errors is a signal rather than an
alarm: what you want to know is not whether anything failed, but whether
anything failed *and stayed failed*.

**Why the whole table, and not `>>`?** Appending is the obvious move and it
does not work — first matching rule wins, and the shipped table ends with a
predicate-less `status 500` catch-all, so a rule appended after it never
matches anything. Try it and read the failure: `stop_reason":"error"` with
`"message":"provider error"` at round 3, because the mock answered 500. This
paragraph exists because that is what happened when the exercise was first
written.

**3. Break the guard as well as the schema.** Chapter 1's second exercise
removes the `read_file` round. Do it to *this* fixture and the call has two
faults at once: wrong field names and a file the session never read. Only one
error comes back. Predict which, run it, then find in `edit_run` why that order
is not arbitrary — the answer is about what the second check needs from the
first.

**4. The reading exercise.** `tu_err` is how every tool reports a failure.
Find it, find what it does to `out`, and then find how many distinct error
strings `edit_file` alone can produce. For each one, answer: could a model act
on this message without seeing the file?

## Where to go next

- Back to [chapter 1](tsuiseki-01-a-tool-round.md), whose closing section is
  this run told as a story. It now has a trace behind it.
- [Fukabori 6](fukabori-06-the-autonomy-envelope.md) for what bounds a run that
  is wrong and does not know it, and
  [Fukabori 11](fukabori-11-ai-supported-coding-examined.md) for the honest
  account of building this way.
- `docs/OBSERVABILITY.md` and `docs/AUTONOMOUS_LOOPS.md`: the same question as
  this chapter — *what would have told you?* — asked of a fleet of runs nobody
  is watching.
- The series index: [追跡（ついせき）*Tsuiseki*](TSUISEKI.md). Four traces, four
  shapes of run. The next one is the one you record yourself:
  `docs/reading/traces/README.md` says how.
