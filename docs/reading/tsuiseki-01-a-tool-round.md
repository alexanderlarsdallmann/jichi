# Tsuiseki 1 — A tool round, byte by byte

*[追跡（ついせき）*Tsuiseki* — the traced run](TSUISEKI.md) · chapter 1 of 4*

## Why this exists

One sentence in, two tool calls out, one file changed. [Annai](ANNAI.md)
chapters 4 and 5 explain that mechanism: the model cannot touch your disk, it
can only *ask*, and the program decides. This chapter takes one such run that
actually happened and shows you every value it produced — the event stream,
the bytes on the wire, the file on disk — and then finds the code responsible
for each.

Hold one question the whole way down: **what did the model actually see, and
when?** Almost everything else about an agent — the loop, the guards, the
context budget, the cost — is downstream of the answer.

## The run

```sh
# in the jichi checkout
make && make smoke-tools
sh docs/reading/traces/capture.sh tool-round
```

The last line printed is a directory. Eight files in it:

| Artifact | What it is |
|---|---|
| `stdout.jsonl` | the run's own event stream (`--output jsonl`) |
| `req.1`, `req.2`, `req.3` | every HTTP request jichi sent, head and body, as the server received it |
| `shape` | a generated projection of those requests: how many messages, which roles, what sizes |
| `workspace.after` | the files afterwards |
| `exit_status`, `stderr.txt` | the boring two, committed because "it said nothing and exited 0" is a claim worth holding |

The same eight are committed under
`docs/reading/traces/tool-round/expected/`, and
`tests/smoke/reading_trace.sh` re-takes them on every `make smoke` and diffs.
So the quotes below are not a description of a run; they are the run, and if
jichi's behaviour changes under them, the build says so.

### The model is a table

There is no model in this trace. There is `tests/tools/mockmodel.c` and a
reply table, and this is the whole of what the "model" decided:

```mm docs/reading/traces/tool-round/replies.mm
rule
  count 1
  tool read_file {"path":"notes.txt"}
```

```mm docs/reading/traces/tool-round/replies.mm
rule
  count 2
  tool edit_file {"path":"notes.txt","old_string":"buy milk","new_string":"buy oat milk"}
```

```mm docs/reading/traces/tool-round/replies.mm
rule
  count 3
  text Changed 'buy milk' to 'buy oat milk' in notes.txt.
```

`count N` means *the Nth request of this run*; the full grammar is in
`tests/tools/mm_core.h`. The user's sentence and the starting file are next
door in `docs/reading/traces/tool-round/trace.sh`: the prompt is
`in notes.txt, change 'buy milk' to 'buy oat milk'`, and `notes.txt` starts as
two lines, `buy milk` and `feed the cat`.

**What this buys and what it costs.** It buys determinism: no key, no network,
no tokens, and the same bytes on your machine as in the committed copy — which
is the only reason a trace can be checked by a lint at all. It costs the
model's judgement. Every "decision" you are about to watch was made in that
table before the run started, so nothing here tells you anything about how a
real model behaves. It tells you exactly how **jichi** behaves when a model
answers that way, which is the part of the system you can read.

## Round 1: the sentence goes out, a tool call comes back

The first four lines of the event stream:

```jsonl docs/reading/traces/tool-round/expected/stdout.jsonl
{"v":1,"type":"message_start","model":"mock","mode":"auto"}
{"v":1,"type":"usage","input":20,"output":5,"cost":0}
{"v":1,"type":"tool_call","name":"read_file","args":"{\"path\":\"notes.txt\"}","id":"c1"}
{"v":1,"type":"tool_result","name":"read_file","is_error":false,"id":"c1","preview":"     1\tbuy milk\n     2\tfeed the cat\n"}
```

Four things worth stopping on.

**`"v":1` is a promise.** The stream is a contract with whatever is reading it
— a supervisor script, another program, you — and it is versioned so a reader
can refuse a shape it does not know. `docs/OBSERVABILITY.md` is the
contract's documentation.

**`args` is a string, not an object.** Look closely: the value is
`"{\"path\":\"notes.txt\"}"` — JSON *inside* JSON. That is not sloppiness, it
is the shape the wire uses, and the loop keeps it that way: to the agent loop
a tool's arguments are opaque text that only the tool itself parses. Nothing
between the model and `read_file` needs to understand what a path is.

**The `preview` has a gutter.** `     1\tbuy milk` — line numbers, tab,
content, exactly like `cat -n`. Those numbers are *not in the file*. They are
added by `src/util/jc_lineno.c:jc_format_numbered` so that the model can say
"line 2" and mean something, and the read tool's schema warns the model not to
paste them back into an edit. This is the first place where the trace shows a
value that exists only for the model's benefit.

