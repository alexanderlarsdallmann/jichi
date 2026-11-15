# A C99 loop in a C89 tree: what it was, why the gate let it through, and how it was fixed

*Analysis, 2026-10-21, written for self-learners at the operator's request: "document the c99 loop fix
honestly, and comprehensively for self-learners, as well as why the extra block hasn't been used,
even though it was standard elsewhere." Written by the hardening drive's supervisor, a Claude Code
session that is itself part of the story (§6, §7). Every command below was run in the form shown, with
gcc 13.3.0 and clang 18.1.3 (Ubuntu 24.04). The counts in §5 were measured on the drive branch at
`993680a4`.*

## 1. In one paragraph

A fix for a memory-safety finding (M1016) added a fuzz target whose cleanup loop read
`for (jc_size i = 0; i < out.len; i++)`. That is C99. jichi's rule is C89, every file, zero warnings.
`make ci` passed anyway, the loop reached master with batch 5, and it stayed there for most of a day.
The fix moved the declaration to the top of the function, `jc_size i;`, and turned on `-Werror` for
the one gate stage that compiles the file. Below:
- the language rule;
- why the gate missed it;
- the fix and its alternatives;
- how often the tree uses each alternative;
- why this one was chosen;
- what the episode teaches.

## 2. The language rule

In C89 (ANSI X3.159-1989, the same language as ISO C90) a `for` statement is

    for ( expression(opt) ; expression(opt) ; expression(opt) ) statement

The first part is an *expression*, such as `i = 0`. It cannot be a *declaration*. Declarations are
allowed only at the start of a block, before its first statement. C99 changed both rules: a
declaration may now stand in the first part of a `for`, and anywhere in a block.

jichi's `CONTRIBUTING.md` states the C89 rule as house style: "**Declarations first:** every block
declares all its locals *before* any statement". The build enforces it with
`-std=c89 -pedantic -Wall -Wextra`, and the gate adds `-Werror`.

The two compilers disagree on how hard to enforce it. Here is a three-line function written the C99
way (`loop99.c`):

```c
#include <stddef.h>

int sum(const int *a, size_t n)
{
    int s = 0;
    for (size_t i = 0; i < n; i++)
        s += a[i];
    return s;
}
```

```
$ gcc -std=c89 -pedantic -c loop99.c
loop99.c:6:5: error: ‘for’ loop initial declarations are only allowed in C99 or C11 mode
  exit status: 1
$ clang -std=c89 -pedantic -c loop99.c
loop99.c:6:10: warning: variable declaration in for loop is a C99-specific feature [-Wc99-extensions]
loop99.c:6:10: warning: GCC does not allow variable declarations in for loop initializers before C99 [-Wgcc-compat]
  exit status: 0
$ clang -std=c89 -pedantic -Werror -c loop99.c
loop99.c:6:10: error: variable declaration in for loop is a C99-specific feature [-Werror,-Wc99-extensions]
  exit status: 1
```

gcc refuses the code. clang accepts it as an extension and warns, and only `-Werror` turns the warning
into a failure. **The same source, the same `-std=c89 -pedantic`, a different verdict.** Remember
that line: it is the whole reason the gate missed this.

## 3. Why the gate let it through

jichi's gate, `make ci`, builds the tree many times: gcc and clang at `-Werror`, a C++ build, sanitizers,
valgrind, and more. The gcc and clang `-Werror` stages build the **agent and the unit-test runner**.
The fuzz targets live in `tests/fuzz/` and are linked only into `run_fuzz`. Only one stage builds that
binary (`Makefile:1389` before the fix):

```
$(MAKE) clean && $(MAKE) SAN=1 CC=clang $(FUZZ_BIN) && \
```

It used clang, the compiler that only warns, and no `WERROR=1`. So the warning scrolled past in a long
log, the stage succeeded, and every gate was green:
- the follower's gate of the batch-5 head (`0fb9d222`, 14:04-14:37);
- the gate of the merge commit that went to master (`ff154a66`, 15:10-15:44).

gcc with the project's own flags would have refused the file at once:
`make tests/fuzz/jc_fuzz_targets.o` failed with `‘for’ loop initial declarations are only allowed in
C99 or C11 mode`. But no gate stage ever asked gcc to compile it.

**The lesson for anyone who keeps a gate:** a rule enforced by compiler flags holds only for the files
those flags compile. List every first-party source file and show that some `-Werror` build of the gate
compiles it. A file built only in a lenient stage is outside the rule, whatever the rule says.

