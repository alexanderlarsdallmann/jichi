# Ireko 4 -- Numbers

*[入れ子（いれこ）*Ireko* — the JSON layer](IREKO.md) · chapter 4 of 7*

## Why this exists

JSON has one kind of number. C has many: `int`, `long`, `unsigned`, `size_t`, `double`. Numbers
arrive in jichi from places it does not control. A model's tool call carries a line number, an
`offset`, a `limit`. A server's reply carries an `index`, a request id, a timestamp in
milliseconds. Each one is text, and jichi has to turn it into a C number it can compute with.

Two things can go wrong, and both have happened.

- **On the way in**, a number too big for the C type it lands in. Converting the `double` 1e300
  to an `int` is not "a wrong number" in C; it is undefined behaviour, and one build of jichi
  aborted on it (M469). The same mistake was then found in the code that *builds* numbers (M866),
  and in 117 places that read numbers out of a parsed tree (M1077).
- **On the way out**, a number printed with too few digits. jichi prints JSON it did not write: it
  re-prints a model's tool arguments, a whole config file, an MCP server's schema. A printer that
  keeps six digits turns `50.5871234` into `50.5871` (M1076).

This chapter reads the four functions that stand in those two places: `parse_number`,
`number_to_int`, the clamp helpers in `src/json/jc_json.c`, and `pb_put_number`.

## The shape

```
text "12.5e3"
   |  parse_value: not " { [ t f n, so it must be a number
   v
parse_number   copy the bytes [0-9 + - . e E] into buf (63 at most)
   |           strtod(buf)          -> valuedouble   (a double: the real value)
   |           number_to_int(val)   -> valueint      (an int view, clamped)
   v
a cJSON node, type cJSON_Number
   |
   |  a caller needs an int, long, unsigned or size_t?
   |      jc_json_clamp_int / _long / _uint / _size (valuedouble)
   v
pb_put_number(d)   NaN, infinity, |d| > 1e300   -> null
                   a whole number, |d| < 2^53    -> "%.0f"
                   anything else                 -> "%.15g"; if that does not
                                                    read back as d, "%.17g"
```

The `double` is the real value. Everything else is a view of it, and every view has an edge.

## Finding the number: `parse_number`

`src/json/cJSON.c:parse_value` looks at the first byte of a value. A quote, a brace, a bracket,
`t`, `f` and `n` each have a case. Everything else goes to the number parser:

```c src/json/cJSON.c:parse_value
        default:  return parse_number(c);
```

```c src/json/cJSON.c:parse_number
static cJSON *parse_number(struct parse_ctx *c)
{
    char buf[64];
    int i = 0;
    const char *p = c->p;
    cJSON *node;
    double val;

    while (p < c->end && i < (int)sizeof(buf) - 1) {
        char ch = *p;
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' ||
            ch == '.' || ch == 'e' || ch == 'E') {
            buf[i++] = ch;
            p++;
        } else {
            break;
        }
    }
    buf[i] = '\0';
    if (i == 0) {
        c->ok = 0;
        return NULL;
    }
    val = strtod(buf, NULL);
    node = parse_new_item(c);
    if (node == NULL) {
        c->ok = 0;
        return NULL;
    }
    node->type = cJSON_Number;
    node->valuedouble = val;
...
    node->valueint = number_to_int(val);
    c->p = p;
    return node;
}
```

> **C sidebar — a fixed buffer on the stack.** `char buf[64]` is 64 bytes that live only while
> the function runs; no `malloc`, no `free`. `sizeof(buf)` is 64, and the loop stops at
> `sizeof(buf) - 1` so one byte is always left for the `'\0'` that ends the C string. The cast
> `(int)sizeof(buf)` is there because `sizeof` gives a `size_t`, which is unsigned, and `i` is
> an `int`: comparing the two directly draws a warning under `-Wextra`.

The loop copies every byte that *could* be part of a number, in any order, and stops at the
first that could not. It does not check the order. JSON's grammar does: an optional minus, then
an integer part that is either a single `0` or digits that do not start with `0` (so `0.5` is
valid and `01` is not), then an optional fraction (a point followed by at least one digit), then
an optional exponent (`e` or `E`, an optional sign, at least one digit). Only a minus may lead;
`+1` is not JSON. Here the
grammar is left to `strtod`, and `strtod` is not asked how much it used (below). Measured in
"Try it" step 3:

| text | parsed as |
|---|---|
| `-`, `e`, `--5` | the number 0 |
| `+1`, `01`, `1.` | 1 |
| `.5` | 0.5 |
| `[1-2]` | the array `[1]`: the `-2` is read and dropped |
| `1e5e5` | 100000 |
| a valid number of 64 or more characters | refused |

The last row is the 64-byte buffer. The loop stops after 63 bytes, the rest of the number is
left behind, and the next byte the parser sees is the number's 64th character (a digit, a point,
a sign or an `e`) where it wanted a `,`, a closing `]` or `}`, or the end of the text. A
63-character number is accepted. What is refused is the whole document, not the one number:
`cJSON_Parse` returns NULL for `{"text":"hi","n":` followed by a 64-character number. RFC 8259
(section 9) allows the limit: "An implementation may set limits on the range and precision of
numbers."

