# Ireko 6 -- Who wrote this JSON?

*[入れ子（いれこ）*Ireko* — the JSON layer](IREKO.md) · chapter 6 of 7*

## Why this exists

jichi reads JSON written by many different authors:

- **jichi itself**: its telemetry log, its run journals, the replies of its own
  control server;
- **a person**: a config file, a workflow map, a session file edited by hand;
- **another program**: an agent config that `jichi-convert` translates, an
  editor speaking ACP, a supervisor script on the control socket, an MCP
  server's reply;
- **a model**: the arguments of every tool call.

The last one needs a word on the AI side. When jichi offers a tool, it sends
the tool's name and a JSON schema for its arguments: `limit` is an integer,
`readonly` is a boolean. The model answers with a tool call whose arguments
are a JSON object, written token by token like any other text it produces.
The schema asks for a type; whether the model writes that type is up to the
model. It can write `"200.0"` where the schema says integer, and M168 records
one that did.

Every reader asks the same question, "what is the value of this key?", and the
helpers in `src/json/jc_json.c` answer it. This chapter is about the hard case:
a value of the wrong type. A **strict** reader returns the caller's default for
anything but the right type. A **lenient** reader also accepts a few spellings
that cannot mean anything else, such as `1` for true. Reading strictly where a person or a model wrote the value turned safety fences off four times:
M519, M530 and M843 in JSON, and M534 in the YAML header of an agent profile (`readonly:`) or a
skill (`restrict-tools:`). CLAUDE.md now states the rule as an invariant, by the
question this chapter is named after.

## The shape

```
reader                     accepts                                   anything else
jc_json_get_bool           true, false                               -> dflt
jc_json_get_bool_lenient   true, false; a number (0 = false);        -> dflt
                           a word: true/false yes/no on/off 1/0,
                           in any case
jc_json_get_num            a number                                  -> dflt
jc_json_get_num_lenient    a number; a string that is wholly a       -> dflt
                           number ("200.0")

who wrote this JSON?
  jichi itself (telemetry, journals, its own control replies)  -> strict:  a non-bool is a bug to see
  a person, another program, an editor, a script, a model      -> lenient: a dialect to forgive
  prose ("probably", "200 lines"), from anyone                 -> never guessed: dflt
```

## The plain accessors

Each accessor takes an object, a key and a default:

```c src/json/jc_json.c:jc_json_get_str
const char *jc_json_get_str(const cJSON *o, const char *key, const char *dflt)
{
    cJSON *item = cJSON_GetObjectItem(o, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return dflt;
}
```

Three properties, read from this function and the ones beside it:

- **NULL-safe.** `cJSON_GetObjectItem(NULL, key)` returns NULL and
  `cJSON_IsString(NULL)` is 0, so `jc_json_get_str(NULL, "k", "d")` returns
  `"d"`. A caller can read a key of an object that may not exist.
- **One lookup for all.** Every accessor goes through `cJSON_GetObjectItem`,
  which since M1113 returns the *last* member with the key (chapter 2).
- **Borrowed strings.** The returned pointer points into the tree, valid until
  `cJSON_Delete` frees it.

`jc_json_get_num` and `jc_json_get_bool` have the same shape, with
`cJSON_IsNumber` and `cJSON_IsBool`. The odd one out:

```c src/json/jc_json.c:jc_json_get_obj
cJSON *jc_json_get_obj(const cJSON *o, const char *key)
{
    return cJSON_GetObjectItem(o, key);
}
```

Despite its name, it checks no type: it returns the item, whatever it is, or
NULL. Callers use it two ways: to fetch a nested object or array that they then type-check
themselves (`models = jc_json_get_obj(root, "models")` in the config loader), and as a **presence
test**, "was this key written at all?". A presence test is at the centre of three of the incidents
below: M519 wrote it with this function, M530 with `cJSON_GetObjectItem` directly, and M534 as a
`has_readonly` flag on a YAML header. M843 had none.

**A copy that outlives the tree.** `jc_json_dup_str(o, key, a)` reads a string
like `jc_json_get_str` and copies it with `jc_arena_strdup(a, s)`.

> **C sidebar — an arena.** An arena (`include/jc_mem.h`) is a chain of big blocks (8 KB each
> unless its creator asks for another size; a larger request gets a block of its own) from which
> small pieces are handed out in order. Pieces are never freed one by one; the whole arena is
> freed, or reset for reuse, at once, which suits data that all dies at the same moment. CLAUDE.md names three arenas by lifetime: the session, one
> top-level turn, one tool call.

