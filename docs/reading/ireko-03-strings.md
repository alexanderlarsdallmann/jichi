# Ireko 3 -- Strings

*[入れ子（いれこ）*Ireko* — the JSON layer](IREKO.md) · chapter 3 of 7*

## Why this exists

Most of what a model sends jichi is text inside quotes. A streamed reply arrives as many small
JSON events, and `src/provider/jc_provider_openai.c:oa_on_event` hands each event's data to
`cJSON_Parse`; the words of the reply are a string field inside it. A tool call is stranger. In
the OpenAI dialect its arguments are *a string that holds JSON*:
`src/provider/jc_provider_openai.c:oa_handle_tool_calls` reads `arguments` with
`jc_json_get_str`, and `src/provider/jc_provider_openai.c:build_messages` sends it back with
`cJSON_AddStringToObject`. So the arguments `{"path":"a.c"}` travel as `"{\"path\":\"a.c\"}"`,
and every quote inside them is an escape the string parser must undo before the arguments can
be parsed a second time.

The text is not only English. A model writes `é`, Japanese, an emoji. JSON may carry such a
character as raw UTF-8 bytes, or spelled with `\u` escapes, and a character above U+FFFF needs
*two* escapes. One function, `src/json/cJSON.c:parse_string`, turns all of that into bytes jichi
can use. Its sibling, `src/json/cJSON.c:pb_put_string`, writes a string back out.

There is also a cost. Every string in a document becomes its own block of memory. Until M725,
each block was sized for the worst case, and the worst case was very large. The end of this
chapter tells that story.

## The shape

```
parse_string(c)                     c->p is at the opening quote
    skip the quote
    pass 1: walk to the closing quote; after a '\' skip one byte   -> span
    out = malloc(span + 1 + 4)                                       (owned by the caller)
    pass 2: copy bytes into out; at a '\' decode one escape:
              \" \\ \/ \b \f \n \r \t   -> one byte
              \uXXXX                    -> hex4 -> code point -> utf8_encode (1 to 4 bytes)
              high \uD800-\uDBFF, then low \uDC00-\uDFFF -> one code point above U+FFFF
              anything else             -> the letter itself
    no closing quote? free(out), fail
    out[len] = '\0'; return out
```

One string, before and after:

```
source bytes (inside the quotes):   c  a  f  \  u  0  0  e  9      9 bytes: the span
decoded bytes:                      63 61 66 c3 a9                 5 bytes, then a NUL
```

The decoded string is never longer than its span. That one fact sizes the buffer.

## Who owns the string

`parse_string` returns a pointer from `malloc`, and the caller decides where it lives. For a
value, `src/json/cJSON.c:parse_value` hangs it on a new node:

```c src/json/cJSON.c:parse_value
        case '"': {
            char *s = parse_string(c);
            cJSON *node;
            if (s == NULL) {
                return NULL;
            }
            node = parse_new_item(c);
            if (node == NULL) {
                free(s);
                c->ok = 0;
                return NULL;
            }
            node->type = cJSON_String;
            node->valuestring = s;
            return node;
        }
```

For a key, `src/json/cJSON.c:parse_object` calls the same function and stores the result in the
member's `string` field. Either way, the tree owns the bytes from then on, and
`src/json/cJSON.c:cJSON_Delete` gives them back:

```c src/json/cJSON.c:cJSON_Delete
        if (item->valuestring != NULL) {
            free(item->valuestring);
        }
        if (item->string != NULL) {
            free(item->string);
        }
```

> **C sidebar — who frees what.** C has no garbage collector. Every `malloc` must be matched by
> exactly one `free`, and the code has to make clear *whose* job that `free` is. The rule here
> is "ownership moves with the pointer". `parse_string` owns `out` until it returns it. Then
> `parse_value` owns it, and if it cannot make a node it frees the string itself (the
> `free(s)` above). Once `node->valuestring = s` has run, the node owns it. Read every early
> `return` with one question: is anything still owned that nobody will free?

## Pass one: how big is this string?

`parse_string` begins by finding where the string ends, before it decodes a single byte:

```c src/json/cJSON.c:parse_string
    {
        const char *q = p;
        while (q < c->end && *q != '"') {
            if (*q == '\\' && q + 1 < c->end) {
                q++;
            }
            q++;
        }
        cap = (jc_psize)(q - p) + 1;
    }
    out = (char *)malloc(cap + 4);
    if (out == NULL) {
        c->ok = 0;
        return NULL;
    }
    g_jc_string_bytes += (size_t)cap + 4;
```

