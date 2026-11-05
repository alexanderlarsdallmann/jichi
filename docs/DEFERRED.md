# Deferred register

> **What this is.** Things deliberately **not done**, with the reason and where the
> reasoning lives. A companion to [`DECISIONS.md`](DECISIONS.md): that page records what
> was chosen, this one records what was consciously left.
>
> **Why it exists.** "Recorded as the next slice" is only honest if the record is
> findable. Deferrals were accumulating inside individual ROADMAP entries and proposals,
> where a reader would have to already know the milestone to find them.
>
> **Coverage starts at M298** (2026-08-05), the beginning of the current program. Earlier
> deferrals live in their own milestone entries and are not back-filled — inventing a tidy
> list of decisions I did not witness would defeat the purpose. Items are removed when
> done, with the closing milestone noted in `DECISIONS.md` or the ROADMAP.

A deferral belongs here when someone could reasonably ask "why isn't this done?" and the
answer is a judgement rather than an oversight.

## Check the checkable part of a reason BEFORE parking the item (M326b)

**Three entries in a row were parked on an assumption that a minute's reading would have
settled:**

| Deferred | Detail |
|---|---|
| Measuring a `core` attempt that needs a hint (M319) | [details](#def-1) |
| Flipping `attempt` to `core` (M320) | [details](#def-2) |
| `repoMap: false` for `attempt` (M326) | [details](#def-3) |

<a id="def-1"></a>
#### Measuring a core attempt that needs a hint
**The reason given:** *"core costs the hint ladder — the machinery `attempt` exists to exercise"*
**What it actually was:** across two models and 24 runs the ladder was **never once called**, six of those runs failing with the tool advertised

<a id="def-2"></a>
#### Flipping attempt to core
**The reason given:** the same hint-ladder cost
**What it actually was:** same; the flip was then refused for an entirely different, measured reason

<a id="def-3"></a>
#### repoMap: false for attempt
**The reason given:** *"tasks 20–22 are explicitly about reading this repository"*
**What it actually was:** all three work on **self-contained fixtures**; none names a file in `src/`


The shape is identical every time: the reason made a **factual claim about a fixture, a tool or a
number**, it sounded obviously true, and nobody checked it. Meanwhile the *judgement* half of each
reason was fine. So:

> **A reason may contain judgement, evidence, or an unchecked factual claim. The first two belong
> here. The third does not — check it first, and write down what the check found.**

Concretely, before adding a row: if the reason asserts what a task contains, what a tool does, how
many call sites something touches, or what is reachable from this machine — **go and look.** These
are minutes of work, and every one of them that went unchecked outlived several milestones and
misdirected the next person to read it.

The audit that produced this rule (M326b) is recorded in the rows below: each remaining reason's
checkable claims were verified, and two of them changed.

**A deferral is not a rejection** (M317). Its first revision conflated the two: three rows
were things decided *against* on the merits, which will never be done and do not belong on a
list of pending work — a register that mixes "not yet" with "not ever" makes both unreadable,
and the "not ever" rows are the ones a future reader would waste time on. Those moved to
[`DECISIONS.md`](DECISIONS.md), where a rejected alternative belongs. **The test: if new
information could change the answer, it is deferred; if the answer follows from the design,
it was decided.**

---

## What a walk of the whole register found (M657, 2026-09-18)

Every row on this page was read in one sitting, and the page's *structure* was
audited mechanically rather than by eye. What that produced is recorded here in
three parts, including the third, because a review that reports only what it
fixed is exactly the shape this page exists to distrust.

**The population.** 26 open sections, 58 open rows. That is what "the deferred
register" refers to when anyone cites it.

**What is now checked forever, for every row** —
[`tests/smoke/deferred_register_lint.sh`](../tests/smoke/deferred_register_lint.sh),
five checks — a floor on its own extraction, then: no closed row under an open
heading, no open heading with no rows, every `path:line` anchor resolves, and the
never-compiled platform set matches `PLATFORMS.md`'s. It found, on its first run: **six struck-through rows under
three `## Open` headings**, **one `## Open` heading with no rows at all**, and
**illumos missing from the register entirely**. Three hand-audits of the same
invariant — M463, M492, this one — is past this project's threshold for
preferring a lint to an audit. The lint is deliberately structural: *it could not
have caught the worst row on the page*, which was well-formed and simply untrue.
What it catches is the class that kept the untrue row invisible.

**What was re-checked by hand, one claim at a time (the M326b rule).** Twelve
claims, and two of them were false:

| Deferred | Detail |
|---|---|
| *"no `LICENSE` or `COPYING` file exists"* (the licence row) | [details](#def-4) |
| *"they will bring [the logo] before release"* | [details](#def-5) |
| *"146 hand-written `printf(name); test_name();` pairs"* | [details](#def-6) |
| *illumos is blocked* | [details](#def-7) |
| the ARM bench rows can be re-run | [details](#def-8) |
| *"a memory-pressure harness the smoke tier does not have"* (three peer-transport rows) | [details](#def-9) |
| the two unbounded read loops are where the rows say | [details](#def-10) |
| *"MCP and LSP shutdown block in `waitpid(pid, …, 0)`"* | [details](#def-11) |
| *"no `seccomp`/`bubblewrap`/`firejail`/`unshare`-as-sandbox exists in `src/`"* | [details](#def-12) |
| *"`jc_mem_total_mb` has an `#if defined(__APPLE__)` branch"* (the macOS row) | [details](#def-13) |
| *"jichi has zero notebook support"* | [details](#def-14) |
| the debt table's arithmetic | [details](#def-15) |

<a id="def-4"></a>
#### "no LICENSE or COPYING file exists" (the licence row)
**What the check found:** **False.** `LICENSE` and `NOTICE` committed 2026-08-27; the public repository shipped that day and was re-cut as `v0.9.1` on 2026-09-17. Closed, below

<a id="def-5"></a>
#### "they will bring [the logo] before release"
**What the check found:** **Expired.** Two releases shipped without it; no logo file in either tree. Re-scoped rather than closed

<a id="def-6"></a>
#### "146 hand-written printf(name); test_name(); pairs"
**What the check found:** **Stale: 174.** True when written; the row's own argument is stronger for it

<a id="def-7"></a>
#### illumos is blocked
**What the check found:** **Not blocked on access.** `/dev/kvm`, group `kvm`, `qemu-system-x86_64`, 24 threads with virtualisation, 137 GB free — all present on this machine today. It costs a session, not a resource

<a id="def-8"></a>
#### the ARM bench rows can be re-run
**What the check found:** **Not today.** Pi Zero 2 W, Pi 400 and UNO Q all fail to answer; the hardware is powered down

<a id="def-9"></a>
#### "a memory-pressure harness the smoke tier does not have" (three peer-transport…
**What the check found:** **Wrong about the test.** Each fix's effect is observable as an error, not as RSS. Re-scoped in place

<a id="def-10"></a>
#### the two unbounded read loops are where the rows say
**What the check found:** Confirmed: `jc_sb_append_n` into an unbounded builder, at both anchors

<a id="def-11"></a>
#### "MCP and LSP shutdown block in waitpid(pid, …, 0)"
**What the check found:** Confirmed, both sites

<a id="def-12"></a>
#### "no seccomp/bubblewrap/firejail/unshare-as-sandbox exists in src/"
**What the check found:** Confirmed — the terms appear in `docs/` only, discussing the gap

<a id="def-13"></a>
#### "jc_mem_total_mb has an #if defined(__APPLE__) branch" (the macOS row)
**What the check found:** Confirmed, `src/platform/jc_platform_posix.c`

<a id="def-14"></a>
#### "jichi has zero notebook support"
**What the check found:** Confirmed: `grep -rc ipynb src/ include/` is still empty

<a id="def-15"></a>
#### the debt table's arithmetic
**What the check found:** Confirmed against the tree as it stood at 298 drivers (299 once this milestone's own driver landed; the table is restated there) — and it exposed a gap of its own: **7 of 18 Verified rows cite a driver count**, so eleven rows have no computable debt at all. Now a row of its own


**What was NOT re-checked, and is therefore as old as its last sweep.** The
factual claims inside the other ~46 rows. Several carry measurements from
2026-08-10 or 2026-08-17 sweeps and say so; several name a revisit *condition*
whose truth nobody has tested since it was written — and the M449 closure is the
warning here, where the unblocking condition ("if a buildroot toolchain becomes
available") had been met for some time and nobody had looked. A walk that claimed
to have verified fifty-eight rows in one sitting would be the overclaim this
register was built to prevent, so: twelve were checked, and this sentence is the
other forty-six.

### The second pass, over those forty-six (M658, same night)

The operator asked for the rest to be checked "as far as possible", so they were.
**Eleven more rows had a mechanically checkable claim; nine of the eleven were
wrong, and one of the nine was wrong in the other direction — the work had been
done and the row never noticed.**

| Deferred | Detail |
|---|---|
| A seeded fuzz-lite harness for the pure cores | [details](#def-16) |
| Fukabori is thin on code, Annai is not | [details](#def-17) |
| A diagram lint would encode a two-item allowlist | [details](#def-18) |
| The system-prompt writer object | [details](#def-19) |
| The per-spec "stuck alone" line | [details](#def-20) |
| The localized decks | [details](#def-21) |
| `PROJECT_TIMELINE` figures behind | [details](#def-22) |
| `telemetry` reads one log of many | [details](#def-23) |
| A records assignment, `74-your-own-registers` | [details](#def-24) |
| `CURRICULUM.md` states no scope boundary | [details](#def-25) |
| Modern C / modern C++ appear zero times | [details](#def-26) |

<a id="def-16"></a>
#### A seeded fuzz-lite harness for the pure cores
**What it claimed:** no such harness exists
**What the check found:** **It exists.** `tests/fuzz/` — a deterministic seeded generator (xorshift32), a committed corpus, a libFuzzer front end, **19 targets**, and a lint already holding the documented count. Three of the four *named* cores still lack a target; that is the remaining work, and it is three targets, not a harness. **Done 2026-09-22 — 19 -> 22, each proven against a planted bug**

<a id="def-17"></a>
#### Fukabori is thin on code, Annai is not
**What it claimed:** Fukabori's twelve chapters carry **one** block each
**What the check found:** **1–4, mean 2.1**, with exactly two still at one — and those two are the two the neighbouring row argues should be. The inversion is gone

<a id="def-18"></a>
#### A diagram lint would encode a two-item allowlist
**What it claimed:** three chapters have no mermaid
**What the check found:** **Exactly two**, chapters 1 and 11. Reason confirmed, not eroded

<a id="def-19"></a>
#### The system-prompt writer object
**What it claimed:** 38 appends in `build_parts`, 57 in the file
**What the check found:** **38 and 81.** The number the cost rests on has not moved in 300 milestones; the decorative one rotted

<a id="def-20"></a>
#### The per-spec "stuck alone" line
**What it claimed:** 7 of 77 specs
**What the check found:** **8 of 87**

<a id="def-21"></a>
#### The localized decks
**What it claimed:** one to two slides short
**What the check found:** **one, two AND three** — the declared gap widened and the prose describing it did not

<a id="def-22"></a>
#### PROJECT_TIMELINE figures behind
**What it claimed:** ja 4; de·es·zh 22
**What the check found:** **ja 0** (M651 finished it) **and 21** for the rest. The Japanese half of that row is done

<a id="def-23"></a>
#### telemetry reads one log of many
**What it claimed:** 1 of 35
**What the check found:** **1 of 6** — M599's narrowing, visible in the count at last

<a id="def-24"></a>
#### A records assignment, 74-your-own-registers
**What it claimed:** a free slot
**What the check found:** **74 is taken** by `74-read-the-turn` (M627)

<a id="def-25"></a>
#### CURRICULUM.md states no scope boundary
**What it claimed:** zero matches, three phrasings
**What the check found:** **Still zero, all three.** Corpus 505 → 524 files

<a id="def-26"></a>
#### Modern C / modern C++ appear zero times
**What it claimed:** `uint32_t` 0, `_Static_assert` 0, `std::span` 0
**What the check found:** **Holds — but only just.** A bare grep now returns 2 each, and **every hit is this register's own row or the analysis table reporting the zero.** The claim about the *teaching corpus* is intact; the measurement had started counting itself


That last one is the reason this pass was worth running twice. A documentation
count must exclude the pages that report the count, or it re-counts itself — the
same species as the four withdrawn recommendations at M406/M407, where an ad-hoc
grep matched "the comment quoting its own subject". It cost one nearly-recorded
false finding, caught by looking at *where* the matches were instead of how many.

**Still not re-checked, and now a much smaller set:** the rows whose claims are
not mechanically decidable from this machine — a workload's measured behaviour, a
third-party corpus, an operator's statement, a judgement about what a learner
needs. Those are named in their rows and stay as old as their last sweep.

### What I recommend doing next, in order

*Rewritten 2026-09-20. **Five of the previous six are done** — the language
course complete (M671/M674/M675), the UI/UX tutorial with its TUI section
(M670/M678), the bibliography's deferred languages (M677), `doctor` on
`maxParallelAgents` (M669), and both red drivers (M672) — and item 6's item zero
plus both Raspberry Pi rows landed at M676/M677. A recommendation list that
outlives its recommendations is worse than none, because it is read as current;
this is the second rewrite under that rule. Ordered by **value per hour**, with
what stands in the way named.*

1. ~~**Run the Debian ladder with a live step.**~~ **Done at M679, and the
   answer is yes.** `v2k` — a whole machine with **160 MB of RAM** — ran both
   turns: `agentic turn: the model called a tool and reported TIER-V-EA7E77`.
   It is now the smallest whole machine in the matrix to have closed the agent
   loop; the Pi Zero's 415 MB held that before. Two of this project's own bugs
   stood between the question and the answer and **both looked like results** —
   a port collision that printed *"the kernel never STARTED … a property of the
   IMAGE"*, and a live step pointed at the wrong guest path that printed two
   FAIL lines on the row asking this very question. *Original entry:* ~~illumos~~ **ran at M678 and is
   now fully Driven** — `18 ok, 1 failed`, agentic phrase `TIER-V-52FE94`, the
   tool half finally happening on that kernel after a year of text turns by
   hand. `tier-v-vm.sh` is still wired and unrun, and its question is the sharper
   one nobody has asked: **does the agent loop close at all in 160 MB?** Every
   gate on those rows is offline, so the answer is genuinely unknown rather than
   assumed. Nothing is blocked; this is wall clock.
1b. ~~**Diagnose the seven drivers still failing on illumos**~~ **— done at
   M683. All ten now pass on OmniOS r151058, and the whole tier does: `smoke: OK (311 drivers, 1,801 checks)` there, against 311/1,814 on this bench.** Every one was a
   defect in the project's own test tooling, not in jichi, and the whole batch
   was **five** fixes because three drivers shared a cause:

   | Driver(s) | Mechanism, measured on the live guest | Fix |
   |---|---|---|
   | `context_tools_live`, `superseded_marker`, `reading_trace` | `mockmodel` wrote `req.N` ending mid-line, because an HTTP request ends with its JSON body. POSIX defines a text file as newline-terminated lines, and two tools disagree about the last one: illumos `grep -o` returns **only the first match** on an unterminated line (27 advertised tools read as 1; 2 sentinels read as 1), and illumos `sed` **terminates** the last line it writes, as POSIX requires, where GNU sed preserves its absence — so every committed trace artifact differed from its illumos re-take by exactly one byte | terminate the capture in `capture_request`; 7 `expected/req.*` re-taken, diff verified to be that byte and nothing else |
   | `doctor_language` | `env -u` is a GNU extension; illumos answers `env: illegal option -- u`. **It exits 1**, and 1 is one of `doctor`'s own verdicts, so the driver's rc guard could not tell a refused option from a result — only its "every capture must contain a language row" half could | `( unset VAR; … )`, which `state_root.sh` had recommended in a comment since it was written |
   | `i18n_tracks_lint` | `RS="-->"` is a gawk extension. illumos one-true-awk (Aug 27, 2018) uses the **first character only**, so it split on `-` and stripped no comment at all — inverting the check, whose premise is that a `<!-- figures-behind: N -->` declaration's own number is not evidence | an `index`/`substr` stripper carrying state in a global; verified equivalent on 590 files under gawk and identical host-vs-guest on all 51 pages |
   | `preprompt_discard` | On illumos a write to a pty master **with no slave open returns the full length and discards the bytes** (measured: Linux 6 of 6 readable, illumos 0 of 6, with and without the STREAMS push). `ptydrive` set `presend_ok` from the write's return value — the attempt, not the effect — so the driver's own skip path could not fire | open the slave in the parent first (which is also the faithful model: a terminal exists before anyone types), verify with `ioctl(FIONREAD)`, and let the child inherit the descriptor across `fork` so the open count never reaches zero. illumos now **runs** the test rather than skipping it |
   | `snapshot_lint` | `git archive <commit>` writes a **pax global header**; GNU tar consumes it silently, illumos tar does not recognise typeflag `g` and writes it out as a regular file — landing `pax_global_header` at the root of the tree this project publishes to strangers | archive `<commit>^{tree}`, which omits the header. Measured: identical 2,332 entries, `export-ignore` still honoured (and `docs/internal/` is tracked, so that comparison is not vacuous) |

   **The bug's own artifact then impersonated the bug.** After the fix, the
   guest still failed: a previous session had run `git add -A` there while a
   stray `pax_global_header` — produced by this very defect — sat in the tree,
   so it became a **tracked file** in the guest's scratch commit and `git
   archive` shipped it legitimately, in both forms. Parsing the tar typeflags on
   both machines is what separated the two.

   **And the diagnosis found a worse defect than the seven.** Checks 8-13 of
   `snapshot_lint` build one text corpus by concatenating every non-ELF file;
   GNU grep prints matches until the **first NUL byte** and then goes silent.
   ELF was excluded for exactly that reason — but a PNG carries NULs too, and
   this session added one. Measured: the first NUL sat **89.1% of the way
   through a 19.5 MB corpus**, so 10.9% was never read, and the cut point moves
   with `find` order. It was hiding a real leak — the author's home path, in
   `docs/ILLUSTRATION.md`, in the paragraph explaining that a screenshot must
   not show one person's machine. The corpus now strips NULs (what
   `smoke_bgrep` does), excludes the `.git/` the `--commit` probe leaves behind,
   and a new floor asserts it is NUL-free, so a corpus that silently shortens
   again is a failure rather than a clean scan of less.

   **Recurrence is linted, not remembered:** `posix_utils_lint` checks 23-26 —
   `env -u`, multi-character `RS`, each with a matcher proved on planted
   positives and negatives first, because both were written to exclude the
   lint's own text and an excluded file plus an unproven matcher is a check that
   can pass while reading nothing.

   *Original entry, for the ten:* (three of the ten closed earlier:
   `bibliography_lint` at M680 — an awk with no interval expressions;
   `tutorial_refs_lint` at M679/M680 — an unescaped `/` in a bracket expression;
   and `config_defaults_lint` at M681 — a newline-less stream into `grep -o`.
   All three diagnosed on a live `--keep` guest, which is the only way any of
   them was visible.) **The instrument is the reusable part:** a PATH shim
   pointing `awk` at busybox awk runs any driver under a stricter parser in
   milliseconds; applied to all 64 `*_lint.sh` drivers it found exactly one
   defect, so the tier's existing awk usage is already portable. What it could
   **not** find is everything above: five of the seven needed a live guest,
   because they are kernel, libc and utility behaviour rather than parsing.

2. ~~**Drive the tiny row for real.**~~ **Already done at M680; this entry was
   stale, and that is the finding.** It said *"the row is turning, not driven"*
   and *"the 160 MB Debian row holds that now"* while
   [`PLATFORMS.md`](PLATFORMS.md) had already recorded the opposite: the agent
   loop closing on a **96 MB** machine with no distro, both turns of the shared
   driven task, the agentic one reporting `TINY-3232A6` — a phrase minted that
   run and reachable only by a tool call. Two pages of this project disagreed
   about its own smallest result for five milestones, and neither
   `platform_retest_lint` nor `deferred_register_lint` could see it, because
   each holds its own page to the tree rather than the two pages to each other.

   **Re-confirmed independently at M686, 2026-09-20**, from a payload rebuilt
   from source rather than reused: a static musl jichi (3,187,984 bytes
   stripped) linked against the cached minimal **mbedTLS** libcurl, `doctor`
   reporting `✓ libcurl available (networking enabled)`. The sweep against a
   real local model (`google/gemma-4-12b`, plain HTTP at 10.0.2.2:1234):

   | ceiling | verdict | jichi's peak RSS |
   |---|---|---|
   | 256 MB | COMPLETE + REAL TURN | 1,672 KB |
   | 192 MB | COMPLETE + REAL TURN | 1,668 KB |
   | 128 MB | COMPLETE + REAL TURN | 1,664 KB |
   | **96 MB** | **COMPLETE + REAL TURN** | **1,668 KB** |
   | 64 MB | PANIC — *"System is deadlocked on memory"* | — |

   The agentic turn reported `TINY-8E8082`, a different nonce from M680's, so
   this is a second observation rather than a re-reading of the first. **Scope,
   narrowly:** plain HTTP, so the TLS handshake path is not exercised; and this
   measures jichi **running** on a machine that size, not building.
3. ~~**Make the tier's `grep -o` extractions portable.**~~ **Done at M681**, and
   it was 11 one-character edits rather than the 30-driver rewrite the register
   predicted — because the recorded mechanism was wrong. See the closed row
   below. One more illumos driver passes; seven remained at the time, and none
   of them shared this cause — **all seven closed at M683**, see 1b above.
4. ~~**Give Cygwin and MSYS2 a rig.**~~ **Done at M698.**
   `scripts/tier-v-cygwin.sh` and `scripts/tier-v-msys2.sh`, sharing
   `scripts/_rig_win.sh`, and **written only after both rows ran by hand** —
   M696 for Cygwin, M697 for MSYS2 — which is the Guix rule this item was held
   open under for four months.

   **They are not shaped like the other rigs, and the reason is in the helper's
   own header.** Every other `tier-v-*` boots a guest and drives it over ssh;
   these two layers are not guests but emulation runtimes installed on the
   machine running the rig, so there is nothing to boot and nothing to reach.
   `_rig_live.sh` had already written the answer down — *"the transport is each
   rig's own business… a rig with a different shape calls the task functions
   directly"* — so they share the **task** (both prompts, the per-run nonce, the
   fixture) and supply a local `sh` as the **transport**. `jc_rig_live` is
   untouched, so `rig_live_commands_lint`'s golden-command comparison still pins
   it byte for byte.

   **What the rigs encode that was previously memory.** Refuse when the other
   installation has live processes (the two runtimes' shared-memory regions
   collide and `fork` starts failing in *both*); refuse a shell that is not the
   row's own `uname -s`; refuse without a measured `--ref-secs`; refuse a model
   outside the free namespace and refuse an empty key, both *before* the first
   request. And they report `pgrep`'s absence loudly rather than inheriting
   `preflight.sh`'s false green — see item 7 below, which these rows are the
   standing proof of.

   **The safety difference this item named is now a check.** MSYS2's `noacl`
   mount makes `chmod` a no-op, and the fix this project documented protects the
   *tests* rather than the *user*: adding `acl` for `/tmp` makes the tier pass,
   because its isolated HOME lives there, while `~/.jichi.env` under `/home`
   stays world-readable. `tier-v-msys2.sh` probes **both** locations and fails
   the row when they diverge, because a green gate over an exposed key file is
   the worst of the four combinations and nothing else in the tree looks for it.
5. ~~**Record trackedness in the run journal**~~ **— done at M684.** The
   `out_of_scope` record keeps `paths` and gains a `tracked` array naming the
   subset git knows about (`jc_git_tracked_paths`, `tests/smoke/journal_trackedness.sh`).
   **The key is absent rather than empty when git cannot answer** — no
   repository, no git on PATH — because "none of these are tracked" and "nobody
   asked" are different facts, and M662's measurement was reversed by exactly
   that kind of conflation. `tests/measure/strict_green_fp.py` prefers the
   recorded answer and falls back to its crude extension-based guess only for
   older journals, printing the two separately and never summing them.

   **The rule itself is still not written, and that is deliberate.** The field
   makes it *writable*; what it does not supply is a corpus. Every journal on
   this machine predates the field, so the 16-of-35 rate and the 85% breakdown
   are still the only numbers, and both came from the guess. The next step is
   runs, not code: once enough post-M684 journals exist, the candidate rule —
   *downgrade only on a TRACKED out-of-scope path* — can be measured against
   the same corpus shape that reversed the last recommendation. Writing it now
   would be the M657 mistake again, a recommendation ahead of its measurement.

6. ~~**Four seams found by driving jichi on somebody else's project**~~ **— two
   closed at M689, two remain.** `doctor` now probes `/v1/models` and says when
   the configured id is not listed (failing open when the listing cannot be
   read), and a shell command that provably changed nothing is reported as such
   instead of as an unattributed change. ~~**Still open:**~~ **the remaining two closed at M692.** The live probe now
   prints the HTTP status as a number and the server's own message; the
   hollow-gate check now reports, in the reach footer's not-checked half, that
   it could not run when the verifier printed no test count. **All four seams
   from that drive are closed.** Original entry:

   **Four seams found by driving jichi on somebody else's project** (zigodot,
   2026-09-20, M683 — the full account with the measurements is
   [`analysis/2026-09-20-driving-jichi-on-zigodot.md`](analysis/2026-09-20-driving-jichi-on-zigodot.md)).
   None is a crash; each is a place where jichi holds the information needed to
   say something better than it says:

   | Seam | Measured | Why it is deferred rather than fixed here |
   |---|---|---|
   | **`doctor` validates the endpoint, not the model id.** `✓ model server reachable` for `jlu/this-model-does-not-exist-9999`, and for the two dead ids zigodot's committed config actually names | the id is absent from `/v1/models`, which jichi **fetches in the very next check** to read limits | The fix is small, but it changes a `✓` to a `✗` on configs that are live elsewhere, and the listing is a gateway's schema rather than the OpenAI API's. It wants a decision about what jichi does when the listing is unavailable — fail open or fail loud — and that is the operator's call. Worth pairing with the `CLAUDE.md` free-namespace rule, which is the same check made by hand |
   | **`doctor --live` discards the server's diagnosis.** Reports `the probe request did not complete (http error, HTTP error)` | the server answered **HTTP 400** with `"You passed in model=hosted_vllm/qwen3-coder-next. There are no healthy deployments for this model."` | Cheap and clearly right; deferred only because it is a message change across every provider's error path, and the right shape is "status + the server's first line, truncated", not "paste the body" |
   | **The envelope cannot tell a reading shell command from a writing one.** `not checked: a shell command ran -- changes it made are not attributed to the run` | fired identically on a run whose 14 shell calls were `grep`/`find`/`ls`, and on one whose single call was `zig fmt` (which rewrites the file) | jichi keeps a snapshot per snapshotted turn, so "a shell command ran and the tree is byte-identical" is a measurement it *could* make. That is the fix worth having, and it is bigger than the warning. Until then the warning is honest but unselective — and it is attached to the one sentence a reader should never skip, which is the argument for doing it |
   | **The hollow-gate detector needs a parseable count, and says nothing when it has none.** | zigodot's `zig build test` prints **nothing** on success; three consecutive run journals recorded no `tests` key, while `zig build test --summary all` recorded `{"tests": 29}` | M86's machinery is right; it is simply disarmed. One line — *"verify passed but printed no test count, so the hollow-gate check could not run"* — would turn a silent blindness into advice. Deferred because "no count" is also the normal state for verifiers that legitimately print nothing, so the note needs a rate limit or it becomes noise on every run |

   **CORRECTED 2026-09-21 — the paragraph that follows was about the wrong
   checkout.** The original text is kept because striking it out silently is the
   habit this register exists to refuse:

   > ~~**What the same session found in the project being driven**, recorded
   > because it is what a second project is for: zigodot's `AGENTS.md` sends the
   > agent to an absolute path under a **different account** than the machine
   > has, and its `zig build test` gate runs **29 of 202** `test` blocks,
   > reaching neither the editor, physics, shader, profiler nor platform
   > subsystems — the physics one having arrived in the second-most-recent
   > commit. jichi reported `verify green` on that, correctly and uselessly.~~

   There were two zigodot checkouts on this machine sharing a root commit, and
   the session surveyed the abandoned one (69 commits, no remote, last touched
   2026-06-30) rather than the live one (416 commits, with an `origin`). The
   abandoned copy sits one directory level shallower, so a glob reaches it
   first. Measured against the **live** repository on 2026-09-21: **109** `.zig`
   files not 81, **558** `test` blocks not 202, of which the gate reaches
   **510** (91%) not 29, and `AGENTS.md` points at
   `/home/<this-account>/development/godotengine/godot` — this account, and a path that
   exists. Its jichi config needed nothing. **The four jichi seams below are
   unaffected**: they are properties of jichi's output, which is the same
   whichever tree it ran in. The stale copy is now archived and deleted.
   Full account: the CORRECTION banner at the top of
   [`analysis/2026-09-20-driving-jichi-on-zigodot.md`](analysis/2026-09-20-driving-jichi-on-zigodot.md). *(jichi handled the dead path well:
   nine tool calls, two errors, and a plain "the directory does not exist"
   rather than an invented function name.)*

7. **Should a capped one-shot exit non-zero?** M687 made the tool-call cap
   visible in the reach footer and in the `[envelope]` verdict, and deliberately
   left the exit code at **0**. M322 chose that, with a reason that is still
   good: the cap is a circuit breaker, not a failure — the history is intact and
   another prompt resumes from it, so returning an error would invite callers to
   discard work that is fine.

   **The counter-argument, which is also good:** that reasoning assumes a *next
   prompt*. A `jichi --no-session -p '…'` one-shot has none. In the run that
   prompted M687 the model looped for 200 tool calls and 4,887,733 tokens,
   produced no final message, and the shell saw `$? == 0`. For that shape the
   work is not "intact pending resumption"; it is gone, and the caller was told
   it succeeded.

   **Why it is deferred rather than done.** It changes a currently-zero exit
   code on a stable-tier contract, which is the same move as the
   `--strict-green` default flip — and that recommendation was **withdrawn at
   M662** precisely for having been made ahead of its measurement. Repeating the
   mistake in a different row would be worse than the defect.

   **CORRECTION (M690): the journal did NOT already record `stop_reason`.**
   The sentence below said it did, and that was read rather than measured.
   Counted on 2026-09-21 over **114 journals and 101 completed runs**, the
   recorded values were `ok`, `running`, `budget_exhausted` and `verify_failed`
   — the *envelope's* outcome, which cannot express `max_iters` at all. A
   capped run wrote `"outcome":"ok"` and was indistinguishable from a clean
   one. So the corpus that was supposed to answer this question could not have,
   and the row was cheaper than it looked only on paper. The field now exists
   (`jc_agent_stop_reason`, recorded in the `end` event), and
   `tests/measure/capped_oneshot.py` asks the question — printing **NOT
   EVIDENCE** below 20 capped runs, because answering from a handful is exactly
   how M662's withdrawn recommendation came about. Every journal on this
   machine predates the field, so the floor is not a formality.

   **SECOND CORRECTION (M713, 2026-09-23): even with `stop_reason`, the journal
   cannot answer the question this row asks.** The question is about
   `--no-session` one-shots and whether they *produced an answer*. The `start`
   event records budget, deadline, tool-call cap, verifier and edit scope —
   nothing that marks a one-shot — and the `end` event records nothing that says
   a final answer was produced. So `capped_oneshot.py` counts every capped
   journal, keeps only the last `start`/`end` pair of a multi-turn file, and
   uses `no_changes` as its "answered" proxy (its own output calls that *"the
   nearest proxy the journal carries"*). Twenty capped runs recorded this way
   would not settle the row. The two fields come first — plan D6 in
   [`plans/2026-09-after-m712.md`](plans/2026-09-after-m712.md) — and the
   corpus after them. Measured on the development bench the same day: 167
   journals, 126 completed runs, **0** carrying `stop_reason`
   ([analysis](analysis/2026-09-23-what-to-build-next.md) §4).
   The corpus pilot added two instances the same day -- capped at 200 tool
   calls, no answer, journal `outcome: ok`, exit 0, at 14.35 M and 6.93 M tokens
   ([pilot](analysis/2026-09-23-the-corpus-pilot.md) §4). Two are an anecdote,
   not the floor's twenty, and are recorded as that.

   **THIRD NOTE (M715, 2026-09-23): the journal can now say both halves.** The
   `start` event carries `one_shot` (headless with `--no-session`: no session to
   resume) and the `end` event `answer_bytes`, the size of the turn's own answer —
   a fact rather than a verdict, because a capped run's last words can be text
   without being an answer. `capped_oneshot.py` reads them: each capped run is a
   one-shot, a session, or `unknown` (a journal older than M715), the sizes are
   printed in buckets, and the `no_changes` proxy is gone. Building the second
   field found that the structured outputs had reported a resumed capped turn's
   *predecessor's* answer as its own; that is fixed. **What is still owed is the
   corpus**: twenty capped `--no-session` runs on an M715+ build. One fact for
   whoever decides: under `--auto` the envelope raises the iteration cap to at
   least 200, whatever `maxToolIters` says (`src/chat/jc_agent.c:4102`; measured
   with `mockmodel`: 200 requests and 200 tool calls, then `max_iters`) — so a
   capped `--auto` one-shot is a 200-call run, never a short one. Over the 36
   journals of the 2026-09-23 drive (M714 build) the script finds 34 completed
   runs and 2 capped, the two pilot runs, both `unknown`.

   *Original sentence, kept because a correction that erases what it corrects
   teaches nothing:* **What would settle it**, and it is cheap: the run journal
   already records `stop_reason`. Count how many completed `--no-session`
   headless runs ended `max_iters` and what fraction of those produced a
   non-empty answer. If a
   capped one-shot almost always answers *something*, the current 0 is defensible
   and this row closes. If it usually answers nothing, the exit code is lying on
   the majority of them and the flip has a number behind it. **A narrower option
   exists if the corpus is ambiguous**: an opt-in flag, or folding the cap into
   `--unattended`'s escalation set (which already holds `privilegedAudit: false`
   and the private-files probe) — an unattended run has nobody to read a footer,
   and its exit code is the whole interface. *(No flag spelling is proposed here
   on purpose: `docs_flags` check 2 refuses a documented flag that does not
   parse, and it caught the first draft of this paragraph inventing one. A name
   in a register is a name somebody tries.)*

**And what I recommend *not* doing yet**, so the list is a judgement rather than
a wish: ~~the **14 `qemu-user` architecture rows** cannot be driven as built —
they link `HAVE_CURL=`, so there is no HTTP in the binary, and
both rigs say so themselves~~ **(done, 2026-09-21 — see the correction below)**;
**macOS**, still blocked on hardware, where no
amount of willingness moves it; the **frontier craft A/B**, superseded by the
standing local-and-free-models rule; a **blind sweep of the remaining bare-`make`
sites**, which is how a portability fix becomes a portability bug; and **notebook
support**, whose 257× token measurement is compelling and whose use case is still
undecided — the honest trigger is somebody saying notebooks *are* the workflow.

> **Correction (M680): the tiny row was wrongly listed there, twice.**
> `HAVE_CURL=` is a property of what those rigs **ship**, not of those systems.
> `tier-v-tiny.sh` has a **`--turn` mode** that carries a NIC and a curl-enabled
> payload, and M430 recorded **a verified model turn at 80 MB** through it. The
> rig requires only that the binary be *static*; whether it has libcurl is the
> operator's choice of `--bin`, and `scripts/minimal-curl.sh` builds exactly
> that — measured at 2,698 KB on disk and **804 KB resident**, *"can make a
> model call? yes"*.
>
> What is actually missing on that row is narrower and is now item 2 of the
> list above: the M430 turn ran against a **mock** model with a one-line probe,
> so the row is not *Driven* in this page's sense. ~~**The qemu-user sweep is a
> different case and the exclusion stands there**: ~20 target triples would each
> need a cross-built libcurl, which is what `zig cc` does not give you.~~ One
> simplification worth recording for whoever tries: the driven task talks to
> `http://127.0.0.1:1234/v1` over a loopback tunnel — **plaintext, no TLS** — so
> a libcurl configured with no TLS backend at all removes the hardest part of
> that cross-compile.

> **Correction (2026-09-21): the exclusion above is retired — all 19 runnable
> triples are Driven.** The simplification the previous paragraph suggested was
> the whole trick, and it was cheaper than the paragraph guessed. `zig cc` does
> not *ship* libcurl; it *builds* one, TLS-free, in about **33 s per triple**.
> The sweep — libcurl, a curl-enabled second binary, and both turns on every
> target — took **13m09s** end to end, and every row reported a phrase minted
> that second. It runs as `scripts/tier-v-arch.sh --drive`, over
> `scripts/minimal-curl.sh --tls none --target <triple>`.
>
> **The premise nobody had checked was the transport.** `qemu-user` has no
> guest: it translates syscalls and hands them to the host kernel, so the
> emulated process shares this machine's loopback and needs **no tunnel at
> all** — the thing every other driven row spends its complexity on. Measured
> before anything was built: 19 of 20 triples opened a socket to the model
> server and read a response, the six MIPS rows included, although `pipe()`
> fails there.
>
> **It also found a live defect in the build, which is what the sweep is for.**
> libcurl types `curl_off_t` as `long long` on every non-LP64 target — the
> stock header, not a hand-built one — so `make WERROR=1` with libcurl fails
> **inside a third-party header on every 32-bit platform**, reachable today on
> a 32-bit Pi and invisible to `make ci` here. The Makefile now probes curl's
> header under the build's own dialect and relaxes exactly one object;
> `portability_lint` holds the scoping, because `src/net/jc_http.c` is the only
> translation unit that includes `<curl/curl.h>`.
>
> **Two traps worth the ink.** `binfmt_misc` makes autoconf believe it is a
> native build — it can execute target binaries — so configure ran its test
> programs under emulation and curl's `getifaddrs` conftest wedged; an explicit
> `--build` fixes it. And a flag change without `make clean` produced a *mixed*
> binary whose `doctor` reported `✓ libcurl available` over the curl-free stub,
> so every driven row now proves the stub string is absent before its turns are
> believed.
>
> **What these rows still do not say:** not a machine (no cache coherency, no
> timing, and RSS measures the emulator), no TLS by construction, and `armeb`
> passing does not retire M542's variadic-double finding, because the shared
> task configures no `temperature`.

---


## Open — the S5 Mermaid remainder (M794)

The Mermaid review converted every reader-facing diagram; two low-value groups are left, as
matters of taste rather than reader dead-ends.

| Deferred | Why | Where |
|---|---|---|
| **Convert the `timeline` diagram and 4 ROADMAP-entry diagrams** | low-value: a timeline is its own record; the rest sit deep in the archive. | `PROJECT_TIMELINE.md`, `ROADMAP.md` |

## Open — found by the docs S3 entry-path review (M782–M788)

The start-here review
([analysis/2026-09-26-docs-s3-entry-path-review.md](analysis/2026-09-26-docs-s3-entry-path-review.md))
fixed every blocker and teaching defect it found across M782–M788. These are the structural
preferences it left, because they are matters of taste that do not make a reader fail; the
report has the detail.

| Deferred | Detail |
|---|---|
| **`INSTALL.md` two-audience ordering** | [details](#def-27) |
| **`SETUP_WIZARD.md` migration history in the body** | [details](#def-28) |
| **`CONFIG_TUTORIAL.md` polish** | [details](#def-29) |

<a id="def-27"></a>
#### INSTALL.md two-audience ordering
**Why:** quick-start reader meets the userland ladder before Install; taste, not a dead end.
**Where:** `docs/INSTALL.md`

<a id="def-28"></a>
#### SETUP_WIZARD.md migration history in the body
**Why:** reads for a maintainer; belongs in an appendix (also `composer` in the axis table only).
**Where:** `docs/SETUP_WIZARD.md`

<a id="def-29"></a>
#### CONFIG_TUTORIAL.md polish
**Why:** `--lite` undefined here; `0a`-before-`0` numbering; §1 long before a first chat.
**Where:** `docs/CONFIG_TUTORIAL.md`


## Open — left by the `/proc` key fix (M770)

The fix is in [HARDENING.md](HARDENING.md) §2, "The parent's own copy". These rows are what it
leaves, on purpose or for lack of a measurement.

| Deferred | Detail |
|---|---|
| **The same protection on the platforms Linux and FreeBSD do not cover.** | [details](#def-30) |
| **The key's other copies:** | [details](#def-31) |
| **A contained run's record pipes still take injected lines.** | [details](#def-32) |

<a id="def-30"></a>
#### The same protection on the platforms Linux and FreeBSD do not…
`PR_SET_DUMPABLE` is Linux's; **FreeBSD is done (M771)** via `procctl(PROC_TRACE_CTL_DISABLE)`; **OpenBSD (M796), NetBSD (M798) and illumos (M799) are done** via an in-place environ scrub -- **partial**: it closes `ps -e` / `/proc/environ` / `pargs -e` but not ptrace, and on a procfs-mounted NetBSD or on illumos not `/proc/<pid>/{fd,mem,as}` either, since none of them has a dumpable or trace-control flag. Left: `ps -e` on **macOS** alone.
**Why:** **FreeBSD measured 2026-09-25; OpenBSD, NetBSD and illumos measured 2026-09-27** ([HARDENING.md](HARDENING.md) §2); only macOS is unmeasured (no rig has ever compiled jichi on it). The platform rules want a measurement per row before any claim. Until then `jichi doctor` warns there (*keys may be readable by other same-user processes*) instead of claiming protection. `tests/smoke/proc_key_hidden.sh` is route-aware: it uses `/proc/<pid>/environ`, `ps -e` or `pargs -e`, and skips only where **none** is known -- reporting *unmeasured*, never *absent* (M770's version skipped green on FreeBSD, where `ps -e` in fact revealed the key; the same trap waited on illumos, where `pargs -e` is the only route). Where a route exists and jichi has no fix, the driver goes red. On each measured platform the scrub was proven by the driver's own witness: with `JICHI_KEEP_DUMPABLE=1` the key returns.
**Where:** `src/util/jc_proc.c`, [PLATFORMS.md](PLATFORMS.md)

<a id="def-31"></a>
#### The key's other copies:
the file it is kept in, and the environment of any other program started from a shell that exported it. A command the model runs is the same user, and can read both.
**Why:** **Not jichi's to close.** No process can hide a file from its owner, or another program's environment from their shared user. The boundary is another user, or a container that mounts no key file ([CONTAINER_TUTORIAL.md](CONTAINER_TUTORIAL.md) §8). It is the shell-sandbox row below, seen from the key's side. `CONFIG_TUTORIAL.md` §1.7 says so.
**Where:** [CONFIG_TUTORIAL.md](CONFIG_TUTORIAL.md) §1.7

<a id="def-32"></a>
#### A contained run's record pipes still take injected lines.
The shell opens the journal and telemetry FIFOs by name and writes a line into each.
**Why:** **Measured after the fix, under both network modes.** M770 closes only the route through jichi's own `/proc/<pid>/fd`. A pipe the run must write to cannot refuse another writer of the same user, so the record is append-only, not closed. The analysis counts any line that is not a jichi event as injected (the contained-drive plan, §8).
**Where:** [plans/2026-09-25-contained-drive.md](plans/2026-09-25-contained-drive.md)


## Open — what the fork-free measurement decides (M769)

From [the measurement](analysis/2026-09-25-a-fork-free-jichi.md). A read, edit and search agent
works without a process, and 271 of 342 smoke drivers pass without one. These are the decisions
it leaves.

| Deferred | Detail |
|---|---|
| **A `jc_proc` backend that starts a program without `fork`** | [details](#def-33) |
| **The parallel pool and the daemon fork jichi's own code.** | [details](#def-34) |
| **A PreToolUse hook that cannot start lets the call through** | [details](#def-35) |
| **Two drivers blame the wrong thing when a child cannot start.** | [details](#def-36) |

<a id="def-33"></a>
#### A jc_proc backend that starts a program without fork
`posix_spawn` for NuttX, `spawn` for DJGPP, an Amiga process launch for AROS. Closed at M832.
**Why:** **Measured:** eight of the ten `fork()` sites fork only to run another program, which is everything but the parallel pool and the daemon, so one backend restores the shell, verifiers, git, grading, MCP stdio, LSP and hooks on three of the four rows. **Closed at M832:** every one of the eight sites (and `jc_proc_popen`, the ninth way) now describes its child in a `jc_spawn_req` and `jc_spawn()` starts it — fork remains the default backend everywhere fork exists, and a **`posix_spawn` backend** (probed at build time, `JICHI_SPAWN_BACKEND=spawn` at run time) starts the same children without fork, with the same rituals expressed parent-side: the pgroup lead, the SIGPIPE reset, and the secret scrub as a parent-built envp. `tests/smoke/spawn_backend.sh` runs the load-bearing fences against both backends of the normal binary on every gate run, each check proven red. DJGPP's synchronous `spawn` and the Amiga launch remain per-row work when those rows are attempted, but they are now one backend function each, not eight call-site rewrites.
**Where:** `src/util/jc_spawn.c` (the two backends), the nine callers

<a id="def-34"></a>
#### The parallel pool and the daemon fork jichi's own code.
Their children run agent loops or turns without `exec`, so no spawn call replaces them.
**Why:** **Measured:** the two sites that do not exec. A port keeps both off, which the measured fork-free subset shows is survivable. Threads or an in-process pool would be a redesign, and nothing measured asks for one.
**Where:** `src/tools/jc_tool_parallel.c`, `src/main.c` (the daemon)

<a id="def-35"></a>
#### A PreToolUse hook that cannot start lets the call through
as `HOOKS.md` documents. Without processes that is every hook, so a hook used as a fence is silently off.
**Why:** Documented behaviour, not a defect. The measurement shows its reach on a port, and M584 already said that *a project with a hook configured believes it is guarded*. The decision is whether a hook may be marked fail-closed, with the call refused when the hook cannot run.
**Where:** `src/chat/jc_hooks.c`, `docs/HOOKS.md`

<a id="def-36"></a>
#### Two drivers blame the wrong thing when a child cannot start.
`readonly_delegate` check 5 reported a read-only parallel task writing a file, but the parent wrote it: the mock is routed by request number, and without a child the child's scripted write reached the parent. `build_rev` failed on the tree's `-dirty` stamp.
**Why:** **Measured, by reading each failure.** Neither is a product defect. A driver that routes by request number assumes the child ran, and could check that it did before it attributes anything.
**Where:** `tests/smoke/readonly_delegate.sh`, `tests/smoke/build_rev.sh`


## Open — found by the GNU/Hurd first row (M767)

Measured on GNU/Hurd (hurd-amd64, Debian, KVM) on 2026-09-25;
[the page](analysis/2026-09-25-gnu-hurd-first-row.md) has the probes. The TUI's freeze, the
row's largest finding, is fixed at M768 and is not here.

| Deferred | Detail |
|---|---|
| **A lint for the drivers that need git.** | [details](#def-37) |
| **M461's unit check reads a group-killed process as alive on the Hurd.** | [details](#def-38) |
| **`turn_scratch` stalls on the Hurd, sometimes.** | [details](#def-39) |

<a id="def-37"></a>
#### A lint for the drivers that need git.
The eight that failed without it -- `hint_record`, `undo_scope`, `attempt_tainted`, `attempt_guard`, `improve_tainted`, `grade_cannot_run`, `verify_stuck_periodic`, `verify_consistency_periodic` -- now carry the `command -v git >/dev/null 2>&1 \|\| t_skip` guard the other thirteen use. Closed at M773: verified against a PATH farm of every tool but git -- each skips with `1..0` where an unguarded copy exited 1. What remains open is the lint.
**Why:** **Measured twice** on the Hurd (M767), re-verified M773. The eight need git *transitively* -- a snapshot-enabling config plus an editing turn, not a direct `git` call -- so the greppable "runs git" universe (20 drivers) is the wrong one and misses six of them. A correct "needs git" lint would read each driver's config and turn, which is not mechanically clean; left as an audit.
**Where:** `tests/smoke/` (the eight, now guarded)

<a id="def-38"></a>
#### M461's unit check reads a group-killed process as alive on the…
`tests/test_proc.c:353` gives a pipeline member 2 s to vanish after `kill(-pgid, SIGKILL)`. On the Hurd the member leaves `ps -A` at once, yet `kill(pid, 0)` answers *exists* for between 45 s and a few minutes.
**Why:** **Measured with two probes.** `kill(pid, 0)` otherwise answers as on Linux (`ESRCH` for a pid that never existed, for a reaped child, and for an exited orphan), so the lease's liveness reading holds there. The fix is a decision: a longer allowance, or asking the process table instead of `kill`. Until then the Hurd's unit suite has one known failure.
**Where:** `tests/test_proc.c` (M461)

<a id="def-39"></a>
#### turn_scratch stalls on the Hurd, sometimes.
Its one TUI turn of 200 tool calls completed in 4 s in two solo runs, and in a third never finished before the driver's wait expired (271 s).
**Why:** **Measured, not diagnosed.** The guest was otherwise idle, so it is not load. It passed in M768's full run of the tier. Linux has never shown it. A lead to try first: a pseudo-terminal whose buffer fills while 200 results are printed.
**Where:** `tests/smoke/turn_scratch.sh`; the Hurd's term server


## Open — found while writing /why (M762)

| Deferred | Detail |
|---|---|
| **A PreToolUse hook's block is not counted in `tool_refusals`.** | [details](#def-40) |

<a id="def-40"></a>
#### A PreToolUse hook's block is not counted in tool_refusals.
Every other gate that refuses a call adds one: the agent loop's ten gates (ten `tool_refusals++` sites in `jc_agent.c`, counted), `jc_tool_execute`'s two, and a tool's own `policy_refusal`. The reach footer prints that count beside the error count, so that fences doing their job are not read as failures (M638). A hook's block is left out of the count, so the footer shows it as an error.
**Why:** From reading, while noting each gate for `/why`. M638's header lists the fences it counts and does not mention hooks, so the omission may have been deliberate: a hook is the user's code, not jichi's fence. That is a decision to state, one way or the other.
**Where:** `src/chat/jc_agent.c`, the PreToolUse block; `include/jc_app.h` (`tool_refusals`)


## Open — found by the `jichi ps` driver (M758)

The crash is fixed (M758). What let it pass the smoke tier is fixed for signals only.

| Deferred | Detail |
|---|---|
| **PTY drivers wait for jichi to exit and never check how it exited.** | [details](#def-41) |

<a id="def-41"></a>
#### PTY drivers wait for jichi to exit and never check how…
`waitexit` records the status and only `assertexit` reads it. Closed at M776.
**Why:** **Measured**: the signal rule turned three drivers red on the unfixed tree (`learner_flow`, `sessions_footprint`, `assignments_stages`: five TUI sessions ending in SIGSEGV). **Closed at M776:** 44 of 63 `waitexit` sites across 26 drivers had no `assertexit`; each now has one with the code that session ends with (all `assertexit 0`, since every one followed a `/exit`). The one exempt site is `no_model_exit.sh`'s exit-7 tooth, which feeds ptydrive an unasserted `waitexit` on purpose. `assertexit_lint.sh` keeps every other `waitexit` read: check 1 flags a missing `assertexit`, check 2 floors the site count at 63 so the universe cannot vanish. Both proven red.
**Where:** `tests/smoke/assertexit_lint.sh`; the 26 drivers


## Open — found while writing TUI_PRINCIPLES.md (M756)

Collecting what five UX documents decided put their sentences side by side. Where they disagree,
the principles page lists the disagreement (its section 7) and settles nothing; each is for
whoever owns the page it is in.

| Deferred | Detail |
|---|---|
| **The UX documents disagree with each other, or with the code, in five places.** | [details](#def-42) |

<a id="def-42"></a>
#### The UX documents disagree with each other, or with the code,…
The accessible reply header (ACCESSIBILITY and the chrome-channel proposal each describe one; the code prints *"Model %s responds with the following:"*, naming the model every time). The spoken approval prompt (three wordings; the catalog matches none). The width to wrap to (76, 78 or 80 columns). What is still to do (the accessibility-by-default proposal lists as remaining the prompt line, M562, the wisdom line, M574, and the ghost suggestion, M578, all shipped). And smaller drift: a digit-grouped example, an "input echo" line for a flag M558 deleted, and the kana question.
**Why:** From reading, each sentence found with `grep -F` in the page it is in, and the header against `src/tui/jc_tui.c`. The code is what runs, so where it settles a question the page is the stale side. Settling them is a decision per page, so the principles page records them instead.
**Where:** [TUI_PRINCIPLES.md](TUI_PRINCIPLES.md) section 7; the five pages it names


## Open — found while writing ALGORITHMS.md (M755)

The page measures four of jichi's algorithms on the tree; two of its measurements, and one
reading of the code on the way, found these.

| Deferred | Detail |
|---|---|
| **The SSE parser does not end a line at a bare CR.** | [details](#def-43) |
| **CLAUDE.md says never call `strcpy`, `strcat` or `gets`, the same as `sprintf`, and only `sprintf` is enforced.** | [details](#def-44) |

<a id="def-43"></a>
#### The SSE parser does not end a line at a bare…
The WHATWG specification allows three line endings: CRLF, LF, and *"a single U+000D CARRIAGE RETURN (CR) character not followed by a U+000A LINE FEED (LF) character"*. `jc_sse_feed()` ended a line only at LF. Closed at M775.
**Why:** **Measured**, by ALGORITHMS.md section 3's program: 0 events. No provider jichi talks to sends bare CRs, so it never bit. **Closed at M775:** `jc_sse_feed` now ends a line at a bare CR too, and one bit of state (`last_was_cr`) swallows the LF of a CRLF split across chunks, so the every-chunk-size property still holds (0 disagreements). The page's program now reads `1 event(s)`, `algorithms_page_lint` check 4 pins it, and `tests/test_sse.c` adds the bare-CR and split-CRLF cases (proven red on the old parser).
**Where:** `src/net/jc_sse.c`

<a id="def-44"></a>
#### CLAUDE.md says never call strcpy, strcat or gets, the same as…
`sprintf_lint.sh`'s universe is `sprintf` alone. `src/` holds nine `strcpy` calls in six files and no `strcat` or `gets` (counted).
**Why:** From reading, while reading `jc_lexical_topn()` for the page. The one there copies between two buffers of the same size, `TOK_MAX`, so it is bounded; the other eight are not checked here. Either the lint grows to cover the rule as written, with each remaining call bounded by construction or replaced, or the rule is reworded to what is enforced.
**Where:** `tests/smoke/sprintf_lint.sh`, `src/index/jc_lexical.c`, `src/json/cJSON.c`, `src/util/jc_auditview.c`, `src/util/jc_jsonrepair.c`, `src/util/jc_rss.c`, `src/util/jc_runsview.c`


## Open — found by the 2026-09-24 overnight drive (M744)

The drive's ten complete arms and six runs of the eleventh, read while it ran
([analysis](analysis/2026-09-25-the-overnight-drive.md)), until a model deleted its data at
00:42:24 (§1 there). Most urgent first.

| Deferred | Detail |
|---|---|
| **A drive's record must be out of the reach of the shell it measures.** | [details](#def-45) |
| **A 0-byte final message ends a run as `done`.** | [details](#def-46) |
| **`spawn_subagent`'s `model` example is a role the config may lack, and `"default"` is refused.** | [details](#def-47) |
| **The `readonly` text does not say that a read-only sub-agent has no shell.** | [details](#def-48) |
| **`describe` omits two always-registered tools** | [details](#def-49) |
| **Strict boolean reads outside `config_bool_lint`'s universe.** | [details](#def-50) |

<a id="def-45"></a>
#### A drive's record must be out of the reach of the…
bonsai-27b's `run_terminal_command` emptied `~/.cache/jichi-corpus/`, both nights' data, from a workspace inside it.
**Why:** **Measured, and my plan's mistake**, not a jichi defect: jichi says the shell is unfenced (AUTONOMY.md; the shell-sandbox row below). The rule for every next drive: run the models where their shell cannot write the drive's data (a container, a VM, or at least another user); push each run's record off the machine as it ends (the pushed branches are what survived); and name every cap a measurement can hit, which the plan did not do for `parallelTaskTimeout` (300 s; it cut 2 of 3 children in one call). Pushing records to a forge is outward-facing and the operator's call.
**Where:** `docs/plans/`, `scripts/corpus-drive.sh`

<a id="def-46"></a>
#### A 0-byte final message ends a run as done.
Ten gemma and gpt-oss runs of 48 ended with an empty answer and no change; eight were edit tasks. The gate passed on the untouched tree, and `runs.tsv` said `rc=0`.
**Why:** **Measured.** The M731 row on a statement of intent stayed open because deciding needs a guess at prose; zero bytes needs none. The options are one nudge ("your last message was empty"), a stop reason of its own, or both. It is a loop change, and a decision.
**Where:** `src/chat/jc_agent.c`

<a id="def-47"></a>
#### spawn_subagent's model example is a role the config may lack, and…
One parent passed `"summarize"`, the schema's own example, and another `"default"`, the word its description uses for the current model. Both got a bare `no model matches`, and both retried without the argument.
**Why:** **Measured**, 2 spawns in 19 delegated runs. The refusal names no choices, so the next try is a guess too. **Closed at M745:** `default` and `current` are the current model, a real role no configured model declares runs on the current model with a `[delegate]` note saying so, and a selector that matches nothing is refused with the configured models listed. `subagent_model_select.sh` checks all three through the real tool.
**Where:** `src/tools/jc_tool_subagent.c`, [SUBAGENTS.md](SUBAGENTS.md)

<a id="def-48"></a>
#### The readonly text does not say that a read-only sub-agent has…
A parent set `readonly: true` and then briefed two children to run `zig build test`, `git show` and a Python heredoc. Both said they had no shell.
**Why:** **Measured.** The children behaved well. What was missing was a sentence in the schema the parent reads. **Closed at M745:** both schemas now say a read-only child is *offered* no shell and no editing tools. They say *offered* on purpose, because whether an unoffered tool is also refused is the open question at the top of the M740 section, which M745 does not touch.
**Where:** `src/tools/jc_tool_subagent.c`, `src/tools/jc_tool_parallel.c`

<a id="def-49"></a>
#### describe omits two always-registered tools
`read_background_output` and `kill_background`. They are registered by `jc_tool_register_configured` whatever the config, and are in neither `tools` nor `conditional_tools`. Closed at M774.
**Why:** From reading, while M744 gave the conditional rows their flags. **Closed at M774:** `run_describe` now registers both into the same registry it builds the always-on `tools` array from, so they carry their real metadata rather than a hardcoded copy. `describe --output json` lists 19 tools; `describe_names_lint` stays green.
**Where:** `src/main.c` (`run_describe`)

<a id="def-50"></a>
#### Strict boolean reads outside config_bool_lint's universe.
The lint looks for `strcmp(…, "true")` and callers of the strict accessor. `cJSON_IsTrue` on a config item or a model's argument is a third spelling: `spawn_parallel`'s `write`, and config keys such as `control`.
**Why:** From reading, while preparing the `spawn_subagent` fix. The M534 lesson again: the lint's universe was the function, not the question.
**Where:** `tests/smoke/config_bool_lint.sh`, `src/config/jc_config.c`, `src/tools/jc_tool_parallel.c`


## Open — found by reading the scanner again (M743)

M743 measured the constraint scanner over the 656 distinct messages of threadwork's session
store and both drives' tasks, after the 2026-09-24 drive's ten complete arms journalled no advisory in 240 runs
([analysis](analysis/2026-09-25-the-scanner-read-again.md)). It fixed seven of the nine
mechanisms it found. What it left:

| Deferred | Detail |
|---|---|
| **"avoid X and build Y" bans the build.** | [details](#def-51) |
| **A third-person *never* is read as a policy.** | [details](#def-52) |
| **"Do not modify the code." no longer infers read-only.** | [details](#def-53) |
| **A comma does not end a negation.** | [details](#def-54) |
| **The cross-check on the workstation's store has not been run** | [details](#def-55) |

<a id="def-51"></a>
#### "avoid X and build Y" bans the build.
*"Optimize the implementation to avoid the intermediate `ArrayList` and build the final string directly."* infers *do not run build commands*. `in_verb_position` treats *and* as joining another banned verb, which is right for *"never commit or push"*.
**Why:** **Measured**, 1 rule in 656 messages. Telling the two apart needs the coordinated verb's own clause, not a word list; a guard for the one sentence would be the M167/M168/M207 pattern again.
**Where:** `src/chat/jc_constraint.c` (`in_verb_position`)

<a id="def-52"></a>
#### A third-person never is read as a policy.
*"the run-gd harness never installs a ClassDB"* infers *do not install packages*. The scanner keeps third-person *never* on purpose, because *"we never push to master"* is a policy worth honouring.
**Why:** **Measured**, 1 rule. The difference is the subject: a thing (*the harness*) or the team the agent works for (*we*).
**Where:** `src/chat/jc_constraint.c` (the `c <= 2` rule)

<a id="def-53"></a>
#### "Do not modify the code." no longer infers read-only.
M743 made *code*, *source* and *src* a kind of file, and a kind a scope, because *"Do NOT edit any source; your only file output is the design doc"* wrote a file under an inferred read-only.
**Why:** A deliberate trade, in M207's safe direction. A brief that means it now gets no backstop, though its sentence is still in the prompt. **Revisit** if the workstation's store shows this phrasing meaning read-only more often than not.
**Where:** `src/chat/jc_constraint.c` (`EDIT_GLOBAL_OBJ`), [DECISIONS](DECISIONS.md)

<a id="def-54"></a>
#### A comma does not end a negation.
*"Do not build, just run the tests"* still bans the tests.
**Why:** From reading; no corpus sentence did it. A comma also separates the items of one real ban (*"do not build, test, or commit"*), so the cut needs the word after it, not the comma.
**Where:** `src/chat/jc_constraint.c` (`clause_len`)

<a id="def-55"></a>
#### The cross-check on the workstation's store has not been run
and the 124 Continue-format messages were not searched for misses, only for what was inferred.
**Why:** The measured population is one machine's, mostly agent-written briefs. `tests/measure/scanner_corpus.py ./jichi --against OLD` is the whole procedure.
**Where:** `tests/measure/scanner_corpus.py`


## Open — found by the research for the operator's twelve questions (M740)

On 2026-09-24 the operator asked twelve questions (reporting a platform, an algorithms course,
small language models, more platforms, JupyterHub, SQLite, CHR, UX, TUI commands, file-based
cooperation, multi-stage prompting, use cases). Six read-only research passes over the tree
answered what exists; along the way they found the defects below. **Measured** means run on
threadwork that day; **from reading** means found in the code and not yet reproduced — the
first thing the fix does is reproduce it. M740 fixed what it measured (`db-report.sh`, the
docs that promised too much); the rest is here, most urgent first.

| Deferred | Detail |
|---|---|
| **Is a `readonly` subagent read-only when it asks for a write anyway?** | [details](#def-56) |
| **`builtin_cmds_lint` checks less than it says, and `/design` means two things.** | [details](#def-57) |
| **`brief-check` still says inferred constraints "will be ENFORCED".** | [details](#def-58) |
| **An MCP server's `readOnlyHint` skips the approval prompt** | [details](#def-59) |
| **`JICHI_ARG_*` is cut at 1023 bytes, silently.** | [details](#def-60) |
| **`examples/config.web-search.json` cannot see its key.** | [details](#def-61) |
| **The postgres branch of `db-report.sh` is believed broken.** | [details](#def-62) |
| **A write by a user tool or an MCP server is misreported by `--revert-out-of-scope`** | [details](#def-63) |
| **Under `toolProfile: core` the skills catalog is still in the system prompt** | [details](#def-64) |
| **`/accessible` is missing from `/help`** | [details](#def-65) |
| **`dream_meta_cmp` ignores ties.** | [details](#def-66) |
| **Two processes lose each other's board updates** | [details](#def-67) |
| **A model-started `jichi -p` gets no API key** | [details](#def-68) |
| **The `platform-test` scaffold pack** | [details](#def-69) |
| **`examples/sqlite/` has no automated test.** | [details](#def-70) |
| **Stale sentences found, not yet corrected:** | [details](#def-71) |

<a id="def-56"></a>
#### Is a readonly subagent read-only when it asks for a write…
Two readings disagree. One: `spawn_subagent readonly:true` only removes mutating tools from the child's menu, and the execution-time gate checks `app->readonly`, which the tool never sets — so a child that names `write_file` regardless would get it. The other: the gate at `jc_tool.c` refuses non-read-only tools for every read-only agent.
**Why:** From reading, contradictory. **Settle it before anything else here**: a scripted mockmodel child with `readonly: true` that calls `write_file`; the file must not appear. A fence that holds only while the model behaves is not a fence (the M530 lesson). **Closed at M750 -- the first reading was right, and it was three callers.** Reproduced with scripted children: a `spawn_subagent` child with `readonly: true` and a `spawn_parallel` task without `"write"` each wrote the guarded file, and no refusal reached the model. The parallel task has no worktree, so its write landed on the live tree. `ask_for_help`'s helper had the same gap. `jc_agent_run_subagent` now sets `app->readonly` whenever the child's menu withholds the mutating tools, and restores it after. `readonly_delegate.sh` checks 2, 3, 5 and 6 fail without it.
**Where:** `src/tools/jc_tool_subagent.c`, `src/tools/jc_tool.c`; `src/chat/jc_agent.c` (the fix)

<a id="def-57"></a>
#### builtin_cmds_lint checks less than it says, and /design means two things.
The lint scrapes `cmds\[\] = {`, which has matched nothing since the table became `TUI_CMDS[]`; its floor is met by the `strcmp` names alone. Behind it: `doctor`'s shadow warning lacks `design` and `predict`, so it cannot say that the built-in `/design` hides the sdlc pack's scaffolded `design.md` in the TUI — which headless `-p "/design …"` does expand.
**Why:** From reading. The same command, two meanings, and the check that should say so is vacuous — the M738 shape again (a check whose universe had shrunk). **Closed at M751, and the rewritten lint found more.** It confirmed all of this before the fix, and added two findings: `/help` presented bare `/learn` as built in, and omitted `/accessible`. The sdlc pack's command is now `/design-doc`. `design` and `predict` are in the registry. The lint's five checks each read their own universe, and `pack_command_shadow.sh` runs doctor's warning, which no test had run.
**Where:** `tests/smoke/builtin_cmds_lint.sh`, `src/util/jc_assetval.c`, `src/tui/jc_tui.c`

<a id="def-58"></a>
#### brief-check still says inferred constraints "will be ENFORCED".
Since M734 they only advise.
**Why:** From reading; the stale wording is M734's own. **Closed at M743**, and it was two places: `brief-check` said the rules *"will be ENFORCED for the run"* and `constraints scan` said *"Each is enforced for the whole run: a matching tool call is refused"*. Both now say ADVISORY; `brief_check.sh` check 9 fails on the old wording.
**Where:** `src/main.c`, `brief-check`

<a id="def-59"></a>
#### An MCP server's readOnlyHint skips the approval prompt
and makes the tool usable in plan mode. Undocumented, and `MCP.md` says the default is *ask*.
**Why:** From reading. A server you did not write can declare a destructive tool read-only. Either document it as a trust decision, or require `autoApprove` as well.
**Where:** `src/mcp/jc_mcp_proto.c`

<a id="def-60"></a>
#### JICHI_ARG_ is cut at 1023 bytes, silently.
**Why:** **Measured** (a 1,236-byte SQL query, cut, still ran). Documented at M740 in `USER_TOOLS.md` and `SQLITE.md` §4.1; the code still cuts. Allocate the value, or refuse to pass a cut one.
**Where:** `src/tools/jc_tool_user.c`

<a id="def-61"></a>
#### examples/config.web-search.json cannot see its key.
It relies on an inherited `TAVILY_API_KEY`, which is on the list jichi scrubs from a tool's environment before it runs.
**Why:** From reading. The fix is the tool's own `env`, or reading a key file — and the doc that recommends the inherited variable.
**Where:** `examples/config.web-search.json`, `docs/USER_TOOLS.md`, `src/util/jc_proc.c`

<a id="def-62"></a>
#### The postgres branch of db-report.sh is believed broken.
It passes its values after `--` as if psql bound them to `$1`/`$2`; psql does not.
**Why:** From reading; psql is not installed here, so untested.
**Where:** `examples/autonomous-loop/db-report.sh`

<a id="def-63"></a>
#### A write by a user tool or an MCP server is…
as "not reverted -- this run ran no shell command": only `run_terminal_command` and `run_tests` set the flag.
**Why:** From reading.
**Where:** `src/chat/jc_agent.c`

<a id="def-64"></a>
#### Under toolProfile: core the skills catalog is still in the system…
but `load_skill` is not offered — the model is told to call a tool it does not have.
**Why:** From reading.
**Where:** `src/chat/jc_sysmsg.c`, `src/tools/jc_tool.c`

<a id="def-65"></a>
#### /accessible is missing from /help
`/help` also lists `/learn` as if it were built in (it is a scaffolded command). Esc does not interrupt a running turn, and nothing says so.
**Why:** From reading. **Two of the three closed at M751**, and each is held by `builtin_cmds_lint` (checks 3 and 4). **The third closed at M753**: Esc stops a running turn, as Ctrl-C does, and says so; `esc_interrupt.sh` holds it.
**Where:** `src/tui/jc_tui.c`

<a id="def-66"></a>
#### dream_meta_cmp ignores ties.
Three `qsort` calls use it to choose which dream files retention removes; equal mtimes (second granularity) give an unspecified order. `DATA_STRUCTURES.md` §5 now uses it as an exercise.
**Why:** From reading; the session comparator had the same defect and was fixed at M198.
**Where:** `src/main.c`

<a id="def-67"></a>
#### Two processes lose each other's board updates
(no reload before save), and **a lease is taken read-then-write**, so two runs starting together could both take it. No command lists live runs, leases or control sockets.
**Why:** From reading. **The last sentence closed at M759**: `jichi ps` lists the live runs with their leases, sockets and journals, and `ps.sh` holds it. **The lease closed at M760**, reproduced first with a FAULT pause (two runs under `--lease fail` both ran): the take and the release now hold a short `fcntl` lock, and `lease_race.sh` holds it. **The board closed at M761**, reproduced first (a run overwrote the card a shell added during it; two edits at once kept one): every edit reloads the board under a lock, and `board_concurrent.sh` holds it. **All three are closed.**
**Where:** `src/chat/jc_board.c`, `src/util/jc_lease.c`

<a id="def-68"></a>
#### A model-started jichi -p gets no API key
model-issued commands run with the keys scrubbed, and `SUPERVISOR_OF_MANY.md` does not say so.
**Why:** From reading; the behaviour is right (M724), the page is incomplete.
**Where:** `docs/SUPERVISOR_OF_MANY.md`

<a id="def-69"></a>
#### The platform-test scaffold pack
hard-codes a reference of "your seconds / 6.66", which `PLATFORM_TESTING.md` says never to do, and has no step for sending the result.
**Why:** From reading.
**Where:** `src/scaffold/jc_scaffold.c`

<a id="def-70"></a>
#### examples/sqlite/ has no automated test.
The smoke tier is Python-free by design and the MCP server is Python; the example was run by hand on 2026-09-24.
**Why:** Measured by hand; nothing re-runs it. A driver that skips without `python3` would do.
**Where:** `examples/sqlite/`

<a id="def-71"></a>
#### Stale sentences found, not yet corrected:
`INSTALL.md` "Never compiled: macOS alone" (FreeMiNT is too); `BUILD.md` lists `sigaction`, which jichi does not call (`signal()`, 0 `sigaction` sites — counted); `INTERFACE_TUTORIAL.md` cites `BIBLIOGRAPHY.md` §6 for interfaces (it is §12); `PYTHON_AND_C.md` says no graded Python course will be added (one shipped at M674); `PLATFORMS.md` calls the Pi Zero the smallest machine to run the loop (the 160 MB VM and the 96 MB tiny rig are smaller); `.github/workflows/ci.yml` says the forge is undecided; `SUBAGENTS.md` describes the pre-M437 report; `MODELS.md` says `--model` accepts a role (it does not); the UNO Q's Driven *no* against `LOW_MEMORY.md`'s M282 live turn.
**Why:** From reading, except the `sigaction` count. Each is a sentence; each needs its fact re-checked, not copied from this row.
**Where:** the pages named


## Open — found while preparing the 2026-09-24 drive (M739)

Both are in `scripts/corpus-drive.sh`, whose copy was pinned for the drive; they wait until it has finished.

| Deferred | Detail |
|---|---|
| **The MANIFEST's `branches:` line repeats the prefix.** | [details](#def-72) |
| **`--dry-run` is not dry.** | [details](#def-73) |

<a id="def-72"></a>
#### The MANIFEST's branches: line repeats the prefix.
A drive with `--branch drive/2026-09-24` records `branches: drive/2026-09-24/TASK/qwen-subdrive/2026-09-24`.
**Why:** `${BRPFX:+$BRPFX/TASK/$ARM}${BRPFX:-(none)}` -- the second expansion yields the prefix itself when it is set, not an empty string. Cosmetic: the branch names are built elsewhere and are right, and `runs.tsv`'s `branch=` column is the record a reader should use.
**Where:** `scripts/corpus-drive.sh`, the MANIFEST block

<a id="def-73"></a>
#### --dry-run is not dry.
Before a dry run reaches its print-and-continue branch it has appended to `out/<arm>/MANIFEST` and run `git reset --hard` + `git clean` in the workspace, once per task.
**Why:** Harmless on a fresh clone at the base, which is what the drive's workspaces are -- and a dry run pointed at a real drive directory leaves a MANIFEST entry for a run that never happened. Found by reading the code before dry-running tonight's arm, which was then pointed at scratch paths instead.
**Where:** `scripts/corpus-drive.sh`, the task loop


## Open — found by FreeMiNT step 4 (M737)

The first run of the driven task from inside the FreeMiNT guest found one thing that is jichi's.

| Deferred | Detail |
|---|---|
| **On FreeMiNT under ARAnyM, every boot produces the same session id.** | [details](#def-74) |

<a id="def-74"></a>
#### On FreeMiNT under ARAnyM, every boot produces the same session id.
Three boots of the step-4 guest, three runs of jichi, one id: `03bead13-8b3c-4ed2-b330-0ff64c680346`.
**Why:** `jc_uuid` reads `/dev/urandom` and, where there is none, seeds `rand()` from `time()` and an address -- and this guest has no `/dev/urandom`, boots with the same clock, and loads the program at the same address, so the seed is the same every time. Session ids are local to one machine, so nothing collided here; but a journal's `run` id comes from the same source, and the measurement scripts join on it, so runs from two boots of such a guest would merge. The fix wants an entropy source the platform has, and MiNT's is not established: `/dev/random` is not in this image either. Recorded, not guessed at.
**Where:** `src/util/jc_uuid.c`; [plans/2026-09-freemint-aranym.md](plans/2026-09-freemint-aranym.md) §11


## Open — found by the userland ladder and the V2f re-run (M736)

Found while measuring the floor INSTALL.md promised; each is recorded rather than built because it is bigger than the fix that found it, or needs something threadwork lacks.

| Deferred | Detail |
|---|---|
| **The C89 fallback formatter has no width, precision or flags.** | [details](#def-75) |
| **The kernel floor is not measured.** | [details](#def-76) |
| **CentOS 5 cannot be provisioned by the ladder.** | [details](#def-77) |

<a id="def-75"></a>
#### The C89 fallback formatter has no width, precision or flags.
`jc_vsnprintf`'s fallback, used where the probe finds no C99 `vsnprintf`, handles only bare `%d %u %x %s %c %f` and their `l` forms, while the tree formats with `%.0f`, `%*d`, `%-10s`, `%02d` and more. On Debian 5 the probe misjudged, the fallback was selected, and the unit suite failed 81 checks, every one formatted output -- `jc_group_num` printed `%..0f` for a number.
**Why:** M736 fixed the probe, which was how that system reached the fallback; no measured platform takes it now. What stays open is the fallback itself: either a real width/precision/flags implementation with the unit suite run against it in the gate (today `make ci` only *compiles* it), or a build that refuses to proceed without `vsnprintf` unless told to. Both are larger than this milestone, and choosing between them is a question of which platforms the fallback exists for, which nothing records.
**Where:** `src/platform/jc_snprintf.c`; ROADMAP M736

<a id="def-76"></a>
#### The kernel floor is not measured.
The userland ladder runs each distribution's gcc, glibc and libcurl in a container on the development box's kernel, so it answers the userland question only. INSTALL.md argues the kernel floor from the calls jichi makes (`fork`, `exec`, `pipe`, `select`, `termios`, AF_UNIX; nothing newer), which is reasoning, not a run.
**Why:** The rig shape exists: `tier-v-vm.sh` boots a distribution's own kernel, and a static jichi as the only userland of a series of older kernels under QEMU is the ladder's second half. Planned, not started: it needs old kernel images this machine does not have, and the drive holds the machine tonight.
**Where:** `scripts/tier-v-ladder.sh`

<a id="def-77"></a>
#### CentOS 5 cannot be provisioned by the ladder.
Its yum cannot reach `vault.centos.org` (the vault redirects HTTP to a TLS its client predates), so the rung never gets a compiler.
**Why:** Below the libcurl floor anyway (7.15.5, like Debian 4, which does provision and does not build); recorded so the missing rung is not read as a pass. An RPM set fetched on the host and copied in would provision it.
**Where:** `scripts/tier-v-ladder.sh`


## Closed at M736 — the oldest-toolchain re-runs

The re-run the M722 section asked for, done on 2026-09-24 -- the row is kept as written, with the closure appended.

| Deferred | Detail |
|---|---|
| **Re-run the two oldest-toolchain rows: V2a (CentOS 7, gcc 4.8.5) and V2f (Debian 9, gcc 6).** | [details](#def-78) |

<a id="def-78"></a>
#### Re-run the two oldest-toolchain rows: V2a (CentOS 7, gcc 4.8.5) and…
**Why:** Both were recorded green at M265 and M273. From M472 to M722 neither could have compiled a file, and since M722 both are *expected* to build again, which is an inference from gcc 4.6.4 and from a wrapper that rejects `-Walloca` as gcc 6 does, not a re-run. About 450 milestones separate M273 from today, so a re-run may find other old-platform gaps that the `-Walloca` error was hiding, the way it hid MiNT's four. `scripts/tier-v-vm.sh` still names V2f's image; no image for V2a is cached here. **Closed at M736 -- re-run, and the row's prediction held.** V2f ran in its VM three times on 2026-09-24: the unit suite built and passed at once, and the smoke tier stopped three times on the tests' own assumptions -- `git switch` (git 2.23) in a fixture, `/usr/bin/grep` on a system before the merged `/usr`, and a `<sys/filio.h>` the STREAMS probe asked for on glibc 2.24 -- and went green (326 drivers) once each was fixed; both live turns passed. V2a's platform ran as a rung of the new userland ladder: CentOS 7, 0 failures. And the ladder found what the `-Walloca` error had been hiding one floor lower: CentOS 6 and Debian 7, the floor INSTALL.md named, did not compile `jc_http.c` at all.
**Where:** [LOW_MEMORY.md](LOW_MEMORY.md); ROADMAP M736


## Closed at M734 — inferred constraints advise, and a negation ends at its sentence

The operator chose both of D2's remedies on 2026-09-24, with M730's measurement in hand. The row is kept as it was written, with the closure appended.

| Deferred | Detail |
|---|---|
| **A refusal by an INFERRED constraint does not say it was inferred.** | [details](#def-79) |

<a id="def-79"></a>
#### A refusal by an INFERRED constraint does not say it was…
Auto-adopt is on in `--auto`; `1d31473d` read *"do not compile"* in a description as an order, and the run refused its own builds for 21 minutes and 2,215,762 tokens before being rolled back. It is the **third** misparse class of the heuristic, after M168 (descriptive read as imperative) and M207 (one file's prohibition read as run-wide), and each cost a whole drive.
**Why:** The mechanical half is small; the **policy** is not. Whether an inferred constraint stays binding in `--auto`, becomes self-explaining, or becomes advisory changes what an unattended run refuses, and that is the operator's call. The plan recommends self-explaining plus a separate count, and measuring the scanner's false-positive rate offline before any further phrase guard. **Measured at M730:** over 289 stored prompts the scanner inferred nine constraints, and six were wrong. Each wrong one forbade the gate its task named, and all six came from a negation joined to a word in the NEXT sentence ("never runs. Wire ... into build"). Scanned per sentence, the three right ones survive and the six wrong ones vanish. The choice is now between sentence-scoped negation with binding constraints, advisory constraints, or both. **Closed at M734, by the operator's option (c), both remedies:** a negation's scope now ends at its sentence, which on M730's sample keeps the three right constraints and drops the six wrong ones; and an inferred constraint *advises* -- rendered in its own prompt section as a guess, noted in the result of the first call per rule per turn that goes against it, and journalled as `constraint_advisory` for every such call (`advised=N` in `runs`) -- while an authored one refuses exactly as before. A refusal by an inferred rule can no longer happen, so the row's question -- does it say it was inferred -- is answered by removing the refusal and saying so in the advice.
**Where:** plan D2; analysis §3.2; [CONSTRAINTS.md](CONSTRAINTS.md)


## Closed at M732 — the journal's stop, LM Studio's overflow wording, grep's warning

Two of the three recording defects the corpus drive found by reading (M731), each fixed check first; the rows are kept as they were written, with the closure appended. The third -- an overflow recorded as an answer -- had its signature fixed in the same milestone and stays open, narrowed, in the section below.

| Deferred | Detail |
|---|---|
| **The journal records an interrupted run as `done`, with `outcome: running`.** | [details](#def-80) |
| **`search_code` drops grep's warning about a malformed pattern.** | [details](#def-81) |

<a id="def-80"></a>
#### The journal records an interrupted run as done, with outcome: running.
bonsai 16 was interrupted by the drive's wall-clock limit: its output stream says `"aborted":true, "stop_reason":"interrupted"`, its journal's `end` says `"stop_reason":"done"`. The end event passes `JC_OK` to `jc_agent_stop_reason`, on the stated assumption that reaching that line *"means the loop returned normally"* -- which an interrupt does. The journal is what the measurement scripts read, so interrupted runs join the `done` population silently.
**Why:** A recording defect, found by reading one run; the fix belongs in its own milestone, check first: a `mockmodel` run interrupted with SIGINT under `--auto --journal` must journal `interrupted`, and the envelope's outcome must not be left `running`.<br>**CLOSED AT M732.** The end event passes the loop's real status to the classifier, and the comment that said an interrupt and a transport error never reach it now says both do. `tests/smoke/journal_stop_reason.sh`: an answering run journals `done`, an interrupted one `interrupted` (exit 130), a failed model call `error`, and the interrupted run's stream and journal agree.
**Where:** `src/chat/jc_agent.c`

<a id="def-81"></a>
#### search_code drops grep's warning about a malformed pattern.
On the no-match path grep's standard error is discarded (so a stray warning never reads as a match) and the pattern is probed only when grep *fails* (exit 2). A pattern grep accepts with a warning -- `(?:…)` gives *"? at start of expression"* -- exits 1 and comes back as a bare `(no matches)`. bonsai 08 repeated such a search thirteen times.
**Why:** Small, and M714's own probe is the fix: on the no-match path, ask grep about the pattern against `/dev/null` and pass on any warning. Deferred only to keep the analysis milestone free of product code.<br>**CLOSED AT M732.** The failure-path probe became `pattern_probe()`, returning grep's exit and its first line; on the no-match path an exit-1 line is a warning, and the result now says `(no matches)` followed by grep's own words and what grep -E does not do. The unit check asks this platform's grep first, so it is two-sided.
**Where:** `src/tools/jc_tool_search.c`


## Open — found by the corpus drive (M731)

Forty-eight headless runs on zigodot, two free models, read run by run:
[`analysis/2026-09-24-the-corpus-drive.md`](analysis/2026-09-24-the-corpus-drive.md).
M731 recorded and did not fix these; each names what reproduces it.

| Deferred | Detail |
|---|---|
| **A context overflow the server returns as content becomes the run's answer, marked `done` and `ok` -- and here it followed a request 14 % over the declared window.** | [details](#def-82) |
| **An interrupted or failed run's journal says `outcome: running`.** | [details](#def-83) |
| **A capped run that wanders is invisible to a repeat detector.** | [details](#def-84) |
| **An edit task can end `done` on a statement of intent.** | [details](#def-85) |

<a id="def-82"></a>
#### A context overflow the server returns as content becomes the run's…
bonsai 18: after two full reads of `codegen.zig` the request was **74,866 tokens** by LM Studio's count against the **65,536** the config declared; LM Studio sent the error as content, and the run ended with it as its answer. **Narrowed at M732:** `jc_text_is_context_overflow` now recognises LM Studio's wording, so the M73 hint fires.
**Why:** Two parts remain: the record (an overflow is not an answer, but M73 deliberately keeps stdout raw -- a decision to revisit, not a line to change), and **why jichi's estimate let the request through**, which is not established.
**Where:** `src/util/jc_cli.c`; [drive](analysis/2026-09-24-the-corpus-drive.md) §2, §9

<a id="def-83"></a>
#### An interrupted or failed run's journal says outcome: running.
Since M732 the journal's `stop_reason` is right for every ending (`interrupted`, `error`), but the envelope's outcome vocabulary has no terminal value for either, so `running` -- which `jichi runs` reads as *the journal has no end event* -- stays in an end event.
**Why:** A new outcome value changes the envelope's vocabulary, `jichi runs` and the exit-code mapping together; a decision of its own, not a fix. `stop_reason` now carries the fact.
**Where:** `src/chat/jc_envelope.c` (`jc_env_outcome_name`)

<a id="def-84"></a>
#### A capped run that wanders is invisible to a repeat detector.
bonsai 12 made 202 calls to the cap without making one call four times -- read, search, `sed`, build, read again, each step a little different. D1 as designed cannot see it.
**Why:** A different signal is needed (re-reading unchanged files, or a budget), and it is a design of its own; D1 ships for the shape it can see. **Measured again at M744, with the note live:** bonsai's `parser-constant` made 22 distinct regex searches that each reached three identical calls. Each was told at its third, and the model moved to a variant differing by one alternation. The note stopped the exact repeat, and the spiral went on until the cap.
**Where:** [drive](analysis/2026-09-24-the-corpus-drive.md) §5; [overnight drive](analysis/2026-09-25-the-overnight-drive.md) §3

<a id="def-85"></a>
#### An edit task can end done on a statement of intent.
bonsai 14's last message was *"Let me search more specifically for patterns that look like loop bounds or limits:"* with no tool call, so the loop took it as the final answer: empty diff, no verification, exit 0.
**Why:** Recorded as a shape, not a defect yet: a message without a tool call *is* the protocol's final answer, and guessing at prose ("Let me …") is the heuristic the constraint scanner's history warns against. The journal's `no_changes` already marks the run. **The narrower case needs no guess:** a 0-byte message, ten times on the 2026-09-24 drive, has its own row under M744.
**Where:** [drive](analysis/2026-09-24-the-corpus-drive.md) §2


## Open — found by the corpus pilot (M714)

Six runs on zigodot, [`analysis/2026-09-23-the-corpus-pilot.md`](analysis/2026-09-23-the-corpus-pilot.md).
The pilot's headline finding -- `search_code` reading patterns as basic regex -- was
fixed in the same milestone; these two were measured and deliberately not.

| Deferred | Detail |
|---|---|
| **A green verifier with no parseable test count cannot see a weakened test file.** | [details](#def-86) |

<a id="def-86"></a>
#### A green verifier with no parseable test count cannot see a…
A local model's edit task added two tests to `src/gdscript/tokenizer_test.zig` and, in the same diff, removed an existing `message != null` assertion (3 -> 2) and eight lines of the notes that adjudicate the tests against the Godot reference; the gate went green and jichi recorded `sanity: no_count`. zigodot's `gate-lint` prints a count (*"529 test blocks run"*) in a form jichi does not read.
**Why:** **One run, one model**, and both remedies are narrow: teach the count parser one project's line, or change the project's gate. Neither is a jichi design question yet. **Revisit when** the full drive's diffs are read and a second instance appears -- they are saved per run, which is what makes that reading cheap.<br>**Revisited at M731 (2026-09-24): the second instance appeared, and it is sharper.** qwen's task 12 asked for *a test that pins down* division by zero; the final diff is one unused `DivisionByZero` in an error set, no test, and the gate went green. Across the drive **all 14 edit verifications that ran reported `sanity: no_count`** -- jichi read no count from any of them. So this is now a design question with two cases behind it (a test weakened, a test never written), still not decided: a gate that declares its count format, or a recognised pattern for lines like `gate_lint: N test blocks run`.
**Where:** [pilot](analysis/2026-09-23-the-corpus-pilot.md) §5; [drive](analysis/2026-09-24-the-corpus-drive.md) §8


## Open — found by measuring M725 (2026-09-24)

| Deferred | Detail |
|---|---|
| **What an index build's ~130 MB of resident memory is.** | [details](#def-87) |

<a id="def-87"></a>
#### What an index build's 130 MB of resident memory is.
**Why:** Measured over this tree (2,072 files, 13,297 chunks, 768 dimensions): 130,692 kB peak RSS with the JSON parse fixed, against 136,080 kB without, so the parse was never most of it. On a 199-file corpus massif's peak heap is 25.6 MB: the vectors (8.45 MB = 2,751 x 768 floats), arena blocks (4.0 MB), chunk texts (3.8 MB), one batch's float nodes (3.16 MB) and the response buffer (2.1 MB). Scaled to 13,297 chunks the vectors alone are 40.8 MB, so whether they, or the chunk texts, are held twice is the first thing to check. Not measured: the peak heap at full scale, since massif over 13,297 chunks is slow.
**Where:** [the massif page](analysis/2026-09-22-index-embedding-allocation-massif.md)


## Closed at M902 — the audit corpus's placeholder pass

The 2026-10-06 sitting's operator decision, applied in one pass: the row is kept as written,
with the closure appended to the detail.

| Deferred | Detail |
|---|---|
| **`snapshot_lint` is red on the 2026-10-05 audit's own files (pre-existing on master).** | [details](#def-321) |

## Closed at M725 — the JSON tree that was never the float nodes

The row is kept as it was written, with the closure appended.

| Deferred | Detail |
|---|---|
| **`jc_embed_parse` builds one JSON node per float.** | [details](#def-88) |

<a id="def-88"></a>
#### jc_embed_parse builds one JSON node per float.
M700 measured it at **92 %** of an index build's peak heap, and `jichi index` over this repository at **170,048 kB** resident — above the total RAM of the 96 MB and 160 MB rows that have run the agent loop.
**Why:** Engineering only, and no reason against it; **queued behind D1** because D1 and D2 each waste whole runs when they fire, and this wastes headroom. It had no row here at all until now, which is the defect this row fixes. **Closed at M725, and the row's premise was wrong twice.** The allocations massif named were `parse_string`'s -- its own output said so -- not float nodes: every string's buffer was sized to the REST of the input and kept, ~190 MB for a reply of ~1.5 MB. Fixed, an index build's requested heap on 199 files fell from 219,112,560 B to 25,596,080 B (-88%). And that heap was mostly address space: jichi pins glibc's mmap threshold at 128 KB (`jc_memtrim.c`), so each oversized buffer was a fresh mapping it barely touched, and peak resident memory over this tree moved from 136,080 kB to 130,692 kB (-4%). The design this row pointed at, reading the floats straight from the text, was built and measured on top of the fix: **zero bytes** off the peak, so it was not kept. What the resident 130 MB is has its own row now.
**Where:** plan D3; [analysis](analysis/2026-09-22-index-embedding-allocation-massif.md)


## Closed at M724 — the unset prefix that failed open, and the registry's cap

The row is kept as it was written, with the closure appended.

| Deferred | Detail |
|---|---|
| **The popen path's key-`unset` prefix is dropped silently when it would not fit its 1 KB buffer.** | [details](#def-89) |

<a id="def-89"></a>
#### The popen path's key-unset prefix is dropped silently when it would…
`jc_proc_secret_env_prefix` returns 0 both for *nothing registered* and for *would overflow*, and its caller cannot tell the two apart, so with enough registered key-variable names a model-issued command on the popen path would run with the keys still in its environment. The fork paths scrub in the child and are not affected.
**Why:** **Read, not reproduced** — reaching it needs roughly forty registered names. It fails open, so it deserves a check-first driver and a prefix sized to fit, but not inside a milestone about stderr. **Closed at M724, and reproduced first:** twelve configured names of 104 bytes put all twelve keys and a stray `JICHI_API_KEY` into a popen-path command's environment. The row's estimate of "roughly forty" names was wrong because the registry's own cap hid a second hole: it held 32 names of under 128 bytes and ignored the rest, so a 33rd configured name leaked on the fork paths too (measured: 1 of 33). The registry grows now; the prefix is sized exactly (`jc_proc_secret_env_prefix_size`) and answers -1, never 0, when it cannot be built; the popen path then refuses the command; and a name the registry cannot keep for lack of memory makes jichi refuse to start. `tests/smoke/secret_env_many_names.sh` drives both holes on both paths, and `test_secret_env_many_names` holds the contract.
**Where:** `src/util/jc_proc.c` (`jc_proc_secret_env_prefix`)


## Open — found while proving M729 (2026-09-24)

| Deferred | Detail |
|---|---|
| **120 `JC_CHECK(E != NULL)` lines are still followed by a use of `E`.** | [details](#def-90) |

<a id="def-90"></a>
#### 120 JC_CHECK(E != NULL) lines are still followed by a use…
**Why:** Counted at M729 in 42 test files: a check records and continues, so each is a segfault waiting for the run where `E` is NULL. M729 converted the 51 whose next lines were one-line checks on `E`, where a transform is safe, plus the nine that crashed on an unwritable `TMPDIR`. The rest have shapes a transform cannot rewrite blind: a multi-line check, a call, a dereference inside a block. The gate line holds the environment that bites; a lint over the shape would hold the code, but it would first have to fail on all 120.
**Where:** `tests/*.c`


## Closed at M729 — the suite on a TMPDIR it cannot write

| Deferred | Detail |
|---|---|
| **The unit suite segfaults on a `TMPDIR` it cannot write, and one test then writes into the directory it runs from.** | [details](#def-91) |

<a id="def-91"></a>
#### The unit suite segfaults on a TMPDIR it cannot write, and…
**Why:** Measured at M728 with `TMPDIR=/nonexistent`, at HEAD and after the fix alike: after 88 failures the suite dies in `test_config`, whose `JC_CHECK(sb.data != NULL)` records and continues into `strstr(NULL, ...)`. Guarding that site with `JC_REQUIRE` moved the crash to `test_session`, so there are more. `test_gradecore`'s `chdir` into its fixture fails there, and it writes `noverify.md`, `cannotrun.md` and more into the current directory: run from the repo root, that is the tree. It is M452's and M457's class (a host without a writable `/tmp`), found again because M728 ran the suite in that environment. The check that would hold it is a gate line running the suite under an unwritable `TMPDIR` and requiring a summary line, not a signal. **Closed at M729:** the suite completes there: 13,452 checks, 405 failures, rc 1, and nothing new in the directory it ran from. nine sites are guarded by hand, `test_gradecore` refuses to go on when its `chdir` fails, `test_patch` writes its fixtures through a checked helper, and 51 `JC_CHECK(E != NULL)` lines whose next lines are checks on `E` became `JC_REQUIRE` guards by a transform; a normal run's count did not move. `make ci` now runs the suite on a `TMPDIR` below a regular file, which fails for root too, and requires a summary line, rc 1 and no new file; with the old binary that line was red (rc 139), and with the old `test_gradecore` it was red on the files.
**Where:** `tests/test_gradecore.c`, the `ci` recipe


## Closed at M728 — the unit suite's `rm -rf`, cut short

| Deferred | Detail |
|---|---|
| **The unit suite runs `rm -rf` on `$TMPDIR` paths built in fixed buffers, and a long `TMPDIR` truncates them to the `TMPDIR` or a parent.** | [details](#def-92) |

<a id="def-92"></a>
#### The unit suite runs rm -rf on $TMPDIR paths built in…
**Why:** Measured with `strace` at M727: under a 127-character `TMPDIR`, `tests/test_bounds.c`'s 128-byte `home` (`$TMPDIR/jichi_bounds_<pid>`) truncates to exactly the `TMPDIR`, and the suite ran `rm -rf "$TMPDIR"` four times. A `TMPDIR` longer than the buffer truncates to a prefix of itself, and a prefix that ends at a `/` is a parent directory. `tests/test_session.c` builds its `rm -rf` with `sprintf` into 128 bytes, which overflows instead, and `tests/test_app.c`'s 128-byte temp name makes a check false-red past about 100 characters. Nothing the gate runs is exposed: `make ci` here runs with `TMPDIR` unset, so in `/tmp`. An agent harness that sets a long `TMPDIR` is, and so is anyone whose `TMPDIR` sits deep in a home directory. The fix wants every such removal behind one helper that refuses a path that is not strictly below `$TMPDIR`, bounded buffers that are checked, and a lint that holds both; it is the next milestone. **Closed at M728:** every fixture removal goes through `jc_test_rm_rf`, which needs no shell and refuses, failing the suite, any path not strictly below `$TMPDIR` (25 sites became 29 calls). Fixture roots check that they fit, `jc_test_tmp` reports a path that does not, the 38 `sprintf` calls became `jc_snprintf`, and the suite refuses a `TMPDIR` longer than 160 characters, where it is measured clean plain and under ASan/UBSan. A canary in a 127-character `TMPDIR` was deleted by the old suite and kept by the new one. `test_fixture_lint` and the widened `sprintf_lint` hold it.
**Where:** `tests/test_main.c`


## Open — found while proving M727 (2026-09-24)

The FreeMiNT rig's first fresh disk found a recursion ([`plans/2026-09-freemint-aranym.md`](plans/2026-09-freemint-aranym.md) §10); proving the fix found these.

| Deferred | Detail |
|---|---|
| **The agent's own call chains are not held to MiNT's stack; the unit suite's are.** | [details](#def-93) |

<a id="def-93"></a>
#### The agent's own call chains are not held to MiNT's stack;…
**Why:** `make ci` runs the curl-free unit suite under `ulimit -s 512` since M727, and it peaks just under 96 KB on the host. The suite never enters `main()`'s dispatch, the TUI or a live turn, and the smoke tier runs `jichi` under the host's 8 MB. On FreeMiNT only `--version` and `describe` have run. Step 5 of the plan, a driven turn, is where the rest would first meet the fixed stack; a smoke subset under the same limit would find it earlier.
**Where:** plan §10


## Closed at M727 — the FreeMiNT rig, written from the procedure

| Deferred | Detail |
|---|---|
| **The FreeMiNT rig is a procedure, not yet a script (`tier-v-freemint.sh`).** | [details](#def-94) |

<a id="def-94"></a>
#### The FreeMiNT rig is a procedure, not yet a script (tier-v-freemint.sh).
**Why:** Steps 1 and 2 ran by hand, and every choice is now a measured fact the script must carry. It pins the archive by commit hash. It uses EmuTOS 1.4's 512k image, because ARAnyM 1.1.0 refuses the archive's 1024k one and then does not exit. It writes its own configuration: no D: (the archive maps the home directory), a case-sensitive E:, no network. It sets `TMPDIR=/tmp`, since the guest's own `mkdir -p` cannot create under `u:/tmp`. It halts through a `Shutdown(0)` helper, streams progress through `/dev/nfstderr`, and checks the sentinel's run id. Boot to halt is about 12 s, so each step's deadline can be measured rather than guessed. **Closed at M727:** `scripts/tier-v-freemint.sh` carries every one of those facts, with the EmuTOS zip and image pinned by sha256 as well. Step 1 takes 9 s and step 2 31 s, and each deadline is about ten times its measurement. Its first fresh disk crashed where the hand runs had completed, which is how a recursion in `jc_path_resolve` and a test that skipped itself without a word were found (plan §10).
**Where:** plan §9, §10


## Open — found by FreeMiNT steps 1 and 2 (2026-09-24)

Run by hand under `aranym-mmu` 1.1.0, [`plans/2026-09-freemint-aranym.md`](plans/2026-09-freemint-aranym.md) §9.

| Deferred | Detail |
|---|---|
| **19 unit-suite failures in the FreeMiNT guest, not classified one by one.** | [details](#def-95) |

<a id="def-95"></a>
#### 19 unit-suite failures in the FreeMiNT guest, not classified one by…
**Why:** From the rig at M727, with `TMPDIR=/tmp`: 13,707 checks, 19 failures, namely `test_tool` 5, `test_proc` 5, `test_pdf` 3, `test_app` 2, and one each in `test_gradecore`, `test_platform` (a FIFO on HostFS), `test_envelope` and `test_bg`. M726's hand runs counted 25 under `TMPDIR=u:/tmp`; the six that went (`test_glossary` 3, `test_refs` 2, `test_memory` 1) were that path. Most of the 19 shell out to utilities the minimal guest lacks (`sleep`, `grep`, `echo`, `false`). The FIFO may be a real platform difference. Telling them apart needs step 3's userland in the guest, or a reading of each check.
**Where:** plan §9, §10


## Open — found by writing the chatbot guide (M841, 2026-09-29)

Writing [`CHATBOT.md`](CHATBOT.md) meant running every setting it recommends against a scripted hostile model and three local ones. It found a defect that M840 fixed (`--readonly` cleared by a bounded run) and three that are recorded here.

| Deferred | Detail |
|---|---|
| **A turn that ends after its tool calls with no answer is reported as done.** | [details](#def-276) |
| **`search_code` searches inside `.git`.** | [details](#def-277) |
| **An embedder cannot choose the system prompt directly.** | [details](#def-278) |
| **The refusal of an unavailable tool lists tools that `--readonly` refuses.** | [details](#def-279) |

<a id="def-276"></a>
#### A turn that ends after its tool calls with no answer is reported as done
**Why:** **Measured** with `qwen/qwen3.5-9b` in LM Studio: the model read the documents and then ended its turn with an empty message, and jichi exited 0 with `stop_reason: done` and an empty `text`. It happened in 2 of 12 turns in plan mode and 4 of 12 in chat mode. In one case the model had marked a to-do item "complete" as its answer. The chatbot example retries once in the same session, asking for the answer, and in the final measurement that retry fired 3 times in 36 turns. jichi already has a corrective nudge for a tool call written as prose (M147), but none for an empty final answer. The fix: one corrective turn when the final message is empty after tool calls, and a field on the terminal object that says so, so a consumer need not test the text for emptiness.
**Where:** `src/chat/jc_agent.c` (the M147 nudge is the pattern), `src/util/jc_agentjson.c`

<a id="def-277"></a>
#### search_code searches inside .git
**Why:** **Measured:** in a knowledge folder that was a git repository, `search_code` returned a match from `.git/hooks/fsmonitor-watchman.sample`. It greps the whole workspace, `.git` included, and once documents are committed a repository's history (`.git/logs`) holds its authors' names and e-mail addresses. For a chatbot, that is personal data a visitor can search for. The chatbot guide works around it by deploying the documents without their history. The fix is to skip version-control directories, portably: `grep --exclude-dir` is not in POSIX, so the tool would list the files itself, or pass the flag only where a probe finds it.
**Where:** `src/tools/jc_tool_search.c`

<a id="def-278"></a>
#### An embedder cannot choose the system prompt directly
**Why:** **Measured:** run from inside this repository, the chatbot example's system prompt began with all of jichi's `CLAUDE.md`. The rules walk loads `AGENTS.md`, or `CLAUDE.md` as a fallback, from every folder up to the nearest `.git`, or up to `/`, plus `~/.config/jichi/AGENTS.md`, and nothing turns that off. The example avoids it by where the knowledge folder lives, and by a server that refuses to start when a rules file sits above it. A persona is reachable only through a custom command with `agent:`, so the example sends every message as `/ask <message>`. An embedder would be better served by a config switch that loads only the listed `instructions` files, and a persona the config or a flag can name for a top-level run.
**Where:** `src/chat/jc_rules.c`, `src/chat/jc_sysmsg.c`, [CHATBOT.md §3.5–3.6](CHATBOT.md)

<a id="def-279"></a>
#### The refusal of an unavailable tool lists tools that --readonly refuses
**Why:** **Measured:** with `"toolProfile": "core"` and `--readonly`, the model is offered three tools (`read_file`, `list_files`, `search_code`). A call to a tool outside the profile is answered `Tool 'fetch_url' is not available to this agent. Available tools: read_file, write_file, edit_file, apply_patch, list_files, search_code, run_terminal_command`, which is the profile's list, four entries of which this run would refuse. A model that believes it wastes a call on a refused write. The list should be the tools this run may actually use.
**Where:** `src/tools/jc_tool.c` (the "is not available to this agent" message)

## Open — found by the 2026-09-29 branch review

Verifying the branches of 2026-09-29 against master `c2f407b5` turned up defects no branch listed.
The full work list, with file:line, fixes and severities, is the internal note
`docs/internal/2026-09-29-branch-review-findings.md`. The security-relevant ones are named here by
class only, following `SECURITY.md`; their mechanism and evidence are in that note. The rest of the
list (misleading records, unbounded loops, ACP protocol gaps) is tracked there rather than here,
with these exceptions recorded in full: def-285, the desktop design's D5, which this review
widened -- it lifts the approval gate without a word, and the operator classified it as an
ordinary defect, not a security finding (2026-09-30); def-286, a design question the operator left
open when deciding how a delegated command is fenced; def-287, the profile rule decided that night,
which waits for the same fix; def-288 and def-289, the rules-file decisions of 2026-10-01; and
def-290, the docs pack's routing, decided the same night; def-291 to def-293, the LSP defects
measured while deciding the web-ts pack's TypeScript server; def-294 and def-295, Godot's
language server, decided after a hand test; def-296, the lint that holds the diagram rule; and
def-297, the desktop application, postponed.

| Deferred | Detail |
|---|---|
| **`--strict-scope` does not stop a shell run through `run_tests`.** | [details](#def-280) |
| **A config fence written as `1` or `"true"` is read strictly and does not apply.** | [details](#def-281) |
| **A model-issued shell command inherits the ACP client's protocol stdin.** | [details](#def-282) |
| **A project config overrides the user's config with no trust step.** | [details](#def-283) |
| **A project's rules files, commands, agents and skills load with no trust step.** | [details](#def-298) |
| **Our cJSON's `cJSON_AddItemToObject` frees an item's key before copying the new one.** | [details](#def-299) |
| **A write child the batch's own timeout stops is kept, never verified and merged.** | [details](#def-300) |
| **Two write children of one batch share one stash stack and can pop each other's entries.** | [details](#def-301) |
| **`jc_mkdir_p` returns success when the leaf directory could not be created.** | [details](#def-302) |
| **`max_tokens` is not fitted to the room left in the context window.** | [details](#def-303) |
| **`spawn_parallel` blocks its lead until every task has ended.** | [details](#def-304) |
| **A child stopped by M829's no-progress check is reported as stopped at its iteration cap.** | [details](#def-305) |
| **A write child's git shows jichi's own checkpoint commits as if they were project history.** | [details](#def-306) |
| **The C++ build is not gated, and it rotted twice unnoticed.** | [details](#def-307) |
| **A line break between "do not" and its verb hides the rule from the constraint scanner.** | [details](#def-308) |
| **A kept state has no one-step way into the live tree.** | [details](#def-309) |
| **No gate holds a write child whose declared metric fell when it moved no pin.** | [details](#def-310) |
| **The metric gate measures with the worktree's own harness.** | [details](#def-311) |
| **A read-only phrase is still read as an order in five shapes M878 does not judge.** | [details](#def-312) |
| **The first-run hint prints before `constraints` and `brief-check`, which call no model.** | [details](#def-313) |
| **No test exercises the setup wizard's model listing.** | [details](#def-314) |
| **A SIGTERM to jichi does not stop the command it is running unless a timeout or a memory budget is set.** | [details](#def-315) |
| **Nothing notes a recursive removal of a granted directory outside a child's worktree.** | [details](#def-316) |
| Mid-turn argument elision (M218) does not skip arguments whose content is already on disk -- **the whole-file skip closed at M912**; the apply_patch fragment case stays open. | [details](#def-317) |
| **A subagent's final answer lands in the parent's context uncapped (closed at M911).** | [details](#def-318) |
| **Top-level design docs are outside the docs-vs-code lint family (M366/M158); seven rotted spots were found in one week.** | [details](#def-319) |
| **The [context] note's unconditional per-round standing form is not built (M900's scope).** | [details](#def-320) |
| **M912's on-disk identity check had no read-intent fence guard (closed at M913).** | [details](#def-323) |
| **`--output jsonl` can split a text delta inside a multi-byte character.** | [details](#def-284) |
| **A loaded ACP session's saved mode replaces the process's, even `--plan`.** | [details](#def-285) |
| **A subagent the model starts runs ASK verdicts without prompting, in chat mode too.** | [details](#def-286) |
| **A profile's `tools:` and `model:` do not apply under `attempt`, nor its `tools:` in a subtask.** | [details](#def-287) |
| **`init` beside a project's own `CLAUDE.md` writes an `AGENTS.md`, and the user's rules stop applying.** | [details](#def-288) |
| **jichi has no rules file of its own, and a rules file it does not read says nothing.** | [details](#def-289) |
| **Combining packs can lose the docs pack's audience routing, and `init` does not say so.** | [details](#def-290) |
| **jichi treats a language server's error reply as success, and the server's reason is lost.** | [details](#def-291) |
| **jichi's LSP client only waits for pushed diagnostics; a server that answers on request gives nothing.** | [details](#def-292) |
| **The web-ts pack's language server refuses TypeScript 7, npm's default, and nothing falls back.** | [details](#def-293) |
| **The godot pack gives no parse gate and no GDScript 4 guidance, and no language-server example.** | [details](#def-294) |
| **jichi's LSP client cannot open a TCP connection; a TCP-only server needs `socat` or `nc`.** | [details](#def-295) |
| **Nothing holds the diagram rule: a page can still add a Mermaid-only diagram.** | [details](#def-296) |
| **The desktop application is postponed; its design and checklist stay proposals.** | [details](#def-297) |

<a id="def-280"></a>
#### --strict-scope does not stop a shell run through run_tests
**Why:** Closed at M846: one predicate (`jc_tool_name_runs_shell`) decides a tool carries a shell command, and all six gate sites use it, so `--strict-scope`, the privileged and kinetic gates, the shell-ran signal and the loop-key derivation treat `run_tests` like `run_terminal_command` (`tests/smoke/shell_tools_fenced_alike.sh`; ROADMAP M846). As found: **[Security, class only.]** `--strict-scope` is documented to forbid the shell, and the edit-scope shell check enforces it by tool name -- but it misses `run_tests` with a `command`, which M501 already counts as a shell. A command run that way executed and wrote outside the edit scope; the envelope warned about the out-of-scope file and still reported the run verified. The fix is to cover every tool counted as a shell. Mechanism and repro: the internal branch-review findings note.
**Where:** `src/chat/jc_agent.c`

<a id="def-281"></a>
#### A config fence written as 1 or "true" is read strictly and does not apply
**Why:** Closed at M843: the four policy fields, `control`, `routing.escalateOnContext`, an MCP server's prompt-argument `required` and `jichi-convert`'s opencode tools map read a boolean leniently, and `config_bool_lint` counts raw boolean reads in the files that read human or foreign JSON (ROADMAP M843). As found: **[Security, class only.]** `permissions.deny` and sibling fence fields accept only a JSON `true` (or `"*"`); a human writing `1` or `"true"` in a config gets the fence read as absent, so it does not apply -- the M519 lenient-boolean class, this time on a fence rather than a value. The fix is to read a fence a human or a model writes leniently, as CLAUDE.md's rule requires, and to widen the boolean lint to the strict `cJSON_IsTrue` readers it does not currently see.
**Where:** `src/config/jc_config.c`, `tests/smoke/config_bool_lint.sh`

<a id="def-282"></a>
#### A model-issued shell command inherits the ACP client's protocol stdin
**Why:** Closed at M844: all six spawn sites that handed a child jichi's own stdin give it `/dev/null` (`tests/smoke/acp_child_stdin.sh` shows the swallow by effect and holds the sites by count; ROADMAP M844). As found: **[Security, class only.]** Over ACP the transport is stdin/stdout JSON-RPC, and a shell command jichi runs for the model inherits that stdin, so a command that reads stdin can consume the client's next protocol message -- a `session/cancel` was swallowed this way. The fix is to give model-issued commands `/dev/null` for stdin. Mechanism and the spawn sites: the internal branch-review findings note.
**Where:** `src/util/jc_proc.c`, `src/chat/jc_app.c`

<a id="def-317"></a>
#### Mid-turn argument elision does not skip arguments whose content is already on disk (ticket half closed at M909)
**Why (the closed half):** The two mid-turn elision passes were asymmetric. The result pass trims old tool
OUTPUTS and first spills each full output to the 0600 preservation store (`midturn_spill`, the M348 claim
ticket); the marker names the file, so the work is recoverable by re-reading. The argument pass
(`jc_compact_trim_tool_args`, M218) took NO spill callback and replaced large tool-call ARGUMENTS with a
~120-byte placeholder that said "cannot be recovered". For `write_file`/`apply_patch` the arguments are the
full document body, so under pressure the session's most irreplaceable material was dropped. Observed in the
2026-10-05 gap-audit session itself (large heredoc-write workload): the same call re-sent 8+ times after its
arguments were elided, and `jc_tool.c` rejects the copy-back of the placeholder (kind
`elided_placeholder`). **Closed at M909:** the args pass gained the M348 ticket — the full
`arguments_json` is offered to the preservation store and the marker names the file (the note no longer
claims the content is unrecoverable; the ticketless text stays byte-identical, D3). R2 (the copy-back trap)
needs no change — it keys on the `elided` field, marker-agnostic; the teeth are green by construction and
proven. R6 (the M361 latch) needs no latch change — it was already arg-aware; the missing half was the
report, which now feeds `mrep.preserved`. R3 (the measurement) landed earlier as M901.
**Why (the remaining half):** the args pass still does not SKIP an argument whose content already exists on
disk — a `write_file` body IS the file, so eliding it at all (ticket or not) spends context on a copy the
model can re-read for free. The ticket bounds the loss; the skip removes the cost.
**Where:** `src/chat/jc_compact.c` (`jc_compact_trim_tool_args_ex`). The skip policy is a per-call path
resolve + a content-identity check (does the file on disk still equal the elided body?), which is why it is
its own sitting rather than riding on M909.

<a id="def-318"></a>
#### A subagent's final answer lands in the parent's context uncapped
**Why:** `spawn_subagent` copies the child's last assistant text verbatim into the tool result
(`jc_agent.c:5818`; M62#6 is only an arena-lifetime note, not a size bound). Measured on the sibling tool's own
jichi-development sessions (2026-10-05 audit, logs referenced by session id only, never committed): 17
subagent hand-backs injected as user turns, 14 of them over 10 KB (median 19 KB, max 31 KB, 301 KB total).
A long subagent report is thus a second context-eater behind the mid-turn pressure that triggers the M218
elision pass — and that pass is precisely what def-317's asymmetry makes lossy. Not measured: whether any
jichi run has hit the same shape (the event is countable in the journal).
**Where:** `src/chat/jc_agent.c` (the spawn/subagent result path). The fix is a size cap or a summary
requirement at the boundary, not in the child's instructions. **Closed at M911 (2026-10-07):** `jc_subagent_answer_into` caps every hand-back at
the boundary (all consumers go through it): over 4096 B the parent gets a
UTF-8-safe head plus a note naming the full answer in the 0600 preservation
store (M348 symmetry); under the cap, byte-for-byte as before. Red-first
test_subagent_answer_capped; the gate's M729 smoke caught and fixed a
double-free in the first draft's mixed arena/heap ownership (see
docs/internal/2026-10-07-def318-m729-regression.md).

<a id="def-319"></a>
#### Top-level design docs are outside the docs-vs-code lint family (M366/M158); seven rotted spots were found in one week
**Why:** The 2026-10-05 gap audit found rot in seven top-level `docs/*.md` files that no lint reaches:
HOOKS.md (env vars that were never set, since the M25 commit), TYPE_AHEAD.md (M753's Esc-abort and off-mode
notice absent from the keys table), TEST_INTEGRITY.md (five vs six properties), GATE_INTEGRITY.md (three
line-number anchors, +1002/+2730/+2746 lines off), HARDENING.md (section header vs its own built notes),
AUTOCOMPLETE.md (a pre-M280 prompt quote attributed to the wrong file — M711's 125-claim sweep fixed the same
file's piece count and missed it), NOTIFY.md (the `/bin/sh` abbreviation hiding `JICHI_SHELL`). The fixes to
all seven were applied in the audit branch (2026-10-05). The pattern is M366's, inverted: OBSERVABILITY.md
rots nothing because a two-way lint with anti-breakage floors reaches its event tables. `reading_refs_lint`
(forbids line anchors) scans only `docs/reading/`; `decisions_superseded_lint` checks one marker form;
DECISIONS.md's 14 duplicated `<a id>` anchors (28 ambiguous links) have no uniqueness check at all.
**Where:** the lint family in `tests/smoke/`. The fix is M366's pattern extended per doc: anchors resolve and
are unique (DECISIONS), section status notes match shipped milestones (HARDENING), quoted code literals match
the code (AUTOCOMPLETE), and key/flag tables match the source (TYPE_AHEAD, HOOKS, NOTIFY).

<a id="def-320"></a>
#### The [context] note's unconditional per-round standing form is not built (M900's scope)
**Why:** **Recorded with M900 (2026-10-06).** C1 made the [context] note STANDING -- it re-fires every
PRESSED mid-turn round (over the 80% high-water), through the cache-stable message-tail vehicle, so the
model keeps a water-level reading while the window is over the edge. That is the version whose cost is
bounded by the existing M323 emission condition. The audit's literal ask -- a line on EVERY round whether
pressed or not -- was not built because its cost is not yet measured: one extra tail message per round, a
possible tool-call reshaping, and whether the M31b tail breakpoint still matches when a note is injected on
a calm round. M899 (the byte-stability test) is the guard that keeps the cached prefix churn-free, but the
tail's own cache behavior under a calm-round injection is a measurement, not an assumption, and the register's
doctrine is measure-before-change.
**Revisit when:** a live corpus shows a turn over the high-water where the model elides an argument BEFORE the
first pressed round's note arrives -- i.e. the standing-while-pressed form is still arriving too late, and the
cost of a calm-round note is worth paying. Then measure the tail-breakpoint cache match on a mock server first.
**Where:** `src/chat/jc_agent.c` (the mid-turn note injection), `src/provider/jc_promptcache.c` (the plan).

<a id="def-321"></a>
#### `snapshot_lint` is red on the 2026-10-05 audit's own files (pre-existing on master)
**Why:** **Found while gating M899-M901 (2026-10-06); verified present on master `79ec7e84`, not caused by
this branch.** `make ci`'s smoke tier fails `snapshot_lint` checks 8 and 9: check 8, a real email address in
`docs/analysis/COMMIT-ATTRIBUTION.md` (the author's address in the worked example, which the lint's allowlist
-- example or no-reply addresses only -- does not accept); check 9, real absolute home paths naming the
operator's account in the 35 audit documents `22d7886f` merged (the working-tree checkout path the
survey's clone of the jichi repository, on the machine that did the survey; the corpus has 52 documents,
and 35 carry the path). Both
are in the PUBLISHABLE tree (`docs/analysis/` is not `export-ignore`d; `docs/internal/` is, and its many
account paths do not count). The register's rule is that nothing merges with a red gate, and no public
snapshot can be cut until these are clean.
**Where:** `docs/analysis/COMMIT-ATTRIBUTION.md` (check 8); `docs/analysis/2026-10-05-JICHI-*.md` (check 9,
35 files). The fix is a placeholder pass: the email to an example or no-reply address and the paths to
`/home/u` (the lint's accepted placeholders are u, you, me, bench) -- or `export-ignore` the audit corpus if it
is deliberately internal. The operator decides which, because the email in COMMIT-ATTRIBUTION.md is a worked
example whose value is in being a real-looking address.
**Closed at M902 (2026-10-06)** -- the operator chose the placeholder pass, option A for the
address and option (ii) for the paths. `COMMIT-ATTRIBUTION.md`'s worked example now uses
`maintainer@example.org`, with a note that the trailer you commit keeps the maintainer's real
address (git metadata is outside the lint's file-content scan), and the 35 audit documents' paths
read `/home/u/development/journey/jichi` with a note at the first appearance in each that `u`
stands in for the home directory on the machine that did the survey. `snapshot_lint` is 16/16
on the branch head. The row's table entry moved to the `## Closed at M902` section below.

<a id="def-316"></a>
#### Nothing notes a recursive removal of a granted directory outside a child's worktree
**Why:** **The demos drive's N53 (b), decided "later" by the operator on 2026-10-04.** Run 0108's worker
ran `rm -rf` on four subdirectories of the Zig global cache, which the drive's `shellFenceAllow` grants
to every write child, twice in a minute. The fence allowed it, as a grant means. M896 documents that a
shared grant is shared state; the advisory threadwork proposed -- note a recursive removal whose target
is a granted directory outside the child's worktree, classifying the command by its effect as M535 did
for tools -- waits on a measurement: how often the drives' journals show such a command, and how often a
legitimate one (a tool clearing its own cache on purpose). A Landlock grant without removal rights is
the other route, unmeasured: caches delete and rename their own files.
**Where:** `src/chat/jc_agent.c` (the shell-call path), `src/util/jc_landlock.c`, `docs/CONFIG_TUTORIAL.md`

<a id="def-315"></a>
#### A SIGTERM to jichi does not stop the command it is running unless a timeout or a memory budget is set
**Why:** **Measured with M885 (2026-10-03).** With `runTimeout` 0, the default, and no memory budget,
`jc_app_run_command_ex` runs a command through its popen path. That path leaves the command in jichi's
own process group, and its read loop never looks at the abort flag. A SIGTERM sent to jichi alone -- a
supervisor's stop, `kill`, systemd -- sets the flag and then waits for the command to end. A
`spawn_parallel` worker running `echo $PPID > f; sleep 60` was sent SIGTERM once the pid was written,
and its own telemetry recorded the call as `ok`, exit 0, 60,003 ms; only then did its run stop. With a
timeout or a budget, the watched path runs the command in its own group and kills it when the run is
stopped; the demos drive sets `runTimeout: 900` and is not affected. Not measured: an interactive
interrupt, which the TUI reads as a key rather than as a signal to the group. The fix is to give every
command the watched path's group and abort check. That changes how every command runs, so it is a
milestone of its own.
**Where:** `src/chat/jc_app.c` (`jc_app_run_command_ex`'s popen path, `run_command_watched`),
`src/util/jc_proc.c` (`jc_proc_popen`)

<a id="def-314"></a>
#### No test exercises the setup wizard's model listing
**Why:** **Found with M881 (2026-10-02).** Since M709 the wizard asks the chosen endpoint's `/v1/models`
and offers what it lists. The one test that drives the wizard, `setup_keyfile`, picks a hosted provider
with no key, so the listing always failed (401) and the drive typed the id; since M881 it runs with its
proxy at a closed port, so the listing fails at once. A listing that succeeds -- the menu, the choice, the
"type the id" entry beside it -- is exercised by no test. A drive that picks option 0 and points it at a
local server answering `/v1/models` would cover it.
**Where:** `src/main.c` (the setup wizard), `src/net/net_util.c` (`jc_net_list_models`)

<a id="def-313"></a>
#### The first-run hint prints before constraints and brief-check, which call no model
**Why:** **Found with M878 (2026-10-02).** With no config file, jichi prints "No model is configured,
so jichi cannot call one yet." and its setup advice on stderr before `jichi constraints` (`scan`,
`list`) and `jichi brief-check`, which only run the scanner. M827 (N15) exempted the offline inspectors
`attempts`, `recover`, `undo` and `checkpoints`; these are the same kind. stdout is right, so a script
is not misled; a person scanning a brief on a fresh machine reads seven lines of setup advice first.
**Where:** `src/main.c` (the first-run hint)

<a id="def-312"></a>
#### A read-only phrase is still read as an order in five shapes M878 does not judge
**Why:** **Recorded with M878 (2026-10-02), measured while building it.** (1) A paragraph or message
that opens with "Read-only ..." is adopted before any cue is read: *Read-only tasks investigate the
live workspace*, 3 of the 6 docs paragraphs M878 still adopts. A role noun after the phrase now stops
it; a task noun does not, since "Read-only task: ..." heads a real order. (2) A scope after the phrase:
*Keep read-only mode for the children.* (3) A preposition first: *With the child in read-only mode,
merge its work.* (4) A list item that continues its lead-in's subject: *The children:* then *- run in
read-only mode*, which reads like *Rules:* then *- run in read-only mode*. (5) A quoted sentence as the
subject: *"do not edit X" must not put a run in read-only.* Each is a false order of N45's class; none
is in the session corpus. Wanted when a drive's prompt meets one; `constraints scan` and `brief-check`
show it before the run, and `"constraintsAutoAdopt": false` (M874) turns inference off for a harness.
**Where:** `src/chat/jc_constraint.c` (`read_only_instruction_at`, `read_only_said_of_other`)

<a id="def-311"></a>
#### The metric gate measures with the worktree's own harness
**Why:** **Recorded with M875 (2026-10-02), a limit stated in its design.** The metric command runs the
worktree's own code, so a child that changes how the number is computed changes the number; the gate
closes "type the number", not "change what is measured". For a project whose measuring harness is
separate from what the tasks change, running the BASE's harness against the worktree's build would
close it. Wanted only when a merged flip is found to be a regression the metric missed (the design's
"When to revisit").
**Where:** `src/tools/jc_tool_parallel.c` (`metric_measure`), `docs/proposals/2026-10-metric-gate.md` §5

<a id="def-310"></a>
#### No gate holds a write child whose declared metric fell when it moved no pin
**Why:** **Recorded with M875 (2026-10-02), its D9.** The metric gate measures only a child that moved
an assertion in a pin file. A general "the metric must not fall" gate for every write child is a
different feature, at a run per child; where a project's verify already holds a pin test, a child
whose metric fell without moving the pin fails that verify. Build it if a project without such a pin
asks for one.
**Where:** `src/tools/jc_tool_parallel.c` (the merge loop), `docs/proposals/2026-10-metric-gate.md` §4

<a id="def-309"></a>
#### A kept state has no one-step way into the live tree
**Why:** **Found answering N46 (2026-10-02), not built with M875.** "Take it on purpose" -- M865's note
for a held child -- means `jichi attempts show <ref>`, then `jichi recover <ref> --into <dir>` (never
the live tree), then applying the change by hand; a `from:` resume is held again, since its diff
still moves the assertion. A lead (a model) taking a flip pays that every time. A `jichi attempts
apply <ref>` that 3-way-applies a kept state onto the live tree, refusing on a conflict, would make
taking one a single step.
**Where:** `src/main.c` (`attempts`, `recover`), `src/snapshot/jc_snapshot.c` (`jc_snapshot_resume_apply`)

<a id="def-308"></a>
#### A line break between "do not" and its verb hides the rule from the constraint scanner
**Why:** **Found while building M872 (2026-10-02), not changed there.** "Please do not\npush to the
remote." infers nothing; with the break after "push" it infers "do not push". M735 meant a hard-wrapped
sentence to go on past a single newline, and the cue's window does, but `in_verb_position` skips only
spaces before a target, so a newline in front of the verb reads as a word that is not a lead-in. The
verb then counts only beside a run verb ("do not\nrun the build" still infers). A miss in the safe
direction -- the prompt still says it, and an inferred rule only advises -- but in a sentence the
scanner means to read. The fix is to skip every blank there; it makes the scanner infer more, so it
wants a corpus run (`tests/measure/scanner_corpus.py --against`) before it lands.
**Where:** `src/chat/jc_constraint.c` (`in_verb_position`)

<a id="def-307"></a>
#### The C++ build is not gated, and it rotted twice unnoticed
**Why:** Closed at M871: the operator decided to gate it, and `make ci` builds, links and tests jichi with
`CC=g++` and runs `cpp-check`. **Found 2026-10-02 (M867), the operator's decision.** M188 kept `cpp-check` out of `make ci` on
purpose ("the C gate is the gate"). M636 found the tier rotted (three published commands broken, "which
nobody knew because nothing ran them") and repaired it. It rotted again within ten days: `make CC=g++` and
`make CC=clang++` failed to link from M690 and failed to compile from M709, until the operator asked for
the build under every compiler. `tests/smoke/cppcheck_lint.sh` guards the recipe on a few files, not the
tree, and `cpp-check` never links, so it cannot see a linkage mismatch. Gating `make CC=g++ jichi` (about
10 s, plus a link) in `make ci` or the smoke tier would hold it; whether to is the operator's call, since
it widens M188's decision.
**Where:** `Makefile` (`ci`, `cpp-check`), `tests/smoke/cppcheck_lint.sh`, `docs/CPP_BUILD.md`

<a id="def-306"></a>
#### A write child's git shows jichi's own checkpoint commits as if they were project history
**Why:** **N44's (c), not done in M863.** A write child's worktree is a linked worktree of jichi's
shadow repository, so its `git log --all` lists jichi's checkpoint commits, whose messages are the
lead's prompts. In run 0090 worker 3 ran `git checkout 55342dd` twice: jichi's checkpoint ("green: You
are the **lead** ..."), and git warned "you are leaving 1 commit behind", the worker's own. Since
M863 that state is kept first, but nothing tells the worker the commit is not the project's. Threadwork
proposed keeping the checkpoints out of the child's `git log --all` and checkout, or labelling them.
**Where:** `src/snapshot/jc_snapshot.c` (`jc_snapshot_take`, the checkpoint messages),
`src/tools/jc_tool_parallel.c` (the worktree's creation)

<a id="def-305"></a>
#### A child stopped by M829's no-progress check is reported as stopped at its iteration cap
**Why:** Closed at M869: the done line carries `looped`, the ending reads "was stopped for repeating the
same successful call", and the delegate report's advice is the loop's. **Found while building M861's
driver (2026-10-02), not changed there.** A write child whose
same `echo` call returned the same result again and again was stopped by M829's no-progress check
after 10 tool calls, with `maxSubagentIters` at 25. The batch result told the lead "This task stopped
at its iteration cap after 10 tool call(s)", and the delegate report "it ran out of iterations ...
re-delegate a NARROWER subtask". M829 sets `last_run_capped` for that stop, so the child's stop is
`max_iters`, and M854's ending names the cap. A lead that reads it may raise `maxSubagentIters` or
narrow the task, and neither helps a child that was repeating itself. The child's done line already
carries `repeated` (M808), which the parent could use to say that the child repeated the same call
with the same result. M861's driver gives each of its checks its own output to stay clear of it.
**Where:** `src/chat/jc_agent.c` (the no-progress stop), `src/tools/jc_parallel.c`
(`jc_parallel_ending`), `src/util/jc_delegreport.c` (`max_iters`'s advice)

<a id="def-304"></a>
#### `spawn_parallel` blocks its lead until every task has ended
**Why:** **The operator's decision of 2026-10-02: recorded, not built yet.** The demos drive's card 214
held run 0090's lead for 4 h 27 min in three one-task batches, with no other card running: the lead
could not plan or measure inside the call (threadwork's handoff of 2026-10-01, §9, "a batch of one task
blocks its lead"). An asynchronous form -- handles returned at once, polled later -- would let a lead
keep working while a long card runs. It is a design change to the tool's contract, the merge (who
merges, and when, while the lead edits the same tree), the board and the end records, so it is
recorded as a design question rather than built beside the fixes.
**Where:** `src/tools/jc_tool_parallel.c` (`parallel_run`, `run_pool`)

<a id="def-303"></a>
#### `max_tokens` is not fitted to the room left in the context window
**Why:** **The operator's decision of 2026-10-02: build it later.** jichi sends the configured
`maxTokens` (or a fifth of `contextLength`, capped at 16,384) on every request
(`jc_provider_openai.c`), so a request fails with HTTP 400 once prompt plus `max_tokens` passes the
window. Threadwork's N42 (e) proposes fitting it: the configured value, or the window less the last
prompt the gateway counted and a margin, whichever is smaller. Measured for the demos drive: the
gateway's window is 256,000 tokens for both `jlu` models while the drive declares `contextLength`
196,608; jichi's own prompt estimate runs a median 1.29 times low against the gateway's count; under
the 400-call cap no worker's prompt passed 197,111 tokens. Fitting against `contextLength` would need
the real window there, which also moves compaction, so it is a decision of its own.
**Where:** `src/provider/jc_provider_openai.c` (`oa_build_request`), `src/config/jc_config.c`
(`jc_config_effective_max_tokens`)

<a id="def-302"></a>
#### `jc_mkdir_p` returns success when the leaf directory could not be created
**Why:** **Found by the demos drive's N41, left by M857.** `jc_mkdir_p`
(`src/platform/jc_platform_posix.c`) returns `JC_OK` when the leaf's `mkdir` fails, whether or not the
leaf exists as a directory: the branch commented "Tolerate the case where the leaf already exists as a
dir" returns `JC_OK` after a failed `stat` too. So a refused or impossible mkdir surfaces only at the
next write, if at all -- under the shell fence, the claim-ticket store's refusal showed only as D3's
"could not preserve" warning. Fixing it touches 34 call sites, each of which must then handle an error
it never saw, and `jc_toolout_preserve`'s own mkdir check returns without D3's warning, so fixing
`jc_mkdir_p` alone would remove the only signal there is. M857 fixed the case the drive hit by granting
the store.
**Where:** `src/platform/jc_platform_posix.c` (`jc_mkdir_p`), `src/util/jc_toolout.c`
(`jc_toolout_preserve`), and its 34 callers

<a id="def-301"></a>
#### Two write children of one batch share one stash stack and can pop each other's entries
**Why:** **Left by M855, measured.** A write child's worktree is a linked worktree of the shadow repo,
and linked worktrees share one `refs/stash`: with git 2.43, worktree 1 stashed A, worktree 2 stashed B,
and worktree 1's `git stash pop` applied B. M855 empties the stack before and after each batch, so no
child pops another BATCH's entry, keeps every entry as a ref, and tells a child that stashes to commit
instead. Within one batch the race remains: two children that stash within the same seconds can each
pop the other's change, which then merges as theirs. Closing it would need a repository per child (a
clone sharing the shadow repo's objects), which reworks M803's preservation and the merge. Not done
because no drive has shown it happen: the drive's 57 stashes are not timed against each other.
**Where:** `src/snapshot/jc_snapshot.c` (`jc_snapshot_worktree_add`), `src/tools/jc_tool_parallel.c`

<a id="def-300"></a>
#### A write child the batch's own timeout stops is kept, never verified and merged
**Why:** **Left by M854.** M854 lets a write child that did not end on its own -- stopped from outside
with SIGTERM, ended by a context-window 400, or capped -- answer with a note and merge after a green
worktree verify. A child the batch stops on its own `parallelTaskTimeout` (default 300 s) is not one:
the parent closes its pipe and kills it (`kill_one`), so it never reports how it ended, and its work is
kept as `parallel-interrupted`, as before. Covering it would mean verifying a worktree whose child sent
no last text and no stop reason. Not built because nothing measured it: the drive's long workers run
under a raised timeout, and its supervisor stops a worker with SIGTERM, which M854 covers.
**Where:** `src/tools/jc_tool_parallel.c` (`kill_one`, the classification before the merge)

<a id="def-299"></a>
#### Our cJSON's cJSON_AddItemToObject frees an item's key before copying the new one
**Why:** **Found while building M851; latent, not live.** `src/json/cJSON.c` frees `item->string` and only then copies the key it was given, so a caller that passes the item's OWN key (`cJSON_AddItemToObject(obj, it->string, it)`, the natural way to move an item between objects) reads freed memory. The cJSON library this file implements the API of copies the new key first. Checked on 2026-10-01: no caller in `src/` or `tests/` aliases the key today -- the three that pass `x->string` add a duplicate whose own key is NULL -- and M851's filter copies the key into a buffer first, with a comment saying why. The fix is four lines (copy, then free, then assign) and a unit test that moves an item by its own key; it was left out of M851 because cJSON is under every path and the milestone's full tier had already run. Our cJSON also has no `cJSON_DetachItemViaPointer` or `cJSON_Duplicate`, which M851 had to write around.
**Where:** `src/json/cJSON.c` (`cJSON_AddItemToObject`), `src/config/jc_config_trust.c` (`detach`, the key copy)

<a id="def-298"></a>
#### A project's rules files, commands, agents and skills load with no trust step
**Why:** **Recorded as the later question of the 2026-10-01 trust decision.** M851 gates a project's CONFIG files; its other assets -- `AGENTS.md`/`CLAUDE.md` (and the coming `JICHI.md`), `.jichi/commands`, `.jichi/agents`, `.jichi/skills` -- still load from a clone with no trust step. They carry instructions rather than permissions, and the untrusted-content rule and the approval gates still apply to what they make the model do, but a command can run a subtask and a skill can restrict or seed tools. Gemini CLI and Codex both gate more than config here (custom commands, project-local rules). Taking it up means deciding which assets are instructions (load with a notice) and which are capabilities (wait for trust).
**Where:** `src/chat/jc_rules.c`, `src/command/`, `src/command/jc_agentdef.c`, `src/skill/`

<a id="def-283"></a>
#### A project config overrides the user's config with no trust step
**Why:** Closed at M851: an untrusted project config applies only in the safe direction until the user trusts its content (`jichi trust`, or the TUI's one question); headless and ACP say so in one line; the classification of every key is held by `tests/smoke/config_trust_lint.sh` (DECISIONS M851; `tests/smoke/project_config_trust.sh`). As found: **[Security, class only.]** `./local/config.json`, else `./.jichi/config.json`, is merged over the user's `~/.jichi` with the project's scalars winning, and nothing gates it -- so a cloned repository, which the project's own threat model (M606) treats as hostile, can change which model endpoint and settings a run uses, which programs jichi starts, and which tools run without asking; through an armed envelope it could also drop the approval prompts of an `--acp` run -- **fixed at M850**, which keeps an ACP session out of the envelope's promotion to auto -- and still can for a headless `-p` run, which the trust gate is for. Nothing warns: `doctor` reports the merged source as a green line. The fix is a trust gate for project configs, honouring a project file's keys only in the safe direction until the directory is trusted. A key-by-key inventory was completed on 2026-09-30; it, the mechanism and the key-exposure detail are in the internal branch-review findings note. The trust step's design was decided on 2026-10-01; it is the last fix of the first batch, and whether it should also cover project rules files, commands, agents and skills is a later question.
**Where:** `src/config/jc_config.c`, `src/main.c`

<a id="def-284"></a>
#### --output jsonl can split a text delta inside a multi-byte character
**Why:** **Measured.** When a secret is registered (a gateway key registers one), the redaction hold-back on the streamed model text advances its safe boundary by a byte offset, so a multi-byte character straddling that boundary is emitted split across two `--output jsonl` lines -- invalid UTF-8 on an interface `docs/EMBEDDING.md` marks Stable, the same class as M439 at a second site. A probe gave 0 invalid lines with no key and 3 of 38 with one; a drive's own streams held 702 invalid lines across 18 of 21 runs. The weekend handoff flagged this as day-one finding 5 and no row was opened. The fix is to move the cut to a character boundary (`jc_utf8_resync` exists) and extend `jsonl_utf8.sh` or `redact_split_delta.sh`.
**Where:** `src/main.c`

<a id="def-285"></a>
#### A loaded ACP session's saved mode replaces the process's, even --plan
**Why:** Closed at M845: a saved mode may narrow a run's mode, never widen it, in ACP's load and in the TUI's and the headless resume, with a note whenever a saved mode is kept out (`tests/smoke/saved_mode_never_widens.sh`; ROADMAP M845). As found: **Measured** on 2026-09-30 with `qwen/qwen3.5-9b` in LM Studio, the only model loaded. ACP's `session/load` switches the process to the mode the session was saved with, unconditionally, where the TUI's resume keeps an explicit `--plan` or `--auto` (its `mode_pinned` check; from the code). A `jichi --acp --plan` process loaded a session saved in `auto`, and the model's `write_file` wrote `b.txt` with no `session/request_permission`. The same process's next `session/new` wrote `c.txt` the same way, and `jichi export` reports that new session saved as `auto` -- saving records the process's current mode (from the code) -- so the mode spreads to sessions created after the load. The controls held: a `--plan` process without the load was not offered `write_file` (the model said so; plan mode advertises no mutating tool, from the code), chat mode asked (answered with a rejection, so no file), and `--readonly` held across the same load. Not a security finding (the operator, 2026-09-30): `ACP.md` documents the restore, and the session files are jichi's own, in the user's `HOME`, with no known way for anyone else to plant one. The proposed fix holds a loaded mode to the rule `jichi control mode` already follows -- it may narrow the process's mode, never widen it (`jc_perm_mode_narrows`) -- and tells the client when the mode changes. Until then, start a process that should only look with `--readonly`. The desktop design (`docs/proposals/2026-09-desktop-gui.md` §3.7 and §9, D5) carries the first measurement and the advice for a client.
**Where:** `src/acp/jc_acp.c` (`handle_load`, `new_session`, `save_session`), `src/chat/jc_perm.c`

<a id="def-286"></a>
#### A subagent the model starts runs ASK verdicts without prompting, in chat mode too
**Why:** **Documented, and open as a design question.** `docs/TOOL_DECISIONS.md` says it: "a *subagent* running under an auto posture executes ASK verdicts without prompting". On 2026-09-30 the operator decided that a delegated *command* will follow the user's own approval mode (the scaffolding review's S2, whose fix lands with the security fixes), and left this sibling open rather than fold it into that fix: should a delegate the *model* starts with `spawn_subagent` follow the user's mode too? For: one rule for every delegate, so chat mode means "ask first" everywhere. Against: a subagent exists to work a task through on its own, and a headless run without `--auto` that relies on one today would start refusing what it runs. That fix landed at M848 and turned the path fence on for every auto-posture delegate, so a subagent's writes stay inside the workspace in chat mode whichever way this goes; the posture question itself is still open. **Narrowed at M849:** a delegate's SHELL calls now follow the user's posture (the operator, 2026-10-01; DECISIONS M849), and a delegate never runs a privileged command through the allowlist. What stays open is the rest: a delegate's file and other tools still run its ASK verdicts without a prompt.
**Where:** `src/chat/jc_agent.c` (`jc_agent_run_subagent`, the auto posture), `src/chat/jc_app.c` (`jc_app_path_fence_on`)

<a id="def-287"></a>
#### A profile's tools: and model: do not apply under attempt, nor its tools: in a subtask
**Why:** Closed at M848: a subtask command passes its profile's `tools:` as its allow list, `attempt --agent` fences its turn with them and takes the profile's `model:` (an explicit `--model` wins), the main turn is unchanged, a refused tool names the list, and the named pack assets are reworded (`tests/smoke/subtask_follows_posture.sh`, `tests/smoke/attempt_profile_fence.sh`; ROADMAP M848). As found: **From the code (2026-09-30); decided the same night.** `jc_app_command_agent_apply` applies a profile's persona, style and read-only flag and nothing else, for a command's `agent:` and for `attempt --agent` alike; only a subagent enforces the profile's `tools:`. So under `attempt` the assignments pack's learner tiers differ by persona alone (`learner-senior` is offered `hint` and `ask_for_help` whenever `assignments` is on), a tier's `model:` does nothing, and a subtask command's profile list has no effect. The operator's rule ([DECISIONS](DECISIONS.md#dec-2026-09-30-profile)): in a run of its own -- a subtask, a subagent, `attempt` -- a profile's `tools:` is a fence that can only narrow, and `attempt` also takes its `model:`; the main turn stays as it is. It lands with the fix for how a delegated command is fenced, in the same function, and with it: a refusal that names the profile's list, a CHANGELOG line for users' own subtask commands that lose a tool, and the pack assets reworded (the scaffolded `model:` sentence, the learner personas, `/release-check`). Test, red first: capture the request of `attempt --agent learner-senior` with `assignments: true` and assert `hint` is not offered; then one `attempt` on a free local model. Still open after it: with `assignments` off, the tiers whose persona leans on `hint`/`ask_for_help` are told to call tools that do not exist.
**Where:** `src/chat/jc_app.c` (`jc_app_command_agent_apply`), `src/main.c` (`attempt`), `src/scaffold/jc_scaffold.c` (the pack assets)

<a id="def-288"></a>
#### init beside a project's own CLAUDE.md writes an AGENTS.md, and the user's rules stop applying
**Why:** **Measured** on 2026-09-30 with the real binary, offline, in a scratch repository: a rule in `CLAUDE.md` is shown by `jichi rules`; `jichi init` exits 0, writes an `AGENTS.md` and prints nothing about `CLAUDE.md`; afterwards `jichi rules` shows only `AGENTS.md` and the rule is gone, because the loader reads `CLAUDE.md` only when a directory has no `AGENTS.md`. The stub `init` writes says "jichi reads this file (and CLAUDE.md)", which is false. Until the fix, a line in `.jichi/config.json`, `"instructions": ["CLAUDE.md"]`, brings the rule back beside the stub (measured the same day). [Since M851 (2026-10-01), only after `jichi trust`: an untrusted project config's `instructions` are skipped, with a warning that names the step. Measured 2026-10-03, M884.] Decided on 2026-10-01 ([DECISIONS](DECISIONS.md#dec-2026-10-01-init-rules)): where the project already has its own `AGENTS.md` or `CLAUDE.md`, `init` writes the pack's rules to `JICHI.md` instead (def-289), or, when that exists too, to `.jichi/rules/<pack>.md` with a message that they are not active yet; it never edits a config; the stub is corrected. Open for the milestone: a second `init` of the same pack must not write its rules into `JICHI.md` again, and composing packs in one call needs its rule stated. Test, on the real binary and red first: `git init`, a rule in `CLAUDE.md`, `jichi init`, and the rule must still be in `jichi rules`; then the same with an existing `AGENTS.md`. It lands with or after def-289, which gives `JICHI.md` its meaning.
**Where:** `src/scaffold/jc_scaffold.c` (the pack writer and the `AGENTS.md` stub)

<a id="def-289"></a>
#### jichi has no rules file of its own, and a rules file it does not read says nothing
**Why:** **From the code (2026-10-01); decided the same day; not built yet.** The loader reads `~/.config/jichi/AGENTS.md`, then in each directory from the git root down `AGENTS.md` or else `CLAUDE.md`, then the config's `instructions`. So jichi's only alternative to the cross-tool name is a vendor's name, and a file shadowed by the first-found rule is simply not read, with no word -- the mechanism behind def-288. The operator's rule ([DECISIONS](DECISIONS.md#dec-2026-10-01-rules-names)): the shared rules are the first file found from a configurable list, default `AGENTS.md` then `CLAUDE.md` (today's behaviour); jichi then also reads its own `JICHI.md` in the same directory and in `~/.config/jichi/`; and `jichi rules` and `doctor` name a rules file that is present and not read, and why. With it: the config key for the list (its name chosen at the milestone), `RULES.md`, `AGENTS_GUIDE.md` and `SCAFFOLDING.md`. Test, red first: a directory with `AGENTS.md`, `CLAUDE.md` and `JICHI.md` -- `jichi rules` shows `AGENTS.md` and `JICHI.md` and names `CLAUDE.md` as present and not read; and with `CLAUDE.md` taken off the list, a project that has only a `CLAUDE.md` gets no shared rules and `doctor` says so.
**Where:** `src/chat/jc_rules.c` (`add_dir_rules`, `jc_rules_load`), `doctor`

<a id="def-290"></a>
#### Combining packs can lose the docs pack's audience routing, and init does not say so
**Why:** **Measured** on 2026-10-01 with the real binary, offline: `jichi init c-cli docs` keeps the generic `/write-docs` and `/proofread`, which do not route, and installs the six audience profiles with nothing to call them; `jichi init docs c-cli` keeps the routing; `init` reports the lost versions with the same `exists` line as a harmless skip. Decided the same night ([DECISIONS](DECISIONS.md#dec-2026-10-01-docs-routing)): one shared text for the two commands in every pack, handing the work to the matching profile when it exists; a unit test that shared paths are byte-identical across packs; and `init` naming a skipped file that differs from the pack's version -- the comparison def-288 needs too, so both land in one change to the writer. Until then, put `docs` first (`init docs <language>`), as `SCAFFOLDING.md` now says. Not fixed by it: a combined `--dry-run` counts only what is on disk (the scaffolding review's E4).
**Where:** `src/scaffold/jc_scaffold.c` (the two command assets, the writer's skip), `tests/test_scaffold.c`

<a id="def-291"></a>
#### jichi treats a language server's error reply as success, and the server's reason is lost
**Why:** **Measured** on 2026-10-01, and read in the code: `lsp_request` returns success as soon as a reply with the matching id arrives, and never looks at its `"error"` member. So when typescript-language-server refuses `initialize` on TypeScript 7 -- "The TypeScript of the workspace (TypeScript 7.0.2 …) provides no tsserver.js. No other valid TypeScript installation was found. Exiting." -- jichi marks the connection initialized, opens the file, waits out the diagnostics time bound (16 s) and prints "no diagnostics received", which reads almost like a clean file. A server that exits at once is reported ("initialize failed"), though without its stderr. Decided the same night ([DECISIONS](DECISIONS.md#dec-2026-10-01-lsp-ts)): report an error reply in the server's own words, and treat a refused `initialize` as a server that did not start, which def-293's fallback then uses.
**Where:** `src/lsp/jc_lsp.c` (`lsp_request`, the `initialize` handshake)

<a id="def-292"></a>
#### jichi's LSP client only waits for pushed diagnostics; a server that answers on request gives nothing
**Why:** **Measured** on 2026-10-01: TypeScript 7's own server (`tsc --lsp --stdio`, typescript-go 7.0.2) advertises `diagnosticProvider`, pushes nothing after `didOpen`, and answers `textDocument/diagnostic` (LSP 3.17 pull) with the exact error; jichi's client handles only `textDocument/publishDiagnostics`, so `jichi lsp` reports "no diagnostics received" after 16 s. Decided the same night ([DECISIONS](DECISIONS.md#dec-2026-10-01-lsp-ts)): when a server advertises pull diagnostics, ask for them. Test: the real server where Node is available, and a scripted pull server in the offline smoke tier.
**Where:** `src/lsp/jc_lsp.c` (the diagnostics collection)

<a id="def-293"></a>
#### The web-ts pack's language server refuses TypeScript 7, npm's default, and nothing falls back
**Why:** **Measured** on 2026-10-01: the pack ships `typescript-language-server --stdio`, which reports the type error in 1.1 s with TypeScript 6 and refuses to start with TypeScript 7; npm's `typescript` `latest` was 7.0.2 that night, so a new project gets the refused version. jichi takes the first `lspServers` entry for an extension and does not try another when it fails (`find_cfg`), so a second entry could not help today. Decided the same night ([DECISIONS](DECISIONS.md#dec-2026-10-01-lsp-ts)): after def-291 and def-292, fall back to the next matching server when one fails or refuses to start, with `jichi lsp` and `doctor` naming the server that ran; then the pack ships `tsc --lsp --stdio` first (TypeScript 7; it fails in 130 ms on TypeScript 6) and `typescript-language-server` second. The same fallback serves elixir-ls or Expert, and erlang_ls or ELP. Until then, pin `typescript@6` in a web-ts project for diagnostics (measured), as `LSP.md` says.
**Where:** `src/lsp/jc_lsp.c` (`find_cfg`, `get_conn`), `doctor`, `src/scaffold/jc_scaffold.c` (the web-ts pack)

<a id="def-294"></a>
#### The godot pack gives no parse gate and no GDScript 4 guidance, and no language-server example
**Why:** **From the pack's assets** (the scaffolding review's D4): the godot pack ships no `config.example.json`, no `--check-only` gate and no GDScript 4 guidance, although Godot 3 syntax is a model's usual mistake. Decided on 2026-10-01 ([DECISIONS](DECISIONS.md#dec-2026-10-01-godot)), after a measured hand test: the pack gains the `--check-only` gate (`--headless --check-only --script` exited 1 in 213 ms on a type error), GDScript 4 guidance (signals, `CharacterBody2D`, `super()`), and an opt-in example of the bridge recipe that `GAME_DEVELOPMENT.md` now documents as run. Assets only, so it reaches projects initialized after it (`init` never overwrites).
**Where:** `src/scaffold/jc_scaffold.c` (the godot pack)

<a id="def-295"></a>
#### jichi's LSP client cannot open a TCP connection; a TCP-only server needs socat or nc
**Why:** **From the code, and measured.** jichi starts a language server and speaks LSP over its stdio (`src/lsp/jc_lsp.c`); Godot's server listens on TCP inside a running editor. A bridge as the server's `command` works -- measured on 2026-10-01 with Godot 4.7.2: `socat - TCP:127.0.0.1:6005` 0.6 s, OpenBSD `nc` 1.1 s -- but not every machine has `socat` or `nc`. Decided the same day ([DECISIONS](DECISIONS.md#dec-2026-10-01-godot)): a TCP transport in the client (for example `"tcp": "127.0.0.1:6005"` in an `lspServers` entry) later, when real use of the recipe asks for it. It must refuse cleanly in a build without sockets, as the DOS build does (M835), and needs a config key and a test against a scripted TCP server.
**Where:** `src/lsp/jc_lsp.c` (the transport)

<a id="def-296"></a>
#### Nothing holds the diagram rule: a page can still add a Mermaid-only diagram
**Why:** **Measured** on 2026-10-01: 162 Mermaid blocks on 91 pages outside `docs/i18n/`, no Graphviz, and 155 of the Mermaid blocks with no text diagram within about 4 KB (a rough proxy, which does not count "In words" paragraphs). The operator's rule ([DECISIONS](DECISIONS.md#dec-2026-09-29-diagrams), `DOC_REVIEW.md` §7) brings pages to it as they are revised, not in one sweep, and decided on 2026-10-01 to hold it with a smoke lint whose count can only fall: Mermaid diagrams without their text diagram may not increase, Graphviz blocks stay at zero. To settle when it is built, as `TESTING_RUNBOOK.md` asks: what "has its text diagram" means precisely (for instance a ```` ```text ```` block in the same section as the Mermaid block), the universe stated in the lint's header and enumerated a second way, the floor set at that day's exact count, and each check shown red. Planned for the next documentation milestone.
**Where:** `tests/smoke/` (a new lint), `docs/DOC_REVIEW.md` §7

<a id="def-297"></a>
#### The desktop application is postponed; its design and checklist stay proposals
**Why:** **Postponed by the operator on 2026-10-01** ([DECISIONS](DECISIONS.md#dec-2026-10-01-desktop)), when the options were to build it now, to build it in stages to an evidence gate, or not to build it. Nothing of it exists. What it would need from jichi is listed in the design's §9 (D1-D7, N1-N6) and is not scheduled by the postponement; D5, which lifts the approval gate on a load, was decided separately (def-285).
**Where:** `docs/proposals/2026-09-desktop-gui.md`, `docs/proposals/2026-09-desktop-gui-checklist.md`

## Open — found by the FreeDOS rig (2026-09-29)

`scripts/tier-v-freedos.sh` step 2 (M836): the DJGPP cross-build's unit suite, run on FreeDOS 1.3 under QEMU with DOSLFN,
ran to the end with **14,072 checks and 108 failures**. Every failing line was read and has a cause below; the plan's §7 has the table.

| Deferred | Detail |
|---|---|
| **DOS cannot start another program the way jichi asks.** | [details](#def-269) |
| **DJGPP opens files in text mode unless told otherwise.** | [details](#def-270) |
| **DOS paths: drive letters, no real symlinks, a disk query per drive.** | [details](#def-271) |
| **DOS has no permission bits, so the private-file checks cannot hold.** | [details](#def-272) |
| **DJGPP's clock: a 32-bit `time_t`, and a wrong date without TZ.** | [details](#def-273) |
| **jichi on DOS needs long file names (DOSLFN).** | [details](#def-274) |

<a id="def-269"></a>
#### DOS cannot start another program the way jichi asks
**Why:** **Measured:** 79 of the 108 failures are DOS's process model. 78 need to start another program: a DJGPP program cannot fork, and DOS has no POSIX shell at `/bin/sh`. They show through the shell tool (`jc_app_run_command`, 19), background jobs (17), `jc_proc` (13), the search tools (7), the envelope's verify (6), ACP's terminal (5), user tools (4), the PDF helper (3), grading (2), command expansion (1) and `jc_shell_path` (1). One more assumes a process 1 exists (`jc_lease_pid_alive(1)`). DOS runs one program at a time, so M832's `jc_spawn` needs a synchronous backend here (DJGPP's `spawnvp(P_WAIT)`, with the child's output in a temporary file), and the commands a model writes need a shell: DJGPP's port of bash, or a refusal that names the gap. **Also measured at M838:** `doctor` reports `process backend: fork (posix_spawn not built)` on DOS, where `fork()` always fails, so that line is wrong here until this backend exists.
**Where:** `src/util/jc_spawn.c`, `src/util/jc_proc.c`, `src/platform/jc_platform_posix.c` (`jc_shell_path`), `src/util/jc_lease.c`; the design, for review: [plans/2026-09-29-running-programs-on-dos.md](plans/2026-09-29-running-programs-on-dos.md)

<a id="def-270"></a>
#### DJGPP opens files in text mode unless told otherwise
Closed at M837. **Why:** **Closed at M837:** every stream mode carries `"b"` and every `open()` of a file carries `JC_O_BINARY` (`include/jc_fcntl.h`); `portability_lint` checks 37 and 38 hold both, floored at 78 and 6 calls. The rig's run drops from 108 failures to 105, exactly the three below. The count below was an undercount: the multi-line scan found 27 text-mode stream opens (16 in `src/`, 11 in the tests) and 4 raw `open()` calls. The record: **Measured:** `jc_inhist` appends with `fopen(path, "a")`, which on DJGPP writes CRLF, and reads the file back through the binary `jc_read_file`, so every line keeps a `\r`: 3 failures. The tree has at least 15 `fopen` calls in a text mode, in 10 files, all harmless on POSIX, where the two modes are the same. The fix is `"b"` in every mode (standard C, and a no-op on POSIX), held by a lint, and `O_BINARY` wherever `open()` creates a file that is read back as bytes.
**Where:** `src/util/jc_inhist.c`, and the other text-mode `fopen` calls

<a id="def-271"></a>
#### DOS paths: drive letters, no real symlinks, a disk query per drive
**Why:** **Measured:** 18 failures from path and file-system semantics that the tests and the code assume. An absolute path begins `C:/`, not `/`: `jc_path` (5), `@C:/...` references (2), and a workspace `/w` that `learn` canonicalises to a drive path (3). A free-space query on a path that does not exist answers for the whole drive instead of failing: `jc_disk_space` and the disk guard (5). A write through DJGPP's emulated symlink to `/dev/full` does not fail (3).
**Where:** `src/util/jc_path.c`, `src/command/jc_refs.c`, `src/util/jc_learn.c`, `src/platform/jc_platform_posix.c` (`jc_disk_space`), `src/util/jc_diskguard.c`

<a id="def-272"></a>
#### DOS has no permission bits, so the private-file checks cannot hold
**Why:** **Measured:** 6 failures where a file or directory that jichi makes private (0600 or 0700) reads back with group and other bits set: the event log, the lease file, the input history and the app's state directory. DOS has no owners and no modes, so this is not a bug to fix but a fence that does not exist on this row. **Measured at M838:** `doctor` already says so, detecting it by effect ("chmod 0600 reported success and the mode is 0644"), but its advice is written for MSYS2 (add `acl` to `/etc/fstab`), which means nothing on DOS. What remains: advice that fits the system, and tests that report the missing fence instead of failing.
**Where:** `jc_make_private` and `jc_mkdir_p_private` (`src/platform/jc_platform_posix.c`), and the four tests

<a id="def-273"></a>
#### DJGPP's clock: a 32-bit time_t, and a wrong date without TZ
Closed at M839. **Why:** **Closed at M839:** the cause is DJGPP's C library, which returns garbage local time, and shifts `time()` itself by the same offset, whenever `TZ` is unset, empty or has no offset (`"UTC"`); a valid `TZ` such as `UTC0` is read correctly. `jc_tz_repair()` runs first in `main()` and sets `TZ=UTC0` where the local time is implausible (`jc_tm_offset_plausible`), and `doctor` explains it; the test uses `UTC0`. The 32-bit `time_t` (a 2038 limit on this row) stays as recorded, not changed. The record: **Measured** with a probe booted on FreeDOS: `time_t` is 4 bytes, so this row ends in January 2038. With `TZ` unset, `localtime()` put 2026-08-10 00:00 UTC on 2026-03-24, 139 days off, while `TZ=UTC`, `UTC0` and `GMT0` all gave the right date. `test_sysmsg`'s date line still fails inside the suite although the test sets `TZ=UTC` (2 failures, cause not yet isolated).
**Where:** `src/chat/jc_sysmsg.c`, `tests/test_sysmsg.c`

<a id="def-274"></a>
#### jichi on DOS needs long file names (DOSLFN)
Closed at M838. **Why:** **Closed at M838:** `jc_long_names_ok()` asks DJGPP's `_use_lfn()` at run time (the Makefile probes whether the C library can tell); `doctor` reports ok or FAIL naming DOSLFN, and a state-writing command warns once unless `--quiet`. The rig measured both states and the before/after. The record: **Measured:** the same suite on plain 8.3 names fails 271 checks instead of 108. All 163 extra failures are file names (sessions 67, memory 12, the index 10, agent definitions 9, and more), and a name like `.jichi.d` cannot exist in 8.3 at all. FreeDOS ships DOSLFN but does not load it by default. `doctor` should detect a DOS without long names (DJGPP's `_use_lfn()`) and say what to load, before a session is lost to a truncated name.
**Where:** `src/main.c` (`doctor`), [the plan's §7](plans/2026-09-29-the-fork-less-rows.md)

## Closed at M835 — found by the DJGPP step 0 (2026-09-29)

A cross-compile with DJGPP (gcc 12.2.0), curl-free, no emulator, run for [`plans/2026-09-29-the-fork-less-rows.md`](plans/2026-09-29-the-fork-less-rows.md) §5.
Nothing was executed. The first two rows are not about DOS at all.

| Deferred | Detail |
|---|---|
| **`cc_warn_ok` keeps the compiler's stdout, so a notice printed there becomes a flag.** | [details](#def-263) |
| **The stack protector is on the compile line only; a libc without its runtime cannot link.** | [details](#def-264) |
| **DJGPP hides all of POSIX under `-std=c89`.** | [details](#def-265) |
| **DJGPP lacks four things the tree uses unconditionally.** | [details](#def-266) |
| **DOS has no sockets without Watt-32, and never AF_UNIX.** | [details](#def-267) |

<a id="def-263"></a>
#### cc_warn_ok keeps the compiler's stdout, so a notice printed there becomes a flag
Closed at M833. **Why:** **Closed at M833:** every probe now reads only the exit status, and `cc_warn_ok` accepts a flag only when the compiler takes it silently; `tests/smoke/probe_quiet_lint.sh`, red-first. The record: **Measured:** DJGPP's gcc prints `-fPIC ignored (not supported for DJGPP)` on **stdout** and exits 0 even under `-Werror`; the probe discarded only stderr, so `HARDENFLAGS` held that sentence and all 322 compiles of the step 0 build died in one second on a shell syntax error at `(`. Any compiler that writes a diagnostic to stdout does the same to every probed flag list. The fix is for the probe to discard both streams and to accept a flag only when the compiler says nothing about it.
**Where:** `Makefile` (`cc_warn_ok`)

<a id="def-264"></a>
#### The stack protector is on the compile line only; a libc without its runtime cannot link
Closed at M833. **Why:** **Closed at M833:** probed once as `HARDEN_SSP` and put on both lines. The record: **Measured:** 5,397 undefined references to `__stack_chk_guard`/`__stack_chk_fail`. gcc's driver adds the runtime (`libssp`) only when `-fstack-protector-strong` is on the **link** line, and the Makefile puts it in `CFLAGS` alone; glibc and musl carry the runtime in libc, which hides the omission. One object, linked both ways: undefined without the flag, rc 0 with it. `-fPIE`/`-pie` are already paired this way.
**Where:** `Makefile` (`HARDENFLAGS`, `HARDENLDFLAGS`)

<a id="def-265"></a>
#### DJGPP hides all of POSIX under -std=c89
Closed at M834. **Why:** **Closed at M834:** the `POSIX_UNGATE` probe adds `-U__STRICT_ANSI__` where `pid_t` is hidden under the dialect and visible without the macro, and threads it into all 17 later probes; the DJGPP build compiles all 322 objects. The record: **Measured:** DJGPP's headers put every POSIX declaration inside `#ifndef __STRICT_ANSI__`, which `-std=c89` defines, so `pid_t`, `mode_t`, `struct stat`, `DIR`, `O_CREAT`, `ENOENT`, `SIGKILL` and the rest vanish: 14 of 16 failing files, and three probes (`vsnprintf`, `lstat`, `TIOCGWINSZ`) answered *absent*, which would have selected the width-less fallback formatter. `-U__STRICT_ANSI__` clears all of it with the compiler still in `-std=c89 -pedantic`. It should be probed, as `-D_XOPEN_SOURCE=600` is for MiNTLib: add it only where `pid_t` is hidden under the build's dialect and visible without the macro.
**Where:** `Makefile` (`STD`, the header probes)

<a id="def-266"></a>
#### DJGPP lacks four things the tree uses unconditionally
Closed at M834. **Why:** **Closed at M834:** `HAVE_SYS_SELECT_H` + `include/jc_select.h` (posix_utils_lint 6a bans a direct include), `HAVE_NANOSLEEP` + a `select()` sleep, and guards for `SIGCHLD` and `_SC_PAGESIZE`; both new branches compile in `make fallbacks-compile`. The record: **Measured:** (1) there is no `<sys/select.h>`: `select()` is declared in `<time.h>` and `fd_set` in `<sys/types.h>`, the placement before POSIX 2001, and ten files include the header; (2) no `nanosleep` and no `struct timespec`, which `jc_sleep_ms` uses (POSIX makes `nanosleep` optional); (3) no `SIGCHLD`, which the daemon's worker reaping names; (4) no `_SC_PAGESIZE`, in the Linux `/proc` memory reading. Each is a probe or a guard with a fallback the tree can already express.
**Where:** the ten `<sys/select.h>` includes, `src/platform/jc_platform_posix.c` (`jc_sleep_ms`), `src/main.c`, `src/util/jc_proc.c`

<a id="def-267"></a>
#### DOS has no sockets without Watt-32, and never AF_UNIX
Closed at M835. **Why:** **Closed at M835:** the Makefile sets `JC_NO_SOCKETS` where `<sys/socket.h>` does not exist; the daemon, `--connect` and `jichi control` refuse (exit 2), `--control` warns and continues, `doctor` says so, and the SSRF classifier reads IPv4 literals with `jc_net_parse_ipv4` (held equal to `inet_pton`) and refuses IPv6 ones. posix_utils_lint 6d holds every socket include to the guard. The DJGPP build links from the tree with no shim. The record: **Measured:** no `<sys/socket.h>`, the socket probe UNRESOLVED, three files: the control channel and the daemon (both AF_UNIX) and `jc_http.c`, whose SSRF address classifier needs the socket headers even curl-free. With everything else worked around, the link lacked exactly `socket`, `bind`, `listen`, `accept`, `connect` and `inet_pton`. This is a design question, not a guard: AF_UNIX will never exist on DOS, so the control channel and the daemon must refuse cleanly, while Watt-32 can bring AF_INET back for the model connection.
**Where:** `src/chat/jc_control.c`, `src/main.c` (the daemon), `src/net/jc_http.c`

## Open — found by the FreeMiNT step 0 (2026-09-24)

A cross-compile against MiNTLib, no emulator, run for [`plans/2026-09-freemint-aranym.md`](plans/2026-09-freemint-aranym.md) §8.
Nothing was executed. Its findings closed at M722 and M723 (the next two sections); what
stays open is a re-run that is not about MiNT at all, and a clock question that is.

| Deferred | Detail |
|---|---|
| **On a platform without `clock_gettime`, every deadline in jichi runs on whole seconds.** | [details](#def-96) |

<a id="def-96"></a>
#### On a platform without clock_gettime, every deadline in jichi runs on…
**Why:** `jc_now_millis` falls back to `time()` × 1000 when the probe proves `clock_gettime` unreachable (M326u), and FreeMiNT is the first platform measured to take that branch: MiNTLib has no `clock_gettime` in any header or in `libc.a`. Nothing fails, which is the problem: 23 call sites read this clock, among them the shell command timeout, the envelope's deadline (made monotonic on purpose at M368) and every model-call latency, and each would be measured in whole seconds, silently. `gettimeofday` exists there and is declared under strict POSIX. It is a wall clock, but so is the `time()` the fallback already uses, so a finer fallback would cost no monotonicity the current one has. It needs its own probe, and it should be measured on the guest (step 2) before it is chosen.
**Where:** [plans/2026-09-freemint-aranym.md](plans/2026-09-freemint-aranym.md), `src/platform/jc_platform_posix.c` (`jc_now_millis`)


## Closed at M723 — the four MiNTLib header gaps

The row is kept as it was written, with the closure appended, including the correction it needed.

| Deferred | Detail |
|---|---|
| **MiNTLib's headers differ from glibc's in four places, and the tree assumes glibc's.** | [details](#def-97) |

<a id="def-97"></a>
#### MiNTLib's headers differ from glibc's in four places, and the tree…
**Why:** Measured with gcc 4.6.4 and MiNTLib 0.60.1, and identical for 68000 and `-m68020-60`: (a) `struct timeval` is incomplete at 13 sites in 10 files, because MiNTLib's `<sys/select.h>` only forward-declares it — `<sys/time.h>` defines it, and including that is harmless everywhere; (b) `lstat`, `readlink` and `symlink` are hidden under `_POSIX_C_SOURCE=200112L`, since MiNTLib wants `__USE_BSD` or `__USE_XOPEN_EXTENDED`, and `-D_XOPEN_SOURCE=600` exposes them — a probe, like illumos's `__EXTENSIONS__` (M657c), keeps it off every platform that does not need it; (c) there is no `<sys/mman.h>` and no `mmap`, and `jc_index` already falls back to a copy when mapping is unavailable (M141), so what is missing is a compile-time guard; (d) `tests/test_proc.c` takes `pid_t` from `<signal.h>`, which MiNTLib's does not define. With all four worked around in a scratch copy, 314 objects compiled and the link lacked only `mmap` and `munmap`. Also measured, not a build failure: there is no `clock_gettime`, so `jc_now_millis` would run on its whole-second fallback (M326u), while `gettimeofday` exists and is declared under strict POSIX. **Closed at M723:** the four are guarded for every file, not for the ones that failed: `<sys/time.h>` wherever `struct timeval` is declared (14 files) and `<sys/types.h>` wherever `pid_t` is used (16), a probe that adds `-D_XOPEN_SOURCE=600` only when `lstat` is hidden, and a probe that sets `JC_NO_MMAP` so `jc_index` reads a copy. `portability_lint` checks 28–31 hold each, and `make ci` compiles both fallback paths. The tree now cross-compiles for FreeMiNT clean under `WERROR=1`, for 68000 and `-m68020-60`, and links `jichi` and `run_tests`; nothing has run there. **One correction to this row as written:** (d) is wrong about the header. `tests/test_proc.c` did include `<unistd.h>`, which POSIX says defines `pid_t`; MiNTLib's defines it only at an X/Open level, measured at M723 (the step 0 check had tried `<signal.h>` alone and generalised).
**Where:** [plans/2026-09-freemint-aranym.md](plans/2026-09-freemint-aranym.md)


## Closed at M722 — `-Walloca` on the mandatory list

Found by the FreeMiNT step 0 the same night. The row is kept as it was written, with the
closure appended, because a correction that erases what it corrects teaches nothing.

| Deferred | Detail |
|---|---|
| **The Makefile passes `-Walloca` unconditionally, and every gcc older than 7 rejects it.** | [details](#def-98) |

<a id="def-98"></a>
#### The Makefile passes -Walloca unconditionally, and every gcc older than 7…
**Why:** All 314 compiles of the step 0 build failed in 1.1 s with `cc1: error: unrecognized command line option '-Walloca'`, before reading a line of source. gcc 4.6 rejects it **without** `-Werror` too, exactly as it rejects a made-up flag, so a plain `make` fails as well. It joined the mandatory `WARN` list at M472 (2026-08-17), measured against gcc 13 and clang 18, which both know it; `cc_warn_ok`, the probe M475 wrote for the GCC-only flags, was never applied to it. **Not MiNT-specific:** CentOS 7 (gcc 4.8.5, row V2a, M265) and Debian 9 (gcc 6, row V2f, M273) were recorded green before M472, no milestone since records a re-run of either, and both compilers predate `-Walloca` — so the oldest-toolchain rows are very likely unbuildable today. That is an inference from the versions, not a re-run. The fix used in the scratch copy is to probe it into `WARN_OPTIONAL`. Its check has to fail under a compiler that rejects the flag, and no compiler in the gate does — so the check needs a wrapper compiler that does. **Closed at M722:** `-Walloca` is probed into `WARN_OPTIONAL` by `cc_warn_ok`. A wrapper compiler that rejects it, as gcc 6 does, built and linked `jichi` in 2 s where the old Makefile compiled nothing (180 of 180 compiles failed); gcc 4.6.4 now compiles the 299 files step 0's patched copy did. `portability_lint` checks 26 and 27 hold the mandatory list to the measured gcc 4.6.4 baseline and keep the flag probed, so gcc 7 and later and clang keep the tripwire. The wrapper this row asked for proved the fix by hand instead of joining the gate: a check against the measured baseline also catches the *next* flag an old gcc lacks, and a wrapper written for `-Walloca` would not. The two old rows are expected buildable, not re-run: that is the open row above.
**Where:** [LOW_MEMORY.md](LOW_MEMORY.md), [plans/2026-09-freemint-aranym.md](plans/2026-09-freemint-aranym.md)


## Open — found by the 2026-09-23 review (M713)

A review of the tree after M712, measured before it was written:
[`analysis/2026-09-23-what-to-build-next.md`](analysis/2026-09-23-what-to-build-next.md)
holds the findings, [`plans/2026-09-after-m712.md`](plans/2026-09-after-m712.md) the
designs. M713 changed documentation and added one measurement script; every row
below is something it found and deliberately did not build, with the reason.

| Deferred | Detail |
|---|---|
| **A loop of SUCCESSFUL calls is told, but not stopped.** | [details](#def-99) |
| **The corpora decisions rest on live on one machine, and get pruned.** | [details](#def-100) |
| **Nothing records whether the agent gets better or worse at tasks.** | [details](#def-101) |
| **`main()` dispatches its subcommands through a chain of 54 `strcmp(args.pos[0], …)` branches, each with its own prologue** | [details](#def-102) |
| **The development bench's userland is recorded nowhere.** | [details](#def-103) |
| **Status lines in `plans/` and `proposals/` that say "not built" about built work.** | [details](#def-104) |
| **Ten commits of 2026-09-21 carry no milestone number and no ROADMAP entry** | [details](#def-105) |

<a id="def-99"></a>
#### A loop of SUCCESSFUL calls is told, but not stopped.
*(Half closed at M733: the note. The row was "meets no detector".)* The third identical successful call in a turn -- same tool, arguments and result -- now gets one sentence folded into its result and a `no_progress` journal event. What is still open is the stop: the plan's second threshold, which ends the turn. The drive's 47 turns cannot fit it, and its exit code is item 7's question. **Revisit when** a drive on the M733 build shows whether the note alone ends the loops: its journal's `no_progress` events against the turns that kept repeating after being told. The row as it stood: M432's loop detector is fed failures only (the `if (res.is_error)` branch in `src/chat/jc_agent.c`); M105's editwatch covers repeated edits. M687 made 200 tool calls with **0 errors** after it had the answer, for 4,887,733 tokens.
**Why:** **Not built because its thresholds must be fitted to a corpus first** — M432's own method — and the corpus is not here. On the development bench, `tests/measure/success_repeats.py` finds **20 of 264 turns** *(corrected at M714: **13 of 264** once events that carry the `full` tier are keyed on their whole arguments and a hash of the result)* repeating one successful call five or more times with nothing changed in between, but every hit predates M432 and the post-M432 window is **10 turns**, which the script prints as `NOT EVIDENCE`. **Revisit when** the script has been run over a post-M432 window on the machine that holds one. **Updated the same day:** that machine is not available, so the operator authorised producing the corpus on the development bench instead — a planned headless drive of jichi on zigodot with free models, its telemetry in a directory of its own (plan D1, step 0). **Pilot run the same day (M714):** the harness works and the loop reproduced twice on the current build -- two runs at the 200-call cap with no answer and exit 0, one repeating a single shell command 51 times -- but `search_code` was fixed first, so the fit is made on the fixed build's full drive, in D1's own milestone.
**Where:** plan D1; analysis §3.1; [pilot](analysis/2026-09-23-the-corpus-pilot.md)

<a id="def-100"></a>
#### The corpora decisions rest on live on one machine, and get…
M710's worst session is not on the development bench; M700 found its four sessions pruned before it could replay them; the craft A/B pack was lost this way (M545).
**Why:** Where content-free extracts should be archived — and whether each measurement script should export one with a hash its analysis page cites — is the operator's choice.
**Where:** plan D7; analysis §4.4

<a id="def-101"></a>
#### Nothing records whether the agent gets better or worse at tasks.
`tests/bench/` has no recorded run since 2026-07-27; every gate runs offline, and the driven task proves only that the loop closes once.
**Why:** Needs model time, which is the operator's. **Revisit** at the next tag: one run per release, never a gate.
**Where:** plan D8; analysis §4.5

<a id="def-102"></a>
#### main() dispatches its subcommands through a chain of 54 strcmp(args.pos[0], …)…
the one root of M444, M608 and M693.
**Why:** The largest item the review found. Deferred for **timing, not merit**: to be built in slices, one subcommand family per milestone, when scheduled.
**Where:** plan D9; analysis §3.6

<a id="def-103"></a>
#### The development bench's userland is recorded nowhere.
uutils coreutils since 2026-04-23 — `timeout` among them, the tool the smoke runner kills a hung driver with — upgraded to 0.10.0 on 2026-09-23; no file in `docs/`, `tests/` or `scripts/` mentions it.
**Why:** A `PLATFORMS.md` row is a verdict with its own lints and conventions, so it is a milestone of its own. The first datum exists: `make ci` green at `b284ab18` in 16 m 45 s on that userland. **A second, the same day, and a sharper one:** uutils `timeout -s INT -k 5 1 sleep 3` returns **137 after 1.0 s** where GNU's returns 124 -- it SIGKILLs at once instead of waiting the kill-after grace (measured side by side against `/usr/bin/gnutimeout`). jichi's tier is unaffected (it passes `-k 5` with the default signal); `scripts/corpus-drive.sh` would have been, which is why it prefers `gnutimeout`.
**Where:** plan D10; analysis §2; [pilot](analysis/2026-09-23-the-corpus-pilot.md) §7

<a id="def-104"></a>
#### Status lines in plans/ and proposals/ that say "not built" about…
Three checked by hand; a sub-agent's survey counted about twenty.
**Why:** A policy question comes first — date them, maintain them, or lint them. M711 left both directories out of its sweep as dated records, which is right for a measurement and wrong for a status line written in the present tense.
**Where:** analysis §5.3

<a id="def-105"></a>
#### Ten commits of 2026-09-21 carry no milestone number and no ROADMAP…
one of them a behaviour change that shipped in v0.10.0 (`1d31473d`).
**Why:** Numbering belongs to whichever machine owns the next numbers, and renumbering from here would collide with it. M713 wrote the missing changelog line; the numbers stay owed.
**Where:** analysis §5.1


## Closed at M715 — ordinary telemetry can tell paging from re-reading

Plan D6's third instrument. The row is kept as it was written, with the closure
appended, because a correction that erases what it corrects teaches nothing.

| Deferred | Detail |
|---|---|
| **`reread_ratio.py` can never be fed by ordinary driving.** | [details](#def-106) |

<a id="def-106"></a>
#### reread_ratio.py can never be fed by ordinary driving.
Measured 2026-09-21: it takes its paths from the `--output jsonl` stream, because `tool_call` events carry `args` — and the run journal **deliberately** does not record them. So every interactive session, every TUI run and every headless run without `--output jsonl` contributes nothing, and the corpus stays below the tool's own `SMALL_N = 50` floor, at which it prints `NOT EVIDENCE` rather than a percentage.
**Why:** The fix is not in the tool. Either driving adopts `--output jsonl` as a habit (cheap, and what the supervised zigodot drives now do), or the journal starts recording tool-call paths — which was decided against on purpose, since a journal that records every path read is a different privacy proposition from one that records what changed. That trade is a decision, not a bug, and wants the operator.<br>**A third route, found at M713 (2026-09-23), that needs neither.** Telemetry already records `read_file`'s path at the default `metrics` tier — as the argument *summary*, which is the path alone; the full arguments are written only at the `full` tier. So ordinary telemetry cannot tell paging from re-reading, which is the trap M287 retracted a finding over. A summary that also carried **offset and limit** — two numbers, no content, read as leniently as the tool reads them — would let every ordinary session feed this measurement without moving the journal's privacy line. Plan D6 in [`plans/2026-09-after-m712.md`](plans/2026-09-after-m712.md). <br>**CLOSED AT M715 (2026-09-23) — the third route was built.** A `read_file` `tool_call` event now carries `offset` and `limit` beside its path: the range the tool *executed*, which `read_file` reports in its own result after argument repair (M148) and unwrapping (M172), so no second parser can disagree with it. The argument summary is untouched — it is also what the TUI prints and a screen reader speaks. `reread_ratio.py` gained a telemetry route, printed apart from the stream route and never summed with it, with a by-path ratio (any build) and a by-range ratio (M715+ events). Exercised on the new build: reads of lines 1–2, 3–4 and 1–2 again measure **67 % by path and 33 % by range** — the paging the old key could not see. So every session that writes telemetry now feeds this measurement, and the journal's privacy line did not move. **The measurement itself stays open** — M326z's row under *Open — tool-output cost* still needs a second workload of real size. `tests/smoke/read_range.sh` holds the teeth.
**Where:** `tests/measure/reread_ratio.py:30-48`


## Closed at M711 — two rows the documentation sweep found finished

Both were **open rows whose work had shipped**, which is the state this page calls
its single unacceptable one. Neither was found by a lint: `deferred_register_lint`
checks that no row is *marked* closed under an Open heading, and deliberately does
not check whether a reason is still true ("that is the M326b rule and it is
deliberately a human's job"). A fleet documentation sweep found them by reading.

| Deferred | Detail |
|---|---|
| ~~**Diagnose the remaining failing smoke drivers on illumos.**~~ | [details](#def-107) |
| ~~**A "what this course does not teach" section in `CURRICULUM.md`**~~ | [details](#def-108) |

<a id="def-107"></a>
#### Diagnose the remaining failing smoke drivers on illumos.
*(238 of 300 at M658 → 249 of 301 once the rig made the shipped tree a git repo → 279 of 301 once the pty cluster was fixed, M661. The **pty cluster is closed**: 19 of 19. **And so is the row: illumos has been GREEN since M703, 2026-09-22 -- 317 of 317 drivers, coverage debt 0.** Nothing is left to diagnose.)* *(62 at M658; ten of those were the rig not making the shipped tree a git repository, fixed at M660 — so ten "illumos failures" were never about illumos.)* **— CLOSED at M711.** illumos is green: 317 of 317 drivers, coverage debt 0, since M703 (2026-09-22). There is nothing left to diagnose.
**Closed by:** **~19 are pty-driven** (`tui_*`, `typeahead*`, `paste*`, `typed`, `tab`, `editor`, `approval_keys`, `view_key`, `slash_leading_space`, …) and point at `tests/tools/ptydrive` against illumos pty semantics — one cause would plausibly explain the whole cluster, which is why it is worth one sitting rather than nineteen. **Most of the rest are text-tool lints** (`bibliography_lint`, `man_page_lint`, `describe_names_lint`, `doc_claims_lint`, `i18n_tracks_lint`, `config_defaults_lint`, `superseded_marker`, …), and the **hypothesis** — untested, and named as one — is the cause the product already hit: illumos ships the legacy Solaris `grep`/`sed`/`awk`, and the tier's own text tooling may assume GNU behaviour. `posix_utils_lint` already polices part of that family and caught a GNU-only `\|` in this milestone's own driver, so if the hypothesis holds, the interesting question is why its universe missed these — but that question only exists once the cause is known. **It is not known.** Saying which cluster a driver is in is not saying why it fails, and this row deliberately stops where the measurement stopped. **Revisit** when the pty cluster is worth a sitting; the rig makes the row one command now (`scripts/tier-v-illumos.sh`).

<a id="def-108"></a>
#### A "what this course does not teach" section in CURRICULUM.md
(the second half, "data structures" → "manual memory" in the 51–54 line, was done at M636g). **Narrowed at M636k:** the premise moved — a hash table (78), a linked list (79) and an ordered map / BST (80) are now taught, each built to a contract and measured, so the section would today name balancing, deletion in a tree, sorting algorithms and graphs, not "data structures". Whether to write it at all stays the operator's call about what this course is. **— CLOSED at M711.** `CURRICULUM.md:269` has carried `## What this course does not teach` since 2026-09-21 (commit a9a4e3a2). The row's reason still argued from "zero matches for 'does not teach'", re-checked at M658 and never re-checked after the section landed.
**Closed by:** `CURRICULUM.md` states **no scope boundary at all** (zero matches for "does not teach", "out of scope", "not a course in" — **re-checked 2026-09-18 (M658): still zero, all three**; the corpus is 524 markdown files now, not the 505 the review counted), so a learner can finish four stages believing data structures were covered. They were not: taught are a growable array (52), a ring buffer (04), an arena (54), an RPN stack (50, 62) and the C++ containers (60–61); **never taught are linked list, hash table, tree, binary search or any sorting algorithm**. The boundary is *correct* — jichi has exactly one general container (`jc_vec`) and a course that reads its own source cannot teach what the source lacks — but the page's own words ("manual memory & **data structures**") let a reader infer more than one task delivers. Same species as the "modern" C++ finding. *Revisit when:* the section exists and the line says the smaller true thing. **Planned 2026-09-16**, and the plan resolves it better than by softening: splitting off a real **"C: files & structures"** course (tasks 76–80) makes the old course's title true by deleting one word rather than by editing a claim down.


## Closed 2026-09-21/22 — the reply that was cut, and the page that was severed

The measurement stood and the proposed fix did not, which is the useful half.

| Deferred | Detail |
|---|---|
| ~~**A seeded fuzz-lite harness for the pure cores**~~ | [details](#def-109) |
| ~~**Two cells on `PLATFORMS.md` are severed, and their tails render as prose below the table.**~~ | [details](#def-110) |
| ~~**A reply truncated by the OUTPUT CEILING is recorded as a clean finish.**~~ | [details](#def-111) |

<a id="def-109"></a>
#### A seeded fuzz-lite harness for the pure cores
**Closed 2026-09-22 — and the row's own correction was right: it was three targets, not a harness.** The harness shipped at M658; what was missing were targets for `jc_patch`, `jc_utf8` and `jc_jsonrepair` (`glob` had one from the start). **19 -> 22.** `jsonrepair` is a Tier-2 PROPERTY target, because its header states one: *"Returns a malloc'd string that cJSON_Parse ACCEPTS"* — so a non-NULL return that does not parse aborts, and a wrong repair executing a tool with arguments the model never meant is exactly what that repairer exists to prevent. `utf8` walks the buffer with next/prev and **asserts progress** rather than breaking out of the loop: the header says *"byte index just past the codepoint at `pos`"*, so below `len` it must advance, and a `break` would have passed a walker that stopped walking. **Teeth, per the row's own rule that a generator which has never found a planted bug has never been seen working:** a planted no-progress in `jc_utf8_next` -> *"made no progress at 10 of 11"*; a planted invalid repair -> *"jsonrepair returned a string cJSON_Parse rejects"*; a planted out-of-bounds read in `jc_patch_count` -> **ASan heap-buffer-overflow**, under `make fuzz SAN=1`. All three planted bugs removed and the tree verified clean.
**What closed it:** Was: three targets remaining.
**Where:** `tests/fuzz/jc_fuzz_targets.c`

<a id="def-110"></a>
#### Two cells on PLATFORMS.md are severed, and their tails render as…
**Closed 2026-09-22.** The ambiguity the row parked on was resolvable, and by the method the row itself named: ask the commits. The illumos row was WHOLE at M661 and split at **M661b**, which appended to the cell and let the text run past the line; M678, M681 and M683 then appended more to the orphan. The second orphan dates to **M479**, where the block followed `| **OpenBSD**
**What closed it:** **Partly verified -- 207 of 209 smoke drivers**` -- a row written unterminated from the start, which is why its tail ends in OpenBSD's own *"Needs `gmake`"* and reads as FreeBSD's. Neither join was guessed. Both tails now sit in `###` sub-sections, the pattern the same page already uses for the Cygwin, WSL2 and FreeBSD rows, with a note saying what they were and nothing but their placement changed. `portability_lint` **check 16b** now refuses the direction check 16 cannot see -- in a table region, a row that opens with `|` must close with one -- and it was born red on this exact damage, naming line 144. | Was: ambiguous ownership; widen check 16 in the same pass.
**Where:** `docs/PLATFORMS.md`, `tests/smoke/portability_lint.sh`

<a id="def-111"></a>
#### A reply truncated by the OUTPUT CEILING is recorded as a…
**Closed 2026-09-21.** The measurement stood; the proposed fix did not. Nothing needed to cross provider -> agent: M334 already carried the flag to `msg->truncated` and `jc_agent.c` already carried that to `app->last_response_truncated` for the tool layer. Only the LAST hop was missing. And it is **not** a new `JC_STOP_*` value -- the enum records why the RUN stopped, and this run finished; a new wire value would reclassify a completed run on an interface EMBEDDING.md declares stable, and could not represent a cap on a non-final turn. It is carried beside the stop reason, the way `verifier_concluded` is, composing with it rather than overriding it. Making `answer_truncated` true was only half: the footer renders `the run %s` from a `stop_clause` that is NULL for a clean stop, so it would have said *"the run did not finish"* about a run that did -- a true flag through a false sentence. The footer now names the ceiling and the remedy. Proven on live capped runs: `stop_reason: done`, `answer_capped: true`, and the control (`maxTokens` 4096) carries no key.
**What closed it:** Was: a new stop value, the reach footer's clause, the `done` event's field.
**Where:** `include/jc_outcome.h`, `src/util/jc_reach.c`, `src/util/jc_agentjson.c`


**The design decision, because it is the part worth keeping.** `DEFERRED.md`
proposed a new `JC_STOP_*` value, and `-Wswitch` would indeed have forced every
renderer to handle it. It is still the wrong shape: the enum answers *why the
RUN stopped*, and this run did not stop — it finished, kept its work, and
returned. A new wire value would have reclassified a completed run on an
interface `EMBEDDING.md` declares stable, and it could not have represented a
cap on a **non-final** turn at all. Truncation is a property of the **answer**,
so it is carried beside the stop reason exactly as `verifier_concluded` already
is, and the two compose: a capped answer is incomplete whatever the stop reason
says, and a clean stop reason no longer overrides it.

**What the register had wrong about the plumbing.** The row said the fix
*"crosses provider → agent → outcome and wants its own milestone"*. Two of those
three hops already existed: M334 put the flag on `msg->truncated` for both
providers through one funnel (`jc_prov_flush`), and `jc_agent.c` already carried
it to `app->last_response_truncated` for the tool layer. Only the last hop was
missing. Read the tree before pricing the work.

**And the half that would have been missed.** Setting `answer_truncated` is not
the fix on its own: the footer renders *"the run %s"* from `stop_clause`, which
is NULL for a clean stop — so a correct flag would have printed *"the run did
not finish"* about a run that finished. A true fact rendered through a false
sentence is worse than the silence it replaced, because it reads as a diagnosis.
`tests/test_reach.c` pins both directions.

## Closed at M680 — `bibliography_lint` check 3, and an awk without intervals

**Diagnosed on a live illumos guest**, which is the only way it could have been:
the rig tears its VM down, and this failure is invisible from the host. One
`--keep` run and a shell on the guest answered it in three commands.

illumos ships **one-true-awk "version Aug 27, 2018"**, which does **not support
ERE interval expressions**. Measured there:

```
$ echo "a{4}b" | awk '/a{4}b/ {print "matched"}'
matched
```

`{4}` is a literal brace, not a repetition count. So check 3's date patterns —
`[0-9]{4}-[0-9]{2}-[0-9]{2}` — matched **nothing** on that platform, and the
entries that still passed were the ones hitting the fourth alternative,
`\[DOI `, which carries no interval. That is why the failure looked like a
handful of odd entries rather than *"the regex does not work here"*.

**It fails silently, which is the part worth remembering.** A regex that matches
nothing is not an error. The check went on reporting, in good faith, that
correctly-marked entries were unmarked.

Fixed by spelling the dates out (`[0-9][0-9][0-9][0-9]`), verified **on the
guest**: 9 of 9. The same run also **established** what M679 had left
unestablished — `tutorial_refs_lint` now passes there, so the unescaped `/`
inside a bracket expression really was that row's cause.
`posix_utils_lint` check 21 bans intervals inside awk regex literals tier-wide;
in `grep -E` they are POSIX and this tree keeps using them there.

**Eight of the original ten remain**, and they do not share this cause — only
two even contain an interval pattern and both are in `grep -E`. One of those,
`i18n_tracks_lint`, uses `grep -oE`, which points at the `grep -o` first-match
problem that is item 3 of the list above.

## Closed at M681 — the tier's `grep -o`, and a mechanism the register had wrong

| Deferred | Detail |
|---|---|
| ~~**Make the tier's `grep -o` extractions portable**~~ | [details](#def-112) |

<a id="def-112"></a>
#### Make the tier's grep -o extractions portable
**— done at M681, and the recorded mechanism was wrong.** The register said *"`grep -o` returns only the FIRST match per line on illumos"*. Measured on a live guest, that is false as stated: `printf "item1 item22 item333\n" \| grep -o "item[0-9]*"` returns **3**. The trigger is a line with **no trailing newline** — the same input without the `\n` returns **1**. On the real case, `jc_config.c` flattened, it was **1 against 593**, and appending a newline restores all 593. **The wrong mechanism made the work look thirty times bigger**: the predicted shape, `tr '\n' ' ' \| grep -o`, occurs **zero** times in the tier; the real shape is a shell variable printed with `printf '%s'`, and there were **11 sites in 6 files**, each a one-character fix. It cost `config_defaults_lint` its first check on that platform (*"only 0 defaults were comparable — the extraction broke"*), which now reports *"compared 13 documented defaults"* there. `posix_utils_lint` check 22 prevents recurrence; `grep -q` is deliberately not covered, since a match is a match either way.


## Open — instrumentation and cost

| Deferred | Detail |
|---|---|
| **A workflow stage's model call is invisible to telemetry.** | [details](#def-113) |
| **The same craft A/B on a frontier model** | [details](#def-114) |

<a id="def-113"></a>
#### A workflow stage's model call is invisible to telemetry.
Measured 2026-09-17: a plain `-p` call adds one `model_call` event to the telemetry stream; the same call made from a workflow `synthesize` stage adds **nothing**, because that path goes through `jc_oneshot_ex` while the sink is wired into the agent loop. A multi-agent workflow's cost, latency and model attribution are therefore unmeasured, and `jichi telemetry` under-reported a strong model as one call when several were made.
**Why:** The fix is not one line: `jc_oneshot_ex` has no app handle, so either the sink is threaded into it or the runner emits the event around it -- and the second risks double-counting the stages that DO go through the loop (`map`, `refute`). M643 fixed the model SELECTION and deliberately left the measurement gap open rather than guess at the shape. **Revisit when** a workflow is run for a measurement rather than for an answer; that is the first time the missing numbers are actually needed.
**Where:** [ROADMAP M643](ROADMAP.md)

<a id="def-114"></a>
#### The same craft A/B on a frontier model
with a task whose deliverable is unstated and whose output a human grades.
**Why:** M318 measured one 31B model and shipped the conclusion its evidence licenses (off under `--lite` only). The frontier case is where the section's claimed value lives. **Checked (M326b): no frontier model is reachable from this machine** — all five configured endpoints are HRZ-hosted (`jlu/gemma-4-31b-it`, `jlu/qwen3-coder-next`, plus embed/rerank), so this was genuinely resource-blocked and not merely unattempted.<br>**Operator's statement (2026-08-06): they will supply an API key with frontier-model access for this test.** So the blocker moves from *"no such model here"* to *waiting on the key* — and the entry is kept rather than closed, because the key is only **one of three** things the experiment needs. The other two do not arrive with it: a task whose deliverable is genuinely **unstated** (every graded curriculum task names its deliverable, which is exactly why M318's pass-rate result was uninformative), and **a grader who is not the author of the section under test** — blind pairs for the operator to grade is the clean form. **Harness and tasks built 2026-08-07** (M326g): `tests/bench/craft_ab/`, three unstated-deliverable tasks, blind pairwise grading, pre-registered in [proposals/2026-08-craft-ab-frontier.md](proposals/2026-08-craft-ab-frontier.md). What remains is the operator running and **grading** it.<br>**Run attempted 2026-08-10 (`session-01`): 18/18 errored on the key budget.** Two runs answered (~96k input) and consumed the key's remainder; sixteen then failed in under a second each with the gateway's own `429 budget_exceeded`, which named the key, the spend and the cap (machine-verifiable, and far more actionable than a bare 429). The 2026-08-07 pilot ran under a different key (`JC_DEV_KEY`, not on this machine). So the blocker is now **the key budget, not the harness**: reachability had been checked, the per-key budget had not — the M326b shape, again, in our own register. Unblock is either a budget raise/reset on that key (the operator report should carry the quota finding) or the pilot's dev key; then the session is one command (a fresh three-pair run under a new label, then the blinding step, then the operator grades). The over-budget key 429s **every** model including `jlu/*`, so ordinary jichi work on this key is blocked with it.<br>**Run completed 2026-08-10 (`session-02`, on the dev key the operator supplied): 18/18 runs `done`, zero truncations** — ~3.73M input / ~54k output on `anthropic/claude-opus-4-5`, ~20 minutes, preflight proving the arms differ (+1316 bytes in ON). The blinded pack was built, and **that pack is gone (checked 2026-08-22, M545).** `results/` is in `.gitignore`, nothing committed it, and the directory exists on no machine here — so ~3.73M input tokens of frontier data produced **no result**, because the one step a machine cannot do was also the slowest and the artifact did not outlive the wait. A writer produced something its reader could never read, and this row went on naming the path as "exactly one thing remaining" for twelve days. The spend is unrecoverable; the frontier question is **open again and now costs money to reopen**. M545 makes `blind` print that the pack is the only copy, with the one command that preserves it (archiving `grading/` alone keeps the blind, since the arm mapping lives in `.sealed/`).<br>**Superseded in practice (M545):** the operator's standing rule is local and free models only, so the frontier arm is not re-run. A **fresh pre-registration on `jlu/qwen3-coder-next`** asks the question that is actually actionable — the craft section ships **on** by default for every non-`--lite` model, that model is one, and nothing has tested it there. M318 measured a 31B (`jlu/gemma-4-31b-it`, no benefit) and this proposal registered a frontier class; the new run is neither, and says so.
**Where:** [analysis](analysis/2026-08-06-craft-ab.md), [§7](analysis/2026-08-09-hrz-gateway-findings.md)


## Open — the graded-attempt cost chain

The measured chain for one 1-point task is **128k → 66k → 29k → 9.4k tokens** (M309, M310,
M312). One lever is still a recommendation rather than a default.

> **Removed from this list (M325b):** *"make `--tool-profile core` the default for `attempt`"*
> carried the reason *"it costs the hint ladder"* — which **M319 and M320 measured away** (two
> models, 24 runs, zero `hint` calls, six of them failures with the tool advertised). M320 then
> refused the flip on a *better* reason and moved it to [`DECISIONS.md`](DECISIONS.md), but this
> row survived with the refuted argument still attached. A register carrying a reason known to be
> false is worse than one missing the row: it invites a future reader to re-open a settled
> question with a dead argument.

| Deferred | Detail |
|---|---|
| **Making `repoMap: false` the default for `attempt`.** | [details](#def-115) |

<a id="def-115"></a>
#### Making repoMap: false the default for attempt.
*(reason replaced, M326 — the old one was false)*
**Why:** The stated blocker was *"tasks 20–22 are explicitly about reading this repository"*. **They are not** — all three are self-contained fixtures, checkable by reading them. The measured reason: 18 runs found no pass-rate difference (4/9 vs 3/9) and a 15–62% token saving, but **+67% and +50% more model calls** without the map, which on the hardest task cancels the saving exactly (408k → 409k). And the leaner arm went 0/3 there — the second time after M320 that a leaner prompt goes 0/3 on the hardest task. To change the default, disprove the call inflation on tasks the model comfortably passes.
**Where:** [analysis](analysis/2026-08-06-repomap-navigating-tasks.md)


## Closed at M710 — from the M321 large-workload measurement, the last item

A 34,216-event log from a private third-party workload
([analysis](analysis/2026-08-06-large-workload-telemetry.md)). Its four findings shipped as
M321 (transport diagnosis), M323 (compaction short-fall, observability only), M324 (the `glob`
gap) and M325 (`spawn_parallel`). **One item was left, and it was a design question rather than
a measurement** — which is why it outlived the others by 389 milestones. It is now decided, and
this section is closed: **none of the three options, because the harm they prevent has not
occurred.** The reason is kept in full, because a decision whose evidence is deleted gets
re-opened.

| Deferred | Detail |
|---|---|
| ~~**Decide what jichi should DO when mid-turn compaction cannot reach its target.**~~ | [details](#def-116) |

<a id="def-116"></a>
#### Decide what jichi should DO when mid-turn compaction cannot reach its…
M323 made the short-fall visible (event fields, a once-per-turn warning, a summary line); the behaviour is unchanged — the request still goes out over the configured `contextLimit`.
**Closed by:** Three options, all lossy in different ways, and **the costs are not equal — checked (M326b), where the entry had implied they were**:<br>**(a) drop old messages** — `jc_history_drop_front` exists, and `jc_compact_find_cut` already knows how to snap a cut to a user-message boundary so a `tool_call`/`tool_result` pair is never split. *Cheapest: the machinery is there.* Loses work the agent may still need.<br>**(b) summarize mid-turn** — `summarize_call` is internal to `jc_compact.c`, so no new plumbing, but it means a model call inside a turn that is already over budget.<br>**(c) refuse the call** — **new code**: `jc_text_is_context_overflow` only recognises a *server's* rejection after the fact; there is no pre-send refusal path. Turns a degraded run into a failed one.<br>Picking one is a judgement about what is acceptable to lose, and wants a workload to measure against. **Checked (2026-08-10 sweep): the local telemetry cannot supply that workload -- 364 events since 08-07, zero compact events; the M321 third-party log remains the only pressured corpus.** **A local pressured corpus now exists (M459): 7 mid-turn compactions, 7/7 `unrelieved`.** It did not settle (a)/(b)/(c), and it changed the question. The run's short-fall was caused by an **under-declared window**, not by a genuinely full one: `contextLength` said 32000 while the server's real `max_model_len` was **256000** and it had been accepting ~160k-token requests throughout. So jichi compacted seven times toward a target it never needed, and M323's warning advised shrinking tool output when the fix was one config number. **Before deciding what to DO in this state, jichi now checks whether the state is real** — `last_prompt_tokens` is the server's own count for a request it ACCEPTED, so a served request larger than the declared limit proves the limit understates the model, and the operator is told that instead (`tests/smoke/context_underdeclared.sh`). That removes the commonest false instance of this row's condition from the population it has to decide about; a workload that presses a CORRECTLY declared window is still owed.<br>**DECIDED AT M710: NONE OF THE THREE, AND THE PREMISE WAS WRONG.** The workload arrived -- **248 pressed passes against a window nothing proves too small**, out of 49,600 events -- and it does not choose between (a), (b) and (c); it removes the harm they exist to prevent. **0 context-overflow rejections in the entire corpus -- and 0 in the 34,216-event corpus that OPENED this row**, which said so itself and was never read that way: *"No HTTP 400 was ever returned, so the declared window was conservative and no turn died of it."* Every request that went out after a short-fall was **served**: 0 of 234 counted over the declared limit by the server itself, closest **98.0%**, median **86.2%**. This row's own words -- *"the request still goes out over the configured `contextLimit`"* -- are false here, because `target` is **60%** of the window and the trigger is **80%**, so missing a comfort mark was being read as an overflow. Priced anyway: **(a)** must close a median **31,311**-token gap -- **43,054** in the worst turn, which is **2.1x everything that turn's 120 `run_tests` results produced**, so it cannot be satisfied by dropping tool output at all and has to reach past the results into the conversation; **(b)** adds a model call to **243 of 248** passes, **228** of which re-trigger immediately and pay again -- 128 extra calls in the worst single turn; **(c)** would have **failed 243 requests the server served**. **And the estimate is not what is wrong**: |served - `after`| median **251** tokens, 226 of 234 within 5%, so the trigger fires on a true reading. **What the data asks for instead is a new row** (below): the pass **exhausts** -- 1st pressed pass of a turn reclaims 19,465 tokens, the 11th-and-later reclaim **161**, and those are 78% of all passes. [analysis](analysis/2026-09-22-compaction-decided.md), `tests/measure/compaction_pressure.py`; behaviour documented in [COMPACTION.md](COMPACTION.md).


## Open — gate integrity

| Deferred | Detail |
|---|---|
| **Flipping `--strict-green` on by default.** | [details](#def-117) |
| **A gate rehearsal that proves a goal gate satisfiable** | [details](#def-118) |
| **The universe floors are calibrated far below the universe.** | [details](#def-119) |
| **`accessible` runs at 95% of its deadline on the reference bench.** | [details](#def-120) |
| **The daemon's error replies are not reliably delivered where AF_UNIX is TCP-backed.** | [details](#def-121) |

<a id="def-117"></a>
#### Flipping --strict-green on by default.
**Why:** M332 shipped it opt-in because the flip changes a currently-zero exit code (a stable interface) and the false-positive rate was unmeasured. **Measured (M343, retroactively from 138 existing journals): 0 downgrades in 21 scoped green runs** — the incidental lock-file FP never occurred; the two flagged runs ended non-ok, which strict-green ignores. Still deferred because the evidence is one project and one operator's gates, and the change is to a stable-tier contract — **the operator's call now, with a number instead of a fear**. Re-run `tests/measure/strict_green_fp.py` as corpora grow; a second project's corpus at 0 FPs is the natural strengthening. **Re-run (2026-08-10 sweep): 94 local journals, 16 completed runs, 0 with an edit scope -- nothing strict-green could downgrade either way; the M343 0/21 stands as the only number.** **Re-run (M459, a genuinely second and third project): jichi driven headless against **chrtext** and **zigodot**, each fenced to one named file — **0/2 downgrades**. Small, and said plainly: two runs, each a single-file documentation edit, so this strengthens M343's 0/21 without transforming it. Combined 0/23. **The more useful half of that re-run was a defect in the measurement itself:** the journal recorded only the *count* of edit-scope globs, so `--edit-scope AGENTS.md` and `--edit-scope '**'` were indistinguishable — and seven concurrent fleet runs, all `'**'`, would have contributed seven free zeroes to a rate that cannot be falsified. The journal now records `edit_scope_globs` and the script excludes vacuously-scoped runs from the denominator (older journals are counted as before rather than guessed at). A rate computed over fences that fence nothing is the shape of evidence this row was right to distrust.<br>**MEASURED AGAIN AT M662, AND THE ANSWER REVERSED.** The M657 recommendation below is **withdrawn**, and it is left in place because a withdrawn recommendation that leaves no trace gets re-proposed (M406/M407's rule). On the corpus as it stands — 91 journals, 63 completed runs, 42 with an edit scope, **35 ending `ok`** — strict-green would downgrade **16 of 35: a 46% rate**, against M343's 0/21 and M459's 0/2. Flipping the default would have failed nearly half of the operator's successful driven runs.<br>**And the classification says why.** Of 1,158 flagged paths (663 distinct): **35% downloaded or generated binaries** (an arxiv run's `.papers/cs_AI/*.pdf`), **30% build artifacts and temporaries** (`*.o`, `*.so`, `*.beam`, `test_0.tmp`), 10% jichi's own `.jichi/` state, 5% ignore files the run itself wrote — **85% is the work's own output**, and only 15% is source at all. M332's own words name the plausible false positive as *"an incidental shell-written file"* and the genuine one as *"the gate edited through the shell"*; **strict-green cannot tell them apart**, and that is the finding rather than the rate.<br>**Why 0/21 looked safe:** that corpus was this project's own tidy gates, and M459's two additions were single-file documentation edits. The moment jichi drives real work on another project, its build writes files.<br>**So: not the default flip, and not a `doctor --unattended` escalation either** — the second was additionally wrong on its own terms, since `--unattended` is a *doctor* flag and the escalation would have meant doctor DEMANDING a setting that fails 46% of real runs. **What the data asks for instead** is a rule that discriminates, and the sharp line is **tracked vs untracked**: a gate file is in version control, a downloaded PDF is not. The journal does not record trackedness, so that is the next step and the measurement's own output now says so. `tests/measure/strict_green_fp.py` classifies the paths as of M662, because telling a reader to "read the flagged paths" meant reading 663 of them.<br><br>*Withdrawn M657 recommendation, kept for the record:* **Recommendation (M657), because this row has said "the operator's call now" since M343 and a question nobody is asked has not been deferred, it has been dropped:** do **not** flip the global default. The objection stands — it changes a currently-zero exit code on a stable-tier contract — and 0/23 is a small denominator that would get cited as though it were large. Flip it **under `--unattended` only**, joining M158b's escalation set, which already holds `privilegedAudit: false` and, since M503, the private-files probe, for exactly this reason: an unattended run has nobody to read a warning, and its exit code is the whole interface. Interactive behaviour is untouched, so the contract a human depends on does not move; the runs where a silent hollow green actually costs something get the stricter rule. The measurement stays what it is (0/23, three projects, two of them single-file edits) and the change no longer needs it to be more. That is a milestone with its own teeth, and it is the operator's to take — but with a proposal in front of it rather than an open question.
**Where:** [GATE_INTEGRITY.md](GATE_INTEGRITY.md) §8b

<a id="def-118"></a>
#### A gate rehearsal that proves a goal gate satisfiable
run the verifier against a stub or hand-completed fixture and confirm green, then red without it (the curriculum's two-sided grader bar, ported to working gates; TEST_INTEGRITY recommendation #1 is its unit-suite sibling).
**Why:** M343's declaration checks the red side for free (a declared goal must be red at start) but cannot prove the green side: that needs a *reference completion*, which only the operator can supply. The manual discipline is a standing rule (ANECDOTES #38: prove the gate green by hand first). **Revisit when** a run has a natural artifact to rehearse against — e.g. `attempt`'s reference solutions, or an operator-supplied stub patch.
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md)

<a id="def-119"></a>
#### The universe floors are calibrated far below the universe.
Measured 2026-09-21 while fixing the empty-operand-list family: `sprintf_lint` floors at **100** files and actually scans **327**, so a 70% loss of its corpus passes the floor and the check reports green on a third of the tree. `arena_lint` floors at 174 against 180, which is tight; the two are not consistent with each other.
**Why:** A floor exists to catch *the extraction breaking*, and for that a near-exact number is right — the loose ones catch only total collapse, which is the case a `< /dev/null` guard now handles anyway. The fix is a sweep: for each floored check, re-measure and set the floor near today's count, the way `curriculum_universe_lint` does ("floor 88 -- today's exact count"). Deferred because it is ~20 drivers and each needs its own measurement; doing it by estimate would reproduce the defect.
**Where:** `tests/smoke/sprintf_lint.sh:34`, `tests/smoke/arena_lint.sh:101`

<a id="def-120"></a>
#### accessible runs at 95% of its deadline on the reference bench.
Measured 2026-09-21: **57 s** against the **60 s** limit it gets from `tests/smoke/run.sh` (named line 253, in the block ending `run_driver "$t" 60` on line 267), with standalone timings of 53.9 s and 57.5 s the same day. In-tier it tips past the limit, is killed, and reports FAILED while all 22 of its checks have printed `ok`.
**Why:** **It is a decision, not a patch, which is why it is here.** Raising the limit is the obvious move and is also a cap change: 60 s presumably bounds a hang, and 120 s bounds it half as well -- on a driver that already fails in a way no check reports. The alternatives are making it cheaper (it drives five pty sessions), splitting it, or moving it to the 120 s group it already resembles in cost. Picking one in passing, while re-measuring a platform row, is how a margin becomes a number nobody chose. **Revisit with** [`proposals/2026-09-calibration-tier.md`](proposals/2026-09-calibration-tier.md) §4, which would make this class of defect visible on every run instead of on the day it fails.
**Where:** `tests/smoke/run.sh:200`, `tests/smoke/accessible.sh`

<a id="def-121"></a>
#### The daemon's error replies are not reliably delivered where AF_UNIX is…
`run_daemon` writes its `limit.line` refusal and calls `close(connfd)` immediately (`src/main.c`), with the rest of the oversized request still unread. Measured on Cygwin 2026-09-21, 8 repetitions per size against the 1 MB cap: a 1 KB overrun is answered **8/8**, 16 KB **2/8**, 71 KB **3/8**, 512 KB **0/8** -- the caller simply gets nothing. Linux delivers in every case. The daemon itself stays healthy throughout (a normal ping answers `pong` immediately after).
**Why:** **Draining before close is the obvious fix and is a decision, not a patch.** The reply is lost because the close resets a connection with unread data, so delivering it means reading and discarding the remainder first -- and this accept loop is **single threaded**, so an unbounded drain hands an attacker a stall and a bounded one may still reset. `SO_LINGER` is a third shape with its own platform variation. None is obviously right, and the failure is **degraded rather than dangerous**: the caller gets silence instead of a named error, while the case that actually matters -- a truncated request served as a valid one -- does not happen. `tests/smoke/daemon_auth.sh` no longer depends on it: it now overshoots the cap by ~1 KB, which tests the same code path without a large unread backlog. **Revisit if** the daemon ever gets a real TCP transport, where this stops being a Cygwin curiosity, or if the accept loop stops being single-threaded and a bounded drain becomes cheap.
**Where:** `src/main.c` (`limit.line`, and the `bad request` reply below it)


## Open — invariants known to be incomplete

| Deferred | Detail |
|---|---|
| **A writer object for the system prompt** | [details](#def-122) |
| **The setup wizard offers macOS's sound commands to every non-Linux host.** | [details](#def-123) |

<a id="def-122"></a>
#### A writer object for the system prompt
where every append names its section, so a section *cannot* be added anonymously.
**Why:** Today `sum(parts) == total` catches a section appended after the last mark, and the zero-slot assertions catch most of the rest; an insertion between two *active* slots is credited to a neighbour. **Counted (M326b): the "~40 call sites" is accurate — 38 `jc_sb_append` calls inside `jc_sysmsg_build_parts` (57 in the file, counting the helpers).** **Re-counted 2026-09-18 (M658), and the halves aged differently:** inside `jc_sysmsg_build_parts` it is still **exactly 38**, so the number this row's cost estimate rests on has not moved in over 300 milestones; the file-wide figure is now **81**, and `mark()` is **18** rather than 16. The decision-relevant count held and the decorative one rotted, which is worth knowing about counts in general.** But the count overstates the *labelling* work: only 16 are section boundaries, which is exactly what the existing `mark()` calls already are.** So the real cost is 38 mechanical call-site changes to route through the writer, not 38 decisions — weaker than the entry claimed, and still not obviously worth converting a misattribution in a diagnostic report into a compile error.
**Where:** [jc_sysmsg.h](../include/jc_sysmsg.h)

<a id="def-123"></a>
#### The setup wizard offers macOS's sound commands to every non-Linux host.
`src/main.c` seeds `aplay` / `notify-send` when `jc_platform_is_linux()` and **`afplay` / `osascript -e 'display notification'`** otherwise -- so FreeBSD, NetBSD, OpenBSD, illumos, Cygwin and MSYS2 are all offered Darwin commands none of them has. Found at M695 while removing the other two `jc_platform_is_linux()` callers.
**Why:** The call is **not** the M695 defect and was deliberately left alone: those two were stating a *verdict* about a platform, which the matrix owns, while this one picks a *default command*, which is a real OS-family question. But two-way branching on "is this Linux" gets the second answer wrong for six measured rows. The reason it is deferred rather than patched is that the right default per platform is a decision nobody has made: FreeBSD has no `aplay` either, illumos has `audioplay`, and seeding a wrong command is worse than seeding none, because writing a `sound` key **registers the play_audio/record_audio tools** and advertises two mutating tools to a project that never asked for sound (the comment at that site says so). **Revisit when** a row is being driven anyway -- the cheapest form is to offer nothing outside the families whose command is known, and let `doctor` say the key is unset.
**Where:** `src/main.c` (`linux_host` in the setup wizard's sound block)


## Open — what a gateway charges that jichi cannot see (M663)

| Deferred | Detail |
|---|---|
| **`doctor` cannot see per-character or per-image pricing** | [details](#def-124) |

<a id="def-124"></a>
#### doctor cannot see per-character or per-image pricing
so a model that bills either reads as free.
**Why:** Found 2026-09-18 while inventorying the HRZ gateway for speech and image work. `jlu/tts-1-hd` declares `input_cost_per_token: 0.0` **and** `input_cost_per_character: 3e-05`; `doctor`'s check is on *missing* `inputCostPer1M`/`outputCostPer1M`, and `CLAUDE.md`'s own free-namespace rule reads the per-**token** fields. So a per-character biller satisfies every signal this project uses for "free". Pointing the other way, all 81 `openai/*` image models publish `input_cost_per_token: 0` because image models bill **per image** — a zero that means nothing, and an easy way to talk oneself into a priced run. **Not fixed here because the shape of the fix is a judgement:** jichi could read the extra LiteLLM fields (which are a gateway's schema, not the OpenAI API's), or `doctor` could say *"this model publishes a cost field jichi does not price"* without pretending to total it. The second is honest and cheap; the first invites jichi to track someone else's billing schema. **Revisit when** the gateway's published cost fields say what `jlu/tts-1-hd` actually costs — its real price cannot be measured from here — because a real charge makes this urgent and a LiteLLM default makes it a documentation note.
**Where:** [analysis/2026-09-18-local-media-and-the-illumos-live-turn.md](analysis/2026-09-18-local-media-and-the-illumos-live-turn.md) §1


## Open — tool-output cost

| Deferred | Detail |
|---|---|
| **Tighten the per-tool output caps automatically when a 0% cache hit-rate is measured.** | [details](#def-125) |
| **A `doctor` check for a high re-read ratio.** | [details](#def-126) |

<a id="def-125"></a>
#### Tighten the per-tool output caps automatically when a 0% cache hit-rate…
**Why:** jichi knows the hit-rate after a few calls (M326w) and the `--lite` caps already exist, so the pieces are there. **Not done because it changes tool behaviour mid-session based on a statistic** — a `read_file` returning 200 KB yesterday and 32 KB today, for reasons invisible in the config and hard to debug from the outside. `doctor` advises instead. **Revisit when** there is a way to make the adaptation *visible* at the point it happens (the truncation notice naming the reason, not just the byte count).
**Where:** [TOOL_OUTPUT_COST.md](TOOL_OUTPUT_COST.md) §7

<a id="def-126"></a>
#### A doctor check for a high re-read ratio.
*(reviewed M503 and deliberately NOT built: the row's own argument still holds -- doctor's advice names a lever the reader can pull, and the lever here is a prompt or a skill doctor cannot check was applied. It needs a second measurement, not a feature.)*
**Why:** Measured at **72%** in one workload — 2,056 `read_file` calls over 584 distinct paths, one path read 216 times — which is a loop: compaction elides the read, the model re-reads, compaction elides it again. Computable from telemetry exactly as the M316 unused-tools check is. **Not done because it is advice about the AGENT's behaviour, not the operator's configuration**, and doctor's other advice all names a lever the reader can pull. The lever here is a prompt or a skill, which doctor cannot check was applied. **Revisit when** a second workload confirms the ratio is high generally rather than specific to one agent on one codebase — **and measure that workload post-M348**, which attacked the loop mechanically: the elision marker is now a claim ticket naming a preservation-store path, so the re-read the loop consists of has a cheap targeted substitute. If the ratio collapses, this row closes without doctor ever advising. **Measured (M459, post-M348, first pressured corpus): 0% — 14 `read_file` calls over 14 distinct paths, no path read twice**, on an unbudgeted read-heavy run over 16k lines of a real codebase that compacted seven times while doing it. Directionally this is what M348's claim ticket was for: the run read every module once, under the very pressure that is supposed to cause the loop, and never went back. **It is NOT evidence and this row stays open**: 14 calls against a 2,056-call reference cannot confirm or refute a 72% figure, and quoting "0%" as a refutation would be the overclaim this row was written to avoid. The measurement is now a committed script, `tests/measure/reread_ratio.py`, which carries a 50-call floor and prints NOT EVIDENCE below it — so the next person to run it cannot accidentally close this row with a handful of doc edits.
**Where:** ROADMAP M326z


## Open — compaction

| Deferred | Detail |
|---|---|
| **A mid-turn mechanism for turns eliding cannot save.** | [details](#def-127) |
| **Mid-turn compaction keeps running a pass that has provably exhausted.** | [details](#def-128) |

<a id="def-127"></a>
#### A mid-turn mechanism for turns eliding cannot save.
**Measured at M588 — the pressured corpus finally exists, and it argues against building the mechanism first.**
**Why:** The row's revisit condition was *"a workload's `unrelieved` share measured on a post-M326y log"*, and the 2026-08-10 sweep found **zero** `compact` events to measure. An overnight autonomous run on a 9B model at a **65,536** window produced one: **29 mid-turn passes, 100% pressured, 27 of 29 (93%) UNRELIEVED, 28 SHORT**, and reclaim that was **100% lossy** (`dup=0`) because the zero-loss dedup found nothing to dedup. So turns eliding cannot save are real, and on that configuration they are almost all of them.<br>**But the control run says the mechanism is not the first lever.** The same jichi, the same envelope shape and near-identical per-call input (~43k vs ~48k) against a **196,608** window: **2 compactions, both zero-loss, zero pressured.** The arithmetic is the whole story — system 11,536 + tool definitions 4,732 = **16,268 fixed tokens before any history**, which is 25% of a 65k window and 8% of a 196k one. Elision can only touch history, keep-recent protects the newest of it, so the pass ends above the line and re-triggers. **Mid-turn summarization would be treating a symptom of mis-provisioning.** The cheaper levers, in order: size the window to the work, then cut tool output (`readMaxBytes`), then prune tool definitions. **Revisit the mechanism when** a workload presses at a window that is already generously sized — this corpus is not that.<br>**REVISITED AT M710: THE CONDITION IS MET, AND THE MECHANISM IS STILL NOT THE LEVER.** A generous window did press -- **138 pressed passes at 150,000**, where the fixed overhead is **9%** of the window rather than 25%. Two halves of this row also reproduced independently, on a corpus gathered separately and read a month later: the 65,536 fixed overhead measures **16,259 tokens (24.8%)** against this row's **16,268 (25%)** -- nine tokens apart, so that number is a property of the program and not of one run -- and the **196,608 control holds at 1 pressed pass, 0 unrelieved**. But the 150,000 pressure is a **different disease with the same symptom**: 128 of those 138 passes are **one turn** that ran the same **676-byte** `run_tests` **120 times** for 13-token replies, against an `ELIDE_MIN_BYTES` of **800** -- so not one of the 120 results was ever eligible, and 124 of 128 passes elided nothing. That is a **livelock**, and mid-turn summarization would summarize 120 copies of one failing test. Said plainly: **one session, one turn -- an existence proof, not a rate.** So the row stays deferred, its revisit condition is retired as answered, and the lever it was holding the door open for is refused for both bands: at 65k mis-provisioning, at 150k no progress. [analysis](analysis/2026-09-22-compaction-decided.md).
**Where:** [proposals/2026-08-observability-seams.md](proposals/2026-08-observability-seams.md)

<a id="def-128"></a>
#### Mid-turn compaction keeps running a pass that has provably exhausted.
Measured at M710 over 248 pressed passes in 12 turns: the **1st** pressed pass of a turn reclaims **19,465** tokens, the 2nd 15,306, the 3rd–10th 6,007, and the **11th-and-later 161** — while being **78% of all pressed passes** (194 of 248). The first pass is 4% of the passes and takes 40% of the reclaim.
**Why:** **It is a decision, not a patch, and M361 already tried the obvious version.** The mechanism is not mysterious: within a turn each result can be harvested once, the first pressed pass takes everything above `ELIDE_MIN_BYTES` outside the `MIDTURN_KEEP_RECENT` window of 6, and every later pass sees only what the last round appended — which is protected. **M361's exhaustion latch was built for exactly this.** *(Corrected at M712: this row first said it "fired on 34 of 248 (13.7%)". That denominator pooled **140 passes from binaries three days older than the feature** -- M361 landed 2026-08-10 and session `0bf75213`, which contributes 138 of the 248 including all 128 of the worst turn, ran on 2026-08-07/08. On the population that HAD the latch it fires on **34 of 108 (31%)**, and it cut zero-elision from 89% to 70%. The exhaustion curve this row quotes is likewise dominated by pre-latch data. This is the same wrong-population error M710 had just finished correcting in M700, made again one milestone later.)* The remaining question is a *re-arm rule*, and choosing one is a judgement about what a turn that is making progress looks like versus one that is not — pick it in passing and the latch either suppresses a pass that would have helped or keeps the 161-token tail. **The cost of being wrong is asymmetric and small**: skipping is CPU-only (M361's own note — "the estimate, `pressed`, and the telemetry event are unchanged, so observability keeps telling the truth"), so a too-eager latch loses a little reclaim, while a too-shy one costs only CPU. That argues for measuring first and it does not argue for guessing now. **Revisit with** a corpus of more than 12 turns -- **post-latch it is 5 turns**, so the condition is further from met than this row first recorded. **One half of it was answerable from the code alone and shipped at M712**: the latch's horizon said "re-check now", not "something is elidable now", so a turn appending sub-threshold results paid a full history scan every keep+1 appends that was guaranteed to find nothing. Measured on the M710 shape replayed as a unit test -- 120 rounds of a 676-byte result against an 800-byte floor -- the scan ran **30 times before the fix and once after**. What is still owed is the harder half: what to do when the range is genuinely dry *and* the pressure is real.
**Where:** `src/chat/jc_compact.c` (`MIDTURN_KEEP_RECENT`, `jc_compact_rearm_len`), [analysis](analysis/2026-09-22-compaction-decided.md)


## Open — lessons that become checks (M602)

| Deferred | Detail |
|---|---|
| **`hook:` bullets under `## Checks` — a lesson committed as a `PreToolUse` hook.** | [details](#def-129) |

<a id="def-129"></a>
#### hook: bullets under ## Checks — a lesson committed as a…
**Why:** A hook is a shell command in `config.json` behind `hooksEnabled: true`, top-level only, that can block a tool with exit 2 (`HOOKS.md`). It is the stronger bridge from a lesson to a refusal — arbitrary predicates, not the constraint scanner's eight phrasings — and exactly for that reason letting the learning loop write one is a larger trust decision than committing a constraint: the loop would be authoring code that runs on every tool call. M602 counts such bullets as *unsupported* and says so in the apply summary. **Revisit when** a real draft proposes a check the constraint vocabulary cannot express and a human would have written the hook by hand anyway — then the design is a `## Checks` kind that writes a script under `.jichi/hooks/` and prints the `config.json` lines for the human to paste, never enabling it itself.
**Where:** ROADMAP M602, [analysis/2026-08-27-the-language-of-lessons.md](analysis/2026-08-27-the-language-of-lessons.md) D4


## Closed at 2026-09-19 — parallel_abort on the BSDs

| Deferred | Detail |
|---|---|
| ~~**Diagnose `parallel_abort` on the BSDs.**~~ | [details](#def-130) |

<a id="def-130"></a>
#### Diagnose parallel_abort on the BSDs.
**DIAGNOSED AND CLOSED 2026-09-19 — and it was never an abort defect.** Instrumenting the real driver on the FreeBSD guest showed **two** jichi processes where Linux has three: the second child was never forked, so the mock was innocent and so was the abort path. `jc_parallel_eff_max` caps workers at `jc_cpu_count()` when no config max is set, and on FreeBSD/NetBSD that is **1** — not because the guest has one CPU (`sysctl hw.ncpu` says 2) but because `_SC_NPROCESSORS_ONLN` is hidden behind `__BSD_VISIBLE` while this tree compiles `-D_POSIX_C_SOURCE=200112L`. Measured on the guest: the same source prints `2` under default flags and `NOT DECLARED` under the tree's. **`src/platform/jc_platform_posix.c` had predicted this consequence in full, in a comment, since M459** — *"spawn_parallel runs one child"* — and nobody had connected the prediction to the red driver. The driver now pins `maxParallelAgents: 2` in its fixture, so it measures abort and reaping, which is its subject, rather than `jc_cpu_count()` by accident. Green on FreeBSD.


## Closed at M672 — the OpenBSD stop that was not a text tool, and not jichi either

Eight of the nine drivers failing on OpenBSD were GNU-isms in the tier's own
patterns. This was the ninth, and it was neither a text tool nor a defect in
jichi — it was the fixture. Kept in full, because **the two refuted hypotheses
are the useful part**: they are the two a reader would try first.

| Deferred | Detail |
|---|---|
| ~~**`preprompt_discard` check 1 on OpenBSD**~~ | [details](#def-131) |

<a id="def-131"></a>
#### preprompt_discard check 1 on OpenBSD
**CLOSED M672 — and the cause is in the FIXTURE, not in jichi.** jichi's own instrumentation settled it: `enter_raw fd=0 pending=0`, five runs of five, so `input_pending()` correctly saw nothing because the bytes had not arrived yet. **Two hypotheses were tested and refuted first** — that `\r` does not terminate a canonical line without `ICRNL` (sending `\n` fails identically), and that `select()` on a canonical pty slave behaves differently there (a direct probe reports CR- and LF-terminated input readable in ~0 ms on both platforms). The real cause: a script `send` runs in the parent **after the fork**, so it races the child's startup, and that race is the whole experiment for this driver. `ptydrive --presend` now writes **before** the fork — which removed a latent race on Linux too. On OpenBSD the fixture still cannot create the precondition, because **a write to a pty master with no slave open returns -1 there** (measured; Linux accepts it and the bytes survive), so ptydrive reports `presend unsupported` and the two dependent checks **skip with that reason**. The safety property — the type-ahead never becomes a prompt — passes on both.


## Closed at M669 — one CPU on the BSDs, and the warning users needed

| Deferred | Detail |
|---|---|
| ~~**`maxParallelAgents` silently defaults to 1 on FreeBSD and NetBSD.**~~ | [details](#def-132) |

<a id="def-132"></a>
#### maxParallelAgents silently defaults to 1 on FreeBSD and NetBSD.
**CLOSED M669.** `doctor` now warns — on the **fact** that the count is not obtainable, not on the suspicion that it is 1, because a genuine one-core VM is normal and this matrix runs several. `jc_cpu_count_known()` answers the second question and sits adjacent to `jc_cpu_count()` so the two cannot drift. The rejected alternatives stand: no `sysctl` path (M459's reasoning), and no widening of the feature-test macros.


## Open — the four programmes raised 2026-09-18

Raised together by the operator. One is designed
([`plans/2026-09-language-course.md`](plans/2026-09-language-course.md), with its
feasibility measured before it was written); the other three are recorded here so
they are a register entry rather than a memory.

| Deferred | Detail |
|---|---|
| **Music creation, robotics and game development for self-learners and advanced users.** | [details](#def-133) |

<a id="def-133"></a>
#### Music creation, robotics and game development for self-learners and advanced users.
**Why:** **Game development first** (operator's choice): the zigodot precedent exists and the driving surface is solved. Each domain needs **two** surfaces and the second decides whether this is engineering or a demo: a *driving* surface (how jichi acts — the Godot protocol, a board over ssh, a score format) and a *verification* surface (what "it worked" means — a scene loads, a program runs in simulation, a score renders to audio a test can measure). Robotics has hardware here (UNO Q, `hardware bench machines`); **music has neither surface today**, and its verification surface is the open problem.
**Where:** undesigned



## Closed — the 2026-09-21 register sweep, and what it cost to find

Found by walking the register against the tree rather than by anyone reporting
them, which is the point: **all three had shipped and none had been struck**.
Two were two milestones stale, and in one case a second register in this same
repository — `BIBLIOGRAPHY.md`'s own *"What is deliberately absent"* — had said
so in writing since M677 while this page went on listing the work as open.

The sweep that found them checked the *checkable* part of each open row, in the
M657/M658 way. Worth recording what that cost and what it did not: the
mechanical half was free and found **nothing** — all 27 file paths the open rows
cite still resolve, and there are no stale `file.c:symbol` anchors. The drift was
entirely in **claims**, and the shape that was wrong every time was *"X is not
built"* where X is built. Two further rows looked stale to a first grep and were
not: ACP's `allow_once` is a tool-approval option and not the path-fence grant
that row means, and a `retention` comment in `jc_snapshot.c` is not a `prune`
scope. **A correction made on a substring match would have been worse than the
stale row** — so each was opened and read before anything was struck.

| Deferred | Detail |
|---|---|
| ~~**Learning a language with jichi, from its official tutorial.**~~ | [details](#def-134) |
| ~~**Complete the bibliography for the six deferred languages** — Racket, Guile, Elixir, Haskell, Clojure, Python.~~ **— DONE, and this row outlived the work (corrected 2026-09-21).** All six have sections: Python §6, Racket §7 (both at **M671**), Guile §8, Elixir §9, Haskell §10, Clojure §11 (all four at **M677**). `bibliography_lint` counts **149 entries across 12 sections**, and `BIBLIOGRAPHY.md`'s own *"What is deliberately absent"* has said since M677 that *"every language with a track in this tree has literature behind it"* — so two registers in the same repository disagreed about the same work for two milestones. That is the failure `DEFERRED.md`'s opening rule names: a register carrying a reason known to be false is worse than one missing the row. | [details](#def-135) |
| ~~**A tutorial for grounded discourse with a model** — citations, cross-references, argumentation theory; on documentation, literature and source.~~ **— BUILT 2026-09-21.** [`GROUNDED_DISCOURSE.md`](GROUNDED_DISCOURSE.md) is the instrument, the third beside `DOC_REVIEW.md` (prose coherent and untrue of the program) and `CODE_REVIEW.md` (a *reading* coherent and untrue of it); this one is for a **discussion** that is coherent and resolves to nothing. Five moves — claim, grounds that resolve, warrant, counter-argument, revision — and [`85-grounded-discourse`](assignments/85-grounded-discourse.md) grades the floor: `Objection:` before `Reply:`, four or more `file.c:symbol` citations that **resolve** against the tree, a page cited by name, and a Revision that is not a byte-copy of the Claim. Eight teeth, two-sided in `curriculum_graders.py`. The row's own instruction — build it on the argumentation program, whose central move is taking reasoning down a tier into an artifact a script can check — is what the grader implements. | [details](#def-136) |
| ~~**Literature for the other six tracks** — Racket, Guile, Elixir, Haskell, Clojure, Python.~~ **— DONE, and the SECOND row in this register to say so (corrected 2026-09-21).** It claimed *"each has a reading or graded track and no bibliography section"*. All six have sections — Python §6 and Racket §7 at **M671**, Guile §8, Elixir §9, Haskell §10, Clojure §11 at **M677**. | [details](#def-137) |
| ~~**A per-spec "if you are stuck alone" line**~~ | [details](#def-138) |

<a id="def-134"></a>
#### Learning a language with jichi, from its official tutorial.
**— BUILT, and this row was two milestones out of date (corrected 2026-09-21).** All six milestones of the plan shipped: `docs/LANGUAGE_COURSE.md` (275 lines, every command run in the form published), `scripts/fetch-course-corpus.sh` (229 lines, names the archive rather than guessing the version), `tests/smoke/course_citations_lint.sh` (171 lines), `jichi init course`, the `course-coach` scaffolded skill, and the graded Python track — **tasks 81–84**, two-sided in `curriculum_graders.py`, closed at **M674** whose ROADMAP entry is titled *"the language course's graded half, in Python — done"*. The row is kept rather than deleted because the **feasibility measurement** in it is still the useful part, and because how it went stale is worth seeing: the work was recorded in the ROADMAP and never struck here. Original text: *"Designed, not built. Feasibility measured first: the Python 3.14 text archive is 537 files / 16 MB, the tutorial alone **17 files**; `jichi docs search` answered a list-comprehension question in **4 s** with a resolving `file:line` anchor, and a grounded agentic turn quoted `controlflow.txt` in **27 s**. Decisions fixed: local snapshot + manifest, deterministic committed graders (the model personalises framing only), ships inside jichi, and the learner-facing tutorial is written against **today's** jichi so every published command is one that runs. "*
**Where:** [`plans/2026-09-language-course.md`](plans/2026-09-language-course.md), ROADMAP M674

<a id="def-135"></a>
#### Complete the bibliography for the six deferred languages — Racket, Guile,…
**Why:** The reasoning is kept because it is the useful part: the list was worked through **when there was a reason**, never alphabetically — Rust came off at M636c for being the only language with a graded course and nothing to read; Python and Racket at M671 because the language course points at them for *"the important literature"*; the last four together, because by then the gap itself had become the anomaly.
**Where:** `docs/BIBLIOGRAPHY.md` §"What is deliberately absent", ROADMAP M671/M677

<a id="def-136"></a>
#### A tutorial for grounded discourse with a model — citations, cross-references,…
**Why:** **The milestone number is still owed**, and deliberately: a second machine held the next one while this was written, so the work sits on branch `docs/grounded-discourse` with no ROADMAP entry and no `Mnnn` cited anywhere — which is what keeps `milestone_currency_lint` and `changelog_coverage_lint` green. Number it, write the ROADMAP and CHANGELOG entries, and merge.
**Where:** `docs/GROUNDED_DISCOURSE.md`, `docs/assignments/85-grounded-discourse.md`

<a id="def-137"></a>
#### Literature for the other six tracks — Racket, Guile, Elixir, Haskell,…
**Why:** **Why the first sweep missed it, which is the useful part.** That sweep searched for the phrasing *"not built" / "undesigned" / "no … yet"* and this row says *"no bibliography section"* — the same claim in words the regex did not cover. Two rows in different sections asserting the same false thing, and a keyword sweep found one. The lesson is the one the sweep itself recorded: **enumerate a second way.** Here the second way is cheap and was not run — for each register row naming a deliverable, ask the tree whether that deliverable exists, rather than asking the row how it phrased its absence.
**Where:** `docs/BIBLIOGRAPHY.md` §6–§11, ROADMAP M671/M677

<a id="def-138"></a>
#### A per-spec "if you are stuck alone" line
**— the premise was stale, and the real gap was one tenth the size (measured 2026-09-21).** The row priced this at *"70 small edits"* on the belief that *"the stuck path … lives on the module pages"*. Measured across the 89 graded specs: **86 already mention `/hint` or `jichi hint`** in their own footer. The three that did not were **p1, p2 and p3 — the plain-register tier**, which shipped a three-rung hint ladder in frontmatter that *the learner was never told about*, on the pages written for absolute beginners; `PLAIN_LANGUAGE.md` did not mention it either. All four now do, in the plain voice, with the ladder explained and the recording said to be for the learner rather than against them.
**Why:** **What the row got right, and is kept for:** hand-writing a bespoke line into 80 specs would have been filler, and it said so. What it got wrong is the count — and the count was checkable the whole time. The bespoke *"If you are stuck alone:"* line remains a process-track (67–75) convention, deliberately, because those eight lines say something specific to their task and a generic one would not.
**Where:** `docs/assignments/p1-ask-for-a-file.md`, `docs/PLAIN_LANGUAGE.md`


## Open — teaching and documentation

| Deferred | Detail |
|---|---|
| **An `init` option that scaffolds the records tree as `.org` instead of markdown.** | [details](#def-139) |
| **A one-command platform report.** | [details](#def-140) |
| **DECISIONS.md has 16 duplicated anchors, so a link to one of them may land on the other.** | [details](#def-275) |

<a id="def-139"></a>
#### An init option that scaffolds the records tree as .org instead…
**Why:** The format itself is a one-line change in the scaffold tables; the *cost* is that shipping `.org` assets pushes users toward one editor, which is exactly what M326s decided against. **Revisit when** two users ask for it — an observable trigger, per this page's own rule, and cheap to honour once someone has. (Deliberately described without inventing a flag spelling: `docs_flags.sh` scans this page — unlike `DECISIONS.md`, which is excluded — so naming a switch here would document one that does not exist. The first draft did, twice: once in the entry and once in the note explaining why not to.)
**Where:** ROADMAP M326s

<a id="def-275"></a>
#### DECISIONS.md has 16 duplicated anchors
**Why:** **Measured** (2026-09-29, while adding the M835–M837 rows): 16 `id="dec-..."` anchors occur twice, among them `dec-m296`, `dec-m298`, `dec-m466` and the placeholders `dec-m?` and `dec-m?-2`. A browser follows a link to the first occurrence, so the second decision of each pair cannot be linked to at all. The fix is a renaming pass (`-2` suffixes where the two rows are different decisions, one anchor where they are the same one) and a lint that fails a duplicate; neither was in scope for the DOS batch that found it.
**Where:** `docs/DECISIONS.md`

<a id="def-140"></a>
#### A one-command platform report.
`docs/VERIFY_A_PLATFORM.md` (M716) gives a volunteer about thirty lines to paste, in eight steps -- identity, a `WERROR=1` build, the unit suite, the smoke tier keep-going, `doctor`, the two live turns, and a tarball. Every line was run as printed before publishing, on Linux; none has run on a Mac.
**Why:** **Not built because the page was asked for first, and a script is code with its own tests.** The design is fixed by the rule the page itself obeys: the live half must *source* `scripts/_rig_live.sh`, the one definition of the Driven task, rather than carry a third copy -- the page's own copy is pinned by `rig_live_commands_lint` check 6 precisely because copies drift. It should write the report folder outside the checkout (the smoke tier's publication check fails on untracked files), take the model id as its only required argument, refuse a non-loopback server unless told otherwise, and prove its plumbing in the smoke tier against `mockmodel` with a fixed phrase. **Revisit when** the first volunteer report arrives: which steps they got wrong is the specification.
**Where:** `docs/VERIFY_A_PLATFORM.md` §9



## Closed 2026-09-21 — the records practice, graded

The row was not stale; it was **right**, and it named the condition under which
it should be revisited. It refused a grader that could "only grade the SHAPE of a
register" and asked for one that grades "the HABIT rather than the headings".
That condition was met rather than waved past, and the design follows from it:
auditing a register beats writing one, because an audit has a checkable answer
and writing has only a shape.

| Deferred | Detail |
|---|---|
| ~~**A graded assignment for the records practice.**~~ | [details](#def-141) |

<a id="def-141"></a>
#### A graded assignment for the records practice.
**— BUILT 2026-09-21 as `86-the-register-that-went-stale`, by meeting the row's own revisit condition rather than ignoring it.** The row refused a grader that could "only grade the SHAPE of a register" and said to revisit "when there is a way to grade the HABIT rather than the headings". The way is to stop asking the learner to *write* a register and ask them to **audit** one: five rows over a small tree, two of them gone stale, and a verdict required for every row.
**Why:** **Why this is the habit and not the shape.** The grader refuses **both directions** — a missed stale row and a wrongly retired standing one are the same defect, and the spec says why the false positive is not the safer mistake: a missed row gets re-walked, a deleted row does not. One row is a trap: `grep -i rate` returns four hits in `upload.c`, every one a *sampler's* rate whose own comment says it is "not anything we control or limit here". A learner who greps without opening the file retires work that still needs doing. And a `STALE` verdict must cite **the file that makes it stale**, not any file that exists — `GROUNDED_DISCOURSE.md`'s *grounds that resolve*, met in a register. Two-sided in `curriculum_graders.py`; six teeth.
**Where:** `docs/assignments/86-the-register-that-went-stale.md`

## Open — JupyterHub and notebooks (M478)

| Deferred | Detail |
|---|---|
| **`read_file` renders an `.ipynb` to cells** | [details](#def-142) |
| **The browser half of the terminal contract** | [details](#def-143) |
| **A German *Einfache Sprache* counterpart** | [details](#def-144) |
| **Shape D: the web bridge behind `jupyter-server-proxy`.** | [details](#def-145) |

<a id="def-142"></a>
#### read_file renders an .ipynb to cells
the way M42's PDF path renders a PDF (detect the extension, transform to text, never mark it read-before-edit because it is not editable that way).
**Why:** jichi has **zero** notebook support (`grep -rc ipynb src/ include/` is empty), and M478 measured what that costs: a notebook with one figure is **~65,631 tokens** — over the 256 KB read cap, so truncated as well — against **~255** for the same code as a jupytext-paired `.py`. **257×.** The fix is modest and in character. **Not started because the use case is undecided:** the request that prompted M478 was a loose "can this be used with JupyterHub?", and the answer to "do your learners live in notebooks?" is not yet known. **Revisit when** someone states that notebooks *are* the workflow — the 257× figure is the argument, and [JUPYTERHUB.md](JUPYTERHUB.md) §14 is the decision aid. Full cell-**editing** is recommended against separately: a new corruption class in exchange for something `jupytext` already delivers.
**Where:** [JUPYTERHUB.md](JUPYTERHUB.md) §4

<a id="def-143"></a>
#### The browser half of the terminal contract
whether xterm.js or the browser wins for the keys jichi binds (Ctrl-R against page-reload, Ctrl-G against Firefox's find-next), plus browser paste and browser resize.
**Why:** M478's rig speaks terminado's **websocket**, so it tests jupyter-server completely and xterm.js not at all — a websocket cannot press a key in a browser. This is **not** a deferral for cost reasons: it is ten minutes with a browser, and the nine-item checklist already ships with `scripts/jhub-verify.sh`. It is deferred because it needs a **human at a display**, which no rig here can be. **Revisit when** anyone runs the course, or sooner — it is the cheapest open item on this page.
**Where:** [JUPYTERHUB.md](JUPYTERHUB.md) §13

<a id="def-144"></a>
#### A German Einfache Sprache counterpart
for the JupyterHub learner section.
**Why:** [`PLAIN_LANGUAGE.md`](PLAIN_LANGUAGE.md) has a German **original** ([i18n/de/EINFACHE_SPRACHE.md](i18n/de/EINFACHE_SPRACHE.md)) — *Einfache Sprache* is a defined register with its own rules, and the English page is its sibling, not its source. The new section was written in English first, which inverts that relationship for this one section. The phased-i18n policy makes English canonical, so this is a known cost rather than a defect. **Revisit when** a German-speaking cohort actually uses a hub — and note it needs a **writer of the register**, not a translation pass.
**Where:** [PLAIN_LANGUAGE.md](PLAIN_LANGUAGE.md)

<a id="def-145"></a>
#### Shape D: the web bridge behind jupyter-server-proxy.
**Why:** `examples/web-bridge/bridge.py` already uses "the Jupyter model" (a boot token) and binds loopback; a hub could put its own authentication in front of it at `/user/<name>/proxy/<port>/`, supplying the one thing the bridge deliberately lacks — real accounts. Attractive, and **entirely unrun**. Two specific unknowns: whether Server-Sent Events survive the proxy without buffering, and whether the bridge's own token still earns its place once the hub authenticates. **Revisit when** someone wants a browser UI rather than a terminal; until then the terminal is the recommendation and costs nothing.
**Where:** [JUPYTERHUB.md](JUPYTERHUB.md) §3


## Closed at M659 — two of the three peer-transport buffers

**M657 refuted the blocker; M659 did the work.** The rows had said for fifty
milestones that a born-red test needed "a memory-pressure harness the smoke tier
does not have". RSS was never the property: each fix makes the bound
**observable**, and what the driver reads is the refusal, not a byte count. One
convention came out of it and is now a decision — *a cap that says nothing is a
cap nobody can test, and nobody can debug either* — carried by
[`include/jc_peercap.h`](../include/jc_peercap.h), which holds the single number
both readers use and the argument for saying it out loud.

| Deferred | Detail |
|---|---|
| ~~**The MCP stdio `rbuf` line buffer is bounded only by a 120 s deadline.**~~ | [details](#def-146) |
| ~~**The ACP `inbuf` line buffer has no byte cap and no deadline.**~~ | [details](#def-147) |

<a id="def-146"></a>
#### The MCP stdio rbuf line buffer is bounded only by a…
**DONE, M659.**
**None:** Capped at `JC_PEER_LINE_MAX` (8 MiB, `include/jc_peercap.h`), and the refusal **names the cap in bytes**. `tests/smoke/peer_line_cap.sh`, 4 checks, proven red: without the cap the reader blocks to its deadline (75 s against 13 s) and reports *"server closed the connection"* — the message that is indistinguishable from a network stall, which is what the naming replaces.
**Closed by:** `src/mcp/jc_mcp_stdio.c`

<a id="def-147"></a>
#### The ACP inbuf line buffer has no byte cap and no…
**DONE, M659 — with its red still owed, and that is stated rather than glossed.**
**None:** Same cap, same message; the buffer is **discarded** rather than flushed, because the eof path hands a final unterminated line to the caller and flushing it would pass on the very megabytes the cap refuses. **What is not done is a driver.** This path has no deadline, so the MCP fixture's argument — latency — does not transfer, and the only observable is the message. That is precisely why the cap speaks; writing the driver that reads it is the remaining work and it is small.
**Closed by:** `src/acp/jc_acp.c`


## Closed at M661b — peer transport shutdown, and the M609 family complete

The third and last row of the family M609 opened. All three are now done, and the
pattern across them is one sentence: **every one was blocked on a claim about its
test, and every claim was wrong.** Two were "this needs a memory-pressure
harness" (M659); this one was "deterministic but fiddly in POSIX sh", which was
fair about the difficulty and wrong about the obstacle.

| Deferred | Detail |
|---|---|
| **MCP and LSP shutdown block in `waitpid(pid, …, 0)` after `SIGTERM`.** | [details](#def-148) |

<a id="def-148"></a>
#### MCP and LSP shutdown block in waitpid(pid, …, 0) after SIGTERM.
**Closed by:** **M661b.** All three sites now use `jc_worker_reap_grace(pid, JC_WORKER_TERM_GRACE_MS)` — the helper the parallel pool and the daemon already used, which the row itself named as the drop-in and was right about. `tests/smoke/peer_reap_grace.sh`, 3 checks, proven red: **45 s against 0 s** on the same fixture. **The fixture took three attempts and the first two measured nothing**, which is the part worth keeping: a hand-rolled mock deadlocked the *request* path (jichi in `select()`, the mock in `read()`) while the driver reported 45 s as though it had measured shutdown, and a second version was defeated by a lingering `sleep` holding jichi's pipe open so the wait was for EOF rather than for the child. The working mock is **derived from the tier's own `smoke_write_mock_mcp`** with exactly two changes — `trap '' TERM` and `exec >&- 2>&-; sleep` — because the protocol half was already proven by three other drivers. The LSP site takes the identical one-line change and is **not** covered by a driver; that is stated in the driver's header rather than implied.



## Open — fence hardening and isolation

| Deferred | Detail |
|---|---|
| **A shell/interpreter sandbox** | [details](#def-149) |
| **The path fence's check-then-open window** | [details](#def-150) |
| **The popen path's descriptor total** | [details](#def-151) |
| **Mid-run one-off fence exceptions** | [details](#def-152) |

<a id="def-149"></a>
#### A shell/interpreter sandbox
real OS-level containment of what `run_terminal_command` (and any program it launches) can read and write.
**Why:** This is the project's oldest safety deferral, and until now it was **invisible from the register built to make deferrals findable** — it lived only in ANECDOTES #12 (*"a fuller shell sandbox is deferred"*) and `proposals/2026-07-privileged-commands.md` §Deferred, both predating this page's M298 coverage window. Recorded here so it is findable. **Checked (M326b):** no `seccomp`/`bubblewrap`/`firejail`/`unshare`-as-sandbox exists anywhere in `src/` or `docs/`; the file-tool fence (incl. `search_code`, M383) does not cover the shell, and M83 detection is writes-only / in-git-tree-only / post-hoc (GATE_INTEGRITY.md §9.1). **The honest position is that this is not a C feature:** a userspace heuristic cannot contain a determined program, so the real answer is deployment — run as a non-root user, in a container/VM (DEPLOYMENT.md §5, AUTONOMOUS_LOOPS.md's systemd unit). **Revisit as** a *documented `bwrap`/`unshare` launcher recipe* wrapping the whole process (not per-tool), and — separately and only behind an explicit opt-in — the heuristic path-screen of GATE_INTEGRITY.md §9.3-C, which is defense-in-depth with a stated miss list, never a wall. **The revisit trigger happened (M744, 2026-09-25):** in an unattended drive, bonsai-27b's `run_terminal_command` deleted `~/.cache/jichi-corpus/`, the drive's own data, from a workspace inside it ([analysis](analysis/2026-09-25-the-overnight-drive.md) §1; ANECDOTES #108). One option this row did not weigh is kernel-enforced and unprivileged, so it is not a userspace heuristic: **Linux Landlock** (5.13+) lets a process restrict the files its children may write, before `exec`. `run_terminal_command` could then write only the workspace and `/tmp`. It is Linux-only and must fail open or closed by an explicit choice on older kernels, and it needs its own threat model. It is a design for the operator to decide, not a patch. **Partly addressed at M831 (O9):** the Landlock write fence now lands for the case where an escaping shell does the most harm and is least supervised — a `spawn_parallel` **write child**, whose shell could reach the parent's live tree past the worktree its file tools are fenced to. `src/util/jc_landlock.c` applies it in the forked child (`shellFence`, default on; `shellFenceAllow` widens it; `doctor` reports the capability; unavailable ⇒ unfenced but never silently). What **remains deferred**: the top-level interactive/headless shell (not just parallel write children), non-Linux kernels (Landlock is a Linux LSM), and the `bwrap`/`unshare` whole-process launcher recipe above — a fence on one hot path is not a general sandbox.
**Where:** [GATE_INTEGRITY.md](GATE_INTEGRITY.md) §9

<a id="def-150"></a>
#### The path fence's check-then-open window
(M472, audit L3). `jc_path_in_root` resolves with `realpath()` and returns a verdict; the `open()` happens afterwards, so a path component swapped in between is a classic TOCTOU.
**Why:** **Analysed and left, with the reasoning stated, because the threat model makes it near-worthless to fix.** The attacker who could win the race is a process running concurrently as the same user -- in practice the model's own background shell. But a model with shell access does not need to race the fence: it can write the file directly, which is [the shell-sandbox deferral](#) above and this project's oldest safety gap. The fence is the ONLY door in exactly one configuration, `--edit-scope --strict-scope`, which forbids `run_terminal_command` -- and there, by construction, there is no concurrent attacker to swap anything. So the window is reachable only where it does not matter, and closed where it would. **The cost of fixing it anyway is not small:** the file tools go through stdio (`fopen`), which has no `O_NOFOLLOW`, so it means converting the file I/O layer to `open()` + `fdopen()` plus an `fstat` re-check across five libcs and three non-Linux kernels -- a portability risk taken for a threat the same configuration already answers. **Revisit when** a sandbox lands (which changes the concurrency assumption), or when a file tool gains a caller that runs while an untrusted process shares the workspace -- a multi-tenant daemon would be the shape.
**Where:** [analysis/2026-08-17-source-hardening-audit.md](analysis/2026-08-17-source-hardening-audit.md) L3

<a id="def-151"></a>
#### The popen path's descriptor total
(M472). Closed at M832. Every child jichi forks and execs itself now inherits stdio and nothing else -- `jc_proc_child_close_fds()` closes the range, `jc_fd_cloexec()` marks the sinks and sockets at creation, and both are pinned by `posix_utils_lint.sh` checks 6/8 and `tests/smoke/child_fds.sh`. A command run WITHOUT a `timeout` goes through `jc_proc_popen`, where the fork happens inside libc, so no jichi code runs between fork and exec and the close-range backstop structurally cannot reach it.
**Why:** **One pipe pair still arrives there, and it is libcurl's, not jichi's** -- created without `O_CLOEXEC` in curl's internals, where jichi has no hook (`CURLOPT_SOCKOPTFUNCTION` covers sockets, not pipes). Measured by strace: jichi's own pipes are all marked, libcurl's is not. The three descriptors with a demonstrated exploit -- the run journal, the telemetry sink, the provider socket -- are closed on BOTH paths, and this residual is not a sink, a socket or a secret; a child can hold it open or write bytes nobody reads. Recorded rather than papered over, because `child_fds.sh` deliberately asserts the total on the fork/exec path only, and a reader of that driver should know why. **Revisit as** routing model-issued commands through jichi's own fork/exec path unconditionally (the `timeout` argument already selects it), which would retire `jc_proc_popen` for tool execution and make the total hold everywhere -- a behaviour change with its own milestone, not a line in this one. **Closed at M832, without that behaviour change:** `jc_proc_popen` is no longer libc popen -- it is a stream over a `jc_spawn` child, so the popen path runs jichi's own child ritual, backstop included (fork backend), or the parent-side `jc_proc_parent_cloexec_all` sweep (posix_spawn backend). Measured both ways: the installed M831 binary hands libcurl's pipe (fds 5, 6) to a popen-path shell, and M832 hands nothing, on either backend. `child_fds.sh` check 4 now asserts the popen total and checks 5-8 repeat all four on the spawn backend; check 4 was proven red against the M831 binary.
**Where:** [analysis/2026-08-17-source-hardening-audit.md](analysis/2026-08-17-source-hardening-audit.md) §H2

<a id="def-152"></a>
#### Mid-run one-off fence exceptions
an interactive "grant read of this path for the session?" prompt when a file tool hits the fence (D1), designed but unbuilt.
**Why:** Designed in the item-7 proposal, not built this batch. **Reads only, interactive front-ends only** (a human answers); writes stay a pre-run decision (rollback cannot undo an out-of-tree write) and are the proposal's D2 deferral; the silence policy is settled (no timeout — structural, D4). The full implementation design — decisions with rejected alternatives, the safety invariant, the negative-test-first sequencing — is [plans/2026-08-tui-fence-grant.md](plans/2026-08-tui-fence-grant.md). **Revisit when** a workload shows the occasional-unforeseen-external-read case is common enough to beat re-launching with `--reference-root`.
**Where:** [plans/2026-08-tui-fence-grant.md](plans/2026-08-tui-fence-grant.md)


## Narrowed at M582 — localized presentation decks

| Deferred | Detail |
|---|---|
| **Bringing the four localized decks up to the English decks' CONTENT** | [details](#def-153) |
| **`PROJECT_TIMELINE.md`: figures behind the M579 recount** | [details](#def-154) |
| **`docs/i18n/ja/PROJECT_TIMELINE.md` owes a corrected third-party row** | [details](#def-155) |
| **Native review of the es · ja · zh decks and prose.** | [details](#def-156) |

<a id="def-153"></a>
#### Bringing the four localized decks up to the English decks' CONTENT
(de · es · ja · zh). Four decks are one to two slides short: `00-super-features` (−1), `03-roadmap` (−2), `04-university` (−2), `05-school` (−1), in every language. **Re-read from the markers 2026-09-18 (M658): the gap has widened to one, two AND three** — sixteen `slides-behind` markers now read 8×1, 4×2, 4×3. The declaration mechanism did its job (the gap is visible and bounded); the prose describing it did not move with it, which is the one thing a declared-gap design cannot check for itself.
**Why:** **The figures half is done (M582)** and is now gated. What is left is prose: the English decks gained sections, and translating them is writing in three languages this repository cannot review. Each of the sixteen files declares the gap with `<!-- slides-behind: N -->`, the count is checked, and the declaration re-fires when the English deck moves again — so the gap is bounded and visible rather than silent. **Revisit when** a reviewer for that language exists (see the row below).
**Where:** [i18n/README.md](i18n/README.md)

<a id="def-154"></a>
#### PROJECT_TIMELINE.md: figures behind the M579 recount
**ja is down to 4** (M587); de · es · zh remain at **22**. **Re-read from the markers 2026-09-18 (M658): ja is at 0** — M651 carried the last numerals across — **and de · es · zh are at 21, not 22.** The Japanese half of this row is therefore *done*; what is left is three languages with no reviewer, which is the row below.
**Why:** M587 brought across everything in the Japanese page that is a *pure numeral*: the 17-slice subsystem pie chart, the tool count inside its label, and five summary rows. The four that remain are each welded to prose that would become false — the test row's breakdown changed shape in English, and the proportion and commits-per-day tables are each followed by a paragraph that *interprets* them (English now argues "documentation now outweighs source"; the daily table was recounted, Jul 24 is 384 not 378, and extended by a month). Redrawing bars is arithmetic; the prose around them is not. **Revisit when** a Japanese writer can take the page as a unit. de/es/zh were left untouched deliberately — the same split applies and none has a reviewer.
**Where:** [i18n/README.md](i18n/README.md)

<a id="def-155"></a>
#### docs/i18n/ja/PROJECT_TIMELINE.md owes a corrected third-party row
(deleted at M587, not rewritten).
**Why:** The row said the bundled cJSON is *"not authored by this project"*. That is **false**, and it contradicts [LICENSING.md](LICENSING.md), the README and the English page, all of which state `src/json/cJSON.{c,h}` is original code (M171) — **and LICENSING.md's argument that the licence choice is unconstrained rests on exactly that**. The translation was *faithful when made*: English carried the same claim until **M498 (2026-08-20)**, four days after this page's tracked commit, and the correction never propagated. That is the failure `tracks:` exists to expose and `i18n_tracks_lint` now gates. Deleting a false claim needs no Japanese; writing a true one does, so the row is **gone and owed**. **Revisit when** a Japanese reviewer can add the corrected sentence.
**Where:** [i18n/README.md](i18n/README.md)

<a id="def-156"></a>
#### Native review of the es · ja · zh decks and…
**Why:** M582 corrected **numbers only**, on the maintainer's explicit instruction, because a wrong number is wrong in every language while phrasing is not auditable without a reader. Two edits sit at that boundary and are named rather than hidden: the four decks' figures now read `10,000+` / `11,000+` where English says "over" (a `+` sign was chosen precisely so no "more than" word had to be invented in three languages), and `docs/i18n/zh/PHILOSOPHY.md`'s CJK numeral `四千五百多` became `一万多`. Both are numerals; neither has been read by a native speaker. Japanese has a route — `llm-jp` as a first-pass reviewer (M580), then the maintainer's friends. Spanish and Chinese have none.<br>**Underway since 2026-09-18 (M657): the Japanese review has a real native reader**, and her first impression is recorded rather than acted on, because it is a first impression and the review is not finished. Two findings so far. (1) **The register**: the pages read *formal, not casual* — a choice nobody here ever made deliberately, now stated as one in [i18n/README.md](i18n/README.md) §Policy item 4, with the argument on both sides and the per-genre question left to her rather than settled by us. (2) **The English idioms**: terms like *dogfooding* need the metaphor explained, not just the usage — nine are now glossed in [VOCABULARY.md](VOCABULARY.md), and the ten tutorials that pointed at that page **nowhere** now point at it. What is still owed to Japanese specifically: there is now an `i18n/ja/VOCABULARY.md` (M706, a machine draft that says so on its face), so a reader of the Japanese pages meets the English jargon with the glossary in the other language. That is a translation, and it waits on the same reviewer.
**Where:** [i18n/README.md](i18n/README.md)


**What the old row got wrong, kept because it is the same failure this section is about.** It listed the stale figures as ``107``, ``~770 KB``, ``~85,000`` and ``~700 KB``, "verified by grep". Re-measured at M582: **only `~700 KB` was still present**; the other three had been gone for milestones. A deferral row that cites grep evidence rots exactly like the documentation it describes, and nothing checked it either. The row also recorded *"deliberately NOT linted: the parity check would have to parse four languages' number prose"* — that premise was false and cheap to test, and the lint that does not parse prose is [`tests/smoke/i18n_tracks_lint.sh`](../tests/smoke/i18n_tracks_lint.sh); see the M582 row in [DECISIONS.md](DECISIONS.md).

## Closed at M499 — documentation owed to the self-learner

The four rows here were the milestone-sized items from
[analysis/2026-08-12-docs-review.md](analysis/2026-08-12-docs-review.md) (four
reviewers, 30 pages, one rubric). All four are done — and **one of them had
already been done and the row had rotted**, which is the M326b rule earning its
place again: check the checkable part of a reason before acting on it.

| Deferred | Detail |
|---|---|
| A "where your state lives" page, and the source-checkout prerequisite | [details](#def-157) |
| The tool-call decision chain, as one page | [details](#def-158) |
| Five tutorial-shaped sections inside reference pages | [details](#def-159) |
| The curriculum's lone-learner gaps | [details](#def-160) |

<a id="def-157"></a>
#### A "where your state lives" page, and the source-checkout prerequisite
**Closed by:** [STATE.md](STATE.md) — all 15 `~/.jichi.d/` subtrees plus the file INSTALL's table omitted (`~/.jichi.env`, the one holding a secret), what is irreplaceable, and the exact `make install` manifest with the consequence stated: the course lives in the tree, not in the binary.

<a id="def-158"></a>
#### The tool-call decision chain, as one page
**Closed by:** [TOOL_DECISIONS.md](TOOL_DECISIONS.md) — the nine steps in the order the code runs them, read out of `jc_agent.c` rather than summarised, including the three that surprise readers: an ALLOW verdict does not mean it runs, your "yes" is not the last word (the scope fence and the hook come after it), and headless refuses an ASK rather than proceeding.

<a id="def-159"></a>
#### Five tutorial-shaped sections inside reference pages
**Closed by:** Written, and pinned by `tests/smoke/self_learner_lint.sh` check 3 — a tutorial section inside a reference page is the least defended documentation there is, and deleting one was a two-line diff nobody would question.

<a id="def-160"></a>
#### The curriculum's lone-learner gaps
**Closed by:** **Four of the five had already shipped** at M396/M406 and the row had not been updated: the skip rule is stated in `assignments/INDEX.md` ("the margin is exactly one 3-point task… leave it, go forward, come back later"), the four `cc`-needing specs carry prerequisite boxes, and CURRICULUM §Who it is for already routes both the process track and `PLAIN_LANGUAGE.md` and names the four INSTRUCTOR sections a lone learner should read. What was genuinely missing was the **cross-reference from the map**: a learner reading "gate: 14/17" saw a threshold, not a permission. One paragraph, now in CURRICULUM.md.


Also closed with them: register item 15, *definitions of the load-bearing words
before use* — [VOCABULARY.md](VOCABULARY.md), **58 terms** (48 when that review closed; nine
English idioms were added at M657 and one had drifted uncounted), with the 24 the review and
the smoke tier force on a reader pinned by check 4. And the **name trap**: a reader
looking for "what does posture mean" opened `docs/GLOSSARY.md` and found a config
page (the `docs/DOCS.md` shape), so that page now carries a sign in front of it,
pinned by check 5.

**Still open from that review, and deliberately:** the items whose value is a
judgement rather than a structure — item 18's promised user-story tutorial, item 19
(`jichi assignments` as an orientation, which is the row below), and the prose-level
findings the register carries. A lint can hold a heading in place; it cannot make a
page good.

## Open — assignment and grading support

Found by the M398 workflow review of the assignment/tutoring documentation; the
workflows themselves shipped, these are the product gaps they had to document
around ([TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md) § "What this feature
does not yet support well").

| Deferred | Detail |
|---|---|
| **A cohort view for teachers** | [details](#def-161) |
| **Per-facet reading tasks** | [details](#def-162) |

<a id="def-161"></a>
#### A cohort view for teachers
one command that reads many learners' `progress.jsonl`.
**Why:** `jichi assignments` reads exactly one workspace, so thirty students are thirty benches with no aggregate; the documented answer is a shell loop collecting `--output json`. **Not done because it is a gradebook**, i.e. someone else's software: it needs identity, storage and a policy about grades, none of which belong in a coding agent (the M165 web-frontend reasoning applies — jichi provides the machine surface, a sidecar owns the aggregation). **Revisit as** a documented recipe or an `examples/` script rather than a subcommand.
**Where:** [SCRIPTING.md](SCRIPTING.md)

<a id="def-162"></a>
#### Per-facet reading tasks
one graded task each for abstraction→concrete, control flow, data flow and execution, beyond the single integrated `74-read-the-turn` (M627).
**Why:** The integrated task exercises all five readings on one unit and ships the instrument (`CODE_REVIEW.md`) and the coaching skill; splitting it into a set costs four reference readings, four graders and four trap cases before anyone has run the first. **Revisit when** a learner has graded 74 and the record (their READING.md against the reference) shows which reading they get wrong most — that is the facet worth its own task, and the evidence for which one is not available yet.
**Where:** ROADMAP M627


## Open — unit-suite integrity

| Deferred | Detail |
|---|---|
| **A `{name, fn}` table for the unit runner** | [details](#def-163) |
| **The M201 re-ask for the unit suite** | [details](#def-164) |

<a id="def-163"></a>
#### A {name, fn} table for the unit runner
replacing `test_main.c`'s **174** hand-written `printf(name); test_name();` pairs *(recounted 2026-09-18: the row said 146, which was true when written and has been wrong for a while — the same rot as M583's "eight call sites" that turned out to be nine in five files, and it makes the row's own argument stronger rather than weaker)*.
**Why:** It is the single enabler for three things at once: a per-function `jc_test_checks` delta (so "every test contributes ≥1 check" becomes checkable with no baseline), per-test selection (the prerequisite for the M201 re-ask below), and a cleaner ground truth than grepping call sites. **Not done because it touches the instrument** — failure mode 1 is "the instrument is broken" and M201's incident was exactly that, so it needs its own milestone with its own teeth: the total check count identical across the change (11,305 → 11,305) and a planted failure still reported with the right name and count. **Checked before parking (M326b):** `jc_test_checks` is a plain global incremented by the JC_CHECK macros, so the delta is trivial once the loop exists; and `test_main.c` takes no `argc`/`argv` at all, so selection genuinely cannot be bolted on without it. **Revisit when** either the ≥1-check assertion or the isolation sweep is wanted — neither is reachable without it.
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md) rec 3/4

<a id="def-164"></a>
#### The M201 re-ask for the unit suite
run each of the **174** test functions alone and compare with the in-suite result, both directions.
**Why:** Blocked on the table above (no per-test selection exists). The expectation is honest: probably **zero** findings — `test_msg` already restores the process-wide language with a comment saying why, so the discipline exists — and a clean zero is a result worth recording (the M343 0/21 precedent), not a reason to skip the measurement. What it could find is order dependence via the `jc_msg` language global, registered redaction secrets, log level, locale, cwd, or a static cache. **Revisit with** the table, as its first customer.
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md) rec 4


## Open — externally blocked

A reason can also rest on **someone's statement** rather than on evidence or judgement, and the
M326b rule applies there too — not by checking the statement (I cannot read anyone's email) but by
**saying whose it is**, so a future reader knows what kind of thing they are looking at. Two of the
three rows below are of that kind, and their elapsed time is stated because "we asked once" quietly
becomes "we asked months ago and never followed up".

| Deferred | Detail |
|---|---|
| **The logo (SVG + PNG).** | [details](#def-165) |
| **Robotics Tier B (motion) and Tier C (the reflex layer).** | [details](#def-166) |

<a id="def-165"></a>
#### The logo (SVG + PNG).
**Why:** **Operator's statement:** they are drafting it. **Re-scoped M657, because the row's own condition expired:** it said they would bring it *"before release"*, and two releases have shipped without it — `v0.9.0` on 2026-08-27 and `v0.9.1` on 2026-09-17 — with no logo file in the private tree or the public one (checked 2026-09-18). So the logo gates nothing, and a row implying it does misdirects whoever reads this page looking for what stands between here and a release. By this page's own test (*"if new information could change the answer, it is deferred"*) it is barely a deferral: it is a want with no unblocking condition. Kept anyway, because the operator said they would draw one and a promise recorded is cheaper to honour than a promise remembered.
**Where:** ROADMAP M307

<a id="def-166"></a>
#### Robotics Tier B (motion) and Tier C (the reflex layer).
**Why:** Deliberately human-gated: a person on the physical E-stop. **A judgement, not a schedule** — and the one row on this page that should never acquire an unblocking condition.
**Where:** [ROBOTICS_BRINGLIST.md](ROBOTICS_BRINGLIST.md)


## Closed at M657 — the licence, and the public repository

The single most consequential row on this page sat under **Open — externally
blocked** for three weeks after it had shipped. It still read *"Waiting on a JLU
rights answer"*, and it still carried, as its M326b evidence, *"no `LICENSE` or
`COPYING` file exists, so 'the first public commit needs it' holds."* Both halves
are false in the tree: `LICENSE` (Apache-2.0) and `NOTICE` have been committed
since **2026-08-27**, and the public repository has been published twice.

This is the M492 failure in its most expensive form. There, four rows had their
closure appended *behind* a reason still written in the present tense, and a
reader scanning the page met the stale assertion first. Here no closure was
appended at all — so a register built to answer *"why isn't this done?"* went on
answering *blocked on a rights question* about the one thing the project had
actually finished, with a tag on it, on two remotes. Nothing in the tree could
correct it, because nothing read this page. That is what
[`tests/smoke/deferred_register_lint.sh`](../tests/smoke/deferred_register_lint.sh)
is for, and it is deliberately structural: it could not have caught *this* row,
which was well-formed and wrong. What it catches is the class that made the wrong
row invisible — closed rows under open headings, open headings with no rows, and
a platform that exists in every other register and not in this one.

| Deferred | Detail |
|---|---|
| **The licence.** | [details](#def-167) |
| **The public repository.** | [details](#def-168) |

<a id="def-167"></a>
#### The licence.
**Closed by:** **M619 (2026-08-27): Apache-2.0**, copyright Justus-Liebig-Universität Gießen, author Alexander-Lars Dallmann, with `scripts/set-license.sh MIT` and the candidate text in the tree should a review switch it. The reasoning and the rights position are in [LICENSING.md](LICENSING.md). The decision is structural rather than remembered: `scripts/make-snapshot.sh` refuses `--commit` without a `LICENSE` file (M484), and `tests/smoke/snapshot_lint.sh` scans the tree that would be published (M485/M487).

<a id="def-168"></a>
#### The public repository.
**Closed by:** **Published 2026-08-27** (`v0.9.0`), re-cut **2026-09-17** (`v0.9.1`; public `573ccce` = private `a64cc4b3`) and again **2026-09-19** (`v0.9.2`; public `3e6a242` = private `f8ead53a`, 1,987 files compared byte for byte with 0 differing, both hosted CI runs green), on GitLab and GitHub, both pushes green on the hosted runner. The development history stays private and the repository advances by curated states — the mechanism, the two deliberate omissions and the tag rule are in [plans/2026-08-public-snapshot.md](plans/2026-08-public-snapshot.md).



## Closed at M503 — four signals that were silent, absent, or invisible

Reviewed as a batch with the operator, and the review itself is the lesson: of
five rows picked as "ready to build", **two turned out to argue against being
built** once read in full. Checking the reason before acting on it (the M326b
rule) is now three-for-three this month.

| Deferred | Detail |
|---|---|
| **Say something when a line typed before the first prompt is discarded** | [details](#def-169) |
| **Verify that a private file actually became private, instead of trusting `chmod`** | [details](#def-170) |
| **An `--auto` run silently inherits `testCommand`, and there is no opt-out** | [details](#def-171) |
| **A `doctor` line for the BSDs, illumos, and any other POSIX host** | [details](#def-172) |
| **Identify the unit check that failed once during M407's gate** | [details](#def-173) |

<a id="def-169"></a>
#### Say something when a line typed before the first prompt is…
(M464)
**Closed by:** `jc_term_readline` now probes for buffered input before the `TCSAFLUSH` that discards it, and says so: *"input typed before this prompt was discarded … please retype it."* The bytes are deliberately **not** recovered — the flush is what stops stray type-ahead from answering a `y/n` prompt nobody has read — so the honest fix is a notice with a way forward, not a smuggled line. The probe reuses M156's existing `input_pending` rather than adding a second readability check that could drift. `preprompt_discard.sh`, 3 checks (one asserting the stray text still never reached the model), proven red.

<a id="def-170"></a>
#### Verify that a private file actually became private, instead of trusting…
**Closed by:** `doctor` now creates a probe under `~/.jichi.d`, tightens it, and **reads the mode back**: `✓ created 0664, tightened to 0600, read back`. On a filesystem that accepts `chmod` and ignores it — MSYS2's default `noacl` mount, some network mounts — it reports *"private files are NOT private on this filesystem"* and names what is exposed (the API key file, the daemon socket, the audit log). **The policy question the row left open is answered by precedent:** WARN interactively, **FAIL under `--unattended`**, joining M158b's explicit escalation set for the same reason `privilegedAudit: false` is in it. It also states what it did *not* exercise: under a narrow umask the probe is already private before the tightening runs, so the guarantee is verified while the mechanism is not. Proven with a new `JC_FAULT_CHMOD` site — the same justification as `JC_FAULT_PROCFS`, since there is no `noacl` mount on the bench and the branch guards a safety property. `faults.sh` checks 9-10.

<a id="def-171"></a>
#### An --auto run silently inherits testCommand, and there is no opt-out
**Closed by:** **Half of it had rotted:** `--verify ""` already disarms the gate, inherited or not — measured, and it was simply undocumented. The half that was real is now built: the journal's `start` event carries **`verify_source`** (`flag` \| `config` \| empty), because `verify: make test` used to mean both "the operator chose this" and "the config supplied it", and those differ sharply when the gate then fails. A deliberate `--verify ""` records as `flag`, so a disarm is a decision rather than an absence. Documented in [AUTONOMY.md](AUTONOMY.md) §"Where the verifier comes from"; `verify_source.sh`, 4 checks, proven red.

<a id="def-172"></a>
#### A doctor line for the BSDs, illumos, and any other POSIX…
**Closed by:** **Declined, on the row's own evidence.** It already said the M400 note fires on any non-Linux `uname`, that no BSD-specific code exists, and that "inventing conditionals for a platform nobody has is how the Darwin branch got into the state M400 found it in". Then M460/M461 measured it: FreeBSD and OpenBSD found **ten real defects between them and none of them wanted a BSD conditional** — every fix was a capability probe, a portable flag, or POSIX-correct signal discipline. A row whose reason has been confirmed by measurement is not deferred work; it is a decision.

<a id="def-173"></a>
#### Identify the unit check that failed once during M407's gate
**Closed by:** **Closed as unidentifiable.** One occurrence, never reproduced, no artefact kept, and nothing left to examine. The honest action is to say so rather than carry a row nobody can act on — and to point at the mechanism that would catch the next one: *the M201 re-ask for the unit suite* (still open below), which labels a failure "in-suite only" or "also alone" instead of leaving it a mystery.


## Closed 2026-09-21 — the source-reading guides, both rows

From the M399 review of `docs/reading/` (25 files measured against their own
documented chapter skeleton; Annai measured complete and uniform, Fukabori
measured thin in one specific way).

| Deferred | Detail |
|---|---|
| ~~**Bring the remaining Fukabori chapters to Annai's code density.**~~ | [details](#def-174) |
| ~~**A lint that each reading chapter carries a diagram or is a stated exception.**~~ | [details](#def-175) |

<a id="def-174"></a>
#### Bring the remaining Fukabori chapters to Annai's code density.
**— CLOSED, the premise expired and the row said so.** Re-measured 2026-09-21: Annai 11 chapters mean **2.7** blocks, Fukabori 12 chapters mean **2.1** — the inversion this row was filed for, the expert guide showing less code than the beginner guide, is gone. Each guide has exactly **two** chapters at one block, and in both they are the chapters that argue or point rather than explain.
**Why:** The row itself asked to *"close with the next reading-guide pass rather than carry a premise that expired"*. This is that pass.
**Where:** `docs/reading/`, DECISIONS "Documentation and teaching"

<a id="def-175"></a>
#### A lint that each reading chapter carries a diagram or is…
**— CLOSED as a DECISION, not as work.** Measured across all 27 chapters, which no earlier count did: Annai 9/11, Fukabori 10/12, **Tsuiseki 0/4**. The row scoped itself to Fukabori and was right there; widening the count does not show drift, it shows structure — the four missing in Annai and Fukabori are *the same four* at one code block, and Tsuiseki is a whole guide whose form is a replayed trace.
**Why:** A lint would encode 8 exceptions across 27 files and check 19 for a property they already have. Recorded in `DECISIONS.md` instead, with the revisit condition sharpened: drift is a **mechanism** chapter without a diagram, not another argument chapter.
**Where:** `DECISIONS.md` "Documentation and teaching"


## Open — platforms never DRIVEN (M665, 2026-09-18)

Raised by the operator: *"jichi must be tested on all platforms with actual
meaningful agentic tasks using language models. That is the point of using jichi,
and must be tested."* It is correct, and the matrix did not measure it.

**Every gate a platform row runs is offline** — build, unit suite, smoke tier and
the four surfaces (`--version`, `doctor`, `describe`, `context`). All of them can
be green on a kernel where jichi has never called a model. `docs/PLATFORMS.md`
now carries a fourth word, **Driven**, for a row that has run a live agentic task.

**The first count published with it — "three of nineteen" — was wrong**, and the
correction is the more useful finding. It counted what `PLATFORMS.md` records and
was written as though it counted what has been done. The **M459 fleet push**
(2026-08-15/16) had already driven a **Raspberry Pi, an Android tablet and a
proot guest** with a model over ssh, executing tools and writing files, with two
findings still in doctrine — recorded in `AUTONOMOUS_LOOPS.md`, `DISTRIBUTED.md`,
ANECDOTES #55 and ROADMAP M459, and **nowhere on the matrix**. The gap is that
the platform rows do not carry driven-ness, not that it never happened.

| Deferred | Detail |
|---|---|
| **Record driven-ness ON the platform rows, and drive the rows that have not been.** | [details](#def-176) |
| **Decide what the minimum driven task IS** | [details](#def-177) |

<a id="def-176"></a>
#### Record driven-ness ON the platform rows, and drive the rows that…
**Why:** Documented today: **WSL2** (chat, reasoning, tool use, embeddings), the **M459 fleet** — Pi, Android tablet, proot guest, a fenced `--auto` task that executed tools — **illumos** (a text turn only, M663 — no tool call), **FreeBSD** (text turn 4 s *and* a native `read_file` tool call, 7 s, M665). **Updated 2026-09-22 (M697): the Driven table now covers every row on the page, and Windows + Cygwin and Windows + MSYS2 are DRIVEN** — both turns, agentic phrases `M697-cygwin-3FF107` and `M697-msys2-63B3BA`, over the HRZ gateway rather than a loopback tunnel, and their rows say which transport carried it. This sentence also listed OpenBSD and NetBSD as never having made a model call, which was already wrong when written: both were driven 2026-09-19. What remains undriven: the Pis other than the two rig rows, Termux/proot, Guix and musl-static. **Updated 2026-09-24: musl-static (M736, x86-64 and aarch64), FreeMiNT (M737) and Guix System (M738) are DRIVEN**, each through a rig, both turns; so are V2e and V2f (M736). What remains undriven now: the Pis other than the two rig rows and the other hardware rows -- the other machine's -- Termux/proot, and the old-userland containers of the M736 ladder, which build and pass the unit suite and have made no model call. **The emulated architectures came off this list at M699, 2026-09-21: 19 of 19 runnable triples DRIVEN, both turns.** The prediction here was that driving them was "not merely undone but impossible as built" -- those rows linked `HAVE_CURL=`, so there was no HTTP at all -- and it was right about the cause and wrong about the remedy being out of reach: `scripts/tier-v-arch.sh` gained `--drive`, which builds a TLS-free libcurl per triple through `minimal-curl.sh`'s cross rung. It did need a different rig; the rig was three days' work rather than a category change.
**Where:** `docs/PLATFORMS.md`, the **Driven** verdict

<a id="def-177"></a>
#### Decide what the minimum driven task IS
so rows are comparable.
**Why:** FreeBSD's *read a file and report its contents* exercises request build, SSE framing, a native tool call, tool execution and the second turn — that is a defensible floor and it is what the FreeBSD row used. It is **not** yet written down as the standard, and rows measured against different tasks are not comparable, which is the same mistake `JC_SMOKE_TIMEOUT_MULT` was introduced to stop people making with build times. A candidate: one text turn, one tool-calling turn, one refused-by-a-fence turn.
**Where:** undecided


## Closed at M698 — the transport rows: one gap that had already closed, one that had not

**This section exists because a register went stale and nobody re-read it.** The
row here said *"the reverse-tunnel arrangement is not in any rig"*: both driven VM
rows were set up by hand with `ssh -R 1234:127.0.0.1:1234`, it was written down in
two analysis pages, and it was in no `scripts/tier-v-*.sh`.

**It was true when written and untrue from M677.** `jc_rig_live` carries the
forward; **seven** rigs use it; and `rig_live_lint` check 5 now *enforces*
`-o ExitOnForwardFailure=yes` on every one of them — because a forward that
cannot bind otherwise WARNS and runs the command anyway, which once produced two
green live turns through a stale forward nobody knew was there. The register went
on claiming the gap for two milestones after it closed, which is the same defect
as an out-of-date platform row and is worth more than the row itself: **a register
is only as good as the pass that re-reads it.**

**The genuine gap was the other direction, and M698 closes it.** Every transport
that existed when the driven task was extracted reached a **keyless** server — LM
Studio on the host's loopback, through a tunnel or an existing forward — so the
config could carry `"apiKey":"unused"` and nothing noticed. The Windows-family
rows cannot use that transport at all: they are not guests, there is nothing to
forward from, and LM Studio is not installed on the machine that has Cygwin and
MSYS2. They drive a real **gateway over TLS**, which wants a key.

`jc_rig_live_config` now takes an optional third argument, the **name** of an
environment variable, emitted as `apiKeyEnv` so no secret reaches the config file
or `ps` — jichi's own doctor warns about a literal `apiKey`, and a rig that
generated one would teach the habit the product warns against. With no third
argument it emits what it always emitted, **byte for byte**, so no existing row's
config changed. The alternative was a second config generator inside the Windows
rigs, and `_rig_ship.sh`'s header settles that: *a second rig is where drift
starts, not the fourth.*

Both halves are now exercised by rigs that have run: the tunnel by the three VM
rows, the gateway by `tier-v-cygwin.sh` and `tier-v-msys2.sh`, each of which also
refuses a model outside the free namespace and refuses an empty key **before** the
first request — because the failure mode of guessing is a charge, and the failure
mode of refusing is a message.

## Open — platforms never compiled

From M400, which asked the honest form of "we have not built this on macOS or WSL":
[`PLATFORMS.md`](PLATFORMS.md) now owns every platform verdict, and these two are
measurements this project does not have.
**Found missing at M657, and the reason it was findable nowhere is the point.** The illumos row below existed in three other documents and not in this one, so a reader following this page's own promise — *"things deliberately not done, with the reason"* — would have concluded the never-compiled set was one platform wide. [`deferred_register_lint.sh`](../tests/smoke/deferred_register_lint.sh) check 4 now enumerates that set from `PLATFORMS.md`, which owns platform verdicts, and fails the build when one of them has no row here. Enumerating the universe a second way, by a different route, is where this project's last four documentation gaps came from (M508, M510, M511).


| Deferred | Detail |
|---|---|
| **Compile jichi on macOS and run `make check-target`.** | [details](#def-178) |
| **Compile jichi against MiNTLib and run it on FreeMiNT under ARAnyM.** | [details](#def-179) |
| **Compile jichi with DJGPP and run it on FreeDOS under QEMU.** | [details](#def-268) |

<a id="def-178"></a>
#### Compile jichi on macOS and run make check-target.
Closed at M817. **Why:** **Closed at M817:** Dima Mellnik compiled, verified and drove it on a MacBook Air M4 (v0.12.0, `MAC-50FFBE`); the row is Partly verified in PLATFORMS.md. The text below is the record as it stood before: **Never compiled — not once.** Not deferred out of doubt: there is no Mac on this project. **Checked before parking (the M326b rule):** the old claim "no Darwin-specific code" in both `BUILD.md` and `INSTALL.md` was **false** — `jc_mem_total_mb` has an `#if defined(__APPLE__)` `sysctl(HW_MEMSIZE)` branch, and it used `unsigned long long` + `ULL` constants, which is three diagnostics under this project's own mandatory `-std=c89 -pedantic -Wall -Wextra` and a failed build under `WERROR=1`. So the honest state was worse than "untested": jichi's only macOS code **could not have compiled**, and no build here would ever have said so. Fixed and pinned by a lint in the same milestone. **Revisit** the moment anyone has access to a Mac for an afternoon; one `uname -srm` + two check counts turns a "never compiled" row into a verified one.
**Where:** [PLATFORMS.md](PLATFORMS.md)

<a id="def-179"></a>
#### Compile jichi against MiNTLib and run it on FreeMiNT under ARAnyM.
**Why:** **Never compiled.** Asked on 2026-09-22 out of curiosity and answered only in a chat; planned on 2026-09-24 because that answer's *"never Driven"* was wrong — a libcurl built with no TLS talks plain HTTP, which is how 19 emulated rows were driven (M699), and ARAnyM's point-to-point network link can carry the same. The payoff is the libc and the kernel, not the CPU: big-endian m68k is covered (M469). **Steps 0–2 ran on 2026-09-24** (the plan's §8–10). The tree cross-compiles clean (M722, M723). M726 gave MiNT builds a 512 KB stack, and M727's rig, `scripts/tier-v-freemint.sh`, found and fixed a recursion that needed more: under ARAnyM the cross-built unit suite completes, 13,707 checks and 19 failures, and `jichi --version` runs. The matrix row went into `PLATFORMS.md` with the measurement. **Step 4 ran on 2026-09-24 (M737), and the row is Driven**, still cross-built and emulated: jichi with a TLS-free libcurl for MiNT ran both live turns from the guest over ARAnyM's tap link (`TIER-M-5D5D0C`), with no setuid helper -- `bridge` mode needs only a tap device the operator made once -- but with an ARAnyM built from source, because Ubuntu 26.04's has no ethernet (the plan's §11 says why). **Next:** the 19 failures (the section *"Open — found by FreeMiNT steps 1 and 2"*), then step 3's userland; a native compile needs a compiler on MiNT that can build the tree.
**Where:** [plans/2026-09-freemint-aranym.md](plans/2026-09-freemint-aranym.md)

<a id="def-268"></a>
#### Compile jichi with DJGPP and run it on FreeDOS under QEMU
**Why:** **Never compiled.** Chosen by the operator on 2026-09-29 as the first fork-less row, because it is fully headless from the workstation and the hardest case for jc_spawn's contract (a synchronous launch, no pipe). Step 0 is done (§5 of the plan), and M833 to M835 closed its five findings: since M835 the tree cross-compiles with no workaround, 322 of 322 objects with 0 warnings, and both binaries link. Since M836 the rig (`scripts/tier-v-freedos.sh`) boots FreeDOS and runs the unit suite there: 14,072 checks, 108 failures, each with a row above. What remains, in order: a synchronous jc_spawn backend (def-269), and Watt-32 with a DJGPP libcurl, which also splits `JC_NO_SOCKETS` (Watt-32 brings `AF_INET` back, never `AF_UNIX`); then a driven model turn.
**Where:** [plans/2026-09-29-the-fork-less-rows.md](plans/2026-09-29-the-fork-less-rows.md)



## Open — what the illumos row found and did not diagnose (M658)

The first illumos row behaved the way the platform doctrine says a
never-compiled row behaves — it detected defects. Three were fixed in the same
milestone. These two were measured and **not** diagnosed, and they are recorded
rather than guessed at.

| Deferred | Detail |
|---|---|
| **The smoke tier invokes `make` by name in several drivers, and on illumos that is Sun make.** | [details](#def-180) |

<a id="def-180"></a>
#### The smoke tier invokes make by name in several drivers, and…
**Why:** Found at M661 inside `install_no_build`, where four checks failed with messages about refusals and DESTDIR that had nothing to do with `install`: the driver shelled out to `make`, illumos **has** a `make`, and it is not GNU's. That driver now uses `${MAKE:-make}` — GNU make exports `MAKE` to every recipe shell, so a driver run by `make smoke` gets the same make that is running it. **Counted before deciding not to sweep:** across `tests/smoke/*.sh` the real invocations are `make install` (15), `make clean` (11), `make test` (9), `make ci` (7), `make info` (6), `make smoke` (4) — but most of the 57 files matching `make ` match it in *prose*, so a blind regex sweep would rewrite comments and grepped strings. **Revisit as** one pass with the count above as its floor, or when a second driver fails this way on a non-GNU-make platform.
**Where:** `tests/smoke/install_no_build.sh`


## Open — what the FreeBSD re-run found and did not diagnose (M665, 2026-09-18)

The row was re-run because its coverage debt had reached **102** — it last
recorded 201 drivers on 2026-08-17 against 303 in the tree. It came back at
**296 of 303** with six failing drivers. **Five were harness defects and are
fixed**; they are not listed here because they are closed. What follows is what
was measured and **not** explained.

Full account, including the four hand-built probes that were all broken:
[`analysis/2026-09-18-freebsd-and-the-probes-i-built.md`](analysis/2026-09-18-freebsd-and-the-probes-i-built.md).

| Deferred | Detail |
|---|---|
| **Finish the bare-`make` conversion across the tier.** | [details](#def-181) |
| **The README's platform count is pinned by nothing.** | [details](#def-182) |

<a id="def-181"></a>
#### Finish the bare-make conversion across the tier.
**Why:** `smoke_make` now exists in `tests/smoke/_smoke.sh` (resolves `$MAKE`, else `gmake`, else `make`) and **two drivers are converted** — `cppcheck_lint` and `install_no_build`, the two that failed. The tier-wide count stands at **install 15, clean 11, test 9, ci 7, info 6, smoke 4**. The rest are unconverted deliberately: a blind sweep across sites whose behaviour on four kernels has not been measured is how a portability fix becomes a portability bug. **Note the shape of the bug this closed:** `install_no_build`'s own header said *"`${MAKE}`, not `make` (M661)"* while one line still ran bare `make` and three more used `"${MAKE:-make}"`, whose fallback is bare `make` exactly when a driver runs standalone — which is what the rig does to classify a failure. A rule stated in a header and applied at some sites is not a rule.
**Where:** `tests/smoke/_smoke.sh`, `smoke_make`

<a id="def-182"></a>
#### The README's platform count is pinned by nothing.
The sentence used to read "Nineteen of them"; it now reads "**20 rows Verified** ... plus **3 Partly verified** ... and **1 Never compiled**", which is three numbers rather than one and still prose.
**Why:** Left unchanged, because a replacement number would be a guess. `portability_lint` check 14 now pins the *never-compiled* half of that paragraph against `PLATFORMS.md`; the count itself is still prose nothing checks.
**Where:** `README.md:207`


## Closed at M661 — the install_no_build row, and the diagnosis that was wrong

| Deferred | Detail |
|---|---|
| ~~**`install_no_build` fails inside a full `check-target`.**~~ | [details](#def-183) |

<a id="def-183"></a>
#### install_no_build fails inside a full check-target.
**DONE at M661 — and the M658 diagnosis recorded here was WRONG, which is worth more than the fix.**
**None:** M658 called this *"an in-suite order dependence"* and went on to accuse the harness's classifier of inheriting the suite's state. **Neither was true.** `make install` copies **both** binaries; `smoke: $(BIN) smoke-tools` builds only `jichi`; `check-target: test smoke` therefore never built `jichi-convert` — so the driver failed for a **missing file** on every device row. Reproduced on the host in one command by moving that file aside: the same two checks, the same two messages. The classifier was **right** to say "ALSO fails standalone → a real defect"; it simply could not say *which* defect, because the file was still missing on the retry. Fixed at both ends: `check-target` builds `all`, and the driver states the precondition and skips with the reason if it is ever unmet another way. **The lesson is the one this page is about:** the reason I filed was a plausible story that fitted the evidence, and checking it cost one `mv`.
**Closed by:** `tests/smoke/install_no_build.sh`, `Makefile`


## Closed at M658 — illumos, compiled

**Added to this register at M657 and measured the same night.** The row had
existed in `PLATFORMS.md`, `LOW_MEMORY.md` and `PLATFORM_RETEST.md` §6 for four
months as "the cheapest remaining row" and in this register not at all; checking
what *blocked* meant found KVM, QEMU and 137 GB free on this machine, so it cost
a session rather than a resource. It took about three hours, and it behaved the
way the platform doctrine says a never-compiled row behaves: **a defect
detector.**

| Deferred | Detail |
|---|---|
| **Compile jichi on illumos and run the gate.** | [details](#def-184) |
| **The M469 procfs prediction, reasoned from the source since 2026-08-17.** | [details](#def-185) |

<a id="def-184"></a>
#### Compile jichi on illumos and run the gate.
**Closed by:** **M658, 2026-09-18: OmniOS CE r151058 under KVM** (cloud image + cloud-init, ssh in 30 s — no graphical console step, which is what made Guix expensive). gcc 14.3.0, GNU Make 4.4.1, `/bin/sh` = ksh93. `gmake WERROR=1 CC=gcc`: **zero diagnostics**; unit suite **13,273 checks / 0 failures**; smoke **238 of 300 drivers, 1,299 checks**. Recorded as **Partly verified**, not Verified — the honest word for a green build, a green unit suite and a smoke tier with 62 failing drivers. [PLATFORMS.md](PLATFORMS.md) carries the row, the two clusters the failures fall into, and what was *not* diagnosed.

<a id="def-185"></a>
#### The M469 procfs prediction, reasoned from the source since 2026-08-17.
**Closed by:** **Measured on the box.** `/proc/self/stat` and `/proc/self/exe` are absent (`/proc/self/path/a.out` is the equivalent); `/proc/self/status` exists as a **1,136-byte binary `pstatus_t`** in which `VmRSS:` occurs **zero** times, so `jc_meminfo_parse` reports *no data* rather than *wrong data*. M647 had already reduced the parsing half to a pure function and pinned it (`tests/test_meminfo.c:test_binary_status`) precisely because it needed no box; the three things that did need one — that `fopen` succeeds, that the struct has this shape, that the `fread` path behaves — are now measured too.


**What it cost the tree: three defects, none of which wanted a platform
conditional** — `-D__EXTENSIONS__` and `-lsocket -lnsl` as Makefile probes, and
`search_code`'s `grep -rnI`, which illumos's grep rejects outright. That last one
had killed the tool on that platform entirely, and it is the M461 `--color`
defect one platform later: a flag four greps accept is not a portable flag. All
three are in [DECISIONS.md](DECISIONS.md).


## Open — verified rows the tree has outgrown (M647)

[`PLATFORM_RETEST.md`](PLATFORM_RETEST.md) turned staleness into arithmetic: a
row's **coverage debt** is the smoke drivers in the tree today minus the drivers
that row actually ran, and past 100 the row is a *historical* datum rather than a
present-tense claim. The policy page computes the number; this is the page a
reader consults for "why isn't this done", so the two rows it produces belong
here as well.

| Deferred | Detail |
|---|---|
| **Re-run the Raspberry Pi Zero 2 W row at rung 3** | [details](#def-186) |
| **State a driver count in the rows that state none** | [details](#def-187) |

<a id="def-186"></a>
#### Re-run the Raspberry Pi Zero 2 W row at rung 3
(`make check-target`).
**Why:** Its verdict is still M272's — **94 drivers**, debt **205** against today's tree, and the only full-gate row past the 100 threshold. README and PLATFORMS both make present-tense physical-ARM claims that rest partly on it, which is precisely what a debt over 100 says needs a new run first. **Ran 2026-09-18 (M658), and it did NOT retire this row — read the word size.** The operator powered the boards; the Pi Zero 2 W answers, and it is running **32-bit armhf**, which is the M454 row, not M272's **aarch64** one. The armhf row was re-run at rung 3 and is now current (297 of 299 drivers, 13,329 unit checks, debt 3, down from 106). The aarch64 row at debt 206 still stands, and retiring it needs the board re-flashed to 64-bit — a human with an SD card, not a session. The Pi 400 (aarch64) was re-run the same night and its unit suite is green, but its smoke tier stopped at one driver, so it does not stand in for this row either.
**Where:** [PLATFORM_RETEST.md](PLATFORM_RETEST.md) §3

<a id="def-187"></a>
#### State a driver count in the rows that state none
so their debt is a number at all.
**Why:** Counted 2026-09-18: **18 Verified rows, 7 of which cite a driver count** — and the debt table works five of those seven. For the other eleven, including the **Pi 400's M451 full `check-target`**, debt is not a large number, it is *not a number*. `platform_retest_lint` check 2 asks that a row be **datable**, which is strictly weaker than **debt-computable**, and the gap is invisible from the table because the table can only show the rows that have the figure. The fresher aarch64 evidence is in that blind spot: M451 is four months newer than M272 and cannot be weighed against it. **Revisit with** the next re-run of any row — the count is free at the time and unrecoverable afterwards.
**Where:** [PLATFORM_RETEST.md](PLATFORM_RETEST.md) §3



## Closed — done, and formerly left sitting under an *Open* heading

Four rows that were **done and still filed as open**. Three were reported at M475 and
fixed at M488; the fourth is M475's own WSL2 row, marked **DONE (M475)** and left for
thirteen milestones under a heading reading *platforms never compiled* — about a
platform `PLATFORMS.md` now records as **Verified, full gate**.

**Moved here rather than left annotated in place,
because that is what went wrong:** the closure was appended to the *end* of each row,
behind a reason still written in the present tense, so `--api-base` went on reading
*"there is **no** base-URL flag"* for four milestones after `main.c` began parsing one.
A reader scanning the register met the stale assertion and never reached the closure —
and all three sat under an **Open** heading while doing it. Found by the
`feature/hrz-model-info` session while landing (M492), which noted it is the same shape
as the `docs_flags.sh` `future` entry that same branch removed: a row saying *"move this
line out when the flag ships"* that outlived the shipping by a milestone.

This page says at its head that items are **removed** when done; ten `## Closed` sections
say the practice is gentler. Either is fine. A closed row under an *Open* heading is not.

| Deferred | Detail |
|---|---|
| **Stop `jc_eventlog_open` tightening a parent directory it does not own.** | [details](#def-188) |
| **Stop a failed precondition in `test_session_roundtrip` from crashing the suite.** | [details](#def-189) |
| **Add `--api-base` to `jichi setup`'s non-TTY flag form.** | [details](#def-190) |
| **Compile jichi under WSL2 and run `make check-target`.** | [details](#def-191) |

<a id="def-188"></a>
#### Stop jc_eventlog_open tightening a parent directory it does not own.
**Why:** **Found 2026-08-18 (M475); reported, not fixed.** `make_parent_dir()` calls `jc_make_private()` on the log path's parent **even when that directory already existed**; the guard catches `/` and nothing else. Run as root — every container, most CI — `--log /tmp/jichi.jsonl` turns `/tmp` into 0700 root-only for the whole machine (measured: 1777 before `run_tests`, 700 after; call site captured with an `LD_PRELOAD` shim on `chmod` and PIE offsets resolved against the symbol table). Non-root is inert **by accident**: the `chmod` fails with EPERM and the return is discarded. **Deferred because the fix is a design call, not a patch** — skip pre-existing directories, refuse when the owner differs, or tighten only what `jc_mkdir_p` actually created; each changes M132's privacy guarantee differently and M132 was deliberate. Note that `jc_platform_posix.c` already reasons about this exact hazard for the state root (*"jc_make_private applies a mode to whatever the path RESOLVES to"*) and guards it with an `lstat` owner/mode check — **the sibling two files away has none.** When a hazard earns a documented fix, grep for its family.
**Where:** `src/util/jc_eventlog.c`  **CLOSED (M488).** Fixed as a FAMILY rather than a patch: `jc_mkdir_p_private()` creates the chain and applies 0700 **only to what it created**, and all four `jc_mkdir_p`+`jc_make_private` sites route through it — two of which (`--log`, `--control`) take the path from the operator, so the hazard was never confined to telemetry. `posix_utils_lint` check 7c bans the old pairing, proven two-sided. **The design question is answered and the rejected options recorded:** refusing on an owner mismatch turns a legitimate shared directory into an error, and chmod-ing only when the mode is wider than 0700 still re-permissions `/tmp`, just conditionally. M132 is unchanged for everything jichi owns — a directory it creates is still 0700, the file still 0600. Unit-tested both ways (`test_parent_dir_not_retightened`, `test_created_dir_is_private`).

<a id="def-189"></a>
#### Stop a failed precondition in test_session_roundtrip from crashing the suite.
**Why:** **Found 2026-08-18 (M475); reported, not fixed.** Reachable once the row above has poisoned `/tmp`: a non-root `make test` reports `FAIL tests/test_session.c:759` (save failed), then four more FAILs that read a `struct jc_session` the failed `jc_session_load_by_id` never populated, then `free(): invalid pointer` / SIGSEGV — deterministic across three runs. `JC_CHECK` is non-fatal **by design**, so a failed *precondition* is followed by code dereferencing the object it was meant to produce. Worse, the abort discards block-buffered stdout, so the FAIL lines that name the cause vanish and the reader gets a bare `Aborted (core dumped)` — `stdbuf -o0` was needed to recover them. **Deferred because the fix is a design call:** leave the block when a precondition fails, or make precondition checks fatal suite-wide (which changes every test's semantics).
**Where:** `tests/test_session.c`  **CLOSED (M488).** Reproduced deterministically **and without root** — pre-create that test's own sessions directory read-only, and it exits **139**. The fatal step turned out not to be the reads named above but `jc_session_free(&back)` on **uninitialised stack memory**: `jc_session_load_by_id` correctly leaves its out-param untouched on failure, and the product's own callers all bail before using or freeing it (checked, so no product change). Fixed by zeroing `back` and guarding the two preconditions with the existing `JC_REQUIRE`; the suite-wide semantics change was not needed. Now: exit **1**, both FAIL lines naming the cause, and the other 12,413 checks still run. `JC_REQUIRE`'s own comment records an audit that found 19 sites across 8 files (M452) — this was the twentieth, and that audit missed it.

<a id="def-190"></a>
#### Add --api-base to jichi setup's non-TTY flag form.
**Why:** **Found 2026-08-18 (M475).** `setup` correctly detects a non-TTY and prints the flag form — `--preset --provider --model --key-env --from-global --import` — but there is **no base-URL flag**, while the interactive wizard *does* prompt for `"apiBase URL"` (`src/main.c:4943`) and `model_obj()` already writes `apiBase` when given one (`jc_setup.c:333`). The machinery exists; only the CLI surface is missing. It bites precisely where it matters: **`small-local`** ("Small local model (7-14B)") and **`constrained`** exist for locally-hosted models, which by definition need a custom endpoint — so following the tool's own non-TTY guidance yields a config pointing at the provider's cloud. Workaround is the one `setup` already prints (`$EDITOR local/config.json`), and the generated scaffold is otherwise sound: verified against a local OpenAI-compatible server, `doctor` 19 ok / 0 problems and live inference green once the line is added.
**Where:** `src/main.c`, `src/setup/jc_setup.c`  **CLOSED (M488).** The flag exists, is wired to `ans.api_base`, and is documented in `SETUP_WIZARD.md` with the local-server example — and, the half that actually mattered, **the not-a-TTY message now names it**, because that printed flag list was itself what produced a config pointing at the cloud. Three smoke checks, two-sided: the flag reaches the config, omitting it still leaves the provider default, and the guidance names it.

<a id="def-191"></a>
#### Compile jichi under WSL2 and run make check-target.
**Why:** **DONE (M475, 2026-08-18).** The row was right about which parts mattered and wrong about how much. The compiler was a non-event — `make` clean on the first try, **12,418 unit checks / 0 failures** — and of the three subsystems it named, **only `/mnt/c` behaved differently**, exactly as warned (the same commit reads 0 modified on ext4 and **1,639 modified** through v9fs, from line-ending translation). The terminal surprised in the *other* direction: smoke ran **209 drivers / 1,104 checks at `JC_SMOKE_TIMEOUT_MULT=1`**, so WSL2's pty layer times like real hardware, not a constrained VM. `PREPARE_AND_BUILD.md`'s walkthrough has now been **executed end to end** — by a non-root user, against pristine HEAD, and it passes. What the row did not anticipate: running the *full gate* found **four pre-existing defects in `ci`-only configurations** (clang, sanitized, valgrind, no-emacs), none WSL-specific, all reproducing on bare-metal Ubuntu 24.04, all traceable to M472 having been measured against gcc/unsanitized/with-emacs — the third instance of the M447/M189 shape. Verdict and numbers: [PLATFORMS.md](PLATFORMS.md); session write-up: anecdote 62 in [ANECDOTES.md](ANECDOTES.md).
**Where:** [PORTING_WINDOWS.md](PORTING_WINDOWS.md)


## Closed at M501 — two ways an explicit intent lost to an inference

| Deferred | Detail |
|---|---|
| **An inferred constraint can override an explicit `--edit-scope`.** | [details](#def-192) |
| **The envelope attributes every mid-run change to the run — a concurrent actor's edits get reverted under `revertOutOfScope`.** | [details](#def-193) |

<a id="def-192"></a>
#### An inferred constraint can override an explicit --edit-scope.
**Closed by:** Already fixed at **M459** for `write_file`/`edit_file` — and the row had rotted, which the M326b rule caught: checking it before acting found the fix in place with six checks green. What was still broken is that M459 tested **tool names**, so `apply_patch` — whose paths live in `edits[]`, and which is what a model reaches for when making several edits — was never exempt, and neither were `generate_audio`/`generate_image`/`record_audio` (a top-level `path` outside the name list). M501 replaces the name list with a property of the **arguments** (`jc_argpath_collect`): exempt when the call declares at least one path and **every** path it declares is in scope. All-or-nothing, because `apply_patch` is atomic and one out-of-scope path must refuse the whole call — and an overflowed collection (more paths than the buffer holds) counts as *not* exempt, so a truncated view can never widen a permission. `constraint_vs_scope.sh` checks 7–9, proven red against M459's behaviour.

<a id="def-193"></a>
#### The envelope attributes every mid-run change to the run — a…
**Closed by:** M501 took the row's second option ("scope the revert to paths the run's own tools touched") and made it provable rather than heuristic. jichi can change a file in exactly two ways: the write chokepoint (fenced to the scope) and a shell command (not fenced). So the envelope now records both — the paths written through `jc_app_write_file`, and whether any shell tool ran — and reverts an out-of-scope change when it was written by this run **or** when a shell command ran (attributable). When neither is true the change **cannot** be the run's: it is left alone, named on stderr, and journalled as `not_ours`. `revert_provenance.sh`, 6 checks, with a floor under the "still reverted" half after the first version passed while the fixture's shell call never happened. **The residual is documented rather than closed:** a run that uses the shell still cannot tell its own out-of-scope writes from a colleague's, so the operating rule stays *one envelope per working tree at a time* — see [AUTONOMY.md](AUTONOMY.md) §"What `revertOutOfScope` will and will not undo".


## Open — found by the 2026-08-20 dogfooding runs (M504)

| Deferred | Detail |
|---|---|
| **Decide whether `.jichi/` state is implicitly inside the edit scope.** | [details](#def-194) |

<a id="def-194"></a>
#### Decide whether .jichi/ state is implicitly inside the edit scope.
**Why:** Measured: a run scoped `--edit-scope 'src/agent/**'` journalled `out_of_scope: [".jichi/memory.md"]` -- the agent writing its own memory file. Detection-only there, so it was reported and kept, which is the right outcome; the question is whether it should have been reported at all. **For an exemption:** it is jichi's own bookkeeping rather than the user's code, an operator trained to ignore the notice will ignore a real one, and under `revertOutOfScope: true` the M501 rule *would* revert it (a run that used the shell is attributable). **Against:** an implicit exemption is a hole in a fence, `.jichi/memory.md` is a file the MODEL controls, and an operator who wants it writable can say so with a second `--edit-scope`. **Not decided here** because it is a fence-semantics decision, not a defect -- and the current behaviour is the conservative one. **Revisit when** a run with `revertOutOfScope: true` loses memory it should have kept, or an operator reports the notice as noise.
**Where:** [analysis/2026-08-20-three-runs-two-projects.md](analysis/2026-08-20-three-runs-two-projects.md)


## Open — from the M505 self-review

| Deferred | Detail |
|---|---|
| **A four-persona documentation pass over the M499/M502 wave.** | [details](#def-195) |

<a id="def-195"></a>
#### A four-persona documentation pass over the M499/M502 wave.
**Why:** M505 ran the *mechanical* half of `DOC_REVIEW.md` over the four new pages and five new sections -- every copy-pasteable command executed, every factual claim checked against the source -- and found one overstated claim plus the product defect above. What it could not do is the judgement half: the author and the reviewer were the same agent, which invalidates exactly the independence the four-reviewer method buys. **Revisit when** an independent pass can be run (a person, or agents given the read-only reviewer brief and no authoring history), before the public release. The mechanical floor is in place, so that pass starts from a corpus whose commands all run.
**Where:** [DOC_REVIEW.md](DOC_REVIEW.md)


## Closed at M506 — the budget stop that reported no verdict

| Deferred | Detail |
|---|---|
| **A budget-stopped run never runs its verifier, so a run that satisfied its own gate is reported as a failure with no gate verdict.** | [details](#def-196) |

<a id="def-196"></a>
#### A budget-stopped run never runs its verifier, so a run that…
**Closed by:** The envelope ran the verifier at a budget exit only when the result could change the **rollback decision** (rollback armed *and* a green checkpoint banked) — correct for a decision, wrong for a report. A run whose gate was red at the start never banks a green, so the verifier was skipped entirely. Measured three times, twice in one afternoon of dogfooding, and one of those runs had done the valuable half of its task with its gate passing a minute later. Now the same verifier runs **for the record** in that case: journalled as `phase: "budget_exit_advisory"` with `advisory: true`, the exit code and the declared kind, and said on stderr when green (*"the run stopped on a budget, but its verifier PASSES on the tree as it stands"*). **Advisory by construction** — the code is journalled, logged, and dropped; the outcome stays `budget_exhausted`, because turning a green advisory verdict into a pass would make a stopped run indistinguishable from a completed one. `budget_stop_verdict.sh`, 4 checks, with check 3 pinning exactly that guarantee and check 1 a floor proving the fixture really stopped on a budget. Proven red. Cost stated in AUTONOMY.md: one extra verifier run per stopped run, bounded by `--verify-timeout`, avoidable with `--verify ""`.


## Open — the data seams (M419)

Seven measured seams in what jichi records, designed but not built:
[`proposals/2026-08-observability-seams.md`](proposals/2026-08-observability-seams.md)
carries the numbers, the seven decisions and their rejected alternatives. The rows
here are only what a reader needs to *find* them; the order is the proposal's
cheapest-first order.

| Deferred | Detail |
|---|---|
| **`history.jsonl`: one append-only summary line per turn or run.** | [details](#def-197) |
| **Readers aggregate by default; retention as new `prune` scopes.** | [details](#def-198) |
| **A bounded failure *class* on `tool_call`, and the remaining unread events.** | [details](#def-199) |

<a id="def-197"></a>
#### history.jsonl: one append-only summary line per turn or run.
**Why:** Work without an envelope (interactive, plain `-p` — i.e. most work) leaves nothing a trend can be computed from, and the raw corpus is the thing retention must be free to delete. The shape is already proven three times here (`progress.jsonl`, the `improve` pass-rate history, `runs/`); this gives the whole tool the family member it lacks, so a trend is `tail`. **Deliberately conditional:** it earns its keep only if the trend is consulted — if nobody runs it in a month, delete it rather than polish it. **One measured need arrived at M425 (probe P7), and it is one field.** M86's hollow-gate check warns on `no_tests` (green with zero) or `fewer_tests` (fewer than an earlier green *within the run*). Measured on chrtext: its own `zig build test` gate reported **8 tests passed, exit 0** while the repository holds **1,525 `test " ` blocks and 99 `addTest` steps** -- roughly 0.5% of the suite, with four of those steps aborting on SIGABRT. Eight is neither zero nor a within-run regression, so nothing fired: the check sees the cliff and the slide, and cannot see a gate that was *always* tiny, because it has no idea what normal is for the project. Telling "8 tests" from "8 tests where there used to be 340" needs a **per-project historical test count** -- cross-run memory, which no current sink holds. So the row moves from *no demonstrated need* to **one measured need with a named consumer (`jc_env_verify_sanity`) and a single required field**; that is not the whole roll-up, and it is deliberately not recorded as a reversal. **Verdict, M421: not built, and this row now says why.** The M420 plan promised to *use* the join before deciding, and the use happened: three bounded runs across two projects, read as `runs --output json` indexed by `run` against telemetry grouped by the same key. The cross-sink question the roll-up was meant to answer — behaviour beside outcome, including the `in/call` quotient that diagnosed two budget deaths — came out of thirty lines of scripting over the existing sinks, with **no new file, schema, lint or version**. Two frictions were real and both were fixed in the reader instead: the sinks' differing vocabularies (now a table in [OBSERVABILITY.md](OBSERVABILITY.md)) and a phantom row when both logs shared a directory (M421). **What is still untested is the longitudinal half** — three runs from one afternoon are not a trend, and the "most work leaves nothing" argument stands on its own for un-enveloped runs. **Revisit** when a trend question is actually asked twice and the answer needs journals that retention has deleted; that, not the correlation question, is what would earn the file.
**Where:** [S4 + D2](proposals/2026-08-observability-seams.md)

<a id="def-198"></a>
#### Readers aggregate by default; retention as new prune scopes.
**Why:** `telemetry` reads **one** log (1 of 35 when this was written; **1 of 6 on this machine as of 2026-09-18**, because M599 made the default one appended file per workspace rather than one per run — the row's own narrowing, now visible in the count) while a throwaway measurement script globs the directory — the shipped reader is weaker than the scratch tool. And `prune` covers sessions only: `doctor` sizes the one store that *has* a policy (370 sessions, 21 MB) and none of the ones that do not. The audit log is decided **never** auto-pruned. **Narrowed at M599:** the default log is now one appended file per workspace and every reader opens *that* file first, so for the common case "one log" IS the project's whole history and the aggregation half is moot; what remains is retention, whose urgency M599 raised by turning `metrics` on by default. **Revisit when** a workspace's telemetry file passes ~100 MB, or when `doctor` grows a size check for the directory (not built: a metrics event is a few hundred bytes, and the operator asked for the memory, not for its pruning).
**Where:** [S3/S6 + D3/D5](proposals/2026-08-observability-seams.md)

<a id="def-199"></a>
#### A bounded failure class on tool_call, and the remaining unread events.
**Why:** At `metrics` an offline reader sees *that* a call failed and never *why* — which is what forced the loop detector's classifier into the loop. The exception is deliberate and narrow: a classifier output (`not_found`/`denied`/`bad_args`/`killed`/`nonzero_exit`/`other`), never the message, because `metrics` must stay content-free.
**Where:** [S2/S7 + D6/D7](proposals/2026-08-observability-seams.md)


**Closed at M584 — D6, the unread events.** Eight telemetry event types were emitted on
every run and displayed by no command. All nine unread types (the eight plus `privileged`)
now have a reader line in `jichi telemetry`, and `telemetry_events_lint.sh` check 10 fails
the build when a new event type arrives without one — the guarantee moves from "somebody
remembered" to "the gate says so". **What the measurement said, against the proposal's own
argument:** on the only real corpus available (42,652 events, one workload) just **two** of
the eight had ever fired — `hook` 15 times, `privileged` twice. The other six had never
occurred, because the features behind them are off by default (auto-context), need a
violation (`history_check`) or need hardware (`kinetic`). D6 argued from **emit sites**, not
from **occurrence**; so six of the new readers are unevidenced, and the honest claim is
structural, not empirical. **Still open from D6:** `history_check` is emitted to the journal
as well and `runs` does not read it there; and D7's bounded failure *class* on `tool_call`
is untouched.

**Closed at M583 — D4, the stamping path.** The row that stood here said *"the eight
direct `jc_eventlog_begin` call sites"* and named `kinetic` and `privileged` among them.
Re-measured before the work: **both already went through `telem()`**, and the real set was
**nine call sites in five files** emitting six event types — `prefix_churn`, `hook`,
`retrieve`, `test_edit`, `args_truncated` and four `args_repair` variants. Same class of rot
as M582's decks: a register row citing a count that nothing re-checks. The fix is structural
rather than per-site — one shared `jc_app_telem_begin()` in `src/chat/jc_app.c`, and
`telemetry_events_lint.sh` check 9 fails the build when an app-sourced event reaches
`jc_eventlog_begin` directly. It also made a **documented** claim true: `TELEMETRY.md` had
listed `depth`/`turn`/`run` as *"common fields on every event"* since M420.

## Closed — the changelog's drift is bounded (M431)

| Deferred | Detail |
|---|---|
| **`tests/smoke/changelog_coverage_lint.sh`** | [details](#def-200) |

<a id="def-200"></a>
#### tests/smoke/changelog_coverage_lint.sh
pins CHANGELOG.md's newest named milestone to within **10** of the ROADMAP's newest entry.
**What it does, and what it deliberately does not:** The row this replaces declined the lint for a real reason — *"a changelog entry is a judgement about what is user-visible, and a check demanding one per milestone would be satisfied by a line of noise"* — and named its own revisit condition: **"revisit if the file drifts again despite the note."** That condition was met by measurement, not preference: M402 added the coverage note after 75 milestones of silent drift, and **M431 then shipped with no entry anyway**. So the objection is honoured rather than overruled — the lint bounds *systemic* silence and demands **no** entry for any individual milestone, which means it would **not** have caught M431's own drift of one. That limitation is stated in the driver's header, because a lint that oversells its reach is worse than a loose one. Content is not checked and cannot be. Deliberately **not** git-dependent: a tighter rule could ask whether the newest milestone's commit touched `src/`, but the published snapshot ships a fresh history, so a history-dependent gate would behave differently in the tree people actually acquire. Teeth proven three ways (a simulated 34-milestone drift, a changelog leading the ROADMAP, and a broken extraction shape) — the second of which caught a defect in the lint itself: it reported a *negative* drift as "within the ceiling", a reassuring green over a broken state.
**Where:** [CHANGELOG.md](../CHANGELOG.md)

## Open — RAM tiers and libcs never measured

From M403, which asked the M400 question of [`LOW_MEMORY.md`](LOW_MEMORY.md): which
tiers are measured, and on what. Three grades of evidence are now tagged per tier
(real machine / cgroup ceiling / advice); these are the gaps that remain.

**Three of the four are now closed.** M430 closed the ≤64 MB row and the
minimal-libcurl row on threadwork, which is the access both were waiting for (see
Closed below); **M449 compiled uClibc** — a Bootlin toolchain, 11,593 checks, though
the unit suite only and not a `check-target`, and it says nothing about a ≤64 MB
uClibc box. What remains is the row below, and it is **narrower than it reads**: a
phone has run jichi since M461, so what is genuinely open is *Termux on a phone* —
an on-device toolchain, not the platform.

| Deferred | Detail |
|---|---|
| **Termux on a phone — an on-device toolchain.** | [details](#def-201) |

<a id="def-201"></a>
#### Termux on a phone — an on-device toolchain.
**Why:** **Narrowed at M461, not closed.** The platform half is done: a **Motorola moto g(30)** (Android 12 / SDK 31, arm64-v8a) ran **11,571 unit checks / 0 failures** and all four offline surfaces, so "never run on a phone" no longer holds — see [PLATFORMS.md](PLATFORMS.md). What a phone still adds is **Termux's own libc/filesystem quirks and battery behaviour on a handset**: no on-device toolchain is installed on that phone, so the build-it-on-the-device claim stays **tablet-only** (M459, Lenovo TB336FU). A second, narrower gap sits beside it, and **M507 moved it**: the first smoke driver has now run on a phone (`smoke_lint`, 17/17, exit 0), but the **rest of the tier has not**, and the reason is cost rather than portability — 9m35s for that one driver against 7.8s on the bench, a fork penalty of roughly **27x** once the handset is held awake. **Revisit** opportunistically; the twenty-minute estimate was wrong for the smoke half.
**Where:** [LOW_MEMORY.md](LOW_MEMORY.md#platform-notes)


## Closed — found by driving jichi on zigodot (2026-08-12)

Four defects measured while using jichi to author and work an assignment in another
repository; **all four are closed** (M409 the hint-ladder truncation, M410 the
attempt verdict, M411 the `--model` override, M412 the unprovable gate — see Closed
below). The section was kept one register cycle as the worked example of a
dogfooding run converting into fixes; **retitled at M463**, because the cycle has
passed and an `## Open` heading with no rows is how this page ends up advertising
work that does not exist. The full write-up, with the fixtures and the token numbers,
is `zigodot/docs/analysis/2026-08-12-driving-jichi-on-zigodot.md`.

## Open — found by the second zigodot campaign (2026-08-13)

| Deferred | Detail |
|---|---|
| **A hang before the first tool boundary is outside every envelope budget.** | [details](#def-202) |

<a id="def-202"></a>
#### A hang before the first tool boundary is outside every envelope…
*(re-scoped M503: M438 closed the reporting half -- the journal's `start` now lands before the first network touch and the control socket answers earlier, so a supervisor is no longer staring at a 0-byte file. What remains is the BOUNDING half: the deadline should cap the PROCESS, not just the loop.)*
**Why:** Measured: an `--auto` run with `--deadline 12m`, `--journal`, and configured `timeouts` (connect 20 / stall 120 / request 900) hung for **22 minutes producing a 0-byte journal** — not even the `start` event — and 121 bytes of stderr (the M411 pin note). Every liveness mechanism jichi has assumes the loop is running: the deadline is enforced at tool boundaries, `--heartbeat` is jsonl-only and fires from the model-call progress callback, and the journal opens with the run. A pre-loop hang (this one was somewhere between arg processing and the first request — the pin note printed, nothing after) is invisible and unbounded. A supervisor's only tell is silence, indistinguishable from a long model call. **What to build:** journal `start` (or a stderr line) *before* the first network touch, and a wall-clock backstop that covers the pre-loop phase — the deadline the user asked for should bound the *process*, not just the loop. **Not diagnosed to root cause** (one occurrence, killed gracefully after announcement; a second concurrent jichi held the same workspace/HOME, so shadow-repo or session-store contention is the suspect).
**Where:** [AUTONOMY.md](AUTONOMY.md)


## Closed at M470 — the two improvements the architecture sweep asked for (M469)

| Deferred | Detail |
|---|---|
| **`tier-v-arch.sh` should snapshot the tree ONCE per sweep, not once per target.** | [details](#def-203) |
| **`tier-v-arch.sh` should preflight `pipe()`/`select()`/fork+exec per target and say "the environment cannot run the subprocess tests".** | [details](#def-204) |

<a id="def-203"></a>
#### tier-v-arch.sh should snapshot the tree ONCE per sweep, not once per…
**Why:** `jc_rig_ship_tar` runs inside the per-target loop, so a commit landing mid-sweep gives later rows a different tree than earlier ones and the table stops being comparable — silently, since every row still prints a check count. It is why M469's commit had to wait for a 21-target sweep to finish before anything could be committed, which is a real workflow cost on a rig meant to run unattended. **Fix:** ship once to `$DIR/src`, then copy per target.
**Where:** `scripts/tier-v-arch.sh`

<a id="def-204"></a>
#### tier-v-arch.sh should preflight pipe()/select()/fork+exec per target and say "the environment cannot…
**Why:** The six MIPS rows reported *"unit suite RAN and reported failures: 11,627 checks, 73 failures"*, which reads as an accusation against jichi. The cause is one level below it: `pipe()` fails under `qemu-mips` with zig's musl (MIPS is the one Linux architecture whose `pipe` syscall returns both descriptors in registers), and all 73 failures are downstream — the seven failing files are exactly those that spawn a subprocess or read `/proc`. A row that blames the program for its emulator's gap is worse than no row, and `tier-v-openbsd.sh` already has the pattern to copy (its procfs check). **Also worth recording in the same pass:** armeb cannot print a `double` (the *literal* `0.2` renders as `-2.35344e-185` while its stored bytes are correct big-endian IEEE-754), so that target is unusable for measurement through no fault of jichi's; and `x86_64-linux-muslx32` builds clean but cannot execute because this kernel lacks `CONFIG_X86_X32`.
**Where:** `scripts/tier-v-arch.sh`, [PLATFORMS.md](PLATFORMS.md)


> **Both closed at M470, and the second found more than it was filed for.** The rig
> snapshots once to `$DIR/src` and copies per target, so a mid-sweep commit can no longer
> make the table non-comparable. `scripts/envprobe.c` runs per target before the suite and
> the MIPS row now reads
> *"11,638 checks, 73 failures — env probe says [pipe=FAIL select=FAIL forkexec=ok
> procfs=present]: the EMULATOR cannot run the subprocess tests"*, classified `no-run`
> instead of accusing jichi. The probe independently confirmed the manual diagnosis
> (`pipe` and `select` fail, `fork`+`exec` does not) and it only ever **downgrades** an
> accusation — a passing suite reads the same whatever the probe said. It also reports
> `procfs=present|absent`, because *present but different* is the illumos hazard
> [`PLATFORMS.md`](PLATFORMS.md) now names.

## Closed by measurement — parallel_abort does NOT reproduce on Guix (M458; re-scoped M466, corrected M467, CLOSED M468)

> **The premise of this row changed on 2026-08-17.** It was filed as isolated to Guix
> System — the one platform whose row cannot rebuild itself — so the next step was
> believed to be building `scripts/tier-v-guix.sh`. The OpenBSD row, once
> `JC_SMOKE_KEEP_GOING=1` let it run to the end, reports **`parallel_abort`: "parent did
> not exit within 15s of SIGINT — abort/reaping deadlocked"** — word for word the Guix
> failure — and it fails standalone there too. **So it reproduces on a platform with an
> unattended rig** (`scripts/tier-v-openbsd.sh`, an 11 MB ISO and one command), and
> Guix is no longer on the critical path. Iterate with
> `sh tests/smoke/run.sh parallel_abort` in that guest. The three suspects named below
> — all landing after the Guix measurement and none re-measured — are now testable
> without Guix at all.
>
> This also means the row was never really about Guix: it is about a **non-glibc,
> non-Linux** reaping path, and the two platforms share that rather than sharing
> Guix's non-FHS layout. The M450 process-group explanation is now doubly incomplete.
>
> **CORRECTION (M467), and it retracts the paragraph above.** OpenBSD's identical
> failure turned out to be a defect in **jichi's own test harness**, not in the agent:
> `parallel_abort.sh` backgrounded a subshell (`( cd "$ws" && "$BIN" ... ) &`) and
> captured `$!`, and on OpenBSD ksh `$!` names **the subshell**, not the command —
> measured, against dash and bash, which both perform an implicit exec and name the
> command. So the driver SIGINT-ed a shell, jichi never received the signal, and the
> check reported *"abort/reaping deadlocked"* about an agent that was never asked to
> abort. With `exec` added it passes.
>
> **Therefore the "both platforms share a cause" claim written at M466 is withdrawn.**
> Guix's `/bin/sh` is **bash**, which does perform that implicit exec, so this
> explanation does **not** transfer. Guix's instance is **still open and still
> unexplained** — but it is now cheap to settle rather than blocked: the `exec` fix is
> shipped, so one re-measure (`sh tests/smoke/run.sh parallel_abort` in a Guix guest)
> distinguishes "the same harness bug after all" from "a real defect in the
> `spawn_parallel` reaping path". Until that runs, neither claim is earned — and the
> earlier framing was exactly the over-reach this register exists to prevent.
>
> **CLOSED at M468, by measuring it.** Guix System 1.5.0 (`guix describe` d58da8a,
> kernel 6.17.12-gnu, `/bin/sh` = bash 5.2.37, **no `cc` and no `c99`** — M458's lesson
> measured rather than recalled), built at HEAD `f025185` with `CC=gcc` inside
> `guix shell`: `-Werror` C89 build **clean**, **11,594 unit checks / 0 failures**, and
>
> ```
> --- smoke: parallel_abort
> ok 1 - SIGINT-ed parent exited (reaped both stalled children)
> ok 2 - exit was prompt (1s) -- the abort path, not the 120s watchdog
> ```
>
> plus `parallel_hang` 2/2, `signals` 4/4 and `stop_reason_capped` 5/5 (the last fails on
> OpenBSD, confirming *that* one as ksh-specific). The likely fix is one of the three
> commits this row already named — `448616d` is the best structural fit, since a child
> spinning on `EPIPE` instead of dying is exactly a parent that never leaves `waitpid`.
>
> **Caveat, so the closure is not stronger than the evidence:** this is not a
> byte-identical re-run of M458's row (that was a Guix System from the recorded config;
> this is `guix shell` in the published desktop image, 6 vCPUs / 8 GB). The defensible
> claim is *does not reproduce at HEAD*, not *was never real*. And **I predicted in
> writing that it would still fail** — see
> [`analysis/2026-08-17-driving-the-published-guix-image.md`](analysis/2026-08-17-driving-the-published-guix-image.md).
>
> **The Guix image itself, same day:** the operator downloaded
> `guix-system-vm-image-1.5.0` and its source tarball. GRUB in that image turns out to
> read *and* write serial — so M450's "cannot be driven" is wrong at the bootloader
> level, and the boot entry (kernel, initrd, root UUID, args) is now extracted and
> recorded. What resists is GRUB's serial **input** reliability across four attempts;
> the decisive error was `unspecified search type`, i.e. GRUB parsing a bare `search`
> whose search-type argument was dropped in transit. Three ranked next moves — QEMU monitor
> `sendkey`, a direct `-kernel`/`-initrd` boot (the `tier-v-tiny.sh` pattern, needing
> the two files copied out via the existing Debian rig rather than root), or building
> the headless image from the now-parameterised `.scm` — are in
> [`analysis/2026-08-17-driving-the-published-guix-image.md`](analysis/2026-08-17-driving-the-published-guix-image.md).
> **The one step that needs a human** is five minutes at that image's graphical console
> to enable sshd; everything after it is automatable.
>
> **Superseded at M738 (2026-09-24): no step needs a human.** `scripts/tier-v-guix.sh`
> edits the entry over the serial line under `-nographic`: `e`, three Downs (the arrow
> key's escape sequence, 300 ms apart, where attempts 2-3 used `Ctrl-N`), `Ctrl-E`,
> ` console=ttyS0`, `Ctrl-X` -- fourteen characters typed, where attempt 4 typed a whole
> command line and lost a token. Guix starts a login on the console the kernel names.
> Three rig runs, three clean edits; which of those differences mattered is not
> isolated. The row is **Driven** (`TIER-G-B31F57`), and no sshd was ever enabled: the
> rig talks to the serial console with pexpect.

| Deferred | Detail |
|---|---|
| **Diagnose why `tests/smoke/parallel_abort.sh` deadlocks on Guix System.** | [details](#def-205) |

<a id="def-205"></a>
#### Diagnose why tests/smoke/parallel_abort.sh deadlocks on Guix System.
**Why:** After SIGINT the parent never exits; the driver reports *"abort/reaping deadlocked"*. **Two explanations were ruled out before filing:** it is **not a timeout artefact** (identical failure at `JC_SMOKE_TIMEOUT_MULT` 1 and 6, so a slower machine is not the cause), and it is **not the absent process groups** that explained the `guix shell -C` failure at M450 — `/proc/self/stat` shows a normal non-zero `pgrp` on the real system. The unit suite is green there (11,599 / 0), so it is isolated to the `spawn_parallel` abort/reaping path in `jc_parallel.c` / `jc_bg.c`, which uses `setpgid` + `kill(-pid, ...)`. **This also corrects M450**, whose process-group explanation was sufficient for the container and is now known to be incomplete. **Not diagnosed in the session that found it** because the row it belonged to was otherwise complete and a signal/reaping deadlock deserves its own sitting rather than the tail of a long one. **Revisit** with the Guix image still buildable from the recorded config — it reproduces in one command. **RE-SCOPED at M466: re-measure before debugging, and three named suspects.** The deadlock was measured at `8f1d4b6` (2026-08-15), and **34 commits later three fixes landed in its exact code path**, none of which has been re-measured on Guix: `448616d` — every child inherited jichi's *ignored* SIGPIPE, so a pipeline producer spins on `EPIPE` instead of dying when its consumer exits (the textbook shape of a parent stuck in `waitpid` on a child that will not die); `0d5b030` — a timed-out capture killed only its direct child and **orphaned the rest of the pipeline**; and `ac166d5` — the smoke harness reaped only the *last* mock a driver started, whose symptom on FreeBSD was a **2.19-second driver reported as a timeout 118 seconds before any deadline, with all five of its checks passing**. That last one is the closest match to the observed report, though its FreeBSD mechanism (a `timeout(1)` that waits for the whole process group) should not apply under Guix's GNU coreutils — which is exactly why it needs measuring rather than reasoning about. **Two blockers removed at M466:** the machine definition no longer hardcodes one host's ssh key path (it reads `JICHI_BENCH_PUBKEY` and refuses when unset), and the re-measure now costs **one boot** rather than one per defect — `JC_SMOKE_KEEP_GOING=1` reports the whole failure set, and `sh tests/smoke/run.sh parallel_abort` runs just this driver. **Still blocked on Guix itself:** `guix` is not installed on the threadwork bench and building a Guix System image needs a working daemon and `/gnu/store`, so this row belongs to whichever machine has Guix. `scripts/tier-v-guix.sh` is deliberately **not** written blind — an untested rig for a platform nobody can run here would be a fourth never-executed artifact, and the honest first step is a re-measure that needs Guix regardless.
**Where:** ROADMAP M458/M466, [LOW_MEMORY.md](LOW_MEMORY.md)


## Closed by measurement — the /tmp assumption and the crash class (M457)

Both rows below were filed at M453 with the reasons they were not done that day. They were
done at M457, in the order the plan's own *"what would make this plan wrong"* section
specified: the crash class first, because the audit found it wide (19 sites on a narrow
pattern, 43 on a broader one) and it is valuable on every platform rather than only on
`/tmp`-less ones.

| Deferred | Detail |
|---|---|
| **Route the unit suite's fixture paths through `TMPDIR`.** | [details](#def-206) |
| **Audit for a recorded null check followed by a dereference.** | [details](#def-207) |

<a id="def-206"></a>
#### Route the unit suite's fixture paths through TMPDIR.
**What it measured:** 158 literal paths converted, plus `jc_test_tmpdir()` for the six directory uses. The idiom was **copied from `_smoke.sh`'s `smoke_tmp`**, not invented. Verification is the **check count, not the colour**: 11,599 / 0 with `TMPDIR` unset, `/tmp`, short, and ~100 chars. That mattered — `test_git.c` built its repo through `system("mkdir -p /tmp/...")` while the C side used the helper, so with any other `TMPDIR` the fixture landed outside the repo and **ten checks silently did not run** while everything stayed green. Three other classes needed reading rather than regex: string concatenation, a non-literal format argument, and expectations whose matching input is literal JSON fixture data.
**Where:** ROADMAP M457

<a id="def-207"></a>
#### Audit for a recorded null check followed by a dereference.
**What it measured:** Two harness verbs added, because the harness had no way to express "check, then branch": `JC_REQUIRE` (records and yields) and `JC_VEC_STR` (fetch-or-NULL). Same gap as M450's `t_skip_one`. **Shown to work on the machine that exposed it:** the Android 4.4 tablet went from *aborting at the 4th of 123 test files* to running to completion — 11,534 checks, 47 failures, zero crashes. The 47 are themselves a finding: they cluster on every test that forks or shells out, because **Android has no `/bin/sh`**, which musl's `system()` invokes.
**Where:** ROADMAP M457, [TEST_INTEGRITY.md](TEST_INTEGRITY.md)


## Closed by measurement — A5, line breaking for German compounds (M579)

**The item as scoped:** *"UAX #14 line breaking, so a German compound does not split
mid-word."* Raised at M554 when the chrome-width budget was first measured, and made to look
urgent by M568 shipping German chrome for real.

**Both halves of its premise turned out to be false, and both were cheap to check.**

**jichi does not wrap output.** `term_cols` (TIOCGWINSZ, then `$COLUMNS`, then 80) appears in
exactly one file — `src/tui/jc_term.c` — and only for cursor arithmetic while the user is
*typing*. No renderer takes a width; `jc_mdrender` has no width parameter at all. Output is
emitted as-is and **the terminal** wraps it, which `jc_tui.c`'s own comment says plainly: *"the
renderer emits them, the terminal wraps on them."* So there is no break-point decision in
jichi's code for UAX #14 to improve.

**And nothing is wide enough to wrap.** Measured by `tests/test_width.c`, widest non-exempt
chrome line per language:

| Deferred | Detail |
|---|---|
| 75 | [details](#def-208) |

<a id="def-208"></a>
#### 75
**de:** **76** | 59 | 64
**zh:** 52


against a budget of 78 — 80 columns minus a two-space indent. **At a standard terminal no chrome
line wraps in any language**, German included, and the budget exists precisely to keep that true:
a translation that would wrap fails the build the day it lands.

**What is genuinely left is a different question, and it needs a measurement I cannot make.**
Should jichi *pre-wrap* its chrome at word boundaries, so that a terminal narrower than ~78
columns breaks between words rather than mid-compound? That is a new capability rather than a
fix, and its value depends entirely on whether a hard mid-word wrap actually harms a listener —
which depends on the reader, the emulator, and how continuation lines are marked. **Nobody here
has heard it.** Building UAX #14 against an unmeasured harm, in a code path that does not exist
yet, would be the shape this project has already paid for twice this month.

**Reopen it when:** somebody reports that a wrapped chrome line reads badly at a narrow width.
Then the work is *pre-wrapping chrome*, and UAX #14 is one possible implementation of it.

## Closed by measurement — uClibc, on the toolchain it was waiting for (M449)

The row said it was blocked on a toolchain and named the unblocking condition: *"Revisit
if a buildroot/OpenWrt toolchain becomes available."* Bootlin publishes buildroot-built
toolchains, so the condition was already met and nobody had checked — the M326b failure
in its mildest form, since the reason was true when written and quietly expired.

| Deferred | Detail |
|---|---|
| **Compile jichi against uClibc.** | [details](#def-209) |

<a id="def-209"></a>
#### Compile jichi against uClibc.
**What it measured:** Bootlin `x86-64--uclibc--stable-2025.08-1` (gcc 14.3.0, uClibc-ng), `HAVE_CURL=` + `-static`: **zero diagnostics** under the project's mandatory `-std=c89 -pedantic -Wall -Wextra -Werror`, **11,593 unit checks / 0 failures** run natively, all offline surfaces OK, `--version` peak RSS **384 KB** with 0 shared libraries. The row predicted the risk was "genuinely lower than the Darwin case" *because* the one libc-dependent feature is probe-gated rather than `#ifdef`-gated — and that prediction was **half right in an instructive way**. There was indeed no rotted `#ifdef` branch. But the probe itself was wrong: it omitted the build's warning flags, so it answered "yes" for a `malloc_trim` that uClibc-ng declares only under `__USE_GNU` while still exporting the symbol, and **every translation unit then failed under `-Werror`**. Probe-gating was the right design and was not, by itself, sufficient.
**Where:** [PLATFORMS.md](PLATFORMS.md#libc-and-ram-tiers), ROADMAP M449


## Closed by measurement — two RAM rows, on the machine they were waiting for (M430)

Both rows above said, in the text, that they were blocked on access to one machine
rather than on a design question. That access arrived; here is what the sitting
produced. Kept rather than deleted because each row's *reason* contained a prediction,
and one of them was wrong in an instructive direction.

| Deferred | Detail |
|---|---|
| **Run jichi on a machine with ≤64 MB of physical RAM.** | [details](#def-210) |
| **Build a minimal single-TLS-backend libcurl and link it into a static musl jichi.** | [details](#def-211) |

<a id="def-210"></a>
#### Run jichi on a machine with ≤64 MB of physical RAM.
**What it measured:** On a kernel + busybox initramfs guest: offline surfaces at **64 MB**, a **verified model turn at 80 MB**, jichi's own peak **1,160 / 1,396 KB**. The row predicted the tier "is not in doubt on jichi's side" and that was right — but it framed the open question as *the three things a real board pays*, and the actual binding constraint turned out to be **the kernel**: swapping Debian's generic kernel for Alpine's `virt` moved the floor 96 → 64 MB with jichi byte-identical. Also measured, and not predicted by anyone: a stock Debian 12 cloud image **cannot boot at 128 MB at all**, so the `~128 MB` tier was never gradeable with a distro. Still a VM, not a board.
**Where:** [LOW_MEMORY.md](LOW_MEMORY.md), [analysis](analysis/2026-08-13-ram-tiers-whole-machine.md)

<a id="def-211"></a>
#### Build a minimal single-TLS-backend libcurl and link it into a static…
**What it measured:** **804 KB** `--version` RSS, 0 shared libraries, and it **makes a real model call** — against the system libcurl's 10,012 KB. M403's honest bound ("between 0.5 and 8.6 MB") collapses to the bottom of its own range. The row's estimate of the work was accurate ("an afternoon of libcurl `configure` flags"); what it did not anticipate is that the four blockers were all *toolchain* facts rather than flags — `zig` as a multi-call binary breaking `CMAKE_AR`, mbedTLS's `-Werror` against a newer clang, `zig cc` linking UBSan by default, and a mock deleted mid-test. Uses **mbedTLS**, not the OpenSSL the recipe names, so it is a sibling claim rather than the same number.
**Where:** [LOW_MEMORY.md](LOW_MEMORY.md#build-time-footprint-reduction)


## Closed by measurement — two recommendations withdrawn (M406/M407)

Kept because a withdrawn recommendation that leaves no trace gets re-proposed. Both
were raised by me after M405, both on **ad-hoc greps whose extraction was wrong** —
the same blindness the project's lints are written to avoid, met four times in one
day from my own probes.

| Deferred | Detail |
|---|---|
| **"9 of 12 curriculum modules have no link forward" — add next-links.** | [details](#def-212) |
| **"Nothing ever runs a documented command" — build an executable doc-command tier.** | [details](#def-213) |

<a id="def-212"></a>
#### "9 of 12 curriculum modules have no link forward" — add…
**Why it was withdrawn:** **All twelve already carry** `[◀ Prev] · [▲ Curriculum map] · [Next ▶]`. My probe grepped for `Next:` and `→` and missed the actual `[Next ▶]` form. Acting on it would have "fixed" a unanimous convention into duplication. Residue: the invariant is now pinned by `docs_locators_lint.sh` check 6, whose comment records this.

<a id="def-213"></a>
#### "Nothing ever runs a documented command" — build an executable doc-command…
**Why it was withdrawn:** **`subcommands_lint.sh` checks 7–11 (M326e) already run every advertised subcommand bare**, in a throwaway workspace, with exclusions named and check 11 guarding stale exclusions. I had read the *static* drivers (`doc_commands_lint`, `docs_flags`, `subcommands_lint` checks 1–6) and generalised from them. A separate driver was written and **deleted** rather than shipped: for six forms it would have been a duplicate wearing a new name. Residue: those six *flag-carrying* forms were genuinely uncovered — the strip between "the verb exists" and "the bare verb runs" — and are now executed by `doc_commands_lint.sh` check 5, which asserts they are not **rejected** (exit 2), the precise M375 signature. Its reach is stated in the driver: six forms, four of which legitimately exit 1 on an empty fixture HOME.


**The transferable lesson**, recorded because it cost four probes in one session: *a
number produced by an ad-hoc grep is not a measurement.* Every one of these
extractions matched something real and missed the thing it was looking for — the
comment quoting its own subject, the wrong output marker, the wrong link form, the
wrong error signature. The rule that survives: before recommending work on the
strength of a count, either derive the count from ground truth the code owns, or
verify one instance by hand.

## Closed during this program

Kept briefly, because "we said we'd do this" is worth being able to check.

| Deferred | Detail |
|---|---|
| **A `--max-tool-calls` recommendation shaped by the WORK, not one number** | [details](#def-214) |
| A Rust section in `BIBLIOGRAPHY.md` | [details](#def-215) |
| A file-I/O task in the C systems course | [details](#def-216) |
| `run` on telemetry + `ws` on the journal's `start` (the S1 join) | [details](#def-217) |
| `verify_stuck` + `test_assertion_edit` in `runs` NOTES | [details](#def-218) |
| Nothing checks that an authored assignment's gate can fail | [details](#def-219) |
| An explicit `--model` reported active, then overridden by config routing | [details](#def-220) |
| A colon in a `hints:` value silently deleting the whole ladder | [details](#def-221) |
| `attempt` reporting PASS over ten goalpost warnings, then deleting the evidence | [details](#def-222) |
| **Tell the model when its own edit moved the goalpost** | [details](#def-223) |
| **Stop advertising tools a depth gate will refuse** | [details](#def-224) |
| **One structured report shape for both delegation tools** | [details](#def-225) |
| **Arm liveness before the first tool boundary** | [details](#def-226) |
| **Cut the jsonl `tool_result.preview` on a UTF-8 boundary** | [details](#def-227) |
| **Tell the model the price list when the numbers are measured** | [details](#def-228) |
| **Teach `mockmodel` to emit a `usage` block on the chat path** | [details](#def-229) |
| **A tool-call id in the jsonl stream** | [details](#def-230) |
| **A `degraded` flag on the terminal object** | [details](#def-231) |
| **`jichi sysmsg` omits every envelope-gated prompt section** | [details](#def-232) |
| **`tests/smoke/mincurl_recipe_lint.sh` is promised by a script and does not exist** | [details](#def-233) |
| `docs/MCP.md`, the one entirely missing feature page | [details](#def-234) |
| STATE-THE-REACH in the system prompt + doctor | [details](#def-235) |
| Skip the lossy mid-turn pass once it is exhausted | [details](#def-236) |
| The CLI `undo` leaving saved sessions believing the pre-undo state | [details](#def-237) |
| The DECLARE-THE-GATE repetition (GATE_INTEGRITY §8's n=1 caveat) | [details](#def-238) |
| The wizard's 17-option first screen, two questions under one list | [details](#def-239) |
| The `context` subcommand reporting `rules ~0` | [details](#def-240) |
| The system prompt's unnamed remainder | [details](#def-241) |
| Per-tool definition sizes | [details](#def-242) |
| Telemetry joined to the tool registry | [details](#def-243) |
| The history as one number | [details](#def-244) |
| Flipping `attempt`'s default to `core` | [details](#def-245) |
| `context tools` under-reporting the live toolset | [details](#def-246) |
| `spawn_parallel` at 4/10 | [details](#def-247) |
| The `glob` capability gap | [details](#def-248) |
| Measuring a `core` attempt that needs a hint | [details](#def-249) |
| Whether the craft section earns its tokens | [details](#def-250) |
| A TUI `/context tools` / `/context history` | [details](#def-251) |
| A `doctor` check for never-called tools | [details](#def-252) |
| German editions of the plain-register assignments | [details](#def-253) |

<a id="def-214"></a>
#### A --max-tool-calls recommendation shaped by the WORK, not one number
**Closed by:** **M595** — and the shape hypothesis was WRONG. Six driven runs across two models and two task kinds: the documentation run ended on the *call* cap and the mechanical sweep on *tokens*, the opposite of the proposed advice. What fits every run is arithmetic rather than shape — with budget `B`, cap `C` and `t` tokens per call, tokens bind when `B / t < C`, and `t` is a property of the backend (21–35k per call cacheless, against 92% cache on one HRZ session). In [AUTONOMY.md](AUTONOMY.md) §1

<a id="def-215"></a>
#### A Rust section in BIBLIOGRAPHY.md
**Closed by:** **M636c** — 9 entries, 7 free, weighted toward ownership and the FFI seam, because Rust was the only language in the matrix with a graded course and zero literature. The intended cost was paid as designed: `bibliography_lint` check 5 went red until every count was redone, and the check itself was wrong on its first run (no terminator for the last section)

<a id="def-216"></a>
#### A file-I/O task in the C systems course
**Closed by:** **M636g + M636h** — tasks 76 and 77 of a new sibling course, "C: files & structures": open a file that may not exist, survive a short read, refuse one over the cap; then replace one atomically via temp + `rename`, `0600` at creation, the temp removed on every failure. It was the one gap in the M636 review that was **not** defensible as a scope decision — 0 of 79 graded tasks against 26 files of `src/` that do it

<a id="def-217"></a>
#### run on telemetry + ws on the journal's start (the S1…
**Closed by:** **M420** — two conditional fields, envelope struct untouched. Proven by performing the join rather than asserting presence: `telemetry_join.sh` compares the journal's `run` against telemetry's and requires **every** event to carry it (partial stamping would join some behaviour and silently drop the rest). Teeth: the perturbed binary reports `journal run='…' vs telemetry run=''`, the exact pre-M420 state

<a id="def-218"></a>
#### verify_stuck + test_assertion_edit in runs NOTES
**Closed by:** **M420** — `stuck=N` and `goalposts=N`, plus the `ws` column the journal now supplies; printed only when non-zero, because an always-present `goalposts=0` trains a reader to skip the column the one time it matters. The plan called this "two counters" and it was three: a `ws` the reader could not read would have been written for nobody

<a id="def-219"></a>
#### Nothing checks that an authored assignment's gate can fail
**Closed by:** **M412** — `grade --expect-fail`, the smaller option the row recommended: exit 0 iff the verify fails on the untouched tree, HOLLOW (exit 1) when it is already green, `--record` refused. A command composes with every authoring path (scaffold, hand-written, a model that ignored its brief); prompt text binds only the model that reads it

<a id="def-220"></a>
#### An explicit --model reported active, then overridden by config routing
**Closed by:** **M411** — `--model` pins the run (routing disabled, one-line note, `-q`-respecting); `--route-fast`/`--route-strong` alongside it keeps routing. CLI-only on purpose: the TUI user sees the `[route]` banner and has `/route off`, which is exactly the visibility the headless `-q` user lacked

<a id="def-221"></a>
#### A colon in a hints: value silently deleting the whole ladder
**Closed by:** **M409** — deferred for about six hours: the row recommended "the lint first, do not touch `jc_yaml` under time pressure", and the driver was indeed built first — born red, it measured **64 of 80** shipped ladders short, far past the sampled five. The parser fix then turned out to be eleven contained lines (`quoted_whole_line` before `find_colon`, quoted keys still mapping), every ladder recovered with no prose changed, and the M289 skip now counts what it drops so `hint` can say so

<a id="def-222"></a>
#### attempt reporting PASS over ten goalpost warnings, then deleting the evidence
**Closed by:** **M410** — all three separable pieces from the row: the counter (`test_edits` on the envelope), the verdict (**TAINTED**, exit 1, via a pure unit-tested word function), and `--keep-worktree`. The "0 hints used" oddity resolved itself: the ladder was unreadable (M409), so the learner *had* no hints to use

<a id="def-223"></a>
#### Tell the model when its own edit moved the goalpost
**Closed by:** **M435** — one sentence appended to `edit_file`'s and `apply_patch`'s own results, escalating with a count. The row's rejection held: not a refusal, because correcting a genuinely wrong test is fair work and a refusal is routable into `sed`. What the row did not anticipate was the precondition — the two tools each carried a near-identical five-part M88 block, so a sixth destination meant factoring them into `tu_report_test_edit` first. Two of my own bugs were caught by the new tests: `"the 2th test assertion"` from a hand-built ordinal (the same bug `jc_toolloop_render` fixed three milestones earlier), and a fixture that failed all six checks for a missing `read_file` rather than for the property under test

<a id="def-224"></a>
#### Stop advertising tools a depth gate will refuse
**Closed by:** **M436** — the row's rejection held (the gate is right, the advertisement was the bug) and the fix went one step further than the row asked: the fact is now ONE field on `struct jc_tool`, read by the builder for the omission and by `jc_tool_execute` for the backstop, and the three hand-written `agent_depth > 0` checks in the tool bodies are deleted. Stating it three times and reading it in none of the places that build the advertisement is how the two halves could disagree at all. Cost: 40 definitions name the new field, because `-Wextra` does not let C89's implicit zeroing of a trailing member pass silently — and naming it everywhere is the better outcome. A name table plus a lint was rejected: with the gates gone the table would be its own ground truth

<a id="def-225"></a>
#### One structured report shape for both delegation tools
**Closed by:** **M437** — the shape shipped with both named defects fixed: the grandchild poison (one save/restore of the per-run slots around every `jc_tool_execute`, restoring rather than zeroing so this run's own earlier failure survives) and the capped parallel child (its stop reason now travels the pipe as a NAME, so an enum renumbering cannot silently reinterpret it). Two things the row did not anticipate. A **policy block never reaches the loop's `is_error` branch**, so the denial case -- the single most valuable one -- bypassed the only recording site; `block_message` is the chokepoint for both block sites and now records there, classified as DENIED directly rather than inferred from wording. And `files_changed[]` shipped for a `spawn_parallel` write child ONLY: a worktree gives a per-delegate baseline, a nested in-process run has no equivalent, and manufacturing one would mean a shadow checkpoint per delegation appearing in the user's `/undo` stack. The row's rejection of forwarding the transcript held

<a id="def-226"></a>
#### Arm liveness before the first tool boundary
**Closed by:** **M438** — all three parts: an `open` journal record at file-creation time (with the pid, so a supervisor can check the process rather than infer life from file growth), a full control boundary before the first request, and a poll-only one BETWEEN the tool calls of a round. The poll is deliberately not the full boundary: that folds steering into history, and a user message between two tool results of one round is malformed under M364's contract. Writing the test taught more than the fix did — the first cut asserted that `status` ANSWERED and passed with the fix reverted, because the client waits 300s; the property is latency, and measured over a 12s round it reads 1s with the fix and 7s without. One limit stands and is stated: the socket is still not served DURING a model call, since polling from libcurl's progress callback would let a `pause` block the HTTP read

<a id="def-227"></a>
#### Cut the jsonl tool_result.preview on a UTF-8 boundary
**Closed by:** **M439** — fixed with the in-tree helper the row named, behind a pure `jc_agentjson_preview` in the module that owns the jsonl schema. The row understated the frequency: 512 is not a multiple of 3, so splitting a character was the NORMAL outcome for non-ASCII output long enough to truncate, not an edge case. It also missed a second site -- `copy_trunc` in `jc_cli.c`, backing `jc_tool_arg_summary`, which reaches stderr and the telemetry `args` field. Two of my own assertions were inverted before the driver was right: "the last byte is not a continuation byte" is FALSE for every valid string ending in a multi-byte character, and a length-modulo test ignored `read_file`'s 7-byte ASCII gutter

<a id="def-228"></a>
#### Tell the model the price list when the numbers are measured
**Closed by:** **M440** — a `# Cost model` section with the effective caps and §6 items 5-8, gated tri-state on the cache verdict exactly as the row asked. Two things the row did not anticipate. The gate had to read the CONFIGURED cache setting, not the observed hit-rate: a running statistic in the system prompt changes the cached prefix every turn and destroys the caching it describes, so the section's signature takes five integers and no `jc_app` -- the hazard is structurally impossible, not merely discouraged, and the remaining gap (a backend that accepts a caching request and returns nothing) is what the explicit flag is for. And the four built-in caps had to move from four `#define`s in four tools into `include/jc_toolcaps.h` first, since a second reader of a number that must agree is the drift M296 forbids; `tool_caps_lint` failed loudly on the move and now reads one file instead of four

<a id="def-229"></a>
#### Teach mockmodel to emit a usage block on the chat path
**Closed by:** **M441** — the tool path now sends usage in a separate final chunk with an empty `choices`, the shape a real provider uses. The row's fear of a blast radius was right in kind and small in size: ONE driver moved. `learn_on_stop_cost` ran on `--budget-tokens 1`, which only sufficed because a tool call cost nothing; it now ends budget_exhausted at 25 tokens, and the fix is a bigger budget plus a comment saying why it is not 1. The payoff was taken at once — `budget_panel.sh` gained the RATE check the row was written to unblock, proven two-sided against the old rig, which reads `0/400000 tokens (0%)` with no rate at all

<a id="def-230"></a>
#### A tool-call id in the jsonl stream
**Closed by:** **M442** — the provider's own id on both `tool_call` and `tool_result`. "Small and additive" understated one thing: it is a shared callback signature, so all four front-ends had to change, and three of them deliberately IGNORE the id — the TUI (a human reads lines in order), ACP (it mints its own `toolCallId` an editor already pairs by, and swapping it would change bytes on a live wire), and the fork pool (a board and an aggregate, never a paired timeline). Each says so at the parameter. The driver pairs two calls to the SAME tool, since two different tools can be paired by name and would have passed with no id at all

<a id="def-231"></a>
#### A degraded flag on the terminal object
**Closed by:** **M443** — an object rather than a boolean, counting `unanswered` / `approval_unavailable` / `privilege_refused`, and present only when one is non-zero so a supervisor tests for the key (the M420 non-zero-gate argument). The row listed `confirm_tool` NULL ⇒ allow among the degradations; that case is an `--auto` run, and it is deliberately NOT counted — auto-approval is the operator's instruction, not a decision taken in their absence, and counting it would make every `--auto` run degraded and the flag worthless. `stop_reason` is untouched: the flag reports, it does not judge

<a id="def-232"></a>
#### jichi sysmsg omits every envelope-gated prompt section
**Closed by:** **M444** — and `context` with it, which the row did not name but which mattered more: its whole job is to size the prompt a run sends. The row said the fix was "a dispatch-order change with a blast radius worth measuring"; measuring it showed moving the dispatch was the WRONG fix, because MCP servers are connected in between and a read-only prompt dump would have begun spawning subprocesses. The arming was split from the journal instead: `envelope_arm` takes the path as a parameter, an introspection command passes NULL, and the driver requires the runs directory to stay empty. Measured effect on `context`: the environment slot goes from ~25 to ~136 tokens

<a id="def-233"></a>
#### tests/smoke/mincurl_recipe_lint.sh is promised by a script and does not exist
**Closed by:** **M445** — it exists, and the script's claim is a fact again. Built with the caution the row asked for: only `--disable-*`/`--without-*` tokens, from BOUNDED regions (the doc's fenced block, the script's single `set --` list), floors of 15 on each side, and curl's own prefix, cross-host and TLS-backend options excluded because they legitimately differ between a page teaching one build and a script parameterised over three rungs. Proven three ways — a flag dropped from the script, one dropped from the page, and a reshaped invocation that trips the floor at 0 instead of comparing two empty sets

<a id="def-234"></a>
#### docs/MCP.md, the one entirely missing feature page
**Closed by:** **M395** — written from the source rather than from memory, which caught three of my own wrong claims mid-draft: `headers` is an array of whole header lines (not an object), `type` is inferred from whether `url` is set, and `autoApprove`/`deny` accept `"*"`/`true` for every tool a server offers. The object-shaped `mcpServers` that Claude Code and Continue use now **warns** instead of configuring nothing in silence

<a id="def-235"></a>
#### STATE-THE-REACH in the system prompt + doctor
**Closed by:** **M387** — a conditional prompt line when an edit scope is armed (deterrent framing; the unit test forbids it reading as an invitation) plus a doctor line, born red at both tiers. It was parked for one batch and closed two milestones later; the row outlived its deferral by a day and was found by reading this page back, which is the failure this page exists to prevent

<a id="def-236"></a>
#### Skip the lossy mid-turn pass once it is exhausted
**Closed by:** **M361** — the exhaustion latch: a dry pressed pass records the exact length at which the sliding window next releases a candidate (oldest protected candidate + keep + 1; every latch expires within keep+1 appends, so a conservative detector can only delay one scan, never skip one forever — the exact re-arm the row demanded); on the driver fixture 12 pressed passes collapsed to 4 full scans

<a id="def-237"></a>
#### The CLI undo leaving saved sessions believing the pre-undo state
**Closed by:** **M350** covered it from the resume side (the mtime drift note); the residual sliver (a clock moving backwards between undo and resume) is not worth a mechanism — kept one register cycle per the row's own note, closed by the 2026-08-10 sweep

<a id="def-238"></a>
#### The DECLARE-THE-GATE repetition (GATE_INTEGRITY §8's n=1 caveat)
**Closed by:** **M344** — five per arm on the #45 setup: the tampering did not recur in either arm; the sentence halved the pre-endpoint poking; table in GATE_INTEGRITY §8c

<a id="def-239"></a>
#### The wizard's 17-option first screen, two questions under one list
**Closed by:** **M326i**

<a id="def-240"></a>
#### The context subcommand reporting rules 0
**Closed by:** **M311**

<a id="def-241"></a>
#### The system prompt's unnamed remainder
**Closed by:** **M312**

<a id="def-242"></a>
#### Per-tool definition sizes
**Closed by:** **M313**

<a id="def-243"></a>
#### Telemetry joined to the tool registry
**Closed by:** **M314**

<a id="def-244"></a>
#### The history as one number
**Closed by:** **M315**

<a id="def-245"></a>
#### Flipping attempt's default to core
**Closed by:** **M320** — closed as *decided against*: the null held on a second model, but the same runs produced a better objection than the one they dissolved

<a id="def-246"></a>
#### context tools under-reporting the live toolset
**Closed by:** **M325b** — one shared registrar for main/context/doctor; 16 → 26 on a git repo, with MCP named as the one exclusion

<a id="def-247"></a>
#### spawn_parallel at 4/10
**Closed by:** **M325** — the log *could* say why after all: 3 watchdog timeouts on a 300 s default against 300–462 s children, 3 forks that could not happen

<a id="def-248"></a>
#### The glob capability gap
**Closed by:** **M324** — `list_files` gained a `pattern`, so the schema objection was fixed rather than argued with, and `glob` graduated from hint to alias

<a id="def-249"></a>
#### Measuring a core attempt that needs a hint
**Closed by:** **M319** — 12 runs, 0 hint calls: the model never asks, so the "capability cost" was theoretical

<a id="def-250"></a>
#### Whether the craft section earns its tokens
**Closed by:** **M318** — measured; a null on a 31B model, so it is now off under `--lite` and unchanged elsewhere

<a id="def-251"></a>
#### A TUI /context tools / /context history
**Closed by:** **M317** — and better there than in the CLI: the TUI holds the *live* history, so no save lag

<a id="def-252"></a>
#### A doctor check for never-called tools
**Closed by:** **M316** — the objection was answered by changing the evidence axis to *distinct sessions* and advising the lever (`toolProfile`) rather than individual tools

<a id="def-253"></a>
#### German editions of the plain-register assignments
**Closed by:** **M309b**


## Open — what the model asked for and M431 did not build (2026-08-13)

M431 implemented Tier 0 (five promises made true). The tiers below are designed, with
every rejected alternative, in
[`proposals/2026-08-model-facing-orchestration.md`](proposals/2026-08-model-facing-orchestration.md).
They are parked as a **scope decision**, not an oversight: the release is weeks away and
Tier 0 was the part that was making jichi's own documents untrue.

The thesis they serve is one sentence: **jichi shows the model its failures and shows the
human its false successes.** Every Tier 1/2 row below is one instance of that.

**Since closed, and removed from the table per this register's own rule** (an item is
removed when done, with the closing milestone in the ROADMAP): the M331 finding at the
periodic verify site and the changelog-coverage bound (**M431b**), the run id on the
machine surface (**M431c**), the hollow completion green (**M431d**), the workspace
lease (**M431e**), the ambient budget panel (**M431f**, shipped OFF so M347's decision
is measured rather than overruled) and the `--connect --output json` downgrade
(**M431g**). Two rows below were **missing entirely** until 2026-08-14 --
the refused-tool advertisement and the UTF-8 preview cut -- so they were findable nowhere
while being named in the proposal, which is the one failure a findability register cannot
afford.

| Deferred | Detail |
|---|---|
| **Charge a subagent's tool calls to the run** | [details](#def-254) |
| **Daemon fleet-worker changes (`fresh`, exit codes, the `--output json` downgrade).** | [details](#def-255) |
| **Taper `spawn_parallel` children's iterations, or state why they are not tapered.** | [details](#def-256) |
| **Reclassify the shell's own cannot-run exits (126/127) as refusals on the grading surfaces.** | [details](#def-257) |

<a id="def-254"></a>
#### Charge a subagent's tool calls to the run
*closed by M431 for the check and the counter*; what remains is the **journal**, which stays depth-0.
**Why:** The enforcement half shipped (`env_budget_applies`). The journal deliberately did not: a forked parallel child appending to the same file would interleave with its siblings, and the parent already reconciles each child's piped count. So `runs`' `tool_calls` for a fan-out is the reconciled total while the journal's `tool_call` events remain top-level only — an asymmetry worth stating before someone joins on it.
**Where:** ROADMAP M431

<a id="def-255"></a>
#### Daemon fleet-worker changes (fresh, exit codes, the --output json downgrade).
**Why:** **Promoted from a deferral to a planned EXPERIMENT with a discard gate**, at the maintainer's direction: [`plans/2026-08-daemon-fleet-worker.md`](plans/2026-08-daemon-fleet-worker.md) carries the honest implications, the design, and — written before any code — the criteria for discarding it. Built on `feat/daemon-fleet-worker`, field-tested on zigodot and chrtext over several sittings, then reviewed. The reason it is not simply built: history persisting across requests is **documented, correct behaviour for a warm interactive helper**, so the real question is whether the daemon should be a fleet worker at all rather than whether it has a bug. One of the three WAS an unambiguous bug (`--connect --output json` silently becoming text, on a Stable surface) and **landed on master at M431g**, ahead of the experiment and exempt from its discard gate; the remaining two (`fresh`, exit codes) are the experiment's.
**Where:** [plans/2026-08-daemon-fleet-worker.md](plans/2026-08-daemon-fleet-worker.md)

<a id="def-256"></a>
#### Taper spawn_parallel children's iterations, or state why they are not tapered.
**Why:** Looked like a defect and is at least an inconsistency: `spawn_subagent` applies `jc_subagent_iters_at_depth` (halving per level) while `run_child` passes `max_subagent_iters` raw, so a depth-1 subagent gets base/2 and a depth-1 parallel child gets base. **Checked before parking:** `SUBAGENTS.md` scopes the taper to *a deep synchronous chain*, and parallel children are a fan at one depth — what bounds a fan is the per-child slice, which M431 makes real. Tapering would silently halve every child's iterations with no measured failure behind it. So the honest remainder is a *documentation* decision (say which rule governs a fan) rather than a code change, and it should be made by whoever can say whether one rule for "a delegate at depth 1" is worth the behaviour change.
**Where:** ROADMAP M431

<a id="def-257"></a>
#### Reclassify the shell's own cannot-run exits (126/127) as refusals on the…
M625 shipped the DECLARED contract (verify exit 77 = cannot-run); 127 (command not found) and 126 (found, not executable) are the same fact said by the shell, and today they still grade FAIL.
**Why:** The ambiguity is real, not hypothetical in shape: a spec whose verify RUNS the learner's deliverable (`./bin/tool`) exits 127 precisely when the work is not done — there, FAIL is the correct grade. M502 ducks this by probing only slash-carrying programs before running; a post-run 126/127 has no way to tell the grader's missing tool from the learner's missing deliverable. Revisit if a real spec shape needs it; the shipped curriculum's graders are all `sh <script>`, covered by 77. (The M624/M615 seam this row replaced — toolchain guards exiting 1, graded as FAIL — closed at M625.)
**Where:** ROADMAP M625, DECISIONS.md M625



## Closed — found while merging M430 and M431 (2026-08-13)

Two milestones were authored the same afternoon and both claimed the number M430; the
later one (this document's M431) was renumbered, because the other was already pushed
and renumbering unpublished work costs nothing. Reading both diffs against each other
is what surfaced its row — and it was the same defect class M431 is about, which is
why it was recorded rather than quietly built. **The row it produced is closed; the
heading is kept for the lesson, retitled at M463 because an `## Open` section with no
rows makes this page advertise work that does not exist.**

## Closed at M637 — the argumentation program (M628–M635), what it left

The program shipped six artifacts with a mechanical floor and said, in each ROADMAP
entry, what was measured and what is still a claim. These are the claims, as rows
with a revisit condition, so the next session starts from the register and not
from memory (recorded 2026-09-16, after M635's gate went green).
**All three closed on 2026-09-17 (M637–M638), and the heading did not follow them until M657** — three struck-through rows under an `## Open` heading, which is the exact state this page names as its one unacceptable one. The rows are kept because each carries what the measurement found, and two of them found something other than what they predicted.


| Deferred | Detail |
|---|---|
| ~~**Run the pre-registered refute A/B**~~ | [details](#def-258) |
| ~~**Read the reach footer and a `PLAN.md` drift line "in anger"**~~ | [details](#def-259) |
| ~~**Link `ARGUMENT.md` from the Ri stage of `JOURNEY.md`**~~ | [details](#def-260) |

<a id="def-258"></a>
#### Run the pre-registered refute A/B
**DONE, M637 (2026-09-17): refute 12 of 12, control 1 of 12; the frame did work the words did not.** Harness `tests/bench/refute_ab/` (first written by jichi itself, then corrected — see the companion note), twelve plants of the three pre-registered kinds listed before the runs, opaque ids, sealed mapping, a per-arm form. The blind was formal (the arm is legible from the answer's own headings) and the false-attack rate was not assessed; both are said on the results page, and the second is the next measurement. [analysis](analysis/2026-09-17-refute-ab.md)
**Why:** The `refute` stage exists (M634) and its benefit is a hypothesis with a number on it, not a result. Plant and record the claims **before** any run. *Revisit when:* the planted list exists in `tests/bench/refute_ab/planted.tsv`; the result page then links the proposal and reports as run or as not run.
**Where:** [proposals/2026-09-refute-ab.md](proposals/2026-09-refute-ab.md); [ROADMAP M634](ROADMAP.md)

<a id="def-259"></a>
#### Read the reach footer and a PLAN.md drift line "in anger"
**DONE, M637 (2026-09-17).** The real task was the A/B harness itself: plan mode wrote a five-section `PLAN.md`; `--auto` under an edit scope and a tool-call fence built the harness by fifty shell appends and stopped on the fence with `23 errors · plan: 3 of 5 predicted files touched · not checked: a shell command ran`. The footer changed the order of reading and exposed an under-specified verifier; it could not see that the harness was wrong. Two footer changes proposed, none made -- then both made, plus the fence fix under the cascade, in M638 the same day. [analysis](analysis/2026-09-17-reading-the-footer-in-anger.md)
**Why:** Nothing in M630 or M631 has been read by a person on a task they cared about; both entries say so in those words. The measurement is one honest session note on what the footer changed, or did not. *Revisit when:* the note exists (a `docs/analysis/` page or an ANECDOTES entry), whichever way it came out.
**Where:** [ROADMAP M630, M631](ROADMAP.md)

<a id="def-260"></a>
#### Link ARGUMENT.md from the Ri stage of JOURNEY.md
**DONE 2026-09-17:** the Ri list's last bullet now sends the reader to the names and says why they come last. (The fourth row this section opened with — a dedicated `workflow.sh` check for the read-only map fence — was done and removed from here by commit 4c0777db on 2026-09-16.)
**Why:** The page is indexed and linked from VOCABULARY.md and CURRICULUM.md, but the one place that discusses leaving the form — where the names are meant to be met — does not point at it. Two sentences. *Revisit when:* the next docs pass touches JOURNEY.md.
**Where:** [JOURNEY.md §Ri](JOURNEY.md); [ARGUMENT.md](ARGUMENT.md)



## Open — the language-teaching coverage review (M636)

Counted, not guessed: `docs/analysis/2026-09-16-language-teaching-coverage.md`
measured what the documentation teaches per language across all 505 markdown
files. Three of the four rows below are consequences of the deliberate
"C is the parameter" choice rather than oversights, and the review says so. They
are recorded because a consequence somebody chose is still a thing the next
reader should meet as a decision rather than as a surprise.

| Deferred | Detail |
|---|---|
| **A "modern C" reading track** | [details](#def-261) |
| **Extend the C++ course, or stop letting the reader read "modern" into it.** | [details](#def-262) |

<a id="def-261"></a>
#### A "modern C" reading track
in the reading-guide genre rather than as more graded tasks.
**Why:** Modern C is currently taught as a **fence**: the constructs named in the docs are the ones CONTRIBUTING.md forbids (`<stdint.h>` 8 files, designated initializers 5, VLA 3), while the ones a modern-C programmer would simply use appear **zero** times (`uint32_t`, `_Static_assert`). A learner finishes able to write 1989's C and to name what jichi may not use. Spine: per construct, what jichi does instead and what that costs. *Revisit when:* a track exists, or the gap is argued closed — Gustedt's *Modern C* is now cited for exactly this and a book may be the right answer.
**Where:** [C_STANDARDS.md](C_STANDARDS.md); [analysis](analysis/2026-09-16-language-teaching-coverage.md) §2.1

<a id="def-262"></a>
#### Extend the C++ course, or stop letting the reader read "modern"…
**Why:** Tasks 59–62 teach RAII, containers and exceptions — a fair curriculum, and **C++98/11 C++**. Across 505 files: `RAII` 15, `std::vector` 8, `C++20` 1, `unique_ptr` 1, `std::span` **0**. The curriculum's own words are accurate; the overstatement happens in the reader's head. Renaming to "C++ fundamentals" is two words and honest; a fifth task on move semantics and `unique_ptr` is better and is a milestone. *Revisit when:* either is done.
**Where:** [CURRICULUM.md](CURRICULUM.md); [analysis](analysis/2026-09-16-language-teaching-coverage.md) §2.2


<a id="def-323"></a>
#### M912's on-disk identity check has no read-intent fence guard (parity follow-up)
**Why:** `args_is_on_disk_write` (M912, jc_compact.c) `jc_read_file`s a path from
the model's write_file args. `read_file` is fence-restricted the same way
(`jc_tool_read.c` → `jc_app_path_denied_ex(app, path, 0)`, read-intent), so the
elision only ever opens files the READ fence permits -- no write-fence bypass,
and no disclosure beyond what the model could already read_file. A one-line
`jc_app_path_denied_ex(app, abs, 0)` guard + a teeth test (file matching outside
the read fence must fall back to the ticket) would make the parity explicit.
This session reverted an untested write-intent guard rather than ship it
unguarded. Small; do red-first.
**Closed at M913 (2026-10-08):** `args_is_on_disk_write` consults
`jc_app_path_denied_ex(app, abs, 0)` after resolving the path and before the
`jc_read_file` -- the read the tool layer would refuse is no longer made by the
compaction layer, and a fence-denied path falls back to the M909 ticket like a
missing one. Red-first `test_on_disk_pointer_respects_read_fence` (a matching
file outside the root must yield a ticket, not the on-disk pointer; teeth
proven: with the guard absent, all three of its checks fail). The write-intent
variant was deliberately not chosen: read-intent is the exact parity with
`read_file`, and a write the model may not make cannot be the identity of a file
it also may not read.

<a id="def-324"></a>
#### A background writer that would block (unbounded output) is still a pipe problem
**Why:** M940 spools background-command output to a file, so the child's `write(2)`
never blocks on a full pipe no matter how much it prints — but the *spool* then grows
with the child's total output. A command that prints unboundedly (an infinite build
log, `yes`, a runaway `tail -f`) fills disk; the 256 KiB in-memory cap (`overflowed`,
head-kept) is unchanged, but the on-disk spool is not capped. That is a different
problem from the stall M940 fixes (a finite build, read slowly), and none has arisen
in a real run. *Revisit when:* a background command is observed to exhaust disk via
its spool — then cap the spool (e.g. stop `O_APPEND` writes past a bound, or splice
through a bounded reader) or move the child's stdout to a size-limited file.
**M984/L-27 (2026-10-19, operator decision):** the pre-fix cap rewrote the spool
with `O_TRUNC` on every drain that saw it oversized, including mid-run drains while
the child still appended -- destroying the child's output in the read->`O_TRUNC`
window (the batch-1 gate's red). Cap-at-reap was accepted: the on-disk file is
uncapped (unbounded) for the whole run until the child exits, and the cap binds at
reap. The RUNNING-time disk bound is this row's deferral, to be restored by a
loss-free design (RLIMIT_FSIZE in the forked child before exec, or a periodic
drain) in a later batch.
**M996/L-27 follow-up (2026-10-20, hardening drive batch 4):** the REAP-time
rewrite was itself loss-unsafe: it opened the spool `O_TRUNC` and wrote in
place, then `ftruncate(d, read_total)` unchecked -- on a short write
(ENOSPC/EIO) the original was already destroyed (the kept tail gone) and the
ftruncate padded the file with NULs to a stale size. The rewrite now writes
the kept tail to a temp file in the spool's own directory and renames it over
the spool: a failed write leaves the spool intact, a successful rename is
atomic and NUL-free. The FAULT knobs `JICHI_FAULT_BGWRITE_SHORT_AT` /
`JICHI_FAULT_BGWRITE_PAUSE_MS` exist for the recovery probe. The
RUNNING-time disk bound above is UNCHANGED and still this row's deferral.
**Where:** [ROADMAP M940](ROADMAP.md), [ROADMAP M984](ROADMAP.md); `src/chat/jc_bg.c`; [ANEC #122](ANECDOTES.md)

<a id="def-325"></a>
#### A model's markdown/prose tool calls are not recovered — they end the turn as an answer
**Why:** this session's M940 field proof hit `qwen/qwen2.5-coder-14b` emitting its
tool calls as fenced ```json blocks (`{"name": "run_terminal_command", "arguments":
{...}}`) instead of native `tool_calls`; the agent loop saw zero native calls and
ended the turn, so nothing ran. A later milestone (operator-requested 2026-10-12)
adds an opt-in, per-model `textToolCalls` recovery pass in the chat layer: when a
reply has no native calls but the model was advertised tools and the flag is on,
well-formed tool-call blocks are parsed and fed to the EXISTING executor (same
fences, budgets, approval preview, telemetry). Rejected: recover unconditionally
(a native model quoting a call would have it executed) and a provider-vtable
change (the markdown is a property of the model's output, not the wire dialect).
**Where:** design at [docs/internal/2026-10-12-md-tool-call-recovery-design.md](internal/2026-10-12-md-tool-call-recovery-design.md); `src/chat/jc_agent.c` (the no-native-calls turn-end); `include/jc_message.h`.
**Closed at M949 (2026-10-13):** `jc_toolcall_recover` (`src/tools/jc_tool.c`)
extracts the well-formed blocks (fence tagged with the tool name, ```json fence,
or a bare `{name, arguments|args}` object; registered names only, JSON objects
only, arguments must be an object) into synthetic tool-call entries on the
assistant message, and the unchanged executor round runs them -- same budget
meter, approval preview, path fence and telemetry as a native call. Opt-in per
model: `textToolCalls` (lenient bool) or `toolCalling: "text"` (the "reserved"
value, now live). The hook fires only when the reply has no native call, so a
quoting model cannot double-execute; the M147 nudge is suppressed for flagged
models. `md_tool_call_recovered` telemetry + `recovered: "text"` on the
`tool_call` events; red-first `test_toolcall_recover` and the
`text_tool_recover` smoke driver (teeth proven both ways).
<a id="def-326"></a>
#### Background output capture keeps the head, so a model can never get a chatty child's last lines
**Why:** the in-memory capture caps at 256 KiB (`JC_BG_BUF_MAX`,
`include/jc_bg.h`) and on overflow "keep reading, keep the head"
(`src/chat/jc_bg.c`): bytes past the cap are dropped. The dropped bytes are the
child's NEWEST output. A 2026-10-13 bonsai-27b re-drive (context 63488, M940
held, rc=0) showed the consequence: each child run wrote 471,144 bytes but the
capture held only the first 256 KiB (the reads returned ~262197 each), so the
final lines never reached the model and it re-ran the command ~13 times before
ending the turn with no answer to the "last three lines" request (handoff:
`docs/internal/2026-10-13-m940-proof-handoff.md`). This is a policy gap, not a
bug in the M940 stall fix, and distinct from def-324 (uncapped on-disk spool):
the disk spool held the complete 471,144 bytes per run; only the
model-facing capture was head-truncated at 256 KiB.
*Revisit when:* making a local model convergable on chatty background output
(proposed fix: tail-aware capture -- keep the last N lines/bytes plus a
total-dropped marker, or make the policy explicit and documented).
**Where:** [ROADMAP M940](ROADMAP.md); `include/jc_bg.h`; `src/chat/jc_bg.c`;
[proof handoff](internal/2026-10-13-m940-proof-handoff.md)
**Closed at the M3 landing (`c31cee0c` + `7d814ea0`, 2026-10-16;
recorded at M960):** tail-aware capture as proposed -- `JC_BG_TAIL_MAX` (8 KiB)
ring in `drain()`, flushed at exit behind the `[... N middle bytes not shown
...]` marker, gated on `overflowed` (the total really over-ran the cap). Red
first: `test_bg`'s M940 assertion flipped to head + tail PRESENT, middle
DROPPED, proven failing before the flush. The in-memory view is now head +
tail; the on-disk spool remains unbounded (that is def-324, still open).

## Open — the platform campaign, what the 2026-10-15 landing left (2026-10-15)

| Deferred | Detail |
|---|---|
| the two remaining boards re-run at the post-merge master, then the V2f stamp | the Pi's run at this head printed 0 red, so expect the same; V2f blocker: `archive.debian.org` health |
| the rig split (the two phases, §6 of the runbook) | so the 20+ minute build is a separate artifact from the 8 minute model call |
| the campaign tooling (§7 of the runbook) | inventory + driver, the three edge-case profiles, the unattended safeguards |
| `ttft_ms` on cut attempts + the stall cap's name in telemetry | a cut's record says nothing about WHY; the prefill measurement (`86c0c9d1`) is what a jichi-side capture would make routine |
| the stall default decision (DECISIONS.md) | half-settled: the prefill measurement landed (`86c0c9d1`) -- 30 s wrong for local models, 240-360 s supported; the DECISIONS.md row remains |
| the portability analysis's findings need register rows | the git-children env, the fallback `snprintf` crash, the weak-entropy fallback, `1.27e270` latency (`2026-10-15-portability-analysis.md`) |

## Open — the hardening drive, what M975 (D6) did not build (2026-10-18)

| Deferred | Detail |
|---|---|
| M975’s detection watching `.git/config` and `.git/hooks` (D19) | the plan said D6’s detection covers B-02’s class; it does not — the story in **D19, the `.git` watching** below |

**D19, the `.git` watching.** The plan’s 0c line and D6-DECISION.md said D6’s detection “also covers B-02’s class” (a planted git-config write); it does not — no code in `jc_scopededit.c`/`jc_agent.c` watches those paths. The operator decided DEFER to batch 4 with B-02 (the M2 “last piece”), 2026-10-18 ~21:45. Until it is built, `.git/config`/hooks changes by a lead are unwatched by D6; 0e’s gated-head note says so plainly, so the M3 drive's supervisor does not rely on it.

| L-26: 20 bare-statement `JC_REQUIRE`s discard their guard | fix the 20, then a lint floored at 0 (details: FINDINGS.md L-26; the same shape segfaulted M729 in test_scopededit.c, fixed 7acde6c8) |

## Open — the self-driving harness (plan 2026-10-21)

The operator asked on 2026-10-21 why D4 (jichi-harness, decided 2026-10-04) was never
built. The answer: it was never deferred or tracked. It had no row on this page, so when
ownership moved nobody knew it was owed. The plan that picks it up is
`docs/plans/2026-10-21-jichi-harness.md` (branch `analysis/2026-10-18-orchestration`); it
lands at the drive's end, and its §16 asks for these rows now so they reach master with
the next batch.

| Deferred | Detail |
|---|---|
| the self-driving harness (D4 of 2026-10-04) | [details](#def-327) |
| a multi-tenant job service for the harness | [details](#def-328) |
| `secretEnv` and `-C <dir>` (plan 2026-10-03, items 4.2 and 4.3) | [details](#def-329) |
| a Landlock rule set for the top-level shell (beside def-149 / O9) | [details](#def-330) |
| whether a second model catches different mistakes in the supervisor role | [details](#def-331) |
| reports as task results, outside the merge | [details](#def-332) |
| the currency lints assume mint order == build order | [details](#def-333) |
| a bare `JC_REQUIRE`/`JC_CHECK` statement, then a dereference | [details](#def-334) |
| the model-request half of `jichi-harness doctor` (live) | [details](#def-335) |
| the rehearsal container-prefix / unit-name rewrite (setup-dir step 4) | [details](#def-336) |
| the pre-push hook installation and the mirror's credential helper | [details](#def-337) |
| the `objects/info/alternates` file's named targets (the recursion the mirror-fetch precheck refuses, does not walk) | [details](#def-339) |
| the allow-list env for the harness's git children (the "minimal env" the C9 correction removes) | [details](#def-340) |
| the `.gz` key scan (the decompressed content; the raw-only CHANGED line) | [details](#def-341) |
| the size-limit config knobs (ZD_PUSH_MAX_FILE_MB / ZD_PUSH_MAX_TOTAL_MB) | [details](#def-342) |
| the alternates-recursion fence as a Mode B (container / separate user) capability | [details](#def-338) |
| the credential-helper's user-name rule from the configured server kind (rehearsal hardcodes none/oauth2) | [details](#def-343) |
| the size-check's report shape (the 8-with-count list, the directory sizes, the commit count) | [details](#def-344) |
| the P4d cell's direct-push fixture shape vs the note's two-port-pushes wording | [details](#def-345) |
| the same-uid check-then-use windows (Mode A; precheck, URL guard, ls-remote, push) | [details](#def-346) |
| the git stderr cut floor (200 bytes can clip `(fetch first)` under a long TMPDIR) | [details](#def-347) |
| the HTTPS/GitLab credential path end-to-end (a rehearsal never pushes to a real HTTPS remote) | [details](#def-348) |
| the off-by-one tooth for list_pushed's argv sizing (Q:f9f3e8d9.F1, the fix at 29df6d33; BUILT this drive -- the committed SAN boundary cell) | [details](#def-350) |
| the duplicated integrity prechecks between `mirror fetch` and `mirror push` (no lint keeps the two copies equal) | [details](#def-349) |
| a lint: a drive's labels are qualified and defined in its TERMS.md | [details](#def-351) |
| the size check's unmeasured-object refusal (R:1879e41a.P3, the `miss` the port dropped) | [details](#def-352) |
| a lint: `clean` removes every object the Makefile builds (the -D-variant-stage gap, run 0012) | [details](#def-353) |
| the smoke-tier count in docs/i18n/ja/VOCABULARY.md is pinned by no lint (i18n_tracks_lint, run 0012) | [details](#def-354) |
| the post-run record collection in `jichi-harness run` (sessions/attempts/lmstudio copy, reference run-lead.sh 181-195) | [details](#def-355) |
| the per-tenant `apiKeyEnv` name in `jichi-harness run` (the run delivers under the fixed `JICHI_API_KEY`) | [details](#def-356) |
| the gateway-up probe in `jichi-harness run` (parking a run while the endpoint is down) | [details](#def-357) |
| the `.gz` notes in `jichi-harness run`'s handoff (the reference gunzips a previous run's compressed notes) | [details](#def-358) |
| the elided-arguments repair (a copied tool-output placeholder's `ticket` is never read by the refusal that names it) | [details](#def-359) |
| the records-branch push in `jichi-harness drive` (the loop commits `records: run N (KIND)` into `dir/recs` but does not push the records branch) | [details](#def-360) |
| the credential-helper classify_url userinfo + percent-escape + the driver bare grep (M1066, SV:0904.5 -- latent, neither applies to this drive remote) | [details](#def-361) |

<a id="def-327"></a>
#### the self-driving harness (D4 of 2026-10-04)
**Why:** the operator decided on 2026-10-04 that the harness is a third C89 program
shipping with jichi. On 2026-10-21 the operator widened it: jichi "or any agent" drives
itself; one model must be enough; four execution modes (normal user, separate user, daemon
or service, containers). Plan: `docs/plans/2026-10-21-jichi-harness.md`. Not built because
it was never scheduled: no row here, and the session that held the plan stopped on
2026-10-04. Built by the jichi-harness drive of 2026-10-23, batches H0-H5:
`docs/internal/drive/2026-10-23-harness/PLAN.md`.

<a id="def-328"></a>
#### a multi-tenant job service for the harness
**Why:** D4's open question (per-job command or service?) is answered "both"
(2026-10-21). The per-job command comes first, with unit templates. A service that accepts
jobs from several tenants (authentication, separation, scheduling, restart with half-done
jobs) waits until several tenants exist.

<a id="def-329"></a>
#### `secretEnv` and `-C <dir>` (plan 2026-10-03, items 4.2 and 4.3)
**Why:** both decided 2026-10-04, never built. The key scrub covers only the configured
key variable; a credential under another name reaches every child (HARDENING.md, M894).
They are the harness's first prerequisites.

**Status:** the 4.2 half (`secretEnv`) is **done at M1043-a** -- a top-level
`"secretEnv"` register, four built-in CI/forge names, and a `doctor` warning for a
secret-looking name in no list (HARDENING.md, section 2). The 4.3 half
(`-C <dir>`) is **done at M1043-b/M1043-b-c (the -C chdir before every
dispatch)** -- the run's workspace is a flag: jichi chdirs to the named
directory before every dispatch (subcommands AND run modes), refused when it
is not a directory or is a file. Both halves are now done.

<a id="def-330"></a>
#### a Landlock rule set for the top-level shell (beside def-149 / O9)
**Why:** in the normal-user mode it is the main defence against a command reading the
operator's files. Today only `spawn_parallel` write children are fenced, and for writes
only.

**Mirror fetch precheck (batch H3b):** `mirror fetch` runs five git-free integrity
checks (bare, ownership, config bytes against the stored copy, no alternates, no planted
hooks) before it lets a git child touch the mirror. The checks and the fetch are the same
user, so a same-uid racer (an agent command that plants between the check and the git call)
is not caught -- the precheck is a check-then-use guard, not a fence. The complete fence is
this row's Landlock rule set (or a separate agent account, the threat model's Mode B, the
plan's section 7.3). The precheck still buys the same-uid case the drive actually ran into
(the agent re-writes its own mirror config in one atomic command and the next fetch refuses
it), and the fetch child's `-c core.alternateRefsCommand=` second layer buys the traversal
side. Design note: the H3b design note's measured behaviour list and its Deferred section.

<a id="def-331"></a>
#### whether a second model catches different mistakes in the supervisor role
**Why:** the operator requires one-model support (2026-10-21). The drive's same-model
reviewer (24 runs, qwen27b like the lead) measured depth, lag and blind spots, but no
review covered the one range where a different model caught a hollow probe (C-01), so the
comparison is open. The plan's rehearsals P7 (two models, then one) measure it.

<a id="def-332"></a>
#### reports as task results, outside the merge
**Why:** a worker's report (its claim, command and red-first evidence) is a file in its worktree, merged like code. With one shared name it collided in 21 of 36 multi-task batches over runs 0001-0063 (38 reports set aside, measured 2026-10-22); with per-task names at the root, `git add -A` committed one and `snapshot_lint` check 16 turned the gate on 152957a1 red. Since 2026-10-22 the drive's lead prompt routes reports to `docs/internal/drive/2026-10-17-hardening/reports/` by convention, which depends on the lead filling in each path. The milestone: `spawn_parallel` collects a declared per-task report outside the merge, keyed by run, batch and task (the lead's label is a label only), with a small machine-readable header (task id, label, model, build, base and final commit, outcome, the red-first command and lines), redacted and stored with the run's records, readable for a kept task; a root `REPORT.md` or `REPORT-*.md` is read as the report and kept out of the merge. Red first: two tasks write reports; both are stored, neither is merged, nothing is set aside. Design: `docs/plans/2026-10-21-jichi-harness.md` section 10.5; analysis `docs/analysis/2026-10-22-one-report-file-for-every-worker.md` (both on branch `analysis/2026-10-18-orchestration`).
<a id="def-333"></a>
#### the milestone-currency lints assume mint-order == build-order
**Why:** `milestone_currency_lint` (check 2) and `changelog_coverage_lint` (checks 2-3) both take "newest" from the ROADMAP's **last** `### M...` heading (`grep '^### M' | tail -1`; the file is chronological) and compare it against the **highest milestone number** cited in docs/ / named in the CHANGELOG. That is one assumption in two places: numbers are minted and built in the same order. Batch G18 (M1048) broke it -- M1048 was *claimed* before M1049/M1050 (the hardening drive's run 0076 registered the number on its own branch) but *built* after them, so a fresh `### M1048` entry at the end made the last heading (M1048) lower than the highest cited (M1050) and both lints went red. The fix that landed (supervisor 04:39) is the precedent on master (c18f805e, M1006): place a lower-number-built-late entry **before** the higher-numbered ones so the last heading still carries the highest number, and keep both banners (README/ROADMAP) at the highest. **Two options for a permanent fix, per the operator's "design now, build when needed":** (a) the lints compare against the **highest** `### M...` number rather than the last heading (order-robust, but stops catching "a number cited in docs/ has no ROADMAP entry at all" the way the last-heading read does -- a cited milestone with no entry would no longer be flagged unless its number is also the max); or (b) keep the order convention and add a lint that verifies the `### M...` headings are non-decreasing (so a future out-of-order entry is caught at the lint, not at the two currency checks). Either way the convention "a late-built lower number goes before the higher ones" needs to be written down (it is only implied by c18f805e and this incident). Registered 2026-10-24 (run 0002), not built.
<a id="def-334"></a>
#### a bare `JC_REQUIRE`/`JC_CHECK` statement, then a dereference
**Why:** the suite's `JC_REQUIRE(cond)` records the failure and keeps going -- it does not return -- so a bare `JC_REQUIRE(write_file(...))` whose write fails lets the next line run against the absent state. The M1051 unit test committed it exactly that way; the follower's M729 stage (an unwritable `TMPDIR`) segfaulted the whole suite at a `strcmp` on a NULL field (rc 139, the run did not complete; the hardening drive's batch 0 met the same class in test_scopededit.c, 2026-10-18). Fixed 2026-10-24 (run 0003) with `if (!JC_REQUIRE(...)) return;` guards. **The deferred build:** a lint that flags every bare-statement `JC_REQUIRE(...)` / `JC_CHECK(...)` -- or at least one whose next non-comment line dereferences a pointer from a fixture write -- would catch this class in the fast gate instead of at a full gate. Not built, per the supervisor's 07:1x note ("register it; do not build it now").
<a id="def-39"></a>
#### measure stream silence before touching the 900 s dead-stream fence
**Why:** the operator asked (2026-10-23) why `timeouts.deadStream` is 900 s and whether it was measured. It was grounded on WHOLE-CALL durations (M810: max 540 s of 12,464 calls) and never on SILENCE, and the `ttft` data is censored at the 30 s `stall` cap -- so the fence's actual trigger condition (a connected stream sending nothing) has no measurement. A board LM Studio prefill ran ~220 tok/s, so a long prompt CAN be silent past 900 s (the fence would then abort a healthy call); the 2026-10-23 data point is the opposite end (the M1006 workers streamed 18+ min at ~6.2 KB/s each, `ss` bytes_received deltas -- the fence correctly stayed quiet). The build is deferred: measure `max_gap_ms` / `last_gap_ms` / `rx_bytes` on every `model_call` + a reader, a second route via `ss -tni`, uncapped board runs, and only then set a default with a stated margin. Design note: `docs/plans/2026-10-23-measure-stream-silence.md` on branch `analysis/2026-10-18-orchestration` (9fdc9931). Not part of batch 14e; the operator's rule of 2026-10-23 -- design when found, build when needed.

<a id="def-335"></a>
#### the model-request half of `jichi-harness doctor` (live)
**Why:** H1b's `jichi-harness doctor` is OFFLINE: it reports on the drive's configuration (the file the M1051 loader parses) -- the three secret references are checked as NAMES and the files they point at are stat'd, never opened, never printed. The half of `harness/doctor-live.sh` that makes ONE real request to a model server (does the gateway answer, is the key accepted, does the model call tools natively) is NOT built here: it needs the model key, which H1b deliberately never reads. It lands with **H4 (`run`)**, the batch that hands the key to jichi over stdin at the moment of use. Registered the same commit as the M1052 note (notes/M1052.md), per the drive's rule that every decision not built gets its row in the same commit.

<a id="def-336"></a>
#### the rehearsal container-prefix / unit-name rewrite (setup-dir step 4)
**Why:** H2's `init` ports setup-dir.sh CHANGED line 6 away. The script, in rehearsal,
rewrote `harness/drive.env` so the rehearsal's container prefix carried an `r`
(`zd-lead-` became `zdr-lead-`) and its default unit got a `-rehearsal` suffix, so a
rehearsal run while the drive runs could never take, or be mistaken for, the drive's
container. The incident it guards (named in the script's comments, 2026-10-01): the
demos drive's save-batch once read the LIVE drive's container from a rehearsal. But that
guard is a Mode D guard -- it separates two CONTAINERS. Mode A has no containers: the
harness and the agent are one user account, and a rehearsal's "container" is a
directory. H2 therefore does not install the rewrite, and there is nothing for it to
rewrite (the `harness/` archive it acted on is CHANGED line 4's drop -- the C port IS
the harness). It lands with **Mode D / P4** (containers), where a rehearsal container
prefix is a thing again. Design note: the M1053 note's CHANGED lines (H2 batch), and
`docs/plans/2026-10-21-jichi-harness.md` section 7.4 (Mode D, containers).

<a id="def-337"></a>
#### the pre-push hook installation and the mirror's credential helper
**Why:** H2's `init` ports setup-dir.sh CHANGED lines 11 and 13 away. The script
installed, into the repos it created: the mirror's three pre-push hooks (the key
scan, the size check, the mirror wrapper that runs them, lines 99-101) and the
mirror's `git-credential-from-file` helper (line 102), and the records clone's
key-scan pre-push hook (line 117). Those are the repos' ARMS -- the key scan is the
line that stops a canary or a real key reaching a remote, and the size check stops a
run from pushing a bloated record. H2 builds them as C logic (the scan, the size
bound, the mirror wrapper, the credential file) in **H3 (`mirror`)**: `init` lays
out the repos without arming them, and the first armed push goes through the
harness's `mirror` command, not a planted hook. Design note: the M1053 note's
CHANGED lines (H2 batch), and `docs/plans/2026-10-21-jichi-harness.md` section 15
(H3 row).

<a id="def-339"></a>
#### the `objects/info/alternates` file's named targets (the recursion the precheck refuses, does not walk)
**Why:** H3b's `mirror fetch` check 5 refuses a mirror that has an
`objects/info/alternates` file, because git runs the mirror's
`core.alternateRefsCommand` while walking the refs of an alternate object
store (the note's M2 vector, supervisor point 1, measured on 2.53: a planted
mirror with a config + alternates file fires the command, removing either does
not). The precheck refuses the FILE ITSELF, so the command never runs. It does
NOT walk the targets the file names: an alternates file points at other
object stores, and following that chain (each of which could name more) is
recursion the harness does not want to own. That walking is **P4**
territory -- the same bucket as def-330's Landlock fence, where a fenced git
child would make the traversal safe to audit. Design note: the H3b design note's
M2 vector (supervisor point 1) and its Deferred section.

<a id="def-340"></a>
#### the allow-list env for the harness's git children (the "minimal env" the C9 correction removes)
**Why:** the H3b design note's C9 paragraph first claimed `git_m2` passes the
fetch child a MINIMAL env. The supervisor (2026-10-24 22:45, point 3) and the
lead both checked the code: `jc_spawn_build_envp` (src/util/jc_spawn.c,
`scrub=1`) copies ALL of `environ`, drops only the names the DENY-LIST scrub
knows (the builtin list in src/util/jc_proc.c:71-78 — the model-key and
git-token names are on it — plus `jc_proc_secret_env_add` registrations), and
appends the configured entries. Every unregistered variable reaches the git
child. So the protection is the deny-list, and the note's "minimal env"
sentence was wrong. The rejected alternative: make `git_m2` pass a real
allow-list env. That would change EVERY harness git child's environment (the
doctor and init children included) for a protection the deny-list already
gives to the names that matter, and its consequences are unmeasured — the
harness reads the model key and the git token from FILES at the moment of use,
so an env copy under a registered name is a scrub target, not a transport.
**When:** if the deny-list is ever found insufficient (a secret crossing a
child under an UNregistered name), the allow-list is the fix; it is a CHANGED
line for the harness's spawn path, with its own red-first driver.

<a id="def-338"></a>
#### the alternates-recursion fence as a Mode B (container / separate user) capability
**Why:** def-339 (H3b) refuses a mirror that has an `objects/info/alternates`
file and names the targets it would run, but does not WALK the targets: the
file can name a repository that has its own `alternates`, and the chain (each
of which could name more) is recursion the harness does not want to own. That
walking is **P4** territory — the same bucket as def-330's Landlock fence,
where a fenced git child would make the traversal safe to audit. The full
fence (the container, the separate user) is Mode B/C, where the userns
boundary closes the threat model the M1054 note's "measured the threat it
defeats" paragraph describes. `docs/HARNESS.md` names def-338 as the complete
fence for the check-then-use guard; the row was lost as collateral in 5d768c47
(run 0009, M1055's code commit) and restored here at M1056. **When:** P4 /
Mode B is built.

<a id="def-341"></a>
#### the `.gz` key scan (the decompressed content)
**Why:** M1056's `mirror push` key scan (behaviour 5's CHANGED line) reads
each blob's RAW bytes and scans for the two secrets verbatim; it does not
decompress a `.gz` blob, so a secret stored only in a blob's compressed form
is not refused. The reference's `pre-push-keyscan.sh` DOES decompress `.gz`
and re-scans. Rejected alternative: link zlib into the harness (a new
dependency of the third program on the LOW_MEMORY rows that do not ship it),
or a `gzip -dc` child (an extra process per `.gz` blob). **When:** a
rehearsal plants a decompressed canary in a `.gz` and the operator wants it
covered; the fix is `gzdopen` or a `gzip -dc` child with its own red-first
cell, the dependency measured on the LOW_MEMORY rows.

<a id="def-342"></a>
#### the size-limit config knobs (ZD_PUSH_MAX_FILE_MB / ZD_PUSH_MAX_TOTAL_MB)
**Why:** the reference's `pre-push-sizecheck.sh` reads two env knobs so the
operator can raise the per-file (8 MiB) and per-push (20 MiB) limits for one
refused push. M1056's size check uses the reference's MEASURED constants, not
config keys, so a limit the operator wants raised cannot be raised without a
rebuild. **When:** the operator raises a limit for a refused push; the fix is
a config key read by the size check, linted like the settings table.

<a id="def-343"></a>
#### the credential-helper's user-name rule from the configured server kind
**Why:** the qwen27b review's F3: `jc_harness_push.c` hardcodes the
`credential-helper`'s user-name rule to `none` for a file/local URL and
`oauth2` for https, rather than calling `jc_harness_user_name_rule_of(kind)`
(the M1051 loader's per-server-kind rule). For the rehearsal (local stand-in,
no servers) the value is `none`; for a real https server kind that is not
GitLab the hardcoded `oauth2` would be wrong (GitHub uses a different
rule). The driver's P11 cells pin the `oauth2` shape, so the change is not
driver-testable in rehearsal. **When:** a non-GitLab https server kind is
configured for a push; the fix reads the rule from the server kind, and the
driver's cell is generalised to assert the configured rule.

<a id="def-344"></a>
#### the size-check's report shape (the 8-with-count list, the directory sizes, the commit count)
**Why:** the qwen27b review's F4: the reference's `pre-push-sizecheck.sh`
reports the over-8 MiB files with their COUNT, the top directory sizes, and
the commit count of the push. M1056's size check reports the over-limit files
and the total MB (`MB in all`, `per push` — what the driver's P4 cells grep).
The richer report is a UX detail, not a safety property: the REFUSAL (the
exit class, the over-limit naming) is the same. **When:** the operator wants
the richer report for a large push; the fix is the reference's report
emission, with the driver's cells left as-is (they grep the minimal shape).

<a id="def-345"></a>
#### the P4d cell's direct-push fixture shape vs the note's two-port-pushes wording
**Why:** the qwen27b review's F7: the note's P4d paragraph (the supervisor's
01:37 item 1) says the driver's cell does TWO pushes (a small file lands on
the first, a second push is clean because the first's tip is the ls-remote
exclusion). The committed driver's P4d instead lands the big blobs by a
DIRECT push (the driver's own instrument) and does NOT refresh the tracking
refs, then the port's first push must be clean. Both forms hold the same tooth
(a tracking-ref-only exclusion re-counts and is refused); the direct-push form
is at least as strong because it leaves the tracking refs STALE (the
supervisor's exact measurement). The note's wording is the deviation.
**When:** if the note is corrected to name the direct-push form (it already
does, at 5196eddc+); this row records that the note's prose and the driver's
fixture were once two different shapes for the same tooth, and the driver's
shape is the one that holds.

<a id="def-346"></a>
#### the same-uid check-then-use windows (Mode A; precheck, URL guard, ls-remote, push)
**Why:** the qwen27b review's F8 (the def-330 class, now named for the push):
the push is a sequence of check-then-use steps (the five prechecks → the URL
guard's get-url → the ls-remote → the key scan → the push), each reading the
mirror's config or the stand-in's refs, and a same-uid process (the agent's
own git child) can plant between any two. The guard is a REFUSAL against the
agent's commands, not a fence against a deliberate same-uid racer (the M1054
note's threat-model paragraph). The complete fence is def-338 / Mode B
(container / separate user). The env files and the stored config copy are
also agent-writable in `cfg.dir` (Mode A has no container). **When:** Mode B
/ P4 is built; the userns boundary closes it.

<a id="def-347"></a>
#### the git stderr cut floor (200 bytes can clip `(fetch first)` under a long TMPDIR)
**Why:** the qwen27b review's F9: `jc_harness_push.c` cuts a git child's
stderr to 200 bytes before reporting it; the P12 non-FF cell greps for
`(fetch first)`, which sits past byte 200 when the TMPDIR path is ~130 chars
(the `To <path>` line pushes it back). The driver's cells are green under the
task's runs (short TMPDIR), but the cut is a latent floor. Also: two `int bad`
declared-but-unused in the key/size scan (dead, the lints do not flag them
because they are in a C89 block-top declaration the warning set does not
catch). **When:** the operator runs under a long TMPDIR and the cell is red;
the fix is a longer cut (or a grep of the full capture, not the cut) and the
dead vars removed.

<a id="def-348"></a>
#### the HTTPS/GitLab credential path end-to-end (a rehearsal never pushes to a real HTTPS remote)
**Why:** M1056's `mirror push` port of the credential-helper reset-then-set
flags, the `useHttpPath=true` and the helper's host+path guard (the
supervisor's 01:37 item 2) is exercised by the driver's LOCAL stand-in (a
file-URL push, which consults no helper) and by the helper's OWN cells (a
direct `credential-helper get` with a scheme URL on stdin). It is NOT
exercised end-to-end: a rehearsal never pushes to a real HTTPS remote (the
drive's secret rule — never GitLab, never a real token, in a test). The
end-to-end HTTPS push is exercised by the drive's own loop (the operator's
go), and its guard is the same code path the driver's argv cells measure.
**When:** P4 / Mode B (the container, the separate user) — the threat model
the M1054 note's "measured the threat it defeats" paragraph describes is
fully closed by the userns boundary there.

<a id="def-349"></a>
#### the duplicated integrity prechecks between `mirror fetch` and `mirror push`
**Why:** M1056's `mirror push` (H3c) re-implements the M1055 five integrity
prechecks (bare, owner, config bytes vs the stored copy, alternates, hooks) as
a second copy in `src/harness/jc_harness_push.c` — the push must not trust a
mirror that has drifted since the last fetch, so it re-runs them, but the note
said "the port shares the M1055 check code (or reuses the mirror-fetch
precheck)". Two independent copies of a security precheck can drift: a fix to
one (as happened to the fetch's GIT_CONFIG isolation, SV:0117.1) does not
propagate to the other, and no lint today notices. Rejected alternative: a
smoke lint asserting the two check sequences are textually equal — the checks
are interleaved with push-specific state (the ref set, the stored URL) and a
textual diff would be brittle; a shared C function is the real fix but would
refactor `jc_harness_mirror.c` (the fetch's owned file) out from under M1055's
verified cells, so it is not done mid-milestone.
**When:** a follow-up that shares the five checks as one C function called by
both the fetch and the push, with a lint that the precheck list (what is
checked, in what order) is named in ONE place.

<a id="def-350"></a>
#### the off-by-one tooth for `list_pushed`'s argv sizing (BUILT -- the committed SAN boundary cell)
**Why:** R:29df6d33.P1 (supervisor 2026-10-25 10:57, the independent review of f4500f78..29df6d33):
28/28 of the push driver's cells stay green with the fix reverted to the `+4` sizing, because every
fixture keeps `an` near 8-11, far below the 76-entry boundary. The UBSan proof (constants 1/1,
"index 6 out of bounds for type char *[6]") lived in the worker's worktree, not the committed gate.
This row keeps that "why"; the cell it names is now built and landed in this drive.
**The exact cell (BUILT, this drive):** `tests/smoke/harness_push_argv_boundary.sh`, run by the
`make smoke-push-boundary` stage (wired into `ci` beside the leak_turn SAN stage). It is GREEN on
the current tree (`+5`) and PROVEN red when the argv sizing reverts to `+4` (UBSan
"index 6 out of bounds for type char *[6]"). Because the boundary is UNREACHABLE at the real
24/48 caps (R:29df6d33.P2: `git_child` refuses `nargs>40`, `git_m2` refuses `nargs>8`, so a
76-argument rev-list never runs), the stage lowers the caps to 1/1 via `-D` defines, which lands the
fixture at ntips=1, excl->n=1, an==6==T+E+4 (the legal max). The `an > T+E+4` guard is defence in
depth; the `+5` array sizing is the fix. The two caps are `#ifndef`-guarded (default 24/48) so every
build that passes no defines is byte-identical. Design note:
`docs/internal/drive/2026-10-23-harness/notes/def-350.md`; report:
`.../reports/2026-10-25-def-350.md`.
**When:** Landed in this drive (the run-0012 follow-up on M1056, board card 16). The row is kept --
it still records the why -- but marked built; nothing is deferred.

<a id="def-352"></a>
#### the size check's unmeasured-object refusal (the `miss` the port dropped)
**Why:** R:1879e41a.P3 (independent review of 29df6d33..1879e41a, 2026-10-25 10:42): the
reference `pre-push-sizecheck.sh` refuses when an object cannot be measured (its awk counts a
non-numeric size field as `miss` and, at END, `if (miss) { print "... object(s) could not be
measured: push refused"; exit 1 }`). The port's `parse_blobs` (jc_harness_push.c) parses a
`cat-file --batch-check` line only when its type field is the literal `blob` and its size field
is all digits; every other line (commit/tag lines, and a hypothetical `missing` line) is
skipped, so it drops out of the size accounting without a refusal. The reviewer judges it
practically unreachable: `cat-file --batch-check` itself refuses a missing object upstream,
so the range listing fails (and the port's own fail-closed "cannot list what the push adds"
refusal fires) before an unmeasured line could reach `parse_blobs`. A port keeps every
behaviour unless its note names the change, so the change is named: this row + the CHANGED
line added to notes/M1056.md (the "miss" refusal is not ported; the upstream cat-file
refusal is the load-bearing guard). Rejected alternative: port the counter + a driver cell
that feeds a synthetic unmeasurable `batch-check` line -- it would test a string the
upstream tool never produces, and a driver cannot make `cat-file` emit one without a
corrupt object database (a fault-injection rig, not a rehearsal).
**When:** a later drive that fault-injects the batch-check child (the FAULT=1 tier has a
place for it) wants the fail-closed line held by its own cell; then the port counts the
unmeasured lines and refuses, with a red-first cell feeding the synthetic line.

<a id="def-351"></a>
#### a lint: a drive's labels are qualified and defined in its TERMS.md
**Why:** the operator, 2026-10-25, could not find what H3, P7 and so on meant. "P7" meant four
things in the harness drive of 2026-10-23. The rule (docs/internal/drive/README.md) and the drive's
TERMS.md hold it by hand. A lint would hold it forever.
**Design:** docs/internal/2026-10-25-drive-label-lint.md (three checks, red-first fixtures, rejected
alternatives).
**When:** before the first batch of the next drive, or the day a collision is found again.

<a id="def-353"></a>
#### a lint: `clean` removes every object the Makefile builds
**Why:** run 0012 (def-350) found that `make clean` removed `$(HARNESS_MAIN)` but not the other six
harness objects (`HARNESS_CONFIG/DOCTOR/INIT/MIRROR/PUSH/CRED`). The `smoke-push-boundary` stage's
leading `make clean` therefore left a stale 24/48 `jc_harness_push.o`, and the stage's sub-make linked
a hybrid whose `jc_harness_push_caps()` reported 24/48 -- the value-pinned gate 2 fired red on the
wrong binary. Two consequences the supervisor (16:37) named: (1) the stage's green inside `make ci` at
042e9f24 is WEAKER evidence than it looked, because the guard had not yet value-pinned the marker; the
supervisor's own teeth (from a fresh clone, nothing stale) still stand. (2) the same gap can hide any
`-D`-variant stage: a stale object built at different caps/defines links into a build that claims
otherwise. The six-object fix (9eaa27df) closes the known instance by hand; a lint holds it forever.
**Design:** a `make`-parse lint (POSIX-sh, no python3) that, for every `*.o` the Makefile's build rules
can produce (the `$(HARNESS_*)` / `$(LIB_OBJ)` / `$(MAIN_OBJ)` / `$(TEST_OBJ)` / fuzz / lf / tt object
lists), asserts the `clean:` recipe's `rm -f` names it. Universe stated in the lint's header; floor set
at today's exact object count; red-first by removing one object from the `rm` list and confirming the
lint goes red. A second check (optional): the `-include` dep line covers every object that is
`-MMD`-generated, so a header-only change forces a rebuild (the F2 nit: the six objects 9eaa27df made
cleanable are not in the `-include` line, only `$(HARNESS_MAIN:.o=.d)` is -- a header change to
`jc_vec.h` will not rebuild `jc_harness_push.o`).
**Rejected:** adding the six objects' `.d` to the `-include` line as the fix -- that cures the header
rebuild but not the stale-across-clean gap, and a lint still holds the `rm`-list completeness. The lint
is the durable form.
**When:** before the next `-D`-variant stage is added (the H4 `jichi-harness run` may need one), or the
day a stale-object link is found again. The two DEFERRED rows def-353 and def-354 together are the
"record the weak evidence + close the gap" pair from run 0012.

<a id="def-354"></a>
#### the smoke-tier count in docs/i18n/ja/VOCABULARY.md is pinned by no lint
**Why:** run 0012 (5b16a11e, the supervisor's 496->497 order) edited three files: CONTRIBUTING.md:358,
docs/VOCABULARY.md:165, and docs/i18n/ja/VOCABULARY.md:77. The first two are pinned by
`docs_counts_lint.sh` check 14 (exact equality of the "N drivers ... Python-free" present-tense figure).
The third -- the Japanese "497 台のドライバ" -- is pinned by NOTHING: `i18n_tracks_lint.sh` checks that
each i18n file's figures MATCH its English source, but check 14 never reads the ja file, so the ja count
rots independently (a future driver bump that edits only the two English files leaves ja at 497 while the
tree has 498). The supervisor (16:37, F3 in the trio review) named this: "worth wiring the count into
i18n_tracks_lint."
**Design:** extend `i18n_tracks_lint.sh` (or add a check to `docs_counts_lint.sh`) to assert that the
ja VOCABULARY.md's driver-count figure equals the lint's own `real_drv` (the same expression check 14
uses: `ls tests/smoke/*.sh | grep -v -e /run.sh -e /_smoke.sh | wc -l`). Red-first: bump the tree's
driver count (add a dummy .sh), leave the ja file stale, confirm the lint goes red, remove the dummy.
Universe + floor in the lint's header.
**Rejected:** hardcoding 497 into a lint -- it rots on the next driver, exactly as the prose did. The
lint must re-measure.
**When:** the next time the smoke-tier driver count is bumped (the H4/H5 batches add drivers), or the day
the ja count is found stale.

<a id="def-355"></a>
#### the post-run record collection in `jichi-harness run` (sessions/attempts/lmstudio copy)
**Why:** M1057 (H4, the jichi-harness drive of 2026-10-23) builds `jichi-harness run` as the C89 port
of the drive's `run-lead.sh`. The reference's lines 181-195, after the child exits, copy the run's
jichi sessions, the workers' transcripts (`~/.jichi.d/attempts/*.jsonl`, M807), and the LM Studio server
logs into `runs/N/` so a run's records are self-contained. The port writes the `.started` marker
(behaviour 10) but does NOT collect: the driver's `home` is the mock's, not a real session store, and
the collection is a follow-up. It is named in the M1057 note's Deferred section (def-355) and is the
reason the run's records are currently stream/stderr/journal/telemetry/meta/config-copy/notes only.
**Design:** after the child exits (behaviour 12's `rc=` append), copy the tenant's home sessions and the
`~/.jichi.d/attempts` transcripts into `runs/N/sessions/` and `runs/N/attempts/` (the reference's
`cp -r` idiom, M2-neutral like the run's other git calls), and the LM Studio server log when one is
configured. Keep it read-only (a failed copy is a records gap the loop reports, not a run failure).
Red-first: a driver cell that plants a session file under the tenant home, runs a happy run, and asserts
it is present in `runs/N/` (and absent on the refusal path).
**Rejected:** collecting during the run (a race against jichi's own writes) or making a collection
failure fatal to the run's exit class (the reference copies best-effort after the child exits).
**When:** the handoff port (after M1057) needs the previous run's sessions to name the files it read; the collection
lands with it.

<a id="def-356"></a>
#### the per-tenant `apiKeyEnv` name in `jichi-harness run`
**Why:** M1057 (H4) builds `jichi-harness run` and reads `drive.leadModel`. The run delivers the key
under the FIXED name `JICHI_API_KEY` (the reference's env var, the M242 setup-wizard default), not a
per-tenant `apiKeyEnv`: the harness does not parse the tenant's jichi config for the key's NAME, so it
cannot read its `apiKeyEnv` -- it delivers under the reference's fixed convention, and jichi registers
that name via `apiKeyEnv` and scrubs it from its children.
**Design:** let each tenant name its `apiKeyEnv` (defaulting to `JICHI_API_KEY`) and deliver the key
under that name, keeping the run's fail-closed-before-write shape (behaviour 1).
**Rejected:** a per-tenant key name that the harness then also logs (it must stay off the records like
the fixed name does).
**When:** a drive with a tenant that expects a different `apiKeyEnv` than `JICHI_API_KEY` (a config
naming one) reaches the model with no key at the name it looks for; the fix lands with its own red-
first cell. (The `drive.leadModel` config-lookup half of this row was BUILT by M1057's lead-model
guard, the reference's run-lead.sh 49-52 behaviour -- an un-named `leadModel` is WARNED on the harness's
stderr and OMITTED from the spawn argv, the run proceeding on the config's routing -- with its own red-
first cell, R10 of tests/smoke/harness_run.sh.)

<a id="def-357"></a>
#### the gateway-up probe in `jichi-harness run` (parking a run while the endpoint is down)
**Why:** M1057 (H4) behaviour 13 keeps the inbox notes on a dead first call WITHOUT a gateway probe.
The reference's `gateway-up.sh --wait` parks a run while the model endpoint is down, BEFORE the inbox is
read, so a broken gateway does not start 13 runs that each die at 860 s. A probe is a network call the
harness should not make in `run` (the mockmodel driver has no gateway), and parking is the outer
loop's concern. It is named in the M1057 note's Deferred section (def-357) as behaviour 13's rejected
alternative.
**Design:** the probe lands with H5's loop (the drive that calls `run` in a sequence), not inside
`run` itself; the loop waits for the endpoint to answer before starting the next run, and behaviour 13's
move-on-success stays the loop's retention, not the run's.
**Rejected:** a probe inside `run` (a network call the one-run primitive should not make, and the
mockmodel driver cannot stand up a gateway to probe).
**When:** H5's loop is built (M1059, 2026-10-26) and does NOT take the probe: a probe in the loop
would double-park (the run keeps behaviour 13's move-on-success without a probe; a probe in the
loop AND behaviour 13 in the run would park twice). The probe lands when the run itself is
changed to park (a P3 decision) -- then the loop's quick-failure count and the probe's park
coexist, and behaviour 13's move-on-success moves to the loop's retention.

<a id="def-358"></a>
#### the `.gz` notes in `jichi-harness run`'s handoff
**Why:** the reference's `handoff.py` gunzips a previous run's compressed notes
(`open(name, 'rb').read().decode(...)` via the `gzip` branch, `handoff.py` 94-104), but the
harness has no compressor: `setup-dir.sh` and the loop never write a `.gz` note, so the branch
is dead in every drive this repository has run. The port (M1058, CHANGED 3) therefore treats a
`.gz` note as `(unreadable)` BY NAME -- the reference's own marker for an unreadable note --
never a silent skip, and the driver pins that (the `d.gz` cell of `tests/smoke/harness_run.sh`
check 12). This is also the CLAUDE.md "libcurl and nothing else" dependency boundary: a
gunzip branch would need zlib or a vendored inflater.
**Design:** when the drive's loop compresses old notes (a retention policy that has not been
written), either the harness gains a minimal inflate or the loop gunzips at snapshot time and
the port's by-name refusal stands until then. The refusal's text already names the class
(`(unreadable)`), so a future note that IS readable-gzip is not silently dropped.
**Rejected:** a zlib dependency (CLAUDE.md: libcurl and nothing else; the Makefile links only
`-lm -lcurl`); a vendored inflate (jichi vendors no third-party source).
**When:** a `.gz` note is ever produced by the drive's own records path.

<a id="def-359"></a>
#### the elided-arguments repair (the placeholder's `ticket` never read by the refusal)
**Why:** when jichi elides a tool call's arguments from context, it leaves a placeholder that
says "the complete arguments are preserved -- read the file named by 'ticket' to recover them"
(`src/chat/jc_compact.c`, the ticket path is the `elided-N.txt` output). If the model then
copies that placeholder back into a call, the refusal at `src/tools/jc_tool.c` (the
"they are not arguments" guard) never looks at the `ticket` field, so the two messages
contradict: one says the ticket is recoverable, the other says the arguments cannot be.
**Evidence:** the drive 2026-10-23-harness run 0015, calls 211-217 (the stream the harness
keeps in the run's records): six refusals of a copied placeholder before call 218 re-composed
the arguments by hand (the supervisor's SV:1037, counted in the stream; SV:1037b corrects the
first count of four to six).
**Design (two options, pick when built):** (a) when the copied placeholder carries a `ticket`,
the refusal names the file and says to read it and send its contents; or (b) the repair takes
the arguments from the ticket itself (the model's copy-back is a no-op) and telemetry records
`args_repair ok:1`.
**Rejected:** nothing -- this is a design row; both options are cheap and (b) removes the
model's work entirely.
**When:** the next time a run hits the refusal and the re-composition costs more than the
repair; it is not H4 work.

<a id="def-360"></a>
#### the records-branch push in `jichi-harness drive`
**Why:** M1059 (H5) ported the loop's `snapshot N (KIND)` exactly as far as the COMMIT: the
reference pushes the records branch to the remote (loop.sh 83-84, `git push ... origin HEAD:refs/heads/$RB`),
but in **Mode A** there is no separate records remote the harness owns -- the records clone's
origin is whatever `init` laid out, and a push from the loop would be a push the agent (the
same user) did not ask for. The commit is the record (it is the durable, the thing a drive
relies on); the push is the distribution. The port therefore NO-OPS the push by name: the loop
logs `snapshot N (KIND): the records push is not built (def-360; the commit is in dir/recs)`
and never pushes, never `--force`, never to master. It is named in the M1059 note (CHANGED 2)
and in `docs/HARNESS.md`'s `drive` section.
**Design:** the push lands when the drive has a records remote the operator names (a P3 or later
decision, not a P2 one) -- the same credential-helper the `mirror push` uses, the same guards
(no force, no master). The commit shape is already the reference's, so only the push is added.
**Rejected:** pushing the records to the TEAM's remote (the stand-in in the driver) -- the
records are a separate branch and a separate concern; mixing them would make the loop's
`mirror push` and the records push the same push, and the reference keeps them apart.
**When:** a drive that needs the records on a remote -- the rehearsal (the milestone after H5,
not yet in the ROADMAP) reads the records from the local clone, so it does not block on this.

<a id="def-361"></a>
#### the credential-helper classify_url keeps userinfo in the host and compares a percent-escaped path raw
**Why:** M1066 (the resumed drive, SV:0904.5, 2026-10-27; register only, pre-existing, LOW). A read
of `src/harness/jc_harness_cred.c` `classify_url` (:136-178) found two latent gaps the byte-exact
identity guard (M1061) turns into a REFUSAL: (1) `classify_url` copies everything between `://` and
the first `/` as the host, so `https://user@host/x` -> host `user@host`, while git presents the
helper `host=host` and `username=user` apart -- a configured remote carrying userinfo would be
refused (not that remote); (2) a percent-escape in the configured URL path is compared raw against
git DECODED `path=` (git decodes before presenting it to helpers), so a remote whose path contains
an escape would be refused too. Neither applies to THIS drive remote (no userinfo, no percent
escapes), so the guard answers correctly for the only remote it is driven against; the shape is
registered so a future drive with such a remote meets it as a known gap, not a mystery refusal.
Style residual (SV:0904.5): harness_mirror_push.sh cell 31 uses bare `grep` where H5 moved the
harness drivers to `$G` (`/usr/bin/grep`), the CLAUDE.md verify-with-the-gate-own-tool rule.
**Design:** strip userinfo in `classify_url` (the `@` before the first `/` or end); at the fix,
decode the configured path before the byte-exact compare (or assert the configured path is
percent-escape-free and refuse otherwise); keep the guard on git form.
**Rejected:** relaxing the byte-exact guard to a looser host match -- the guard whole value (M1061)
is that a DIFFERENT project on the SAME host is refused; a userinfo/escape in the configured URL
must be normalised, not the comparison loosened.
**When:** the next drive that names a remote whose URL carries userinfo or a percent-escaped path;
the style fix (`grep` -> `$G` in cell 31) can ride any later touch of that driver. Files:
`src/harness/jc_harness_cred.c`, `tests/smoke/harness_mirror_push.sh`.
