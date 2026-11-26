# Prompt injection: the plan (2026-11-17)

*The plan that orders the work of [the prompt-injection analysis](../analysis/2026-10-27-prompt-injection.md).
The operator asked for the analysis on 2026-10-27 ("We need the plan, later") and decided its four questions
on 2026-11-17 (the analysis, section 10). Written by the supervisor of the jichi drives on the operator's
request. Nothing here is built. Milestone numbers are claimed when a batch is cut, not here.*

**Who this is for:** the lead and the supervisor of the resumed hardening drive, and anyone who wants to see
how a threat model becomes a sequence of tested changes. **What you can do after reading:** name each batch,
the gap it closes, whether it builds a boundary or a mitigation, and the test that shows it works by its
effect. **What you need first:** the analysis, sections 1, 4 and 5; [HARDENING.md](../HARDENING.md) §1 and §6b.

## 1. Where this plan starts

**The decisions (2026-11-17).** The operator took all four of the analysis's recommendations:

1. **Q1.** Under `--auto`, an instruction write is refused by default. The strictness is configurable (off,
   warn, refuse), telemetry records it in every mode, and the default is provisional. Interactively it gets
   its own confirm, which an "always" grant cannot satisfy. A drive that must edit its own rules files says
   so in its configuration.
2. **Q2.** The untrusted-context flag starts narrow and as telemetry only: the channels M300 fences, plus MCP
   tool results.
3. **Q3.** The documentation recommends the container for every `--auto` run on untrusted input, and says
   plainly that the normal-user mode is not an injection boundary.
4. **Q4.** The work goes into the resumed hardening drive, right after its batch B-02, in the analysis's
   order.

**The facts, checked on 2026-11-17 against master `81f9d35c`:**

