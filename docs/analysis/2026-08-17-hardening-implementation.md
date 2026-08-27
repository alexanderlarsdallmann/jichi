# Implementing the hardening audit: eight fixes, ten mistakes, and two real runs

*2026-08-17, immediately after
[`2026-08-17-source-hardening-audit.md`](2026-08-17-source-hardening-audit.md).
That document is the review; this one is what happened when it was implemented —
the design decisions with their rejected alternatives, **every mistake made along
the way**, and the validation against a real model on a real project.*

*Written for someone learning this work. The mistakes are the point: §3 is longer
than §2 deliberately, because the fixes are ordinary and the errors are where the
transferable lessons are. Eight of the ten were caught by a machine (the test suite,
a lint, a reverted-fix check); two needed a human to say "stop".*

*Read in two passes if it is long: §§1–5 are the first wave (H1, H2, H4, M4) and its
six mistakes; §6 is the second (H3, L4, M1, and the hygiene items) with four more and
a second validation run. §5 and §6f are the lesson lists.*

---

## 0. What landed

Nine commits on top of `6fd4231`. Every finding from the audit's §6 is implemented
except L3, which was analysed and deliberately left (§6d).

**First wave** — the four demonstrated defects:

| Finding | Fix | Test that proves it | Shown red first |
|---|---|---|---|
| **H1** provider key exfiltrated on a 3xx | `follow_redirects`, default 0; caps on every request; a diagnostic naming the target | `tests/smoke/provider_redirect.sh` (3 checks) + `test_http_describe_redirect` | yes — 3/3 and 2 unit failures |
| **H4** unbounded JSON recursion | `JC_JSON_MAX_DEPTH 256`, counted in `parse_value` | `test_json_depth_limit` + 2 fuzz corpus seeds | yes — 3 failures, no crash |
| **M4** unset `HOME` → state in `/tmp` | `getpwuid` step, uid-scoped 0700 fallback, `doctor` posture check | `tests/smoke/state_root.sh` (4 checks) | yes — 2/4 fail naming `/tmp` |
| **H2** children inherit journal/telemetry/socket | `jc_fd_cloexec`, `jc_pipe_cloexec` (12 sites), `jc_proc_child_close_fds` (8 sites) | `tests/smoke/child_fds.sh` (3 checks) + 2 new lints | yes — "leaked: 4 5 6 7" |

**Second wave** — the rest of §6:

| Finding | Fix | Test that proves it | Shown red first |
|---|---|---|---|
| **H3** control bytes reach the terminal | M363's rule extracted to `jc_ctrl_display_safe`; applied at the raw terminal writes | `output_escapes.sh` (4) + `tui_tool_escapes.sh` (3, under a PTY) + `test_ctrl_sanitize` | yes — 3 ESC + 2 BEL; OSC 52 in the PTY transcript |
| **L4** no TLS floor; no plaintext warning | `SSLVERSION` TLS 1.2, explicit verify options, a `doctor` check that stays quiet for loopback | `transport_posture.sh` (4) | yes — 2/4 |
| **M1** no hardening flags at all | probed `HARDEN=1` block, `OPT` knob to make `_FORTIFY_SOURCE` reachable, `make info` posture | `harden_flags_lint.sh` (6, incl. a floor) | yes — the floor, and the dropped-`-Wl` bug |
| **M3** unchecked size arithmetic | two guards in `jc_sb_reserve`; `JC_LSP_MAX_BODY` | `test_sb_reserve_bounds`, `test_lsp_frame_bounds` | yes — 2 and 1 |
| **M2** narrow warning set | eleven flags, each measured at zero first | the build itself, at `-Werror` | n/a (flags verified to fire) |
| **L1/L2** weak ids; multipart injection | `/dev/urandom` with a documented fallback; one escaper for all header parameters | `test_multipart_header_injection` | yes — 5 |

Gate after everything: **12,407 checks / 0 failures**; `posix_utils_lint` 14/14;
`smoke_lint` 15/15; every `*_lint` driver green. The first wave was additionally
verified under `SAN=1` (ASan + UBSan + float-cast-overflow, `-fno-sanitize-recover`).

**Everything in the audit's §6 is now implemented.** The second wave — H3, L4, M1
and the §3 hygiene items — is recorded in §6 below, with its own mistakes and its
own validation run. Only one item was analysed and deliberately left: L3, the path
fence's check-then-open window, whose reasoning is in `DEFERRED.md` and summarised
in §6d.

---

## 1. How to read a fix in this milestone

