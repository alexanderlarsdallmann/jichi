# Stress-testing an LLM server with jichi fleets

Can jichi load-test an LLM server — LM Studio, llama.cpp, vLLM, a gateway —
by scaling out headless instances and collecting the telemetry to find the
system's limits? **Yes**, and it needed no new C: jichi already had the
pieces (headless one-shots, per-instance telemetry with `latency_ms` /
`ok` / token counts on every `model_call` event, hard timeouts), and the
orchestration deliberately lives in a supervisor script rather than in the
binary — the same rule as the web front-end: no load generator in the C
core, ever. The harness is `examples/stress/` (Python stdlib, the
web-bridge precedent).

```sh
python3 examples/stress/stress.py \
    --jichi ./jichi --server http://127.0.0.1:1234/v1 --model my-model \
    --instances 8 --requests 50 --out /tmp/step-08

python3 examples/stress/report.py /tmp/step-08
```

`stress.py` spawns N jichi instances (staggered ramp-up, SIGINT-clean),
each running M requests in a throwaway workspace against the one target
server. Two artifacts: the driver's ground-truth `requests.csv` (wall time
and exit per request) and each instance's standard **telemetry JSONL** —
the same sink every jichi run writes, reused instead of inventing a format.
`report.py` merges them into latency percentiles (p50/p90/p99),
error/timeout/retry counts, requests/s, and output tokens/s.

## Finding the knee

Run the *same* request count at increasing concurrency, one out-dir per
step, then hand all steps to the reporter:

```sh
for n in 1 2 4 8 16 32; do
    python3 examples/stress/stress.py --jichi ./jichi \
        --server http://127.0.0.1:1234/v1 --model my-model \
        --instances $n --requests 32 --out /tmp/step-$(printf %02d $n)
done
python3 examples/stress/report.py /tmp/step-*
```

The knee is where **p90 bends up while req/s stops climbing** — beyond it
the server queues instead of serving. That row is your server's honest
concurrency; tune and re-run the same steps:

- **llama.cpp / LM Studio:** parallel slots (`--parallel`), context size
  per slot (total KV cache = slots × context — the usual silent limit),
  batch size (`--batch-size`), and whether continuous batching is on.
- **Gateways:** worker counts and upstream pool sizes; watch the timeout
  column — a gateway that queues past jichi's stall timeout converts
  latency into errors.
- Re-measure after every change; keep the out-dirs — they are the record.

## Request shapes (cost per request)

- `--mode turn` (default): a full agent turn (`-p`, streaming, tools
  advertised) — realistic per-request weight, what your users actually do.
- `--mode complete`: one non-streaming completion, no tools — the cheapest
  shape, for isolating pure server throughput. (Requires a server that
  answers non-streaming requests; the e2e mock only streams.)
- Prompt size is load: pass `--prompt` with a long text to test prefill;
  the default one-liner tests decode + overhead.

## Scale, honestly

1 → 100,000 **requests** is trivial — it's a loop; the harness happily runs
all night. 100,000 **concurrent instances** is not a laptop exercise: each
jichi headless process peaks ~10–16 MB RSS
([the M181 measurements](analysis/2026-07-28-footprint-comparison.md)), so
1,000 instances ≈ 10–16 GB plus file descriptors and process slots — fine
on a big host — while 100k concurrent means a cluster and, long before
that, a saturated server: **any single LLM server's knee arrives well under
512 concurrent streams**. Scale requests, bound concurrency (32–512), and
spend the excess on request count and prompt variety instead. For repeated
runs against a warm process, the [daemon](DAEMON.md) (`--connect`)
amortizes startup — though at ~30 ms and ~10 MB per cold start, jichi's
startup is rarely the bottleneck being measured.

## What else this pattern covers

The same fan-out-and-read-telemetry shape serves beyond load testing:
soak-testing a server overnight (`--requests 10000 --instances 4`),
comparing two quantizations or two servers (same steps, two `--server`
values, one report table), regression-checking a gateway config change,
and generating realistic parallel traffic while you profile the server.
For orchestrating *work* (not load) across many agents, see
[SUPERVISOR_OF_MANY.md](SUPERVISOR_OF_MANY.md) and
[AGENT_COLLABORATION.md](AGENT_COLLABORATION.md); for scoring a *config*
(zero requests), that is the `benchmark` subcommand, a different tool.

E2E: `tests/e2e/stress.py` smokes the harness (2×3 against the SSE mock;
driver CSV and telemetry report must agree).