`p` points right after the opening quote and `c->end` one past the last byte of the document.
The loop walks `q` forward to the next `"`. A backslash makes it skip one extra byte, so `\"`
inside a string does not count as the end. `q - p` is the span: the number of bytes between the
quotes. The buffer gets the span, plus one for the closing NUL, plus four more. The `+ 4` is
older than M725 and has no comment of its own; the comment above
`src/json/cJSON.c:utf8_encode` asks callers to "size generously".

> **C sidebar — subtracting pointers.** When two pointers point into the same array, `q - p`
> is the number of elements between them. Its type is a signed integer type, so the code casts it
> to `jc_psize`, which this file defines as `size_t`, the unsigned type C uses for sizes. The
> `const char *q = p;` sits inside its own `{ ... }` block because C89 allows declarations
> only at the top of a block: a new block is the way to declare a variable halfway through a
> function.

Why is the span always enough? The comment above this block gives the argument: "every escape
decodes to no more bytes than it occupies (\uXXXX, six, to at most three; a surrogate pair,
twelve, to four)". A plain byte decodes to itself. `\n` is two bytes in the source and one in
the output. So the output can only shrink.

That argument holds only if pass two stops at the same quote pass one found. Pass two treats a
backslash the same way, and a `\u` escape consumes only hex digits, which are never a quote or a
backslash. Question 2 below shows what would happen if pass one skipped nothing.

The last line counts the bytes in a file-level variable, `g_jc_string_bytes`. jichi exposes it
as `src/json/cJSON.c:cJSON_jc_string_bytes`, a jichi extension that is not part of the cJSON API,
so a test can see what the parser allocated. "Try it" uses it.

## Pass two: the escapes

```c src/json/cJSON.c:parse_string
    while (p < c->end && *p != '"') {
        if (*p == '\\') {
            p++;
            if (p >= c->end) {
                break;
            }
            switch (*p) {
                case '"':  out[len++] = '"';  p++; break;
                case '\\': out[len++] = '\\'; p++; break;
                case '/':  out[len++] = '/';  p++; break;
                case 'b':  out[len++] = '\b'; p++; break;
                case 'f':  out[len++] = '\f'; p++; break;
                case 'n':  out[len++] = '\n'; p++; break;
                case 'r':  out[len++] = '\r'; p++; break;
                case 't':  out[len++] = '\t'; p++; break;
                case 'u': {
...
                default:
                    /* Unknown escape: keep the char literally. */
                    out[len++] = *p;
                    p++;
                    break;
            }
        } else {
            out[len++] = *p;
            p++;
        }
    }
```

> **C sidebar — `out[len++] = x`.** This writes `x` at index `len`, and *then* adds one to
> `len`. It is the usual C way to append to a buffer. `len` always says how many bytes are
> filled, and it is also the index of the next free byte.

Two things in this loop are more generous than JSON's grammar, which knows only the eight
one-letter escapes above and `\u`:

- **An unknown escape is kept.** `\q` becomes `q`. A strict parser would refuse the document.
- **A raw control byte is copied as it is.** The `else` branch copies any byte that is not a
  backslash, a real newline included. JSON's grammar requires the bytes below 0x20 to be
  escaped inside a string, and this parser does not check.

Neither comes with a reason in the code, so read them as what the parser does, not as a policy
someone argued for. "Try it" step 1 shows both.

A third leniency is about the encoding, not the grammar. RFC 8259 (section 8.1) requires JSON
exchanged between systems to be UTF-8, but the same `else` branch copies any byte, so a raw
`0xff`, or the raw bytes `ed a0 bd` of a surrogate, is kept as it came: `"a<ff>b"` decodes to
`61 ff 62`. None of these leniencies is forbidden to a parser: RFC 8259 (section 9) says a parser
"MUST accept all texts that conform to the JSON grammar" and "MAY accept non-JSON forms or
extensions". Whether jichi should is its own choice, which is why the end of this chapter calls
each one a decision.

After the loop, the function checks that it stopped on a quote and not at the end of the input.
If not, the string was never closed: it frees `out` and sets `c->ok = 0`, which makes every
caller up the recursion give up too, so the whole document is refused. Otherwise it steps past
the quote, writes the terminating `out[len] = '\0'`, and returns `out` to its new owner.

