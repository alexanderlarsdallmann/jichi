# jichi calling jichi: instances that call each other, share tasks and communicate

*An analysis for design decisions, written 2026-10-23 for self-learners. Nothing here is built by this
document. Every claim carries a mark that says how it is known (§0.2). The companion analysis is
[`2026-10-23-orchestration-at-scale.md`](2026-10-23-orchestration-at-scale.md). That one asks how a system
of agents grows; this one asks how two jichi **processes** reach each other.*

**The question, in the operator's words (2026-10-23):** *"does jichi support jichi instances calling other
jichi instances and sharing tasks, collaboration communication? Please, analyze this too, and write
documentation with design decisions, implications, recommendations."*

**What was read:** jichi `master` at **`fc638450`**: the source, the user guides, the proposals and the
decision register. One read-only research agent enumerated every channel at file:line; the author re-read
the central and the security-relevant claims by hand (§13 lists which). No channel was exercised for this
document.

---

## 0. How to read this

### 0.1 A reading path

1. Read §1, the answer in eight points.
2. Read §2 if "instance", "call", "task sharing" or "steering" are used loosely where you work. They are
   used exactly here.
3. Read §3, the channels that exist, and §4, what the record shows. That is the evidence.
4. Read §5, the standing decisions that shaped them.
5. Read §7 (the design problems) and §8 (the options). Then §9 to §11: decisions, plan, implications.
6. Do an exercise in §14.

**If you only need the decisions:** read §1, then §9.

### 0.2 The marks

| Mark | Meaning |
|---|---|
| **[V]** | Verified: re-read at the cited line by the author of this document. |
| **[R]** | Reported: read at the cited line by the research agent, not re-read by the author. |
| **[I]** | Inferred: argued from V or R; never observed. |
| **[GK]** | General knowledge: not from this tree. |

Line numbers refer to `fc638450`.

---

## 1. The answer, up front

1. **Yes, by composition; no, by protocol.** One jichi process can start, steer or share work with
   another, through nine channels (§3). None of them is a jichi-to-jichi protocol. The coordination
   protocol JCP is specified, and only its `hello` slice is built [R: `docs/proposals/2026-08-jichi-protocol.md:3-11`].
2. **Calling** works three ways, all built [V/R]:
   - start a child `jichi -p … --output json|jsonl`, locally or over ssh;
   - send a prompt to a warm daemon over its Unix socket (`jichi --connect`);
   - wrap either in a user-defined tool.
3. **Steering** works through the control socket: `status`, `inject`, `pause`, `resume`, `abort` and a
   narrowing `mode`. It is same host, same user, served only at depth 0, and one-way: there is no reply
   path from the steered model to the sender [V: `src/chat/jc_control.c:481, 586`; R].
4. **Sharing** works through files:
   - the board, which is file-locked [R];
   - `.jichi/memory.md`, which is rewritten whole with no lock [V];
   - the queue's atomic `rename` [R];
   - the relay baton, which hands one task to the next process, never to one running at the same time [R].
5. **A model reaches every channel only through the shell or a user-defined tool.** The usual gates apply:
   chat mode asks, plan and read-only modes hide the shell, and `--strict-scope` refuses it [V/R]. But
   `--strict-scope` does not refuse user-defined tools [V: `src/tools/jc_tool.c:1560-1572`].
6. **A child jichi started from a jichi's shell gets no API key from its environment.** `JICHI_API_KEY`,
   `JLU_API_KEY`, seventeen other names and every configured `apiKeyEnv` are scrubbed from every child
   [V: `src/util/jc_proc.c:71-78`]. Shell composition therefore works only with a keyless route, a key in
   the child's own config, or a user tool that sets the key in its `env` [R]. No page says so (§6).
7. **Collaboration communication is deliberately artifacts, not conversation.** "There is **no
   agent-to-agent negotiation protocol, no shared mutable agent memory, no swarm consensus**"
   (`docs/AGENT_COLLABORATION.md:110-118`) [R]. Two agents that disagree produce artifacts, and a mechanical
   check decides.
8. **The recommendation** is a first-class, artifact-shaped call (§8):
   - one jichi asks another to run a task, over the surfaces that already exist (a local child, the daemon
     socket, ssh stdio);
   - the call carries a **call-chain id, a hop count and a budget slice**;
   - it runs under the **receiver's** fences;
   - the caller gets back the `done` object and the report.

   Shared tasks come through a coordinator (push) or later JCP `peer` (pull). Notes keep using the control
   socket, with a sender identity the transport proves. Agents never chat.

Two findings on these channels are security-relevant. Following this project's rule for unreproduced
findings, they are recorded in `docs/internal/2026-10-23-jichi-calling-jichi-findings.md`, and stay there
until a red-first test reproduces them (§6, §13).

---

## 2. Vocabulary

