# Plan: the next drive in a container

*A plan, 2026-09-25. **Status: a prototype is built and measured; no drive has run this way
yet.** It is written for whoever runs jichi unattended over a list of tasks, person or agent, and
for a reader who wants to know why a model's shell needs a wall that jichi itself does not
provide. The prototype and its raw results are kept with the project's internal notes, in
`docs/internal/drives/contained/`, which public snapshots do not carry. For the ideas behind it,
explained from the start and runnable, read [CONTAINER_TUTORIAL.md](../CONTAINER_TUTORIAL.md).*

---

## 0. In short

A *drive* runs jichi unattended over a list of tasks, one headless `--auto` run per task, to
produce a corpus of journals and telemetry. At 00:42 on 2026-09-25, a model in such a drive sent
one shell command that deleted the drive's entire record ([ANECDOTES #108](../ANECDOTES.md)).
jichi's fences guard its file tools, not the shell. The drive ran the shell as the operator,
inside the operator's home, next to the record.

The next drive runs each task in its own rootless podman container. A prototype, driven by a
scripted mock model on the host, measured what that buys:

- The model's shell cannot reach anything outside its mounts.
- `rm -rf /*` destroys one run's workspace and HOME, and nothing else.
- The record leaves through pipes whose files only the host opens. The shell can add lines to
  it, but cannot remove or truncate any.

It also found something no container fixes: **a weakness in how jichi kept API keys from the
programs it starts.** It was jichi's to fix, and it was fixed as M770 on the same day. Following
[SECURITY.md](../../SECURITY.md), the details stayed out of the public tree until then. §6 now
gives them.

## 1. Why the shell needs a wall

jichi changes files in exactly two ways ([AUTONOMY.md](../AUTONOMY.md)):

1. **The write chokepoint.** Every file tool goes through it, and `--edit-scope` and the path
   fence are enforced there.
2. **A shell command.** `sed -i`, a redirect, a build step: "the fence does *not* cover this".

A shell can do anything the user running jichi can do. The oldest safety row in
[DEFERRED.md](../DEFERRED.md) says so, and names the answer: deployment, "in a container/VM",
because a program cannot reliably contain the programs it starts. The fence is not useless.
It is what keeps an honest model's file edits in scope. It is simply not a wall.

The overnight drive's analysis ([§11](../analysis/2026-09-25-the-overnight-drive.md)) sets three
rules for the next drive:

1. The model's shell must not reach the drive's record.
2. The record leaves the machine as each run ends.
3. Every cap a measurement can hit is named in the plan.

This plan covers rule 1, and the part of rule 2 that happens on the machine. Rule 3 belongs to the
drive's own plan, and §3.5 adds two caps to its list.

## 2. Containers, for a reader who has not used them

A **Linux container** is an ordinary process that the kernel shows a different view of the system.
It gets its own filesystem tree, process list, network and user ids, each a separate *namespace*.
Nothing is emulated. The process runs on the host's kernel at full speed.

**podman** starts such processes from an *image*: a filesystem tree built from a recipe, the
`Containerfile`. **Rootless** podman needs no root. It builds the container's user ids from the
operator's own uid and a range of subordinate ids the operator owns (`/etc/subuid`). By default,
the container's root *is* the operator's uid on the host. With `--userns=keep-id`, the operator's
uid is mapped to the same uid inside and the run runs as it, while the container's root becomes a
subordinate id, which is nobody on the host. Two things follow:

- Files the run writes into a mounted directory belong to the operator on the host.
- The run is not root inside the container either. Root inside a `keep-id` container still holds
  capabilities over the files of the ids it maps, the operator's included, so the design also
  drops them (§3.5). [CONTAINER_TUTORIAL.md](../CONTAINER_TUTORIAL.md) §2.4 shows the mappings
  and the measurement.

*Corrected on 2026-09-25, before any drive used it:* the first version of this paragraph said
that the container's root is a nobody outside, which is true only with `keep-id`.

A **bind mount** (`-v host-path:container-path:mode`) makes one host directory, or one file,
visible inside the container, either read-write or read-only. Mounts are the only way host files
enter the container's view. **What is not mounted does not exist for the process**, so a command
that names a host path finds nothing there.

