# The tier no gate built: three drivers, three vacuous checks, one real defect

**Date:** 2026-08-18 · **Milestone:** M482 · **Result:** a product defect fixed at
four call sites, three vacuous checks replaced with a property, a `make smoke-faults`
stage added to `make ci`, and a lint so the stage cannot be silently dropped.

The starting question was small: *are jichi's error-path tests still passing?* Nobody
could answer it, because nothing had run them.

---

## 0. What was actually wrong: the drivers were invisible

`tests/smoke/` has three drivers for jichi's failure handling — `faults`,
`faults_net`, `faults_net_midstream`. Each begins by checking that the binary was
built with fault injection compiled in, and skips otherwise:

    "$BIN" --version | grep -q "FAULT=1" \
        || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

That is a good design. `FAULT=1` compiles an injection surface into every
allocation and network call, so it must not be in a release binary, and a driver
that needs it should decline rather than fail.

The problem is what happened next. **No stage of `make ci` ever built one.** Its
seven stages cover gcc, clang, ASan/UBSan, Valgrind, the fuzz targets, `all`, and
the curl-free link — none of them `FAULT=1`. So all three drivers declined in
every run, on every platform, including the development box:

    --- smoke: faults
    # skip: needs a FAULT=1 binary (make clean && make FAULT=1)

They ran only when somebody typed `make clean && make FAULT=1 && make smoke` by
hand, and nothing anywhere said they hadn't. This is the **orphan check one level
up**: `smoke_lint` asserts every driver on disk is named in `run.sh`, and these
were. Being *listed* and being *reachable* are different properties when a driver
gates itself on a build flag.

---

## 1. The good news first

Built and run, all three pass — 17 checks, no rot:

| driver | checks | what it covers |
|---|---|---|
| `faults` | 8 | read failure, allocation failure, an unenforceable `memBudgetMb` |
| `faults_net` | 4 | the retry ladder: backoff schedule, exact retry count, final diagnostic |
| `faults_net_midstream` | 5 | a connection dying *after* the response began (M227's handle reuse) |

The `FAULT=1` build is also clean under `-Werror` and announces itself in
`--version`, which is how the drivers detect it — a far better probe than
inferring it from behaviour.

---

## 2. Then: three of those checks were measuring nothing

`faults.sh` ran the allocation injector at three budgets and asserted survival:

```sh
for after in 200 800 3000; do
    ... env JICHI_FAULT_ALLOC_AFTER=$after "$BIN" ... ls --all ...
    if   [ $rc -ge 124 ] && [ $rc -le 128 ]; then t_fail "HUNG"
    elif [ $rc -gt 128 ];                    then t_fail "died on a signal"
    else t_ok "alloc failure after $after handled without crash/hang (rc=$rc)"
    fi
done
```

Two independent faults in one loop.

**First: nothing was injected.** `ls --all` makes exactly **two** counted
allocations, so at 200, 800 and 3000 the injector never fires. Measured three ways
on the same store:

| run | stdout |
|---|---|
| no injection | 227 bytes, 4 sessions |
| `JICHI_FAULT_ALLOC_AFTER=200` | **227 bytes — byte-identical** |
| `JICHI_FAULT_ALLOC_AFTR=200` (name misspelled) | **227 bytes — byte-identical** |
| `JICHI_FAULT_ALLOC_AFTER=1` | 19 bytes: `(no saved sessions)` |

A check whose output is unchanged by misspelling the variable it depends on is not
looking at that variable. All three checks described an ordinary run.

**Second: even firing, they could not fail on the defect.** `rc` in `0..123`
passes, and this file's own header says what the defect is:

> The assertion is deliberately not "it survived": surviving an allocation failure
> by silently dropping sessions from the listing **with exit 0** is the DEFECT —
> each case asserts a diagnostic reaches the user.

Exit 0 with an empty listing sits squarely inside the passing range. The driver
stated the defect precisely and then wrote three checks incapable of detecting it.

---

## 3. And the defect was there

At a firing budget, `ls` printed `(no saved sessions)` and exited **0**. The cause,
`src/main.c`:

```c
if (jc_session_list_ex(&metas, arena, &skipped) != JC_OK || metas.len == 0) {
    printf("(no saved sessions)\n");
```

That `||` gives two different answers the same words: *the enumeration failed* and
*you have no sessions*. `skipped` (added at M198) counts per-file read failures, so
those did produce a warning — but an allocation failure inside the call returns
non-`JC_OK` with `skipped == 0`, and nothing was printed to stderr.

The sharpest part is M198's own header, on the function being called:

> M198: this count is the difference between degrading and degrading SILENTLY.
> Before it existed, an allocation failure, an I/O error, or a session larger than
> `JC_READ_FILE_MAX` simply dropped that session from `/sessions` and `ls` **with a
> success exit code** — the user's sessions appeared to have vanished with no
> diagnostic anywhere.

M198 fixed the per-file half and left the whole-call half doing exactly what that
paragraph describes. Four milestones' worth of confidence rested on a fix that
covered one of two paths, and the test that would have noticed was the one no gate
built.

**The JSON surface was worse.** `run_ls_json` called `jc_session_list` and
discarded the status under the comment `/* empty vec => empty array */`, so a
failed enumeration produced a well-formed `{"v":1,"sessions":[]}` with exit 0 — and
`ls --output json` is a **stable** interface (`docs/EMBEDDING.md`) that a
supervisor *parses instead of reading*. It also dropped `skipped` entirely, so the
machine surface could not report unreadable files that the text surface could.

---

## 4. The fix, and the third state I found while fixing it

First cut: treat any non-`JC_OK` as an error. `subcommands_lint` immediately caught
it — `jichi ls` exited 1 with empty stdout on a machine that had simply never saved
a session, because `jc_session_list_ex` returns non-OK when the store *does not
exist*. I had conflated in the opposite direction.

There are **three** states, and the old code had two names for them:

| state | before | after |
|---|---|---|
| no store yet (fresh install) | `(no saved sessions)`, exit 0 | unchanged — `(no saved sessions)`, exit 0 |
| store exists, cannot be read | `(no saved sessions)`, **exit 0** | `error: could not list sessions (…)`, **exit 1** |
| enumeration failed (OOM) | `(no saved sessions)`, **exit 0** | `error: could not list sessions (out of memory)`, **exit 1** |

Distinguishing them needed a fix one layer down. `jc_list_dir` returned
`JC_ERR_NOTFOUND` whenever `opendir` failed — for a missing directory *and* for
`EACCES`, `ENOTDIR` or `EMFILE` alike — so every one of its 27 callers read a
permissions failure as "there is nothing here". It now returns `JC_ERR_NOTFOUND`
only for `ENOENT`/`ENOTDIR` and `JC_ERR_IO` otherwise; all 27 callers test
`!= JC_OK`, so the split is compatible. `jc_session_list_ex` propagates it instead
of flattening everything to `NOTFOUND`.

**That last row matters beyond fault injection.** A store that exists and cannot be
read needs no injector to produce — `chmod 000` is enough, and so is a permissions
mistake, an NFS hiccup, or running out of descriptors. So *that* check went into
`tests/smoke/sessions.sh`, which every platform runs, rather than into the tier
that needs a special build. Shown to fail without the fix:

    not ok 5 - an unreadable session store reported rc=0 with
               stdout='(no saved sessions)' stderr=''

Fixed at four sites: `run_ls`, `run_ls_json`, the TUI's `/sessions`, and
`jc_list_dir` beneath them.

---

## 5. The checks that replaced the vacuous ones

The new checks assert a **property**, not three numbers: *at every budget, the run
either produces the whole listing or tells the user it could not.* Never a
plausible partial answer with a success status.

- Budgets are **swept** (`1 2 3 4 6 10 20 60 200`) rather than named, so the checks
  keep their teeth when this path's allocation count drifts — which is precisely
  what defeated the old ones.
- An **anti-vacuity floor** requires the injector to change the run at *some*
  budget, and it is deliberately **not** derived from the sweep's own verdicts.
  That is M479's lesson: a floor gated on the thing it guards disables itself
  exactly when it is needed.
- The JSON surface is checked separately, because a well-formed empty array is
  worse than a wrong sentence.

Proven in both directions. Against the unfixed product:

    not ok 5 - silent degradation at budget(s): 1 2 -- an incomplete listing
               with exit 0 and no diagnostic
    not ok 6 - ls --output json returned a successful, incomplete listing at
               budget(s): 1 2 -- a supervisor cannot tell this from an empty store

and check 7 — "no hang, no signal", the old checks' only real content — **passes in
both**, which is the clearest possible statement that the old shape could never
have caught this.

---

## 6. Making it impossible to lose again

`make smoke-faults` builds `FAULT=1` and runs the three drivers; `make ci` calls
it, placed before the final `all` stage so the gate still leaves an ordinary binary
in the tree. It cleans first, because `FAULT=1` changes CFLAGS for every
translation unit and a stale tree would link objects that skip on a binary which
looks built.

And a lint, because a stage is only as durable as the thing that notices its
absence — `smoke_lint` check 16: every driver whose text contains
`needs a FAULT=1 binary` must be named in the Makefile. Adding a fourth fault
driver now fails the gate until it is wired in. Proven by deleting one line from
the target:

    not ok 16 - 3 FAULT-gated driver(s), but the Makefile names none of:
                faults_net.sh -- they will SKIP in every build and nothing
                will say so

Its own first draft pointed at `$ROOT/Makefile` in a driver that has no `$ROOT`, so
it reported every driver missing from a Makefile it had never opened — a false
**red**, which is the safe direction and was obvious on the first run.

---

## 7. Lessons

1. **A test that can skip is a test that can disappear.** The skip was correct; the
   absence of anything that built the prerequisite was not. Ask of every
   conditional test: *what runs the condition?*
2. **"Listed" is not "reachable."** The orphan lint that guarantees every driver is
   named in `run.sh` was satisfied the whole time.
3. **If misspelling a variable does not change the result, the test is not reading
   that variable.** One cheap differential; it is now the first thing I would try
   on any environment-driven test.
4. **A range-based assertion (`rc in 0..123`) usually cannot fail on the defect its
   own comment describes.** Assert the property, not the absence of a crash.
5. **Sweep, do not name.** The old budgets were valid when written and rotted
   silently as the allocation count changed. A swept range plus a firing floor
   cannot rot that way.
6. **Fixing one half of a conflation invites forgetting the other.** M198 split
   per-file failures out of "no sessions" and left whole-call failures inside it,
   under a comment describing that exact defect.
7. **A machine surface deserves the stricter treatment, not the looser one.** The
   JSON path was the one that discarded its status, and it is the one a supervisor
   cannot sanity-check by eye.
