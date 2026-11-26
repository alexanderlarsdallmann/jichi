# Functional style in C89 for jichi: what the language allows, what jichi would use, and why the JSON layer comes first

*2026-11-19. A design note, not a plan: nothing here is built. Written for a contributor who wants
pattern matching or function chaining in jichi's C, and for a reviewer deciding whether a milestone
that adds them is worth its cost. Read it with [`include/jc_json.h`](../../include/jc_json.h) open.
You need the C89 rules in [`CONTRIBUTING.md`](../../CONTRIBUTING.md) and the three arenas in
[`CLAUDE.md`](../../CLAUDE.md#conventions--invariants). The register row is
[`DEFERRED.md` def-417](../DEFERRED.md#def-417).*

## 1. The question

The operator asked, on 2026-11-19: what options does C89 give for a library that provides pattern
matching and function chaining, which design decisions and alternatives does it involve, what are the
implications, and which use-cases in jichi are reasonable? A second question followed from the first
answer: would extending jichi's JSON layer be the better option?

The short answer:
- Do not build a general functional library.
- Extend the JSON layer first.
- Add two small conventions beside it: a sticky-error builder and dispatch tables with an explicit
  "unknown" arm.
- Measure on one provider before the third step.

## 2. What C89 rules out

| You would want | C89 has | So |
|---|---|---|
| lambdas, closures | no | every "lambda" is a named `static` function plus a `void *` context |
| generics | no `_Generic` (C11), no `typeof` | `void *` plus an element size, a tagged value type, or macro-made copies |
| a pattern literal in an expression | no compound literals, no designated initializers | patterns are `static` arrays with positional initializers |
| a macro language | no variadic macros (C99) | a `MATCH(...)` or `PIPE(...)` needs one macro per arity |
| cheap small combinators | no `inline` (C99) | every combinator call is a real, often indirect, call |
| a comparator with context | `qsort`/`bsearch` take none | `qsort_r` is not standard, and glibc and the BSDs order its arguments differently |

GCC's nested functions and Clang's blocks are not C. Nested functions that are referenced by address
also need an executable stack for their trampolines, which a hardening review would refuse anyway.

## 3. The design decisions, their alternatives, their implications

### 3.1 Closures: a function pointer and a context pointer

This is the idiom jichi already uses: `struct jc_stream_sink { on_text, user }`
(`include/jc_provider.h`), `jc_sse_cb` and `jc_http_chunk_cb` both take a `void *user`.

Alternatives:
- a closure struct that bundles the two (the same thing, named);
- macros that stamp out a typed copy per element type (type-safe, but every use reads as macro
  expansion).

Implication: type safety ends at the `void *`, so a test has to catch the wrong context.

### 3.2 Chaining: sequential statements with a sticky error, checked once

```c
/* sketch, not compiled */
jc_sb_init(&b);
jc_sb_append(&b, head);
jc_sb_append_fmt(&b, "%d", n);
if (jc_sb_status(&b) != JC_OK) { ... }   /* the first failure, kept */
```

This is the `FILE *` and `ferror()` pattern, and it is honest C: the receiver is named in every call.

Rejected:
- **The hidden receiver.** A published C library, `google/iso-c-chaining`, gets `x->f()->g()` syntax
  from function pointers that act on a per-type "self" stack instead of on the pointer written before
  the arrow.
  - Measured on 2026-11-19 in a scratch build of its commit `0a89260`: with two saved slices `a` and
    `b`, `a->add(x)` written without the rebinding macro put `x` into `b`.
  - It compiles cleanly and changes the wrong object. That is the design, not a corner of it.
- **Returned-object nesting** `f(g(h(x)))`: it reads inside out, and an error still has to be checked
  at every level.

Kept for later: a pipeline described as data, an array of `{op, fn, ctx}` steps run by one loop.
It is worth it only where the stages are configurable, the way the gate's stage table is. Lazy
iterators save the intermediate arrays, but they cost complexity no measured use needs.

### 3.3 Pattern matching: patterns as data, read by one interpreter, with an explicit "no match"

A pattern is a `static` table. An interpreter walks it, binds captures into the caller's slots, and
returns either a match or **"no match, because ..."**. That fallback must never be a finding: it
says "unknown" (the classifier rule in [`CLAUDE.md`](../../CLAUDE.md), from M519 and M628).

Rejected:
- **A scanf-style format string** (`"{type:%s}"`): its varargs are invisible to the compiler's
  format checking, so a wrong capture type is found at run time or not at all.
- **A `MATCH`/`CASE` macro language**: it hides control flow, and without variadic macros it is
  awkward to write.

`switch` over an enum stays the tool for jichi's own tagged unions.

### 3.4 Errors: a status passed along, never a jump

Every step passes the first error through, and the caller checks once. `setjmp`/`longjmp` is in
C89, but a jump skips the cleanup of a cJSON tree or an open file.

### 3.5 Memory: arenas, not reference counts

Intermediate values go in the per-call or per-turn arena, so nothing is freed one by one and
immutability is free. A value that outlives the arena is copied out at the boundary. Reference
counting is rejected: in the library of 3.2, which methods free a temporary depends on where the call
sits in the chain.

### 3.6 The implication that is jichi's own: models write jichi

jichi is developed by agents, and agents copy the nearest code
([`docs/analysis/COMMIT-ATTRIBUTION.md`](COMMIT-ATTRIBUTION.md) is the commit-identity case of it).
Every abstraction becomes a dialect the lead model and the worker models must use correctly. So each
piece must:
- read as plain C at the call site;
- be small enough to read whole;
- be guarded by a lint;
- have every pattern and every helper shown red first.

Performance hardly matters next to model latency, except on the stream path (SSE framing, the
provider's event handler).

## 4. What jichi does today, measured

At commit `98bfcbd0` (the harness-port drive's branch, 2026-11-19), from the repository root:

| Command | Result |
|---|---|
| `grep -rhoE 'cJSON_GetObjectItem(CaseSensitive)?\s*\(' src \| wc -l` | 251 |
| `grep -rlE 'cJSON_GetObjectItem' src \| wc -l` | 50 |
| `grep -rhoE 'jc_json_get_[a-z_]+\s*\(' src \| wc -l` | 768 |
| `grep -rhoE 'cJSON_Is[A-Z][a-zA-Z]*\s*\(' src \| wc -l` | 415 |
| `grep -rhoE 'jc_json_get_str\s*\(' src \| wc -l` | 364 |
| `grep -rhoE 'jc_json_get_str\s*\([^;]*,\s*NULL\s*\)' src \| wc -l` | 251 |
| `grep -rhoE 'strcmp\s*\([^;]*\)\s*==\s*0' src \| wc -l` | 1243 |
| `grep -rlE 'strcmp\s*\(\s*[a-z_]*type\s*,' src \| wc -l` | 9 |
| `grep -rlE '\bjsonq\b' tests/smoke \| wc -l` | 91 |
| `wc -l tests/tools/jq_core.c tests/tools/jq_core.h` | 129 + 53 |

What the numbers say:
- **Reading foreign JSON is the largest surface:** about 1,400 call sites look into a cJSON tree.
- **jichi's own helper family is already the idiom:** 768 `jc_json_get_*` calls against 251 raw
  lookups.
- **Presence matters more than defaults:** 251 of the 364 string reads pass `NULL` as the default,
  so the caller can tell absent from present. Numbers and booleans have no such channel: their
  default is a value.
- **Type dispatch is a pattern of its own:** 9 files choose a branch with `strcmp` on a type field.
  They are the providers, ACP, MCP, the control protocol, the config loaders and a converter.
- **Unknown values fall through silently.** In `src/provider/jc_provider_anthropic.c`, the
  `content_block_delta` branch ignores a `delta.type` it does not know. No count records it.
- **A path language already exists, but only in the tests.** `tests/tools/jq_core` is 182 lines.
  Through `jsonq` it is used by 91 smoke drivers, and it already links the product's own parser
  (M209, decision D8).

Two caveats:
- The `strcmp` pattern undercounts: two comparisons on one line match once.
- No lookup is written as a single nested expression. Lookups go through a variable and rely on the
  getters accepting `NULL`, which they do. The current code reads well, so a new layer has to earn
  its place.

## 5. The recommendation: extend the JSON layer first

The operator's reading is right, for three measured reasons:
- the JSON surface is the largest;
- the `jc_json_get_*` family is what authors and models already reach for, so an extension follows
  the nearest code instead of introducing a dialect;
- the path code exists and is exercised.

The steps, each its own milestone, each red first:

- **J1. A path lookup in the product.**
  - Promote `jq_core`'s dot-path parser into the `jc_json` layer: keys and indexes, no `malloc` per
    key (walk the path text, or take the arena).
  - `jsonq` then links the product's version, so the 91 drivers exercise it. That is D8's rule, one
    step further.
  - Keys that contain `.` or `[` need an escape or a step-array form. MCP tool names and some config
    keys can carry a dot.
- **J2. Getters that return an outcome.**
  - Found, missing or wrong type, as a value the caller can branch on, with the leniency decided per
    field by who wrote the JSON (the rule in [`CLAUDE.md`](../../CLAUDE.md), M519 and M530).
  - This gives numbers and booleans the presence channel strings already have, and lets a handler
    count what it did not understand.
- **J3. A field-table matcher, only if J1 and J2 leave a gap.**
  - The table of 3.3, built on J1 and J2.
  - Decide it by measurement: rewrite one provider's event handler (the Anthropic one) with J1 and J2
    in a scratch branch, then compare lines, the unknown-type outcome and the tests.
  - If J1 and J2 already make the handler clear, J3 is not built.

`cJSON.h` keeps its API exactly. Every extension lives in `jc_json.h`, jichi's own layer, so the
promise that `src/json/cJSON.c` implements the cJSON API stays true.

Beside the JSON layer, smaller and independent:
- **S1. A sticky-error builder:** `jc_sb` keeps its first failure; a fixed-buffer formatter keeps a
  sticky "cut" flag. It pairs with the `jc_snprintf(...) < 0` truncation finding of the same day,
  where `< 0` was the only check at 97 call sites, measured by a census that masks comments and
  string literals.
- **S2. Dispatch tables with an explicit "unknown" arm:** for command, tool and event names. A
  sorted table with `bsearch`, and "did you mean" from `jc_str_closest`.
- **S3. At most five sequence helpers on `jc_vec`:** filter, map, fold, find, any/all, each with a
  context pointer. `jc_vec` has none today. The value is modest.

Not reasonable for jichi:
- a general functional library (persistent collections, currying, monads, lazy streams): no measured
  use needs one;
- a hidden receiver;
- a macro language for match or lambda;
- `longjmp` to short-circuit a chain.

## 6. When to pick it up

The register row carries the trigger. Pick it up at the first of:
- the next milestone that adds or rewrites a wire dialect's event handler;
- the first defect traced to a JSON default that hid an absent or mistyped field;
- the snprintf truncation lint's milestone, for S1.

## Read further

- [`DEFERRED.md` def-417](../DEFERRED.md#def-417): the register row and its trigger.
- [`include/jc_json.h`](../../include/jc_json.h): today's getters and the lenient readers.
- [`tests/tools/jq_core.h`](../../tests/tools/jq_core.h): the path syntax J1 would promote.
- [`TEST_INTEGRITY.md`](../TEST_INTEGRITY.md): why every pattern and helper is shown red first.
