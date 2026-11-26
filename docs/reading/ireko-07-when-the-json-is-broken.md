# Ireko 7 -- When the JSON is broken

*[入れ子（いれこ）*Ireko* — the JSON layer](IREKO.md) · chapter 7 of 7*

## Why this exists

The parser of chapters 2 to 4 is strict about structure: one
comma too many, as in `{"a": 1,}`, and `cJSON_Parse` returns NULL. (Inside a string it is looser than JSON: chapter 3 measures three things it accepts that JSON forbids, a lone surrogate, a raw control byte and an unknown escape. None of them is what this chapter repairs.) That is the
right rule for a parser. But two kinds of writer send jichi text that is
*almost* JSON.

- **A model.** When a model calls a tool, it writes the tool's arguments as JSON
  text, one token at a time, the same way it writes prose
  ([Annai 4](annai-04-tool-calling.md) tells the whole protocol). jichi cannot make
  that text valid; it can only read what arrives. The repair module's header names
  what arrives:

```c include/jc_jsonrepair.h
 * Small models emit almost-JSON: trailing commas, missing closing brackets,
 * Python literals (True/False/None -- very common from Python-trained
 * models), or single quotes throughout. Candidate #3 of the small-model
 * agentics proposal: repair conservatively, ONLY after a real parse failure,
 * and count every attempt -- never guess so hard that a wrong repair
 * executes a tool with arguments the model didn't mean.
```

- **A person.** Someone editing a configuration file wants to write a comment
  beside a setting, and does not want a trailing comma to break the file. jichi's
  own documentation had fifteen config examples with `//` comments in them, and
  until M459 jichi's own config reader refused every one.

jichi answers the two writers differently, and the difference is this chapter's
lesson. A model's arguments are **repaired**: only after a real failure, by a few
narrow rules, checked, counted, and the model is told. A person's configuration
is read in a **dialect**: always, silently, because deleting a comment cannot
change any value. Two small files hold the two answers:
`src/util/jc_jsonrepair.c` (177 lines) and `src/convert/jc_jsonc.c` (121 lines).

The repair is M148, "conservative args repair + schema-echo errors", built from
section 5.2 of [the small-model agentics proposal](../proposals/2026-07-small-model-agentics.md).
Before it, a model that sent broken arguments got one flat line back, "could not
parse tool arguments as JSON", and no help.

## The shape

```
A MODEL wrote it: a tool call's arguments, e.g.  {'path': 'a.c',}

jc_tool_execute
  jc_tool_normalize_args
    args = cJSON_Parse(text)                     strict (chapters 2 to 4)
    if args is NULL:
        if the reply was cut off:  give up       (M334)
        fixed = jc_jsonrepair(text)
            1. pass_quotes                ' -> "  only if there is no " at all
            2. pass_python_literals       True/False/None -> true/false/null
            3. pass_commas_and_closers    drop ,} and ,]   append missing } ]
            4. cJSON_Parse(result)        still not JSON?  -> NULL
        args = cJSON_Parse(fixed)
  args still NULL?  -> error + the tool's expected shape   (the "schema echo")
  run the tool; if the arguments were repaired, a note rides the result (M353)

A PERSON wrote it: a configuration file

config_parse_file
    text = jc_jsonc_strip(text)    always: comments and trailing commas go
    root = jc_json_parse(text)     strict
    if root is NULL:  "malformed config JSON: <path>"
```

## Where a model's arguments come from

On an OpenAI-compatible wire, a tool call names a function and carries its
arguments as **a string that holds JSON text**: `"arguments": "{\"path\": \"a.c\"}"`.
The reply streams, so the string arrives in pieces, and
`src/provider/jc_provider_openai.c:oa_handle_tool_calls` appends each piece to the
call's slot. Only when the reply is complete does anyone try to read the string as
JSON. You will see the raw string in Try it, step 5: the run prints it as
`"args":"{path: \"f.txt\"}"`.

## Where the repair runs: only after a real failure

Every tool call passes through `src/tools/jc_tool.c:jc_tool_normalize_args` before
the tool runs. Its first step is the whole M148 contract:

```c src/tools/jc_tool.c:jc_tool_normalize_args
    /* Step 1: parse the raw arguments. */
    args = (arguments_json[0] != '\0') ? cJSON_Parse(arguments_json)
                                        : cJSON_CreateObject();
    if (args == NULL && app != NULL && app->last_response_truncated) {
        /* M334: truncated response -> reject, not repairable. */
        if (repaired_out) *repaired_out = 0;
        return NULL;
    }
    if (args == NULL) {
        /* M148: conservative repair only after a real parse failure. */
        char *fixed = jc_jsonrepair(arguments_json);
...
        if (fixed != NULL) {
            args = cJSON_Parse(fixed);
            repaired = (args != NULL);
            free(fixed);
        }
    }
```

Read it in order:
- An empty string counts as `{}`. That is not a repair: a tool with no
  arguments may be called with none.
- Valid JSON is **never** repaired. The repair runs only when the strict parse
  has already failed, so a string that merely looks odd, `"True story"`, is
  safe from it.