## `\u` escapes: four hex digits, and sometimes eight

A `\u` escape names a character by its number, four hex digits: `\u00e9` is U+00E9, `é`.
`src/json/cJSON.c:hex4` reads the four digits:

```c src/json/cJSON.c:hex4
static int hex4(const char *s)
{
    int v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        char ch = s[i];
        int d;
        if (ch >= '0' && ch <= '9') {
            d = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            d = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            d = ch - 'A' + 10;
        } else {
            return -1;
        }
        v = (v << 4) | d;
    }
    return v;
}
```

> **C sidebar — building a number four bits at a time.** One hex digit is four bits.
> `v << 4` moves what `v` holds four bits to the left, which makes room at the bottom, and
> `| d` puts the new digit there. After four digits, `v` is between 0 and 0xFFFF. That leaves
> `-1` free to mean "not a hex digit": an *error value* outside the range of good answers,
> a common C pattern.

The `\u` case uses it:

```c src/json/cJSON.c:parse_string
                case 'u': {
                    int code;
                    int ilen;
                    unsigned long cp;
                    if (p + 5 > c->end) {
                        free(out);
                        c->ok = 0;
                        return NULL;
                    }
                    code = hex4(p + 1);
                    if (code < 0) {
                        free(out);
                        c->ok = 0;
                        return NULL;
                    }
                    cp = (unsigned long)code;
                    p += 5;
                    /* Surrogate pair handling. */
                    if (cp >= 0xD800UL && cp <= 0xDBFFUL) {
                        if (p + 6 <= c->end && p[0] == '\\' && p[1] == 'u') {
                            int lo = hex4(p + 2);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000UL +
                                     ((cp - 0xD800UL) << 10) +
                                     ((unsigned long)lo - 0xDC00UL);
                                p += 6;
                            }
                        }
                    }
                    ilen = 0;
                    utf8_encode(out + len, &ilen, cp);
                    len += (jc_psize)ilen;
                    break;
                }
```

`p` points at the `u`. `p + 5 > c->end` refuses a `\u` with fewer than four bytes after it, so
`hex4` never reads past the document. A non-hex digit makes `hex4` return -1, and the whole
document is refused.

**Surrogate pairs.** Four hex digits reach only U+FFFF. JSON borrows UTF-16's trick for the
characters above that: a *high surrogate* (D800 to DBFF) followed by a *low surrogate* (DC00 to
DFFF). Each half carries ten bits of a 20-bit number: the code point minus 0x10000. The decoder joins
the two ten-bit halves and adds the 0x10000 back. The emoji 😀 is U+1F600, written
`\ud83d\ude00`:

```
(0xD83D - 0xD800) << 10   =  0x3D << 10  =  0xF400
 0xDE00 - 0xDC00          =                 0x0200
 0x10000 + 0xF400 + 0x200 =                0x1F600
```

The code does exactly this. When `cp` is a high surrogate, it looks for a second `\u` right
after it, and joins the two only when that second escape really is a low surrogate.

**What it does not refuse.** Read the `if`s again and look for the `else` that is missing:

- A high surrogate with no low one after it keeps `cp` as it is (0xD83D, say), and
  `utf8_encode` writes those three bytes, `ed a0 bd`. Those bytes are not well-formed UTF-8:
  UTF-8 has no encoding of a lone surrogate.
- A low surrogate on its own is never checked at all. It is encoded the same way.
- A high surrogate followed by a `\u` that is not a low one: the high half is kept alone, as
  above, and the second escape is decoded on the next trip round the loop.

JSON's grammar lets such an escape through, but UTF-8 has no way to write it. This parser
neither refuses it nor replaces it: it writes bytes that are not valid UTF-8. jichi has two
backstops further on, and both come after the tool. `src/chat/jc_message.c:dup_content` passes
every message body that enters the history (a user turn, assistant text, a tool result) through
`src/util/jc_utf8.c:jc_utf8_sanitize`, and `src/provider/jc_provider.c:jc_prov_print_body` does
the same once more to each printed request body. The helper `src/util/jc_utf8.c:seq_ok` refuses a
`0xED` byte followed by anything above `0x9F`, which is how an encoded surrogate starts, and the
header says `jc_utf8_sanitize` replaces "every ill-formed byte" with U+FFFD, so `ed a0 bd` becomes
three U+FFFD. So a lone surrogate from a model does not reach the next request as it came. It
does reach the tool: the arguments are decoded when the tool reads them, and nothing under
`src/tools/` sanitises, so `write_file` given `"content":"A\ud83dB"` writes the five bytes
`41 ed a0 bd 42` into the file (measured by the review of this course, with a scripted model).

