# Annai 7 — Memory, the jichi way

*[案内（あんない）*Annai* — the guided tour](ANNAI.md) · chapter 7 of 10*

## Why this exists

C makes you answer a question most languages hide: **how long does each
piece of data need to live?** jichi's answer is unusually legible — three
**arenas**, one per lifetime, each with a name you can say out loud — and
it is legible because getting it wrong cost this project its worst bugs.
This chapter gives you the model; curriculum set D (tasks 20–22) then
puts your hands on the same bug classes in miniature.

## The shape

```mermaid
flowchart TD
    subgraph lifetimes, longest to shortest
        A["session arena (app->arena)\nconfig, rules, tool registry...\nfreed at exit"]
        S["per-turn scratch\nsystem message, expansions...\nreset when the NEXT turn starts"]
        T["per-tool-call scratch\na file's bytes while a tool works\nreset before EVERY tool call"]
    end
    A -.->|"never per-turn data here"| S
    S -.->|"never per-call data here"| T
```

The rule for contributors — and for you as a reader — is one sentence:
**pick the shortest lifetime that outlives the data.** Every arena
misuse in the project's history was that sentence violated in one
direction: per-call data placed on a per-turn or per-process lifetime,
where it was never *leaked* (everything is freed at exit) and never
*freed in time* either.

> **C sidebar — what an arena is.** A bump allocator:
> `src/util/jc_mem.c:jc_arena_alloc` hands out slices of a block by
> advancing a pointer; there is no per-object free. Reclaim is wholesale:
> `src/util/jc_mem.c:jc_arena_reset` returns every block at once. You
> trade fine-grained control for two things C sorely lacks — allocation
> that cannot leak piecemeal, and a *lifetime you can point at in the
> code* ("this arena resets at line X" is a provable claim; ten thousand
> matched `free()` calls are ten thousand claims).

## The idea

Where the loop resets what, as pseudo code:

```
per user turn (jc_agent_run_turn, top level):
    reset per-turn scratch          # last turn's expansions die here
    reset per-tool-call scratch     # and anything a stray caller parked
    build system message ON per-turn scratch
    loop (run_agent_loop):
        model call ...
        for each tool call:
            reset per-tool-call scratch      # <- before EVERY call
            execute tool                     # file bytes live HERE
        ...
# session arena: touched at startup, freed at exit, ideally silent between
```

The subtle clause: a tool that *spawns a nested agent* (subagents, later
in your reading) cannot keep anything on the per-tool-call arena across
that nested run — the nested run's own tool calls reset it. That
invariant is stated where the arenas are declared and enforced by a lint
(below), because it is exactly the kind of rule comments alone do not
keep.

## The C

1. **`include/jc_app.h:jc_app_scratch`** — read the comments on the three
   arena members and the two accessor functions. This half-page is the
   contract; everything else is enforcement.
2. **`src/util/jc_mem.c:jc_arena_reset`** — one screen: blocks are
   freed back to `malloc` except the first, so a reset arena is
   genuinely small again, and `src/util/jc_mem.c:jc_arena_used` is the
   gauge behind everything you are about to measure.
3. **The gauge, wired up:** `src/chat/jc_context.c:jc_context_report` —
   the `/context` command's engine. It prints each arena's used/reserved
   bytes *and* the process's resident-set size next to them, because the
   gap between those numbers is where one whole class of bug hides.
4. **The enforcement:** `tests/smoke/arena_lint.sh` — a lint, not an
   audit: every use of the session arena in the hot layers must be on an
   allowlist with a written reason. Read its header comment; it names
   the incidents that justified it.

> **AI sidebar — why an agent stresses memory strangely.** An agent
> workload is hostile in a specific shape: one *turn* can run hundreds of
> tool calls (a marathon `--auto` run), so "per-turn" cleanup can be no
> cleanup at all; and every model call re-sends the whole conversation,
> so the request-build path allocates and frees the same large buffers
> thousands of times. Both shapes defeat the usual instruments — which
> is the next point.

**The lesson under the lesson:** a leak checker cannot see these bugs.
Memory reachable until exit is, to ASan and valgrind, *zero leaks* — while
the resident set climbs into gigabytes. The instruments that work are a
**footprint gauge** (is live memory flat across identical work?) and a
**peak** (what was the worst moment, not the end state?). This project
learned that at measured scale — a session listing that retained ~491 MB
with every checker green, an intra-turn peak of 50 MB that per-turn
*slope* reported as 0.0 (`docs/analysis/2026-07-29-tool-arena.md`), and a
balanced malloc/free pattern that still grew the process because of how
the C library's allocator responds to it
(`docs/analysis/2026-08-01-telemetry-memory.md`).

## Prove it to yourself

The gauge, live — open the TUI, run `/context`, and read the arena lines
against the diagram. Then make the lifetimes move: ask for something
tool-heavy ("read every file in src/util and summarize each"), run
`/context` again mid-conversation, and watch which numbers grew (history)
and which stayed flat (the scratches — reset per call, exactly as the
pseudo code claims).

Then the curriculum's hands-on versions, in order:
[assignment 20](../assignments/20-the-wrong-lifetime.md) (the wrong
lifetime, with an arena gauge), [21](../assignments/21-the-invisible-growth.md)
(growth no leak checker sees), and [22](../assignments/22-slope-lies-keep-the-peak.md)
(write the checker that convicts by the peak). Each is one of the real
incidents above, shrunk to a fixture.

## Where this bit us

Three escalating rounds, all documented: session listings and tool reads
retaining whole files on the process-lived arena (fixed by moving reads
to shorter lifetimes); a read-heavy turn defeating per-turn cleanup
(fixed by *adding* the third arena — the per-tool-call one — after an
audit showed per-call reset was almost, but not quite, always safe); and
finally the allocator itself keeping freed memory from the OS under the
agent's exact churn pattern (fixed with two `mallopt` lines and a sweep
at turn boundaries — `src/util/jc_memtrim.c`, probed at build time
because not every libc has it). The arc is the chapter's real content:
each fix made the *next* bug measurable, and the gauges came before the
cures every time.

*Next: [chapter 8 — when the conversation gets too long](annai-08-when-the-conversation-gets-too-long.md):
the other unbounded thing.*
