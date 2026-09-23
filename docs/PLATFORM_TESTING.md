# Does this work on my machine? — testing jichi on a system, step by step

*For a self-learner with one laptop, and for a junior developer handed "check
whether this runs on the new box" with no further explanation.*

> **This is not [`TESTING_TUTORIAL.md`](TESTING_TUTORIAL.md).** That page teaches
> you to *write* a test — the loop, the traps, proving the teeth. This one
> teaches a different activity: taking a program you did not write and finding
> out, honestly, whether it works on a machine. You need the other page to build
> a check. You need this one to answer a question about a computer.

---

## 0. What you are actually being asked

"Does it work here?" is three questions wearing one coat, and answering the wrong
one is the commonest mistake on this page:

1. **Does it build here?** — a toolchain question.
2. **Do its own tests pass here?** — a correctness question.
3. **Does it *do its job* here?** — for jichi, that means calling a model and
   running a tool. Every gate in this project is **offline**; all of them pass on
   a machine where jichi has never reached the network.

Most reports answer 1 and 2 and imply 3. Say which you answered.

---

## 1. The ladder

Six rungs, cheapest first. **Each rung is a complete answer** — stop after rung 2
and you have learned something true, not half a method. Nothing below assumes a
second machine until rung 2, or money at any point.

| Rung | You do | Costs | It proves |
|---|---|---|---|
| **0** | the gate on the machine you have | minutes | the gate runs here |
| **1** | the same under a different libc | minutes | what you assumed about libc |
| **2** | a whole VM you provision | an hour, once | a container is not a machine |
| **3** | a foreign kernel | an evening | portable ≠ Linux |
| **4** | a board you own | an evening + hardware | a VM is not hardware |
| **5** | **drive a model** on any of the above | an hour | the only rung that tests the point |

### Rung 0 — the machine you have

```sh
make check-target          # = make test + make smoke
```

That is the whole rung. It builds, runs the unit suite, and runs the smoke tier
against the binary it just built.

**If it fails:** read the *first* failure, not the last. The tier is fail-fast by
default, so a later driver's silence means "never ran", not "passed".

**If it is slow or times out**, the tier's deadlines assume a quiet machine:

```sh
JC_SMOKE_TIMEOUT_MULT=3 make check-target
```

The multiplier is a **ratio** — this machine's `make WERROR=1` seconds divided by
the reference bench's. Copy the formula, never somebody else's number.

**If you want every failure rather than the first:**

```sh
JC_SMOKE_KEEP_GOING=1 make smoke
```

On an unfamiliar machine this is usually what you want. Each round trip to a
strange box is expensive; collect the whole failure set in one.

### Rung 1 — a different libc, without leaving your desk

Your machine has one libc, and most "portable" code has only ever met it. The
cheapest second opinion is a musl container:

```sh
podman run --rm -v "$PWD":/src -w /src alpine:latest \
    sh -c 'apk add build-base curl-dev pkgconf >/dev/null && make check-target'
```

(`docker run` takes the same arguments if that is what you have; podman is
suggested only because it needs no daemon and no root.)

**What this rung is and is not:** it changes the C library and nothing else.
Same kernel, same CPU, same filesystem. A pass here does not mean "portable"; it
means "not glibc-specific", which is a smaller and still useful claim.

### Rung 2 — a whole virtual machine

A container shares your kernel. A VM does not, and the difference shows up in
memory ceilings, boot behaviour and `/proc`.

```sh
scripts/tier-v-vm.sh v2e --ref-secs <this machine's build seconds>
```

**Get `--ref-secs` right**, because everything downstream scales from it:

```sh
make clean && time make WERROR=1
```

**Two failure modes to tell apart**, and the rig now helps:

- **`INFRASTRUCTURE:`** — qemu did not start. Usually a port already in use.
  Nothing about the program was measured.
- **`FINDING:`** — the guest itself could not get to userspace at that memory
  ceiling. That is a real result about the image.

That distinction exists because this project once recorded the second when the
truth was the first, and the false finding went into a results file that the
platform page quotes. **When a run reports something surprising, ask what it
would look like if your instrument were broken.**

### Rung 3 — a foreign kernel

```sh
scripts/tier-v-bsd.sh --ref-secs <yours>          # FreeBSD, one command
```

Expect to spend an evening the first time, most of it on the download. Expect
also to find bugs in *your own test tooling* rather than in the program: on the
BSDs, this project's failures were overwhelmingly GNU-only assumptions in the
test tier — `grep -r --include=`, a BRE `\(…\)\?`, `\|` alternation — not defects
in jichi.

**Use `gmake`, not `make`.** The BSDs' `make` is a different program and the
error you get is a parse error in the middle of a makefile rather than a clear
"wrong make".

### Rung 4 — a board you own

```sh
scripts/tier-b-device.sh user@board --ref-secs <yours> --label pi400
```