**`\u0000`.** The escape for the NUL character decodes to a zero byte in the middle of `out`. C
strings end at the first zero byte, and the cJSON API has no length field, so everything after
it is lost without a word. `"a\u0000b"` reads as `"a"`. JSON allows this character in a string;
a C string cannot hold it. This one is on record: `docs/FILE_HANDLING.md` explains that
`write_file` therefore cannot detect a NUL a model sends this way, and calls it "a limitation of
the cJSON API in this codebase".

## `utf8_encode`: a code point becomes bytes

```c src/json/cJSON.c:utf8_encode
static void utf8_encode(char *buf, int *len, unsigned long cp)
{
    if (cp < 0x80UL) {
        buf[(*len)++] = (char)cp;
    } else if (cp < 0x800UL) {
        buf[(*len)++] = (char)(0xC0 | (cp >> 6));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000UL) {
        buf[(*len)++] = (char)(0xE0 | (cp >> 12));
        buf[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        buf[(*len)++] = (char)(0xF0 | (cp >> 18));
        buf[(*len)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    }
}
```

UTF-8 stores a code point in one to four bytes. The first byte says how many follow (`0xC0`
means two in all, `0xE0` three, `0xF0` four), and every following byte starts with the bits
`10` (`0x80`) and carries six bits of the number. For `é`, U+00E9, the second branch writes
`0xC0 | (0xE9 >> 6)` = `c3` and `0x80 | (0xE9 & 0x3F)` = `a9`.

> **C sidebar — an in/out parameter.** `len` is a *pointer* to the caller's counter, so the
> function can add to it. `(*len)++` means "follow the pointer, then add one to what is there".
> The parentheses matter: `*len++` would move the pointer instead.

Two limits, both read in the code. There is no check for a code point above U+10FFFF. None can
arrive: the largest pair, `\udbff\udfff`, gives 0x10FFFF. And there is no check for the
surrogate range. That is how the lone surrogate above becomes `ed a0 bd`.

## Printing a string: `pb_put_string`

The printer goes the other way. `src/json/cJSON.c:print_value` calls
`src/json/cJSON.c:pb_put_string` for every string value and for every key. The function writes
a quote, then the bytes, then a quote. Most bytes need no escape, so a fast path (M1084, read in
chapter 5 with the print buffer) copies a whole run of them at once: every byte from 0x20 up,
except `"` (0x22) and `\` (0x5c). Every other byte goes to the slow path, a `switch`:

```c src/json/cJSON.c:pb_put_string
            switch (ch) {
                case '"':  pb_puts(b, "\\\""); break;
                case '\\': pb_puts(b, "\\\\"); break;
                case '\b': pb_puts(b, "\\b");  break;
                case '\f': pb_puts(b, "\\f");  break;
                case '\n': pb_puts(b, "\\n");  break;
                case '\r': pb_puts(b, "\\r");  break;
                case '\t': pb_puts(b, "\\t");  break;
                default:
                    if (ch < 0x20) {
                        char tmp[8];
                        sprintf(tmp, "\\u%04x", (unsigned int)ch);
                        pb_puts(b, tmp);
                    } else {
                        pb_putc(b, (char)ch);
                    }
                    break;
            }
```

So the printer writes:

| byte | printed as |
|---|---|
| `"` and `\` | `\"` and `\\` |
| backspace, form feed, newline, carriage return, tab | `\b` `\f` `\n` `\r` `\t` |
| any other byte below 0x20 | `\u00XX`, lower-case hex: a bell is `\u0007` |
| `/` | `/`, not escaped |
| 0x7f (DEL) | the raw byte |
| any byte 0x80 or above | the raw byte: no `\u`, and no check that the bytes are valid UTF-8 |

Two consequences. First, apart from `\u0000` (the parser already cut that string short, above), a round trip keeps
the *value* but not always the *spelling*:
`"caf\u00e9"` prints back as the raw bytes of `café`, and `\/` prints back as `/`. Second,
the printer writes back whatever bytes the parser kept, including the `ed a0 bd` of a lone
surrogate. The backstop in `jc_prov_print_body` runs after the printer, so it sees those bytes.

