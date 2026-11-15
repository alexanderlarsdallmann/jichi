# The slow reviewer: 106 minutes, 179,012 output tokens, and the wrong first diagnosis

*Analysis, 2026-10-22, written at the operator's request: "document this issue honestly, and
comprehensively including recommendations for a better design, or configuration." Written by the
hardening drive's supervisor, a Claude Code session. That supervisor first named the wrong cause (§4).
Every number below comes from the run's own telemetry (`runs/0069/telemetry.jsonl`, `model_call` rows),
read with the script in §9.*

## 1. In one paragraph

In run 0069 the lead dispatched a second-model review of the M1042 fix (`spawn_parallel` call 45,
model `jlu/qwen3.8-27b`, read-only), beside a teeth probe. The teeth probe finished in minutes. The
review took 106 minutes, and the lead could do nothing else meanwhile. Its tree held an uncommitted
worker report for over 100 minutes, and the one-word fix for a red gate waited behind it. The review's
model calls grew from 5 seconds to 15 minutes each, and one reply hit the 32,768-token output ceiling
with reasoning and no answer. The supervisor first blamed a growing context. The telemetry shows
otherwise. Prompt processing stayed at 1 to 4 seconds throughout, because the context was cached. The
time went to output: 179,012 output tokens, mostly reasoning, at about 25 tokens per second. The review
finished on its own, approved the fix, and found one real medium-severity issue. The operator's word to
end it arrived after it had finished.

## 2. What happened

| time | event |
|---|---|
| 16:14 | the lead commits M1042 (`f2a81ec4`) and keeps a worker's report uncommitted |
| about 16:16 | call 45: [0] a review on qwen27b, read-only; [1] a teeth probe on coder-next, writable |
| 16:16-16:19 | the teeth probe makes its 36 model calls (median 4 s each) |
| 16:50 | the watch's 35-minute alert: one uncommitted path, the worker report |
| 17:35 | the supervisor tells the lead that the review is slow "because it reads whole files and its context keeps growing" (wrong, §4) |
| 17:51 | stderr: "model returned reasoning but no answer text, and the reply hit the OUTPUT CEILING" |
| 17:55 | the supervisor asks the operator whether to end the review worker |
| 18:02:42 | call 45 returns: "Verdict: APPROVED (code correct) -- with one MED finding that falsifies a stated invariant (F1)" |
| 18:14 | the operator's "end the slow reviewer" is acted on: the worker is already gone; no signal is sent |

## 3. Where the time went

A selection of the reviewer's 36 `model_call` rows (the script in §9 prints all of them):

| time | history tokens | new input tokens | output tokens | time to first token (s) | total (s) |
|---|---|---|---|---|---|
| 16:16:50 | 1,175 | 3,740 | 517 | 1 | 18 |
| 16:17:54 | 14,025 | 9,989 | 285 | 2 | 11 |
| 16:23:52 | 27,639 | 2,147 | 521 | 1 | 17 |
| 16:41:46 | 32,367 | 2,990 | 10,866 | 2 | 668 |
| 16:54:14 | 37,144 | 2,703 | 15,568 | 1 | 624 |
| 17:19:38 | 45,107 | 4,389 | 15,858 | 1 | 604 |
| 17:51:27 | 58,528 | 2,816 | 32,768 | 3 | 917 |
| 18:02:42 | 61,700 | 3,233 | 17,790 | 2 | 444 |

What the table shows:
- **The context did grow,** from about 1,000 to about 62,000 history tokens. But the server read it from
  its cache (`cache_read_in` up to 92,512), so the time to the first token stayed at 1 to 4 seconds.
  Prompt processing was never the cost.
- **The output grew per turn,** from a few hundred tokens to 10,000-33,000 tokens. Most of it was
  reasoning: the 17:51 reply was reasoning only, cut at the 32,768-token ceiling.
- **Generation ran at about 16-40 tokens per second.** 179,012 output tokens took 6,368 seconds of model
  time: the whole 106 minutes of the batch.

