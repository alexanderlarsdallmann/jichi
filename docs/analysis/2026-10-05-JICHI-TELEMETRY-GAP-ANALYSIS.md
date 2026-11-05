# jichi gap analysis — telemetry / event-log redaction (data flow)

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

jichi's opt-in event log (`src/util/jc_eventlog.c`) writes **full tool
arguments** at its `JC_EVENTLOG_FULL` tier — `args_full` for every tool call
(`src/chat/jc_agent.c:3885`), which for `edit_file` is the *entire content of
the file being edited* (`old_string`/`new_string`), and for
`run_terminal_command` is the *whole shell command*, plus `output`
(`:3887`). Redaction *is* applied to these fields (M132/M155: every
`jc_eventlog_add_text` routes through the scrubber) — but the scrubber is a
**registered-value allow-list** whose only members are **API keys**
(`src/main.c:15987,15993`, `src/command/jc_refs.c:385`). So the log redacts
the three configured key strings and transcribes everything else — paths,
workspace layout, command contents, file contents. The register concedes the
log "may hold prompts" (ANECDOTES.md:3706, the M132 comment) and its defense
of the *unregistered* content is a **0700 directory** — a *permission*, not a
*transformation*. There is no redaction *class* for paths/commands/file
content, and no test that shares the log (the obvious reason to have one) and
checks what leaks. The control that matches the threat model ("I share this
log to file a bug / show a reviewer / feed it to a dashboard") does not
exist.

## 1. What exists — and why it is a strength

- **The log is opt-in and scoped.** `jc_eventlog` is "the opt-in JSONL event
  sink" (tests/test_eventlog.c:4); "OFF level leaves it disabled even with a
  path" (test_eventlog.c:32-33). It is not a silent always-on recorder.
- **It is privacy-protected by *permission*.** The parent directory is
  `jc_make_private`'d 0700 — and the register says *why* in the code comment:
  "0700: telemetry may hold prompts (M132)" (ANECDOTES.md:3706). The register
  *knows* the log holds sensitive text and isolates it by mode bits.
- **Every log field passes through a scrubber — but only for *registered* values.**
  `jc_eventlog_add_text` (jc_eventlog.c:259-273) calls
  `jc_redact_secrets` on every string before it lands on disk, at *both* tiers
  (M132: "the full tier records raw prompts + tool I/O, which may echo a key";
  M155: the metrics-tier summary "was NOT scrubbed (only full-tier fields
  were) — route it through the redactor so a secret in a command can't leak").
  The scrubber (`jc_log.c:163`) masks a value only if it is in `g_secrets`, and
  the only strings ever registered are **API keys** (main.c:15987
  `jc_redact_register(km->api_key)`, main.c:15993
  `jc_redact_register(app.config.search.api_key)`) and a `/ref` value
  (jc_refs.c:385).
- **UTF-8 is sanitized and text is bounded.** `jc_utf8_sanitize` on each line
  (jc_eventlog.c:241); truncation "backing off a split UTF-8 sequence"
  (jc_eventlog.c:283). The log is not a raw byte dump.

The net: the log is opt-in, permission-protected, and every field passes
through a scrubber — but the scrubber is an **allow-list of registered
values**, and only API keys (plus a `/ref` value) are ever registered.
Everything else the tool calls reveal — paths, commands, file contents —
passes the scrubber *unchanged*.

## 2. The one thing definitely lacking

**Redaction of *content* — the workspace's actual data — in the log.** The
scrubbing mechanism is *value-allow-list*: only strings passed to
`jc_redact_register` are masked. Verified: the only callers are the two API-key
sites (main.c:15987,15993) and the `/ref` value (jc_refs.c:385). So:

- **`edit_file`'s `old_string`/`new_string` are logged unredacted at the full
  tier** — i.e. the *content* of whatever file the agent touched, in
  `args_full` (jc_agent.c:3885). The scrubber runs over it, but the file's
  bytes are not a registered value, so they pass through unchanged. An `.env`,
  a private config, a source file with a hardcoded credential that *isn't the
  configured API key* — all land in the log.
- **`run_terminal_command`'s command is logged unredacted** — the full shell
  string at the full tier, and a *slice of the command/path* at the metrics
  tier (M155, jc_agent.c:3862-3872) — which often embeds paths and inline
  data (`echo "TOKEN=$X" > /tmp/f`).
- **File *paths* are logged unredacted** — the workspace layout, which on a
  real machine can be identifying (project names, usernames in paths).

The 0700 mode protects against *other users on the same machine*. It does
nothing for the log's actual consumers — the operator sharing it to debug, a
reviewer, a dashboard, or the agent's own analytics pipeline. For those, the
log is a *transcript of the workspace's edits and commands* in which the only
content ever masked is the two configured API-key strings (and a `/ref`
value); everything else — paths, commands, file contents — is the plaintext
the agent actually handled.

## 3. Why "definitely lacking" (alternatives and their status)

- *"It's opt-in, so the operator chose to log."* Opt-in controls *whether* a
  log exists; it does not control *what class* of content it holds. The
  operator who enables telemetry "to see what the run did" is not, by that
  choice, accepting a verbatim copy of every file the agent edited. The
  register's own M132 comment shows it knows the content is sensitive and
  picked *permission* as the control — but permission is the wrong control for
  the sharing threat, and nothing says the operator must 0700-forever any log
  they intend to send to someone.
- *"API keys are the crown jewels; that's redacted."* True, and it's the one
  registered class. But the log's *data exposure* is far broader than the API
  key: it is every file-content edit and every command. A log that redacts the
  three configured key strings and transcribes the rest is not "redacted" in
  the sense the word is used elsewhere in the register — it is
  "permission-protected with three values masked."
- *"The audit log (jc_audit.c) is separate and always-on; maybe it redacts."*
  The audit log records *privileged-command attempts* (a different artifact).
  The *telemetry* event log — the one that holds `args_full` and "may hold
  prompts" (M132) — is the one analyzed here. Confusing the two would be the
  M326b shape: a check that passes because its universe shrank to the artifact
  that happens to be more protected.
- *"Content is truncated (jc_eventlog.c:283), so it's bounded."* Bounded
  length ≠ redacted. A 4 KB cap on a `.env` still logs 4 KB of the credential.
  Truncation limits *size*, not *sensitivity*.

## 4. Evidence

- **`src/chat/jc_agent.c:3885,3903`** — the tool-call event logs
  `jc_eventlog_add_text(o, "args_full", args_copy, ...)` (the *full* arguments)
  and (for the transcript) `jc_eventlog_add_text(to, "args", args_copy, ...)`.
  `args_copy` is the tool's JSON argument string — for `edit_file` that is the
  file's `old_string`/`new_string`. (Verified.)
- **`src/util/jc_eventlog.c:259-273`** — `jc_eventlog_add_text` routes *every*
  field through `jc_redact_secrets` before disk (M132: "the full tier records
  raw prompts + tool I/O, which may echo a key"; M155, `jc_agent.c:3864-3872`:
  the metrics-tier command/path summary "was NOT scrubbed (only full-tier
  fields were) — route it through the redactor"). So the scrubber is applied;
  its *coverage* is the registered-value set, which is the gap. (Verified.)
- **`src/util/jc_log.c:163`** — `jc_redact_secrets` = `jc_redact_apply(g_secrets,
  g_nsecrets, ...)`: redaction is driven by `g_secrets`, the registered set.
  (Verified.)
- **The only registrars** — `grep -rn "jc_redact_register" src/ | grep -v
  jc_log.c`: `src/command/jc_refs.c:385` (a `/ref` value), `src/main.c:15987`
  (`km->api_key`), `src/main.c:15993` (`app.config.search.api_key`). **No
  registrar for paths, commands, or file content.** (Verified.)
- **`src/util/jc_log.c:115-116`** — the redaction emits `***` (bounded).
  (Verified.)
- **`docs/ANECDOTES.md:3706`** — `jc_make_private(dir); /* 0700: telemetry may
  hold prompts (M132) */`. (Verified — the register knows the log holds
  sensitive text; the control is a mode bit.)
- **`tests/test_eventlog.c:4,32-33`** — "the opt-in JSONL event sink";
  "OFF level leaves it disabled even with a path." (Verified.)
- **`src/util/jc_eventlog.c:241,283`** — `jc_utf8_sanitize` per line;
  truncation backing off a split UTF-8 sequence. (Verified — sanitization and
  bounding exist; they are not content redaction.)
- **No sharing-leak test** — `grep -rln "redact\|args_full\|args" tests/smoke/`
  for a telemetry-share driver: none plants sensitive content in a tool call,
  enables the log, and asserts it is *absent* from the log (or present-but-
  masked). (Verified by search.)

## 5. Implications

- **At the full tier, a shared log is a near-plaintext workspace transcript.**
  Anyone who receives the JSONL gets the agent's edits *as file content*, its
  commands *as shell strings*, and its file *paths* — the scrubber having masked
  only the two configured API-key values (and a `/ref` value). The M132 comment
  ("may hold prompts") understates it: it holds more than prompts; it holds
  the *full arguments and output* of every tool call.
- **The control is orthogonal to the threat.** 0700 defends against the
  co-resident user; the log's real risk is *transmission* (share-to-debug,
  dashboard, cross-machine). A permission cannot be carried in a file that is
  `cp`'d or `git commit`'d (a log inside a workspace is a *secrets-in-repo*
  incident waiting to happen — the register's own snapshot_lint M484 was built
  precisely because a manual audit missed "three compiled binaries, six copies
  of the author's email").
- **It is the register's most under-protected data path.** Every other
  sensitive artifact has a *transformative* control: prompts are fenced
  (untrusted), configs are trust-gated (M851), secrets-in-repo are linted
  (M484), the tool-output spill is 0600 *and* claim-ticketed (M348). The
  telemetry log's only control is a directory mode. That asymmetry is the
  seam.

## 6. Recommendation

1. **Content redaction, not just permission.** Add a redaction *class* the
   event log applies to `args`/`args_full`: mask (a) absolute paths to a
   workspace-relative or `~/<anon>` form, (b) a configurable "redact these
   substrings" list beyond the API keys (so the operator can name `.env`
   values, tokens), and (c) optionally *drop `args_full` by default* (log the
   tool name + a bounded summary, and require an explicit `argsFull: true` for
   the verbatim version — the "sensitive detail is opt-*in* within the opt-in
   log" shape).
2. **A red-first leak driver.** Plant a tool call whose argument contains a
   canary (a fake token, a path with a username), enable the log, and assert
   the canary is **absent** from the emitted JSONL (or masked to `***`). Before
   the fix: red (the canary is in `args_full`). After: green. This is the test
   the register built for secrets-in-repo (snapshot_lint M484) applied to the
   log instead of the tree.
3. **One line in the telemetry doc** next to M132: the log is 0700-protected
   *and* content-redacted (paths + registered values +, after step 1, a
   bounded `args_full`); sharing the log no longer transcribes file contents.

Rejected, in the register's spirit:

* *Rely on 0700 + operator discipline.* The register's own M484 history
  (snapshot_lint.sh header: the manual audit "found what it knew to look for,"
  missing binaries/emails/paths) is the standing argument that the *transitive*
  path — someone sharing the file — is where permissions stop being a control.
* *Redact everything, make the log useless.* Overkill; the operator needs the
  tool sequence to debug. The fix is to redact *identifiers and secret
  content* (paths, registered values, verbatim args) while keeping the
  structure (which tools, in what order, what error) — a privacy-preserving
  transcript, not an empty one.

## 7. Limitations and honest counterarguments

1. **The log is genuinely opt-in, and API keys *are* scrubbed.** I am not
   claiming jichi exfiltrates or logs keys — it registers the two configured
   key values and masks them. The gap is that "what's registered" is a 3-line
   allow-list and "what's logged" is the full argument set.
2. **M132's 0700 is a *real* control for its *real* threat** (the co-resident
   user). I do not dismiss it; I scope the gap to the *transmission* threat,
   where a mode bit cannot travel.
