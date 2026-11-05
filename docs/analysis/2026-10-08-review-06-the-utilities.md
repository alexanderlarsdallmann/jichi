# Review 6 — The toolbox: `src/util`

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on a report that
was **paused** at the operator's request after 65 of the slice's 78 files had
been read; §5 lists the files no reviewer reached. Its six code excerpts were
checked line for line against the source. Written by Claude Opus 5.5 on
threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to see the small,
reusable pieces a C program is built from: memory pools, growable strings,
process spawning, logs and the readers that make sense of them.

**After reading it you will be able to:**

- explain jichi's split between pure functions and thin effectful shells, and
  why it makes code testable;
- write an overflow-safe growth check for a buffer;
- explain why an unknown reading must never trip a fence;
- recognise the "accumulator" formatting idiom, and the one guard without which
  it becomes a buffer overflow.

**What you need first:**
[Fukabori 3](../reading/fukabori-03-the-three-arena-lifetime-model.md) on the
three arenas, and [DATA_STRUCTURES.md](../DATA_STRUCTURES.md).

---

## 1. Eighty small modules, one idea

`src/util` holds about 80 small C89 modules that everything else calls. Almost
every one is either:

- **a pure function:** input in, output out; no files, no clock, no network; so
  it can be unit-tested offline; or
- **a thin effectful shell** around one: it reads a file, runs a program or
  writes a log, then hands the bytes to the pure part.

That split is the slice's main design idea. You will see it in the disk guard
(§3.4), the telemetry summary, the run viewer and the lesson drafter.

**The groups, by job:**

- **Memory.** `jc_mem` is an *arena*, a bump allocator: it hands out slices of
  big blocks and frees them all at once, never one by one. jichi keeps three
  arenas by lifetime: the session, the top-level turn, and the tool call. `jc_sb`
  is a growable string ("string builder") for data of unknown size, and `jc_vec`
  a growable array.
- **Bounded text.** `jc_snprintf` replaces `sprintf`. `jc_utf8` measures display
  width, validates, cuts on character boundaries, repairs bad bytes and strips
  terminal control bytes. Also `jc_base64`, `jc_sha256`, the frontmatter splitter
  `jc_md`, and shell quoting.
- **Processes.** `jc_spawn` starts every child the same way:
  - its own process group;
  - stdio wired;
  - secrets scrubbed from the environment;
  - stray descriptors closed;
  - SIGPIPE reset.

  `jc_proc_capture` runs a command, feeds its stdin, collects its output up to a
  cap, and kills the group at a deadline. jichi also makes its own process
  non-dumpable, so a model's shell cannot read jichi's environment through
  `/proc`.
- **Observability.** `jc_eventlog` writes telemetry as JSON Lines, one object
  per line. `jc_log` writes leveled diagnostics, and owns the registry of
  secrets to redact. `jc_audit` records privileged decisions. Four *readers*
  turn the logs into reports and proposed lessons.
- **Fences.**
  - `jc_path` keeps paths inside the workspace (§3.3);
  - `jc_diskguard` stops before the disk fills (§3.4);
  - `jc_untrusted` labels fetched content as data;
  - `jc_multipart` builds upload bodies that cannot be header-injected (§3.6);
  - Landlock, privileged-command detection and the "kinetic" command gate.

```
  model output / tool args            config + .jichi/ files
          |                                   |
          v                                   v
  +---------------+   argv,env   +-----------+---------+      +-------------+
  | chat / tools  |------------->| jc_spawn  | jc_proc |----->| child (sh)  |
  | (jc_agent.c)  |              +-----------+---------+      +-------------+
  |               |--events----->| jc_eventlog (JSONL) |--> ~/.jichi.d/telemetry/
  |               |--diag------->| jc_log (+redact)    |--> stderr
  +-------+-------+              +---------------------+
          | arenas: jc_mem (session / turn / tool)        readers (pure):
          | strings: jc_sb, jc_utf8, jc_snprintf          telemetry, insights,
          v                                               learn, runs view
```

**Invariants to keep:**

- every fallible call returns `jc_status`;
- nothing calls `sprintf`;
- a reader never trusts its log, and skips malformed lines;
- an unknown reading never trips a fence;
- every child process goes through `jc_spawn`, which a lint enforces.

---

## 2. A formatting idiom with a sharp edge

