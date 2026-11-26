# Ireko 5 -- Building and printing

*[入れ子（いれこ）*Ireko* — the JSON layer](IREKO.md) · chapter 5 of 7*

## Why this exists

Chapters 2 to 4 read JSON: text comes in, a tree comes out. This chapter goes
the other way. Every model call starts with jichi *writing* JSON: the request
body, with the model's name, the whole conversation so far and the tools the
model may call. A provider's `build_request` builds that body as a cJSON tree,
and `src/provider/jc_provider.c:jc_prov_print_body` prints the tree into the
string that travels to the server. CLAUDE.md's flow diagram adds that the body
is "rebuilt per retry attempt".

So two costs come back on every call: putting items into containers, and
turning the tree into text. Both were found slow and fixed in one merge, on
2026-10-29:

- **M1085** made appending constant-time. Before it, building an array of n
  items walked about n²/2 links.
- **M1084** made printing copy runs of plain bytes in one step. The record
  gives the reason: "Re-serialising the request dominated jichi's own CPU in a
  tool-heavy turn".

M1083 landed in the same merge, but it is about saving sessions, not JSON.

## The shape

```
build:  root = CreateObject()
        AddStringToObject(root, "model", ...)       Create + AddItemToObject
        msgs = AddArrayToObject(root, "messages")
        AddItemToArray(msgs, m)                     append after msgs->tail
print:  PrintUnformatted(root) -> print_with(root, 0)
          -> print_value(root, depth 0)             recursive, one call per box
               -> pb_putc / pb_puts / pb_put_string / pb_put_number
                    -> pb_ensure                    grow the buffer
free:   cJSON_Delete(root)                          the whole tree
        free(text)                                  the string is separate
```

In memory, a container points at its first item with `child` and, since
M1085, at its last with `tail`; the items are chained by `next` and `prev`:
`root.child -> "model" <-> "messages" <-> "tools" <- root.tail`.

## Making and freeing a node

Every constructor starts from one function:

```c src/json/cJSON.c:cJSON_New_Item
static cJSON *cJSON_New_Item(void)
{
    cJSON *node = (cJSON *)malloc(sizeof(cJSON));
    if (node != NULL) {
        memset(node, 0, sizeof(cJSON));
    }
    return node;
}
```

> **C sidebar — `malloc`, `memset` and ownership.** `malloc` returns a block
> the size of one node, or NULL when memory runs out. `memset` zeroes it, so
> every integer starts as 0 (so `type` is `cJSON_Invalid`) and, on every platform jichi runs on,
> every pointer starts NULL and `valuedouble` 0.0. C89 itself does not promise those last two: a
> null pointer and 0.0 need not be all-zero bits. Whoever
> holds the pointer **owns** the block and must `free` it exactly once.
> `static` makes the function visible only inside `cJSON.c`.

Each `cJSON_Create*` sets a type. A string node gets its own copy of the text
(`malloc(strlen(string) + 1)`, one more byte for the NUL, then `strcpy`), so
the caller may free its own string afterwards; a NULL argument becomes `""`.
`cJSON_CreateNumber` fills the `int` view as the parser does (M866, chapter 4).
The constructors do not count against M1089's node cap, which bounds one
*parse* (chapter 2); in the words of `src/json/cJSON.c:parse_new_item`, "the
finding is the parser, and jichi builds its own request bodies through them."

Freeing is one function, and it frees more than one node:

```c src/json/cJSON.c:cJSON_Delete
    while (item != NULL) {
        next = item->next;
        if (item->child != NULL) {
            cJSON_Delete(item->child);
        }
...
        free(item);
        item = next;
    }
```

The loop follows `next`. So `cJSON_Delete(item)` frees `item`, everything
below it, **and every sibling after it**. On a root, which has no siblings,
that is what you want. On a node still in a list, it frees the rest of the
list too. `cJSON_ReplaceItemInObject`, below, has to work around this.

## Adding to a container

An object is an array whose items carry keys. `cJSON_AddItemToObject` copies
the key into the item and hands over to the array function:

```c src/json/cJSON.c:cJSON_AddItemToObject
    if (item->string != NULL) {
        free(item->string);
    }
    item->string = (char *)malloc(strlen(string) + 1);
    if (item->string == NULL) {
        return 0;
    }
    strcpy(item->string, string);
    return cJSON_AddItemToArray(object, item);
```