Which arena is the caller's choice, and the config loader shows why a copy is
needed at all. `src/config/jc_config.c:config_load_impl` parses the file,
reads the tree, and ends with `cJSON_Delete(root)`. But strings such as the
`verify` and `testCommand` commands must last the whole session. So it copies
them with `jc_json_dup_str(root, "verify", a)`, and `src/main.c:main` passes
the arena that becomes `app.arena`, CLAUDE.md's session-lived one. A borrowed
pointer would dangle as soon as the load returned.

The rest of the file: `jc_json_parse` is `cJSON_Parse` with a NULL check;
`jc_json_print` is `cJSON_PrintUnformatted` (chapter 5), and the caller frees
its result with `free`; the four `jc_json_clamp_*` functions (M1077) turn a
double from untrusted JSON into an integer without undefined behaviour
(chapter 4).

## Numbers: strict and lenient (M168, M287)

`jc_json_get_num` accepts a JSON number and nothing else. The lenient one also
accepts a string, if the whole string is a number:

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

> **C sidebar — `strtod` and its end pointer.** `strtod(s, &end)` reads the
> longest number at the start of `s`, after skipping any leading whitespace (so `" 7"` reads 7),
> and sets `end` to the first byte after
> it; `end == s` means there was no number at all. The code above then allows
> trailing whitespace and nothing else: `"200.0"` passes, `"200 lines"` stops
> at the `l` and is refused. That is the line between a number written as a
> string and prose.

**Why it exists (M168).** A model sent `{"limit": "200.0"}` to `read_file`.
The integer reader of that time took only a JSON number and returned the
default, and for `limit` the default is 0, "no limit". Five such calls each
returned a 172 KB file instead of 200 lines, each adding 40,000 to 57,000
input tokens that a backend without a cache re-billed on every later call. The
record's rule: "a type mismatch must never silently select the costliest
behaviour available."

**Why it lives here (M287).** The compaction layer must agree with the read
tool about what `{"limit": "100.0"}` means, so M287 moved the parse out of the
tool layer into `jc_json.c`. In the header's words: "Two readers of one
quantity, each with its own parser, is the drift that cost M286". Today
`src/tools/tool_util.c:tu_arg_int` is one line around it:

```c src/tools/tool_util.c:tu_arg_int
    return jc_json_clamp_int(jc_json_get_num_lenient(args, key, (double)dflt));
```