- **Instance:** one jichi **process**. A `spawn_subagent` delegate or a `spawn_parallel` child lives inside
  its lead's process tree and is not an instance in this sense. Those are the subject of the companion
  analysis, §3.1.
- **Call:** one instance asks another to run a task and waits for, or later collects, its result. A call
  across processes is the distributed form of `spawn_subagent`.
- **Task sharing:** work is offered, claimed and delivered between instances, as with JCP's
  `peer.offer` / `claim` / `result` / `decline`.
- **Steering:** one party changes a *running* instance's course: a note (`inject`), a pause, an abort, a
  narrower mode.
- **Artifact:** something a human can read and a program can check: a file, a diff, a commit, a report, a
  journal record, a verdict.
- **Call chain:** the sequence of instances a task passed through (A called B, which called C). The **hop
  count** is its length.
- **Caller identity:** who an instance believes sent a request. It is only as good as the transport that
  proves it: a Unix socket's owner, an ssh key. A string the sender typed proves nothing.

---

## 3. The channels that exist today

### 3.1 The gates every shell-based channel passes

A model inside a run reaches another instance with `run_terminal_command` or a user-defined tool. What
gates that:

- **Mode** [R: `docs/AGENT_MODES.md:67-71, 106-110`]:
  - chat mode asks before running the shell; a headless ASK becomes a tool error [R];
  - auto mode runs it;
  - plan mode and `--readonly` hide it.
- **`--strict-scope`** with an edit scope refuses `run_terminal_command` and `run_tests` at any depth [V:
  `src/chat/jc_agent.c:4112-4121`]. The predicate that decides covers only those, "NOT a user-defined or MCP
  tool" [V: `src/tools/jc_tool.c:1560-1572`].
- **The privileged-command gate** knows sudo, sudoedit, doas, pkexec, su and run0. Neither `jichi` nor
  `ssh` is gated [R: `src/util/jc_priv.c:38-41`].
- **The path fence** covers the file tools. A socket path inside a shell command is not fenced [R:
  `docs/AUTONOMY.md:484-489`].
- **The key scrub** removes model keys from every child's environment [V: `src/util/jc_proc.c:71-78`].

### 3.2 The nine channels

| # | Channel | Direction | Reach | Built |
|---|---|---|---|---|
| 1 | Control socket (`jichi control`) | steer a running instance | same host, same user | yes |
| 2 | Daemon (`jichi daemon`, `--connect`) | send a prompt to a warm instance | same host; remote through an ssh-forwarded socket | yes |
| 3 | Child `jichi -p --output json\|jsonl` | start an instance and read its result | local, or remote over ssh | yes |
| 4 | User-defined tool | wrap 2 or 3 as a tool | as 2 or 3 | yes, by configuration |
| 5 | Shared workspace files | board, memory, lease | one filesystem | yes |
| 6 | Per-user state | journals, sessions, telemetry | one host and user | yes, read-mostly |
| 7 | Relay baton (`--handoff-in`) | one task handed to the next process | one host | yes |
| 8 | Queue (`examples/autonomous-loop`) | claim tasks by `rename(2)` | one filesystem | yes, outside the binary |
| 9 | JCP `peer` | offer, claim, result, decline | any | **no**: specified, unbuilt |

### 3.3 Channel 1: the control socket

- **What it is.** A run started with `--control` serves a Unix socket. `jichi control <sock> <verb>`
  speaks to it with one of six verbs: `status`, `inject`, `pause`, `resume`, `abort`, `mode` [R:
  `src/util/jc_control_proto.c`].
- **Access.** The socket is `0600` in a `0700` directory and is close-on-exec, so children do not inherit it
  [R: `jc_control.c:101-111, 128-132`]. "No TCP, ever" (`docs/CONTROL.md:213-215`) [R]. Reaching a run on
  another host means running `jichi control` there over ssh.
- **When it is served:**
  - only at depth 0 [V: `jc_control.c:481, 586`];
  - at tool boundaries, and since M801 also while a shell call or a parallel batch runs; an inject is
    acked then and applied at the next boundary [R: `jc_control.c:313-314`];
  - never during a model call [R: `CONTROL.md:197-199`].
- **What can be sent:**
  - `inject` text up to 16 KB per line, with at most 4 × 16 KB queued [R];
  - `mode`, which may only narrow [R: `jc_control.c:435-443`].
- **What comes back:** a JSON ack, or a status snapshot: run id, mode, tokens, tool calls, deadline,
  outcome, last tool, `injected`, `queued_injects` [R: `jc_control_proto.c:225-261`]. **Nothing comes back
  from the steered model to the sender** [R].
- **Discovery:** `jichi ps` joins leases, control sockets and journals by pid [R: `src/main.c:5716-5800`].
- **Can a model use it?** Yes. From its shell it can steer any same-user run on its host that serves a
  control socket [I].

### 3.4 Channel 2: the daemon

