# Review 4 — Tools, subagents and the parallel pool

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on the
complete report for `src/tools/`: every line of its 36 files, about 12,600 lines.
Its six code excerpts were checked line for line against the source. Written by
Claude Opus 5.5 on threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to know what a model can
*do* in jichi, and how one agent starts others. You should know what a tool call
is ([Annai 4](../reading/annai-04-tool-calling.md)).

**After reading it you will be able to:**

- describe the contract between a tool and the agent loop, and why "tool errors
  are values" is enforced in one place;
- explain the difference between `spawn_subagent` and `spawn_parallel`: one runs
  in-process, the other forks children with their own worktrees;
- trace what happens to a parallel child's work when it finishes, fails, is
  stopped or is resumed;
- recognise three classic traps in argument handling: the strict boolean, the
  empty fence and the unchecked option.

**What you need first:** [Annai 5](../reading/annai-05-one-tool-all-the-way-down.md)
follows one tool from the model's request to the result, and
[SUBAGENTS.md](../SUBAGENTS.md) and [PARALLEL.md](../PARALLEL.md) are the
references.

---

## 1. What a tool is

A **tool** is a `struct jc_tool` (`include/jc_tool.h:103`):

- a name and a description;
- a function returning its JSON Schema;
- a `readonly` flag;
- a `run(args, out, app)` function.

Two later flags, `main_agent_only` and `plan_allowed`, default to 0 because C89
fills omitted initialisers with zeros, so old tool definitions did not have to
change. A *dynamic* tool (MCP, user-defined, image generation) carries a context
pointer instead.

**The result** (`struct jc_tool_result`) is the contract with the loop. Its
fields:

| Field | Means |
|---|---|
| `content` | the text the model reads (heap-owned) |
| `is_error` | something went wrong |
| `exit_status` | for tools that run a command: what the command returned; a red test is not a broken tool |
| `policy_refusal` | a fence said no; not a malfunction, so routing must not escalate to a stronger model |

**One entry point.** `jc_tool_execute` (`src/tools/jc_tool.c:1195`) does
everything around the call:

1. resolves aliases;
2. enforces the read-only and main-agent-only gates;
3. parses and repairs the arguments: malformed JSON, a reply cut in the middle,
   a copied elision placeholder, a stringified object;
4. runs the tool;
5. converts any non-OK status into an error value;
6. **always returns `JC_OK`.**

That last point is how "tool errors are values" is enforced: the reviewer found
no tool that returns a machinery error for a model-level failure, and the entry
point would convert one if it did.

**Memory** follows the three-arena rule, checked by `tests/smoke/arena_lint.sh`:

- per-call bytes on the per-call arena;
- anything a nested agent run needs on the per-turn arena;
- the session arena only for a dynamic tool's registration record.

---

## 2. One agent starting others

```
 parent turn (depth 0)
   |
   +-- spawn_subagent  -> same process, synchronous: depth + 1,
   |     a fresh history, max_iters = taper(maxSubagentIters, depth),
   |     a tool fence from the profile and skills
   |     -> the answer (capped at 4 KB, M911) -> one tool result
   |
   +-- spawn_parallel  -> a fork pool (up to min(cpus, 8) at once, 32 tasks)
         child k: its own process (a copy-on-write copy of the parent)
           a write task gets its own git worktree, a Landlock shell fence,
           its own $TMPDIR and a transcript
           -> a pipe of JSON lines: {"t":"tool"} {"t":"tok"} ... {"t":"done"}
         parent: a select() loop every 200 ms: watchdog, abort, control socket
           -> endings (M854) -> verify and hold gates
           -> file-level, first-wins merge into the live tree
           -> unmerged worktrees preserved as git refs (M803)
           -> one aggregated result
```

**In words.**

- **`spawn_subagent`** runs a nested agent inside the same process and waits for
  it. Its iteration cap halves at each level of nesting, never below 4: the
  default 25 becomes 12 at depth 1. Its tools are limited by its profile and
  skills, and it spends the parent's budget.
- **`spawn_parallel`** forks child processes. Each writing child works in its
  own git worktree, so children cannot overwrite each other's files. The parent
  watches them through pipes. When they finish, it merges their changes file by
  file into the live tree: the first child to change a file wins it, and the
  others' changes to that file are kept aside.

**How a child can end, and what the parent gets:**

