# Daemon (warm process)

Every `jichi -p …` run normally cold-starts: it reloads the config,
reconnects MCP servers, respawns LSP servers, reloads the index, and rebuilds the
request prefix. The **daemon** keeps one fully-configured app hot and serves
requests over a Unix socket, so a client pays none of that per request — cutting
latency and, on a cacheless backend, cost.

## Usage

```sh
# Start the warm process (foreground; Ctrl-C to stop). Its workspace is the cwd
# it starts in -- run one daemon per project root.
jichi --config ./config.json daemon --socket /tmp/jichi.sock

# From anywhere, send a turn to it (thin client -- no app setup on this side):
jichi --connect /tmp/jichi.sock -p "explain src/foo.c"
jichi --connect /tmp/jichi.sock --output jsonl -p "run the tests"
echo "summarise this" | jichi --connect /tmp/jichi.sock -p -
```

If `--socket` is omitted, the path is `$JICHI_DAEMON_SOCK`, else
`~/.jichi.d/daemon.sock`. The client uses the same default, so on a single
project `jichi daemon` and `jichi --connect …` need no path.

## Setting one up, step by step

For a first time. Each step says what to expect, so you can tell a working step from a
half-working one.

**1. Check the plain path works first.** A daemon cannot fix a broken config, it only makes it
warm:

```sh
cd /path/to/your/project
jichi doctor          # every line a tick, or fix what it names
jichi -p "say ok"     # a real answer, so the model is reachable
```

If `doctor` reports a problem, stop here. A daemon started on a broken config serves that
broken config to every client until you restart it.

**2. Start it in the foreground and read the banner.**

```sh
jichi daemon
```

You should see one line like:

```
daemon: listening on /home/you/.jichi.d/daemon.sock (Ctrl-C to stop); 4 worker(s)
```

Two things in it matter. The **path** is what clients connect to. The **worker count** is how
many turns can run at once (config `daemonWorkers`, default `min(cpu, 4)`).

**3. Check the socket, from another terminal.**

```sh
ls -l ~/.jichi.d/daemon.sock
# srw-------  1 you you 0 ... daemon.sock
```

The leading `s` means socket; `rw-------` means only you can connect. If you see anything
looser, you are on a build older than M322 — see [Trust model](#trust-model-and-hardening-m322).

**4. Send it a turn.**

```sh
jichi --connect ~/.jichi.d/daemon.sock -p "list the files here"
```

The answer arrives on stdout exactly as with a normal `-p` run. The difference is invisible and
is the point: no config reload, no MCP reconnect, no index reload.

**5. Confirm you are actually saving something.** Time both paths on the same prompt:

```sh
time jichi -p "say ok"                                   # cold
time jichi --connect ~/.jichi.d/daemon.sock -p "say ok"  # warm
```

If the warm one is not noticeably faster, your cold start was already cheap (no MCP, no LSP, no
index) and the daemon is not buying you much. That is a legitimate outcome — better to learn
it here than to build a workflow on it.

**6. Then move it into the background**, once the foreground run works. Under tmux is the
friendliest way to start, because you can look at it again:

```sh
tmux new -s jichi-daemon -d 'jichi daemon'
tmux attach -t jichi-daemon      # look at it
# Ctrl-b d to detach and leave it running
```

See [TMUX.md](TMUX.md) for the layout, scrollback and the detach/attach mechanics.

### The workspace is the directory it started in

A daemon serves **one project**: the cwd at start becomes its workspace, and every client turn
runs against that. `--connect` from another directory does **not** move it. Run one daemon per
project root, with one socket path each:

```sh
cd ~/work/alpha && jichi daemon --socket ~/.jichi.d/alpha.sock
cd ~/work/beta  && jichi daemon --socket ~/.jichi.d/beta.sock
```

### When something is wrong

| Symptom | Cause | Fix |
|---|---|---|
| `could not bind/listen` | a daemon is already running, or the path is stale and not writable | check with `ls -l`; a stale socket from a crash is unlinked automatically, a live one is not |
| `socket path too long (max 107 bytes)` | `sun_path` is a fixed 108-byte field — this is the OS, not jichi | use a shorter path; `~/.jichi.d/` is short by design |
| the client hangs | every worker is busy | raise `daemonWorkers`, or wait; each turn holds a worker for its whole duration |
| answers ignore a config change | the daemon does not reload | restart it |
| answers are about the wrong project | wrong daemon, or wrong cwd at start | one socket per project root, and check the banner's path |

## Protocol

One newline-delimited JSON request per connection; the daemon streams the
response and closes the connection (EOF frames the reply). Requests:

```json
{"v":1,"type":"prompt","prompt":"...","cwd":"...","mode":"chat|plan|auto","format":"text|jsonl"}
{"type":"ping"}       ->  {"type":"pong"}
{"type":"shutdown"}   ->  {"type":"bye"}   (the daemon then exits)
{"v":1,"type":"hello"}                     (M528; optional -- see below)
```

`hello` is **additive and optional**. `prompt`, `ping` and `shutdown` are listed
as *Stable* in [EMBEDDING.md](EMBEDDING.md), so a handshake is never required and
a client that has never heard of `hello` keeps working unchanged. Its reply:

```json
{"v":1,"type":"hello.ok","agent":"jichi 0.9.0","proto":[1],
 "groups":["session"],
 "limits":{"maxLine":1048576,"maxConcurrent":4},
 "auth":{"transport":"unix","mechanism":"socket-mode","uid":1000,
         "modeVerified":true,"peercred":false}}
```

`limits` lists only what is *enforced* — a `maxPromptBytes` was drafted and
removed, because the prompt shares the line budget and has no separate cap.

### The `assignment` verb group (M529)

Three verbs make jichi's teaching features reachable by other software — a course
platform, a marking service, an editor plugin — instead of only by running the CLI
and parsing output written for a human:

```json
{"v":1,"type":"assignment.list"}
{"v":1,"type":"assignment.get","name":"00-hello.md"}
{"v":1,"type":"assignment.grade","name":"00-hello.md"}
```

`assignment.list` returns the set with each spec's title, phase, difficulty,
points, whether a solution sibling exists, and the learner's standing (attempts,
passed, best percentage, hint pulls). `assignment.get` returns the frontmatter, the
task body, the audience-framed rendering — and the **`verify` command**, because a
grade whose basis a caller cannot inspect is an opinion. `assignment.grade` runs it
and returns a verdict:

