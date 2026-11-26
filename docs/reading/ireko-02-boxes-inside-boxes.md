# Ireko 2 -- Boxes inside boxes: the recursive-descent parser

*[入れ子（いれこ）*Ireko* — nested boxes](IREKO.md) · chapter 2 of 7*

## Why this exists

Chapter 1 showed the tree. This chapter shows how text becomes a tree, which is the
job of a **parser**. jichi's is the 547 lines of `src/json/cJSON.c` from
`struct parse_ctx` to the end of `cJSON_Parse`, comments included. This chapter
reads all of them except strings and numbers, which get chapters 3 and 4.

Every JSON message a model, an MCP server, a language server or an editor sends passes through
these lines, so the parser's limits are jichi's limits. Four of them have a
story in the record: how **deep** a document may nest (M472), what counts as **one**
document (M1078), how **many** values one parse may allocate (M1089, M1094), and what
a **duplicated key** means (M1113).

It is a **recursive-descent** parser. That sounds harder than it is: one function per
kind of value, and the function for a box calls the function for a value, which may
be another box. The nesting in the text becomes nesting in the calls.

## The shape

The grammar, as pseudo code (`[ ]` is optional, `{ }` repeats):

```
value  := string | number | object | array | true | false | null
object := '{' [ string ':' value { ',' string ':' value } ] '}'
array  := '[' [ value { ',' value } ] ']'
```

The functions that follow it. The arrows marked "recursion" are where a box opens
another box:

```
cJSON_Parse
  └─ parse_value          reads ONE character, then chooses:
       '"'          → parse_string, then one node
       '{'          → parse_object ─┬─ parse_string   (the key)
                                    └─ parse_value    (the member)   ← recursion
       '['          → parse_array  ─── parse_value    (each element) ← recursion
       't' 'f' 'n'  → parse_literal
       anything else → parse_number
```

## The cursor: `struct parse_ctx` and `skip_ws`

```c src/json/cJSON.c
struct parse_ctx {
    const char *p;    /* current position */
    const char *end;  /* one past the last byte */
    int         ok;
    int         depth; /* container nesting; bounds stack use (M472) */
    size_t      nodes; /* parse-time allocations; bounds heap use (M1089) */
};
```

`p` is the cursor, the next byte to read. `end` is one past the last byte, and every
read checks `c->p < c->end` first, so the parser never reads past its input. `ok`
starts at 1; a function that finds something wrong sets it to 0 and returns NULL, and
its callers give up too. That is "errors are values" at its smallest: nothing exits
the program and nothing jumps out of the parser; one flag carries the verdict. `depth` and `nodes` are the two limits this chapter is about. One
`struct parse_ctx` lives in `cJSON_Parse`, and every function gets a pointer to it,
`struct parse_ctx *c`, so they all move one cursor.

Between tokens, `src/json/cJSON.c:skip_ws` moves the cursor past a space, a tab, a
newline or a carriage return. That is JSON's whole list of whitespace, so a form feed
between two values is an error.

> **C sidebar — `static` functions.** Every `parse_*` function, and `skip_ws`, is
> declared `static`: visible only inside this file. The rest of jichi sees only the
> functions without it, such as `cJSON_Parse`, and so do the "Try it" programs.

## One character decides: `parse_value`

```c src/json/cJSON.c:parse_value
static cJSON *parse_value(struct parse_ctx *c)
{
    skip_ws(c);
    if (c->p >= c->end) {
        c->ok = 0;
        return NULL;
    }
    switch (*c->p) {
        case '"': {
...
        }
        case '{':
        case '[': {
...
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
        case 't': return parse_literal(c, "true",  cJSON_True);
        case 'f': return parse_literal(c, "false", cJSON_False);
        case 'n': return parse_literal(c, "null",  cJSON_NULL);
        default:  return parse_number(c);
    }
}
```

The whole decision is `switch (*c->p)`: one character, looked at and not yet
consumed. A `char` is a small integer, so `switch` can jump on it, and two labels in
a row, `case '{':` and `case '[':`, share one body. JSON was designed so that one
character is enough: a `"` can only start a string, a `{` only an object, a `t` only
`true`. The parser never backs up to try another reading. In textbook terms, this is
recursive descent with one character of look-ahead.

Three details deserve a second look.
- **The string case** (elided above) parses the text first and makes the node
  second. If the node cannot be made, it frees the text it already holds.
