# Measuring stream silence: grounding `timeouts.deadStream`

*Design note, 2026-10-23. Written at the operator's request: "write the design note, and document it
honestly". Written by the hardening drive's supervisor, a Claude Code session. Nothing here is built.
The note asks for a measurement first and a default second.*

## 1. The question

The operator asked: why is the dead-stream fence set at 15 minutes, and was that measured?

The short answer is that 900 seconds was grounded on a measurement, but on a different quantity from the
one the fence bounds. It was never measured against the longest healthy silence, and that is the
number that decides whether the fence can cut a healthy call.

## 2. What the fence does

`timeouts.deadStream` (default 900, `src/config/jc_config.c:1118`) is passed to libcurl as its
low-speed window. The limit is 1 byte per second (`JC_HTTP_STALL_LOW_SPEED`, `include/jc_http.h:44`).
`LOW_SPEED_TIME` is `min(stall, deadStream)` when the stall cap is on, and `deadStream` when it is off
(`src/net/jc_http.c:1027-1046`). libcurl aborts a transfer whose speed stays under the limit for the
whole window. In plain terms: a model call ends when it receives (almost) nothing for 900 seconds.

"Nothing arriving" has two causes:
- **A dead stream.** The server or the network has gone, and no more bytes will come.
- **A healthy silence.** Before the first token the server is reading the prompt (prefill) and sends
  no bytes. On slow hardware with a large context this can take a long time.

The fence cannot tell these apart. It is safe only while every healthy silence is shorter than its
window.

## 3. Where 900 came from

M810, 2026-09-28 (commit 5b0936f6; `docs/ROADMAP.md`, "M810 -- L2"). The measurement behind it is
experiment E0-R4 of the 2026-09-26 drive (`docs/internal/2026-09-27-preserve-agent-work-handoff.md`,
the E0-R4 row; runs 0183-0200):
- 3,086 lead calls (qwen27b): median 8 s, p99 176 s, maximum **540 s**;
- 9,375 worker calls: median 1 s, p99 21 s, maximum 331 s;
- 3 lead calls without a model id: maximum 408 s;
- one dead call ran **11,677 s** before anything stopped it.

M810 set the default to 900, "above the 540 s healthy max", and amended CLAUDE.md to call the setting a
fence: it "can only fire when nothing is arriving, so it cannot manufacture a different answer the way a
duration cap can".

## 4. What that measurement grounds, and what it does not

**Grounded.** No healthy call in that corpus would have been cut. A call cannot be silent for longer than
it lasts, and the longest one lasted 540 seconds.

**Not grounded:**
1. **The quantity.** E0-R4 measured how long whole calls took. The fence bounds the longest silence
   inside a call. Silence was never measured directly.
2. **Prefill on slow hardware.** The longest healthy silence is usually the wait before the first
   token. The time-to-first-token data in the workstation's telemetry is cut off at the stall cap: a
   call cut at 30 seconds never enters it (`docs/internal/2026-10-15-stall-default-handoff.md`, §4:
   12,192 successful calls, p99 18.5 s, maximum 35.3 s). On a RISC-V board LM Studio processed a
   12,300-token prompt at about 220 tokens per second, about 55 seconds of silence, and jichi cut it at
   36 seconds (the same handoff, §2; `include/jc_http.h`, the M959 comment). The rate fell as the context
   grew (271, 232, 217 tokens per second at 2,048, 4,096, 6,144 tokens). **An extrapolation, not a
   measurement:** at 220 tokens per second a 128,000-token prompt is about 580 seconds of silence, and at
   the falling rate more. So a healthy prefill near or above 900 seconds on such hardware is plausible.
   Nobody has measured one.
3. **The margin.** 900 against 540 is a judgment. It was not derived from a distribution, and the note
   that chose it does not say why 900 rather than 600 or 1,800.
4. **The age of the data.** The corpus is from 2026-09-26. The models have changed. On 2026-10-22 one
   reasoning reply ran 917 seconds in total (`docs/analysis/2026-10-22-the-slow-reviewer.md`, the
   17:51:27 row: 32,768 output tokens). It streamed, so it was not silent, but whole calls now exceed
   900 seconds.
5. **Keep-alive bytes.** A server that sends keep-alive lines during a long queue or prefill (some SSE
   servers send comment lines) would keep libcurl's window open, and the fence would then not fire on a
   stream that is alive but stuck. Whether the HRZ gateway or LM Studio does this is not measured.

So "it cannot manufacture a different answer" holds only as long as no healthy silence lasts 900
seconds. On slow hardware, nobody has checked.

**A correction to the supervisor's own wording.** On 2026-10-23 the supervisor reported "jichi's
dead-stream fence would end the stream after 900 seconds with nothing arriving" as if the threshold were
settled. It is the configured value, not a measured bound.

## 5. Today's data point

