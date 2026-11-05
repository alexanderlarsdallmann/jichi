# jichi-harness: driving jichi or any agent on its own

This page is for a learner setting up a drive, and for an operator reading what the harness
protects before choosing a mode.

## The limit to read first

In **Mode A** (the normal user account) the harness and the agent run as the same user. The harness
therefore keeps the git token from the model's commands — not from a determined attacker with that
account. The 2026-10-03 plan says it plainly: "The only boundary that holds is a process the model
cannot drive." For untrusted input, the modes that do hold are **a separate user (Mode B)** or
**a container (Mode D)**: "The harness can support that; it cannot enforce it."

## What it is

`jichi-harness` is jichi's third C89 program, built and installed beside `jichi` and
`jichi-convert`. Its job is to drive an agent — jichi or any agent — unattended, with the operator deciding:
lay out a drive, run the agent's turns, mirror the agent's clone, push under a fixed prefix, and
stop when the drive's end is reached or the operator stops it. It is C89 for D4's reason: it reuses
what jichi already has and tests — the spawn primitive, the environment scrub, the redactor, the
memory protection — and passes the same gate and lints; a shell script would rebuild those by hand,
and quoting a credential in `sh` is where such scripts go wrong.

## The threat model

Carried from section 6 of the plan: what is protected, from whom, and why it matters.

| What to protect | From whom | Why it matters |
|---|---|---|
| The git token, the model API key | the model's commands | a stolen token pushes anywhere the token can; a stolen key spends money or quota |
| The operator's own files (`~/.ssh`, other projects, the browser profile) | the model's commands | everything the agent's user can read, a hostile command can read and send away |
| The repository's history and other branches | a run gone wrong | a force-push or a rewrite cannot be undone for others |
| The host's integrity (planted hooks, `core.fsmonitor`) | a planted file that runs later as the operator | a `.git/hooks/*` or `core.fsmonitor` runs when anyone runs git there |
| Other tenants | each other | on a shared server, one user's run must not read another's |

The standing caveat: jichi's own protections stop at one boundary, and "A command runs as you"
([`HARDENING.md`](HARDENING.md)).

## The commands, as they land

| Command | Phase | Role |
|---|---|---|
| `init` | P2 | lay out a drive's or a rehearsal's directory (built, M1053 H2) |
| `doctor` | P2 | check the environment and its prerequisites |
| `mirror` | P2 | `fetch` + `head` (batch H3b) read the team's clone into a harness-written mirror; `push` (batch H3c) writes it to the remote -- the sections below |
| `run` | P2 | one lead run: the key from its file into the spawn's env, the prompt WITH the previous run's handoff, the records outside the workspace (built, M1057+M1058 H4) |
| `drive` | P2 | the outer loop until the drive's end: fetch, run, push, the records snapshot; pause after three quick failures (built, M1059 H5) |
| watchers | P3 | `status`, `watch`, `gate`, `review`, `verify`, `probe` — not yet phased in a batch |
| landing | P5 | `pending`, `approve`, `land`, `integrate` — not yet phased in a batch |