Several reports met the same idiom, so it is worth learning on its own. To build
a string from pieces into a fixed buffer:

```c
n = 0;
n += jc_snprintf(buf + n, cap - n, "first part");
n += jc_snprintf(buf + n, cap - n, ", second part");
```

`jc_snprintf` follows the C99 rule: it returns the length it *would* have
written, even when it had to cut. So after one cut, `n` is larger than `cap`.
Then `cap - n`, an unsigned subtraction, wraps around to a huge number, and the
next call writes far past the end of `buf`.

**The idiom is safe only with a guard after each step:**

- clamp: `if (n >= cap) n = cap - 1;`;
- or a loop condition that stops early, as the setup wizard does
  ([Review 8](2026-10-08-review-08-tui-setup-acp.md) §2.5);
- or a growable `jc_sb` instead of a fixed buffer.

The review found one place in this slice where the guard is missing, in an
end-of-run report. That is finding 0 below, a memory-safety defect.

---

## 3. Code to study

Six excerpts, each copied exactly from `17bbb895`.

### 3.1 Overflow guards on a growable buffer — `src/util/jc_str.c:64-87`

```c
    if (extra > (jc_size)-1 - b->len - 1) {
        return JC_ERR_TOOBIG;
    }
    need = b->len + extra + 1; /* +1 for terminator */
    if (need <= b->cap) {
        return JC_OK;
    }
    if (need > ((jc_size)-1) / 2) {
        /* Cannot double into range. Ask for exactly what is needed and let the
         * allocator refuse it, rather than looping. */
        newcap = need;
    } else {
        newcap = (b->cap == 0) ? 64 : b->cap;
        while (newcap < need) {
            newcap *= 2;
        }
    }
    p = (char *)realloc(b->data, newcap);
    if (p == NULL) {
        return JC_ERR_OOM;
    }
    b->data = p;
    b->cap = newcap;
    return JC_OK;
```

**What it teaches.** Two `if`s turn "every caller passes a sane length", a claim
about the whole program, into a local guarantee:

- **The first guard cannot overflow itself.** It subtracts from the maximum
  instead of adding to `len`. `len + extra + 1 > MAX` would overflow before it
  compared anything.
- **The second guard stops the doubling loop** from wrapping to 0 and spinning
  forever. Past half the maximum, it asks for exactly what is needed and lets
  `realloc` refuse.

**The same guards are missing from the arena and the vector** (finding 8). A
primitive's guarantees belong in the primitive, or every caller must remember
them.

### 3.2 Let the compiler enforce completeness — `src/util/jc_outcome.c:53-70`

```c
const char *jc_run_stop_wire(enum jc_run_stop stop)
{
    /* STABLE STRINGS. These are the `stop_reason` values in `--output json`,
     * which docs/EMBEDDING.md lists as a stability tier. They predate this file
     * and are reproduced exactly; the point of moving them here is that they
     * now have one definition instead of a chain of else-ifs in main.c. */
    switch (stop) {
    case JC_STOP_DONE:          return "done";
    case JC_STOP_INTERRUPTED:   return "interrupted";
    case JC_STOP_TIMEOUT:       return "timeout";
    case JC_STOP_ERROR:         return "error";
    case JC_STOP_BUDGET:        return "budget";
    case JC_STOP_VERIFY_FAILED: return "verify_failed";
    case JC_STOP_SCOPE_TAINTED: return "scope_tainted";
    case JC_STOP_MAX_ITERS:     return "max_iters";
    }
    return "done";   /* unreachable; -Wswitch guards the enum */
}
```

**What it teaches.** There is no `default:` in this switch, on purpose. With
`-Wall`, which includes `-Wswitch`, and `-Werror` in the gate, adding a value to
the enum fails the build *here* until someone writes its string. A `default:`
would silently give the new value whatever string it returned.

These strings are a public interface, the `stop_reason` of `--output json`, so
there must be exactly one definition. The
[relay proposal](../proposals/2026-10-context-relay.md) §6.3 adds two values,
and this switch is one of the places the compiler would send its author.

### 3.3 Fail closed when a path cannot be named — `src/util/jc_path.c:239-257`