- **What it is.** `jichi daemon` keeps a warm process on a Unix socket. `jichi --connect … -p` sends a
  prompt, and the daemon forks a worker per request [R: `src/main.c:16306-16760`].
- **Access.** The socket's mode is verified owner-only after `bind()`, and the daemon refuses to serve
  otherwise (M322, M528) [R]. "Anything that can `connect()` can send a prompt" (`docs/DAEMON.md:255-258`)
  [R]. Remote use is through an ssh-forwarded socket (`docs/REMOTE_SSH.md:133-145`) [R].
- **Requests:** `prompt` (prompt, cwd, mode, format), `ping`, `shutdown`, an optional `hello`, and
  `assignment.list/get/grade`, one request per connection [R: `src/util/jc_daemon_proto.c:28-98`].
- **What comes back:** the turn's output as text, json or jsonl. **The exit code is lost**: the client
  returns 0 unless its own write fails, so read the jsonl `done` event instead [R: `main.c:16757,
  16843-16856`]. The request's `cwd` is ignored [R: `DAEMON.md:318-319`].
- **Can a model use it?** Through `jichi --connect … -p` in the shell [I]. `hello` and the `assignment.*`
  verbs need a hand-written socket client [R].

### 3.5 Channel 3: a child `jichi -p`, locally or over ssh

- **What it is:** the documented Layer 2 and Layer 3 of `docs/SUPERVISOR_OF_MANY.md`, and the dispatch
  pattern of `docs/AGENT_COLLABORATION.md:98-108` [R].
- **What can be sent:** the task text, flags and config. Config can go on stdin, which keeps secrets off the
  command line (`REMOTE_SSH.md:58-91`) [R].
- **What comes back:** the final answer, jsonl events ending in `done` with its economics, and an exit code
  (`docs/SCRIPTING.md:307-316`) [R]. Over ssh, judge by positive markers in the stream, not by the exit code
  [R: `docs/DISTRIBUTED.md` §2].
- **Limits:**
  - **No key from the environment** (§3.1) [V].
  - **The lease.** A child that arms an envelope in its parent's tree sees the parent's lease and warns;
    it is refused only with `--lease fail` [V: `include/jc_lease.h:13-30`; R: `main.c:19266-19308`]. The
    parent's out-of-scope sweep cannot tell the child's edits from its own shell's writes [V: `jc_lease.h`].
  - **No nesting guard.** `JICHI_BIN` is exported so scaffolded commands can call jichi again, and nothing
    bounds how deep that goes ("no nesting guard for `JICHI_BIN`",
    `docs/proposals/2026-10-context-relay.md:657`) [R].
- **Never recorded:** a jichi supervising jichi workers as separate processes. The recorded supervisor has
  always been Claude [R: `docs/analysis/2026-10-05-JICHI-ORCHESTRATION-GAP-ANALYSIS.md`].

### 3.6 Channel 4: a user-defined tool

- `SUPERVISOR_OF_MANY.md` and `AGENT_COLLABORATION.md` name a `delegate_to_worker` tool in prose.
  `docs/USER_TOOLS.md` has no such example [R].
- A user tool runs a command the config fixes. It has a 30 s default timeout and a 32 KB output cap, and its
  arguments arrive on stdin and in `JICHI_ARG_*` variables [R: `USER_TOOLS.md:47, 52-69`].
- Its `env` is applied after the scrub, so it *can* supply a key [R: `src/util/jc_spawn.c:109`].
- `readonly: true` makes it usable in plan mode, and `--strict-scope` does not refuse it [V/R]. A tool
  declared read-only that starts a writing jichi is therefore a hole in the fence, opened by whoever wrote
  the config [I].

### 3.7 Channels 5 to 8: shared state

- **Board** (`.jichi/board.json`). Each edit locks `.jichi/board.lock` (5 s wait), reloads and writes
  atomically [R: `src/chat/jc_board.c:331-421`]. "Two processes can edit one board" (`docs/BOARD.md:131-137`)
  [R]. The board is for the main agent only [R].
- **Memory** (`.jichi/memory.md`). `remember` reads the file, appends, and rewrites it whole [V:
  `src/chat/jc_memory.c:296-312`]. `jc_lease.h` itself lists memory and the board as surfaces where a second
  run can lose an update [V: `include/jc_lease.h:13-22`]. Memory is loaded once at startup [R].
- **Lease.** Advisory: "Default is to warn and proceed", with `--lease fail` "for a supervisor that wants
  serialisation" [V: `jc_lease.h:24-29`]. It does not stop two runs in one tree.
- **Per-user state.** Journals are append-only, so read only the run you mean [R]. Sessions can be resumed
  by any same-user process; no lock was found [R]. Telemetry feeds `learn analyze` and `dream`, which
  produce drafts that a human must apply [R].
- **Relay baton.** One process ends a leg and writes `~/.jichi.d/relay/<id>/leg-NN.md`; a fresh process
  starts from it with `--handoff-in` [R: `docs/RELAY.md:62-72`]. It is a hand-off in time, not a channel
  between processes running at once.
