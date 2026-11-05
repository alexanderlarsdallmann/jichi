# jichi-harness: a harness jichi can drive itself with

*Plan: nothing built yet. Written 2026-10-21 by the hardening drive's supervisor (a Claude Code session)
for three requirements the operator set that day:*

> "And we need to have a harness that jichi (or any agent) can use on its own: currently you (claude
> code) are driving jichi, and we need jichi be able to drive itself with a suitable harness, or set of
> harnesses."

> "If a user has only access to one model, then the harness has to support that use-case, too, even if
> the performance may be bad. The first version must support several options: running in the normal
> user account, running as a seperate user, possibly running as a daemon, or service, and within
> containers. You may explore these, and the other questions, too, document design decisions,
> alternatives, and recommendations honestly, and comprehensively for self-learners. This is a good
> topic. And do we know why D4 hadn't been built?"

*It builds on the operator's decision **D4** of 2026-10-04 (`docs/plans/2026-10-03-learners-models-and-drives.md`,
§D4): `jichi-harness` is a third C89 program, built, tested and installed beside `jichi` and
`jichi-convert`, and configurable for "various git servers ... the systems of various users, and
tentants". It also builds on the outage proposal of the same day
(`docs/proposals/2026-10-model-outage-fallback.md`).*

*The evidence is the hardening drive of 2026-10-17 to 2026-10-21:*
- *its harness scripts, kept in the repository under `docs/internal/drive/2026-10-17-hardening/harness/`;*
- *its supervision log;*
- *the records of its independent reviewer;*
- *the lessons ledger (`docs/internal/lessons/LEDGER.md`);*
- *read-only measurements on the drive's host (Ubuntu 24.04.5, kernel 6.8).*

*Facts are marked where it matters:*
- **[V]** verified on the drive's host on 2026-10-21;
- **[D]** from jichi's documents;
- **[GK]** general knowledge, not measured here.

*It is written for a reader who has not run an unattended agent before: each section says what the
choice is, what each answer protects and costs, and what to do.*

## 1. What "drive itself" means

A drive today has four parties:

| Party | What it is | What it does |
|---|---|---|
| The team | jichi: a lead (`jlu/qwen3.8-27b`) and its workers (`jlu/qwen3-coder-next`), in a rootless container | plans a batch, writes the task texts, has workers write code and tests, reviews, merges, keeps the log |
| The harness | about 2,650 lines of shell, Python, configuration and prompts, and a set of timers, outside the repository at run time | starts runs, carries data between them, mirrors the team's branches, pushes, gates, reviews, watches |
| The supervisor | a Claude Code session, woken by a timer every few minutes | reads the state, verifies the team's claims, relays notes, gates batches, moves master on the operator's go, keeps the records, fixes the harness |
| The operator | a person | decides: the plan, the go for every master move, priced models, deletions |

"Drive itself" means the drive no longer needs the third row, and works for any agent, not only jichi.
The operator stays. What the supervisor did is split between **code** (the harness) and **an agent run
in a supervisor role**. Every decision the operator owns is still put to the operator.

## 2. Why D4 was never built

The operator asked. The answer, from the tree, the remote branches and the supervisors' notes (a
research pass, with its central claims re-checked by hand):

**What happened:**
1. **Not rejected, deferred or superseded, just never picked up.** On 2026-10-04 the operator decided
   D1-D10, D4 among them. The plan file was last edited that day (`f6402aed`). Its line 7 still says
   "Nothing proposed here has been built". `docs/DEFERRED.md` has no row for `jichi-harness`,
   `secretEnv` or the harness plan (item 4.5). No later plan, handoff or drive plan mentions D4.
2. **It sat at step 5 of 8.** It was gated on two things that never started: the harness's own plan,
   and an operator question the plan asked and never got answered: "is the harness a command that runs
   once per job ... or a long-running service that takes jobs from many tenants?" (plan §D4).
3. **The session that held the plan stopped the same day**, with steps 2-4 unfinished.
4. **From 2026-10-05 jichi's code belonged to another session**, working from a different plan (the
   gap self-audit). From 2026-10-08 the session that wrote the plan was assigned to zigodot. On
   2026-10-11 the operator restated: "you do not drive jichi to work on jichi, apart from documentation
   work on separate branches. Please, stick to zigodot".
5. **The work that was restored later skipped it.** When the other session restored the plan's
   stranded board and documentation work (M932, M941-M948, 2026-10-11/12), it took only what it could
   see on a branch. The headless line, 4.2, 4.3, 4.5 and D4, existed only as text in the plan.
6. **From 2026-10-17 the work was the hardening drive**, ordered by finding severity. D4 is in none of
   its batches.

**What follows (inference, not stated anywhere):** D4 stalled through a handoff gap, not a decision.
The queue lived in one session's notes. Ownership moved, and nothing in the tree said "this is decided
and still owed". The project already has the rule that would have caught it: a "later" verdict gets
its `DEFERRED.md` row in the same session.

**The lesson for this plan:** every decision here that is not built at once gets a `DEFERRED.md` row,
with the decision and its date, in the same commit as the decision. Section 16 lists the rows this
plan needs.

## 3. What the drive harness does today

Each row is a script that works today. Together they are the reference implementation to port.