**What it does not do.** Prose still falls through, so the caller's default
stays in charge: `{"limit": "200 lines"}` gives 0, and `read_file` returns
every line. The record chose that ("prose still falls through rather than
being guessed at"), so M168's rule holds for a number in the wrong type, not
for prose. And `strtod` accepts more than the comment's examples. Measured
here with glibc (Try it, step 1): a leading space (`" 7"` reads 7),
hexadecimal (`"0x10"` reads 16) and `"nan"` (NaN, which `jc_json_clamp_int`
turns into 0). Another C library may accept less. For `read_file`, `"nan"` reopens the door M168 closed: `tu_arg_int` turns it into 0, and a limit
of 0 (or any negative number) means no limit, so it returns every line; `"inf"` and `"1e999"`
become 2147483647, which is no limit in practice.

## Booleans: strict and lenient (M519, M534)

```c src/json/jc_json.c:jc_json_get_bool_lenient
    cJSON *item = cJSON_GetObjectItem(o, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item) ? 1 : 0;
    }
    if (cJSON_IsNumber(item)) {
        return item->valuedouble != 0.0 ? 1 : 0;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
...
        int b = 0;
        if (jc_bool_from_word(item->valuestring, &b)) {
            return b;
        }
    }
    return dflt;
```

Three tries: a real boolean; a number, 0 for false and anything else true (so
`0.5` and `-1` read as true); a boolean word. The words live in one place,
`src/util/jc_str.c:jc_bool_from_word`: `true`/`false`, `yes`/`no`, `on`/`off`
and `1`/`0`, in any case, matched exactly, so `" true"` with a leading space is
not a word. Everything else returns `dflt`.

One small drift, read in the tree: the comment above this function in
`include/jc_json.h` lists the strings `"true"/"false", "yes"/"no", "1"/"0"`.
It predates M534, which added `on`/`off`; the code accepts them (Try it, step
1, reads `"On"` as 1). The same comment also predates M530: it names values "a HUMAN or a FOREIGN PROGRAM writes", while
since M530 every boolean a model writes reaches this function too, through `tu_arg_bool`.

## The rule, and the four incidents behind it

CLAUDE.md's invariant: "A boolean a human, a foreign program or a MODEL writes
is read leniently." Only jichi's own sinks "stay strict, where a non-bool is a
bug to see", and prose "still falls through to the default", because "guessing
at prose is how a typo becomes a policy". Each part was paid for.

### M519: fifteen configs turned a fence off

The path fence keeps file tools inside the workspace. Its key, `pathFence`,
has three states: unset (`-1`, on in autonomous runs only), on, and off. Before
M519 the loader read it like this (the record quotes it):

```c docs/ROADMAP.md
if (jc_json_get_obj(root, "pathFence") != NULL) {
    out->path_fence = jc_json_get_bool(root, "pathFence", 0) ? 1 : 0;
}
```

Fifteen configs shipped in `examples/` said `"pathFence": 1`. The presence test
fired, the strict read saw a number and returned the default 0, and the fence
was set **off**, overriding the unset state that would have turned it on in
the unattended runs where a fence matters most. "Writing `1` did not fail to
enable the fence; it disabled it." Seven keys that default to 1 had the mirror
defect: `"snapshots": 0` ran with snapshots on.

The fix was **two accessors, not one lenient one**: the strict reader stays for
jichi's own data, and `jc_json_get_bool_lenient` serves JSON a person or
another program wrote, including an MCP server's `isError`, where a `1` had
reported a failed tool call to the model as a success. The record adds: "Not
one of the fifteen configs was edited. The values were right; the reading was
wrong, and fixing files would have left the sixteenth config broken." That gave,
until 0.15.0, a presence test and a lenient read with a fallback of 0:
`if (jc_json_get_obj(root, "pathFence") != NULL) { out->path_fence =
jc_json_get_bool_lenient(root, "pathFence", 0) ? 1 : 0; }`. Keep that form in
mind: "Try it" below finds what it still got wrong. Today the key goes through a
helper of its own:

```c src/config/jc_config.c:config_load_impl
    cfg_read_fence_key(root, "pathFence", &out->path_fence,
                       out->path_fence_unreadable,
                       sizeof(out->path_fence_unreadable));
```

### M530: a read-only child that was writable

M519 converted 52 call sites and left eight files for an audit "worth doing".
The audit found five more cases. The worst was in `spawn_subagent`, the tool a
model calls to start a helper agent:

```c docs/ROADMAP.md
has_arg_ro = (cJSON_GetObjectItem(args, "readonly") != NULL);
arg_ro     = tu_arg_bool(args, "readonly", 0);
```

A model asking for a read-only helper with `{"readonly": 1}` got present = 1,
value = 0: an **explicit** "not read-only", and the helper ran writable. The
other four were an editor's ACP capability flags, a session file's `isError`,
a supervisor script's control request, and a workflow map's `readonly`. The
decision ([DECISIONS, M530](../DECISIONS.md#dec-m530)) made the tool layer's
reader the same function:

```c src/tools/tool_util.c:tu_arg_bool
    return jc_json_get_bool_lenient(args, key, dflt);
```

The audit found one more defect, of another kind. The terminal UI's diff
preview read `replace_all` strictly while the edit tool read it leniently, so
`"replace_all": "true"` showed a preview with **one** replacement and ran an
edit that replaced **all**: the user approved a narrower change than the one
that ran. CLAUDE.md gained a second invariant: "A preview must read every
argument exactly as the executor will."

### M534: one dialect, and the third fence

jichi then had four opinions about how "true" is spelled. The agent profiles
(`.jichi/agents/*.md`) read `readonly:` with `strcmp(str, "true")`, and the
record reproduced it with four profiles, each meant to be read-only:

```text docs/ROADMAP.md
ro-1     readonly: 1     -> WRITABLE
ro-yes   readonly: yes   -> WRITABLE
ro-True  readonly: True  -> WRITABLE
ro-true  readonly: true  -> readonly   (the only one fenced)
```

A skill's `restrict-tools:` had the same flaw, and `config set` accepted
`on`/`off`, the spelling it writes, while no reader accepted it. The fix is one
function, `jc_bool_from_word`, used by every reader, with `on`/`off` added: "a
spelling the tool blesses must be a spelling the tool reads".

### M843: the widening fields too

`src/config/jc_config.c:parse_policy_field` reads, among others, the `deny`
field of `"permissions"`. It called `cJSON_IsTrue` itself, so it never met the
lenient accessor and no lint saw it: `"deny": 1` read as absent, and with a real model under
`--auto`, `write_file` wrote. The fix reads all four policy fields leniently,
**including the two that widen permissions**, `allow` and an MCP server's
`autoApprove`, so `"allow": 1` now means allow-all. The decision
([DECISIONS, M843](../DECISIONS.md#dec-m843)) records the rejected
alternative, lenient for fences and strict for widening fields: "a strict read
fails safe there, but it is still a silent misread of an explicit setting".
The rule is who wrote the JSON, not which way the key points.

## The boundary, held by a lint

`tests/smoke/config_bool_lint.sh` holds the rule in 25 checks:

- **Nine files read booleans only leniently** (`jc_config.c`,
  `jc_mcp_proto.c`, `jc_acp_proto.c`, `tool_util.c` and five more), each with a
  floor under its count of lenient calls.
- **The same nine hold no raw `cJSON_IsTrue`, `IsFalse` or `IsBool` read**
  beyond exact counts (M843): a raw read is as strict as the strict accessor,
  and the first checks cannot see it.
- **The strict reader is used by exactly three files, by name**:
  `src/main.c`, `src/util/jc_runsview.c` and `src/util/jc_telemetry.c`, the
  readers of jichi's own data. A count could not say whether the right files
  are on the right side.
- **The effect, asked of the binary**: `jichi doctor` must report the fence on
  for a shipped config with `"pathFence": 1`; a `0` must turn a default-on key
  off (`"snapshots": 0`), and a `1` must turn `pathFence` on from its default, auto (the lint's own
  label calls it "a default-OFF key"); prose must change nothing.
- **The question, not the callers** (M534): no `strcmp(..., "true")` in
  `src/`, and `jc_bool_from_word` defined in exactly one file.

The M530 record states what it cannot see: "it compares the set of files, so a
single call crossing inside a file that keeps others is invisible".

## Try it

Step 1 compiles against `src/json/jc_json.c` without building jichi. That file
needs four others: `cJSON.c`; `src/util/jc_str.c` for `jc_bool_from_word`;
`src/util/jc_mem.c` for the arena; and `src/platform/jc_snprintf.c`, which
`jc_str.c` calls. Step 1 makes a scratch directory; run the later steps in the
same shell.

**1. One key, four readers, and a copy.** `-1` is the default in every call,
so a `-1` means "fell through".

```sh
# in the jichi checkout
T=$(mktemp -d)
cat > "$T/who.c" <<'EOF'
/* who.c -- one key, read strictly and leniently; then a copy. */
#include <stdio.h>
#include "jc_json.h"

static const char *const docs[] = {
    "{\"x\": true}", "{\"x\": 1}", "{\"x\": \"1\"}", "{\"x\": \"true\"}",
    "{\"x\": \"On\"}", "{\"x\": \" true\"}", "{\"x\": \"probably\"}",
    "{\"x\": 0.5}", "{\"x\": \"200.0\"}", "{\"x\": \"200 lines\"}",
    "{\"x\": \" 7\"}", "{\"x\": \"0x10\"}", "{\"x\": \"nan\"}", "{}"
};

int main(void)
{
    int i;
    cJSON *o;
    struct jc_arena *a;
    char *copy;

    printf("%-20s %6s %6s %8s %8s\n", "document", "bool", "bool~", "num", "num~");
    for (i = 0; i < (int)(sizeof docs / sizeof docs[0]); i++) {
        o = jc_json_parse(docs[i]);
        printf("%-20s %6d %6d %8g %8g\n", docs[i],
               jc_json_get_bool(o, "x", -1),
               jc_json_get_bool_lenient(o, "x", -1),
               jc_json_get_num(o, "x", -1),
               jc_json_get_num_lenient(o, "x", -1));
        cJSON_Delete(o);
    }

    a = jc_arena_new(0);
    o = jc_json_parse("{\"name\": \"ireko\"}");
    copy = jc_json_dup_str(o, "name", a);
    cJSON_Delete(o);              /* the tree is gone; the copy is not */
    printf("copy after cJSON_Delete: %s\n", copy);
    jc_arena_free(a);             /* and now the copy is gone too */
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Iinclude -Isrc/json -o "$T/who" "$T/who.c" \
    src/json/jc_json.c src/json/cJSON.c src/util/jc_str.c src/util/jc_mem.c \
    src/platform/jc_snprintf.c -lm
"$T/who"
```

```
document               bool  bool~      num     num~
{"x": true}               1      1       -1       -1
{"x": 1}                 -1      1        1        1
{"x": "1"}               -1      1       -1        1
{"x": "true"}            -1      1       -1       -1
{"x": "On"}              -1      1       -1       -1
{"x": " true"}           -1     -1       -1       -1
{"x": "probably"}        -1     -1       -1       -1
{"x": 0.5}               -1      1      0.5      0.5
{"x": "200.0"}           -1     -1       -1      200
{"x": "200 lines"}       -1     -1       -1       -1
{"x": " 7"}              -1     -1       -1        7
{"x": "0x10"}            -1     -1       -1       16
{"x": "nan"}             -1     -1       -1      nan
{}                       -1     -1       -1       -1
copy after cJSON_Delete: ireko
```

No warnings from the compiler. Read the table by columns. The strict `bool`
column knows one row; the lenient one adds every unambiguous spelling and stops
at `" true"` and `"probably"`. The `-1` for `{"x": 1}` in the `bool` column is
M519. And `true` is not a number to either number reader.

**2. Ask the binary.** This step needs `make`. Each config differs only in how
it writes `pathFence`, and `doctor` reports the fence it ended up with. `HOME`
points at the scratch directory, so `doctor`'s own state goes there. The
`apiBase` is port 9 on `localhost`, where nothing listens: the reachability
check fails at once and no request leaves the machine. `lowResource` is pinned,
as the lint does, so a small machine's defaults do not change the picture.

```sh
# in the jichi checkout, after `make`; the same shell as step 1
for v in absent 1 '"on"' 0 '"ture"'; do
    kv=",\"pathFence\":$v"
    [ "$v" = absent ] && kv=
    printf '{"models":[{"name":"m","provider":"openai","model":"x",%s}],%s%s}\n' \
        '"apiBase":"http://localhost:9/v1"' '"lowResource":false' "$kv" > "$T/c.json"
    printf '%-8s ' "$v"
    HOME="$T" ./jichi --config "$T/c.json" doctor < /dev/null 2>&1 | grep 'path fence'
done
```

```
absent   ✓ path fence auto
1        ✓ path fence on (all modes)
"on"     ✓ path fence on (all modes)
0        ! path fence off
"ture"   ✗ path fence ON: pathFence has a value jichi cannot read
```

That last line is new in 0.15.0. Until then it read `"ture"   ! path fence off`, and the next
paragraphs describe that tree: they are why the line changed.

The first four rows are the rule at work: `1` and `"on"` mean on, as their
writers meant, and absence keeps the three-state default. The last row is the
subject of the next section.

**3. Run the lint that holds the boundary.** `grep -v` hides the passing
lines, so what is left is the plan and anything that failed.

```sh
# in the jichi checkout, after `make`
sh tests/smoke/config_bool_lint.sh < /dev/null | grep -v '^ok '
```

```
1..25
```

Twenty-five checks planned, no failure printed. Run it without the `grep` to
read all 25 lines; checks 1 to 3 show counts above their floors (49 against 40
in `jc_config.c`), because calls have been added since M519 set them.

## What went wrong here, and what it teaches

### One misreading, four times

M519, M530 and M534 are one defect in three places: a **presence test** followed by a **reader
that could not understand the value**. The test says "the key was written", the reader returns the
fallback, and the code acts on the fallback as if the writer had chosen it. The fence is not merely
missed; it is denied, on what looks like the writer's own instruction. M843 is the same misreading
without the presence test: `parse_policy_field` called `cJSON_IsTrue` itself, so `"deny": 1` read
as if the key were absent, and the fence was missed rather than denied. Three lessons came out of
the repairs.

- **Fix the reader, not the files.** M519 edited none of the fifteen configs:
  editing them would have left the sixteenth, someone else's, still wrong.
- **One dialect, in one place.** While each reader had its own idea of "true",
  they drifted: four readers with four different word lists in M534, and a tool that blessed a
  spelling none
  of its readers accepted. `jc_bool_from_word` is the one definition, and the
  lint's last check keeps it the only one.
- **Audit the universe, not the result.** The lint first counted callers of
  the accessor, so it could not see a reader that never called it: the YAML
  readers in M534, `parse_policy_field`'s raw `cJSON_IsTrue` in M843. Each fix
  widened what the lint looks at, from a function's callers to the question. The universe is still a list: the raw-read check reads nine named files, so
  `src/tools/jc_tool_parallel.c` still reads a model's `"write"` with a raw `cJSON_IsTrue`
  (`{"write": 1}` gives a read-only task, the safe direction), recorded by the 2026-10-08 review as
  finding F10 and outside the lint.

### What is left, measured for this chapter

The `pathFence` case is not in the record; it was measured while writing this chapter (Try it,
step 2). Its shape is in the record. The 2026-10-08 review found it one layer over, as finding F9
([`docs/analysis/2026-10-08-review-04-tools-and-delegation.md`](../analysis/2026-10-08-review-04-tools-and-delegation.md),
section 3.2): `spawn_subagent` tests `readonly` for presence and reads it with a fallback of 0, so
`"readonly": null`, or a word outside the dialect, overrides a read-only agent profile. That
finding is still open at this tree. And M534's record chose the behaviour on purpose for agent
profiles: "A non-boolean on a fence key is read conservatively: the key was written, we could not
understand it, and a fence is not something to grant by guessing."

The lenient reader fixed the *encodings*: `1`, `"on"`, `"YES"`. A value it does
not understand still falls through to `dflt`, by design. But look at what
`dflt` is at the `pathFence` call:
`jc_json_get_bool_lenient(root, "pathFence", 0)`, behind the presence test. A
typo such as `"ture"` was present, was not a word, and so became 0: **off**.
Absent, the key would have meant "auto", on in autonomous runs. So a typo on
this key still turns the fence off in the runs M519 was about, and `doctor`
shows it as one `!` line among its warnings. `doctor --unattended`, the profile for exactly those runs, fails it, with
`✗ unattended: pathFence is explicitly off`, which calls a typo an explicit choice. Three things
bound it: `--path-fence` on the command line overrides the file; an untrusted project config never
applies it (the trust step parks the key, and `doctor` lists it as not applied, described as
`pathFence: turn the path fence off`); and the sibling fence key resolves the same typo the other
way, because `shellFence` is read with a fallback of 1. `selfReview`, read like `pathFence`, turns
off on a typo too.

The comments in the tree promise more. `include/jc_str.h` says an unknown word
"leaves *out untouched, so a caller's default survives and a typo can never
flip a fence in either direction". The word is indeed not misread, but the
caller's default here is 0, and 0 is not what absence means.
`config_bool_lint.sh` check 23 tests prose on `snapshots`, a key read without a
presence test, so it cannot see this case. The agent profiles treat an unknown
`readonly:` word the same way, and the comment in
`src/command/jc_agentdef.c:parse_one` calls that "the conservative reading".
Whether "off" is the conservative reading of an unreadable fence is what the
last question below asks you. The project's answer is the next section.

### The decision (2026-11-17)

The operator answered that question on the day this chapter was written, and the answer is
recorded in [`DECISIONS.md`](../DECISIONS.md#dec-2026-11-17-unreadable-fence). The code quoted
above is how the tree read until 0.15.0. The fix is registered as
[def-415](../DEFERRED.md#def-415). Its core was built on 2026-11-26 for `pathFence` and
`selfReview`: the strict reading, the warning at load time and both `doctor` lines; and the same
day for the `readonly` a model passes to `spawn_subagent` and the `readonly:` of an agent profile,
where an unreadable value now means read-only. The rest of the list below (the stricter setting,
the telemetry event, the lint) is still open.

- **The reading fails closed.** A value jichi cannot read, on a key that sets a fence, turns the
  fence **on**, in every mode. That is stricter than leaving the key out, which for `pathFence`
  means auto. A key that grants a permission grants nothing. No setting changes this.
- **The report is loud.** By default jichi prints one line at load time naming the key, the value
  and the reading; a stricter setting makes it refuse to start. Either way a telemetry event
  records it, `doctor` shows it as a problem, and `doctor --unattended` fails it as an unreadable
  value rather than as "explicitly off".
- **One rule, in one place.** Every fence key goes through one helper, held by a lint:
  `pathFence`, `selfReview`, the `readonly` a model passes to `spawn_subagent` (finding F9 above),
  and the YAML keys M534 already reads conservatively.

Three other readings were weighed and rejected:
1. *Off*, today's. It fails open, silently, and it contradicts the two comments that promise a typo
   can never flip a fence.
2. *The key's absent meaning, with a warning.* Auto fences only the autonomous runs, so a typo
   meant as `true` would leave an interactive run unfenced: weaker than what the writer most likely
   meant.
3. *Refusing to load, by default.* The strict reading already removes the risk, and stopping every
   run on a typo, unattended runs included, adds an outage without adding safety. It stays
   available as the stricter setting.

What the decision costs, and why it was accepted. A typo meant as `false` fences a run its writer
wanted open: the fence then refuses an edit outside its scope, and the refusal and the warning both
say why, so the cost is visible and one edit to the config removes it. Today's reading has the
opposite cost, and nothing shows it: the run goes unfenced and no line says so. For a security
boundary, a visible inconvenience beats an invisible exposure. A typo is also stricter than an
absent key, which can surprise; the warning says why. And a config that carries such a value
changes behaviour on upgrade, which the CHANGELOG will say.

The lesson reaches beyond JSON: the fallback of a lenient reader is part of the policy. "Prose
falls through to the default" is right for a limit or a timeout, where the default is a safe value.
For a fence, the fallback must be the closed side, whatever the key means when it is absent.

## Check yourself

1. Fifteen configs said `"pathFence": 1`. Why did M519 change the reader and
   not the files?
2. A model calls `spawn_subagent` with `{"readonly": "yes"}`. What happened
   before M530, what happens now, and which function decides?
3. `jc_json_get_obj(o, "limit")` returns a non-NULL pointer. What do you know
   about the value of `limit`?
4. `{"limit": "200 lines"}` reaches `read_file`. What does `tu_arg_int`
   return, and does that keep M168's rule?
5. Until 0.15.0 a config that said `"pathFence": "ture"` had the fence off, and
   `doctor` reported it so. Name two ways the code could treat this case
   differently, and what each would cost. Then compare with what was decided.

<details><summary>Answers</summary>

1. The values were right and the reading was wrong. Editing fifteen files would
   have left every other config that writes `1`, including ones jichi never
   sees, still misread. Fixing the reader fixes all of them.
2. Before M530, `tu_arg_bool` accepted `"true"` and `"1"` (since M168) but not
   `"yes"` and not a number: the presence test fired, the value read as 0, and
   the helper ran writable. Now `tu_arg_bool` is `jc_json_get_bool_lenient`,
   `"yes"` is a word in `jc_bool_from_word`, and the helper is read-only.
3. Only that the key was written. `jc_json_get_obj` checks no type: the value
   may be a number, a string, a boolean, an array or `null`.
4. `"200 lines"` is not wholly a number, so `jc_json_get_num_lenient` returns
   the default, 0, and `read_file` returns every line. The rule holds for a
   number in the wrong type; prose falls through to the default by design, and
   here the default is the costly one.
5. For example: pass the key's absent meaning as the fallback and keep it (`v =
   jc_json_get_bool_lenient(root, "pathFence", -1); out->path_fence = v < 0 ? -1 : v;`; changing
   only the 0 to -1 is not enough, because the `? 1 : 0` turns -1 into 1, on), so an unreadable
   value changes nothing; or treat an unreadable value on a fence key as "on", which fences a run
   its writer may have meant to leave open (`shellFence` already does this: its fallback is 1); or
   refuse to load, or warn at load time naming the key and the value, which costs a stop or a line
   of output but makes the typo visible (M1006-a did this for `strictness.dirtyDispatch`, keeping
   the fallback and adding the warning). Each moves the cost of the typo somewhere else; the current code puts it on the fence.
   The project chose the second, with the load-time warning of the third, and the refusal as a
   stricter setting: see "The decision (2026-11-17)" above.

</details>

*Next: [chapter 7 — When the JSON is broken](ireko-07-when-the-json-is-broken.md),
where a model's nearly-JSON is repaired and a config file's comments are
stripped.*
