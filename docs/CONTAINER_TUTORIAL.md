# Tutorial: a container around jichi's shell

> **Prerequisites:** a Linux machine with rootless podman 5 (`podman --version`), and a jichi
> checkout built with `make` and `make smoke-tools`. You need no model server and no API key. The
> model in this tutorial is a scripted mock that ships with jichi's tests.

> **Unfamiliar word?** §10 defines the container terms this page uses, and
> [`VOCABULARY.md`](VOCABULARY.md) defines the project's own.

**Status, honestly.** Everything on this page was measured on one machine on 2026-09-25: Ubuntu
26.04, Linux 7.0, podman 5.7.0. The model was a scripted mock, not a real one. The design is the
one planned for jichi's next unattended drive
([plan](plans/2026-09-25-contained-drive.md)), and no drive has run this way yet. One finding
from the same measurements was left out of the first version of this page, because
[`SECURITY.md`](../SECURITY.md) puts the fix before the write-up. It was a weakness in jichi, it
was fixed the same day (M770), and §8 now says what it was.

---

## 0. What you will learn

- why jichi's own fences cannot stop a shell command, and what can;
- what a container is: an ordinary process with a different view of the system;
- how to run one jichi task in a rootless container, and check from outside what it could and
  could not do;
- how to keep a record that the program you are recording cannot delete;
- how a rootless container gets a network (pasta), and how to give it exactly one door;
- how the measurements of a boundary go wrong, through five mistakes made while measuring and
  writing this page.

## 1. The problem

jichi changes a file in one of two ways ([AUTONOMY.md](AUTONOMY.md)):

1. **Through its file tools.** They all pass one chokepoint, where `--edit-scope` and the path
   fence are enforced.
2. **Through a shell command**, which the fence does not see.

