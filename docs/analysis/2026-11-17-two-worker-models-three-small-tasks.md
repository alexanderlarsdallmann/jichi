# Two worker models, three small tasks: what the numbers say, and what they do not

*2026-11-17. A measurement made during the harness-port drive (2026-11-05 onwards), written for anyone
who has to choose a model for agent work and wants to see how such a comparison goes in practice,
mistakes included. The scripts, the task texts and every number's source file are in the private
repository's `docs/internal/2026-11-17-worker-model-ab/`.*

## 1. The question

In the drive, a lead agent hands small, well-specified tasks to **worker** agents and checks what they
return. The workers ran on `jlu/qwen3-coder-next`. The operator asked whether a faster model could take
its place. The model they had in mind is not offered by the gateway the drive uses. The nearest is
`jlu/qwen3.8-27b-fast`:
- the gateway names the same upstream model for it as for `jlu/qwen3.8-27b`, the lead's model, served
  from a second deployment on the same GPU host;
- like every `jlu/*` id it costs nothing per token;
- it accepts up to 229,376 input tokens.

So the question was: **on the drive's own worker tasks, does `jlu/qwen3.8-27b-fast` do the work better,
faster or more cheaply than `jlu/qwen3-coder-next`?**

## 2. What was compared

Three real worker tasks from one of the drive's runs (2026-11-16), all on the same base commit. Each
task text names the files the worker owns, the change, the checks to run and the exact commit to make,
and asks for a short written report committed with the work.

| Task | The change | The checks it names |
|---|---|---|
| W-A | add one line to a C table: classify one config key for the trust step | a lint over that table, the unit suite |
| W-B | shorten one row of a documentation table below 200 characters, the detail moving to the text below | the table-row lint, an `awk` count |
| W-C | rewrite a code comment that justified a check by a state the code cannot reach | build at zero warnings, the unit suite, comment lines only |

Each model ran each task once, in a fresh clone:
- with the drive's configuration, minus model routing and the drive's verify step;
- with the same jichi build the drive's workers had used;
- headless (`-p`, `--auto`), with the drive's fences on: the task's files as `--edit-scope`, the path
  fence, `--max-tool-calls 400`;
- with **no caps**: no deadline, no token budget, stall and request timeouts off.

The two models ran the same task **side by side**, started in the same second, so both met the gateway
at the same moment. W-C ran on 2026-11-16 at 21:11, W-A and W-B the next morning at 02:28 and 02:31.

The command, from the script that ran it:

```
HOME=$R/home TMPDIR=/tmp/jm-$t-$v nice -n 19 "$PIN" -p "$(cat $R/task.txt)" --config $R/config.json --model "$model" \
    --auto --no-route \
    --edit-scope "$scope" \
    --edit-scope "$report" \
    --max-tool-calls 400 --timeout-stall 0 --timeout-request 0 \
    --log $R/telemetry.jsonl --log-level full --journal $R/journal.jsonl \
    --output jsonl --no-session < /dev/null > $R/out.jsonl 2> $R/stderr.log
```

## 3. Results: speed and tokens

Every number comes from the run's own telemetry: one `model_call` row per request to the model, one
`tool_call` row per tool. "coder" is `jlu/qwen3-coder-next`, "fast" is `jlu/qwen3.8-27b-fast`.

| | W-A coder | W-A fast | W-B coder | W-B fast | W-C coder | W-C fast |
|---|---|---|---|---|---|---|
| wall time | **68 s** | 184 s | 139 s | **122 s** | **249 s** | 449 s |
| model calls | 16 | 16 | 40 | **13** | 70 | **16** |
| tool calls (failed) | 15 (1) | 17 (1) | 39 (1) | 12 (2) | 68 (7) | 16 (2) |
| waiting for the first token | 13 s | 28 s | 42 s | 18 s | 95 s | 29 s |
| generating | 14 s | 115 s | 53 s | 76 s | 86 s | 377 s |
| running tools | 27 s | 27 s | 30 s | 1 s | 55 s | 29 s |
| generation speed, median | 152 tok/s | 84 tok/s | 144 tok/s | 88 tok/s | 145 tok/s | 85 tok/s |
| tokens generated | 2,032 | 9,455 | 7,628 | 6,473 | 12,191 | 31,458 |
| input tokens computed fresh | 43,359 | 128,469 | 67,513 | 93,181 | 90,653 | 105,247 |
| input tokens served from the cache | 532,576 | 526,400 | 1,897,472 | 278,400 | 3,464,192 | 379,200 |

Over the three tasks:

| | coder | fast |
|---|---|---|
| wall time | 456 s | 755 s |
| model calls | 126 | 45 |
| tokens generated | 21,851 | 47,386 |
| input computed fresh | 201,525 | 326,897 |
| input from the cache | 5,894,240 | 1,184,000 |

**How to read the time.** A model call costs a wait for the first token, then generation:

> model time ≈ calls × first-token wait + tokens generated ÷ generation speed

- coder-next makes many short calls (173 tokens each on average), so 44 to 52% of its model time is
  spent waiting for first tokens.
- 27b-fast makes few, long calls (1,053 tokens each on average) and generates at 55 to 61% of
  coder-next's speed, so 80 to 93% of its model time is generation.
- Neither is "the efficient one"; they spend time in different places.
- Where coder-next made the most calls (W-B: 40 against 13), 27b-fast finished first. Where the call
  counts were equal (W-A), coder-next was 2.7 times faster.

The generation speeds match an earlier, unrelated measurement of the same two models on the same gateway
(2026-10-04, [the chatbot speed study](2026-10-04-chatbot-speed.md)): 27b-fast 86-87 tok/s then and
84-88 now. coder-next ran 175-178 then and 144-152 now; that drop is unexplained.

## 4. Results: did the work get done?

A fast wrong answer is worth nothing, so every run was checked against **its own task text**:
- the checks the task names were re-run afterwards in each run's clone, by the drive's supervisor;
- each commit message was compared with the one the task prescribes;
- the trailers were parsed with git;
- the files changed were listed.

| Requirement | W-A coder | W-A fast | W-B coder | W-B fast | W-C coder | W-C fast |
|---|---|---|---|---|---|---|
| the change, as asked | yes | yes | yes, one clause weaker | yes | partly | yes |
| the task's checks, re-run: green | yes | yes | yes | yes | yes | yes |
| exact commit message, trailers parse | yes | yes | yes | yes | yes | yes |
| only the owned files changed | yes | yes | yes | yes | yes | yes |
| **report committed** | **no** | yes | **no** | yes | **no** (staged) | yes |
| ran the trailer check it was asked to | no | yes | yes | yes | yes | no |
| tool calls after its commit | 4 | 2 | 7 | 2 | 34 | 0 |

What the "partly" and "weaker" rows mean:
- **W-B, coder-next.** The new row says the event is emitted "per task at batch end". It drops the word
  *LAUNCHED*, which carries the rule that a task never started emits nothing. The text below the table
  still states the rule. 27b-fast kept it ("ONE per LAUNCHED task at the batch end").
- **W-C, coder-next.** The task asked to replace the wrong reason with the real one. coder-next did
  state the real one, but:
  - it deleted the sentence saying which child is killed ("only a child with NO BOUNDARY is killed");
  - it did not say why the old reason was wrong;
  - it pasted a near-copy of its new comment into the function's header comment.

  27b-fast kept the sentence and explained the old reason's error.
- **Both models, W-C.** Both added text to the function's header comment, which the task suspected of
  the same error. On inspection the header had no such error; the commit the drive accepted left it
  alone.

So, in these runs, **27b-fast finished every task completely, and coder-next finished none of them
completely**. The thing coder-next most often left out was the report. On W-A it ended with "No bugs, no
regressions, fully addresses the request."

**But.** In the drive itself, the same three tasks had been done by coder-next workers the day before. All
three committed their reports: the drive's merge applied six files, each task's file plus its report.
Those workers ran as sub-agents of the lead (`spawn_parallel`), not as top-level `-p` runs. They took 18,
13 and 22 tool calls, where the scratch runs here took 15, 39 and 68. **The same model, on the same task
text, differed by a factor of three in tool calls, and differed in whether it wrote the report at all.**

## 5. What this does and does not show

It shows:
- **27b-fast is slower per task on most tasks.** It generates more tokens, and at a lower speed. Fewer
  calls do not make up for that unless the other model loops.
- **27b-fast was more thorough in this setting.** It wrote every report. On W-A, unasked, it ran the
  lint before its edit and quoted the red, showing the check could fail. The code line its W-A report
  cites is correct.
- **In this setting, coder-next declared success without finishing.** In all three runs its final
  answer called the work complete while the report was missing or uncommitted.

It does not show:
- **How the models compare as the drive's workers.** The runs here were top-level runs. The drive runs
  its workers as sub-agents, with a different prompt, worktrees, a verify step and a merge. coder-next
  wrote its reports there, and the measurement did not reproduce that.
- **A difference larger than the noise.** There was one run per model per task, and the one case with a
  second sample (coder-next in the drive) varied threefold.