## Converting it: `strtod`

> **C sidebar — `strtod`.** `double strtod(const char *s, char **end)` reads the longest prefix of
> `s` that looks like a floating-point number and returns its value. If `end` is not `NULL`, it
> stores there a pointer to the first byte it did *not* use. That pointer is the only way to know
> whether the whole string was a number. If no number starts at `s`, it returns 0 and `*end` is
> `s` itself. A value too large for a `double` comes back as `HUGE_VAL`, which is infinity here.

`parse_number` passes `NULL`, so it never learns where `strtod` stopped. For `1-2`, `strtod` reads
`1` and stops at `-`; the parser has already moved its cursor past all three bytes. For `-` alone
`strtod` reads nothing and returns 0, and the parser makes a node with the value 0.

The same directory holds a reader that does ask.
`src/json/jc_json.c:json_str_to_double` turns a numeric *string* (a model that sent `"200"`
where it meant `200`) into a number:

```c src/json/jc_json.c:json_str_to_double
    v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
```

`end == s` means nothing was read. A non-blank byte after the number means the string was not
only a number, so `"200 lines"` is refused. The `end` check is the piece `parse_number` leaves
out.

Two limits of that fix. First, the `end` check would catch only part of the table above: `-`,
`e`, `--5`, `1-2` and `1e5e5` read nothing or leave bytes unread, but `+1`, `01`, `1.` and `.5`
are consumed whole by `strtod`, so they would still pass. Second, `strtod`'s own grammar is wider
than JSON's: it skips leading white space, and since C99 it also reads `inf`, `infinity`, `nan`
and hexadecimal numbers such as `0x10`, ignoring case. So `jc_json_get_num_lenient`, which calls
`json_str_to_double`, turns a model's `{"limit":"nan"}` into a NaN and `{"limit":"inf"}` into
infinity: the one route by which a NaN from outside reaches the clamp helpers below, whose NaN arm
reads it as 0.

The other edge is size. `1e400` is a valid JSON number and too big for a `double`. `strtod`
returns infinity, the code does not look at `errno`, and the node holds infinity (step 2). The
small end is quieter still: `1e-400` is too small for a `double`, `strtod` returns 0, and
`[1e-400]` prints back as `[0]`.

### The locale: a setting the parser never checks

`strtod` reads the decimal point *of the current locale*. In a locale that writes `2,5`, the
`.` in `2.5` is not a decimal point, and `strtod("2.5")` stops at it. `sprintf`, which prints
numbers, follows the same setting. Nothing in `src/json/cJSON.c` mentions locales. A C program
starts in the `"C"` locale, where the point is `.`, and stays there until it calls `setlocale`.

jichi addresses it from the outside, in two places. `main` switches only the time format:

```c src/main.c:main
    /* Localize wall-clock time display only (LC_TIME): the assistant-response
     * timestamp uses %X / the locale's format. Deliberately NOT LC_ALL/LC_NUMERIC
     * -- a locale decimal comma would corrupt cJSON number output (see
     * jc_locale_group_sep, which restores LC_NUMERIC="C"). */
    setlocale(LC_TIME, "");
```

And the one function that needs the number locale borrows it and gives it back:

```c src/platform/jc_platform_posix.c:jc_locale_group_sep
    /* Query the locale's numeric thousands separator, then RESTORE the C
     * numeric locale so printf/cJSON keep using '.' as the decimal point. A
     * persistent LC_NUMERIC switch would emit "1,5" into JSON and corrupt it. */
    setlocale(LC_NUMERIC, "");
    s = localeconv()->thousands_sep;
...
    setlocale(LC_NUMERIC, "C");
```

So the parser is correct only while every part of the program keeps that promise. Step 4 shows
what happens when it is broken: in a decimal-comma locale, `[0.5, 2.25]` reads back as `[0,2]`,
silently, and `2.5` prints as `2,5`, which is not JSON.

## The `int` view: `valueint` and `number_to_int`

Every number node carries two values: `valuedouble`, the real one, and `valueint`, an `int`.
`src/json/cJSON.h` calls `valueint` a "deprecated mirror of valuedouble". It is one of upstream
cJSON's fields, and this file keeps upstream's API so that the real library could replace it.

> **C sidebar — from `double` to `int`.** A cast like `(int)2.9` drops the fraction and gives 2;
> `(int)-2.9` gives -2. That is *truncation toward zero*. But if the value does not fit in an
> `int`, the C standard does not say what happens: the behaviour is *undefined*. The comment in
> `parse_number` cites C89 6.2.1.3. Undefined does not mean "some wrong number". It means the
> compiler may assume it never happens, and a build that checks for it may stop the program.

The int view used to be `(int)val`. Today it is this:

```c src/json/cJSON.c:number_to_int
static int number_to_int(double val)
{
    if (val != val) {
        return 0;
    }
    if (val >= (double)INT_MAX) {
        return INT_MAX;
    }
    if (val <= (double)INT_MIN) {
        return INT_MIN;
    }
    return (int)val;
}
```

