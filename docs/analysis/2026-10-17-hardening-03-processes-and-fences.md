# Hardening review 3 — Processes, exec, and the fences

*Part of the 2026-10-17 hardening review; start with [the overview](2026-10-17-hardening-00-overview.md). Drafted by reviewer B (Claude Opus 5.5), edited by the review's lead, who re-ran the central measurements. All code citations are at commit
`e5d832a4` (origin/master that day). Two unpushed commits from the project's own
session exist and are named where they bear: `bac217bd` (the in-flight M2 git
work) and `e1704658` (docs).*

## Who this chapter is for

A self-learner or junior developer who knows a little C and does not yet know
jichi. After reading it you will understand how jichi starts a child process,
what a *fence* is in jichi's vocabulary and how its fences are built, which of
them hold and which have a gap at this commit, and — the part worth carrying to
any codebase — *why* a security check so often fails not because the check is
wrong but because it reads a slightly different thing than the code it is meant to
guard.

**What you need first.** Nothing installed. It helps to have met the project's
documentation standard once (`docs/DOC_REVIEW.md`), because this chapter follows
it: every term is defined on first use, every recommendation says what to do
next, and every claim is marked for how it was checked.

**How claims are marked.** `[V]` means I read the code path end to end and cite
`file:line`. `[M]` means I (or the review lead) ran something and the output is
given or cited. `[R]` means read but not traced to the end. `[GK]` is general
knowledge about a platform or a library, not checked here. Severities use the
project's scale: **high** (a crash, data loss, or a security boundary that does
not hold), **medium** (a wrong or misleading result in a plausible case), **low**
(an edge case or a maintainability cost), **note** (worth knowing, not a defect).

Security findings are stated here **by class only** — what kind of defect, how
severe, which file, and the shape of the fix — because the project promises
reporters that a defect is described publicly by class until it is fixed
(`SECURITY.md`). The exact inputs, the reproductions, and the measured output
live in the internal companion to this chapter.

## What this area is

jichi is an AI coding agent: a language model proposes **tool calls** and jichi
runs them. A handful of those tools start a child process — a shell command,
`git`, a test runner, a background job, a sub-agent, a language server (LSP), an
MCP (Model Context Protocol) server. Everything in this chapter is either *how a
child is started* or *whether an action is allowed to happen at all*.

Two words recur. A **fence** bounds what an action can reach and stays on — the
path fence (writes stay inside the workspace), the edit scope (writes stay inside
a set of globs the operator named), the Landlock shell fence (a child's shell
cannot write outside its worktree). A **cap** bounds how *long* something runs —
a timeout, a token budget — and is meant to fire. The distinction matters: a
fence that quietly fails is a security defect; a cap that quietly fails wastes
time or, as you will see, hangs a run.

### One door for starting a child

For most of jichi's life each of its eight "fork then exec another program" sites
carried its own copy of the child setup. M832 (closing the long-standing
`def-33`) moved that into one translation unit, `src/util/jc_spawn.c`. A caller
fills a `struct jc_spawn_req` — what to run, where its stdin/stdout/stderr go,
whether it leads its own process group, whether to scrub secret environment
variables, any extra environment, a working directory — and calls `jc_spawn`.
There are two backends chosen at run time: the historical **fork** backend (on
every system with `fork`), and a **posix_spawn** backend for the fork-less ports.

The child half of the fork backend is the load-bearing part, and it is worth
reading once:

```c
/* src/util/jc_spawn.c:112-141 -- child_run(), the fork backend's child */
static void child_run(const struct jc_spawn_req *req)
{
    if (req->new_pgroup) {
        setpgid(0, 0);
    }
    child_wire_fd(req->fd_stdin,  0);
    child_wire_fd(req->fd_stdout, 1);
    child_wire_fd(req->fd_stderr, 2);
    if (req->cwd != NULL && req->cwd[0] != '\0') {
        if (chdir(req->cwd) != 0) {
            _exit(126);
        }
    }
    if (req->scrub_env) {
        jc_proc_scrub_secret_env();
    }
    child_apply_env(req->env);
    /* The backstop and the reset sit IMMEDIATELY before each exec, per branch:
     * posix_utils_lint checks 6/6c hold them adjacent by position, so a new
     * exec cannot drift away from either. */
    if (req->argv != NULL) {
        jc_proc_child_close_fds(); /* M472: not our fds */
        jc_proc_child_sigreset();  /* M461: not our SIG_IGN */
        execvp(req->argv[0], req->argv);
    } else if (req->shell_cmd != NULL) {
        jc_proc_child_close_fds(); /* M472 */
        jc_proc_child_sigreset();  /* M461 */
        execl(jc_shell_path(), "sh", "-c", req->shell_cmd, (char *)NULL);
    }
    _exit(127);
```