| Component | What it does | Port to |
|---|---|---|
| `setup-dir.sh` | lays out a drive's directory (or a rehearsal's) | `jichi-harness init` |
| `drive.env`, `config.json`, `lead-prompt.md`, `review-*.{json,md}` | names, ends, models, limits, prompts: one place | the harness configuration (D4's servers, credentials, tenants, plus the drive) |
| `loop.sh` | the outer loop until the drive's end: fetch, run, push; a pause after three quick failures | `jichi-harness drive` |
| `run-lead.sh` | one lead run in a rootless container: mounts, network, the key over stdin, the prompt with the handoff and the inbox notes, a `--control` socket | `jichi-harness run` |
| `mirror.sh`, `git-credential-from-file` | read the team's repositories only through `git-upload-pack` in a sandbox, into a mirror only the host writes; push with a credential helper, never `--force`, never master | `jichi-harness mirror` (D4 items 1-4) |
| `pre-push-keyscan.sh`, `pre-push-sizecheck.sh`, `pre-push-mirror.sh` | refuse a push that carries a secret or bulk data | built into the harness's push |
| `handoff.py` | writes what the previous run left the next one, from its own records | `jichi-harness run` |
| `wip-snapshot.sh` | copies uncommitted work out of the lead's container every 30 minutes | `jichi-harness watch` |
| `checkin.py` (and its `--alerts` watch) | one read-only status pass: commits, tree, calls, workers, signals, alerts | `jichi-harness status`, `watch` |
| `ci-gate.sh`, `ci-follow.sh` | `make ci`, uncapped, on exactly one commit in a fresh clone; the follower gates every new head | `jichi-harness gate` |
| `review-run.sh`, `review-follow.sh` | an independent reviewer (an agent in its own container and clone) on every new range of commits | `jichi-harness review` |
| `verify-head.sh`, the supervisor's `teeth.sh` | re-run claimed red-first tests on the pushed head, the fix reverted and in place | `jichi-harness verify` |
| `task-run.sh`, `doctor-live.sh` | one run on one task outside the loop; a live doctor check in the drive's container | `jichi-harness task`, `doctor` |
| the gateway probe (2026-10-21) | one keyless request a minute; the outage's ground truth | `jichi-harness probe` |

**Five defects of this harness were found on 2026-10-21 alone.** They are what the port must not
repeat:
- **Progress marked on failure, twice.** The CI follower and the review follower marked a head "done"
  whatever their exit code. After two reviews died in the gateway outage, 37 commits counted as
  reviewed and none had been.
- **A merge-base assumption, twice.** The gate assumed master was an ancestor of the drive branch;
  when another session moved master, gates refused or went red at the mutation sweep.
- **Records drift.** Three scripts were fixed on the host and not in the repository's copy.
- **A drive that dies at logout.** The loop and the timers are `systemd --user` units. With lingering
  off **[V]**, they live only while the operator is logged in. The operator enabled lingering on
  2026-10-21.
- **Units without a record.** Transient units vanish when stopped, and the command that started them
  was not recorded.

## 4. What the supervisor does, and who should do it

From the supervisor's standing instructions and the work of 2026-10-17 to 2026-10-21:

| Duty | How often | Kind | Owner in the plan |
|---|---|---|---|
| Check that the loop runs; read new commits, the tree, alerts | every few minutes | mechanical | harness (`status`, `watch`) |
| Flag uncommitted work older than 30 minutes; snapshot it | continuous | mechanical | harness (`watch`) |
| Re-run each claimed red-first test with the fix reverted, and in place | per claim | mechanical, **once the claim is machine-readable** (§12.2) | harness (`verify`) |
| Notice a hollow test: a check that cannot fire, a probe that never reached its target, a placeholder never filled | per milestone | judgment | the supervisor role (§9), helped by mechanical denominators |
| Measure a disputed finding (C-01 on 2026-10-21: "not a defect" on a hollow probe, then reproduced and fixed) | rare | judgment and work | the supervisor role, in a sandbox of its own |
| Read the lead's task texts for structural mistakes (three tasks appending to one file) | per dispatch | partly mechanical | harness (an overlap check at dispatch) and the supervisor role |
| Notice a fence side door (a refused file edit made through the shell) | per call | mechanical | harness (`watch`), later a product fix |
| **Relay the reviewer's findings to the lead** | per review | mechanical | harness (`review` writes the note itself). On 2026-10-21 two finished reviews were never relayed. One found a C99 loop in a C89 tree that is now on master |
| Relay a note to the lead now (inject) or to the next run (inbox) | as needed | mechanical once written | harness (`notify`) |
| Gate `BATCH <n> READY` with `make ci` on the exact head | per batch | mechanical | harness (`gate`) |
| Check the work list against the diff | per batch | judgment | the supervisor role |
| Ask the operator for the go; move master after it | per batch | mechanical, with refusals | harness (`pending`, `land`) |
| Integrate a master that moved under the drive | rare | mechanical; conflicts are judgment | harness (`integrate`), §10.3 |
| Keep the records: supervision log, lessons ledger, dated corrections | continuous | judgment | the supervisor role; the harness keeps its own event log |
| Fix the harness | when it breaks | engineering | milestones on `jichi-harness`, never live edits |
| Notice and handle an outage | rare | mechanical | harness (`probe`, park; the outage proposal) |
| Report to the operator | per event | judgment, then delivery | the supervisor role writes; the harness delivers (§10.1) |

## 5. Decisions taken so far

| Date | Decision | Source |
|---|---|---|
| 2026-10-04 | D4: `jichi-harness` is a C89 program that ships with jichi, configurable for git servers, users and tenants | the 2026-10-03 plan, §D4 |
| 2026-10-21 | The harness drives an agent without a Claude Code supervisor, for jichi "or any agent" | the operator, first quote above |
| 2026-10-21 | **One model must be enough.** A user with a single model is supported, "even if the performance may be bad" | the operator, second quote |
| 2026-10-21 | **The first version supports four execution modes:** the normal user account, a separate user, a daemon or service, and containers | the operator, second quote |
| 2026-10-21 | Unattended runs **park** when the model's server is down; fallbacks and routes are optional and declared (the outage proposal, §11) | the outage proposal |
| 2026-10-21 | **Built separately**, not inside the hardening drive (§10.2) | the operator: "Let's build it separately." |
| 2026-10-22 | **In-between**: after batch 13's residual cards and a batch 14 of prerequisites, the hardening drive pauses and the harness drive starts; batches 7-12 become the harness's first workload (§10.2, supersedes the 2026-10-21 timing) | the operator: "Yes, in-between that way, go ahead." |
| 2026-10-23 | **The switch comes after item (e) and the (f)/(g)/(h) design notes**; (b), (f), (g), (h) are built when their phase needs them. **B1:** opt-in `background: true`, automatic 3-way merge at all-done, one note per tool round, no concurrent waves in v1 (§10.2) | the operator: "Yes, follow your recommendations for both, please." |
| 2026-10-23 | **M1006, the configurable dirty-tree dispatch guard, is built now** (before item (e); it was chosen on 2026-10-20 and lost from the register). More generally: a tool or guard that helps is designed when found and built when needed | the operator: "Build the guard, now. Anything that helps us, let's design, and implement when we need it." |
| 2026-10-23 | **Landing is standing:** a READY head moves master on a green `make ci` of exactly that head, as a fast-forward, without a per-batch go (§10.2) | the operator: "Please, keep driving like this, including pushing to master." |
| 2026-10-21 | **A clean merge of a moved master into the team's branch needs no go**; a conflict goes to the operator; master moves only on the operator's go (§10.3) | the operator: "Go with your recommendation." |
| 2026-10-21 | **The first version's notifier is the digest file plus a command hook** (§10.1) | the operator: "Yes, for the first version." |
| 2026-10-21 | **Platforms: Linux on the various hardware platforms first, then every operating system jichi can support.** Limitations and exclusions are documented honestly, each with its design decision and constraint (§7.7) | the operator: "Linux on the various hardware platforms, first. Then any operating system we can support, we must. Limitations, and exclusions will be documented honestly, with design decisions, and constraints." |

The last decision partly answers D4's open question too. The harness is **both** a per-job command and
a service: the operator asked for the service mode, and a per-job command is what every other mode is
made of (§7.4).

## 6. The threat model, first

A learner should decide what the harness protects before choosing a mode. An unattended agent runs a
model's commands, and the model reads untrusted text: web pages, issues, files in the repository.
Assume that sooner or later a command it runs is hostile.

| What to protect | From whom | Why it matters |
|---|---|---|
| The git token, the model API key | the model's commands | a stolen token pushes anywhere the token can; a stolen key spends money or quota |
| The operator's own files (`~/.ssh`, other projects, the browser profile) | the model's commands | everything the agent's user can read, a hostile command can read and send away |
| The repository's history and other branches | a run gone wrong | a force-push or a rewrite cannot be undone for others |
| The host's integrity (hooks, cron, shell profiles) | a planted file that runs later as the operator | a `.git/hooks/*` or `core.fsmonitor` in the agent's clone runs when *anyone* runs git there |
| Other tenants | each other | on a shared server, one user's run must not read another's |

jichi's own protections stop at one boundary **[D]**:
- It scrubs the configured key variable from its children (M130).
- It makes its process non-dumpable on Linux (M770) and on FreeBSD (M771). On OpenBSD, NetBSD and
  illumos it scrubs its environment (M796-M799).
- It fences its file tools (`pathFence`).
- It fences a parallel write child's shell to its worktree with Landlock (M831, writes only).

Reports are a further surface: models write them and they carry tool output, so a report can carry a
secret or a planted instruction. They are redacted before storage and read as data, never as
instructions (§10.5, decision 7).

None of this stops a command from reading what the user can read. `HARDENING.md` says it plainly: "A
command runs as you". The 2026-10-03 plan says: "The only boundary that holds is a process the model
cannot drive." Each mode below draws that boundary in a different place.

## 7. The four execution modes

### 7.1 Mode A: the normal user account

**What it is.** The harness and the agent run as the operator, in the operator's session or from the
operator's crontab. Nothing is set up beyond the harness's own directory.

**What it protects:**
- the token from the model's commands, as long as the token never enters the agent's environment.
  The harness hands it to git through a credential helper only for its own pushes (D4);
- the history, because the harness never pushes with `--force` and never pushes master;
- the operator from planted hooks, because the harness never runs git in the agent's clone. It reads
  the clone through the mirror pattern (`git-upload-pack` into a mirror only the harness writes).

**What it does not protect:**
- every file the operator can read: `~/.ssh`, key files, other repositories, the token file itself;
- the API key, from anything but jichi's own process memory. Any other process holding it is readable,
  as are files that contain it;
- the host, from outbound network traffic.

**The light sandbox that helps here.** Landlock on Linux restricts a process's own file access with no
privileges. This host's kernel offers it (LSM list `lockdown,capability,landlock,yama,apparmor`
**[V]**, ABI 4 **[D]**). jichi uses it only for parallel write children, and for writes, not reads
**[D]**. A harness in this mode should run the agent under a Landlock rule set:
- read-write: the workspace and a scratch directory;
- read-only: the toolchain;
- nothing else, so the operator's HOME is denied.

