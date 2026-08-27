# Racket by contrast — functional programming for someone who reads jichi

*A curriculum extra, and the first of the **functional-programming family**
(the map is [CURRICULUM.md](CURRICULUM.md)). 温故知新（おんこちしん） — study
the old to know the new: this track uses the C you already understand —
jichi's own — as the ground truth for learning the functional paradigm, with
**Racket** as the first concrete language. Every Racket snippet below was run
on the reference box (Racket v8.10) and its output is quoted, not imagined.*

This is a **reading track**, not a graded course — deliberately, like
[RUST_INTEROP.md](RUST_INTEROP.md). The lesson is a *paradigm*, and a
per-language grader would test syntax, not the shift in thinking. A standalone
graded course in a functional language — the curriculum's sets A–D re-homed
with new fixtures and two-sided graders — is a separate, larger future step
this track precedes.

## Why a *paradigm* track, not a *migration* track

The systems-programming family migrates jichi's C seam by seam behind a stable
C header: [ZIG_INTEROP.md](ZIG_INTEROP.md), [CPP_INTEROP.md](CPP_INTEROP.md),
compile → extend → refactor, the linker as referee. **That model does not
transfer to functional languages.** Like Rust
([RUST_INTEROP.md](RUST_INTEROP.md), and the structural argument in
[Fukabori 12](reading/fukabori-12-the-migration-road.md)), their central
benefit spans the *whole program graph* and rides a runtime — Racket's CS
virtual machine, Elixir's BEAM, Haskell's GHC RTS, Clojure's JVM. You cannot
splice Racket into jichi's C at a header the way you splice in Zig. So the
functional family cannot be a migration; it teaches the paradigm **by
contrast** instead, and the contrast object is a C codebase you already know.

## Why Racket first

- **It was built to teach.** Racket descends from the Scheme of SICP and the
  *How to Design Programs* tradition — designed to be a first language *and* a
  language laboratory, with **DrRacket**, a REPL-and-editor where you can watch
  the whole paradigm work, one `apt install racket` away.
- **It is a Scheme.** Most of what you learn transfers almost verbatim to
  **Guile** (which also embeds in C — a different jichi-adjacent angle), and
  the Lisp shape generalizes to **Clojure**.
- **The punchline:** Racket makes *default* the discipline jichi enforces *by
  hand*. Purity, immutability, and errors-as-values are the house style of this
  C codebase; in Racket they are the language.

## The surprise: you have been writing functional C all along

jichi's architecture is **pure core, thin shell**: the logic lives in
deterministic functions with no hidden state that return their errors as
values, and I/O is quarantined at the edges. That *is* functional programming —
with the safety off. A concrete one, freshly shipped (M231):

```c
/* src/util/jc_reread.c -- pure: same bytes in, same number out, no state. */
unsigned long jc_reread_hash(const char *data, unsigned long len)
{
    unsigned long h = 5381u;         /* djb2 */
    unsigned long i;
    for (i = 0; i < len; i++)
        h = ((h << 5) + h) + (unsigned char)data[i];  /* h*33 + c */
    return h;
}
```

Nothing here reaches outside its arguments. Racket makes that the *default* —
values are immutable, functions are pure unless they visibly aren't — where C
makes it a discipline you can silently break with one stray global or one
mutated argument. Same idea; inverted defaults. (The last section rebuilds this
exact function in Racket and checks the numbers match.)

## Values that don't change — and "who frees this?"

jichi's `jc_sb` grows a buffer *in place*; the **three-arena model** (session /
per-turn / per-tool-call — see the lifetime rules in `CLAUDE.md` and
`docs/analysis/2026-07-29-tool-arena.md`) is a hand-rolled answer to *who owns
this memory and when does it die* — the question whose wrong answers cost
milestones M197–M199. Racket's values are **immutable** and its **garbage
collector** answers "who frees this" for you: that whole class of arena-lifetime
bug simply cannot occur, because there is nothing to free and nothing to alias.
The cost is not zero — it is a runtime and a GC — and the honest accounting is
two sections down.

## Functions are values — the vtable is a closure you built by hand

