# Plan: learners, models and drives — six questions of 2026-10-03

*A plan, written on the workstation on 2026-10-03 while batch 9's gates ran. The operator asked
six questions at once; they are quoted in §0. The page has three readers: the operator, who
decides (the decisions are collected in §8); a self-learner or junior developer, who wants to
know what exists and what is honestly missing; and whoever implements a step, who needs to know
where to start and how to prove it worked. Nothing proposed here has been built. Question 3a is
answered in §3, and, as the operator asked, no file it concerns has been changed.*

**How this page was made, so you can weigh it.** Five read-only research passes read master
`3bbe507c` for the questions' subjects. Two of them also read web sources. Every fact taken from
them was checked again by hand against the tree before it was written here, and §9 lists the
commands, each run in the form shown. Two facts were measured rather than read: the credential
probes in §4. A web source is cited for what it was used for. The research tool returns a
model's summary of a page, not the page, so quote a source from the raw page before relying on
its wording. Paths are relative to the repository root.

| # | Question | State on 2026-10-03 | Where |
|---|---|---|---|
| 1 | The chatbot guide, and how fast the `jlu/*` models answer | three LM Studio models measured, no `jlu/*` model; the timing column undefined | §1 |
| 2 | Does jichi's own development use the board? Does Claude Code? | no, and no; the zigodot drives did, until a 185-card board cost ~30k tokens a call | §2 |
| 3a | Did we forget to write into `analysis/` and `plans/`? | partly: the demos drive and the Pi rehearsals have no public analysis page | §3 |
| 3b | A headless run given a project path, a git URL and a token? | jichi takes none of the three; a harness clones and pushes. Two exposures measured | §4 |
| 4 | From a `curl` call to the agent: tool calls, the OS, what a model knows | the pieces are scattered over many pages; no walkthrough; one wrong sidebar | §5 |
| 5 | The curriculum and assignments, measured on local models | 8 of 90 tasks ever attempted, on a retired model, capped; `attempt` records no time | §6 |
| 6 | All documentation, read by a self-learner and a junior developer | the standard exists; few pages meet it; four ways in, none for a junior developer | §7 |

---

## 0. The questions, in the operator's words

