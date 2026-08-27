# Elixir by contrast — the actor model, and jichi's fork pool by another name

*The third member of the **functional-programming family** (the map is
[CURRICULUM.md](CURRICULUM.md); start with [RACKET_PARADIGM.md](RACKET_PARADIGM.md)).
Elixir is functional, so the paradigm lessons of the Racket track carry over —
this track spends its length on what is distinctive: Elixir runs on the
**BEAM** (the Erlang virtual machine), whose entire reason to exist is
**concurrency — millions of cheap isolated processes passing messages, kept
alive by supervision trees that make "let it crash" a fault-tolerance
strategy.** And jichi already runs a hand-rolled, OS-level version of exactly
that: its fork-based parallel pool. So this track reads jichi's concurrency C
against the model it approximates. A reading track, no graders, like the others.
Every snippet was run on the reference box (Erlang/OTP 25) and its output
quoted.*

## The paradigm transfers — see the Racket track

Immutable data, pattern matching, functions as values: same as
[RACKET_PARADIGM.md](RACKET_PARADIGM.md), and this track will not re-teach them.
Elixir's own accent is the **pipe** `|>` (feed a value through a chain of
functions — function composition made linear) and the `&(&1)` capture shorthand:

```elixir
import Bitwise

# jc_reread_hash's djb2 -- equals jichi's C output, 210714636441
defmodule Djb2 do
  def hash(str) do
    str
    |> :binary.bin_to_list()
    |> Enum.reduce(5381, fn b, h -> band(h * 33 + b, 0xFFFFFFFFFFFFFFFF) end)
  end
end
IO.puts(Djb2.hash("hello"))                       # => 210714636441

# map/filter/sum via the pipe -- jichi's manual jc_vec loop, read left to right
[1, 2, 3, 4, 5]
|> Enum.map(&(&1 * &1))
|> Enum.filter(&(rem(&1, 2) == 0))
|> Enum.sum()
|> IO.puts()                                        # => 20

# pattern matching on tagged tuples -- an enum jc_ref_kind analog
describe = fn
  {:file, p} -> "inline file #{p}"
  {:url, p}  -> "fetch url #{p}"
  {:diff, _} -> "the working-tree diff"
end
IO.puts(describe.({:url, "x.com"}))                 # => fetch url x.com
```

## The BEAM's big idea: isolated processes and messages

Here is where Elixir stops looking like the other two. jichi's `spawn_parallel`
tool runs N subtasks in a **fork pool** (`src/tools/jc_parallel.c`,
[`docs/PARALLEL.md`](PARALLEL.md)) sized to `jc_cpu_count()` and capped at
`min(cpu, 8)`: real **OS processes** — heavyweight (a `fork` each), isolated by
the kernel and a copy-on-write arena, few enough to count on one hand. The BEAM
does the same *shape* with **green processes**: microseconds to spawn, a couple
of kilobytes each, hundreds of thousands live at once, isolated by the VM with
**no shared memory** and a private heap per process.

```elixir
# spawn an isolated worker, hand it work, receive its reply --
# the BEAM's in-VM version of jichi forking a child and reading its pipe.
parent = self()
worker = spawn(fn ->
  receive do
    {:work, x, from} -> send(from, {:done, x * x})
  end
end)
send(worker, {:work, 7, parent})
receive do
  {:done, r} -> IO.puts("worker returned #{r}")     # => worker returned 49
end
```

The safety property is the same on both sides, reached two ways: jichi's
children can't corrupt each other because `fork` gives each a **copy-on-write**
snapshot; BEAM processes can't because data is **immutable** and each heap is
separate. "No shared mutable state" is the rule that makes both safe — jichi
enforces it with the OS, Elixir with the language.

## Message passing — jichi does it over a pipe; the BEAM does it in the VM

Look at what jichi's forked children actually *do*: they stream
**newline-framed JSON** back to the parent over a Unix pipe —
`{"t":"tool",…}` / `{"t":"tok",…}` during the run, then a final
`{"t":"done",answer,tokens}` — which the parent parses with
`jc_parallel_parse_msg` while it waits in `select`. That **is** message passing:
a mailbox, `send`, and `receive`, hand-built out of a pipe and a line protocol.
The BEAM gives every process a mailbox and `send`/`receive` natively — messages
are Erlang *terms*, not text to serialize and re-parse. jichi wrote the
mechanism; the BEAM *is* the mechanism.

## "Let it crash", and supervision — jichi's watchdog is a supervisor by hand

This is the deepest contrast. jichi's pool does not trust its children to
behave. A **per-child watchdog** in `run_pool` kills and reaps any task past
`parallel_task_timeout` (default 300 s); teardown SIGTERMs then **escalates to
SIGKILL** after a grace window (`reap_grace`) so a child trapping SIGTERM can't
deadlock the parent; and only cleanly-finished results are merged. One layer up,
the autonomous-loop **supervisor** ([`docs/AUTONOMOUS_LOOPS.md`](AUTONOMOUS_LOOPS.md))
restarts and re-queues tasks, and the autonomy envelope rolls a red run back to
its last green checkpoint.