## 4. The fix

The fix is commit `fd15771c`, made by a worker on the lead's task text.

```diff
@@ static void t_toolcall(const unsigned char *data, size_t len)
     struct jc_vec out;
     struct jc_sb reason;
     int n;
+    jc_size i;
 
     if (s == NULL) return;
@@
     /* Free the tool calls */
-    for (jc_size i = 0; i < out.len; i++) {
+    for (i = 0; i < out.len; i++) {
         struct jc_tool_call *tc = (struct jc_tool_call *)jc_vec_at(&out, i);
         free(tc->id);
```

What makes it correct:
- **The declaration is in the right place.** It sits with the function's other locals, before the
  first statement.
- **The `for` holds only expressions** (`i = 0`).
- **The type is unchanged.** `i` is still `jc_size`, the type of `out.len` (`include/jc_vec.h:21`),
  so the comparison mixes no signed and unsigned values.
- **The line inside the loop is valid C89 as it was.** `struct jc_tool_call *tc = ...` is a
  declaration at the start of the loop body's own block. In C89 an automatic scalar may be initialised
  with any expression, including a function call. Only aggregate initialisers (arrays, structs) must be
  constant expressions.
- **The behaviour is identical.** The compiled program does the same thing. Only the variable's
  *scope* changed: it is now visible in the whole function, not only in the loop.

The same commit changed the gate stage to `SAN=1 WERROR=1 CC=clang`, so clang's warning now fails the
build. The supervisor checked both directions on the pushed head:
- **With the fix:** the full stage builds with 0 diagnostics.
- **With the old file put back:** it stops at
  `jc_fuzz_targets.c:523:10: error: variable declaration in for loop is a C99-specific feature
  [-Werror,-Wc99-extensions]`.

One gap was found on the way. The worker's report says it did *not* build the full `run_fuzz` stage,
"due to linker configuration issues with sanitizer runtime libraries"; it compiled the object file
only. The supervisor's build of the whole stage is the evidence that the stage itself is green.

## 5. The alternatives, and how often the tree uses each

C89 gives three places to declare a loop counter. All three compile cleanly, with gcc and clang alike,
under `-std=c89 -pedantic -Wall -Wextra -Werror` (the demos `loop89top.c` and `loop89block.c` exit 0
under both compilers).

**A. At the top of the function**, with the other locals. This is what the fix does:

```c
int sum(const int *a, size_t n)
{
    int s = 0;
    size_t i;

    for (i = 0; i < n; i++)
        s += a[i];
    return s;
}
```

**B. At the top of a block that already exists**, such as an `if` or `else` branch or a loop body,
when the loop belongs to that branch only.

**C. In a block opened only for scope**, the "extra block":

```c
int sum(const int *a, size_t n)
{
    int s = 0;

    {
        size_t i;
        for (i = 0; i < n; i++)
            s += a[i];
    }
    return s;
}
```

Form C has two real uses. It keeps the counter's scope as small as C99's form would. And it lets a
declaration appear *after statements*, in the middle of a block, where C89 allows no declaration at
all.

**Measured on `993680a4`**, over the 362 `.c` files in `src/` and `tests/`. The method was a script
that finds each `for (x = ...)` and walks back to the declaration of `x`. It classifies by indentation
(4 spaces means the function top), and then by the line that opens the enclosing block. It is a
heuristic: it relies on the tree's consistent formatting, and 95 loops it could not place (mostly
parameters used as counters) are counted separately.

| Where the loop counter is declared | Loops | Share |
|---|---|---|
| A: top of the function | 891 | 66.7% |
| B: top of an existing `if`/`else` branch | 153 | 11.5% |
| B: top of an enclosing loop's body | 64 | 4.8% |
| B: a `switch` case block | 1 | 0.1% |
| C: a block opened only for scope | 132 | 9.9% |
| not placed by the heuristic | 95 | 7.1% |
| C99 declaration in the `for` itself | 0 after the fix (1 before) | |

The fuzz file follows the same pattern. Its loops at lines 309, 524 and 547 use form A. Its loop at
353 uses form B: an `int i;` at the top of an `if (anthropic) { ... }` branch.

**A correction.** Earlier the same day the supervisor described the inner block as the form "which the
file uses elsewhere". That is true and misleading. The file's one inner declaration is form B, an
existing branch, not the extra block of form C. Across the tree, form A is the norm, used for two loops
in three. Form C, the extra block, is the least common of the three at one loop in ten. It is a
recognised house idiom, not the standard.