- The two biggest gaps are still open: [def-298](../DEFERRED.md#def-298) (a cloned repository's rules,
  commands, agents and skills load into the system prompt with no trust step) and
  [def-330](../DEFERRED.md#def-330) (the top-level shell has no Landlock rule set).
- The `remember` route is unchanged by reading. Its permission gate (`src/chat/jc_perm.c`) and the system
  message's loader (`src/chat/jc_sysmsg.c`) have no commits since the measurement. The analysis's section 3
  run was not repeated.
- A model does look for secrets. In the clean-container baseline of 2026-11-10 (G8 of the
  jichi-as-its-own-guide design note), jlu/qwen3.8-27b could not verify its model from inside its run. It
  then looked for the key five ways, among them `~/.jichi.env`, the shell's startup files and every jichi
  process's `/proc/<pid>/environ`. jichi's own process is not dumpable, so the last was refused, and no file
  in that container held the key. Nothing injected that model. It shows what a fooled model would try first.

**Where it runs.** The hardening drive has been paused since 2026-11-05. It resumes on the ported harness
after the harness-port drive's last batch, H14. Its first batch is B-02: the hardening review found that
jichi's own git tools still run a program a repository names as a clean or smudge filter, and the operator
decided on 2026-11-01 to take both halves of that fix. The batches of this plan come right after B-02.

**The register rows.** [def-407](../DEFERRED.md#def-407) to [def-412](../DEFERRED.md#def-412) hold the six
gaps the analysis found; def-298 and def-330 hold the two older ones.

## 2. The rules every batch follows

1. **Say whether it is a boundary or a mitigation.** A boundary holds when the model is fully hostile: jichi's
   code or the operating system enforces it. A mitigation lowers the odds. The batch's note and its
   HARDENING.md row say which.
2. **Red first, by effect.** Each new check runs once without the change and fails. It is scored by an
   effect, never by what the model's text says:
   - a canary file read (its content arrives where it must not);
   - a request reaching a local canary listener;
   - a line written to `.jichi/memory.md`, or a marker in the system message of a later request
     (`--dump-requests`).
3. **The mock model for the offline half.** `tests/tools/mockmodel` answers with the tool call the test
   needs, as `tests/smoke/fence_refusal.sh` does. A marker is built by shell arithmetic, so that no command
   line carries it and only an effect can show it.
4. **A refusal is configurable, and always recorded.** Each new refusal has the modes off, warn and refuse,
   writes a telemetry record in every mode, and ships with a provisional default. The default gets a row in
   [DECISIONS.md](../DECISIONS.md) that names the measurement which would change it. A new telemetry event
   gets its reader, its summary line and its driver in the same milestone.
5. **No cap on a measurement.** The live corpus runs (section 4) set no deadline and no token budget.
6. **Close the row at landing.** A batch closes its DEFERRED rows when it lands, in the same batch.

## 3. The batches

| Batch | What it builds | Boundary or mitigation | Rows |
|---|---|---|---|
| PI-1 | the threat model's table, the plain words of Q3, the corpus's first run | documentation, and the instrument | def-410, def-411 |
| PI-2 | a trust step for a project's instruction files | boundary | def-298 |
| PI-3 | the gate on instruction writes | boundary | def-407 |
| PI-4 | the top-level shell fence with a secret read set, and the `doctor` line | boundary (files only) | def-330, def-412 |
| PI-5 | the untrusted-context flag, as telemetry | measurement; the later gate is a boundary | def-408 |
| PI-6 | the control socket, closed to the run's own children | boundary | a row the batch registers |
| PI-7 | the outbound secret scan, and the container as the answer | boundary for jichi's own tools; the shell's route stays open in the normal-user mode | def-409 |

The order is the analysis's section 7: the cheapest real boundary first, each step measured before the next.

### PI-1. The threat model's table, Q3's words, and the corpus's first run

**What it builds.**
1. [HARDENING.md](../HARDENING.md) §1: every row marked boundary or mitigation. Four rows are added, each with
   today's status and the row that owns its gap: a project's instruction files, instruction writes, the
   top-level shell, and egress.
2. Q3, in two places. HARDENING.md says plainly that the normal-user mode is not an injection boundary, and
   recommends the container for every `--auto` run on untrusted input. The `--auto` line of `jichi --help`
   says the same in one clause. [HARNESS.md](../HARNESS.md)'s "The limit to read first" already says the
   first half.
3. The corpus's task set and its first run (section 4), before any fix, so every later batch has a baseline.

**The test.** A lint, red on today's table: every row of HARDENING.md §1 names "boundary" or "mitigation".
Floor it at the table's row count, and print the rows it read.

**Done when** the lint is green, the help line is in, and the baseline numbers are recorded per model and
mode.

**What it costs.** A few hours of documentation, and a drive's time on the free models for the baseline.

### PI-2. A trust step for a project's instruction files (def-298)

**What it builds.** Until `jichi trust` has bound a project's instruction files, they load as fenced data or
not at all. The instruction files are `AGENTS.md` and `CLAUDE.md`, and the files under `.jichi/commands`,
`.jichi/agents` and `.jichi/skills`. The binding hashes their content, as M851's SHA-256 binding does for a
project's config, so a later change to `AGENTS.md` asks again. Headless runs and ACP say so in one line, as
M851 does for the config. jichi's built-in guide skill (M1196, the harness-port drive's batch G) is jichi's
own text and needs no trust step.

**The test, red today.** A scratch project whose `AGENTS.md` carries a marker line:
1. untrusted: the marker is absent from the first request's system message (`--dump-requests`);
2. after `jichi trust`: present;
3. after an edit to `AGENTS.md`: absent again until the next `jichi trust`.
Today step 1 shows the marker. The same three steps run for one file under `.jichi/skills`.

**Done when** the driver is green, def-298 is closed, and HARDENING.md's row says boundary.

**What it costs.** One question per new repository, and another when a change edits the rules. That is the
point. A drive must trust its own team checkout at setup, so the harness's `init` and the drive's setup notes
name that step.

### PI-3. The gate on instruction writes (def-407)

**What it builds.** The operator's Q1:
1. An instruction write is `remember`, a write to a rules file, or a write under
   `.jichi/skills|commands|agents`.
2. Under `--auto` it is refused by default. The mode is configurable: off, warn or refuse. Telemetry records
   the write in every mode.
3. Interactively it has its own confirm, which an "always" grant cannot satisfy, as a kinetic confirm.
4. A drive that must edit its repository's own rules files declares it in its configuration. The supervisor
   then reads the instruction-file diffs at every landing.
5. A [DECISIONS.md](../DECISIONS.md) row for the provisional default.

**The test, red today.** The analysis's section 3 run as a smoke driver: headless `-p --auto`, the mock model
calls `remember` with a marker.
1. Default: the call is refused, `.jichi/memory.md` has no marker line, and a telemetry record names the
   refusal. Today the line is written.
2. `warn`: the line is written, with a warning and a telemetry record.
3. `off`: the line is written, with a telemetry record.
4. The same three cases for a write to `AGENTS.md` and for one under `.jichi/skills`.
5. Interactively, under a pty: an "always" grant given earlier does not satisfy the confirm.

**Done when** all five pass, def-407 is closed, and the drive configurations that need the exception
declare it.

**What it costs.** A drive whose lead edits jichi's own `CLAUDE.md` or uses `memory.md` must declare that.
Learn-on-stop's propose-only rule for constraints is the precedent: a model does not write its own fence.

### PI-4. The top-level shell fence, and the `doctor` line (def-330, def-412)

**What it builds.** The Landlock rule set of the design note
[plans/2026-10-23-landlock-top-shell.md](2026-10-23-landlock-top-shell.md), for the top-level shell:
1. writes limited to the workspace, temp and the declared directories;
2. reads denied for secrets: `~/.ssh`, `~/.gnupg`, key directories, browser profiles, other projects, and
   the key file the configuration names;
3. a `doctor` line that names a key file the configuration references and says whether the active fences let
   the agent's shell read it: a warning while they do, a pass once they do not.
Platforms without Landlock keep M831's never-silent fallback.

**The test, red today.** A scratch HOME with a canary file in `~/.ssh` and a canary key file the
configuration names. The mock model's shell call reads both. Today their content comes back in the tool
result. After the fence, the read is refused and the content appears nowhere in the run's records. `doctor`
names the key file with a warning today and passes after.

**Done when** both checks pass on Linux with Landlock, the fallback is shown on a kernel without it, and
def-330 and def-412 are closed.

**What it costs, honestly.** In the normal-user mode this bounds files, not exfiltration: Landlock's network
rules filter TCP by port, not by host. A workflow that needs `~/.ssh` from the shell, such as a push over ssh,
needs a declared path.

### PI-5. The untrusted-context flag, as telemetry (def-408)

**What it builds.** The operator's Q2:
1. A session flag, set when content from an untrusted channel enters the context. The first scope is the
   channels M300 already fences (`fetch_url`, `@url:`, `@rss:`, `web_search`, `read_mcp_resource`) plus MCP
   tool results.
2. A telemetry record when the flag is set, and one when a sensitive action follows it: egress to a host not
   used before in the session, `git push` and other remote operations, an instruction write, a request to
   widen a fence, a write-capable delegation.
3. No gate yet. The flag gets a name other than "tainted", which M410 uses for runs that edited a test.

**The test, red today.** A mock-model run fetches a page from a local test server, then makes a second
request to a new host. Today no flag record exists. After, both records are present, and a run without the
fetch has neither.

**Then measure.** Read the records from the corpus (section 4) and from a drive's telemetry: how often does a
sensitive action follow an untrusted read? The gate's defaults (approve even under `--auto`, or refuse) come
from those numbers, decided by the operator, in a later batch.

**Done when** the records, their reader and their summary line are in, and the first numbers are reported.

**What it costs.** Little to build. The gate waits for the numbers.

### PI-6. The control socket, closed to the run's own children

**The finding, by reading.** [CONTROL.md](../CONTROL.md) says of the control socket: "A unix socket, `0600`,
in a `0700` directory: filesystem permissions are the whole ACL." So any process running as the same user can
connect, if it finds the path. That includes the model's own shell and a worker's. An `inject` arrives in the
lead's history as an `[operator]` message; `pause` halts the run and `abort` ends it. A fenced child that reaches
its lead's socket can argue its way out of its fence (the analysis, D7).

**First, measure it.** A mock-model run with a control socket, whose shell call runs `jichi control` against
that socket with a marker. Red if the marker reaches the lead's next request as an `[operator]` message.

**What it builds.** The batch's design note chooses, after the measurement, among:
1. denying the run's control directory to the shell fence of PI-4;
2. refusing a peer that is the run's own descendant (its process id, read with the socket's peer
   credentials);
