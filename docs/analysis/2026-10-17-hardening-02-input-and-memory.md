# Hardening review 2 — Reading untrusted input without getting hurt

*Part of the 2026-10-17 hardening review of jichi at commit `e5d832a4`. This chapter covers the code that
turns bytes from outside jichi into C data structures: the JSON parser, the SSE stream framer, the provider
event handlers, the YAML/JSONC config converter, the small utility parsers (patch, diff, UTF-8, base64, the
test-output scraper, the markdown renderer), the protocol codecs (LSP, MCP, ACP, control, daemon), and the
tool-argument readers. Drafted by reviewer A (Claude Opus 5.5), edited by the review's lead, who re-checked the central
claims. Start with [the overview](2026-10-17-hardening-00-overview.md). Security defects are given here by class only; the reproductions live
in the internal companion to this chapter.*

---

## Who this is for, and what you will know after reading it

**For** a self-learner or junior developer who knows a little C and wants to see how a program defends itself
when every input comes from somewhere it does not control: a language model, a remote server, an editor, a
cloned repository, a test runner.

**After reading it you will be able to:**

- name the three stages every parser in jichi goes through — framing, parsing, interpretation — and what can
  go wrong at each;
- explain why converting an out-of-range `double` to an `int` in C is not merely wrong but *undefined*, and
  why that matters more on a 32-bit board than on a laptop;
- recognise the one rule that ties together half the findings in this chapter: **every reader of an input must
  interpret it the same way the code that acts on it does;**
- judge when a size cap actually bounds what you think it bounds.

**What you need first:** the project's documentation standard, [DOC_REVIEW.md](../DOC_REVIEW.md), sets the
house rules this chapter follows. For the code itself, [Annai 6](../reading/annai-06-the-wire.md) is the
guided tour of the request/response path and [Fukabori 8](../reading/fukabori-08-streaming-and-the-no-buffering-invariants.md)
is the deep dive on streaming. The companion chapters are
[chapter 03, processes and fences](2026-10-17-hardening-03-processes-and-fences.md), which owns the full
treatment of the edit-scope fence, and [chapter 01, build and binaries](2026-10-17-hardening-01-build-and-binaries.md) for the
build-mode and 32-bit numbers.

**A note on scope.** This chapter reviews about 12,900 lines read in full and another few thousand searched.
It does **not** review the transports underneath the protocol codecs (`jc_mcp_stdio.c`, `jc_mcp_http.c`,
`jc_acp.c`, `jc_lsp.c`, `jc_http.c` beyond their size caps), the converter's per-format mapping logic after
the parse, or `jc_config.c` beyond its numeric casts. Those are marked "not reviewed" rather than "clean".

---

## 1. The shape of the problem

A **fence**, in jichi's vocabulary, is a limit that bounds what a run can do: the path fence keeps file access
inside the workspace, `--edit-scope` keeps writes inside a glob, a read-only agent runs no mutating tool. A
fence is supposed to hold no matter what the model or a server sends.

Almost everything jichi reads is written by something it does not trust to be well-behaved. The model server
streams Server-Sent Events (SSE). The model writes its tool-call arguments as a JSON string. MCP and LSP
servers, a code editor over ACP, and a supervisor over a control socket all send JSON-RPC. A repository the
model cloned supplies Markdown files with YAML frontmatter. A test runner prints output jichi scrapes for
pass/fail counts. None of these is an attacker in the usual sense — but a model emits malformed and surprising
shapes constantly, a server can be buggy or hostile, and a repository can contain anything.

Every one of those inputs flows through the same three stages, and each stage has its own failure mode:

```text
   bytes from outside
        |
        v
  [1] FRAMING      cut the stream into messages
        |          failure: a peer that never ends a message -> unbounded memory
        v
  [2] PARSING      turn a message into a tree / struct
        |          failure: the input controls recursion depth -> stack overflow
        |                   a number too big for the C type    -> undefined behaviour
        v
  [3] INTERPRETATION  read values out, decide what to do
                   failure: two readers disagree about the same bytes -> a fence
                            that does not hold
```