```c
        if (lstat(cur, &st) == 0 && S_ISLNK(st.st_mode)) {
            ssize_t tl = readlink(cur, aux, sizeof(aux) - 1);
            if (tl <= 0) {
                return JC_ERR_NOTFOUND;
            }
            aux[tl] = '\0';
            slash = strrchr(cur, '/');
            if (aux[0] == '/' || slash == NULL) {
                memcpy(cur, aux, (size_t)tl + 1);
            } else {
                /* Relative: join against the link's directory, which is cur
                 * up to and including its last slash ("/" for a link in /). */
                size_t dlen = (size_t)(slash - cur) + 1;
                if (dlen + (size_t)tl + 1 > sizeof(cur)) {
                    return JC_ERR_TOOBIG;
                }
                memcpy(cur + dlen, aux, (size_t)tl + 1);
            }
            continue;
```

**What it teaches.** `realpath()` fails on a symbolic link whose target does not
exist yet. Before M607, the code then treated such a link as "a new file inside
the workspace", and the later `fopen` followed the link *outside*.

**How it works now.** A link at the end of the path becomes the next step of a
bounded loop: 40 hops, Linux's own limit.

- **A relative target** is joined against the link's own directory.
- **A failed `readlink`** returns NOTFOUND, which every caller reads as *deny*.
- **It is a loop, not recursion,** because each recursive frame held about 16 KB
  of buffers, and a 512 KB stack on FreeMiNT overflowed.

### 3.4 A pure decision, split from its I/O — `src/util/jc_diskguard.c:23-45`

```c
int jc_diskguard_below(double free_mb, double free_files,
                       double floor_mb, double floor_files)
{
    /* UNKNOWN (a negative reading) never trips; a floor <= 0 is disabled. */
    if (floor_mb > 0.0 && free_mb >= 0.0 && free_mb < floor_mb) {
        return 1;
    }
    if (floor_files > 0.0 && free_files >= 0.0 && free_files < floor_files) {
        return 1;
    }
    return 0;
}

int jc_diskguard_due(double last_ms, double now_ms, int force)
{
    if (force) {
        return 1;
    }
    if (last_ms < 0.0) {
        return 1;
    }
    return (now_ms - last_ms) >= JC_DISKGUARD_INTERVAL_MS;
}
```

**What it teaches.** Both functions take numbers and return a verdict: no
`statvfs`, no clock. That is what lets two invariants be unit-tested without
filling a disk:

- **Unknown never stops.** A negative reading means "could not measure" and
  never trips the fence. A fence that fires on missing data would stop healthy
  runs.
- **Throttled by time, not by call count.** "Never measured" (`last_ms < 0`)
  always measures.

The stateful wrapper only measures, latches and words the reason. This is the
pattern of the whole slice in 23 lines.

### 3.5 Classify by a positive list — `src/util/jc_runsview.c:30-49`

```c
static int journal_exclusive(const char *ev)
{
    static const char *NAMES[] = {
        "open", "start", "end", "verify", "budget", "budget_notice", "rollback",
        "baseline", "checkpoint", "preserved", "strict_green", "self_review",
        "ask", "control", "post_outcome", "verify_stuck",
        "test_assertion_edit", "learn_on_stop", "out_of_scope",
        "blocked_repeat", "tool_loop", "no_progress", "constraint_advisory",
        "attempt",
        NULL
    };
    int i;

    for (i = 0; NAMES[i] != NULL; i++) {
        if (strcmp(ev, NAMES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}
```

**What it teaches.** Two log formats, the run journal and telemetry, both use the
key `"event"`. A reader must tell them apart.

**Two ways to do it:**

- **A blocklist** of telemetry names to reject would rot the day telemetry adds a
  name.
- **This positive list** accepts a file only if a name that occurs *only* in
  journals appears. Names shared by both formats are deliberately left out, and a
  lint fails if that stops being true.

The caller turns "no exclusive event" into a refusal, so a telemetry log is
never rendered as an all-zero phantom run.

### 3.6 Strip, do not escape, at an injection boundary — `src/util/jc_multipart.c:37-50`

```c
static void append_quoted(struct jc_multipart *mp, const char *s)
{
    jc_size i;
    if (s == NULL) {
        return;
    }
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\r' || c == '\n' || c == '\\' || c < 32) {
            continue;
        }
        jc_sb_append_n(&mp->body, (const char *)&c, 1);
    }
}
```

