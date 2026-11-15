# When the model server goes away: fallback for long unattended runs

*Proposal: not built. Three of its four open questions were decided by the operator on 2026-10-21, and
D8 (more than one route per model) was added at the operator's word (§11). Written 2026-10-21 by the hardening drive's supervisor (a Claude Code session), during a gateway
outage, at the operator's request: "The local LM Studio with bonsai-27b is too weak to
function as a fallback in such cases, but a fallback we are going to need. So, please, document design
decisions, implications, and recommendations for such use-cases." Code references are to master
`ff154a66`. Times are local (+0200). The run's records are on the drive's records branch
(`drive/2026-10-17-hardening/records`, run 0047). The reachability probe log is on the drive's host.*

## 1. What happened

The hardening drive runs jichi on jichi for days. A lead (`jlu/qwen3.8-27b`) plans, reviews and merges.
Workers (`jlu/qwen3-coder-next`) write. Both models, and the harness's reviewer, are served by one
endpoint, the institution's gateway. On 2026-10-21 that gateway stopped answering mid-run. jichi's
telemetry and a host-side probe give this timeline:

| Time | Source | What |
|---|---|---|
| 16:54:46-16:55:20 | telemetry | the lead's read-only review subagent (depth 1) makes 7 model calls, first token after 0.26-3.97 s |
| ~16:55:23 | telemetry | the next call's first token arrives after 3.16 s; then nothing more arrives |
| 16:55:20-17:11:19 | telemetry, stream | **nothing is written** to the run's telemetry or its event stream for 16 minutes |
| 17:10:33 | harness watch | "stale" alert: neither file written for 15 min |
| 17:11:19 | telemetry | the silent call ends (`model_call` status 200, then `model_retry` "transient"), about 900 s after its last byte. That is the `deadStream` fence. So the gateway died mid-stream at about 16:56 |
| 17:12:19-17:24:30 | telemetry | retries 1-12, each "could not connect within 60s (timeouts.connect)", 60-68 s apart |
| 17:24:30 | telemetry | `spawn_subagent` returns an error to the lead |
| 17:25:30 on | telemetry | the lead's own model call (depth 0) fails the same way, retry after retry |
| 17:16:32; 17:25:01 on, every minute | host probe | `GET /v1/models` on the gateway: connection refused after 20-29 ms. The university's GitLab and web server answer from the same host |

**The end, added 2026-10-21 21:14:**
- The host probe was refused every minute until 20:55:20, and answered at 20:56:20 (HTTP 401 to the
  keyless request, connect 44 ms).
- The drive's next successful model call came at 20:56:59, with the first token after 0.8 s.
- So the outage lasted about four hours, from about 16:56 to 20:56.
- In that time 13 runs (0048-0060) started, failed every connect, and ended after exactly 860 s each.

Three more harness observations from the window:
- The review follower started reviews into the outage, because its guard looks only at the lead's
  time to first token, and an hour without calls counts as "not judged".
- Its marker, and the CI follower's, recorded failed steps as done: 37 commits counted as reviewed
  without a review.
- The watch raised "no progress for 4 hours" with no word that the model server was down.

The same refusal costs two very different amounts. From the host it is refused in 20-29 ms. Inside
the lead's container every attempt waits the full 60 s connect timeout. The cause is not measured yet;
the container's user-mode network stack is the first suspect (the run's metadata records
`network=pasta`). So twelve retries cost about 14 minutes
of a run instead of a second.

## 2. What jichi has today

- **Retries.** `maxRetries` (default 4, or 2 in low-resource mode, `jc_config.c:1302`; the drive sets
  12). The backoff doubles up to 8 s. A connect failure and a server error are both "transient", so the
  whole budget is spent against an endpoint that refuses connections.
- **Timeouts.** `timeouts.connect` (the drive: 60 s), `stall` and `request` (caps, off in the drive:
  it measures), and `deadStream` (default 900 s, a fence: it fires only when nothing arrives on a
  connected stream). `deadStream` is what ended the silent call. It is right to keep it long: a slow
  prefill sends no bytes, so a shorter fence would cut legitimate long answers.