What the table does not show: why each turn reasoned longer than the one before. The review read more
code as it went, so there was more to reason about. That is plausible, but this data cannot separate it
from the model's own tendencies. The server's load at the time is also unmeasured.

## 4. What the supervisor got wrong, and what it got right

- **Wrong: the cause.** At 17:35 the supervisor told the lead the calls were slow "because it reads whole
  files and its context keeps growing, so every call re-processes all of it". The fields that disprove
  it (`ttft_ms`, `in_tok`, `out_tok`) were in the telemetry the whole time. The diagnosis fitted the
  visible symptom (the review read big files) and was not checked.
- **Right: slow, not stuck.** At 17:15 and 17:55 the supervisor checked the process tree (one worker, no
  shell children) and the gateway connection (established), and saw model calls still completing.
- **Would have been costly: ending it.** At 17:55 the supervisor offered to end the worker. Had that
  happened before 18:02, the review's one real finding (F1, medium severity) would have been lost. The
  review was slow and also useful.
- **Right: the order.** The lead had dispatched the review before committing everything, against its
  own practice 2 ("the audit follows the commit so that verified work never waits uncommitted behind a
  review"). That is why a slow review blocked the tree.

## 5. What jichi offers today

By reading the tree at `8bee4fcd` and the `jichi control` help:
- **`maxTokens` per model** bounds one reply (here 32,768). When a reply is cut, `continueCutReply`
  asks the model once for the rest.
- **`timeouts.deadStream`** (900 s) ends a stream that delivers nothing. A slow stream that is still
  delivering tokens is not dead, so it does not fire. That is correct for this case.
- **`jichi control <sock>`** offers `status`, `inject`, `pause`, `resume`, `abort` and `mode`. `abort` acts
  on the run as a whole. There is no verb that stops one worker of a batch. The only other way is a signal
  to its process.
- **No reasoning budget.** jichi reads the `reasoning` field of a response, but it sends no reasoning-
  effort or thinking-budget parameter with a request (no such field anywhere in `src/`). `maxTokens` is the only bound on how long a model
  may think.
- **`spawn_parallel` blocks the lead** until every task returns. The non-blocking form is planned:
  batch 12, B1 (`PLAN.md` §3).

## 6. Recommendations

In order of cost. None of them is a cap on a measurement, and the reason is in item 6.

1. **Commit before the review (policy, now).** The lead prompt says it already (practice 2). Make it
   mechanical: the lead's dispatch of a review task refuses, or the watch alerts, while the tree has
   uncommitted changes older than a few minutes. Cost: none. It would have kept the report committed
   and the gate fix moving.
2. **Non-blocking reviews (jichi, planned).** Batch 12's B1 runs `spawn_parallel` in the background, so the
   lead can commit, fix the red gate, and read the review when it lands. This removes the blocking
   itself, whatever the review's speed.
3. **The right model for the role (configuration, measure first).** A mechanical hunk audit does not need
   a reasoning model. In the same batch, coder-next answered 36 calls in about 3 minutes, though for a
   different task. The comparison is not controlled. The drive's register already holds the question
   (`DEFERRED.md` def-331: does a second model catch different mistakes?). Measure: the same review
   tasks on both models, counting findings found and false findings. Then choose per role.
4. **A per-role output budget (configuration).** A reviewer entry in the config with a lower `maxTokens`
   (for example 8,192) bounds each turn's thinking, and `continueCutReply` asks once for the rest. The
   trade: a cut reasoning chain may lose a finding. Measure on recorded review tasks before adopting.
5. **A reasoning-effort setting (jichi, design).** Where the server supports a thinking budget or effort
   level, a per-model setting that jichi sends with the request bounds thinking directly. jichi has none
   today (§5). This is a design proposal; it needs a red-first test against a server that honours it.
6. **Make it visible, then let a person decide (harness).** A watch alert when a worker's replies exceed,
   say, 10,000 output tokens three times running, or when a batch has blocked the lead for more than
   30 minutes. It should not be a cap that ends the worker. A cap would have killed this review before
   it found F1. The drive's rule on caps and fences applies: bound blast radius automatically, and leave
   duration to a person with the evidence.
7. **A clean way to stop one task (jichi).** `jichi control <sock> stop-task <n>`, which ends one worker,
   records it as stopped, and lets the batch return what it has. Today the only ways are `abort` (the
   whole run) or a signal to a process found by hand.
8. **Scope the review (prompt).** A hunk audit that reads `git show <commit>` plus the lines it needs
   (practice 2) gives the model less to reason about. This is expected, not measured; measure it with
   item 3.

## 7. What the drive does now

- The supervisor relayed the correction to the lead (§4). The two standing points: commit before
  dispatching a review, and a hunk audit for the next one.
- The new watch alert in item 6, and the jichi changes in items 2, 5 and 7, need the operator's word.
  They are not made.

## 8. What the episode teaches

1. **Read the fields you already collect before naming a cause.** Time to first token separates prompt
   processing from generation; the output token count separates thinking from waiting.
2. **Slow is not stuck, and stuck is not useless.** Check the process, the connection, and whether calls
   still complete. Then ask what the slow work is producing.
3. **A cap would have hidden the finding.** A reviewer that thinks for a long time is not a failure to
   cut short; a lead that cannot work meanwhile is the problem. Solve the blocking first.
4. **An order rule exists for a reason.** "Commit, then review" looked like ceremony until a review took
   106 minutes.

## 9. Try it yourself

The script reads a run's `telemetry.jsonl`, which jichi writes with `--log-level full`, and prints one
task's model calls. Save it as `reviewer_calls.py` and run it from the drive directory as
`python3 -I reviewer_calls.py runs/0069/telemetry.jsonl b3-t1`:

```python
import json
import sys
import time

# Usage: python3 -I reviewer_calls.py <telemetry.jsonl> <task_run substring>
tel, task = sys.argv[1], sys.argv[2]
rows = []
for line in open(tel, encoding='utf-8', errors='replace'):
    try:
        row = json.loads(line)
    except ValueError:
        continue
    if row.get('event') == 'model_call' and task in str(row.get('task_run')):
        rows.append(row)
print('time      hist_tok  in_tok  out_tok  ttft_s  total_s')
for r in rows:
    print('%s  %8s  %6s  %7s  %6s  %7s' % (
        time.strftime('%H:%M:%S', time.localtime(float(r['ts']))),
        r.get('hist_tok'), r.get('in_tok'), r.get('out_tok'),
        round(r['ttft_ms'] / 1000) if r.get('ttft_ms') is not None else '-',
        round((r.get('latency_ms') or 0) / 1000)))
out = sum(r.get('out_tok') or 0 for r in rows)
secs = sum(r.get('latency_ms') or 0 for r in rows) / 1000
print('calls %d, output tokens %d, model time %d s' % (len(rows), out, round(secs)))
```

On the drive host it printed 37 lines of table and ended with
`calls 36, output tokens 179012, model time 6368 s`.

Questions to think through:
1. Which single column would have told the supervisor at 17:35 that the context was not the cost?
2. Item 4 trades thinking for speed. How would you measure whether an 8,192-token budget loses findings?
3. Why is "the batch blocked the lead for 30 minutes" a better alert than "a model call took 10 minutes"?

## Sources

- `runs/0069/telemetry.jsonl` and `runs/0069/stderr.log` of the hardening drive (on the drive host, not
  in this tree).
- The supervision log, 2026-10-22 16:50 to 18:2x.
- `docs/internal/drive/2026-10-17-hardening/harness/lead-prompt.md`, practice 2.
- `docs/internal/drive/2026-10-17-hardening/PLAN.md` §3, batch 12 (B1).
- `docs/DEFERRED.md` def-331.
