# The testing runbook

*A procedure, not a philosophy. Every step exists because skipping it cost
something in this repository, and the cost is named. Written 2026-08-22 (M540)
from one session's mistakes — six defects found in the product and seventeen in my
own instruments — and intended for whoever writes the next check: a person, or an
agent like me.*

**How to use it.** Work top to bottom when you add or change a test. Each step is a
question with a mechanical answer; if you cannot answer it mechanically, you have
found the thing this runbook is for. The companion pages are
[`TEST_TIERS.md`](TEST_TIERS.md) (what each tier is),
[`TESTING_TUTORIAL.md`](TESTING_TUTORIAL.md) (how to write one),
[`TEST_INTEGRITY.md`](TEST_INTEGRITY.md) (the incident register) and
[`analysis/2026-08-22-learning-from-errors.md`](analysis/2026-08-22-learning-from-errors.md)
(why prose alone does not prevent recurrence).

---

## 0. Before you touch anything

```sh
sh scripts/preflight.sh          # refuses a tree whose gate is already running
```

**Why.** `make ci` and `make jichi` write the same object files and the same
binary. Running them together produced `collect2: error: ld returned 1 exit
status` — which I spent a minute reading as a code defect before recognising my own
concurrency. Worse, a gate that straddles an edit reports a verdict about a tree
that never existed. If you edit mid-run, throw the result away and re-run; do not
reason about it.

## 1. Prove the test can fail — per **check**, not per driver

```sh
sh tests/teeth.sh                # scripts the ritual
# or by hand: revert the guard, run, confirm the failure, restore
```

**The unit that can be vacuous is a single check, not a file.** Four times in one
session a check of mine passed while covering less than its header claimed, and in
one case it stayed green *inside the very perturbation run* that proved a sibling
check red. Perturb what each check guards, watch **that** check go red, and read its
message.

Failures this catches, all real:

| What I wrote | Why it always passed |
|---|---|
| `awk` from the first `JC_TOOLCAT_WRITE` | that is the `return` line; it extracted 2 names of 6 and compared a subset to a superset |
| "no stray log inside the worktree" | the worktree is deleted when the attempt ends, so the count could only ever read 0 |
| `find "$ws/.jichi.d"` | `.jichi.d` lives under `$HOME`; the path never existed |
| "the report says `untracked`" | a created file is a *tracked* addition in the checkpoint's shadow repo, so the clause cannot fire in that fixture |

**A perturbation has a direction, and an absence check's is the one that
*produces* the forbidden thing.** A check that asserts "no `dirty_dispatch` event"
is still green when the event-emitting code is *removed* -- that is the vacuous
direction, and it cannot show the check is able to fail. The tooth makes the
forbidden thing HAPPEN: M1060 (the hardening drive, run 0077, verified by the
supervisor by effect on 2026-10-27) added one bare emission of the event, flow-
neutral, and only then did the driver's absence checks go red *through their own
grep*. Ask every red-first task on an absence check for that direction; a
"suppress the writer" tooth proves the check is not broken, not that it works.

## 2. Prove the *instrument* can fail

A perturbation proves the check notices a broken product. It does not prove your
harness is wired to the product at all.

```sh
# Does the override you are using actually reach the driver?
JC_SMOKE_BIN=/nonexistent sh tests/smoke/<driver>.sh   # must go red
```

**Why.** Sweeping drivers against a stub, I set `BIN=` and got *12 of 12 still
green*. `_smoke.sh` sets `BIN="${JC_SMOKE_BIN:-…}"`, so my variable was ignored and
every driver ran the real binary. The result was implausible, which is the only
reason I checked. **An implausible measurement is a measurement of your
instrument.** Include a check whose job is to prove the probe can fire — e.g.
`describe_names_lint` check 5 asserts an invented flag *is* rejected, so that
check 4's "every advertised flag is accepted" means something.

## 3. State the universe, and floor the extraction

Every set-difference check is clean against an empty set. So assert the size
first, and **floor it at today's exact count**:

```sh
n=$(grep -o '"name"' file | wc -l)      # occurrences, not lines
[ "$n" -ge 148 ] || t_fail "extraction floors missed: got $n"
```

**Fix the extraction, never lower the floor.** And print the **set** on failure,
not only its size — every extraction bug this session was caught by looking at
members:

- `grep -c '"name"'` over one-line JSON read **1** for a table of 21. `grep -c`
  counts *lines*; `grep -o | wc -l` counts occurrences.
- A "references" floor read **9** where there were **7** call sites, because the
  bare symbol name also matched two comments *mentioning* it. Match the full call
  form.
- A roff pattern matching `--[a-z-]*` against `\-\-budget\-tokens` stopped at the
  first internal `\`, yielding an eight-character stub that is not a flag at all --
  and reporting *"61 documented, 17 phantoms"* against a truth of 77 and 0. **Three
  times in one session.** De-roff first (`sed 's/\\-/-/g'`), match second.
  (`docs_flags` caught the first draft of this very bullet for quoting that stub as
  though it were a flag, which is the lint doing exactly its job on the page about
  lints doing their job.)
- A too-*wide* extraction reported `ping` and `shutdown` as missing subcommands;
  they are daemon **request** names from a different table. Over-wide costs exactly
  as much trust as too-narrow.

## 3b. After editing a fixture, look at the bytes you changed

**The incident (M551).** A ptydrive script contained `send "run it\r"`. An edit
rewrote the file through Python's `io.open(path, encoding='utf-8')`, which reads in
*universal-newline* mode — so a lone `CR` came back as `\n` and the line was silently
split in two. `pd_script_parse` rejected the script, and the symptom was **not** a
parse error: it was a **missing log file**, three checks red, and nothing pointing at
the cause. Two checks in the same driver stayed green throughout, because they were
reading a capture from the previous run's leftovers.

**The rule.** A fixture is input to a parser you did not write. After you edit one,
`cat -A` the lines you touched and confirm the escapes are still escapes. It costs one
command; the alternative is debugging the product for a defect in your editor.

Pass `newline=''` to any Python that reads-then-writes a file with control bytes in it.
`sed -i` and shell heredocs do not have this failure mode.

## 3c. Make the fixture behave like the real peer

**The incident (M882).** M880 kept a socket watch in the transfer's stack frame, and
libcurl closes a connection the server keeps open after that frame has returned: the
close wrote into it. Every real provider keeps its connection open. The smoke tier's
mock answered `Connection: close` and closed every connection inside the transfer,
while the watch was still valid. So 2,422 smoke checks and `make ci`'s sanitizer turn
passed, and the first board that drove a real model crashed on both turns
([ANECDOTES #120](ANECDOTES.md)).

**The rule.** A mock answers the questions you thought to ask; the peer decides which
code runs. Before you trust a green against a mock, name what the real peer does that
the mock does not, and make the mock do it -- `mockmodel --keep-alive` exists since
M882 -- or say in the driver's header that it does not.

## 4. Enumerate the universe a second way, by a different route

`--help`'s command column and `grep 'strcmp(args.pos[0], …)'` should agree. They do
not: the grep misses `export`, `checkpoints`, `recover`, `undo` and `rewind`, which
dispatch through other paths. `subcommands_lint`'s header records this costing it
four names; I walked into it again two milestones later. **Diff the two routes.
Where they disagree, one of them is your bug.**

## 5. Ask whether the driver would notice the product disappearing

```sh
make smoke-mutant                # changed drivers, seconds
MUTANT_BASE=<commit> make smoke-mutant   # also every driver changed since <commit>:
                                         # a batch of commits gated by one make ci (M842)
