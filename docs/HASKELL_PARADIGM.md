# Haskell by contrast — when the type checker enforces jichi's discipline

*The fourth member of the **functional-programming family** (the map is
[CURRICULUM.md](CURRICULUM.md); start with [RACKET_PARADIGM.md](RACKET_PARADIGM.md)).
The functional paradigm transfers, so this track spends its length on what is
distinctive: Haskell is **purely functional with a static type system**, and the
two disciplines jichi enforces by hand — *pure core / thin shell*, and *errors
as values* — Haskell makes into **compiler-checked laws**. A pure function
*cannot* secretly do I/O; an error you return *must* be handled. Plus laziness,
the one genuinely new evaluation model in this family. A reading track, no
graders. Every snippet was run on the reference box (GHC 9.4.7) and its output
quoted — including the compiler catching a bug the C equivalent would wave
through.*

## The paradigm transfers — see the Racket track

Immutability, first-class functions, pattern matching, recursion: same story as
[RACKET_PARADIGM.md](RACKET_PARADIGM.md), not repeated here. Haskell's accent is
that **everything has a type**, written above the definition, and function
application needs no parentheses (`f x`, not `f(x)`).

## Purity, promoted from discipline to type

jichi's architecture is *pure core, thin shell*: logic in deterministic
functions, I/O quarantined at the edges. In C that is a **convention** — upheld
by review and the arena lint, breakable by one stray global or one mutated
argument, because the compiler does not know or care. Haskell makes it a
**type**:

```haskell
import Data.Char (ord)
import Data.Word (Word64)

-- pure BY ITS TYPE: String -> Word64 cannot do I/O, cannot mutate, cannot
-- surprise you. (Word64 wraps mod 2^64 -- exactly C's unsigned long.)
djb2 :: String -> Word64                 -- == jc_reread_hash
djb2 = foldl (\h c -> h * 33 + fromIntegral (ord c)) 5381

main :: IO ()                            -- the thin shell: only IO-typed code does I/O
main = print (djb2 "hello")              -- => 210714636441
```

The `IO` in `main :: IO ()` is the whole point: the pure/impure boundary jichi
draws by hand and polices with a lint is drawn *by the compiler* and checked on
every build. A function without `IO` in its type is pure, guaranteed — not by
your care, but by the type checker's refusal to compile otherwise. And `Word64`
is a quiet lesson of its own: it *is* C's `unsigned long`, wrapping behaviour
and all, made an explicit, honest part of the type rather than a machine detail
you must remember (the theme of [C_STANDARDS.md](C_STANDARDS.md)'s
implementation-defined section).

## Errors as values — made a checked law

This is the deepest tie to jichi. jichi's first rule (`CLAUDE.md`) is that
**errors are values**: `jc_status` returned, tool failures carried as
`is_error`, never thrown as control flow. But in C that is enforced by
*convention* — nothing stops you ignoring a returned `jc_status`, and "forgot to
check the return" is a real bug class. Haskell makes the same rule a **law**:

```haskell
safeDiv :: Int -> Int -> Either String Int   -- Left = error, Right = ok
safeDiv _ 0 = Left "divide by zero"
safeDiv a b = Right (a `div` b)

-- you cannot get the Int out without confronting the error case:
report = case safeDiv 10 0 of
  Right n -> "ok " ++ show n
  Left  e -> "error: " ++ e                   -- => "error: divide by zero"
```

You cannot use the result of `safeDiv` without unwrapping the `Either`, and to
unwrap it you must write both branches. The discipline jichi asks you to *keep*,
Haskell's type checker *requires*.

## Make illegal states unrepresentable — and let the compiler check it

jichi approximates sum types with a tag plus a union (`enum jc_ref_kind` + a
payload union, `include/jc_refs.h`): the tag and the arm can disagree, and C
will happily let you read a `url` payload from a `diff`. Haskell binds each
constructor to its payload and checks that you handled every case:

```haskell
data Ref = File FilePath | Url String | Diff   -- payload tied to constructor
describeRef :: Ref -> String
describeRef (File p) = "inline file " ++ p
describeRef (Url u)  = "fetch url " ++ u
describeRef Diff     = "the working-tree diff"
```

Delete the `Diff` equation and compile with `-Wall`, and GHC says — verified,
not asserted:

```
warning: [-Wincomplete-patterns]
    Pattern match(es) are non-exhaustive
        Patterns of type ‘Ref’ not matched: Diff
```

That is the missing-`case`-label bug — the one a C `switch` compiles in silence —
caught at build time. The tagged-union pattern jichi builds by hand becomes safe
by construction.

## Laziness — the one genuinely new idea