| Ending | `spawn_subagent` | `spawn_parallel` child |
|---|---|---|
| a final answer | the answer, capped at 4 KB, the rest spilled to a file | its answer in the done line; its work merged after any verify |
| the iteration cap | the last answer, "capped" in the report | the last text plus a note; merges only after a green verify (M854) |
| a loop stop | the last answer, "looped" | `looped` or `repeated` in the done line |
| a budget | stops where it stands | its slice of the run's budget, or its own `max_tool_calls` (M861) |
| a context-window refusal | an error result | `overflow: 1` in the done line, and the work is kept |
| the watchdog (`parallelTaskTimeout`) | — | killed; the work is preserved, no answer |

**What survives an unfinished child:**

- the worktree, committed and pinned under `refs/jichi/discarded/…` (M803);
- an end record as the commit's message (M808);
- the child's transcript (M807);
- a *Kept* list at the head of the result (M809).

**`{"from": "<ref>"}` resumes a kept state** (M816). The kept change is applied
to a fresh worktree, and a note listing what the predecessor did outside its
tree is put in front of the task. This is already a handover between sessions,
for children.

---

## 3. Code to study

Six excerpts, each copied exactly from `17bbb895`.

### 3.1 One flag must not stand for two facts — `src/tools/jc_tool.c:39-57`

```c
int jc_tool_result_is_malfunction(const struct jc_tool_result *r)
{
    if (r == NULL || !r->is_error) {
        return 0;
    }
    /* M291: a policy refusal is not a malfunction. The tool did its job; the
     * request was out of bounds. Escalating to a stronger model would meet the
     * same fence, so it is pure cost -- which is exactly what the first route
     * event on a newly fenced project turned out to be. */
    if (r->policy_refusal) {
        return 0;
    }
    /* A non-negative exit_status means a command ran and reported it, so the
     * tool itself worked. Every other error path leaves the -1 that
     * jc_tool_execute initialises (a tool that runs nothing can only fail by
     * malfunctioning), including the "failed to start command" path -- which is
     * a real malfunction and correctly keeps -1. */
    return r->exit_status < 0;
}
```

**What it teaches.** `is_error` alone said "something went wrong", and three
different things looked the same:

- the tool broke;
- the command it ran said no (a failing test);
- a fence refused.

Routing escalated to a stronger model in all three cases, which helps only the
first. The fix is two fields and **one pure predicate every consumer calls**,
instead of each consumer guessing from the message text.

Note the sentinel: a tool that runs no command keeps `exit_status = -1`, so it
can only fail by malfunctioning. The safe reading falls out without a branch.

### 3.2 Reading a boolean a model wrote — `src/tools/tool_util.c:82-105`

```c
int tu_arg_bool(const cJSON *args, const char *key, int dflt)
{
    /* M530: one reader for a boolean a MODEL wrote, shared with every other
     * caller that has to interpret one (jc_json_get_bool_lenient, M519).
     *
     * This function accepted a real bool and the stringified spellings a model
     * sometimes sends ("true", "1") -- M168's fix -- but NOT a JSON number, so
     * `{"replace_all": 1}` silently became the caller's default. For
     * `spawn_subagent`'s `readonly` that was a fence: the call site is
     *
     *     has_arg_ro = (cJSON_GetObjectItem(args, "readonly") != NULL);
     *     arg_ro     = tu_arg_bool(args, "readonly", 0);
     *
     * so a model asking for a read-only child with `{"readonly": 1}` produced
     * present=1, value=0 -- read as an EXPLICIT "not read-only", and the child
     * ran WRITABLE. That is M519's `"pathFence": 1` defect verbatim: the
     * presence check fires and the fallback becomes a denial of what was asked.
     *
     * The lenient reader also accepts "yes"/"no", which is a widening of what
     * this function took; the boundary it does NOT cross is the one that
     * matters -- prose still falls through to the caller's default, in both
     * directions, so a typo can never flip a fence either way. */
    return jc_json_get_bool_lenient(args, key, dflt);
}
```

**What it teaches.** A trap worth memorising:

1. a **strict** reader returns the caller's *default* for a value it does not
   recognise;
2. a caller that also checks whether the key is *present* treats that default as
   an explicit choice;
3. so `{"readonly": 1}` became "explicitly writable", and a child the model
   asked to be read-only could write.

**The fix** is one lenient reader for every boolean a person or a model writes,
shared with the config layer, so "what does `1` mean?" has one answer in the
whole program. Prose still falls through to the default in both directions, so
a typo never flips a fence.

**The trap is not fully gone** (finding F9): `"readonly": null` still counts as
present, so the call site's presence check must also ignore values the reader
could not interpret.

### 3.3 An empty list must not mean "no fence" — `src/tools/jc_tool.c:907-919`