The `sprintf` call is worth a look. The house rule is "never call `sprintf`", and
`tests/smoke/sprintf_lint.sh` enforces it. This file is the one first-party exception: the lint's
header says the printer "must stay jc_snprintf-free to remain swappable as a pair" with
upstream cJSON (M171), and it allowlists the four `sprintf` lines in `src/json/cJSON.c` by their
exact text. Here `tmp[8]` holds `\u` plus four digits plus the NUL, seven bytes.

## Try it

You need only a C compiler: `src/json/cJSON.c` uses nothing but the C library. The outputs below
were measured on x86-64 Linux with glibc and gcc 13. Nothing prints the name of your temporary
directory, so the output on such a machine should match line for line.

**1. Decode: `é` two ways, a surrogate pair, a lone surrogate, a raw newline.** Each line shows
the decoded string's length and its bytes in hex.

```sh
# in the jichi checkout (no build needed: this compiles src/json/cJSON.c only)
T=$(mktemp -d)
cat > "$T/strings.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

/* Parse DOC; if it parses, print the decoded string's bytes in hex. */
static void show(const char *label, const char *doc)
{
    cJSON *root = cJSON_Parse(doc);
    const unsigned char *b;

    printf("%-20s -> ", label);
    if (root == NULL) {
        printf("refused\n");
        return;
    }
    printf("len %lu:", (unsigned long)strlen(root->valuestring));
    for (b = (const unsigned char *)root->valuestring; *b != '\0'; b++) {
        printf(" %02x", *b);
    }
    printf("\n");
    cJSON_Delete(root);
}

int main(void)
{
    show("caf\\u00e9", "\"caf\\u00e9\"");
    show("caf<c3><a9> (raw)", "\"caf\xc3\xa9\"");
    show("\\ud83d\\ude00", "\"\\ud83d\\ude00\"");
    show("\\ud83d alone", "\"\\ud83d\"");
    show("\\ud83d then \\u0041", "\"\\ud83d\\u0041\"");
    show("\\u00g9", "\"\\u00g9\"");
    show("a<0a>b (raw)", "\"a\nb\"");
    show("\\q", "\"\\q\"");
    show("a\\u0000b", "\"a\\u0000b\"");
    show("\"abc (no end)", "\"abc");
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/strings" "$T/strings.c" src/json/cJSON.c -lm
"$T/strings"
```

```
caf\u00e9            -> len 5: 63 61 66 c3 a9
caf<c3><a9> (raw)    -> len 5: 63 61 66 c3 a9
\ud83d\ude00         -> len 4: f0 9f 98 80
\ud83d alone         -> len 3: ed a0 bd
\ud83d then \u0041   -> len 4: ed a0 bd 41
\u00g9               -> refused
a<0a>b (raw)         -> len 3: 61 0a 62
\q                   -> len 1: 71
a\u0000b             -> len 1: 61
"abc (no end)        -> refused
```

The compiler prints nothing: no warnings. The escaped and the raw `é` decode to the same two
bytes. The pair becomes one four-byte character. The lone surrogate is accepted as the three
bytes `ed a0 bd`, and so is the high half that a plain `A` follows. A bad hex digit refuses the
whole document. The raw newline and `\q` are accepted. `a\u0000b` comes back as `a`.

**2. Print a string with control characters.** `visible()` shows every byte outside printable
ASCII as `<hex>`, so nothing invisible hides in the output.

```sh
# in the jichi checkout, the same shell as step 1
cat > "$T/print.c" <<'EOF'
#include <stdio.h>
#include "cJSON.h"

/* Print S with every byte outside printable ASCII shown as <hex>. */
static void visible(const char *s)
{
    const unsigned char *b;
    for (b = (const unsigned char *)s; *b != '\0'; b++) {
        if (*b >= 0x20 && *b < 0x7f) {
            putchar(*b);
        } else {
            printf("<%02x>", *b);
        }
    }
    putchar('\n');
}

static void print_it(cJSON *item)
{
    char *text = cJSON_PrintUnformatted(item);
    visible(text);
    cJSON_free(text);
    cJSON_Delete(item);
}

int main(void)
{
    print_it(cJSON_CreateString("tab\t nl\n cr\r quote\" back\\ slash/"));
    print_it(cJSON_CreateString("bell\a esc\x1b del\x7f caf\xc3\xa9"));
    print_it(cJSON_Parse("\"caf\\u00e9 \\ud83d\\ude00 \\ud83d\""));
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/print" "$T/print.c" src/json/cJSON.c -lm
"$T/print"
```

