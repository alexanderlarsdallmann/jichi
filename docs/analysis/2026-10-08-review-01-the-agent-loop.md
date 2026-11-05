# Review 1 — The agent loop and its context management

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on the
agent-loop reviewer's report, which filled every section before the reviews were
paused. Its six code excerpts were checked line for line against the source.
Written by Claude Opus 5.5 on threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to understand the part of
jichi that does the work: the loop that calls the model and runs tools until the
model answers. You should have read [Annai 3](../reading/annai-03-main-to-the-loop.md)
(how `main` reaches the loop) or know what a tool call is.

**After reading it you will be able to:**

- follow one turn through `jc_agent_run_turn` and `run_agent_loop`, and name the
  ways it can end;
- state the six invariants the loop keeps, and recognise code that breaks one;
- read six small pieces of real code that each teach a C technique;
- explain the most important defects the review found here, and how each would
  be proven by a test before it is fixed.

**The files:**

| File | Lines | What |
|---|---|---|
| `src/chat/jc_agent.c` | 6,034 | the turn and the loop |
| `src/chat/jc_compact.c` | 1,677 | between-turn summaries, mid-turn elision |
| `src/chat/jc_envelope.c` | 1,270 | budgets, verify, rollback, journal |
| `src/chat/jc_sysmsg.c` | 1,039 | building the system prompt |
| `src/chat/jc_context.c` | 794 | the `/context` report |
| `src/util/jc_toolloop.c` | 655 | the repeated-call detectors |
| `src/util/jc_calib.c` | — | the per-model token calibration |

---

## 1. Words first

- **Turn.** One user message and everything the agent does to answer it
  (`jc_agent_run_turn`, `src/chat/jc_agent.c:4973`). A headless `-p` run is
  exactly one turn.
- **Round.** The code says *iteration*: one model call plus the tool calls its
  reply asked for. `maxToolIters` caps rounds, not tool calls. Its default is 25
  (12 under `--lite`), and it is raised to at least 200 when an autonomy envelope
  is armed (`jc_agent.c:5294-5296`).
- **Tool call and tool result.** The model asks for a tool with JSON arguments
  and an id; jichi answers with a `tool`-role message carrying the same id.
- **History.** The conversation, a vector of `struct jc_message`.
- **Placeholder.** An empty assistant message appended before each model call,
  so the streamed reply can be written into it in place.
- **Delegate** (or subagent). A nested agent run started by a tool, such as
  `spawn_subagent` or a `spawn_parallel` child. It runs the same loop at
  `agent_depth >= 1`.
- **Envelope.** The bounds of an unattended run: budgets (tokens, time, tool
  calls, reads), an edit scope, a verifier with rollback, and a journal.
  Budgets are *enforced* at every depth but *settled*, meaning verified, rolled
  back and journaled, only at the top level.
- **Compaction.** Two mechanisms: the between-turn summary, and the mid-turn
  elision ([Review 2](2026-10-08-review-02-when-the-window-fills.md) §2).
- **Calibration.** A learned per-model ratio of real prompt tokens to jichi's
  bytes/4 estimate.
- **Claim ticket.** When elision shortens a tool result, the full text is
  written to a private file, and the marker in history names that file (M348).

---

## 2. One turn, from start to return

```
 caller: TUI / headless (main.c) / ACP / daemon -- appends the user message
   |
   v
 jc_agent_run_turn  (jc_agent.c:4973)
   route to the fast tier, then jc_compact_run (summary, if over 80%)
   build the system prompt; envelope "start" record; hooks
   |
   v
 run_agent_loop  (jc_agent.c:1860; every delegate enters here too)
   for iter < max_iters:
     abort? budget? task budget? disk?  ----------------> return
     placeholder; stream_once -> provider -> HTTP/SSE
       (retry transient errors; meter tokens; learn calibration)
     error? ------------------------------------------> return st
     no tool calls? -> continue-once / nudge / self-review /
                       envelope verify gate ----------> return
     for each call: budget check -> gates -> jc_tool_execute
                    -> loop detectors -> append the tool result
     control socket, steering, 80% budget notice, budget panel
     jc_compact_midturn (elide); [context] note if pressed
     periodic verify; a delegate's loop-stop -> break
   cap reached --------------------------------------> return JC_OK (capped)
   |
   v
 out-of-scope sweep, Stop hooks, envelope "end" record, telemetry
```

**In words.** The caller adds the user message. `jc_agent_run_turn` does the
once-per-turn work: it routes to the fast model tier, then tries the summary,
in that order on purpose (M298, so the trigger measures the model that will
actually run), and builds the system prompt. It then calls `run_agent_loop`.

**Each round of the loop:**

- checks for an abort, a budget, a task budget and the disk fence;
- streams one model reply;
- returns if the reply failed, or if it was a final answer that passed the
  completion gates;
- otherwise runs each tool call through the gates and the tool, and appends each
  result;
- then, at the round's end, serves the control socket, adds any notices, and
  runs mid-turn compaction.

When the round counter reaches `max_iters`, the loop returns `JC_OK` with a
"capped" flag: the iteration cap is a circuit breaker, not a failure. After the
loop the turn sweeps for out-of-scope changes, runs Stop hooks, and writes its
`end` record.