jichi passes *behavior* with function pointers: the provider vtable
(`struct jc_provider_vtable`, so the agent never branches on provider), and
callbacks like `jc_completer_fn`, `jc_suggest_fn`, and the streaming
`jc_stream_sink`. That is higher-order programming in C. The recurring C idiom
of a **context pointer next to a function pointer** (`ctx` + `fn`, threaded by
hand through MCP tools, user tools, and the dynamic-tool mechanism) is exactly
a **closure** — a function that carries some captured environment — assembled
manually. Racket has closures and first-class functions natively:

```racket
;; the vtable is a dispatch table; a closure captures its environment for free
(define (make-adder n) (lambda (x) (+ x n)))   ; captures n
(define add10 (make-adder 10))
(add10 5)                                       ; => 15
(map add10 '(1 2 3))                            ; => '(11 12 13)
```

## Recursion where C loops

jichi scans with `for` loops (`jc_chunk_ranges`, the repomap line scan,
`jc_cosine_topn`). Racket expresses many of those as **structural recursion**,
and it *guarantees* proper tail calls — a tail-recursive loop runs in constant
space, so recursion is not the stack-overflow risk C training makes you fear:

```racket
;; sum a list, tail-recursively -- constant space, like a C for-loop
(define (sum lst [acc 0])
  (if (null? lst) acc (sum (cdr lst) (+ acc (car lst)))))
(sum '(1 2 3 4 5))                              ; => 15
```

## Make illegal states unrepresentable — tagged unions become structs + match

jichi approximates **sum types** with a tag plus a union: `enum jc_ref_kind`
with a payload union (`include/jc_refs.h`), `jc_message`'s role plus its
optional content / tool-calls, `enum jc_patch_strategy`. The tag-and-`switch`
is a hand-built pattern match, and nothing stops you reading the wrong union arm
for the tag. Racket's `struct` + `match` make the cases first-class and the
match checkable:

```racket
(require racket/match)
(struct ref (kind payload) #:transparent)       ; cf. enum jc_ref_kind + union
(define (describe r)
  (match r
    [(ref 'file p) (format "inline file ~a" p)]
    [(ref 'url  p) (format "fetch url ~a" p)]
    [(ref 'diff _) "the working-tree diff"]
    [_             "unknown ref"]))
(describe (ref 'url "x.com"))                    ; => "fetch url x.com"
(describe (ref 'diff #f))                        ; => "the working-tree diff"
```

## Errors as values, not exceptions — where jichi is already there

This is the cleanest one-to-one. jichi's rule is stated in `CLAUDE.md`:
*fallible functions return `jc_status`; outputs via pointers; no exceptions* —
and *tool errors are returned as values (`is_error`), never as control flow.*
That **is** the functional error discipline. `enum jc_status`
(`JC_OK` / `JC_ERR_OOM` / `JC_ERR_IO` / …) is a result type carried by value;
Racket writes the same shape with multiple return values or an option-like
result, and a language like Haskell later turns it into a type-checked law. The
translation here is nearly mechanical, because the C already thinks
functionally about failure.

## Lists and transformation — map / filter / foldl

jichi loops over `jc_vec`s by hand. `jc_rrf_fuse`
(`src/index/jc_retrieve.c:65`) fuses two ranked candidate lists by Reciprocal
Rank Fusion; `jc_cosine_topn` ranks then trims. Those are **folds and maps**
wearing a `for` loop. The same RRF as a fold reads as what it *is*:

```racket
;; Reciprocal Rank Fusion: score each id by 1/(k+rank), summed across lists.
(define (rrf k . ranked-lists)
  (define scores (make-hash))
  (for ([lst ranked-lists])
    (for ([id lst] [rank (in-naturals)])
      (hash-update! scores id (lambda (s) (+ s (/ 1.0 (+ k rank 1)))) 0.0)))
  (map car (sort (hash->list scores) > #:key cdr)))
(rrf 60 '(a b c) '(b c a))                       ; => '(b a c)   (b ranks high in both)

;; and the everyday trio, replacing a manual accumulate loop:
(foldl + 0 (filter even? (map (lambda (x) (* x x)) '(1 2 3 4 5))))  ; => 20
```

## The REPL changes the loop