Each line here is a past incident. A child used to inherit jichi's API keys in
its environment, so a model-issued shell could read them (fixed at M130 — the
scrub). A child used to inherit jichi's open descriptors — the run journal, the
telemetry sink, the live provider socket — and a model-issued `echo … >&3` once
forged a record in the very journal an unattended run is gated on (fixed at M472
— every descriptor jichi opens is marked *close-on-exec* at creation, with the
close loop above as a backstop). `jc_spawn` makes these impossible to forget per
call site, and `tests/smoke/spawn_backend.sh` and `tests/smoke/child_fds.sh`
assert the *result* on both backends. This is the single best structural decision
in the area.

### The gates, in order

Before a tool runs, the agent loop in `src/chat/jc_agent.c` resolves one
**canonical** tool name and decides on that, never on the raw name the model
sent:

```c
/* src/chat/jc_agent.c:3147 */
gate_name = jc_tool_canonical_name(name_copy != NULL ? name_copy : "");
```

This matters because `jc_tool_execute` resolves aliases before running — a model
that calls `create_file` runs `write_file`. M532 found that a gate checking the
raw name missed the alias: with `create_file` the deny list, the edit scope and
the privileged audit all failed to fire. The rule it set — *a gate must decide on
what will run; a message must say what was asked* — is the theme of this chapter.

With the canonical name in hand the loop runs, in order: the allowed-tools fence,
the permission verdict (`jc_perm_for_tool`: deny wins, then plan mode, then the
allow list, then the MCP policy, then the mode baseline), the authored-constraint
gate, the privileged-command gate (sudo/doas/… detected *below* the approval
verdict, so an "approve everything" grant cannot satisfy it), the kinetic gate
(hardware actuation), the interactive prompt, and the edit-scope/path fence. Each
refusal is returned as a *value* the model reads, never a crash.

## Design decisions worth carrying

**One spawn door, fork as the measured default (M832, closes `def-33`).** The
reason was portability: fork-less targets (FreeDOS/DJGPP, AROS, NuttX) cannot
`fork`, and eight sites that only forked-to-exec could instead *describe* a child.
The good implication is that the scrub, the close-fds backstop and the SIGPIPE
reset live in one place and are tested on both backends. The subtle cost is that
"two backends behind one API" are not identical — exec failure is a child
`_exit(127)` on fork versus a start failure on spawn, and the spawn backend does
a parent-side `chdir` round trip — so the differences are where bugs hide
(finding B-09 below). **Lesson:** when you hide two code paths behind one
function, write down the differences that remain and test each; "it's the same
contract" is where the next bug lives.

**Gates decide on the canonical name (M532; see `DECISIONS.md` M532/M591).** The
decision was forced by a measured bypass — `permissions.deny: ["write_file"]`
failed to stop a `create_file` call. **Lesson:** a security check and the code it
guards must agree on the *identity* of what is happening; two spellings of one
action are a bypass waiting to be found.

**Privileged and kinetic gates sit below the approval verdict (M153/M163a).** A
blanket "auto" or a terminal "always" grant is exactly what let an agent escalate
privilege unbidden, so a `sudo`/`doas` command is re-checked *after* the verdict,
and an unattended run refuses it, with an always-on audit log. **Lesson:** the one
gate that guards an irreversible action must not be satisfiable by a convenience
grant the user gave for everything else.

