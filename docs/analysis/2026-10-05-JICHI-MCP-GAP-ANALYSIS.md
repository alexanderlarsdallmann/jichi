# jichi MCP integration: the one thing it definitely lacks — a tested remote *tool call*

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`). No files were
> modified. All claims cite a source; inferences are flagged.
> **Companions:** the other `JICHI-*-GAP-ANALYSIS.md` files (six earlier).

---

## 1. TL;DR

jichi's MCP client is real and substantial: a stdio transport, a
streamable-HTTP/SSE transport (`jc_mcp_http.c`), a JSON-RPC protocol layer
(`jc_mcp_proto.c`, unit-tested), a manager, namespaced tool registration
(`<server>__<tool>`), and a safety layer (`autoApprove`, `deny`, `kinetic`,
with `"*"` wildcards added at M843). Its own title says the whole point:
*"MCP servers — giving the agent tools jichi does not have … jichi ships ~45
built-in tools and will never ship a Jira tool, a Postgres tool, or your
company's deployment tool. MCP is how those …"* (`docs/MCP.md:1–10`).

But the thing MCP exists to do — **a remote *tool call* executed end to
end** (the model chooses a tool served by an external process, jichi passes
the approval/`autoApprove` gate, issues `tools/call` over the wire, parses
the result, and feeds it back to the model) — **is not exercised by any
test against a server that actually serves a tool.** The smoke suite's mock
MCP server returns `tools/list` → `{"tools":[]}` and answers every other
method (including `tools/call`) with the catch-all `{"result":{}}`. The MCP
smoke tests cover only *read-only context injection* — `resources/list`,
`resources/read`, `prompts/list`, `prompts/get`, `@mcp:<uri>` inlining, and
MCP-prompt-as-slash-command. No end-to-end test configures an MCP server at
all. The single test that touches a `tools/call` result
(`tests/test_mcp.c:test_parse_call_result`) is a **pure parse of a canned
JSON string** — no server, no model, no approval gate. And the one recorded
multi-run live drive (the zigodot weekend) lists MCP explicitly under
"**Not tested**" (`docs/analysis/2026-09-20-driving-jichi-on-zigodot.md:258`).

So the MCP surface is *discovery-proven* and *actuation-unproven*: jichi is
demonstrated to find and read what an external server offers (resources,
prompts), but not demonstrated to *use* the external server's tools — which
is the feature, and the risk, in one.

## 2. What MCP support *does* have (for precision)

- **The protocol layer, unit-tested** (`tests/test_mcp.c`): `test_parse_tools`,
  `test_parse_tools_error`, `test_parse_call_result`,
  `test_parse_call_result_iserror`, `test_parse_call_result_rpc_error`,
  `test_build_request`, `test_build_notification`, `test_message_id`,
  `test_fqname` — the JSON-RPC *shapes* are proven.
- **The stdio transport + manager, smoke-tested against a mock**
  (`tests/smoke/mcp.sh`, "Exercises the real stdio transport + manager"):
  `resources/list` discovered, `resources/read` returned content,
  `prompts/list` discovered, `prompts/get` rendered.
- **Resource references** (`tests/smoke/mcp_ref.sh`, M47): `@mcp:<uri>` in a
  message inlines the resource's text into the user turn.
- **Prompts as slash commands** (`tests/smoke/mcp_prompt.sh`, M49): a
  server-advertised prompt is tab-completable and renders into a turn.
- **The config-safety layer, lint-tested**: M395 made an object-shaped
  `mcpServers` (the Claude Code/Continue shape) *warn* rather than configure
  nothing in silence; `autoApprove`/`deny` accept `"*"`/`true` for every tool
  a server offers (M843); `kinetic` gates hardware-actuating tools.
- **The HTTP transport, documented** (`jc_mcp_http.c`, "streamable HTTP, JSON
  or SSE responses" — `docs/MCP.md:163`).

Every *read* surface of MCP is proven. The one *write/actuate* surface —
calling a remote tool — is the gap.

## 3. The gap, stated

> **jichi's MCP integration has no test — smoke, e2e, or otherwise — that
> drives a remote *tool call* end to end against a server that serves a real
> tool. The load-bearing path (model picks `<server>__<tool>` → approval /
> `autoApprove` gate → `tools/call` JSON-RPC → result parsed into context) is
> exercised only as pure-string unit parses; the mock MCP server returns an
> empty tool list and a catch-all empty result; and the one recorded live
> multi-run drive marks MCP "Not tested." So the capability the feature is
> named for — "giving the agent tools jichi does not have" — is
> discovery-verified and actuation-unverified.**

Two strength levels, as in the companion analyses:

* **(Strong) No test serves a real tool and calls it.** Grep of `tests/` for
  a non-empty `tools` list in any mock/e2e MCP server: **none** (the only
  mock returns `[]`). Grep of `tests/e2e/` for a config with `mcpServers`:
  **none**. The `tools/call` round-trip through the *agent* loop is
  untested.
* **(Weak, but the one with teeth) The untested path is the *risky* path.**
  A remote *resource read* is read-only context. A remote *tool call* is
  actuation — the MCP title's "deployment tool," "Postgres tool." The
  `autoApprove: "*"` wildcard (M843) means "you are pre-approving whatever
  the server happens to expose today **and** after its next update." The
  safety decision that makes MCP dangerous is precisely the one gating the
  one path no test runs.

## 4. Why "definitely lacking"

| Candidate | Status |
|---|---|
| MCP resource discovery/read (stdio) | **Smoke-tested** (`mcp.sh`, M43) against the real transport + manager. Not a hole. |
| MCP prompts (list/get, slash-command) | **Smoke-tested** (`mcp.sh`, `mcp_prompt.sh`, M43/M49). Not a hole. |
| `@mcp:<uri>` resource inlining | **Smoke-tested** (`mcp_ref.sh`, M47). Not a hole. |
| JSON-RPC protocol shapes (incl. call-result parsing) | **Unit-tested** (`test_mcp.c`). Not a hole — but parse-only, no wire. |
| Misconfigured `mcpServers` silently ignored | **Fixed + linted** (M395: now warns). Not a hole. |
| **Remote tool *call*, end to end (model → approval → `tools/call` → result)** | **No test against a tool-serving server.** The mock serves zero tools; no e2e configures a server; the one real drive marks MCP "Not tested." |
| **MCP over HTTP (streamable/SSE)** | **Documented, no smoke test** (only stdio is exercised by the mock). A second, narrower instance of the same shape. |

The distinctive form, the eighth instance, and the sharpest sibling of the
editor analysis: the **offline-mock** design is correct (hermetic, no
network, no external process in CI), and it is *exactly* what keeps the
mock from being able to serve a tool — because a tool, by definition, does
something external. The read paths (resources/prompts) happen to be
representable by a canned mock; the actuation path is not. So the suite
proves the half of MCP that a stub *can* fake (injection) and is structurally
incapable of proving the half that is the point (actuation). Same root as
the editor gap ("a text-only stub can't exercise the path that touches
files"); here it is "a canned mock can't exercise the path that touches a
process."

## 5. Evidence

### 5.1 The mock serves no tools

`tests/smoke/_smoke.sh`, `smoke_write_mock_mcp()` dispatch:

```
initialize)   → protocolVersion/serverInfo
tools/list)   → {"tools":[]}                       # <— empty; the feature's core is empty
resources/list) → [ {mem://notes …} ]
resources/read) → NOTES_BODY for <uri>
prompts/list)   → [ greet ]
prompts/get)    → a user message
*)             → {"result":{}}                      # <— catch-all: tools/call lands here, empty
```

The `tools/list` branch returns an empty array, and `tools/call` is not a
named branch — it falls into the catch-all, which returns `{"result":{}}`.
Therefore no smoke test can register a remote tool, let alone have a model
choose and call one. (`mcp.sh:3`: "Exercises the real stdio transport +
manager" — but for resources and prompts only.)

### 5.2 The MCP smoke tests are all read/injection

`mcp.sh` (5 checks): resources/list, resources/read, prompts/list,
prompts/get — plus the M395 misconfig-warning check. `mcp_ref.sh`: `@mcp:`
inlining. `mcp_prompt.sh`: prompt-as-slash-command. **None** drives a tool
call. Grep of `tests/smoke/` + `tests/e2e/` for a mock/e2e config with a
non-empty MCP `tools` list, or any `tools/call` through the agent loop:
**none** (the only `tools/call` in `tests/` is `test_mcp.c`).

### 5.3 The only `tools/call` test is a string parse

`tests/test_mcp.c:test_parse_call_result`:

```c
const char *resp =
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":[…\"line one\"…\"line two\"…],\"isError\":false}}";
jc_status st = jc_mcp_parse_call_result(resp, &text, &is_err);
JC_CHECK(st == JC_OK);  JC_CHECK(is_err == 0);  JC_CHECK_STR(text, "line one\nline two");
```

It proves the *parser* handles a well-formed call result. It does not prove
that a call result *arrives* — no server, no request, no model, no
approval. The sibling `_iserror` and `_rpc_error` tests are the same class:
parse canned strings.

### 5.4 The real drive marks MCP "Not tested"

`docs/analysis/2026-09-20-driving-jichi-on-zigodot.md:258` (the companion to
the weekend drive): "*Not tested:* the TUI; any priced model; multi-turn
sessions (`--no-session` throughout); subagents; **MCP**; the LSP tools,
which zigodot's config declares but which need a Zig language server this run
did not start." The one recorded multi-run live event — the set's strongest
evidence that orchestration works — explicitly excluded MCP.

### 5.5 The risk sits on the untested path

`docs/MCP.md:62`: "`autoApprove` … Also accepts `\"*\"` or `true` … for
*every* tool this server offers — a real decision, not a convenience: you are
pre-approving whatever the server happens to expose today **and** after its
next update." The `kinetic` flag (`MCP.md:64`) is the safety valve for
hardware-actuating remote tools. Both gate the *call* path — the path with no
end-to-end test.

