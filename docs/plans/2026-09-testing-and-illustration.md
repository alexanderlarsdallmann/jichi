# Four programmes: platform testing for learners, honest illustration, and self-testing

*Design, 2026-09-20 (M680). Four things were asked for together, and they are
related: three of them are about making **platform testing** something a
self-learner can do, and the fourth is about deciding what to do next. Nothing
here is built yet except where it says **DONE**; this page is the design and the
argument, so that building it is a matter of following it rather than
re-deciding it.*

---

## 0. The reader these are for

The same one the rest of this tree is written for: **a self-learner with a
laptop, alone**, and a junior developer who has been handed a task nobody has
time to explain. Two consequences that shape every design below:

- **No step may assume a second machine, a budget, or a colleague.** Where one
  is genuinely required, the page says so *before* the reader starts, not in the
  middle.
- **Every instruction is one someone ran.** This project's own rule: a page that
  tells you to type something nobody typed is a page that will waste an evening.

---

## 1. Testing jichi on the various systems — a tutorial, not a reference

**The gap.** [`PLATFORMS.md`](../PLATFORMS.md) is a *verdict page*: twenty-odd
rows, each an answer. [`BUILD.md`](../BUILD.md) is a *per-platform reference*.
Neither teaches the **activity**. A learner who wants to answer "does this work
on my machine?" has no page that starts where they are.

And the activity is genuinely teachable, because this project has done it about
twenty times and written down what went wrong each time.

### 1.1 What the tutorial teaches, in order

A ladder, cheapest rung first, and **each rung is a complete answer** — a reader
who stops after rung 2 has learned something true rather than half a method.

| Rung | What the reader does | What it costs | What it proves |
|---|---|---|---|
| **0** | `make check-target` on the machine they already have | minutes | the gate runs here |
| **1** | the same under a **different libc** (Alpine/musl in a container) | minutes | their assumptions about libc |
| **2** | a **whole VM** they provision themselves (`tier-v-vm.sh`) | an hour, once | the difference between a container and a machine |
| **3** | a **foreign kernel** (FreeBSD via `tier-v-bsd.sh`) | an evening | that portability is not a synonym for Linux |
| **4** | a **board** they own (`tier-b-device.sh` on a Pi) | an evening + hardware | that a real machine is not a VM |
| **5** | **driving a model** on any of the above | an hour | the only rung that tests the point of the program |

### 1.2 The three lessons that are the actual content

A tutorial that only listed commands would be a worse `BUILD.md`. The content is
the **judgement**, and this project has paid for all three:

1. **Tell an infrastructure failure from a result.** The worked example is
   M679's, verbatim: a rig reported *"the kernel never STARTED … a property of
   the IMAGE"* when the truth was a port collision. The reader learns to ask
   *what would this look like if my instrument were broken?* before recording a
   verdict.
2. **A green result is not evidence until you know it can go red.** Perturb the
   thing you just proved and watch the check fail. Every lint in this tree was
   toothed this way, and several reported green against the exact defect they
   were written for until somebody perturbed them.
3. **Say what you did NOT test.** Rung 3 does not test hardware. Rung 2 does not
   test a kernel you did not choose. A row that overstates its scope is worse
   than a missing row, because it will be quoted later by someone who was not
   there.

### 1.3 Shape

`docs/PLATFORM_TESTING.md` — **not** `TESTING_TUTORIAL.md`, which already exists
and teaches a different activity (*writing* a check: the loop, the traps, proving
the teeth). This design originally proposed that filename and would have
collided with it; the new page opens by saying which is which. Written like
[`LANGUAGE_COURSE.md`](../LANGUAGE_COURSE.md):
numbered steps, every command in the form it was run, an escape route per step
(*"if this fails, here is what it means and what to do"*), and a **log table**
at the end — one line per rung, what the reader measured on *their* machine.

**Graded tasks** follow the curriculum's existing shape: a `platform` stage,
where the grader checks the structural floor a script honestly can (a results
file exists, names a machine, carries a verdict and a *scope* sentence) and says
plainly that whether the testing was *good* is a reviewer's judgement.

---

## 2. Screenshots and generated images — and the line between them

**This item needs a constraint stated before anything is built, because getting
it wrong would damage the documentation rather than improve it.**