**The shell fence is kernel-enforced, applied before the child's work, and
refuses when it cannot apply (M831/M852).** A `spawn_parallel` write child edits
in an isolated git worktree and its *file tools* are path-fenced to it — but its
*shell* could write anywhere the process can, and a check on the command *text* is
not a fence (a variable, a `cd ..`, or a script the child writes then runs all
defeat it). So the fence is Linux **Landlock**: a kernel write restriction,
inherited across `exec`, applied in the forked child before the agent loop
(`src/util/jc_landlock.c`, `src/tools/jc_tool_parallel.c:448`). Where Landlock is
absent the child runs unfenced but **never silently** (a warning, a telemetry
event, and `doctor` reports the capability); where Landlock is present but the
rule cannot be built, the task is **refused** rather than run unfenced. **Lesson:**
a guard on what a program *says* it will do is not a guard on what it *can* do;
and a fence that cannot be applied should fail closed and loudly, not quietly
degrade.

**Config trust: a project config is filtered to safe keys until `jichi trust`
(M851).** A file checked into a repository must not silently turn on auto mode,
add an MCP server, or loosen a policy, so `src/config/jc_config_trust.c` carries a
table classifying **every** top-level key as safe, narrowing, or gated, with a
two-way lint that fails the build if the table and the loader disagree.
**Lesson:** "who wrote this input" is the deciding question, and a default-deny
table with a lint that checks it both ways is how you keep that honest as the
config grows.

## Findings

The blocks below are ordered by what a maintainer should fix first. Each carries the review's finding id (B-nn for this area; A-01 comes from chapter 2), the id the overview and the fix plan use.

### B-04 and A-01. The edit-scope fence does not read arguments the way the executor does — high — [M] — new

**Class.** The edit scope (`--edit-scope`, a set of globs that bounds which files
a run may write) is checked by code that parses and interprets the tool arguments
*differently* from the code that then performs the write. Two independent argument
shapes a model emits on its own slip past the check and reach the executor, which
normalises them and writes the file. This is one defect with two faces, and both
are instances of a rule the project already states for its approval previews
(M530): **a check must read every argument exactly as the executor will.**

- **Face one — path shape.** The scope check matches the path *as text* and does
  not normalise a path that walks back up with `..` before matching it against the
  globs; the write then resolves the path and lands outside the scope.
  (`src/chat/jc_envelope.c:218` `jc_env_path_in_scope` → `jc_env_relpath:142`;
  the check site is `src/chat/jc_agent.c:1036`.)
- **Face two — argument shape.** jichi helpfully normalises two argument shapes a
  model commonly sends — a call whose arguments are wrapped in an object named
  after the tool, and an array sent as a JSON string — but it does so *inside*
  `jc_tool_execute` (`src/tools/jc_tool.c:1774`, `:1800`), on a tree the scope
  check (`src/chat/jc_agent.c:999`) never saw. M532 taught the scope check to run
  one of the three normalisers (`jc_jsonrepair`) but not these two. (Reviewer A
  found and measured face two; the review lead reproduced it, measurement L-13.)

**Severity.** High: a fence the operator sets to bound a run's blast radius does
not hold. The reach is narrow — the write is still contained to the workspace by
the path fence, and the actor is the model itself, not a third party — but a
fence that silently passes what it was configured to refuse is the class the
project treats as serious. The review lead reproduced both faces end to end
(measurements L-08 and L-13); the end-of-turn `revertOutOfScope` sweep is the only
backstop and it is **off by default** (`src/config/jc_config.c:1605`), so with it
off the write stands.

**Recommendation (smallest correct fix).** Normalise once, before the fence, and
assert the invariant that *the bytes the fence inspects are the bytes the tool
acts on.* Concretely: (1) for the path shape, normalise or reject `..` before the
glob — `jc_path_normalize` (`src/util/jc_path.c:65`) already rejects `..` and is
the natural reuse; (2) for the argument shape, factor the unwrap + un-string +
repair sequence into one helper and call it in `call_out_of_scope_path`
(`src/chat/jc_agent.c:977`) and in the parallel-write merge check
(`src/tools/jc_tool_parallel.c:2746`), or move the normalisation above the point
where the arguments fork toward the fence and the tool so both see the same tree.
There is precedent for choosing to normalise-before-check in this codebase: the
assignment verbs reject a location before anything opens (M529).