None of Racket, Guile, or Elixir default to this; Haskell evaluates **on
demand**. An infinite list is an ordinary value, and only the part you use is
ever computed:

```haskell
take 5 (map (* 2) [0 ..])                -- => [0,2,4,6,8]   ([0..] is infinite)
```

It lets you separate *producing* data from *consuming* it, and write infinite
structures without looping forever. Be honest about the cost, and it lands
pointedly here: laziness is where Haskell's memory becomes **unpredictable** —
unevaluated thunks pile up, "space leaks" are a genre of bug. jichi spent a
whole wave (M197–M199, `docs/analysis/2026-07-29-tool-arena.md`) buying
*predictable* memory — arenas by lifetime, a footprint gauge, reachable-until-
exit is not "no leak." Laziness trades exactly that predictability for
expressiveness. Opposite priorities, honestly named.

## The synthesis: two roads to correct software

Step back, because this is the lesson the whole functional family was building
toward. **Haskell and jichi reach the same goal — programs that are correct —
by opposite means.**

- **Haskell** rejects wrong programs at **compile time**: purity, exhaustive
  sum types, errors-as-values are *types the checker enforces*. Make it not
  compile if it is wrong.
- **jichi** rejects wrong programs at **CI time**: the same disciplines — pure
  cores, errors-as-values, make-illegal-states-hard — upheld by a *wall of
  verification* instead of a type system. Two-sided graders shown red first,
  *prefer a lint to an audit*, ASan/UBSan + valgrind + fuzz, 9,639 offline
  assertions. C's type system is too weak to give what Haskell's gives (and its
  runtime is what jichi's targets can't afford), so jichi **earns** with
  discipline and tooling what Haskell gets **by construction**.

That is why studying Haskell from jichi's chair is worth it even though jichi
will never be written in it: it shows you the guarantee your test wall is
*approximating*, and names the trade — buy correctness with a type system, or
earn it with discipline plus verification. Knowing which your requirements can
afford (Fukabori 12 again) is the craft.

## Where Haskell pays, and where it doesn't

- **It pays** where compile-time correctness is worth a heavy runtime: compilers
  (GHC is written in Haskell; so are Elm and PureScript), financial systems,
  formal-methods-adjacent work — anywhere "if it type-checks, whole classes of
  bug are gone" is the goal.
- **It costs** the GHC runtime and its GC, laziness's unpredictable space, a
  steep learning curve, and slower iteration than a live REPL. jichi is C89 for
  predictable, tiny, deterministic-memory, low-resource runs
  (`docs/LOW_MEMORY.md`) — the opposite of laziness's trade — so it takes the
  other road to correctness.

## Prove it to yourself

1. **Pure Haskell** (`runghc Main.hs`, wherever `ghc` is installed): the `djb2`
   above prints `210714636441`, exactly `jc_reread_hash("hello")` — verified on
   GHC 9.4.7. Note `Word64` *is* C's `unsigned long`, wrapping and all.
2. **The type rung:** model `jc_status` as an `Either` or a sum type, write a
   function returning it, then **delete a case** from a pattern match and build
   with `-Wall`. Watch `-Wincomplete-patterns` catch what a C `switch`'s missing
   `case` would let through in silence. That is the compiler doing jichi's review
   and lint for you — the guarantee, seen directly.

## Where to go next

- **Clojure** — [CLOJURE_PARADIGM.md](CLOJURE_PARADIGM.md), the last reading
  track: a Lisp on the JVM — persistent immutable data structures, the hosted
  bet (jichi's opposite), and managed-reference concurrency.

Across Racket (the paradigm), Guile (embedding in C), Elixir (the actor model),
and Haskell (types), you have now seen functional programming from four angles,
each read against jichi's C.

## Then do it — the graded course

Reading shows you the paradigm; a graded course makes you *live* in it. Haskell's
now exists: **[the graded Haskell functional course](assignments/INDEX.md#functional-track--haskell-graded)**
(tasks 43–46, `runghc` + a base-only suite, two-sided like every task in the
curriculum) is the *doing* half of this track — the fix-forward loop, tests as
proof, a *manual-recursion → pipeline* refactor, and a capstone that leans on
exactly this track's ideas: a **`Token` sum type** whose pattern match the
compiler checks is total. It joins the graded
[Racket](assignments/INDEX.md#functional-track--racket-graded),
[Guile](assignments/INDEX.md#functional-track--guile-graded), and
[Elixir](assignments/INDEX.md#functional-track--elixir-graded) courses; and
[Clojure](CLOJURE_PARADIGM.md) completes the set -- the whole functional family
is now graded.