This turns the biggest gap of Mode A into a narrow one. On other systems the equivalents are
`pledge`/`unveil` (OpenBSD) and Capsicum (FreeBSD, a poor fit for a shell) **[D]**. macOS's
`sandbox-exec` is deprecated **[GK]**.

**Costs.** None to set up. The drive depends on the operator's session unless lingering is on (§7.3).

**When to use it.** A learner on their own laptop with a repository they trust, and the first run of
anything. Use it with the Landlock rule set where the kernel has it. Never on a server shared with
others, and never with a token that can write to repositories other than the one being worked on.

### 7.2 Mode B: a separate OS user

**What it is.** The agent runs as its own account, for example `jichi-agent`, with its own HOME and
workspace. The harness can run as the operator, or as a third account.

**How it is started** **[GK]**:
- On Linux: by root once at setup (`useradd -r`); then `sudo -u`, `runuser` (present here **[V]**),
  `systemd-run --uid=` or a system unit with `User=`.
- On the BSDs: `pw useradd` and `daemon(8)`, or `doas`.
- On illumos: SMF's `method_credential`.
- On macOS: a LaunchDaemon with `UserName`.

**What it protects:**
- the operator's files, if their permissions deny others (a HOME of mode 700 does; check it);
- the operator's processes: one user cannot read another's `/proc/<pid>/environ` or ptrace it.
  Yama's `ptrace_scope` is 1 here **[V]**;
- the token, which stays in the harness's account: the agent user cannot read it, by file permission,
  not by jichi's care.

**What it does not protect:**
- world-readable files: many systems leave some;
- the network;
- the kernel;
- anything the operator puts in a shared directory for the exchange.

**How the work comes back.** Not through a directory both users can write. That is exactly the
planted-hook risk. The mirror pattern works across users: the harness reads the agent's clone with
`git-upload-pack` run *as the agent user*, and writes the result into a mirror only the harness's user
owns.

**Costs:**
- root once, to create the account;
- a little permission work (§13);
- for a separate user that also runs rootless containers (Mode B + D): its own `/etc/subuid` and
  `/etc/subgid` ranges, which this host has only for `alex` **[V]**, and lingering for that user;
- a separate account cannot use the operator's desktop session, keyring or agent forwarding, which is
  the point, and occasionally a surprise.

**When to use it.** Any machine where the operator also keeps things that matter, and every shared
server. It is the cheapest boundary that holds against the model's commands.

### 7.3 Mode C: a daemon or service

**Two different things share the name:**
- **The harness as a long-running service**: the loop, the watchers, the gate follower, the reviewer,
  the probe. This is how today's drive runs: transient `systemd --user` units and timers.
- **The harness as a job service**: a process that accepts drives from several users or tenants. This
  is D4's open question.

**For the first sense, ship real unit files, not transient units.** A unit file is a record: it says
what runs, with which environment, and it survives a restart. Today's transient units vanished when
stopped, and the command that made them was not written down. They also lived only while the
operator was logged in, because lingering was off **[V]**. On Linux:
- a **user unit with lingering** (`loginctl enable-linger <user>`) for Modes A and B;
- or a **system unit** with `User=` and, where available, `DynamicUser=`, `ProtectSystem=strict`,
  `ProtectHome=`, `PrivateTmp=`, `NoNewPrivileges=`, `MemoryMax=` and `TasksMax=` **[GK]**. These add
  confinement without writing a sandbox.

