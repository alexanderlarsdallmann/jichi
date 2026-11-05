# Review 3 — `main.c`: the front door, the run modes and the exits

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on two complete
reviewer reports, one for each half of `src/main.c`. Their eleven code excerpts
were checked line for line against the source, and the prune finding was re-read
in the source. Written by Claude Opus 5.5 on threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to know what happens
between typing `jichi …` and the agent loop starting, and what happens after it
ends: the command line, the subcommands, the run modes and the exit codes. It
is also the chapter for anyone who writes a script or supervisor that runs
jichi.

**After reading it you will be able to:**

- find any subcommand's code in a 17,793-line file in under a minute;
- classify every subcommand by its effect (prints, reads, writes, network)
  before you run it;
- read the exit-code contract and know which code a run will end with;
- explain why `atoi` on a command-line flag is a defect, and what to write
  instead;
- read six pieces of real code that each guard against a specific mistake.

**What you need first:** [Annai 3](../reading/annai-03-main-to-the-loop.md) is
the guided path from `main` to the loop. [SCRIPTING.md](../SCRIPTING.md) is the
exit-code contract this chapter checks against.

---

## 1. The shape of the file

`src/main.c` holds everything that is not a library layer. By line range:

| Lines | What | Size |
|---|---|---|
| 159–660 | `print_version`, `print_help`: the human index of flags and subcommands | ~500 |
| 664–1573 | `struct cli_args` (~150 fields) and `parse_args`: one `if / else if` chain over `argv` | ~900 |
| 1575–2823 | the headless front end: output formats, redaction, the reach footer, the exit code | ~1,250 |
| 2826–9191 | the subcommands, one `run_<name>` each; the setup wizard alone is ~2,070 | ~6,400 |
| 9195–9379 | arming the autonomy envelope | ~190 |
| 9380–15552 | `doctor` (2,443), the assignment harness, `workflow`, the daemon and its clients | ~6,200 |
| 15556–17793 | `main()` itself: dispatch, setup, the run, the exit-code fold, teardown | 2,237 |

**How `main()` decides what to do:**

```
argv -> parse_args -> commands that need no config (ls prune export init
          |            setup assign hint grade control dream improve ...)
          v                                   ... each returns early
   load config (+ trust prompt) -> arm the secret registry -> CLI overrides
          |
          v
   commands that need config (index mcp lsp test status doctor docs
          |                   undo rewind skills agents sysmsg context ...)
          v
   tools, provider, signals, MCP, hooks, LSP, snapshots
     -> envelope (if armed) -> control socket -> rules, memory, skills
     -> telemetry -> scratch arenas -> lease
          |
          v
   workflow | attempt | improve | daemon | serve | -p / stdin | TUI
          |
          v
   fold the envelope's verdict into the exit code -> teardown -> exit
```

**In words.** Commands that need no configuration run first and return. Then
the configuration is loaded, and the secret registry is armed before anything
can start a child process: it is the list of keys to redact and to keep out of
children's environments (M608). Commands that need only the configuration run
next. Then everything a model run needs is built: tools, the provider, signal
handlers, MCP servers, snapshots, and, when one is asked for, the autonomy
envelope with its journal. Last comes the run itself, in one of six modes, and
its verdict becomes the exit code.

**The order of those blocks is load-bearing.** Where a new subcommand's `strcmp`
block is inserted decides whether it sees trust prompts, config errors and the
first-run banner. Only comments say so.

**Finding your way, in practice:**

- `/usr/bin/grep -n '"undo"' src/main.c` finds the dispatch block in `main()`,
  which calls `run_undo` in the first half.
- Three traps:
  - `ls` is dispatched on the *prompt* field, not the first positional word
    (finding R03-F13 below);
  - `embed`, `rerank` and `index` go through `is_index_subcommand`;
  - `undo`, `attempts`, `recover` and `checkpoints` share one block.
- Read a function's comment before its code. Most record the incident that
  shaped it, and the milestone number (`M375`, `M802b` …) finds the full story
  in [ROADMAP.md](../ROADMAP.md).

---

## 2. What each subcommand does, before you run it