Keep that picture. Every finding below sits at one of those three stages.

---

## 2. Framing: a cap only counts if you know what it caps

Framing cuts a byte stream into messages. `jc_sse_feed` (`src/net/jc_sse.c:86`) walks the stream a byte at a
time and fires a callback per complete event. `jc_lsp_framer_pop` (`src/lsp/jc_lsp_proto.c:85`) reads
`Content-Length` headers. Each framer carries a byte cap so a peer that never ends a message cannot grow a
buffer forever — `JC_SSE_FIELD_MAX` is 4 MB, the LSP body cap is 64 MB, the MCP and ACP line cap is 8 MiB.

These caps are real and each one was added after an incident — the LSP `Content-Length` guard, for example,
came from M609 after a value that wrapped around the integer type (`2^64 + 5` becoming `5`) was found able to
desync the stream (`src/lsp/jc_lsp_proto.c:65-78`). That is good defensive work.

But a cap only means something once you are sure which quantity it bounds, and when it is checked. Two gaps
remain:

- **The cap bounds one field, not the whole reply.** Streamed tool-call arguments and assistant text are
  appended across *every* event of a stream with no ceiling on the running total (`src/net/jc_sse.c` caps one
  `data:` field; `src/provider/jc_provider.c:108` and `jc_provider_openai.c:288` accumulate the sum). See
  A-07.
- **The cap bounds input bytes, not memory.** The next section shows a 4 MB parse becoming ~160 MB of tree.

**The lesson for any codebase:** write a cap in the unit of the thing you are protecting. If you are protecting
memory, cap memory, not the wire bytes that happen to correlate with it on a good day.

---

## 3. Parsing: recursion depth and the range of a C type

### The parser that gets this right

jichi ships its own JSON parser (`src/json/cJSON.c`). It keeps the cJSON *API* but shares none of its code
(M171) — the comment at `src/json/cJSON.c:8-19` explains that the file was once mis-filed as vendored
third-party source and is not. It is recursive descent, which means the *input* decides how deep the C call
stack goes. Feed it `[[[[[[...`, and each `[` is another stack frame; enough of them and the process runs off
the end of its stack and dies.

M472 fixed this the right way, and it is worth reading because the reasoning generalises:

```c
/* src/json/cJSON.c:508-523 */
case '{':
case '[': {
    /* The ONLY recursion in this parser, so the depth bound lives here
     * rather than in parse_object/parse_array -- each of those has five
     * return points, and a counter decremented at five of them is a
     * counter that leaks at the sixth someone adds (M472). */
    cJSON *v;
    if (c->depth >= JC_JSON_MAX_DEPTH) {
        c->ok = 0;
        return NULL;
    }
    c->depth++;
    v = (*c->p == '{') ? parse_object(c) : parse_array(c);
    c->depth--;
    return v;
}
```

Two things to notice. First, the bound is placed at the single point where recursion happens, not spread
across the two functions that recurse — so there is one increment and one decrement, and no future edit can
"leak" the counter. Second, 256 is generous: real payloads nest about a dozen deep, so nothing legitimate is
refused. This is a model fix.

### The parser that does not

The YAML parser (`src/convert/jc_yaml.c`) is recursive descent too — `parse_node` -> `parse_seq`/`parse_map`
-> `parse_node` — and has **no** depth bound (A-04, **high**). It is reached from every Markdown file with
frontmatter that jichi loads (`src/util/jc_md.c:52`): command, agent, skill and output-style definitions under
a project's `.jichi/` directory, and from converting a Continue `config.yaml`. Those files come from a cloned
repository or a user-supplied config — outside the trust boundary.