It does not look for the key first: adding `"x"` twice gives two `"x"`
members, and since M1113 a read returns the last (chapter 2). Nor does it check
where `item` came from: a node still in another list keeps its `next` and drags
its old siblings along. The header's rule, "Items are owned by their parent
after being added", is the caller's to keep.

The `cJSON_Add*ToObject` helpers create and add in one call:

```c src/json/cJSON.c:cJSON_AddStringToObject
    cJSON *s = cJSON_CreateString(string);
    if (s == NULL) return NULL;
    if (!cJSON_AddItemToObject(object, name, s)) {
        cJSON_Delete(s);
        return NULL;
    }
    return s;
```

On failure the helper frees the node it made, so nothing leaks. On success it
returns the node as a *borrowed* pointer: the object owns it, and the caller
may use it only while the object lives. That is how
`msgs = cJSON_AddArrayToObject(root, "messages")` is used.

## How appending finds the end

A list linked by `next` knows its first item; the last has to be found. Before
M1085, `cJSON_AddItemToArray` walked from `child` on every call, with
`while (child->next != NULL) child = child->next;` (the old line, quoted in the
header of `tests/test_cjson_append_linear.c`). The first append only sets
`child`, the second walks nothing, and the k-th walks k − 2 links. For n
appends that sums to (n − 1)(n − 2) / 2, which for n = 10,000 is 49,985,001:
the exact number the M1085 record measured. Twice the items, four times the
work.

> **C sidebar — O(1), O(n), O(n²).** O(1) means the cost of one step does not
> grow with the list; a walk to the end is O(n); doing an O(n) step n times is
> O(n²). The record's bench: 16,557 appends took 0.236 s before the fix and
> 0.0008 s after it.

M1085 added a field, `tail`, and the append reads it first:

```c src/json/cJSON.c:cJSON_AddItemToArray
    child = array->tail;
    if (child == NULL || child->next != NULL) {
        child = array->child;
        while (child->next != NULL) {
            child = child->next;
            g_jc_append_steps++;
        }
        array->tail = child;
    }
    child->next = item;
    item->prev = child;
    array->tail = item;
    return 1;
```

The `if` guards against a wrong cache, in the two forms code can detect: NULL
(nobody set it, as when code outside this file links a list by hand), or a
cached node that has a `next` (somebody appended without updating it). Then
the function walks once, repairs the cache and goes on. Each step of that walk
adds one to the `static` counter `g_jc_append_steps`, which the public
`cJSON_jc_append_steps()` reports. With a healthy cache it does not move. The
parser also stores its tail now (`arr->tail = item;` in
`src/json/cJSON.c:parse_array`), so a parsed tree arrives with a correct cache.

**Why a field.** The finding behind M1085 proposed keeping the tail in the
first child's `prev`. The record explains why that did not fit: here `prev`
means "previous sibling", the head's `prev` is NULL, and
`cJSON_ReplaceItemInObject` relies on that to recognise the head. The field
costs one pointer in every node: on a 64-bit system `sizeof(cJSON)` went from 64 to 72 bytes, an
eighth more memory for every parsed or built value (M1089's comment, written the same day, still
says "a malloc'd 64-byte node"). It also ages a sentence: the provenance note
atop `cJSON.c` said, until release 0.15.0 corrected it, that the real library "can
replace this file and cJSON.h as a pair without touching anything else in the tree", but
`src/setup/jc_setup.c:strip_named_member` now writes `->tail`, a field jichi added at M1085, and
both provider dialects call the jichi-only `cJSON_jc_parse_refused_by_cap()` (M1094). A swap would
no longer compile, before any difference in behaviour (last-wins duplicate keys, M1113; refusing
trailing content, M1078) is considered.

**Who keeps the cache right.** In `cJSON.c`: the append, the parser and the
replace. Outside it, `src/setup/jc_setup.c:strip_named_member` repoints the
tail when it removes the last member;
`src/provider/jc_provider_openai.c:oa_build_request` prepends, which leaves the
tail alone; `src/config/jc_config_trust.c:detach` unlinks a member and does not
touch `tail`. The test's third leg copies that `detach` on a *middle* element,
"the common case". Try it, step 3, detaches the last one.

## Replacing a member, and who owns what

