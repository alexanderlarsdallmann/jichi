# Algorithms, read in the program you are using — four lessons, each measured on this tree

A reading page, **ungraded**, for the self-learner. It exists because the operator asked on
2026-09-24 whether jichi should carry an algorithms course. The decision the next day was *a
reading page first, a graded track later, once the page has been read*
([`plans/2026-09-24-twelve-questions.md`](plans/2026-09-24-twelve-questions.md), item 2). This
is that page. [`CURRICULUM.md`](CURRICULUM.md)'s graded path does not depend on it, and nothing
here is graded.

**What jichi has that a textbook does not: its own implementations, running.** Each lesson
below takes one function from `src/`, says why it exists, shows its shape and its C, and then
has you **measure** it on the tree you are in. You compile a small program against the real
source file and read what it prints. The measurements printed here were taken on 2026-09-25 at
M755, and yours will differ where the tree has grown. Where they differ, trust yours.

**The house shape, from the reading guides** ([`reading/FUKABORI.md`](reading/FUKABORI.md)):
*why it exists* → *the shape* → *the C* → *prove it to yourself* → *what it taught*. The
programs are C89, like the tree. Build jichi once (`make`) so the headers are in place, and run
every command from the checkout's root.

---

## 1. Hashing — two hashes, and neither of them for a table

**Why it exists.** jichi hashes to ask *is this the same thing again?* It never uses a hash to
find a bucket. `jc_reread_hash()` (djb2, `src/util/jc_reread.c`) fingerprints a tool call's name,
arguments and result, so the loop can tell that *the same call was answered the same way*
(`src/util/jc_toolloop.c`). `jc_prefix_hash()` (FNV-1a, `src/util/jc_prefix.c`) fingerprints the
request prefix, the system prompt and the tools, so jichi notices when it changes turn after turn.
Then the provider's prompt cache has stopped matching and every request re-bills the whole
prefix (M365). There is no hash table anywhere in the tree, and [`DATA_STRUCTURES.md`](DATA_STRUCTURES.md) section 3
argues why not.

**The shape.** Both walk the bytes once and mix each one into a running value.
- djb2 starts at 5381 and does `h = h * 33 + c`. The multiply is written `(h << 5) + h`.
- FNV-1a starts at 2166136261 and does `h ^= c; h *= 16777619`. It keeps 32 bits by masking, so
  the value is the same whether `unsigned long` is 32 or 64 bits wide.

**The C** — `src/util/jc_reread.c`:

```c
unsigned long jc_reread_hash(const char *data, unsigned long len)
{
    unsigned long h = 5381u; /* djb2 */
    unsigned long i;
    if (data == NULL) {
        return h;
    }
    for (i = 0; i < len; i++) {
        h = ((h << 5) + h) + (unsigned long)(unsigned char)data[i]; /* h*33 + c */
    }
    return h;
}
```

**Prove it to yourself.** The question is not "is djb2 a good hash?", which you can look up.
It is *how does it behave on the strings this program will actually see?*, which you can only
measure. Save this as `/tmp/hc.c`:

```c
/* hash the lines on stdin with jichi's two hashes; count what collides */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jc_reread.h"
#include "jc_prefix.h"

#define MAXN 20000
static unsigned long hd[MAXN], hf[MAXN];

static int cmp(const void *a, const void *b)
{
    unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;
    return x < y ? -1 : x > y;
}

static int full_dups(unsigned long *h, int n)
{
    int i, d = 0;
    unsigned long *c = malloc((size_t)n * sizeof *c);
    memcpy(c, h, (size_t)n * sizeof *c);
    qsort(c, (size_t)n, sizeof *c, cmp);
    for (i = 1; i < n; i++) if (c[i] == c[i - 1]) d++;
    free(c);
    return d;
}

static void buckets(const char *name, unsigned long *h, int n, unsigned long m)
{
    static int load[65536];
    unsigned long i;
    int k, maxl = 0, empty = 0;
    memset(load, 0, sizeof load);
    for (k = 0; k < n; k++) load[h[k] % m]++;
    for (i = 0; i < m; i++) {
        if (load[i] == 0) empty++;
        if (load[i] > maxl) maxl = load[i];
    }
    printf("  %-6s %5lu buckets: longest chain %2d, empty %5d\n", name, m, maxl, empty);
}

int main(void)
{
    char buf[4096];
    int n = 0, s;
    unsigned long sizes[3] = { 64, 1024, 4096 };
    while (n < MAXN && fgets(buf, sizeof buf, stdin) != NULL) {
        size_t L = strlen(buf);
        if (L > 0 && buf[L - 1] == '\n') buf[--L] = '\0';
        hd[n] = jc_reread_hash(buf, (unsigned long)L);
        hf[n] = jc_prefix_hash(buf);
        n++;
    }
    printf("%d strings\n", n);
    printf("  full-width collisions: djb2 %d, fnv1a-32 %d\n", full_dups(hd, n), full_dups(hf, n));
    for (s = 0; s < 3; s++) {
        buckets("djb2", hd, n, sizes[s]);
        buckets("fnv1a", hf, n, sizes[s]);
    }
    return 0;
}
```

```sh
cc -std=c89 -Iinclude -o /tmp/hc /tmp/hc.c src/util/jc_reread.c src/util/jc_prefix.c
git ls-files | /tmp/hc
```

On 2026-09-25 (M755, 2,115 tracked paths) it printed:

```
2115 strings
  full-width collisions: djb2 0, fnv1a-32 0
  djb2      64 buckets: longest chain 47, empty     0
  fnv1a     64 buckets: longest chain 45, empty     0
  djb2    1024 buckets: longest chain 10, empty   130
  fnv1a   1024 buckets: longest chain  7, empty   135
  djb2    4096 buckets: longest chain  5, empty  2435
  fnv1a   4096 buckets: longest chain  4, empty  2465
```

**What it taught.**
- **At full width neither hash collided** on any of 2,115 paths. That is what jichi relies on:
  a collision would make two different calls look like the same one.
- **Put into buckets, both behave close to random.** For *n* keys in *m* buckets a random hash
  leaves about *m·e^(−n/m)* buckets empty: 130 of 1,024 here, and 2,444 of 4,096. Both hashes
  are within a few buckets of that.
- **The difference is in the longest chain**: djb2's is 10 at 1,024 buckets against FNV-1a's 7.
  A power-of-two table uses only the low bits. djb2's low bits are set mostly by the last few
  characters, and paths share their endings (`.md`, `.c`, `.sh`). One measurement on one tree
  is a direction, not a verdict. It is still the reason FNV-1a is the usual choice when a table
  is involved.
- **Try it on the strings your program will see.** Pipe in the tool names
  (`jichi describe --output json` lists them), your own shell history, or a word list. The
  numbers change; the method does not.

---

## 2. Edit distance — the "did you mean" behind a mistyped command

**Why it exists.** Type `/hlep` and jichi says *did you mean /help*. Call `serch_code` as a
model and it is told about `search_code`. Both come from one rule, so the human's kindness and
the model's cannot drift apart (M91, M345): `jc_str_edit_distance()`, `jc_str_close_enough()`
and `jc_str_closest()` in `src/util/jc_str.c`.

**The shape.**
- **Levenshtein distance**: the fewest insertions, deletions and substitutions, each costing 1,
  that turn one string into the other.
- It is computed by dynamic programming over two rows, `prev` and `curr`, so memory is one row
  of the shorter string rather than the whole table. Time is the product of the two lengths.
- Past 63 characters it gives up and returns the difference in length, since it is meant for
  command and tool names.
- **Close enough** means at most about half the typed length, clamped to between 2 and 4. A
  wild guess must get silence, never a misleading nudge. On a tie, the first candidate listed
  wins.