```c
    /* M848: two fences that share NO tool admit nothing. An empty list reads as
     * "no fence" to jc_tool_allowed, so leaving `out` empty here made the
     * strictest outcome the widest one: a profile and a restrict-tools skill
     * with nothing in common ran their subagent with every tool. Push the
     * sentinel, which matches no tool name, and return 0 so the caller can say
     * so. A static string, not an arena copy, so it cannot fail to land. */
    if (other != NULL && out->len == 0) {
        static char none[] = JC_TOOL_ALLOW_NONE;
        char *np = none;
        jc_vec_push(out, &np);
        return 0;
    }
    return (int)out->len;
```

**What it teaches.** `jc_tool_allowed` reads an empty allow-list as "no fence".
That is convenient when nobody configured one, and catastrophic when two fences
*intersect to nothing*: the strictest outcome became the widest.

**The fix** keeps the convenient reading, and makes "nothing" representable as
a sentinel name that no tool can have. The caller refuses the run when this
returns 0, so it fails closed either way.

**A habit to take away:** whenever an empty collection has a special meaning,
ask which other computation can produce an empty collection by accident.

### 3.4 A lenient parse that still refuses — `src/tools/jc_parallel.c:234-258`

```c
int jc_parallel_parse_max_calls(const struct cJSON *v, int *out)
{
    const cJSON *j = (const cJSON *)v;
    double d;

    if (j == NULL || cJSON_IsNull(j)) {
        return 0;
    }
    if (cJSON_IsNumber(j)) {
        d = j->valuedouble;
        if (!(d >= 1.0) || d != floor(d)) {
            return -1;   /* 0, negative, NaN, or a fraction */
        }
    } else if (cJSON_IsString(j) && j->valuestring != NULL) {
        const char *p = j->valuestring;
        d = 0.0;
        if (*p == '\0') {
            return -1;
        }
        for (; *p != '\0'; p++) {
            if (*p < '0' || *p > '9') {
                return -1;
            }
            if (d <= (double)JC_PAR_MAX_CALLS_CAP) {
                d = d * 10.0 + (double)(*p - '0');
```

**What it teaches.**

- **Two spellings accepted:** a model sends `150` or `"150"`, and both are fine.
- **Everything else refuses the whole task:** 0, a fraction, NaN, words. The
  task is refused rather than the key ignored, because a lead that *believes* a
  budget binds when it does not is worse off than one told its call was wrong.
- **`!(d >= 1.0)` is a deliberate double negative.** `d < 1.0` is false for NaN,
  so NaN would slip through; `!(d >= 1.0)` catches it.
- **Overflow is impossible.** The digit loop stops growing past the cap, so a
  thousand-digit string cannot overflow the `double` into infinity.
- **It is pure,** so it is unit-tested directly.

Compare it with `main.c`'s `atoi` flags in
[Review 3](2026-10-08-review-03-main-and-the-run-modes.md): the same project,
two attitudes to the same problem.

### 3.5 All or nothing across files, reported honestly — `src/tools/jc_tool_apply_patch.c:284-303`

```c
                for (k = 0; k < i; k++) {
                    struct fileent *w =
                        (struct fileent *)jc_vec_at(&files, k);
                    if (jc_app_write_file(app, w->path,
                                          w->orig != NULL ? w->orig : "",
                                          w->orig_len) == JC_OK) {
                        jc_sb_append_fmt(&es, "\n  %s: reverted to original",
                                         w->path);
                    } else {
                        revert_failed = 1;
                        jc_sb_append_fmt(&es,
                            "\n  %s: REVERT FAILED -- still contains the "
                            "patched content", w->path);
                    }
                }
                if (wst == JC_ERR_DENIED) {
                    /* Denial happens before any byte is written, so this is
                     * provably untouched (a restore would be equally denied). */
                    jc_sb_append_fmt(&es, "\n  %s: write denied -- untouched",
                                     ent->path);
```

**What it teaches.** `apply_patch` changes several files at once.

1. **First line of defence:** it checks every edit in memory before it writes
   anything.
2. **Second line,** this excerpt: when a write fails half way, the files already
   written are put back from the originals kept in memory.
3. **The error lists each file's real state.** It does not claim an atomicity
   the filesystem cannot give. A fence denial needs no restore, because it
   happens before any byte is written.

**Its blind spot** is finding F4: "one file" is decided by the path's spelling,
not by the file's identity.

### 3.6 "Not found" versus "could not look" — `src/tools/jc_tool_ls.c:202-215`