**What it teaches.** A filename chosen by the model lands inside a header,
`Content-Disposition: …; filename="…"`. A carriage return and line feed there
would inject a new header, or a whole extra part, into the upload (*header
injection*).

The comment above it records the decision: servers implement backslash-escaping
unreliably, so *removing* quotes, backslashes, CR, LF and control bytes is safer
than escaping them, and losing a quote from a file's label costs nothing.
Filtering byte by byte into a growable string needs no length arithmetic at all.

---

## 4. What the review found (in the 65 files read)

Seventeen findings so far: two high, one medium-high, three medium, three
low-medium, eight low. Status is the reviewer's; every one listed here was
reproduced with a small harness in a private copy.

### High

- **0 — a stack buffer overflow in an end-of-run report, when one of its lists is
  long** (memory-safety class; verified under AddressSanitizer). It is the
  unguarded form of §2's idiom. The trigger and location are kept with the
  maintainers until it is fixed. *The fix:* clamp after every append, or build
  the text in a growable string, and bound the list at its producer. *The proof:*
  a unit test with a 2 KB list, run under the sanitizer build the gate already
  has.
- **1 — `jc_proc_capture` crashes when called without an output buffer and the
  child prints anything** (verified: a segmentation fault). Eight call sites pass
  no buffer, among them the `rm -rf` of a worktree and `git gc`. The first time
  `rm` meets a read-only directory and prints "Permission denied", jichi dies in
  the middle of a cleanup. Nothing in the header says a NULL buffer is allowed.
  *The fix:* treat NULL as "discard": drain the pipe and skip the append.

### Medium-high and medium

- **2 — a secret registered for redaction can be lost within one turn.** The
  same lifetime defect as [Review 5](2026-10-08-review-05-sessions-memory-control.md)
  F1, found independently from this side.
- **3 — `jc_proc_capture` stops enforcing its timeout and the abort flag once
  the child closes its output** (verified: a 1-second timeout returned after 6
  seconds). A hook or user tool that redirects its own output and keeps running
  blocks jichi forever.
- **4 — two parsers disagree about what a frontmatter fence is** (a fail-open
  class: a declared read-only flag in an agent profile can be silently dropped by
  harmless-looking formatting).

### Low-medium and low (selected)

- **8** — the arena and the vector lack the overflow guards of §3.1.
- **10** — the ban on `strcpy` and `strcat` is stated in CLAUDE.md and not
  enforced: no lint checks it.
- **11** — private log files are created with the default permissions and
  `chmod`ed afterwards, a small race window. Compare
  [Review 3](2026-10-08-review-03-main-and-the-run-modes.md) §4.5, which closes
  the same race the right way.
- **12** — the disk fence measures the workspace's file system, while the
  records it protects live under HOME, which may be another file system.
- **13** — the untrusted-content wrapper uses a fixed, public end-marker. A
  per-wrap random marker would make a counterfeit end detectable (low, security
  class).

---

## 5. What was not reviewed

The report was paused before these 13 files were read. Nothing on this page
describes them, and nothing should be read as saying they are clean:

`jc_voice.c`, `jc_rss.c`, `jc_delegreport.c`, `jc_diff.c`, `jc_auditview.c`,
`jc_lease.c`, `jc_assign.c`, `jc_cli.c`, `jc_progress.c`, `jc_msg.c`,
`jc_patch.c`, `jc_mdrender.c`, `jc_testparse.c`.

`jc_toolloop.c` and `jc_calib.c` were reviewed with the agent loop
([Review 1](2026-10-08-review-01-the-agent-loop.md)).

---

## 6. The design, seen whole

**What the slice does well.**

- **The pure and effectful split is consistent,** and it is why so much of this
  slice is unit-tested.
- **The readers never trust their logs.**
- **Fences fail closed on what they cannot name, and open on what they cannot
  measure,** which is the right way round for each: a path you cannot resolve
  is denied; a disk you cannot measure is not reported full.

**Where it is weak.** The defects cluster where a primitive's contract is
unwritten:

- whether `jc_proc_capture` accepts a NULL buffer (1);
- whether it promises its timeout after the child closes its output (3);
- what counts as a frontmatter fence (4);
- what overflow the arena and the vector guard against (8).

*Lesson:* write a primitive's contract in its header, and test the edges of the
contract, not only its common case.

---