```json
{"v":1,"type":"assignment.grade.ok","name":"10-pass.md","passed":true,"pct":100,
 "points":3,"of":3,"tests":{"run":0,"failed":0},
 "verify":{"command":"true","exitCode":0}}
```

All three read from the **same collector and the same grading core the CLI uses**,
so `assignments`, `grade` and the wire cannot disagree about what an assignment is
or what it scored.

**A name, never a path.** The verbs take a plain `<file>.md` and the server
resolves it inside `<workspace>/docs/assignments/`. Anything that expresses a
location — a `/`, a `..`, a leading dot — is refused with
`{"code":"assignment.name"}`. This is not decoration: grading **runs the spec's own
`verify` command**, so a caller who could name a location could name a file they
wrote. `assignment.list` therefore returns a bare `name` beside `file`, so turning
a listing into a request needs no string surgery.

**A refusal is not a failing grade.** Three distinct error codes, deliberately not
collapsed into `passed: false`:

| Code | Meaning |
|---|---|
| `assignment.no_verify` | the spec defines no success condition, so there was nothing to fail |
| `assignment.not_gradeable` | the verify command cannot run from this workspace — M502's "this is NOT a grade", on the wire |
| `assignment.not_found` | a well-formed name, but no such spec |

The middle one is the one to care about: a marking service that recorded a FAIL
because a *harness* was missing would repeat the exact defect M502 fixed for the
CLI, where a learner saw a failing grade on correct work.

**There is no `assignment.attempt`.** The protocol proposal specifies one — submit
an attempt as an artifact — and it is deliberately not here: it means writing
caller-supplied files into a workspace the daemon did not choose (its workspace is
the directory it started in) and then executing a verify over them, with no
artifact validation anywhere. That needs a fence of its own, not a verb bolted
onto this one.

For a `prompt`, the daemon runs a normal headless turn and streams its stdout
(the answer in `text` mode, or `jc_agentjson` events in `jsonl` mode) back over
the socket; diagnostics stay on the daemon's own stderr. The pure request codec
is `jc_daemon_proto` (`include/jc_daemon.h`); the socket loop + warm app live in
`run_daemon` (`src/main.c`).

## Trust model and hardening (M322)

> **M528 changed three things below; read this first.** The specification is
> [`proposals/2026-08-jichi-protocol.md`](proposals/2026-08-jichi-protocol.md)
> (M525), which treats "no token, no authentication and no peer check" as the one
> item on its list that is a missing *concept* rather than a missing verb. What
> has now been implemented is the portable part of it:
>
> 1. **The mode is verified, not merely requested.** The daemon reads the socket's
>    mode back after `bind()` and **refuses to serve** unless it is owner-only and
>    owned by the running user. The paragraph below explains why asking was not
>    enough: it names a case — "platforms that ignore the umask for sockets" —
>    that neither the umask nor `jc_make_private` can rule out, and on such a
>    platform nothing would have said so.
> 2. **There is a request-size limit.** There was none: the reader took one byte
>    at a time into an unbounded buffer until a newline, so a client that never
>    sent one made the server allocate until it died. Now 1 MiB
>    (`JC_DAEMON_MAX_LINE`), and exceeding it is `{"code":"limit.line"}` — an
>    error, never a truncated request that means something else. **M530 also made
>    the reader buffered**, because declaring that limit is what made the
>    one-byte-per-`read(2)` loop reachable: measured on a single legitimate
>    900 KB request, **900,128 read syscalls and 3.50 s** of syscall time became
>    **270 and 0.001 s**.
> 3. **`hello` states the posture.** A new, *optional* request returns the
>    protocol version, the limits, the uid the server resolved, and two honest
>    fields: `modeVerified` (true — the process refused to start otherwise) and
>    **`peercred": false`**.
>
> **What is still not done, and why:** there is no peer-credential check.
> `SO_PEERCRED` with `struct ucred` and BSD's `getpeereid` are both *hidden* under
> `_POSIX_C_SOURCE=200112L` on glibc — measured, not assumed — and the two ways
> round that are defining `_GNU_SOURCE` (this project is POSIX-only by rule) or
> hardcoding the `SO_PEERCRED` constant (which differs by architecture on Linux,
> the exact portability landmine this project refuses). So the field says `false`,
> because claiming an authentication that is not performed is worse than having
> none. On a POSIX system a `0600` socket can only be connected by its owner or
> root anyway; the verified mode is the control, and a peer check would be defence
> in depth on top of it.

