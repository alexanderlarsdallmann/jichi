# When the model speaks an older dialect of your language

*For anyone using an AI coding assistant — jichi or otherwise — on a language or
framework that has had breaking changes. Written from a measured case, not from
speculation: every number below came from a command, and the ones that are weak
evidence say so.*

---

## 1. The symptom

You are on Zig 0.16. Your `AGENTS.md` says so. Your `build.zig.zon` pins it. The model
still writes:

```zig
var list = std.ArrayList(u8).init(allocator);   // error: has no member named 'init'
const m = std.math.min(a, b);                   // error: has no member named 'min'
```

You correct it. Two files later it does it again. You add a note to the project's
memory. It does it again.

This is not the model being careless, and it is not random hallucination. It is a
**dialect mismatch**: the model's training data has a centre of mass at an older
version of your toolchain, and in the absence of a reason to do otherwise it writes the
version it knows best.

## 2. How to tell a dialect problem from a hallucination

Do this before you change anything, because the two need different fixes. Collect the
errors and date each API.

From the measured case:

| API the model wrote | When it was valid |
| --- | --- |
| `std.ArrayList(T).init(alloc)` | up to 0.14; removed when ArrayList became unmanaged |
| `std.math.min` / `max` | removed in 0.12, replaced by the `@min`/`@max` builtins |
| `*std.mem.Allocator` (by pointer) | the pre-0.11 convention |
| `allocator.print(...)` / `.fmt(...)` | **never existed in any version** |

The first three cluster: they are all coherent older-Zig, roughly 0.11–0.13. That is a
dialect. The fourth belongs to no version at all — that is confabulation, and no amount
of version guidance will fix it, because there is no version in which it was right.

**The diagnostic is the clustering.** If the wrong APIs are internally consistent with
one another and with some past release, you have a dialect problem, which is
addressable. If they are scattered across versions and non-versions, you have a model
that is guessing, which is a different and worse situation.

## 3. Why stating the version does not work

The obvious fix is to put the version in the prompt. In the measured case it was
already there — twice — in a file that jichi loads into every single request
(`rules ~1616` tokens, confirmed with `jichi context`). The model read "Zig version:
0.16.0" on every call and kept writing 0.13.

The reason is worth internalising:

> **A version number is not actionable knowledge.** "0.16" only helps a reader who
> already knows what changed between 0.13 and 0.16. A model whose centre of mass is
> 0.13 has no such mapping. You have told it *which* dialect to speak without telling
> it *how* that dialect differs from the one it knows.

This generalises past Zig. "We're on React 19", "this is Pandas 2.x", "Rust 2021
edition" are all the same shape: a label the model cannot expand into concrete
differences.

## 3b. Do this first: make the toolchain's source readable

**This section is a correction to the one below it.** When this page was written the
delta table was presented as the fix. It is a fix, and it is not the first one. The first
one is cheaper, more accurate, and was available all along:

> **Point your assistant's read-fence at the standard library's source.**

Most toolchains ship it. Zig's arrives with the compiler
(`<install>/lib/std/*.zig`); Python's is in `lib/python3.x/`; Go's in `$GOROOT/src`;
Rust's via `rustup component add rust-src`. jichi refuses reads outside the workspace
when the path fence is on, so it must be listed explicitly:

```json
{ "referenceRoots": ["/home/you/.local/lib/zig-x86_64-linux-0.16.0/lib"] }
```

or `--reference-root <dir>` per run. Writes stay workspace-only; this grants reads only.

**The measurement.** Before: a bounded run spent **four tool calls** compiling throwaway
files to discover what replaced a removed `std.time.Timestamp` — `_ = std.time.Clock;`,
then `std.time.timestamp(...)`, then two more — and never did find it. After: one
`read_file`, a correct answer, with line numbers, and the model volunteered that the file
contains *no* replacement — which is the true answer and the one guessing cannot reach.
The replacement had moved to a different module entirely (`std.Io`), and no amount of
probing `std.time` would have found it.

**Why this beats the table on accuracy, not just cost.** A delta table's rows come from
failures you happened to observe, so it is a record of your bad luck. The source is
ground truth, and it answers the questions a table cannot: *what is the new name*, *what
are its parameters*, and — the one that matters most — *is there a replacement at all*.
Three of the rows in this project's own table were only written correctly after reading
the source; two of them had been wrong.