3. **I have not enabled the log and shared it** (read-only). The "args_full
   logs file content verbatim" claim is verified by reading jc_agent.c:3885
   and the registrar list; the *consequence* (a shared log leaks) is the
   arithmetic of (full args logged) × (no content redaction) × (log is a file
   you can send). Step 2's driver is what executes it.
4. **Truncation and UTF-8 sanitization *are* data-handling** — but they bound
   *size* and *encoding*, not *sensitivity*. I do not count them as redaction.
5. **The audit log (jc_audit.c) is a separate, always-on artifact** and I did
   not analyze it here; its redaction posture may differ. This analysis is the
   *telemetry* event log specifically.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the log writes full tool args verbatim
    sed -n '3883,3886p' src/chat/jc_agent.c              # "args_full"
    sed -n '3900,3905p' src/chat/jc_agent.c              # transcript "args"

    # redaction is a registered-value allow-list
    sed -n '163,166p' src/util/jc_log.c                   # jc_redact_secrets = apply(g_secrets)
    grep -rn "jc_redact_register" src/ --include=*.c | grep -v jc_log.c
                                                          # only /ref, api_key, search.api_key

    # the control is a permission, and the register knows it
    sed -n '3706p' docs/ANECDOTES.md                      # "0700: telemetry may hold prompts (M132)"

    # opt-in + UTF-8 sanitize + truncate (bounding, not redaction)
    sed -n '4p;32,33p' tests/test_eventlog.c
    sed -n '241p;283p' src/util/jc_eventlog.c

    # no driver that plants a canary in an arg and asserts it's absent from the log
    grep -rln "args_full\|redact" tests/smoke/ | grep -i "telem\|eventlog"      # (expect none)