The other platforms' service managers **[GK]**: launchd (macOS), rc.d (FreeBSD, OpenBSD, NetBSD), SMF
(illumos), `launch_daemon` (Haiku). FreeDOS has none, and no fork **[D]**.

**For the second sense (a multi-tenant service):** do not build it first. A service that accepts jobs
from tenants must:
- authenticate them;
- keep each tenant's credentials, mirrors and runs apart;
- schedule;
- survive restarts with jobs half done.

That is a server product. The per-job command, run by each tenant's own unit, cron or CI runner, gives
most of the value with none of that. `jichi daemon` (M322) already shows the house style for a
service: a Unix socket of mode 0600, same user only **[D]**.

**Recommendation.** Build the harness as per-job commands (`jichi-harness drive`, `run`, `gate`, and so
on). Ship unit-file templates for systemd and example definitions for launchd, rc.d and SMF that run
those commands. Defer the multi-tenant job service until several tenants actually exist. Write that
down as a `DEFERRED.md` row, so it does not stall the way D4 did.

### 7.4 Mode D: containers

**What it is, measured.** Today's drive runs the team in rootless podman 4.9.3 (crun 1.14.1, cgroups
v2, netavark) **[V]**, with these settings:
- `--userns=keep-id`, so files in the bind mounts belong to the operator;
- `--cap-drop=all --security-opt=no-new-privileges`;
- `--cpus`, `--memory` and `--pids-limit` from the drive's settings;
- `--network=pasta:-T,1234`: outbound to the model gateway, plus one forwarded local port for LM Studio;
- read-write mounts for the workspace, the agent's HOME and a control directory;
- read-only mounts for the pinned `jichi` binary, the configuration and earlier records;
- the API key over standard input to a small wrapper, so it is never in the container's environment
  or on a command line;
- the run's journal and telemetry as named pipes that only the host reads;
- the host reads the agent's repositories only through `git-upload-pack` in a separate network-less
  container.

**What it protects:**
- the operator's files: only what is mounted is visible;
- the operator's processes;
- the host's tools and configuration;
- with `--cap-drop=all`, jichi's own non-dumpable protection keeps working inside **[D]**.

**What it does not protect** **[D]**:
- everything mounted read-write, including the agent's HOME and its `.git`, where a run can plant
  hooks (with `keep-id` the files are the operator's, so git's `safe.directory` does not help);
- lines a run adds to records it can write;
- outbound network;
- the kernel;
- any mounted file that holds a key.

**Costs and surprises, measured here:**
- An image to build and keep current: 1.02 GB for today's drive image **[V]**.
- **Network behaviour differs.** During the gateway outage of 2026-10-21, the host saw the gateway
  refuse connections in 20-29 ms. Inside the container every attempt waited the full 60 s connect
  timeout. The run's metadata records `network=pasta`, the first suspect.
- **Ubuntu 24.04 restricts unprivileged user namespaces** to programs with an AppArmor profile
  (`apparmor_restrict_unprivileged_userns=1` **[V]**). podman and crun have profiles; bubblewrap, also
  installed here, has none **[V]**. So a harness that picks a container tool by "what is installed"
  can pick one the kernel refuses. Check by effect, not by presence.
- Rootless podman needs `/etc/subuid` and `/etc/subgid` ranges for the user (present for `alex`
  **[V]**).
- cgroups v2 delegates only the controllers systemd hands down: `cpu memory pids` here, no `io` **[V]**.
- SELinux hosts need `:z`/`:Z` on mounts, and cgroups v1 hosts cannot limit memory rootless **[D]**.
- On macOS and Windows a container runs in a Linux VM **[D]**.

**Alternatives** **[GK]**:
- docker (absent here **[V]**);
- bubblewrap (no namespaces here without a profile);
- systemd-nspawn (absent here **[V]**);
- jails (FreeBSD);
- zones (illumos);
- none on OpenBSD, NetBSD or Haiku.

The harness should talk to "a container backend" with one implementation per backend, starting with
rootless podman. A missing backend must be refused with a clear message, never silently replaced by
Mode A.

**When to use it.** Long unattended drives, untrusted repositories, and anything a stranger can write
into: issues, pull requests, web pages the agent fetches.

### 7.5 Combining modes

The modes stack, and the strongest practical setup combines them:

| Setup | Protects the operator's files | Protects the token | Needs root once | Survives logout | Notes |
|---|---|---|---|---|---|
| A | no | from the model's commands only | no | only with lingering | add Landlock where present |
| A + Landlock | mostly (what the rule set denies) | as A | no | as A | Linux 5.13+; the rule set must be right |
| B | yes, by permissions | yes, by permissions | yes | with lingering or a system unit | the cheapest real boundary |
| C (service) | depends on A, B or D underneath | depends | for system units | yes | a unit file is the record |
| D | yes, except what is mounted | yes, if not mounted | no (rootless) | with lingering | network and timing differ |
| B + D | yes, twice | yes | yes | yes | today's drive is A + C + D; this would be stronger |

### 7.6 Which to choose

1. **A learner, own laptop, own repository:** Mode A with Landlock, run by hand. Then add lingering and
   a user unit when the runs get long.
2. **A long unattended drive on a workstation that also holds other work:** Mode D (rootless
   containers), run as a user service with lingering (Mode C). Add Mode B when root is available.