**`is_error` is a field, not an exception.** Hold that thought; it comes back
below with force.

### Which code emitted these four lines?

Not a logger. The events are the run's own callbacks: the headless front end
fills a `struct jc_agent_callbacks` — for this line,
`src/main.c:hl_tool_result` — and the loop calls them as it goes. That is worth
following, because it answers the question you should ask of any log: *is
that preview what the model got, or a summary someone wrote for the UI?*

```c src/chat/jc_agent.c:run_agent_loop
                    /* Show what the MODEL actually received -- including any
                     * PostToolUse [hook] context or [jichi] redo note -- so the
                     * TUI/jsonl observer sees the same tool result the model
                     * does, not the pre-injection raw output. */
```

It is the same string. One value, two consumers: the conversation and the
observer. That is why a trace is evidence here rather than decoration — and it
is a design decision you can find, argue with, and check, which is more than
most logging gets.

## Round 2: the price of having asked

Now the wire. Reading a 15 KB single-line JSON body is no way to learn
anything, so `capture.sh` also writes a projection of the three requests:

```text docs/reading/traces/tool-round/expected/shape
req.1 body=14496 messages=2 tools=18
  [0] system    content=2594
  [1] user      content=49
req.2 body=14737 messages=4 tools=18
  [0] system    content=2594
  [1] user      content=49
  [2] assistant content=- tool_call=read_file id=c1
  [3] tool      content=36 for=c1
req.3 body=15090 messages=6 tools=18
  [0] system    content=2594
  [1] user      content=49
  [2] assistant content=- tool_call=read_file id=c1
  [3] tool      content=36 for=c1
  [4] assistant content=- tool_call=edit_file id=c1
  [5] tool      content=87 for=c1
growth req.1->req.2 = +241 bytes
growth req.2->req.3 = +353 bytes
```

Read the left column downwards: **2 messages, then 4, then 6**. Nothing is
ever taken away. The model has no memory between requests, so the entire
conversation is re-sent every time — which is the single fact that most of
jichi's design is a response to, and the reason `docs/COMPACTION.md` exists at
all.

Read the rows: `[2] assistant content=-` is an assistant turn with **no text
whatsoever**, only a tool call. The model said nothing; it asked. And `[3]
tool ... for=c1` is the read result, sitting in the conversation as a message
of its own, tied by id to the call that produced it.

Read the bottom two lines. A tool round costs **+241 bytes** of request, then
**+353** — the call plus its result, re-sent for the remainder of the
conversation. The system prompt (`content=2594`, byte-identical in all three)
is re-sent too: three times, for one sentence of user input. Those numbers are
tiny here because the fixture is tiny; the *shape* of them is what
[Fukabori 5](fukabori-05-context-economics.md) argues about with real
workloads.

### The loop, in four quotes

The trace's control flow is one `for` loop in
`src/chat/jc_agent.c:run_agent_loop`. Its head:

```c src/chat/jc_agent.c:run_agent_loop
    for (iter = 0; iter < opts->max_iters; iter++) {
```

**One iteration is one HTTP round trip.** Three iterations happened in this
trace, so `iter` reached 2 — and `max_iters` is why an agent cannot loop
forever, which is a bound you can see rather than trust
([Fukabori 6](fukabori-06-the-autonomy-envelope.md)).

The exit:

```c src/chat/jc_agent.c:run_agent_loop
        if (ncalls == 0) {
```

**A round with no tool calls is the answer.** That is the whole termination
rule. Round 3 of this trace took that branch; rounds 1 and 2 did not.

The work, when there are calls:

```c src/chat/jc_agent.c:run_agent_loop
                    jc_tool_execute(app->tools, name_copy, args_copy, &res, app);
```

And then the thing that makes the next request different from the last:

```c src/chat/jc_agent.c:run_agent_loop
                jc_history_add_tool_result(hist, call_id,
                    have_combined ? combined.data : content, res.is_error);
```

That one line is the `[3] tool content=36 for=c1` row in `shape`. Follow it
into `src/chat/jc_message.c:jc_history_add_tool_result` and the trace's data
flow closes:

```c src/chat/jc_message.c:jc_history_add_tool_result
struct jc_message *jc_history_add_tool_result(struct jc_history *h,
                                              const char *tool_call_id,
                                              const char *content,
                                              int is_error)
{
    struct jc_message *m = jc_history_add(h, JC_ROLE_TOOL, content);
    if (m == NULL) {
        return NULL;
    }
    m->tool_call_id = (tool_call_id != NULL) ? jc_strdup(tool_call_id) : NULL;
    m->is_error = is_error;
    return m;
}
```