**The same as pseudocode,** with the exits named:

```
run_agent_loop(history, opts):
    repeat up to opts.max_iters times:
        if aborted:                         return ABORTED
        if a budget or task budget is used: return settle_budget()    # see 4.5
        if the disk is below its floor:     return ABORTED
        reply = stream_once(history)        # may retry transient errors
        if reply failed:                    return the error
        if reply has no tool calls:
            maybe continue a cut reply once, nudge a prose "call", self-review,
            or run the envelope's verify gate
            return OK                       # the model answered
        for each call in reply:
            if a budget is used:            return settle_budget()    # finding F1
            if a gate refuses:              append an error result; next call
            result = run_tool(call)
            update the loop detectors
            append result
        end-of-round notices; mid-turn compaction; [context] note if pressed
        if a delegate is looping:           break
    return OK, capped
```

---

## 3. Six invariants the loop keeps

A change to this file must keep all six. The first four are the ones a newcomer
breaks without noticing.

1. **Tool errors are values.** A refused or failed call becomes a tool result
   with `is_error` set. `jc_status` only says whether the machinery worked. The
   model reads the error and can act on it.
2. **Every call is answered once.** Each tool call in a reply gets exactly one
   result before the next model call, because providers reject a call without
   its result (`jc_history_check`, `src/chat/jc_message.c:308`). Finding F1 is
   the one exit that breaks this.
3. **Synthetic notes go at the tail.** Budget, context, verify and steering notes
   are added as user messages *after* a round's results, never into the system
   prompt, so the cached prefix of the prompt stays byte-identical (M31, M347,
   M358).
4. **Lifetimes are chosen, not defaulted.** Copies of a call's name, arguments
   and id are heap-owned for one iteration (§4.3). The system prompt lives on the
   per-turn arena. The per-tool-call arena is reset before every tool call, so
   nothing a nested run needs may live there.
5. **Budgets are metered at the attempt.** `--max-tool-calls` counts a call
   before any gate decides, at every depth (`jc_agent.c:2729-2765`). A refused
   call is still a call made.
6. **Depth decides who settles.** Journal records, the verify gate,
   self-review, the budget notice and the panel run only at depth 0. Loop
   *stops* end only delegates; the top level is warned, never stopped.

---

## 4. Code to study

Six excerpts chosen by the reviewer, each copied exactly from `17bbb895`, with
what it teaches.

### 4.1 A bounded buffer beside a streaming parser — `src/chat/jc_agent.c:102-118`

```c
static int bridge_chunk(const char *buf, jc_size n, void *user)
{
    struct stream_bridge *b = (struct stream_bridge *)user;
    /* Keep a bounded prefix of the raw bytes for error diagnostics (M85). */
    if (b->raw_len + 1 < sizeof(b->raw)) {
        jc_size room = sizeof(b->raw) - 1 - b->raw_len;
        jc_size take = n < room ? n : room;
        memcpy(b->raw + b->raw_len, buf, take);
        b->raw_len += take;
        b->raw[b->raw_len] = '\0';
    }
    jc_sse_feed(&b->sse, buf, n);
    if (b->abort_flag != NULL && *b->abort_flag) {
        return 1; /* abort the transfer */
    }
    return 0;
}
```

**What it teaches.** libcurl calls this function for every chunk of the reply,
and it has two jobs it keeps apart:

- **It feeds every byte to the SSE parser,** which turns the stream into events.
- **It keeps only the first 2 KB of raw bytes,** so an HTTP error body can be
  shown later. The copy is bounded by construction: `room` can never exceed what
  is left of the fixed array, and the test `raw_len + 1 < sizeof(raw)` always
  leaves space for the terminator.

**The return value is the cancel channel.** Returning 1 tells libcurl to stop.
A Ctrl-C only sets a flag (signal handlers must do almost nothing), and this
callback honours the flag between chunks. This is the C89 shape of "bounded
buffer plus cooperative cancellation".

### 4.2 Cutting text for a model without breaking it — `src/chat/jc_compact.c:800-821`

```c
    if (strstr(m->content, "bytes of tool output elided") != NULL) {
        return 0;
    }
    /* Cut on character boundaries, not byte offsets (M191). A head ending in the
     * first byte of an em-dash -- or a tail starting on its last -- makes the
     * whole request body ill-formed UTF-8, and a strict server then rejects every
     * turn for the rest of the run, since the split byte stays in the history.
     * That is exactly how a zigodot run wedged: HTTP 500 in 40ms, forever.
     * See docs/ANECDOTES.md #22. */
    head = jc_utf8_trunc_len(m->content, (jc_size)ELIDE_HEAD);
    tail_off = jc_utf8_resync(m->content, len, len - (jc_size)ELIDE_TAIL);
    /* M348: the claim ticket. Offer the FULL original to the preservation
     * store; on success the marker names where the bytes went, so the model
     * can retrieve exactly what was taken instead of re-running the original
     * call at full price (the measured re-read loop). On failure -- or with
     * no spiller -- the marker is the old ticketless text, byte for byte. */
    ticket[0] = '\0';
    if (spill != NULL) {
        if (!spill(spill_ctx, m->content, len, ticket, sizeof(ticket))) {
            ticket[0] = '\0';
        }
    }
```

