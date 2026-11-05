# jichi LSP integration — the one thing definitely lacking

*Companion to the UX, curriculum, presentations, editor, platform,
orchestration, MCP, secrets, daemon, snapshot, cost, memory, session, and
trust analyses. All read-only; evidence is at HEAD `1b71247d` and re-verified
by the appendix. Inferences are flagged; nothing here is asserted that the
record does not show.*

## 1. TL;DR

jichi's LSP integration is one of the most **hand-measured** features in the
tree — `docs/LSP.md` names four real tool-version findings, each with a
milestone (def-291 a lost `initialize` refusal; def-292 a push-only
diagnostics server; def-293 TypeScript 7 refusing to start; def-295 a
TCP-only server needing a `socat` bridge). It is *not* untested in the way
the editor and MCP analyses found their integrations untested.

The one thing definitely lacking is a **repeatable test of the real-server
path.** Every `test_lsp*.c` in the tree is an **offline protocol-framing unit
test** (`test_lsp.c`: frame encode, framer, `Content-Length: 5\r\n\r\nhello`;
`test_lsp_nav.c`: navigation). The load-bearing path — spawn a language
server, complete the `initialize` handshake, receive `textDocument/
publishDiagnostics`, run a format, resolve a definition — is exercised
**nowhere** by any test. The working behavior is locked in by *one-off manual
measurements on specific tool versions* ("measured 2026-10-01"), so nothing
regression-protects the integration, and the moment the operator's
`clangd`/`pyright`/`tsserver` version differs from the hand-test, the
documented behavior is a claim, not a verified one.

## 2. What it *does* have (for precision)

- **A full manager** (`src/lsp/jc_lsp.c` + `jc_lsp_proto.c`, 2,107 lines):
  stdio transport, spawn, handshake, diagnostics, formatting, nav, and a
  `formatCommand` shell fallback for languages with no LSP formatter.
- **A protocol layer, unit-tested offline.** `test_lsp.c` covers framing,
  URIs, diagnostic formatting, `apply_text_edits`, code actions — pure
  string/JSON, no server.
- **Hand-measured, milestone-attributed findings.** def-291/292/293/295 are
  real: each names the tool version it was measured against and the jichi
  behavior observed. That is the register's strongest form of evidence for
  a feature.
- **An honest docs page.** LSP.md states its limits plainly (push-only
  diagnostics, lost refusals, the TypeScript 7 pin, the TCP bridge).

## 3. The gap, stated

The integration's *correctness against a real server* is established once, by
hand, on a specific tool version, and recorded as prose — it is not a test.
The suite verifies the framer, not the handshake it frames.

### 3.1 The split is structural

- Offline, tested: `jc_lsp_proto.c` framing (`Content-Length` encode/decode),
  URI mapping, diagnostic JSON formatting.
- Live, untested: `jc_lsp.c` spawn + `initialize` + `publishDiagnostics` +
  format + nav, against any real binary.

The boundary between "protocol bytes" and "a server is on the other end" is
exactly where the feature's risk lives, and it is exactly where the suite
stops.

### 3.2 The evidence is version-pinned and non-repeatable

"measured 2026-10-01" is not a check anyone can re-run; it is a datum. A
different machine, a different `npm install` (LSP.md itself records that
TypeScript 7.0.2 is `latest` and breaks the server), a different `clangd`
release, and the recorded behavior may no longer hold — and **no test would
tell the operator.** def-293 exists *because* the npm default moved; the
document pins `typescript@6`, but a pin in prose is a reminder, not a guard.

## 4. Why "definitely lacking" (the alternatives and their status)

| Candidate | Status |
|---|---|
| The protocol layer is untested | **False** — `test_lsp.c`/`test_lsp_nav.c` cover it offline. |
| Real-server behavior is unknown | **False** — def-291/292/293/295 measured it against real tools. |
| The `formatCommand` fallback is untested | **Partially recorded** — it is a shell path, covered by the shell-tool drivers, not the LSP ones. |
| **A repeatable test of the real-server path** | **Absent.** No driver spawns a language server. The gap is the *form* of the evidence (one-off manual), not its absence. |

## 5. Evidence

- `tests/test_lsp.c` — "offline tests for the LSP protocol layer
  (jc_lsp_proto.c)"; `test_frame_encode`, `test_framer`
  (`Content-Length: 5\r\n\r\nhello`), `test_format_diagnostics`,
  `test_apply_text_edits`, `test_format_code_actions`. No server. (Verified.)
- `tests/test_lsp_nav.c` — navigation, same offline class. (Verified.)
- **No smoke/e2e driver spawns a real server.** `grep -rlE 'gopls|pyright|
  clangd|zls|tsserver|pylsp|rust-analyzer|jdtls' tests/` returns no LSP
  driver (the hits are unrelated lint files). (Verified.)