MUTANT_ALL=1 make smoke-mutant   # everything, ~9 minutes; run before a release
```

Replaces the binary with a script that prints nothing and exits 0. Any driver that
stays green is measuring its own fixtures. Swept over 211 drivers this found
**one** real defect: `parallel_abort.sh` asserted only that a process "exited
promptly" after SIGINT — which a program that does nothing satisfies perfectly —
while its header claimed it verified two forked children were reaped.

*A syntactic lint for this was tried first and abandoned after measurement: three
formulations each produced false positives on the first files inspected, because
the vulnerable unit is a check and `grep` can only see a file. The negative result
is why the mutant sweep exists.*

## 6. Run the whole gate, not the tier you are working in

```sh
make ci
```

The ASan stage was red for roughly sixteen milestones because nobody ran it. A
tier that runs nowhere is a tier that does not exist — which is also why
`smoke-faults` and `smoke-mutant` are called *from* `make ci` rather than left as
targets somebody might remember.

## 7. Before running a command against a real tree, name what it writes

**Classify by effect: prints / reads / writes.** An existence question is never
answered by execution.

```sh
jichi undo --dry-run     # yes
jichi undo               # 768 files, 41,927 deletions, one line of output
```

Checking whether the `undo` subcommand existed, I ran
`for c in export rewind undo; do ./jichi $c; done` — three names treated as one
homogeneous set when they differ in the only dimension that matters. `--dry-run`,
`--help`, `jichi describe`, or the forty lines of source already open in the file I
was reading would each have answered for free. **Probe in a workspace you are
willing to lose**: every driver here uses `smoke_tmp` for exactly this reason, and
the repository is the worst possible place to test a checkpoint revert.
[ANECDOTES #66](ANECDOTES.md).

## 8. For a measurement run: fences on, caps off, diagnostics **on**

| | Setting | Why |
|---|---|---|
| Fences | **on** — `--readonly`, `pathFence`, `--edit-scope` | bound blast radius; these are real projects |
| Caps | **off** — no `--deadline`, no `--budget-tokens`, raise `timeouts.stall` | a cap that fires does not hide the answer, it manufactures a different one |
| `connect` timeout | keep; raise it for a shared gateway | it bounds TCP and the TLS handshake, never an arriving answer (M877) |
| `-q` / `--quiet` | **off** | see below |
| Detach | `setsid` | a tool timeout SIGTERMs the process group and kills the work |

**`-q` silences the diagnostics that explain your result.** Measuring jichi against
four projects, one run ended `rc=0`, `stop_reason: done`, and an **empty answer**.
jichi has a warning for exactly that triple — M167, "no tool call and no text while
N tools were advertised" — and M521's reasoning diagnostic besides. Both go to
stderr. I had passed `-q`. `CLAUDE.md` already records the same lesson from a
different direction: *"jichi does warn, and I did not read it."*

**And a run that fires a cap is not a measurement.** The first jichi run failed at
84s with `stop_reason: timeout` — the default stall timeout, on a 9B model reading a
large repo. Raised to 1800s the same run completed in 119s. Had I reported the first
number, I would have published a property of my timeout as a property of the model.

**And off means no wrapper, not a generous one.** Reproducing a crash, I ran each build
as `timeout 120 jichi ...`. The cap never fired, and the same build crashed under it
and answered without it: `timeout(1)` runs its command in a separate process group,
and the defect was undefined behaviour, whose symptom follows the conditions. Reproduce
a failure the way it ran, measure the operation before choosing any bound, and settle
a symptom that comes and goes with an instrument -- here AddressSanitizer -- not with
another run ([ANECDOTES #120](ANECDOTES.md)).

## 9. Do not report a defect you have not reproduced

Same prompt, same model, same config, two runs at ~95–100k input tokens: one
returned 312 output tokens and empty content, the other a 4,505-character answer.
That is a model-side flake at high context, **not** a jichi defect, and it would
have been dishonest to file it as one. Equally: I have *not* observed M167's
warning firing in the wild — only established that the code path exists and would
fire — because the failure did not reproduce. Say which of those you have.

## 10. Write the failure message for the person who will read it

They will read it once, in a hurry, without your context. Name the number, the
floor, and what to do:

> `not ok 8 - rows in the workspace=0 (want >=2), hint logs on disk=1 (want exactly 1, in the workspace). Found: /tmp/…/worktrees/att-2840854/wt-0/.jichi/hints.jsonl`

That message names the stray path. The version before it said `stray=0` and was
both wrong and useless.

And when a check's header claims more than the check delivers, **fix the header**.
One of mine claimed two sibling checks "would both pass" on a planted defect; the
perturbation disproved it. Overstating what a check *uniquely* catches is the same
species of error as overstating what it covers.

---

## The short form

1. `preflight.sh`. Never edit a tree mid-gate.
2. Perturb **per check**. Read the message.
3. Prove the instrument can fire, not just the check.
4. Floor every extraction; print the set, not the count.
5. Enumerate the universe twice, by different routes.
6. `make smoke-mutant` — would this driver notice the product vanishing?
7. `make ci`, whole.
8. Name what a command writes before you run it. `--dry-run` exists.
9. Measurement: fences on, caps off, diagnostics on, `setsid`.
10. Reproduce before you report.

**The one sentence.** Every failure above passed a check I had written, believed and
in most cases just watched go green — so the question is never *did it pass* but
*what would have had to be true for it to fail*.