**Three lessons in twenty lines:**

1. **An explicit idempotence guard.** A second pass must never shorten a marker
   it wrote itself.
2. **Cut on character boundaries.** In UTF-8 one character can be up to four
   bytes. A cut in the middle of one makes the whole request invalid, and since
   the broken byte stays in the history, every later request fails too: one run
   failed "in 40 ms, forever" (ANECDOTES #22).
3. **The ticket is offered, not assumed.** The marker names a file only if
   writing it succeeded. Never hand a model a path that is not there.

Finding F4 below shows where that guarantee ends: at the end of the process.

### 4.3 Ownership that survives a reallocation — `src/chat/jc_agent.c:2679-2695`

```c
            /* Copy the call fields: appending the tool result below may
             * realloc the message vector and invalidate `tc`. The copies are
             * MALLOC-OWNED by this iteration and freed at `next_call` (M218):
             * they are consumed within the iteration (callbacks, history and
             * telemetry all take their own copies). The session arena leaked
             * ~3 strings per call for the life of the process (fixed M180);
             * the per-turn scratch M180 moved them to still accumulated
             * name+id+FULL ARGS x hundreds of calls for the whole of a
             * marathon turn (~190 KB measured by tests/smoke/turn_scratch.sh
             * at 200 x 900 B) -- and tool_scratch is off-limits here because
             * a spawn_* call runs a nested agent loop that resets it while
             * these are still live (the include/jc_app.h invariant). Every
             * early-skip path below must `goto next_call`, never `continue`. */
            name_copy = jc_strdup(tc->name ? tc->name : "");
            args_copy = jc_strdup(tc->arguments_json ? tc->arguments_json
                                                     : "{}");
            id_copy = jc_strdup(tc->id ? tc->id : "");
```

**What it teaches.** `tc` points *into* the assistant message, which lives in a
growable vector. Appending the tool result can move the vector, and `tc` then
points at freed memory: a classic use-after-free.

So the fields are copied first. The comment records why each other home was
wrong:

- the session arena leaked per call;
- the per-turn arena grew without bound in a long turn;
- the per-call arena is reset by a nested agent run while these copies are still
  in use.

`malloc` plus a single exit label (`next_call`) is the shortest lifetime that is
long enough. That is CLAUDE.md's "pick the shortest arena that outlives the
data", applied by elimination. Note the last sentence of the comment: every
early exit must go through the label, or the copies leak.

### 4.4 Hysteresis keeps a cache key stable — `src/chat/jc_sysmsg.c:57-75`

```c
    band = limit_tokens / 8;
    if (band > 1024) {
        band = 1024;
    }
    if (band < 64) {
        band = 64;
    }
    if (*held > 0 && *held_limit == limit_tokens) {
        drift = raw - *held;
        if (drift < 0) {
            drift = -drift;
        }
        if (drift < band) {
            return *held;
        }
    }
    *held = raw;
    *held_limit = limit_tokens;
    return raw;
```

**What it teaches.** The system prompt's size limits are computed from the
context budget divided by the calibration ratio, which moves a little after
every model call.

- **Without this, the prompt changed every turn.** The point where the rules
  were truncated moved by a few bytes, so the system prompt changed every turn,
  and the provider's prompt cache was re-billed every turn without anyone
  noticing (M365).
- **The fix is hysteresis,** as in a thermostat: keep the previous value until
  the new one drifts a whole band away, and bound the band so the kept value is
  wrong by at most a known amount.
- **The function stays pure.** The state (`held`) lives in the caller, so this
  function has no side effects and can be unit-tested.

### 4.5 One verdict, two settlements — `src/chat/jc_agent.c:1615-1630`

```c
    if (app->agent_depth == 0) {
        *st = env_stop_for_budget(app, b);
        return 1;
    }
    /* A delegate. Stop where it stands and keep the work -- the iteration cap's
     * circuit-breaker contract, for the same reason: what it produced so far is
     * valid and is already in its history. Settlement (journal, verifier, the
     * rollback decision) belongs to the top level, whose own check runs next. */
    jc_logf(JC_LOG_WARN,
            "envelope: %s budget exhausted at depth %d -- stopping this "
            "sub-agent; the top-level run settles the outcome",
            jc_env_budget_name(b), app->agent_depth);
    app->last_run_budget_stopped = 1;
    *st = JC_OK;
    return 1;
}
```

**What it teaches.** When a budget runs out, depth decides what happens:

- **at the top level,** jichi records it in the journal, runs the verifier and
  decides whether to roll back;
- **a delegate** just stops where it stands, keeps its work and returns `JC_OK`,
  leaving the settling to the top level.

Putting the decision in one function keeps its two callers, before a request
and before each tool call, from drifting apart. It is also where the review's
most serious finding in this slice lives (F1): the decision is right, but one of
its callers returns in the middle of a round.

### 4.6 A detector that never allocates — `src/util/jc_toolloop.c:318-342`

```c
static int slot(struct jc_toolloop *w, const char *tool, const char *key, int exact)
{
    int i;
    /* M868 (LOOP-1): compared by hash and length, as the success-side watch
     * (jc_noprogress) compares its triples -- the whole key, however long. */
    unsigned long kl = (unsigned long)strlen(key);
    unsigned long kh = jc_reread_hash(key, kl);

    for (i = 0; i < w->n; i++) {
        if (w->exact[i] == exact && w->keylen[i] == kl && w->keyhash[i] == kh &&
            strcmp(w->tool[i], tool) == 0) {
            return i;
        }
    }
    if (w->n >= JC_TOOLLOOP_MAX_ENTRIES) {
        return -1;
    }
    i = w->n++;
    jc_snprintf(w->tool[i], JC_TOOLLOOP_TOOL_MAX, "%s", tool);
    w->keyhash[i] = kh;
    w->keylen[i] = kl;
    w->exact[i] = exact;
    w->count[i] = 0;
    w->told[i] = 0;
    return i;
```

**What it teaches.** The loop detector watches for a model repeating the same
failing call. It runs inside the tool loop, so it allocates nothing:

- a fixed table of 24 rows;
- keys compared by hash plus length (an earlier version compared truncated text,
  which could never match a long command, M868);
- a full table simply stops tracking new pairs, returning `-1`.

This is a deliberate trade: **a bounded miss is better than an allocation in the
hot loop**. An advisory detector may miss a finding, but it can never fail a
turn.

---

## 5. What the review found

Seventeen findings: one high, six medium, the rest low. Status is the reviewer's;
"ran" means it was reproduced in a private copy with the mock model.

### F1 (high, verified by a run) — a budget stop in the middle of a round strands tool calls

**What.** The budget check before *each* tool call returns from the loop
(`jc_agent.c:2668-2674`). If a reply asked for three calls and the budget runs
out after the second, the third never gets a result. The saved session holds a
call without its answer.

**The scenario, reproduced.**
1. A mock reply asks for three `read_file` calls, and the run has
   `--max-tool-calls 2`.
2. The run ends `budget_exhausted` after two calls.
3. `--continue` warns `unanswered tool call 'c3'` and sends a request with three
   calls and two results.

Real providers reject that with HTTP 400. So the documented advice, "the work is
kept for you to review or resume", leads to a run that fails at its first model
call.

**Why it matters.** Invariant 2 of §3 breaks, and a comment in
`jc_message.c:287-289` claims it cannot.

**The fix and its proof.** Before returning, append an error result for each
remaining call ("not run: the run's budget stopped before this call"), or stop
only at round boundaries. The test, written first: this exact shape as a smoke
driver, asserting that the saved session has zero wire-shape violations.

### F2 (medium, ran) — a delegate on another model is measured against the parent's window

**What.** A child running on a different model gets its own provider, but the
loop's idea of "the active model" stays the parent's.

**What follows from that.** The child's elision, calibration and system-prompt
line all use the parent's window.

**Reproduced.** A parent with a 200,000-token window and a child with 3,000:

- the child's last request was about 22,000 tokens, seven times its window;
- no compaction fired;
- its prompt told it the window was 200,000;
- its telemetry rows named the parent's model.

A real 3k server would have refused the second request.

**The fix.** Carry the called model in the run's options, and use it for the
limit, the calibration and the prompt.

### F3 (medium, ran) — one failed summary window silently drops its messages

**What.** The summary is made window by window. If one window's call fails,
that window is skipped, and the history is still cut as if every window had been
summarized.

**What the documentation says.** COMPACTION.md says a failed summary leaves the
history intact.

**Reproduced.** The mock summarizer was made to return 500 for the window holding
turn 3. Turn 3 then disappeared from both the history and the summary, and no
warning was printed.

**The fix.** Abandon the compaction if any window fails.

### F4 (medium, ran) — claim tickets outlive the store they point into

**What.** The ticket store is per process (`~/.jichi.d/tool-output/<pid>`) and is
deleted at exit. The markers that name it are saved in the session.

**Reproduced.** Run 1 ended with 25 ticketed markers in its history. Its
`--continue` re-sent all 25, and **none of the 25 paths existed** any more.

**The fix.** Rewrite markers to their ticketless form when saving, or key the
store by session.

### F5 (medium, ran) — continuing a cut reply moves a delegate onto the parent's model

**What.** When a reply is cut at the output ceiling, the loop continues it once
(M859). For a delegate on another model, that continuation switches the
provider to the parent's. Every later round of the child then runs on the wrong
model, while the telemetry still names the child's.

**The fix.** Leave the provider alone in that path; nothing changed it.

### F6 (medium, ran) — the standing `[context]` note is never removed

**What.** Since M900 the note is added on *every* pressed round, as a new user
message. Nothing removes or shortens old ones, so a long pressed loop fills its
window with notes about its window.

**The fix.** Replace the previous note instead of adding another, or keep only
the newest.

### F7 (medium, ran) — compiler errors are classified as "bad arguments"

**What.** The failure classifier files a failed build or a failing assertion as
`bad_args`. The loop detector's note then tells the model its *arguments* are
wrong, when its *code* is.

### F8 to F17 (medium-low to low), in brief

- **F8** The summarizer request has a hard-coded 60-second cap. A slow local
  summarizer always fails, which then triggers F3.
- **F9** At each process's first turn, the "non-history" estimate (system
  prompt plus tools) is a flat 2,000 tokens. A nested run overwrites the
  parent's.
