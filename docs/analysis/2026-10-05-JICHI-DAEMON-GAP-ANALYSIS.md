# jichi daemon (warm process): the one thing it definitely lacks — a continuity that the doc promises and the code (and tests) do not have

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`). No files were
> modified; no daemon was started for this analysis. All claims cite a
> source; inferences are flagged.
> **Companions:** the other `JICHI-*-GAP-ANALYSIS.md` files (eight earlier).

---

## 1. TL;DR

jichi's daemon is a real, hardened, well-tested feature: a warm process
holding config, MCP connections, LSP servers, the index, and the prompt
prefix hot across requests over an AF_UNIX socket (`docs/DAEMON.md:1–7`).
The hardening record is exceptional — the socket mode verified not merely
requested (M528), an unbounded read loop found by *measuring* a 900 KB
request (900,128 `read` syscalls, 3.50 s → 270, 0.001 s; M530), a
bounded fork-per-request worker pool with a per-request watchdog (W3,
commit 1508a645), access control tested as an *effect* (`daemon_auth.sh`,
M528), a refusal on socket-less builds (M835), and error-delivery rows
(def-121).

But the feature carries **one claim that the code has not carried since the
claim's own milestone + 2 days** — and no test, no DEFERRED row, and no
analysis records that it no longer carries it:

> `docs/DAEMON.md:315`: "**Warm session** — **history persists across
> requests until the daemon restarts** (continuity is usually what you want;
> restart for a clean slate)."

M100 (`b4d3591e`, 2026-07-07) built the daemon serial — one request at a
time — and a serial daemon *can* hold conversation history in the warm app
across requests. **W3 (1508a645, 2026-07-09) replaced the serial daemon
with fork-per-request workers**: each PROMPT is "served by a forked child
(a COW snapshot of the warm app, **so a turn's mutations never leak into the
parent or a sibling**), while the parent keeps accepting" (`src/main.c:14575–
14577`). The child runs one `run_headless` turn and `_exit`s
(`src/main.c:15113–15156`); the client sends no session id
(`jc_daemon_build_prompt(prompt, cwd, NULL, format)`); the parent's
`run_daemon` body contains **no** session, resume, persist, or history code
(verified by grep of the function). "So a turn's mutations never leak into
the parent" is, for the conversation history, the *opposite* of "history
persists across requests": a second `--connect` request is a **fresh
one-shot** that knows nothing about the first.

So the daemon surface *does* keep warm what is expensive (config, MCP,
LSP, index, prompt prefix — the doc's real value proposition, and the
"save something" check at `DAEMON.md:87–92` measures exactly that). What it
no longer keeps warm — silently, since 2026-07-09 — is the *session*: the
"continuity is usually what you want" half of the feature. The one line in
the page that promises conversation continuity is the one line the
W3 commit made false, and the page's own verification recipe (`time jichi
--connect … -p "say ok"`, one prompt, twice) cannot detect it: both
prompts are one-shots either way.

## 2. What the daemon *does* have (for precision)

- **The warm value, real and self-checkable**: config, MCP servers, LSP
  servers, index, prompt prefix held hot; `DAEMON.md:87–92` tells the user
  to `time` cold vs warm and says plainly "If the warm one is not
  noticeably faster … the daemon is not buying you much. That is a
  legitimate outcome."
- **Fork-per-request isolation (W3)**: bounded pool (`daemonWorkers`,
  default `min(cpu,4)`), per-request watchdog (`daemonWorkerTimeout`,
  default 300 s, SIGTERM→SIGKILL), shared primitives with
  `spawn_parallel` (`jc_workerpool.{c,h}`, unit-tested), and the *stated*
  design property: "turns don't corrupt each other's in-memory state."
- **Access control as an effect** (`daemon_auth.sh`, M528): the socket
  mode is read back after `bind()` and the daemon **refuses to serve**
  unless owner-only; a client that is not the owner gets refusal,
  measured.
- **The measured-then-fixed hardening record**: M528 (verified mode,
  1 MiB line limit, `hello` posture request), M530 (buffered reader,
  900 KB measurement), M835 (socket-less builds refuse, `doctor` says so),
  def-121 (AF_UNIX-over-TCP error delivery), def-34's closure (the daemon's
  fork moved onto the unified `jc_spawn` backend, M832).
- **Smoke coverage of the *protocol***: `daemon.sh` (8 checks: socket
  created, ping, malformed request, text/jsonl/json error relay and shape,
  shutdown) and `daemon_auth.sh`.

Everything that is *expensive to re-warm* is warm and tested. Everything
that is *conversation state* is not warm, is not tested, and is
**documented as warm**.

## 3. The gap, stated

> **`DAEMON.md:315` promises "history persists across requests until the
> daemon restarts"; the W3 architecture (in place since 2026-07-09, and the
> *stated design goal* of W3 being that "a turn's mutations never leak
> into the parent") makes each request a fresh one-shot: the parent holds
> no session, the client sends none, and the child exits after one turn.
> No test exercises two requests for continuity (the suite's own W3
> verification was "ping, bad-request, 3 concurrent workers each returning
> with EOF, watchdog kill, clean shutdown"); no DEFERRED row or analysis
> records the discontinuity; and the page's own verification recipe
> (time one prompt cold vs warm) is structurally unable to see it, because
> one prompt cannot need continuity.**

Two strength levels:

* **(Strong) The doc and the code contradict each other on a
  load-bearing sentence, since the code's second commit.** M100 (2026-07-07)
  and W3 (2026-07-09) are two days apart in this repo's history; the W3
  commit touched `docs/DAEMON.md` (the commit message lists it) but left
  the "Warm session" bullet standing. The bullet describes the M100
  serial architecture. Anyone reading the feature's stated properties —
  "continuity is usually what you want; restart for a clean slate" — is
  reading a description of a daemon that does not exist in the tree.
* **(Weak, but the durable lesson) The feature's value test cannot
  falsify the false claim.** `DAEMON.md:87–92`'s "confirm you are saving
  something" is a *latency* check on a *single prompt*. The session
  continuity claim is a *state* claim across *two* prompts. The page gives
  the user a test for the half that works and no test for the half that
  changed — so the user's own verification, run faithfully, passes on a
  daemon whose doc is wrong.

## 4. Why "definitely lacking"

| Candidate | Status |
|---|---|
| Warm config/MCP/LSP/index/prefix across requests | **Real, documented, self-checkable** (`DAEMON.md:1–7, 87–92`). Not a hole — this is the feature. |
| Concurrency safety of warm state | **Architected and unit-tested** (W3 fork-per-request, `jc_workerpool` deadline predicate; "turns don't corrupt each other's in-memory state"). Not a hole. |
| Access control | **Measured as an effect** (`daemon_auth.sh`, M528; mode verified post-bind; refusal on non-owner). Not a hole. |
| Socket-less builds, line limit, buffered reader, error delivery, unified spawn | **Each a fixed + linted/measured row** (M835, M528, M530, def-121, M832). Not holes. |
| **Conversation history across requests** | **Claimed in the doc (`DAEMON.md:315`), not carried by the code since W3 (2026-07-09), untested, unrecorded.** The one property the page states, the code's second commit removed, and nothing noticed. |
| A soak / N-request continuity check of any kind | **None exists** — `daemon.sh` sends three single-shot prompts (text/jsonl/json error relay) and shuts down; no test sends two *successful* prompts expecting the second to see the first. |

The distinctive form, the tenth instance, and the set's first *self-
contradiction*: nine earlier analyses found a boundary the record stops at
and, generously, *names*. This one finds the record **crossing its own
line** — a shipped sentence and a shipped architecture, two days apart,
incompatible, with the sentence still in the page. It is the M326b shape
turned on the documentation itself: the check (the user's own "confirm you
are saving something") passes, and the universe it covers (one prompt's
latency) is not the universe the doc claims (a session).

## 5. Evidence

### 5.1 The claim

`docs/DAEMON.md:315` (current at `1b71247d`):

> - **Warm session** — history persists across requests until the daemon
> restarts (continuity is usually what you want; restart for a clean
> slate).

Introduced with M100 (`b4d3591e`, 2026-07-07, "daemon (warm process) —
keep the app hot across requests"), when the daemon served requests
**serially** and the statement was true of the warm app.

### 5.2 The architecture that ended it

`src/main.c:14575–14577` (W3, commit `1508a645`, 2026-07-09):

> Bounded pool of forked request workers for the daemon (W3). Each
> accepted PROMPT is served by a short-lived child (fork = a copy-on-write
> snapshot of the **warm app**, so **a turn's mutations never leak into the
> parent or a sibling**), while the parent keeps accepting.

The W3 commit message states the design goal explicitly: "so turns can't
corrupt each other's in-memory state." Mutation-isolation is the point;
for the conversation history, that means **no persistence across
requests**. The serving path confirms it: the child `dup2`s the socket,
runs `run_headless(app, req.prompt, req.fmt)` once (`src/main.c:15154`),
and `_exit`s (`:15157`); the parent, after `fork`, only tracks the
worker in the pool — `run_daemon` (14706–15190) contains no
`session`/`resume`/`persist`/`history`/`no_session`/`one_shot` token at
all (verified by grep of the function body).

### 5.3 The client side agrees: no session travels

`src/main.c:15220`: `reqline = jc_daemon_build_prompt(prompt, cwd, NULL,
args->output_json)` — the third argument (the session id) is **NULL**.
A `--connect` request carries a prompt, a cwd, a format, and nothing else
that could mean "continue this conversation." Two `--connect` calls are
two one-shots. One nuance, verified in source: the daemon never sets
`no_session`, and the child's `run_headless` therefore calls
`jc_session_save` — each request *does* write a session file, but under
a **fresh id** (no resume is sent or returned), so the saved file is a
log of that request, not a handle the next request can pick up.

### 5.4 The suite never tests two prompts for continuity

`tests/smoke/daemon.sh`: `t_plan 8` — socket created, ping, malformed
request, text/jsonl/json error relay and shape, clean shutdown. **All three
prompt checks use an unreachable model** ("maxRetries 0 so a turn fails
instantly"); none asserts that a second successful request sees the
first's context. W3's own verification (commit message): "ping,
bad-request, 3 concurrent workers each returning with EOF, watchdog kill
path, clean shutdown" — concurrency and lifecycle, not continuity. Grep of
`tests/smoke/daemon*.sh` for `leak|drift|long.live|many turns|warm
state`: **none**.

### 5.5 The page's own check cannot see it

`docs/DAEMON.md:87–92`: "Confirm you are actually saving something. Time
both paths on the same prompt … `time jichi -p "say ok"` # cold /
`time jichi --connect … -p "say ok"` # warm. If the warm one is not
noticeably faster … the daemon is not buying you much." The measured
quantity is *one prompt's latency*. A user who runs exactly this check,
faithfully, on a W3 daemon, gets the warm-speed win (MCP/LSP/index/prefix
are genuinely hot) and has verified — by the page's own recipe — that the
daemon is working, while the doc's session sentence is false. **Flagged
inference:** I have not *run* a two-prompt continuity probe (no daemon was
started for this analysis); the claim rests on the serving path
(§5.2–5.3), which is unambiguous in source: there is no channel by which
turn 2 could know turn 1.

## 6. Implications

1. **Users who build a "one daemon, one conversation" workflow get one
   daemon, one-shot conversations.** The doc's "continuity is usually what
   you want" steers exactly that workflow; the architecture serves exactly
   the opposite. The failure is quiet: no error, no log line, no doctor
   note — the second prompt just lacks the first's context.
2. **The warm/persist boundary is the feature's real design, and it is
   un-documented.** What the W3 daemon *does* persist (config, MCP, LSP,
   index, prompt prefix — the expensive re-warm) is documented; what it
   *deliberately* does not (conversation state, by mutation-isolation) is
   the design goal stated in the commit, but the page's bullet still
   lists continuity among the properties. The bullet's fix is not to
   restore serial persistence (W3's isolation is correct and load-bearing)
   but to say what "warm" means: *the setup is warm, the session is
   not — and if you want a session, that is `--resume <id>` per request,
   which the doc should show the client can carry.*
3. **`jc_daemon_build_prompt`'s NULL session slot is the seam.** The
   request codec already has the parameter position; passing the client's
   `--resume` id through — and letting the parent (or the child) re-hydrate
   from the saved session file — is the two-line change that would make
   the bullet true again *and* keep W3's in-memory isolation. Until then
   the codec's signature is a fossil of the M100 architecture: it has a
   session argument, and the only caller passes NULL.
4. **The doc-lint universe does not cover feature pages' *property*
   claims.** The tree lints deck slide counts, doc flags, command
   surfaces, and the register itself — the machinery that would catch a
   "31 vs 29" — but no check compares a page's stated *behavioural*
   bullets against the architecture in `src/`. The "Warm session" bullet
   is a behavioural claim with no corresponding test; that is the class
   of claim the M326b register was built to flag, applied to
   documentation.
5. **Meta, the tenth, the set's first self-contradiction.** Debugger, UX,
   curriculum, presentations: never happened. Editor, MCP: the offline
   design excludes the live path. Platform: the live datum is a
   photograph. Orchestration: the live datum is one machine. Secrets: the
   promise is unmeasured. **Daemon: the promise is contradicted by the
   code two days after it was written, and the contradiction is
   unrecorded.** The register's deepest rule — "the state the row
   describes must be the state the tree has" — has, here, a row the tree
   is already past.

## 7. Recommendation

1. **Fix the bullet, or the daemon — decide, and record it.** Two
   coherent options, and the register wants one of them named with the
   other rejected: (a) *the doc*: rewrite `DAEMON.md:315` to "the *setup*
   is warm; each request is a fresh one-shot (W3's mutation isolation);
   for a continuing session, `--connect … -p … --resume <id>`" and add
   the two-request continuity check to the "confirm you are saving
   something" section; or (b) *the code*: thread the client's session/resume
   id through `jc_daemon_build_prompt` (the NULL slot is already there) so
   the warm session is true again — with `daemon.sh` gaining a
   two-prompt continuity check (prompt 2 must reference prompt 1's
   context; mock model scripted to answer only if it sees it).
2. **Either way, add the test that was missing: two successful prompts,
   one daemon, an assertion about what the second saw.** This is the
   check that would have caught the W3 flip on 2026-07-09, not on
   2026-10-05. Red-first: write it against the current tree, where
   continuity is false, watch it fail, then fix (1a or 1b) and watch it
   pass.
3. **File the DEFERRED row for whichever option is rejected.** The
   register's form: *the daemon's conversation continuity — claimed
   (DAEMON.md:315), architecturally false since W3 (1508a645), no test
   either way*; closed when (1a) lands the rewrite, (1b) lands the
   plumbing, and the two-prompt check is in `run.sh`.
4. **Generalise the lesson into the doc-lint universe.** A page bullet
   that asserts a behavioural property ("X persists", "X is warm", "X
   survives") should map to a named test or a named DEFERRED row, the way
   the deck slide-counts map to the lint. The "Warm session" bullet is
   the found instance; the mechanism is the fix.

Rejected, in the register's spirit:

* *Restore the serial daemon as an option* — W3's isolation is the reason
  concurrent turns are safe; serial mode would be a regression dressed as
  compatibility. The session belongs in `--resume`, which already exists
  per-request.
* *Call it "works as documented for the warm parts" and stop* — that is
  the M326b shape: the check (the doc's own time test) passes, and the
  universe shrank to one prompt. The bullet is a claim; the claim is
  false; a false claim in the feature's property list is not a warm-part
  success.
* *Silently edit the doc as a typo* — it is not a typo: M100's
  architecture made it true. The honest record is a DEFERRED row with the
  M100/W3 provenance, so the next reader sees *when* the daemon stopped
  meaning what the page says.

## 8. Limitations and honest counterarguments

1. **The daemon's headline value is real and I am not disputing it.**
   Config/MCP/LSP/index/prefix warmth is genuine, measured-by-the-user
   (`DAEMON.md:87–92`), and is what the feature is for. §3 targets one
   bullet, not the feature. A reader who never wanted conversation
   continuity from a daemon has no gap here.
2. **"Warm session" might have meant the prompt prefix.** The M100 commit
   says "keep the app hot across requests"; one could read "history" in
   the bullet as "request prefix history" (the cached prompt prefix),
   which *is* warm. But the bullet's own gloss — "(continuity is usually
   what you want; restart for a clean slate)" — is conversation-state
   language, and "restart for a clean slate" is meaningless for a prefix
   that is recomputed from config. I treat the conversation reading as the
   natural one; if the prefix reading is intended, the bullet should say
   so, which is a fix in the same direction as §7.1.
3. **Absence-based on the suite and a private workflow.** No two-prompt
   continuity test exists in the tree — verified by reading `daemon.sh`
   and `daemon_auth.sh` in full. The operator's own tmux workflow
   (`DAEMON.md:96–100`) could have hit the discontinuity in daily use;
   nothing records it either way.
4. **I did not run a daemon.** §5.5's inference is from the serving path
   (source), not from an observed response. A two-prompt probe (§7.2)
   would convert it to a datum.
5. **Two days is a small window, and the bullet may have been intended to
   be rewritten.** The W3 commit lists `docs/DAEMON.md` among its changed
   files; the rewrite of the concurrency section happened, the session
   bullet did not. Whether that is a missed edit or a not-yet-made one
   is not recoverable from the record — which is itself the point: the
   record carries the false sentence with no note that it is pending.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the claim, and when it was written (serial daemon era)
sed -n '315,316p' docs/DAEMON.md
git log --format="%h %ci %s" -S "history persists across requests" -- docs/DAEMON.md     # M100, 2026-07-07

# the architecture that ended it (W3, 2026-07-09)
sed -n '14575,14577p' src/main.c
git log --format="%h %ci %s" -1 1508a645
git show 1508a645 --stat | grep -i "DAEMON.md"      # the commit touched the page; the bullet survived

# the serving path: one turn, then _exit; no session channel
sed -n '15143,15156p' src/main.c
grep -cE "session|resume|persist|history|one_shot" <(sed -n '14706,15190p' src/main.c)   # 0 in run_daemon

# the client sends NULL for the session slot (line 15220)
grep -n "jc_daemon_build_prompt(prompt" src/main.c

# each child still saves a session file under a fresh id (no resume travels)
grep -n "jc_session_save" src/main.c | awk -F: '$1 > 2570 && $1 < 2610'   # inside run_headless

# the suite: 8 checks, 3 single-shot error-relay prompts, no continuity
grep -n "t_plan\|type\":\"prompt\|maxRetries" tests/smoke/daemon.sh

# the page's own verification recipe (one prompt, latency only)
sed -n '87,92p' docs/DAEMON.md

# no record anywhere of the discontinuity
grep -rn -i "warm session\|continuity" docs/DEFERRED.md docs/analysis/ | head -3         # empty
```

**Verdict line for the register this could seed:** *jichi's daemon
promises a "Warm session — history persists across requests" (DAEMON.md:315,
M100, 2026-07-07); W3's fork-per-request workers (1508a645, 2026-07-09),
whose stated design goal is "a turn's mutations never leak into the
parent," make each request a fresh one-shot — the parent holds no session
code, the client sends NULL for the session slot, the child runs one
`run_headless` and exits — and the suite (8 checks, all three prompts
error-relays against an unreachable model) plus the page's own
verification recipe (one prompt, timed cold vs warm) cannot detect the
discontinuity. The warm *setup* (config/MCP/LSP/index/prefix) is real and
self-checkable; the warm *session* is a M100 sentence surviving W3,
unrecorded in DEFERRED or analysis/. Revisit when: the bullet is either
rewritten ("the setup is warm, the session is not; `--resume <id>` per
request") or made true (session id threaded through `jc_daemon_build_prompt`),
and `daemon.sh` gains a red-first two-prompt continuity check.*