```
"tab\t nl\n cr\r quote\" back\\ slash/"
"bell\u0007 esc\u001b del<7f> caf<c3><a9>"
"caf<c3><a9> <f0><9f><98><80> <ed><a0><bd>"
```

The table above, byte for byte: the three named controls in the input (tab, newline, carriage
return) get their letters (backspace and form feed would too; the input has neither), bell and escape get
`\u00XX`, `/` and DEL pass through, and every byte from 0x80 up is copied raw. The third line is
a parse followed by a print. The escapes are gone, and the lone surrogate's three bytes come out
as they went in.

**3. What strings cost today.** The document is an array of 400 two-letter strings followed by
one 64 KiB string, the shape of M725's test. `cJSON_jc_string_bytes()` reports what
`parse_string` allocated.

```sh
# in the jichi checkout, the same shell as step 1
cat > "$T/cost.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

#define SHORT 400                 /* short strings: "ab" */
#define TAIL  65536               /* one long string at the end */

static char doc[1 + SHORT * 5 + 1 + TAIL + 2 + 1];

int main(void)
{
    char *p = doc;
    size_t before;
    cJSON *root;
    int i;

    *p++ = '[';
    for (i = 0; i < SHORT; i++) {
        memcpy(p, "\"ab\",", 5);
        p += 5;
    }
    *p++ = '"';
    memset(p, 'x', TAIL);
    p += TAIL;
    *p++ = '"';
    *p++ = ']';
    *p = '\0';

    before = cJSON_jc_string_bytes();
    root = cJSON_Parse(doc);
    printf("document:      %lu bytes, %d strings\n",
           (unsigned long)strlen(doc), cJSON_GetArraySize(root));
    printf("string bytes:  %lu\n",
           (unsigned long)(cJSON_jc_string_bytes() - before));
    cJSON_Delete(root);
    return 0;
}
EOF
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/cost" "$T/cost.c" src/json/cJSON.c -lm
"$T/cost"
```

```
document:      67540 bytes, 401 strings
string bytes:  68341
```

Each `"ab"` costs its span of 2, plus 1, plus 4: 7 bytes, and 400 of them make 2,800. The tail
costs 65,536 + 5 = 65,541. The total is about the size of the document.

**4. Put the old bound back and measure again** (measured for this chapter). Before M725 the
buffer was sized by `(c->end - p) + 1`, the rest of the input. A copy of today's file with that
one line changed allocates exactly what the old parser did, and the counter still counts:

```sh
# in the jichi checkout, the same shell as step 1
sed 's/cap = (jc_psize)(q - p) + 1;/cap = (jc_psize)(c->end - p) + 1;/' \
    src/json/cJSON.c > "$T/before-m725.c"
grep 'cap = (jc_psize)' "$T/before-m725.c"
cc -std=c89 -pedantic -Wall -Wextra -Isrc/json -o "$T/cost-old" "$T/cost.c" "$T/before-m725.c" -lm
"$T/cost-old"
```

```
        cap = (jc_psize)(c->end - p) + 1;
document:      67540 bytes, 401 strings
string bytes:  26683743
```

The `grep` line shows that the `sed` changed the line; if it printed `(q - p)`, the file has moved
on and the copy is not the old parser. The same 67,540-byte document now costs 26,683,743 bytes
of strings, about 390 times as much. Every short string near the start was charged for nearly
the whole document after it.

## What went wrong here, and what it teaches

### M725: the allocation nobody looked at