### 2.1 A generated screenshot is a fabricated record

A screenshot is a **record of something that happened**. An image model does not
record; it invents. An image that *looks like* jichi's terminal output but shows
text the program never produced is a **fabricated artifact** — and putting one in
documentation is the same class of error as an invented citation or a hand-typed
test result. This project refuses those elsewhere and must refuse this.

Two practical reinforcements, both measured rather than assumed:

- The available model here is **`sd-1.5-ggml`** (LocalAI, `127.0.0.1:8090`).
  Stable Diffusion 1.5 renders *text* as plausible-looking nonsense. A generated
  "terminal" would contain garbled pseudo-words — recognisably wrong to a careful
  reader and, worse, **not** recognisably wrong to a hurried one.
- jichi's own `generate_image` tool writes into the path fence like any other
  tool, so nothing stops a generated file being committed next to real ones. The
  discipline has to be in the *rule*, not the fence.

**So: the rule is that an image asserting "this is what the program printed"
must be captured, and an image asserting nothing may be generated.**

### 2.2 The honest split

| Kind | How it is made | What it may claim |
|---|---|---|
| **Terminal capture** | run the real command, capture the real bytes | *"this is what it printed"* |
| **Annotated capture** | a capture + arrows/labels added deterministically | the same, plus emphasis |
| **Diagram** | mermaid, already used throughout this tree | a structure, not a run |
| **Illustration** | **generated** — a header image, a conceptual figure | nothing factual |

Everything in rows 1–2 is a **capture**. Only row 4 is generated, and a
generated image in this tree carries a visible marker saying so, the same way a
bibliography entry carries its verification marker.

### 2.3 How captures get made, with what already exists

No new machinery is needed for the honest kind:

- `tests/tools/ptydrive` already drives jichi in a **real pty** and can capture
  exactly what a terminal would receive, including colour.
- `scripts/tier-v-terminals.sh` already drives jichi inside **real X11 terminal
  emulators** — the one instrument that can photograph what an emulator actually
  renders.
- A capture is reproducible: the command, its config, and the captured bytes go
  beside the image, so a reader can re-run it and get the same picture. **An
  image nobody can reproduce is decoration.**

### 2.4 The setup, and the tutorial

`docs/ILLUSTRATION.md` — the setup and the rule together, because a setup
without the rule is an invitation:

1. **LocalAI**, loopback only, on `127.0.0.1:8090`, with `--models-path` and
   `--backends-path` under a fixed prefix. *(Recorded the hard way in
   [`ANECDOTES.md`](../ANECDOTES.md) §13: run from the repo, it left 7.3 GB of
   backends inside the project tree.)*
2. **A model declaring `role: image`** in the config; jichi registers
   `generate_image` only when one exists.
3. **`toolProfile: full`** — the lean `core` profile does not advertise the media
   tools, and a model that "cannot generate images" is usually a model that was
   never offered the tool. *(ANECDOTES §13 again; it cost a debugging session.)*
4. **Poll the backend registry, not the job list** — a gallery backend registers
   a beat after its download reports 100%, and the first request 500s.

Then: how to capture, how to annotate, where to put the marker, and **what not
to generate**, with the reasoning rather than a prohibition.

### 2.5 The test file

`docs/INTERFACE_TUTORIAL.md` is the right first subject and not an arbitrary
one: it is a page *about interfaces* that currently shows none. It gets

- **two real captures** — `jichi --help` and a `doctor` run, the surfaces §2
  argues about, photographed rather than described;
- **one generated header illustration**, marked as generated, asserting nothing;
- a short *"how these were made"* footer, so the page demonstrates its own rule.

---

## 3. jichi tests its own platform, guided

**The goal, in the learner's words:** *"I have a machine. I want to know whether
jichi works on it, and I want to be walked through it."*

### 3.1 Why this is not just a script

A script that ran everything would answer the question and teach nothing — and
it would fail the moment the machine differed from the author's. The design is a
**scaffold plus a coached loop**, exactly the shape M675 used for the language
course, because the same argument applies: the learner must understand what was
measured or they cannot defend the result.

### 3.2 `jichi init platform-test`

A 33rd scaffold pack, writing:

- **`PLATFORM_TEST.md`** — the route: identify the machine → run the offline
  gate → record → (optional) drive a model → write the row. With a **results
  table** the learner fills in, because the artifact of this work is a row.