Every one of the four has the same three-part shape, and it is worth naming
because it is the shape most security fixes have:

1. **A guard**, usually a handful of lines. `follow_redirects ? 1L : 0L`. A depth
   counter. An `fcntl`. The guard is never the hard part.
2. **A test that fails without the guard.** This is most of the work, and §3 shows
   it going wrong three times.
3. **A lint or a structural rule**, so the guard cannot be forgotten at the *next*
   site. This project's `TEST_INTEGRITY.md` argues for preferring a lint to an
   audit, and H2 is the case that proves it: the same defect existed at eight exec
   sites, which means one more will be added eventually.

If you only do (1), you have fixed an instance. If you do (1) and (2), you have
fixed a bug. All three fixes a *class*.

---

## 2. The design decisions, with what was rejected

### H1 — a new field rather than reusing an existing one

`CURLOPT_FOLLOWLOCATION` was unconditional; M131's caps sat behind
`req->block_private_addrs`. The quickest fix would have been to gate redirects on
that same flag — one line, no header change.

**Rejected**, because `block_private_addrs` means "this URL came from the model,
check the resolved address". Redirect-following is a different question ("is a
3xx expected here?"), and the two happen to coincide today only because
`fetch_url` is the sole caller of both. Conflating them would make the next
caller inherit an answer to a question it never asked. A separate
`follow_redirects` field costs a header entry and says what it means.

**Rejected: stripping credentials per hop instead of not following.** This is
what libcurl does for `Authorization`, so it looks like the natural fix. It
requires jichi to compare scheme+host+port itself, to know which of its headers
are credentials — including any the operator added via
`requestOptions.headers`, which jichi *cannot* classify — and to be right on
every hop. Strictly more code, defending a capability with no use case: an API
endpoint has no legitimate reason to redirect.

**The decision that mattered more than either**: enumerating all 20 request sites
before changing the default. Two of them — `jc_docs.c` (a documentation URL) and
`jc_refs.c` (an RSS feed) — are credential-free *content* fetches whose targets
redirect constantly (`http`→`https`, `/latest`→`/v1.2`, trailing slashes). Worse,
both check `status >= 400`, so an unfollowed `302` would not have errored: it
would have cached an **empty page** and an **empty feed**. A hardening change that
silently breaks documentation indexing is not a hardening change. Nothing but
reading all twenty sites would have found those two.

### H4 — the counter goes in the caller, not the callees

`parse_object` and `parse_array` each have five return points. Incrementing at
entry and decrementing before each `return` is five chances to leak the counter,
and a sixth when someone adds a return.

`parse_value` is the *only* recursion in the file — both containers reach each
other only through it — so the guard goes there: one increment, one decrement,
one place to be right.

**Also decided: `print_value` gets no clamp**, and the file now says why instead
of growing a second guard. Nothing can hand it a deep tree: printed trees are
either built by the construction API at a nesting the source fixes, or parsed,
and the parser now refuses past 256. A clamp there would buy nothing and could
refuse legitimate output — the worse failure. That reasoning is written into the
comment so the next reader does not have to redo it, along with the condition
that would invalidate it.

**The number is 256** because real provider, MCP and LSP payloads nest under
about 12. Two orders of magnitude of headroom means no legitimate document is
refused, which is the property that makes a limit safe to ship.

### M4 — degrade loudly rather than exit

The audit recommended "fail loudly on unset `HOME`". Implemented as a warning
plus a `doctor` check, **not** an exit.

A getter that kills the process is a worse contract than one that degrades — 49
call sites reach `jc_home_dir()`, at arbitrary points, and any of them could now
terminate the program. And a hard exit would break a deployment that works today.
The loudness went where it matters instead: `doctor` carries a "state root" check
that is a WARN interactively and a **FAIL** under `--unattended`, so a loop
supervisor gating on the exit code stops.

**The three-step ladder** matters more than the loudness. `HOME`, then
`getpwuid(getuid())->pw_dir` — ask the system rather than guessing — then a
uid-scoped `/tmp/jichi-<uid>` created `0700`, and if *that* is not a directory we
own with no group/other bits, a pid-scoped one that cannot have been pre-planted.
Step 2 resolves every realistic case, which is what demotes step 3 from a policy
to a backstop. Step 3's final branch trades persistence — already lost in an
environment this broken — against ever writing a key into a directory somebody
else controls.

### H2 — two layers, because they fail differently