With `--rm`, podman deletes the container when its process ends. Every run starts from the same
image, so one run can leave something for the next only through a mount.

**The threat model, stated.** A container shares the host's kernel, so a kernel exploit crosses
it, where a virtual machine would not. The threat here is a model's mistaken or misguided command,
not an attacker with a kernel exploit, and for that a container is proportionate.

## 3. The design

### 3.1 One container per run

Each task runs as its own `podman run --rm`, from one image built from the `Containerfile`. The
image is Ubuntu 26.04 with git, python3, curl and libcurl. The run gets a fresh, empty HOME. The
image is not pinned by digest, so the drive's manifest records the image ID of each run.

### 3.2 What the container sees

| Inside | From the host | Mode | Why |
|---|---|---|---|
| `/work` | the run's workspace, a clone outside every repository | read-write | the run's job is to change it |
| `/drive-home` | a fresh, empty directory per run | read-write | jichi's HOME, deleted after the run |
| `/cfg` | the drive's config | read-only | the run must not change what it is measured with |
| `/opt/jichi/jichi` | the pinned jichi binary | read-only | the same |
| `/opt/tool` | the toolchain the test project's gate needs | read-only | the same |
| `/record/*.fifo` | two named pipes | written through a pipe | the record, §3.3 |

### 3.3 The record leaves through pipes

- **The stream and stderr.** jichi's stdout (the `--output jsonl` stream) and stderr are relayed
  by podman. The host redirects them into files the container never sees.
- **The journal and telemetry.** `--journal` and `--log` point at two named pipes (FIFOs) mounted
  into the container. A reader on the host appends each one to a file. The container sees a
  pipe, never the file behind it.

**Why pipes, and not a mounted record directory.** Anything mounted writable can be written by the
model's shell, because the shell runs under jichi's uid. The far end of a pipe is out of its reach.
The prototype's worst case shows the difference. `rm -rf /*` emptied the run's HOME, which is
where jichi keeps its journal and telemetry by default, while the copies that went through the
pipes arrived whole.

**The limit, measured.** The record is *append-only*, not unforgeable. The shell can write to every
channel that jichi writes to, and in the prototype one injected line arrived in each of the three.
An analysis must therefore read the record as what jichi wrote plus whatever the shell appended.
It can tell the two apart only with a second witness outside the container. The model server's
own log is one: LM Studio keeps every request and response.

### 3.4 The network

The recommended mode is `--network=pasta:-T,<port>`. The container gets its own network namespace,
and the one port the model server listens on is forwarded to the same port on the host's
loopback. The config keeps `http://127.0.0.1:<port>/v1`, and LM Studio can stay bound to loopback,
as `CLAUDE.md` requires.

| Mode | The model server on the host's loopback | Another service on the host's loopback |
|---|---|---|
| `--network=pasta:-T,<port>` | reached | not reached (measured) |
| `--network=host` | reached | **reached** (measured: HTTP 200) |
| `--network=pasta` | **not reached** through `host.containers.internal` | not reached (measured) |

**Why only that port, measured twice over.** pasta itself would also map the container's
gateway address to the host's loopback. podman switches that off: its debug log shows the
arguments it passes, `-t none -u none -T none -U none --no-map-gw` and
`--map-guest-addr 169.254.1.2`, and podman-run(1) states the `--no-map-gw` default.
`host.containers.internal` is 169.254.1.2, which pasta maps to the host's own network address,
not to its loopback. A probe against a service bound to the host's `127.0.0.1` then found no
connection through any of the three routes: `127.0.0.1`, the gateway, and
`host.containers.internal`. A control run opened a route on purpose
(`--map-host-loopback,169.254.1.3`), and the same probe reached the service, so the probe can see
a route when there is one. [CONTAINER_TUTORIAL.md](../CONTAINER_TUTORIAL.md) §5 has the commands.

**Outbound connections are open.** From inside a `pasta:-T` container, an HTTPS request to
`example.org` got `HTTP 200`. Nothing in this design restricts outbound traffic.

### 3.5 Limits, and the two caps they add