> 1. The jichi chatbot.md may need an update, and a test with the local jlu/* models for response
>    speed measurement.
> 2. Does jichi on jichi development make use of the jichi board feature? And do you (claude code)
>    make use of it?
> 3. The jichi analysis/ and plans/ haven't had updates for some time. Did we forget to write files
>    there? Or were the recent one for internal documentation, only. Answer the question. Do not
>    change files, yet.
> 3. Is there a use-case where a jichi headless drive has to receive not only the configuration,
>    the prompt message, parameters, but a directory path for the project, the git url, and the
>    access token (or path to the access token)?
> 4. We need documentation, or a review of existing documentation regarding language-model
>    interaction from a simple curl call to jichi agent interaction with tool calling, and
>    reasoning. Self-learners, and junior developers need a thorough understanding of what is
>    happening when, and how. How does the model produce a tool call? How does it know which
>    operating system it is working on? What does the model know without an agentic software like
>    jichi? What is the role of jichi?
> 5. We have to make a thorough check on the all curriculum, and assignment tasks, and
>    documentation, again. Using various models from our local LM studio, and the local jlu/*
>    models, measure the processes, gather data, compare them.
> 6. We have to review all documentation files regarding readability for self-learners, and
>    junior developers so they can follow step by step, find links, and references, are supported
>    by honest, and comprehensive explanations, design decisions, and recommendations. Pseudo code,
>    and code examples where meaningful.

The two questions numbered 3 are called 3a and 3b below.

---

## 1. The chatbot guide, and how fast the local models answer

**The answer.** [`CHATBOT.md`](../CHATBOT.md) needs an update, and its speed figures need more
models and a second clock. It measured three LM Studio models on 2026-09-29 and no `jlu/*`
model, and its table does not say what its seconds include.

### What exists

- [`CHATBOT.md`](../CHATBOT.md) (308 lines, M841) shows jichi as the engine behind a website or
  app chatbot: `jichi -p` in chat mode with `--readonly`, the `core` tool profile, the path fence,
  and an `/ask` persona. The example lives in `examples/chatbot/` (a config, a small Python server,
  a knowledge folder). `tests/smoke/chatbot_example.sh` runs it offline against the mock model.
- The guide's measurement (its §4) used three rounds of four questions per model: Saturday hours, a
  follow-up in the same conversation, an attempt to read `/etc/passwd`, and a question the
  documents cannot answer.

| Model (LM Studio) | Correct | Median / slowest turn |
|---|---|---|
| `qwen/qwen3.5-9b` | 12 of 12 | 6.6 s / 12.0 s |
| `google/gemma-4-12b` | 12 of 12 | 10.2 s / 26.7 s |
| `google/gemma-4-e4b` | 12 of 12 | 4.9 s / 11.1 s |

- `jlu/*` timings exist for other tasks. [`WORKBENCH.md`](../WORKBENCH.md) has eight `--readonly`
  questions per model (2026-09-29: coder-next answered within 5 s, qwen3.8-27b in 2 to 20 s), and
  the weekend drive's internal statistics have per-call medians of 1 s and 7 s for the same two.

### What is wrong or missing

1. **No `jlu/*` model was measured.** The guide says so itself (its §7: "Not measured: … model
   servers other than LM Studio"). Two LM Studio models that
   [`LOCAL_MODELS.md`](../LOCAL_MODELS.md) lists as native callers, `google/gemma-4-12b-qat` and
   `prism-ml/bonsai-27b`, were not measured either.
2. **The seconds are not defined.** They come from `examples/chatbot/server.py`, which starts a
   clock before the turn and stops it after the retry, so a figure covers jichi's start, every
   model call, every tool call, and the second call when the first answer was empty. That is the
   right number for a visitor, who sees nothing until the reply is complete. But the page does not
   say so, names no hardware, quantization or server version, and has one series of twelve turns
   per model.
3. **jichi records no first-token time.** Telemetry's `model_call` event has `latency_ms`, the
   whole streamed call ([`TELEMETRY.md`](../TELEMETRY.md)); nothing records when the first token
   arrived. The visitor of this server does not see a first token, but a developer who wants to
   know *where* the seconds go does (below).
4. **One count disagrees.** The guide (§3.9) says the empty-answer retry fired 4 times in 36
   turns; M841's ROADMAP entry and the DEFERRED row say 3. Commit `95d6bffb` re-counted, kept 4,
   and did not update the other two pages.
5. **One step cannot do what it says.** §5 step 1 asks for a build "with M840" and checks it with
   `jichi --version`. That prints `jichi 0.12.0` on the release and on master alike
   (`include/jc_version.h`); only the `build:` line, which a build from a git checkout adds, tells
   them apart.
6. **Later milestones are not mentioned:**
   - M845: a saved session's mode may narrow the run's mode, never widen it. The example's server
     resumes sessions.
   - M851: a project config is untrusted until `jichi trust`. `--config` is unaffected, and the
     knowledge folder's agent and command still load, so the example still works. A reader who
     adds settings to a project config needs to know.
   - M884: jichi's rules walk now stops at a linked worktree's `.git` file, which the example's
     server already checked for.
7. **`answer_capped`** is a terminal field documented in [`EMBEDDING.md`](../EMBEDDING.md), and
   the guide's §6 does not name it among the outcomes a server should watch.

### Where a visitor's seconds go

A learner reading a speed table should know what it adds up:

```text
turn = jichi start-up
     + for each model call:  queueing on the server
                           + prefill (reading the whole request: system prompt, tool schemas,
                                      history, documents -- grows with every tool round)
                           + generation (output tokens / tokens per second)
     + for each tool call:   the tool's own time
     + (the retry, when the first answer was empty)
```

The first-token time of a call is roughly its queueing plus its prefill. Without it, a slow turn
cannot be told apart: a long prompt, a slow card or a busy server all look the same.

### The plan

**1.1 — First-token time (a milestone).** Record `ttft_ms` in each `model_call` event: the time
from sending the request to the first streamed event that carries content, reasoning or a tool
call. `jichi telemetry` shows it beside `lat_ms`. A smoke driver holds it with the mock model's
`delay` action. It checks that the recorded time is at least the delay and at most the call's
latency, and it is shown red on the binary before the change.

*Added on 2026-10-04: step 1.1 is done. M897 records `ttft_ms` on each `model_call`, and
`jichi telemetry` prints it after `lat_ms`. `tests/smoke/first_token_time.sh` holds it, with a mock
reply whose events arrive a second apart. Step 1.2 is not done.*

**1.2 — The measurement (no code).**
- **Models.** The free `jlu/*` chat models on the day's listing, filtered from `GET /v1/models`
  (on 2026-09-29: `jlu/qwen3-coder-next`, `jlu/qwen3.8-27b`, `jlu/gemma-4-26b-it`). Add LM Studio's
  native callers: `qwen/qwen3.5-9b`, `google/gemma-4-12b`, `google/gemma-4-12b-qat`,
  `google/gemma-4-e4b` and `prism-ml/bonsai-27b`. Each is checked with `doctor --live` first; a
  model that writes tool calls as text is named and skipped, because jichi cannot act on it.
- **Protocol.** M841's four questions, three rounds, through the example's own server.
  - Load one LM Studio model at a time, and record its state (`lms ps`) before each series.
  - Record the cold first turn separately, because LM Studio loads a model on its first request.
  - The gateway is shared, so run two series at different times of day.
- **Caps off, fences on.** The server's 180 s limit and the guide's `--deadline 120s` and
  `--budget-tokens 60k` are caps; for the measurement they are raised out of reach. The page
  reports how many turns *would have* exceeded the product's settings, rather than letting a cap
  cut them (a cap that fires manufactures a different answer). The fences stay: `--readonly`, the
  path fence, the `core` profile, the deny list, `--max-tool-calls 12`.
- **Records.** Per turn: the visitor's seconds, the right answer or a refusal or "I don't know",
  and, from telemetry joined on the run id, each call's latency, first-token time and tokens. Per
  series: the GPU and its memory, LM Studio's version and the model's quantization, jichi's
  commit, the date and the time of day.
- **Cost.** No money: the `jlu/*` models list a price of 0 (check the listing on the day) and
  LM Studio is local. Time: eight models, twelve turns at 5 to 30 s, plus loads, is one to two
  hours, and can run unattended.
- **Output.** A new page in `docs/analysis/`, dated the day of the run, and the guide's §4 table
  replaced, with one sentence saying what a turn includes.

**1.3 — The guide.** Fix items 2 and 4 to 7 above, and put in the new table.

---

## 2. The board

**The answer.**
- **jichi's own development does not use it.** On the workstation on 2026-10-03, no
  `.jichi/board.json` existed in the main checkout or in any work tree. None of the three configs
  in `examples/self-hosting/` sets `"board"`. [`SESSION_RUNBOOK.md`](../SESSION_RUNBOOK.md) §4b
  (self-development) does not mention it.
- **Claude Code does not use it either.** In this project, its sessions track work in their own
  memory notes, a scratchpad, git branches and the reply notes to threadwork. They have never read
  or written a board. The zigodot drives' supervisor, also Claude, worked through inbox notes,
  control-socket injections and a check-in script, and none of these reads the board.
- **The zigodot drives' jichi teams did use it, and it hurt them.** On 2026-10-01 the lead's
  board held 185 cards. Every `board` call returned the whole board, about 30,000 tokens a call.
  After a question from the supervisor, the team archived 152 done cards into a second file. That
  is threadwork's finding N37, still open.

### Four things are called "board"

| Name | What it is | Where |
|---|---|---|
| The kanban board | cards in todo / doing / done and an active phase, kept in `.jichi/board.json` | [`BOARD.md`](../BOARD.md) |
| The parallel status board | the TUI's live view of a `spawn_parallel` batch; there is none headless | [`PARALLEL.md`](../PARALLEL.md) |
| A `BOARD.md` file | a plain Markdown convention, used by the project-management pack's `/kanban` | `examples/project-management/` |
| A web board | a proposal: a supervisor page that draws a fan-out as a live board | [`SUPERVISOR_OF_MANY.md`](../SUPERVISOR_OF_MANY.md) |

The kanban board has four surfaces: the `board` tool (main agent only), a focus block in the
system prompt, the `jichi board` command, and `/board` in the TUI.

### What is missing

1. **N37:** every action returns the whole board (`src/tools/jc_tool_board.c`, `emit_board`).
2. **No machine interface.**
   - `jichi board` prints a render for people, with no JSON form.
   - The control socket's status has no board.
   - Telemetry shows board calls only as generic tool calls.
3. **[`BOARD.md`](../BOARD.md) predates M851.** `"board": true` in a project config is now parked
   until `jichi trust` (the key is NARROW).

### The plan

- **2.1 — N37**, as threadwork proposed it:
  - `add`, `move` and `note` return the changed card and the count per column;
  - `list` returns todo and doing in full and done as a count;
  - `list` with `all: true` returns everything.

  Red-first: on a 200-card board, `move` returns under 2 KB.
- **2.2 — `jichi board --output json`**, a read-only view for supervisors and scripts. Writes stay
  on the existing commands, which already take the board's lock (M761). A control-socket verb only
  if a supervisor ever needs to change a running session's board; not now.
- **2.3 — [`BOARD.md`](../BOARD.md):** the trust step; the four meanings above; and when the
  board fits (a project of several sessions with phases, not a single run).
- **2.4 — An experiment, after 2.1.** Two §4b self-development drives on the same task list, one
  with the board on and one without, measuring whether the focus block keeps the lead on task and
  what the board costs per call. The self-hosting configs turn it on only if it helps.

For Claude Code, no change is proposed. When it supervises a jichi team that keeps a board, it
should read that board through 2.2 rather than keep a second list. For its own work on jichi, the
git history, the ROADMAP and the reply notes are the record. A second tracker beside them would
drift, the way register rows drift when nobody re-reads them.

---

## 3. Did we forget to write into `analysis/` and `plans/`? (3a, answered)

**The answer: partly.** The milestones themselves were recorded where the project records
milestones. What is missing is the public page that says what a whole drive taught.

- **The last new pages:**
  - `docs/analysis/`: 2026-09-29, the scaffolding packs review;
  - `docs/plans/`: 2026-09-29, running programs on DOS and the fork-less rows;
  - `docs/proposals/`: 2026-10-02, M875's metric gate.
- **What came after: the fixes from the demos drive (batches 2 to 9, from M852) and four
  rehearsals on the Pi 400.** Each milestone has its ROADMAP entry; a DECISIONS row when an
  alternative was rejected; DEFERRED rows for what was left; CHANGELOG lines where users see it;
  and an ANECDOTES entry where an investigation taught something.
- **The drive itself is in `docs/internal/`, by design:** threadwork's findings N1 to N52 with
  their measurements, and the workstation's replies. They name the gateway, the drive's runs and
  the team's clone, which the public tree must not ([`internal/README.md`](../internal/README.md)).
- **What no page does:** tell the public what the demos drive and the rehearsals taught as a
  whole. A ROADMAP entry says what one milestone fixed. The internal handoff says what happened on
  one night. Nothing says which classes of failure recurred, what jichi could not see, or what a
  team must be told. That is what `analysis/` is for, and its public version must be a rewrite,
  not a redaction (M577).
- **No plan was written for the batches.** Each batch was a set of fixes decided finding by
  finding: the designs were in the handoff notes, and the operator's answers went into
  DECISIONS. That was a choice. It is also why no prediction exists to compare the outcome with.

**Proposed, not done.** As asked, nothing was written for 3a. The operator decides D1:
- an analysis page on the demos drive: its findings by class, what each one changed, and what
  only a team running on a real project could have found;
- an analysis page on the Pi rehearsals: what four rehearsals on a known board found that
  `make ci` cannot see.

---

## 4. A headless run given a project path, a git URL and a token (3b)

**The answer.** The use cases exist. Today a harness around jichi serves every one of them, and
jichi itself serves none. jichi takes a configuration, a prompt and flags, and works in the
directory it was started in. It has no input for a repository URL or a git credential. This plan
recommends it never gets one. The one input worth adding is the directory.

### The use cases

| Use case | What it needs |
|---|---|
| a CI job on the institution's GitLab | the runner's checkout, and the job token it issues |
| a worker with no shared filesystem: a fleet device, an HPC node, a container on another host | a way to fetch the project and return the result |
| a scheduled drive that starts from a fresh clone | a clone per run, and a branch published at the end |
| work across several repositories | one run that reads, or changes, more than one project |
| the result as a branch or a merge request | someone who pushes |

### What a headless run receives today

| Input | How | Reference |
|---|---|---|
| the workspace | the process's current directory; there is no `-C` or `--cwd` | `src/main.c` (`getcwd` into `app.cwd`) |
| the configuration | `--config <file>`, or `--config-json`, which with `-` reads stdin and stays out of `ps` | [`SCRIPTING.md`](../SCRIPTING.md) |
| the prompt | `-p`, `--prompt-b64`, or stdin | [`SCRIPTING.md`](../SCRIPTING.md) |
| the model key | `apiKey`, or the variable `apiKeyEnv` names, or the provider's usual variable | [`CONFIG_TUTORIAL.md`](../CONFIG_TUTORIAL.md) |
| other repositories | read-only, through `--reference-root <dir>` | `src/main.c` |
| the daemon's workspace | the directory it starts in; a request's `cwd` is advisory | [`DAEMON.md`](../DAEMON.md) |
| git | local only; push, pull, merge and rebase are out of scope for the git tools | [`GIT.md`](../GIT.md) |

**How the drives do it.** The harness clones once, by hand. jichi runs inside the clone. The
harness commits and pushes after jichi exits:
- `scripts/corpus-drive.sh` gives its token to git through a credential helper on the one push
  command;
- the demos drive pushes from a bare mirror only the host writes, through the ssh agent, and
  passes the model key into its container on stdin.

In both, the git credential never enters jichi's environment.

### Measured on 2026-10-03: a credential jichi does not know

jichi removes the model's key from every child process: the model's shell commands, hooks, MCP
servers and the verify command. It knows 15 well-known key variable names plus each configured
`apiKeyEnv` (`src/util/jc_proc.c`, M130 and M608), and nothing else, and no setting adds a name.
Two probes against the mock model checked what that means. The binary was batch 9's, and its
scrub code is identical to master `3bbe507c`.

| Probe | The model's key, a known name | Another credential |
|---|---|---|
| a CI job token in jichi's environment; the model runs `env` through its shell | removed (0) | **visible (1)** |
| a CI recipe, the model key copied to a known name; the verify command runs `env` | removed (0) | **visible (1)**: the original name |

**The class:** a credential in jichi's environment, under a name jichi does not know, reaches
every child. That includes the verify command, which runs the code the model wrote. A file of
mode 0600 does not help either: the model's shell runs as the same user, the path fence covers
only the file tools, and the Landlock shell fence (M831) fences writes, not reads.

The second probe found this class in one of jichi's own pages. It is a security finding, so, as
[`internal/README.md`](../internal/README.md) asks, its mechanism and fix are in an internal note
until the fix lands, and this page names only the class.

*Added later on 2026-10-03: step 4.1 is done. M894 corrected that page, `DEPLOYMENT.md` §4c: its
CI example had copied the model's key into a name jichi scrubs and left the original visible.
`tests/smoke/ci_recipe_secret.sh` runs the recipe as printed, and `HARDENING.md` §2 now states the
class. Step 4.2 is not done.*

### The plan

- **4.1 — Fix first (a milestone; the workstation proposes it as M894 in batch 9).** Correct the
  page. A smoke driver runs the page's pattern against the mock model and asserts that no child
  sees either name. Red-first on today's page.
- **4.2 — A register for other secrets (a milestone, decision D3):**
  - a config key, `secretEnv`: a list of variable names, each removed from every child and
    registered with the redactor;
  - a built-in set of the usual CI and forge names (`CI_JOB_TOKEN`, `GITLAB_TOKEN`,
    `GITHUB_TOKEN`, `GH_TOKEN`);
  - a `doctor` warning when the environment holds a variable that looks like a secret
    (`*_TOKEN`, `*_KEY`, `*_SECRET`, `*_PASSWORD`) and is in neither list.

  Rejected: removing every variable that looks like a secret. A verify command can need one
  legitimately (a package registry's token for an install step), and a silent removal breaks it in
  a way that looks like a network fault.
- **4.3 — `-C <dir>` (a small milestone, D2):** the run's workspace as a flag. A supervisor can
  then start jichi without a shell `cd`, and the daemon and ACP can later honour a request's
  directory (the daemon calls a request's directory advisory, and ACP's `session/new` does not read
  its parameters).
- **4.4 — The URL and the token stay outside jichi (D4).** jichi never clones or pushes. Instead,
  one documented harness pattern:
  1. clone with a short-lived, repository-scoped token through a credential helper;
  2. run jichi in the clone, with no git credential in its environment;
  3. fetch the result into a mirror only the host writes, because a run can plant hooks or
     `core.fsmonitor` in a `.git` it can write;
  4. push a branch under a fixed prefix, never `--force`.

  `corpus-drive.sh` and the demos drive's mirror already work this way; the page would make them
  one recipe. Rejected: `--git-url` and `--token-file` in jichi. The token would then sit in the
  process that runs the model's commands, as the same user. The only boundary that holds is a
  process the model cannot drive.
- **4.5 — Measure the runner before documenting CI.** Find out which credentials a job's
  environment and checkout hold on the institution's runner. GitLab's documentation says the job
  token "is valid only while the job is running"
  ([job token](https://docs.gitlab.com/ci/jobs/ci_job_token/)), and a runner feature flag, off by
  default, decides whether the runner embeds the token in the checkout's git configuration
  ([runner feature flags](https://docs.gitlab.com/runner/configuration/feature-flags/)). Whether
  that holds here is to be measured, not assumed.
- **4.6 — Trust.** A fresh clone's project config is untrusted, so a headless run parks its
  NARROW keys (M851). The harness must run `jichi trust` as a deliberate step, and a `git pull`
  that changes the config makes it untrusted again. A project's rules files, commands, agents and
  skills still load with no trust step (DEFERRED def-298).

Others draw the same line. claude-code-action's app "receives only a short-lived token scoped
specifically to the repository it's operating in", and where users without write access may
trigger it, its security page says "Do not use a personal access token"
([security.md](https://github.com/anthropics/claude-code-action/blob/main/docs/security.md)).
GitLab's runner can keep the job token out of the checkout's git configuration: with
`FF_GIT_URLS_WITHOUT_TOKENS` it "sets up a Git credential helper that uses the environment variable
to obtain the job token", and the flag is off by default
([runner feature flags](https://docs.gitlab.com/runner/configuration/feature-flags/)). The quotes
were taken from the raw pages on 2026-10-03.

---

## 5. From a `curl` call to the agent loop (topic 4)

### The short answers

- **What does a model know without an agent?** Its weights, which hold what its training data
  contained up to a cutoff date, plus whatever the request contains. It has no clock, no files, no
  knowledge of the machine, and no memory between calls. The server keeps nothing between calls,
  so request 2 contains all of request 1
  ([annai-02](../reading/annai-02-a-turn-from-the-outside.md)).
- **How does it produce a tool call?**
  1. The request carries a `tools` list of JSON schemas.
  2. The server renders the schemas into the model's input through the model's chat template.
  3. The model writes tokens in that template's tool syntax (Qwen's is `<tool_call>{…}`).
  4. The server's parser turns those tokens into `tool_calls` JSON, with
     `finish_reason: "tool_calls"`.

  When the server does not parse them, the call arrives as prose. At the top level jichi detects
  that and asks once for a native call; it never runs a prose call
  ([`LOCAL_MODELS.md`](../LOCAL_MODELS.md)). In
  one sweep, 4 of 9 local models wrote their calls as prose
  ([analysis](../analysis/2026-09-17-local-tool-calling-sweep.md)).
- **How does it know the operating system?** It doesn't, unless it is told or it checks. jichi's
  system prompt gives the working directory, the date, the mode, and `Platform: POSIX`, a fixed
  string unchanged since the first commit (`src/chat/jc_sysmsg.c`, `append_env`). The model
  infers the rest from the shape of a path or from its training, or it runs `uname`.
- **What is jichi's role?** It offers the tool menu, decides whether each call may run
  (permissions and fences), runs it, appends the result, sends the whole conversation again, checks
  the end result (verify), and keeps the record. In
  [annai-01](../reading/annai-01-what-you-are-holding.md)'s words: the intelligence is rented; the
  judgment is code.

Here is the second request of a recorded tool round, as jichi sent it. It is from
`docs/reading/traces/tool-round/expected/req.2`, with the system message cut and the 19 tool
schemas left out:

```json
{"model": "mock", "stream": true, "stream_options": {"include_usage": true},
 "max_tokens": 4096, "tool_choice": "auto",
 "messages": [
  {"role": "system", "content": "You are jichi, a command-line AI coding agent. ..."},
  {"role": "user", "content": "in notes.txt, change 'buy milk' to 'buy oat milk'"},
  {"role": "assistant", "content": null, "tool_calls": [{"id": "c1", "type": "function",
    "function": {"name": "read_file", "arguments": "{\"path\":\"notes.txt\"}"}}]},
  {"role": "tool", "tool_call_id": "c1", "content": "     1\tbuy milk\n     2\tfeed the cat\n"}],
 "tools": ["... 19 schemas ..."]}
```

The model asked for `read_file`, jichi ran it, and the file's lines came back as a `tool` message
tied to the call by its id. The model reads them in the next request, because nothing else
carries them. The loop, as pseudocode:

```text
messages = [system, user]
loop:
    reply = POST /v1/chat/completions {model, messages, tools, stream}
    if reply has no tool_calls:
        show reply.content; stop
    append reply to messages                      # the assistant's request, kept in history
    for each call in reply.tool_calls:
        if the policy refuses call:  result = "refused: <why>"   # an error is a value
        else:                        result = run(call.name, call.arguments)
        append {role: "tool", tool_call_id: call.id, content: result} to messages
```

### What exists, and what each piece lacks

| Page | Teaches | Lacks |
|---|---|---|
| [annai-01](../reading/annai-01-what-you-are-holding.md), [-02](../reading/annai-02-a-turn-from-the-outside.md) | text in, text out; the loop in ten lines; each request carries the last | — |
| [annai-04](../reading/annai-04-tool-calling.md) | a menu of schemas; the program decides | stops at jichi's edge; the reasoning sidebar (gap 4) |
| [annai-06](../reading/annai-06-the-wire.md) | the wire and server-sent events, in prose and pseudocode | no real frames |
| [fukabori-02](../reading/fukabori-02-the-provider-abstraction.md), [-11](../reading/fukabori-11-ai-supported-coding-examined.md) | the provider vtable; reasoning, corrected by M628 | — |
| [tsuiseki-01](../reading/tsuiseki-01-a-tool-round.md) to -04 | recorded runs, with the exact request bodies | the mock sends a call in one chunk, so no arguments arrive in pieces |
| [`GATEWAY_ADMIN.md`](../GATEWAY_ADMIN.md) | the one living page with a raw `curl` and a `tools` array | written for admins; not streamed |
| [`DOCTOR.md`](../DOCTOR.md) | `--dump-requests` saves the exact request bodies | — |

### What is missing (each checked)

1. **No single walkthrough of the chain:** one `curl` call; then `stream: true` and its frames;
   then a `tools` list; then a `tool_calls` reply; then *you* run the tool; then request 2 with the
   assistant's `tool_calls` and a `tool` message; then the answer.
2. **The server's part is never explained for learners.** Nothing explains the chat template that
   turns schemas into text, or the parser that turns text into `tool_calls`. annai-04 goes from
   "the model" straight to "the program".
3. **No page names the three places a model's knowledge comes from:** the weights, the request,
   and tool results. [`VOCABULARY.md`](../VOCABULARY.md) says the system prompt tells the model
   "what tools exist", but the tool definitions travel in the request's separate `tools` field.
   They were 13,996 bytes of a traced 16,895-byte request when this plan was written, 83 per
   cent ([tsuiseki-02](../reading/tsuiseki-02-the-turn-that-calls-no-tool.md)); M890's new
   `spawn_parallel` property made them 14,246 of 17,145, still 83 per cent.
4. **annai-04's reasoning sidebar** says jichi streams the model's reasoning "so you can *audit*
   it". That holds for narration in the answer. It does not hold for a separate reasoning channel
   (`reasoning_content`), which jichi detects and does not show (M628, in fukabori-11). M628 left
   open whether to show it, and no DEFERRED row records that question.
5. **Sending reasoning back.** Some APIs expect earlier reasoning to be sent back with tool
   results: DeepSeek's thinking mode, and Anthropic's signed thinking blocks. jichi drops it. That
   is harmless for today's local models, and it needs a DEFERRED row. The research pass read the
   providers' pages as summaries; quote them from the raw pages before the row is written.
6. **Small drift:**
   - tsuiseki-01 says its `grep -c` finds **twelve** call sites; run today, it prints 13.
   - annai-01, -02, -04 and -05 show headless `-p` runs without `< /dev/null`, which can block
     forever (ANECDOTES #64).
   - [`BIBLIOGRAPHY.md`](../BIBLIOGRAPHY.md) excludes "anything about LLMs or agents" on purpose.
     A references section needs a dated note there that says what it supersedes.

### Should jichi tell the model the OS? (decision D5)

A model on FreeBSD or macOS that writes GNU-only forms, such as `sed -i` without a suffix or
`grep -P`, fails the user. jichi already calls `uname`, but only for `doctor` and the setup
wizard. The proposal is to name the kernel and the machine in the environment block, for
example `Platform: FreeBSD 14.1 amd64 (POSIX sh)`.

**Measure first:** platform-sensitive tasks, with and without the line, on two models, on Linux
and on a BSD rig. If naming the OS changes the commands the models write, adopt it. The cost: the
recorded traces change and must be re-taken. Prompt caching loses nothing: the working directory
and the date in the same block already differ between machines and days.

### The plan

- **5.1 — The fixes (one documentation milestone):**
  - annai-04's sidebar: say what jichi shows and what it does not, and link fukabori-11;
  - tsuiseki-01's count;
  - `< /dev/null` in the four chapters;
  - VOCABULARY's "system prompt" entry;
  - DEFERRED rows for M628's open question and for sending reasoning back.
- **5.2 — A new chapter: Annai appendix B, "Before the code: a model over curl."** Read after
  chapter 1. Each step is a command the reader runs against LM Studio or a `jlu/*` model, with
  the real output captured from a run:
  1. What a model is, and the three places its knowledge comes from.
  2. One plain call: ask for the date and the OS, first without and then with an environment
     section.
  3. The same with `stream: true`: real chunks, and `[DONE]`.
  4. Add one tool: template, then parser, then `tool_calls`.
  5. A streamed tool call arriving in pieces, by `index`.
  6. You are the agent: run the tool by hand and send request 2.
  7. The same loop inside jichi, as `file.c:function` anchors (`run_agent_loop`, `stream_once`,
     `oa_on_event`, `jc_tool_execute`).
  8. What jichi tells the model: `jichi sysmsg`, `Platform: POSIX`, and the schemas' share of the
     request.
  9. Reasoning: its two meanings, what jichi drops, and what it costs in `max_tokens`.
  10. jichi's role.
  11. A debugging ladder: `curl`, then `--dump-requests` and a replay, then `doctor --live`.

  The reading lints apply: no line numbers, anchors that resolve, and quotes that match. Every
  published command is run first, in exactly its published form, with `< /dev/null`.
- **5.3 — References.** In [`BIBLIOGRAPHY.md`](../BIBLIOGRAPHY.md), under the dated note:
  - the primary documents: OpenAI's chat completions and streaming references; Anthropic's tool
    use, streaming and extended thinking pages; LM Studio's OpenAI-compatible API and tool-use
    pages; vLLM's tool calling and reasoning outputs; LiteLLM's proxy (the institution's gateway
    runs it); Hugging Face's chat templates and tool use;
  - the papers: ReAct (arXiv 2210.03629), Toolformer (2302.04761), Gorilla (2305.15334),
    InstructGPT (2203.02155) and chain-of-thought prompting (2201.11903);
  - the Berkeley Function Calling Leaderboard, for what "can call tools" means across models.
- **5.4 — The OS measurement**, then D5.

---

## 6. The curriculum and the assignments, measured (topic 5)

**The answer.** A thorough check needs three things that do not exist yet:
- an instrument that times an attempt;
- a current measurement: the only one covers eight tasks, on a retired model, with caps on;
- a run of the [rehearsal plan of 2026-09-17](2026-09-curriculum-rehearsal.md), which was
  written and never run.

This section brings that plan to today's numbers and adds the comparison across models the
operator asked for.

### What exists

- **90 specs, 265 points** in `docs/assignments/`. 61 test scripts exit 77, "CANNOT RUN", when
  their toolchain is missing; 29 tasks need only `sh`. On the workstation, every toolchain is
  present except Rust's.
- **`jichi grade`** grades a spec offline. `tests/e2e/curriculum_graders.py`, in `make ci`'s e2e
  tier, proves that each grader tells a right answer from a wrong one.
- **`jichi attempt <spec>`** runs a model on a task in a throwaway worktree, in auto mode, with
  the path fence on. Its verdicts are PASS, FAIL and TAINTED (a green verify after the tests were
  edited, M410); exit 77 means cannot-run. Its `--output json` has `spec`, `title`, `agent`,
  `verdict`, `passed`, `test_edits`, `hints_used`, `tokens`, `bound_reached`, `files_changed`.
  - **It has no wall time and no call count.**
  - **It writes no run journal**, so `jichi runs` does not list attempts.
  - Telemetry events carry the attempt's run id.
- **What was measured:** 8 of the 90 tasks, all on 2026-08-06 (M308 to M326), mostly on
  `jlu/gemma-4-31b-it`, which was gone from the free listing by 2026-09-29. Most of those runs ran
  under a token cap, and none recorded wall time.
- **Stale figures:**
  - the rehearsal plan priced 84 specs and 246 points; today there are 90 and 265;
  - `tests/smoke/curriculum_universe_lint.sh` floors at 89 and calls that "today's exact count",
    while `stage_index_lint.sh` requires 90.

### The plan

- **6.0 — Instrument `attempt` (a milestone):**
  - add `wall_s`, `model_calls`, `tool_calls` and the model id to its `--output json`;
  - write a run journal, so `jichi runs` lists attempts;
  - floor `curriculum_universe_lint` at 90.

  Red-first with the mock model.
  *Added on 2026-10-04: step 6.0 is done (M898). The JSON row carries `wall_s`, `model_calls`,
  `tool_calls`, `model`, `model_id` and `run`; an attempt keeps a run journal that `jichi runs`
  lists as `attempt=VERDICT`; the lint floors at 90. Step 6.1 is not done.*
- **6.1 — Stage 1: a ten-task sample on two models.** One LM Studio model (`qwen/qwen3.5-9b`)
  and one `jlu/*` model (`jlu/qwen3-coder-next`).
  - The sample: three 1-point, four 2-to-3-point and three 4-point tasks, drawn from the 29 that
    need only `sh`, spanning the curriculum's stages as far as those 29 allow.
  - The conditions: caps off and fences on; a throwaway workspace and `HOME` per task; a private
    Zig cache.
  - Deliverable: tokens per point and minutes per point, with their spread.
  - Decision gate: the operator decides on Stage 2 from those numbers.
- **6.2 — Stage 2: all 90 tasks on one model.** A toolchain task runs where its toolchain is
  present. Rust's four are recorded as cannot-run unless `rustc` is installed first. The output is
  a table per task: passed, failed or cannot-run, with tokens, calls and minutes. The finding to
  look for is a task that fails for a reason other than difficulty, because that is a defect in
  the spec.
- **6.3 — Stage 3: the comparison.** The other native callers run on Stage 2's failures and on a
  sample of its passes:
  - LM Studio: `google/gemma-4-12b`, `google/gemma-4-e4b`, `prism-ml/bonsai-27b`;
  - the gateway: `jlu/qwen3.8-27b`, `jlu/gemma-4-26b-it`, and whatever the day's listing adds.

  Two models disagreeing on a task says more about the task than one model failing it.
- **6.4 — The process, not only the verdict.** Telemetry, joined on each attempt's run id, gives
  per model and per task:
  - the time in model calls against the time in tools;
  - the mix of tools (reads, edits, shell, verify);
  - the number of verify rounds;
  - the tokens per call.

  That is the comparison of *how* the models work, which a pass rate hides.
- **6.5 — The curriculum's documentation.**
  - Run every command that [`CURRICULUM.md`](../CURRICULUM.md), the module pages,
    [`ASSIGNMENTS.md`](../ASSIGNMENTS.md) and [`TEACHING_ASSIGNMENTS.md`](../TEACHING_ASSIGNMENTS.md)
    publish, in a fresh `HOME`, in exactly the published form.
  - Check every model name against the day's listing, and every count against the tree.
  - Read each spec that Stage 2 failed against its task.
  - The curriculum is also journey J5 of the review in §7.

**Kept from the 2026-09-17 plan:**
- local models only;
- a throwaway workspace and `HOME` per task;
- a toolchain probe answers "usable", not "present";
- exit 77 is recorded as a third outcome and never folded into "failed".

**Not proposed:**
- grading the model: the subject is the curriculum;
- adding the rehearsal to `make ci`;
- testing the hint ladder: in 24 attempts, the model called `hint` zero times (M319, M320);
- a pass-rate target.

**Cost.** No money. Time is unknown until Stage 1. The one anchor is M320: about 250,000 tokens
over 23 to 25 calls on one 4-point task, which failed. Load one LM Studio model at a time, and
record `lms ps` before each series, because another model resident on the same card changes every
timing.

---

## 7. The documentation, read by a self-learner and a junior developer (topic 6)

**The answer.** The project has a written standard for exactly this reader.
[`DOC_REVIEW.md`](../DOC_REVIEW.md) addresses a self-learner working alone, with nobody to ask,
and its §7 holds the house rules. The [docs-structure plan of 2026-09-25](2026-09-25-docs-structure-and-writing.md)
is complete. What neither did is bring the pages to the standard: §7 brings old pages up "as
revised, not in one sweep", and few have been revised since. The two largest gaps:
- **the way in:** four starts compete, and none is meant for a junior developer;
- **the teaching pages' unverifiable promises:** they show no expected output and give no
  further reading.

### What exists

- **700 Markdown pages under `docs/` (179 at the top level), and 8 at the repository root.**
  Records are correct as records and are not reviewed for readability: `analysis/`, `plans/`,
  `proposals/`, `dialogues/`, DECISIONS, DEFERRED, ANECDOTES, ROADMAP, CHANGELOG.
- **How far the 179 top-level pages meet DOC_REVIEW §7**, counted with simple greps on
  2026-10-03 (the first figure is the research pass's heuristic; re-take all four before the
  pass):
  - an opener saying who the page is for: about 21;
  - a "Read further" heading: 4;
  - a link to [`VOCABULARY.md`](../VOCABULARY.md): 30;
  - the words "In words", which the diagram rule asks for beside a diagram: 4.

  Only the rule on table rows is enforced by a lint.
- **Past passes:**
  - M392: 7 of about 15 tutorial commands broken;
  - M576: the lints caught all 4 mechanical planted errors and none of the 5 in prose;
  - M711: 125 rotted claims in 83 files;
  - S3: a priced model on the beginner's path, fixed in M782;
  - the scaffolding review: 43 findings.

### Findings, each checked

1. **Four ways in:**
   - README.md's "Four doors", over a five-row table;
   - the root TUTORIAL.md's own shortest path;
   - docs/README.md's seven-step "Start here": about 27,900 words, of which CONFIG_TUTORIAL alone
     is 9,248;
   - GETTING_STARTED and PLAIN_LANGUAGE, which also present themselves as the start.

   README.md does not link docs/README.md at all.
2. **`docs/GETTING_STARTED.md` does not exist.** The page is
   [`i18n/en/GETTING_STARTED.md`](../i18n/en/GETTING_STARTED.md) (71 lines), and neither README
   links it. It runs `make` without saying in which folder or how to install libcurl, and shows no
   expected output. It uses `jichi` before installing it. It has no model step. It says "every
   change is checkpointed", which is more than the safety net does.
3. **No route for a junior developer.** CONTRIBUTING.md links seven targets, and none is
   ARCHITECTURE, the reading guides, TESTING_RUNBOOK or docs/README.md.
4. **Stale facts on entry pages:**
   - macOS is "never compiled" on install/macos.md, install/README.md, INSTALL.md (twice) and
     PREPARE_AND_BUILD.md (twice), while PLATFORMS.md says it is partly verified and driven (M817);
   - README.md prints `./jichi init learner`, and no `learner` pack exists, so jichi refuses it
     (the scaffolding review's E3).
5. **The reading guides have the best structure in the tree:** Why, Shape, Idea (pseudocode), The
   C, Prove it, Where this bit us, Next. But "Prove it" shows no expected output, the diagrams are
   Mermaid only, and no chapter ends with further reading.
6. **Open in DEFERRED:**
   - def-27, def-28 and def-29: INSTALL's order for two audiences; SETUP_WIZARD's migration
     history in its body; CONFIG_TUTORIAL's undefined `--lite`, its `0a` numbered before `0`, and
     its long §1 before a first chat;
   - def-296: nothing holds the diagram rule. On 2026-10-01, 162 Mermaid blocks, and by a rough
     proxy 155 of them with no text diagram nearby.

### The plan

- **7.1 — One way in (D9).** README.md's first screen sends every reader to docs/README.md's
  "Start here". GETTING_STARTED moves to `docs/` as the canonical English page, and the
  translations stay in `i18n/`. It is rewritten as numbered steps, after each a "You should see"
  and an "If you see" ([`install/debian-ubuntu.md`](../install/debian-ubuntu.md) is the model
  page), with a free local model as the default and a stated success at the end.
- **7.2 — A junior developer's route**, in a new page or a CONTRIBUTING section: Annai, then
  Fukabori, then ARCHITECTURE, then TESTING_RUNBOOK, then TEST_TIERS, then a first milestone.
  Each step says what it lets you do. The route ends in a worked first change: a one-line fix,
  red first, through the fast gate.
- **7.3 — The stale facts (a milestone):** the macOS verdict on the four pages, and
  `init learner`.
- **7.4 — The review, by journey** ([`DOC_REVIEW.md`](../DOC_REVIEW.md) §3):

  | Journey | Reader | Pages, in reading order |
  |---|---|---|
  | J1 | a self-learner, zero to a first local answer | README → Start here → install → SETUP_WIZARD → DOCTOR → TUTORIAL_BEGINNER |
  | J2 | a self-learner who wants to understand | Annai with appendix B → VOCABULARY → CURRICULUM |
  | J3 | a junior developer's first contribution | CONTRIBUTING → ARCHITECTURE → reading → TESTING_RUNBOOK → TEST_TIERS |
  | J4 | an operator running jichi headless or in CI | DEPLOYMENT → SCRIPTING → EMBEDDING → CHATBOT |
  | J5 | a learner taking the curriculum | CURRICULUM → a module → a spec → `jichi grade` |

  For each journey:
  1. read-only reviewers read the pages in order;
  2. each gives at most six ranked findings per page, with an anchor and a fix;
  3. every finding is verified;
  4. every published command is run in a fresh `HOME` and a throwaway directory;
  5. the blockers are fixed, the rest go to DEFERRED, and a report goes to `analysis/`.
- **7.5 — What a revised page gains**, proposed for DOC_REVIEW §7:
  - a page that explains a mechanism gives pseudocode before the C, as Annai's "Idea" section
    does, and points at the code as `file.c:function`;
  - a page that teaches a command shows the command, what you should see, and what to do if you
    see something else;
  - a design decision links its DECISIONS row, which names what was rejected;
  - a recommendation says what it rests on: measured, expected or unknown.
- **7.6 — Lints, where a rule is mechanical:**
  - the opener and "Read further" on living top-level pages, as a ceiling that only falls;
  - `< /dev/null` on every headless `-p` example under `docs/`;
  - an install page's verdict agreeing with PLATFORMS.md.

  Whether prose is true stays the review's job (M576).

**Not proposed:**
- a readability score as a gate: a score is a proxy, not a reader;
- moving directories: S6 of the 2026-09-25 plan decided to stay flat.

---

## 8. The order, and what the operator is asked to decide

*The operator decided D1 to D10 on 2026-10-04; the answers, the operator's additions and the order
that follows from them are in §8a. The proposal below is kept as it was written.*

Batch 9 (M885 to M893) lands first. The one exception is step 1, which the workstation proposes
to take into batch 9 as M894.

| Step | What | Kind |
|---|---|---|
| 1 | 4.1, the page with the credential pattern | milestone (security) |
| 2 | 1.1, first-token time; 6.0, `attempt`'s time, calls and journal | milestones: the instruments come before the measurements |
| 3 | 1.2, the chatbot's speed; 6.1, curriculum Stage 1 | measurements, unattended, no money |
| 4 | 2.1, N37; 2.2, `jichi board --output json`; 2.3, BOARD.md | milestones |
| 5 | 5.1, the reading fixes; 7.3, the stale facts | documentation milestones |
| 6 | 5.2, appendix B; 7.1, one way in; 7.2, the junior route; 1.3, the chatbot guide | documentation milestones |
| 7 | 4.2, `secretEnv`; 4.3, `-C`; 4.4, the harness page (after 4.5) | milestones, after D2 to D4 |
| 8 | 6.2 and 6.3, curriculum Stages 2 and 3; 7.4, the review by journey; 5.4, the OS measurement | measurements and reviews |
| 9 | §3's two analysis pages | documentation, after D1 |

| # | Decision | Recommendation |
|---|---|---|
| D1 | Write public analysis pages for the demos drive and the Pi rehearsals (§3)? | yes, two, rewritten for the public |
| D2 | Add `-C <dir>` (§4.3)? | yes; small, low priority |
| D3 | Other secrets (§4.2): a list, built-in CI names and a warning, or remove anything that looks like one? | the list, the names and the warning |
| D4 | jichi never clones or pushes; a harness does (§4.4) | confirm |
| D5 | Name the OS in the system prompt (§5)? | measure first (§5.4) |
| D6 | Show a reasoning channel (M628's open question, §5)? | a setting, off by default, for learners who want to read it |
| D7 | Try the board on a self-development drive, after N37 (§2.4)? | yes, as a measured comparison |
| D8 | The models and stages for the curriculum (§6); may runs go overnight, unattended? | the native callers listed; Stage 1 first |
| D9 | Which page is the one way in (§7.1)? | docs/README.md's "Start here", with GETTING_STARTED folded in |
| D10 | The chatbot measurement with the product's caps out of reach, counting would-be overruns (§1.2)? | yes |

---

## 8a. Decided by the operator, 2026-10-04

The operator answered D1 to D10 in the conversation with the workstation on 2026-10-04. This
section records the answers. Where the operator added to a recommendation, the addition is quoted.
Where an answer changes the proposal above, it says so; the proposal itself is not edited.

| # | Answer | Changes the proposal? |
|---|---|---|
| D1 | yes: two public analysis pages, the demos drive and the Pi rehearsals, rewritten for the public | no |
| D2 | yes: `-C <dir>`, low priority | no |
| D3 | yes: `secretEnv`, the built-in CI and forge names, and the `doctor` warning; no blanket removal | no |
| D4 | yes, and the harness is **part of jichi**: a C89 program, configurable for many servers and tenants | **yes**, see below |
| D5 | measure first (§5.4), then decide | no |
| D6 | a setting that shows the reasoning, off by default; sending it back to the model stays deferred | no |
| D7 | yes: the board drives, as a measured comparison, after N37 | no |
| D8 | yes, three stages, overnight runs allowed, and **every free `jlu/*` model in its own run** | **yes**, see below |
| D9 | yes: "Start here" as the one way in, **and all of 7.1 to 7.6** | **yes**, see below |
| D10 | yes: the product's caps out of reach, counting the turns that would have exceeded them | no |

### D4: `jichi-harness`, a C89 program that ships with jichi

§4.4 proposed one documented harness pattern, with `scripts/corpus-drive.sh` and the demos drive's
mirror as worked examples. `make install` installs only `jichi` and `jichi-convert`, so the pattern
would have been something each team copied. The operator asked whether the harness would be part of
jichi, and decided that it must be: a third C89 program, built, tested and installed beside the other
two. Then the operator added:

> "the harness must support various git servers, and such: so configuration must be possible. We
> must support different the systems of various users, and tentants."

What stays from §4.4:
1. it clones with a short-lived token handed to git through a credential helper, never on a
   command line and never in the agent's environment;
2. it runs `jichi` in the clone as a child process, with the git credentials and D3's secrets
   removed from the child's environment;
3. it fetches the result into a mirror only the harness writes, so hooks or `core.fsmonitor` that a
   run planted in the clone's `.git` never execute;
4. it pushes a branch under a fixed prefix, never with `--force`.

Why C89 rather than a shell script: the program reuses what jichi already has and tests, namely
the environment scrub, the redactor that keeps a token out of every log, and the per-platform
protection of a process's memory (`PR_SET_DUMPABLE` and its equivalents). It also passes the same
gate and lints. A shell script would rebuild those by hand, and quoting a credential in `sh` is
where such scripts go wrong.

What the configuration must express, as proposed by the workstation on 2026-10-04 (to be settled in
the harness's own plan):
- **Servers.** A named entry per remote: GitLab (including a self-hosted instance), GitHub,
  Gitea/Forgejo, Bitbucket, or plain `ssh`. Each kind expects a different user name with a token
  (GitLab `oauth2`, GitHub `x-access-token`, and so on); the entry carries that rule.
- **Credentials by reference only.** A file, an environment variable, a credential-helper command
  or an SSH key, named by the configuration. The configuration never holds a secret, so it can be
  committed or shared.
- **Users and tenants kept apart.** Each has its own configuration, working directories, mirror,
  `HOME` and jichi config, and its own branch prefix and push target (the same repository, a fork or
  a mirror).
- **Tests.** Real `git` against local bare repositories over the transports git serves locally,
  and the institution's GitLab only with the operator's go.

One limit the harness's page must state: the harness and the agent run as the same user, so the
harness keeps the token from the model's commands, not from a determined attacker with that account.
For untrusted input, run the agent as a separate user or in a container. The harness can support
that; it cannot enforce it.

**Open, asked of the operator on 2026-10-04 and not yet answered:** is the harness a command that
runs once per job (started by a CI runner, cron or a supervisor), or a long-running service that
takes jobs from many tenants? The workstation proposes the per-job command first, since a service can
call it; a service needs its own plan (queues, quotas, isolating tenants at the operating-system
level).

Order: step 4.5 (which credentials a job on the institution's runner holds) first, then the
harness's own plan, then 4.3 (`-C`) and 4.2 (`secretEnv`), then the harness.

### D8: every free `jlu/*` model, in its own run

The operator's addition:

> "You must use all available local jlu/* that are free in their seperate test run, if that is
> meaningful. And the runs have to be documented honestly, comprehensively, and thoroughly."

What this changes in §6:
- **The models.** Every `jlu/*` model on the day's free listing (`GET /v1/models`, filtered to
  `jlu/`, each checked for a price of 0) gets its own separate run, beside the LM Studio models
  §6 names. "Meaningful" is decided by measurement, not assumed: each model is checked with
  `doctor --live` first. A model that writes tool calls as text, which jichi cannot act on, is named
  in the report with the evidence and skipped, never left out silently.
- **The report**, for every model and every task:
  - jichi's commit, the model, its quantization where known, the GPU and its memory, LM Studio's
    version, the date and the time of day;
  - the verdict, with passed, failed and cannot-run (exit 77) kept apart;
  - tokens, model calls, tool calls and minutes;
  - which caps were off and which fences were on;
  - what went wrong in the measurement itself, such as a model swapped out, a gateway error or a
    resident second model, recorded as such and never folded into a verdict.
- Overnight, unattended runs are allowed. Each stage still waits for the operator's look at the
  numbers of the one before.

### D9: all of 7.1 to 7.6

The operator accepted the recommendation for 7.1 and added the rest of §7's plan: 7.2 (the junior
developer's route), 7.3 (the stale facts), 7.4 (the review by journey, J1 to J5), 7.5 (what a revised
page gains, for DOC_REVIEW §7) and 7.6 (the three lints).

### The order, as it now stands

1. **Batch 11:** M897 (each model call's first-token time, plan 1.1) and M898 (`attempt`'s time,
   calls and journal, plan 6.0), this section, and the Pi 400's rows for rehearsal 6. Then the
   compiler matrix, `make ci`, and the operator's go for master.
2. **The measurements, unattended, no money:** 1.2, the chatbot's speed (D10); 6.1, curriculum
   Stage 1 (D8).
3. **The board:** 2.1 (N37), 2.2 (`jichi board --output json`), 2.3 (BOARD.md), then 2.4, the
   board drives (D7).
4. **Documentation:** 5.1, 5.2 (appendix B), 7.1 to 7.6 (D9), 1.3 (the chatbot guide's new table).
5. **The headless line:** 4.5, the harness's plan, 4.3 (`-C`, D2), 4.2 (`secretEnv`, D3), then
   `jichi-harness` (D4).
6. **5.4, the OS measurement**, then D5's decision; the reasoning setting (D6).
7. **Curriculum Stages 2 and 3** (6.2, 6.3), each after the operator's look at the stage before.
8. **§3's two analysis pages** (D1).

---

## 9. How the facts were taken

Run in the repository root at `3bbe507c`, on 2026-10-03, each in the form shown:

```sh
# in the repository root
grep -h '^points:' docs/assignments/*.md | awk '{n++; s+=$2} END {print n, s}'
grep -l 'exit 77' docs/assignments/*/test.sh | wc -l
grep -c jc_history_add_tool_result src/chat/jc_agent.c
grep -n 'Platform: POSIX' src/chat/jc_sysmsg.c
grep -n 'fired [0-9] times in 36' docs/CHATBOT.md docs/ROADMAP.md docs/DEFERRED.md
grep -c 'docs/README.md' README.md
git log --diff-filter=A --format='%ad %s' --date=short 3bbe507c -- docs/analysis | head -n 1
git log --diff-filter=A --format='%ad %s' --date=short 3bbe507c -- docs/plans | head -n 1
```

They print, in order:
- `90 265`;
- `61`;
- `13`;
- the line of `append_env` that writes `Platform: POSIX`;
- the three disagreeing counts;
- `0`;
- the 2026-09-29 scaffolding review;
- the 2026-09-29 DOS plan.

The credential probes in §4 ran against the mock model, from a scratch directory outside the
repository. Their script is in the internal note.