- `jc_fd_cloexec()` at creation is the **precise** fix. It scales: a descriptor
  marked here is dropped by every exec, including ones added later. It uses
  `fcntl(F_SETFD)` rather than `O_CLOEXEC`, which is POSIX-2008 and unavailable
  under this tree's `-D_POSIX_C_SOURCE=200112L` — `INSTALL.md` already lists
  `O_CLOEXEC` among the features jichi does not require, so using it would have
  quietly narrowed the platform matrix.
- `jc_pipe_cloexec()` replacing all twelve bare `pipe()` calls. The subtlety
  worth learning: **`dup2()` does not copy the close-on-exec flag**, so a child
  that installs an end as its stdio keeps working while the original
  high-numbered end is dropped by exec. That is exactly the split you want, and
  it is why marking both ends in the parent is safe.
- `jc_proc_child_close_fds()` at all eight exec sites is the **backstop** —
  everything above stderr, closed after the `dup2`s. Given that the defect
  existed at eight sites, "someone adds a ninth and forgets" is the failure mode
  with the track record.

**Ordering decision**: the new call goes *before* `jc_proc_child_sigreset()`, not
after. `posix_utils_lint.sh` check 6 requires `sigreset` on the line immediately
preceding `exec`, and its comment says explicitly that this is so "nothing can be
inserted between them later". Inserting before preserves that invariant and left
the existing lint untouched. Reading the lint before editing the code it guards
is cheaper than discovering the constraint from a red build.

**Deliberately not fixed, and recorded rather than papered over**: a command run
without a `timeout` goes through `jc_proc_popen`, whose fork happens inside libc,
so no jichi code runs between fork and exec and the close-range backstop
structurally cannot reach it. One pipe pair still arrives there — **libcurl's**,
created without `O_CLOEXEC` where jichi has no hook. It is not a sink, a socket or
a secret. `DEFERRED.md` carries the row and names the retirement path (route tool
execution through jichi's own fork/exec unconditionally — a behaviour change with
its own milestone). `child_fds.sh` therefore asserts the *total* on the fork/exec
path and the two named sinks on both, and says so in its header, so a reader knows
what is and is not guaranteed.

---

## 3. The six mistakes

Read this section before §2 if you are new. The fixes above are ordinary
engineering; these are the things that actually cost time, and every one of them
is a pattern rather than a one-off.

### Mistake 1 — I added a cache nobody asked for, and it segfaulted the suite

Rewriting `jc_home_dir()`, I resolved the answer once into a `static` buffer,
reasoning in the comment that "49 call sites should not each re-derive it". It
built clean. Then `make test`:

```
[jichi error] http: URL using bad/illegal format or missing URL
make: *** [Makefile:473: test] Segmentation fault (core dumped)
```

Backtrace: `__strcmp_avx2` ← `test_cached_load` ← `test_index`. And
`tests/test_index.c:178`:

```c
setenv("HOME", jc_test_tmp("jichi_home_test"), 1);
```

The test **mutates `HOME` at runtime** and expects the next `jc_home_dir()` to see
it. With the cache, the planted index cache was never found and a NULL reached
`strcmp`.

**The lessons, in order of how much they generalise.**

1. **A getter that reads mutable global state must not memoise it.** `HOME` is not
   a constant; it is a variable in a mutable environment. `jc_shell_path()` in the
   same file *does* cache, and correctly — it caches the result of probing the
   filesystem for `/bin/sh`, which no caller changes. The distinction is not
   "expensive vs cheap", it is "does anything change the input".
2. **The cache was not part of the fix.** It was tidying, bundled into a security
   change. Bundling is how a small correct change acquires an unrelated
   regression, and it is why the fix and the tidying should have been two commits
   — at which point `git bisect` would have named the tidying in one step.
3. **Run the whole gate after touching a function 49 places call.** I had built
   `jichi` and hand-tested `doctor`, which passed. The unit suite found it in one
   run. The cheap check I skipped was the one that mattered.

The cache is gone; the common path returns the `environ` pointer exactly as
before, and only the one-time notice keeps state. The comment now records the
segfault, because the next person will be tempted the same way for the same
plausible reason.

### Mistake 2 — I assumed a mechanism instead of reading it

`CLAUDE.md` says `doctor --unattended` "escalates posture WARNs to FAILs so a loop
supervisor can gate on the exit code". I wrote my new check as a plain
`JC_DOC_WARN` and a comment claiming `--unattended` would escalate it.

It does not. There is no blanket rule. Escalation is written out per check:

```c
jc_doctor_add(&d, unattended ? JC_DOC_FAIL : JC_DOC_WARN, ...)
```