- **A fallback chain.** A model may name a `fallback` selector (`docs/CONFIG_TUTORIAL.md` §6). When a
  model with a fallback is about to be used, jichi probes `GET {apiBase}/models` with a 2 s timeout
  (`jc_net_reachable`) and switches to the first reachable model in the chain
  (`jc_app_effective_model`, `jc_app.c:1207`; `jc_config_fallback_chain`, `jc_config.c:2414`). It is
  consulted for the active model at start-up (`main.c:18754`), at routing escalations
  (`jc_agent.c:1855`, `:1963`, `:5937`) and for role lookups (summarize, embed, rerank, transcribe).
- **The chain cannot help mid-run.** Reachability is probed once per distinct `apiBase` and cached for
  the life of the process (`model_reachable`, `jc_app.c`). No code path at `ff154a66` resets the cache. A run that
  started while the gateway was up keeps believing it is up, and retries it. The drive also configures
  no `fallback` at all. Both models share one `apiBase`, so the chain would have had nothing to switch
  to inside the gateway anyway.
- **Liveness.** `--heartbeat 60` writes a heartbeat to the event stream, but not while an in-process
  subagent runs (measured 2026-10-18: none in 22 minutes of an active review child). Telemetry is
  written only per event. A call that hangs writes nothing at any depth until it ends.

## 3. The drive's harness today

- A watch raises "stale" after 15 minutes without a write. That equals the `deadStream` window, so it
  cannot tell a model that is thinking from a server that died.
- The loop pauses for 10 minutes after three runs in a row that each end with an error **within 2
  minutes**. An outage that shows up as connect timeouts makes slow failures: each run would first
  spend its retry budget (12 retries, 60-68 s each here) before ending. So the pause would not trigger,
  and each new run would start against the dead endpoint.
- A run that ends loses the lead's context. The next run gets a handoff, a prompt-sized summary.
- A supervisor's note left for the next run is consumed when that run starts: its text goes into the
  run's prompt, and the file leaves the inbox. A run that starts during an outage consumes the notes
  and never reaches a model. Run 0048 started at 17:39:05 against the dead gateway with such a note in
  its prompt, and the next run would not have had it. The supervisor now holds notes outside the inbox
  while the gateway is down.
- A supervisor's injected note waits for the lead's next tool boundary. During an outage that boundary
  comes late: a note sent at 17:01 was applied at 17:24:30, when the failed subagent returned, 23
  minutes later.

## 4. Constraints any fallback must keep

1. **Free models only.** A key that can reach priced models does not license them. A priced fallback
   needs the operator's grant for that one run, asked before the request, so it can never be
   automatic. The candidates are another free route to the same models, or a local server.
2. **Local capacity is small.** The local server runs on one consumer GPU that also drives the display.
   The operator's rule is to ask before loading more than about 12 GB of weights. Today it offers
   `prism-ml/bonsai-27b`, `google/gemma-4-12b` (and a QAT build), `qwen/qwen3.5-9b`,
   `qwen/qwen2.5-coder-14b`, `google/gemma-4-e4b`, `llm-jp-4-8b-thinking` and an embedding model. The
   operator judged bonsai-27b too weak to stand in for the gateway's models (2026-10-21).
   `gemma-4-12b` is the model the platform rigs drive their tool-calling turns with
   (`docs/PLATFORMS.md`). That shows it calls tools natively, not that it can lead a drive.
3. **Never degrade silently.** The record must say which model did which piece of work: telemetry
   rows, the run journal, and the `Jichi-Model:` trailer of every commit.
4. **Fences stay on, caps stay off.** A fallback must not arrive as a shorter timeout. Cutting a
   silent stream because the clock ran out turns "slow" into "failed". That manufactures an answer, the
   failure `CLAUDE.md` warns about.
5. **A weaker model must not be able to land damage.** The drive's safeguards were tuned for the
   strong models: red-first tests, worker verify, a lead that reviews every diff, the supervisor's
   checks. On 2026-10-21 the strong lead still closed a real defect as "not a defect" on a hollow
   probe. A weaker model in the same seat would do that more often.