- **F10** The calibration sample includes the reply just received.
- **F11** The between-turn summary keeps its tail measured in raw bytes, and
  ignores the system prompt's size.
- **F12** Nothing checks for an abort *inside* a round; a Ctrl-C waits for every
  tool call of the reply.
- **F13** An `apply_patch` edit without a `path` is refused as "out of scope"
  instead of "missing path".
- **F14** Command subtasks get the 4 KB hand-back cap meant for answers to a
  parent model.
- **F15** A run capped under an envelope is recorded `outcome: ok` though no
  completion verify ran (already recorded as DEFERRED item 7).
- **F16** Comments and docs contradict the code: "once per run" for a note that
  is now per round, and "turn-lived" for tickets that are process-lived.
- **F17** Smaller items: a successful `run_tests` does not reset the success
  detector's counts the way a successful shell command does, and others.

---

## 6. The design, seen whole

**The size.**

- `jc_agent.c` is 6,034 lines, and `run_agent_loop` alone runs from line 1860 to
  4926, about 3,070 lines.
- Its per-tool-call body is about 1,780 lines, with a dozen `goto next_call`
  exits and about 25 local state variables.
- Most blocks carry the milestone that produced them. That history is valuable,
  and it buries the control flow: the reviewer needed a line map of every exit
  to see the function's shape.