**Keep the table anyway, for two things it does that reading cannot.** It fires without
the model choosing to look, which matters precisely because a model speaking the wrong
dialect does not know it should look. And it can say *"never existed"* — the one claim no
amount of source-reading will volunteer, because absence does not appear in a file. So:
source for the answers, table for the reflex, and keep the table short.

**And the operator guesses too — that is the harder half.** This section was written
about the model reaching for an API it half-remembered. The same failure, from the person
driving, is more expensive because nothing contradicts it.

A worked case. Four functions in a downstream project were blocked on a removed API, and
the operator wrote in three separate code comments, an analysis document and a milestone
entry that porting them "means deciding where this project's `Io` comes from" — framing it
as an architectural decision and deferring it three times. It also justified deleting a
mutex and a timer field.

One `read_file` of `std/process.zig` retired the whole thing:

```zig
pub const Init = struct {
    io: Io,                        // "an appropriate default Io implementation"
    environ_map: *Environ.Map,     // "Environment variables, initialized with `gpa`"
    arena: *std.heap.ArenaAllocator,
    gpa: Allocator,
    ...
};
```

The runtime hands a program both the `Io` and the environment, via the parameter
`pub fn main(init: std.process.Init)` receives. There was no decision. It had been
invented — plausibly, consistently, and in writing — by reasoning about an API instead of
reading it. The same session had *already* found that the two functions needed no `Io` at
all, because raw syscalls served; that near-miss should have prompted the read and did not.

The tell, in hindsight, is worth naming: **a "design decision" that never acquires
options.** It was restated five times and never once enumerated a choice, because there was
nothing to choose between. If a deferred decision cannot name its alternatives, the thing
to do is read, not defer.

**One caution.** Give it the version you build against, and nothing else. Having a newer
toolchain's source on the same machine is useful *to you* for forward-compatibility
checks, but a reference root pointing at it invites code that will not compile — the
mirror image of the problem this page is about.

## 4. What also works: a delta table, in the always-loaded rules

Replace the label with the differences. Concretely, in the file your tool injects on
every request (`AGENTS.md` / `CLAUDE.md` / equivalent):

```markdown
## Zig 0.16 API deltas — check these BEFORE writing code

| Do NOT write | Write instead |
| --- | --- |
| `std.ArrayList(T).init(alloc)` | `var l: std.ArrayList(T) = .empty;` then pass the allocator to `append`/`deinit` |
| `list.append(x)` | `list.append(allocator, x)` |
| `std.math.min(a, b)` | `@min(a, b)` — removed in 0.12 |
| `std.mem.join(u8, sep, parts)` | `std.mem.join(allocator, sep, parts)` — allocator FIRST |
| `allocator.print(...)` | `std.fmt.allocPrint(allocator, ...)` |
```

Four properties make it work, and all four matter:

1. **Old form beside new form.** The model needs the mapping, not the destination.
2. **Every row is one you actually saw fail.** A speculative list of "things that might
   have changed" is long, costs tokens on every call, and dilutes the rows that matter.
3. **In the always-loaded rules, not a skill or a doc.** Progressive disclosure is the
   right default for most guidance, but it only helps if the model chooses to load it —
   and a model that does not know it is speaking the wrong dialect has no reason to go
   looking. This is the rare case for paying the always-on cost.
4. **Short.** The measured table cost ~514 tokens per call (rules went 1616 → 2130).
   That is real money on a backend without prompt caching. Keep it to observed failures.

### The measurement

One run, immediately after adding the table, on a task whose seven known errors included
two of the exact rows. Counting only what the model **newly wrote** — parsing the
tool-call arguments and reading `content` and `new_string`, never `old_string`, since a
hit there means it was *deleting* the old form:

```
wrote 7497 B of new code

OLD-Zig APIs newly written:
  ArrayList(...).init(     written= 0   (removed= 4)
  std.math.min/max(        written= 0   (removed= 1)
  allocator.print/.fmt(    written= 0
  list.append(x) 1-arg     written= 0

NEW-style APIs written:
  @min/@max builtin        1
  ArrayList .empty         7
  append(allocator, x)     12
```

Zero old-dialect APIs in newly-written code, and it actively removed five. Before the
table, `ArrayList(...).init` had appeared in three separate runs on three separate
tasks.

**How strong is this evidence?** Suggestive, not conclusive. It is n=1 with no control
arm: the earlier runs that showed the old dialect were different tasks, so task
difficulty is confounded with the intervention. If you want to know whether it works on
*your* model, hold the task fixed and vary only the table — that is the A/B this case
did not run.

## 5. What the table does NOT fix, and why that matters more

The same run then failed anyway, on a different error class:

```
src/animation/index.zig:66: error: expected type 'mem.Allocator', found '*mem.Allocator'
src/animation/test.zig:27:  error: type '*Track' does not support struct initialization syntax
```

Note the direction: the model passed a **pointer where a value was wanted** — the
opposite of the earlier mistake. This is not a dialect problem, and it is not fixable by
any table about Zig versions, because it is not a Zig question. That codebase passes
allocators **three different ways**:

- by value — `std.mem.Allocator`
- mutable pointer — `*std.mem.Allocator`
- const pointer — `*const std.mem.Allocator`

No consistent rule predicts which a given function wants; you must read each signature.
A model cannot infer an inconsistency, and neither can a human without looking. The
model was not being stupid — it was guessing at a coin flip the codebase had left for it.

> **The general lesson: a dialect problem is cheap to fix and an inconsistency problem is
> not.** Version drift yields to a lookup table. Your own codebase's incoherence does
> not, and it will dominate your error rate once the easy class is gone. If an AI
> assistant keeps making "stupid" mistakes in one area of your code, check whether that
> area is actually ambiguous before blaming the model.

A related trap in the same case: the codebase's own sources already used the older
`*std.mem.Allocator` convention in places. If a codebase was substantially
model-written, the dialect is baked into the source, and the model reads those
signatures back as evidence for what this project does. The mistake becomes
self-reinforcing, and the fix is to make the codebase consistent, not to keep correcting
the completions.

## 6. Choosing a model, with this in mind

- **Test the dialect before you commit to a model.** Ask it for ten lines using the APIs
  your project touches most, and date what comes back. Five minutes here is worth days
  later. A cheap, fast coder-tuned model can be an excellent choice *and* be two years
  behind on your stdlib.
- **Recency of the toolchain matters more than benchmark scores** for a language that
  breaks compatibility. Zig, pre-1.0 frameworks and fast-moving ML libraries punish an
  older model far more than C89 or POSIX shell would. Judge a model against *your*
  dependency's release cadence, not a leaderboard.
- **Cheap tier for mechanical work, but pay for the delta table.** In this case the
  coder-tuned tier produced the project's working code across hundreds of calls; its
  weakness was narrow and patchable. Discarding it over the dialect would have been the
  wrong call.
- **Prefer a model that fails loudly.** A wrong API is a compile error, which is the
  best kind of mistake: the toolchain catches it for free. The expensive failures in the
  same session were the ones that *compiled* — a function returning `from` instead of
  interpolating, a serializer emitting a `String` where a NodePath was meant. Those cost
  far more than any dialect slip.
- **Write the gate before the code.** None of the above matters if your check cannot
  tell success from a no-op. See `AUTONOMY.md` on `--verify`, and
  `analysis/2026-08-07-driving-zigodot-harness-findings.md` §3 and §8 for five gates
  that all initially passed while the work was absent or wrong.

## 7. A checklist

When an assistant repeats a "silly" API mistake:

0. **Make the toolchain's source readable first** (§3b) — a `referenceRoots` entry, or
   `--reference-root`. It is one line, it is ground truth, and it answers the question a
   table cannot: whether a replacement exists at all. Measured at four probe calls → one
   `read_file`.
1. **Collect and date the wrong APIs.** Clustered around one old release → dialect.
   Scattered or nonexistent → confabulation; a table will not help.
2. **Check the reference is actually reaching the model.** In jichi, `jichi context`
   shows whether your rules are in the prompt and what they cost. Do not fix a delivery
   problem you have not confirmed.
3. **If it is being delivered and ignored, the content is wrong, not the channel.**
   Replace labels with mappings: old form → new form.
4. **Put the table in the always-loaded rules** and measure the token cost
   (`jichi context` — this project's went 2130 → 3352 tokens per call). Confirm each row
   against the source now that you can read it, rather than trusting the compile error
   that prompted it: two of this project's rows were wrong until checked that way.
5. **Verify by parsing what the model WROTE**, not by grepping your logs — the old API
   appears in your own brief, in compiler errors, and in the `old_string` of a correct
   fix. All three will fool a naive `grep`.
6. **Then look for ambiguity in your own code.** What remains after the dialect class is
   usually your inconsistency, and that is the more expensive half.

---

*Case notes and the full run data:
[`analysis/2026-08-07-driving-zigodot-harness-findings.md`](analysis/2026-08-07-driving-zigodot-harness-findings.md).
The story as an anecdote: [`ANECDOTES.md`](ANECDOTES.md) #37.*
