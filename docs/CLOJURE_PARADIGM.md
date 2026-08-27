# Clojure by contrast — a Lisp on a giant, and immutability without the copy

*The fifth and (for now) final member of the **functional-programming family**
(the map is [CURRICULUM.md](CURRICULUM.md); start with
[RACKET_PARADIGM.md](RACKET_PARADIGM.md)). Clojure is a Lisp, so the paradigm
lessons of the Racket and Guile tracks transfer almost verbatim — this track
spends its length on what is distinctive: **persistent immutable data
structures** (immutability without the copy cost), the **hosted philosophy** (a
Lisp built *on* the JVM — the deliberate opposite of jichi's depend-on-nothing
stance), and a **third concurrency model** (managed references) to set beside
jichi's fork isolation and Elixir's actors. A reading track, no graders. Every
snippet was run on the reference box (Clojure on the JVM) and its output
quoted.*

## The paradigm transfers — see the Racket and Guile tracks

Immutable data, first-class functions, recursion, s-expressions: Clojure is a
Lisp, so [RACKET_PARADIGM.md](RACKET_PARADIGM.md) and
[GUILE_PARADIGM.md](GUILE_PARADIGM.md) already taught the shape. Clojure's accent
is **data-first** — keywords (`:kind`), vectors `[1 2 3]`, and maps `{:a 1}` are
literal, everyday values, and code is written to transform them:

```clojure
;; jc_reread_hash's djb2 -- equals jichi's C output for "hello"
(defn djb2 [s] (reduce (fn [h c] (+ (* h 33) (int c))) 5381 s))
(println (djb2 "hello"))                 ; => 210714636441

;; first-class functions and a tag-dispatch (enum jc_ref_kind analog)
(println (map (partial * 2) [1 2 3]))    ; => (2 4 6)
(defn describe [r]
  (case (:kind r)
    :file (str "inline file " (:payload r))
    :url  (str "fetch url "   (:payload r))
    :diff "the working-tree diff"))
(println (describe {:kind :url :payload "x.com"}))   ; => fetch url x.com
```

A per-language footnote worth collecting: that `djb2` needs no bit-mask because
`"hello"` doesn't overflow a 64-bit `long` — but Clojure's default arithmetic
*throws* on overflow (you'd reach for `unchecked-*` to get C's silent wrap).
Every language in this family met C's fixed-width wrapping `unsigned long`
differently — Racket and Guile mask a bignum, Haskell's `Word64` wraps by type,
Clojure's `long` throws unless you opt into wrapping. The machine detail C hides
in plain sight, each language makes a visible choice about.

## Persistent data — immutability without the copy

This is Clojure's signature, and it answers a cost jichi pays by hand. When
jichi forks a session, `jc_session_fork` **deep-copies** the entire history —
every message, tool call, and image — because that is how you get an independent
copy in C: O(n) bytes moved. Clojure's collections are **persistent**: a
"modified" version *shares structure* with the original and leaves it untouched,
in O(log n), no bulk copy:

```clojure
(def m  {:kind :url})
(def m2 (assoc m :payload "x.com"))       ; a NEW map...
(println m)                               ; => {:kind :url}          (original intact)
(println m2)                              ; => {:kind :url, :payload x.com}
```

`m` is unchanged; `m2` is a new value that reuses most of `m`'s internal tree.
You get immutability's safety — no aliasing bug, no "who else holds this?" — without
the deep-copy jichi must perform, and without the arena bookkeeping jichi needs
because there is nothing to free until the GC decides. That is the same
trade the whole family names: the guarantee is cheap here because a runtime and a
GC are paying for it.

## The hosted philosophy — the exact opposite of jichi's stance

Here is where Clojure's *design* contrasts with jichi's most sharply, and most
usefully. Clojure is a Lisp **on the JVM** — by design, not accident. It embraces
its host: any Java library is a Clojure library, the JVM's JIT and GC and threads
are Clojure's, the whole ecosystem is reused rather than rebuilt. Its philosophy
is *stand on a giant and inherit everything it offers*.

jichi's philosophy is the deliberate inverse. It **depends only on libcurl**,
**vendors nothing** (its cJSON is original code, M171), builds as **C89** on
small strange toolchains, and targets machines where a JVM would not fit
(`docs/LOW_MEMORY.md`: ~9 MB RSS, a ~1 MB binary, a Pi Zero 2). Its philosophy is
*depend on nothing and build the minimum*.

Neither is wrong; they answer different requirements
([Fukabori 12](reading/fukabori-12-the-migration-road.md)). Clojure's bet is that
the JVM's size buys more than it costs — libraries, performance, operational
maturity — for the long-running services it targets. jichi's bet is that on its
targets the platform's size *is* the cost. Reading Clojure teaches you the
hosted bet directly, and reading it *from jichi's chair* makes both bets legible
side by side.

## A third concurrency model: managed references