## 7. What this means for the context relay

From the reviewer's notes:

- **The relay reuses this slice's pieces:**
  - the atomic writer, for the notes;
  - the telemetry emitter, for the new `relay` events;
  - the claim-ticket store (`jc_toolout`), which the notes must *not* cite;
  - the disk guard, which must leave room for the notes.
- **Finding 3 matters for it:** a relay that runs a verify command at a handover
  must not hang on one that redirects its output.

---

## 8. Something to do

**Without a bench (the read-only twin):**

1. In §2, set `cap = 16` and build `"first part"` then `", second part"` by hand.
   What are `n` and `cap - n` after each call? Write the clamp that makes it
   safe.
2. In §3.1, show with small numbers (an 8-bit `jc_size`, maximum 255) why
   `extra > MAX - len - 1` is safe and `len + extra + 1 > MAX` is not.
3. In §3.4, list three situations in which `statvfs` cannot measure. Why is
   "do not stop" the right answer in each?

**With a bench:** read `tests/test_str.c` lines 204–233, the test that pins
§3.1's guards. Then read `tests/test_arena.c`, and confirm that nothing there
tests an overflowing request (finding 8). Writing that missing test, red first,
is a good first contribution.

## 9. Read further

- [DATA_STRUCTURES.md](../DATA_STRUCTURES.md), [ALGORITHMS.md](../ALGORITHMS.md),
  [OBSERVABILITY.md](../OBSERVABILITY.md), [TELEMETRY.md](../TELEMETRY.md): the
  references. *Primary for jichi.*
- [Fukabori 3](../reading/fukabori-03-the-three-arena-lifetime-model.md): why
  three arenas. *Primary for jichi.*
- The C99 standard, §7.19.6.5 (`snprintf`): the "would have written" return value
  of §2's idiom. *Primary.*
- RFC 7578, *Returning Values from Forms: multipart/form-data*: the
  `filename` parameter of §3.6. *Primary.*
- David R. Hanson, *C Interfaces and Implementations*, Addison-Wesley, 1996,
  chapter 6 ("More Memory Management", arenas): the technique behind `jc_mem`.
  *Secondary.*

---

## Completed on 2026-10-17 (at e5d832a4)

*This section finishes the review above, which was paused on 2026-10-08 at
`17bbb895` (M913). It does not rewrite the chapter: people may have acted on
it, so what changed is said here, next to it. Written by Claude Opus 5.5 on
2026-10-17 against master `e5d832a4`, as part two of the
[2026-10-17 hardening review](2026-10-17-hardening-00-overview.md) (called
"Phase 1" below), whose finding ids (A-01, B-05, ...) are used here. Every
earlier finding was re-read in the code at `e5d832a4`. Six were re-measured,
and each new finding marked "measured" was measured, with small test programs
built from jichi's own object files in a private copy: offline, no model, the
default build (no `-O`), on the review workstation, 2026-10-17.*

**Who this section is for:** the same reader as the chapter, a self-learner or
junior developer. **After reading it you will be able to:**

- say which of the chapter's findings are fixed, which still stand, and which
  changed shape;
- explain why `errno` must never be read after a call that succeeded, and spot
  the bug when it is hidden in an `else if` chain;
- explain how a fix can pass its own test and still move the defect, and how
  to write a test that would have caught it;
- read the ordered list of what to fix next, and what each item rests on.

**What you need first:** the chapter above (§2's accumulator idiom and §6's
lesson about contracts), and [Fukabori 3](../reading/fukabori-03-the-three-arena-lifetime-model.md).

### A. What happened between the two dates

About 45 milestones landed (M914 to M959). Of the 81 files in `src/util`,
11 changed, one of them new: `jc_relay.c`, the decision core of the *context
relay* (a long run that fills the model's context window ends one "leg" at a
clean boundary and hands written notes, the *baton*, to a fresh leg). Three of
the chapter's findings were fixed within three days, each with a test written
to fail first.

### B. The chapter's findings, one by one

"Fixed" names the milestone. "Phase 1" ids point into the
[hardening review](2026-10-17-hardening-00-overview.md), which carries the
finding from now on.