**Flagged inference:** "no test drives a real tool call" proves the *suite*
doesn't. The operator could have, in a private run, pointed jichi at a real
`server-filesystem` or a company MCP tool and watched it work — exactly as the
weekend drive was a private-repo event. The register culture makes an
unrecorded MCP actuation unlikely (every real event here gets an `analysis/`
page), but the bound is the same as the other analyses.

## 6. Implications

1. **The feature's named value is the unverified half.** "Giving the agent
   tools jichi does not have" is a *tool-call* claim. What is verified is a
   *context-injection* claim (read this resource, run this prompt). A user
   who configures an MCP tool server on the strength of the docs is relying
   on the path no test runs — the one where jichi hands an external process
   the ability to act in the project.
2. **The approval-gate interaction is the real safety surface, and it's
   untested through MCP.** jichi's own tools have extensive approval/fence
   tests (`tui_fences.sh`, `shell_tools_fenced_alike.sh`, `fence_alias_bypass.sh`,
   `test_perm.c`). But a *remote* tool — namespaced `<server>__<tool>`,
   governed by `autoApprove`/`deny`/`kinetic` — is a different permission
   decision than a built-in one. The `autoApprove: "*"` wildcard and the
   `kinetic` gate live in that decision, and no test exercises a remote tool
   *passing or failing* the gate, because no test has a remote tool to call.
