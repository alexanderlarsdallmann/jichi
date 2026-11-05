# A fork-free jichi, measured: what needs a process, and what does not

*2026-09-25 (M769). The operator's decisions on the twelve questions, item 4: yes to the
fork-free measuring milestone, "which decides four rows at once". Those rows are AROS/AmigaOS,
FreeDOS, NuttX and a unikernel. Each lacks `fork`, and the plan's table puts all four behind this
measurement. The question the plan asked is below, answered with a build that cannot create a
process.*

## 0. The answer first

- **A read, edit and search agent works without a single process.** A headless turn from a
  build with no `fork` and no `popen` completed, and ran `list_files`, `read_file`,
  `search_code`, `edit_file` and `write_file`, changing the workspace. Its shell call was refused
  as a tool error the model read, *error: failed to start command*, not as a crash.
- **271 of 342 smoke drivers pass** in that build (1,617 checks), and **71 fail**. The unit suite
  gives **13,962 checks, 77 failures**, in the 10 files that start processes. Section 3 groups the
  71 by the feature that needed a process.
- **jichi forks in two ways, and they decide the four rows differently.** Eight of the ten
  `fork()` sites fork only to *run another program*: the shell and `run_tests`, verifiers, git,
  MCP servers, language servers, background commands, and the captured commands (hooks, user
  tools, grading, `pdftotext`). A spawn call
  could replace every one of them: `posix_spawn`, DOS's `spawn`, or an Amiga process launch.
  **Two fork to run jichi's own code in a child:** the parallel pool's children and the daemon's
  per-prompt worker. Those need a real `fork`, threads, or a redesign.
- **Hooks fail open by design**, as `HOOKS.md` states: *the action proceeds*. Without processes
  every hook fails to start, so every PreToolUse hook is off. On a port, that is a fence gone
  silent. It is recorded as a decision to make, not a defect.

## 1. How it was measured

`make NOFORK=1` (new at M769, a measuring build and never a product) compiles jichi with
`-Dfork=jc_nofork_fork -Dpopen=jc_nofork_popen`. The two stand-ins in
`src/platform/jc_platform_posix.c` refuse the way a system without them would: `-1` or `NULL`,
with `errno` set to `ENOSYS`. The rename happens in the preprocessor, so no call site changed,
threadwork's two files included. The smoke tools keep their own flags and still fork, so the
harness can drive the build it tests. `jichi --version` says `build: NOFORK=1`, and
`make fallbacks-compile`, part of `make ci`, keeps the configuration compiling for every file
that calls `fork` or `popen`.

To reproduce:

```sh
make clean && make -j8 NOFORK=1 jichi smoke-tools
make NOFORK=1 test                          # 13,962 checks, 77 failures (2026-09-25)
JC_SMOKE_KEEP_GOING=1 make NOFORK=1 smoke   # 271 of 342 drivers
make clean && make                          # back to a normal build
```

The probe for section 0's first line is a scripted mock-model turn of six tool calls. Its marker
for *the shell ran* is produced by arithmetic (`echo SHELL_$((40+2))_RAN`), so it can appear only
in real output, never in the command. Against the normal build that check fires, which shows it
can.

## 2. The ten `fork()` sites

| Site | What the child does | Replaceable by a spawn call? |
|---|---|---|
| `src/util/jc_proc.c` | runs a program and captures it: hooks, user tools, grading, `pdftotext`, sound, notifications, and the `@`-references that run a command | yes |
| `src/chat/jc_app.c` | the shell tool and `run_tests`: `/bin/sh -c` under the memory watchdog (M117) | yes |
| `src/chat/jc_bg.c` | a background command | yes, but it must be concurrent |
| `src/chat/jc_envelope.c` | the envelope's verifier | yes |
| `src/snapshot/jc_snapshot.c` | git, for checkpoints and undo | yes |
| `src/tools/jc_tool_git.c` | git, for the git tools | yes |
| `src/mcp/jc_mcp_stdio.c` | an MCP server over stdio | yes, but it must be concurrent |
| `src/lsp/jc_lsp.c` | a language server | yes, but it must be concurrent |
| `src/tools/jc_tool_parallel.c` | **jichi's own agent loop**, one per parallel task | **no**: needs `fork`, threads, or a redesign |
| `src/main.c` (the daemon) | **jichi's own turn**, in a copy-on-write worker per prompt | **no**, the same |

