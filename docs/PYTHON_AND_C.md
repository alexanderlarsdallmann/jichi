# Python and C — where the dynamic language meets the systems language (and why jichi left it)

*A curriculum **extra** (the map is [CURRICULUM.md](CURRICULUM.md)), and
deliberately **not** a Python course. The net has those in abundance; the whole
thesis of this curriculum is that the world needs better **systems and craft**
courses, not another tutorial. Python appears here for exactly two reasons, both
of which serve the C-first thesis rather than dilute it: the reference Python
**is a C program**, so Python's real power lives at the **C boundary** — a
systems topic; and jichi's **own history with Python** — its tests were Python,
then deliberately left — is a "which requirements are load-bearing" lesson told
in this repository's actual commits. Read Python here as a **mirror** for C and
for engineering judgment, not as a destination.*

## Not a Python course — a C lesson wearing Python

Say it plainly so no one mistakes the intent: this document will not teach you
Python, and the curriculum will not add a graded Python or Ruby course. C stays
the exercise language (the decision is in
[`proposals/2026-07-curriculum.md`](proposals/2026-07-curriculum.md) §4), because
the shortage is *systems programmers and software craftsmen*, not people who
have seen a Python tutorial. What Python offers this course is a **contrast that
sharpens the C**, twice over — the interpreter's insides, and this project's own
decision to walk away from it.

## CPython is C

The reference Python — the one almost everyone runs — is a **C program**
(`python/cpython`). Every Python value you touch is a `PyObject *`: a C struct
carrying a **reference count** and a pointer to its type. The Global Interpreter
Lock, the performance ceiling, the entire extension story — all C facts about a
C program. So the moment you ask *how does Python actually work*, you are reading
C.

Set it beside jichi and you get a clean design-space contrast in "who frees
this?":

- **jichi** answers with **arenas** — bulk lifetime, freed by scope
  (session / turn / tool-call), no per-object bookkeeping.
- **CPython** answers with **reference counting** plus a cyclic garbage
  collector — per-object bookkeeping, freed the instant the last reference
  drops.

Neither is "the" answer; they are two points a systems programmer should be able
to place. A learner who worked the arena discipline (`docs/analysis/2026-07-29-tool-arena.md`)
already has the frame to read CPython's choice critically.

## The C boundary — Python extends *with* C (the inverse of Guile)

The [Guile track](GUILE_PARADIGM.md) showed C **hosting** a language — a C
program boots a Scheme world inside itself. Python is the **inverse**: C
**accelerates** Python. You extend Python by writing C against the C-API
(`PyObject`, `PyArg_ParseTuple`, and the `Py_INCREF`/`Py_DECREF` refcount
discipline you must get *exactly* right or you leak or crash), and you call
existing C from Python with `ctypes`/`cffi`. The fast core of NumPy, pandas, and
PyTorch is C or C++; the Python is glue over it.

The lesson lands hard from jichi's chair: **dynamic languages reach for C
exactly where jichi already lives.** The instant performance or a real system
interface matters, Python is standing at the C boundary — and on the other side
of that boundary is the language this curriculum teaches. You can see it in one
minute, using jichi's own pure C from Python:

```sh
# compile jichi's pure jc_reread_hash (the djb2 from every functional track)
cc -shared -fPIC -Iinclude src/util/jc_reread.c -o /tmp/libreread.so
```
```python
import ctypes
lib = ctypes.CDLL("/tmp/libreread.so")
lib.jc_reread_hash.restype  = ctypes.c_ulong
lib.jc_reread_hash.argtypes = [ctypes.c_char_p, ctypes.c_ulong]
print(lib.jc_reread_hash(b"hello", 5))   # => 210714636441   (verified)
```

`210714636441` — the *same* number the C, Racket, Guile, Elixir, Haskell, and
Clojure versions all produced, now reached by Python calling straight into
jichi's C. And note what you meet at that boundary: `argtypes`/`restype` are you
telling Python the C **types** it forgot it needed, and a real extension would
face the `Py_INCREF`/`Py_DECREF` version of the very "who frees this?" question
jichi answers with arenas. The dynamic language does not escape C's questions; it
delegates them, and at the seam you answer them again.

## Python's own story in this repository

Now the requirements lesson — told not in the abstract but in jichi's commit
history. jichi's end-to-end tests **were Python** (`tests/e2e/*.py`). At
milestones **M209–M217** the whole portable suite was ported to a
**python-free smoke tier** — POSIX-sh drivers backed by four small C89 helpers
(`mockmodel`/`ptydrive`/`jsonq`/`sockq`) — because `make check-target` had to
gate a build on machines with **no `python3` at all**: the small, embedded, and
old systems `docs/LOW_MEMORY.md` targets. Python was the **right** tool for
writing tests fast, and the **wrong** tool for the portability requirement. The
plan and the decisions are in
[`plans/2026-07-python-free-testing.md`](plans/2026-07-python-free-testing.md);
the rule that fell out is "one driver, one tier — `python3` optional-recommended."

But Python did **not** leave entirely, and that is the sharper half of the
lesson. Around **16 files** remain under `tests/` where Python is still the right
tool — the local-model **bench** (needs a compiler and rich orchestration), the
measurement harness, a VT-emulator, the example products. The residual is
*deliberate*: keep Python exactly where its velocity pays and nothing portable
depends on it; move everything a build must gate on to `sh` + C89. That is
[Fukabori 12](reading/fukabori-12-the-migration-road.md)'s
"which requirements are load-bearing" as an **actual migration in this repo you
can read commit by commit** — not a claim, a receipt.

## When Python is the right tool, and when it isn't

| Reach for Python when… | Reach for C (jichi's tier) when… |
|---|---|
| developer velocity outweighs runtime cost | the target has no interpreter to ship |
| glue, scripting, data, ML, prototyping | memory must be tiny and predictable (~9 MB, a Pi Zero 2) |
| orchestration (jichi's **bench** is Python) | zero runtime dependencies is a requirement |
| the code will run on a workstation | portability across small/old toolchains is the point |

Sticking to C for jichi is **not** a rejection of Python — it is a requirement
met. And that is why this belongs in a systems curriculum: reading Python here
teaches you to **see the boundary**, so you reach for the right tool instead of
the familiar one. The opposite of language tribalism is knowing precisely why
you chose what you chose — the whole craft, in one habit.

## Prove it to yourself

1. **Python calls C** (above): compile `jc_reread_hash` to a shared object and
   call it from Python via `ctypes`; get `210714636441`, the djb2 of every
   track, across the C boundary. Then look at what `argtypes`/`restype` are
   *for*.
2. **Read the migration:** open a `tests/smoke/*.sh` driver and, where the plan
   lists its retired `tests/e2e/*.py` origin, read the same test expressed in
   Python and in POSIX-sh + C89. Judge the trade for yourself — velocity vs. a
   build that gates with no `python3`.

## Where this sits in the curriculum

Not a language-family member; not graded. A curriculum **extra**, like
[C_STANDARDS.md](C_STANDARDS.md) and
[READING_OPEN_SOURCE.md](READING_OPEN_SOURCE.md) — it teaches a **C boundary**
and **engineering judgment**, using Python as the mirror. The families stay what
they are: **systems** (C, C++, Zig, Rust) and **functional** (Racket, Guile,
Elixir, Haskell, Clojure). Python's place is *here*, as the lens that shows why.
The same holds for **Ruby** — MRI is a C program too; if it ever appears, it
appears the same way, a mirror and not a member. jichi stays C89 by choice, and
now you have read that choice defended in a language built to make the opposite
one.