Read it from the bottom. The cast on the last line is safe because the two tests above it have
already sent away every value that would not fit. A number at or above `INT_MAX` reads as
`INT_MAX`; at or below `INT_MIN`, as `INT_MIN`. This is a *clamp*: an out-of-range number is
pinned to the nearest end of the range. Nothing reports it: no error, no log line, and the model
is not told. A tool asked for `{"limit":1e10}` reads with a limit of 2,147,483,647, and one asked
for `{"offset":-1e10}` with -2,147,483,648. [DECISIONS.md, M469](../DECISIONS.md#dec-m469)
weighed this and kept the clamp: "a saturated value is at least ordered correctly", and a range
error would be "a new error nobody checks".

The first test looks strange: when is a value not equal to itself? When it is NaN, "not a
number". Every comparison with NaN is false, `val == val` included. Without this test, NaN would
fail both range tests and fall through to the cast, which is the undefined case again. The
comment in `parse_number` admits that JSON cannot produce a NaN here at all: there is no NaN
literal, and `strtod` on the bytes this parser collects does not make one. It is "written
anyway", because the fall-through "would be the same undefined cast this exists to remove".

`valuedouble` is never touched. A clamped `valueint` of 2147483647 sits beside the exact
`3000000000` (step 1).

**The builder, too (M866).** M469 put the clamp into the parser only. Numbers jichi *builds* go
through `cJSON_CreateNumber`, which kept the bare cast until M866 moved the clamp into this one
function for both. Its line in `cJSON_CreateNumber` now reads:

```c src/json/cJSON.c:cJSON_CreateNumber
        n->valueint = number_to_int(num);   /* M866: as the parser does */
```

Who reads `valueint` today? Outside `src/json/`, two places at this tree:
`src/net/jc_embed.c:item_index`, which reads an embeddings row's `index` through it on purpose
(its comment cites M469), and `src/harness/jc_harness_handoff.c:hh_write_render`. Everything else
reads `valuedouble` and converts it itself, which is where the clamp helpers come in.

## The clamp helpers: `jc_json_clamp_int`, `_long`, `_uint`, `_size`

A caller that wants a `long` or a `size_t` cannot use `valueint`. Before M1077 such callers wrote
`(int)jc_json_get_num(...)` or `(long)node->valuedouble`, the same undefined cast, in 117 places.
The hardening review of 2026-10-17 counted them (finding A-02, in
[`docs/analysis/2026-10-17-hardening-02-input-and-memory.md`](../analysis/2026-10-17-hardening-02-input-and-memory.md))
named the hottest: the tool-call `index` in both providers, the JSON-RPC ids of the MCP, ACP and
LSP layers, every line and column an LSP server sends, and `tu_arg_int`, "which every tool with a
numeric argument goes through". A model that sends `{"limit":1e10}` reached one of them. The
class itself was older: the review of 2026-10-08 had recorded it as F9 ("wire and config numbers
are cast to `int` or `long` without a range check",
[`docs/analysis/2026-10-08-review-07-the-wire.md`](../analysis/2026-10-08-review-07-the-wire.md)),
and A-02 calls it "already recorded as R07-F9 and still open at scale".

M1077 added four helpers with `number_to_int`'s shape, one per target type, and converted all
117 sites. The unsigned ones have one more arm:

```c src/json/jc_json.c:jc_json_clamp_uint
unsigned int jc_json_clamp_uint(double val)
{
    if (val != val) {                 /* NaN */
        return 0;
    }
    if (val <= 0.0) {                 /* -Inf, and every negative value -> min */
        return 0;
    }
    if (val >= (double)UINT_MAX) {    /* +Inf, and everything above it */
        return UINT_MAX;
    }
    return (unsigned int)val;
}
```

A negative number cannot be an unsigned value, so it becomes the smallest one, 0.

One detail in `src/json/jc_json.c:jc_json_clamp_long` is easy to miss. The test is
`val >= (double)LONG_MAX`, and on a machine with a 64-bit `long`, `(double)LONG_MAX` is *not*
`LONG_MAX`. A `double` has 53 bits of precision, so 2^63 − 1 rounds up to 2^63 (step 5 prints
9223372036854775808). With `>=`, the value 2^63 itself, which does not fit, is clamped; every
`double` below it is at most 2^63 − 1024, which fits. With `>` instead, 2^63 would reach the
cast.

The helpers take a `double`, not a key. The comment in `src/json/jc_json.c` records that as the
lead's decision, and `include/jc_json.h` gives the reason: "a caller that has the key does
jc_json_get_num(o, key, dflt) and clamps the double it gets back".

### The lint that keeps the casts out

Fixing 117 sites fixes 117 sites. The 118th would arrive with the next feature. So M1077 also
added `tests/smoke/json_cast_lint.sh`, which scans every `.c` and `.h` under `src/` for two
shapes:

```sh tests/smoke/json_cast_lint.sh
p1='\((int|long|unsigned (int|long)|jc_size)\) *jc_json_get_num'
p2='\((int|long)\) *[A-Za-z_][A-Za-z_0-9]*->valuedouble'
```

Its history is in its header. It was written first, red on purpose, with its floor at the exact
count of 117 ("100 of P1 + 17 of P2"), and it printed every site so the sweep could work from the
list. When the sweep landed, the floor became `MAX_HITS=0` in the same commit. Check 4 keeps the
count *exactly* at the floor, so a new cast fails loudly instead of being absorbed. And check 1
plants a file with known casts first, to prove the patterns can match anything at all: "A lint
that cannot catch a planted cast is a lint that cannot catch a real one."

What it cannot see follows from its patterns. A cast is caught only when it is written directly
on the reader **and** names one of the listed types: P1 knows `int`, `long`, `unsigned int`,
`unsigned long` and `jc_size`; P2 knows only `int` and `long`. So `(size_t)jc_json_get_num(...)`,
`(unsigned)jc_json_get_num(...)`, `(int)(jc_json_get_num(...))` with an extra parenthesis,
`(unsigned int)n->valuedouble` and `(int)cJSON_GetObjectItem(o, "k")->valuedouble` all pass it.
So does a cast in two steps: store the number in a variable first, `double d =
jc_json_get_num(...);` then `(int)d`, and neither pattern matches (step 6). The header says as
much: it "only forbids the bare cast the sweep replaces". The two-step shape is not hypothetical:
server token counts are narrowed with `(long)(in_tok + cache_read + cache_write)` in
`src/chat/jc_agent.c`, and [`docs/DEFERRED.md`](../DEFERRED.md) def-378 records it ("F9 to F11:
sizes, timeouts and token counts narrowed without a range check").

## Printing a `double` so it reads back the same: `pb_put_number`

```c src/json/cJSON.c:pb_put_number
static void pb_put_number(struct print_buf *b, double d)
{
    char tmp[64];
    if (d != d || d > 1.0e300 || d < -1.0e300) {
        /* NaN/Inf are not valid JSON; emit null. */
        pb_puts(b, "null");
        return;
    }
    if (d == floor(d) && fabs(d) < 9007199254740992.0) {
        /* Integral with |d| < 2^53: %.0f is exact and involves NO cast.
         * The old (long)d was undefined behaviour once |d| outgrew long,
         * and on ILP32 (32-bit long) it printed 3000000000 as
         * -2147483648 -- A-09/L-04, pinned by test_json_print_exact
         * (M1076) and a -m32 scratch build (L-09: no ci stage builds
         * -m32, so the scratch is the tooth). */
        sprintf(tmp, "%.0f", d);
    } else {
        /* Fractional, or integral at/above 2^53 where %.0f would print a
         * value the double is not: %.15g first, and only when 15 digits
         * do not read back to the same double, %.17g (upstream cJSON's
         * two-step). The round-trip check is what makes the output
         * exact: re-parsing it yields d, bit for bit. */
        double rt;
        char *end;
        sprintf(tmp, "%.15g", d);
        rt = strtod(tmp, &end);
        if (rt != d) {
            sprintf(tmp, "%.17g", d);
        }
    }
    pb_puts(b, tmp);
}
```

> **C sidebar — `%.0f`, `%.15g`, `%.17g`.** In a `printf` format, `%f` prints a number in plain
> decimal and the number after the dot is how many digits follow the point: `%.0f` prints a whole
> number, no point. `%g` counts *significant* digits instead, drops trailing zeros, and switches
> to exponent form (`1e+300`) when the exponent is below -4 or at least the number of digits
> asked for. `%.15g` keeps 15 significant digits.

There are three branches.

1. **Not a JSON number.** JSON has no NaN and no infinity, so those print as `null`. NaN needs its own
   test because it fails every comparison: without `d != d` it would pass both range tests, fall
   through to the `%.15g` branch and print as `nan` (glibc prints `-nan` for `0.0 / 0.0`), which
   is not JSON. Infinity is caught by the range tests.
2. **A whole number below 2^53.** A `double` holds every whole number up to 2^53 exactly
   (`jc_json.c`'s comment: "every integral double at 2^53 and below is exact"). Such a number
   prints in full with `%.0f`, with no cast to any integer type. `-0.0` prints as `-0`, which is
   valid JSON and reads back with its sign (step 1).
3. **Everything else.** Try 15 significant digits, then *check*: read the text back with `strtod`
   and compare. If it is not the same `double`, use 17 digits. The M1076 record says "17 digits
   round-trip every finite double". Most numbers that people write are short in 15 digits (`0.1`
   prints as `0.1`); a value that needs all 17 gets them.

The check is the idea worth keeping. The printer does not trust a format to be exact; it reads its
own output back and compares.

**The 1e300 edge.** Read the first test again: `d > 1.0e300 || d < -1.0e300`. The comment talks
about NaN and infinity, but the test also catches every *finite* number whose magnitude is above 1e300, on both sides of
zero (`-1e301` prints as `null` too). Measured in
step 2: `1e301` parses to a finite `double` and prints as `null`. The largest `double`,
`1.7976931348623157e308`, does the same. `1e300` itself still prints. The test is as old as the file's first commit,
and no record discusses it. It is reported with this chapter as a possible defect: it is the one
place in the printer's own code where the M1076 promise, "re-parsing it yields d, bit for bit",
does not hold for a finite number (step 4 shows the other way to break it: a changed
`LC_NUMERIC`). The value does not merely lose digits: it changes type, from a number to `null`.
jichi re-prints JSON it did not write (`config set` rewrites the whole config, and the Anthropic
dialect re-prints a non-streamed `tool_use` input,
`src/provider/jc_provider_anthropic.c:an_parse_full`), so a tool would read `null` where the
model sent a number.

**2^53 + 1.** `9007199254740993` prints back as `9007199254740992`. That loss happens in `strtod`,
not in the printer. 2^53 + 1 lies exactly halfway between two doubles, 2^53 and 2^53 + 2; `strtod` rounds a tie to
the one whose last significand bit is 0 ("round half to even"), which is 2^53, and the printer prints that double
exactly (step 1 shows "same bits").

`sprintf` again breaks the house rule on purpose, and `tests/smoke/sprintf_lint.sh` allowlists
these three lines by their exact text, as chapter 3 described. `tmp[64]` is far more than the
longest output, which `%.17g` keeps to 17 digits, a sign, a point and an exponent.

### Where the record says this fails

The architecture sweeps run jichi's unit suite on other CPUs, under an emulator. Here is what the
records say about doubles there, in their words; this section explains nothing beyond them.

- **M469** (`docs/ROADMAP.md`): on `armeb` (big-endian ARM, built with `zig cc` and musl),
  "armeb *aborted* mid-suite because `zig cc` traps UB by default". That abort is how the bare
  `(int)val` in the parser was found. The same entry also records that on armeb "the **literal**
  `0.2` prints `-2.35344e-185`, while its stored bytes ... are correct big-endian IEEE-754", and
  files it as "**toolchain.** jichi cannot make a compile-time constant misprint."
- **M469, defect 2:** on m68k, five checks of the unit suite failed on exact `==` comparisons of
  doubles, and "the values were right". The 68881 computes in an 80-bit format, so the comparison
  was the problem. The suite's answer was `JC_CHECK_NEAR` at the five sites.
- **M542** (`docs/PLATFORMS.md`, the re-sweep row): "One toolchain failure, `armeb` — a `printf`
  variadic-double bug reproduced in a minimal C program with no jichi in it, not a product
  defect."
- **0.14.0** (`docs/PLATFORMS.md`, the development box's row): "at 60edf145 armeb ran 37966
  checks, 75 failures (the known JSON double round-trip class)". The CHANGELOG lists it among the
  0.14.0 known issues as "a known toolchain class".

## Try it

You need only a C compiler for steps 1 to 4. Step 5 compiles four more files from the tree, step 6
runs one smoke driver, and step 7 needs a compiler with the UndefinedBehaviorSanitizer runtime.
The outputs were measured on x86-64 Linux with glibc and gcc 13. Floating-point printing belongs
to the C library, so another library or CPU may print some lines differently (the record's armeb
rows are an example). Nothing prints the name of your temporary directory.

**1. Round trips: 0.1, 1e300, -0.0, 2^53 + 1, a number too big for an `int`.** `num` parses each
argument, shows `valuedouble` with 17 digits and `valueint`, prints the node, parses the printed
text, and compares the two doubles bit for bit (`memcmp`, which, unlike `==`, can tell `-0` from
`0`).

```sh
# in the jichi checkout (no build needed: this compiles src/json/cJSON.c only)
T=$(mktemp -d)
cat > "$T/num.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

/* For each argument: parse it, show the two views of a number, print it
 * back, and check that the printed text parses to the same bits. */
int main(int argc, char **argv)
{
    int i;
    printf("%-18s %-24s %11s  %-18s %s\n",
           "input", "valuedouble (%.17g)", "valueint", "printed", "re-read");
    fflush(stdout);
    for (i = 1; i < argc; i++) {
        cJSON *a = cJSON_Parse(argv[i]);
        cJSON *b;
        char *text;
        int same;

        if (a == NULL) {
            printf("%-18s refused\n", argv[i]);
            continue;
        }
        text = cJSON_PrintUnformatted(a);
        b = cJSON_Parse(text);
        same = b != NULL && b->type == a->type &&
               memcmp(&a->valuedouble, &b->valuedouble, sizeof(double)) == 0;
        if (cJSON_IsNumber(a)) {
            printf("%-18s %-24.17g %11d  ", argv[i], a->valuedouble, a->valueint);
        } else {
            printf("%-18s %-24s %11s  ", argv[i], "(not a number)", "");
        }
        printf("%-18s %s\n", text,
               !cJSON_IsNumber(a) ? "-" : same ? "same bits" : "CHANGED");
        fflush(stdout);
        cJSON_free(text);
        cJSON_Delete(a);
        cJSON_Delete(b);
    }
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/num" "$T/num.c" src/json/cJSON.c -lm
"$T/num" 0.1 1e300 -0.0 9007199254740993 3000000000 -2.9
```

```
input              valuedouble (%.17g)         valueint  printed            re-read
0.1                0.10000000000000001                0  0.1                same bits
1e300              1.0000000000000001e+300   2147483647  1e+300             same bits
-0.0               -0                                 0  -0                 same bits
9007199254740993   9007199254740992          2147483647  9007199254740992   same bits
3000000000         3000000000                2147483647  3000000000         same bits
-2.9               -2.8999999999999999               -2  -2.9               same bits
```

No warnings. `0.1` is not exactly 0.1 in binary (17 digits show it), yet it prints as `0.1`,
because 15 digits read back to the same double. The int view of anything past `INT_MAX` is
`INT_MAX`, while the double keeps every digit. `-2.9` truncates toward zero, to -2.

**2. The edges: above 1e300, overflow, NaN and Infinity as text.**

```sh
# in the jichi checkout, the same shell as step 1
"$T/num" 1e301 1.7976931348623157e308 1e400 NaN Infinity
```

```
input              valuedouble (%.17g)         valueint  printed            re-read
1e301              1.0000000000000001e+301   2147483647  null               CHANGED
1.7976931348623157e308 1.7976931348623157e+308   2147483647  null               CHANGED
1e400              inf                       2147483647  null               CHANGED
NaN                refused
Infinity           refused
```

`1e301` and the largest `double` are finite and still print as `null`: the 1e300 edge. `1e400`
overflows to infinity in `strtod` and prints as `null`, as the comment intends. `NaN` and
`Infinity` start with `N` and `I`, which `parse_value` sends to `parse_number`, whose loop copies
nothing: refused.

**3. The grammar `strtod` is left to enforce.**

```sh
# in the jichi checkout, the same shell as step 1
"$T/num" - e --5 +1 .5 01 1. '[1-2]' 1e5e5 \
    123456789012345678901234567890123456789012345678901234567890123 \
    1234567890123456789012345678901234567890123456789012345678901234
```

```
input              valuedouble (%.17g)         valueint  printed            re-read
-                  0                                  0  0                  same bits
e                  0                                  0  0                  same bits
--5                0                                  0  0                  same bits
+1                 1                                  1  1                  same bits
.5                 0.5                                0  0.5                same bits
01                 1                                  1  1                  same bits
1.                 1                                  1  1                  same bits
[1-2]              (not a number)                        [1]                -
1e5e5              100000                        100000  100000             same bits
123456789012345678901234567890123456789012345678901234567890123 1.2345678901234569e+62    2147483647  1.2345678901234569e+62 same bits
1234567890123456789012345678901234567890123456789012345678901234 refused
```

None of the first nine is valid JSON, and all nine are accepted. The last two are JSON numbers.
The 63-digit one is accepted; the 64-digit one is refused.

**4. The locale.** The program switches `LC_NUMERIC` to the locale you name, then parses and
prints. `en_DK.utf8` is a locale whose decimal point is a comma; if your machine does not have
it, the program says so.

```sh
# in the jichi checkout, the same shell as step 1
cat > "$T/locale.c" <<'EOF'
#include <stdio.h>
#include <locale.h>
#include "cJSON.h"

int main(int argc, char **argv)
{
    cJSON *a;
    char *text;
    if (argc < 2 || setlocale(LC_NUMERIC, argv[1]) == NULL) {
        printf("this machine has no such locale\n");
        return 0;
    }
    a = cJSON_Parse("[0.5, 2.25]");
    text = cJSON_PrintUnformatted(a);
    printf("[0.5, 2.25] reads back as %s\n", text);
    cJSON_free(text);
    cJSON_Delete(a);
    a = cJSON_CreateNumber(2.5);
    text = cJSON_PrintUnformatted(a);
    printf("2.5 built and printed:    %s\n", text);
    cJSON_free(text);
    cJSON_Delete(a);
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/locale" "$T/locale.c" src/json/cJSON.c -lm
"$T/locale" C
"$T/locale" en_DK.utf8
```

```
[0.5, 2.25] reads back as [0.5,2.25]
2.5 built and printed:    2.5
[0.5, 2.25] reads back as [0,2]
2.5 built and printed:    2,5
```

No error anywhere. The fractions are silently gone. And `2,5` is not one number: inside an array
it reads back as two values (`[2,5]` is a two-element array), and anywhere else it makes the
document invalid (`{"a":2,5}` and a bare `2,5` are both refused).

**5. Numbers jichi builds (M866), and the clamp helpers.** `jc_json.c` needs a few more files
from the tree: the string and memory helpers it calls, and the bounded `jc_snprintf` they use.

```sh
# in the jichi checkout, the same shell as step 1
cat > "$T/clamp.c" <<'EOF'
#include <stdio.h>
#include <limits.h>
#include "jc_json.h"

static void built(const char *label, double d)
{
    cJSON *n = cJSON_CreateNumber(d);
    char *text = cJSON_PrintUnformatted(n);
    printf("CreateNumber(%s): valueint %d, printed %s\n",
           label, n->valueint, text);
    cJSON_free(text);
    cJSON_Delete(n);
}

int main(void)
{
    double zero = 0.0;
    built("5e9", 5e9);
    built("NaN", zero / zero);
    built("inf", 1.0 / zero);
    printf("clamp_int(1e10)   = %d\n", jc_json_clamp_int(1e10));
    printf("clamp_uint(-1)    = %u\n", jc_json_clamp_uint(-1.0));
    printf("clamp_long(1e100) = %ld\n", jc_json_clamp_long(1e100));
    printf("clamp_size(NaN)   = %lu\n", (unsigned long)jc_json_clamp_size(zero / zero));
    printf("(double)INT_MAX   = %.0f\n", (double)INT_MAX);
    printf("(double)LONG_MAX  = %.0f\n", (double)LONG_MAX);
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Iinclude -Isrc/json -o "$T/clamp" "$T/clamp.c" \
    src/json/jc_json.c src/json/cJSON.c src/util/jc_str.c src/util/jc_mem.c \
    src/platform/jc_snprintf.c -lm
"$T/clamp"
```

```
CreateNumber(5e9): valueint 2147483647, printed 5000000000
CreateNumber(NaN): valueint 0, printed null
CreateNumber(inf): valueint 2147483647, printed null
clamp_int(1e10)   = 2147483647
clamp_uint(-1)    = 0
clamp_long(1e100) = 9223372036854775807
clamp_size(NaN)   = 0
(double)INT_MAX   = 2147483647
(double)LONG_MAX  = 9223372036854775808
```

5e9 is the value M861's unit test built, the one that stopped `make ci` (M866). A NaN can be
*built*, even though it cannot be parsed, and the `val != val` arm gives it the int view 0; the
printer writes it as `null`. C89 has no `NAN` macro, so the program divides by a variable that
holds zero. Strictly, C89 leaves a division by zero undefined; on IEEE 754 hardware, as here, it
gives NaN and infinity, and `tests/test_json.c` makes its NaN the same way (`0.0 / 0.0`).

The last two lines were measured on a machine with a 64-bit `long`. `INT_MAX` survives the trip
into a `double`; `LONG_MAX` comes back one larger, which is why the clamp tests with `>=`.

**6. The lint, and what it cannot see.**

```sh
# in the jichi checkout, the same shell as step 1
sh tests/smoke/json_cast_lint.sh < /dev/null
printf 'double d = jc_json_get_num(o, "k", 0.0);\nint n = (int)d;\n' > "$T/two-step.c"
grep -cE '\((int|long|unsigned (int|long)|jc_size)\) *jc_json_get_num' "$T/two-step.c"
```

```
1..4
ok 1 - the matcher flags both cast shapes and spares the clamp helper
ok 2 - scanning 233 .c/.h files under src/
ok 3 - no out-of-range int/long cast of an untrusted JSON double (clean tree)
ok 4 - the count is exactly the post-sweep floor (0) -- the tree is clean and stays floored so a new bare cast fails loudly (M1077 sweep landed)
0
```

The `grep` uses the lint's own pattern P1 on a two-line file holding the same unsafe cast in two
steps: 0 matches (and `grep -c` exits 1 when it counts nothing). The lint is green and the cast
is there.

**7. Put the bare cast back (measured for this chapter).** This replays M469 and M470 on
`x86-64` with gcc. The `cd` happens inside `( ... )`, a subshell, so your shell stays in the
checkout, and the sanitizer's message names `bare-cast.c` without your temporary directory.

```sh
# in the jichi checkout, the same shell as step 1 (needs the compiler's UBSan runtime)
cp src/json/cJSON.h "$T/"
sed 's/node->valueint = number_to_int(val);/node->valueint = (int)val;/' \
    src/json/cJSON.c > "$T/bare-cast.c"
grep 'node->valueint =' "$T/bare-cast.c"
(cd "$T" && cc -std=c89 -pedantic -Wall -Wextra -fsanitize=undefined \
    -o num-ub num.c bare-cast.c -lm && ./num-ub 1e300)
(cd "$T" && cc -std=c89 -pedantic -Wall -Wextra -fsanitize=undefined,float-cast-overflow \
    -fno-sanitize-recover=all -o num-trap num.c bare-cast.c -lm && ./num-trap 42 1e300)
```

```
    node->valueint = (int)val;
input              valuedouble (%.17g)         valueint  printed            re-read
1e300              1.0000000000000001e+300  -2147483648  1e+300             same bits
input              valuedouble (%.17g)         valueint  printed            re-read
42                 42                                42  42                 same bits
bare-cast.c:433:5: runtime error: 1e+300 is outside the range of representable values of type 'int'
```

The first build has a sanitizer and stays silent: gcc's `-fsanitize=undefined` does not include
the float-to-int check, and on x86-64 the undefined cast gave `INT_MIN`, the wrong sign. The
second build names the check and stops at the first error, exit status 1. The `42` before it went
through without a word: the check fires only on an input that reaches it.

## What went wrong here, and what it teaches

### M469 and M470: the sanitizer was there, the input was not

`make ci` has run the unit suite under ASan and UBSan for a long time. Yet `(int)val` in
`parse_number` survived until M469, and it was found by a *different compiler on a different
architecture*, on the same workstation: the sweep cross-built jichi for big-endian ARM (armeb)
with `zig cc` and ran it under the `qemu-user` emulator; `zig cc` traps
undefined behaviour by default, and the armeb row aborted in the middle of the suite. M469's own
words: "**a sanitizer only sees the inputs you give it**". Nothing in the unit corpus had ever fed
the parser a number outside `int`'s range.

M470 then asked why the gate had missed it, and found "four separate things had to be true":
the bug; the input was absent (the `json` fuzz target "had **no seed corpus at all**"); the check
was disabled (gcc's `-fsanitize=undefined` does not include `float-cast-overflow`; step 7 shows
it); and the sanitizer recovered (it "prints `runtime error: ...` and carries on by default, so
the run still exits 0"). Its rule: "*A sanitizer only sees the inputs you give it, only checks
what you enabled, and only fails if you told it to.*" The fuzz corpus now holds the byte
sequences that reach the cast, and the Makefile names the check and turns recovery off.

[DECISIONS.md, M469](../DECISIONS.md#dec-m469) records why the answer is a clamp and not a
refusal: `1e300` "**is** valid JSON, and refusing a whole model response over one large number
turns a latent UB into a live outage".

### M866 and M1077: a fix to one instance is not a fix to the class

The clamp went into the parser. `cJSON_CreateNumber`, a few hundred lines further down, kept
`(int)num` until M866, when a new unit test built 5e9 and the sanitizer stage of `make ci` stopped
on it. Then the hardening review counted the callers: about 115 sites (117 when the sweep began)
still cast a JSON `double` straight to `int` or `long`. The analysis puts the lesson plainly: "a
fix to a bug *class* is only finished when you have found every instance of the class". The house
rule it cites is "audit the universe, not the result". M1077 answered with all three pieces:
helpers, a sweep of every site, and a lint whose floor fell from 117 to 0 in the commit that
finished the sweep.

### M1076: a printer that was not exact

Before M1076, `pb_put_number` printed a whole number up to 1e15 with `sprintf(tmp, "%ld",
(long)d)` and everything else with `%g`: six digits, so `1760684400.125` became `1.76068e+09`.
On a 32-bit `long`, `(long)d` of 3,000,000,000 is out of range, and it "printed -2147483648"
under `gcc -m32`. jichi re-prints JSON it did not write, so "a rounded reprint is a silently
corrupted wire value" (the comment above `test_json_print_exact` in `tests/test_json.c`). That
test parses nine literals, prints each, and requires the text to read back as the identical
double.

The thread through all four: a number from outside is data you did not choose. Every conversion
of it, into a smaller type or into text, has an edge, and the edge has to be decided in code, not
left to the platform.

## Check yourself

1. Why does `number_to_int` test `val != val` before anything else, when JSON cannot produce a
   NaN?
2. A model sends `{"limit": 1e10}` to a tool that wants an `int`. What did the tool get before
   M1077 on x86-64, and what does it get now?
3. Why is 2^53 the boundary between the `%.0f` branch and the `%.15g` branch?
4. `9007199254740993` comes back as `9007199254740992`. Which function lost the 1?
5. `[1-2]` parses as `[1]`. Which argument in `parse_number` throws away the information that
   would have caught it, and which function in `src/json/jc_json.c` shows the fix?

<details><summary>Answers</summary>

1. NaN fails every comparison, so both range tests would be false and NaN would reach the cast,
   which is undefined. The parser cannot produce one, but `cJSON_CreateNumber` shares the
   function, and step 5 builds a NaN. And a NaN can come from outside through a different reader:
   `jc_json_get_num_lenient` reads the string `"nan"` as NaN, and the clamp helpers read it as 0.
2. Before: an undefined cast, which on x86-64 read as `INT_MIN`, and which a trapping build turns
   into an abort. Now: `jc_json_clamp_int` gives `INT_MAX`.
3. Up to 2^53, every whole number is exactly a `double`, so a whole-number `double` there is a
   plain integer, and `%.0f` prints it in full with no cast. Above 2^53 some whole numbers are
   not doubles at all, and the code sends every number there to the `%g` branch, whose
   round-trip check makes sure the text reads back as the same `double`. There is also a practical
   reason. Above 2^53 `%.0f` grows with the number (1e300 prints as 301 characters, far past
   `tmp[64]`), and the C standard asks `printf` to be correctly rounded only up to `DECIMAL_DIG`
   significant digits (C99 7.19.6.1), so past that another C library may print digits that are
   not the double's exact value: the case the code comment means by "a value the double is not".
   Below 2^53 the output is at most 16 digits and a sign.
4. `strtod`, in `parse_number`. 2^53 + 1 is exactly halfway between the doubles 2^53 and 2^53 + 2, and `strtod` rounds the tie to even, which is 2^53. The printer prints that
   double exactly.
5. The `NULL` passed as `strtod`'s second argument: without the end pointer, the parser cannot
   see that `strtod` stopped at `-`. `json_str_to_double` passes `&end` and refuses a string with
   anything left after the number. It catches `[1-2]`, but not `+1`, `01`, `1.` or `.5`, which
   `strtod` consumes whole, and it lets through what `strtod` itself adds: `nan`, `inf`, `0x10`.

</details>

*Next: [chapter 5 — Building and printing](ireko-05-building-and-printing.md), where jichi makes its
own trees and the print buffer grows to hold them.*