`cJSON_ReplaceItemInObject(object, key, newitem)` swaps one member for another
in place. It came with prompt caching (M31b): the Anthropic dialect turns the
last message's plain-string `content` into a one-block array, so that the cache
marker "has a block to ride on" (the call is in
`src/provider/jc_provider_anthropic.c:cache_control_last_block`).

```c src/json/cJSON.c:cJSON_ReplaceItemInObject
    while (child != NULL) {
        if (child->string != NULL && strcmp(child->string, string) == 0) {
            /* newitem inherits the member key from the old item. */
            if (newitem->string != NULL) {
                free(newitem->string);
            }
            newitem->string = child->string;
            child->string = NULL;
...
            child->next = NULL;
            child->prev = NULL;
            cJSON_Delete(child);
            return 1;
        }
        child = child->next;
    }
    return 0;
```

Three ownership moves:

1. **The key moves; it is not copied.** `newitem->string = child->string;
   child->string = NULL;` hands the malloc'd key over. The NULL is what keeps
   `cJSON_Delete(child)` from freeing a key `newitem` now uses.
2. **The old item is cut loose before it is freed.** Its `next` and `prev`
   become NULL first, because `cJSON_Delete` follows `next`. The elided lines
   relink the neighbours and, if the old item was the tail, point `tail` at
   `newitem`.
3. **When nothing matches, `newitem` is still yours.** The function returns 0
   and frees nothing. That is why
   `src/config/jc_configedit.c:jc_configedit_set_bool` looks the key up first
   and adds it when it is missing.

And one thing to notice: the loop stops at the **first** member with the key,
while since M1113 a read returns the **last**. On an object that holds a key
twice (a parsed object can, since the parser keeps every member, and so can a built one, since
`cJSON_AddItemToObject` does not look for the key), the replace changes a member no read
returns. Try it, step 3, measures it.

## The print buffer

The printer writes into a `struct print_buf`: `data`, `len` (bytes written),
`cap` (bytes allocated), `fmt` (pretty-print or not) and `oom`. All growth goes
through one function:

```c src/json/cJSON.c:pb_ensure
    size_t need = b->len + extra + 1;
...
    nc = (b->cap == 0) ? 256 : b->cap;
    while (nc < need) {
        nc *= 2;
    }
    p = (char *)realloc(b->data, nc);
    if (p == NULL) {
        b->oom = 1;
        return;
    }
```

(Before this, it returns at once if `oom` is set or the buffer is big enough;
after it, `data` and `cap` take the new block.)

The growth rule: `need` counts the bytes written, the bytes to come and
**one** more, for the NUL that `print_with` writes last. (`print_with` still calls `pb_ensure(&b, 1)` before it writes the NUL, so at the end the NUL is
reserved twice: a 255-byte output, which would fit 256 bytes with its NUL, ends in a 512-byte
buffer.) The first block is 256
bytes, and after that the size **doubles** until it fits; the old blocks
together are smaller than the newest, so `realloc` copies less than the final
size in all. `realloc` returns into a temporary `p`, so a failure leaves the old
block in `b->data` to be freed (`b->data = realloc(b->data, nc)` would lose it).
A failure sets `oom`, every later call returns at once, and `print_with` frees
the buffer and returns NULL: one check at the end instead of one per write. Not
checked: whether `nc *= 2` overflows, which needs a buffer past half of
`size_t`'s range, over 2 GiB on a 32-bit system. If it did overflow, `nc` would wrap to 0 and the loop would double 0 forever: a hang, not a short
buffer. Reaching it needs a 2 GiB block that a 32-bit process rarely gets; a failed `realloc` sets
`oom` instead.

On top of it, `src/json/cJSON.c:pb_putc` writes one byte, `pb_puts` a C string
with one `memcpy`, and `pb_indent` two spaces per level, only when `fmt` is
set.

## `print_value`, and the two formats

`src/json/cJSON.c:print_value` mirrors the parser's `parse_value`: a `switch`
on the type, and for a container a loop that calls `print_value` again, one
level deeper. The object case:

```c src/json/cJSON.c:print_value
        case cJSON_Object: {
            cJSON *child = item->child;
            pb_putc(b, '{');
            if (child != NULL && b->fmt) pb_putc(b, '\n');
            while (child != NULL) {
                pb_indent(b, depth + 1);
                pb_put_string(b, child->string);
                pb_putc(b, ':');
                if (b->fmt) pb_putc(b, ' ');
                print_value(b, child, depth + 1);
                if (child->next != NULL) pb_putc(b, ',');
                if (b->fmt) pb_putc(b, '\n');
                child = child->next;
            }
            if (item->child != NULL) pb_indent(b, depth);
            pb_putc(b, '}');
            break;
        }
```