So my check would have been a WARN in both modes: **a gate that gates nothing**,
with a comment asserting the opposite. Caught by running
`doctor --unattended` and seeing exit 0.

**The lesson.** A documentation sentence describes intent; it is not the
mechanism. "Escalates posture WARNs" was true of the checks that opt in, and I
read it as a property of the system. When your change depends on a behaviour,
find the code that implements it — and then run it, which is what actually caught
this.

There is a second-order lesson worth naming: my *comment* asserting automatic
escalation would have outlived the bug and misled the next reader. A wrong comment
next to correct code is a trap; a wrong comment next to broken code is two.

### Mistake 3 — a test that asserted on text that was never there. Twice.

`child_fds.sh`, first cut: run the agent, grep **jichi's stdout** for the fd
listing. It passed. Then I neutered both layers of the fix and ran it again — and
it *still passed*, because the tool result never appears on stdout. It travels
back in the **next request body**. I was grepping for text that was never there,
so the check could only ever pass.

Second cut: read the captured request bodies (`mockmodel` records them for exactly
this reason) and pull bare fd numbers out with a regex. Now it failed correctly
with the fix reverted — but the failure read:

```
not ok 1 - descriptors leaked to the model's shell: 17 4 5 6 7
```

There is no fd 17. The `17` came from an unrelated numeric field elsewhere in the
JSON. **A matcher that can false-positive can also false-negative**, so that
number was a warning about the whole approach, not a cosmetic blemish.

Third cut: have each probe command **redirect** its output to a file the driver
reads. Precise, no JSON parsing — and a redirect, unlike a pipeline, adds no
descriptor of its own.

**The lessons.**

1. **A test that has never been observed failing has never been observed
   working.** This project states that rule in `CONTRIBUTING.md`, and I would have
   shipped a vacuous check without it. Reverting the fix is not a formality; it is
   the only thing that distinguishes a test from a comment.
2. **Assert on the narrowest artifact that carries the fact.** The fd listing is a
   fact about a file; parsing it out of a 40 KB JSON request body added a whole
   class of failure that had nothing to do with the thing under test.
3. **A sloppy match is a bug in the test, not a detail.** The `17` was the test
   telling me it did not know what it was looking at.

### Mistake 4 — my new names broke an existing lint, and the lint was wrong too

After adding `jc_pipe_cloexec()`, `posix_utils_lint.sh` check 6 went red:

```
not ok 6 - exec without jc_proc_child_sigreset(): src/chat/jc_app.c(1/2) ...
```

The lint counts exec sites with `grep -c "exec[a-z]*("`. And
`jc_pipe_cloexec(` contains **`exec(`** — "clo·**exec(**". Every call to my new
helper registered as an unguarded exec site.

The tempting fix is to rename the helper. The right fix was to tighten the lint:
it now requires `exec` not preceded by a letter or underscore, which is the same
word-boundary trick **check 9 already used for `popen`**. The matcher had always
been too loose; it had simply never collided.

Then — and this is the part that is easy to skip — I checked that the tightened
matcher still *matches something*. A lint made narrower can silently start
checking nothing, which is worse than the red build it replaced. Removing one
`jc_proc_child_close_fds()` call:

```
not ok 8 - exec without jc_proc_child_close_fds(): src/chat/jc_bg.c(0/1)
```

`0/1` — one exec site found, zero guarded. The floor holds.

**The lesson.** When your change breaks a lint, there are three possibilities and
only one of them is "my change is wrong": the lint may be right, the lint may be
too loose, or the lint may be too strict. Diagnose before you work around. And
after tightening any matcher, prove it still fires — this project's
`TEST_INTEGRITY.md` calls that "putting a floor under the ground-truth
extraction", and it exists because a lint that checks nothing passes forever.

### Mistake 5 — three wrong hypotheses, chased by reasoning instead of measuring

After the H2 fix, the journal, telemetry and provider socket were gone from the
child — but two descriptors, `5` and `6`, both ends of one pipe, remained. I
theorised, in order:

1. **"It is the shell's own pipeline pipe."** My probe was `ls … | paste`, and a
   shell does create a pipe for a pipeline. Plausible, and a real behaviour — but
   testing `/bin/sh -c` directly showed only fds 0–3, so not the answer here.
