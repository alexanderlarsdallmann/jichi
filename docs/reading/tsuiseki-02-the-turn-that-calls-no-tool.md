# Tsuiseki 2 — The turn that calls no tool

*[追跡（ついせき）*Tsuiseki* — the traced run](TSUISEKI.md) · chapter 2 of 4*

## Why this exists

[Chapter 1](tsuiseki-01-a-tool-round.md) traced a run with two tool calls in
it and could not tell you which parts of the machinery were *the tool round*
and which were *every turn*. This chapter is the control: the same binary, the
same flags, the same workspace, the same 18 tools advertised — and an answer
with no tool call in it.

Read it as a subtraction. Everything chapter 1 showed that is missing here was
the cost of asking; everything still present is what a turn costs before the
model has decided anything at all. The second half is the surprising one.

## The run

```sh
# in the jichi checkout
sh docs/reading/traces/capture.sh plain-turn
```

The fixture is one rule, because the run is one round trip:

```mm docs/reading/traces/plain-turn/replies.mm
rule
  count 1
  text A translation unit is one source file after preprocessing, with every #include pulled in and every macro expanded -- the thing the compiler actually compiles.
```

The question is about C, not about the workspace, so nothing needs opening for
it to be answerable: `in one sentence, what is a translation unit in C89?`

The interesting line in `docs/reading/traces/plain-turn/trace.sh` is the
bound:

```sh docs/reading/traces/plain-turn/trace.sh
# One round trip, and request 2 finds nothing listening. That bound is an
# assertion: this run must not need a second call.
MAX_REQUESTS=1
```

`MAX_REQUESTS` is not a convenience. The mock serves exactly that many
requests and then exits, so a second round trip does not get a polite refusal
— it gets a closed port, and the run fails. The number is the chapter's claim
about control flow, enforced.

## What came out: four lines

```jsonl docs/reading/traces/plain-turn/expected/stdout.jsonl
{"v":1,"type":"message_start","model":"mock","mode":"auto"}
{"v":1,"type":"text","delta":"A translation unit is one source file after preprocessing, with every #include pulled in and every macro expanded -- the thing the compiler actually compiles."}
{"v":1,"type":"usage","input":20,"output":5,"cost":0}
```

Chapter 1's stream was twelve lines; this one is four. Three of them are above;
the fourth is the summary, and the fields worth reading in it are the zeros:

```jsonl docs/reading/traces/plain-turn/expected/stdout.jsonl
{"v":1,"type":"done","text":"A translation unit is one source file after preprocessing, with every #include pulled in and every macro expanded -- the thing the compiler actually compiles.","model":"mock","tokens":{"input":20,"output":5},"cost":0,"tool_calls":0,"aborted":false,"stop_reason":"done","work_kept":true,"starved":false,"peak_input":20,"cache":{"read":0,"write":0},"tools":{"read":0,"write":0,"shell":0,"other":0}}
```

`"tool_calls":0` and `"tools":{"read":0,"write":0,"shell":0,"other":0}`. No
`tool_call` event, no `tool_result` event, and — the part you cannot see —
**no second `message_start`**. One `message_start` per round trip is the
cheapest way to count iterations in a stream you are handed by someone else.

**`text` arrives as a `delta`.** One here, because the mock puts the whole
sentence in a single SSE chunk. A real model sends dozens, and the field is
named `delta` for that reason: the answer is assembled by the reader, not
delivered whole. Chapter 1's `done` event carries the joined `text` for
exactly this reason — a consumer that only wants the final answer should not
have to concatenate.

## What went in: 14,498 bytes for a 51-byte question

```text docs/reading/traces/plain-turn/expected/shape
req.1 body=14498 messages=2 tools=18
  [0] system    content=2594
  [1] user      content=51
```

One request, two messages. The question is **51 bytes**. The request is
**14,498**. The difference is not overhead in the usual hand-waving sense —
it is one thing, and you can weigh it:

```sh
# in the jichi checkout
awk 'b { print; next } /^\r?$/ { b = 1 }' \
    docs/reading/traces/plain-turn/expected/req.1 > /tmp/body.json
tests/tools/jsonq .tools /tmp/body.json | wc -c
```

**11,599 bytes of tool schemas** — eighty per cent of the request — describing
eighteen tools, for a turn that used none of them. The menu is sent whether or
not anyone orders from it, because the model cannot ask for a tool it has not
been told about, and there is no round trip to spare for asking what is
available.

That is the whole argument for `--tool-profile core` and for the `auto` profile
on small-context models: not that the tools are expensive to *run*, but that
they are expensive to *offer*. `shape`'s `tools=18` is the field to watch, and
[Fukabori 5](fukabori-05-context-economics.md) argues the economics with real
workloads. `docs/TOOL_OUTPUT_COST.md` measures the other half — what tool
*results* cost once they are in the history, which is chapter 1's `growth`
lines.

## The loop that ran once

Chapter 1's loop head and exit are the same two lines here; what is worth
opening in this chapter is the thing *inside* one iteration, because with a
single iteration there is nothing else in the way.
`src/chat/jc_agent.c:stream_once` is one round trip: build the request, put it
on the wire, feed the response to the SSE parser, accumulate.

```c src/chat/jc_agent.c:stream_once
        st = prov->vt->build_request(prov, hist, system_msg, tools, 1, &body);
```

```c src/chat/jc_agent.c:stream_once
        st = jc_http_stream(&req, &http_status, bridge_chunk, &bridge);
        latency = jc_now_millis() - t0;
        /* body is now owned and freed by jc_http_stream; do not touch it. */
```

Two things to take from those three lines. **`prov->vt->build_request` is the
only place the provider's dialect enters**: the loop hands over history, system
message and tools, and gets back bytes it does not inspect. That is the vtable
[Fukabori 2](fukabori-02-the-provider-abstraction.md) defends, and it is why
`req.1` in this directory is OpenAI-shaped without the loop containing the word
"openai".

And **the body's ownership moves**. `jc_http_stream` uploads it through a
read-callback and frees it the moment it is fully sent, so it is not held in
memory while the response streams back — which is also why the request is
rebuilt per retry attempt rather than kept. A 14 KB body is nothing; the same
loop carries requests hundreds of times that size after a few tool rounds, and
the comment exists because the alternative had been measured.

## The disk: nothing happened

```text docs/reading/traces/plain-turn/expected/workspace.after
== notes.txt ==
buy milk
feed the cat
```

That is byte-identical to what `trace.sh` seeded, and it is the same file
chapter 1's run edited. A turn that calls no tool cannot touch your disk —
not by policy, but because the only path from a model's output to a file is a
tool call, and there wasn't one.

## What is the system here, and what is the instrument

- **One `text` delta** is the mock's doing; a real model produces many.
  Anything you conclude from this trace about *chunking* is a conclusion about
  `tests/tools/mockmodel.c`.
- **`"input":20,"output":5`** are the fixture's constants again. The `done`
  event's `tokens` are the same two numbers here because there was one call to
  sum.
- **`MAX_REQUESTS=1`** is a property of the trace, not of jichi: a real turn
  may loop up to `max_iters` times. What the bound proves is that *this* run
  needed one.
- **Something happened that produced no event.** Before accepting a text-only
  answer as final, the loop scans it for a tool call written as prose (the next
  section). The scan ran here and found nothing, and nothing in the artifacts
  records that it ran. Absence of an event is not absence of work — a trace
  shows you what was emitted, not what executed.

## Where this bit us

The shape of this trace — one round trip, text, no tool call — is also the
shape of a particular failure, and telling them apart is impossible from the
event stream alone.

