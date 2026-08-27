# Guile by contrast — Scheme, and the extension-language seam jichi didn't take

*The second member of the **functional-programming family** (the map is
[CURRICULUM.md](CURRICULUM.md); start with [RACKET_PARADIGM.md](RACKET_PARADIGM.md)).
Guile is a Scheme, so the paradigm lessons of the Racket track transfer almost
verbatim — this track does not repeat them. What is distinctive, and jichi-
specific, is that **Guile is GNU's official *extension language*: a Scheme built
to be embedded inside a C program** to make it scriptable. That is a seam jichi
could actually host in-process — and the honest, load-bearing lesson here is
**why jichi doesn't take it.** A reading track, no graders, like Racket and
Rust. Every snippet below was run on the reference box — the pure-Scheme ones
under GNU Guile 3.0.9, and the C-embedding one compiled against `libguile`
3.0.9 with the build line it shows — and their output is quoted, not imagined.*

## The paradigm transfers — see the Racket track

Guile is a Scheme, so everything [RACKET_PARADIGM.md](RACKET_PARADIGM.md) says
about jichi's pure-core C being functional programming with the safety off
holds unchanged: immutable values, first-class functions and closures, recursion
with guaranteed tail calls, sum-types-as-`match`, and errors that are values.
Here is the whole of it in Guile, so you can see the family resemblance — and
the small dialect differences from Racket — in one runnable block:

```scheme
(use-modules (srfi srfi-1) (ice-9 match))

;; jc_reread_hash's djb2 -- equals jichi's C output, 210714636441
(define (djb2 str)
  (fold (lambda (b h) (logand (+ (* h 33) b) #xFFFFFFFFFFFFFFFF))
        5381
        (map char->integer (string->list str))))
(format #t "~a\n" (djb2 "hello"))                    ; => 210714636441

;; closures, and map/filter/fold in place of a manual jc_vec loop
(define (make-adder n) (lambda (x) (+ x n)))
(format #t "~a\n" (map (make-adder 10) '(1 2 3)))    ; => (11 12 13)
(format #t "~a\n" (fold + 0 (filter even? (map (lambda (x) (* x x))
                                               '(1 2 3 4 5)))))  ; => 20

;; a tagged union (enum jc_ref_kind) as a match
(define (describe r)
  (match r (('file p) (format #f "inline file ~a" p))
           (('url  p) (format #f "fetch url ~a" p))
           (('diff _) "the working-tree diff")
           (_         "unknown ref")))
(format #t "~a\n" (describe '(url "x.com")))          ; => fetch url x.com
```

Note the dialect drift from Racket: `use-modules` for `require`, `logand` for
`bitwise-and`, `format #t "~a"` for `printf`-style output, `match` clauses in
plain parens. Same paradigm, cousin syntax — which is exactly why Racket was the
doorway and Guile is the easy second step.

## What Guile adds: a C program that *hosts* Scheme

This is the distinctive thing, and it inverts jichi's usual relationship with
the outside world. jichi normally reaches *out* — it shells out to tools, talks
MCP to a subprocess, forks a child. Guile's `libguile` is built for the
opposite: your C `main` **boots a Scheme world inside itself** and hands it C
functions to call. The C program becomes the host; a Scheme becomes the driver.

Concretely — expose jichi's pure `jc_reread_hash` to a live Scheme:

```c
#include <libguile.h>
#include <string.h>
#include <stdlib.h>
#include "jc_reread.h"                 /* jichi's pure core, unchanged */

/* Wrap the pure C function so Scheme can call it as (reread-hash "str"). */
static SCM scm_reread_hash(SCM s)
{
    char *str = scm_to_utf8_string(s);
    unsigned long h = jc_reread_hash(str, (unsigned long)strlen(str));
    free(str);
    return scm_from_ulong(h);
}

static void *inner(void *data)
{
    (void)data;
    scm_c_define_gsubr("reread-hash", 1, 0, 0, (scm_t_subr)scm_reread_hash);
    scm_c_eval_string("(display (reread-hash \"hello\")) (newline)");
    return NULL;                       /* prints 210714636441 */
}

int main(void)
{
    scm_with_guile(inner, NULL);       /* the C process hosts a Scheme world */
    return 0;
}
```

> **Verified on the reference box** (libguile 3.0.9). Build it with
> `cc embed.c src/util/jc_reread.c -Iinclude $(pkg-config --cflags --libs guile-3.0) -o embed`
> (install `guile-3.0-dev` on Debian, `guile-devel` on Fedora), run `./embed`,
> and it prints **`210714636441`** — a Scheme REPL calling jichi's own C `djb2`,
> the same number `jc_reread_hash("hello")` returns. It uses GNU Guile's
> documented C API: `scm_with_guile` / `scm_c_define_gsubr` / `scm_c_eval_string`
> / `scm_from_ulong` / `scm_to_utf8_string`.

That handful of lines is the whole idea: a pure C core, unchanged, now callable
from a real programming language running in the same process. It is what lets
LilyPond be scripted in Scheme, GnuCash be extended in Scheme, GNU Make grow a
Guile dialect — the C stays; the Scheme drives.

## Errors across the boundary