2. **"It is glibc's `popen` clearing close-on-exec on the parent's end."** Also a
   real behaviour, and I confirmed it in a trace:
   `pipe2([6,7], O_CLOEXEC)` followed by `fcntl(6, F_SETFD, 0)`. I wrote a fix for
   it (re-arming the flag in `jc_proc_popen`, which is worth keeping). The
   descriptors were still there.
3. **"It is one of jichi's own pipes I missed."** All twelve were wrapped;
   `grep` confirmed no bare `pipe()` remained.

The answer came from `strace`, not from thinking: an unmarked `pipe2(…, 0)` with
no `fcntl` after it, in a position no jichi code accounts for — **libcurl's**.

**The lessons.**

1. **Two of the three hypotheses were true statements about the world that were
   irrelevant to the case.** That is the characteristic failure mode of debugging
   by reasoning: explanations that fit are cheap, and fitting is not the same as
   being the cause. The audit document I had just written says exactly this about
   M471 ("an explanation's fit to the case that produced it says nothing about its
   reach") and I did it anyway, within the hour.
2. **The measurement was available the whole time.** One `strace -e trace=pipe2,fcntl`
   would have answered it before hypothesis 1.
3. **Instrument the boundary, not the theory.** I kept asking "what could create
   this pipe?" when the answerable question was "what *did*?".

### Mistake 6 — I built harnesses instead of driving the product

This one was not caught by a machine. The user stopped me:

> Stop. You are building more python scripts than running jichi, and checking what
> it does.

And, later, the constructive form of the same correction: drive jichi in headless
agent mode on a real project and check whether the hardening bears fruit.

Both were right, and the diagnosis is precise. I had written five throwaway Python
servers to observe descriptor tables, when the product under test is an agent that
can be *asked* to report its own environment. §4 is that run: it took one command,
produced better evidence than any of the harnesses, and — because it used the real
HRZ gateway over real HTTPS — validated things no mock could (that the redirect
change does not break a real provider, that the depth guard does not reject real
model payloads).

**The lessons.**

1. **A synthetic harness answers the question you encoded; the real system answers
   the question you asked.** My mocks all sent `Connection: close`, which is why
   the provider socket was absent from my first fd measurement and I nearly
   concluded it was not inherited. The real gateway uses keep-alive.
2. **When the thing you are hardening is a tool, use the tool.** The evidence in
   §4 is stronger *because* it came through the product's own telemetry, which a
   reader can reproduce with one command instead of trusting a script I wrote.
3. **Notice the ratio.** Time spent building instruments versus time spent
   observing the system is a signal, and mine had gone wrong well before I was
   told. That is a habit to watch for, not a one-time slip. This project's
   `docs/analysis/2026-08-16-instruments-that-lie.md` and
   `2026-08-17-instruments-not-systems.md` are apparently about the same failure,
   which suggests it is a house pattern rather than my invention.

---

## 4. The real run: does the hardening bear fruit?

Driven headless against the **real HRZ gateway** (`https://api.hrz.uni-giessen.de/v1`,
model `jlu/qwen3-coder-next`) on the **zigodot** project, with `--auto`, a run
journal and `--log-level full` telemetry. Reproducible with the commands in this
section.

### 4a. Nothing broke

The first thing to establish is that four security changes did not damage the
product. Via `jichi telemetry`:

```
events=6 turns=1 retries=0 routes=0 compacts=0 errors=0 (timeouts=0)
Models:
  coder     calls=2 err=0  in=36456 out=123  lat_ms mean=1199 max=1212
Tools:
  run_terminal_command   calls=2 ok=2/2 (100%)  dur_ms mean=65.4
```

Two live model calls, **zero errors, zero retries, zero timeouts**, tools 2/2.
Specifically:

- **H1 did not break the provider path.** Real HTTPS requests to a real gateway
  succeed; the endpoint does not 3xx, so not following costs nothing.
- **H4 does not reject real payloads.** Two real model responses, including
  tool-call arguments, parsed clean under the 256 limit.
- **The journal is intact and machine-readable** by jichi's own reader:

```
RUN                    WHEN        OUTCOME   TOKENS  TOOLS  VERIFY
71346176-e2e6-4412-... 08-17 22:25 ok         69.2k      4       -
```

An incidental observation, and a good sign about the product rather than about
this milestone: on one run I phrased the task with "Do not edit any file", and
jichi's constraint inference read that, enforced a session read-only constraint,
and correctly refused a shell redirect. It reported exactly why. That is the fence
working without being asked.

### 4b. The descriptors, from the model's own mouth

Asked to report `/proc/self/fd`, the real agent's shell returned:

```
0 -> /dev/null
1 -> pipe:[705999]
2 -> pipe:[705999]
3 -> /proc/219418/fd        (ls's own handle on the directory it is reading)
5 -> pipe:[672407]
6 -> pipe:[672407]
```

**Absent: the run journal, the telemetry sink, and the provider socket** — all
three open and in active use at that moment (telemetry at `full` was being
written; a keep-alive TLS connection to the gateway was live). Before this
milestone all three were in that listing, at fds 3, 4 and 7.

Present: fds 5/6, the documented libcurl residual.

### 4c. The tamper primitive, retried for real

The audit's headline demonstration was a one-line `echo … >&3` forging a record
into the run journal. Asked to probe each descriptor for writability, the real
agent reported:

| fd | Before this milestone | Real run, after |
|---|---|---|
| 3 | run journal, **WRITABLE** — forged record landed | **closed** |
| 4 | telemetry sink, **WRITABLE** | **closed** |
| 5 | pipe | closed |
| 6 | pipe | writable (libcurl residual, DEFERRED.md) |
| 7 | provider socket, **read/write** | **closed** |

And the journal itself afterwards: **4 records, all parsing as JSON, zero forged
lines.**

One honest wrinkle, because it would otherwise look like a finding.
`grep -c TAMPER` on the *telemetry* file returns 2, not 0. Those are not
injections: at `--log-level full` telemetry records the prompt and the tool
arguments, and the probe command contained the string `TAMPER-ATTEMPT`. Both
matches sit inside well-formed records, in the `prompt` and `args`/`args_full`
fields, and the file has **zero malformed lines**. That is telemetry doing its
job. Checking *where* the string was, rather than *that* it was there, is the
difference between a report and a scare — and it is the same discipline as
Mistake 3.

### 4d. Reproducing it

```sh
# from the project root, with a config naming a live model
jichi --config <cfg> --no-session --auto \
      --journal /tmp/j.jsonl --log /tmp/t.jsonl --log-level full \
      --max-tool-calls 8 \
      -p 'Use run_terminal_command to run: ls -l /proc/self/fd. Report the output verbatim.'
jichi telemetry /tmp/t.jsonl     # the measurement
jichi runs /tmp                  # the run's triage row
```

---

## 5. What a junior should take from this milestone

1. **The guard is the easy part.** Four security fixes, all of them a handful of
   lines. The work was the twenty request sites read before changing a default,
   the three attempts at one test, and the lint that had to be corrected before it
   could be extended.
2. **Prove the test red.** Two of my four tests passed while the fix was reverted.
   Neither would have been caught by review, because both *looked* right.
3. **Do not bundle tidying into a security change.** The one regression I shipped
   into the suite was a cache I added for neatness, and it segfaulted.
4. **Verify mechanisms, do not infer them from prose.** "`--unattended` escalates
   WARNs" was intent, not implementation.
5. **When your change trips a lint, diagnose the lint.** Mine was too loose; the
   collision was a symptom, not the disease. And after narrowing any matcher,
   prove it still fires.
6. **Measure the boundary; do not reason about it.** Three plausible hypotheses,
   two of them true-but-irrelevant, one `strace` away from the answer.
7. **Use the product.** The most convincing evidence in this document came from
   asking the agent to report its own descriptors, over one real gateway request —
   not from any of the five mock servers that preceded it.
8. **State the residual.** One descriptor still reaches `popen`'d children. It is
   in `DEFERRED.md` with its retirement path, and the driver's header says what it
   does not assert. A stated gap is a known risk; an unstated one is a surprise
   for whoever comes next.

---

## 6. The second wave: H3, L4, M1, and the hygiene items

Written after the first four landed, and it changed two of my beliefs about how to
test this kind of work.

### 6a. H3 — the output-side control-byte strip

M363 had already decided the rule (*strip C0 except LF and TAB, plus DEL*) and
written down why, in the words "output-side paste injection, the twin of the attack
bracketed paste exists to stop". It applied it at the **paste** chokepoint. So the
work was not to invent a rule but to **extract** it: the condition moved out of
`jc_paste_splice` into `jc_ctrl_display_safe`, and both sides now call it. One rule,
several consequences — the M326e shape.

Three decisions worth the words:

1. **Where.** For the TUI it goes at the *top* of `cb_text`, not at the writes,
   because the markdown renderer **inserts jichi's own SGR colour**: a strip after
   it would erase jichi's escapes along with the model's. Stripping the delta on the
   way in leaves all three write paths clean and jichi's own output untouched.
2. **Not in the JSON paths.** cJSON escapes a control byte to `\u001b`, so it is
   already inert there, and stripping would cost a machine consumer fidelity about
   what actually arrived. A check pins that exemption so a future "strip everywhere"
   change is noticed rather than silently shipped.
3. **Two drivers, because there are two paths.** Model text reaches the terminal
   through the front-end writer; a **tool result** reaches it through
   `cb_tool_result`, and that needs no model cooperation at all — a file in the repo
   containing OSC 52, shown by `read_file`, is enough, which puts it in M300's
   untrusted-content class. Headless never printed tool output (`(void)result`), so
   the second path is TUI-only and needed a PTY driver. The PTY one asserts on the
   **OSC 52 byte sequence**, not on "no ESC in the transcript": the TUI emits its own
   colour constantly, so a blanket check would be red by construction and would have
   to be weakened until it checked nothing.

### 6b. L4 — the transport, and the check that must stay quiet

`CURLOPT_SSLVERSION` (TLS 1.2 floor), `SSL_VERIFYPEER` and `SSL_VERIFYHOST` are now
stated in the source. libcurl's defaults were already correct, so this changed no
behaviour — the point is that H1 had just been a lesson about being saved by a
libcurl default this project neither requests nor tests, on a matrix reaching back
to libcurl 7.19.4 where the floor is TLS 1.0. TLS 1.2 and not 1.3 deliberately: 1.3
needs libcurl 7.52 plus a backend that has it, and refusing to connect on an older
row would be a portability regression wearing hardening's clothes.

The harder half was the new `doctor` check for a plaintext `apiBase`. **Loopback had
to stay silent.** `http://127.0.0.1` is the documented, normal shape for a local
model; there is no network to sniff, and a check that warned about it would train
operators to ignore the check — which is worse than not having one. It reuses
`jc_net_host_is_blocked`, already pure and unit-tested, rather than re-deciding what
"local" means. Check 2 of the driver asserts the silence as hard as check 3 asserts
the warning.

### 6c. M1 — the flags, and a lint that was vacuous twice

The measurement that justifies the whole item, on a toolchain with the distro's
defaults countered — the shape of a musl, bionic or hand-built gcc, four of the five
libcs in `PLATFORMS.md`:

| | canary | PIE | RELRO | BIND_NOW |
|---|---|---|---|---|
| defaults countered, no flags | 0 | 0 | 0 | 0 |
| defaults countered, our flags | 2 | 1 | 1 | 1 |

So the previous state was hardened on the developer's box, unknown everywhere else,
measured nowhere. `HARDEN=1` by default, every flag probed one at a time (compile
**and** link, since a `-Wl,` flag needs the link), so a toolchain without
`-fstack-clash-protection` is not failed for lacking it.

`_FORTIFY_SOURCE` got a *condition* rather than a flag, because it is inert without
optimization and the default build passes no `-O`. A new `OPT` knob — empty by
default, so today's behaviour is byte-identical — makes it reachable:
`make OPT=-O2` yields `__memcpy_chk`, `__sprintf_chk`, `__strncat_chk`,
`__realpath_chk` and six more, which is precisely the nine `strcpy` and three
`sprintf` sites the audit flagged.

**The lint took three tries, and the second and third failures are the lesson.**

1. First design checked a fixed list of mitigations. Rejected before shipping: it
   would fail a legitimate row whose linker has no `-z relro`, and a lint that fails
   honest platforms gets weakened until it checks nothing. It compares **ask versus
   got** instead, reading the ask from `make info`.
2. That failure mode is not hypothetical — it happened here. `comma := ,` was defined
   *after* the `:=` that used it, so every `-Wl,` probe expanded to a bare `-Wl`,
   failed, and was silently dropped. `make info` printing `-pie` alone was the only
   symptom.
3. Then I tested the lint by dropping `HARDENLDFLAGS` from the link line — and all
   four checks **still passed**, because Ubuntu supplied the mitigations anyway. The
   lint was **vacuous on this project's own dev box.** Check 5 is the floor: it
   counters the toolchain's defaults and proves the selected flags are what produce
   canary/PIE/RELRO. If it goes red, checks 1–4 are decoration, and its failure
   message says exactly that.

And then `posix_utils_lint` check 11 caught **me**: `sed 's/…\(A\|B\)…'` in the new
driver uses GNU `\|` alternation, which BSD sed reads as a literal pipe. The pattern
would have matched nothing, `asked` would have been empty, every `want` false — and
the lint would have passed vacuously on exactly the platforms it exists for. That is
the fifth member of the family after `grep -P`, `\|`, `\b` and `\xNN`
(M461/M466/M471), typed once again by someone who had just read those rows.

### 6d. The hygiene items, and two accidents mistaken for guards

`jc_sb_reserve` got its two overflow checks; `jc_lsp_framer_pop` got
`JC_LSP_MAX_BODY`. The honest part is *why* the LSP one matters, because I had
already tested the hostile headers and found no memory corruption: the reasons were
that `malloc` fails first, and that `SIZE_MAX` happens to collide with
`find_content_length`'s own `(jc_size)-1` error sentinel. **Two accidents, not two
guards** — either of which a refactor removes. The live defect was the missing cap:
`Content-Length: 4000000000` made the framer buffer without limit toward a body that
never arrives.

`jc_uuid` reads `/dev/urandom` with `rand()` kept as a documented fallback — not
because any consumer is an auth token (none is), but because the multipart boundary
is a delimiter whose payload is a model-chosen file that is never checked for
containing it.

`jc_multipart` escapes its header parameter values, and **my first test asserted the
wrong thing.** I wrote `strstr(body, "X-Injected") == NULL` and it failed against the
*working* fix — because stripping the CRLF leaves the attacker's text as inert
content inside the quoted parameter, so the substring is still there. The substring
was never the danger; the **structure** was. The assertions now look for a header at
the start of a line and for a boundary delimiter. Asserting on the payload instead of
the grammar is how a security test measures the wrong thing in both directions at
once.

L3, the path-fence TOCTOU, is the one item analysed and left. The attacker who could
win the race is a concurrent process as the same user — in practice the model's own
background shell — and a model with shell access does not need to race the fence: it
can write the file directly. The fence is the only door in exactly one configuration,
`--edit-scope --strict-scope`, which forbids the shell, and there no concurrent
attacker exists. Reachable only where it does not matter; closed where it would.
`DEFERRED.md` carries it with its revisit condition.

### 6e. The second validation run

Driven again against the **live HRZ gateway** on **zigodot**, hardened binary
(RELRO, BIND_NOW, PIE, canary, IBT/SHSTK, NX all verified on the ELF), with a run
journal and `--log-level full`.

**H2, from the model's own probe of each descriptor:**

| fd | Before M472 | This run |
|---|---|---|
| 3 | run journal, **WRITABLE** — a forged record landed | **closed** |
| 4 | telemetry sink, **WRITABLE** | **closed** |
| 5 | pipe | closed |
| 6 | pipe | writable (the libcurl residual, `DEFERRED.md`) |
| 7 | provider socket, **read/write** | **closed** |

Journal afterwards: **6 well-formed records, 0 injected lines.** Both sinks were
open and in use at that moment, and a keep-alive TLS connection to the gateway was
live.

**H3, and this is the result worth the whole section.** Asked to reproduce a tainted
file's bytes in its own reply, the real model returned text containing **two raw ESC
bytes** — telemetry records what arrived, verbatim:

```
'line one is harmless\n\x1b]52;c;cHduZWQ=\x07CLIPBOARD-ATTACK-ABOVE\n\x1b[2Kline three\n'
```

That is a live OSC 52 clipboard write and an `ESC[2K` line-erase, produced by a real
model on a real project. **jichi wrote 0 raw ESC bytes to stdout**, and the text
survived: `]52;c;cHduZWQ=` is present as printable characters with no ESC to arm it.
Note also that telemetry kept the unstripped bytes — which is the design, stated in
the code: the analyst gets fidelity, the terminal gets safety.

**And nothing regressed.** `jichi telemetry`: 4 model calls, **0 errors, 0 retries,
0 timeouts, 0 compactions**, tools 3/3, mean latency 984 ms.

### 6f. What the second wave adds to §5's lessons

9. **A test can assert on the payload when the danger is the grammar.** The
   multipart case: the injected text is still *in* the body after the fix, inert;
   only its structure is gone. If your assertion would fail against a working fix,
   the assertion is describing the wrong property.
10. **A lint can be vacuous on the machine you wrote it on.** Mine passed with the
    flags deliberately dropped, because the distro supplied them. Ask what would
    make the check fail, then arrange for that condition and watch it fail — on
    *this* box, not in principle.
11. **`make clean` deletes the test tools, and `make jichi` does not restore them.**
    Two lints went red and I checked a stashed tree before concluding anything, which
    is the cheap habit: find out whether it is yours before you debug it as if it is.