A shell runs as the user who started jichi, so it can do everything that user can. At 00:42 on
2026-09-25, during an unattended drive, a model sent one `run_terminal_command` that deleted the
drive's whole record: every run's telemetry, journal and stream
([ANECDOTES #108](ANECDOTES.md)). The fence was on and working. It simply has no say over the
shell.

**Could jichi filter shell commands instead?** Not reliably. A filter reads a command's text, and
the same effect has countless spellings: `rm -rf`, `find -delete`, a one-line Python script, a
Makefile rule. A filter that catches the spellings it knows is a list of spellings, not a wall.
The reliable answer is to change what the *process* can reach, whatever it runs. The operating
system's isolation features do that, and a container packages them. The oldest safety row in
[DEFERRED.md](DEFERRED.md) says the same: contain the shell by deployment.

## 2. Six ideas you need

### 2.1 A process runs as a user

Every process has a user id (uid), and the kernel checks it against each file's owner and
permission bits. Your shell, your editor and jichi all run as you, so each can read and delete
your files. That is the problem in one sentence: as far as the kernel knows, the model's shell
*is* you.

### 2.2 Namespaces: a different view of the same system

A namespace gives a process its own copy of one kind of system resource:

| Namespace | What the process gets its own of |
|---|---|
| mount | the tree of mounted filesystems: what `/` is, and what lies under it |
| PID | the list of processes it can see |
| network | interfaces, addresses, routes, and its own loopback, `127.0.0.1` |
| user | the mapping from user ids inside to user ids outside |

A **container** is an ordinary process started in new namespaces, with an image as its root
filesystem. Nothing is emulated. It runs on the host's kernel at full speed.

That is also its limit. A container shares the kernel, so a kernel exploit crosses it, where a
virtual machine would not. The threat here is a model's mistaken command, not a kernel exploit,
and for that a container is enough.

### 2.3 An image, from a Containerfile

An **image** is a filesystem tree: a Linux userland without a kernel. A `Containerfile` is its
recipe: `FROM` a base image, then `RUN` commands at build time. Every container starts from the
same tree. With `--rm` it is deleted when its process ends, so one run can leave something for
the next only through a mount.

### 2.4 Rootless, and keep-id

**Rootless** podman needs no root. It builds each container's user ids from two things you own:
your own uid, and a range of *subordinate* ids listed in `/etc/subuid`. Check that you have a
range:

```sh
grep "^$USER:" /etc/subuid
```

Which id inside is which id outside depends on one option, and it is worth seeing rather than
taking on trust. `/proc/self/uid_map` prints the mapping, one range per line: the first id
inside, the first id outside, and how many ids follow.

```sh
podman run --rm docker.io/library/ubuntu:26.04 cat /proc/self/uid_map
podman run --rm --userns=keep-id docker.io/library/ubuntu:26.04 cat /proc/self/uid_map
```

On the machine that measured this, the user's uid was 1000 and the subordinate range was
100000–165535. The first command printed `0 1000 1` and `1 100000 65536`:

- **By default, root inside the container is you outside.** The container's uid 0 is your uid
  1000. The process runs as root inside, and a file it writes into a mounted directory belongs to
  you. That is not root on the host, but it is you, with your access to everything you mount.

The second printed `0 1 1000`, `1000 0 1` and `1001 1001 64536`. Those lines are relative to
podman's own user namespace, not to the host. `podman unshare cat /proc/self/uid_map` prints that
namespace, `0 1000 1` and `1 100000 65536`, and the lines read through it:

- **Your uid inside is your uid outside, and the process runs as it.** A file it writes belongs
  to you. The image's own files belong to root inside, so they are not its to change.
- **Root inside becomes one of your subordinate ids,** 100000 here, which is nobody on the host.
  Inside its namespace, though, it keeps root's capabilities, and they reach the files of every
  id the namespace maps, yours included. Measured: a root process in a `keep-id` container
  created a file in a mode-0700 directory of yours. With `--cap-drop=all`, the same attempt was
  refused.

That is why the run in §3 uses both. `keep-id` makes jichi and its shell run as you, not as root.
`--cap-drop=all` leaves nothing inside with root's powers.

### 2.5 Bind mounts: the only doors

`-v host-path:container-path:mode` makes one host directory, or one file, visible inside the
container, read-write (`rw`) or read-only (`ro`). **What is not mounted does not exist for the
process.** A command that names a host path finds nothing there. A read-only mount cannot be
changed by the run. In the prototype, `rm -rf /*` left every read-only mount intact, although the
run's uid owned the files behind them.

### 2.6 Limits: cgroups

`--memory` and `--pids-limit` limit the memory and the number of processes of everything in the
container, so a runaway build or a fork bomb stops at the limit. Rootless podman can set them
only on cgroups v2, and only when the `memory` and `pids` controllers are delegated to your user:

```sh
cat /sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/cgroup.controllers
```

**A limit is a cap.** A build that needs more memory is killed, and its result is then a
different one. When you measure something, name every limit you set. This project learned that
the hard way ("Caps versus fences" in its testing rules).

## 3. Try it: one jichi task in a container

Each step can be pasted as it stands. Run them in order, in one shell.

**Step 1: a scratch directory, and something to protect.** Start in your jichi checkout:

```sh
J=$(pwd)
W=$(mktemp -d)
cd "$W"
mkdir workspace home cfg record sentinel capture
echo "the operator's data" > sentinel/keep.txt
git -C workspace init -q
mkfifo record/journal.fifo
cat record/journal.fifo > record/journal.jsonl &
READER=$!
```

- `sentinel/keep.txt` stands for everything the run must not touch.
- `workspace` is the only project directory the run will see.
- The last three lines make a named pipe for jichi's journal and start its reader. §4 explains
  why.

**Step 2: the image.**

```sh
cat > Containerfile <<'EOF'
FROM docker.io/library/ubuntu:26.04
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates git python3 curl libcurl4t64 && \
    rm -rf /var/lib/apt/lists/*
RUN git config --system --add safe.directory /work
WORKDIR /work
EOF
podman build -t jichi-sandbox - < Containerfile
```

Reading the recipe from standard input (`- < Containerfile`) gives the build no context, so
nothing from this directory goes into the image. The image holds only the libraries jichi links
against and the tools a task may use. jichi itself stays on the host and is mounted read-only in
step 5.

**Step 3: a scripted model.** The mock model in jichi's tests answers each request with the next
scripted reply. This script sends a shell command that deletes the sentinel *by its absolute host
path*, which is what the command in the incident did. Then it writes a file, then it answers.

```sh
cat > model.script <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"rm -rf $W/sentinel; ls $W; id -u"}
rule
  count 2
  tool write_file {"path":"hello.txt","content":"written from inside the container\\n"}
rule
  count 3
  text Done: tried to delete the sentinel, then wrote hello.txt.
EOF
"$J/tests/tools/mockmodel" --script model.script --capture capture \
    --port-file port --deadline 300 --max-requests 10 > mock.log 2>&1 &
MOCK=$!
until [ -s port ]; do sleep 0.2; done
PORT=$(cat port)
```

The mock listens on `127.0.0.1` only, the way this project keeps LM Studio: nothing outside
the machine can reach it.

**Step 4: the config**, which the run will only be able to read:

```sh
cat > cfg/config.json <<EOF
{"models": [{"name": "mock", "provider": "openai", "model": "mock",
             "apiBase": "http://127.0.0.1:$PORT/v1", "apiKey": "not-a-key",
             "roles": ["chat"]}],
 "snapshots": false, "repoMap": false, "toolProfile": "full"}
EOF
```

**Step 5: the run.**

```sh
podman run --rm --userns=keep-id --network=pasta:-T,$PORT \
    --security-opt=no-new-privileges --cap-drop=all \
    --memory=2g --pids-limit=256 \
    -v "$W/workspace:/work:rw" \
    -v "$W/home:/drive-home:rw" \
    -v "$W/cfg:/cfg:ro" \
    -v "$J/jichi:/opt/jichi/jichi:ro" \
    -v "$W/record/journal.fifo:/record/journal.fifo" \
    -e HOME=/drive-home \
    jichi-sandbox \
    /opt/jichi/jichi --config /cfg/config.json --auto --no-session -q \
        --journal /record/journal.fifo --output jsonl -p "try the sandbox" \
    < /dev/null > record/stream.jsonl
```

| Option | What it does |
|---|---|
| `--rm` | deletes the container when jichi exits |
| `--userns=keep-id` | runs jichi as your uid inside, not as root (§2.4) |
| `--network=pasta:-T,$PORT` | its own network, with one door: the model's port (§5) |
| `--security-opt=no-new-privileges` | a setuid program cannot raise privileges inside |
| `--cap-drop=all` | leaves nothing inside with root's powers, which reach your mounted files (§2.4) |
| `--memory`, `--pids-limit` | caps on memory and processes (§2.6) |
| `-v …:rw` | the workspace and a fresh HOME: the only host directories the run can write |
| `-v …:ro` | the config and the jichi binary: readable, not changeable |
| `-v …journal.fifo` | the journal's pipe (§4) |
| `< /dev/null` | a headless run must not wait for input |
| `> record/stream.jsonl` | jichi's event stream, written by the host shell, outside the container |

**Step 6: look from outside.**

```sh
cat sentinel/keep.txt
ls workspace
grep '"type":"tool_result","name":"run_terminal_command"' record/stream.jsonl
wait $READER; wc -l record/journal.jsonl
kill $MOCK
```

What you should see:

- **The sentinel.** It still says `the operator's data`. Inside the container, `$W` does not
  exist, so `rm -rf` found nothing to remove.
- **The workspace.** It holds `hello.txt`, written from inside: it is the one project directory
  the run can write.
- **The tool result.** It shows what the shell saw. `ls` could not find `$W`, and `id -u` printed
  your own uid, not 0.
- **The journal.** It arrived through its pipe. The reader ends when jichi closes the pipe. If
  jichi failed before it opened the pipe, the reader is still waiting: stop it with
  `kill $READER`.

**Step 7: clean up.**

```sh
cd "$J"
[ -n "$W" ] && rm -rf "$W"
podman image rm jichi-sandbox
```

## 4. A record the shell cannot delete

A record stored inside the container, or in a mounted directory, can be deleted by the run's
shell, because the shell runs as the same uid as jichi. A **named pipe** (FIFO) changes that. The
container sees the pipe, and only the host opens the file behind it.

Try the pipe on its own, in any scratch directory:

```sh
mkfifo demo.fifo
cat demo.fifo &
echo hello > demo.fifo
wait; rm demo.fifo
```

`cat` blocks until a writer opens the pipe. It prints `hello`, and ends when the last writer
closes the pipe. The kernel passes the bytes directly: "the FIFO special file has no contents on
the filesystem" (fifo(7)). That is why step 1 starts the reader *before* the run. jichi's open of
the pipe waits until there is a reader, and the reader waits until jichi opens it.

Two things follow for the record:

- **It cannot be deleted from inside.** The file behind the pipe is on the host, outside every
  mount. The pipe is mounted into `/record`, a directory that belongs to the container's root,
  so the shell cannot remove it. The prototype measured this: `Permission denied`. jichi's
  stdout goes to podman, which hands it to the host shell, so the stream stays out of reach in
  the same way.
- **It can be added to.** The shell can open the same pipe and write lines into it. In the
  prototype, one injected line arrived in each channel. So the record is *append-only*, not
  unforgeable. Read it as what jichi wrote plus whatever the run appended. Tell the two apart
  with a second witness outside the container, such as the model server's own log.

## 5. The network: pasta, and one door

### 5.1 Why a container needs pasta

A new network namespace starts empty: no interfaces except its own loopback, and no route out.
A container run as root gets a virtual bridge and firewall rules. A rootless one cannot create
either, so an ordinary program connects it instead. In podman 5 that program is **pasta** ("Pack
A Subtle Tap Abstraction", from the passt project). podman-run(1) calls it "the default for
rootless containers".

pasta works like this:

1. It puts a virtual network card, a *tap* device, inside the container.
2. It runs on the host, as you.
3. Every packet the container sends goes to it, and it turns each connection into an ordinary
   socket on the host. When the container opens a connection to a web server, pasta opens a
   normal connection to that server, as you, and relays the bytes.

No privileges and no firewall rules are involved. By default it copies the host's addresses and
routes into the container. Its manual calls this "the illusion that application processes
residing on the guest are running on the local host".

### 5.2 The catch: `127.0.0.1` inside is the container's own

The container's loopback is not the host's. A server that listens only on the host's
`127.0.0.1`, as LM Studio should, does not exist from inside. With plain `--network=pasta`, jichi
could not reach the mock model at all.

Podman does add a name for the host, `host.containers.internal`. pasta maps it to the host's own
address on its network (`--map-guest-addr`; the manual calls it the host's "global address"), not
to its loopback. So it reaches what the host offers its network, and a loopback-only server is not
there. That was measured.

### 5.3 `-T`: forwarding exactly one port

pasta forwards ports in two directions:

| Option | Direction | Used for |
|---|---|---|
| `-t PORT` | host → container | making a server inside visible on the host (what `podman -p` sets up) |
| `-T PORT` | container → host | letting the container reach one port on the host's loopback |

The `-t` row was checked the way §5.4 checks the rest: publishing a port with `-p 18080:80` made
podman pass `-t 18080-18080:80-80` to pasta.

With `--network=pasta:-T,1234`, pasta listens on port 1234 on the container's loopback. It
passes each connection to port 1234 on the host's loopback. The config keeps
`http://127.0.0.1:1234/v1`, and LM Studio stays bound to loopback.

```
   container (its own network namespace)          |   host
                                                  |
   jichi  -> 127.0.0.1:1234  --- pasta -T 1234 ---+-> 127.0.0.1:1234   the model server
   shell  -> 127.0.0.1:5432  --- nothing there    |   127.0.0.1:5432   a database
   shell  -> a web server    --- pasta, as you ---+-> the internet
```

### 5.4 Only that port: check it yourself

pasta has a second way to the host's loopback: by default it maps the container's gateway
address there (`--map-host-loopback`). Podman switches that off. You can see the exact arguments
podman passes:

```sh
podman --log-level=debug run --rm --network=pasta jichi-sandbox true 2>&1 |
    grep -o 'pasta arguments: .*'
```

On the machine that measured this, they were
`--config-net --dns-forward 169.254.1.1 -t none -u none -T none -U none --no-map-gw --quiet
--netns … --map-guest-addr 169.254.1.2`:

- `-t none -u none` and `-T none -U none`: no ports forwarded in either direction until you ask.
- `--no-map-gw`: the gateway does not lead to the host's loopback.
- `--map-guest-addr 169.254.1.2`: this is `host.containers.internal`, the host's network address.

A probe against a second service, listening only on the host's `127.0.0.1`, tried every route in:

| Route from inside | `pasta:-T,<model port>` | `host` |
|---|---|---|
| `127.0.0.1:<its port>` | no connection | reached |
| the default gateway's address | no connection | no connection |
| `host.containers.internal` | no connection | no connection |

"No connection" is a claim only if the probe could have seen a connection. So one more run opened
a route on purpose, with `--network=pasta:--map-host-loopback,169.254.1.3`. The same probe,
pointed at `169.254.1.3`, reached the service. To repeat that yourself, with a second mock as the
service (run it before step 7, in the same directory):

```sh
mkdir capture-other
printf 'wire openai\nrule\n  text other\n' > other.script
"$J/tests/tools/mockmodel" --script other.script --capture capture-other \
    --port-file other.port --deadline 300 --max-requests 20 > other.log 2>&1 &
OTHER_PID=$!
until [ -s other.port ]; do sleep 0.2; done
OTHER=$(cat other.port)
for net in "pasta:-T,$PORT" host "pasta:--map-host-loopback,169.254.1.3"; do
    case "$net" in *map-host*) addr=169.254.1.3 ;; *) addr=127.0.0.1 ;; esac
    podman run --rm --network="$net" jichi-sandbox \
        curl -s -m 3 -o /dev/null -w "$net: $addr:$OTHER -> HTTP %{http_code}\n" \
        "http://$addr:$OTHER/v1/models"
done
kill $OTHER_PID
```

Expect `HTTP 000`, meaning no connection, for the first, and `HTTP 200` for the other two.

### 5.5 What pasta does not stop

- **Outbound traffic.** Giving the container a network is pasta's job. The run can reach the
  internet from your address: an HTTPS request from inside a `pasta:-T` container to
  `example.org` got `HTTP 200`. By the same mechanism it can presumably reach your LAN and
  whatever the host offers on its network address, though that was not measured. Nothing here
  restricts any of it.
- **The model server's other endpoints.** The forwarded port carries the server's whole API, not
  only chat. A run can send it requests of its own, and a server that loads models on demand will
  load whichever model it is asked for.

`--network=host` is the opposite choice. The container shares the host's network completely,
reaches every service on the host's loopback (measured: HTTP 200 above), and can open ports on
the host.

## 6. What the full prototype measured

The design's prototype ran four cases against the same kind of mock. It is kept with the
project's internal notes, and the plan summarises it. §3 reproduces the first case. The others,
in brief:

| The model's shell | What happened |
|---|---|
| deletes a host directory by its host path | nothing: the path does not exist inside |
| runs `rm -rf /*` | emptied the workspace (`.git` included) and the run's HOME, and nothing else: the image's programs, the read-only mounts and the record survived (§2.4, §2.5) |
| attacks the record | no truncation, and the pipes survived; one injected line per channel arrived. Since M770, the line through jichi's stdout is refused (§8), and the two pipes still take one each |

**Do not try the `rm -rf /*` case on your own machine.** It is safe only if every mount is what
you think it is. The command that deleted the drive did exactly what it said, in a place it should
not have been, and a typo in one `-v` option is the same mistake. If you want to see it, use a
throwaway virtual machine.

## 7. Five mistakes made while measuring and writing this, and what caught them

Each one produced a confident result that was wrong. They are here because they are the part of
this page most worth learning.

1. **A check assumed the first event.** The tamper check asked whether the stream still began
   with `message_start`. With `--journal` on, jichi's first event is `run_start`, so the check
   reported a truncation that had never happened.
   - *Lesson:* a failing check must print what it saw, and should ask for what must be there,
     not guess at a shape.
   - *Proof of the fix:* the check now looks for jichi's first line and for the tool call before
     the attack, and a record cut at that tool call turns it red.
2. **A probe of a service that never started.** The mock refuses to start without `--capture`,
   and the script neither passed it nor checked. The probe reported "unreachable", which was true
   because nothing was listening. The host-network run, where the service *must* be reachable,
   said "unreachable" too, and that exposed it.
   - *Lesson:* every "cannot" needs a control run that must say "can".
3. **A number read in the wrong base.** `/proc/net/route` stores addresses as little-endian hex.
   POSIX awk does not parse hex strings and silently produced 0, so the "gateway" was `0.0.0.0`
   and that probe tested nothing. Printing the raw table showed the real address.
   - *Lesson:* print what you extracted before you trust what you computed from it.
4. **One route of three.** The claim that nothing on the host's loopback but the forwarded port
   was reachable had been tested through `127.0.0.1` only. pasta's manual described a second
   route, the gateway mapping, and `host.containers.internal` is a third. Measuring all three, plus
   a control that opened one on purpose, confirmed the claim. Only then was it a measurement.
   - *Lesson:* enumerate the ways in a second time, by a different route. Here that route was the
     manual. [TESTING_TUTORIAL.md](TESTING_TUTORIAL.md) §6 teaches this as "audit the universe".
5. **A claim written from memory.** The first draft of §2.4 said that rootless podman makes "root
   inside the container a nobody outside". Read back as a learner would read it, the sentence
   looked too simple. Measuring the uid map showed it was true only with `keep-id`: by default,
   root inside *is* you. The design's plan, written an hour earlier, said the same thing, and was
   corrected along with this page. The measurement then found something neither text had said:
   root inside a `keep-id` container can still write your files unless its capabilities are
   dropped.
   - *Lesson:* a sentence about how a system behaves is a claim like any other. If you have not
     measured it, you have only remembered it, and the cheapest time to measure is before the page
     is published.

## 8. What a container does not contain

- **The run's own workspace and HOME.** This is by design, and it is why the workspace must be a
  clone you can lose.
- **Protection for what you do with that workspace *after* the run.** The workspace is mounted
  read-write, `.git` included, so a run can leave a hook (`pre-commit`, `post-checkout`) or a config
  entry (`core.fsmonitor`, `core.sshCommand`, `diff.external`) in `.git/`. `git help git` §SECURITY:
  *"it is not safe to run Git commands in a … working tree … when that .git directory itself comes
  from an untrusted source"* — and a rootless `--userns=keep-id` container writes files **you** own,
  so `safe.directory` does not help. It needs no malice: a model that adds a formatter `pre-commit`
  hook, or follows an instruction in a file it read, is enough. So read what the run did **through
  the container** — `podman run … --userns=keep-id -v "$WS:/work:ro" "$IMAGE" git -C /work diff` —
  or by fetching the branch into a bare mirror the host owns, never with `git diff`/`git log`/`git
  status` on the host in that workspace. The same holds for jichi's shadow repo and `jichi attempts`
  run against a HOME the container wrote.
- **Lines added to the record** (§4).
- **Outbound network traffic, and the model server's whole API** (§5.5).
- **The kernel,** which the container shares with the host (§2.2).
- **Time.** A container does not stop a run that never ends; a timeout outside it does.
- **Anything you mount, including a key file.** The model's shell can read every mount the
  container has, so never mount the file you keep an API key in. It is safest to give a contained
  run **no key at all** — a local server such as LM Studio needs none — but when a run must reach a
  keyed gateway, **hand the key to jichi on its standard input**, not with `podman run -e NAME`.
  The reason is the next bullet: `-e` puts the value in a *process's* environment, and a same-user
  process in the container can read another process's environment. jichi closes that route for its
  **own** process (below), but it cannot close it for whatever process holds an `-e` value —
  and when the container runs an init as PID 1 (`--init`, which reaps zombies), that process is
  the **init**, not jichi. So instead run jichi through a one-line wrapper that reads the key from
  stdin into jichi's environment and `exec`s jichi, so the only process that ever holds the key is
  jichi itself:

  ```sh
  # key-wrap.sh -- the container's entrypoint
  #!/bin/sh
  IFS= read -r JICHI_API_KEY || exit 1
  export JICHI_API_KEY
  exec jichi "$@"
  ```
  ```sh
  printf '%s\n' "$JICHI_API_KEY" | podman run -i --rm ... key-wrap.sh -p "..."
  ```
  The wrapper `exec`s (does not fork), so it *becomes* jichi at the same PID; nothing long-lived
  holds the key but jichi, whose own environment is hidden below. `jichi doctor` warns —
  *the API key is exposed in PID 1's environment* — when PID 1 holds a configured key variable,
  the exact misconfiguration this bullet prevents (it reads the variable's NAME only, never its
  value).
- **Until M770, the key in jichi's own environment.** jichi removes its key variables from every
  command it runs, but the command runs as the same user as jichi, inside the container too. A
  same-user process may read the environment of any *dumpable* process, and jichi was one, so the
  model's shell could read the key out of jichi's environment and write into its output. jichi now
  makes itself non-dumpable when it starts. In this page's container that route is refused
  (`Permission denied`) **when jichi is PID 1** — which it is only when the container runs *without*
  `--init`; with an init as PID 1, jichi is a child (still protected), but an `-e` key would sit in
  the init's readable environment, which is why the bullet above hands the key on stdin instead.
  The protection holds because the container runs with `--cap-drop=all` (§3), so nothing in it holds
  `CAP_SYS_PTRACE`, the one capability that passes the kernel's check even against a non-dumpable
  process. Never give it back to the container. [HARDENING.md](HARDENING.md) §2 has the details.
  `jichi doctor` shows *keys are hidden from other same-user processes* when the protection is in
  force.

## 9. What may differ on your machine

- **An older podman.** Before pasta, rootless podman used slirp4netns, which has other options
  (for example `allow_host_loopback`), so §5's commands will not apply.
- **SELinux** (Fedora, RHEL and others). podman-run(1) says that bind-mounted content needs a
  label, `:z` or `:Z` after the mode, or processes inside may be denied access. Not measured here.
- **cgroups v1, or no delegation.** podman-run(1) says the resource options are "not supported on
  cgroups V1 rootless systems". Leave out `--memory` and `--pids-limit` there, and know that the
  run is then unbounded.
- **The binary.** Step 5 mounts the host's jichi into an Ubuntu 26.04 image. That works when the
  image's C library and libcurl match the ones jichi was built against, and on the machine that
  measured this, both were Ubuntu 26.04. On another distribution, build jichi inside the image
  instead. That is not shown here, and it was not measured.
- **macOS and Windows.** podman runs containers inside a Linux virtual machine there, so the
  network paths differ. This page does not cover them.

## 10. Terms

| Term | Meaning here |
|---|---|
| namespace | the kernel giving one process its own view of one resource (§2.2) |
| container | a process in new namespaces, with an image as its root filesystem |
| image | a filesystem tree a container starts from; built from a `Containerfile` |
| rootless | run by an ordinary user; the container's ids map to ids the user owns |
| keep-id | the user namespace mapping that keeps your uid the same inside |
| bind mount | a host directory or file made visible inside, `rw` or `ro` |
| cgroup | the kernel's accounting and limits for a group of processes |
| capability | one piece of root's power; `--cap-drop=all` removes them all |
| FIFO, named pipe | a pipe with a name in the filesystem; no contents on disk |
| pasta | the program that gives a rootless container its network |
| tap device | a virtual network card whose packets go to a program, not to a wire |
| loopback | `127.0.0.1`, the machine talking to itself; each network namespace has its own |

## 11. References

In this repository:

- [ANECDOTES.md #108](ANECDOTES.md): the drive whose record was deleted.
- [AUTONOMY.md](AUTONOMY.md): the two ways jichi changes a file.
- [DEPLOYMENT.md §5](DEPLOYMENT.md): hardening an unattended host.
- [plans/2026-09-25-contained-drive.md](plans/2026-09-25-contained-drive.md): the design this page
  teaches, and the decisions it leaves open.
- [SECURITY.md](../SECURITY.md): why a finding waits for its fix before its write-up.
- [TESTING_TUTORIAL.md](TESTING_TUTORIAL.md) §6: auditing the universe, not the result.

Manuals. On the machine that measured this, all seven were also installed, so `man podman-run`,
`man pasta` or `man 7 fifo` shows the same text offline. Check yours with `man -w 7 fifo`:

- podman-run(1): <https://docs.podman.io/en/latest/markdown/podman-run.1.html>
- pasta(1), which shares its manual with passt: <https://passt.top/builds/latest/web/passt.1.html>
- namespaces(7): <https://man7.org/linux/man-pages/man7/namespaces.7.html>
- user_namespaces(7): <https://man7.org/linux/man-pages/man7/user_namespaces.7.html>
- cgroups(7): <https://man7.org/linux/man-pages/man7/cgroups.7.html>
- capabilities(7): <https://man7.org/linux/man-pages/man7/capabilities.7.html>
- fifo(7): <https://man7.org/linux/man-pages/man7/fifo.7.html>
