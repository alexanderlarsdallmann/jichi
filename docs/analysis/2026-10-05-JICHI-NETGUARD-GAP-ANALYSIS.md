# jichi gap analysis: the network guard / SSRF (program flow)

*Read-only survey of `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey), HEAD `1b71247d`.
Method: find the one thing the area is definitely lacking; verify every
load-bearing claim against source; flag inferences; appendix reproducible.*

## TL;DR

The one thing lacking: **a test that the SSRF guard defeats the two attacks its
own header promises — DNS-rebinding and redirect-to-internal.** The guard
(`opensocket_guard`, `jc_http.c:597`, installed via `OPENSOCKETFUNCTION` at
`:956` for `block_private_addrs` requests) is the *right* design: it runs at
**connect time**, so curl calls it "for every connection -- including after DNS
resolution and on redirects," defeating the two classic bypasses (the
pre-M190 classifier-only guard "dropped IP-literal SSRF"). But
`fetch_ssrf.sh` (M542) drives only **direct** blocked addresses —
`127.0.0.1`, `169.254.169.254`, `10.0.0.1` — and the driver's own header states
the tier *cannot* test the redirect/rebinding path (the only local HTTP server
is `mockmodel` on `127.0.0.1`, exactly what the fence blocks, and the tier bans
real network clients). So the guard's two headline security properties — the
ones that make it a connect-time guard rather than a hostname check — are
promised in the code comment, claimed to the model in the tool description
("a page can choose the next URL … turn the agent into a probe for internal
services"), and **never exercised by any test**. The gap is a red-first test
that was knowingly not written because the tier cannot reach a redirect target.

## 1. What the area claims

- **The guard, in code** (`jc_http.c:531-535`): "Connect-time SSRF guard:
  refuse a socket to any blocked address. curl calls this for **every
  connection -- including after DNS resolution and on redirects** -- so it
  defeats DNS-rebinding and redirect-to-internal." The classifier covers
  loopback, `10/8`, `172.16/12`, `192.168/16`, `169.254/16` (cloud metadata),
  and reserved — IPv4 **and** IPv6 (`::1`, `fe80::/10`, `fc00::/7`), so the
  "use the IPv6 loopback to dodge the IPv4 check" bypass is closed too.
- **The tool, to the model** (`jc_tool_fetch.c:58-61`): "jichi will not fetch
  URLs that only resolve inside this machine or network, because a **page can
  choose the next URL** and that would turn the agent into a probe for internal
  services."
- **The boundary, stated honestly** (`jc_http.h:98-104`): `block_private_addrs`
  is "Set by fetch_url on model-supplied URLs; **left 0 for operator-configured
  endpoints (providers, MCP)**." M472 separately hardened *all* requests to
  follow only http/https with a redirect hop cap, so a credentialed provider
  call can no longer be talked into handing the key to another protocol/host.

## 2. The crux: the two promised bypass-defeats are untested

Verified:

- **What is tested** (`fetch_ssrf.sh`, M542): the model asks `fetch_url` for
  `http://127.0.0.1:9`, `http://169.254.169.254/latest/meta-data/`, and
  `http://10.0.0.1/admin`; all three must be refused (checks 1–3), and a
  malformed URL must give a *different* error (check 4, the positive control).
  **These are direct, IP-literal addresses.** The driver's header is explicit
  about what it *cannot* do: "There is no positive control that fetches
  successfully: a real remote fetch is forbidden here … the one HTTP server
  available -- mockmodel -- listens on 127.0.0.1, which is exactly what the
  fence blocks."
- **What is *not* tested:** a URL that (a) resolves via DNS to a private
  address and re-resolves differently on the second connection (DNS-rebinding),
  or (b) returns a `302 Location: http://169.254.169.254/...` (redirect-to-
  internal). Both are the guard's *reason to exist at connect time*; the
  `OPENSOCKETFUNCTION` path that re-checks the resolved address is exercised by
  none of the four cases. `test_http.c` covers the *predicate*
  (`jc_net_host_is_blocked`, eight checks) and `jc_http_describe_redirect`
  (the *message* for a 302/307/308) — not the connect-time *enforcement* of a
  redirect into a blocked address.