## 5. Failure classes

Each class wants a different response, so the first decision is to tell them apart:

| Class | Example | Today | Should |
|---|---|---|---|
| F1 endpoint unreachable | refused, connect timeout, DNS failure | retried as "transient" | park (or fall back), confirmed by an independent probe |
| F2 connected stream goes silent | 16:56-17:11 above | waits `deadStream`, then retries | keep the fence; if an independent probe also fails, treat as F1 at once |
| F3 server answers with an error | 5xx, 429 | retried | retry with backoff, as now |
| F4 answers, but slowly or badly | long time to first token | nothing | not an outage: measure, do not fail over |
| F5 refused credentials | 401, 403 | an error | never fail over: a configuration problem |

An independent probe is a cheap request on a new connection: `GET {apiBase}/models`. A 401 counts as
"up", because the server answered, so the probe needs no key.

## 6. Options considered

- **A. Park and resume.** On F1, or on F2 with a failing probe, stop spending retries. Keep the run and
  its context, probe at a growing interval, and resume on the same model when it answers. No loss of
  quality and no ambiguity in the record. No progress during the outage.
- **B. Role-scoped degraded continuation.** Give the local model only work that cannot land damage:
  read-only review, triage, summarising a long context, drafting log text. Never a write task, a merge
  or a "not a defect" verdict. Some progress, weaker output, and every result marked for re-check.
  The operator's decision generalises this: a fallback's access and roles are declared per fallback,
  so a strong enough fallback may also write (D5).
- **C. Fail the whole lead over to the local model.** Rejected: the operator judged bonsai-27b too weak
  for that, and constraint 5 says why a weaker lead is worse than no lead.
- **D. Fail over to a priced model automatically.** Rejected outright by constraint 1.
- **E. A second or third route to the same model.** The only fallback that keeps quality: the same
  model, the same weights, through another endpoint (a second gateway, a mirror, a self-hosted copy).
  **Kept in the design (D8).** The HRZ has no second route today (the operator, 2026-10-21). Other
  deployments may have one, and the HRZ may add one, so the mechanism must support it.
- **F. Where the logic lives.** In jichi alone, it cannot stop the harness from starting the next
  run. In the harness alone, it sees only files, and today a dead stream was invisible to files for 16
  minutes. Proposed split: jichi detects, classifies and parks. The harness decides across runs: start
  no new run, alert, resume.

## 7. Proposed decisions

Each is a separate milestone, cheapest first. The operator decided D3's default and D5's shape on
2026-10-21 (§11). Nothing is built yet.

- **D1. Re-probe instead of caching reachability forever.** On an F1 failure, mark the endpoint
  "unknown" so that the next use probes again. On its own this helps role lookups and routing
  escalations, which consult the chain on every use. It does not help the active model, whose chain
  is consulted once at start-up (`main.c:18754`). So D1 also consults the chain once after an F1 on
  the active model, and switches if a reachable fallback exists. Small, and testable with a mock
  model server that the run itself stops.
- **D2. Classify transport failures** (F1-F5) as a stable field in the `model_call` telemetry row, not
  only as free text. The harness and later analysis read the class; today they would have to parse
  "could not connect within 60s".
- **D3. Park mode for unattended runs.** A setting (name tentative, `onProviderDown`: `park` or
  `fail`; **`park` is the decided default**). When the endpoint is down, a role with a declared
  fallback (D5) moves to it, and every other role parks. Under `park`, an F1 that a probe confirms
  stops the retry counter and writes
  `provider_down` (with its time) to telemetry and the journal. jichi then probes at a growing interval
  (60 s, then up to 5 minutes), keeps writing heartbeats at every depth, and on the first answer writes
  `provider_up` with the window and retries the call. `maxRetries` still bounds F3. `park` is not a cap:
  it has no time limit of its own, and the run's own deadline, if any, still applies. Injected notes
  should still be accepted while parked.
