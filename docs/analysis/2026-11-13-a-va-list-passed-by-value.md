# A `va_list` passed by value: a fix that was right on one ABI

*2026-11-13. Milestone M1172. Written for self-learners, junior developers, and
agents. It tells one defect from symptom to fix, including the day it was
misread.*

## In three sentences

jichi's C89 `snprintf` fallback handed its argument list to a helper function,
and the helper read from it. The C standard says the caller's list is then
*indeterminate*. x86-64 happens to make it work, and most other ABIs do not.
Every gate ran on x86-64, so every gate was green from 2026-10-29 to 2026-11-13.
A board test reported the defect on the first board runs after it, and the
report was then filed under the wrong name for a day.

## 1. The rule in C

A variadic function such as `printf` reads its extra arguments through a
`va_list`: `va_start` opens the list, each `va_arg` takes the next argument,
and `va_end` closes it.

You may pass a `va_list` to another function. The standard says what happens
next (C89 §4.8, C99 §7.15 paragraph 3, paraphrased): if the called function
uses `va_arg` on it, the caller's `va_list` becomes **indeterminate**. The
caller may then only hand it to `va_end`.

So this pattern is not portable:

```c
static void skip_one(va_list ap) { (void)va_arg(ap, int); }   /* reads one */

static int second(int n, ...)
{
    va_list ap;
    int got;
    va_start(ap, n);
    skip_one(ap);              /* hopes this advanced ap ...              */
    got = va_arg(ap, int);     /* ... and reads the SECOND argument here  */
    va_end(ap);
    return got;
}
```

**No compiler warned about it.** gcc 13.3 at `-std=c89 -pedantic -Wall -Wextra
-Werror` and clang 18.1 at `-Weverything -Werror` both compile it silently.

## 2. Why it works on some machines

`va_list` is a different type on each ABI:

- **An array type** on x86-64 (System V), 32-bit PowerPC and s390x. An array
  parameter decays to a pointer, so `skip_one` receives a pointer to the
  caller's state. Its `va_arg` moves the caller's list. The code works by luck.
- **A struct** (aarch64, 32-bit ARM) **or a plain pointer** (i386, riscv64,
  powerpc64le, loongarch64, mips), passed by value. `skip_one` gets a COPY. Its `va_arg`
  moves the copy, and the caller's list stays where it was.

### Measured, 2026-11-13

The program in section 6, `second(2, 111, 222)`. A result of 222 means the
helper moved the caller's list, so the defect is hidden. A result of 111 means
the defect shows.

| ABI | how it ran | result |
|---|---|---|
| x86-64 | native, gcc 13.3 | **222** (hidden) |
| powerpc (32-bit) | zig cc + qemu-user | **222** (hidden) |
| s390x | zig cc + qemu-user | **222** (hidden) |
| aarch64 | the Raspberry Pi 500, native gcc 12.2 | 111 |
| aarch64 | zig cc + qemu-user | 111 |
| riscv64 | zig cc + qemu-user | 111 |
| arm (32-bit, hard float) | zig cc + qemu-user | 111 |
| x86 (i386) | zig cc + qemu-user | 111 |
| powerpc64le | zig cc + qemu-user | 111 |
| loongarch64 | zig cc + qemu-user | 111 |
| mips, mipsel, mips64el | zig cc + qemu-user | 111 |

Three ABIs hide the defect and the other nine show it. Our development machine
and every gate machine are in the first group.

## 3. Where it was in jichi

`src/platform/jc_snprintf.c` holds a C89 fallback formatter. It is used only
where the C library has no `vsnprintf`, which in practice means very old
systems.

In October, a security review found that the fallback **printed** conversions
it does not support, such as `%n`, `%a` or an unknown letter, but did not
**consume** their argument. Every argument after it was then read one slot too
early, which could crash (M1079, finding A-10).

The fix added a helper to consume the right type for each class:

```c
static void consume_class(va_list ap, char conv, int flags)
{
    if (conv == 'n') {
        (void)va_arg(ap, int *);      /* %n's operand is a pointer */
    } else if (/* the integer classes */) {
        ...
    } else {
        (void)va_arg(ap, double);
    }
}
...
            consume_class(ap, conv, sp.flags);   /* the caller's list */
```

On x86-64 this was correct. On aarch64 and riscv64 it reintroduced the defect
the fix was written for: the helper consumed from its copy, and the next
conversion read the argument that should have been skipped.

## 4. How it showed up, and how it was misread