`--memory=4g --pids-limit=512` bound a runaway build or a fork bomb. The shell read both back
from `/sys/fs/cgroup`. Rootless podman can set them only where the user's systemd slice delegates
the `memory` and `pids` controllers (it does on the bench that measured this).

**Both are caps**, in `CLAUDE.md`'s sense of "Caps versus fences". A build that needs more memory
is killed, and the run's result is then a different one, not a missing one. So the drive's plan
names them under rule 3, and takes their values from the test project's measured peak, not from
this prototype.

`--security-opt=no-new-privileges` stops a setuid program from raising privileges inside.
`--cap-drop=all` leaves nothing inside with root's capabilities. Measured: with podman's default
capabilities, root inside a `keep-id` container created a file in a mode-0700 directory of the
operator's; with `--cap-drop=all`, the same attempt was refused. The run itself is not root, so
this is a second belt, for anything that would otherwise be root inside.

## 4. What the prototype measured

A script starts a scripted mock model on the host. The mock answers jichi's first request with one
shell command and its second with a file write. The script then checks, on the host, what
survived. All cases passed under `--network=pasta:-T,<port>` and under `--network=host`
(jichi 0.12.0, podman 5.7.0, Linux 7.0). A fourth case measured the finding of §6.

| Case | The model's shell | Result |
|---|---|---|
| reach | deletes a host directory by its absolute host path, the shape of ANECDOTES #108 | the path does not exist inside; the directory is intact |
| worst | runs `rm -rf /*` | the workspace, including `.git`, and the run's HOME are emptied; the image's programs, the read-only mounts and the record are intact |
| tamper | tries to truncate jichi's stdout, deletes the record pipes, writes into each channel | no truncation (stdout is a pipe); the pipes survive (`Permission denied`); **the injected lines arrive** |

**After M770** the environ and tamper cases ran again, under both network modes. jichi's
`/proc/1/environ` and `/proc/1/fd` are refused, and the stream takes no injected line. The
journal and telemetry pipes still take one line each, because the shell opens them by name.
Before the rerun, the tamper case was changed to run each `/proc` step in a subshell of its
own: its first step, `: > file`, is a special built-in, and a refused redirection there ends a
non-interactive shell. So a first rerun reported "0 injected" for pipe writes it never tried.

Two of the script's checks were wrong at first, and each reported a finding that was not there.
The internal README records both. One assumed the stream's first event was `message_start`, but it
is `run_start` when `--journal` is set. The other probed a service that had never started. A
control run caught each one.

## 5. What the container does not contain

1. **The run's own workspace and HOME.** This is by design. The drive re-clones a workspace
   whose `.git` is gone.
2. **Lines added to the record** (§3.3).
3. **The API keys jichi is started with.** Since M770, jichi's own copy is hidden (§6). What the
   container holds in its mounts, the shell can read.
4. **Outbound network traffic,** which is open (§3.4).
5. **The model server's API.** The forwarded port carries the server's whole API, not only chat.
   A run can send it requests of its own, and a server that loads models on demand will load
   whichever model it is asked for.
6. **The kernel**, which the container shares with the host (§2).
7. **Time.** Bounding a run's duration is the drive's outer timeout, not the container's.

## 6. A finding about API keys, fixed as M770

The prototype found a way for the programs jichi starts, the model's shell among them, to reach
the API keys jichi was started with. jichi already took steps to keep those keys from them. A
container does not close it, and it affected every use of the model's shell, not only drives.
Following [SECURITY.md](../../SECURITY.md), which promises a fix first and a public write-up
after, this section waited for the fix.

**The mechanism.** Since M130, jichi removes every key variable from a child's environment before
it runs the child. But the child runs as the same user as jichi. Linux lets a process read
`/proc/<pid>/environ` of any process of the same user whose *dumpable* flag is set, and every
ordinary process has it set. The file holds the environment the process started with. The scrub
changes the child's copy, not the parent's, so one command got the key back:

```sh
tr '\0' '\n' < /proc/$PPID/environ | grep KEY
```

It was measured on 2026-09-25 with a fake key, in the container and on the host, under Yama
`ptrace_scope` 1, which restricts attaching and not this read. The tamper case used the same
route, `/proc/$PPID/fd/1`, to write into jichi's stdout.