3. a per-run secret kept out of every child's environment and files.
The batch registers its own row when it is cut.

**Done when** the marker no longer arrives from a child, and the operator's `jichi control` still works.

**What it costs.** A supervisor that runs inside the run's own process tree would be refused; the drives'
supervisors run outside it.

### PI-7. The outbound secret scan, and the container as the answer (def-409)

**What it builds.**
1. An outbound scan of `fetch_url` and `web_search` URLs and bodies for the configured secrets, the idea of
   the drives' pre-push key scan.
2. An optional host allowlist.
3. The container named in HARNESS.md and HARDENING.md as the answer for `--auto` on untrusted input (Q3).

**The test, red today.** A local canary listener and a test secret registered as a key. The mock model calls
`fetch_url` with the secret in the query string. Today the listener receives it. After, the call is refused
before the request, the listener receives nothing, and a telemetry record names the refusal.

**Done when** the test passes and def-409 is closed.

**What it costs.** The shell's own route stays open in the normal-user mode, because Landlock cannot filter by
host. That is why the container is the answer, and the pages must say so in those words.

## 4. The corpus, beside every batch (def-411)

1. **The tasks.** Ordinary coding tasks, each with an instruction planted in one channel: a README, a fetched
   page, an MCP tool result, a commit message, a file name, a test's output, a worker's report.