**The C** — `src/util/jc_str.c`:

```c
    for (j = 0; j <= lb; j++) {
        prev[j] = (int)j;
    }
    for (i = 1; i <= la; i++) {
        curr[0] = (int)i;
        for (j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = curr[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int m = del < ins ? del : ins;
            curr[j] = m < sub ? m : sub;
        }
        for (j = 0; j <= lb; j++) {
            prev[j] = curr[j];
        }
    }
    return prev[lb];
```

**Prove it to yourself.** Save as `/tmp/ed.c`:

```c
/* the "did you mean" behind a mistyped command, on part of jichi's own table */
#include <stdio.h>
#include "jc_str.h"
int main(void)
{
    static const char *const cmds[] = {
        "/help", "/clear", "/model", "/mode", "/plan", "/auto", "/status", 0
    };
    static const char *const typed[] = {
        "hlep", "stauts", "mdoe", "modle", "pln", "xyzzy", "h", 0
    };
    int i;
    for (i = 0; typed[i] != 0; i++) {
        const char *s = jc_str_closest(typed[i], cmds);
        printf("%-7s -> %s\n", typed[i], s != NULL ? s : "(silence)");
    }
    return 0;
}
```

```sh
cc -std=c89 -DJC_HAVE_VSNPRINTF -Iinclude -o /tmp/ed /tmp/ed.c src/util/jc_str.c src/platform/jc_snprintf.c
/tmp/ed
```

It printed:

```
hlep    -> help
stauts  -> status
mdoe    -> mode
modle   -> mode
pln     -> plan
xyzzy   -> (silence)
h       -> (silence)
```

**What it taught.**
- **`modle` suggests `/mode`, not `/model`.** Levenshtein counts swapping two neighbouring
  letters as *two* edits, so `modle` → `model` costs 2 while `modle` → `mode` (drop the `l`)
  costs 1. The Damerau variant counts a swap as one edit. Under it the two tie at 1, and
  `/model` wins because it is listed first. Which one a user meant is a question about typists,
  not about strings, and this is the case to think about before you change the algorithm.
- **The two silences are the rule working.** `xyzzy` is far from everything. `h` is one letter,
  so its threshold is the floor of 2, and even `/help` is 3 edits away.

---

## 3. A state machine over bytes — Server-Sent Events

**Why it exists.** A model's answer arrives as a stream of Server-Sent Events: text lines such as
`data: {...}`, with a blank line ending each event. The network delivers that stream in chunks of
any size, cut anywhere, including in the middle of a line or of `\r\n`. `jc_sse_feed()`
(`src/net/jc_sse.c`) turns arbitrary chunks into whole events.

**The shape.** A **state machine**: everything the parser knows between two chunks lives in the
parser, never in the chunk.
- One loop over the bytes. Bytes are collected into a line buffer until a line terminator —
  CRLF, LF, or a bare CR (all three the WHATWG spec allows).
- At the terminator the line is handled. A `data:` line appends its value to the event, joined
  with `\n`. An `event:` line sets the type. A line starting `:` is a comment. A blank line
  dispatches the event. One bit of state (`last_was_cr`) swallows the LF of a CRLF whose CR ended
  the previous chunk, so the terminator can be cut in two and the events are unchanged.
- Every buffer is capped (`JC_SSE_FIELD_MAX`), and hitting the cap sets `overflowed` rather than
  growing without limit. A hostile stream is bounded by construction.

**The C** — the whole feed loop, `src/net/jc_sse.c`:

```c
void jc_sse_feed(struct jc_sse_parser *p, const char *bytes, jc_size n)
{
    jc_size i;
    for (i = 0; i < n; i++) {
        char c = bytes[i];
        if (c == '\r') {
            /* A CR ends a line (WHATWG: bare CR is a terminator). Remember it,
             * so the LF of a CRLF -- which may arrive in the NEXT chunk -- is
             * swallowed rather than ending a second, empty line (M775). */
            handle_line(p, (p->line.data != NULL) ? p->line.data : "",
                        p->line.len);
            jc_sb_clear(&p->line);
            p->last_was_cr = 1;
        } else if (c == '\n') {
            if (p->last_was_cr) {
                p->last_was_cr = 0; /* the LF of a CRLF; the CR ended the line */
            } else {
                handle_line(p, (p->line.data != NULL) ? p->line.data : "",
                            p->line.len);
                jc_sb_clear(&p->line);
            }
        } else {
            p->last_was_cr = 0;
            if (p->line.len < (jc_size)JC_SSE_FIELD_MAX) {
                jc_sb_append_char(&p->line, c);
            } else {
                p->overflowed = 1; /* drop bytes of a pathologically long line */
            }
        }
    }
}
```

**Prove it to yourself.** The property a state machine promises is that *the chunk boundaries do
not matter*. So feed one stream at every chunk size from 1 byte up to the whole thing, and
compare. Save as `/tmp/sse.c`:

```c
/* feed one SSE stream to jichi's parser in every chunk size; do the events agree? */
#include <stdio.h>
#include <string.h>
#include "jc_sse.h"
#include "jc_str.h"

static struct jc_sb got;   /* jichi's own growable string, jc_str.h */
static int nev;

static void on_event(const struct jc_sse_event *ev, void *user)
{
    (void)user;
    nev++;
    jc_sb_append_fmt(&got, "[%s|%s]", ev->event, ev->data);
}

/* Feed `s` in pieces of `chunk` bytes; return the events, rendered. */
static int run(const char *s, size_t chunk, struct jc_sb *out)
{
    struct jc_sse_parser p;
    size_t i, n = strlen(s);
    jc_sb_clear(&got);
    nev = 0;
    jc_sse_init(&p, on_event, NULL);
    for (i = 0; i < n; i += chunk) {
        jc_sse_feed(&p, s + i, (i + chunk <= n) ? chunk : n - i);
    }
    jc_sse_free(&p);
    jc_sb_clear(out);
    jc_sb_append(out, got.data != NULL ? got.data : "");
    return nev;
}

int main(void)
{
    static const char stream[] =
        ": keep-alive comment\r\n"
        "event: delta\r\n"
        "data: {\"t\":\"Hel\"}\r\n"
        "\r\n"
        "data: line one\n"
        "data: line two\n"
        "\n"
        "data: [DONE]\n\n";
    static const char bare_cr[] = "data: only CR ends these lines\r\r";
    struct jc_sb first, cur;
    size_t c, n = strlen(stream);
    int e0, bad = 0;
    jc_sb_init(&got); jc_sb_init(&first); jc_sb_init(&cur);
    e0 = run(stream, n, &first);
    for (c = 1; c <= n; c++) {
        int e = run(stream, c, &cur);
        if (e != e0 || strcmp(cur.data, first.data) != 0) bad++;
    }
    printf("%d events: %s\n", e0, first.data);
    printf("chunk sizes 1..%lu that disagree with one read: %d\n", (unsigned long)n, bad);
    printf("a stream whose lines end in a bare CR: %d event(s)\n",
           run(bare_cr, strlen(bare_cr), &cur));
    jc_sb_free(&got); jc_sb_free(&first); jc_sb_free(&cur);
    return 0;
}
```

```sh
cc -std=c89 -DJC_HAVE_VSNPRINTF -Iinclude -o /tmp/sse /tmp/sse.c src/net/jc_sse.c src/util/jc_str.c src/util/jc_mem.c src/platform/jc_snprintf.c
/tmp/sse
```

It printed:

```
3 events: [delta|{"t":"Hel"}][|line one
line two][|[DONE]]
chunk sizes 1..102 that disagree with one read: 0
a stream whose lines end in a bare CR: 1 event(s)
```