**What is strong.** The pure cores are small, side-effect free and unit-tested:
the loop detectors, the envelope's helpers, the compaction passes, the prompt
fitting and the calibration blend. The weak spots are where those cores meet
the loop's shared state (F2, F5, F9).

**Four structural observations, each with a lesson:**

1. **`struct jc_app` is an implicit return channel.** The loop reports through
   about 40 fields of the shared application struct (`last_run_*`,
   `last_fail_*`, `ctx_overflow` …). Nested runs write the same fields, so the
   code keeps a hand-maintained save and restore list around every tool call,
   and that list already misses four fields (F9). *Lesson:* a function that
   reports through shared state needs a list of what to restore. A returned
   `struct jc_run_result` would make the hand-off explicit, and would be the
   natural carrier for a relay's "why I stopped".
2. **Eight producers of synthetic messages, no shared policy.** Budget notes,
   nudges, self-review, verify feedback, the `[context]` note, operator steering:
   each has its own throttle, or none (F6). *Lesson:* when the same kind of thing
   is produced in eight places, give it one small API (kind, text, throttle,
   history budget) and the class of defect becomes visible in one place.
3. **The model's identity is split.** The provider a call uses and the model the
   rest of the loop believes is active can differ for a delegate (F2, F5).
   *Lesson:* one fact, one place. Pass the model in the run's options.
4. **Thresholds are compile-time.** 800 bytes, 6 messages, 80 and 60%, 9
   repeats, 24 entries, 4,096 bytes: each is measured and documented, none is
   configurable. A relay will need at least the pressure thresholds as
   parameters.

**What the reviewer would refactor first, and the cost:**

1. Turn the per-call gate chain into an ordered table of gate functions, each
   returning allow, or refuse with a text and a reason. The subtle ordering rules
   become data.
2. Extract the completion gate into its own function.
3. Make every per-call exit append results for the remaining calls through one
   helper. That fixes F1, and F12's class with it.

Cost: several days, and real risk. What makes it feasible is that the smoke tier
already covers most gates by their effect.

---

## 7. What this means for the context relay