Where form C is used, it is usually for one of two reasons:
- **A loop that comes after statements in the middle of a block.** For example `src/chat/jc_agent.c:2105`:
  a statement `last = NULL;` comes first, so a declaration there needs a new block.
- **A teardown loop at the end of a long function.** For example `src/acp/jc_acp.c:1076`, which frees a
  vector's strings, the same job as the fuzz loop.

## 6. Why the extra block was not used here

From the record, in order:

1. **The supervisor's note** that reported the defect said: "declare `i` at the top of the block".
2. **The lead's task text** for the worker made that concrete: "declare the loop variable at the top of
   the enclosing block (`jc_size i;` with the other declarations) and change the for to
   `for (i = 0; ...)`. Do not restructure the function."
3. **The worker** did exactly that, and its report records no other option.
4. **The lead's read-only reviewer** (`qwen27b`) approved it as it was.

No one in the record weighed form C. The honest answer is that **the choice was made by the wording of
the instruction, and the instruction came from the supervisor**, not from a comparison of the forms.

**Was it the right choice anyway?** On the merits, both are correct, and for this function form A
costs nothing:
- **For form A:**
  - `t_toolcall` is about 30 lines long, and its locals are declared together at the top.
  - The loop is its only loop, and nothing comes after it but the frees.
  - `i` is used nowhere else, so the wider scope has nothing to collide with.
  - Form A is also what two thirds of the tree does. "Do not restructure the function" kept the diff
    to two lines.
- **For form C:**
  - It keeps C99's narrow scope.
  - If someone later adds a second loop or more code after this one, a function-wide `i` can be
    reused by mistake.
  - `src/acp/jc_acp.c:1076` shows the tree using form C for exactly this kind of teardown loop.

A reader could reasonably prefer either. Neither would be wrong.

**The general lesson:** an instruction that names one form of a fix ("at the top of the block") gets
that form. If the choice matters, the instruction should name the options and the reason for the
preferred one, or leave the choice to the one who reads the code.

## 7. What it cost, and what the episode teaches

**Timeline (2026-10-21, local time):**

| When | What |
|---|---|
| 08:19 | `ddfb261e` (M1016, batch 5) adds the fuzz target with the C99 loop |
| 10:42-12:10 | the harness's independent reviewer reviews that range and reports, as its finding "P2 — HIGH", that the target does not compile under C89 |
| — | the supervisor never passes that report on to the lead |
| 14:37 and 15:44 | `make ci` is green on the batch head and on the merge commit; the clang stage only warns |
| 16:28 | master moves to `ff154a66`, with the loop in it |
| about 18:00 | a research pass reading the reviewer's records finds the finding again; the supervisor confirms it with gcc |
| 16:56-20:56 | the gateway outage delays the note to the team |
| 22:48 | `fd15771c` fixes the loop and the gate stage |
| about 23:17 | the supervisor verifies both directions |

Master still carries the loop until the next batch lands.

**What it teaches:**
1. **Know which compiler is the strict one for your rule.** For C89 declarations, gcc refuses and clang
   warns. A rule checked only by the lenient compiler is checked by nothing.
2. **Every file must pass through a strict build.** Name each source file and the gate stage that
   compiles it at `-Werror`. A test-only file, a fuzz target or a tool counts as much as the product.
3. **A finding that is not delivered is lost.** The reviewer did its job at 12:10. The relay failed,
   and the defect was found a second time only by accident. The harness plan now has the reviewer
   deliver its own findings.
4. **The wording of a fix instruction is a design decision.** Here it chose between three correct
   forms without anyone noticing.
5. **Count the tree before calling a form "standard".** The supervisor's impression ("used elsewhere")
   was true and misleading. The measurement took two minutes.

## 8. Try it yourself

1. Save `loop99.c` from §2 and compile it both ways:
   `gcc -std=c89 -pedantic -c loop99.c`, then `clang -std=c89 -pedantic -c loop99.c`. Note the exit
   statuses.
2. Rewrite it in form A, then in form C, and check each with
   `gcc -std=c89 -pedantic -Wall -Wextra -Werror -c <file>` and the same with `clang`. All four exit 0.
3. In jichi's tree, find which make target builds a given file (for example
   `make -n run_fuzz | grep jc_fuzz_targets`). Then check that some `make ci` stage builds that target
   with `WERROR=1`.