- **Queue.** `loop.sh` or `jichi-supervisor.c` claims a task by an atomic `rename()` and routes on the exit
  code [R]. jichi itself never claims. A model can enqueue by writing a file into `pending/` [I].

### 3.8 Channel 9 and the rest

- **JCP:**
  - built: `hello`, the verified socket mode and `maxLine` [R];
  - not built: correlation ids, `session.cancel` and `session.status`, `assignment.attempt`, `lesson.*`,
    `record.*`, the four `peer` verbs, `jichi serve --stdio` (no `--stdio` flag exists), TCP+TLS and peer
    credentials [R: proposal `:128-265, 149-163, 427-435`].
- **ACP:** jichi is an ACP **server** only [V: `include/jc_acp.h:4`]. It has no ACP client, so one jichi
  cannot drive another over ACP [R].
- **MCP:** jichi is an MCP **client** only. It has no server mode, so one jichi cannot appear to another as
  an MCP tool [R].
- **The web bridge** (`examples/web-bridge`) binds 127.0.0.1, needs a token, and starts and cancels `-p`
  runs. A model could reach it only with `curl` from the shell [R/I].
- **Hooks and notify** are shell commands configured by the operator, at top level only. They could call
  `jichi control`; the model cannot configure them [R].

---

## 4. What the record shows

- **Steering across processes is the drive's daily tool.** In the hardening drive, the supervisor (Claude)
  steers a jichi lead with `jichi control … inject` from outside its container [R: the drive's supervision
  log]. The record of that channel:
  - one note was applied 23 minutes after it was sent;
  - one was lost when its run ended;
  - two were never applied during a FIFO hang;
  - an inject waited about four minutes behind an in-process `spawn_subagent`, because the socket is
    served only at depth 0 [R: companion analysis §4.1; V: `jc_control.c:481`].
- **Calling a separate jichi was recorded once, without a team.** The M459 fleet episode pushed independent
  `jichi -p` tasks over ssh to a Pi and a tablet [R].
- **A nested batch happened inside one process tree.** A write child started its own batch and collided
  with its lead's base (M860, CTL-5) [V: `src/tools/jc_tool_parallel.c:369-385`]. That is the in-process
  form of the missing call-chain guard (§7.3).
- **No record shows one jichi calling, steering or sharing a task with another jichi as a team member.**
  Every recorded supervisor of a jichi team was Claude [R].

---

## 5. The standing decisions, and what they allow

- **`docs/AGENT_COLLABORATION.md:110-118`** [R]:

  > "Stated so nobody searches for it: there is **no agent-to-agent negotiation protocol, no shared mutable
  > agent memory, no swarm consensus**. The interfaces between agents are the same ones humans get — files,
  > the board, budgets, journals, and verifiers — because an interface a human can read is an interface a
  > human can audit. Where two agents must agree, a mechanical check decides (first-wins merges + conflicts
  > *reported*; two-sided verifies), not a conversation between models."

  It **allows** files, the board, budgets, journals and verifiers; a jichi dispatching `jichi -p` with
  jsonl reports (`:98-108`); and the control socket (`:89-93`).