`popen` has one call site, `jc_proc_popen`. It is the ninth way to start a program, and it is
replaceable the same way.

## 3. The 71 drivers, by the feature that needed a process

Each is classified by its first failing line, read one by one, not by its name.

| Feature | Drivers |
|---|---|
| the shell and `run_tests` | `shell_stderr_captured`, `shell_wrote_nothing`, `run_kill_note`, `run_outcome_agrees`, `outcome_join`, `secret_env_many_names`, `child_fds`, `constraint_advisory`, `kinetic`, `format_command`, `fence_write_tools`, `search_grep_dash_i` (its fallback runs `grep`), `no_progress`, `budget_stop_verdict`, `hollow_green_note`, `elide_ticket` |
| verifiers and the envelope's baseline | `verify_kind`, `verify_consistency_periodic`, `verify_stuck_periodic`, `baseline_checkpoint`, `no_changes_field`, `probe_says_why`, `brief_check`, `secret_env_subcommands` |
| git: checkpoints, undo, rewind, worktrees | `undo_scope`, `undo_across_branch`, `undo_note`, `rewind`, `revert_provenance`, `journal_trackedness`, `attempt_guard`, `attempt_tainted`, `improve`, `improve_tainted`, `prune_worktrees` |
| grading: specs whose tests run as commands | `grade`, `grade_cannot_run`, `grade_expect_fail`, `grade_wrongdir`, `hint_record`, `assignments_stages`, `assignment_verbs`, `design_objections`, `reading_review` |
| background commands | `bg` |
| MCP servers over stdio | `mcp`, `mcp_prompt`, `mcp_ref`, `peer_line_cap`, `peer_reap_grace`, `context_tools_live` |
| the parallel pool and delegation by process | `parallel_abort`, `parallel_merge`, `parallel_timeout_msg`, `subtask_persona`, `supervisor`, `readonly_delegate` (see section 5) |
| hooks | `hooks` |
| external helpers | `pdf`, `docs_pdf` (`pdftotext`), `sound` (the player), `notify` |
| the daemon | `daemon` (its reply is *fork failed*, which is honest) |
| drivers that keep a run alive with a shell tool | `control`, `liveness`, `lease`, `ps`, `board_concurrent`, `typeahead_live`, `learner_flow` |
| not a fork-free finding | `build_rev`: the measuring tree was dirty (section 5) |

## 4. What it decides for the four rows

| Row | What it lacks | What a port would keep, measured here | What it would need for the rest |
|---|---|---|---|
| **AROS / AmigaOS 3** | `fork`; process creation is `CreateNewProc` / `SystemTags` | the agent loop, the file tools, search, sessions, the board, memory, the TUI | a spawn backend for the eight run-a-program sites; no parallel pool and no daemon workers |
| **FreeDOS + DJGPP** | `fork`; `spawn` is synchronous | the same | a synchronous spawn backend for the shell, verifiers, git and grading. Background commands, MCP stdio and LSP need concurrency, which a single-tasking OS lacks |
| **NuttX** | `fork`; it has `posix_spawn` and `vfork` | the same | a `posix_spawn` backend restores the eight. The pool and the daemon still need a redesign |
| **a unikernel** | processes at all | the same, and only that | nothing restores the rest in a single address space |

The design the plan's table asks for is therefore narrower than *a fork-free jichi*. It is a
`jc_proc` backend that starts a program without `fork`. That covers eight of ten sites and
every row but the unikernel. The parallel pool and the daemon stay POSIX-only unless they are
redesigned, and nothing measured here says they must be.

## 5. What the measurement found about the tests themselves

- **`readonly_delegate` check 5 blamed the wrong process.** It reported that a read-only parallel
  task wrote the guarded file. The driver routes the mock by request number and assumes that
  request 2 is the child's. Without `fork` the child never started, request 2 was the parent's,
  and the parent, in `--auto`, carried out the child's scripted write. The read-only fence did
  not open: the pool marks a task *fork failed* and runs nothing in its place
  (`jc_tool_parallel.c`).
- **`build_rev` failed on the measuring tree's `-dirty` stamp**, not on anything fork-free.
- **Hooks**: `HOOKS.md` documents that a hook which fails to start lets the action proceed. The
  measurement adds only this: on a fork-free port that is every hook. Whether a PreToolUse hook
  should be able to fail closed is in DEFERRED.