| Finding | Status at `e5d832a4` |
|---|---|
| 0, stack overflow in the end-of-run report | Fixed (M928); changed shape, see C2 |
| 1, `jc_proc_capture` crashes without an output buffer | Fixed as a crash (M929); the fix moved the defect, see C1 |
| 2, a secret registered for redaction lost within one turn | Fixed (M926): the registry now keeps its own copy |
| 3, timeout not enforced after the child closes its output | Still open; re-measured: a 1 s timeout returned after 4.0 s |
| 4, the two frontmatter parsers disagree (fail-open class) | Still open; re-measured, unchanged |
| 5, terminal control bytes: one class not stripped | Still open |
| 6, `learn apply` files rules under the wrong heading | Still open |
| 7, `learn apply` rewrites rules files in place | Still open; now Phase 1 D-18 (medium) |
| 8, arena and vector lack overflow guards | Still open (latent) |
| 9, readers compare a truncated key with the full one | Still open; re-measured: three rows where one was due |
| 10, the `strcpy`/`strcat` ban is not linted | Still open; now Phase 1 C-18 |
| 11, a short permission window when a private log is created | Still open |
| 12, the disk fence measures the workspace, not HOME | Still open |
| 13, the untrusted-content marker is fixed and public | Still open |
| 14, a spawn backend can leave jichi's directory moved | Still open; now Phase 1 B-09 |
| 16, the Landlock structure on 32-bit Linux | Still open; now Phase 1 E-02 (fails closed) |
| 17, privileged-command detection behind some wrappers | Still open; re-measured; the privileged half is Phase 1 B-05 |

One of the chapter's small notes no longer applies: the function it named,
`jc_eventlog_set_task`, has no caller left and can be deleted.

### C. Two fixes that moved the defect

This is the most useful lesson of the second pass, and it is about tests, not
about C.

#### C1. Finding 1, and why `errno` is only meaningful after a failure

**The term.** `errno` is a global integer that a C library call sets **when
it fails**, to say why. The C standard and POSIX promise nothing about its
value after a call that *succeeded*: it keeps whatever an earlier call left
there, and a library function may even change it while succeeding. So the
rule is: test the return value first, and read `errno` only on the failure
branch.

**The fix.** M929 made a missing output buffer legal: the child's output is
read and thrown away. In pseudocode, the read loop became:

```text
n = read(pipe)
if n > 0 and buffer exists:   append the bytes
else if n == 0:               end of output, stop reading
else if errno is not EINTR
        and not EAGAIN:       a real error, stop reading
```

Follow the case the fix was for: `n > 0` and **no** buffer. The first test
fails (no buffer), the second fails (`n` is not 0), and the third reads
`errno` after a `read` that *succeeded*. Unless an earlier call happened to
leave EINTR or EAGAIN behind, the loop stops after the first chunk and closes
the pipe. The child's next write then gets the signal SIGPIPE ("you are
writing to a pipe nobody reads"), and since jichi resets that signal to its
default for children, the child is killed.

**Measured** (a child that prints a line, waits 0.3 s, prints another, then
creates a marker file): with an output buffer it returns 0 and the marker
exists; with no buffer it returns -1 and the marker is missing, for every
stale `errno` value tried except EAGAIN. Eight call sites pass no buffer, all
cleanups: removing a pruned worktree or the tool-output store, deleting git
references, a git garbage collection. A cleanup that prints two warnings now
dies after the first, part-way through.

**Why the fix's test passed.** It ran a child whose whole output (1,000 bytes)
arrives in one read, so the early stop was never visible. The test was real
and red first; its fixture was gentler than the cleanup commands it stands
for.

**The fix of the fix** is to separate "did the read return data" from "where
does it go": `if (n > 0) { if (buffer) append; } else if (n == 0) ... else ...`.
Its test: a child that writes twice with a pause between, no buffer, `errno`
set to 0 before the call; assert exit code 0 **and** the marker file.

#### C2. Finding 0, and a test buffer larger than the real one

M928 removed the stack overflow the right way: the two halves of the report
are built in growable strings, and the text is cut once, at the caller's
buffer. But the caller's buffer is 1,100 bytes, and the fix's test used 2,400.
The first half of the report carries the whole list of files the run wrote
that its plan did not name, so with a long list the second half, which starts
with "THE ANSWER IS INCOMPLETE" on purpose, is cut off. **Measured** at
1,100 bytes: with 30 such files the line ends mid-sentence; with 40, the
whole "not checked" half is gone. `--output json` is not affected.