C is edit → compile → run. Racket — especially in **DrRacket** — is a live
**REPL**: evaluate an expression, see its value, reshape it, keep the state
you've built. jichi has REPL-*shaped* pieces (its TUI, the one-shot
`complete`/`fim` calls), but a language-level REPL changes how you *develop*,
not just how you run. Be honest about the trade the other way: jichi's
edit-compile-**test** loop is exactly why its CI is fast, offline, and
deterministic (9,659 assertions, zero network) — the REPL is a different
workflow, not a free upgrade.

## Where the paradigm pays, and where it doesn't

Read this against [Fukabori 12](reading/fukabori-12-the-migration-road.md)'s
question — *which requirements are load-bearing* — not against fashion.

- **It pays** for correctness-critical logic, transformation-heavy code,
  exploratory and teaching work: immutability plus purity delete whole bug
  classes. The M197–M199 lifetime bugs are *unreachable* in Racket — there is
  nothing to free and nothing to alias — and "errors as values" stops being a
  house rule you must remember and becomes how the language works.
- **It costs** a runtime. jichi is C89 **because** it targets constrained
  machines (`docs/LOW_MEMORY.md`: ~9 MB RSS, a ~1 MB binary, comfortable on a
  Raspberry Pi Zero 2's 512 MB). Racket CS carries a multi-tens-of-MB runtime
  and real GC pauses — the *opposite* trade. On the single-board tier
  (`docs/plans/2026-07-hardware-testing.md`) that is decisive; on a workstation
  writing a compiler it is nothing. Same lesson as the migration road: the
  language decision is only as good as the requirements that feed it.

## Prove it to yourself

Not a graded task — this is a reading track — but do it with your hands.
**Reimplement one of jichi's pure cores in Racket and check it against the C.**
The worked example is `jc_reread_hash` (djb2). jichi's C prints
`210714636441` for `"hello"`; so must your Racket:

```racket
#lang racket/base
(define (djb2 bs)                                ; == jc_reread_hash
  (for/fold ([h 5381]) ([b (in-bytes bs)])
    (bitwise-and (+ (* h 33) b) #xFFFFFFFFFFFFFFFF)))  ; mask to C's 64-bit unsigned long
(djb2 #"hello")                                  ; => 210714636441  (matches the C, verified)
```

The one wrinkle is the whole point of the last-but-one section in
[C_STANDARDS.md](C_STANDARDS.md): Racket integers are unbounded, so you mask to
64 bits to match C's `unsigned long`; C's *fixed-width, wrapping* integers are
a machine detail Racket abstracts away. When your Racket matches jichi's C on
the same input, the paradigm has stopped being abstract. Then try the byte/4
token estimate (`jc_compact_estimate_tokens`) or `jc_rrf_fuse`.

## Where to go next — the rest of the functional family

Racket is the doorway; each sibling is its own paradigm track when it lands:

- **Guile / Scheme** — [GUILE_PARADIGM.md](GUILE_PARADIGM.md): almost a direct
  transfer (same Lisp core), and the one family member whose runtime *embeds in
  C* — the seam jichi could host, and the honest story of why it doesn't.
- **Clojure** — [CLOJURE_PARADIGM.md](CLOJURE_PARADIGM.md): a Lisp on the JVM,
  persistent immutable data structures front and centre.
- **Elixir** — [ELIXIR_PARADIGM.md](ELIXIR_PARADIGM.md): functional on the
  **BEAM** — actors, supervision trees, and the concurrency story that jichi's
  fork-based parallel pool ([`docs/PARALLEL.md`](PARALLEL.md)) approximates with
  OS processes.
- **Haskell** — [HASKELL_PARADIGM.md](HASKELL_PARADIGM.md): purity plus a type
  system that makes "errors as values" a *checked* law, plus laziness — the
  guarantee jichi's test wall approximates.

The larger step beyond reading is a **standalone graded course** — and Racket's
now exists: **[the graded Racket functional course](assignments/INDEX.md#functional-track--racket-graded)**
(assignments 31–34, `raco test` + `rackunit`, two-sided like every task in the
curriculum) is the *doing* half of this track. Read here to see the paradigm in
the C you know; go there to live in it. **Guile now has a graded course too**
([tasks 35–38](assignments/INDEX.md#functional-track--guile-graded), SRFI-64) —
the same four skills in Scheme's other dialect; the whole functional
family is now graded (Racket, Guile, Elixir, Haskell, Clojure).
