# Review 2 — When the context window fills: what two drives measured

*Part of the 2026-10-08 review of jichi, written on threadwork (the operator's
second machine) by Claude Opus 5.5 at master `17bbb895` (M913). The overview,
[Review 0](2026-10-08-review-00-overview.md), lists the other chapters. This one
answers a question the operator asked on 2026-10-08, with measurements: why does
"autocompact" sometimes seem not to work, and what does `jlu/qwen3.8-27b` do when
its window is nearly full?*

---

## Who this is for, and what you will be able to do

**For** a self-learner or a junior developer who has used jichi for a long run and
wondered why the context window filled up anyway. You do not need to have read
the compaction code. You should know what a tool call is (the
[beginner's tour](../reading/annai-04-tool-calling.md) explains it).

**After reading it you will be able to:**

- tell the two compaction mechanisms in jichi apart, and say which one can run in
  a headless (`-p`) run and which one cannot;
- read a `compact` telemetry event and a `model_call` event, and compute the size
  of a prompt from them;
- explain, with numbers, what happens to a long agent loop after its first
  "context pressure" pass;
- design a small experiment of your own that induces context pressure cheaply.

**Where it fits.** The mechanism is taught in
[Annai 8](../reading/annai-08-when-the-conversation-gets-too-long.md) and argued in
[Fukabori 5](../reading/fukabori-05-context-economics.md); the full reference is
[COMPACTION.md](../COMPACTION.md). This chapter measures what those pages
describe. The design that answers it is the
[context-relay proposal](../proposals/2026-10-context-relay.md).

---

## 1. Five terms first

- **Token.** The unit a language model reads. A token is a few characters of
  text; English prose is roughly four bytes per token, code and JSON are denser.
  jichi ships no tokenizer, so it *estimates* tokens from bytes and corrects the
  estimate from what the server reports (COMPACTION.md, "Token-estimate
  calibration").
- **Context window.** The largest request a model accepts, prompt plus the reply
  it is allowed to write. For both free `jlu/*` models used below the gateway's
  window is **256,000 tokens**, and jichi reserves up to 16,384 of them for the
  reply. jichi's own idea of the window is the `contextLength` the configuration
  declares; the drives below declared **196,608**.
- **Prompt.** Everything sent on one model call: the system prompt, the tool
  definitions and the whole conversation so far. An agent loop re-sends all of it
  on every call, so the prompt grows with every tool result.
- **Prompt cache.** The server remembers the beginning of the last prompt. When
  the next prompt starts with exactly the same bytes, that part is not processed
  again, which makes a long prompt cheap and fast. Change one byte early in the
  prompt and everything after it is processed again.
- **Compaction.** jichi's name for making the prompt smaller. It has **two**
  mechanisms, and the rest of this chapter is about the difference between them.

---

## 2. The two mechanisms, in the code

### 2.1 The summary: between turns only

A **turn** is one user message and everything the agent does until it answers.
At the start of each turn jichi checks whether the conversation has grown past
80% of the window, and if so asks a model to summarize the old part. This is
the only place the summary runs (`src/chat/jc_agent.c:5211-5224`):

```c
    /* Fold older history into a summary if it has grown large (between-turn
     * only; a no-op below threshold). Failures leave history intact. Runs AFTER
     * the routing above, so it measures the model that will actually run. */
    {
        int did_compact = 0;
        jc_compact_run(app, hist, cb, &did_compact);
        if (did_compact) {
            cJSON *o = telem(app, "compact");
            jc_eventlog_end(app->telemetry, o);
            /* M218: the summarized prefix's messages were just freed --
             * another low-water moment worth sweeping back to the OS. */
            jc_mem_release_os();
        }
    }
```

And the check inside it (`src/chat/jc_compact.c:740-746`):

```c
    limit = effective_limit(app);
    /* Scale the byte estimate to real tokens (M77) so the trigger fires against
     * the model's actual context usage, not a ~2x-optimistic guess. */
    est = jc_compact_effective_est(app, hist);              /* M536 */
    if (est <= limit * 4 / 5) {
        return JC_OK; /* below the trigger */
    }
```

**What to notice:**

- The check runs **before** the turn's first model call. In an interactive session
  that is the right moment: the previous turn has ended, nothing is in flight, and
  the old part of the conversation can be replaced cleanly.
- **A headless run is one turn.** `jichi -p "task"` sends one user message, and
  the agent then works for as many tool rounds as it needs. When the check runs,
  the history holds only the task text, which is far below 80%. The check never
  comes round again. Pseudocode of a headless run:

  ```
  run_turn(task):
      if estimate(history) > 0.8 * window:      # history = [task]: never true
          summarize_old_part()
      loop until the model answers or a limit fires:
          reply = model(system + tools + history)
          history += reply + tool results       # grows every round
          midturn_elision(history)              # section 2.2: the only relief
  ```

- The event it writes, `compact` with no other fields, records **that** a summary
  ran but not how big the history was before and after. A reader of the telemetry
  cannot tell whether a summary helped. (Review finding R2-3 below.)

### 2.2 Elision: every round, at any depth, without a model call

After every round of tool results jichi runs `jc_compact_midturn`. It cannot
summarize, because the conversation ends in tool results the model has not
answered yet, so it **elides**: it replaces the middle of an old, large tool
result with a short marker, and since M348 writes the full text to a file the
marker names. Its tuning is six constants (`src/chat/jc_compact.c:759-764`):

```c
#define MIDTURN_HIGH_PCT    80  /* act above this % of the effective limit  */
#define MIDTURN_TARGET_PCT  60  /* elide down to this %                     */
#define MIDTURN_KEEP_RECENT 6   /* never elide the last N messages          */
#define ELIDE_MIN_BYTES     800 /* only elide tool results larger than this */
#define ELIDE_HEAD          400 /* bytes kept from the start                */
#define ELIDE_TAIL          200 /* bytes kept from the end                  */
```

**Read them as the mechanism's limits:**

- Only results **larger than 800 bytes** can be elided. A history of a thousand
  small results (`git status`, a 20-line read, a passing test) has nothing to cut.
- The **last six messages** are never touched, so the newest work is safe.
- An elided result shrinks to about 660 bytes, which is under the 800-byte floor,
  so it can never be elided a second time. Each result can give back its bytes
  once.

A pass that runs because the prompt crossed 80% is called **pressed**. When it
cannot get back under 60% it is **short**, and when it ends still over 80% it is
**unrelieved** (COMPACTION.md, "Reading a `compact` event").

### 2.3 What the model is told

On a pressed pass jichi also adds one message to the conversation, the
`[context]` note (`src/chat/jc_compact.c:287-317`, abridged to its two texts):

```c
    jc_sb_append_fmt(out,
                     "[context] this turn has used ~%ld%% of the ~%ld-token "
                     "context budget. ",
                     pct, limit);
    if (reached) {
        jc_sb_append(out,
                     "Older tool output is being elided to make room. Prefer "
                     "read_file with offset/limit and search_code over "
                     "whole-file reads, and do not re-read files that have "
                     "not changed.");
    } else {
        jc_sb_append(out,
                     "Eliding old tool output could NOT bring it back under "
                     "the target, so upcoming requests may exceed the "
                     "window. Finish the current step and write durable "
                     "results now; use offset/limit reads and search_code "
                     "from here on.");
    }
```

Until M900 (2026-10-06) the note was sent **once per agent loop**. Since M900 it
is sent on **every** pressed round. Keep the date in mind: the drives below ran
before M900, the experiments in §5 after it.

**What the note does not do** is give the model a way out. "Write durable results
now" tells the model to save its work, but nothing ends the session or starts a
fresh one. The model must either carry on in a full window or stop and answer, and
stopping and answering ends the run.

---

## 3. How the measurement was made

**The data.** Two drives in which a lead agent delegated work to parallel
workers on the zigodot project, all headless, all on free models:

| Drive | Dates | jichi builds | Runs | Agent loops |
|---|---|---|---|---|
| the weekend drive | 2026-09-25 to 09-28 | `b5280d5d` | 248 | 286 |
| the demos drive | 2026-09-28 to 10-03 | M829 `d630c2c2`, M851 `da680c3b`, M871 `66c9678b` | 118 | 329 |

Each run directory holds a `telemetry.jsonl`. An **agent loop** is one agent's
conversation: the lead of a run, or one worker task. A loop is identified by its
session id, its depth (0 for the lead, 1 for a worker) and its `task_run`.

**The prompt size** of a call is what the server counted. jichi records the
cached and the uncached part separately, so the size is their sum:

```python
# the heart of the per-loop measurement (Python, reads jichi's telemetry)
def loop_key(o):
    return (o.get('sid'), o.get('depth', 0), o.get('task_run'))

if o['event'] == 'model_call' and o.get('ok'):
    prompt = ((o.get('in_tok') or 0)          # the uncached part
              + (o.get('cache_read_in') or 0)  # served from the prompt cache
              + (o.get('cache_write_in') or 0))
```

The `compact` events of the same loop give `pressed`, `short`, `unrelieved`,
`noticed` (the note was sent), `before` and `after`, and the `tool_call` events
count the tool calls between them. The scripts that did this are kept with the
project's internal drive records; the excerpt above is the part you need to run
the same analysis on your own `~/.jichi.d/telemetry` (§7).

**What the numbers are not.** Two drives on one project, with two models and four
builds, are not a population of all jichi use. Medians and 90th percentiles are
given so a reader can see the spread. Where a number would need a control group
the corpus does not have, the text says so.

---

## 4. What the drives show

### 4.1 The summary never ran

**All 7,818 `compact` events in the 366 runs are mid-turn elisions. Not one is a
between-turn summary.** That is §2.1 working exactly as written: every run was a
single headless turn.

Threadwork's own telemetry from interactive and headless use, 93 files, shows the
same order of magnitude in ordinary use: **6** between-turn summaries against
**1,086** mid-turn passes.

So for long unattended work, "autocompact" in practice means **elision only**.
The summarizing compaction that COMPACTION.md opens with is an interactive
feature.

### 4.2 The lead: a sawtooth under the high-water mark

Of 95 lead loops on `jlu/qwen3.8-27b` in the demos drive, **74** had at least one
pressed pass and **73** had a short one. The first pressed pass came at a median
of tool call **67**. After it, the lead made a median of **18** more tool calls
(90th percentile 86, maximum 138), and then ended its run.

One lead loop, run 0040, drawn from its telemetry. Each column covers about one
and a half model calls; `*` is the largest prompt in the column and `o` the
smallest; `P` marks a pressed pass:

```
 256k |
 240k |
 224k |
 208k |========================================================================
 192k |
 176k |
 160k |--------------------------------------*****-*--*------------------------
 144k |                      ****************o     o**     ********************
 128k |.....................*o....................*..o.****....................
 112k |        *************
  96k |
  80k |
  64k |********
  48k |
  32k |o
  16k |
      +------------------------------------------------------------------------
                             P                    PP PPP
       model calls 1..111 (each column ~1.5 calls); P = a pressed mid-turn pass
       declared limit 196608 (=), high-water 80% (-), target 60% (.); real window 256000
```

**In words:** the prompt climbs to about 140k tokens within the first thirty
calls, then stays between the 60% target (118k) and the 80% high-water mark
(157k) for the remaining eighty. Each pressed pass pulls it down a little and the
next tool results push it back up. It never comes near the real window, and it
never comes back down to where a fresh session starts (25k, §4.4).

**This is the operator's observation, measured:** the model keeps working in
what is left of the window, and elision keeps that space just large enough for it
to do so.

**One claim in COMPACTION.md does not hold here.** COMPACTION.md says the reclaim
of pressed passes "decays within a turn" (median 10,324 tokens on the first pass,
1,306 on the second, about 0 after), measured on an earlier workload of 593
passes. In the demos leads the first six pressed passes reclaimed a median of
**35k, 43k, 41k, 43k, 44k and 27k** tokens. The difference is not explained here;
both measurements stand for their own workloads, and the page should say which
workload its numbers come from.

### 4.3 The workers: long tails, and the wall

Workers run far longer than leads, and they keep going after the first pressure:

| Workers | Loops | Pressed | Tool calls after the first pressed pass (median / p90 / max) |
|---|---|---|---|
| demos, `jlu/qwen3-coder-next` | 169 | 74 | 195 / 879 / 3,775 |
| weekend, `jlu/qwen3.8-27b` | 38 | 16 | 466 / 1,279 / 4,085 |

**Five demos workers and two weekend leads ended on an HTTP 400** from the
gateway: *"This model's maximum context length is 256000 tokens. However, you
requested 16384 output tokens and your prompt contains at least 239617 input
tokens"*. jichi sends the same `max_tokens` whatever room is left, which is
recorded as def-303 in [DEFERRED.md](../DEFERRED.md).

One of them, worker `a-t2` of run 0043, drawn the same way (each column is about
24 calls):

```
 256k |
 240k |                                                                  ******
 224k |                                                            ******o
 208k |=====================================================*******============
 192k |                                               ******o
 176k |                                         ******o
 160k |----------***--***-**-*********----******o------------------------------
 144k |        **o  **o  *o *o oo oo o****o
 128k |......**o...oo...o..o...................................................
 112k |    **o
  96k |
  80k |
  64k |  *
  48k | *o
  32k |*o
  16k |o
      +------------------------------------------------------------------------
                   P    P  P   P  P  P         PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP
       model calls 1..1745 (each column ~24.2 calls); P = a pressed mid-turn pass
       declared limit 196608 (=), high-water 80% (-), target 60% (.); real window 256000
```

**In words:** for the first 700 or so calls this worker shows the lead's
sawtooth: pressed passes hold it near the high-water mark. Then there is nothing
large left to elide. From about call 1,000 every column has a pressed pass, every
pass is unrelieved, and the prompt climbs straight through the declared limit
(208k row) to 240k, where the gateway refuses the request. The worker did
**about 750 tool calls after elision had stopped helping**, and lost the last
reply.

### 4.4 The cost: elision breaks the prompt cache

An elision changes a message in the middle of the conversation, so the prompt
cache can only serve the part before it. Measured on the call right after a
pressed pass that elided something:

| Model, role | Calls | Prompt (median) | Uncached tokens (median) |
|---|---|---|---|
| `qwen3.8-27b`, lead, the call after an eliding pass | 305 | 134,197 | 107,527 |
| `qwen3.8-27b`, lead, the first call of a fresh loop | 162 | 25,493 | 14,668 |
| `qwen3-coder-next`, lead, after an eliding pass | 101 | 131,395 | 98,681 |
| `qwen3-coder-next`, lead, the first call of a fresh loop | 188 | 20,272 | 159 |

For a lead, **one call after an eliding pass re-processes about seven times as
many tokens as the first call of a fresh session**. For workers the same
comparison does not show it: their post-elision calls kept a median of 2,300
uncached tokens, so their elisions mostly left the cached prefix intact. Why the
two differ is not measured here.

Latency follows. For `qwen3.8-27b`, all roles together (728 calls after an eliding
pass against 19,207 other calls above 100k tokens): median **7.0 s** against
3.2 s, 90th percentile **50.7 s** against 22.6 s.

### 4.5 A quality signal, with a caution

The share of tool calls that failed (`ok: false`: a refused call, a bad argument,
a command that exited non-zero), grouped by the size of the prompt the model had
just read:

| Model | below 50k | 50k–100k | 100k–150k | above 150k |
|---|---|---|---|---|
| `qwen3-coder-next` (69,892 calls) | 6.2% | 7.0% | 7.8% | **20.8%** |
| `qwen3.8-27b` (34,727 calls) | 5.5% | 6.1% | 5.6% | 6.1% |

**What this does and does not say.**

- It does not show that a long prompt *causes* failures. Late in a loop the work
  is different too: more tests, more edits.
- It does show a difference between the two models. `qwen3-coder-next` fails
  three times as often above 150k. `qwen3.8-27b` does not: it keeps its failure
  rate in a nearly full window. It copes rather than degrades, which is the
  behaviour the operator described.
- One cause of the jump is countable. Before M909, an elided `write_file` left a
  placeholder in the history, and models copied it back as if it were the real
  arguments. The demos workers did that **797** times, the weekend workers 48
  times. M909 replaced the placeholder with a claim ticket on 2026-10-07.

---

## 5. Three small experiments with `jlu/qwen3.8-27b`

**Why experiments at all.** The drives show what happened, not what the model
does when it is told clearly what to do under pressure. That is what a design
needs to know.

**The setup** (run on threadwork on 2026-10-08 with jichi `17bbb895`, which
sends the `[context]` note on every pressed round):

- a workspace with 80 small files, each holding an 8-character key;
- the task: read the files in order and append `item-NN: KEY` to `RESULTS.md`,
  one file per step, no shell, no search;
- `contextLength` declared as **16,384**, so pressure comes after about thirty
  items, while the real window stays 256,000 and nothing fails;
- headless, `--auto`, no deadline, no token budget, no timeout (a measurement run
  keeps its caps off), the core tool profile;
- a key sheet outside the workspace scores every line: correct, wrong, missing,
  duplicated.

Each result is **one run** (n = 1). It shows what can happen, not how often.

**Experiment A — no rule about pressure.**

- 83 tool calls in 199 s, then the model stopped by itself at item 39.
- Each of its 16 pressed passes elided **nothing**: every result was under 800
  bytes, so every pass was short and unrelieved.
- The model's last message was an honest status report: *"What's complete: items
  01–39 … What's incomplete: items 40–80 … The context budget has been exhausted,
  so I can't reliably continue this run."*
- jichi reported **`stop_reason: done` and exit 0** for a task that was 39 of 80
  done. A supervisor that routes on the exit code files it as finished.
- Score: 39 correct, 0 wrong, 41 missing.

**Experiment B — with a rule.** The same task plus one paragraph: *"if a message
starting with [context] reaches you, do not process another item. Instead write
HANDOFF.md with (1) the last item you appended, (2) the next item to process,
(3) anything a fresh session needs to continue correctly. Then reply with
HANDOFF and stop."*

- The model obeyed at the **first** note: 70 tool calls, 170 s, items 1–31 done,
  0 wrong.
- Its `HANDOFF.md` named the last item, the next item (with the key it had
  already read), the method that worked (anchor `edit_file` on the last line),
  and a list of pitfalls.
- One pitfall was its own mistake: it had **started a handoff twice too early**,
  at items 25 and 27, before any note arrived, and corrected it when the real note
  came.

**Experiment C — fresh sessions continue from the handoff, by hand.** Each leg is
a new `jichi -p` run in B's workspace, with the task text, *"Read HANDOFF.md
first, then RESULTS.md, and continue from the next unprocessed item"*, and the
same rule:

| Leg | Items done | Tool calls | Seconds | First prompt | Why it ended |
|---|---|---|---|---|---|
| 1 (that is experiment B) | 1–31 | 70 | 170 | 2,396 | at the first real `[context]` note |
| 2 | 32–60 | 61 | 140 | 2,437 | at the first real `[context]` note |
| 3 | 61–74 | 35 | 144 | 2,437 | **no note had arrived**: see below |
| 4 | 75–80 | 15 | 37 | 2,437 | the task was done |

- **The result: 80 of 80 correct**, none wrong, missing or duplicated, in four
  sessions that each started at about 2,400 tokens. Experiment A, one session
  with no handover, ended at 39 of 80.
- **Continuity held.** Each leg resumed at exactly the item its predecessor
  named, and never re-created `RESULTS.md`, although the task text still says
  "create RESULTS.md before the first item".
- **The model invented the trigger.** Leg 3 handed over with no `[context]`
  note in its conversation: its largest prompt was 12,576 tokens, under the
  13,107-token high-water mark. Its session shows the assistant saying a
  `[context]` message had arrived **twelve times** while none had. Once it
  caught itself (*"I keep claiming a `[context]` message arrived, but I have now
  checked the actual conversation multiple times and no such message has ever
  been sent"*), and a few calls later it claimed it again and handed over. Leg 1
  did the same twice before its real note came. **Two of the three legs that
  carried the rule claimed a signal that had not come.**
- One self-report was wrong where the file was right: leg 2's final message
  counted "29 existing lines" where there were 31.

### 5.1 What the experiments show

- A **pressure signal that is specific** ("when X arrives, do Y") is followed by
  this model; a **general** one ("write durable results now") produced, in A, a
  sensible stop with nowhere to go.
- The model can write a **useful handoff note** when it is asked for one with a
  fixed structure, and a fresh session can **continue from it correctly** (C:
  80 of 80 against A's 39).
- A model told to *watch for* a signal **may imagine it** (C, legs 1 and 3). So
  the decision to hand over must be jichi's: jichi measures the window, and asks
  for the notes only at the moment it has decided to end the session. The model
  should never be told to wait for a trigger it can mistake.
- jichi's **stop reason cannot say "stopped early on purpose"**: a self-stopped
  partial run reads as `done`, in A and in every leg of C.

---

## 6. What this means

1. **"Autocompact doesn't seem to work" is accurate for long unattended runs,**
   for a structural reason. The mechanism that summarizes runs only between
   turns, and a headless run has no "between". The mechanism that runs mid-turn
   can only shrink large results, can shrink each one only once, and in doing so
   breaks the prompt cache.
2. **The model is told, but it has no exit.** The `[context]` note informs, and
   the leads respond within a few dozen calls by finishing. Workers go on for
   hundreds of calls, and some reach the hard wall.
3. **A fresh session is cheaper than the sawtooth.** For a lead, one call after
   an eliding pass costs about seven fresh starts in uncached tokens (§4.4), and
   the sawtooth repeats it.
4. **What is missing is a controlled handover:** end the session at a chosen
   point, keep what matters in a bounded note, and continue in a fresh session.
   §5 shows the model can take part in that if jichi decides when. That is the
   design in [the context-relay proposal](../proposals/2026-10-context-relay.md).

## 7. Something to do

**With a bench (jichi, a free model):** repeat experiment A at a small scale.
In an empty directory, with jichi configured for a free model:

```sh
mkdir -p data
for i in $(seq -w 1 40); do
  key=$(od -An -N4 -tx1 /dev/urandom | tr -d ' \n')
  { printf 'Item %s\nKEY: %s\nNotes: ' "$i" "$key"
    od -An -N180 -tx1 /dev/urandom | tr -d ' \n'; printf '\n'; } > "data/item-$i.txt"
done
jichi --auto --context-limit 8000 -p "In data/ there are 40 files, item-01.txt to item-40.txt. Process them strictly in order, one file per step: read the file with read_file, then append exactly one line item-NN: KEY (the 8 characters after KEY:) to RESULTS.md. Do not use the shell, do not search, and do not read more than one data file per step. When all 40 are done, reply DONE." < /dev/null
echo "exit code: $?"
```

Then find your telemetry file under `~/.jichi.d/telemetry/`. Count its `compact`
events, how many have `pressed` true, and how many elided anything. Read the
final answer and the exit code: does the answer say the work is incomplete? Does
the exit code?

**What it did when this chapter was written,** on threadwork on 2026-10-08, with
jichi `17bbb895` and `jlu/qwen3.8-27b`:

- 65 model calls;
- 26 pressed passes that elided **nothing**;
- the largest prompt was 13,888 tokens, against the declared 8,000 (the real
  window is far larger, so nothing failed);
- the model stopped at item 22 with *"I cannot answer DONE — the task is 22/40"*
  and a procedure for resuming;
- **the exit code was 0**.

An earlier version of this exercise, with tiny files and a looser task text,
produced *no* pressure at all: the model read several files per call and
finished in 17 calls. Running it before publishing is what showed that. A
reproduction is a measurement too.

**Without a bench (the read-only twin):** take any telemetry file you have, or
read the two charts in §4 again, and answer from the numbers alone:

1. At which call did the lead in §4.2 first cross the high-water mark? How many
   calls did it make after that?
2. In the worker chart, find the column after which every column has a `P`. What
   does that tell you about what was left to elide?
3. Using §4.4, estimate how many fresh sessions the worker in §4.3 could have
   started for the uncached tokens of its post-elision calls. (Careful: §4.4 says
   that for workers the cache mostly survived. What does that do to your
   estimate?)

## 8. Findings this chapter adds to the review

Numbered as in [Review 0](2026-10-08-review-00-overview.md); severity and status
use the review's scale.

- **R2-1 (high, verified): the summary cannot run in a headless run.** The
  between-turn check (`jc_agent.c:5216`) sees only the task text. COMPACTION.md
  says so in its "Limitations", but the opening section and Annai 8 present the
  summary as the general mechanism. Readers running long unattended work should be
  told on the first screen.
- **R2-2 (high, verified): no exit under pressure.** Nothing ends a session in a
  controlled way when elision can no longer cope. The design is the relay
  proposal.
- **R2-3 (medium, verified): the between-turn `compact` event carries no
  numbers** (`jc_agent.c:5218`). Its mid-turn sibling records `before`, `after`,
  `target` and `limit`; the summary event should too, plus the summary's size.
- **R2-4 (medium, verified, n = 1): a deliberate early stop reads as `done`.**
  In experiment A, `stop_reason: done` and exit 0 described a task left at 39 of
  80 by a model that said it was stopping because the window was full.
- **R2-5 (low, verified): a measured number in COMPACTION.md is presented as
  general.** The reclaim decay of the M326y section does not hold in the demos
  drive (§4.2).
- **R2-6 (design input, verified, n = 3 legs): a model told to wait for a
  trigger may imagine it.** In experiment C, two of three legs whose prompt said
  "if a [context] message arrives, hand over" claimed such a message had arrived
  when none had (§5). Any handover design that relies on the model noticing a
  signal inherits this; the relay proposal therefore lets jichi decide.

## 9. Read further

- [COMPACTION.md](../COMPACTION.md) — the reference for both mechanisms, with the
  history of each constant. *Primary for jichi.*
- [Annai 8](../reading/annai-08-when-the-conversation-gets-too-long.md) and
  [Fukabori 5](../reading/fukabori-05-context-economics.md) — the mechanism taught
  and argued. *Primary for jichi.*
- [analysis/2026-09-22-mid-turn-compaction-measured.md](2026-09-22-mid-turn-compaction-measured.md)
  — the earlier measurement of 313 mid-turn passes, which found the same
  shortfall from another workload. *Primary for jichi.*
- [proposals/2026-09-run-handoff.md](../proposals/2026-09-run-handoff.md) — the
  handoff between whole runs that the drives' harness implements outside jichi.
  *Primary for jichi.*
- Nelson F. Liu et al., *Lost in the Middle: How Language Models Use Long
  Contexts*, TACL 2024 — why a model may use a nearly full window less well than
  an empty one. *Secondary: a research paper about other models; nothing in it was
  measured on the models above.*