- **D4. Probe while a stream is silent.** While a connected stream has been silent for, say, 60 s, run
  the independent probe once a minute. If it fails twice, handle the call as F1 (park or fall back)
  without waiting out `deadStream`. If it answers, keep waiting: the model may be prefilling. This
  keeps `deadStream` a fence and still notices a dead server in about two minutes instead of fifteen.
- **D5. Fallbacks that declare what they may do.** Shaped by the operator's decision (§11). A fallback
  can be another server or a local model, so what it may do depends on the fallback, not on jichi.
  Each fallback entry declares two things (names tentative):
  - its access, `"access": "read-only"` or `"read-write"`;
  - the roles or routing tiers it may serve (lead, write task, review, summarise, ...).

  A model reached through a fallback inherits every fence, and serves only the declared roles at the
  declared access. A role with no declared fallback parks. **Recommended default:** an entry that
  declares no `access` is read-only, because a missing word should never grant writes. For this
  drive's local candidates that means read-only, until a measurement says more (§9, item 4).
- **D6. Make degradation visible.** A call answered by a fallback carries `fallback_from` in its
  telemetry row, a `route` journal event names both models, and a commit made under a fallback model
  names it in `Jichi-Model:`. The lead's next prompt says which of its inputs came from a fallback.
- **D7. Harness: count classes, not durations.** The loop starts no new run while the host-side probe
  fails. It alerts once at onset and once at recovery, with the window. It keeps the lead's run alive
  if the run is parked. The stale alert names the last event the run wrote, and stops claiming a
  heartbeat that does not exist at depth 1. Notes for the next run stay in the inbox until a run's
  first model call succeeds, instead of being consumed at its start.
- **D8. Routes: more than one endpoint for the same model.** Added on the operator's word: "But we
  must include a second, or third route for use-cases nonetheless. It's just that our HRZ does not
  have a second route, today." A model may list its endpoints in order (name tentative, `routes`: a
  list of `apiBase`, each with its own key variable and timeouts). A route serves the same model, so
  failing over between routes keeps the model's identity and its full access. It is not a fallback in
  D5's sense, and needs no declaration of access. The order of use:
  1. the model's routes, in order (D1's re-probing applies to each);
  2. then its declared fallbacks (D5);
  3. then park (D3).

  Two rules carry over:
  - The record names the route that answered: telemetry and the journal get the route, the trailer
    keeps the model. Equal weights behind two endpoints is a claim, and the record must let a reader
    check it when results differ.
  - A route that bills is a priced model under constraint 1. It is never used without the operator's
    grant for that run, so it can never be an automatic route. `doctor` should check each route the
    way it checks a model: reachable, key accepted, native tool calling, and the same model id served.

## 8. Implications

- **Throughput.** Under `park` an outage costs its length plus one probe interval, and nothing more:
  the lead keeps its context, and no run is spent. Today's design costs the outage, plus at least one
  run's context, plus about 14 minutes per new run that starts during it.
- **Quality of the record.** Work done by a fallback is weaker evidence. D6 marks it, and the next
  review has to re-check it. A degraded-mode result the next strong review contradicts counts as noise,
  not as a finding.
- **Complexity and tests.** Parking adds states to the agent loop: parked, resumed, a parked call
  cancelled by the user. Each needs a driver against a mock that behaves like a dead gateway: refuses
  connections, accepts and then goes silent, and comes back. Such a mock must be checked against
  today's real data. The doctrine that a fixture behaves like the real peer applies, and here the real
  peer took 60 s to fail inside a container.
- **Resources.** A local fallback competes with the operator's own GPU use. Loading it on demand takes
  time and may fail when memory is short. A fallback that fails to load must park, not crash.
- **Cost.** Zero by construction (constraint 1).
- **The rules stay true.** No new duration cap; the existing fence `deadStream` keeps its meaning; the
  free-models rule needs no exception.

## 9. Recommendations, in order