**What it taught.**
- **The property holds.** At all 102 chunk sizes, cuts through `\r\n` and through `data:`
  included, the parser produced the same three events as a single read. That is what state in
  the parser buys. A parser that looked for events inside each chunk would fail at chunk size 1.
- **And the measurement found something — since fixed (M775).** The WHATWG HTML specification
  says a line may end in CRLF, in LF, or in *"a single U+000D CARRIAGE RETURN (CR) character not
  followed by a U+000A LINE FEED (LF) character"*. jichi used to end a line only at LF, so a
  bare-CR stream produced **no event at all** — the last line read `0 event(s)`. No provider jichi
  talks to sends bare CRs, so it never bit, but the parser now honours all three terminators and
  the line reads `1 event(s)`.
- **How the fix keeps the property.** A CR ends the line immediately; the one case that breaks the
  naive version is a CR at the end of one chunk with the LF at the start of the next, which would
  end a second, empty line. One bit of state, `last_was_cr`, swallows that LF, so the every-chunk-
  size count above stayed at **0 disagreements**. The program here is its test, and
  `tests/test_sse.c` pins it: bare CR, CRLF split across a chunk boundary, and a lone CR before an
  LF each produce the right events.

---

## 4. Ranking — BM25-lite, over the files you are reading

**Why it exists.** `codebase_search` finds the parts of a repository that matter to a question.
With hybrid retrieval on, it ranks the index's chunks twice: once by embedding similarity, and
once by the words they share with the query, with no model at all. The second ranking is
`jc_lexical_topn()` in `src/index/jc_lexical.c`. `jc_retrieve_from_index()`
(`src/index/jc_retrieve.c`) fuses the two rankings with Reciprocal Rank Fusion (`jc_rrf_fuse()`).

**The shape.** **BM25**, the ranking function behind most search engines' first pass, in its
simplest form.
- A token is a run of letters and digits, lowercased.
- For each query term, a document scores *idf · f·(k1+1) / (f + k1·(1 − b + b·dl/avgdl))*.
  *f* is how often the term occurs in the document, *dl* is the document's length in tokens,
  and *avgdl* is the average length.
- *idf = ln(1 + (N − df + 0.5)/(df + 0.5))* rewards terms that few of the *N* documents contain
  (*df* of them).
- *k1* = 1.2 makes repetition saturate. *b* = 0.75 discounts long documents.
- There is no inverted index. Every query re-reads every document, which is fine for a
  repository and would not be for the web. The top *n* are kept by insertion into a sorted
  array.

**Prove it to yourself.** Save as `/tmp/bm.c`:

```c
/* jichi's BM25-lite on files named on the command line; then its saturation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jc_lexical.h"

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (b == NULL) { fclose(f); return NULL; }
    n = (long)fread(b, 1, (size_t)n, f); b[n] = '\0'; fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    int n = argc - 2, i, k, idx[5];
    double sc[5];
    char **docs;
    if (argc < 3) { fprintf(stderr, "usage: bm QUERY FILE...\n"); return 2; }
    docs = malloc((size_t)(n + 1) * sizeof *docs);
    for (i = 0; i < n; i++) docs[i] = slurp(argv[i + 2]);
    k = jc_lexical_topn((const char *const *)docs, n, argv[1], 5, idx, sc);
    printf("query \"%s\" over %d files:\n", argv[1], n);
    for (i = 0; i < k; i++) printf("  %6.2f  %s\n", sc[i], argv[idx[i] + 2]);
    {   /* saturation: one document saying "arena" r times, among the same files */
        static char synth[65536];
        int r, reps[] = { 1, 2, 4, 8, 16, 64, 256 };
        printf("one extra document repeating \"arena\" r times (plus 200 other words):\n");
        for (r = 0; r < 7; r++) {
            int j; char *p = synth;
            for (j = 0; j < 200; j++) { memcpy(p, "word ", 5); p += 5; }
            for (j = 0; j < reps[r]; j++) { memcpy(p, "arena ", 6); p += 6; }
            *p = '\0';
            docs[n] = synth;
            k = jc_lexical_topn((const char *const *)docs, n + 1, "arena", 5, idx, sc);
            for (j = 0; j < k; j++) if (idx[j] == n) break;
            if (j < k) printf("  r=%3d  score %6.2f  (rank %d of the top 5)\n", reps[r], sc[j], j + 1);
            else printf("  r=%3d  not in the top 5\n", reps[r]);
        }
    }
    return 0;
}
```