- **The box case** is the only place where the parser calls itself, through
  `parse_object` or `parse_array`. It counts `depth` up before the call and down
  after it, so `depth` always says how many boxes are open.
- **The `default` arm** sends every other character to `parse_number` without
  checking that it can start a number. One that cannot, such as `x`, is refused,
  because `parse_number` collects nothing. But `parse_number`'s idea of a number is
  wider than JSON's: "Try it" measures `[e]` parsing as `[0]`. Chapter 4 reads why.

## The small boxes: `parse_literal`

`true`, `false` and `null` are fixed words, and `src/json/cJSON.c:parse_literal`
reads all three. It checks that the word fits in the bytes left
(`(jc_psize)(c->end - c->p) < n`; `jc_psize` is this file's name for `size_t`),
compares with `strncmp`, makes a node, and moves the cursor past the word. What it
does **not** check is the byte after the word: `truex` matches `true` here. The
refusal comes one step later, from the caller: an array expects `,` or `]` and finds
`x`, and at the top level `cJSON_Parse` refuses anything after the root.

## The big boxes: `parse_array` and `parse_object`

`src/json/cJSON.c:parse_array` makes its node, steps over the `[`, handles `[]` on
its own, and then loops:

```c src/json/cJSON.c:parse_array
    for (;;) {
        cJSON *item;
        skip_ws(c);
        item = parse_value(c);
        if (!c->ok || item == NULL) {
            c->ok = 0;
            cJSON_Delete(arr);
            return NULL;
        }
...
        skip_ws(c);
        if (c->p < c->end && *c->p == ',') {
            c->p++;
            continue;
        }
        if (c->p < c->end && *c->p == ']') {
            c->p++;
            return arr;
        }
        c->ok = 0;
        cJSON_Delete(arr);
        return NULL;
    }
```

One turn of the loop: parse a value, link it after the last one (the elided lines;
the first element becomes `child`), then look at **one character**. A `,` means
another element, a `]` ends the array, anything else is an error. It is the
one-character rule of `parse_value` again, applied after each element.

Both error exits call `cJSON_Delete(arr)`. The array already owns every element
linked so far, so that one call frees them all (chapter 1's rule 1), and a parse that
fails halfway leaves nothing behind. `[1,]` fails here: after the comma,
`parse_value` meets `]`, the `default` arm hands it to `parse_number`, which collects
nothing. A trailing comma is not JSON, and this parser agrees. (jichi's own configuration files still accept one, because `config_parse_file` strips trailing
commas with `jc_jsonc_strip` before it parses; chapter 7.)

`src/json/cJSON.c:parse_object` is the same loop with a key before each value:

```c src/json/cJSON.c:parse_object
        key = parse_string(c);
...
        if (c->p >= c->end || *c->p != ':') {
            free(key);
...
        item = parse_value(c);
        if (!c->ok || item == NULL) {
            free(key);
...
        item->string = key;
```

The key is stored on the member only after the value has been parsed. Until then it
belongs to this function alone, so the two error exits between reading the key and storing it, a missing `:` and a value that
fails, `free(key)` before they delete the object. The last exit, a missing `,` or `}`, does not: by
then the key belongs to the member, and `cJSON_Delete(obj)` frees it with the member. Open the function and check that every exit frees exactly what the
tree does not own yet. Note also what is **not** here: a check whether the key was
used before. Every member is appended, duplicates included.

## Recursion is the nesting

Follow `{"a":[1,{"b":null}]}`. When the parser reads `null`, all of these calls are
running, each waiting for the one below it to return:

```
cJSON_Parse
  parse_value        sees '{'   depth 0 -> 1
    parse_object     key "a"
      parse_value    sees '['   depth 1 -> 2
        parse_array  element 0 was 1; now element 1
          parse_value      sees '{'   depth 2 -> 3
            parse_object   key "b"
              parse_value  sees 'n'
                parse_literal "null"
```

Each open box is one more `parse_value` and one more `parse_object` or `parse_array`
on the stack. Each closing bracket is a `return`, and `depth` goes back down.

> **C sidebar — recursion and the stack.** A call reserves a **stack frame**: room
> for the function's locals, its arguments and the address to return to. A function
> that calls itself, directly or through another, holds one frame per call that has
> not returned. The stack has a fixed size; on this machine `ulimit -s` says 8192,
> that is 8 MB, and a small board may give a process far less. When the frames no
> longer fit, the process touches memory it does not own, and the kernel kills it
> with `SIGSEGV`, a segmentation fault.

## How deep may it go? (M472)

Put the two together. The input decides how many boxes are open at once, and every
open box costs stack, so **the input decides how much stack the parser uses**. Before
M472 nothing stopped it. The comment above the limit:

```c src/json/cJSON.c
/* Hard bound on container nesting (M472). The input controls how deep this
 * parser recurses, so without a bound the input controls the stack: measured
 * SIGSEGV at ~75,000 levels (150 KB of '[') on an 8 MB stack, ~6,000 (12 KB) at
 * 512 KB, and ~2,000 (4 KB) at 128 KB -- the small-stack rows LOW_MEMORY.md
 * ships to, where a 4 KB message was enough to kill the agent. Reachable from
...
#define JC_JSON_MAX_DEPTH 256
```

A model that wrote four kilobytes of `[` as its tool-call arguments could crash jichi
on a small machine. The fix is the check in `parse_value`. `depth` is tested before it
is counted up, so 256 nested boxes parse and the 257th is refused. "Try it" measures
both, and then the crash itself, with the limit removed in a copy.

Three choices in the fix are worth learning from.
- **Where the counter lives.** The audit that found the defect suggested counting in
  `parse_object` and `parse_array`
  ([the audit, H4](../analysis/2026-08-17-source-hardening-audit.md)). The fix put it
  in `parse_value`, and its comment, elided in the quote above, says why: each container function has several return
points (five in `parse_array`, seven in `parse_object`; the comment says five for both), and "a
counter decremented at five of them is a counter that leaks at the sixth someone adds".
- **Why 256.** "Generous by two orders of magnitude", says the comment: real
  provider, MCP and LSP payloads nest under about 12. A limit must stop the attack
  and nothing legitimate.
- **Why no other limit helped.** jichi already capped one SSE field at 4 MB. That bounds bytes, and the crash needed far fewer: the audit's "4 KB" at a 128 KB stack counts
about 2,000 levels with their closing brackets, but the parser dies on the way down, so about 2 KB
of `[` is enough (the "150 KB of `[`" in the comment counts both brackets too): "a byte cap and a depth cap are not
  substitutes". The same comment says why the fuzzer that runs over this parser never
  found it: it hunts for inputs that reach new code, and `[[[[` reaches no new code
  after the second `[`. Two deep inputs now sit in `tests/fuzz/corpus/json/` for the
  fuzzer to replay.

`cJSON_Delete` and the printer's `print_value` recurse once per level too. The
printer's comment explains why it has no limit of its own: a tree jichi prints is
either built at a nesting jichi's source fixes, or parsed, and a parsed tree is now at
most 256 deep.

## One document, and nothing after it (M1078)

The entry point ties the pieces together:

```c src/json/cJSON.c:cJSON_Parse
cJSON *cJSON_Parse(const char *value)
{
...
    if (value == NULL) {
        return NULL;
    }
    g_cjson_last_parse_refused_by_cap = 0; /* M1094 (G3) */
    c.p = value;
    c.end = value + strlen(value);
...
    root = parse_value(&c);
    if (c.ok) {
...
        skip_ws(&c);
        if (c.p != c.end) {
            c.ok = 0;
        }
    }
    if (!c.ok) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return NULL;
    }
    return root;
}
```

It sets up the context, parses **one** value and, since M1078, checks that only
whitespace is left. Before M1078 it returned after the first value. The record of the fix (`docs/ROADMAP.md`, M1078) lists four shapes the hardening review's finding
A-08 measured as silently accepted: `{"a":1}}`,
`{"a":1}{"b":2}`, `{"path":"x"} garbage` and `[1,2]]`. It also said why the first
matters ([the A-08 paragraph](../analysis/2026-10-17-hardening-02-input-and-memory.md)):
tool-call arguments with one brace too many were accepted and echoed back onto the
wire, where a strict server rejects the whole request. The code's own comment, elided
above, ends: "Upstream cJSON accepts the trailing data; jichi refuses it on purpose."

A stricter parser can break its callers, so the record (`docs/ROADMAP.md`, M1078)
shows how this change was checked. The callers were read one by one, to make sure
none fed the parser part of a document. 2,308 real tool-call argument strings from
recorded runs were counted, and none had a valid root followed by more content. Two
endings that must still parse became tests: a config line ending in `\r\n` and a
JSONL line with its newline. Both are whitespace, so `skip_ws` eats them.

Two more things in this function. **`c.end = value + strlen(value)`:** the parser
takes a C string, so it sees the text up to the first NUL byte and nothing after it.
A document followed by a NUL and junk parses, because the junk is invisible ("Try it"
measures it). **The error path** deletes whatever was built: the caller gets a whole
tree or NULL, never half a tree.

## How many boxes? (M1089, M1094)

The depth limit bounds the stack, not the **heap**. A document can be shallow and
still huge: `[0,0,0,...]` costs two bytes of text and one node per element, 72 bytes
on the machine chapter 1 measured. The comment above the second limit:

```c src/json/cJSON.c
/* Hard bound on the number of nodes ONE parse allocates (M1089, A-06).
 * M472's depth cap bounds nesting, but not node count: every value is a
 * malloc'd 64-byte node, so a dense array of small numbers is one node per
 * element, and a large-but-valid reply expands ~40x its bytes into tree
 * memory while it is parsed. The lead confirmed on head: a 1,000,000-element
 * array of numbers (1.9 MB) parses into ~78 MB peak RSS, delta 77,952 KB.
...
#define JC_JSON_MAX_NODES 1000000
```

On a board with 256 MB, one large but valid reply could use up the memory while the
4 MB byte cap "held". `docs/LOW_MEMORY.md` now says it in one line: "A byte cap is not
a memory cap." The fix counts nodes, and every node the parser makes comes from one
function:

```c src/json/cJSON.c:parse_new_item
static cJSON *parse_new_item(struct parse_ctx *c)
{
    cJSON *node;
    if (c->nodes >= g_cjson_max_nodes) {
        c->ok = 0;
        g_cjson_last_parse_refused_by_cap = 1; /* M1094 (G3) */
        return NULL;
    }
    node = cJSON_New_Item();
    if (node != NULL) {
        c->nodes++;
    }
    return node;
}
```

- **Every value is one node.** A key is not a node, it is stored on its member. A long
  string is one node, so this cap does not bound strings (chapter 3 sizes them).
- **The count is per parse.** With the default of 1,000,000, a document of exactly
  1,000,000 nodes parses and one more is refused, as a whole: `c->ok = 0`, and every
  container above deletes itself on the way out.
- **Only the parser is capped.** jichi builds its own request bodies with the public
  constructors, which call `cJSON_New_Item` directly.
- **Why 1,000,000.** The largest legitimate parse the comment names is an embeddings
  reply of about 524,000 nodes. The cap clears it with nearly twice the room.

`g_cjson_max_nodes` is a variable so that a test can lower it with
`cJSON_jc_set_max_nodes`. The header says no jichi code path calls that, and a search
agrees: its only callers are in `tests/`. "Try it" uses it the same way.

**What the reader does with a refusal (M1094).** A refusal is an error value, and an
error value helps only if someone reads it. M1089's effect check (a second look at the fix by what it does to a whole run, not by its unit
test; the M1094 entry of `docs/ROADMAP.md`) found that nobody did. Both streaming readers turned a NULL from `cJSON_Parse` into a plain `return`,
and a NULL could not say *why*. A reply that should have read `HELLO WORLD` read
` WORLD`, with exit status 0 and no mark that anything was missing.

M1094 added one flag. `cJSON_Parse` clears it and only `parse_new_item`'s refusal
sets it, so it tells "the cap said no" apart from "this is not JSON".
`cJSON_jc_parse_refused_by_cap()` reads it, and both readers now ask:

```c src/provider/jc_provider_openai.c:oa_on_event
    root = cJSON_Parse(ev->data);
    if (root == NULL) {
...
        if (cJSON_jc_parse_refused_by_cap()) {
            s->truncated_reply = 1;
...
        }
        return;
    }
```

`truncated_reply` reaches `src/provider/jc_provider.c:jc_prov_flush`, which marks the
finished message `truncated`, and `truncated_by_cap` unless the server reported its
own length limit too. What the model hears depends on the reply. If the cut reply made no tool call and continuing is
allowed (in a headless top-level run by default, and in every delegate; `continueCutReply` decides
otherwise), `src/chat/jc_agent.c` sends one nudge naming jichi's own size limit as the cause. If a
tool call's arguments then fail to parse, the tool error says jichi's per-call limit cut them. In
any other case, an interactive reply among them, the model is not told; the mark stays on the
message and in the telemetry. jichi also prints one `[jichi warn]` line to standard error (unless
`--quiet`), which is not the telemetry log.

What the flag is, and is not. It is **one global variable** for the whole process,
which is safe because only one thread ever calls `cJSON_Parse`: jichi's own code creates no thread
(a search of `src/` finds no `pthread_create`), and the libcurl it links may start a short-lived
thread to look up a host name, but never touches JSON there. It is **reset after the NULL check**, so `cJSON_Parse(NULL)` leaves
it as the previous parse set it; measured for this chapter with cap 1: parse `[1]`
(flag 1), then `cJSON_Parse(NULL)` (flag still 1). The readers never pass NULL. It
answers "did the cap stop this parse", **not** "was the document well formed"; "Try
it" shows a broken document that reads 1. And it is **not a mark for every loss**: an
event that is not JSON is still dropped without one, unless it is an `error` event (the elided branch of the OpenAI reader; its code comment says M909,
the number it had on its branch, and `docs/ROADMAP.md` records it as M914, "an error inside a 200
stream fails the call, both dialects"). The non-streamed reader,
`src/provider/jc_provider_openai.c:oa_parse_full`, was loud already: it returns
`JC_ERR_PARSE`.

## Looking a key up (M1113)

Every `jc_json_get_*` helper ends in this function:

```c src/json/cJSON.c:cJSON_GetObjectItemCaseSensitive
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string)
{
    cJSON *child;
    cJSON *match = NULL;
    if (object == NULL || string == NULL) {
        return NULL;
    }
    child = object->child;
    while (child != NULL) {
        if (child->string != NULL && strcmp(child->string, string) == 0) {
            match = child;   /* remember, do not return on the first */
        }
        child = child->next;
    }
    return match;
}
```

A lookup walks the members along `next` and compares each key with `strcmp`. There is
no index and no hash table, so an object of `n` members costs `n` comparisons; jichi's
objects are small, and the code accepts that trade. Two things surprise people.
- **`cJSON_GetObjectItem` is case-sensitive too.** In this file
  `src/json/cJSON.c:cJSON_GetObjectItem` is one line that calls the function above.
  `"Path"` is not found by `"path"`. This surprises people who know upstream cJSON: there `cJSON_GetObjectItem` ignores case (measured
with upstream 1.7.17, `{"Path":"a.txt"}` looked up with `"path"` gives `"a.txt"`), and only
`cJSON_GetObjectItemCaseSensitive` matches exactly. Upstream also returns the **first** of two
duplicates.
- **It keeps walking after a match.** `match = child` remembers the member and goes
  on, so the **last** member with that key wins. Since M1113 that is the duplicate-key
  policy: last write wins. It also means every lookup walks the whole object.

Why does a parser need such a policy? Because `parse_object` keeps every member, and
JSON's standard, RFC 8259, says only that names "SHOULD be unique". The comment above
the function says why duplicates are resolved here and not during the parse: replacing
a member while parsing would cost a search per member, O(n²) per object. So the tree holds both members, printing prints both, and a loop over the members sees both. A
lookup chooses the last. One other function in this file chooses too, the other way:
`src/json/cJSON.c:cJSON_ReplaceItemInObject` replaces the **first** member with the key. Measured
for this chapter: parse `{"model":"a","model":"b"}`, replace `model` with `"c"`, and the tree
prints `{"model":"c","model":"b"}` while a lookup still returns `"b"`, so the replacement is
invisible to every reader. jichi's own objects never carry a duplicate, so this bites only a tree
parsed from outside: `src/config/jc_config.c:jc_config_merge_json` looks a key up and then
replaces it, and with a key written twice in the global config, a project's override of it is
lost. "What went wrong here" tells why the lookup's choice is about security. "What went wrong here" tells why that choice is about security.

## Try it

You need a C compiler. Every file you write goes into a throwaway directory, and
`src/json/cJSON.c` is compiled straight into each program.

**1. A nested parse.** `find` reads a document on standard input, follows a path of
keys and array indexes into it, and prints what it finds. `CAP=n` in its environment
sets the node cap (step 4).

```sh
# in the jichi checkout
T=$(mktemp -d)        # a throwaway directory; keep this shell open for the steps below
cat > "$T/find.c" <<'END_OF_C'
/* find.c -- parse standard input, follow a path of keys and indexes into
 * it, and print what is there. CAP=n in the environment sets the node cap. */
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"

static char buf[4194304];   /* 4 MiB, room for the largest test below */

int main(int argc, char **argv)
{
    size_t n;
    cJSON *root;
    const cJSON *at;
    char *text;
    int i;

    n = fread(buf, 1, sizeof buf - 1, stdin);
    buf[n] = '\0';
    if (getenv("CAP") != NULL) {
        cJSON_jc_set_max_nodes((size_t)strtoul(getenv("CAP"), NULL, 10));
    }
    root = cJSON_Parse(buf);
    if (root == NULL) {
        printf("refused (%lu bytes in; refused by the node cap: %d)\n",
               (unsigned long)n, cJSON_jc_parse_refused_by_cap());
        return 1;
    }
    at = root;
    for (i = 1; i < argc && at != NULL; i++) {
        if (cJSON_IsArray(at)) {
            at = cJSON_GetArrayItem(at, atoi(argv[i]));
        } else {
            at = cJSON_GetObjectItem(at, argv[i]);
        }
    }
    text = (at != NULL) ? cJSON_PrintUnformatted(at) : NULL;
    printf("%.60s\n", (text != NULL) ? text : "not found");   /* at most 60 chars */
    cJSON_free(text);
    cJSON_Delete(root);
    return (at != NULL) ? 0 : 1;
}
END_OF_C
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/find" "$T/find.c" src/json/cJSON.c -lm
D='{"a": [1, {"b": null}], "c": "x"}'
printf '%s' "$D" | "$T/find"
printf '%s' "$D" | "$T/find" a
printf '%s' "$D" | "$T/find" a 1
printf '%s' "$D" | "$T/find" a 1 b
```

The compiler prints nothing. The program prints:

```
{"a":[1,{"b":null}],"c":"x"}
[1,{"b":null}]
{"b":null}
null
```

Each step opens one more box, with `cJSON_GetObjectItem` for a key and
`cJSON_GetArrayItem` for an index. The `null` is the one from the call-stack picture.

**2. One document, and nothing after it.**

```sh
# in the jichi checkout, the same shell
printf '{"a":1}}' | "$T/find"
printf '{"a":1} x' | "$T/find"
printf '{"a":1}\r\n' | "$T/find"
printf '{"a":1}\0junk' | "$T/find"
printf '[truex]' | "$T/find"
printf '[1,]' | "$T/find"
printf '[x]' | "$T/find"
printf '[e]' | "$T/find"
```

```
refused (8 bytes in; refused by the node cap: 0)
refused (9 bytes in; refused by the node cap: 0)
{"a":1}
{"a":1}
refused (7 bytes in; refused by the node cap: 0)
refused (4 bytes in; refused by the node cap: 0)
refused (3 bytes in; refused by the node cap: 0)
[0]
```

The extra `}` and the trailing ` x` are refused (M1078); `\r\n` is whitespace. The NUL
case is accepted: 12 bytes went in and the parser saw 7. `truex` is refused by the
array's next check, not by `parse_literal`. `[1,]` and `[x]` are refused because
`parse_number` collected nothing, and `[e]` comes back as `[0]`.

**3. The depth limit, and the crash it prevents.** `deep N` builds `N` nested arrays
and parses them. It prints the size before the parse and the verdict after, so a crash
shows up between the two.

```sh
# in the jichi checkout, the same shell
cat > "$T/deep.c" <<'END_OF_C'
/* deep.c -- build N nested arrays, [[[...]]], and hand them to cJSON_Parse. */
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"

int main(int argc, char **argv)
{
    long n = (argc > 1) ? atol(argv[1]) : 1;
    long i;
    char *doc;
    cJSON *root;

    doc = (char *)malloc((size_t)n * 2 + 1);
    if (doc == NULL) {
        return 2;
    }
    for (i = 0; i < n; i++) {
        doc[i] = '[';
        doc[n + i] = ']';
    }
    doc[n * 2] = '\0';
    printf("%ld levels (%ld bytes): ", n, n * 2);
    fflush(stdout);
    root = cJSON_Parse(doc);
    printf("%s\n", (root != NULL) ? "parsed" : "refused");
    cJSON_Delete(root);
    free(doc);
    return (root != NULL) ? 0 : 1;
}
END_OF_C
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/deep" "$T/deep.c" src/json/cJSON.c -lm
"$T/deep" 256
"$T/deep" 257
"$T/deep" 70000
```

```
256 levels (512 bytes): parsed
257 levels (514 bytes): refused
70000 levels (140000 bytes): refused
```

Now see what the limit is for. The next block writes a **copy** of `cJSON.c` into the
throwaway directory with the limit raised to a billion, the parser as it was before
M472, and builds `deep` against the copy. The checkout is not changed.

```sh
# in the jichi checkout, the same shell
sed 's/^#define JC_JSON_MAX_DEPTH 256$/#define JC_JSON_MAX_DEPTH 1000000000/' src/json/cJSON.c > "$T/unbounded.c"
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/deep_unbounded" "$T/deep.c" "$T/unbounded.c" -lm
"$T/deep_unbounded" 65000
"$T/deep_unbounded" 70000
echo "exit status $?"
(ulimit -s 128; "$T/deep_unbounded" 1000)
(ulimit -s 128; "$T/deep" 1000)
```

```
65000 levels (130000 bytes): parsed
70000 levels (140000 bytes): Segmentation fault (core dumped)
exit status 139
1000 levels (2000 bytes): Segmentation fault (core dumped)
1000 levels (2000 bytes): refused
```

Measured for this chapter on the default 8 MB stack: 65,000 levels parsed, and 70,000
killed the process. The verdict was never printed, so it died inside `cJSON_Parse`.
Exit status 139 is 128 + 11, and signal 11 is `SIGSEGV`. That puts one level at
roughly 120 to 130 bytes of stack for this build. The parentheses run a subshell, so
`ulimit -s 128` gives only that subshell a 128 KB stack, the smallest row of M472's
table. There, 1,000 levels, **2,000 bytes** of input, kill the unbounded parser, and
the real one refuses them. Your thresholds will differ with your compiler and its
flags; the shape will not. The crash message comes from your shell, so its wording
depends on the shell and on the system's core-dump settings.

**4. The node cap.** First a cap of 3 nodes, then the real default.

```sh
# in the jichi checkout, the same shell
printf '[1,2]' | CAP=3 "$T/find"
printf '[1,2,3]' | CAP=3 "$T/find"
printf '[1,2,' | CAP=3 "$T/find"
printf '[1,2,3' | CAP=3 "$T/find"
awk 'BEGIN { printf "["; for (i = 1; i < 999999; i++) printf "0,"; printf "0]" }' | "$T/find"
awk 'BEGIN { printf "["; for (i = 1; i < 1000000; i++) printf "0,"; printf "0]" }' | "$T/find"
```

```
[1,2]
refused (7 bytes in; refused by the node cap: 1)
refused (5 bytes in; refused by the node cap: 0)
refused (6 bytes in; refused by the node cap: 1)
[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
refused (2000001 bytes in; refused by the node cap: 1)
```

`[1,2]` is three nodes: the array and two numbers. `[1,2,3]` needs a fourth and is
refused, and the flag says why. `[1,2,` is refused for being broken, and the flag says
0. `[1,2,3` is broken *and* too big: the parser reads left to right, reaches the cap
at the fourth node, and never gets to the missing `]`, so the flag says 1. The `awk`
lines build arrays of 999,999 and 1,000,000 zeros. With the array's own node, that is
exactly 1,000,000 nodes, which parse (the program shows the first 60 characters), and
1,000,001, which do not: about 2 MB of valid JSON, refused at the default cap.

**5. Duplicate keys, and the case of a key.**

```sh
# in the jichi checkout, the same shell
printf '{"path":"BLOCKME.txt","content":"x","path":"ok.txt"}' | "$T/find"
printf '{"path":"BLOCKME.txt","content":"x","path":"ok.txt"}' | "$T/find" path
printf '{"Path":"a.txt"}' | "$T/find" path
rm -r "$T"            # done: remove the throwaway directory
```

```
{"path":"BLOCKME.txt","content":"x","path":"ok.txt"}
"ok.txt"
not found
```

The tree keeps both `path` members, and printing gives both back. A lookup returns the
last. `Path` with a capital P is a different key.

## What went wrong here, and what it teaches

### Two readers, two answers (M1113)

The arguments in step 5 are not invented. They are the shape of a fence bypass the
record measured before M1113, on scratch clones of the tree with a mock model
(`docs/ROADMAP.md`, M1113 and M1113-b).

jichi lets a user install a **hook**: a program that runs before a tool call and may
block it. The hook gets the call as JSON on its standard input, built by
`src/chat/jc_hooks.c:build_stdin`, which parses the model's arguments and attaches the
tree:

```c src/chat/jc_hooks.c:build_stdin
        ti = (args_json != NULL) ? jc_json_parse(args_json) : NULL;
        if (ti != NULL) {
            cJSON_AddItemToObject(o, "tool_input", ti);
        }
```

The tree held both `path` members, so the hook's input did too. The hook read it the
way most JSON readers do, took the **last** `path`, `ok.txt`, and allowed the write.
jichi's own lookup, before M1113, returned the **first** match, so the tool wrote
`BLOCKME.txt`: the file the hook existed to protect. Each reader was reasonable on its
own. Together they made a fence with a door in it.

The fix changed one function: the lookup now returns the last match, so the hook and
the tool resolve the same member. The parser and the printer did not change; the
duplicate is still in the tree and in the hook's input, as step 5 shows. What holds the fix is an invariant, written into the comment above the lookup: every value jichi
reads goes through that one function. It holds for every read by key, and those are the reads the
built-in tools make. It is not universal: `src/tools/jc_tool_user.c:user_tool_run` walks a
user-defined tool's arguments to export each one as `JICHI_ARG_<NAME>`, and
`cJSON_ReplaceItemInObject` picks the first match. Code that walks the members instead of looking
a key up is where the hook and jichi could disagree again. `tests/smoke/hooks.sh` now drives the bypass
with a mock model and checks that the hook's verdict and the written file agree.

The lesson is about agreement, not duplicates. When two programs read the same bytes,
they must settle every ambiguity the same way, or one can be told one story while the
other acts on another. Before you call a format "parsed", ask what your parser does
where the standard leaves a choice open, and what the other readers of the same bytes
do there.

### A refusal nobody heard (M1094)

The node-cap section told it: the refusal was right, and the silence was not. The fix
did not touch the decision; it made the decision visible downstream. An error value is
only as good as the code that reads it. When you add a refusal, follow the NULL to
every caller and check what each one does with it.

## Check yourself

1. In `[-]`, `parse_value` meets the `-`. Which function runs, and what does step 2's
   `[e]` lead you to expect?
2. Why does the depth counter live in `parse_value`, and not in `parse_array` and
   `parse_object`?
3. `{"a":1}` followed by a newline parses, and `{"a":1} x` does not. Which lines of
   `cJSON_Parse` make the difference?
4. With the cap at 3, why does `[1,2,3` (no closing bracket) say the node cap refused
   it, although the document is also broken?
5. A model sends `{"path":"a.txt","path":"b.txt"}` to a tool that writes files. Which
   file does jichi write, and what does a hook see on its standard input?

<details><summary>Answers</summary>

1. The `default` arm sends `-` to `parse_number`. As with `e`, it is not refused:
   measured for this chapter, `[-]` parses as `[0]`. Chapter 4 reads why.
2. `parse_value` is the only place where the parser recurses, so one increment and one
   decrement there cover every box. The container functions have several return points each (five in `parse_array`, seven in
   `parse_object`), and a counter decremented at each of them leaks at the first new return someone
   adds.
3. After the root, `skip_ws(&c)` skips the newline, and `if (c.p != c.end)` finds the
   cursor at the end. With ` x`, the cursor stops at `x`, so `c.ok` becomes 0 and the
   tree is deleted.
4. The parser reads left to right. It needs a fourth node for the `3` before it reaches
   the end of the input, and `parse_new_item` refuses that node and sets the flag. The
   missing `]` is never reached.
5. `b.txt`, because the lookup returns the last `path`. The hook's input holds both
   members, because the tree keeps them and `build_stdin` prints the tree; a hook that
   reads the last one agrees with jichi.

</details>

*Next: [chapter 3 — Strings](ireko-03-strings.md), where `parse_string` decodes
escapes and sizes each string to its own length.*