At this commit `init` (M1053, H2), `doctor` (M1052, H1b), `mirror fetch` + `head` (batch H3b),
`mirror push` + the `credential-helper` subcommand (batch H3c), `run` (M1057+M1058, H4: the lead
run and the handoff of the previous run's records) and `drive` (M1059, H5: the outer loop) are
built — every P2 command; the port lands them batch by batch, H1 through H5, and this table is
what a learner checks when one lands. `doctor` takes the global `--config <file>` option; it
reports on the harness configuration in the same shape as `jichi doctor` (the
`%d ok, %d warning(s), %d problem(s)` summary line, exit 1 on a problem, and a `--output json`
form). `mirror fetch` takes the global `--config <file>` plus a `<mirror-dir>` and the
`fetch`/`head` verbs (see its section below). `run` takes the global `--config <file>` plus a
`--tenant <name>` and a `--run <N>` (see its section below). `drive` takes the global
`--config <file>` plus a `--tenant <name>` and the `--once` / `--tick <s>` options (see its
section below). A bare `mirror` (no verb) still refuses with `not built yet` (exit 2) — it is the
one verb the port has not claimed. The later phases are named only so the page does not promise a
shape no batch has claimed.

## `init`: laying out the drive or a rehearsal

`jichi-harness init --config <file> <drive|rehearsal> DIR [--from <repo>] [--jichi-bin <path>]` is
the C89 port of the drive's `setup-dir.sh` (M1053, H2): the first harness command that runs git,
and the only one that lays out a directory. What it lays out, in the order the script did:

- the plain directories (`runs/`, `inbox/`, `backups/`, `bin/`, `verify/`, `home/.cache/`,
  `home/.jichi.d/tool-output/`);
- the pinned `bin/jichi` (a copy of `--jichi-bin`, or of the `jichi` next to the running harness,
  mode 755, reported by its `build:` stamp);
- the secrets: in a **drive**, `key.env`, which references the model key file
  (`JICHI_HARNESS_MODEL_KEY_FILE`) and reads it when sourced — never a copy of the key — mode 600;
  in a **rehearsal**, random canaries (`canary.env`, `canary.token`);
- in a rehearsal, `standin.git`, a local bare stand-in for the remote, carrying the source's plan
  branch — a sink the source is pushed into by path, with no origin of its own, and no real remote.
  **Until the push commands land (H3), do not push from an init-made mirror with plain git; nothing
  in jichi-harness pushes yet** (the mirror has no push guard: the key-scan and size-check land in
  H3, def-337);
- `team/`, a clone of the source's plan branch switched to `<prefix>/main`, its origin pointed at
  `/nonexistent-the-harness-pushes` (the team never fetches or pushes; the harness pushes from the
  mirror), its `*.sample` hooks removed;
- `mirror.git`, a bare repo whose `origin` is the remote (the drive's, or the stand-in in a
  rehearsal), its fetch refspec `+refs/heads/*:refs/remotes/origin/*`, and an initial fetch —
  reported, not fatal — that runs with a cleared credential helper;
- `recs/`, an orphan branch `<prefix>/records` with a committed `README.md` (the identity from the
  config's `drive.gitName` / `drive.gitEmail`) and `origin` set.

The **idempotence promise** (the core of H2): `init` never deletes. A part that already exists is
reported ("left as it is") and left alone, so a second run completes what a first run left undone
and changes nothing else — the canaries are redrawn only if absent. A usage refusal (an unknown
mode, a missing `DIR` or `--from`, a missing pinned binary, a drive with no model-key reference)
exits 2 and says what is missing **before** any part is written. Every git child runs M2-neutral
(the `-c` flags and the secret-env scrub, as in `src/tools/jc_tool_git.c`).

The **canary rule**: a rehearsal's key and token are random 24-hex draws, `CANARY-` and
`tokcanary-` prefixed, independent per directory — a rehearsal's canary is useless outside its own
directory, and it is never the real key or token.

What `init` **deliberately does NOT do** (the port's CHANGED lines): no `DIR/harness/` archive —
the C port IS the harness, there is no script to install (CHANGED 4); no rehearsal
container-prefix / unit-name rewrite — Mode A has no containers, the rewrite lands with Mode D
(def-336); and no pre-push hooks or credential helper — `init` lays out the repos **without arming
them**; the key-scan, size-check and mirror wrapper land in H3 (def-337). A bare `init` with no
mode still prints the `not built yet` refusal (the driver
`tests/smoke/harness_third_program` asserts that form).

`doctor` takes no live probe at this commit: it never opens a secret file and never makes a model
request, so a broken or missing `--config` is a reported problem (exit 1), not a usage rejection
(exit 2). `--output json` is a doctor option; `--config` is global (before the command), the same
place `jichi` takes its `--config`.

## `mirror fetch`: reading the team's clone into a mirror only the harness writes

`jichi-harness mirror fetch --config <file> <mirror-dir>` is the C89 port of the drive's
`mirror.sh` `mirror_fetch` (batch H3b) — the first harness command that reads the team's
repositories. `jichi-harness mirror head --config <file> <mirror-dir>` is the port of
`mirror_head` (the team's last committed head, as the mirror holds it).

The **mirror** is a bare repository the harness wrote (via `init`); the **source** is the team's
clone. Before any git child touches the mirror, `mirror fetch` runs five git-free integrity
checks and **refuses (exit 2, naming the failed check)** on the first that fails:

1. **bare** — the mirror's `HEAD` is a symref (a `ref: ...` line); a non-bare dir (the team
   clone) or a detached HEAD is refused;
2. **ownership** — the mirror directory is owned by the harness's uid (a `git init --bare`
   mirror is umask 022, mode 755; the concrete, init-consistent half of the note's check 2);
3. **config bytes** — the mirror's `config` is byte-identical to the copy `init` stored at
   `<mirror's parent>/mirror.git-config.harness-copy` when it created the mirror; a missing
   stored copy is refused (check 3 cannot run), as is a diff;
4. **no alternates** — there is no `objects/info/alternates` under the mirror (the file that
   makes git run `core.alternateRefsCommand` over an alternate object store);
5. **hooks** — `hooks/` holds nothing but git's own `*.sample` templates (init lays the mirror
   out unarmed; any regular file there is a plant, def-337).

A same-uid racer (a plant between the check and the git call) is **not** caught — the precheck is
a check-then-use guard, and the complete fence is Mode B / P4 (def-338); walking the targets an
`alternates` file names is likewise deferred (def-339).

Once the checks pass, the fetch runs, all through a git child carrying the M2 neutralisation
(`-c core.fsmonitor=` `-c log.showSignature=false` `-c commit.gpgsign=false`
`-c core.hooksPath=/dev/null`), `GIT_CONFIG_GLOBAL`/`GIT_CONFIG_SYSTEM` pointed at two empty
harness-owned files, and the second layer `-c core.alternateRefsCommand=`:

- the **team's branches** — `fetch -q --no-tags <team clone>`, refspecs
  `refs/heads/$BR/main:refs/heads/$BR/main` and `refs/heads/$BR/work/*:refs/heads/$BR/work/*`
  (where `$BR` is the config's `drive.branchPrefix`). No `+`, no `--prune`, no tags. A
  **non-fast-forward** rejection (the expected case the no-`+` discipline is for) is reported as a
  line and **tolerated** — the other refspecs in the same fetch still applied, so the command
  exits 0 and the mirror keeps the rejected ref's old tip. A **transport** failure (a bad or
  missing source) exits 1 with the captured git output (first ~200 bytes).
- the **kept attempts** — for each checkpoint store under
  `<dir>/home/.jichi.d/checkpoints/<k>/` whose `config`'s `worktree =` line (read as a file) is
  **byte-exactly** the team's workdir, `fetch -q --no-tags <store>` with refspec
  `refs/jichi/discarded/*:refs/kept/<k>/*`. A store whose worktree is not the team's is **skipped,
  not errored** (the 2026-10-18 incident: a lead's scratch-probe store with a worktree that
  exists only in its own container); a failing kept fetch is a report line, not fatal.

`mirror head` prints the mirror's `refs/heads/$BR/main` short hash on stdout; a missing ref
prints nothing (the script's `2>/dev/null` shape). The **push** (batch H3c, its section below) takes the same five prechecks, adds the URL guard and the
C key scan + size check, and pushes through the credential-helper (def-337's hook installation is what H3c
replaces with C logic: no hooks on the push). Driver: `tests/smoke/harness_mirror_fetch` (red-first).

## `mirror push`: writing the mirror to the drive's remote (and the credential-helper)

`mirror push` takes the global `--config <file>` plus a `<mirror-dir>` and is the WRITE half of the
mirror: it reads the mirror and pushes the drive's refs to the remote `init` laid out (a rehearsal's
local bare stand-in, a drive's GitLab URL). It is, in order, a sequence of REFUSALS — each step
refuses (exit 2, a precheck or the URL guard) or refuses (exit 1, a secret or a size) and the push
never happens:

1. **the five integrity prechecks** — the fetch's checks, run git-free, BEFORE any git call in the
   mirror (a same-uid plant is a check-then-use window; the complete fence is Mode B, def-338 /
   def-346);
2. **the URL guard** — the effective push URL (`git remote get-url --push origin`, the mirror's own
   config) must equal the mirror's stored-config-copy `remote.origin url` byte-exact. A plant that
   re-points `origin` (a `set-url`, a `pushurl`, an `insteadOf`) is refused; a GLOBAL-config plant is
   *neutralised* (the child's `GIT_CONFIG_GLOBAL` is an empty file the harness wrote) — it is
   invisible to both the guard and the push, so the push lands at the stored URL;
3. **the key scan** — the model key and the git token are read from the files the configuration
   names, at the moment of use (a secret that cannot be loaded is a refusal, fail-closed), and every
   blob the push ADDS is scanned for the two secrets verbatim. The range is the pushed tips minus
   the remote's CURRENT tips — read by `git ls-remote <stored URL>` — because a push by URL does not
   advance the mirror's `refs/remotes/origin/*` (the tracking refs are stale by construction); a
   `.gz`'s decompressed content is not scanned (def-341);
4. **the size check** — no blob version over 8 MiB, no push over 20 MiB (the reference's constants;
   the config knobs are def-342); a scan that resolves zero pushed tips is a refusal, not a pass;
5. **the push** — `git -C <mirror> push -q <stored URL>` the refset `<br>/main` + `<br>/work/*`,
   through the M2 neutralisation + `core.hooksPath=/dev/null` (the guards are C logic, no hooks) +
   the credential-helper. `--force` is never on the argv, and `refs/kept/*` is never pushed (the
   reference's `PUSH_KEPT=0` default). A non-fast-forward refusal is a FAILURE (exit 1, git's
   `(fetch first)` captured), unlike the fetch's report line.

**The credential-helper** is the push's only secret channel, and it is a subcommand git spawns, not
a shell script the mirror holds. The push child carries, in its env: the TOKEN FILE's path (a name,
never the token) and the CONFIGURED REMOTE URL. On `get`, the helper parses the credential identity
git sends on stdin (`protocol=`, `host=`, `path=`, `-c credential.useHttpPath=true`) and answers
with the token read from the file only when the identity is the configured remote — the same-uid
process that can re-point `origin` between the URL guard's check and the push (def-346) cannot make
the helper answer for a different project: a `get` with another host or path is a refusal (exit 1,
no `password=` line). `store` and `erase` read and discard stdin and print nothing. The token
never appears on a command line, in a log, or in the mirror.

Driver: `tests/smoke/harness_mirror_push` (red-first, 31 cells).

## `run`: one lead run in Mode A

`jichi-harness run --config <file> --tenant <name> --run <N>` (M1057, H4) starts ONE lead run and
carries its records. It is the C89 port of the drive's `run-lead.sh` with the **container half
dropped** (Mode A has no podman — D4) and the **record and key-delivery half kept**. In order:

1. **the pre-run refusals, all BEFORE any layout or spawn** (exit 2, nothing is created): the
   loader's verdict on the config; a `--tenant` that names no tenant (named); a selected tenant with
   no `workdir`; a drive with no `dir`; a model-key reference not set (the REFERENCE is named,
   never a value);
2. **the key** — read from the file the configuration names, at the moment the run starts, and
   handed to jichi through the spawn's ENV vector under the fixed `JICHI_API_KEY` — never on argv,
   in a URL, a log or a record. The parent unsets `JICHI_API_KEY`/`JLU_API_KEY` from its own
   environment before the read, so the file is the only source an ambient key cannot masquerade as
   (the driver's R2 cell proves a distinct ambient key never reaches the model). jichi's own scrub
   (M1043-a) drops the name again from every child it forks;
3. **the lead-model guard** — a `drive.leadModel` the tenant's config does not name WARNS on the
   harness's stderr and OMITS `--model` (the run proceeds on the config's routing). This is a COST
   FENCE, not a refusal: a raw id reaching the gateway is the priced-model class (ANECDOTES #68);
4. **the spawn** — the pinned jichi, `-C` the tenant's workdir, with the full flag set: **caps off**
   (no `--deadline`, no `--budget-tokens`) and **fences on** (`--max-tool-calls`, `--verify-timeout`);
   stdin at `/dev/null` so a blocked spawn cannot hang the run (the ANECDOTES #64 class). The
   harness exits the child's exit status.

**The records land OUTSIDE the workspace**: the journal, telemetry, stream, stderr, the prompt and
the run's meta all go to `<dir>/runs/N/`, never under the tenant's workdir. The `## This run`
prompt and the meta state that path, so the model reads it where it is (Mode A has no read-only
`/records` mount; the container's mounts are the port's CHANGED lines). **The supervisor's inbox
notes are moved into the run's records ONLY after the run's first model call succeeds** — a run
that dies before its first call keeps its notes in the `inbox/` for the next run (the incident the
reference's gateway-wait names: runs that consumed the inbox and died without delivering it).

### The handoff (M1058, H4): what the previous run left you

Before the spawn, the run builds the `## What the previous run left you` section of the prompt
from the PREVIOUS run's own records (the port of the drive's `handoff.py`) and keeps it with this
run's records as `runs/N/handoff.md` (an empty file when there is no section). The section is
mechanical: the previous run's `meta.txt` (how it ended — `rc`, the minutes, the `team_head_after`
commit), its `stream.jsonl` (the model's last message, its todo list as last written, the files it
read or changed, its `spawn_parallel` batches, the manager's notes it was given — at most four,
the rest counted), each line capped and the whole section cut at 6000 bytes with a marker. It
FAILS OPEN: a previous run whose records are missing or unreadable starts the next run without the
section (the run's exit class is unchanged), and the reason is filed to `runs/N/handoff.err`.
Port differences from the reference, each named: the run writes the `team_head_after` meta line
itself (the reference's loop wrote it), the notes' "full text is in" path is the Mode A records
dir (there is no read-only `/records` mount), `apply_patch` edits are counted per `edits[].path`
(the reference reads only a top-level `path`/`file` and so never counts one), the batch lines pair
each `spawn_parallel` call with its `tool_result` BY ID (ties by stream order), and a `.gz` note is
`(unreadable)` by name (def-358 — no gunzip; the reference gunzips, but no drive in this
repository has ever written a compressed note).

**Not built yet**: the post-run record collection (def-355), a per-tenant
`apiKeyEnv` name (def-356) and the gateway-up probe (def-357; H5's loop is built but the probe
itself stays in the `run` — a broken gateway is not parked twice, once in the run and once in the
loop).

Driver: `tests/smoke/harness_run` (red-first, 17 cells).

## `drive`: the outer loop, until the drive's end

`jichi-harness drive --config <file> --tenant <name> [--once] [--tick <s>]` (M1059, H5) is the C89
port of the drive's `loop.sh`: it runs the team's lead in a loop **until the operator stops it or
the config's `drive.end` is reached** — a bound on the *drive*, not a cap on the *runs* (the plan
§3: "the loop sets no deadline and no budget" on a run; a run keeps the reference's caps-off,
fences-on). It is the last P2 command; with it the harness can drive itself (or any agent) with no
supervising session — the program D4 said it would build. In order, per the reference:

1. **the pre-loop refusals, all BEFORE any run or layout** (exit 2, nothing runs): no `--tenant`
   (named); a `--tenant` that names no tenant, or a tenant with no `workdir` (the `run`'s own
   refusal, reused — so a bad tenant is refused the way a run would refuse it); `--tick 0` (a zero
   tick is a refusal, not an "always"); and a `drive.end` that does not parse (exit 1, the line
   `the end time does not parse` logged BEFORE any `loop start` — a deadline that parses to the
   wrong instant is the class of cap that manufactures a plausible different answer, so it is
   refused, not guessed).
2. **the run counter** — read from `<dir>/runs/` ONCE at start: a name counts only when every byte
   is a digit, parsed in base 10 (a drive's counter is `0009`, and `0009` is not octal `9`), and
   the drive starts the next after the max (a plant of `runs/0008` yields run `0009`, not `0001`).
3. **the outer loop** (the reference's order, kept): at the top of every iteration the STOP file
   (present → `STOP file present: loop ends`, exit 0) and the deadline (past →
   `deadline reached: loop ends`, exit 0) are checked BEFORE a run starts, so the loop never starts
   a run it will not finish; then `mirror fetch`; then `run N: start`; then the run is spawned as a
   **child process** (the harness's own binary, `run --config … --tenant … --run N`, in its own
   process group with stdin at `/dev/null` — never a re-exec that would lose the loop's state); then
   the loop POLLS the run every 10 s (the reference's granularity): at the `--tick` second it does
   the periodic push + a `partial` snapshot (so a long run's records reach the records clone before
   it ends), and if the deadline has been reached mid-run it sends the run's group a **SIGINT — the
   way Ctrl-C stops it, so the run's journal is written — and never starts the next run** (the run
   forwards the signal to its jichi child and SURVIVES, writing its own `rc=130`/`end=`/
   `team_head_after=` before it exits; see the handoff's `Not built yet` above for the run's side);
   then `run N: end rc=<rc> after <t> s`, the end-of-run push, a `run N: team head <sha>` line (the
   MIRROR's main, read after the push — the run's own `team_head_after` meta line reads the
   WORKDIR's HEAD, and the two may differ when the lead left a non-main branch checked out; the
   note names it so a mismatch is read as two different truths, not a bug), and a `final` snapshot;
   then **the quick-failure count**: a run that exits non-zero in under 120 s is "quick", and after
   THREE quick failures in a row the loop logs `three quick failures in a row: waiting 10 minutes`
   and pauses (the reference's `sleep 600`) — but the port sleeps it in 10-s slices each checked
   against the deadline and the STOP file, so a deadline that arrives mid-pause ends the drive
   (the reference's solid `sleep 600` cannot see a deadline arrive during it; a pause outlives the
   drive there). `--once` ends the loop after the current run; `--tick <s>` overrides the config's
   `drive.tick` (default 1200 s).
4. **the records snapshot** (every periodic and every final) — the run's `runs/N/` is copied into
   the records clone's `recs/runs/N/`: a regular file over 1 000 000 bytes is gzipped and its plain
   twin removed (and vice-versa, so a file that shrank under the threshold between a `partial` and
   the `final` leaves the current form, not both); the four subdirs `notes`/`lmstudio`/`sessions`/
   `attempts` are ALWAYS gzipped, no threshold. Then the records clone is committed (`records: run N
   (KIND)`, the KIND is `partial` or `final`) with the identity the config carries, and — in **Mode
   A** — the push of the records branch is the **named no-op** (def-360): the commit is the record,
   the push is not built, and the loop logs `the records push is not built`. The team's own push
   (`save_team`'s `mirror push`) IS built, and is **switchable**: a `drive.teamPush` of `false`
   makes `save_team` fetch but NOT push the team's refs (the operator's "loop, fetch, snapshot, no
   push" switch; the reference's `TEAM_PUSH`, default on).

**What the snapshot never copies**: a SYMLINK, a FIFO or a device under `runs/N` is SKIPPED by name
(logged), and every git child the loop starts inside the records clone runs with git's config
neutralised (no fsmonitor, no hooks, no gpgsign, no signature check, and an empty `GIT_CONFIG_GLOBAL`
/`GIT_CONFIG_SYSTEM`) — in Mode A the agent is the SAME user as the harness and can plant a
`runs/N/link` at a key file or a `recs/.git/hooks/pre-commit`, and neither reaches the remote or
runs as the harness. That is the one place the port is TIGHTER than the reference (the reference's
`[ -f ]`/`cp -p`/`gzip -c` all follow symlinks), and the note (behaviour 11, CHANGED 7/10) names it
with the reason.

**The secret rule holds through the loop**: the model key and the git token never reach the loop's
argv, `harness.log`, a snapshot, or a child's environment beyond what `run` and `mirror push`
already do (the run reads the key at its own spawn; the push's credential-helper reads the token
when git asks). A `partial`/`final` snapshot that captured a key would push it (def-360 aside, the
records are the point) — the symlink-skip and the neutralised git children are the teeth.

Port differences from the reference, each named in `notes/M1059.md`: the container half is dropped
(the one `podman kill --signal INT` becomes a SIGINT to the run's process group); the run child's
stdout is CAPTURED into `harness.log` (the reference redirects no run output there, only its git
children); the deadline is read from the config's `drive.end` (a documented form, else refused)
rather than a shell `ZD_END`; the `team_head_after` meta line the loop once wrote is the run's own
now (M1058); and the records push is the def-360 no-op. The note is the authoritative list.

Driver: `tests/smoke/harness_drive` (red-first, 11 cells; the whole P2 port is now exercised end to
end against the real mockmodel, real git, and a local bare stand-in — no GitLab, no gateway, no
real key or token).

## The settings (env, `JICHI_HARNESS_`)

The harness's settings use the prefix `JICHI_HARNESS_`; each name says what its value holds, and a
secret's name says it is one (D13, plan §10.4). One setting per secret. A refusal names the setting
it lacks and what that setting holds — for example "the model key file
(`JICHI_HARNESS_MODEL_KEY_FILE`) is not set, so the key scan has nothing to look for". Each setting
may be set in the config (a shareable default) or overridden by its env variable (a per-machine
value); the env variable wins. This table is linted from the source so it cannot drift
(`tests/smoke/harness_doctor.sh`, check 9, reads the three names out of
`include/jc_harness_config.h` and checks each appears here).

| Setting | Holds | Default | Who reads it |
|---|---|---|---|
| `JICHI_HARNESS_DIR` | the drive's runtime directory | none (the config's `dir`) | `doctor`, `init` (H2) |
| `JICHI_HARNESS_MODEL_KEY_FILE` | the file holding the model API key (a reference, never the key) | none (the config's `modelKeyFile`) | `doctor`, `run` (H4) |
| `JICHI_HARNESS_GIT_TOKEN_FILE` | the file holding the git push token (a reference, never the token) | none (the config's `gitTokenFile`) | `doctor`, `mirror` (H3) |

## The modes

One line each; section 7 of the plan is the full text.

- **Mode A, the normal user account** (plan §7.1) — built first; its limit is the one read first above.
- **Mode B, a separate OS user** (plan §7.2) — the cheapest boundary that holds against the model's commands.
- **Mode C, a daemon or service** (plan §7.3) — unit files as the record; the multi-tenant job service is deferred.
- **Mode D, containers** (plan §7.4) — rootless podman first; the boundary for untrusted input.

## The secret rule

A model key and a git token are the only secrets the program handles. Neither ever reaches a command
line, a URL, a log, a record, the agent's environment or a file the harness writes; each is read
from the file the configuration names, at the moment it is used. `init` (H2) writes only the
*reference* — `key.env` names the key file and reads it when sourced — and a rehearsal's canaries
are random, never the real key or token. `mirror push` (H3c) holds the git token the same way
(the credential-helper reads it from the file at the moment git asks). `run` (H4) is the first
command to carry a value: it reads the model key from the file at run start and hands it to the
spawn through the environment, never on argv — the parent unsets the key's name from its own
environment first, so the file is the only source.

## Read further

- The plan: `docs/plans/2026-10-21-jichi-harness.md` (threat model §6, modes §7, commands §3, phases §15).
- D4, the decision the harness implements: `docs/plans/2026-10-03-learners-models-and-drives.md` §D4.
- def-327, the deferral row this page is pointed at: `docs/DEFERRED.md`.
- The threat model's home: [`HARDENING.md`](HARDENING.md).
- The drive's internal plan, in this tree: `docs/internal/drive/2026-10-23-harness/PLAN.md`.