**The lesson of C1 and C2:** after a fix, test it against the production peer
(the real buffer size, a child that writes more than once), not against the
smallest case that turns the old test green. The project's own rule,
[TESTING_RUNBOOK.md](../TESTING_RUNBOOK.md) §3c, says "make the fixture behave
like the real peer"; it applies to the test of a fix as much as to the first
test.

### D. New findings

Severity uses the chapter's scale. Security-class items are described by class
only, as [SECURITY.md](../../SECURITY.md) promises; the details are with the
maintainers.

**Medium**

- **N1. A missing output buffer kills the child after its first chunk of
  output** (section C1). Measured. *Fix:* the corrected `if` chain.
- **N4. The failure classifier calls a script without its execute bit a policy
  refusal.** jichi reads a failed command's output to guess the cause of a
  repeated failure, and words like "denied" anywhere in that output win over
  the exit code. `./run.sh: Permission denied` (exit 126) is filed as a
  *policy* refusal, so the model is told it "will not succeed by rephrasing"
  and a delegating parent is told re-delegation "will be refused again", for a
  file that needs `chmod +x`. Measured. This extends an earlier finding of
  [Review 1](2026-10-08-review-01-the-agent-loop.md) (compiler and test
  failures filed as bad arguments); one fix covers both: when a command ran
  and exited non-zero, use the exit status before any wording.
- **N6. Information-exposure class, in one secret-redaction sink of the
  relay.** Measured. Off by default (the relay is opt-in) and reached only when
  a registered secret appears in the notes. *Fix:* one shared helper for
  "redact a copy, then use the copy's own length", used by all six sinks that
  do it by hand today.

**Low**

- **N2. The end-of-run report can lose its "not checked" half** (section C2).
  Measured. *Fix:* shorten the file list in the line ("40 files, e.g. ...")
  and keep the full list in the JSON field that already carries it.
- **N3. The failure-loop detector goes blind for the rest of a turn after 24
  distinct failures.** Its table is fixed-size so that nothing is allocated in
  the tool loop, a good decision, but a full table never forgets a row, and a
  headless run is one long turn. Measured: after 24 different failures, the
  same failing call nine times in a row produced no note. Its twin for
  successful calls already evicts the least-recently-seen row; the fix is to
  do the same.
- **N5. `doctor --live` still recommends the old setting for a model that
  writes its tool calls as text.** M949 made `toolCalling: "text"` the right
  setting for that case, but the advice still names `"none"`, and a model
  configured `"text"` is reported as `"native"`.
- **N7. Resource-exhaustion class, in the converter for fetched feeds.**
  Measured (default build, this workstation): crafted input of 256 KB took
  17 s, 512 KB took 70 s, and 2 MB, the largest feed jichi accepts, took
  18.6 minutes; the time grows with the square of the input. Nothing can
  interrupt it.
- **N8. `jc_proc_capture` returns -1 both when a child could not start and
  when it was killed by a signal.** A PDF extractor that crashes is reported as
  "not installed"; a user tool that crashes as "failed to start". The shell
  tool already uses the shell's convention, 128 plus the signal number.

**Notes** (each verified by reading): a signed overflow on a very long
`timeout` duration in a model-written command (the class of Phase 1 A-12);
`tee` counted as a filter that writes nothing; an unknown stop name from a
child read as "done"; a calibration table that stops learning new models
silently when full; a failing `setup` step of a graded assignment that is
never shown; one more caller-sized buffer that is safe only at today's size
(the class of A-11). The two `src/util` parts of
[Review 8](2026-10-08-review-08-tui-setup-acp.md)'s finding on terminal escapes
(the ghost text and the tool-call summary) are still open.

### E. The files nobody had read

§5 of the chapter listed 13 files no reviewer had reached and two left to
another reviewer. All are read now: `jc_voice.c`, `jc_rss.c` and
`jc_delegreport.c` in this pass (N7 is from `jc_rss.c`); `jc_toolloop.c` and
`jc_calib.c` by Review 1 and again here (N3, N4); the other ten by the Phase 1
reviewers, with their findings there (A-03 for the diff, D-01 for patches,
A-12 for test output, B-07 and D-17 for the lease, C-18 for the audit view).
Every file in `src/util` has now been read by at least one reviewer.

