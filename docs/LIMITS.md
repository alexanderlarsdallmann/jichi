# Limits: every default limit in jichi, and where its number came from

> **Who this is for.** Anyone who reads, configures or changes jichi, and learners above all. A default limit is a
> number someone chose once and everyone else inherits. When the number is wrong, nothing looks broken: a stage
> "fails", a tool "times out", a list "is complete". This page says what each limit bounds, what happens when it
> fires, and, honestly, whether anyone ever measured it.
>
> **When and how it was made.** The census was taken on 2026-11-21 at `master` 0e301f54. The candidates were found
> in two independent ways, which the project's rule "audit the universe twice" asks for:
> - every `#define` whose name looks like a limit (328 lines);
> - every numeric config default, every literal deadline at a call site, and every limit-named assignment (327 lines).
>
> Five agents then classified all 655 lines by reading the source, read only. The supervisor checked their sources
> by hand. See [How the census was checked](#how-the-census-was-checked) for what was and was not verified. Every
> limit is listed in [`LIMITS_TABLE.md`](LIMITS_TABLE.md).
>
> **Updated 2026-11-23.** The counts and the census page still describe 0e301f54. What changed since is in
> [section 6](#6-since-the-census): the five checks of [section 4.4](#44-checks-that-failed-open-fixed-by-m1208)
> were fixed, and the re-pin port no longer passes `0`.

## 1. What a default limit is

A default limit is any number in the code that stops, cuts or refuses something unless a user sets otherwise: a
timeout, a deadline, a stall window, a grace period, a retry count, a size cap, a maximum count, a budget, a
threshold. The census sorts them into three kinds, using the project's own words (CLAUDE.md, "Caps versus fences"):

- **A fence** bounds the blast radius: a path fence, a read-only child, `--max-tool-calls`, a refusal. A fence that
  fires prevents damage. Fences stay on, even in measurement runs.
- **A cap** bounds duration or amount: a timeout, a stall window, an output cut, a retry budget. A cap that fires
  CHANGES the result. It kills, cuts, truncates or stops, and the result still looks like a result. Anecdote 64 puts
  it exactly: a cap "does not merely hide the answer, it manufactures a plausible different one."
- **A structural limit** is an internal buffer or array size. It has no visible effect until something exceeds
  it, and then it behaves like a cap, usually a silent one.

## 2. Five questions to ask of any limit

Ask them of jichi's limits, of your own, and of any library you call.

1. **What does it bound?** Name the thing in plain words: "a shell command's wall time", "the bytes of a tool's
   output the model sees".
2. **Is it a fence or a cap?** If it changes the result when it fires, it is a cap, and a cap must never sit on a run
   that measures anything.
3. **Where did the number come from?** A measurement (what, where, when, the distribution, the headroom chosen), an
   incident (which one), or nothing. "Reasonable", "generous" and "should be plenty" are not sources.
4. **What happens when it fires, and will you be told?** By name, in the result you read? Only on a terminal that may
   be gone? Or not at all?
5. **What do `0` and "absent" mean?** "No limit"? "The built-in default"? Something else? Check the code, not the name.
   [Section 4.1](#41-zero-means-at-least-five-different-things) shows why.

## 3. The rule

Approved by the operator on 2026-11-21 (CLAUDE.md, "Before you act", item 14; the decision is in
[`DECISIONS.md`](DECISIONS.md#dec-2026-11-21-default-limits)):

1. **Every default limit states its source at its definition.** The source is one of:
   - `measured:` what was measured, where, when, the distribution and the headroom chosen;
   - `incident:` the ANECDOTES or DECISIONS entry;
   - `unmeasured:` and the [`DEFERRED.md`](DEFERRED.md) row that will measure it.
2. **A measured default comes from the measured distribution of what it bounds**, with the headroom stated. It never
   comes from a distribution censored at the cap itself: the reasoning of [DECISIONS M959](DECISIONS.md#dec-m959).
3. **`0`, a negative value or an absent key never silently means a cap.** "No limit" is an explicit value. A
   default cap is a named constant that every call site passes visibly.
4. **A limit that fires says so**, where the reader looks afterwards: which limit, its value, and where the value came
   from (a config key or a constant). It is never a silent cut, and never a timeout dressed as a result.
5. **Fences stay on. Caps stay off for measurement runs.** A default cap is a measurement still owed, not a settled
   fact.
6. **A test of a limit includes a fixture that runs longer than any default it could inherit.** A fixture that
   finishes inside a cap cannot see the cap.
7. **A lint holds it** (`limits_lint`), not review alone. At first it only reports, with a floor at today's count.
   Once every limit is tagged, it fails the build.

## 4. What the census found

| | Limits | No source for the number | Reasoned only | Measured | Incident | Unclear |
|---|---|---|---|---|---|---|
| All default limits | 507 | 335 | 140 | 17 | 7 | 8 |
| Caps (they change the result when they fire) | 290 | 213 | 60 | 6 | 3 | 8 |
| Time limits (timeouts, deadlines, stalls, graces) | 113 | 80 | 19 | 4 | 2 | 8 |

Of the 290 caps, 106 change the result without saying so.

Read these counts with three cautions:
- Some limits are counted twice: a header constant and the config key that sets it were classified in different
  shards. The 6 sourced time rows hold such a pair, so the DISTINCT time limits with a written source number 5
  (section 4.2).
- "Reasoned" means a comment gives a reason without a measurement.
- "Unclear" means a source is claimed but not found (section 4.3).

### 4.1 Zero means at least five different things

Each line was read in the source at 0e301f54.

| Where | What `0` (or "not set") becomes |
|---|---|
| `jc_proc_capture()` (`src/util/jc_proc.c`, `JC_PROC_DEF_TIMEOUT` in `include/jc_proc.h`) | **a 30-second kill.** Any `timeout <= 0` |
| hooks' and user tools' `"timeout"` (`src/config/jc_config.c`, read with a default of `0`) | **30 seconds**, through the line above (measured: [section 5](#5-try-it-yourself)) |
| `jc_proc_reap_bounded()` (`src/util/jc_proc.c`) | **now + 10⁹ ms, about 11.6 days** ("a far-future bound") |
| `parallelTaskTimeout` (`src/tools/jc_tool_parallel.c`), `daemonWorkerTimeout` (`src/main.c`) | **300 seconds**, the built-in default; M325 measured working children at 300 to 462 s |
| `jc_config_cap()` (`src/config/jc_config.c`), the general size-cap helper | **the built-in value** |
| `jc_worker_over_deadline()` (`src/util/jc_workerpool.c`) | **never over the deadline: no limit** |
| an HTTP request's `timeout_secs` (`src/net/jc_http.c`) | **no total time limit** (no `CURLOPT_TIMEOUT` is set) |
| `runTimeout` (shell commands; `jc_config_run_timeout()`) | **no limit**, as [`CONFIG_TUTORIAL.md`](CONFIG_TUTORIAL.md) documents |
| the diff renderer's line limit (`src/util/jc_diff.c`) | **400 lines** (`DIFF_DEFAULT_MAX_LINES`) |

None of these is wrong in itself. Together they mean a reader cannot know what `0` does without reading each
function. On 2026-11-21 that cost the ported gate a 30-second kill under a comment that said "NO timeout"
([ANECDOTES 130](ANECDOTES.md)).

### 4.2 The time limits that have a source

Five distinct time limits have a measurement or an incident written down. Each source was read by hand:

| Limit | Value | Source |
|---|---|---|
| `timeouts.connect` / `JC_HTTP_CONNECT_TIMEOUT_DEFAULT` | 30 s | incident: at the old 10 s, "14% of model calls failed, all clustered at 10,003 ms" (M326v, [ROADMAP](ROADMAP.md)) |
| `timeouts.deadStream` (also the side calls' dead-stream fence) | 900 s | measured: "above the 540 s healthy max the drive measured" (M-L2; `include/jc_config.h`) |
| `parallelSupervise.noEditSec` / `JC_PAR_SUP_NO_EDIT_SEC` | 3600 s | measured: "60 min: 7 of 563 stalled" ([PARALLEL.md](PARALLEL.md)) |
| `LSP_DIAG_GRACE` | 2.0 s | measured: the server's real diagnostics came 0.6 s behind the empty set (M938; `src/lsp/jc_lsp.c`) |
| the lead run's verifier (`RUN_VERIFY_TIMEOUT`) | 30 min | incident: a looping test kept the verifier spinning; "a green suite takes seconds to minutes" |

The other 107 time-limit rows have no measurement or incident written down. Among them are `timeouts.stall` (30 s, which
[M959](DECISIONS.md#dec-m959) keeps on purpose and labels as a cap), the 30-second `jc_proc_capture` default, the
gate port's 600-second caps on helper commands, the 120-second git caps, and every workflow and grading deadline.

### 4.3 Claimed as measured, with no measurement recorded

Eight network side-call totals carry comments or a report calling them "measured" or "measured-working", but no
measurement is written down anywhere the census could find:

- posts, image and audio generation: 60 s each;
- transcription: 120 s;
- the model probes: 5, 6 and 6 s.

When one fires, the message can name the wrong limit: a reply still arriving reported as a dead stream, a probe cut
by a slow TLS handshake reported as the 30-second connect limit. The census found this by reading, not by running.
Handshakes of 9.9 to 11.7 s were measured on 2026-11-05 (M1144), longer than a 5- or 6-second probe.

### 4.4 Checks that failed open (fixed by M1208)

A safety check must fail CLOSED: when it cannot finish, it refuses. The census found five checks whose limits made
them fail OPEN. They were found by reading on 2026-11-21. The SECURITY batch (milestone M1208, on `master` since
2026-11-22) then proved each one by effect, with a test that failed first, and fixed it so that it now fails closed
([`CHANGELOG.md`](../CHANGELOG.md), "Batch SEC"). What the census found, and what happens now:

1. **The redirect private-address guard** (`redirect_hop_header`, `src/net/jc_http.c`) ignored a status line of 64
   bytes or more. The reason phrase in `HTTP/1.1 302 <reason>` is the server's choice, so a hostile server could keep
   the hop counter from advancing, and the guard against a redirect to an internal address would not apply.
   **Now** the counter reads the status code without copying the reason phrase, and an oversized line that yields
   no code counts as a hop.
2. **The privilege-launcher and kinetic-command checks** (`src/util/jc_priv.c`, `src/util/jc_kinetic.c`) gave up
   after 64 leading tokens and answered "nothing here": `sudo` behind 65 `VAR=x` assignments was not flagged.
   **Now** such a command is classed as suspicious and refused like `sudo`, and the kinetic check finds the hidden
   command word.
3. **Log redaction** (`src/util/jc_log.c`) registered at most 32 secrets; a 33rd was dropped and never scrubbed.
   **Now** the registry grows, and a registration it cannot afford is a logged refusal.
4. **The push's key scan and size check** (`src/harness/jc_harness_push.c`) read a `git rev-list --objects` listing
   cut at 8 MiB, and reported the cut as success, so objects past it were never checked. **Now** a cut listing
   refuses the push and names which listing overflowed.
5. **The parallel hold fence** (`src/tools/jc_tool_parallel.c`) compared only the first 16 new directories with its
   hold bounds, so a large 17th directory merged unchecked. **Now** every new directory is checked; the result still
   names 16 and counts the rest.

The order is the lesson: the census found these by READING, and the reading was not taken as proof. Each fix began
with a test that showed the open path by effect, and only then changed the code.

### 4.5 Silent caps worth knowing

A selection. [`LIMITS_TABLE.md`](LIMITS_TABLE.md) has all 106.
- **Read by hand:** the 30-second `jc_proc_capture` kill. Its callers report it as `rc=-2`, "the build failed" or
  "exit -2", never as "timed out".
- **Read by hand:** a hook killed by its timeout leaves no record in the run's telemetry, only a stderr line
  (measured, [section 5](#5-try-it-yourself)).
- **From the census, not re-checked by hand:**
  - `JC_PROV_MAX_CALLS` 32 drops any tool call at index 32 or above. Anthropic's index also counts text and thinking
    blocks, so fewer than 32 tool calls can reach it.
  - `JC_CONSTRAINT_MAX` 64: a 65th constraint is never enforced.
  - Custom-command `@file` text is cut at 32 KB with no marker.

### 4.6 Where the documentation and the code disagree

- **Read by hand:** the gate port's stage call is commented "NO timeout" and runs under the 30-second kill.
  `docs/reading/tazuna-08-gate-run.md` repeats "No timeout".
- **Read by hand:** [`HOOKS.md`](HOOKS.md) says a hook's timeout defaults to 30. True, but it does not say that `0`
  also means 30, while `runTimeout`'s `0` means no limit.
- **From the census, not re-checked by hand:**
  - `JC_CONTROL_LINE_MAX`'s comment says longer control requests are refused at 16 KiB; the socket reads 128 KiB for
    every request.
  - WEBSEARCH's `maxResults` is called a cap, but the model can ask for more.
  - `JC_DEFAULT_MAX_TOKENS` (4096) applies without a `contextLength`, which the config tutorial does not say.

## 5. Try it yourself

This takes about two minutes, needs no network and no key, and shows by effect what `0` means for a hook.

1. In a jichi checkout, build the agent and the test tools:
   ```sh
   make jichi smoke-tools
   ```
2. Run the script `docs/examples/limits/hook-timeout.sh` from the checkout's top directory. It starts a local mock
   model, runs three one-line jichi sessions in a throwaway directory under `/tmp`, and removes it:
   ```sh
   sh docs/examples/limits/hook-timeout.sh ./jichi tests/tools/mockmodel < /dev/null
   ```
3. You should see, with your own timings (this is the run of 2026-11-23):
   ```
   A (no "timeout" key) rc=0 wall=30s hook-telemetry: none
       stderr: [jichi warn] hook timed out and was killed
   B ("timeout": 0) rc=0 wall=31s hook-telemetry: none
       stderr: [jichi warn] hook timed out and was killed
   C ("timeout": 60) rc=0 wall=31s hook-telemetry: none
   ```
4. Read it with the five questions of section 2.
   - The hook sleeps 31 s. With no `timeout` and with `0` it is killed: `0` is not "no limit" here.
   - With `60` it finishes.
   - In all three runs the hook's fate appears only on stderr, not in telemetry.

**If it does not match.** `mockmodel did not start` means step 1 did not build `tests/tools/mockmodel`. An `rc=127`
means a path is wrong: pass `./jichi` and `tests/tools/mockmodel` from the checkout's top directory, as above. If
`A` finishes instead of being killed, the default changed. That is good news: read this page's history for the change
and update this section.

## 6. Since the census

The census describes `master` 0e301f54 (2026-11-21). By 2026-11-23:

- **Fixed:** the five checks of [section 4.4](#44-checks-that-failed-open-fixed-by-m1208) (M1208, the SECURITY batch,
  on `master` since 2026-11-22).
- **Fixed, with a new unmeasured number:** the re-pin port (`src/harness/jc_harness_repin.c`) no longer passes `0`
  to `jc_proc_capture`. Each of its children now runs under a named 600-second deadline, `RR_CHILD_DEADLINE`
  (M1203-i, on `master` since 2026-11-22), so a child that runs past 30 seconds, such as a `doctor --live` with a
  slow TLS handshake, is no longer killed. The 600 is itself a number without a measurement:
  [`DEFERRED.md` def-422](DEFERRED.md#def-422) covers it.
- **Still open:** the gate orchestrator's stage call and its land and re-pin commands (`src/harness/jc_hp_gate.c`)
  still pass `0` and so still run under the 30-second kill, under the comment "NO timeout". They do not run real
  batches yet: the harness-port drive's host still runs its gate script, which calls the ported `land` and `repin`
  verbs directly. An explicit "no deadline" value for `jc_proc_capture`, and a firing deadline that names itself,
  were decided on 2026-11-21 and are not built yet.
- **Still open:** `0` and an absent `timeout` still mean 30 seconds for hooks and user tools (measured again on
  2026-11-23, [section 5](#5-try-it-yourself)). The pages that document them now say so.
- **Not built yet:** `limits_lint` and the tagging of each limit's source (rule 7 in [section 3](#3-the-rule)).

## How the census was checked

- **The universe, twice.** Route A (limit-named `#define`s) and route B (config defaults, literal deadlines at call
  sites, limit-named assignments) overlap only partly. Both were classified, and each agent could add limits the
  lists missed (about 130 were added that way).
- **The classification was done by five agents reading the source**, and was not re-done by hand. So every row is a
  reading, not a measurement, and a row you find wrong is a defect in this page: report it.
- **Checked by hand on 2026-11-21:**
  - every time limit graded "measured" or "incident" (sources read);
  - a seeded random sample of ten "no source" time limits (three read: two have no rationale at all, one cites a
    design item but no measurement);
  - each item of 4.1, 4.2 and 4.4;
  - the two by-effect runs of section 5.
- **Checked again on 2026-11-23, for the update:**
  - section 5's script, run in its published form on the harness-port drive's branch at 5df3335b, with the result
    shown there;
  - the code behind section 6 (`RR_CHILD_DEADLINE`, the two `0L` calls in `jc_hp_gate.c`, `JC_PROC_DEF_TIMEOUT`),
    read in the source;
  - M1208's five fixes as [`CHANGELOG.md`](../CHANGELOG.md) and [`ROADMAP.md`](ROADMAP.md) record them: each with a
    test that failed first, and the batch landed through a green gate. Those tests were not re-run for this page.
- **Not checked by hand:** the remaining entries of the census page, and items marked "from the census" above.

## Read further

- [`LIMITS_TABLE.md`](LIMITS_TABLE.md): every one of the 507 limits, with what it bounds and where its number came
  from.
- [`ANECDOTES.md`](ANECDOTES.md), entry 130, "Zero meant thirty": the 30-second kill that started the census.
- [`DECISIONS.md`](DECISIONS.md#dec-2026-11-21-default-limits): the rule of section 3, with the alternatives that
  were rejected.
- [`DEFERRED.md`](DEFERRED.md#def-421), def-421 to def-423: the measurements still owed for the time limits.
- [`TEST_INTEGRITY.md`](TEST_INTEGRITY.md): why a fixture has to behave like the real peer, which here means running
  longer than any limit it could inherit.
- [`CONFIG_TUTORIAL.md`](CONFIG_TUTORIAL.md): the limits you can set yourself, and what `0` means for each.