A tool's output becomes **a message in the history**, and the history is the
only state the loop has. There is no separate "tool memory", no side channel:
what a tool returns is conversation, priced and re-sent like every other word
in it.

Now grep that function's name across the loop:

```sh
# in the jichi checkout
grep -c jc_history_add_tool_result src/chat/jc_agent.c
```

**Eleven** call sites. One of them is the successful call above. The other ten
are refusals, and reading the list is a fast tour of everything that can stop a
tool: a tool the active profile fences off, a call denied by policy or by an
adopted constraint, a path outside `--edit-scope`, a command disabled by
`--strict-scope`, a call the operator declined at the prompt — or that nobody
was there to approve — and a `PreToolUse` hook's veto. Every one of them
appends a *tool result* rather than returning an error up the stack. This is the
codebase's central claim about tool errors, and the trace is where you can see
it is true: `is_error` is a field on a message, so a refusal is something the
model reads and can act on, not an exception that ends the turn.

## Round 3, and the disk

```jsonl docs/reading/traces/tool-round/expected/stdout.jsonl
{"v":1,"type":"text","delta":"Changed 'buy milk' to 'buy oat milk' in notes.txt."}
{"v":1,"type":"usage","input":20,"output":5,"cost":0}
```

The final round produced text instead of a call, so the loop returned, and the
run ended with a summary event: `"tool_calls":2`, `"stop_reason":"done"`,
`"tools":{"read":1,"write":1,"shell":0,"other":0}`. And on disk:

```text docs/reading/traces/tool-round/expected/workspace.after
== notes.txt ==
buy oat milk
feed the cat
```

Note the order of events, because it is the opposite of how the run *reads*:
the file changed during round 2. By the time the model wrote "Changed 'buy
milk' to…", the change was already several hundred bytes back in its own
context. **The final sentence of an agent run is a report, not an action** —
and, as the last section shows, not necessarily a true one.

## What is the system here, and what is the instrument

A trace is only evidence if you know which of its values came from the thing
you are studying. In this one:

- **The two calls share the id `c1`.** A real provider gives every tool call a
  distinct id; `mockmodel`'s counter restarts with each reply. Pairing calls
  to results by *name* is exactly the bug `tests/smoke/jsonl_tool_id.sh` was
  written for — so do not learn "ids repeat" from this artifact.
- **`"input":20,"output":5` is a constant** the fixture emits (`usage` in
  `tests/tools/mm_core.h`, default `20 5`). The `done` event's
  `"tokens":{"input":60,"output":15}` is three of those added up. Nothing in
  this trace tokenized anything, and `"cost":0` is arithmetic on a zero price,
  not a discount.
- **`<PORT>`, `<DATE>`, `<WORKSPACE>`, `<VERSION>`, `<CACHE_KEY>`, `<LEN>`**
  are substitutions: the six values that legitimately differ between two
  honest runs. `docs/reading/traces/capture.sh` lists them, and says how that
  list was arrived at — by taking the trace twice into differently-named
  directories and diffing, which is how `prompt_cache_key` was found after the
  list had been "finished" at five.
- **The sizes in `shape` are measured on the normalized body**, so they are
  reproducible on your machine but a few bytes off what the socket saw
  (`<WORKSPACE>` is shorter than your `$TMPDIR` path). The two `growth` lines
  are exact either way: the system message cancels.

None of this is pedantry. `docs/analysis/2026-08-16-instruments-that-lie.md`
is an afternoon of this project's life spent believing a number that came from
the measuring apparatus, and the discipline it recommends is the one this
section is: before you conclude anything from a trace, say out loud which
parts of it are the system.

## Prove it to yourself

**1. Re-take it and diff.** The claim is reproducibility; check it.

```sh
# in the jichi checkout
sh docs/reading/traces/capture.sh tool-round /tmp/mytrace
diff -r docs/reading/traces/tool-round/expected /tmp/mytrace && echo identical
```

**2. Watch a guard fire.** Copy the trace out of the repository, delete the
model's first decision — the `read_file` call — so the edit arrives with
nothing read:

```sh
# in the jichi checkout
cp -r docs/reading/traces/tool-round /tmp/vary && rm -rf /tmp/vary/expected
sed 's|tool read_file {"path":"notes.txt"}|tool edit_file {"path":"notes.txt","old_string":"buy milk","new_string":"buy oat milk"}|' \
    /tmp/vary/replies.mm > /tmp/vary/r.new && mv /tmp/vary/r.new /tmp/vary/replies.mm
sh docs/reading/traces/capture.sh /tmp/vary /tmp/vary-out
sed -n 4p /tmp/vary-out/stdout.jsonl
```

