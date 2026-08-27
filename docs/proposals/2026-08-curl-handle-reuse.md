# Proposal: reuse one libcurl easy handle per process (connection keep-alive)

**Status: IMPLEMENTED (2026-08, M227).** Written from the same telemetry
that drove M218: ~9,400 model calls in one log, every one paying a fresh
TCP + TLS handshake, alongside a 25% transport-level (status 0) failure
rate that the retry ladder then absorbed.

> **Implementation + verification note.** Landed as designed: one
> pid-stamped cached handle in `src/net/jc_http.c` (`http_handle_acquire`
> resets-and-reuses when this process owns it, a forked child abandons the
> inherited handle without cleanup and makes its own), reuse only on a
> clean transfer (`http_handle_release(curl, rc == CURLE_OK)` — a poisoned
> connection after an error is dropped, not reused), teardown in
> `jc_http_global_cleanup`. Verified: WERROR build clean; 9,619 unit
> checks; the HTTP smoke drivers (`headless_tool`, `output_json`,
> `websearch`) green; the **fork-safety** drivers (`parallel_merge`,
> `parallel_hang`, `parallel_abort`) green — a forked child making HTTP
> would corrupt the parent's connection if the pid guard were wrong;
> **valgrind 0 errors** over a real request/response cycle exercising
> acquire → release → cleanup. The one thing NOT verified here is the
> *benefit*: the smoke mock sends `Connection: close`, so reuse is a
> transparent no-op in tests (correctly). The latency/error-rate A/B
> against a keep-alive server stays a measurement-tier follow-up
> (`soak.py --profile retry`, a live bench run) — the change is safe and
> a no-op where the server closes; the payoff shows only where it does
> not.

## Today

`jc_http_stream` / `jc_http_perform` call `curl_easy_init` +
`curl_easy_cleanup` per request (src/net/jc_http.c). That is jichi's
documented fork-safety story: `spawn_parallel` children, MCP stdio spawns,
and the envelope verifier can fork at any moment, and a handle that never
crosses a request boundary can never be shared across a fork.

## What reuse would buy

- **No handshake per call.** libcurl keeps the connection alive inside the
  easy handle's connection cache; on this workload that is thousands of
  avoided TLS handshakes against the same host — latency (hundreds of ms
  each on a loaded server) and server-side accept pressure.
- **Plausibly fewer transient failures.** The observed status-0 errors are
  consistent with connect/handshake-stage failures on a busy backend; a
  kept-alive connection skips that stage entirely. (Not provable from this
  log — worth measuring, which is exactly what the M219 `JC_FAULT_NET` +
  `soak.py --profile retry` instruments now make possible.)
- **Less allocator churn** feeding the M218 heap-high-water vector (each
  init/cleanup cycle allocates and frees the handle + TLS session state).

## Design sketch

1. One cached `CURL *` on a per-process singleton in `jc_http.c`, created
   lazily, `curl_easy_reset` before each use (reset clears options but
   keeps the connection cache — the entire point).
2. **Fork safety by pid check:** stamp the cache with `getpid()`; a user of
   the cache whose pid differs (a `spawn_parallel` child) abandons the
   inherited handle WITHOUT cleanup (cleaning up would send TLS
   close-notify on the parent's fd) and creates its own. The parent's
   handle is never touched post-fork.
3. All per-request options are already funneled through `apply_common`, so
   reset+reapply is a small, auditable change; the M20e body-ownership
   contract is per-request state and unaffected.
4. Teardown: one `curl_easy_cleanup` at process exit (or leave it to the
   OS, matching the current handle-per-request behavior on abort paths).

## Risks / why it waited

- The fork interaction is subtle exactly where jichi forks most
  (`spawn_parallel` write children under load); a wrong pid-cache
  interaction corrupts the parent's live connection.
- Long-lived connections change failure modes: a server that silently
  drops idle connections turns the *first* call after a pause into the
  flaky one — the retry ladder covers this, but the telemetry signature
  changes and dashboards/expectations should change knowingly.
- The M218 wave was already large; this is a behavior change to the wire
  layer and deserves its own A/B (latency, status-0 rate, RSS) via
  `soak.py --profile retry` and a live bench run.

## Acceptance criteria (when implemented)

- `make ci` green including the fork-heavy smoke drivers
  (`parallel_merge`, `supervisor`).
- A soak A/B showing per-call latency down and no RSS regression.
- A `faults_net.sh`-style driver proving a mid-stream connection death
  still classifies as transient and retries cleanly on a fresh connection.
  > **Met (M269):** `tests/smoke/faults_net_midstream.sh`, on a new fault site
  > (`JICHI_FAULT_NET_MID_AT` / `_BYTES`, `include/jc_fault.h`) that kills the
  > n-th transfer from inside `write_stream` — status and headers already
  > received, connection warm — which curl reports as `CURLE_WRITE_ERROR` with
  > `aborted` clear. Proven both ways: neutering the fault site reds 3 of 5
  > checks, and classifying the error as `JC_ERR_ABORTED` instead of via
  > `jc_http_classify` reds all 5. Honest limit: mockmodel answers
  > `Connection: close`, so warm-socket reuse itself is a no-op in the driver —
  > what it pins is the classification, the poisoned-handle drop, and the clean
  > fresh-connection retry. The latency/status-0 A/B above is still unrun.