**The fix.** jichi makes itself non-dumpable at startup (`prctl(PR_SET_DUMPABLE, 0)`). Its
`/proc` entries then refuse every other process of the same user. The kernel resets the flag at
exec, so the programs jichi starts are ordinary.
- **What it costs:** jichi's own core dump, and a debugger's attach. `JICHI_KEEP_DUMPABLE=1`
  keeps both, and `jichi doctor` warns while it is set.
- **Where it applies:** Linux only. On other systems the flag does not exist, and doctor warns.
- **The details:** [HARDENING.md](../HARDENING.md) §2, "The parent's own copy".

**In the container, after the fix:** `/proc/1/environ` is refused, and so is the write through
`/proc/1/fd/1` (§4).

**What is still open, for the drive.** The fix hides jichi's copy of a key. Anything the container
mounts, the shell can read, so **a key file is never mounted**, and a key reaches the container
only in jichi's environment. With that, a drive can use a model that needs a key. LM Studio needs
none.

## 7. The drive's rules, carried over

- **Fences on, caps off, every cap named.** The list now includes the container's memory and process
  limits (§3.5).
- **The record is out of the shell's reach** (§3), and **leaves the machine after each run**. Where
  it goes is the operator's decision, because pushing to a forge is outward-facing.
- **The workspace is a clone outside every repository.** Every task resets it, and a workspace
  whose `.git` is gone is re-cloned.
- **No priced model**, checked before the first request, as `scripts/corpus-drive.sh` already does.
  **No key file in the container**: a key reaches it only in jichi's environment, which M770 hides
  from the shell (§6).

## 8. Before the next drive

1. **`scripts/corpus-drive.sh` gains a container mode:** one `podman run` per task, the journal
   and telemetry through pipes, a fresh HOME, and a re-clone when `.git` is gone. The internal
   README lists the changes in the script's own terms.
2. **The drive's plan names the container's caps**, with values from the test project's measured
   peak.
3. **The analysis scripts read the record as append-only.** They count any line that is not a
   jichi event as injected, and cross-check the rest against the model server's log.
4. **The fix for §6** lands first, or the drive uses only models that need no key. *Done: M770.*
5. **Rehearse in stages:** a dry run of the real drive script against the mock model, then one real
   task, then the drive.

## 9. Decisions for the operator

1. **The network mode.** `pasta:-T,<port>` is recommended over `host`.
2. **Where each run's record goes** off the machine.
3. **Whether the fix for §6 is the first milestone after the merge.** Recommended: it should land
   before any drive that needs a key, and before the next public snapshot. Every user of the
   model's shell is affected, not only drives, and the snapshot that carries the fix can then
   carry its write-up. *Decided 2026-09-25: yes. It landed as M770.*
4. **Later: the rest of the public recipe.** [DEPLOYMENT.md §5](../DEPLOYMENT.md) now points at
   [CONTAINER_TUTORIAL.md](../CONTAINER_TUTORIAL.md), which covers one run. DEFERRED's
   shell-sandbox row asks for a launcher recipe, and it now also names Linux Landlock, which
   restricts a child's writes from inside jichi. The two can work together, and a design for
   Landlock is its own plan.

## 10. References

In this repository:

- [ANECDOTES.md #108](../ANECDOTES.md): the drive that was deleted.
- [analysis/2026-09-25-the-overnight-drive.md](../analysis/2026-09-25-the-overnight-drive.md)
  §11: the three rules.
- [DEFERRED.md](../DEFERRED.md): the shell-sandbox row, and the row on the drive's record.
- [AUTONOMY.md](../AUTONOMY.md): the two ways jichi changes a file.
- [DEPLOYMENT.md §5](../DEPLOYMENT.md): hardening an unattended host.
- [SECURITY.md](../../SECURITY.md): how a vulnerability is reported, fixed and written up.

Elsewhere:

- podman-run(1): <https://docs.podman.io/en/latest/markdown/podman-run.1.html>
- pasta(1), which shares its manual with passt: <https://passt.top/builds/latest/web/passt.1.html>
- user_namespaces(7), on the uid mapping behind rootless containers:
  <https://man7.org/linux/man-pages/man7/user_namespaces.7.html>