Embedding makes two error models meet, and they disagree. jichi's rule is
errors-as-values: `jc_status` returned, no exceptions, tool failures carried as
`is_error` (`CLAUDE.md`). Scheme's default is the opposite — it **throws**
(conditions/exceptions that unwind the stack). When a Scheme call can throw and
you invoke it from C, an uncaught exception unwinds *through your C frames*,
past every `jc_status` you would have returned. You must catch it at the
boundary with `scm_c_catch` (or `catch` in Scheme) and translate back into a
value your C code can branch on. That friction — a clean errors-as-values core
meeting a throwing language in-process — is a real cost of embedding, not a
detail, and it is one of the reasons the next section goes the way it does.

## Why jichi didn't embed a Scheme — the load-bearing section

jichi *could* embed Guile and become scriptable in a real language. It
deliberately doesn't, and the reasons are the same requirements that chose C89
in the first place:

- **Dependency + footprint.** jichi depends only on libcurl and vendors nothing
  (M171); it targets constrained machines (`docs/LOW_MEMORY.md`: ~9 MB RSS, a
  ~1 MB binary, comfortable on a Pi Zero 2's 512 MB). `libguile` is a
  multi-megabyte runtime carrying **its own garbage collector** — a second
  memory model beside the three arenas, and the opposite of that budget.
- **Portability.** The core builds as C89 on small, strange toolchains
  (`docs/plans/2026-07-hardware-testing.md`'s single-board tier). Linking
  `libguile` raises that floor everywhere the binary has to run.
- **Security.** jichi runs unsupervised (`--auto`, the autonomy envelope). A
  full Scheme interpreter in-process is a large attack surface that is hard to
  sandbox; jichi's design keeps untrusted logic **out of process** on purpose.
- **What jichi does instead.** Extension lives at arm's length: **user-defined
  tools** (shell out — `docs/USER_TOOLS.md`), **MCP** (out-of-process JSON-RPC),
  config, and lifecycle hooks. The trade is explicit — out-of-process is heavier
  per call (a `fork`/`exec`, a protocol round-trip) but keeps the core tiny,
  keeps it **language-agnostic** (script a tool in *any* language, not only
  Scheme), and keeps it sandboxable.

That is the payoff of studying Guile from jichi's chair: you learn the embedding
paradigm *and* you watch a careful C project decline it for reasons it can name.
It is [Fukabori 12](reading/fukabori-12-the-migration-road.md)'s question again —
*which requirements are load-bearing* — applied to "should this program host a
language?"

## Where Guile IS the right call

Flip every one of those constraints and embedding becomes the obvious move. The
enabling condition is: **in-process scripting is the point, and the footprint
budget allows it.**

- **LilyPond** — which jichi's own music pack drives (`docs/MUSIC.md`) — embeds
  Scheme as its extension language; every non-trivial engraving tweak is Scheme
  reaching into the C++ core.
- **GnuCash** exposes its ledger to Scheme; **GNU Make** grew a Guile dialect;
  **TeXmacs** scripts its editor in Scheme.
- Any C/C++ application whose power users should be able to *program* it in a
  real language, in-process, where a tens-of-MB runtime is a rounding error
  against the app's own size.

jichi is simply the other kind of program: small, unsupervised, and
cross-compiled onto boards where that runtime would dominate. Same paradigm,
opposite verdict — because the requirements differ.

## Prove it to yourself

Two rungs, and the second is the one no other family member offers:

1. **Pure Guile** (runs wherever `guile` is installed): reimplement a jichi pure
   core in Scheme and match the C. The `djb2` block at the top prints
   `210714636441`, exactly `jc_reread_hash("hello")` — verified on Guile 3.0.9.
   Then try `jc_compact_estimate_tokens` (byte/4) or `jc_rrf_fuse`.
2. **The embedding rung** (needs `guile-3.0-dev`): build the C-hosts-Scheme
   program above and call `(reread-hash "hello")` from Scheme; watch a jichi C
   function answer a Scheme REPL. That is the seam this track exists to show —
   a C core and a functional language sharing one process.

## Where to go next

- **Clojure** — [CLOJURE_PARADIGM.md](CLOJURE_PARADIGM.md): a Lisp on the JVM —
  the Lisp shape you now know, persistent immutable data structures, and a whole
  managed VM underneath (the hosted bet, jichi's opposite).
- **Elixir** — [ELIXIR_PARADIGM.md](ELIXIR_PARADIGM.md): functional on the
  **BEAM** — actors and supervision trees, the concurrency story jichi's
  fork-based pool (`docs/PARALLEL.md`) approximates with OS processes.
- **Haskell** — [HASKELL_PARADIGM.md](HASKELL_PARADIGM.md): purity plus a type
  system that turns "errors as values" into a *checked* law, plus laziness.

And the observation this track earns: **Guile is the one functional-family
member whose runtime was designed to live *inside* a C process.** The others are
whole separate runtimes you talk to across a boundary — which is exactly the
boundary jichi already crosses with MCP and tools.

## Then do it — the graded course

Reading shows you the paradigm; a graded course makes you *live* in it. Guile's
now exists: **[the graded Guile functional course](assignments/INDEX.md#functional-track--guile-graded)**
(tasks 35–38, `guile` + SRFI-64, two-sided like every task in the curriculum) is
the *doing* half of this track — the fix-forward loop, tests as proof, and
refactoring `set!` into a fold, all in Scheme. It re-homes the
[graded Racket course](assignments/INDEX.md#functional-track--racket-graded); the
whole functional family is now graded
(Racket, Guile, Elixir, Haskell, Clojure).