```c
        /* NOT part of the else-if chain above, deliberately. Those three are
         * mutually exclusive because they are competing explanations of the same
         * result; this one COMPOSES with all of them, and the pairing that
         * matters most is the one the chain would have hidden -- zero matches
         * AND an unreadable subtree, which is the difference between "these
         * files do not exist" and "I could not look everywhere". */
        if (w.unreadable > 0) {
            jc_sb_append_fmt(&sb,
                "\n[... %d director%s under this path could not be READ and %s "
                "not searched, so the results above are incomplete -- absence "
                "here does not mean the file does not exist ...]\n",
                w.unreadable, w.unreadable == 1 ? "y" : "ies",
                w.unreadable == 1 ? "was" : "were");
        }
```

**What it teaches.** "Zero matches" and "I could not read part of the tree" are
different facts, and the second changes the meaning of the first: absence no
longer means the file does not exist. Counting unreadable directories during the
walk costs one integer, and leaving it out produced confident wrong answers.

The structural point is in the comment. The three notices before this one are
*alternatives*, so they form an `else if` chain. This one *composes* with all of
them, so it stands outside the chain.

---

## 4. What the review found

25 findings: two high, thirteen medium, nine low, one note. Status is the
reviewer's. Security findings are described by class.

### High

- **F1 — a parallel child's change to a file whose name git quotes is lost, and
  reported as merged** (verified).
  - **The cause.** The merge lists a child's changes with `git diff
    --name-status`, which *quotes* any path with a non-ASCII byte, a tab or a
    quote: `A\t"caf\303\251.md"`. The parser copies the quoted text as the path
    (`src/tools/jc_parallel.c:87-100`). Copying `wt/"caf\303\251.md"` fails, and
    a failed copy is recorded nowhere.
  - **The result.** The child is marked merged, and its worktree, which holds
    the only copy of the work, is deleted without preservation. The result says
    "N file(s) applied, no conflicts".
  - **Who meets it.** Any project with a file named `café.md` or `测试.txt`, on
    the first edit; and any other failed copy, such as a full disk.
  - **The fix.** Parse `git diff -z` output, which is never quoted, and make a
    failed copy a conflict that preserves the worktree.
  - **The proof.** A unit test feeding the quoted line to the parser, and a mock
    child that creates `café.md`.
- **F2 — `/tutor` frees memory an arena owns** (`src/tui/jc_tui.c:4192`). The
  cause is a contract comment in `include/jc_tool.h:196` and
  `src/tools/jc_tool_help.c:75` that still says "malloc'd (caller frees)" after
  the memory moved to an arena. It is noted only in an internal page, with no
  DEFERRED row.

### Medium

- **F3** — `edit_file` and `apply_patch` cut a file at its first NUL byte and
  report success. A 14-byte file became 3 bytes.
- **F4** — `apply_patch` keys files by the path's spelling. Two spellings of one
  file lose the first edit, while the result claims both.
- **F5** — `app->ctx_overflow` is never cleared. After one context-window
  refusal in the lead, every later parallel write child is reported "ran out of
  context", and kept instead of merged.
- **F6** — a parallel child's capped answer is spilled under the *child's*
  process id, a path the fenced lead cannot read.
- **F7** — the `from:` resume note lists every outside-tree call of the
  predecessor, with no bound, so resuming a worker that overflowed can overflow
  again.
- **F8** — a git argument taken from the model is not separated from git's
  options (an argument-injection class). Some branch names are read as options
  or paths, and can discard uncommitted work while the tool reports success.
- **F9** — `"readonly": null` counts as present and overrides a read-only agent
  profile (§3.2).
- **F10** — `spawn_parallel` reads a task's `write` strictly, outside the lenient
  rule.
- **F11** — the lead's budget reconciliation leaves out the calls and tokens of a
  parallel child's own delegates, so the run's budget is looser than it says.
- **F12** — `pause` during a batch can end the batch: queued tasks are never
  started, and render as "(error: unknown)".
- **F13** — the edit results are bounded in lines, not bytes: an edit to a
  one-line file can return megabytes into the context.

### Low and notes (selected)

- **F14** — a failed delegate's partial work is dropped on the way back.
- **F16** — `git_add` drops every path after the 18th and reports "Staged
  changes."
- **F17 and F20** — two paths read or write outside the path fence (a fence-gap
  class): the LSP navigation tools' reads, and the parallel merge's writes.
- **F18** — `parallelTaskTimeout` cannot be switched off.
- **F19** — a parallel child whose model's provider cannot be built runs on the
  lead's model, labelled as the requested one.
- **F21** — `search_code` passes the path without `--` (an argument-injection
  class).
- **F22** — `strcat` is used four times, and the lint CLAUDE.md cites covers only
  `sprintf`.

---

## 5. The design, seen whole

**What is solid.**

- **The result contract and the single entry point.** They work as designed: no
  tool leaks a machinery error into control flow.
- **Argument reading is uniform,** with one strict reader left (F10).
- **Arena use is clean.**
- **Output caps are central** (`include/jc_toolcaps.h`), and over-cap shell
  output is spilled to a file, not lost.
- **The comments carry their incidents,** which makes every guard's intent
  auditable.

**What is fragile, and why.**

- **One function grew by accretion.** `jc_tool_parallel.c` is 3,163 lines, and
  `parallel_run` alone is about 1,150, with about 40 local variables. It parses
  the tasks, salvages orphans, slices budgets, runs the pool, classifies endings,
  runs the gates, merges, preserves and renders. Each milestone added a stage in
  place.
- **The field list is kept in three places.** The child reports through about 25
  JSON fields in its done line, and each was added by hand in three places: the
  emitter, the parser and the struct.
- **Two delegation tools, two behaviours.** `spawn_subagent` and
  `spawn_parallel` differ on what happens when a model's provider cannot be
  built (refuse versus silent fallback), on `readonly`, on partial work and on
  where the answer is spilled. A model using both learns two contracts.
- **The merge is the weakest stage.**
  - it trusts git's human-oriented output (F1);
  - it treats a failed copy as success (F1);
  - it bypasses the write chokepoint (F20).

  It is where silent data loss lives today.

**What the reviewer would refactor first, with the cost:**

1. **Move the merge into its own file,** one function per gate, parsing `-z`
   output, where "applied" means applied. About 400 lines moved, a day, low
   risk: the gates are already sequential, and the `parallel_*` smoke drivers
   are the safety net.
2. **One table for the done line's fields,** used by the emitter and the parser:
   half a day.
3. **One "resolve and validate a task" helper** shared by both delegation tools:
   half a day.

**A testing note.** The harness the reviewer used, a tool called directly on a
temporary directory as `tests/test_tool.c` already does, reproduced F3, F4 and
F13 in seconds. They belong in the unit suite.

---

## 6. What this means for the context relay

- **`spawn_parallel` already has most of a handover:** kept states, end records,
  transcripts and `from:`. The relay's child form reuses them
  ([proposal](../proposals/2026-10-context-relay.md) §6.11).
- **Three findings sit on that path and come first** (proposal §9): F5, the
  sticky overflow flag; F6, the unreadable spill; and F7, the unbounded resume
  note.
- **Budgets for children are slices of the parent's,** and a task's own
  `max_tool_calls` only narrows its slice. That is the pattern for the relay's
  variant V4, "the parent allocates" (proposal §6.10).

---

## 7. Something to do

**Without a bench (the read-only twin):**

1. In §3.2, write down the four values `tu_arg_bool(args, "readonly", 0)` returns
   for `true`, `1`, `"yes"` and `"maybe"`. Then trace the call site in the
   comment for `{"readonly": "maybe"}`. Is the child read-only?
2. In §3.3, describe two configurations whose fences intersect to nothing. What
   would the subagent have been allowed to do before M848?
3. For F1, write the one-line change to the parser's *input* that removes the
   quoting (hint: one git option). Then write the test that proves it, before
   the change.

**With a bench (git only; it works in a fresh temporary directory):** see the
quoting for yourself.

```sh
cd "$(mktemp -d)" && git init -q . && echo hi > café.md && git add café.md
git diff --cached --name-status
git diff --cached --name-status -z | tr '\0' '\n'
```

What you should see, run on threadwork with git 2.53 on 2026-10-08:

```
A	"caf\303\251.md"
A
café.md
```

The first command prints the name quoted, with the two bytes of `é` as octal
escapes. That is exactly the text F1's parser took for a path. The second, with
`-z`, separates fields with NUL bytes instead and never quotes.

## 8. Read further

- [SUBAGENTS.md](../SUBAGENTS.md), [PARALLEL.md](../PARALLEL.md),
  [USER_TOOLS.md](../USER_TOOLS.md): the references for this chapter. *Primary
  for jichi.*
- [Annai 5](../reading/annai-05-one-tool-all-the-way-down.md) and
  [Fukabori 7](../reading/fukabori-07-fork-based-parallelism.md): one tool all the
  way down, and why the pool forks. *Primary for jichi.*
- `git-diff(1)`, the `-z` option and `core.quotePath` in `git-config(1)`: why the
  default output quotes paths. *Primary.*
- MITRE CWE-88, "Improper Neutralization of Argument Delimiters in a Command":
  the class of F8 and F21. *Primary.*