**The test that would go red first.** A smoke driver with a narrow `--edit-scope`
and a model that emits each bypassing shape, asserting the out-of-scope file is
not created and the model is told the write was refused. Red at this commit (the
file is written), green after. Effort: **M**.

### B-02. A planted repository setting can still make a git child run a program — medium — [M] — new (bears on the in-flight M2 work)

**Class.** jichi's git tools run `git` against the user's repository. git reads
behaviour from that repository's configuration and attributes, and several of
those settings name an external program that git itself then executes. The M2
work (`src/tools/jc_tool_git.c:71`, `src/snapshot/jc_snapshot.c:130`) neutralises
three such settings at the spawn — the file-system monitor, the signature-display
program, and the hooks path — and `bac217bd` adds a fourth, commit signing. Those
four I confirmed are genuinely neutralised. But **another class of setting, which
also names a program and is also run by git itself, is not covered**, so a
repository the user did not write — or a directory a write child can reach — can
still cause a program to run when jichi runs a routine git tool. The review lead
reproduced this with plain git (measurement L-07), confirming it is git's own
behaviour and not specific to jichi's wrapper.

**Severity.** Medium. The blast radius is bounded: such a program runs with the
secret environment scrubbed (M2 added `scrub_env` to the git children), and for a
`spawn_parallel` write child the Landlock shell fence still restricts where it can
write. But it *is* code execution that jichi initiated, and `bac217bd`'s own
message claims "every config-driven exec vector through jichi's fixed argv is
neutralised at spawn", which this shows is not yet true. It is the same class M2
set out to close, with one more member.

**Recommendation.** First, correct the claim to "every *known* vector" and name
this class as not yet covered. Then close it where M2 already plans to: the
deferred "last piece" — denying a write child's shell the ability to write into
the repository's control directory in the first place — removes the way a child
plants such a setting. For the user's-own-repository path, the honest note is that
running jichi's git tools inside an untrusted repository inherits git's own
program-execution behaviour, the same exposure the user would face typing `git`
there themselves; document it in `SECURITY.md`. jichi's own snapshot worktrees are
not exposed, because jichi controls their configuration.

**The test that would go red first.** Extend `tests/smoke/git_neutralisation.sh`
to plant the setting and drive a git tool, asserting the program did not run. Red
today; land it with the mechanism that closes it. Effort: test **S**, fix **L**
(a design decision, already on the M2 plan).

### B-01 and B-06. The shell tool's timeout and interrupt stop applying once the command closes its output — medium — [M] — new

This one is a cap and a process-hygiene defect, not a security boundary, so it is
described in full.

**What happens.** `run_terminal_command` reads the command's combined output in a
loop that also checks the timeout and the interrupt flag. When the command closes
its output but keeps running — a backgrounded job, a step that detaches, a daemon
that daemonises — the read loop ends on end-of-file and the code then waits for
the child with an unbounded wait (`src/chat/jc_app.c:778` then `:821`; the same
shape in `src/util/jc_proc.c:636`, which R06-3 of the 2026-10-08 review recorded
for hooks and verifiers). The per-call `timeout` is gone, and so is the
wall-clock deadline.

Measured in my copy (I ran these):

```
command: `exec >/dev/null 2>&1; sleep 12`  with timeout 2
  -> jichi returned after 12.1 s, exit status 0   (the 2 s timeout never fired)
control: plain `sleep 12`                   with timeout 2
  -> jichi returned after 2.2 s, exit status 124  (timed out, correct)
```

**And the interrupt.** Because jichi is blocked in that unbounded wait when a
Ctrl-C arrives, it never reaches the code that kills the command's process group.
Measured: jichi exited promptly on Ctrl-C (good) but left the backgrounded command
running, re-parented to init. On an unattended run this is the worse half — a
single command can make a turn hang for the command's whole lifetime.

**Severity.** Medium. It defeats the one duration bound the operator set, and it
can orphan a process, but it is not a security boundary.