Measured (reviewer A, default build, varying the stack with the shell's stack-size limit): a deeply nested YAML
document overflows the stack at about 47,800 levels on a normal 8 MB stack, about 2,900 levels on the 512 KB
stack jichi gives itself on FreeMiNT, and about 690 levels on a 128 KB stack. At two bytes per level that is a
few kilobytes of frontmatter — well within a file a repository can carry. This is exactly the class M472
closed for JSON, in the sibling parser the fix never reached. The fix is the same: one depth counter at the
`parse_node` funnel, refusing past a generous bound.

### Turning a `double` into an `int`

JSON has one number type, and jichi stores it as a C `double` (`src/json/cJSON.h:45-46`). Code that wants an
integer casts: `(int)value`. Here is the trap. In C, converting a floating-point value to an integer type when
the value does not fit is **undefined behaviour** (C89 6.2.1.3) — not "you get a wrong number", but "the
standard no longer defines what your program does". In practice x86 yields `INT_MIN`, 32-bit ARM saturates,
and a build with undefined-behaviour trapping turned on (as `zig cc` does by default, and as clang's
`-fsanitize=float-cast-overflow` does) *aborts the program*.

jichi learned this the hard way. M469 found `node->valueint = (int)val` in the number parser abort on a
big-endian ARM build because the test corpus finally fed it `{"x":1e300}`; the fix was a clamp,
`number_to_int` (`src/json/cJSON.c:282`), and M866 and M725 applied the same clamp to two more sites. The
parser, the number constructor, and the embeddings reader are now safe.

But the clamp was never propagated to the *callers*. Reviewer A counted about 115 sites that still cast an
untrusted `double` straight to `int` or `long` (A-02, **medium**, already recorded as R07-F9 and still open at
scale). The hottest sit directly on a model stream or a server reply: the tool-call `index` in both providers
(`src/provider/jc_provider_openai.c:268`, `jc_provider_anthropic.c:392,408`), the JSON-RPC id in the MCP, ACP
and LSP layers, every `line`/`character` an LSP server sends, and `tu_arg_int` (`src/tools/tool_util.c:123`),
which every tool with a numeric argument goes through. Reviewer A drove three of them end to end through the mock model and watched each abort
under `-fsanitize=float-cast-overflow`. The fix is to export the clamp cJSON already has as a
`jc_json_get_int`/`jc_json_get_long` helper, use it everywhere, and add a lint forbidding the bare cast on a
JSON value.

**The lesson:** a fix to a bug *class* is only finished when you have found every instance of the class. A
clamp in the parser and three call sites is not the same as a clamp at every place a wire number becomes a C
integer. jichi's own rule names this ("audit the universe, not the result", CLAUDE.md); A-02 is the universe
the audit had not yet swept.

---

## 4. Interpretation: when two readers disagree, a fence falls

This is where the most serious findings live, and they share one root cause. jichi is generous with the
model's malformed input: it repairs nearly-JSON arguments (M148), unwraps arguments a model nested under the
tool's own name (M172), and parses an argument a model sent as a quoted string back into the array it should
have been (M193). Each of these was measured to help on real runs. But a repair is a *second parser*, and the
program now has two views of the same bytes: the raw arguments, and the normalised ones. Everything is fine
only while every reader uses the same view.

### The edit-scope fence can be walked around (class only; detail in the internal companion)

**A-01, high, measured, new.** jichi's `--edit-scope` flag confines a run's file writes to a glob. The check
that enforces it (`src/chat/jc_agent.c`, `call_out_of_scope_path`) parses the *raw* tool arguments itself and
reads the path out. The model's argument normalisations — the self-named unwrap and the string-to-array
coercion — run *later*, inside `jc_tool_execute` (`src/tools/jc_tool.c:1774,1800`), on a tree the fence never
saw. An earlier fix (M532) taught the fence to run one of the repairs; the other two were not carried across.

The result: two argument shapes that a model emits on its own cause the fence to read one path (or none) while
the tool writes another. The fence does not fire; the out-of-workspace write lands. The review's lead reproduced this
independently, and it is the same shape as a sibling path-normalisation bypass that
[chapter 03](2026-10-17-hardening-03-processes-and-fences.md) covers (there, a `..` segment in the path). Both
are instances of a rule the project already states for its approval previews — *"a preview must read every
argument exactly as the executor will"* (CLAUDE.md, M530) — applied to a fence instead of a preview. The two
bypasses are written up together in chapter 03; the smallest fix is to normalise the arguments **once**, at
the boundary, and hand the same tree to the fence and to the tool.

**The lesson, and it is the theme of this chapter:** normalise untrusted input exactly once, as early as
possible, and let every later reader — the fence, the preview, the executor — read the normalised form. Two
readers that disagree about what the input says are one security boundary that does not hold.

### The same shape, without the security impact

The disagreement pattern recurs in lower-stakes places, which is a good way to learn to see it:

- **The lenient number reader is looser than its contract.** `jc_json_get_num_lenient` accepts a numeric
  *string* so a model sending `{"limit":"200"}` is understood (M168, a real incident where the strict read
  returned the default 0, meaning "no limit", and a tool read a whole 172 KB file). But it uses `strtod`, so
  it also accepts the words `nan`, `inf` and hex like `0x7fffffff` as numbers — and then casts the result
  without a range check (A-02 again). Accepting more spellings is fine; accepting any *value* is not.
- **Duplicate object keys.** `{"x":1,"x":2}` parses with both members kept, and `cJSON_GetObjectItem` returns
  the *first* (A-13, low). Most JSON libraries, and most servers, take the last. A peer whose own parser took
  the last value for a security-relevant key would disagree with jichi about what it sent. No concrete exploit
  was traced, but it is the same disagreement class.
- **Trailing garbage after the root.** `cJSON_Parse("{\"a\":1} x")` returns the object and ignores the ` x`
  (A-08, low, already recorded as R07-F11). This matches upstream cJSON, but it means a malformed tool-argument
  blob like `{"a":1}}` is accepted and echoed back onto the wire, where a strict server rejects the whole
  request — the "wedged run" shape `jc_prov_args_wire` was written to prevent. The one-line fix is to require
  only whitespace after the root value.

### The bool boundary, done right

It is worth saying what is done **well** here, because it is the counter-example that makes the rule concrete.
The project's rule is *"who wrote this JSON?"* (CLAUDE.md): input from a human, a foreign program or a model is
read *leniently* (a `1` or `"true"` means true, because people and models write that), while jichi's own logs
are read *strictly* (a non-bool there is a bug to surface). Reviewer A checked every strict boolean read in the
reviewed files and found them all on jichi's own side of the boundary — telemetry, run views, jichi's own
control reply (A-15). The leniency rule is applied consistently for booleans. A-02 is simply the same rule not
yet applied to *numbers*: a wire number should be range-checked the way a wire bool is type-checked.

---

## 5. Memory safety on the small boards

jichi targets 32-bit systems and boards with 256 MB of RAM and 128-512 KB stacks. Two findings only bite
there, and both are invisible on a 64-bit laptop.

### A product that overflows a 32-bit `long`

**A-03, high on 32-bit, measured, new.** The unified-diff renderer sizes its dynamic-programming table with
`long cells = (long)(mm + 1) * (long)(nn + 1);` (`src/util/jc_diff.c:101`), where `mm` and `nn` are the line
counts of the two sides. On a 64-bit system `long` is 64 bits and this is fine. On a 32-bit system `long` is
32 bits, the product overflows once both sides pass about 46,340 lines, and the cap check that is supposed to
fall back to a cheaper algorithm is then made on a meaningless number. Reviewer A measured crashes (compiled
with `gcc -m32`) at 46,341 lines per side and up; under the undefined-behaviour sanitizer it reports the
signed overflow at that line. The diff runs on every `edit_file` and `apply_patch` and in the TUI preview, on
file content the model chose — so a 32-bit board editing a large generated or minified file crashes the
agent. The fix is to test the bound before multiplying, in unsigned arithmetic.

**The lesson:** on a 32-bit target, any `int`-or-`long` multiplication whose operands come from input is a
place to check for overflow *before* the multiply. `(long)(a) * (long)(b)` does not help if `long` itself is
32 bits.

### A parse that costs forty times its bytes

**A-06, medium, measured, new.** The JSON parser allocates one 64-byte node per value. A dense array of small
numbers is one node each. The byte cap bounds the input at 4 MB per SSE field, but nothing bounds the node
*count*. Reviewer A measured the resident memory: a 4 MB JSON array of small numbers produced about 169 MB of
tree — roughly 40x the input, and linear (8 MB in gave ~337 MB). An embeddings reply, or any model/MCP/LSP
JSON up to the field cap, costs about forty times its size in RAM while it is parsed. On a 256 MB board a
single large reply can exhaust memory even though the byte cap "held" — which is the framing lesson from
section 2 made concrete. This is inherent to a node-per-value tree; the realistic fixes are a tighter per-reply
element cap (embeddings already carry an expected count) and a line in [LOW_MEMORY.md](../LOW_MEMORY.md) saying
the field cap is not a memory cap.

---

## 6. Numbers on the wire: precision and portability (merged A-09 / L-04)

This finding was found independently by reviewer A and measured in depth by the lead; the numbers below are the
lead's.

**Medium, measured and verified.** jichi's JSON *printer* (`src/json/cJSON.c:917-929`, `pb_put_number`) prints
an integral value with `|d| <= 1e15` through `sprintf(tmp, "%ld", (long)d)` and everything else through
`sprintf(tmp, "%g", d)`. Two problems follow.

**Precision loss, on every platform.** `%g` keeps six significant digits. The lead measured these round-trips
(parse a number, print it back), on x86-64:

| input | printed back |
|---|---|
| `50.5871234` | `50.5871` |
| `1760684400.125` | `1.76068e+09` (4,400 seconds off) |
| `9007199254740991` | `9.0072e+15` |
| `1234567890123456789` | `1.23457e+18` |

This matters because jichi reprints JSON it did not write. When an MCP tool is called, the model's parsed
arguments are cloned by printing and re-parsing them and then printed again onto the wire
(`src/mcp/jc_mcp.c`): a model asking an MCP tool for `{"amount": 1234.5678}` sends the server `1234.57`. An
MCP server's input schema is reprinted too, so a `"maximum": 9007199254740991` reaches the model as
`9.0072e+15`.

**Undefined behaviour, on 32-bit.** The `(long)d` cast is the same trap as section 3: on a 32-bit `long`,
every integral value in the range `(2^31, 1e15]` is out of range, so the cast is undefined. The lead measured,
with `gcc -m32`, that `2147483648`, `4294967295` and a millisecond timestamp like `1760684400000` all print as
`-2147483648`; the sanitizer reports the overflow at `cJSON.c:924`.

The fix is C89-clean and one place: print an integral value below `2^53` with `"%.0f"` (exact, no `long` cast),
and other values with `"%.17g"` (which round-trips; upstream cJSON uses `%1.15g` and retries at `%1.17g`). The
`sprintf_lint` allowlist pins these two lines by their exact text, so it has to be updated in the same change.
A red-first test parses and prints `50.5871234`, `9007199254740991`, `4294967295` and `1760684400.125`,
compares the strings, and runs under both `-m32` and `-fsanitize=float-cast-overflow`.

---

## 7. The C89 formatter that no gate ever runs

**A-10, medium where it is selected, measured, already recorded (def-75, open).** Strict C89 has no
`snprintf`, so jichi carries its own: `jc_snprintf` forwards to the C99 function when a configure probe finds
it, and otherwise uses a hand-written fallback (`src/platform/jc_snprintf.c`). The C99 path is used everywhere
jichi is measured and is correct — reviewer A confirmed its truncation return values match C99 (a formatted
write that would need 13 bytes into an 8-byte buffer returns 13 and NUL-terminates at 7, exactly as C99
specifies).

The fallback is a different story. It handles only bare conversions (`%d %u %x %s %c %f` and their `l` forms).
For anything else — `%.*s`, `%5d`, `%-10s`, `%02x`, `%g` — it falls to a branch that prints the specifier's
two characters literally and *consumes no argument*. In C's variadic ABI, that means every following argument
is read from the wrong slot. Reviewer A measured: built against the fallback, a call with `%.*s` followed by
`%s` crashes, because `%.*s` consumed nothing and the `%s` then read an `int` as a `char *` and dereferenced
it. The tree uses the unsupported specifiers at dozens of sites. On any platform where the probe mis-selects
the fallback — and that has happened, on a mis-probing old libc and on a Cygwin build whose linker could not
write the probe's test binary (M476) — these crash or print garbage.

Two things make this worse than it looks. The header comment claims width and precision are "limited"
(`include/jc_snprintf.h:12`); they are absent (A-14). And **no gate ever runs the fallback** — `make ci`
compiles it but never executes it, so a crash in it would reach a user before a test. The fix is to implement
the missing specifiers, or at minimum make an unsupported conversion consume its argument so later arguments
stay aligned, and to add a gate stage that runs the unit suite against the fallback.

---

## 8. Smaller findings and what is already fixed

Three more low findings, each a bounded-input hygiene issue:

- **A-11 (low):** `jc_group_num` (`src/util/jc_str.c:259`) stages a number in a 40-byte buffer and then copies
  it out; for a huge value the staged length is the *would-be* length, so a caller that passed a buffer larger
  than 41 bytes would make the copy loop read past the staging buffer. No current caller does (all pass 40),
  so it is latent, but it is keyed on a caller's size argument. Clamp the staged length.
- **A-12 (low):** the test-output scraper accumulates a line number as `n = n*10 + digit` with no overflow
  guard (`src/util/jc_testparse.c:89`), so a test runner printing `file.c:999999999999999999999: error`
  overflows `long` (undefined). Guard it the way `jc_cli.c:28` and `jc_lsp_proto.c:72` already do.
- **A-05 (medium, class only; detail internal):** the parser that recovers a tool call a model wrote as
  markdown (`jc_toolcall_recover`, used only when the `textToolCalls` option is on) reads past the end of the
  reply buffer for one particular reply shape. Reviewer A caught it with the address sanitizer
  and pinned the arithmetic (`src/tools/jc_tool.c:977-978`); in a normal build the read lands in allocator
  slack, so the effect is usually nothing, but the index arithmetic is wrong. The fix is to treat a fence with
  no following line as an empty body; add the case to the recovery unit test under the sanitizer, and add this
  parser to the fuzz registry (it had none — see below).

And a great deal is **done well**, with evidence:

- The JSON depth bound (section 3) is a model fix, and committed fuzz-corpus files keep it found.
- The number clamp was propagated to three sites when found (`number_to_int`); A-02 is the *remaining*
  callers, not a regression.
- The framing caps are real and each cites its incident (SSE M24, LSP M609, MCP/ACP M659).
- `jc_utf8.c` splits cleanly into a lenient decoder for the line editor and a strict RFC-3629 validator for
  truncation and sanitising; overlongs, surrogates and values above U+10FFFF are all rejected
  (`src/util/jc_utf8.c:105-152`). The request body is sanitised to valid UTF-8 before it goes on the wire
  (M191, `src/provider/jc_provider.c:199`), which closed a class where a tool result cut mid-character wedged
  every later turn.
- `jc_base64_decode` bounds every write and is fuzzed round-trip.
- `jc_lsp_framer_pop` fails *closed*: an overflowing or oversized `Content-Length` returns a sentinel and
  resyncs rather than trusting a wrapped value.
- Parsers return a sentinel and let the agent turn it into a `tool` message the model can act on, rather than
  aborting — errors are values, not control flow.

The project also has a **fuzz harness** (`tests/fuzz/`, M123; coverage-guided via libFuzzer, M269) with targets
for JSON, SSE, YAML, the providers, patch, UTF-8, jsonrepair, base64 and the embeddings reader. It is a real
strength. The gaps are exactly the parsers with no target: the markdown tool-call recovery (A-05 would have
been found immediately), the argument normalisation, the protocol *reply* interpreters, and the diff. The
pattern to adopt is the project's own: add the fuzz target in the same commit as a new parser.

---

## 9. Recommendations, by value for effort

1. **Normalise tool arguments once, before the fence (A-01).** Factor the repair + unwrap + un-stringify
   sequence into one helper and run it before `call_out_of_scope_path` and the parallel-write scope check, or
   move normalisation above both the fence and the executor. This closes a fence bypass. Effort **M**. (Shared
   with chapter 03.)
2. **Add a depth bound to the YAML parser (A-04).** One counter at `parse_node`, refusing past 256, mirroring
   the JSON fix. Closes a stack-overflow-from-a-repo. Effort **S**.
3. **Fix the two 32-bit memory-safety bugs (A-03, A-09/L-04).** Overflow-check the diff table size before
   multiplying; print numbers with `%.0f`/`%.17g` instead of `(long)d`/`%g`. Each is a few lines and removes a
   crash and a silent-wrong-number on every 32-bit board. Effort **S** each.
4. **Export a clamped `jc_json_get_int`/`_long` and sweep the ~115 casts (A-02), with a lint to hold it.**
   Turns a whole class of undefined behaviour into defined clamping. Effort **M**.
5. **Fix the recovery over-read and give it a fuzz target (A-05).** Effort **S**.
6. **Run the C89 formatter fallback in `make ci`, and fix its argument-consumption bug (A-10).** A fallback no
   gate runs is a fallback nobody has seen work. Effort **M**.
7. **Require only whitespace after a JSON root (A-08); guard the testparse overflow (A-12); clamp
   `jc_group_num` (A-11); decide the duplicate-key rule (A-13).** Small hardening, each **S**.
8. **Document that the SSE/field byte caps are not memory caps (A-06), and consider a per-reply element cap.**
   Effort **S** for the note, **M-L** for a cap.

---

## 10. Read further

- [CONFIG_TUTORIAL.md](../CONFIG_TUTORIAL.md), [LOW_MEMORY.md](../LOW_MEMORY.md),
  [HARDENING.md](../HARDENING.md): the references this chapter rests on. *Primary for jichi.*
- [Annai 6](../reading/annai-06-the-wire.md) and
  [Fukabori 8](../reading/fukabori-08-streaming-and-the-no-buffering-invariants.md): the request/response and
  streaming walkthroughs. *Primary for jichi.*
- [Review 7, "The wire"](2026-10-08-review-07-the-wire.md), the 2026-10-08 review of the same files: the prior
  pass many of these findings extend. *Primary for jichi.*
- RFC 8259, *The JavaScript Object Notation (JSON) Data Interchange Format*: the grammar the parser implements,
  including that numbers have no size limit (section 3's problem) and that duplicate keys are allowed but their
  handling is unspecified (A-13). *Primary.*
- The WHATWG HTML Living Standard, "Server-sent events", section 9.2: the SSE line-ending and field rules
  jichi's framer follows. *Primary.*
- SEI CERT C Coding Standard, rules INT31-C (integer conversion) and FLP34-C (float-to-integer conversion):
  the named rules A-02, A-03 and A-09 break. *Primary.* (In [BIBLIOGRAPHY.md](../BIBLIOGRAPHY.md).)
- [chapter 03, processes and fences](2026-10-17-hardening-03-processes-and-fences.md): the full treatment of
  the edit-scope bypasses (A-01 and the `..` sibling) and the git-config exec vectors. *Primary for jichi.*