- `docs/LSP.md:165–180` — the four findings, each "measured" against a named
  tool version (TypeScript 7.0.2 = npm `latest` 2026-10-01). (Verified.)
- `docs/LSP.md` — "The language server must be installed separately (e.g.
  `clangd`, `pyright`)" — the test environment would have to install real
  servers, which the offline suite deliberately avoids. (Verified.)

## 6. Implications

1. **The integration is a claim that decays with tool versions.** The moment
   an operator's toolchain differs from the 2026-10-01 hand-test, the
   documented behavior is unverified. The register's own def-293 (npm's
   default moved and broke a server) is the live example of the decay.
2. **The offline tests give false confidence in the live path.** "The LSP
   framer is tested" is true and says nothing about "a real `clangd`
   completes a handshake and pushes diagnostics" — the two are the same
   class of mock-only gap the editor and MCP analyses found, one layer up.
3. **Every other real-server integration in the tree is at least
   mock-integrated.** MCP has a mock stdio server (mcp.sh); the ACP terminal
   round-trip is mock-driven; even the daemon forks a real child. LSP is the
   one "speak to a process" feature with *no* process in any test — not even
   a mock one. That is the sharpest form of the gap.

## 7. Recommendation

1. **Add a mock LSP server, the way mcp.sh has one.** A Python-free stdio
   script that answers `initialize` (with and without an error — def-291),
   `textDocument/publishDiagnostics` (def-292's push), and
   `textDocument/formatting`. Drive `jc_lsp.c` against it: assert the
   handshake, the diagnostic arrival, and the lost-refusal message. Offline,
   CI-safe, and it converts the four def-rows from "measured once" to
   "regression-protected."
2. **Gate one real-server smoke driver on tool availability** (skip when
   absent, as `elisp-test` skips when Emacs is not installed), so a machine
   that *has* `clangd` gets the live proof the suite otherwise never runs.
3. **Make the version-pins loadable.** LSP.md's `typescript@6` pin and the
   def-292 push-only note are prose; a lint that checks the config's
   `extensions`/`command` against the documented pins would turn
   def-293's "npm's default moved" from a recurring discovery into a red
   test.

Rejected, in the register's spirit:

* *Require real language servers in CI* — the suite is deliberately offline
  and tool-agnostic (platforms go as far as 512 MB boards); the mock is the
  right altitude, matching mcp.sh.
* *Trust the def-rows as sufficient* — they are the strongest evidence in
  the set, but they are dated manual data; the register's own rule is that a
  check is worth what it can re-run.

## 8. Limitations

1. **I did not run a language server.** §5's "no driver spawns one" is an
   absence verified by grep, not by installing `clangd`. A mock server
   already in the tree that I missed would falsify §3.
2. **The offline tests are real and I do not underclaim them.** The gap is
   the live path specifically; the protocol layer is genuinely covered.
3. **def-291/292/293/295 are, for this tree, the best LSP evidence that
   exists.** The "definitely lacking" claim is about *repeatability*, not
   about whether the feature works today on the versions the operator
   measured.

## 9. Appendix: reproducible

```sh
# the offline tests (protocol framing only)
grep -n "offline tests for the LSP protocol" tests/test_lsp.c
grep -n "static void test_" tests/test_lsp.c   # frame_encode, framer, diagnostics, edits, actions
grep -n "Content-Length: 5" tests/test_lsp.c

# no real-server driver exists
grep -rlE "gopls|pyright|clangd|zls|tsserver|pylsp|rust-analyzer|jdtls" tests/   # no LSP driver
ls tests/smoke/ | grep -i lsp     # (empty)

# the hand-measured findings
sed -n '160,180p' docs/LSP.md     # def-291/292/293/295, "measured 2026-10-01"
grep -n "must be installed separately" docs/LSP.md

# the manager that talks to a process (untested live)
grep -n "initialize\|publishDiagnostics\|formatting" src/lsp/jc_lsp.c | head
```

**Verdict line for the register:** *jichi's LSP integration is hand-measured
and milestone-attributed (def-291/292/293/295, each against a named tool
version, "measured 2026-10-01") — the register's strongest form of evidence
— but the real-server path (spawn, handshake, publishDiagnostics, format,
nav) is exercised by no test: test_lsp.c/test_lsp_nav.c cover the offline
protocol framer only, and no driver spawns a language server, mock or real.
The documented behavior is therefore a dated manual datum that decays with
the operator's toolchain (def-293's own cause: npm's TypeScript default
moved to 7). Revisit when: a mock LSP server (mcp.sh's pattern) drives
jc_lsp.c in CI and asserts the handshake, the diagnostic push, and the
lost-refusal message — converting the four def-rows from "measured once" to
regression-protected.*