A board is not a VM: it has real thermal limits, a real SD card, and real
memory pressure. It is also where the multiplier stops being theoretical — a
Raspberry Pi Zero runs at multiplier 12 against this bench.

### Rung 5 — drive a model, which is the only rung that tests the point

Everything above is **offline**. jichi's job is to call a model and run tools;
none of the previous rungs touches that.

```sh
scripts/tier-b-device.sh user@board --ref-secs <yours> \
    --live-port 1234 --live-model <a model id>
```

**Two turns, and the second is the one that matters.** The first (`reply with
OK`) proves the wire: provider, request, SSE framing. It chooses no tool and
executes nothing. The second asks the model to read a file with a tool and report
a phrase **generated that run** — a token it can only have obtained by making the
call. Every documented failure in this area lives past the first turn: a model
that *describes* a tool call instead of making one exits cleanly with an empty
workspace.

**Keep the model server on loopback** and reach the target with a reverse
forward; there is no reason to expose it on the network.

---

## 2. The three lessons, which are the actual content

Commands you can copy. Judgement you have to practise.

### 2.1 Tell an infrastructure failure from a result

They look identical in a log, and the infrastructure one is *more* plausible
because it fails in the shape of a result.

A real case from this project: a rig reported

> `FINDING: the kernel never STARTED at -m 1024 … a property of the IMAGE, not of jichi.`

One line above it, unread:

> `Could not set up host forwarding rule 'tcp:127.0.0.1:2222-:22'`

qemu never started; the port was already in use. The rig waited 120 s for a
kernel banner from a process that did not exist, and blamed the operating system.

**The habit:** before recording any verdict, ask *what would this look like if my
instrument were broken?* If the answer is "exactly like this", go and check the
instrument first.

### 2.2 A green result is not evidence until you have seen it go red

A check that has never failed has never been shown to work. This applies to your
platform testing too, not just to the project's own lints:

- Ran the gate and it passed? **Break something on purpose** — rename a source
  file, corrupt a fixture — and confirm the gate notices.
- Got a model turn to succeed? **Point it at a dead port** and confirm you get a
  connection error rather than a pass.

The second one matters more than it sounds. A live-turn check that passes when
the model server is switched off is not testing what its name says.

### 2.3 Say what you did not test

This is the difference between a useful row and a misleading one, and it costs a
sentence:

- Rung 3 tested a kernel. It did **not** test hardware.
- Rung 2 tested a machine you provisioned. It did **not** test the one in
  production.
- A green offline gate says nothing about whether a model call works.
- An emulated architecture tests the instruction set, **not** the machine — no
  cache-coherency bug, no timing bug, nothing about real silicon.

A row that overstates its scope will be quoted later by somebody who was not
there. That is the whole risk, and it is why every row in
[`PLATFORMS.md`](PLATFORMS.md) carries a scope sentence.

---

## 3. Writing the row

The artifact of this work is not a green tick. It is a **row somebody else can
read and reproduce**:

```
machine   : <what it is — CPU, RAM, OS, kernel, libc, compiler>
command   : <exactly what you typed, including the flags>
date      : <when>
result    : <the numbers the run printed, copied not paraphrased>
scope     : <what this did NOT test>
```

**Copy the numbers, do not summarise them.** "the tests passed" is not a result;
`13,362 checks / 0 failures` and `smoke: OK (311 drivers, 1,806 checks)` are.

---

## 4. Your log

One line per rung. This is yours, and it is the only record that you did the
work rather than read about it.

| Date | Rung | Machine | Result | What I did not test |
|---|---|---|---|---|
| | | | | |

---

## 5. When it goes wrong

| Symptom | What it usually is |
|---|---|
| The tier times out on a slow box | `JC_SMOKE_TIMEOUT_MULT`, a ratio you compute yourself |
| One driver fails, the rest are silent | fail-fast; use `JC_SMOKE_KEEP_GOING=1` |
| A rig says a VM never booted | check for `INFRASTRUCTURE:` first — usually a port in use |
| `make` dies parsing the makefile on a BSD | that is `bmake`; use `gmake` |
| Every capability probe answers "no" | no `cc` on the box; pass `CC=gcc` |
| A live turn answers but no tool runs | the model emits tool calls as prose; check `doctor --live` calls it **native** |
| A live turn passes with the server off | your check is not testing what it says |

---

## 6. Where to go next

- [`PLATFORMS.md`](PLATFORMS.md) — every row this project has measured, and what
  each one taught. Read a row before you write one.
- [`VERIFY_A_PLATFORM.md`](VERIFY_A_PLATFORM.md) — when your machine is one this
  project does not have: the exact commands and what to send, so your result
  becomes a row.
- [`BUILD.md`](BUILD.md) — the per-platform build reference.
- [`TESTING_TUTORIAL.md`](TESTING_TUTORIAL.md) — how to *write* a check, once you
  want the answer to stay answered.
- [`LOW_MEMORY.md`](LOW_MEMORY.md) — if your machine is small, this is the page
  with the ceilings.