```sh
cc -std=c89 -Iinclude -o /tmp/bm /tmp/bm.c src/index/jc_lexical.c -lm
/tmp/bm "arena lifetime" docs/*.md
```

It printed (M755, 171 top-level pages, this one among them):

```
query "arena lifetime" over 171 files:
    6.90  docs/DATA_STRUCTURES.md
    6.71  docs/RACKET_PARADIGM.md
    6.52  docs/LOW_MEMORY.md
    6.36  docs/ALGORITHMS.md
    5.90  docs/CODE_REVIEW.md
one extra document repeating "arena" r times (plus 200 other words):
  r=  1  not in the top 5
  r=  2  score   2.94  (rank 4 of the top 5)
  r=  4  score   3.16  (rank 2 of the top 5)
  r=  8  score   3.29  (rank 1 of the top 5)
  r= 16  score   3.35  (rank 1 of the top 5)
  r= 64  score   3.41  (rank 1 of the top 5)
  r=256  score   3.42  (rank 1 of the top 5)
```

**What it taught.**
- **Repetition saturates.** From 2 repetitions to 256 the score rises from 2.94 to 3.42, and
  most of that happens by 8. Saying a word 250 more times buys almost nothing. That is *k1*.
- **But a short page full of the word still wins.** The synthetic document is 200-odd words
  against pages of thousands, and *b*'s length normalisation reaches rank 1 by 8 repetitions.
  BM25 rewards *density*, and a page that is mostly one word is dense. Keep that in mind when
  `codebase_search` ranks a tiny file first.
- **What the lite version leaves out** is a list worth knowing: no stemming (*arenas* and
  *arena* are different words to it), no stop words, no phrases, and no index.

---

## What jichi does not have, and how you would know it had stopped being right

No binary search, no hash table, no tree. The one bounded memory that looks as if it wants a hash
(`jc_noprogress_note()`'s 32-entry least-recently-used set, by logical clock) is a linear scan, and
at 32 entries a scan is cheaper than hashing and following a list.
[`DATA_STRUCTURES.md`](DATA_STRUCTURES.md) makes that argument at length. This page's addition is
the question to ask of each: **what measurement would tell you the simple thing had stopped being
right?** For the scan, a profile in which it dominates. For BM25-lite, a repository large enough
that re-reading every document per query is the slowest step of a turn. For the hashes, a
collision in a fingerprint that makes two different calls look the same, which at 64 bits you
will not see and at 32 you might.

## Where this page came from, and what comes next

- The operator's decision of 2026-09-25 (twelve questions, item 2): a reading page first,
  ungraded, and a graded track only once the page has been read. Exercises stay in C.
- The graded version, if it comes, is a real milestone. The curriculum holds every graded task
  to a two-sided grader proof, and four lints must move with it
  ([`plans/2026-09-24-twelve-questions.md`](plans/2026-09-24-twelve-questions.md) section 2).
- The plan names more lessons than this page carries: the diff (`jc_diff_unified`, and why it
  is not Myers), recursive descent (`src/json/cJSON.c`), UTF-8 validation (`jc_utf8_valid`),
  glob matching (`jc_glob_match`), retry backoff, and the fork pool. They are left for the next
  pass, which should be written after someone has read this one.