1. **Now, harness only.** **Done in the drive's harness on 2026-10-21, with the operator's go:**
   - `gateway-up.sh` probes each endpoint from the host, keyless;
   - `run-lead.sh` waits on it before it reads the handoff or the inbox, giving up only on the STOP
     file or the drive's end;
   - the review follower starts no review while it fails, and both followers mark only finished steps;
   - the watch alerts `provider-down`, then `provider-up` with the window, and its status shows the
     gateway.

   Each part was checked by effect against a closed port, a server that appears, the STOP file, a
   passed end, and the real gateway. `jichi-harness` will carry this as `probe` (the harness plan,
   2026-10-21). The stale alert's text is still to correct.
2. **jichi: D1 and D2.** They are small, and they make the existing chain and all later analysis
   honest.
3. **jichi: D3 and D4.** They are what an unattended drive needs most: no lost context, no burned
   runs, and a dead server noticed in minutes.
4. **Measure each fallback for the access you want to give it** before declaring it (D5). For
   read-only roles, use the drive's own review tasks: give a candidate the inputs of past reviews with
   known findings. Count the findings it recovers and the false findings it adds. Read-write needs
   more: the drive's worker tasks with known fixes, judged by the same red-first tests the drive
   uses. The VRAM rule applies to local candidates (ask before 12 GB).
5. **jichi: D8, D5 and D6.**
   - D8 is the routes. It is the cheaper of the two, and the existing chain machinery carries most of
     it. It is tested with two mock endpoints, one of which dies.
   - D5 and D6 let a measured fallback be declared.
6. **Collect outage data for the paging threshold** (§11, question 4): keep the host-side probe, and
   once D3 exists keep the `provider_down`/`provider_up` events, so that every outage's length is
   recorded. Make the threshold a setting with a provisional value, and let the measured distribution
   decide it.

## 10. When to drop this

- **Park mode (D3, D4):** drop it if outages turn out rare and short: measure, over several weeks of
  drives, outage minutes per drive hour from the probe logs. If an outage costs less than one run's
  context per week, plain retries are enough.
- **A declared fallback (D5, D6):** the mechanism stays; a particular fallback goes. Drop a
  candidate, or lower its declared access, when it fails recommendation 4: a read-only reviewer that
  misses most known findings or adds false ones, or a writer whose fixes fail the red-first tests.
  A reviewer that is wrong half the time costs more review than it saves.
- **Routes (D8):** the mechanism stays, as the operator decided. A particular route goes when `doctor`
  finds it serving a different model id, or when its answers measurably differ from the first
  route's on the same task.
- **Re-probing (D1):** keep it regardless. A cached "up" that is never re-checked is a defect, whatever
  else is built.

## 11. What the operator decided (2026-10-21)

The four questions this proposal asked, with the operator's answers verbatim:

1. **The default for unattended drives.** "park, and optional fallbacks: read-only, read-write,
   depending on what the fallback is." So D3's `park` is the default. A fallback is optional, and its
   access is a property of the fallback, which is how D5 now reads.
2. **Which roles may fall back.** "Whatever is suitable for the fallback solution, because it could be
   another server, a local model." So the roles are declared per fallback (D5), not fixed in jichi.
   jichi provides the mechanism; the measurement in §9 item 4 decides each declaration.
3. **A second route to the same models.** "No, there is no second route." And, a moment later: "But
   we must include a second, or third route for use-cases nonetheless. It's just that our HRZ does not
   have a second route, today." So option E stays in the design as D8. This drive cannot use it today;
   other deployments can.
4. **What outage length should page the operator.** "I don't know." **Still open.** It becomes a
   setting with a provisional value, and the outage data decides it (§9 item 6). The first data point
   is the outage of 2026-10-21 itself.

What the decisions rule out:
- a fixed list of fallback-capable roles in jichi;
- a global "fallbacks are always read-only" rule (only the missing-declaration default is read-only);
- a design that assumes one endpoint per model (D8);
- counting on a second route at the HRZ today.

`docs/DECISIONS.md` gets its row with the milestone that builds D3, D5 or D8, under that milestone's
number. The rejected alternatives are options C and D, the fixed role list, and the one-endpoint
assumption.