The `b->fmt` tests are the whole difference between the formats. Formatted
(`cJSON_Print`) adds a newline after `{` and after each member, two spaces of
indent per level, and a space after the colon. Unformatted
(`cJSON_PrintUnformatted`) adds none. Keys, values, order, escapes and numbers
are the same, and an empty container prints `{}` or `[]` in both.

The request body is printed unformatted. Of the six `cJSON_Print` calls in
`src/`, five write a file or listing a person may open, such as a saved config
(`src/config/jc_configedit.c:jc_config_edit_save`). The sixth prints a subtree
and parses it back as a deep copy
(`src/setup/jc_setup.c:jc_setup_inherit_config`): this subset has no
`cJSON_Duplicate`.

Formatting changes no value, but it changes the bytes. M519's record shows the
cost for a tool that reads JSON as *text*: LM Studio pretty-prints, a `grep`
pattern written for single-line replies could not cross the line break, and a
probe reported that no local model could call tools.

`print_value` recurses with no depth limit; its comment argues that every tree
jichi prints is built by its own code (about six levels) or parsed under M472's
limit of 256. No code in this tree makes a `cJSON_Raw` node, though the case is
there. `src/json/cJSON.c:print_with` sets up the buffer, calls `print_value`,
writes the NUL and returns the string, which the caller frees with `free`.
Freeing the tree does not free the string, nor the other way round.

## The string fast path (M1084)

A JSON string must escape `"`, `\` and the control bytes below 0x20; every
other byte is copied as it is. Before M1084, `pb_put_string` sent each byte
through a `switch` and one `pb_putc`. Now it finds a **run** of bytes that need
no escape and copies the run in one step:

```c src/json/cJSON.c:pb_put_string
            if (ch >= 0x20 && ch != 0x22 && ch != 0x5c) {
                const unsigned char *r = p;
                size_t run;
                while (*r && *r >= 0x20 && *r != 0x22 && *r != 0x5c) {
                    r++;
                }
                run = (size_t)(r - p);
...
                pb_ensure(b, run);
                if (b->oom) return;
                memcpy(b->data + b->len, p, run);
                b->len += run;
                g_pb_run_bytes += run;
                p = r;
                continue;
            }
```

0x22 is `"` and 0x5c is `\`. The old `switch` stays below as the slow path.
Two notes from the code. **The printer does not validate UTF-8**: a byte at or
above 0x80 is copied, valid or not; on the request path,
`jc_prov_print_body` runs `jc_utf8_sanitize` afterwards, and M1084 gave that a
fast path too. And **`cJSON.c` calls `sprintf`** (four times) **and `strcpy`** (twice: the copies in
`cJSON_CreateString` and `cJSON_AddItemToObject` shown above), which the house rules ban:
`tests/smoke/sprintf_lint.sh` allowlists all six lines by exact text, the `sprintf` ones because
the printer "must stay jc_snprintf-free to remain swappable as a pair", the `strcpy` ones as
bounded sites, each copying into a block just sized with `strlen + 1`.

`g_pb_run_bytes` counts what the fast path copied; `cJSON_pb_run_bytes()` reads
it and `cJSON_pb_run_reset()` zeroes it. Unlike `cJSON_jc_append_steps`, they
are not in `cJSON.h`: a program that uses them declares them itself.

## The print parity test

`tests/test_cjson_print_parity.c` holds M1084. Its header names the kind of
change: the fast paths "change HOW the bytes get written, not WHAT bytes get
written". A test that fails before the fix cannot express that, so this one
compares:

- **An oracle.** The file holds a copy of the *pre-M1084* `pb_put_string`.
  Every corpus string is printed by both, and the results must be identical,
  byte for byte.
- **A corpus at the edges.** The empty string, every escape alone and mixed,
  every control byte from 0x01 to 0x1f, UTF-8 of every width, invalid bytes,
  long plain runs, and runs broken by one escape at the start, middle and end.
- **Proof that the fast path ran.** A fast path that never runs is also
  byte-identical. So the test asks the counter after each print:

```c tests/test_cjson_print_parity.c:test_print_parity_one
    cJSON_pb_run_reset();
    real = cJSON_PrintUnformatted(item);
    want = oracle_put_string(s);
...
        same = (strcmp(real, want) == 0);
        JC_CHECK(same == 1);
...
            size_t run = cJSON_pb_run_bytes();
            size_t expect = noescape_count(s);
            JC_CHECK(run == expect);
```

The oracle is pinned too, by eight explicit expected strings, because a broken
oracle would agree with a broken printer. The record says the test went red,
10 failures, when the 0x22 test was dropped from the fast path.

**And prompt caching?** The M1084 record gives CPU as its reason, not caching.
Where the record ties printed bytes to caching (the M31 entry), it states a
precondition: "the rendered prefix must be **byte-stable across turns**". M31d
holds it in `tests/test_provider.c`: two successive `build_request` calls on
one provider must produce byte-identical bodies.

## Try it

No build of jichi is needed: `cJSON.c` needs only the C library. Step 1 makes a
scratch directory; run the later steps in the same shell.

**1. A request-shaped object, printed both ways.**

```sh
# in the jichi checkout
T=$(mktemp -d)
cat > "$T/req.c" <<'EOF'
/* req.c -- build a request-shaped object, then print it both ways. */
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"

/* Defined in cJSON.c for the parity test; not declared in cJSON.h. */
void   cJSON_pb_run_reset(void);
size_t cJSON_pb_run_bytes(void);

int main(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *msgs;
    cJSON *m;
    char *flat;
    char *pretty;

    cJSON_AddStringToObject(root, "model", "example-model");
    msgs = cJSON_AddArrayToObject(root, "messages");
    m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "user");
    cJSON_AddStringToObject(m, "content", "Say \"hi\".\n");
    cJSON_AddItemToArray(msgs, m);
    cJSON_AddArrayToObject(root, "tools");
    cJSON_AddBoolToObject(root, "stream", 1);
    cJSON_AddNumberToObject(root, "max_tokens", 256);

    cJSON_pb_run_reset();
    flat = cJSON_PrintUnformatted(root);
    printf("%s\nfast-path bytes: %lu\n", flat,
           (unsigned long)cJSON_pb_run_bytes());
    pretty = cJSON_Print(root);
    printf("%s\n", pretty);
    free(flat);
    free(pretty);
    cJSON_Delete(root);   /* frees m and every other node under root */
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/req" "$T/req.c" src/json/cJSON.c -lm
"$T/req"
```

```
{"model":"example-model","messages":[{"role":"user","content":"Say \"hi\".\n"}],"tools":[],"stream":true,"max_tokens":256}
fast-path bytes: 69
{
  "model": "example-model",
  "messages": [
    {
      "role": "user",
      "content": "Say \"hi\".\n"
    }
  ],
  "tools": [],
  "stream": true,
  "max_tokens": 256
}
```

The compiler printed nothing: no warnings. Check the 69 by hand: the keys are
45 plain bytes (`model` 5, `messages` 8, `role` 4, `content` 7, `tools` 5,
`stream` 6, `max_tokens` 10), and the values add `example-model` (13), `user`
(4) and `Say hi.` (7). The two `"` and the newline took the slow path. Keys
count because `pb_put_string` prints them too.

**2. Appending stays O(1), measured with the public counter.**

```sh
# in the jichi checkout, the same shell as step 1
cat > "$T/steps.c" <<'EOF'
/* steps.c -- how many links does appending walk? */
#include <stdio.h>
#include "cJSON.h"

static void fill(cJSON *arr, long n)
{
    long i;
    for (i = 0; i < n; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)i));
    }
}

static void count(const char *label, cJSON *arr, long n)
{
    size_t before = cJSON_jc_append_steps();
    fill(arr, n);
    printf("%-22s %lu steps, size %d\n", label,
           (unsigned long)(cJSON_jc_append_steps() - before),
           cJSON_GetArraySize(arr));
}

int main(void)
{
    cJSON *a = cJSON_CreateArray();
    cJSON *b = cJSON_CreateArray();
    cJSON *c = cJSON_CreateArray();

    count("1000 appends:", a, 1000);
    count("10000 appends:", b, 10000);
    count("100000 appends:", c, 100000);
    b->tail = NULL;               /* lose the cache, as outside code can */
    count("tail lost, 1 append:", b, 1);
    count("next append:", b, 1);
    cJSON_Delete(a);
    cJSON_Delete(b);
    cJSON_Delete(c);
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/steps" "$T/steps.c" src/json/cJSON.c -lm
"$T/steps"
```