That run prints:

```
{"v":1,"type":"tool_result","name":"edit_file","is_error":true,"id":"c1","preview":"error: read the file before editing it"}
```

Then find the guard: it is a set of paths the session has read, in
`src/chat/jc_app.c:jc_app_was_read`, consulted by
`src/tools/jc_tool_edit.c:edit_run` before it touches the file — though not
before *everything*: the required-argument check runs first, because there is
no point asking whether a path was read until you know you were given one.
Chapter 4 is that check firing. An agent editing a file it never looked at is
the classic hallucinated edit.

**3. Make the edit ambiguous.** Take a *fresh* copy (so the `read_file` rule is
back), give `notes.txt` a third line identical to the first — in `trace.sh`,
`printf 'buy milk\nfeed the cat\nbuy milk\n' > notes.txt` — and re-run. Now the
read succeeds and the edit is the thing that cannot proceed, because two lines
match. The refusal names both:

```
{"v":1,"type":"tool_result","name":"edit_file","is_error":true,"id":"c1","preview":"error: old_string is not unique (2 matches)\nhint: it matches at line 1, 3 -- extend old_string with a nearby line that differs between them (or set replace_all to change all of them)"}
```

Read that message as a piece of interface design: it is addressed to a
*reader who cannot see the file*, so it says where the collisions are and what
to do about it. Advice a model can act on has to include the location.

**4. Break the gate on purpose.** Change one byte of an `expected/` artifact
and run `sh tests/smoke/reading_trace.sh`. It should go red and name the file.
A gate you have never seen fail is a gate you are trusting on faith — the
project's own rule (`docs/TEST_INTEGRITY.md`), applied to the machinery under
this chapter.

**5. The reading exercise.** With no help from this page: in
`src/chat/jc_agent.c`, find the branch that decided this run was over, and the
variable that counts what the model asked for. Then follow what happens when a
model asks for a tool that does **not** exist — it begins in `run_agent_loop`
(the gate is deliberately skipped for an unresolvable name) and ends in
`src/tools/jc_tool.c:jc_tool_execute`, which spells the answer as a tool
result, with a spelling suggestion when one is close enough. Every step of
that is visible in a trace.

## Where this bit us

The first capture of this trace did not work, and how it failed is the reason
this series exists.

The fixture ordered the edit with the argument names a person would guess —
`old` and `new`:

```
tool edit_file {"path":"notes.txt","old":"milk","new":"oat milk"}
```

The run's event stream came back with this in it:

```
{"v":1,"type":"tool_result","name":"edit_file","is_error":true,"id":"c1","preview":"error: 'path', 'old_string', and 'new_string' are required"}
```

The tool refused. Because a refusal is a value, the loop carried on, the third
round fired, and the "model" — a fixture that had decided its final sentence in
advance, exactly as a confident model would — announced:

> Done -- notes.txt now says oat milk.

jichi exited **0**. `stderr` was **empty**. The final answer was a clear,
grammatical, complete lie, and `notes.txt` still said `buy milk`. Everything a
person normally checks agreed that the run had succeeded; the only artifact
that disagreed was the trace.

The argument names were never in doubt — they are declared in the schema at the
top of the very file that refused the call:

```c src/tools/jc_tool_edit.c:edit_schema
    tu_schema_string(s, "old_string", "Exact text to find and replace", 1);
```

Two lessons, and the second is the point of this guide. The small one: read
the schema, not your intuition. The large one: **a run's answer is not
evidence about the run.** Exit codes, empty stderr and fluent summaries are
all compatible with nothing having happened. Chapter 4 records that failing
run as a trace of its own.

## Where to go next

- The concepts under this chapter, from zero:
  [Annai 4](annai-04-tool-calling.md) (tool calling, the whole idea) and
  [Annai 5](annai-05-one-tool-all-the-way-down.md) (`read_file` and
  `edit_file`, from schema to guard).
- The arguments: [Fukabori 4](fukabori-04-the-agent-loop-as-a-state-machine.md)
  for the loop as a state machine,
  [Fukabori 5](fukabori-05-context-economics.md) for what those `growth` lines
  cost at scale.
- The instruments on a *real* run, where there is no fixture to hold still:
  `docs/OBSERVABILITY.md` — telemetry, run journals, and the offline readers
  for both.
- Chapter 2 of this guide: the same sentence with no tool call in it, which is
  the shortest path through `run_agent_loop` and the fastest way to see which
  of the machinery above was optional.