CLAUDE.md's rule: *classify a command by its effect before running it to learn
what it does*. One day, three commands probed as a set reverted 768 files
(ANECDOTES #66). The reviewer classified every subcommand in the first half:

| Effect | Subcommands |
|---|---|
| **prints only** | `describe`, `status`, `--help`, `--version` |
| **reads** | `ls`, `export` to stdout, `map`, `context`, `telemetry`, `audit`, `ps`, `runs`, `constraints`, `learn analyze`, `skills`, `agents`, `commands`, `rules`, `board list` |
| **reads, but creates its store** | `checkpoints`, `attempts`: the first use creates the shadow repository and may trim it |
| **writes** | `undo`, `rewind` (revert the workspace; `rewind` also cuts the saved session), `recover`, `trust`, `untrust`, `init`, `learn apply`, `board add` and friends, `dream`, `export -o` |
| **deletes** | `prune` (sessions, dreams, index caches, worktrees of *all* projects), `checkpoints gc --yes` |
| **network** | a prompt, `embed`, `rerank`, `index`, `setup` (model listing; `--advisor` and `--onboard` call a model) |
| **anything** | `test` (runs a shell command), `mcp call` (runs any MCP tool) |

**`--dry-run` exists for `undo`, `rewind` and `prune`.** It is the safe way to
answer "what would this do?".

**A design suggestion from the review:** one table, `{name, effect, needs,
handler, help}`, would generate the dispatch, the help text, `describe`'s rows
and this classification. A reader could then see which command writes without
running it.

---

## 3. The headless run and its exits

A **headless** run has no terminal UI: `jichi -p "task"`, a prompt on stdin, or
a non-TTY stdin. `run_headless` (`src/main.c:2329-2823`):

1. opens or resumes a session;
2. expands `/command` and `@file` references;
3. runs one turn;
4. saves the session;
5. prints the **reach footer**: two stderr lines saying what the run checked,
   computed from counters and never from the model;
6. maps the outcome to an exit code.

**Three output formats:**

- `text`: the answer on stdout;
- `json`: one object at the end, with `stop_reason`, economics and reach;
- `jsonl`: one object per event as it happens.

Diagnostics always go to stderr.

**The exit codes, as the code implements them** (`src/main.c:2808-2822`,
`:17712-17729`):

| Exit | When | `stop_reason` |
|---|---|---|
| 0 | the turn completed (also when a downstream pipe closed early) | `done`, or `max_iters`: the cap is a circuit breaker, not a failure |
| 1 | a provider, network or I/O error; or the envelope's verdict is a failed verify, an exhausted budget or a tainted scope | `error`, `timeout`, `verify_failed`, `budget`, `scope_tainted` |
| 2 | a usage error (bad flag, empty prompt …) | none: no run |
| 130 | interrupted by SIGINT | `interrupted` |
| 143 | terminated by SIGTERM (graceful) | `interrupted` |

**Two status channels meet at the end.** `jc_status` says whether the machinery
worked. The envelope's outcome says whether the work is accepted. The fold
upgrades only 0 to 1, so an interrupt (130 or 143) or a usage error (2) is never
hidden behind a verdict (§4.6).

**What a resumed run keeps and loses** (`-c`, `--resume`, `--session <id>`).
It keeps:

- the history with every tool call and result;
- the todo list;
- the title;
- the saved mode, which may only *narrow* the current one.

It loses:

- **the whole envelope:** budgets, the journal and the out-of-scope lists start
  fresh;
- read-tracking, so re-read detection starts empty;
- constraints inferred from the earlier request.

The [relay proposal](../proposals/2026-10-context-relay.md) §6.9 is about
exactly this loss.

---

## 4. Code to study

Six excerpts from the two reports, each copied exactly from `17bbb895`.

### 4.1 A number parsed the right way — `src/main.c:832-852`

```c
/* Parse a --timeout-* value: a non-negative whole number of seconds, or
 * "off"/"none" (=> 0, disabled). Writes seconds to *secs; returns 0 on a
 * malformed value. */
static int parse_timeout_arg(const char *s, long *secs)
{
    const char *p;
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (strcmp(s, "off") == 0 || strcmp(s, "none") == 0) {
        *secs = 0;
        return 1;
    }
    for (p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    *secs = atol(s);
    return 1;
}
```

**What it teaches.** This is how every numeric flag should be read, and most are
not (§5, F1 and F3):

- **It checks the whole string before converting,** so `5m`, `1k` and `abc` are
  refused instead of becoming 5, 1 and 0.
- **It names the spellings that mean "off",** instead of letting 0 mean both
  "disabled" and "I could not read that".
- **It reports failure as a value,** and leaves the message to the caller, which
  knows the flag's name.

**One gap is left for you to spot.** `atol` on a 30-digit string overflows,
which is undefined behaviour in C89. `strtol`, with a check of `errno` against
`ERANGE`, is the bounded version.

### 4.2 Refusing what is probably a mistake — `src/main.c:960-975`

```c
            /* M375: refuse a flag-shaped prompt. `jichi -p --no-session
             * "question"` once sent the model the FLAG as its prompt and
             * dropped the question as an ignored positional. "-" (stdin)
             * and "- bullet"-style text stay valid; a prompt genuinely
             * starting with an option-like '-' has spellings that say so. */
            if (argv[i + 1][0] == '-' &&
                (argv[i + 1][1] == '-' ||
                 isalnum((unsigned char)argv[i + 1][1]))) {
                fprintf(stderr, "error: %s took '%s', which looks like a "
                        "flag, not a prompt\n", a, argv[i + 1]);
                fprintf(stderr, "  put the prompt text directly after %s; "
                        "for text that really starts with '-', use "
                        "--prompt-b64 or a positional prompt after --\n", a);
                return -1;
            }
            out->print_prompt = argv[++i];
```

**What it teaches.** A parser that takes "the next word" as a flag's value will
happily take the next *flag*. This guard refuses a `-p` value that looks like an
option, and keeps the two legitimate cases: `-` for stdin, and text that starts
with `- ` like a bullet.

Its error message names the escape hatch. That is the lesson: **fail loudly on
the ambiguous case, and tell the user what does work.** Note also
`(unsigned char)`: passing a plain `char` to `isalnum` is undefined behaviour
for bytes above 127.

### 4.3 Redacting a stream whose secret may be split — `src/main.c:1672-1694`

```c
/* M802b: emit the redacted streamed text that is now safe to release. The held
 * buffer is redacted whole (so a secret split across deltas is scrubbed once both
 * halves have arrived), and all but the last jc_redact_maxlen() bytes are emitted
 * unless `final`. The emitted prefix is stable across deltas: no secret straddles
 * the safe boundary, because a secret ending in the held tail (length <= maxlen)
 * also starts inside it. `text_emitted` therefore only advances. */
static void hl_text_flush(struct hl_ctx *c, int final)
{
    jc_size hold, rlen, safe;
    char *red;
    if (c->broken_pipe || c->text_hold.len == 0) {
        return;
    }
    hold = jc_redact_maxlen();
    red = (char *)malloc((size_t)c->text_hold.len + 1);
    if (red == NULL) {
        return;
    }
    jc_redact_secrets(c->text_hold.data != NULL ? c->text_hold.data : "",
                      red, (jc_size)c->text_hold.len + 1);
    rlen = (jc_size)strlen(red);
    safe = final ? rlen : (rlen > hold ? rlen - hold : 0);
    if (safe > c->text_emitted) {
```

**What it teaches.** A redactor that matches whole strings fails on a stream: a
secret can arrive cut in two, and neither half matches. The fix:

1. keep the raw text received so far;
2. redact the *whole* buffer each time a chunk arrives;
3. release only the part that can no longer be the start of a secret: everything
   except the last `maxlen` bytes, where `maxlen` is the longest secret.

The comment states the invariant that makes the released prefix stable, which
is what a reviewer needs to check the arithmetic. This kind of reasoning — what
can still change, and what is safe to release — recurs in every streaming system.

### 4.4 A failure is not an empty answer — `src/main.c:2867-2884`

```c
    /* M482: this called jc_session_list and DISCARDED its status under the
     * comment "empty vec => empty array", so a failed enumeration produced a
     * well-formed `{"v":1,"sessions":[]}` -- a machine-readable lie, and worse
     * than the text path's, because `ls --output json` is a STABLE interface
     * (docs/EMBEDDING.md) that a supervisor parses instead of reading. It also
     * dropped `skipped` entirely, so the JSON could not say that files were
     * unreadable while the text form could. Both fixed: a failure is a non-zero
     * exit with a stderr reason and NO stdout object, and `skipped` is reported
     * as a field (an added field is a compatible change under M301's contract). */
    st = jc_session_list_ex(&metas, arena, &skipped);
    if (st != JC_OK && st != JC_ERR_NOTFOUND) {   /* see run_ls on NOTFOUND */
        fprintf(stderr, "error: could not list sessions (%s)\n",
                st == JC_ERR_OOM ? "out of memory"
                                 : "the session directory exists but could "
                                   "not be read");
        jc_vec_free(&metas);
        return 1;
    }
```

**What it teaches.** This is the machine-output twin of "tool errors are
values": **a listing that could not be produced must not print a well-formed
empty listing.** A supervisor parses the JSON and never reads the prose, so a
false empty list is worse there than in text.

Note the one status that *is* treated as empty: `JC_ERR_NOTFOUND`, "no store
yet". It is a genuine empty case, and the comment points to the bug that came
from getting it wrong the first time. This is the opposite of the false-success
theme in [Review 0](2026-10-08-review-00-overview.md) §5.1.

### 4.5 Closing a race instead of patching it — `src/main.c:14759-14769`

```c
    {
        mode_t old_umask = umask(0177);   /* 0600 for anything created below */
        int rc_bind = bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
        umask(old_umask);
        if (rc_bind != 0 || listen(lfd, 4) != 0) {
            fprintf(stderr, "daemon: could not bind/listen on %s\n", path);
            close(lfd);
            return 1;
        }
        jc_make_private(path);
    }
```

**What it teaches.** A Unix socket's file mode *is* its access control, and
`bind()` creates the file.

- **The race.** A `chmod()` after `bind()` leaves a window in which the socket
  exists with the default mode, and someone can connect.
- **The fix.** Setting the umask around `bind()` creates the file private from
  its first moment.
- **The double check.** The code then reads the mode back and refuses to serve
  if it is wrong (`:14783-14796`), because a successful `chmod` says the call
  worked, not that the file is private.

**Two lessons:** prevent the window rather than shrinking it, and verify the
effect, not the return value.

### 4.6 An exit code that carries a verdict without hiding an interrupt — `src/main.c:17712-17729`

```c
    /* The agent loop returns JC_OK even when the verifier failed (the turn
     * completed); the envelope's outcome carries the verdict. Surface it. */
    if (env_active_run) {
        enum jc_env_outcome oc = app.env->outcome;
        char tk[40];
        jc_group_num(app.env->tokens_used,
                     jc_group_sep_audience(app.config.group_sep,
                                           app.config.accessible),
                     tk, sizeof tk);
        if (oc == JC_ENV_VERIFY_FAILED || oc == JC_ENV_BUDGET_EXHAUSTED
                || oc == JC_ENV_SCOPE_TAINTED) {
            if (!args.quiet) {
                fprintf(stderr, "[envelope] %s (tokens %s, tool calls %d)\n",
                        jc_env_outcome_name(oc), tk, app.env->tool_calls);
            }
            if (exit_code == 0) {
                exit_code = 1; /* preserve 130 (interrupt) / 2 (usage) */
            }
```

**What it teaches.** Precedence between two facts belongs in one place, written
down. Here the more specific codes win: an interrupt (130 or 143) and a usage
error (2) survive, and only a 0 becomes 1. jichi does the same for the
`stop_reason` string, which one function decides for every surface
(`jc_agent_stop_reason`), so the JSON, the journal and the footer cannot
disagree.

---

## 5. What the review found

**About forty findings across the two halves:** two high, ten medium, the rest
low or notes. Status is the reviewers'. Security findings are described by class.

### High

- **R02-F1 — `prune --keep <not a number>` deletes everything** (verified by a
  run, and re-read in the source). `--keep` is parsed with `atol`, so `""`,
  `abc` or `all` become 0 (`src/main.c:1268`). The only guard checks for a
  *negative* value (`:3375`). `prune` then keeps zero sessions, and deletes
  every session of every project, plus dreams, index caches and stale
  worktrees. The reviewer's private run deleted three of three sessions. *The
  fix:* read the number as §4.1 does, and refuse anything else. *The proof:*
  `prune --keep abc` must exit 2 and delete nothing.
- **R03-F1 — the second Ctrl-C kills jichi** (verified in a pseudo-terminal, and
  re-read). The signal handlers are installed with `signal()`
  (`src/main.c:17253-17255`). With `-std=c89` and no BSD feature macro, glibc's
  `signal()` gives the System V behaviour: the handler is reset after one
  delivery. The handler never re-installs itself (`:145-157`), so the second
  SIGINT kills the process. There is no teardown, and the TUI leaves the
  terminal without echo. The smoke tier cannot see it: a shell's `wait` reports
  130 for a process killed by SIGINT too. *The fix:* install the handlers with
  `sigaction()` from one helper in `src/platform/`.

### Medium

- **R02-F3 — numeric flags go through `atoi` and `atol`.** Measured:
  `--max-tool-calls abc` silently removes the cap, `1k` becomes a cap of 1, and
  `--context-limit 32k` becomes a 32-token limit. A typo removes a fence or
  changes a unit, with no message.
- **R02-F4 — `undo` and `rewind` read a non-numeric N as 1.** `jichi undo
  dry-run`, with the dashes forgotten, reverted the workspace in the reviewer's
  run. The files are preserved by default since M338; the conversation
  messages that `rewind` cuts are not.
- **R02-F5 — the setup wizard's default provider is the priced one.** Pressing
  Enter at "Model provider:" picks `anthropic` (`src/main.c:6346`). M709
  reordered the menu but kept the default index. The project's own rule is
  free models first ([LOCAL_MODELS.md](../LOCAL_MODELS.md); CLAUDE.md's models
  section), and a learner who presses Enter is sent to a paid API.
- **R02-F2 and F7 — two output paths miss the secret redaction** that the
  streamed text gets (security class; details with the maintainers).
- **R03-F2 — one slow daemon client blocks every request.** The daemon reads
  each request in the parent, before forking, with no time limit. A client that
  sends half a line stops every other client. DAEMON.md says this cannot happen.
- **R03-F3 — `brief-check` predicts a different envelope than the run arms.** It
  ignores the config's `testCommand`, `editScope` and `verifyKind`, so it reports
  "nothing gates this run" for a run that is gated.
- **R03-F4 — `--journal -`, documented as "disable the journal", arms the
  envelope by itself,** and a headless run with an envelope auto-approves every
  tool. A flag meant to turn a record off changes the permission posture.
- **R03-F5 — the daemon's "warm session" is not warm.** By default every request
  starts a fresh session, unless the daemon itself was started with `-c`.
- **R03-F6 — an interrupted `attempt` is still graded,** recorded and trended.
- **R03-F7 — usage and config errors exit 1 in some paths, where the contract
  says 2** (`src/main.c:15853-15903`, `:16017-16044`). Exit codes are a stable
  interface ([EMBEDDING.md](../EMBEDDING.md)).

### Low, and notes (selected)

- **`jichi -p ls` lists sessions** instead of sending "ls" to the model
  (R02-F10, R03-F13).
- **`--control`'s optional value swallows a positional prompt** (R02-F9).
- **The signal handler writes a `volatile int`,** not a `volatile
  sig_atomic_t` (R02-F15). C89 promises atomicity only for the second.
- **JSON printed with `printf` and no escaping** on machine-readable surfaces
  (R02-F6).
- **A resumed run with `--no-session` still writes the session file** (R02-F14).
- **The daemon deletes whatever is at its socket path,** and a second daemon
  orphans the first (R03-F8).
- **`workflow` exits 0 when interrupted or when a stage fails** (R03-F10).
- **CLAUDE.md says `sprintf_lint` also covers `strcpy` and `strcat`.** It checks
  only `sprintf`, and there are 13 `strcpy`/`strcat` calls in `src/`.

---

## 6. The design, seen whole

**The size is the first fact.**

- `main.c` is 17,793 lines. `main()` alone is 2,237; `run_doctor` is 2,443; the
  setup wizard about 2,070.
- About sixty dispatch blocks of the form `if (args.npos > 0 &&
  strcmp(args.pos[0], "…") == 0)` each repeat three to five lines of teardown.

**What the reviewers would change, in order, with the cost:**

1. **Move the parser out.** `struct cli_args`, `parse_args`,
   `parse_timeout_arg` and a new validated count parser would go to
   `src/util/jc_args.c`.
   - *Why:* `parse_args` is pure but `static` in `main.c`, so no unit test can
     reach it. That is why F1, F3, F4, F8 and F9 exist without a test.
   - *Cost:* about 750 lines moved. The real cost is repointing the lints that
     read flag spellings from `main.c`, each proven red again.
2. **Table-driven dispatch.** One table `{name, needs, effect, handler, help}`
   would generate the dispatch, the help column, `describe`'s rows and §2's
   classification, with one teardown path. The order of the levels (no config,
   config, provider …) becomes data instead of block position.
3. **Move the setup wizard** to `src/setup/`. It is the largest and most
   self-contained block.

**Four more observations, each a reusable lesson:**

- **Validate early.** Several argument checks run *after* side effects: the
  privileged-command policy after the config load, `--lease` after arming the
  envelope and the control socket. *Lesson:* pure checks belong in the parser,
  before anything changes state.
- **One source, not three re-derivations.** `envelope_arm` is meant to be the
  single source of a run's bounds, yet `brief-check` and the `attempt` envelope
  re-derive parts of it, and their predictions drifted (R03-F3, F15). *Lesson:*
  when two places must agree, make one call the other.
- **Separate gathering facts from judging them.** Each `doctor` check probes
  files, the network and `/proc`, *then* decides WARN or FAIL in the same block,
  so no judgement is unit-testable. jichi's own `jc_gradecore` and `jc_reach`
  show the better shape: gather, then a pure judge.
- **Flags are global.** Every flag is accepted by every subcommand and ignored
  where it means nothing: `jichi undo --keep 3 --html` parses cleanly, and both
  flags do nothing. A per-subcommand set of allowed flags turns that into a
  usage error.

---

## 7. What this means for the context relay

- **A handover needs its own stop reason and exit code.** The switch in §4.6's
  neighbourhood has no `default:` on purpose, so a new reason fails the build
  until it is classified ([proposal](../proposals/2026-10-context-relay.md)
  §6.3, §6.14).
- **The relay depends on clean signals,** so R03-F1 comes first (proposal §9).
- **A resumed run loses its envelope** (§3). The relay keeps one envelope per
  relay for exactly that reason (proposal §6.9).
- **jichi never re-executes itself.** It exports `JICHI_BIN` so a model's shell
  can start a *nested* jichi, with no guard against depth. A relay must add one
  if it ever re-executes (proposal §6.8 rejects re-exec for now).

---

## 8. Something to do

**With a bench (any directory you can throw away, an isolated `HOME`):** see
R02-F3 for yourself, safely. `brief-check` calls no model and writes nothing; it
only prints the envelope a run *would* get. Run from an empty directory:

```sh
printf 'Say hi.\n' > brief.txt
jichi brief-check brief.txt --max-tool-calls 3   < /dev/null 2>/dev/null | grep 'max tool calls'
jichi brief-check brief.txt --max-tool-calls abc < /dev/null 2>/dev/null | grep 'max tool calls'
jichi brief-check brief.txt --max-tool-calls 1k  < /dev/null 2>/dev/null | grep 'max tool calls'
```

What you should see, measured on `17bbb895` on 2026-10-08:

- `max tool calls : 3`;
- `max tool calls : 0`, which means *no cap at all*: the fence vanished;
- `max tool calls : 1`: `1k` became a cap of one call.

Never run `prune` or `undo` to learn what they do: §2 tells you, and
`--dry-run` shows you.

**Without a bench (the read-only twin):**

1. Rewrite the `--keep` parse from `src/main.c:1268` in the style of §4.1. What
   does your version do with `""`, `abc`, `-3`, `07` and
   `99999999999999999999`?
2. In §4.6, list every combination of `exit_code` (0, 1, 2, 130, 143) and
   outcome (ok, verify failed, budget exhausted), and the code the fold
   produces. Which combination would be wrong if the `if (exit_code == 0)` test
   were removed?
3. In §2's table, which subcommands would you allow an unattended agent to run
   without asking? Why those?

## 9. Read further

- [SCRIPTING.md](../SCRIPTING.md) and [EMBEDDING.md](../EMBEDDING.md): the exit
  codes and the stable machine surfaces. *Primary for jichi.*
- [Annai 3](../reading/annai-03-main-to-the-loop.md): from `main` to the loop,
  step by step. *Primary for jichi.*
- The POSIX specification of `sigaction()`, and the GNU C Library manual's
  "Basic Signal Handling" (on `signal()`'s two behaviours): background for
  R03-F1. *Primary.*
- The C89 standard, §4.10.1.2 (`atoi`) and §4.10.1.5 (`strtol`): why `atoi` has
  no way to report an error, and what `strtol` reports instead. *Primary.*
- Michael Kerrisk, *The Linux Programming Interface*, No Starch Press, 2010,
  chapters 20–22 (signals) and 57 (Unix domain sockets). *Secondary.*