```
1000 appends:          0 steps, size 1000
10000 appends:         0 steps, size 10000
100000 appends:        0 steps, size 100000
tail lost, 1 append:   9999 steps, size 10001
next append:           0 steps, size 10002
```

A hundred times more items, still zero steps: each append costs the same, so
building the array is linear in its size. Before M1085 the second line would
have read 49,985,001. The last two lines show the guard: with the cache lost,
one append walks the list once and repairs it, and the next is free again.

**3. Two limits: a replace, and a detached tail.**

```sh
# in the jichi checkout, the same shell as step 1
cat > "$T/limits.c" <<'EOF'
/* limits.c -- what a replace replaces; what a stale tail does. */
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"

static void show(const char *label, const cJSON *o)
{
    char *s = cJSON_PrintUnformatted(o);
    printf("%-14s %s\n", label, s);
    free(s);
}

int main(void)
{
    cJSON *o = cJSON_Parse("{\"x\":1,\"x\":2}");
    cJSON *spare = cJSON_CreateNumber(9);
    cJSON *b;

    cJSON_ReplaceItemInObject(o, "x", cJSON_CreateNumber(3));
    show("replaced x:", o);
    printf("%-14s %g\n", "x reads as:", cJSON_GetObjectItem(o, "x")->valuedouble);
    if (!cJSON_ReplaceItemInObject(o, "nope", spare)) {
        cJSON_Delete(spare);      /* no match: spare is still ours */
    }
    cJSON_Delete(o);

    o = cJSON_Parse("{\"a\":1,\"b\":2}");
    b = cJSON_GetObjectItem(o, "b");
    o->child->next = NULL;        /* unlink the last member, as detach() */
    b->prev = NULL;               /* does, and leave o->tail pointing at b */
    cJSON_AddNumberToObject(o, "c", 3);
    show("object:", o);
    printf("%-14s %s\n", "b->next:", b->next != NULL ? b->next->string : "-");
    cJSON_Delete(b);              /* frees b, and c with it */
    cJSON_Delete(o);
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/limits" "$T/limits.c" src/json/cJSON.c -lm
"$T/limits"
```

```
replaced x:    {"x":3,"x":2}
x reads as:    2
object:        {"a":1}
b->next:       c
```

The first two lines are the first-against-last mismatch: the replace changed
the first `"x"`, every read sees the second. The last two are the tail guard's
blind spot: the stale tail pointed at `b`, `b->next` was NULL, so the guard saw
a healthy cache and linked `c` after a node that had left the list. If you have
valgrind, `valgrind --leak-check=full "$T/limits"` reports "All heap blocks
were freed -- no leaks are possible": the program freed `spare` itself, as the
ownership rule requires.

## What went wrong here, and what it teaches

### The builder that walked (M1085)

The finding noticed an asymmetry: the parser kept a local tail and appended in
O(1), while the builder, used for every request, did not. Each append was
correct; only the sum was quadratic.

`tests/test_cjson_append_linear.c` counts instead of timing: a time depends on
the machine and its load, a step count does not. Its threshold, 20,000 steps
for 10,000 appends, is "far above the fixed count, far below the old": red on
the old loop (the record's red run, 49,985,001, proves the instrument can
fire), green on the new (0), and still red for a regression that walked once
per 50 appends. The record also shows a recipe checked against the tree before
it was applied (the `prev` idea), and a cost of parallel work: M1084 and M1085
both changed `cJSON.c` in one wave, and the lead joined them with a three-way
merge before both were gated again.

### A faster printer that must print the same bytes (M1084)

The record measured user CPU in a 200-round run built at `-O2`: 0.62 s before,
0.45 s after, 27% less. The finding had claimed 62%, measured on a slower
machine; the record kept the number it measured. The lesson is the test's
shape. When a change must make code faster and *not* different, compare it
with the old code, kept as an oracle, on inputs at the edges, and add a counter
that proves the new path ran.

### Two limits, measured for this chapter