On 2026-10-23 at 11:22-11:23 two workers of the hardening drive (coder-next, and a qwen27b reviewer)
each started a model call that was still running at 11:42, about 19 minutes later. The fence did not
fire. Sampled from the container's network namespace with `ss -tni`, 15 seconds apart (11:42:36 and
11:42:51):
- one connection received 93,594 bytes (3,225,118 to 3,318,712), `lastrcv` 13 ms;
- the other received 93,100 bytes (9,441,824 to 9,534,924), `lastrcv` 15 ms. In the first sample
  its `lastrcv` was 9,743 ms: a gap of almost 10 seconds inside a healthy stream.

Both streams were alive, at about 6.2 KB/s each. The calls were long outputs, not silences, and the
fence was right not to fire. `bytes_received` counts the whole connection, which may carry earlier calls,
so only the differences are meaningful.

## 6. The design

**One measurement, recorded on every model call.**
- `max_gap_ms`: the longest interval between two received response bytes, including the interval from
  the end of the request upload to the first response byte. Measured in the response write callback
  (`jc_http.c`), from the same clock as `ttft_ms` (M897).
- `rx_bytes`: the response bytes received, so a reader can tell a silent call from a slow one.
- On a failed call, `last_gap_ms`: the silence that was running when the call ended. A dead stream cut
  by the fence shows `last_gap_ms` close to the window; a healthy call cut by it shows a gap that a
  successful call of the same model and context size would have survived.

All three fields go on the `model_call` row on success and on failure. A field that appears only on
failures would describe only the calls the fence already decided about (the censoring of §4.2).

**A reader in the same milestone** (`telemetry_events_lint` check 10): the `jichi telemetry` summary
prints `max_gap_ms` percentiles (p50, p99, maximum) per model, and the number of calls ended by
`timeouts.deadStream` with their `last_gap_ms`.

**A cross-check by a second route.** The `ss -tni` sampling of §5 (`bytes_received`, `lastrcv`) reads
the same silence from the kernel, not from jichi. A short script on the host samples it during a drive
and compares its longest observed gap with the `max_gap_ms` jichi reports for the same calls. Two routes
that agree are worth more than one.

## 7. The measurement plan

All measurement runs follow the rule for measurements: no caps (`stall 0`, `deadStream 0`, no
`request` timeout, no wrapper), watched by a person who ends a truly dead call by hand and records it.
1. **The drive, passively.** Once the fields exist, every drive call records them. The gateway models
   (qwen3.8-27b, qwen3-coder-next) give the first distribution within a day.
2. **Local models on slow hardware.** On the boards (VisionFive 2, Pi 500+, BPI-F3) and on the
   workstation's LM Studio, run the same task at growing context sizes (for example 8k, 32k, 64k, 128k
   tokens, as far as the model allows), each first with a cold cache. Record `ttft_ms` and `max_gap_ms`.
   This is the population the 900 seconds was never measured against.
3. **Keep-alive behaviour.** Find out whether the gateway and LM Studio send bytes during a long
   prefill. A mockmodel leg that sends a comment line every N seconds shows what libcurl's window does
   with it.

## 8. From the measurement to a default

- The default must sit above the longest healthy silence observed in every population jichi is used
  with, with a margin that is written down and justified, not chosen.
- If one number cannot serve both the gateway (seconds of silence) and a slow board (possibly many
  minutes), the default can be per provider or per model, declared in the model's configuration, with
  the global value as the fallback.
- The decision goes into `docs/DECISIONS.md`, marked PROVISIONAL until the board population is measured,
  with the measurement and the margin named.
- CLAUDE.md's sentence that the fence "cannot manufacture a different answer" is then qualified with the
  measured bound it rests on.

## 9. Until then

- The drive keeps `deadStream 900`. With the gateway's silences measured in seconds (§4.2; today's
  `lastrcv` of under 10 s), the risk there is low.
- A run on slow hardware with a large context should set `deadStream` higher, or 0 while a person
  watches, and say so in its record. Otherwise a long prefill may be cut and reported as a dead stream.

## 10. Open questions

1. Does the HRZ gateway send any bytes before the first token? Does LM Studio?
2. Is one global default enough, or does it need to be per model? The board measurement answers this.
3. Should the fence's error say how long the stream had been silent (`last_gap_ms`), so a reader can tell
   a dead stream from a long prefill without the telemetry?

## Sources

- `src/config/jc_config.c:1118`, `src/net/jc_http.c:1027-1046`, `include/jc_http.h:44` and :60-79
  (master at fc638450).
- `docs/ROADMAP.md`, "M810 -- L2"; commit 5b0936f6 (2026-09-28).
- `docs/internal/2026-09-27-preserve-agent-work-handoff.md`, the E0-R4 row.
- `docs/internal/2026-10-15-stall-default-handoff.md`, §2 and §4.
- `docs/analysis/2026-10-22-the-slow-reviewer.md`, §3.
- The hardening drive, run 0074, 2026-10-23 11:22-11:43: `runs/0074/telemetry.jsonl` and the `ss -tni`
  samples quoted in §5 (on the drive host).