### F. The relay, then and now

§7 of the chapter said which pieces the relay would reuse. It shipped in
M920 to M954, and most of the advice was followed: the notes are written
atomically and privately, are redacted, are cut on a character boundary,
live outside the per-turn memory, and have their own journal events and stop
reasons (`handover`, `relay_stalled`). Two pieces are still missing: the disk
fence is not consulted before the notes are written, and finding 3 still
applies to anything a leg runs through `jc_proc_capture`. One piece was done
with a defect (N6).

The `switch` statements of §3.2 did what that section promised: the two new
stop reasons forced a decision at every site, at compile time.

### G. The design, seen whole, after the second pass

- **The pure cores held.** Every changed pure function came with a unit test,
  and the new relay core was written pure from the start. The defects of the
  second pass sit at the *edges*: a read loop, a buffer size at a call site, a
  file write after redaction. That is where to look first in a code review.
- **Copies of one rule drift.** Six places redact into a copy and must then
  re-measure it; one did not. Two places cut text at a character boundary,
  with two rules. Two places turn a child's exit into a number, with two
  conventions. Each pair is one helper waiting to be written.
- **A fix needs a review too.** Two of the three fixes were correct about the
  line they changed and wrong about a neighbour. A second reader of a fix
  should ask: what does the test's fixture *not* do that production does?

### H. What to fix, in order

Each item says what it rests on.

1. **`jc_proc_capture`, three items together:** N1, finding 3 and N8. One
   function, one test file; every hook, user tool, notifier, grader and
   cleanup goes through it. *Measured* (N1, finding 3); N8 by reading.
2. **One redaction helper** for the six sinks (N6). *Measured.*
3. **One frontmatter fence rule** for the parser and the validator (finding 4),
   because it guards an agent profile's read-only flag. *Measured.*
4. **Classify a failed command by its exit status first** (N4 with Review 1's
   finding). *Measured.*
5. **Strip the remaining terminal control bytes** (finding 5) and the two
   escape paths of Review 8 that run through `src/util`. *Expected* to matter on some terminals; the
   terminal behaviour is still *unknown* (not measured).
6. **Bound the file list in the end-of-run line** (N2). *Measured.*
7. **Evict in the failure-loop table** (N3) and **make the feed converter
   linear** (N7). *Measured.*
8. The rest, as maintenance: finding 9's key comparison, N5's advice text,
   finding 8's overflow guards, finding 10's lint (C-18).

### I. Something to do

**Without a bench:** in the pseudocode of C1, list every combination of
"`n` is positive, zero or negative" and "buffer present or absent", and say
for each which branch runs and whether `errno` is read. Which combination reads
an `errno` nobody set? Rewrite the chain so that no combination does.

**With a bench** (in a copy of the tree, not your working checkout):

1. Add the test of C1 to `tests/test_proc.c`, before the fix. Run `make test`.
   You should see `2 failures`, both from your new test (the exit code is not
   0, the marker file is missing). If you see `0 failures`, check that the
   child pauses between its two writes.
2. Apply the fix of C1 and run `make test` again. You should see `0 failures`.
3. Remove the pause from the child, undo the fix, and run `make test` once more.
   Does the test still fail? It should not: when both lines arrive in one
   read, the early stop does no harm. Measured on the review's bench: 5
   `make test` runs out of 5 passed against the unfixed code. A test that
   passes without its fix tests nothing, which is why the pause is part of
   the test.

### Read further

- ISO/IEC 9899:1990 (C90, the same text as ANSI C89), §7.1.4, and
  POSIX.1-2017, `errno`: "the value of
  errno ... is never set to zero by any library function", and it is
  meaningful only after a reported failure. *Primary.*
- POSIX.1-2017, `write()`: the EPIPE error and the SIGPIPE signal when the
  reading end of a pipe is closed. *Primary.*
- W. Richard Stevens and Stephen A. Rago, *Advanced Programming in the UNIX
  Environment*, 3rd ed., Addison-Wesley, 2013, chapter 15 (pipes and
  SIGPIPE). *Secondary.*
- [TESTING_RUNBOOK.md](../TESTING_RUNBOOK.md) §3c, "Make the fixture behave
  like the real peer": the incident (M882) that made it a rule. Sections C1
  and C2 above are two more cases of it. *Primary for jichi.*