The smoke driver `tests/smoke/snprintf_fallback.sh` compiles the fallback on
purpose and compares it against libc. Its cell 15, "n nowrite resync", formats
`"%n%s"` with a pointer to an `int` holding 77, then the string `"tail"`. It
expects the output to end in `tail`.

**Green on x86-64. On every other board it was red,** with the same line:

    ... MISMATCH (jc r=3 out=[%nM] cnt=77; want r in [0,63], cnt==77, ends in tail)

**The output is the diagnosis.** `M` is character 77. The `%s` did not read
`"tail"`. It read the `%n` operand, the pointer to the `int` 77, as a string.
That is exactly an argument list that did not advance past `%n`.

**The misreading, told plainly.** The board rows of 2026-11-12 and 2026-11-13
(the Pi 500, a Pi Zero 2 W, a Pi 400, a VisionFive 2 and a LicheeRV Nano) all
failed cell 15, every one with the identical `out=[%nM]` line. The
supervisor, the Claude session that runs these drives, recorded the failure in
three successive ways:
- first as "an aarch64 item";
- then as "the %n case on non-x86-64 ABIs";
- then as a known issue for the 0.14.0 release.

Each record named the symptom. None of them read the `M`. The cause was found on
2026-11-13 in about fifteen minutes, by asking what the stray byte was.

## 5. Why no gate caught it

**The fallback is in no binary.** It is compiled only where `vsnprintf` is
missing, and no platform in jichi's matrix lacks it. So the unit suite, built
with the real `vsnprintf`, never ran the fallback.

**The smoke driver runs it, but only on real machines.** The smoke tier needs a
real system. The cheap emulated sweep (`scripts/tier-v-arch.sh`, 20 zig
targets, most under qemu-user) runs the unit suite only.

**Every gate machine is x86-64.** `make ci` runs on one; the review's own tests
ran on one. That is the one ABI family where the defect hides.

**The test itself was right.** It asked for "the conversion after `%n` stays
aligned", which is the property that breaks. It simply never ran where it could
fail, until the first board run.

## 6. Reproduce it yourself

You need a C compiler. For the other architectures you also need
[zig](https://ziglang.org/) and qemu-user with binfmt support (on Debian or
Ubuntu, the package `qemu-user-static`).

1. Save this as `va_demo.c`:

   ```c
   #include <stdio.h>
   #include <stdarg.h>

   static void skip_one(va_list ap)
   {
       (void)va_arg(ap, int);          /* the helper reads the FIRST argument */
   }

   static int second_after_helper(int count, ...)
   {
       va_list ap;
       int got;
       va_start(ap, count);
       skip_one(ap);                   /* the caller hopes this advanced ap */
       got = va_arg(ap, int);          /* the caller now asks for the SECOND */
       va_end(ap);
       return got;
   }

   int main(void)
   {
       int got = second_after_helper(2, 111, 222);
       printf("after the helper, the caller's next va_arg read %d: %s\n", got,
              got == 222 ? "222 = the helper advanced the caller's list (it MASKS the bug)"
                         : "111 = the caller's list did NOT move (the bug shows)");
       return 0;
   }
   ```

2. Build and run it natively:

   ```sh
   cc -std=c89 -pedantic -Wall -Wextra -Werror -o va_demo va_demo.c && ./va_demo
   ```

3. Build it for another ABI and run it under emulation:

   ```sh
   zig cc -target aarch64-linux-musl -std=c89 -pedantic -o va_demo-aarch64 va_demo.c && ./va_demo-aarch64
   ```

   Swap in `riscv64-linux-musl`, `x86-linux-musl`, `s390x-linux-musl` or
   `powerpc-linux-musleabihf` to fill in the table yourself.

**If step 3 fails** with "Exec format error", qemu-user's binfmt handlers are
not registered. Install `qemu-user-static`, or run the binary on a real board.

## 7. The fix

**The consumption now happens in the caller's own frame,** on the caller's own
list. The helper became a macro, `JC_CONSUME_CLASS`, which expands inside
`jc_vsnprintf`'s loop:

```c
#define JC_CONSUME_CLASS(ap, conv, flags)                    \
    do {                                                     \
        if ((conv) == 'n') {                                 \
            (void)va_arg(ap, int *);                         \
        } else if (/* the integer classes */) {              \
            ...                                              \
        } else {                                             \
            (void)va_arg(ap, double);                        \
        }                                                    \
    } while (0)
```

**Two alternatives were rejected, each for a reason a learner meets again:**