**Recommendation.** After the read loop ends, replace the blocking wait with a
bounded, interrupt-aware reap that still honours the deadline: poll for the child
in the same timed slices the read loop uses, and on deadline or interrupt send the
*process group* `SIGTERM` then `SIGKILL`. `jc_worker_reap_grace`
(`src/util/jc_workerpool.c`) is almost this shape; a deadline-aware variant serves
both this site and `jc_proc_capture`. Make the popen-path child lead its own
process group as the watched path already does, so the group kill reaches the
whole pipeline.

**The test that would go red first.** A driver that runs a command which closes
its output and then sleeps past its timeout, asserting the call returns promptly
with the timeout status and that no child survives jichi's exit. Red today.
Effort: **M**.

### B-03. A git tool passes a model-controlled name to git without the end-of-options marker — medium — [V] — already recorded (R04-F8, 2026-10-08), still open

**Class.** Most of jichi's git tools insert `--` (the "everything after this is an
operand, not an option" marker) before a model-supplied path, so a path beginning
with `-` cannot be read as a git option. One tool — the branch tool
(`src/tools/jc_tool_git.c:529`, the name is appended at `:545`) — does not. A
model-supplied name that begins with `-` is therefore read by git as an option
rather than a branch name. This is argv-safe (no shell is involved) but it is
*option* injection into git, which can discard work. It is not a new observation —
the 2026-10-08 review recorded it as R04-F8 — and it is untouched by `bac217bd`.

**Severity.** Medium: a tool's fixed-argv fence that does not hold.

**Recommendation.** Insert `--` before the name, exactly as the other git tools
do. One line. **Test:** a check that the branch tool, given a name beginning with
`-`, does not delete or detach anything. Effort: **S**.

### B-05. The privileged-launcher detector does not recognise every form of a wrapped launcher — medium — [V] — already recorded (SA-G6 / R06-17), still open

**Class.** The gate that catches a privileged launcher (`sudo`, `doas`, `pkexec`, `su`, `run0`)
in a shell command (`src/util/jc_priv.c:113`) understands transparent wrappers such as `nice`
and `nohup`, but it does not recognise every form a wrapped or path-qualified launcher can take,
so some forms reach the shell without the gate firing.

**Severity.** Medium. The gate silently does not fire for those forms, and the
audit records nothing. It is bounded by the larger, acknowledged gap that the
top-level shell has no OS sandbox (`def-149`), and a lexer can never be complete
(a launcher assembled at run time is out of scope by design), so this is
defence-in-depth, not a wall.

**Recommendation.** Make the lexer skip a wrapper's own options the way it already
skips `env`'s arguments, and match a launcher by its final path component. **Test:**
a table-driven unit test of the detector over wrapped and path-qualified launcher
forms (the forms are in the internal record). Effort: **M**.

### B-08. Language servers inherit jichi's environment, including the model API keys — medium — [V] — already recorded (R09-F8), still open

**Class.** Every other child jichi starts — the shell, git, background jobs, the
verifier, MCP servers — has its secret environment scrubbed. The language-server
spawn (`src/lsp/jc_lsp.c:95`) does not set that flag, so a configured language
server runs with the model keys in its environment. A language server is a trusted
program (the config key that starts one is gated behind `jichi trust`), so this is
a missing layer of defence-in-depth rather than an open door, but there is no
reason a language server needs the model keys.

**Recommendation.** Set the scrub flag in the LSP spawn, as the MCP spawn does.
One line. **Test:** a mock language server that reports its environment, asserting
the keys are absent — there is currently no test that spawns a language server at
all (R09-F11), which is a gap worth closing alongside. Effort: **S** for the fix,
**M** for the missing test.

### B-07. The per-workspace lease becomes advisory, and silently so, where file locking is unavailable — low — [V] — partly recorded (M760)

**What happens.** jichi takes a per-workspace lease so two copies do not edit one
tree at once (`src/util/jc_lease.c:226`). The read-decide-write is correctly done
under a lock (M760). But when the lock cannot be taken — a filesystem that does
not support `fcntl` locking, such as some network mounts — the take proceeds
anyway, by deliberate choice ("a diagnostic must not become an outage"). The
consequence is that `--lease fail` does not actually guarantee a single writer on
such a filesystem. A warning is logged, which is the honest half.