3. **A shared server or several users:** Mode B per user, with Mode D inside, and system units. One
   configuration, mirror and HOME per user (D4's tenants).
4. **CI:** the per-job command in the runner's own container. The runner is the service.

### 7.7 Platforms

| Platform (jichi's verdict) | Service manager | Container or jail | Light sandbox | Separate user |
|---|---|---|---|---|
| Linux (Verified) | systemd (user units need lingering), OpenRC, runit | rootless podman, docker, bubblewrap, nspawn | Landlock, seccomp, `NO_NEW_PRIVS` | `useradd -r`; `runuser`, `systemd-run --uid` |
| FreeBSD (Verified) | rc.d, `daemon(8)` | jails | Capsicum (a poor fit for a shell), `procctl` | `pw useradd` |
| OpenBSD (Verified) | rc.d, `rcctl` | none (VMs) | `pledge`, `unveil` | `useradd`, `doas` |
| NetBSD (Verified) | rc.d | none native | little | `useradd` |
| illumos (Partly verified) | SMF | zones | privilege sets | `useradd`, SMF credentials |
| macOS (Partly verified) | launchd | podman or docker in a VM | `sandbox-exec` (deprecated) | `sysadminctl`, `dscl` |
| Haiku, GNU/Hurd, Cygwin/MSYS2 (Partly verified) | `launch_daemon`; sysvinit; Windows services | none; experimental; Windows-side only | none to little | not meaningful on Haiku; usual elsewhere |
| FreeDOS (Never compiled) | none | none | none | none: no fork, no sockets, no daemon |

The verdicts are jichi's **[D]**. The mechanisms are general knowledge **[GK]**, and each needs a
measurement before the harness claims to support it. The project's rule applies: a row is not tested
until a model has been driven on it.

**The order, decided by the operator on 2026-10-21:**
1. **Linux on the various hardware platforms first:**
   - the x86_64 workstation;
   - the boards jichi's platform matrix already records: Raspberry Pi 400, 500+ and Zero 2 W, Arduino
     UNO Q, VisionFive 2 and LicheeRV Nano;
   - the BPI-F3, measured on 2026-10-19 and still without its row.

   They differ in architecture, memory (from a few hundred megabytes to several gigabytes), kernel
   version, and therefore in Landlock and container support. A mode that needs 1 GB for an image, or a
   recent kernel, will not run on every board.
2. **Then every operating system jichi can support.**

Each limitation or exclusion is documented with its design decision and its constraint, for example:
- FreeDOS has no fork and no sockets, so it gets no harness at all;
- on Haiku, a separate user is not meaningful.

The unit of the record is a cell: (platform, mode). A cell is *supported* only when the mode was
checked by effect there and a model was driven through the harness there. It is *excluded* with its
reason, or *not yet measured*. Never claimed by analogy.

## 8. One model must be enough

**The decision.** A user with a single model is supported. Then the lead, the workers, the reviewer
and the supervisor role all run on that model. The operator expects the performance may be bad. The
harness must still work, and must say honestly what it lost.

**What a second model buys, measured.** The drive's independent reviewer already runs on the same
model as the lead (`jlu/qwen3.8-27b`), so this drive is a single-model measurement for the review
role. From its records, 2026-10-20 00:31 to 2026-10-21 17:25:

| Measure | Result |
|---|---|
| Runs | 24, the rehearsal included: 21 finished with a summary line, 1 finished without one, 2 died in the gateway outage |
| Length | 16 to 299 minutes, median about 84, 66 to 275 tool calls |
| Caught, confirmed by perturbation | vacuous checks again and again: at least seven cases, among them a planted one, a smoke driver, guards in two code paths, a depth bound, and unit teeth a commit claimed |
| Caught, real defects | a double free in setup; symlink escapes in two appenders; a parent-directory symlink escape; a C99 loop in a C89 tree that is now on master |
| False or overstated | 3 findings, one withdrawn by the reviewer itself |
| Missed, because of its inputs | the lead editing a worker-owned file through the shell: the reviewer reads commits and logs, never the run journals |
| Missed, because it died | the C-01 range and the 120-byte key leak: both fell into reviews that the outage killed |
| Lag | 1.5 to 5 hours per review, so findings often arrived after the head had moved on |

**What the measurement says (inference):**
- A same-model reviewer is useful for depth: it re-runs, perturbs and measures. Its findings were
  mostly right.
- It is slow.
- It shares the lead's outages.
- It cannot see what its inputs leave out.

This drive gives no measurement of whether a second model catches *different* mistakes. The Claude
Code supervisor caught C-01's hollow probe, but no same-model review covered that range, so the
comparison is not a fair one.

**How the harness compensates when there is one model:**
1. **Mechanical checks first.** Everything §4 marks mechanical runs as code and needs no model: the
   red-first re-runs, the gates, the denominators, the overlap and side-door checks.
2. **Fresh context, narrow questions.** A supervisor run starts with no memory of the lead's
   reasoning. It gets one question at a time ("does this check fail without the fix? run it"), not
   "review everything".
3. **The inputs the model needs.** The run journals, not only the commits: the side door was
   invisible for lack of them.
4. **Time it to the batch.** One review per ready batch, not per commit, so a slow model is not
   always behind.
5. **Say what was not checked.** The digest names every claim that only a model checked. With one
   model, the operator knows which of those to read.

## 9. The supervisor role as an agent run

The judgment rows of §4 go to an agent run that the harness starts on events (new commits,
`BATCH READY`, an alert) and on a timer. It is a different run from the team's, with different rights:

- **Reads everything, writes almost nothing.** It reads:
  - the harness's status (`--output json`);
  - the mirror and the diffs;
  - the run journals;
  - the records.

  It writes only notes (to the lead, to the operator) and records, through harness commands. It never
  edits the team's tree, never pushes, never moves master.
- **It may measure.** It gets a sandbox of its own: a clone of the mirror in its own container, network
  off. There it may change code to reproduce or perturb, then throw the change away.
- **Its model:**
  - With two or more models, use a different one from the lead's; that is the recommendation.
  - With one model, the same one, under §8's compensations.

  The outage proposal's park and fallback rules apply to it too.
- **Its claims are checked like the team's.** On 2026-10-21 alone the supervision log records mistakes
  by the Claude Code supervisor:
  - a "green" that covered only the lint drivers;
  - an instrument that counted the `Authorization:` header as a leak;
  - times typed by hand, several wrong;
  - a false "the note is still queued";
  - two finished reviews never relayed.

  The defences were mechanical (print what the instrument reached, take times from a clock, read the
  journal), plus a dated correction each time. The harness enforces the mechanical ones for the agent
  supervisor: it stamps the time, refuses a "verified" without the verify run's record, and relays
  every finished review itself.

## 10. The other questions, explored

### 10.1 How escalations and the digest reach the operator

| Channel | Needs | Good at | Weak at |
|---|---|---|---|
| A file in the drive's directory (the digest) | nothing | always works, is the record | only seen when looked at |
| E-mail | an SMTP server and its credentials | reaches a phone | one more secret to hold; mail can be delayed or filtered |
| An issue or merge-request comment on the git server | an API token with that scope | lives beside the code; the team sees it too | a token with more scope than a push needs |
| A desktop notification | a session on the same machine | immediate | useless when the operator is away; not on servers |
| A chat webhook | a webhook URL (itself a secret) | reaches a phone | one more service |

**Decided for the first version (the operator, 2026-10-21).** The digest file is always written; it is
the record. A notifier is a pluggable
command that the configuration names (`notify: [...]`), given the digest's path. The first version
ships two: the file (default) and a command template. Mail or chat is then a one-line script on the
operator's side, and the harness holds no extra secret it does not need. Which events page and which
only write to the digest is a setting. The outage proposal's paging threshold is still open, and its
data decides it.

### 10.2 Build it inside this drive, or as a drive of its own?

**Inside** (batches after 13) means the drive changes the harness that runs it. A bug in the new
harness stops the drive that would fix it.

**Separately** means the current scripts keep running the hardening drive, unchanged, and a new drive
builds `jichi-harness`, supervised by the current scripts. Then a rehearsal runs on the new harness.

**Decided (the operator, 2026-10-21): separately**, after the hardening drive's batches, or in parallel
on its own branch if the gateway can carry two teams. Only the jichi prerequisites (§12) belong in the hardening drive,
because they are product fixes.

**Decided again (the operator, 2026-10-22): in-between.** This supersedes the timing in the paragraph
above ("after the hardening drive's batches, or in parallel"). The operator's own words on 2026-10-21 were
only "Let's build it separately"; the timing was the supervisor's reading, and it was not what the
operator meant: "I thought that the c harness development would come in-between the drive." The order,
accepted on 2026-10-22 ("Yes, in-between that way, go ahead."):
1. The hardening drive finishes batch 13's residual cards (74, 73, 75). Then that head is gated, and
   master moves on the operator's go.
2. The hardening drive runs **batch 14, the harness prerequisites** (§12), as jichi product fixes in its
   own branch and gates. Batch 12's B1 (non-blocking `spawn_parallel`) moves into batch 14.
3. The hardening drive pauses at that point: the STOP file, with the reason in the supervision log.
4. **The harness drive** starts in a directory and branch of its own, run by today's scripts. It works
   through phases P2-P5 (§15).
5. **Batches 7-12** that remain become the harness's first real workload, and the P7 rehearsals: the new
   harness drives them, first with two models and then with one, against a drive whose behaviour is
   known.

Running both drives in parallel was not chosen: one gateway serves both teams. A single reviewer took 106
minutes on 2026-10-22 (`docs/analysis/2026-10-22-the-slow-reviewer.md`), and two teams would also muddy
every measurement.

**Decided (the operator, 2026-10-23, about 08:25): the switch point, and B1.** This refines step 2-3 of
the order above; it does not withdraw it. By the morning of 2026-10-23, items (a), (c) and (d) of batch 14
were on master (5e70f838, fc638450), and (b), (f), (g) and (h) waited for the operator's decisions. The
supervisor recommended, and the operator answered "Yes, follow your recommendations for both, please.":
- **The switch point.** The hardening drive finishes (e), liveness at every depth, and writes the design
  notes for (f), (g) and (h) without building them. That head is declared, gated and landed. Then the
  hardening drive pauses and the harness drive starts. Items (b), (f), (g) and (h) are built once
  decided, when the phase that needs them comes; the core (P2) needs none of them.
- **B1, non-blocking `spawn_parallel`** (the four questions of
  `docs/plans/2026-10-23-b1-nonblocking-spawn-parallel.md` on the drive branch):
  1. Opt-in, as `background: true`. The blocking call stays the default until the harness drive has
     measured background mode; then the default is revisited.
  2. The merge runs automatically when the whole wave is done, 3-way against the HEAD at that moment.
     There is no separate merge call.
  3. One note per tool round lists every task that finished since the last note.
  4. A second wave is refused while one runs, in the first version.

The landing rule became standing on the same morning: a READY head moves master when the ledger matches
the diff, `make ci` on exactly that head is green, and the move is a fast-forward ("Please, keep driving
like this, including pushing to master.").

### 10.3 May the harness integrate a moved master on its own?

On 2026-10-21 another session pushed two documentation commits to master while the drive ran. Bringing
them into the team's branch took:
- a merge commit;
- a 35-minute gate;
- a bundle;
- a resolved conflict in an index table;
- a note to the lead.

**Options:**
1. Always ask the operator.
2. Integrate on its own when the merge is clean, and ask on a conflict.
3. Always integrate, resolving conflicts by rule.

**Decided (the operator, 2026-10-21): option 2.** Integrating is not a master move: it changes the team's branch, and every
head of that branch is gated anyway. A clean merge of master is mechanical, and asking would only delay
the next batch. A conflict needs judgment and goes to the operator, or to the supervisor role in a
single-operator setup that allows it. Master itself moves only on the operator's go, always. The
operator's rule of 2026-10-21 still applies: while a drive runs, only the drive's side pushes master.
Integration is therefore rare.

### 10.4 What the settings are called

Added 2026-10-22, after the operator asked what `ZD_ENV_FILE` is, what "ZD" stands for, and whether the
name is appropriate.

**What the drive harness does today.** Its settings share the prefix `ZD_`: 47 names in 19 files, among
them `ZD_DIR`, `ZD_KEY_FILE`, `ZD_ENV_FILE` and `ZD_TOKEN_FILE`. The prefix is spelled out nowhere. It almost certainly
means "zigodot drive": the harness was written for the zigodot drive of 2026-10-08, whose containers were
`zd-lead-*`. The hardening drive of 2026-10-17 renamed its containers to `jd-lead-*` and kept the
prefix. So in this drive `ZD` is a leftover that misleads, since the drive is not about zigodot.

The names inside the prefix have a second problem: three of them concern secrets, and none says so.
- `ZD_KEY_FILE`, read only by `setup-dir.sh`, names the file that holds the model API key, outside the
  drive directory.
- `ZD_ENV_FILE` names the drive's `key.env`. That file holds no key: its one line,
  `JICHI_API_KEY=$(tr -d ... < <the key file>)`, reads the key file when it is sourced, so no copy of the
  key is written (setup-dir.sh, step 4).
- `ZD_TOKEN_FILE` names the GitLab push token's file.

A reader cannot tell the three apart without opening the scripts. In this session the supervisor itself
described `key.env` as holding the key, from the variable's name, and had to correct that after reading
`setup-dir.sh`. The push hook refuses every push when `ZD_ENV_FILE` is unset, because it loads the key to
scan for it. Twice the supervisor's session lacked the variable: a push was refused (2026-10-21), and a
doctor run stopped before its request (2026-10-22).

**Decided for jichi-harness (a recommendation, pending the operator's word):**
1. One prefix that names the product, `JICHI_HARNESS_`, so a setting says whose it is.
2. A name says what the value holds, and a secret says it is a secret: `JICHI_HARNESS_DIR`,
   `JICHI_HARNESS_MODEL_KEY_FILE` (the file with the key), `JICHI_HARNESS_GIT_TOKEN_FILE`. One setting
   per secret: the indirection through an env file that sources the key file stays an implementation
   detail, not a second name.
3. A refusal names the setting it lacks and what that setting holds, for example "the model key file
   (`JICHI_HARNESS_MODEL_KEY_FILE`) is not set, so the key scan has nothing to look for".
4. Every setting is listed in one table, in the harness's README, with its meaning, its default and who
   reads it. The table is generated or linted from the scripts, so it cannot drift.

**Not decided: renaming the running drive's settings.** The loop unit, its timers, the push hook and 19
files read the `ZD_` names. A rename while the drive runs risks a stopped loop, or a hook that refuses
every push. The recommendation is to leave them until the drive ends. A transition that accepts both
spellings is possible but adds a second name for every setting, so it is not recommended.

**Decided (the operator, 2026-10-23 20:25; D13 in the drive's `DECISIONS.md`).** The recommendation is
taken: the harness's settings use the prefix `JICHI_HARNESS_`, each name says what its value holds, and
a secret's name says it is one (`JICHI_HARNESS_DIR`, `JICHI_HARNESS_MODEL_KEY_FILE`,
`JICHI_HARNESS_GIT_TOKEN_FILE`). One setting per secret. A refusal names the setting and what it holds.
The setting table lives in one place and is generated or linted from the source, so it cannot drift.
This is **provisional, "for now"**: a later rename is one change to that table. From 2026-10-23 this
drive owns this file; the documentation branch will not change it again.

### 10.5 Where a worker's report goes

Added 2026-10-22, on the operator's request: the reports route "will have to be generic, and abstract
enough so that it can be used for other projects, and drives, too."

**What the drive does today.** A worker writes a report: the claim, the exact command, what it printed,
and what it did not do. Its red-first evidence lives there. The report is an ordinary file in the
worker's worktree, so `spawn_parallel` merges it like code. That caused two failures, measured on
2026-10-22:
- One shared name (`REPORT.md`) collided in 21 of 36 multi-task batches over runs 0001-0063: 38 reports
  set aside, the code merged, the evidence out of sight
  (`docs/analysis/2026-10-22-one-report-file-for-every-worker.md`).
- Per-task names at the root (`REPORT-<milestone>.md`) merged cleanly, `git add -A` committed one at the
  root, and `snapshot_lint` check 16 turned the gate on 152957a1 red.

The drive's lead prompt now routes reports to `docs/internal/drive/2026-10-17-hardening/reports/`
`<date>-<milestone>.md` (the operator's word, 2026-10-22). That is a convention: one project's path,
written into a prompt, and it depends on the lead filling in each path correctly.

**The principle.** A report is a task's *result*, not a change to the tree. jichi provides the
mechanism (collect, key, store); the harness configuration provides the policy (where reports are
published).

**Decisions (recommendations, pending the operator's word on each):**
1. **Collected outside the merge.** A worker writes its report where jichi tells it, for example a
   per-task path in an environment variable. `spawn_parallel` collects that file and never merges it as
   a tree change. This removes collisions, worker-owned report files (D6), `git add -A` sweeping a
   report into a commit, and files at the root, all at once.
2. **Keyed by identity, not by name.** The key is run, batch and task index. The milestone or card the
   lead names is a label only, so two tasks with the same label do not collide.
3. **A small machine-readable header, then prose.** Task id, label, model, build, base commit, final
   commit, outcome (done, stuck, budget), and the red-first claim: the command, the failing line before,
   the green count after. With it, verification becomes a harness step (§9, §12 item 2).
4. **Stored with the run's records, always, outside the workspace.** A report survives a kept,
   discarded or destroyed worktree, and reading a kept task's report needs no `jichi attempts show`.
5. **Publication is policy, as a path template.** The sink is `none`, a records branch (this drive's
   `recs/`), a directory in the tree (`{records_dir}/{date}-{label}.md`), or a command hook. No project
   path is compiled in. The default keeps reports out of the product tree. A directory in the tree is
   allowed only when the project marks it unpublished (`export-ignore`, or a published-tree check such
   as `snapshot_lint`), and the harness checks that at start-up.
6. **A tolerant reader.** A worker, or another agent, that still writes `REPORT.md` or `REPORT-*.md` at
   its worktree root has that file read as its report and kept out of the merge.
7. **Untrusted data.** Reports are written by models and carry tool output. They are redacted with
   jichi's secret redactor before storage, capped in size, and never read by a later run as
   instructions.
8. **The same route for any team size.** One model driving itself files its own task reports through
   the same mechanism (§8); only the number of reports changes.

**Alternatives weighed.** A relayed note each run (reaches only the runs it is sent to; failed at run
0063). Per-task names in a prompt (works today; depends on the lead; reports become worker-owned in the
tree). The worker's final answer as the report (`spawn_parallel` returns it already, but it is not
committed and a long run compacts it away). Dispatching one task at a time (no collision, no
parallelism). The recommendation: the convention now, decisions 1-4 and 6 as a jichi milestone, 5 and 7
in the harness.

**Order.**
1. Now: keep the convention, but the harness's lead-prompt template names the path as a variable
   (`{report_path}`) that the harness fills from the project's configuration, never a literal.
2. jichi: decisions 1-4 and 6 (`DEFERRED.md` def-332). Red first: two tasks write reports; both are
   stored, neither is merged, nothing is set aside, and a kept task's report is readable.
3. jichi-harness: decisions 5 and 7, the configuration, and the start-up check.
4. Later: the supervisor role reads the headers to verify red-first claims (§9).

## 11. The operator's interface

The operator keeps every decision. The harness provides:
- `jichi-harness pending`: what waits for the operator, each item with its evidence:
  - a master move after a green gate;
  - a priced model for one run;
  - a branch deletion;
  - a plan change;
  - an integration conflict.
- `jichi-harness approve <id>` and `deny <id>`, each recorded with the time and the operator's name.
- The digest (§10.1).

When nothing is approved, the drive goes on with the next batch on its branch, as it does today.

## 12. What jichi itself needs first

1. **`-C <dir>` and `secretEnv`** (the 2026-10-03 plan's items 4.3 and 4.2, decided as D2 and D3, never
   built). The harness runs jichi in a directory it chooses, and names which environment variables are
   secrets to scrub and redact. The key scrub covers only the configured key variable today: a
   credential under another name reaches every child **[D]**.
2. **Machine-readable claims.** A trailer on a fix commit naming its red-first test and the files the
   test needs reverted, for example `Red-First: tests/smoke/fetch_ssrf_reuse.sh checks 2,3; reverts
   src/net/jc_http.c` (form to be settled). With it, `jichi-harness verify` does what the supervisor
   did by hand for every claim this drive.
3. **The outage work.** Batch 13 (MO-1, MO-2), then park mode (the outage proposal).
4. **Liveness at every depth.** Heartbeats during in-process subagents and during model waits.
5. **Inject delivery state.** `jichi control` reports whether a queued note was applied.
6. **A Landlock rule set for the top-level shell** (def-149, O9 **[D]**): Mode A's best defence. Today
   only parallel write children are fenced.
7. **Reports as task results, outside the merge** (§10.5, decisions 1-4 and 6; `DEFERRED.md` def-332).
   Until it exists, the harness relies on a prompt convention for the report path.

## 13. How a learner sets up each mode (outline)

The harness's own guide will carry the exact commands, each run in its published form before it is
published (the project's rule). The outline:

- **A:** install jichi, run `jichi-harness init` in an empty directory, name the repository and the
  credential by reference, run `jichi-harness drive`. With Landlock present, check that `doctor`
  reports the rule set active.
- **B:**
  - as root, create the agent account with its own HOME (mode 700);
  - give the harness's user the right to start commands as it (one `sudoers` line or a system unit);
  - check by effect: from the agent account, try to read the operator's HOME and the token file, and
    expect "Permission denied".
- **C:** install the shipped unit file or definition, enable lingering for a user unit, and check by
  effect: log out, log in, and the drive's run counter has moved on.
- **D:**
  - build the image the harness names;
  - check that the container tool may create a user namespace on this system (run it once, by
    effect);
  - check by effect: inside, the operator's HOME is absent, and the key is not in `/proc/1/environ`.

## 14. What this drive taught, as mechanisms

| Lesson (ledger and supervision log, 2026-10-18 to 2026-10-21) | Mechanism in the harness |
|---|---|
| No cap on a run that measures; fences stay on | no `timeout` around runs or gates |
| Never run git in a repository the agent can write | the mirror pattern, in every mode |
| Secrets never enter the agent's environment, a log or a URL | credential helper, stdin hand-over, pre-push key scan, redaction |
| Commit verified work every 30 minutes | `watch` alert plus the snapshot |
| A gate that stops at the first red hides the others | the follower runs the smoke tier to the end |
| A "green" is the whole gate, not a hand-picked part of it | `gate` runs `make ci`; nothing narrower is called a gate |
| A translation unit built only without `-Werror` escapes the C89 rule (the fuzz target, 2026-10-21) | a lint that every first-party `.c` file is compiled by some `-Werror` stage |
| One writer to master; read handoff branches before a master move | `land` refuses while an unread `handoff/*` branch exists, and refuses anything but a fast-forward |
| Progress is marked only when the step succeeded | every follower writes its marker on success only |
| A finished review that is not relayed is lost | `review` writes its note to the team itself |
| A record file is part of every task's write set | an overlap check at dispatch |
| A refused edit made through the shell is a side door | `watch` alert; the product fix belongs to jichi |
| "Not a defect" needs a probe that could have seen the defect | `verify` asks every claim for its denominator |
| Times typed by hand are wrong | the harness stamps every record |
| Notes are consumed by runs that never reach a model | keep notes until a run's first model call succeeds |
| An outage looks like slow failures | `probe`, park, and a loop that counts failure classes |
| A harness edited live drifts from its record | the harness is code in the tree, changed by milestones |
| A service without lingering dies at logout | `doctor` checks lingering for user units, and the guide says so |
| A decided item without a tracked row is forgotten (D4) | every unbuilt decision gets its `DEFERRED.md` row in the same commit |

## 15. Phases

Each phase is a set of milestones with red-first tests. Real `git` runs against local bare
repositories; the institution's servers only with the operator's go (D4).

1. **P1, jichi's prerequisites** (§12), with batch 13 beside them. Since 2026-10-22 this is the hardening
   drive's **batch 14**, with batch 12's B1 moved into it (§10.2); batch 13 is on master (8bee4fcd).
   Since 2026-10-23 P1 ends after item (e) and the (f)/(g)/(h) design notes; (b), (f), (g) and (h) are
   built later, when their phase needs them (§10.2, "Decided ... 2026-10-23").
2. **P2, the core in Mode A:** `init`, `run`, `mirror`, `drive`, the configuration (D4's servers,
   credentials by reference, tenants) and the push guards, ported from today's scripts.
3. **P3, the watchers:** `status`, `watch`, `gate`, `review`, `verify`, `probe`. Each runs beside the
   current script during a drive, and both outputs are compared before the script is retired.
4. **P4, the other modes:** Mode D (the rootless podman backend), Mode B (starting runs as another
   user), Mode C (unit templates and service definitions). Each is checked by effect on this host, and
   then on one other platform.
5. **P5, landing:** `pending`, `approve`, `land`, `integrate`, re-pin, the digest and the notifier.
6. **P6, the supervisor role:** its prompt and rights, its sandbox, its model choice, and §8's
   single-model compensations.
7. **P7, two rehearsals without Claude Code** on a small, known task set (since 2026-10-22: the
   hardening drive's remaining batches 7-12, §10.2):
   - one with two models;
   - one with a single model.

   Compare them with a supervised drive on defects caught, false alerts, the operator's interventions,
   and time lost to the harness itself.

## 16. Risks, and the rows this plan needs

**Risks:**
- **Self-supervision**, the weakest point (§8). The rehearsals measure how much is left.
- **Security.** D4's limit stands: in Mode A the harness keeps the token from the model's commands, not
  from an attacker with the same account. Mode B or D is the boundary that holds, and the guide must
  say so on its first page.
- **Load on the model server.** A supervisor role adds calls, and in an outage it parks too.
- **Scope.** Four modes on several platforms is a lot for a first version. Each mode ships when it is
  checked by effect on at least one platform, and the guide's table says which.
- **Porting drift.** The scripts grew over four drives. Port behaviour first, measured side by side,
  then improve.
- **The report convention, until jichi collects reports itself (§10.5).** It depends on the lead filling
  in each path: a literal placeholder or a reused label collides again, and a report written elsewhere
  can still reach a commit.

**`DEFERRED.md` rows to add with this plan** (so that nothing here stalls the way D4 did):
- D4 itself and its open question, now answered (§5);
- the multi-tenant job service (§7.3);
- `secretEnv` and `-C` (§12);
- the top-level shell's Landlock rule set (§12; def-149 and O9 exist);
- the multi-model comparison for the supervisor role (§8: not yet measured);
- reports as task results, outside the merge (§10.5; def-332, asked of the drive's lead on 2026-10-22).

## 17. The operator's answers (2026-10-21)

The four questions this section asked were answered the same day. The answers are recorded in §5:
- separately;
- integrate a clean merge without asking;
- the digest file plus a command hook;
- Linux on the various hardware platforms first, then every operating system jichi can support, with
  limitations and exclusions documented.

Still open elsewhere: the outage proposal's paging threshold, which its outage data will decide.