The relay proposal's hook points are all in this file, and the
[proposal's §9](../proposals/2026-10-context-relay.md) lists the findings above
that must be fixed first:

- **F1:** a handover must end at a round boundary;
- **F2:** a child's trigger must read the child's window;
- **F3:** notes must never lose content silently;
- **F4:** notes must never cite a ticket;
- **F6:** a leg must not fill with notes.

**The exit to copy** is the delegate loop-stop (`jc_agent.c:4877-4906`), which
ends with the history whole and a reason recorded.

---

## 8. Something to do

**With a bench (jichi built, the mock model from `make smoke-tools`):**
reproduce F1.

1. Script a mock reply with three tool calls.
2. Run it headless with `--max-tool-calls 2`, then `--continue`.
3. Find the `[history] wire-shape` warning.
4. Then read `jc_message.c:287-289`, the comment the finding contradicts.

**Without a bench (the read-only twin):**

1. In §4.3, the comment names three homes for the copies that were tried and
   rejected. For each, write one sentence: what would go wrong if the copies
   lived there?
2. In §4.4, compute the band for a 32,000-token limit and for a 2,000-token
   limit. Which clamp applies to each?
3. In §2's pseudocode, mark every exit. Which of them can leave a tool call
   without a result? (There is exactly one, and it is F1.)

## 9. Read further

- [Annai 3](../reading/annai-03-main-to-the-loop.md) and
  [Fukabori 4](../reading/fukabori-04-the-agent-loop-as-a-state-machine.md): the
  loop as a guided tour and as a state machine. *Primary for jichi.*
- [Tsuiseki 1](../reading/tsuiseki-01-a-tool-round.md): one recorded tool round,
  byte for byte. *Primary for jichi.*
- [COMPACTION.md](../COMPACTION.md) and [AUTONOMY.md](../AUTONOMY.md): the
  references for §4.2 and §4.5. *Primary for jichi.*
- The Unicode Standard, chapter 3, "Conformance", the UTF-8 definition (D92):
  why a cut inside a sequence makes the text ill-formed (§4.2). *Primary.*
- Robert Nystrom, *Game Programming Patterns*, "Game Loop" chapter (online,
  2014): a readable account of loops that must stay responsive while doing work.
  *Secondary.*

---

## Completed on 2026-10-17 (at e5d832a4)

*This section finishes the review above. The chapter was written from a report that was paused
before its findings could be checked against later code; about 45 milestones (M914 to M959) have
landed since `17bbb895`. The text above is left as it was, because people may already have acted on
it; this section says what is true now. It covers this chapter and the measured findings of
[Review 2](2026-10-08-review-02-when-the-window-fills.md) (R2-1 to R2-6). Written by Claude Opus 5.5,
2026-10-17, at master `e5d832a4`.*

### How this was checked

Every earlier finding was looked up again in the source at `e5d832a4`, never in a document. Where a
file had not changed at all since `17bbb895` (`git diff` prints nothing for it), the earlier
measurement still describes the same code; that is true of `jc_compact.c`, `jc_context.c`,
`jc_envelope.c`, `jc_toolloop.c` and `jc_calib.c`. `jc_agent.c` grew from 6,034 to 6,668 lines, and
all of its new code was read.

New findings were reproduced in a private copy of the tree, against the repository's scripted mock
model (`tests/tools/mockmodel`), with no API key and no network. Two builds were used: the default
build, and one made with `make SAN=1`, which adds the **sanitizers** described below. No jichi run
was wrapped in a timeout.

Marks, as in the rest of the review: **[V]** read end to end, **[M]** measured, **[R]** read but not
traced to the end.

### Words for this section

- **Sanitizer.** A compiler option that adds checks to every memory access of the built program.
  *AddressSanitizer* (ASan) stops the program at the first read or write outside a live allocation
  and prints where the memory came from and where it was freed. *LeakSanitizer* reports memory still
  allocated at exit with no pointer left to it. `make SAN=1` builds jichi with both.
- **Use after free.** Reading memory through a pointer after that memory has been given back. Often
  the old bytes are still there and the program "works"; on another allocator, or after the memory is
  reused, it reads garbage or crashes. That is why a sanitizer, not a normal run, is the instrument.
- **The context relay** (built since the review, M920 to M954, off by default; see
  [RELAY.md](../RELAY.md)). When a headless run's context window is exhausted, the run ends at a
  round boundary (a **handover**), writes notes for a successor (the **baton**), and the task continues
  in a fresh session (the next **leg**). Before the baton is written, the loop asks the model for its
  own summary in one extra call (the **account call**). *Topology A:* a supervisor starts the next leg.
  *Topology B:* jichi starts it in the same process, up to a ceiling of legs.
- **The trust step** (M851). A configuration file inside a project is not trusted until the user says
  so. Until then each of its keys is applied according to a table: *safe* keys apply, *narrowing*
  keys apply only in the stricter direction, *gated* keys wait for trust.

### 1. The earlier findings now

| Finding | Then | Now (at `e5d832a4`) |
|---|---|---|
| F1 a budget stop strands tool calls | high | **Fixed (M919).** The unrun calls get error results that name the budget (`jc_agent.c:3058-3101`). The fix itself has a new defect: N2 below. |
| F2 a delegate on another model is measured against the parent's window | medium | **Changed.** Its telemetry now names the model it called (M922). The window, the calibration and the prompt's "Context window" line still use the parent's. |
| F3 a failed summary window drops its messages | medium | **Still open** (file unchanged). |
| F4 claim tickets outlive their store | medium | **Still open** for a resumed session. The relay's notes remove ticket paths from their own copy. |
| F5 a cut-reply continuation moves a delegate onto the parent's model | medium | **Still open, and now contradicted by its own record:** the continuation goes to the parent's model while the telemetry (since M922) names the child's. Measured again. A new feature copied the same line: N3. |
| F6 the `[context]` note is added every pressed round | medium | **Still open.** With the relay on, an exhausted window now ends the leg instead. |
| F7 compiler errors classified as "bad arguments" | medium | **Still open** (file unchanged). |
| F8 the summarizer's fixed 60-second limit | medium-low | **Still open;** the 2026-10-17 hardening review generalised it to every side call ([chapter 4](2026-10-17-hardening-04-network-and-secrets.md), C-09). |
| F9 the non-history estimate, and a nested run overwriting it | low | **Still open, and the class grew:** see N4. |
| F10 to F15 | low | **All still open.** F15 remains DEFERRED item 7. |
| F16 docs and comments that contradict the code | low | **Partly fixed:** the `jc_message.c` comment was corrected with M919. The others stand. |
| F17 smaller items | low/note | **Still open.** A failed model call's telemetry row still names the lead's model, while the successful row no longer does. |
| R2-1 the summary cannot run in a headless run | high | **Still true** (by design). |
| R2-2 no exit under pressure | high | **Fixed when opted in:** the relay. The default (`"relay": false`) is unchanged. |
| R2-3 the between-turn `compact` event carries no numbers | medium | **Still open.** |
| R2-4 a deliberate early stop reads `done` | medium | **Still open** by default; a relay handover has its own stop reason, `handover`. |
| R2-5 a measured figure presented as general | low | **Still open.** |
| R2-6 a model told to wait for a trigger may imagine it | design input | **Taken:** jichi, not the model, decides when to hand over. |

In numbers: of the 23 earlier items, 1 is fixed, 1 is fixed for runs that opt in, 1 design input was
taken, 3 changed shape or were partly fixed, and 17 still stand.

### 2. What changed in the loop, and why it matters to a reader of this chapter

- **M919, the budget stop.** It does what the review proposed: before returning, it answers every
  remaining call of the round with an error result. *Design decision:* an error, not a success,
  because the tool was asked for and did not run, and "tool errors are values" (§3, invariant 1).
- **M917, the overflow flag's lifetime.** A context-window refusal used to stay flagged for the rest
  of the process; now each loop clears it on entry. It is the right question, asked in one direction
  (see N4 for the other).
- **M909, an error inside a stream.** A server that answers HTTP 200 and then reports an error inside
  the stream now fails the call, exactly as an HTTP error would.
- **M937, the "give your answer" nudge.** A delegate whose reply contains only reasoning and no answer
  is asked once more. *Implication:* it is a new place where the loop continues a delegate, so it
  inherited F5's question of *which* model continues (N3).
- **M949, recovering written tool calls.** For a model marked `textToolCalls`, a tool call written as
  a fenced JSON block is executed instead of nudged.
- **M920-M954, the relay.** The decision is a small pure function (`relay_decide`,
  `src/util/jc_relay.c:32-104`) called after each round's mid-turn pass at depth 0. The handover
  copies the exit this chapter recommended (§7): it leaves at a round boundary with the history
  whole and a distinct reason recorded.

### 3. New findings

**N1 (high, [M]) -- the relay's account-call fallback crashes.** When the account call fails, the loop
quotes the leg's last answer instead (`jc_agent.c:2056-2070`). The copy ends with

```c
*out[b.len] = '\0';
```

In C, `[]` binds tighter than unary `*`, so this is `*(out[b.len])`: it treats the caller's pointer
variable as an array of pointers, reads one from far up the stack, and writes a zero byte wherever
that points. The copy itself is never terminated. The default build crashes (SIGSEGV), so the
handover's notes and the turn's session save are lost. It needs three things together: the relay on,
a failed or empty account call, and an earlier answer in the session. The account call sends the
whole exhausted history, so on a real server it is the call most likely to be refused. The existing
driver tests the fallback only on a leg with *no* earlier answer, which is why it stayed green.
*Fix:* `(*out)[b.len] = '\0';`. *Test first:* a third run in `relay_account.sh` with an earlier answer
and a refused account call.
*Lesson:* when a pointer is passed by address, write `(*p)[i]`, and test the branch where the data
exists, not only the one where it does not.

**N2 (high, [M]) -- M919's fix reads through a pointer that its own appends invalidate.** The loop
takes a pointer to the assistant message once, then appends one result per unrun call. The history is
a growing array (`jc_vec`): when it reaches its capacity (8, 16, 32, ... messages), an append moves it
to new memory and frees the old. From the second unrun call on, the pointer can point at freed
memory. AddressSanitizer reports a *use after free* when the first append crosses a capacity
boundary. On glibc the old bytes happen to be intact, so the resumed session looks right and the smoke
driver (which strands exactly one call) cannot see it. The same file explains this hazard a few lines
further down, in the comment §4.3 teaches.
*Fix:* re-fetch the message by index inside the loop.
*Test first:* a round of four calls that crosses a capacity boundary, run against the sanitizer build.
*Lesson:* a pointer into a growable array is valid until the next append. §4.3's rule, "copy before
you append", applies to every loop that appends.

**N3 (medium, [M]) -- the M937 nudge moves a delegate onto the lead's model.** It ends with the same
line as F5, `prov = app->provider;` (`jc_agent.c:2577`). A child running on its own model gets its
final answer from the lead's model, and the telemetry names the child's. Measured, together with F5.
*Fix:* remove the line in both places; nothing changed the provider there.
*Test first:* two configured models, and an assertion that every request carrying the child's task
names the child's model.

**N4 (medium, [M]) -- a nested run resets and overwrites the lead's relay state.** Every
`run_agent_loop` entry, including a `spawn_subagent` child's, clears the lead's leg counters, its
prompt-growth series, its last verify result and the overflow flag (`jc_agent.c:2168-2208`). The
save-and-restore around a tool call does not cover them. So a *child's* context-window refusal is still
flagged when the lead decides, and the lead hands over with reason `overflow` while its own prompt was
a few tokens of a large window (measured); the baton's call count and the leg budget undercount
whenever a delegate ran.
*Fix:* do these resets only at depth 0, and add the overflow flag and the last prompt size to the
save-and-restore list.
*Lesson:* this is §6's first observation, "`jc_app` is an implicit return channel", growing by eight
fields. A field that a nested run can write belongs in a returned result.

**N5 (medium, [M]) -- the relay never sees the lead's own overflow.** A model-call error at depth 0
returns from the loop at once, before the point where the relay decides. So the overflow trigger can
only fire from a nested run (N4), and a headless leg whose server refuses its request as too long ends
`error` with no notes, which is the outcome [RELAY.md](../RELAY.md) names as the problem in its first
paragraph. *Fix:* route that error into the handover, with an account call on a reduced history as the
relay proposal intended. *Test first:* a mock that refuses the lead's second request as too long; assert
`handover` and one baton.

**N6 (medium, [M]) -- written tool calls are recovered once per run.** The recovery pass of M949 sets a
flag after its first success and never clears it, and the older prose nudge is switched off for these
models, so a model's *second* written call becomes its final answer: measured, the run ended `done`
after one tool call. *Fix:* recover on every reply, and number recovered call ids per run (today two
rounds can produce the same id). *Test first:* a two-call script in `text_tool_recover.sh`.

**N7 (low, [M]) -- two small leaks** in the same new code: the recovered calls' strings after they are
copied (about 300 bytes per recovered call), and the relay's prompt series at exit.

**N8 (medium, security class: a trust-step classification gap).** The relay's configuration keys are
classified as *safe* in the trust table (`src/config/jc_config_trust.c:122-125`), so a project the user
has never trusted can switch the relay and its in-process legs on. Together they multiply how many
model calls one run makes, which the trust step otherwise treats as needing trust (the iteration cap
is a narrowing key). RELAY.md states the opposite rule. Measured. *Fix:* classify them as narrowing
(an opt-in whose "off" is the strict side, and a bound whose smaller value is stricter), and let a lint
refuse a safe relay key.

**N9 (medium, [M]) -- RELAY.md describes stop conditions that are not built.** No stop for "two legs
in a row made no progress" exists (`relay_may_continue` has no caller), the leg ceiling ends as
`handover` rather than `relay_stalled`, and an abort writes no notes. Measured: legs that each changed
nothing ran to the ceiling. *Fix:* wire the progress check, or first correct the page to what is
built.

**N10 (low) -- smaller items.** The baton cannot report a green verify ("no verify" and "passed" are
both stored as 0); the account call is counted as the leg's last prompt and is not checked against
the token or time budget; the memory-truncation note puts its closing parenthesis in front of the
memories; with the relay on in the TUI or over ACP the decision still fires but nothing writes notes
([Review 8](2026-10-08-review-08-tui-setup-acp.md)'s completion, N1, measures it).

**N11 (low) -- the gate's sanitizer stage never runs these paths.** `make ci` runs the unit suite, seven
read-only subcommands and exactly one agent turn under the sanitizers; every relay, budget-stop and
recovery driver runs only against the default build. That is why N2 and N7 reached master green.

### 4. Lessons

1. **A fix is new code, and gets the same review.** M919 closed F1 correctly and opened N2 with the
   exact hazard the file documents a few lines away. Re-reading a fix against the file's own stated
   invariants is cheap.
2. **Copy-paste carries defects forward.** F5's line was copied into M937 (N3), and F9's missing
   restore became eight missing restores (N4). When a review finding names a line, search for that
   line's siblings before closing it.
3. **Test the branch where the data exists.** N1's driver was written for "no earlier answer", the one
   case where the crashing branch does not run.
4. **Choose the instrument by the defect.** A use after free and a leak are invisible to a smoke run
   on glibc; a sanitizer build sees them in one run (N2, N7, N11).
5. **A pure core is only as right as its inputs.** `relay_decide` is clean and tested, and still hands
   over for the wrong reason when the loop feeds it a child's flag (N4) and never sees the lead's own
   overflow (N5).

### 5. Recommendations, in order

1. Fix N1 (one expression) and N2 (one re-fetch), each with its red-first test; run both tests against
   the sanitizer build in `make ci` (N11).
2. Reclassify the relay keys in the trust table (N8).
3. Fix N3 together with F5 (two deleted lines and one two-model driver).
4. Make the relay's resets depth-0 only and extend the save-and-restore list (N4, closing F9's
   restore half too); then route the lead's own overflow into the handover (N5).
5. Remove the once-per-run latch in the recovery pass and make recovered ids unique (N6), and free
   the recovered strings (N7).
6. Correct RELAY.md's stop conditions and status table, or build them (N9); fix the baton's verify
   encoding (N10).
7. Then the open items of the original list, in its order: F3 and F8 before anything depends on the
   summarizer, F6, F7, F2.

### 6. Something to do

**Without a bench.** In N1's line, add parentheses to show how C groups `*out[b.len]`. Then write the
version that means what the author meant. Which of the two would `gcc -Wall` warn about? (Neither: both
are well-typed, which is why the review needed a run to find it.)

**With a bench (`make SAN=1 jichi` and `make smoke-tools`).** Write a mock script whose first two
replies each call one tool and whose third asks for four, and run it headless with
`--auto --max-tool-calls 4`. Read the sanitizer's report: which function freed the memory, and which
one read it afterwards? Then move the `jc_history_get` call into the loop and run it again.