1. **Pass a pointer, `&ap`.** C99 explicitly allows passing a *pointer to* a
   `va_list`. But here `ap` is itself a function parameter. On x86-64 a `va_list`
   parameter has already decayed from array to pointer, so `&ap` has the wrong
   type. gcc 13.3 reports `passing argument 1 of 'skip_one' from incompatible
   pointer type`; zig's aarch64 build is silent. Code that compiles cleanly on
   only one ABI is the same trap again.
2. **`va_copy`.** It is C99. jichi is C89 to the letter.

## 8. The proof, red then green

**The board.** The smoke driver on the Raspberry Pi 500 (aarch64, gcc 12.2),
on two throwaway copies of the same tree:

| tree | cell 15 | result |
|---|---|---|
| unfixed | `not ok 15`, with `out=[%nM]` | 15 of 16 |
| fixed | `ok 15` | 16 of 16 |

On x86-64 both copies pass 16 of 16. The ABI hides it.

**The new unit test,** `tests/test_snprintf_fallback.c`. It compiles the
fallback into the unit suite under private names, so it runs wherever the suite
runs, including the emulated sweep.

| target | unfixed | fixed |
|---|---|---|
| x86-64, native | 0 failures | 0 failures |
| aarch64, qemu-user | 2 failures (cells 1 and 2) | 0 failures |
| i386, qemu-user | 3 failures (cells 1, 2 and 3) | 0 failures |
| riscv64, qemu-user | **the unit suite crashed (SIGSEGV)** | 0 failures |

Two details in that table are worth understanding.

**The riscv64 crash.** A ten-line program calling the unfixed fallback with
`"%a%s", 1.5, "tail"` crashed under qemu-riscv64 (exit 139). The fixed one
printed `%atail`. The `%s` had read the bits of the double 1.5 as a pointer.
That is the crash class A-10 was written to close.

**aarch64 survives cell 3.** aarch64 keeps separate positions in the argument
list for integer registers and floating-point registers. So consuming a
`double` from a copy does not disturb the caller's next pointer argument. Cell
3 is red on i386 and riscv64, where there is one list.

## 9. What keeps it from coming back

1. **The unit test** above. The sweep runs the unit suite on every target it
   can execute (18 on 2026-11-13), and most of them do not hide the defect.
2. **`portability_lint` check 40.** No function may call `va_arg` on a
   `va_list` it received as a parameter, except `jc_vsnprintf` itself. Its
   caller, `jc_snprintf`, calls `va_start`, then `jc_vsnprintf` once, then only
   `va_end`, which is the legal use.
   - It reads every C file under `src/`, `include/` and `tests/`.
   - It requires exactly the two `jc_vsnprintf` definitions it knows today. An
     extraction that reads nothing fails instead of passing.
   - It is red on the old tree, where it names `consume_class` at each of its
     `va_arg` lines, and green on the fixed tree.

## 10. Lessons

1. **Behaviour the standard leaves indeterminate can look correct on the machine
   you test on, and the machines that agree will not tell you.** It is not the
   first case in this project:
   - a drive snapshot's `gzip` argv lacked its NULL terminator, and x86-64's
     stack happened to hold a NULL there (M1165-b, found the same day on the
     Pi 500);
   - a test tool built LSP frames from uninitialised memory that x86-64 zeroed
     by luck (the Pi 500+ row, 2026-10-15);
   - `uname()`'s success value is non-negative, not zero, and only illumos
     returned a positive one (ANECDOTES #97).
2. **Read a wrong output as data.** `M` was 77. One minute with an ASCII table
   would have saved a day of calling it "the aarch64 item".
3. **A known-issue entry that names a symptom is a deferral, not a diagnosis.**
   Before writing "fails on platform X", write what the platform does
   differently.
4. **Code compiled only in a configuration no machine uses is never tested.**
   Compile it on purpose in the suite that runs everywhere, as the new unit test
   does.
5. **Put ABI-sensitive checks where the cheap emulated sweep reaches them.** The
   smoke tier needs real machines. The unit suite runs under qemu on most of
   the sweep's 20 targets in minutes.
6. **Prefer a lint to an audit.** Check 40 finds the next `va_list` handed to a
   helper, in any file, for as long as the lint exists.
7. **For agents:** when a check fails only on some machines, write the smallest
   program with no project code in it that shows the difference, and run it on
   one machine that passes and one that fails. Section 6 is that program. It
   turned "an aarch64 item" into a rule from the C standard in two runs.