The record is `docs/ROADMAP.md`, M725, and the decision is
[DECISIONS.md, M725](../DECISIONS.md#dec-m725).

`jichi index` builds embeddings: it sends chunks of code to an embeddings model and gets back
long lists of floats, as JSON. An earlier milestone, M700, had profiled its memory with valgrind's
massif tool and blamed the floats: every number in the reply became a cJSON node. So the plan
(D3) was to stop building those nodes and read the floats straight out of the text. It was built
carefully, with bit-identity tests and a fuzz property.

Then it was measured. On a 199-file corpus the fast path moved massif's peak heap from
219,112,560 B to 217,122,976 B, about 1 %. The massif tree said why, "as M700's own output had in
its second line": the allocations were in `parse_string`. It sized every buffer to the rest of
the input, `cap = (c->end - p) + 1`, a worst-case bound, and kept that buffer as the node's key or
value. An LM Studio batch was about 1.5 MB of pretty-printed text with about 260 strings, so
about 190 MB.

The fix is the first pass you read above. The same corpus then peaked at 25,596,080 B, 88 % less.
The fast path on top of the fix changed that peak by zero bytes, so it was removed: about 240
lines "for nothing measurable". DECISIONS.md also records an alternative that was rejected:
`realloc` the buffer down after decoding. It still reserves the rest of the input once per string,
and pays a copy.

One more finding is the subtle one. **Resident memory barely moved**, measured on a larger run than
the massif numbers above (the whole jichi tree: 2,072 files, 13,297 chunks): peak RSS was 136,080 kB
before, 130,692 kB after. jichi sets glibc's mmap threshold to 128 KB (`src/util/jc_memtrim.c:jc_mem_tune`), so each
oversized buffer was a fresh mapping of which a page or two was ever touched: "address space, not
memory, on this Linux". The fix stays anyway: "The requested bytes are real where nothing pages
them lazily: FreeMiNT, ..., a strict commit limit, or a `ulimit -v`."

What it teaches:

- **Measure where the allocations are before you design the fix.** The plan was built on a
  correct-looking explanation that matched the numbers "to within 1% by coincidence".
- **A worst-case bound is fine for a moment and expensive when it is kept.** Reserving the rest
  of the input for one decode would cost little if the buffer were freed right away. Here it
  became the node's value and lived as long as the tree.
- **Hold the fix with an instrument.** `cJSON_jc_string_bytes` exists so that
  `tests/test_json.c`'s `test_json_string_alloc` can hold the parser's string bytes "under twice
  the document". The record says it was red on the old sizing, and the test also checks that the
  counter counted (`spent > 64 * 1024`): a counter that stayed at zero would pass "under twice the
  document" and prove nothing. Step 4 above shows by hand what the old sizing does to that
  counter.

### What this chapter found, and did not fix

Writing this chapter measured three behaviours that the records searched for it (the roadmap,
`docs/DEFERRED.md`, the decisions, the anecdotes and `docs/analysis/`) do not discuss: a lone
surrogate is accepted and becomes ill-formed UTF-8; a raw control byte inside a string is
accepted; an unknown escape is kept as its letter. Each is shown in step 1. They are reported for
review, not changed here. Whether each is a defect or a useful leniency toward a model's
imperfect JSON is a decision, and no one has made it yet.

## Check yourself

1. The source `"\ud83d\ude00"` has a span of 12 bytes. How many bytes does `parse_string`
   allocate for it, and how many does it fill?
2. Suppose pass one did not skip the byte after a backslash. What happens to the string
   `"\"\"\"\"\"\"\"\""` (eight escaped quotes)?
3. A model sends `{"path":"notes\u0000.txt"}`. What path does jichi read?
4. Which bytes does `pb_put_string` write as `\u` escapes? Does it ever escape a byte of `é`?
5. After M725, resident memory on Linux barely moved. Why was the fix kept?

<details><summary>Answers</summary>

1. `cap` is 12 + 1 = 13, and `malloc` gets 13 + 4 = 17 bytes. The decoded emoji fills 4, plus
   the NUL: 5.
2. Pass one would stop at the second byte, the first escaped quote. The span would be 1, so
   `malloc(1 + 1 + 4)` gives 6 bytes. Pass two decodes all eight quotes and adds the NUL: 9
   bytes into 6, a heap overflow. The two passes must agree on where the string ends.
3. `notes`. The escape decodes to a zero byte, and a C string ends there. Nothing reports it.
4. Only the bytes below 0x20 that have no letter of their own (`\b`, `\f`, `\n`, `\r`, `\t` do).
   No byte of 0x80 or above is ever escaped, so `é` is printed as its two raw bytes.
5. The requested bytes are real where nothing pages them lazily: FreeMiNT, a strict commit limit,
   a `ulimit -v`. And massif's peak heap fell by 88 %.

</details>

*Next: [chapter 4 — Numbers](ireko-04-numbers.md), where a JSON number becomes a `double`, an
`int` and, on the way out, text again.*