- **Why it is the M439 shape**: `fetch_ssrf.sh` opens by quoting M439 — "the
  pure function's boundary behaviour … cannot show that main.c's path CALLS it
  -- the wiring is the other half, and the wiring is what shipped broken."
  That driver tested the *wiring* (tool calls predicate, direct address). The
  connect-time re-check is a *second* wiring — the same class, one layer
  deeper, and it has no driver.

## 3. Why "definitely lacking" (and the alternatives' status)

- *The classifier (private-range decision)* — pure, eight unit checks
  (`test_http.c`), IPv4 + IPv6. Closed.
- *The wiring (tool calls the guard)* — M542's whole reason to exist; tested
  for direct addresses. Closed.
- *Redirect protocol confinement (credentialed request talked into another
  scheme/host)* — M472: `PROTOCOLS_STR http,https` + `MAXREDIRS 5` applied to
  **every** request, not just the guarded one. Closed at the protocol level.
- *DNS-rebinding / redirect-to-internal* (a fetch that starts public and
  lands on a private address) — **the one thing not tested.** It is the
  property that distinguishes a connect-time guard from a hostname check, and
  the property the tool *claims to the model*. Lacking.

The register has already paid the cost of exactly this shape three times
(M439, M536 ×2): "the pure function is tested; the caller's path ships
broken." `fetch_ssrf.sh` is the driver that fixed the *first* layer of that
shape. The connect-time re-check is the *second* layer, and it is where the
next M536 would live.

## 4. The second, honest, *known* limit: the guard is a `fetch_url` property

`block_private_addrs` is "left 0 for operator-configured endpoints
(providers, MCP)" — a deliberate, documented boundary: the operator
configured those hosts, so they are trusted. The boundary is *correct*; what
is absent is its **reconciliation with the rest of the machine's egress**:

1. **The model's shell is not guarded at all.** `run_terminal_command`
   (and the `fetch_ssrf.sh` header's own pointer: "To read a local …" —
   the tool text redirects the model to *shelling out*) can `curl`
   `169.254.169.254` directly. The shell is the one tool the model always
   has, and the SSRF fence is one of several *other* tools it can reach
   around. (This is the same shape as analysis 6's re-entry: the fence
   covers the instrumented channel; the uninstrumented channel reaches
   the same addresses.)
2. **`SECURITY.md` never names the guard.** The security page states the
   redaction story, the injection posture, the child-env scrubbing — and
   says nothing about SSRF. The *only* places the guard is documented are
   its own header comment and the tool's model-facing text. An operator
   reading `SECURITY.md` would not know the boundary exists, let alone
   that it is fetch-only.
3. **MCP stdio** spawns processes the operator configured (fine); but MCP
   *http* (the `resp_headers` Mcp-Session-Id path in the same struct)
   follows the operator-0 rule too — an untrusted repo can't get past
   M851, so that is consistent, and I say so rather than inflate it.

## 5. Evidence

- `src/net/jc_http.c:531-535` — the connect-time claim (verbatim, §1).
- `src/net/jc_http.c:597, 953-957` — `opensocket_guard` + installation
  under `block_private_addrs` only.
- `src/net/jc_http.c:44-60` — IPv4 ranges; `jc_net_ipv6_blocked` —
  `::1`/`fe80::/10`/`fc00::/7`.
- `src/net/jc_http.c:905` — `CURLOPT_MAXREDIRS 5`; `:892-898` (M472) —
  protocol confinement on *every* request.
- `src/tools/jc_tool_fetch.c:58-61` — the claim to the model; `:74,77-79`
  — the guard set, redirects on, "Every hop is still re-checked."
- `tests/smoke/fetch_ssrf.sh:1-40` — the M542 driver and its stated
  limitation ("no positive control… mockmodel listens on 127.0.0.1").