Read that list again as an Elixir programmer and it is a **supervision tree**
plus **"let it crash"**: isolate failure to one worker, kill the wedged one,
restart or recover, keep the system up. Elixir's **OTP** gives you that as a
library — `Supervisor` with restart strategies (`one_for_one`, `rest_for_one`),
`GenServer` for a stateful worker, links and monitors for propagation. jichi
builds in C, minimally and with zero dependencies, what OTP provides as a
framework. That is the honest way to see jichi's concurrency code: **a small,
specific, dependency-free OTP** for one bounded job.

## Immutability is why any of it is safe

The thread tying the paradigm to the concurrency: the actor model needs **no
locks** precisely *because* data is immutable — a message can be shared or
copied but never mutated under another process's feet. jichi reaches the same
guarantee from the other direction — `fork`'s copy-on-write isolates each
child's memory, and the house rule that errors are *values* (`jc_status`, never
a shared errno-style global) means there is no cross-worker state to race on.
Immutability is the common root of both jichi's fork isolation and the BEAM's
process isolation; the concurrency safety is a *consequence* of the functional
discipline, not a separate feature.

## Where the BEAM pays, and where it doesn't

Read against [Fukabori 12](reading/fukabori-12-the-migration-road.md)'s
question — *which requirements are load-bearing.*

- **It pays** for massively concurrent, fault-tolerant, long-running,
  distributed services: Phoenix web apps, WhatsApp, Discord's real-time
  infrastructure, the telecom switches Erlang was born for. The whole VM exists
  so a system *keeps running while parts of it fail* — nine-nines uptime as an
  architecture, not a hope.
- **It costs** a whole virtual machine: a scheduler, per-process GC, a runtime
  measured in hundreds of megabytes. jichi is C89 for **bounded, short-lived,
  low-resource** work (`docs/LOW_MEMORY.md`: ~9 MB RSS, a ~1 MB binary,
  comfortable on a Pi Zero 2). Its concurrency is a fork pool that lives for
  *one turn* and exits — `fork` + a pipe is exactly right-sized for that, the
  same way the BEAM is exactly right-sized for a million-connection server.
  Neither is the other's tool.

## Prove it to yourself

Two rungs, and the second is the one that makes jichi's C click:

1. **Pure Elixir** (runs wherever `elixir` is installed): the `djb2` block above
   prints `210714636441`, exactly `jc_reread_hash("hello")` — verified on
   Erlang/OTP 25. Then try `jc_compact_estimate_tokens` (byte/4) or `jc_rrf_fuse`.
2. **The actor rung:** run the spawn/`send`/`receive` worker above, then open
   `src/tools/jc_parallel.c` and read the `fork` → pipe → `select` loop and
   `jc_parallel_parse_msg`. When you can point at jichi's `{"t":"done"}` line and
   Elixir's `receive do {:done, r}` and see they are the *same idea* — an
   isolated worker mailing a result home — the actor model has stopped being a
   framework feature and become a shape you recognize.

## Where to go next

- **Haskell** — [HASKELL_PARADIGM.md](HASKELL_PARADIGM.md): purity plus a type
  system that turns "errors as values" into a *checked* law, plus laziness — the
  last big paradigm shift in this family.
- **Clojure** — [CLOJURE_PARADIGM.md](CLOJURE_PARADIGM.md): a Lisp on the JVM —
  the Lisp shape you know, persistent immutable data structures, and *managed
  references* (atoms, STM refs, agents) — a third concurrency model beside these
  actors and jichi's fork isolation.

And the observation this track earns: **Elixir and the BEAM are the family
member whose core idea — isolated processes plus supervision — jichi already
*implements* in C, minimally and without the VM.** Reading it teaches you both
what OTP hands you for free and what that framework costs, which is the whole
point of learning a paradigm from inside a program that chose a different one.

## Then do it — the graded course

Reading shows you the paradigm; a graded course makes you *live* in it. Elixir's
now exists: **[the graded Elixir functional course](assignments/INDEX.md#functional-track--elixir-graded)**
(tasks 39–42, `elixir` + ExUnit, two-sided like every task in the curriculum) is
the *doing* half of this track — the fix-forward loop, tests as proof, and one
lesson only Elixir teaches this way: with no mutable variable to remove, the
loops-to-folds refactor is *manual recursion → an `Enum` pipe*. It joins the
[Racket](assignments/INDEX.md#functional-track--racket-graded) and
[Guile](assignments/INDEX.md#functional-track--guile-graded) graded courses; the
whole functional family is now graded (Racket, Guile,
Elixir, Haskell, Clojure).
