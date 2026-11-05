# A Landlock rule set for the top-level shell (batch 14, item (g)) — design note

*The finding: def-330, beside def-149 / O9 — a Landlock rule set for the
top-level shell, batch 14 item (g) of
`docs/internal/drive/2026-10-17-hardening/PLAN.md` (design note first).
Milestone: **unclaimed** — this is a note, not a build. Written 2026-10-23
(run 0075).*

**Status:** form settled for discussion; **not built**.

## Why this

def-330's own words: *"in the normal-user mode it is the main defence
against a command reading the operator's files. Today only
`spawn_parallel` write children are fenced, and for writes only."*

The current fence (src/util/jc_landlock.c, M852's `jc_landlock_permission_error`
path) is **write-only** and applies to **spawn_parallel write children**
(`run_child`'s worktree children). The top-level `run_terminal_command` shell
— the surface a model has the most freedom on, and the one that runs in the
live workspace — has no Landlock rule set at all. A prompt-injected or
hallucinated `cat ~/.jichi.env` from the top-level shell reads the operator's
files today; the write fence would not stop it.

The drive's F3 hypothesis (supervisor 13:39, card 87: a Landlock-fenced write
child may still `connect()` to the lead's control socket) is the same class —
the fence does not cover the socket surface — and is to be tested, not
assumed, when this is built.

## The shape (proposed)

A read-write rule set for the top-level shell, applied when:

- the run is **normal-user mode** (not `--lite`/low-resource, not the
  supervised drive, where the supervisor's fences and the grant govern);
- the model calls `run_terminal_command` with a **write-able** command
  (the existing privileged/kinetic classifier's `write` class — a command
  the model is not just reading output from);
- the workspace is a git repo with a fenceable root (the existing
  `pathFence` / `--edit-scope` root, so the rule set's path set is the one
  the edit fence already bounds).

The rule set grants:

- **read** on the workspace root and its descendants (the model must read
  the code it is editing — a read-only fence on the workspace is not the
  finding; def-330 is about the operator's files *outside* the workspace);
- **write** on the workspace root and its descendants (a top-level shell
  that writes is expected to write inside the fence);
- **no read** on anything else (the operator's home outside the workspace,
  `~/.jichi.env`, other repos);
- **no write** anywhere outside the workspace (the path fence's existing
  guarantee, now at the syscall layer instead of only at jichi's tool
  layer);
- **no connect** on anything (the F3 surface — a shell that `connect()`s is
  out of scope for a file fence, but Landlock's `NET_BIND`/`NET_CONNECT`
  rules would cover it if the operator wants the network surface fenced too;
  this is an open question, not a default).

The refusal message reuses `jc_landlock_permission_error`'s wording, extended
to name the path the command was refused on and the rule set that refused it,
so the model can read a `cat ~/.jichi.env` refusal as "outside the workspace
fence," not "the sandbox broke."

## The cost

Landlock is a kernel feature (`SYS_landlock_create_ruleset` = 444, already
probed in jc_landlock.c). The cost is the **probe and the fallback**: on a
kernel without Landlock, the top-level shell runs unfenced (today's
behaviour), and `doctor` names it ("no Landlock for the top-level shell; the
write fence is tool-layer only"). The probe's wording follows the
capability-probe doctrine (M449: `-Werror=implicit-function-declaration`,
the right question, not "does the symbol link").

## Open questions for the operator

1. **Read or read-write fence?** def-330's words are "defence against a
   command *reading* the operator's files," which is a read fence. But a
   top-level shell that writes inside the workspace must also write; a
   read-only fence on the workspace breaks the common `write_file` +
   `run_terminal_command` flow. Is the rule set read-write inside the fence,
   read-only outside (recommended), or read-only everywhere with writes
   routed to the `spawn_parallel` worktree path only?
2. **The network surface.** F3 asks whether a fenced child can still
   `connect()`. Landlock's `NET_BIND`/`NET_CONNECT` rules would fence the
   socket layer, but a top-level shell that `curl`s the model's apiBase
   (the HRZ gateway) is *expected* to connect. The fence would need an
   allow-list of the provider's hosts — a surface the drive's free-models-only
   rule already bounds. Is the network surface in scope for this note, or a
   separate one (F3's test)?
3. **Which mode gates it.** "Normal-user mode" is the finding's words, but
   jichi has no explicit normal/user mode flag — the distinction is
   `--lite`/`lowResource` off, `pathFence` on, and the supervisor's fences
   absent. Name the exact predicate, or add a config key (e.g.
   `strictness.topShellFence` beside M1006's `strictness.dirtyDispatch`)?
4. **The refusal message's altitude.** The model reads a tool result. If the
   kernel refuses a `cat`, the shell exits non-zero with the kernel's errno
   (EACCES), not a jichi message — `jc_landlock_permission_error` only
   formats it if jichi sees the errno. The top-level shell's result must
   carry the errno and the formatted message, or the model sees "Permission
   denied" and cannot tell it is the fence. Is that a `run_terminal_command`
   result change, or a separate probe?