3. **HTTP transport compounds it.** The stdio mock covers the read paths; the
   HTTP/SSE transport (`jc_mcp_http.c`) has no smoke test at all — so the
   actuation gap is the *union* of "no tool call over stdio" and "nothing
   over HTTP." A production MCP deployment is far more likely to be the
   `url` + `Authorization` header shape (HTTP) than the local `command`
   shape (stdio).
4. **The M395 fix was the right instinct, and it points the way.** M395
   caught "a configured-but-ignored server is worse than an absent one" —
   the *discovery* path failing silently. The actuation path has the same
   failure mode (a registered remote tool that, on call, returns
   `{"result":{}}` silently, or an `isError` the model is never shown), but
   no test has yet made it *louder than silence*.
5. **Meta, the eighth, and the sibling of #2 (editor).** Editor support:
   offline by design, and the offline design excludes the file-touching
   path. MCP support: offline by design, and the offline design excludes the
   process-touching path. The shared root — the hermetic mock can fake the
   *read* and not the *actuation* — has now appeared twice. The
   recommendations are the same shape: give the mock a hand that does the
   one external thing (write a file / run a tool), and assert the
   coordination around it (revert offer / approval gate + result
   propagation).

## 7. Recommendation

1. **Give the mock a tool, and call it.** Extend `smoke_write_mock_mcp`
   with a `tools/list` that returns one trivial, *harmless* tool (e.g.
   `echo` / `add`) and a `tools/call` branch that returns a canned result.
   Add a smoke test that configures it, runs a headless turn where the mock
   chat model is scripted to call `<server>__echo`, and asserts (a) the
   approval/`autoApprove` decision fired, and (b) the tool's result reached
   the model (the mock answers `TOOL_OK` only if it saw the echoed text — the
   exact pattern `mcp_ref.sh` and `mcp_prompt.sh` already use). Offline,
   hermetic, CI-safe — and it tests the one path the feature is named for.