The first is in no public record. An internal note of the drive that built M1085 logged it as a
follow-up ("logged, not fixed"), calling the case "unreachable in-tree today"; no `docs/DEFERRED.md`
row holds it, and the ROADMAP's M1085 entry says the opposite ("stays correct at a one-off cost").
The second is in no record. Both were measured while writing this chapter, with Try it, step 3.

- **The tail guard cannot see a tail that left the list.** The comment over
  the guard says the cache may be "stale (a detach path unlinked the cached
  tail without updating it)" and that "the fallback walks to the real tail".
  The check is `child == NULL || child->next != NULL`, and a detached node has
  `next == NULL`. `src/config/jc_config_trust.c:detach` can detach a last
  member without updating `tail`, and
  `src/config/jc_config_trust.c:jc_cfgtrust_filter` then frees it (or, for a narrowing key, moves
  it into another object, where the stale tail would then point). An append after that reads and
  writes freed memory, and the appended member never appears in the object: step 3 with `cJSON_Delete(b)` moved before
  the append, built with `-fsanitize=address`, stops with a
  heap-use-after-free in `cJSON_AddItemToArray`. Read at this tree, nothing
  appends to that object afterwards: `src/config/jc_config.c:config_load_impl`
  either merges it into another config as a read-only overlay or only reads
  it. The defect is latent.
- **A replace and a read can disagree.** The replace changes the first member
  with a key, the read returns the last.
  `src/config/jc_config.c:jc_config_merge_json` finds a member with the
  last-wins read and then replaces by key, and
  `src/config/jc_configedit.c:jc_configedit_set_bool` does the same, so on a
  config file that holds a key twice the replace lands on the member no read
  returns. The review of this course drove it through `jichi` itself: with a config holding
  `"snapshots"` twice, `config set snapshots true` reports success and exits 0, while `config show`
  still says the snapshots are off. Through `jc_config_merge_json`, a global config that holds a key
  twice loses the project's override: the project's value lands on the first member and every read
  returns the global's second. The same mismatch reaches a model's tool arguments:
  `jc_tool_unstring_args` (`src/tools/jc_tool.c`) converts a stringified array by replace, so
  `{"edits":"[1]","edits":"[2]"}` reports two fixes while the read still returns the string
  `"[2]"`.

Both have one cause: an invariant kept in more than one place. The tail is kept
by whoever links nodes, inside the file or out. "Which member does a key name?"
is answered one way by the reader and another way by the replacer. A rule kept
in two places can drift apart while neither place changes.

## Check yourself

1. You hold `m`, a message node in the middle of an array. What does
   `cJSON_Delete(m)` free, and what should you do first?
2. Why does `cJSON_ReplaceItemInObject` set `child->string = NULL` before
   `cJSON_Delete(child)`?
3. A tree prints to 600 bytes. Which buffer sizes does `pb_ensure` allocate on
   the way, and why is the last not 601?
4. The parity test compares the new printer with a copy of the old one. Why is
   that not enough alone, and what does the test add?
5. In step 3, why can neither of the guard's two conditions catch the detached
   tail?

<details><summary>Answers</summary>

1. `m`, everything under it, and every sibling after it, because
   `cJSON_Delete` follows `next`. Unlink `m` first: join its neighbours, fix the
   array's `child` or `tail` if `m` was first or last, then set `m->next` and
   `m->prev` to NULL.
2. The key now belongs to `newitem`. `cJSON_Delete` frees a node's `string`, so
   without the NULL it would free the key `newitem` still uses.
3. 256 first, then doubling until the request fits. With small writes the sizes are 256, 512, then
   1024. When one write is large (a 592-byte string run), the loop doubles twice inside one
   `pb_ensure`, and the sizes are 256, then 1024. The last is 1024 either way: the size starts at
   256 and only doubles, and 1024 is the first such size of at least 601 (600 bytes and the NUL).
4. A fast path that never runs also prints identical bytes. The test reads
   `cJSON_pb_run_bytes()` after every print and requires it to equal the number
   of bytes that need no escape. It also pins the oracle with explicit strings.
5. `tail` is not NULL, and `tail->next` is NULL, exactly as for a healthy tail.
   To catch it the guard would have to know whether the node is still in *this*
   list, which means walking the list (the cost the cache avoids), or every
   unlinking path must update `tail`, as `strip_named_member` does.

</details>

*Next: [chapter 6 — Who wrote this JSON?](ireko-06-who-wrote-this-json.md),
where reading a value depends on who wrote it.*