- **`.jichi/skills/platform-tester/SKILL.md`** — the coach. Its stance is the
  one this tree keeps arriving at: **it does not run the gate for them.** It
  asks what they expect, has them run it, and asks what happened. On a failure
  it walks the *diagnosis*, not the fix — "is this your machine, your build, or
  your instrument?" is the question being taught.
- **`.jichi/commands/platform-test.md`** — `/platform-test` to start or resume.
- **`AGENTS.md`** — the rules of this directory: record what you ran, never a
  verdict you did not measure, and say what you did not test.

It writes **no config**, for the reason `DECISIONS.md` already records for the
course pack: the model and its endpoint are guesses, and a scaffolded config
full of placeholders reads as a setup that *failed* rather than one not yet done.

### 3.3 What the model may and may not do

The fence matters more here than usual, because the subject is *evidence*:

- **May**: read results files, explain a failure, suggest what to run next,
  draft the prose of a row.
- **Must not**: write a verdict the learner has not measured, or fill in a
  number. The skill says so, and the grader checks the structural half — a row
  must name a machine, a command, a date and a scope sentence.

### 3.4 The honest limit, stated in the pack

jichi can guide the platform test and can run the *offline* gate. It cannot
provision a VM for the learner, cannot buy them a board, and **cannot verify the
result is true** — only that it is recorded in a checkable shape. The pack says
this on its first page.

---

## 4. Priorities across everything open

Ordered by **value per hour**, with what stands in the way named. The full
register is [`DEFERRED.md`](../DEFERRED.md); this is the reading of it.

### Now

1. **The three programmes above (§1–§3).** They are the largest block of
   user-facing value currently at zero, and they share a subject, so writing
   them together costs less than writing them apart. §2's rule should land
   first — it constrains what §1 and §3 may illustrate.
2. **The eight drivers still failing on illumos.** Two of ten closed at M680 and
   the method is now proven: `--keep`, a shell on the guest, three commands. The
   remaining eight are the cheapest real measurements left.
3. **The `grep -o` sweep.** Measured, floor of 27 drivers, and the largest single
   lever on the illumos row. Mechanical once started.

### Next

4. **A rig for Cygwin and MSYS2** *(postponed to next week at the operator's
   request)*. They are the only rows measured purely by hand, so neither is
   reproducible, and MSYS2 is the only row with a documented **safety**
   difference — `chmod` is a no-op there under the default mount.
5. **Record trackedness in the run journal.** What M662's own measurement asked
   for: `--strict-green` cannot discriminate without it, and 85% of what it
   flags is the work's own output.

### Later, with the trigger named

6. **The qemu-user architecture rows.** ~20 triples, each needing a cross-built
   libcurl. The simplification worth recording: the driven task talks plaintext
   HTTP over a loopback tunnel, so a libcurl with **no TLS backend at all**
   removes the hardest part. Trigger: somebody wanting those rows driven enough
   to spend a day on the toolchain.
7. **Notebook support.** The 257× token measurement is compelling and the use
   case is still undecided. Trigger: somebody saying notebooks *are* the
   workflow.
8. **macOS.** Blocked on hardware, and no amount of willingness moves it.

### Not recommended

- A **blind sweep of the remaining bare-`make` sites** — how a portability fix
  becomes a portability bug.
- The **frontier craft A/B** — superseded by the local-models rule.

---

## 5. What is already done, so nobody rebuilds it

- **§1 is built**: [`PLATFORM_TESTING.md`](../PLATFORM_TESTING.md). **DONE (M682).**
- **§2 is built**: [`ILLUSTRATION.md`](../ILLUSTRATION.md), the capture recipe,
  and one real screenshot. **DONE (M680).**
- **§3 is built**: `jichi init platform-test`, the 33rd pack. **DONE (M682).**

- **The tiny row is driven** (M680): the agent loop closes on a **96 MB** machine
  with no distro, agentic phrase `TINY-3232A6`. **DONE.**
- **The driven task has one definition** (`scripts/_rig_live.sh`, M676) and seven
  rigs share it. **DONE.**
- **The language course** is complete — tutorial, corpus fetcher, citation lint,
  scaffold, coach, and one graded track. **DONE.**
