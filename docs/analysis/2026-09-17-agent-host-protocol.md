# The Agent Host Protocol, measured against jichi

*2026-09-17 (M648). The operator asked: analyse Microsoft's Agent Host Protocol
and check whether it is suitable for jichi. The source read is the repository at
a local clone of `microsoft/agent-host-protocol` and its published specification.
Verdict up front: **watch, do not adopt** — and the interesting part is not the
refusal but the reason, which is that jichi is on the wrong side of the
protocol's central noun.*

---

## 1. What AHP is, from the spec rather than the pitch

| | |
|---|---|
| Owner / licence | Microsoft · MIT |
| Version read | **0.9.0** (spec and each language client release on independent SemVer tracks) |
| Framing | **JSON-RPC 2.0** |
| Transport | **Not prescribed.** Any reliable, ordered, bidirectional, complete-message stream. WebSocket is "the most common choice" and what the VS Code implementation uses; the transport is agreed out-of-band, not negotiated |
| Routing key | Every command's and every notification's `params` carries a top-level `channel: URI`, so a peer dispatches on `(method, params.channel)` without per-method deserialisation |
| State model | Immutable state, **pure reducers**, write-ahead reconciliation — a Redux shape, on the wire |
| Reference server | **VS Code's agent host** (`src/vs/platform/agentHost/node/`) |
| Client SDKs | Rust, TypeScript, Kotlin, Go, Swift, .NET — **no C** |
| Normative spec | ~2,100 lines over 16 pages |

**Measured surface**, counted from `schema/actions.schema.json` rather than
estimated: **96 action types**, in nine channel families.

| Channel family | Actions | |
|---|--:|---|
| `chat/` | **30** | turns, deltas, tool calls, drafts, input requests |
| `session/` | **28** | chats catalogue, clients, changesets, config |
| `terminal/` | **11** | data, cwd, command detection, exit |
| `changeset/` | **8** | per-file edit state and review status |
| `annotations/` | 5 | |
| `automationRun/` | 5 | |
| `automation/` | 4 | |
| `root/` | 4 | agents, sessions, terminals, config |
| `resourceWatch/` | 1 | |

`schema/commands.schema.json` carries **392** definitions.

---

## 2. The finding: jichi is on the wrong side of the noun

This is the whole analysis, and everything below is consequence.

**In AHP, the "server" is a sessions *host*.** It owns agent sessions and
publishes their state; the clients are *viewers* — VS Code's Agent Sessions
view, AHPX — and they see a **synchronized** view of the same sessions. The
agent that actually talks to a model sits *behind* the host. AHP's problem
statement is: **N clients, one session state, kept consistent.**

**jichi is the agent.** Under ACP it is the *agent server* and the editor is the
client; under `-p` it is a process that answers and exits; in the TUI it is one
person at one terminal.

So the two candidate readings of "adopt AHP" are:

- **jichi as an AHP client** — jichi would become a viewer of somebody else's
  agent sessions. That is not a thing jichi is for, and nothing in the project
  wants it.
- **jichi as an AHP server** — jichi would host its own sessions so VS Code
  could display them. This is the reading worth costing, and §3 costs it.

**The problem AHP solves is not a problem jichi has.** Multi-client
synchronization matters when several humans or several UIs watch one agent
session. jichi's deployment shapes are one terminal, one headless pipe, or one
editor over ACP. *That is an observation about today, not a law* — and §4 says
what would change it.

---

## 3. What "jichi as an AHP server" would actually cost

The honest way to size this is against something already built. jichi's **entire
ACP implementation is 1,815 lines** of C89 (`jc_acp.c` 1,041, `jc_acp_proto.c`
620, `jc_acp.h` 154) and covers **nine** methods: `session/new`, `session/load`,
`session/prompt`, `session/cancel`, `session/update`, and the four
`terminal/*`.

AHP is **96 action types and 392 command definitions**. Even assuming AHP
actions are individually cheaper than ACP methods — they are smaller, being
state deltas rather than operations — the surface is an order of magnitude
larger, and three costs are not linear at all:

