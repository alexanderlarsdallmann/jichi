---
title: Read the turn — reference reading
audience: student
---
# Reference reading: five readings of one turn

Solve the task before reading this. A reference reading is worth exactly as
much as the **comparison**: where your five readings differ from these, one of
you is wrong about a program you can both open — go and find out which. Every
anchor below is a place to check, not a claim to accept.

The block between the two HTML-comment markers is a complete `READING.md` that
passes the grader. It is quoted here verbatim so that the grader's two-sided
proof (`tests/e2e/curriculum_graders.py`), the smoke driver
(`tests/smoke/reading_review.sh`) and this page share **one** text and cannot
drift apart.

<!-- READING.md -->
# One turn, read five ways

## Abstraction to concrete

The loop never names a provider. In `src/chat/jc_agent.c:stream_once` the
request is built through a function pointer: `prov->vt->build_request(...)`.
The slot is declared in `include/jc_provider.h:build_request` as a member of
the provider vtable, so the question "what runs here?" has exactly as many
answers as there are vtables that fill it. In this tree there are two:
`src/provider/jc_provider_openai.c:oa_build_request` (the OpenAI-compatible
dialect, which the institutional gateway and LM Studio both speak, so it is
the one a default local run reaches) and
`src/provider/jc_provider_anthropic.c:an_build_request`. Each is `static` and
reaches the loop only through the vtable it is installed in; nothing else in
`src/chat/` knows either name. That is the invariant CLAUDE.md states as "the
agent never branches on provider", read from the code rather than the rule.

## Control flow

One turn is `src/chat/jc_agent.c:jc_agent_run_turn`, which resets the
per-turn scratch arena and hands the history to
`src/chat/jc_agent.c:run_agent_loop`. The loop body is one call to
`src/chat/jc_agent.c:stream_once` followed by a decision: if the assistant
message that just streamed carries tool calls, execute each with
`src/tools/jc_tool.c:jc_tool_execute`, append the results to the history as
`tool`-role messages, and go round again; if it carries none, the text is the
answer and the turn returns. Inside `stream_once` there is a second, inner
loop, `for (attempt = 0; ; attempt++)`, and that is where a transport failure
re-enters -- at the top, rebuilding the request, never resuming the old one.
The order that actually runs for a two-tool turn is therefore:
run_turn -> loop -> stream_once -> (build, send, stream) -> execute tool ->
loop -> stream_once -> (build, send, stream) -> return.

## Data flow

The value followed is the request body, `char *body` in `stream_once`. It is
born inside `build_request` -- the concrete provider allocates and fills it --
and returned to the loop by pointer. The loop then sets
`req.stream_body = 1` and hands the pointer to
`src/net/jc_http.c:jc_http_stream`; the comment at the call site says why:
that flag *transfers ownership* to the HTTP layer. Inside jc_http the body is
wrapped in a `src/net/jc_http.c:body_reader` and uploaded by libcurl through
`src/net/jc_http.c:body_read_cb`, which frees the buffer the moment
`off >= len` -- while the response is still streaming back. So the body lives
in **none** of the three arenas: it is heap memory owned first by the
provider, then by the request, and gone before the first response byte is
read. That is also the reason the retry loop rebuilds it each attempt: a
freed body cannot be sent twice, so "retry the request" means "build the
request again".

## Execution

Checked against `docs/reading/traces/tool-round/expected/stdout.jsonl`, the
recorded run of a two-tool turn. Its events, matched to the code that emits
them: the first `message_start` is `stream_once` opening the stream for
request 1; the `tool_call` with `name":"read_file` is the provider's
`on_event` accumulating a call out of the SSE deltas fed by
`src/net/jc_sse.c:jc_sse_feed`; the `tool_result` with `is_error":false` is
the return of `jc_tool_execute`, already appended to the history; and the
*second* `message_start` is the loop going round -- request 2, built fresh,
which is why `docs/reading/traces/tool-round/expected/req.2` is larger than
`req.1` by exactly the tool result. Where my reading said the body was reused,
the record's two request files said otherwise, and the record wins.

## Review

The claim I would least like a recorded run to contradict is "the body is
freed before the response is read" -- it is the sentence the whole data-flow
reading hangs on, and it is easy to assert from the comment alone. I checked
it in `body_read_cb`: the free is guarded by `br->own && !br->freed &&
br->off >= br->len`, so it happens on the last upload chunk, not at request
end. The finding a reviewer would raise about the code: a `tool_result` with
`is_error` true is appended to the history *exactly like a success* and the
loop goes round on it -- the error is a value the model reads, not a branch
the loop takes. Tsuiseki 04 shows the consequence: a wrong call and a right
call leave byte-identical summary events. A reading that stops at "the loop
handles errors" is fluent, and wrong about what handling means here.
<!-- /READING.md -->

## Where readings usually differ from this one

- **One provider named, not two.** The commonest gap: following the pointer to
  the provider *you* run and stopping. The grader asks for both because an
  abstraction followed to one target is a guess about the rest.
- **The body "lives in the scratch arena".** It does not; that is what the
  `stream_body` handoff is for. If your data-flow reading placed it in an arena,
  re-read `body_read_cb` and ask who calls `free`.
- **Execution described, not checked.** A section that narrates what the loop
  *would* do, with no event from the trace quoted, is the assumption the task
  exists to refuse. Open the record.
