# Helping verify a platform — sending jichi a report it can record

*For a self-learner or a junior developer who has a machine this project does not
— a Mac, a BSD, an unusual Linux, a small board — and wants to find out, and tell
us, whether jichi really works there. Written at M716, when two macOS users said
they had compiled jichi and the honest answer to "is that verified now?" was
"not yet — and here is exactly what would make it so".*

> **Three pages, three jobs.** [`PLATFORM_TESTING.md`](PLATFORM_TESTING.md)
> teaches the *judgement* — how to tell an infrastructure failure from a result,
> why a green check means nothing until you have seen it go red, how to say what
> you did not test. [`PREPARE_AND_BUILD.md`](PREPARE_AND_BUILD.md) gets a compiler
> and libcurl onto your machine. **This page is the procedure for turning your
> machine's results into a report the project can check and record** — the
> commands, what each one proves, what to send, what never to send, and what
> happens to it afterwards.

---

## 0. Why your machine matters

jichi has one author and one main development machine, which runs Linux. Every
non-Linux platform anyone has brought to it so far has found a **real defect that
was present everywhere** and only became visible elsewhere:

- **macOS, before anyone had compiled it.** jichi's only macOS-specific code — the
  `#if defined(__APPLE__)` branch that asks the kernel how much memory the machine
  has — used `unsigned long long`, which is not C89. Under the project's own
  mandatory flags it could never have compiled, and it sat like that for months,
  through every build, four compilers and four audit passes, because *no machine
  here compiles it*. It was found with `grep` (M400; the story is in
  [`PLATFORMS.md`](PLATFORMS.md#the-finding-that-made-this-page-m400)). Fixed
  then, it has **still never been compiled where a log reached the project** —
  so a Mac build is the first real test of it.
- **FreeBSD** found the mirror image: code with no guard at all, which looked
  portable because four C libraries agreed about it — and all four were Linux's.
- **illumos** found four places that tested `uname()` for `== 0`. POSIX only says
  it returns a *non-negative* number on success; illumos returns a positive one,
  so a working call read as a failure.

A platform guard for a platform nobody builds is untested code that *looks*
tested, because it sits in a file that compiles cleanly. The only instrument
that reaches inside it is a machine like yours.

**Where macOS stands today:** the matrix says **Never compiled**
([`PLATFORMS.md`](PLATFORMS.md), the macOS row). Two people have said they
compiled jichi on a Mac, and that is welcome news — but no build log, test count
or commit has reached the project yet, so there is nothing the matrix can
record. A message saying "it compiled" is where this starts; the rest of this
page is what turns it into a row.

---

## 1. What a report can prove — the four words

[`PLATFORMS.md`](PLATFORMS.md) uses four words strictly, and your report is
measured against them. Nothing is ever called verified because it "should work".

| The row says | It means | The evidence that earns it |
|---|---|---|
| **Never compiled** | No compiler on that platform has ever seen this tree. Not "probably fine" — unmeasured. | — (where macOS is now) |
| **Partly verified** | Compiled there, and *some* gate ran green, with the missing part named. | A clean build **and** the unit suite's result |
| **Verified** | Compiled there **and** its test gates ran, by a person, with the numbers written down. | The above **and** the whole smoke tier |
| **Driven** | A **live task with a real model** ran there: a request over the wire, a tool the model chose, the tool executed, and a second turn that used the result. | The two live turns of step 7, with the pass phrase |

**Driven is a separate axis, and it is the one that matters most.** The build,
the unit suite and the smoke tier are all *offline*: they can be green on a
machine where jichi has never once called a model. A row that is Verified but not
Driven says "it compiles and its tests pass here"; it does not say "it works
here", because the agent loop *is* the product.

**Why "it compiled" alone moves nothing.** A compile without a test run cannot
tell "works" from "builds a binary that crashes on its first request". It also
cannot say *which* source was compiled, with *which* flags, or whether files were
edited on the way. Every one of those gaps is closed by one of the steps below.

---

## 2. What you need

- **The machine**, and an account on it. Nothing here needs root, except
  installing the build prerequisites if they are missing.
- **A C compiler, `make`, `git`, and libcurl's development headers.**
  [`PREPARE_AND_BUILD.md`](PREPARE_AND_BUILD.md) has a section per platform; on a
  Mac that is Xcode's command-line tools plus Homebrew's `curl` (its §3 and §4 —
  macOS ships libcurl but not the headers the build needs). If you already
  compiled jichi, you have all of this — keep the same environment.
- **A git clone, not a downloaded archive.** `jichi --version` prints a `build:`
  line only when it was built from a git checkout — the Makefile stamps the
  commit from `git rev-parse` — and without it nobody can tell which source you
  tested. The smoke tier's publication check reads git as well.
- **Time.** On the development machine (an x86-64 Linux workstation with 32
  hardware threads, 2026-09-23) a clean build took **5 s**, the unit suite
  **11 s** including its own build, and the whole smoke tier **801 s** — about
  13 minutes, measured while other work shared the machine, so read it as an
  upper figure for that box rather than a clean timing. Yours will differ, in
  either direction; a small board is many times slower (step 5 says what to do
  then). The clone,
  built and tested, took **154 MB** of disk there.
- **For the live turns only:** a model server you run yourself — LM Studio or
  Ollama — with a model that can call tools. **This check never needs a paid
  model**, and you should not use one for it. If you point jichi at a paid
  service anyway, the money is yours, and note what `jichi doctor` warns you
  about: a config that declares no pricing reads **$0.00** for every call, so the
  number on screen will not show what the run cost.

---

## 3. Four rules that protect the result

Each of these was learned by breaking it.

1. **Keep your report folder OUTSIDE the jichi checkout.** The smoke tier
   includes a check that fails when the tree holds files git does not know about
   (it guards what would be published). Write your logs inside the checkout and
   the tier goes red because of *your notes*. It was caught while this page's
   commands were being written, because the same check had already failed the
   maintainers' own gate over a file nobody had added to git yet.
2. **Do not edit the source to make it build — or if you must, keep the diff and
   send it.** A change you needed *is* the finding. Step 2 records `git diff` for
   exactly this reason; an empty file there is also information.
3. **Nothing else may talk to your model server while the live turns run.** A
   local server shares its memory between requests. When this page's live turns
   were first tried, the model server was also serving a long-running experiment:
   both of the page's requests failed with *"Context size has been exceeded"* —
   and so did two of the experiment's, at the same seconds, spoiling one of its
   runs. The server is part of the measurement.
4. **Copy numbers; never summarise them.** "The tests passed" is not a result.
   `13610 checks, 0 failures` and `smoke: OK (319 drivers, 1870 checks)` are.
   ([`PLATFORM_TESTING.md`](PLATFORM_TESTING.md) §3 says why.)

And one more that is not about the result but about you: **never put an API key
in anything you send.** Section 5 lists what stays at home.

---

## 4. The procedure

Run everything from inside your jichi clone, in `zsh` (the macOS default) or
`bash`. Each step says what it proves and what success looks like. The lines are
split into steps so you can stop, read and understand each one. Steps 2 to 6 also
paste as one block; step 7 needs your model server running and your model's id
typed in first.

### Step 1 — the source

```sh
git clone https://github.com/alexanderlarsdallmann/jichi.git
cd jichi
```

The public repository is also mirrored on the HRZ GitLab as project
`jichi-public/jichi`; either is fine. If you already have a clone, `git pull`
first and use that.

### Step 2 — who you are: machine, compiler, source

```sh
R=~/jichi-report; mkdir -p "$R"            # OUTSIDE the checkout (rule 1)
sw_vers > "$R/system.txt"; uname -srm >> "$R/system.txt"
cc --version > "$R/cc.txt" 2>&1
git rev-parse HEAD > "$R/commit.txt"; git status --short >> "$R/commit.txt"
git diff > "$R/local-changes.diff"
command -v timeout gtimeout curl-config > "$R/tools.txt" 2>&1
make info > "$R/info.txt" 2>&1
```

**What this proves:** which operating system release (`sw_vers` is macOS's
command for it; on other systems leave it out — `uname -srm` then carries the
kernel and the CPU architecture), which compiler, which commit, whether anything
was edited, and what the build's configure step detected (`make info` prints
every capability probe: the C dialect, `vsnprintf`, libcurl, and so on).
`tools.txt` says whether a `timeout` command exists. The smoke tier looks for
`timeout` by that name only, and without one it falls back to a slower built-in
watchdog, which is fine but worth knowing. (macOS has traditionally shipped no
`timeout`; Homebrew's coreutils installs one as `gtimeout`, which the tier does
not look for.)

**An Apple Silicon Mac and an Intel Mac are two different rows** (`arm64` and
`x86_64` in `uname -m`). Say which you have.

### Step 3 — build under the project's own rules

```sh
make clean > /dev/null 2>&1
make WERROR=1 > "$R/build.log" 2>&1; echo "build exit=$?" >> "$R/build.log"
./jichi --version > "$R/version.txt" 2>&1
```

**What this proves:** that jichi compiles with **zero warnings** under the flags
every one of its source files must pass (`-std=c89 -pedantic -Wall -Wextra`,
with `WERROR=1` turning each warning into an error). A plain `make` can succeed
while printing warnings, and a warning on a platform nobody has compiled on is
precisely the kind of thing this whole exercise exists to find.

**Success looks like** `build exit=0` as the last line of `build.log`, and
`version.txt` ending in a `build:` line naming a commit:

```
jichi 0.10.0
Copyright (c) 2026 Justus-Liebig-Universität Gießen
Author: Alexander-Lars Dallmann
licence: Apache-2.0
build: 8e3972be
```

**If it fails** — any `build exit=` other than `0` — do not work around it — the failing log *is* the report. Send
`build.log` and `info.txt` (step 8). If you then get it to build by changing
something, send `git diff` from after the change as well.

Why `echo "exit=$?"` on its own line rather than a `| tee`: after a pipe, `$?`
is the exit status of the *last* command in it — `tee`, which always succeeds.
That mistake has published a wrong exit code on this project before.

### Step 4 — the unit suite

```sh
make test > "$R/test.log" 2>&1; echo "test exit=$?" >> "$R/test.log"
```

**What this proves:** the C unit tests — over thirteen thousand checks of
individual functions — run on your machine. Use `make test`, never
`make && ./run_tests`: plain `make` builds the product, not the test binary, so
the second form re-runs a stale test binary and reports success every time.

**Success looks like** a line near the end reading `N checks, 0 failures`, then
`test exit=0`. On the development machine that was `13610 checks, 0 failures`
for commit `8e3972be`; your count may differ a little if your commit differs.

**A short or missing count line means the suite crashed** partway, which is
itself a finding — send the log. Failures print as `FAIL tests/<file>.c:<line>:
<the check that failed>`, so each one names its file, its line and the condition
that did not hold. Two things are known to cause
failures that are about the machine, not jichi: no writable `/tmp` (set
`TMPDIR` to a directory you can write), and no `/bin/sh` (tests that start a
shell cannot work — on Android 4.4 that is 47 of them). Say if either applies.

### Step 5 — the smoke tier, all of it

```sh
JC_SMOKE_KEEP_GOING=1 make smoke > "$R/smoke.log" 2>&1; echo "smoke exit=$?" >> "$R/smoke.log"
```

**What this proves:** jichi's end-to-end tests run on your machine — over three
hundred small POSIX shell scripts in `tests/smoke/`, each driving the real binary
against a fake model server, a pseudo-terminal, a socket. No Python needed.

**Why `JC_SMOKE_KEEP_GOING=1`:** the tier normally stops at the first failing
script, so one early failure hides every later one — you would learn about them
one run at a time. Keep-going runs everything and lists every failure.

**Success looks like** the last lines reading
`smoke: OK (319 drivers, 1870 checks)` (the numbers for commit `8e3972be`), then
`smoke exit=0`.

**How to read a failure,** because it is the most useful thing you can send:

- Each script prints [TAP](VOCABULARY.md) lines — `ok 3 - <what it checked>` or
  `not ok 3 - <what went wrong>`. The `not ok` line usually says what it saw.
- When a script fails, the runner re-runs it **alone** and labels the result:
  `PASSES standalone -> IN-SUITE-ONLY failure` means it failed only among the
  others (often timing or a leftover from a neighbour); `ALSO fails standalone ->
  a real defect` means it fails by itself. Both labels are evidence; send the log
  either way.
- Every script opens with a comment block — all 319 of them, counted —
  usually saying what it checks and which past incident it guards against.
  `head -40 tests/smoke/<name>.sh` is often enough to understand a failure, and
  that reading is exactly the kind of work that turns a report into a diagnosis.

**On a slow machine** — a small board, an emulator — scripts can run out of time
without being wrong. The knob is `JC_SMOKE_TIMEOUT_MULT`, and it is a **ratio**,
not a number to copy: your clean-build seconds divided by a reference machine's
([`LOW_MEMORY.md`](LOW_MEMORY.md) names the reference hosts and their build
times). If you need it, time a clean `make WERROR=1`, send that number, and say
which multiplier you used. A recent laptop is unlikely to need it — and if yours
does, that is a datum too.

### Step 6 — what jichi can see about your system

```sh
./jichi doctor > "$R/doctor.txt" 2>&1 < /dev/null
```

**What this proves:** jichi's own health check, run on your machine — where its
files live, whether they are really private, whether libcurl works, what
configuration it found. It **never prints secrets**: keys are reported only as
present or absent. It does print paths, which include your user name; you may
replace that with `<user>` before sending.

`< /dev/null` gives jichi an empty input. Without it, a jichi run that has no
terminal to talk to can wait for input forever — it is on every headless command
on this page for that reason.

### Step 7 — the live turns: does the agent actually work here?

This is the step that can earn **Driven**. It needs a model server on your own
machine. The instructions are for LM Studio. For Ollama — unmeasured here — its
documentation gives the OpenAI-compatible address as `http://127.0.0.1:11434/v1`,
and the context window is set differently (below); everything else is the same.

**Prepare the server first:**

- Start LM Studio's local server and load a model with tool-use support; a
  recent coder model is a reasonable first choice. Whether it *really* calls
  tools is decided by `doctor --live` below, not by the model's description.
  Note its **model id** exactly as LM Studio shows it.
- Give it a context length of **at least 32,768 tokens** in the model's load
  settings. jichi's very first request is about **twelve thousand tokens** before
  any conversation — measured at 12,182 with one model's tokenizer; others
  differ — because it carries the system prompt and the tool descriptions. With
  no `contextLength` in the config, jichi assumes a window of about 32,000
  tokens. A server loaded with a small default window answers the first request
  with *"Context size has been exceeded"*, which looks like a platform failure
  and is not one. (Ollama sets its window differently from LM Studio; the same
  minimum applies, and how to set it is Ollama's documentation, not this page's
  — nothing here has been measured on Ollama.)
- Make sure nothing else is using the server (rule 3).

**Then run:**

```sh
cat > "$R/live.json" <<'EOF'
{"models":[{"name":"live","provider":"openai","model":"MODEL-ID",
 "apiBase":"http://127.0.0.1:1234/v1","apiKey":"unused","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"maxRetries":1,"lowResource":false}
EOF
./jichi --config "$R/live.json" doctor --live > "$R/doctor-live.txt" 2>&1 < /dev/null
J="$PWD/jichi"; W=$(mktemp -d)
P="MAC-$(od -An -N3 -tx1 /dev/urandom | tr -d ' \n' | tr 'a-f' 'A-F')"
printf 'The pass phrase is %s.\n' "$P" > "$W/note.txt"; echo "$P" > "$R/phrase.txt"
/usr/bin/time -p "$J" --config "$R/live.json" --output json -p 'reply with OK' \
    < /dev/null > "$R/live-wire.json" 2> "$R/live-wire.err"
(cd "$W" && /usr/bin/time -p "$J" --config "$R/live.json" --auto -q \
    -p 'Use the read_file tool to read note.txt in this directory, then report the pass phrase.' \
    < /dev/null > "$R/live-tool.txt" 2> "$R/live-tool.err")
grep -c "$P" "$R/live-tool.txt"
```

Replace `MODEL-ID` with your model's id before you run it. `"apiKey":"unused"` is
right for a local server that wants no key; never write a real key into this
file.

**What each line does, and why:**

- `live.json` is a configuration naming exactly one model, on your own machine —
  so nothing can be routed anywhere else. It is the configuration the other
  Driven rows used: `scripts/_rig_live.sh` generates it, and only the address,
  the model and (for a gateway) how the key is named ever differ. That sameness
  is what makes your result comparable with theirs, and a check keeps this copy
  identical to what the script generates.
- `doctor --live` asks the server one question and reports how the model calls
  tools. You want the line **`tool calling observed "native"`**. A model that
  answers `text` *describes* tool calls in prose instead of making them; it will
  fail the second turn in a way that looks exactly like a platform bug. Choose
  another model rather than report that.
- `P` is a **pass phrase made up this second** — six random hexadecimal digits.
  It is written into `note.txt` in a brand-new empty directory, `W`.
- **Turn 1**, `reply with OK`, proves the wire: jichi built a request, the server
  answered, the streamed reply was understood. It chooses no tool and runs none.
- **Turn 2** asks the model to read `note.txt` *with the `read_file` tool* and
  report the phrase. The model cannot know the phrase except by making the tool
  call, jichi executing it, and the model reading the result — so the phrase in
  the answer is proof that the whole loop closed. That is the definition of
  **Driven**.
- `--auto` lets jichi run the tool without asking you; it is safe here only
  because the one thing the task does is *read* one file in a throwaway
  directory. Do not make a habit of `--auto` in a directory you care about. `-q`
  keeps the answer on its own.
- `/usr/bin/time -p` puts each turn's duration (`real` seconds) into the `.err`
  files. Rows record how long a turn took.
- The final `grep -c` prints **`1`** when the answer contains the phrase — the
  pass — and `0` when it does not.

**What a pass means, stated so nobody over-reads it:** the request, the
streaming, a real tool call, its execution, and a second turn that used the
result all worked, once, on your machine. It does **not** mean long sessions,
memory under pressure, or several agents at once have been tested. It means the
loop closed here.

### Step 8 — package it and send it

```sh
tar czf ~/jichi-report.tgz -C ~ jichi-report
```

Open an issue on the public repository and attach `jichi-report.tgz` — if the
site refuses the archive, attach the files one by one — or send it to whoever
asked you to test. Issues, questions and bug reports are the most
useful thing anyone can send this project ([`CONTRIBUTING.md`](../CONTRIBUTING.md)
says why); patches are applied by hand, not merged, and credited. In the message,
add in your own words:

- **the model** you used for step 7, and the server (LM Studio or Ollama);
- **anything you had to set** to make the build find curl or the compiler;
- **anything surprising**, even if everything passed;
- **whether you want to be named** in the row and the changelog, and how.

---

## 5. What never to send

| Do not send | Why |
|---|---|
| API keys, `~/.jichi.env`, any config file holding a key | They are secrets; nothing on this page needs one. |
| Telemetry (`~/.jichi.d/telemetry/`), run journals, session files | Not needed for a platform verdict, and they hold paths, commands and — at the `full` logging tier — the contents of files the agent read. If a live turn fails, a second round may ask for the log of that *one* run in a throwaway directory, and will say so. |
| Anything from a real project | The live task runs in an empty temporary directory precisely so no real work is involved. |

Your user name appears in paths in several files. Replacing it with `<user>` is
fine and loses nothing.

---

## 6. What happens to your report — checked, not trusted

Nobody can re-run your machine, so the report is built to check itself:

- **Which source.** `commit.txt` and the `build:` line name the commit. Public
  releases are curated snapshots of a private history, and the README records
  which public commit equals which private one — so the maintainer knows exactly
  which tree you tested, and the numbers it should produce.
- **The counts.** Your unit and smoke counts are compared with that commit's
  own. A count that is *lower* is a crash to explain, not a pass to round up.
- **Each failure** becomes one of two things: a **defect** (fixed, with your
  report credited — and, if history is a guide, a defect present on every
  platform), or an **environment note** (a missing tool, a slow disk), recorded
  as that. Nothing is quietly dropped.
- **The pass phrase** was generated on your machine a second before it was asked
  for, so a correct answer cannot come from anywhere but the tool call.
- **Two machines** of the same kind corroborate each other; two different ones
  (an Apple Silicon and an Intel Mac) are two rows.

Then a row is written in [`PLATFORMS.md`](PLATFORMS.md) with the strict word it
has earned, the numbers copied from your files, the date, and — if you want —
your name. **A row is a dated fact about one commit.** It does not go stale on a
calendar; it simply speaks for less of the tree as the tree moves, and
[`PLATFORM_RETEST.md`](PLATFORM_RETEST.md) says when that calls for a re-run.

---

## 7. When something goes wrong

| What you see | What it usually is | What to send |
|---|---|---|
| `build exit=` anything but `0` | a warning or error nobody has seen on your platform — the point of the exercise | `build.log`, `info.txt`, `cc.txt` |
| The build cannot find `curl/curl.h` | libcurl's development headers are missing, or not where the build looks | follow [`PREPARE_AND_BUILD.md`](PREPARE_AND_BUILD.md) §3–4 first; if that fails, `build.log` and `info.txt` |
| `make` fails parsing the Makefile | a BSD `make`; use `gmake` | — |
| Every probe in `make info` says `no` | no `cc` on the system; run with `CC=gcc` (or `CC=clang`) | `info.txt` with and without |
| The unit suite's count line is missing | it crashed partway | `test.log` |
| Many unit failures mentioning `/tmp` | no writable `/tmp` | re-run with `TMPDIR` set; send both |
| One smoke script fails, the rest are silent | you left out `JC_SMOKE_KEEP_GOING=1` | re-run step 5 as written |
| Smoke scripts time out on a slow machine | the timeouts assume a fast machine | step 5's ratio, and your build seconds |
| The smoke tier fails a check about untracked files | your report folder, or other files, inside the checkout | move them out (rule 1) and re-run |
| A live turn prints *"Context size has been exceeded"* | the model is loaded with too small a window, or the server is busy with something else | reload at ≥ 32,768 tokens, stop other clients, re-run step 7 |
| `doctor --live` says `text`, and turn 2 has no phrase | the model writes tool calls as prose | choose a model `doctor --live` calls `native` |
| A command sits there forever | a headless run without `< /dev/null` | re-run it as written |
| The final `grep -c` prints `1` even with the model server stopped | the check is not testing what it says — please report that too | everything |

[`PLATFORM_TESTING.md`](PLATFORM_TESTING.md) §5 has more of these, from the
maintainers' own rigs.

---

## 8. A worked example — this procedure, run on the development machine

Every command on this page was run before it was published, on 2026-09-23, on
the machine jichi is developed on — **Linux, not macOS**. `sw_vers` does not
exist there and was the one command left out; everything else ran as printed,
in a fresh clone of commit `8e3972be`, with the report folder outside it.

| Step | Result, copied |
|---|---|
| 2 | `Linux 7.0.0-34-generic x86_64`; `cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`; `git status` clean; `local-changes.diff` empty |
| 3 | `build exit=0` in 5 s; `version.txt` ends `build: 8e3972be` |
| 4 | `13610 checks, 0 failures`; `test exit=0`; 11 s including the build |
| 5 | `smoke: OK (319 drivers, 1870 checks)`; `smoke exit=0`; 801 s, on a machine shared with other work |
| 7, first try | **failed**: both turns returned *"Context size has been exceeded"* — the server was shared with a running experiment (rule 3) |
| 7, second try | `tool calling observed "native"`; turn 1 answered `OK` in **0.71 s**; turn 2 answered `The pass phrase is: **MAC-B46684**` in **0.87 s**; `grep -c` printed **`1`** |

The second try used a free model on an OpenAI-compatible gateway rather than a
local server. For that, the config names the gateway's address and its model id
and, instead of `"apiKey":"unused"`, **`"apiKeyEnv":"NAME"`** — the *name* of an
environment variable holding the key, so the key itself never sits in a file or
appears in a process list. Those are the only differences; the task is the same.

Two things this example taught the page, both now in it: a model server shared
with other work is not a clean instrument (rule 3 — the first try's failure), and
the first request is about twelve thousand tokens, so the model needs a real
window (step 7 — measured on the second try). The report folder's place outside
the checkout (rule 1) was not learned here; it was known in advance, and the run
confirmed the clone stayed clean.

---

## 9. What this page cannot promise

- **Nothing on this page has run on a Mac.** The commands are meant to be
  portable, and every one ran on Linux as printed (§8), but whether macOS's own
  `od`, `tar`, `mktemp`, `/usr/bin/time` and `sw_vers` accept exactly these forms
  is unmeasured. If one of them fails on your Mac, that is a finding about this
  page — send it.
- **A Driven pass is one closed loop**, not a claim about long or busy sessions
  (step 7 says exactly what it covers).
- **The live task is a copy.** Its one definition is `scripts/_rig_live.sh`,
  which the maintainers' rigs source; this page repeats its two prompts, its
  fixture and its configuration for you to paste. A copy is where drift starts,
  so a check pins it: `tests/smoke/rig_live_commands_lint.sh` fails if this page
  and the rig ever disagree.
- **There is no one-command script yet.** Everything above is copy-and-paste. A
  script that sources the rig's definition and writes the report folder for you
  is the obvious next step, and is recorded as open work in
  [`DEFERRED.md`](DEFERRED.md) rather than promised here.

---

## 10. Words used on this page

- **unit suite** — the C tests in `tests/`, compiled into one program,
  `run_tests`, that checks individual functions (`make test`).
- **smoke tier** — the end-to-end tests in `tests/smoke/`, one POSIX shell script
  each, run by `make smoke` (see [`VOCABULARY.md`](VOCABULARY.md)).
- **TAP** — the `ok N - …` / `not ok N - …` lines those scripts print
  ([`VOCABULARY.md`](VOCABULARY.md)).
- **`WERROR=1`** — build with warnings treated as errors, so a single warning
  fails the build.
- **build stamp** — the `build:` line of `jichi --version`: the git commit the
  binary was built from.
- **fail-fast / keep-going** — stop at the first failure, or run everything and
  list all failures (`JC_SMOKE_KEEP_GOING=1`).
- **pass phrase** (a *nonce*) — a value made up fresh for one run, so that seeing
  it again proves something happened in *that* run.
- **native tool calling** — the model returns tool calls in the structured form
  the API defines, which jichi executes; the alternative, `text`, is a model
  writing tool calls as prose, which nothing executes.
- **Never compiled / Partly verified / Verified / Driven** — the four words of
  §1, defined in [`PLATFORMS.md`](PLATFORMS.md).

---

## Where to go next

- [`PLATFORMS.md`](PLATFORMS.md) — every row measured so far, and what each
  taught. Reading one row before you write yours is the best preparation there is.
- [`PLATFORM_TESTING.md`](PLATFORM_TESTING.md) — the judgement behind every step
  here, and a ladder of further things to try once your machine is green.
- [`PREPARE_AND_BUILD.md`](PREPARE_AND_BUILD.md) — installing the prerequisites,
  platform by platform.
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) — how contributions are received, and
  what a good bug report contains.