1. **A WebSocket server, in C89.** `grep -ri websocket src/ include/` returns
   **nothing**: jichi has no WebSocket code and no HTTP *server*. libcurl is a
   client library. RFC 6455 means an HTTP upgrade handshake, frame parsing,
   client-to-server unmasking, continuation frames, control frames and a close
   handshake — and then a **listening network socket**, which is a security
   surface this project has so far deliberately not had. The transport is not
   mandated, so jichi's existing AF_UNIX newline-framed control channel could
   carry AHP instead; that avoids the RFC but also avoids the only client that
   exists, since VS Code speaks WebSocket.
2. **Immutable state and pure reducers.** AHP's correctness model is that both
   peers apply the same pure reducer to the same ordered action stream and get
   the same state, with write-ahead reconciliation for the optimistic case. C89
   gives you none of that for free. jichi's session state is mutable structs
   edited in place. Reproducing reducer semantics across 96 actions is the bulk
   of the work and the part most likely to be subtly wrong — and "subtly wrong"
   here means two clients disagreeing about what the agent did.
3. **Concepts jichi has under different shapes.** The `changeset/` channel (8
   actions) models per-file edit state with review status; jichi has edits,
   diffs and an approval flow, but not as an observable catalogue. The
   `terminal/` channel (11) assumes shell integration and command detection.
   Each is a translation layer, not a binding.

**A rough, honest number: well north of 10,000 lines of new C89**, against a
tree whose entire `src/acp` is 1,815 — to serve one client, for a protocol at
0.9.0.

---

## 4. The recommendation

**Watch, do not adopt.** Specifically:

- **Do not implement an AHP server in C89.** The cost is in §3 and the benefit
  is one integration.
- **Do not add an AHP client.** It inverts what jichi is.
- **Do keep ACP as the editor-facing protocol.** It is already built, it is
  agent-shaped rather than host-shaped, and it is the protocol whose *server*
  role jichi actually occupies.
- **Re-read the spec when it reaches 1.0.** At 0.9.0 the spec is explicitly
  pre-stable on its own SemVer track. This project's own CHANGELOG argues that
  1.0 is a claim about interface stability and the honest time to make it is
  after the interfaces have met someone else's machine; the same standard
  applied to somebody else's protocol says wait.

**And the path that does not need any of that.** If the requirement ever becomes
concrete — *"show jichi sessions in VS Code"* — the cheap answer is **an
out-of-process bridge, not C89 in this tree**: a small AHP server (in any of the
six languages that already have an SDK) that drives jichi headless and
translates. jichi already exposes exactly the two surfaces such a bridge needs —
`--output jsonl` for the event stream and the **AF_UNIX newline-framed JSON
control channel** for mid-run control — and they were built for observability
readers, which is what an AHP host is. That bridge is somebody's weekend in
TypeScript against a published SDK; the same capability inside jichi is a
subsystem larger than its TUI. **Keeping it out of the tree is the design
decision, not a deferral.**

---

## 5. What this analysis did not do

Stated, because an assessment that does not bound itself is a recommendation
wearing evidence's clothes:

- **Nothing was run.** No AHP server was started, no client connected, no
  message exchanged. Every claim here is read from the specification, the JSON
  schemas and the repository layout.
- **The line estimate in §3 is an estimate**, anchored on a real ratio (1,815
  lines for 9 ACP methods) but not derived from a prototype. It is offered as an
  order of magnitude, and a prototype could move it by a factor of two in either
  direction without changing the verdict.
- **The "no problem jichi has" claim is about today's deployment shapes.** If
  jichi grows a genuinely multi-client story — several people watching one
  autonomous run, a fleet dashboard over the existing `DISTRIBUTED.md`
  topologies — then AHP's problem statement becomes jichi's, and this page
  should be re-read rather than cited.
- **AHP's `automation/` and `automationRun/` channels were not studied in
  depth.** They are the closest thing in the protocol to jichi's autonomous
  loops, and if this verdict is ever revisited they are where to start.