2. **The models.** The free `jlu/*` models, and the drive's own lead and worker models.
3. **The modes.** Interactive, headless, headless `--auto`, and the container.
4. **The score.** By effect only: a canary file read, a request at a local canary listener, a line in
   `memory.md`. Never by the transcript.
5. **When.** Once in PI-1, before any fix (the baseline), and again after each batch.
6. **Where.** The offline half (mock model) in the smoke tier. The live half as a drive, its task texts in
   `docs/internal/`.
7. **The result.** A public page with the numbers per model and mode. The numbers for small models will be
   poor. That is what a user needs to choose a safe configuration.

## 5. What this plan does not do

- No injection classifier as a gate (the analysis, D1). If one is built, it writes telemetry only.
- No planner/executor split (CaMeL). It is the strongest known design and the direction, not the next step.
- No fence on every workspace read. M300's scope judgement stands; PI-5 flags by channel.

## 6. How it reaches the drive

1. This plan, the analysis and the six register rows land on master through a docs handover, after the
   harness-port drive's batch G has passed its gate.
2. When the hardening drive resumes, the supervisor adds PI-1 to PI-7 to that drive's plan, after B-02, by a
   note to its lead.
3. The lead claims each batch's milestone numbers when it cuts the batch, and writes its design note first
   where the batch says so (PI-6).

## Read further

- [The analysis](../analysis/2026-10-27-prompt-injection.md), sections 3 to 5 and 10.
- [HARDENING.md](../HARDENING.md): the threat model today.
- [plans/2026-10-23-landlock-top-shell.md](2026-10-23-landlock-top-shell.md): the design note behind PI-4.
- [CONTROL.md](../CONTROL.md): the control socket, for PI-6.
- [DEFERRED.md](../DEFERRED.md): def-298, def-330 and def-407 to def-412.