**Severity.** Low. The lease is a convenience that catches two jichis on one tree,
not a security boundary.

**Recommendation.** Note the degradation in `SECURITY.md`/`docs/HARDENING.md`, and
optionally let a strict mode refuse to start when the lock cannot be taken.
Effort: **S** (documentation).

### B-09. The posix_spawn backend changes jichi's own working directory for the duration of a spawn — low — [V] — already recorded (R06-14), still open

**What happens.** The posix_spawn backend has no portable way to set the child's
working directory, so it changes jichi's own directory, spawns, then changes back
(`src/util/jc_spawn.c:293`). jichi is single-threaded before a fork, so the window
is unobservable in practice, but if the change-back fails (the old directory
vanished mid-call) jichi runs on with a silently moved directory; it logs a
warning, which is the honest half. This path is only reached on the non-default
backend (`JICHI_SPAWN_BACKEND=spawn`).

**Recommendation.** On a failed restore, treat it as fatal to the current turn, or
hold an open handle to the original directory and return to it through that handle.
Effort: **S**.

### B-10. Hooks run only for the top-level agent, and the hook page promises more than the child gets — low — [V] — already recorded (R05-F5)

**What happens.** A `PreToolUse` hook is the project's model-independent gate: a
program the operator writes that can refuse a tool call. It fires only for the
top-level agent (`src/chat/jc_hooks.c:308` returns early when `agent_depth != 0`),
so the tool calls of a sub-agent or a parallel child are not seen by it. The hook
child also inherits jichi's environment minus the secrets (`:253`), while
`docs/HOOKS.md` promises environment variables that are not set. The depth-0 rule
is defensible — hooks are top-level lifecycle — but an operator who believes "every
write is gated by my hook" is wrong for delegated writes. Two related decisions are
open: a hook that cannot start lets the call through (`def-35`), and a hook's
refusal is not counted with the other refusals (`def-40`).

**Recommendation.** State the depth-0 rule in `docs/HOOKS.md` where an operator
reads it, and correct the environment list. Effort: **S** (documentation).

### B-11. The descriptor-closing backstop stops at descriptor 4096 — note — [V] — recorded reasoning, correct

The loop that closes inherited descriptors in a child
(`src/util/jc_proc.c:445`, `:463`) caps its scan at 4096. This is a *backstop*:
the primary mechanism is marking every descriptor close-on-exec at creation, so a
descriptor above 4096 is still closed at `exec`. The cap only matters for a
third-party library that opens a high descriptor without the close-on-exec flag.
The reasoning is sound; I note it so the assumption is on the record. On Linux,
`close_range(2)` (kernel 5.9+) would let the backstop cover the whole table in one
syscall — see the platform notes.

## Platform notes

jichi runs on many systems; this area has something real to say about several.

- **Linux (glibc, musl).** Uses Landlock for the shell fence
  (`src/util/jc_landlock.c`, open-coded syscalls with a runtime capability query,
  so it compiles on any Linux and decides at run time — a clean portability
  choice) and `prctl(PR_SET_DUMPABLE, 0)` to stop a same-user process reading the
  keys out of memory. It does **not** use `close_range(2)` (which would make B-11's
  backstop complete and faster, one syscall instead of up to ~4093 `close` calls)
  nor any OS sandbox on the *top-level* interactive/headless shell (the Landlock
  fence covers only parallel write children; `def-149` tracks the gap). [V] for
  what is used; [GK] for what could be.
- **FreeBSD.** Uses `procctl` to close the memory-read channel. [GK] Capsicum
  capability mode would be a stronger shell-fence equivalent but needs the program
  to pre-open everything; not used, so write children run unfenced there (honestly
  reported).
- **OpenBSD.** [GK] `unveil`/`pledge` is the natural OpenBSD twin of the Landlock
  write fence — `unveil` the workspace read-write and the rest read-only — and is
  not used; OpenBSD gets the in-place environment scrub only (partial).