- **JCP's principles** [R: proposal §2, §11]:
  - P1: artifacts and verdicts, never conversations;
  - P2: every message readable by a human without tools;
  - P3: refuse rather than guess;
  - P4: the simple case must not get worse;
  - P5: a peer's task is untrusted input, run under the receiver's fences.

  It also refuses negotiation, shared memory, distributed locks, work stealing, proxying model calls, file
  transfer and crypto of its own. It rejects modelling a peer as an MCP tool ("A peer jichi is not a tool
  server") and extending ACP.
- **The control channel's refusals** [R: `CONTROL.md:204-215`; `DECISIONS.md:479-482`]: no widening
  `mode`, no `approve` verb, no budget, scope or permission changes, and no TCP. Remote approval "needs its
  own proposal with its own authentication story" (`proposals/2026-07-control-channel.md:176-179`).
- **The model-facing orchestration proposal** (2026-08) [R: `:281-295`]:
  - no `approve` verb ("privilege escalation wearing a convenience hat");
  - "an async/non-blocking subagent API" listed as **not asked for**.

  The second has since been reversed for `spawn_parallel`. def-304 records the operator's 2026-10-02
  decision, and B1's opt-in background batches were decided on 2026-10-23 [R: companion analysis].
- **M478** rejects a hub-level jichi service, because "jichi has no notion of a caller identity"
  (`DECISIONS.md:3289`) [R]. That sentence is the root of §7.2.
- **DISTRIBUTED.md §5** [R]: push before pull, no bearer token invented here, and observation before any
  fleet budget enforcement.

---

## 6. Where the pages and the code disagree

Found by the research agent and listed here so they can be corrected. Two of them are security-relevant.
They are recorded in `docs/internal/2026-10-23-jichi-calling-jichi-findings.md`, need a red-first test
before any public claim, and are not detailed here.

1. **Daemon history.** `DAEMON.md:315` says "history persists across requests until the daemon restarts".
   Each prompt is served by a forked worker whose headless run builds a new session unless the daemon was
   started with `--session` or `-c` [R: `main.c:16713-16757, 3315-3317`]. The fleet-worker plan builds on
   the page's claim [I].
2. **Daemon `hello.ok`.** `DAEMON.md:144` shows `"groups":["session"]`; the code emits `["session",
   "assignment"]` [R: `jc_daemon_proto.c:221-225`].
3. **JCP transport.** The proposal's `jichi serve --stdio` does not exist; `serve` is ACP over stdio [R].
4. **Layer 3 and keys.** `SUPERVISOR_OF_MANY.md` Layer 3 and `AGENT_COLLABORATION.md:99-102` present shell
   dispatch of `jichi -p` as working. Neither page says that the child gets no key from its environment
   [V: §3.1].
5. **The read-only supervisor.** `SUPERVISOR_OF_MANY.md:72-74` suggests a "read-only" supervisor. Read-only
   and plan modes hide the shell, so only a `readonly: true` user tool can dispatch, and that tool can start
   a worker that writes [R/I: §3.6].
6. **Shared memory.** "No shared mutable agent memory" sits uneasily with `.jichi/memory.md`. Every
   instance in a workspace can write that file through `remember`, and nothing locks it [V: §3.7].
7. **Minor.** The `jc_control.c` header comment says "five verbs"; there are six [R].
8. and 9. **Two security-relevant contradictions** about what a same-user client can make an instance do,
   and how a note's sender is labelled. Recorded internally.

---

## 7. The design problems a jichi-to-jichi call raises

### 7.1 Three shapes of interaction

| Shape | Who waits | Today | Natural channel |
|---|---|---|---|
| **Call** (do this, give me the result) | the caller, or a background slot | child `jichi -p`, daemon prompt | a local child, the daemon socket, ssh stdio |
| **Task sharing** (here is work; whoever can, take it) | nobody: the work waits | the queue's `rename`, one filesystem | coordinator push (now), JCP `peer` pull (later) |
| **Note** (change course) | nobody: one-way | control `inject` | the control socket, over ssh for another host |

Keeping the three apart is the main design choice. A call is a delegation with a result. A shared task is
an assignment with a claim. A note is a steering input. Mixing them, for example by expecting a note to
produce a reply, creates a conversation, which P1 refuses.

### 7.2 Identity and authority

- **Who called me?** jichi has "no notion of a caller identity" (M478) [R]. Each transport proves something
  different:
  - a Unix socket proves the same user, by file mode [R];
  - ssh proves a key [GK];
  - a self-declared string proves nothing.
- **What may the caller ask for?** Under P5, a task runs under the **receiver's** fences, budgets and
  permissions. No field may request more. The control socket's narrow-only `mode` follows this [R].
- **Recommendation:** a sender identity is taken from the transport, or from the coordinator's assignment
  that started the run, and is written to the journal. A label the sender types is display text at most,
  never provenance.

### 7.3 Call chains: recursion and amplification

- **The depth limit does not cross processes.** `agent_depth` and `maxSubagentDepth` bound delegation
  *inside* one process [R]. A child `jichi -p` starts again at depth 0. Nothing stops A calling B calling A
  ("no nesting guard for `JICHI_BIN`") [R].
- **Fan-out multiplies.** An instance can run a parallel batch, and each child can call another instance
  that runs its own batch. Model calls grow as a product of the widths, against shared model capacity
  (companion analysis §5.1) [I].
- **Recommendation:** every call carries:
  - a **call-chain id**, so the whole chain's telemetry joins under one trace;
  - a **hop count**, refused past a limit, as `maxSubagentDepth` does in process;
  - a **budget slice** that the callee may not exceed and the caller counts as its own spending.

  For a local child the cheapest carrier is the environment. For JCP it is a field.

### 7.4 Credentials

- The scrub is right: a key must not travel by environment to a process that need not hold it [V].
- A callee therefore needs **its own** credentials: a keyless route, its own key, or the key relay of the
  companion analysis (§5.10, D-S7). **A key is never part of a task.**
- What is missing is documentation: today shell composition fails in a way the pages do not mention (§6
  item 4).

### 7.5 Results as artifacts

- **The contract exists, half-used.** The jsonl `done` event carries the outcome and positive markers
  (`no_changes`, `tool_calls`, `tool_calls_executed`) [R: `DISTRIBUTED.md` §2]. The daemon drops the exit
  code [R]. M1044 stores a worker's report outside the merge [R: companion analysis].
- **Recommendation:** a call returns:
  - the `done` object;
  - a report, stored outside the callee's tree;
  - the callee's changes as a commit on its own branch, never as edits to the caller's tree.

  The integrator merges (companion analysis §5.3).

### 7.6 Shared state

- **One actor per tree** is the assumption the autonomy envelope rests on [V: `jc_lease.h:13-22`]. So two
  instances that both write must each have their own worktree or clone, and writers should use `--lease
  fail` [I].
- **The board** is already safe for several processes [R]. It still lacks an owner or claim field for a
  card, which task sharing would need [R: companion analysis §3.1].
- **Memory** needs a lock, or must become proposal-only for every instance but one. Today, two instances
  that both `remember` can lose an update [V/I].

### 7.7 Liveness and cancellation

- A caller must tell slow from gone. A callee started with `--control` answers `status`, and a heartbeat
  journal shows liveness [R]. A callee reached through the daemon offers neither: the daemon's worker has no
  control socket of its own [I].
- **Caps stay off for measurements, and fences stay on.** A call must not carry a wall-clock cap that could
  manufacture a failure. It carries an abort the caller can send, and the callee's own `deadStream` fence
  [R: `CLAUDE.md`].

### 7.8 Communication without conversation

- Today a note is one-way, and a callee cannot ask its caller anything [R].
- **Recommendation:** a callee that needs input ends its call with that need as its result: an outcome
  such as `needs_input`, with the question in its report. The caller decides and makes a *new* call. Each
  exchange is then an artifact, the chain stays auditable, and no channel ever holds two models in a
  dialogue.

### 7.9 Across system sizes

| Tier (companion §2.3) | How one instance calls another |
|---|---|
| T0 one device | a local child `jichi -p`; no daemon needed |
| T1 lab | the above, plus a daemon per device, and ssh between devices |
| T2 site | calls go through the coordinator's assignments; notes over ssh to the device's control socket |
| T3 institution | the same, per partition; JCP `peer` where devices come and go |
| DJGPP / FreeDOS | no fork and no sockets [R]: no daemon or control socket, and no child process until the spawn backend lands there |

---

## 8. The options for "a jichi calls a jichi"

| Option | For | Against |
|---|---|---|
| **(a) Composition only:** the shell or a user tool, as today, with the pages fixed | Exists. Nothing to build | Fragile: quoting, no structured result through the daemon, the key scrub, no call chain, no budget accounting, refused by `--strict-scope` but open through user tools |
| **(b) A built-in call tool over existing surfaces** | Structured result. Call chain, hop limit and budget built in. Uses the stable jsonl surface. No new listener | Something to build and maintain. Needs a transport per tier (child, daemon, ssh) |
| **(c) An ACP client in jichi** | ACP is bidirectional, with approvals | Built for an editor driving an agent, not for peers. A large surface for a C89 tool. JCP declined to extend ACP |
| **(d) An MCP server mode** | Other agents could call jichi as a tool | "A peer jichi is not a tool server": it would flatten a run, its budget and its journal into a function call (JCP §1) |
| **(e) A message bus or agent chat** | Feels natural | Conversation, which P1 refuses, and AGENT_COLLABORATION states as not existing |

**Recommendation: (b), designed now and built when a need appears.** The harness and the companion
analysis's coordinator (S3) are the expected first needs. Its first slice:

1. **A local child**: spawn `jichi -p … --output jsonl`, carry the call-chain id and hop count in the
   environment, and read the `done` event.
2. **Then ssh stdio** to another host, the transport JCP recommends.

No listener is added. The result is the `done` object plus the report path, and the call counts against the
caller's budget. It is fenced on the caller's side by policy (who may call, which hosts) and on the callee's
side by its own posture (what runs).

Until (b) exists, keep (a) and fix its pages (§6).

**Task sharing:** push through the coordinator now (companion D-S2). Use JCP `peer` with generations when
devices come and go (companion D-S10). Add an owner or claim field to the board when a shared board is
wanted.

**Notes:** keep the control socket, add a proven sender identity that is journaled, and use ssh for remote
notes.

---

## 9. Decisions for the operator

None of them needs answering before the hardening drive's current work. They are written so that the harness
and the coordinator can start from them.

| # | Decision | Recommendation |
|---|---|---|
| D-J1 | How one instance calls another | A built-in call tool over existing surfaces (§8 b), designed now, built when needed |
| D-J2 | Call chains | A chain id, hop count and budget slice on every call; refuse past the hop limit |
| D-J3 | Credentials for callees | Never in the task; the callee's own route, key or relay |
| D-J4 | Sender identity on notes and calls | From the transport or the coordinator, journaled; a typed label is display only |
| D-J5 | Shared workspace state | One writer per tree; memory gets a lock or becomes proposal-only |
| D-J6 | Replies and questions | Artifacts only: a callee that needs input ends with `needs_input`; no dialogue |
| D-J7 | Background calls | The same rules as B1 for `spawn_parallel`: opt-in, results folded at round boundaries |
| D-J8 | The two internal findings | Red-first tests first, then fixes, then the pages |

**D-J1. How one instance calls another.**

- Options: (a) composition only; (b) a built-in call tool; (c) an ACP client; (d) an MCP server; (e) a
  message bus.
- Recommendation: **(b)**, first slice a local child with jsonl results, then ssh stdio. (a) until then,
  with its pages fixed.
- Rests on: §8.

**D-J2. Call chains.**

- Options: (a) a call-chain id, hop count and budget slice on every call, refused past a hop limit; (b)
  rely on budgets alone; (c) nothing.
- Recommendation: **(a)**. Start the hop limit equal to `maxSubagentDepth`, and set the default from
  measurement. A guard on `JICHI_BIN` nesting is useful even under composition: it closes the gap
  `context-relay.md:657` names.
- Rests on: §7.3.

**D-J3. Credentials for callees.**

- Options: (a) the callee's own route, key or relay; (b) pass the caller's key in the task or the
  environment.
- Recommendation: **(a)**, and document the scrub's effect on composition today. (b) undoes the scrub.
- Rests on: §7.4.

**D-J4. Sender identity.**

- Options: (a) identity from the transport or the coordinator's assignment, journaled; (b) a label the
  sender types.
- Recommendation: **(a)**. A typed label is display text at most.
- Rests on: §7.2; internal findings.

**D-J5. Shared workspace state.**

- Options: (a) one writer per tree (its own worktree or clone, `--lease fail`), the board for shared
  status, memory locked or proposal-only; (b) leave it advisory.
- Recommendation: **(a)**. The envelope's "one actor per tree" assumption is load-bearing [V].
- Rests on: §7.6.

**D-J6. Replies and questions.**

- Options: (a) artifacts only, where a callee that needs input ends with `needs_input` and a question;
  (b) a reply channel on the control socket.
- Recommendation: **(a)**. (b) turns notes into a dialogue, which P1 refuses.
- Rests on: §7.8.

**D-J7. Background calls.**

- Options: (a) the B1 semantics (opt-in, results folded at round boundaries, one note per round, no second
  wave); (b) a separate design.
- Recommendation: **(a)**: one rule for in-process and cross-process background work.
- Rests on: B1 (decided 2026-10-23), companion analysis §5.2.

**D-J8. The two internal findings.**

- Options: (a) a red-first test for each, then the fix, then the pages; (b) fix without a test; (c) leave
  them.
- Recommendation: **(a)**. Each finding becomes a register row the day it is found, as the operator ruled
  on 2026-10-23 ("design now, build when needed").
- Rests on: `docs/internal/2026-10-23-jichi-calling-jichi-findings.md`.

---

## 10. A plan

Each step is verified with the companion analysis's vocabulary: Designed, Mechanics-tested, Driven.

1. **J0: correct the pages (small).** The six public contradictions in §6, and a paragraph in
   `SUPERVISOR_OF_MANY.md` and `AGENT_COLLABORATION.md` on what a child jichi needs to reach a model.
2. **J1: the internal findings.** Red-first tests, fixes, then the pages (D-J8).
3. **J2: the call-chain guard.** A chain id and hop count in the environment of every child jichi, refused
   past the limit, with the chain id in telemetry. It is useful under composition already. Driven at T0.
4. **J3: the call tool, local slice.** Spawn a child with jsonl results, a budget slice and the report
   stored outside the tree. Driven at T0 and T1 with real models.
5. **J4: the call tool, ssh slice,** together with the companion analysis's coordinator (S3). Driven at T1
   across the bench's machines.
6. **J5: proven sender identity** on control notes, journaled (D-J4).
7. **J6: task sharing by pull** (JCP `peer`), only when a deployment needs it (companion D-S10).

---

## 11. Implications

- **For security.** Every channel widens who can make an instance act.
  - Today the boundary is the Unix user: anything running as that user can steer, prompt or start jichi.
  - Inside that boundary, a fenced agent is still a process of the same user. What it can reach (the shell,
    sockets, other instances) decides whether its fences hold. §3.1's gates, and the internal findings, are
    where to look.
  - Every new channel must be fenced at both ends: the caller by policy, the callee by its own posture
    (P5).
- **For model capacity.** Calls across instances multiply model calls. Budget slices and hop limits are what
  keep a chain from spending a site's capacity (companion §5.1).
- **For observability.** Without a call-chain id, a task that crossed three instances is three unrelated
  runs in three journals. With one, it is one trace.
- **For learners.** The composition patterns in today's pages look complete and are not: the key scrub, the
  lost daemon exit code, the advisory lease. Fixing the pages (J0) is the cheapest step with the largest
  effect on people trying this.
- **For portability.** Every recommendation keeps the agent without a listener and in C89. Where sockets
  or fork are missing (DJGPP), the call tool degrades to "not available" and says so; it is never silently
  weaker.

---

## 12. Recommendations, in order

1. **J0:** fix the six public contradictions, and document what a child jichi needs to reach a model.
2. **J1:** red-first tests for the two internal findings, then the fixes.
3. **Decide D-J1 to D-J7.** D-J1 and D-J2 shape everything after.
4. **J2:** the call-chain guard. It is small, and useful even before any call tool exists.
5. **J3, then J4:** the call tool, local then ssh, built when the harness or the coordinator needs it.
6. **Keep the standing refusals:** no agent chat, no shared mutable memory without a lock, no listener in
   the agent, no widening of authority by a caller, no key in a task.

---

## 13. What this analysis did not do, and what to doubt

- **No channel was exercised.** Everything here was read from the source and the pages at `fc638450`.
- **The research agent read most lines; the author re-read these:**
  - the daemon's handling of a request's `mode` and the control socket's `from` label and journal record
    (both internal findings);
  - the key scrub list (`jc_proc.c:66-80`);
  - the `--strict-scope` predicate (`jc_tool.c:1560-1572`, `jc_agent.c:4112-4121`);
  - `remember`'s whole-file rewrite (`jc_memory.c:296-312`);
  - the lease's advisory design (`jc_lease.h:13-30`);
  - control served at depth 0 only (`jc_control.c:481, 586`);
  - ACP server-only (`jc_acp.h:4`).

  Everything else marked [R] is as the agent reported it.
- **Security-relevant findings are internal** until a red-first test reproduces them. That is this
  project's rule for unreproduced security claims, not a judgement that they are minor.
- **The design in §7 and §8 is this analysis's proposal.** Only the call-chain guard and the page fixes
  answer something recorded (the M860 nested batch, the missing `JICHI_BIN` guard, and the undocumented
  scrub). The rest is argued.
- **The author supervises the drive that uses the control socket daily** (§4), so the evidence on notes
  comes from one observer.

---

## 14. Exercises for a self-learner

1. **Trace the scrub.** Read `src/util/jc_proc.c` around line 70. Which variable names does a child process
   never see? Then explain why `jichi --connect … -p` still reaches a model. *Check:* the daemon already
   holds its own key; the client needs none (§3.1, §3.4).
2. **Find the gate gap.** In `src/tools/jc_tool.c`, read `jc_tool_name_runs_shell`. Which tools does
   `--strict-scope` refuse, and which does it let through? *Check:* the shell and `run_tests` are refused;
   user-defined and MCP tools are not.
3. **Sort three interactions.** "Review this diff and tell me what is wrong." "Whoever is free, build the
   ARM binary." "Stop editing tests." Which is a call, which a shared task, and which a note? *Check:* §7.1.
4. **Design a hop guard.** Write down what a child jichi must read at start-up to refuse a call chain
   longer than 2, and where it must write the chain id so that telemetry joins. *Check:* §7.3 and D-J2. An
   answer that relies on `agent_depth` is wrong, because it does not cross processes.
5. **Find the lost update.** Two instances in one workspace each call `remember` once, at nearly the same
   time. Using `jc_memory.c:296-312`, explain how one note can disappear. *Check:* both read the old file,
   both append, the later write wins.

---

## 15. Sources

- **jichi** `fc638450`:
  - `src/chat/jc_control.c`, `src/util/jc_control_proto.c`, `src/util/jc_daemon_proto.c`;
  - `src/main.c` (daemon 16306, daemon client 16777, control client 16865, `ps` 5716, lease 19258);
  - `src/util/jc_proc.c`, `src/util/jc_spawn.c`, `src/tools/jc_tool.c`, `src/chat/jc_agent.c`;
  - `src/tools/jc_tool_user.c`, `src/chat/jc_board.c`, `src/chat/jc_memory.c`;
  - `include/jc_lease.h`, `include/jc_acp.h`.
- **Pages:**
  - `docs/AGENT_COLLABORATION.md`, `docs/CONTROL.md`, `docs/DAEMON.md`, `docs/DISTRIBUTED.md`;
  - `docs/SUPERVISOR_OF_MANY.md`, `docs/REMOTE_SSH.md`, `docs/RELAY.md`, `docs/USER_TOOLS.md`,
    `docs/BOARD.md`, `docs/AGENT_MODES.md`;
  - `docs/proposals/2026-08-jichi-protocol.md`, `docs/proposals/2026-08-model-facing-orchestration.md`,
    `docs/proposals/2026-07-control-channel.md`, `docs/proposals/2026-10-context-relay.md`;
  - `docs/DECISIONS.md` (M304, M478, M648, M951), `docs/DEFERRED.md` (def-304),
    `docs/analysis/2026-10-05-JICHI-ORCHESTRATION-GAP-ANALYSIS.md`.
- **Companion:** `docs/analysis/2026-10-23-orchestration-at-scale.md` (this branch).