- **What 27b-fast costs on a busy host.** The lead's model, on the same GPU host, had a request in
  flight for 99-100% of every run's window. During W-A and W-B the drive's independent reviewer, on the
  same model, was running too. No run was made with that host quiet. A pool of six 27b-fast
  workers would add to the lead's load, and that was not measured.
- **Tool times on a quiet machine.** On the machine running the agents, the drive's own gate was busy:
  its test sweep during the W-A pair, its full CI during the W-B pair, and its start during the end of
  W-C. Nobody checked for it before starting. Both models met the same load, since each pair ran side by
  side, but the tool seconds above are not those of an idle machine. Model time is time spent waiting
  on the gateway, so the local load barely enters it.
- **Anything about larger work.** All three tasks are one-line or one-paragraph changes. Every first
  prompt was about 18,000 tokens. The task text is about 3.5 KB of it; the rest is the same
  preamble.

**The decision on this evidence: no swap.** What would decide it:
1. the same three tasks as one `spawn_parallel` call under the drive's own configuration, workers routed
   to each model in turn, three times each;
2. the same with the lead idle;
3. one larger task, a C fix with a test that must fail first.

## 6. A correction: the "7× fewer tokens" that wasn't

After W-C, the first summary said 27b-fast used "**7× fewer tokens**" (0.52M against 3.57M). The figure
was jichi's own end-of-run total, `tokens_used`. It was read as "work done". It is not:
- `tokens_used` adds up, over every model call, the **whole input** sent plus the tokens generated;
- an agent re-sends its whole conversation on every call, so this total grows with *calls × context
  size*;
- most of that input is served from the server's prefix cache. Of coder-next's 3.57M on W-C, 3.46M were
  cache reads.

Counted as what the server actually computed (fresh input plus generated tokens), 27b-fast used **more**
on every task:

| | W-A | W-B | W-C |
|---|---|---|---|
| coder | 45,391 | 75,141 | 102,844 |
| fast | 137,924 | 99,654 | 136,705 |

On a free gateway neither figure is a bill. If you compare models by tokens, say which tokens.

**The same trap is inside jichi.** A run's `tokens_used` counts cached input. A `spawn_parallel`
worker's reported `tokens` (the `[delegate]` summary line, the `worker_outcome` telemetry row) counts
only fresh input and output: the child's usage callback drops the cache reads. Two figures with one
name, measuring different things. This study found it; it is not yet in the defect register, and
registering it is the next step.

## 7. Lessons

1. **Name what a token count counts.** Total input with cache, fresh input, generated: they differ by
   up to 35× here (W-C coder-next, 3.57M against 103k). See §6.
2. **Split time into its parts before explaining it.** Wall time is first-token waits, generation, tool
   time and the harness's own time. "27b-fast is slow" became "27b-fast writes six times as much per
   call, at 55 to 61% of the speed", which says what would have to change.
3. **One run per arm shows a spread, not a winner.** The only repeated cell, the same model on the same
   task text, varied threefold. Plan repetitions before reading a ranking into n = 1.
4. **Measure in the conditions you will use.** A top-level run is not a sub-agent run. The behaviour
   that decided this comparison, whether the report gets written, changed with the setting.
5. **Judge against the task, not against the accepted answer, when the accepted answer came from one of
   the contestants.** The drive's accepted commits were coder-next's. Closeness to them would have
   favoured coder-next, so each run was scored against its task's own list of requirements.
6. **A command in a task text must run as written, in the agent's shell.**
   - Every task said "verify with `git log -1 --format=%(trailers)`". In `sh` that is a syntax error,
     because the parentheses are unquoted.
   - Agents that typed it as given failed 4 times.
   - Seven of the 14 failed tool calls in this study were that kind of quoting error, in `git log` and
     `git commit` lines.
   - Quote it (`'%(trailers)'`) and run every command before you hand it out.
7. **Record what you could not control, and check for what you could.** Here a busy lead model on the
   same GPU host, the time of day and the machine's load were outside the measurement's control. The
   drive's own gate running beside two pairs was not: it was visible beforehand, and nobody looked. The
   fair statement of the result includes all of them.

## 8. A defect seen along the way

jichi's `--output jsonl` stream, on the build used here, sometimes cut a text piece in the middle of a
multi-byte character, such as an em dash split into `e2` and `80 94` on two lines. Five of the six runs
did it: 28 lines that are not valid UTF-8 on their own. The defect was already registered (def-284) with
a synthetic test; its fix was written the same night. These runs are evidence that it happens in real
model streams.