2. **Cover the approval gate, both directions.** In that same mock, assert a
   *denied* tool is refused if the model asks anyway, and that
   `autoApprove: "*"` lets it run — the M843 wildcard's behavior, which is
   currently untested because no test has a remote tool to approve.
3. **One HTTP smoke test.** Even a single `url`/SSE mock (or a
   `curl`-backed stub) exercising `initialize` + `tools/list` over the HTTP
   transport would move the HTTP half from "documented" to "seen." The
   stdio mock proves the manager; the HTTP test proves the second transport
   actually connects.
4. **Record an actuation in the next real drive.** The zigodot companion
   lists MCP "Not tested"; the cheapest close for the *live* half is one
   MCP tool call (even the built-in `server-filesystem` reading a file) in
   the next recorded drive, so the "Not tested" line can be struck with a
   dated evidence stamp — the register's own form.

Rejected, in the register's spirit:

* *Point CI at a real `npx -y server-filesystem`* — a network + npm +
  external-process event in PR CI: the exact thing the offline design
  refuses (and the editor/platform analyses already concluded). The right
  fix is a *local* mock that serves a tool, not a remote server.
* *Assert the tool call by reading the unit parse* — `test_mcp.c` already
  parses a canned result; that proves the parser, not that a result
  arrives. The gap is the *round-trip*, which needs a server that answers.
* *Mark MCP "partly verified" and move on* — the register's own M395 lesson
  is that "configured but silently not doing the thing" is the worst state.
  "Partly" would paper over that the *tool* half — the feature — is the
  partly part.

## 8. Limitations and honest counterarguments

1. **The read half is genuinely, well tested.** A reader expecting "jichi's
   MCP is broken" will find the opposite: resources, prompts, references,
   inlining, the stdio transport + manager, and the JSON-RPC shapes are all
   green. The claim is narrow: the *actuation* half (a remote tool call) is
   untested end-to-end. If the operator considers resource/prompt injection
   the "MCP support" and tool-calling an add-on, §3 weakens to: *the tool
   path — which the docs name as the point — is discovery-verified, not
   actuation-verified.*
2. **Absence-based on the suite.** "No test serves a real tool and calls it"
   rests on greps (empty `tools` in the only mock; no `mcpServers` in any
   e2e config; one parse-only unit test) plus the zigodot "Not tested" line.
   An off-record live MCP tool call is invisible here — same bound as the
   other analyses, and the register culture is the discount.