- A reply that was cut off is **not** repaired, whether the model's output limit cut
  it or jichi's own per-call size limit did (1 MiB of arguments per tool call, A-07 /
  M1090; the error message names which). The story of why is in "What went wrong" below.
- The lines elided above send one telemetry event per attempt, `args_repair`,
  with `kind` `"json"` and `ok` true or false. That was the proposal's
  "instrumentation-first" rule: count the repairs before arguing about them.

Two functions call `jc_tool_normalize_args`: the executor,
`src/tools/jc_tool.c:jc_tool_execute`, and the edit-scope fence,
`src/chat/jc_agent.c:call_out_of_scope_path`. M981 made the fence call the same
helper, so that the fence checks the same repaired tree the tool will run on (with
one exception, the cut-off reply, below). The fence passes NULL for `app`, so the telemetry event is written once, by the
executor. The cut-off check above needs `app` too, and the fence passes none
(`src/chat/jc_agent.c:call_out_of_scope_path` calls `jc_tool_normalize_args` with
`app` set to NULL), so the fence does not make that check: on a reply that was cut
off, the fence repairs the text and checks the path in the repaired tree, a tree
the executor would then refuse to run. When that path lies outside the edit scope,
the fence's refusal reaches the model first, and it names the wrong problem: the
kind of misreport (M333) that M981's NULL return exists to prevent. The review of
this course measured it with a scripted cut-off reply; the measurement is recorded
in [the course's design page](../plans/2026-11-17-ireko-the-json-course.md),
section 4, as a defect found while writing.

> **C sidebar — who frees what.** `jc_jsonrepair` returns memory from `malloc`,
> or NULL. The caller **owns** that memory and must give it back with `free`,
> which is why the block above calls `free(fixed)` straight after the parse.
> C89 says `free(NULL)` does nothing, so code may free a pointer that might be
> NULL without checking it first. The Try it program relies on that.

## Pass 1: `pass_quotes`

Single quotes are the boldest guess, so the rule is the narrowest:

```c src/util/jc_jsonrepair.c:pass_quotes
static char *pass_quotes(const char *s)
{
    char *out;
    jc_size i;
    if (strchr(s, '"') != NULL || strchr(s, '\'') == NULL) {
        return NULL; /* ambiguous or nothing to do */
    }
    out = (char *)malloc(strlen(s) + 1);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; s[i] != '\0'; i++) {
        out[i] = (s[i] == '\'') ? '"' : s[i];
    }
    out[i] = '\0';
    return out;
}
```

`static` in front of a function means it is visible only inside this file; the
passes and their helper `jr_word_char` are private, and `jc_jsonrepair` is the
one public name.
`strchr(s, c)` returns a pointer to the first `c` in `s`, or NULL.

The pass refuses if the text holds even one double quote. With `{'path': "a.c'}`
there is no way to know which quotes were meant as quotes, so the honest answer
is "I don't know", and NULL says it.

When it does run, the swap is **blind**: every `'` becomes `"`, wherever it sits.
This pass does not use the in-string state machine described next. The comment at
the top of the file says otherwise:

```c src/util/jc_jsonrepair.c
/* Is s[i] inside a JSON string, given the scan state? The passes below all
 * walk with this little state machine so string CONTENT is never touched. */
```

It reads like the description of a helper function, and the file has none: each
of passes 2 and 3 carries its own copy of the state machine. For those two the
comment is true. For pass 1 it is not, and "What went wrong" shows what that
costs.

Why does this pass run first? `jc_jsonrepair` answers in its own comment: quote
conversion "changes what 'inside a string' means for the later, string-aware
passes". After the swap, `'True story'` has become `"True story"`, and pass 2 can
see that it is a string and leave it alone.

## Pass 2: `pass_python_literals`, and the state machine

A model trained on a lot of Python writes `True`, `False` and `None`. JSON spells
them `true`, `false` and `null`. The pass must change a `True` that is a value
and never one inside a string. It tells them apart with three states:

```
               "                        \
   OUTSIDE ──────────▶ IN STRING ──────────▶ AFTER BACKSLASH
      ▲                  │   ▲                    │
      └─────── " ────────┘   └──── any one char ──┘
```

Two `int` variables hold the state: `in_str` and `esc`. Here is the loop:

```c src/util/jc_jsonrepair.c:pass_python_literals
    jc_size i = 0;
    int in_str = 0;
    int esc = 0;
    while (s[i] != '\0') {
        char c = s[i];
        if (esc) {
            esc = 0;
        } else if (in_str) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                in_str = 0;
            }
        } else if (c == '"') {
            in_str = 1;
        } else if (!jr_word_char(i > 0 ? s[i - 1] : ' ')) {
            if (strncmp(s + i, "True", 4) == 0 && !jr_word_char(s[i + 4])) {
                memcpy(s + i, "true", 4);
                i += 4;
                continue;
            }
...
        }
        i++;
    }
```

- **After a backslash**, the next character is skipped whatever it is. That is
  how `"say \"True\""` stays one string: the `\"` does not end it.
- **Inside a string**, nothing is changed. Only a backslash or a closing `"`
  moves the state.
- **Outside**, the pass looks for a whole word. `src/util/jc_jsonrepair.c:jr_word_char`
  says whether a character can be part of a word (a letter, a digit or `_`). The
  character before must not be one, and neither may the character after. So
  `TrueX` is left alone, and the final parse refuses it.

The swaps work **in place**, on the copy `jc_jsonrepair` made (never on the model's own string), with `memcpy` writing over
the old bytes. That is safe only because each pair has the same length: `True`
and `true` are 4 bytes, `False` and `false` 5, `None` and `null` 4. The ROADMAP's M148 entry
calls them "same-length in-place swaps", and the comment above the function says "the replacements never grow"; a swap that grew the text would need a
new buffer.

> **C sidebar — `isalnum((unsigned char)c)`.** `jr_word_char` casts before it
> calls `isalnum`. On many platforms a plain `char` is signed, so a byte above
> 127 (part of a UTF-8 letter) is a negative number, and passing a negative value
> other than `EOF` to the `<ctype.h>` functions is undefined behaviour. The cast
> is the standard fix.

## Pass 3: `pass_commas_and_closers`, and `JR_MAX_DEPTH`

The last pass walks the same state machine and fixes two things: a comma before
a closer, and closers that never came.

```c src/util/jc_jsonrepair.c:pass_commas_and_closers
static char *pass_commas_and_closers(const char *s)
{
    struct jc_sb sb;
    char stack[JR_MAX_DEPTH];
    int depth = 0;
...
        } else if (c == '{' || c == '[') {
            if (depth >= JR_MAX_DEPTH) {
                jc_sb_free(&sb);
                return NULL;
            }
            stack[depth++] = (c == '{') ? '}' : ']';
        } else if (c == '}' || c == ']') {
            if (depth > 0) {
                depth--;
            }
        } else if (c == ',') {
            /* Trailing comma? Peek at the next non-whitespace char. */
            jc_size k = i + 1;
            while (s[k] == ' ' || s[k] == '\t' || s[k] == '\n' ||
                   s[k] == '\r') {
                k++;
            }
            if (s[k] == '}' || s[k] == ']' || s[k] == '\0') {
                continue; /* drop it */
            }
        }
        jc_sb_append_char(&sb, c);
    }
```

**The stack.** `stack` is an array of 64 `char`s. Each time an opener appears
outside a string, the closer it will need goes on top: `}` for `{`, `]` for `[`.
Each closer that appears takes the top one off. Whatever is left at the end is
exactly the list of closers that never came, innermost on top:

```c src/util/jc_jsonrepair.c:pass_commas_and_closers
    /* An unterminated string cannot be repaired conservatively. */
    if (in_str) {
        jc_sb_free(&sb);
        return NULL;
    }
    while (depth > 0) {
        jc_sb_append_char(&sb, stack[--depth]);
    }
    return jc_sb_finish(&sb);
```

Popping from the top appends them in nesting order: `{"a": {"b": [1, 2` gets
`]}}`. An unterminated string is refused: where it should end is a guess about
content, and content is what the repair promises not to touch.

**`JR_MAX_DEPTH` is 64.** C does not check array bounds: writing a 65th entry
into `stack` is undefined behaviour, and in practice it overwrites whatever memory lies next to the array, here on the call stack. So an opener that would make 65 containers open at once
makes the pass give up instead. The count is of containers still open, not of openers seen: `[[],[], ... ,]` with 66 empty arrays is repaired. Note that the pass is a loop, not a recursion, so
the cap protects the array, not the call stack. The parser has its own, larger
bound, `JC_JSON_MAX_DEPTH`, 256, on its one recursion in
`src/json/cJSON.c:parse_value` (chapter 2 tells M472). So text nested between 65
and 256 deep, which the parser would read once repaired, is refused by the
repair. The parser's comment says real payloads nest under about 12.

**The comma.** On a comma outside a string, the pass looks ahead past spaces,
tabs and newlines. If the next character is `}`, `]` or the end of the text, the
comma is dropped: `continue` jumps to the next loop turn and skips the append at
the bottom. The look is forward only, so `[,]` loses its comma and becomes `[]`.

**What it does not check.** A closer pops the top entry without asking whether it
matches. `{"a": [1, 2}` pops the `]` with the `}`, appends one more `}`, and
produces `{"a": [1, 2}}`. That is not JSON, and the final step refuses it. The
pass never moves a closer and never inserts one in the middle.

> **C sidebar — `struct jc_sb`.** A growable string builder: a heap buffer, its
> length and its capacity, doubled as needed
> ([Annai 5](annai-05-one-tool-all-the-way-down.md) meets it first).
> `jc_sb_finish` hands the buffer to the caller, who must `free` it, and leaves
> the builder empty. `jc_sb_free` throws the buffer away. Every early `return
> NULL` above calls `jc_sb_free` first, so a refusal leaks nothing.

## `jc_jsonrepair`: a repair is only a repair if it parses

The public function runs the passes in order, then checks the result with the
strict parser:

```c src/util/jc_jsonrepair.c:jc_jsonrepair
    pass_python_literals(work);
    next = pass_commas_and_closers(work);
    free(work);
...
    /* A repair is only a repair if the result actually parses. */
    chk = cJSON_Parse(next);
    if (chk == NULL) {
        free(next);
        return NULL;
    }
    cJSON_Delete(chk);
    return next;
```

- `work` is the output of `pass_quotes`, or, if that pass declined, a copy the
  function makes with `malloc` and `strcpy`: pass 2 writes into the text, and
  the caller's string is `const`. The `strcpy` is safe, because the buffer was
  sized from the same string one line before; it is one of the audited sites
  that `tests/smoke/sprintf_lint.sh` allows by the exact text of the line (the file name and `strcpy(work, s);`), not by a line number.
- The last lines keep the promise in the header: the function returns text that
  `cJSON_Parse` **accepts**, or NULL. `tests/fuzz/jc_fuzz_targets.c:t_jsonrepair`
  holds that promise as a fuzzing property: any non-NULL result that does not
  parse aborts the fuzzer.
- The check builds a tree and deletes it, and the caller then parses the same
  text again. Two parses is the price of a function that returns plain text: it
  is pure, has no state, and is tested with string tables alone.

## What it refuses to guess

The header states the one deliberate refusal:

```c include/jc_jsonrepair.h
 * REJECTED (deliberately): unquoted keys -- the tokenizer ambiguity is too
 * high for a conservative repairer; revisit only with a failing corpus.
```

"Revisit only with a failing corpus" means: do not widen the repair on a hunch,
only when recorded failures show the need. Seven of the unit test's sixteen
cases are refusals, and its header says why: "just as important -- the inputs a
CONSERVATIVE repairer must refuse".

```c tests/test_jsonrepair.c:test_jsonrepair
    /* Mixed quotes are ambiguous: refuse. */
    null_case("{'path': \"a.c'}");
...
    /* A word literal fused to an identifier is NOT a Python literal. */
    null_case("{\"a\": TrueX}");
```

The others are unquoted keys, an unterminated string, garbage, and an empty or
NULL input. Reading the passes adds more that nothing repairs: a comment, a
missing comma between two values, `TRUE` in capitals, a closer of the wrong
kind, and more than 64 open containers. Try it, step 3, shows several.

## What the model reads back

The repair is half of M148. The other half is what the model is told, because a
model can correct itself only from what it reads.

**When nothing works**, `src/tools/jc_tool.c:jc_tool_execute` returns an error
result, and adds the tool's expected shape:

```c src/tools/jc_tool.c:jc_tool_execute
            /* Unrepairable JSON -> error with schema hint. */
            {
                struct jc_sb es;
                jc_sb_init(&es);
                jc_sb_append(&es, "error: could not parse tool arguments as JSON");
                args_schema_hint(t, &es);
```

`src/tools/jc_tool.c:args_schema_hint` asks the tool for its schema, from the same
function that describes the tool's parameters to the model, and writes out each
property's name and type, then the required list:

```c src/tools/jc_tool.c:args_schema_hint
    props = cJSON_GetObjectItem(schema, "properties");
    if (cJSON_IsObject(props)) {
        jc_sb_append(es, ". Expected arguments: {");
        cJSON_ArrayForEach(p, props) {
            const char *ty = jc_json_get_str(p, "type", "any");
...
    req = cJSON_GetObjectItem(schema, "required");
```

This is the "schema echo". The ROADMAP's M148 entry calls it "the M38
give-the-model-the-exact-bytes philosophy applied to arguments": do not lecture
the model about JSON; show it the shape it should have sent. For `read_file` the
model reads `Expected arguments: {path: string, offset: integer, limit: integer};
required: path.` (Try it, step 5). It names keys and types, not descriptions, and only at the top level: for `apply_patch` the echo is `Expected arguments: {edits: array}; required: edits.`, which says nothing of the `path`, `old_string` and `new_string` each edit must carry.

**When the repair works**, the tool runs, and since M353 one note rides its
result: the arguments "were not valid JSON and were conservatively repaired
before running", followed by the three usual causes. The section below tells why.

## JSON with comments: a person's dialect

`src/convert/jc_jsonc.c` began in the config converter, which reads other tools'
configuration files. Its header says why:

```c include/jc_jsonc.h
 * opencode configs are JSONC: they may carry // line comments, block comments,
 * and trailing commas, none of which cJSON accepts. jc_jsonc_strip removes
 * those (string-literal and escape aware, so a "//" or "," inside a string is
 * preserved) and returns plain JSON. It is idempotent on already-clean input,
 * so it is safe to run on every JSON source unconditionally.
```

**`src/convert/jc_jsonc.c:strip_comments`** copies `s` into `out`. Outside a
string, it skips a comment instead of copying it:

```c src/convert/jc_jsonc.c:strip_comments
        if (c == '/' && p[1] == '/') {
            p += 2;
            while (*p != '\0' && *p != '\n') {
                p++;
            }
            continue; /* keep the newline for the next iteration */
        }
        if (c == '/' && p[1] == '*') {
            p += 2;
            while (*p != '\0' && !(*p == '*' && p[1] == '/')) {
                p++;
            }
            if (*p != '\0') {
                p += 2; /* skip the closing */
            }
            *o++ = ' ';
            continue;
        }
```

A line comment stops before its newline, so the newline stays. A block comment
becomes **one space**, "so adjacent tokens do not merge": `[1/*x*/2]`
becomes `[1 2]`, which the parser refuses, rather than `[12]`, a different number
that it would accept. Inside a string, the code copies a backslash and the
character after it together, so `\"` cannot end the string. That is the same
three-state machine as pass 2, written with a pointer step instead of an `esc`
flag.

**`src/convert/jc_jsonc.c:strip_trailing_commas`** then edits the result in
place:

```c src/convert/jc_jsonc.c:strip_trailing_commas
        if (c == ',') {
            const char *q = p + 1;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') {
                q++;
            }
            if (*q == '}' || *q == ']') {
                p++; /* drop the comma */
                continue;
            }
        }
        *o++ = c;
        p++;
```

> **C sidebar — two pointers over one buffer.** In `strip_trailing_commas`, `p` reads and `o` writes, and
> both start at `s`, the same array. (In `strip_comments` the same names walk two different arrays, `s` and `out`.) Each turn reads at least as many bytes as it writes, so `o`
> never passes `p`, and a write never lands on a byte not yet read. That is how
> a string shrinks in place with no second buffer.

Comments go first for a reason: in `"b", // the last tag` the comma is followed by
a comment, not by `]`. Only after the comment is gone does the comma stand before
the closer. Note one small difference from the repair: here a comma before the
**end of the text** stays, so a file that ends in a comma still fails to parse.

**`src/convert/jc_jsonc.c:jc_jsonc_strip`** puts the two together:

```c src/convert/jc_jsonc.c:jc_jsonc_strip
    n = (jc_size)strlen(src);
    buf = (char *)jc_arena_alloc(a, n + 1);
    if (buf == NULL) {
        return NULL;
    }
    strip_comments(src, buf);
    strip_trailing_commas(buf);
    return buf;
```

The result is never longer than the input, so `n + 1` bytes always suffice.
The memory comes from the caller's **arena** and is never freed on its own; it
goes when the whole arena does
([Fukabori 3](fukabori-03-the-three-arena-lifetime-model.md) argues the three
arenas). Compare the repair, which hands back `malloc`'d memory the caller must
free.

**Who calls it**, at this tree:
- jichi's own config, `src/config/jc_config.c:config_parse_file` (M459);
- the config writer, `src/config/jc_configedit.c:jc_config_edit_load`,
  `src/config/jc_configedit.c:jc_configedit_path_is_object` and
  `src/config/jc_configedit.c:jc_configedit_path_has_comment` (M924, below);
- the converter, `src/convert/jc_convert_core.c:jc_convert_run`,
  `src/convert/jc_convert_core.c:jc_convert_detect` and
  `src/convert/jc_convert_claude.c:jc_convert_claude`;
- workflow files, `src/util/jc_workflow.c:jc_workflow_parse`;
- setup, `src/setup/jc_setup.c:jc_setup_merge_config` and
  `src/setup/jc_setup.c:jc_setup_inherit_config`.

jichi's own config reader got it late, at M459: it was already reading other tools'
files this way. (M459's ROADMAP entry, written afterwards at M463, does not mention this change; the comment below and the M924 entry give it to M459, and the commit is `36135cc45`, 2026-08-16.) The comment in `config_parse_file` tells the story and ends with
the argument for safety:

```c src/config/jc_config.c:config_parse_file
     * A strict widening: every config that parsed before still parses, since
     * stripping comments and trailing commas from a file that has neither is
     * the identity. */
    {
        char *clean = jc_jsonc_strip(text, a);
        if (clean != NULL) {
            text = clean;
        }
    }
```

`tests/test_jsonc.c:test_jsonc_idempotent_on_clean` pins the identity case, and
the trap beside it: `{"s":"x,]"}`, a string that looks like a trailing comma,
must come back unchanged.

## Repairing a model, accepting a person

| | `jc_jsonrepair`: a model's arguments | `jc_jsonc_strip`: a person's config |
|---|---|---|
| When it runs | only after `cJSON_Parse` has failed | always, before the parse |
| What it changes | quotes, literals, commas, closers | comments and trailing commas |
| Can it change a value? | yes: it guesses, so it can guess wrong | no: neither thing it removes is part of a value |
| Who checks the result | the function itself, with `cJSON_Parse` | the caller's parse; a failure is an error |
| Memory | `malloc`; the caller frees | the caller's arena |
| Who is told | the model (the note) and the operator (telemetry) | nobody: a dialect is not an error |

The two are not interchangeable. The JSONC strip is too weak for a model: it
fixes a trailing comma and none of the other three classes, and it tells nobody.
The repair would harm a person: a config file cut short by a bad copy would come
back "repaired" into a smaller, valid config, with settings silently missing.
For a person's file, a loud "malformed config JSON" is the kind answer.

## Try it

Steps 1 to 4 and 6 need no build of jichi at all: the repair compiles with three
other files of the tree, the strip with one. Your temporary paths will differ.

**1. Build a tiny program around `jc_jsonrepair`.** `jc_jsonrepair.c` needs the
string builder (`src/util/jc_str.c`), which formats through
`src/platform/jc_snprintf.c`, and the parser (`src/json/cJSON.c`) for its check.

```sh
# in the jichi checkout
T=$(mktemp -d)
cat > "$T/repair.c" <<'EOF'
/* repair.c - hand each argument to jc_jsonrepair, print what comes back. */
#include <stdio.h>
#include <stdlib.h>
#include "jc_jsonrepair.h"

int main(int argc, char **argv)
{
    int i;
    char *fixed;

    for (i = 1; i < argc; i++) {
        fixed = jc_jsonrepair(argv[i]);
        printf("%s  ->  %s\n", argv[i], fixed != NULL ? fixed : "(NULL: refused)");
        free(fixed);
    }
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Iinclude -Isrc/json -o "$T/repair" "$T/repair.c" \
    src/util/jc_jsonrepair.c src/util/jc_str.c src/platform/jc_snprintf.c \
    src/json/cJSON.c -lm
echo "cc rc=$?"
```

```
cc rc=0
```

No warnings: the compiler printed nothing.

**2. What it repairs.** Single quotes, Python literals, a missing closer, a
trailing comma, quotes and a `,]` inside a string, the pass order, and a stray
comma:

```sh
# in the jichi checkout, the same shell
"$T/repair" "{'path': 'a.c'}" '{"on": True, "nil": None}' '{"a": {"b": [1, 2' \
    '{"path": "a.c",}' '{"msg": "say \"True\", then stop,]",}' \
    "{'note': 'True story', 'on': True}" '[,]'
```

```
{'path': 'a.c'}  ->  {"path": "a.c"}
{"on": True, "nil": None}  ->  {"on": true, "nil": null}
{"a": {"b": [1, 2  ->  {"a": {"b": [1, 2]}}
{"path": "a.c",}  ->  {"path": "a.c"}
{"msg": "say \"True\", then stop,]",}  ->  {"msg": "say \"True\", then stop,]"}
{'note': 'True story', 'on': True}  ->  {"note": "True story", "on": true}
[,]  ->  []
```

In the fifth line, the escaped quotes, the `True` and the `,]` inside the string
all survive; only the comma outside it goes. In the sixth, pass 1 turned the
quotes first, so pass 2 saw `"True story"` as a string.

**3. What it refuses.**

```sh
# in the jichi checkout, the same shell
"$T/repair" '{path: "a.c"}' "{'path': \"a.c'}" '{"path": "a.c' '{"a": [1, 2}' \
    '{"a": 1 // why}' '{"a": TRUE}'
```

```
{path: "a.c"}  ->  (NULL: refused)
{'path': "a.c'}  ->  (NULL: refused)
{"path": "a.c  ->  (NULL: refused)
{"a": [1, 2}  ->  (NULL: refused)
{"a": 1 // why}  ->  (NULL: refused)
{"a": TRUE}  ->  (NULL: refused)
```

**4. `JR_MAX_DEPTH`.** 64 open brackets are closed; 65 are refused.

```sh
# in the jichi checkout, the same shell
"$T/repair" "$(printf '%064d' 0 | tr 0 '[')" "$(printf '%065d' 0 | tr 0 '[')"
```

```
[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[  ->  [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]
[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[  ->  (NULL: refused)
```

**5. The real binary: what the model reads back.** No model is called. The
smoke tier's scripted server, `tests/tools/mockmodel.c`, stands in for one, so
the broken arguments are exactly the bytes in the script: first `{path:
"f.txt"}` (unquoted key), then `{"path": "f.txt",}` (trailing comma). `HOME`
points at the throwaway directory, so the run reads no settings from your own
home directory and writes its telemetry into the throwaway one.

```sh
# in the jichi checkout, after `make jichi smoke-tools`
J=$PWD/jichi
W=$(mktemp -d)
mkdir "$W/cap"
printf 'hello from f.txt\n' > "$W/f.txt"
cat > "$W/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {path: "f.txt"}
rule
  count 2
  tool read_file {"path": "f.txt",}
rule
  text DONE
EOF
tests/tools/mockmodel --script "$W/r.mm" --capture "$W/cap" \
    --port-file "$W/port" --max-requests 3 > /dev/null &
until [ -s "$W/port" ]; do sleep 1; done
cat > "$W/c.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$(cat "$W/port")/v1","apiKey":"x","roles":["chat"]}]}
EOF
(cd "$W" && HOME="$W" "$J" --config c.json --auto --no-session --output jsonl \
    -p "read f.txt" < /dev/null > out.jsonl)
grep -E '"type":"tool_(call|result)"' "$W/out.jsonl"
```

```
{"v":1,"type":"tool_call","name":"read_file","args":"{path: \"f.txt\"}","id":"c1"}
{"v":1,"type":"tool_result","name":"read_file","is_error":true,"id":"c1","preview":"error: could not parse tool arguments as JSON. Expected arguments: {path: string, offset: integer, limit: integer}; required: path."}
{"v":1,"type":"tool_call","name":"read_file","args":"{\"path\": \"f.txt\",}","id":"c1"}
{"v":1,"type":"tool_result","name":"read_file","is_error":false,"id":"c1","preview":"     1\thello from f.txt\n\n[note: the arguments you sent for this call were not valid JSON and were conservatively repaired before running -- send strictly valid JSON (double-quoted keys and strings, no trailing commas, no Python literals)]"}
```

The first call is refused, with the schema echo. The second is repaired, the file
is read, and the note rides the result. `tests/smoke/repair_note.sh` holds the
second half of this on every `make smoke`.

**6. The JSONC strip.** It needs `src/util/jc_mem.c` for its arena, and nothing
else.

```sh
# in the jichi checkout, the same shell as steps 1 to 4
cat > "$T/strip.c" <<'EOF'
/* strip.c - print what jc_jsonc_strip makes of standard input. */
#include <stdio.h>
#include "jc_jsonc.h"

int main(void)
{
    char buf[4096];
    size_t n;
    struct jc_arena *a;
    char *out;

    n = fread(buf, 1, sizeof buf - 1, stdin);
    buf[n] = '\0';
    a = jc_arena_new(0);
    if (a == NULL) {
        return 1;
    }
    out = jc_jsonc_strip(buf, a);
    fputs(out != NULL ? out : "(NULL)\n", stdout);
    jc_arena_free(a);
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Iinclude -o "$T/strip" "$T/strip.c" \
    src/convert/jc_jsonc.c src/util/jc_mem.c
echo "cc rc=$?"
cat > "$T/c.jsonc" <<'EOF'
{
  // the model to use
  "model": "m", /* its short name */
  "apiBase": "http://127.0.0.1:1234/v1",
  "tags": ["a", "b", // the last tag
  ],
}
EOF
"$T/strip" < "$T/c.jsonc"
printf '[1/*x*/2]\n' | "$T/strip"
printf '{"on": True}\n' | "$T/strip"
```

```
cc rc=0
{
  
  "model": "m",  
  "apiBase": "http://127.0.0.1:1234/v1",
  "tags": ["a", "b" 
  ]
}
[1 2]
{"on": True}
```

The `//` inside the URL survives, all three comments (two `//` line comments and one block comment) and both trailing commas go, and
the lines keep their places (the blank second line was the first comment). The
block comment became one space, so `1` and `2` did not merge. `True` is left
alone: the strip is not a repair.

## What went wrong here, and what it teaches

### M334: the repair that made a cut-off reply look whole

The ROADMAP tells it as measured. In one run, a model tried to write a whole test
file in one `write_file` call. The reply hit exactly 8192 output tokens, the model's
configured `maxTokens`, and was cut off in the middle of the arguments. The
strict parse failed. "M148's conservative repair then succeeded: it closed the
braces and produced a valid object with no fields." The tool answered that `path`
and `content` were required. The model, told its argument shape was wrong, kept sending
the same thing: 197 `write_file` calls in all, for 6,977,850 tokens, the most expensive run of
that engagement. M333 records the same run from the fence's side: only 20 of the 197
reached the tool's "'path' and 'content' are required"; the other 177 were refused as
"out of edit-scope", because the edit-scope fence read the missing `path` as a path
outside the scope. Two diagnoses, both wrong, and neither said "cut off".

The record states the cause in one sentence: "M148 cannot tell a trailing comma
from a severed response, and for the second the repair is exactly the wrong
move." Measured for this chapter, three shapes a cut can leave:

```sh
# in the jichi checkout, the same shell as steps 1 to 4
"$T/repair" '{' '{"path": "t.c"' '{"path": "t.c", "content": "int ma'
```

```
{  ->  {}
{"path": "t.c"  ->  {"path": "t.c"}
{"path": "t.c", "content": "int ma  ->  (NULL: refused)
```

A cut inside a string is refused, but a cut between values is "repaired" into a
smaller, valid object. (The record does not show where its cut fell.) The bytes
alone cannot tell a forgotten brace from an amputation. The cause had to come from
somewhere else, and it was on the wire all along: the provider's
`finish_reason` (on Anthropic's wire, `stop_reason`), which jichi had never read. M334 reads both. Today the early return at
the top of `jc_tool_normalize_args` skips the repair (M334 put the check inline in `jc_tool_execute`; M981 moved it into this shared helper), and `jc_tool_execute`
tells the model that its reply was cut off, "NOT a problem with their shape".

It was not the end. M858 measured that a gateway ended a cut **tool-call** reply
with `finish_reason: "tool_calls"`, not `"length"`, so M334's check never fired
there: "`args_truncated` fired 0 times in 90 runs, while M148's repair ran 830
times." `src/provider/jc_provider_openai.c:oa_mark_cut_by_usage` now also calls a
reply cut when its output tokens reach the requested ceiling.

**The lesson:** a repair sees only the text. Before you repair, ask whether
something outside the text already knows why it is broken.

### M353: a repair that told only the operator

Until M353, the unrepairable path taught (the schema echo) and the repairable path
told only the operator, through a log line and telemetry. The ROADMAP puts it
from the model's seat: "I send broken JSON, someone quietly fixes it, the tool
returns a clean result, I learn 'my JSON was fine', and I keep the habit
forever". The note now rides every repaired call's result, per call, because
"thirty repairs mean thirty chances the conservative fix guessed wrong". The
conversation history still keeps the model's original broken string; the note
beside the result is the correction. On the next request jichi sends that string
back, and because a provider expects the arguments to be an object, a string that
does not parse goes out wrapped, as `{"_unparsed_arguments": "..."}`
(`src/provider/jc_provider.c:jc_prov_args_wire`; M145 for Anthropic, M269 for the
OpenAI dialect). So on its next turn the model sees its own broken call, labelled
unparsed, beside the repaired result and the note.

### M924: the reader accepted a comment, and the writer did not

After M459, jichi's config **reader** stripped comments. The **writer** behind
`config set`, `jc_config_edit_load`, still used strict `cJSON_Parse`. A commented
file failed there, and the failure was silently mapped to a fresh empty object.
`config set` then saved that empty object plus the one key being set. A reviewer
measured it: a trusted project config lost its model, `pathFence`,
`permissions.deny` and `configEditable`, and stayed trusted in its wiped form.

The fix makes the writer read what the reader reads:

```c src/config/jc_configedit.c:jc_config_edit_load
                char *clean = jc_jsonc_strip(text, a);
                if (clean != NULL) {
                    root = jc_json_parse(clean);
                } else {
                    root = jc_json_parse(text);
                }
```

and `jc_configedit_path_is_object` refuses to edit a non-empty file that does not
parse to an object. One cost remains, and the success message states it: the
save writes the parsed tree back out, so the comments are lost, and the `.bak`
copy keeps the original.

**The lesson** is the one M981 later applied to tool calls: when two pieces of
code read the same input, they must read it through the same function. The
edit-scope fence and the executor share `jc_tool_normalize_args` for exactly
this reason.

### Measured for this chapter: the quote swap reaches inside strings

`pass_quotes` turns every `'` into `"`, as its loop shows. Usually that breaks
the text and the final check refuses it. But a single-quoted string may escape
its apostrophe as `\'`, and `\"` is valid JSON:

```sh
# in the jichi checkout, the same shell as steps 1 to 4
"$T/repair" "{'msg': 'don't'}" "{'msg': 'it\\'s'}"
```

```
{'msg': 'don't'}  ->  (NULL: refused)
{'msg': 'it\'s'}  ->  {"msg": "it\"s"}
```

The second result parses, so it passes the check, and its value is `it"s`, not
the `it's` that was sent. The header says each pass is "string-aware -- content
inside JSON strings is never touched", and the comment at the top of the file
says every pass walks the state machine. `pass_quotes` does neither. The M353
note would still tell the model that its arguments were repaired, but not how.
This was found while writing this chapter; it is reported with it, not fixed.

**The lesson:** a validator checks the shape, not the meaning. `cJSON_Parse` can
refuse a repair that is not JSON. It cannot refuse one that is valid JSON and
says something else. That is the failure M148's header warned about, "a wrong
repair executes a tool with arguments the model didn't mean", and the only
defence against it is a narrow rule.

## Check yourself

1. A model sends `{"cmd": "ls",}`. Which pass changes it, what does the tool
   receive, and what else does the model read in the result?
2. `pass_quotes` gives up if the text holds a single `"` anywhere. Why is that
   the conservative choice?
3. In `{"note": "None left", "x": None}`, pass 2 changes one `None` and not the
   other. Which variable protects the first one?
4. A config line reads `"apiBase": "http://h/v1", // the gateway`. Why does the
   `//` in the URL survive `strip_comments`, and the comment not?
5. Why would `jc_jsonrepair` be the wrong tool for a person's config file?

<details><summary>Answers</summary>

1. Pass 3 drops the comma, because the next character past it is `}`. The tool
   receives `{"cmd": "ls"}`. The result carries the M353 note: the arguments were
   not valid JSON and were repaired.
2. With both kinds of quote present, nothing says which `'` was meant as a quote
   and which as an apostrophe. A guess could produce valid JSON with a different
   meaning, so the pass returns NULL.
3. `in_str`. The first `None` sits between two `"`, so the loop is in the
   in-string state and changes nothing there.
4. The URL is inside a string: `strip_comments` copies everything between the
   quotes without looking for comment markers. The `//` after the comma is
   outside any string, so it starts a comment that runs to the end of the line.
5. It guesses. A config file cut short would be completed with closers into a
   smaller, valid config, and settings would vanish without a word. A person
   needs the loud "malformed config JSON" instead, and the JSONC strip only
   removes things that carry no value.

</details>

*Back to [the course index](IREKO.md), for the seven chapters and how this guide
was written.*