- **illumos, macOS.** [GK] illumos privileges and the macOS sandbox could bound a
  child; neither is used, and the memory-read protection has no macOS equivalent
  yet.
- **FreeDOS / DJGPP (no `fork`).** This is exactly what the `jc_spawn` request
  struct and the backend switch were built for, but DJGPP has neither `fork` nor
  `posix_spawn`, so it needs a third backend mapping the request onto DJGPP's own
  spawn-and-redirect primitives. [R] The struct is already the right abstraction
  (argv, descriptors, working directory, environment), so a DOS backend is
  additive, not a rewrite; the process-group and SIGPIPE parts are correctly
  no-ops there.
- **Small, 16 KB-page boards (Pi 5, Pi Zero 2 W, LicheeRV Nano).** [V] The fork
  backend's close-fds backstop makes up to ~4093 `close` syscalls per spawn; on a
  slow board driving a long tool loop that is measurable, and `close_range` or
  lowering the cap to the real descriptor limit when it is small would help. Low
  priority.

## Recommendations, ordered by value for effort

1. **Fix B-04 and A-01 (edit-scope normalisation).** Highest value: it closes two bypasses of
   a security fence with one invariant — the fence inspects the bytes the tool
   acts on. Effort M.
2. **Fix B-03 (git `--`) and B-08 (LSP key scrub).** One line each, both close a
   recorded gap. Effort S.
3. **Fix B-01 and B-06 (bounded, interrupt-aware reap after EOF).** Removes a real hang on
   unattended runs and a process leak. Effort M.
4. **Close B-02's class on the M2 plan** and correct the claim in the interim.
   Effort: design (L), with the interim doc change S.
5. **Fix B-05 (privileged-launcher forms).** Defence-in-depth on a gate
   that silently misses forms today. Effort M.
6. **Add `close_range` on Linux (B-11), tidy B-07/B-09, and document B-10.** Low priority. Effort S.

## What is done well

- **One spawn chokepoint with a two-backend contract test** [V+M]. The child
  ritual cannot drift per call site, and the tests assert the *result* on both
  backends, not the attempt.
- **Gates decide on the canonical name** [V+M] — a measured bypass closed at the
  one place that will run.
- **Fences fail closed, and the degraded case is never silent** [V] — the shell
  fence refuses a task it cannot confine, warns when the kernel lacks Landlock,
  and `doctor` reports the capability; the path resolver fails closed on an
  unresolvable path.
- **The descriptor fence is tested by a total, not a blocklist** [V] — the test
  asserts "no descriptor above 3", which catches a leak nobody named in advance.
  That is the right shape for a fence test.
- **The M2 git neutralisation is real where it claims a measured hole** [M] — I
  independently confirmed it blocks the file-system monitor and the hooks path;
  B-02 is a *different* mechanism, not a regression.

## What this chapter did not review

The ACP layer (`src/acp/`), the configuration editor (`jc_configedit.c`), and the
scaffolder (`src/scaffold/`) are outside this chapter. The 32-bit Landlock
ruleset initialisation (R06-16) I did not re-measure; it depends on the 32-bit
build the review lead ran. The daemon worker fork holds the model key in memory by
design (it does not re-exec); the socket it listens on is owner-only — I verified
the socket permission but did not audit every child a daemon worker may start.

## Read further

- **Primary.** `fork(2)`, `execve(2)`, `posix_spawn(3)`, `close_range(2)`,
  `landlock(7)`, `credentials(7)`, `fcntl(2)` (record locking) — the system
  interfaces this chapter's code is built on.
- **Primary.** `gitattributes(5)` and `git-config(1)` — they explain, from git's
  own side, why a repository setting can name a program git runs (B-02).
- **Secondary.** `docs/HARDENING.md` (the project's own hardening record, M130,
  M133, M472, M770), `docs/DECISIONS.md` entries M530 and M532 (the rule this
  chapter turns on), and `SECURITY.md` (what jichi promises and what it defers).
- **Secondary.** `docs/analysis/2026-08-17-source-hardening-audit.md` for the
  descriptor-inheritance incident that M472 fixed.
