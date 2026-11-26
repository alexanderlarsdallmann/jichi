# 入れ子（いれこ）*Ireko* — the JSON layer

*The fifth of the jichi source reading guides.
- The beginner edition is [案内（あんない）*Annai* — the guided tour](ANNAI.md).
- The expert edition is [深掘り（ふかぼり）*Fukabori* — the deep dive](FUKABORI.md).
- The traced runs are [追跡（ついせき）*Tsuiseki*](TSUISEKI.md).
- The harness is [手綱（たづな）*Tazuna*](TAZUNA.md).
- [記録（きろく）*Kiroku*](KIROKU.md) is a reading guide too, but it reads the
  record, not the code.
- This guide reads jichi's JSON layer: the parser, the tree and the printer in
  `src/json/cJSON.c`, the helpers in `src/json/jc_json.c`, the repair of a
  model's nearly-JSON in `src/util/jc_jsonrepair.c`, and the comments and
  trailing commas a configuration file may carry, `src/convert/jc_jsonc.c`.*

*Ireko* means "nested boxes": a set of boxes that fit one inside the next. A
JSON document is that. An object holds values, a value can be an array, the
array can hold another object, and so on down. The parser that reads it calls
itself once for every box it opens.

Every request jichi sends to a model is JSON, and so is every reply it reads.
So are its configuration files, its sessions, its telemetry, and the messages it
exchanges with editors, tool servers and language servers. All of it passes
through about 2,000 lines of C89 that jichi wrote for itself.

## Why read it

- **It is jichi's own code.** The parser implements the interface of the cJSON
  library, but it is not a copy of that library, and never was (M171). Every
  line can be read, and every line is jichi's to answer for.
- **It is a complete parser you can hold in your hands.** A recursive-descent
  parser, a tree of linked nodes, and a printer, in strict C89.
  `src/json/cJSON.c` needs nothing but the C library, so you can compile a short
  program against it and watch it work without building jichi at all.
- **The hard parts are the small ones.** How deep may an input make the parser
  recurse? What does `é` turn into? Does a `double` printed and read back
  come back as the same number? The last one is not a theoretical question:
  `docs/PLATFORMS.md` records 75 unit failures on the `armeb` toolchain, "the
  known JSON double round-trip class".
- **It meets several kinds of writer.** Some JSON comes from jichi itself, some from
  a person, some from another program (an editor over ACP, a tool server, a config
  that `jichi-convert` translates), and some from a model that only *nearly* writes
  JSON. Chapters 6 and 7 show how the reading changes with the writer: strict for
  jichi's own records, lenient for what a person, another program or a model wrote,
  and, for a model's tool call that does not parse at all, a narrow, checked repair.

## Who this is for

Anyone who has finished [Annai](ANNAI.md) chapters 1 to 5. Two other readers
are in mind too:
- **a junior developer** who has used a JSON library and wants to see what one
  does inside, or who has to change how jichi reads a value;
- **an agent** that reads and writes JSON all day, in every tool call it makes,
  and may want to know what happens to the text it sends.

You need to read C at the level Annai teaches. Each chapter explains the C idioms
it meets the first time it needs them: a `struct` with `next`, `prev` and
`child` pointers, recursion, `strtod`, who owns memory and who frees it.

## Before you start

Compile and run a tiny program against the parser. It needs only the C library
and the maths library (`-lm`):

```sh
# in the jichi checkout
T=$(mktemp -d)
cat > "$T/boxes.c" <<'EOF'
/* boxes.c - parse a JSON text, then print the tree back out. */
#include <stdio.h>
#include "cJSON.h"

int main(void)
{
    const char *text = "{\"box\": [1, {\"box\": [2, {\"box\": [3]}]}]}";
    cJSON *root;
    char *out;

    root = cJSON_Parse(text);
    if (root == NULL) {
        printf("not JSON\n");
        return 1;
    }
    out = cJSON_Print(root);
    if (out != NULL) {
        printf("%s\n", out);
        cJSON_free(out);
    }
    cJSON_Delete(root);
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/boxes" "$T/boxes.c" src/json/cJSON.c -lm
"$T/boxes"
```