A model that *describes* calling a tool instead of calling it ("I'll now read
notes.txt with read_file...") produces text and no tool call. The loop sees
`ncalls == 0` and, if nothing else happened, would hand that prose to the user
as a finished answer — a run that did nothing, reported as a run that
finished. Small local models do this often enough that jichi keeps a latch for
it:

```c src/chat/jc_agent.c:run_agent_loop
                    jc_sb_append_fmt(&msg,
                        "You described calling `%s` but did not invoke it. "
                        "Emit the tool call natively -- do not write it as "
                        "text. Do not repeat your previous answer.", tname);
```

One corrective nudge per turn, top level only, and only when the name the
model narrated actually resolves in the live registry — a genuine final answer
that happens to mention `read_file` in passing must not be nudged. The
precision matters more than the recovery: a nudge that fires on a correct
answer teaches the model to distrust its own output.

Two artifacts tell you which case you are in. `tools={read:0,...}` on the
`done` event says nothing ran; the *workspace* says whether anything changed.
Chapter 4 is what happens when those two disagree with the answer.

## Prove it to yourself

**1. Re-take it and diff.**

```sh
# in the jichi checkout
sh docs/reading/traces/capture.sh plain-turn /tmp/pt
diff -r docs/reading/traces/plain-turn/expected /tmp/pt && echo identical
```

**2. Weigh the menu you did not order from.** Copy the trace, add
`--tool-profile core` to its `run_trace`, re-take, and compare `shape`:

```sh
# in the jichi checkout
cp -r docs/reading/traces/plain-turn /tmp/lean && rm -rf /tmp/lean/expected
sed 's|--output jsonl|--tool-profile core --output jsonl|' /tmp/lean/trace.sh \
    > /tmp/lean/t.new && mv /tmp/lean/t.new /tmp/lean/trace.sh
sh docs/reading/traces/capture.sh /tmp/lean /tmp/lean-out
head -3 /tmp/lean-out/shape
```

Read the `tools=` and `body=` fields against the committed ones. On the machine
that wrote this chapter the lean run reported `req.1 body=7345 messages=2
tools=7` — **half the request**, for a turn that needed none of either set. The
question to hold: which of the eleven tools that vanished would this turn have
missed?

**3. Make it want a second round trip.** In another copy, change the fixture's
single `text` rule into a `tool read_file {"path":"notes.txt"}` rule and
re-take. `capture.sh` still writes a full artifact set — it captured what
happened — and what happened is worth reading. The tool ran, the loop came
back for round two, and `MAX_REQUESTS=1` meant nobody was listening:

```
{"v":1,"type":"done","text":"","model":"mock","tokens":{"input":20,"output":5},"cost":0,"tool_calls":1,"aborted":false,"stop_reason":"error","work_kept":true,"error":{"code":4,"type":"error","message":"http error"},"starved":false,"peak_input":20,"cache":{"read":0,"write":0},"tools":{"read":1,"write":0,"shell":0,"other":0}}
```

`exit_status` is **1**, `stop_reason` is `"error"`, the text is empty — and
`"work_kept":true` says the run kept what it had already done rather than
throwing it away for failing to finish. Note also which fields *stayed*
honest: `tool_calls:1` and `read:1` really did happen. Raise the bound to 2,
add a `text` rule for the second request, and you have rebuilt half of
chapter 1.

**4. The reading exercise.** `stream_once` retries. Find the loop, the status
codes it treats as transient, and the backoff; then find why the request body
is rebuilt inside that loop rather than once outside it. The answer is three
lines above the `build_request` call quoted here, and it is about ownership,
not about correctness of the request.

## Where to go next

- [Chapter 3](tsuiseki-03-the-run-that-never-reaches-the-network.md): a run
  with no model in it at all — most of the binary is not this loop.
- [Chapter 1](tsuiseki-01-a-tool-round.md) again, now that you know what the
  floor costs: every number in its `shape` is this chapter's plus the asking.
- [Annai 6](annai-06-the-wire.md) for what `jc_http_stream` and the SSE parser
  are doing under `stream_once`, from zero.