- `tests/test_http.c` — predicate (8 checks) + `jc_http_describe_redirect`
  (the *message*, 302/307/308); no connect-guard redirect test.
- `grep -n "ssrf\|block_private" SECURITY.md` → **empty** — the security
  page is silent on the guard.
- `grep -n "block_private\|ssrf" src/tools/jc_tool_run_terminal.c` →
  **empty** — the shell has no network fence.

## 6. Implications

- **The model is told an absolute that the tests cannot back.** "a page can
  choose the next URL … probe for internal services" is the *exact*
  redirect case with no driver. If a future change moved the re-check
  from `OPENSOCKETFUNCTION` (connect time) to a pre-flight
  `CURLOPT_OPENSOCKETDATA`/resolver check, `fetch_ssrf.sh` would still
  pass — the direct-address cases don't distinguish a connect-time guard
  from a hostname check — and the redirect protection would be gone
  silently. That is the "check passes because the universe shrank to the
  thing I verified" shape, for a security fence.
- **Cloud exposure is the concrete risk.** `169.254.169.254` (AWS/GCP/Azure
  metadata) and the `10/8`/`172.16/12` admin surfaces are the targets the
  driver names. A redirect into them is the standard SSRF payload; the
  guard is designed to stop it; nothing proves it does.
- **The fence's *coverage* is a second, separate claim also unproven
  end-to-end**: "every hop is still re-checked" (`jc_tool_fetch.c:77`)
  is a comment. With `follow_redirects = 1` and `MAXREDIRS 5`, hop 2's
  address is decided by hop 1's response — i.e., by content the model (or
  the page the model fetched) does not control. That is the *highest-risk
  channel in jichi* by the fetch code's own words (analysis 6, M300: "the
  URL is chosen by the MODEL"), and its riskiest moment is the redirect.

## 7. Recommendation

1. **A connect-guard redirect test that the tier *can* run.** The tier's
   limitation (no non-loopback server) is real, but the guard is at
   *connect time*, not fetch time — so the positive control does not need a
   public server at all. Two offline shapes work:
   - *Unit, curl-free:* drive `opensocket_guard` directly with a fake
     `curl_sockaddr` for (a) a public address (must return a socket) and
     (b) a private address (must return -1), exactly the two outcomes the
     guard exists to produce. `test_http.c` already links the pure
     classifier; the guard is the same layer, one function down.
   - *Smoke:* `mockmodel` *is* a server on 127.0.0.1 that the guard
     refuses — repurpose a second mock socket on a public-looking bind
     that 302-redirects to 127.0.0.1 and assert the *second* hop is
     refused. If the tier truly cannot bind non-loopback, record that as
     the driver's stated limitation (M542's own form) and file the unit
     test as the substitute — the register's doctrine is a *stated*
     limitation, not a silent one.
2. **Name the guard and its boundary in `SECURITY.md`.** One paragraph:
   fetch_url is connect-time guarded (loopback/link-local/private/reserved,
   IPv4+IPv6, every redirect hop, M131/M190/M542); operator-configured
   endpoints (providers, MCP) are trusted by configuration (M472 confines
   their redirects to http/https, 5 hops); the model's shell is not
   network-guarded — the boundary is *another user or a container* (the
   page already says this of secrets; say it of egress).
3. **The shell boundary is a DEFERRED row, not a hole to fix here.**
   Gating `run_terminal`'s network would break the tool's purpose (it is
   a shell). The honest statement is: the SSRF fence is a *fetch_url*
   property, the shell is out of scope by design, and the operator's
   protection is the same container boundary as secrets — record it so
   the next reader does not re-derive it.

Rejected: *a pure hostname classifier as the "real" fix* — that is the
pre-M190 design, and M190's measured lesson is exactly that it drops IP-
literal and rebind cases; the connect-time guard is correct and the gap is
its *test*, not its *shape*. *Guarding the shell* — scope error, above.

