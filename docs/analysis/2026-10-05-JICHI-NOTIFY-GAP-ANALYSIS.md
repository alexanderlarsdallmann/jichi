# JICHI NOTIFY.md — gap analysis (analysis 46)

## TL;DR

*Read-only survey of docs/NOTIFY.md at HEAD 1b71247d (58 lines), every claim cross-checked against src/util/jc_notify.c, the two call sites, config parsing, and the smoke test, 2026-10-05.*

*This is an INVERSE finding: NOTIFY.md is accurate in every detail, and its single abbreviation ("runs <cmd> via /bin/sh -c") hides the JICHI_SHELL override — a portability fact that matters to exactly the users the doc names (tmux/notify-send operators). No behavioral gap, no stale claim, no unregistered deferral.*

## Claim-by-claim verification (all from the jichi checkout)

| Doc claim | Code | Verdict |
|---|---|---|
| opt-in, nothing by default | `notify_bell = 0`, `notify = NULL` (jc_config.c:1049, default-init) | true |
| reuses fork/exec runner | `jc_proc_capture` (jc_notify.c:27) | true |
| front-ends fire; agent core + ACP never do | `jc_notify_fire` callers: jc_tui.c:5380, main.c:2802 only | true |
| TUI: after each completed turn | fired right after `jc_session_save` (jc_tui.c:5376-5381) | true |
| headless `--auto` once on completion; plain `-p` does not | `if (app->mode == JC_MODE_AUTO)` (main.c:2801-2803) | true |
| `notifyBell`/`--bell` write BEL to stderr | `fputc('\a', stderr); fflush(stderr)` (jc_notify.c:42-43) | true |
| stdout stays clean for `-q`/`--output json`/pipe | the stderr choice, per the code comment | true |
| `notify`/`--notify <cmd>` runs on turn finish | `jc_notify_fire` | true |
| short timeout, output discarded, can't wedge front-end | `NOTIFY_TIMEOUT 10L` (jc_notify.c:18), sink discarded | true |
| env `$JICHI_NOTIFY` = one-line summary (session title) | `push_env("JICHI_NOTIFY", summary)`; call sites pass `session.title` | true |
| env `$JICHI_CWD` = workspace dir | `push_env("JICHI_CWD", cwd)`; call sites pass `app->cwd` | true |
| signature `jc_notify_fire(command, bell, cwd, summary)` | jc_notify.c:36-37, explicit fields (not jc_app) | true |
| TUI + run_headless wiring | jc_tui.c:5380, main.c:2802 | true |
| e2e `tests/smoke/notify.sh` asserts BEL-on-stderr + env vars | 3 `t_ok` assertions | true |

## The one nuance (the doc's single abbreviation)

The doc says the notify command runs "`via /bin/sh -c`" (Configuration section)
and "via the shared `jc_proc_capture`" (Internals). Both are true but the
`/bin/sh` claim is an *abbreviation*: the actual shell is `jc_shell_path()`
(jc_platform_posix.c:193), which resolves, in order:

1. `$JICHI_SHELL` (if set and executable),
2. `/bin/sh` (if executable),
3. `$PREFIX`-derived candidates.

So an operator who has set `JICHI_SHELL` for their tool runner will also run
their *notify* command under that shell. This is almost certainly intended
(the whole point of `JICHI_SHELL` is a single override for every jichi shell
spawn), and it is harmless for a notifier — but the doc's "/bin/sh" is the
default, not the invariant. Worth one parenthetical, not a correction.

This is the *opposite* of the HOOKS finding (44): here the doc understates a
portability knob rather than inventing a nonexistent one.

## The genuinely-lacking thing

None, at the behavioral level. NOTIFY.md is one of the most accurate short docs
in the tree. The honest gap is **coverage breadth**: the `notify` command is
fired from exactly two call sites (TUI turn-end, headless AUTO completion) and
there is no notification for the *other* "I should go get the user's
attention" moments the feature's own purpose implies:
- a `--auto` run that **blocks on a privileged command / path fence** (the
  operator must come back to approve) — no bell, no command;
- a `--auto` run that **fails its verify gate and rolls back** (the outcome is
  not success, but the user left to check).

Both are "the agent wants input / the run did not finish cleanly" states, which
is literally what the doc's first paragraph says the feature is *for* ("so you
can step away and come back when the agent is done **or wants input**"). The
"wants input" half is the deferred part.

## Recommendations

1. **Document the `JICHI_SHELL` override** in one line (the "/bin/sh" claim is
   the default). Cheap, corrects the abbreviation.
2. **Extend `jc_notify_fire` (or a sibling) to the "wants input" and
   "verify-failed/rollback" states**, each with a distinct `JICHI_NOTIFY`
   summary ("jichi: awaiting approval" / "jichi: run rolled back") so a single
   `notify-send` command can surface all three. This is the real feature gap —
   the "or wants input" in the doc's own stated purpose.
3. **A `notify.sh` case** for the new states if/when 2 lands (the existing test
   only covers the success path).

## Limitations
- I did not PTY-drive a live notify; the verification is static (call sites,
  config parse, the shell resolver). The smoke test exists and I counted its
  assertions (3 `t_ok`), not executed it here.
- The "wants input" gap is inferred from the doc's stated purpose + the two
  call sites, not from a spec that forbade it. It may be an explicit deferral
  (checked DEFERRED.md: no row for it. Note the *related but distinct*
  DEFERRED:2190 / M695 finding — the seeded *default* notify command is
  platform-mis-detected on BSDs — is already registered and is about the
  default-command seed, not the missing wants-input firing.)
