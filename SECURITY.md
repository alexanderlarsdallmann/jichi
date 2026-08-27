# Security policy

## Reporting a vulnerability

**Do not open a public issue.** Report it privately to the maintainer, whose
address is the one on the commits in this repository, with `jichi security` in
the subject line.

Please include what you ran, on what platform, and what you observed. A proof of
concept is welcome and not required — a clear description of the mechanism is
usually enough, and this project would rather hear a suspicion than not hear it.

There is one maintainer and no service-level agreement to offer. What you will get
is an acknowledgement, an honest assessment of whether it reproduces, and — if it
does — a fix and a public write-up naming the finding and crediting you unless you
ask otherwise. Findings are recorded, including the ones that turn out not to be
defects, because the reasoning is worth as much as the patch.

## What jichi is, from a security point of view

This matters more than usual here, so it is stated plainly rather than assumed.

**jichi runs commands a language model chose.** That is the product, not a flaw in
it. The model reads your files, edits them, and executes shell commands — under
approval gates, but it is the mechanism. Everything below is about bounding that,
never about eliminating it.

The defences that **do not depend on the model's cooperation** are the real ones:

- **The path fence** — file tools resolve through one chokepoint and refuse paths
  outside the workspace; read-only `referenceRoots` widen reads without widening
  writes.
- **Approval gates** — per-tool, per-mode, composed with config allow/deny lists;
  `plan` mode is read-only. A shell command launched under `sudo`/`doas`/`pkexec`
  is detected and gated *below* the ordinary verdict, so a blanket "always" grant
  cannot satisfy it, and every attempt is written to an always-on audit log.
- **The autonomy envelope** — token, wall-clock and tool-call budgets, an
  edit-scope glob fence, a verification gate, and a JSONL journal of what happened.
- **Snapshots** — a shadow git repository whose work tree is your workspace, so
  `/undo` and rollback never touch your own `.git`.
- **Secret handling** — keys come from environment variables named by the config,
  are scrubbed from child environments, and are redacted from logs. jichi's own
  private state (sessions, telemetry, checkpoints) lives outside any workspace.

**Prompt injection is mitigated, not solved.** Content jichi fetches at the
*model's* choosing — a web page, an RSS feed, an MCP resource — is wrapped in an
explicit "data, not instructions" fence, and the convention is stated once in the
system prompt. That is a mitigation whose success depends on the model behaving,
which is exactly why it is listed *after* the defences that do not.
[`docs/HARDENING.md`](docs/HARDENING.md) §6b says so in the same words.

Known-and-accepted gaps are not hidden: [`docs/DEFERRED.md`](docs/DEFERRED.md)
records them with the reasoning, including a check-then-open window in the path
fence and the absence of OS-level sandboxing for `run_terminal_command`. If one of
those matters to your deployment, read
[`docs/HARDENING.md`](docs/HARDENING.md) and
[`docs/AUTONOMY.md`](docs/AUTONOMY.md) before running jichi unattended.

## Supported versions

jichi is distributed as **source only** and there is no back-porting: fixes land on
the current tree. See [`docs/PLATFORMS.md`](docs/PLATFORMS.md) for what has actually
been compiled and gate-run, and [`../CHANGELOG.md`](CHANGELOG.md) for what changed.