```
{
  "box": [
    1,
    {
      "box": [
        2,
        {
          "box": [
            3
          ]
        }
      ]
    }
  ]
}
```

Three boxes, one inside the next. The compiler printed nothing: no warnings.

Have the source open while you read. The chapters point at code by name, and
each pointer is an instruction: open that file and go to that function.

## The chapters

| # | Chapter | What it reads |
|---|---|---|
| 1 | [Everything passes through here](ireko-01-everything-passes-through-here.md) | where jichi uses JSON; the `cJSON` struct; who owns a tree |
| 2 | [Boxes inside boxes: the recursive-descent parser](ireko-02-boxes-inside-boxes.md) | `cJSON_Parse` and the `parse_*` functions; looking a key up |
| 3 | [Strings](ireko-03-strings.md) | `parse_string`, `\u` escapes and UTF-8; how a string is printed |
| 4 | [Numbers](ireko-04-numbers.md) | `parse_number`, the `int` view and its clamp; printing a `double` so it reads back the same |
| 5 | [Building and printing](ireko-05-building-and-printing.md) | the `Create`/`Add` functions, appending, the print buffer |
| 6 | [Who wrote this JSON?](ireko-06-who-wrote-this-json.md) | `jc_json.c`: strict and lenient reading |
| 7 | [When the JSON is broken](ireko-07-when-the-json-is-broken.md) | `jc_jsonrepair.c` and `jc_jsonc.c` |

Each chapter has the same sections: **Why this exists**, **The shape**, a walk
through the code, **Try it**, **What went wrong here, and what it teaches**, and
**Check yourself**. The incidents in the fifth section are told from the tree's
own records and cited by milestone, or measured in the chapter and marked as
such.

## Conventions

- **Anchors.** Code is named as `file.c:function_name`: open the file and search
  for the name. Line numbers are never used, because they rot. Every anchor in
  the chapters resolves in the tree, and `tests/smoke/reading_refs_lint.sh`
  checks that on every `make smoke`.
- **Tagged quotes.** A code block whose opening fence names a file, as in the
  fence ```` ```c src/json/cJSON.c:parse_value ````, is copied from that file
  line for line. A line holding only `...` marks lines left out.
  `tests/smoke/reading_quotes_lint.sh` checks that each quoted line is still in
  its file, and that the tag names the function the block is in. Neither lint
  checks the order of the quoted lines. A block with no tag is pseudo code, a
  diagram or an invented example, and is never presented as the source.
- **Try it blocks.** The first line of every command block says where it runs,
  usually `# in the jichi checkout`. Every command was run exactly as printed,
  and its output is shown as it came. Scratch files go to a throwaway directory
  made by `mktemp -d`, never into the checkout. A headless `./jichi` run ends
  with `< /dev/null`. The C programs are C89 and compile with
  `-std=c89 -pedantic -Wall -Wextra` with no warnings. Your temporary paths will
  differ from any shown.

## How this guide was written

The chapters were drafted on 2026-11-17 from the tree at `81f9d35c`, by four
Claude agents working from one shared brief and the same rules: quote only what
the code says, run every published command first, claim nothing unmeasured. Four
fresh reviewers then checked every chapter against the code and the record, and
the supervising session checked each of their findings, applied them, and ran the
reading lints over the whole course. Every published command was re-run on
2026-11-18 at `8c51afe5`: the JSON code had not changed, and two counts had grown
(chapter 1's file counts and chapter 4's lint scan), because M1196 and M1198 had
added two C files.

The design, and the record of what the review finds, is kept on the guide's
design page,
[`docs/plans/2026-11-17-ireko-the-json-course.md`](../plans/2026-11-17-ireko-the-json-course.md).
If you find a sentence the code contradicts, the code wins. Please report it.