## 8. Limitations and honest counterarguments

1. **The guard is probably correct.** A connect-time `OPENSOCKETFUNCTION`
   that checks the resolved address is the standard, right fix, and the
   header's M190 history shows the design matured from a measured failure.
   The analysis is about *evidence*, not doubt: an untested security
   property is a promise, and promises are the thing M542 exists to stop
   shipping.
2. **Redirect-to-internal on the guarded fetch may be "obviously"
   covered** because curl *calls the guard for every connection*. That is
   a library contract, and it is *trusted but not proven in jichi's test
   suite* — which is precisely the register's line between Driven and
   "rests on the library" (analysis 1's WSL2/M665 shape: the claim is
   carried by a dependency's behaviour no test pins).
3. **I have not run a rebinding attack.** §2 is an absence claim verified
   by reading the drivers and the suite's server inventory (mockmodel on
   127.0.0.1; tier bans real network clients). The recommendation's test
   is what would close it.
4. **The shell point (§4.1) is the *strongest* counter to the framing
   "jichi has no SSRF protection" — it has *bounded* SSRF protection, and
   the bound is honest.** The lacking thing is a *test* of the bound's
   two dynamic properties and a *record* of the bound itself; both are
   one-day items, and the document should not read as "the machine can
   reach the metadata service" — it can, through the shell, by design, as
   any shell on the host can.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the connect-time guard: claim + installation
sed -n '531,536p'  src/net/jc_http.c         # "defeats DNS-rebinding and redirect-to-internal"
sed -n '597,610p'  src/net/jc_http.c         # opensocket_guard reads the resolved addr
sed -n '953,957p'  src/net/jc_http.c         # installed under block_private_addrs only
grep -n "MAXREDIRS" src/net/jc_http.c        # 5 (M472, every request)

# the classifier, both families
sed -n '44,60p'   src/net/jc_http.c          # IPv4 ranges
grep -n "ipv6_blocked\|::1\|fe80\|fc00" src/net/jc_http.c

# the tool: the claim to the model + the guard set + "every hop re-checked"
sed -n '58,63p'   src/tools/jc_tool_fetch.c
sed -n '74,79p'   src/tools/jc_tool_fetch.c

# the test: 3 direct addresses + the stated limitation
sed -n '1,40p'    tests/smoke/fetch_ssrf.sh
grep -n "redirect\|302\|rebind" tests/smoke/fetch_ssrf.sh tests/test_http.c  # (message only)

# the boundary: fetch-only, shell unguarded, SECURITY.md silent
grep -rn "block_private_addrs" src/ --include=*.c | grep -v "\.o"
grep -n "ssrf\|block_private\|metadata" SECURITY.md                     # (empty)
grep -n "block_private\|ssrf" src/tools/jc_tool_run_terminal.c          # (empty)
```

**Verdict line for the register this could seed:** *jichi's SSRF guard is
the right shape (connect-time `OPENSOCKETFUNCTION`, IPv4+IPv6, every
redirect hop, matured from M190's measured classifier failure) and it is
tested for the direct-address cases M542 exists for — but the two properties
that make a connect-time guard a connect-time guard, "defeats
DNS-rebinding and redirect-to-internal" (`jc_http.c:535`), are promised in
the header and claimed to the model ("a page can choose the next URL",
`jc_tool_fetch.c:60`) and exercised by no test: the driver cannot reach a
redirect target (the tier's only server is 127.0.0.1, which the fence
blocks — stated, not faked), and a future change that demotes the
re-check to a hostname pass would keep all four checks green. The bound
itself — fetch-guarded, provider/MCP operator-trusted (M472 confines
their redirects), shell unguarded by design — appears nowhere in
`SECURITY.md`. Revisit when: a curl-free unit drives `opensocket_guard`
public-allow / private-refuse, a redirect-to-loopback hop is refused under
a stated tier limitation, and `SECURITY.md` names the guard, its
operator-trusted boundary, and the shell's same "another user or a
container" limit.*