**Verdict line for the register this could seed:** jichi's opt-in telemetry
event log (src/util/jc_eventlog.c, tests/test_eventlog.c) routes every field
through the registered-value scrubber (M132/M155) — but at its full tier it
logs the full tool arguments, `args_full` (jc_agent.c:3885), which for
`edit_file` is the *content of the file being edited* and for
`run_terminal_command` is the whole shell string, and for all of them the
file *paths*. The only strings registered for redaction are the two configured
API keys (main.c:15987,15993) and a `/ref` value (jc_refs.c:385);
`jc_redact_secrets` (jc_log.c:163) is an allow-list over exactly those. The
register knows the log holds sensitive text — the M132 comment says "0700:
telemetry may hold prompts" (ANECDOTES.md:3706) — and its entire control for
the *unregistered* content is a directory mode bit, which protects the
co-resident user but not the log's real
consumers (a debug share, a reviewer, a dashboard, a cross-machine copy). Every
other sensitive artifact has a *transformative* control (prompts fenced, configs
trust-gated M851, secrets-in-repo linted M484, tool-output spill 0600+
claim-ticketed M348); the telemetry log's only control is permission, so it is
the most under-protected data path in the register. Definitely lacking:
(1) content redaction in the log — mask paths, redact a configurable
beyond-the-API-keys value list, and drop verbatim `args_full` to an explicit
opt-in; (2) a red-first driver that plants a canary in a tool argument and
asserts it is absent from the emitted JSONL — the snapshot_lint M484 "secrets
in the artifact" pattern applied to the log. Revisit when: a shared log no
longer transcribes file contents and commands, and a test proves the canary
stays out — at which point M132's "may hold prompts" is replaced by "holds a
privacy-preserving transcript."