**The socket's file mode is the entire access-control list.** There is no token, no
authentication and no peer check: anything that can `connect()` can send a prompt, and a
prompt makes the daemon read and write files, run shell commands and call tools **as the user
running the daemon**. It is exactly as privileged as a shell on that account.

So, since M322, the daemon creates its socket **`0600` — same-user only**, by setting the
umask around `bind()` rather than `chmod`ing afterwards (a `chmod` leaves a window in which
the socket already exists at the looser mode and a connection can succeed).

Before M322 the mode came from the process umask: with a typical `umask 022` that is **0755**,
and with `umask 002` it is **0775** — in both cases *any local user could drive the daemon*.
The M159 [control channel](CONTROL.md) had this right from the start; the more dangerous of
the two sockets did not.

What this means in practice:

- **Do not put the socket somewhere group- or world-writable** and expect the mode to save
  you: a user who can *delete* your socket can bind their own in its place and collect the
  prompts your client sends. Keep it under `~/.jichi.d/` (the default) or another directory
  only you can write.
- **Do not share a daemon between accounts.** If two people need one, they need two daemons.
- **The daemon is not a network service.** It is `AF_UNIX` only, deliberately, and there is no
  option to make it TCP. If you need remote access, put SSH in front of it
  ([REMOTE_SSH.md](REMOTE_SSH.md)) — that way the authentication is SSH's, which is a system
  that has been thought about.
- **On a shared machine, prefer a per-user socket path** and check it:

  ```sh
  ls -l "${JICHI_DAEMON_SOCK:-$HOME/.jichi.d/daemon.sock}"
  # want: srw-------  (s = socket, rw for you only)
  ```

- **The daemon inherits the config it started with**, including `pathFence`, `editScope` and
  the permission posture. A daemon started with a permissive config stays permissive for every
  client afterwards — so start it with the posture you want, and restart it after changing the
  config (it does not reload).

`tests/smoke/stop_reason_capped.sh` asserts the `0600` mode, so a regression is a red build
rather than a quiet exposure.

## Semantics & limits

- **Bounded fork-per-request worker pool (W3)** — each `PROMPT` is served by a
  forked child (a copy-on-write snapshot of the warm app), and the parent keeps
  accepting up to `daemonWorkers` concurrent turns (config `daemonWorkers`, default
  `min(cpu, 4)`; `0` = auto). A per-request watchdog (`daemonWorkerTimeout`, default
  300s) force-kills a wedged turn (SIGTERM→SIGKILL) so one bad request can't hang
  the listener. Because a child mutates only its COW copy of the app, turns don't
  corrupt each other's in-memory state.
  - *Filesystem caveat:* concurrent workers still share the workspace on disk. For
    write-heavy concurrent turns set `"daemonWorkers": 1`, or delegate writes to
    isolated git worktrees via `spawn_parallel` (`write:true`).
- **Output formats** — `text` (default), `json` (one terminal object) and `jsonl`
  (one event per line) all work over the socket. Before **M431g** the wire `format`
  was a boolean, so `--connect --output json` was silently served as **text** and the
  single-object contract was unavailable over the daemon; if you are on an older
  build, use `--output jsonl` and read the terminal `done` event. An unknown
  `format` is served as text rather than refused, so a newer client cannot make an
  older daemon error out — but it *will* get text, so pin both versions when you
  depend on the shape (the `EMBEDDING.md` advice).
- **Warm session** — history persists across requests until the daemon restarts
  (continuity is usually what you want; restart for a clean slate).
- **Workspace is the daemon's cwd** — the path fence root and snapshot key are
  bound at startup, so run one daemon per project. A request's `cwd` is currently
  advisory.
- **Networking required** — the daemon (like the agent) needs libcurl.

See `docs/SELF_IMPROVEMENT.md` (M100) for the design and the roadmap it anchors.
For working a *queue of tasks* on a loop (rather than serving ad-hoc requests),
see [AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md).