3. **The mock's empty `tools/list` is a *choice*, not a defect.** The suite
   was built to test discovery/injection, and an empty tool list is the
   honest way to test "resources/prompts work without a tool in the way."
   Calling the mock a gap misreads it as failing; it succeeds at what it
   was written to do. The gap is that a *second* mock — one that serves a
   tool — was never written, because the tool path is the actuation the
   offline design sidesteps.
4. **I did not run any MCP test.** Driving a mock MCP server is a
   subprocess event; per the register's rule it is a datum, not a read.
   Everything here is read from `tests/smoke/mcp*.sh`, `tests/smoke/
   _smoke.sh` (the mock), `tests/test_mcp.c`, `src/mcp/*`, `docs/MCP.md`,
   and the zigodot companion.
5. **`test_mcp.c` does more for the call path than I first credited — but
   still not the round-trip.** It tests `parse_call_result`,
   `parse_call_result_iserror`, and `parse_call_result_rpc_error`: the three
   result *shapes*. That is real coverage of the parser's error handling.
   It does not, however, test that a request is *built and sent*, that the
   server *receives* it, or that a *model* triggers it — the round-trip is
   the gap, and the parser tests are orthogonal to it.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the feature's own named value (actuation)
sed -n '1,10p' docs/MCP.md                                   # "giving the agent tools jichi does not have"

# the mock serves NO tools and a catch-all empty result
sed -n '/smoke_write_mock_mcp()/,/^}/p' tests/smoke/_smoke.sh | grep -nE "tools/list|tools\"|^\s*\*)"

# the MCP smoke tests: all read/injection
head -8 tests/smoke/mcp.sh tests/smoke/mcp_ref.sh tests/smoke/mcp_prompt.sh
grep -rn "mcpServers" tests/e2e/                              # empty: no e2e configures an MCP server

# the only tools/call test is a string parse
grep -n "tools/call" tests/                                   # test_mcp.c only
sed -n '/test_parse_call_result (void)/,+12p' tests/test_mcp.c

# the protocol layer that IS unit-tested
grep -n "test_parse_tools\|test_build_request\|test_fqname" tests/test_mcp.c

# the safety layer on the untested path
sed -n '62,64p' docs/MCP.md                                    # autoApprove "*", deny, kinetic
grep -rn "M395" docs/DEFERRED.md | head -1                     # the discovery-silence fix

# the real drive marks MCP "Not tested"
sed -n '258,261p' docs/analysis/2026-09-20-driving-jichi-on-zigodot.md

# the HTTP transport: documented, no smoke test
grep -n "jc_mcp_http\|streamable HTTP" docs/MCP.md
ls tests/smoke/ | grep -i mcp                                  # mcp, mcp_ref, mcp_prompt — no mcp_http
```

**Verdict line for the register this could seed:** *jichi's MCP integration
has no end-to-end test of a remote *tool call*: the smoke mock
(`smoke_write_mock_mcp`) returns `tools/list` → `{"tools":[]}` and answers
`tools/call` via the catch-all `{"result":{}}`; the MCP smoke tests
(`mcp.sh`/`mcp_ref.sh`/`mcp_prompt.sh`) cover only resource/prompt
discovery and injection; no e2e configures an MCP server; the sole
`tools/call` test (`test_mcp.c:test_parse_call_result`) parses a canned
string with no server, model, or approval gate; and the one recorded live
multi-run drive marks MCP "Not tested" (2026-09-20 companion, :258). The
path the feature is named for — "giving the agent tools jichi does not
have," gated by `autoApprove:"*"` (M843) and `kinetic` — is
discovery-verified and actuation-unverified, over stdio; the HTTP/SSE
transport (`jc_mcp_http.c`) has no smoke test at all. Same root as the
editor gap: the hermetic mock can fake the read, not the actuation.
Revisit when: a mock that *serves* one harmless tool is added, a headless
turn is scripted to call it, and the test asserts (a) the
`autoApprove`/`deny` gate fired and (b) the tool's result reached the model
— plus one HTTP-transport smoke.*