Two tracks have now shown a concurrency model. jichi uses **process isolation**
(`fork`, no shared memory — [PARALLEL.md](PARALLEL.md)); Elixir uses **actors**
(message passing, no shared state — [ELIXIR_PARADIGM.md](ELIXIR_PARADIGM.md)).
Clojure offers a *third*: **managed references** over immutable values — you
*may* share mutable state, but every change is coordinated.

```clojure
(def counter (atom 0))                    ; a managed reference
(swap! counter + 5)                       ; a coordinated compare-and-set
(println @counter)                        ; => 5
```

An `atom`'s update is a compare-and-set; `ref`s coordinate several changes in a
transaction (`dosync`, software transactional memory); `agent`s apply changes
asynchronously. All three are safe for the same reason the actor model is —
**the values inside are immutable**, so a change is a swap of one immutable
snapshot for another, never a mutation someone else can observe half-done. Three
answers to "how do you change shared state safely": don't share it (jichi),
don't share state at all and pass messages (Elixir), or share a *reference* and
discipline the swap (Clojure). Immutability is the root of all three.

## Where Clojure pays, and where it doesn't

- **It pays** where the JVM is an asset, not a tax: long-running data-heavy
  backends, systems that reuse the Java ecosystem, and REPL-driven interactive
  development (Clojure's live-reload workflow is one of its joys). Its persistent
  data structures make immutable-by-default *practical* at scale.
- **It costs** JVM startup (seconds, not milliseconds — you felt it running
  these snippets) and a runtime measured in hundreds of megabytes. jichi is C89
  for **instant, tiny, low-resource, bounded** runs — the opposite tier — so it
  takes the depend-on-nothing road.

## Prove it to yourself

1. **Pure Clojure** (`clojure prove.clj`, or `clojure -M prove.clj` with the
   deps CLI): the `djb2` above prints `210714636441`, exactly
   `jc_reread_hash("hello")` — verified. Then try `jc_compact_estimate_tokens`
   (byte/4) or `jc_rrf_fuse`.
2. **The persistence rung:** `assoc` onto a map, print the original, and watch it
   stay unchanged — the immutability jichi enforces with `fork`'s copy-on-write
   and hand-written deep copies, here a property of the data structure itself.
   Then reach `src/session/jc_session.c`'s `jc_session_fork` and see the O(n)
   deep copy Clojure's structural sharing replaces.

## Where to go next — the family, complete

This is the last of the five reading tracks. Set them side by side and the point
of the whole family comes into focus — five angles on functional programming,
each read against jichi's own C:

- **[Racket](RACKET_PARADIGM.md)** — the paradigm itself: jichi's pure-core /
  thin-shell architecture *is* functional programming with the safety off.
- **[Guile](GUILE_PARADIGM.md)** — embedding a Scheme *in* C, and the honest
  story of why jichi keeps extension out-of-process instead.
- **[Elixir](ELIXIR_PARADIGM.md)** — the actor model, which jichi's fork pool and
  its watchdog/supervisor already hand-roll in C.
- **[Haskell](HASKELL_PARADIGM.md)** — purity and errors-as-values as *compiler-
  checked laws*, the guarantee jichi's test-and-sanitizer wall approximates.
- **Clojure** (here) — persistent data (immutability without the copy), the
  hosted bet, and managed-reference concurrency.

Three concurrency models (process isolation, actors, managed references), two
roads to correctness (a type checker vs a verification wall), one machine detail
(`unsigned long`) surfaced five different ways — and, underneath all of it, one
idea: **immutability**, which jichi reaches by discipline and the OS, and these
languages reach by default. jichi stays C89 by choice, for reasons it can name;
a learner who has walked all five now understands *what* that discipline
approximates and *why* the constraints were chosen.

## Then do it — the graded course

Reading shows you the paradigm; a graded course makes you *live* in it. Clojure's
now exists: **[the graded Clojure functional course](assignments/INDEX.md#functional-track--clojure-graded)**
(tasks 47–50, `clojure` + clojure.test, two-sided like every task in the
curriculum) is the *doing* half of this track — the fix-forward loop, tests as
proof, a capstone fold over a stack, and a refactor that closes the whole family
back where it started: replacing an **atom** (Clojure's `set!`) with a
`reduce`/`->>` pipeline, the same mutation lesson the
[Racket](assignments/INDEX.md#functional-track--racket-graded) and
[Guile](assignments/INDEX.md#functional-track--guile-graded) courses open with.

With Clojure, **the functional family is complete** — all five reading tracks
([Racket](assignments/INDEX.md#functional-track--racket-graded),
[Guile](assignments/INDEX.md#functional-track--guile-graded),
[Elixir](assignments/INDEX.md#functional-track--elixir-graded),
[Haskell](assignments/INDEX.md#functional-track--haskell-graded), and Clojure)
now pair a reading track with a graded course, each two-sided and toolchain-gated
with a loud skip, each teaching the same four skills in a language that shows one
facet of the paradigm most clearly.
