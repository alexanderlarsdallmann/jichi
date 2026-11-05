# The architecture sweep — what it found

*Moved out of [`PLATFORMS.md`](../PLATFORMS.md) at M780 so the matrix stays short and the [install page](../install/) stays a lean walkthrough. This is the developer/porting record — the defects each row found and how they were fixed — kept verbatim; `git log -S` on any phrase names the commit that wrote it.*

## What the architecture sweep found, and what it refused to blame on jichi (M469)

**Two real defects, neither visible from a green gate on this workstation.**

1. **Undefined behaviour in the JSON number parser.** `src/json/cJSON.c` did
   `node->valueint = (int)val`, undefined when the double exceeds `int` — and jichi
   parses JSON it did not write. armeb *aborted* mid-suite on it (`zig cc` traps UB by
   default) and it then reproduced on x86-64 under `clang -fsanitize=undefined`.
   `make ci` runs UBSan and had never seen it: **nothing in the corpus had ever fed the
   parser a number outside `int` range.** The sanitizer was there; the input was not.
2. **The suite could not validate under excess floating-point precision.** Five checks
   compared doubles with `==`. On m68k the values are correct — `strtod("0.2")` gives
   `0.20000000000000001`, byte-for-byte what x86-64 gives — but the comparison runs in
   the 68881's 80-bit format (`sizeof(long double)` **12** vs 16), where the literal
   carries more bits than the double. C89 permits it; i386/x87 does the same.

**Three impostors, each separated from a finding by one probe.** Recorded because a row
that blames the program for its emulator's gap is worse than no row:

| target | symptom | probe | verdict |
|---|---|---|---|
| all six **MIPS** variants | 11,627 / **73**, identical BE and LE, 32 and 64 | `pipe()` fails on mips, works on powerpc | **`qemu-mips` cannot `pipe()`.** All 73 downstream; the seven failing files are exactly those that spawn a subprocess or read `/proc`. MIPS is the one Linux arch whose `pipe` returns both fds in registers |
| **armeb** (BE8) | doubles are garbage | the **literal** `0.2` prints `-2.35344e-185`, and its stored bytes `3f c9 99 99 99 99 99 9a` are correct big-endian IEEE-754 | **toolchain.** jichi cannot make a compile-time constant misprint. `aarch64_be` and `powerpc64` are big-endian and green |
| **mips64**/`mips64el` abi64 | "builds but NOT warning-free" | read it | **zig's own driver**: `ignoring '-fno-PIC' … [-Werror,-Woption-ignored]`. The `abin32` siblings build clean |
| `x86_64-linux-muslx32` | built, `Exec format error` | — | this kernel has no `CONFIG_X86_X32`; a rig result, not a jichi one |

Full table, the four failed approaches, and the four findings about the instrument:
[`analysis/2026-08-17-the-architecture-sweep.md`](../analysis/2026-08-17-the-architecture-sweep.md).
