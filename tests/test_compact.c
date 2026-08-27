/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_compact.c - offline tests for the pure compaction core (jc_compact.c).
 *
 * Only the pure helpers are exercised here (estimate / find_cut / render and
 * the jc_history_drop_front mutation). The model-call orchestration in
 * jc_compact_run is verified end-to-end against a real model. */

#include "jc_test.h"
#include "jc_app.h"
#include "jc_compact.h"
#include "jc_json.h"
#include "jc_message.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_utf8.h"

#include <stdlib.h>
#include <string.h>

/* Fill `buf` with `n` copies of 'a' and NUL-terminate. */
static void fill(char *buf, int n)
{
    memset(buf, 'a', (size_t)n);
    buf[n] = '\0';
}

static void test_estimate(void)
{
    struct jc_history h;
    char big[401];

    jc_history_init(&h);
    JC_CHECK(jc_compact_estimate_tokens(&h) == 0);

    fill(big, 400);
    jc_history_add(&h, JC_ROLE_USER, big); /* ~400/4 + 4 = 104 tokens */
    JC_CHECK(jc_compact_estimate_tokens(&h) >= 100);

    jc_history_free(&h);

    /* String estimator (M41): ~bytes/4, NULL-safe, no per-message overhead. */
    JC_CHECK(jc_compact_estimate_text(NULL) == 0);
    JC_CHECK(jc_compact_estimate_text("") == 0);
    JC_CHECK(jc_compact_estimate_text("abcd") == 1);
    JC_CHECK(jc_compact_estimate_text(big) == 100); /* 400 / 4 */
}

/* Build a representative six-message session and return it. */
static void build_session(struct jc_history *h, char *huge)
{
    struct jc_message *a;
    char args[401];

    fill(args, 400);
    fill(huge, 5000); /* exceeds RENDER_TRUNC (4000) */

    jc_history_add(h, JC_ROLE_USER, "first request");          /* 0 */
    a = jc_history_add(h, JC_ROLE_ASSISTANT, "working");        /* 1 */
    jc_msg_add_tool_call(a, "id1", "read_file", args);
    jc_history_add_tool_result(h, "id1", huge, 0);             /* 2 */
    jc_history_add(h, JC_ROLE_ASSISTANT, "done with first");   /* 3 */
    jc_history_add(h, JC_ROLE_USER, "second request");        /* 4 */
    jc_history_add(h, JC_ROLE_ASSISTANT, "answer");           /* 5 */
}

static void test_find_cut(void)
{
    struct jc_history h;
    char huge[5001];
    jc_size cut;

    jc_history_init(&h);
    build_session(&h, huge);

    /* Tight budget keeps only the most recent user turn (index 4). */
    cut = jc_compact_find_cut(&h, 100);
    JC_CHECK(cut == 4);
    JC_CHECK(jc_history_get(&h, cut)->role == JC_ROLE_USER);

    /* Generous budget fits the whole history from index 0 => nothing to fold. */
    cut = jc_compact_find_cut(&h, 100000);
    JC_CHECK(cut == 0);

    jc_history_free(&h);

    /* Too few messages => never compact. */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "hi");
    jc_history_add(&h, JC_ROLE_ASSISTANT, "hello");
    JC_CHECK(jc_compact_find_cut(&h, 1) == 0);
    jc_history_free(&h);
}

static void test_render(void)
{
    struct jc_history h;
    struct jc_arena *ar = jc_arena_new(0);
    char huge[5001];
    char *txt;

    jc_history_init(&h);
    build_session(&h, huge);

    txt = jc_compact_render_transcript(&h, 4, ar);
    JC_CHECK(txt != NULL);
    JC_CHECK(strstr(txt, "User: first request") != NULL);
    JC_CHECK(strstr(txt, "Assistant: working") != NULL);
    JC_CHECK(strstr(txt, "-> called read_file(") != NULL);
    JC_CHECK(strstr(txt, "Tool result: ") != NULL);
    JC_CHECK(strstr(txt, "...[truncated]") != NULL);
    /* end=4 excludes the second user request. */
    JC_CHECK(strstr(txt, "second request") == NULL);

    jc_history_free(&h);
    jc_arena_free(ar);
}

static void test_drop_front(void)
{
    struct jc_history h;
    char huge[5001];
    long before, after;

    jc_history_init(&h);
    build_session(&h, huge);
    before = jc_compact_estimate_tokens(&h);

    jc_history_drop_front(&h, 4);
    JC_CHECK(jc_history_len(&h) == 2);
    JC_CHECK_STR(jc_history_get(&h, 0)->content, "second request");
    JC_CHECK(jc_history_get(&h, 0)->role == JC_ROLE_USER);
    JC_CHECK_STR(jc_history_get(&h, 1)->content, "answer");

    after = jc_compact_estimate_tokens(&h);
    JC_CHECK(after < before);

    /* Over-large count clamps to the length. */
    jc_history_drop_front(&h, 99);
    JC_CHECK(jc_history_len(&h) == 0);

    jc_history_free(&h);
}

/* The history rewrite that compaction persists: after applying a summary at the
 * cut, message 0 is the kept user message with the summary prepended, the
 * summarized prefix is gone, and the tail is intact. This is exactly the shape
 * jc_session_save serializes, so it stands in for the persistence guarantee
 * without a network round-trip. */
static void test_apply(void)
{
    struct jc_history h;
    struct jc_arena *ar = jc_arena_new(0);
    char huge[5001];
    struct jc_message *m0;

    jc_history_init(&h);
    build_session(&h, huge); /* 6 messages; cut=4 keeps [4,6) */

    jc_compact_apply(&h, 4, "SUMMARY: codename ZEPHYR-9, number 4271", ar);

    JC_CHECK(jc_history_len(&h) == 2);
    m0 = jc_history_get(&h, 0);
    JC_CHECK(m0->role == JC_ROLE_USER);
    JC_CHECK(strstr(m0->content,
                    "[Earlier conversation summarized to save context]") != NULL);
    JC_CHECK(strstr(m0->content, "SUMMARY: codename ZEPHYR-9") != NULL);
    JC_CHECK(strstr(m0->content, "[Most recent request follows]") != NULL);
    /* The original content of the kept message is preserved after the marker. */
    JC_CHECK(strstr(m0->content, "second request") != NULL);
    /* The tail message is untouched. */
    JC_CHECK_STR(jc_history_get(&h, 1)->content, "answer");

    jc_history_free(&h);
    jc_arena_free(ar);
}

/* M30: windowing the prefix so each summarizer call fits a small context. */
static void test_window(void)
{
    struct jc_history h;
    char huge[5001];
    jc_size e0, e1;

    jc_history_init(&h);
    build_session(&h, huge);

    /* A generous budget swallows the whole history in one window. */
    JC_CHECK(jc_compact_window_end(&h, 0, 1000000) == jc_history_len(&h));

    /* A tiny budget still advances by at least one message (the huge tool
     * result at index 2 alone exceeds the budget). */
    e0 = jc_compact_window_end(&h, 0, 10);
    JC_CHECK(e0 > 0);

    /* Walking the whole history in windows always makes progress and never
     * overshoots the end. */
    {
        jc_size start = 0;
        int guard = 0;
        while (start < jc_history_len(&h) && guard < 1000) {
            jc_size end = jc_compact_window_end(&h, start, 30);
            JC_CHECK(end > start);                 /* progress */
            JC_CHECK(end <= jc_history_len(&h));    /* bounded */
            start = end;
            guard++;
        }
        JC_CHECK(start == jc_history_len(&h));
    }

    /* render_range honors [start,end): the prefix-only render excludes the tail
     * and the suffix-only render excludes the head. */
    {
        struct jc_arena *ar = jc_arena_new(0);
        char *head = jc_compact_render_range(&h, 0, 2, ar);
        char *tail;
        e1 = jc_history_len(&h);
        tail = jc_compact_render_range(&h, 4, e1, ar);
        JC_CHECK(head != NULL && strstr(head, "first request") != NULL);
        JC_CHECK(strstr(head, "second request") == NULL);
        JC_CHECK(tail != NULL && strstr(tail, "second request") != NULL);
        JC_CHECK(strstr(tail, "first request") == NULL);
        jc_arena_free(ar);
    }

    jc_history_free(&h);
}

/* M76: mid-turn compaction elides old large tool-result content, keeps recent
 * messages, preserves structure, and shrinks the estimate. */
static void test_midturn_trim(void)
{
    struct jc_history h;
    char big[5000];
    jc_size elided;
    long before, after;

    fill(big, 4096); /* a ~4 KB tool output */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "do a big task");        /* 0 */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "build");           /* 1 */
    jc_history_add_tool_result(&h, "id1", big, 0);            /* 2 big (old) */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "again");           /* 3 */
    jc_history_add_tool_result(&h, "id2", big, 0);            /* 4 big (old) */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "more");            /* 5 */
    jc_history_add_tool_result(&h, "id3", big, 0);            /* 6 big (old) */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "nearly");          /* 7 */
    jc_history_add_tool_result(&h, "id4", big, 0);            /* 8 big (recent)*/
    jc_history_add(&h, JC_ROLE_ASSISTANT, "final");           /* 9 (recent) */

    before = jc_compact_estimate_tokens(&h);
    elided = jc_compact_trim_tool_output(&h, 500, 3); /* keep last 3 */
    after = jc_compact_estimate_tokens(&h);

    JC_CHECK(elided > 0);
    JC_CHECK(after < before);
    /* The oldest big tool result (idx 2) is elided. */
    JC_CHECK(jc_history_get(&h, 2)->role == JC_ROLE_TOOL);
    JC_CHECK(strstr(jc_history_get(&h, 2)->content, "elided to fit") != NULL);
    JC_CHECK(strlen(jc_history_get(&h, 2)->content) < 4096);
    /* A recent tool result (idx 8, within keep_recent=3) is left intact. */
    JC_CHECK(strstr(jc_history_get(&h, 8)->content, "elided to fit") == NULL);
    JC_CHECK(strlen(jc_history_get(&h, 8)->content) == 4096);
    JC_CHECK(jc_history_get(&h, 8)->role == JC_ROLE_TOOL);

    /* Idempotent: a second pass does not re-elide an already-elided message. */
    {
        jc_size len2 = (jc_size)strlen(jc_history_get(&h, 2)->content);
        jc_compact_trim_tool_output(&h, 500, 3);
        JC_CHECK((jc_size)strlen(jc_history_get(&h, 2)->content) == len2);
    }

    /* keep_recent >= len => no-op. */
    JC_CHECK(jc_compact_trim_tool_output(&h, 1, 100) == 0);
    jc_history_free(&h);
}

/* M348: the claim ticket. A lossy elision offers its FULL content to the
 * spiller; success puts the path in the marker so the model can retrieve
 * exactly what was taken, failure falls back to the ticketless marker byte
 * for byte (D3: never name a path that is not there). The spiller is a stub
 * here, so the pass stays pure under test. */
static int stub_spill_ok(void *ctx, const char *text, jc_size len,
                         char *path_out, jc_size cap)
{
    (void)text;
    if (ctx != NULL) {
        *(jc_size *)ctx = len; /* the FULL original length reached the store */
    }
    jc_snprintf(path_out, cap, "%s", jc_test_tmp("ticket-7.txt"));
    return 1;
}

static int stub_spill_fail(void *ctx, const char *text, jc_size len,
                           char *path_out, jc_size cap)
{
    (void)ctx; (void)text; (void)len; (void)path_out; (void)cap;
    return 0;
}

static int stub_spill_longpath(void *ctx, const char *text, jc_size len,
                               char *path_out, jc_size cap)
{
    jc_size i;
    (void)ctx; (void)text; (void)len;
    /* A ticket path long enough to push the elided message back over the
     * ELIDE_MIN_BYTES floor -- the knife-edge the explicit idempotence guard
     * exists for. */
    for (i = 0; i + 1 < cap && i < 300; i++) {
        path_out[i] = 'p';
    }
    path_out[i] = '\0';
    return 1;
}

static void test_elide_claim_ticket(void)
{
    struct jc_history h;
    char big[5000];
    jc_size preserved = 0;
    jc_size seen_len = 0;
    const char *c;

    fill(big, 4096);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    jc_history_add_tool_result(&h, "id1", big, 0);
    jc_history_add(&h, JC_ROLE_ASSISTANT, "ok");

    JC_CHECK(jc_compact_trim_tool_output_ex(&h, 100, 1, stub_spill_ok,
                                            &seen_len, &preserved) == 1);
    JC_CHECK(preserved == 1);
    JC_CHECK(seen_len == 4096);          /* the FULL original was offered */
    c = jc_history_get(&h, 1)->content;
    {
        char want[600];
        jc_snprintf(want, sizeof want, "preserved at %s",
                    jc_test_tmp("ticket-7.txt"));
        JC_CHECK(strstr(c, want) != NULL);
    }
    JC_CHECK(strstr(c, "read or search THAT path") != NULL);
    JC_CHECK(strstr(c, "elided to fit") != NULL);

    /* Idempotent even when the ticket pushed the message back over the size
     * floor: the marker-signature guard, not the byte count, is the stop. */
    {
        jc_size p2 = 99;
        JC_CHECK(jc_compact_trim_tool_output_ex(&h, 1, 1, stub_spill_longpath,
                                                NULL, &p2) == 0);
        JC_CHECK(p2 == 0);
    }
    jc_history_free(&h);

    /* A failing spiller: the old ticketless marker, byte for byte. */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    jc_history_add_tool_result(&h, "id1", big, 0);
    jc_history_add(&h, JC_ROLE_ASSISTANT, "ok");
    preserved = 99;
    JC_CHECK(jc_compact_trim_tool_output_ex(&h, 100, 1, stub_spill_fail,
                                            NULL, &preserved) == 1);
    JC_CHECK(preserved == 0);
    c = jc_history_get(&h, 1)->content;
    JC_CHECK(strstr(c, "elided to fit") != NULL);
    JC_CHECK(strstr(c, "preserved at") == NULL);
    jc_history_free(&h);
}

/* M191 regression: elision must cut on character boundaries. A multi-byte
 * character straddling the head cut (or the tail's start) used to leave a lone
 * lead byte in the message, making the whole request body ill-formed UTF-8; a
 * strict server then rejected EVERY later turn too, since the byte lives on in
 * the history. Symptom: HTTP 500 in 40ms, permanently. docs/ANECDOTES.md #22. */
static void test_elide_utf8_boundary(void)
{
    char big[5000];
    jc_size off;

    /* Sweep the em-dash across the head cut (400) and the tail start (len-200),
     * so every one of its three bytes lands on each boundary in turn. */
    for (off = 396; off < 404; off++) {
        struct jc_history h;
        const char *c;
        memset(big, 'a', 4096);
        memcpy(big + off, "\xe2\x80\x94", 3);          /* em-dash at the cut */
        memcpy(big + 4096 - 200 - 1, "\xe2\x80\x94", 3); /* and at the tail   */
        big[4096] = '\0';
        JC_CHECK(jc_utf8_valid(big, 4096));            /* the input is clean */

        jc_history_init(&h);
        jc_history_add(&h, JC_ROLE_USER, "task");
        jc_history_add_tool_result(&h, "id1", big, 0);
        jc_history_add(&h, JC_ROLE_ASSISTANT, "ok");

        JC_CHECK(jc_compact_trim_tool_output(&h, 100, 1) == 1);
        c = jc_history_get(&h, 1)->content;
        JC_CHECK(strstr(c, "elided to fit") != NULL);
        /* The whole point: what remains is still well-formed UTF-8. */
        JC_CHECK(jc_utf8_valid(c, (jc_size)strlen(c)));
        jc_history_free(&h);
    }
}

/* M191 ingest guarantee: a message never holds a byte the wire cannot carry,
 * whatever the producer did -- a tool output capped mid-character, a grep over a
 * binary file, a latin-1 source file. */
static void test_history_sanitizes_content(void)
{
    struct jc_history h;
    const char *c;

    jc_history_init(&h);
    /* a lone lead byte (the M191 shape) and a stray continuation byte */
    jc_history_add_tool_result(&h, "id1", "Phase \xe2\n", 0);
    jc_history_add(&h, JC_ROLE_USER, "caf\xe9 latin-1");

    c = jc_history_get(&h, 0)->content;
    JC_CHECK(jc_utf8_valid(c, (jc_size)strlen(c)));
    JC_CHECK(strncmp(c, "Phase ", 6) == 0);          /* good bytes preserved */
    c = jc_history_get(&h, 1)->content;
    JC_CHECK(jc_utf8_valid(c, (jc_size)strlen(c)));
    JC_CHECK(strstr(c, " latin-1") != NULL);

    /* well-formed content is passed through untouched */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "an em-dash \xe2\x80\x94 survives");
    JC_CHECK(strcmp(jc_history_get(&h, 2)->content,
                    "an em-dash \xe2\x80\x94 survives") == 0);

    /* jc_msg_set_content is the other ingest door (compaction rewrites) */
    jc_msg_set_content(jc_history_get(&h, 2), "cut \xe2");
    c = jc_history_get(&h, 2)->content;
    JC_CHECK(jc_utf8_valid(c, (jc_size)strlen(c)));
    jc_history_free(&h);
}

/* M93: superseded-read elision -- a read_file result is dropped when the same
 * path is read again later; the newest read of each file (and unique reads) stay. */
static void test_superseded_reads(void)
{
    struct jc_history h;
    char big[5000];
    struct jc_message *a;
    jc_size elided;

    fill(big, 4096);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");                       /* 0 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                 /* 1 */
    jc_msg_add_tool_call(a, "r1", "read_file", "{\"path\":\"a.zig\"}");
    jc_history_add_tool_result(&h, "r1", big, 0);                   /* 2 a.zig #1 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                 /* 3 */
    jc_msg_add_tool_call(a, "r2", "read_file", "{\"path\":\"b.zig\"}");
    jc_history_add_tool_result(&h, "r2", big, 0);                   /* 4 b.zig once */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                 /* 5 */
    jc_msg_add_tool_call(a, "r3", "read_file", "{\"path\":\"a.zig\"}");
    jc_history_add_tool_result(&h, "r3", big, 0);                   /* 6 a.zig #2 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                 /* 7 */
    jc_msg_add_tool_call(a, "r4", "read_file", "{\"path\":\"a.zig\"}");
    jc_history_add_tool_result(&h, "r4", big, 0);                   /* 8 a.zig #3 */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "final");                 /* 9 */

    /* budget=1 => never early-stops, so every superseded read is elided. */
    elided = jc_compact_trim_superseded_reads(&h, 1, 2, NULL);
    JC_CHECK(elided == 2); /* a.zig #1 (idx2) and #2 (idx6) */
    /* M354: the superseded marker names its true reason and points below --
     * the old "to fit the context window" text was false for the eager pass
     * (budget 0, no pressure) and withheld the pointer that stops re-reads. */
    JC_CHECK(strstr(jc_history_get(&h, 2)->content,
                    "elided: superseded") != NULL);
    JC_CHECK(strstr(jc_history_get(&h, 2)->content,
                    "LATER in this conversation") != NULL);
    JC_CHECK(strstr(jc_history_get(&h, 2)->content,
                    "to fit the context window") == NULL);
    JC_CHECK(strstr(jc_history_get(&h, 6)->content,
                    "elided: superseded") != NULL);
    /* b.zig read only once -> kept verbatim. */
    JC_CHECK(strlen(jc_history_get(&h, 4)->content) == 4096);
    /* The newest a.zig read (idx8) -> kept verbatim (latest, and keep_recent). */
    JC_CHECK(strlen(jc_history_get(&h, 8)->content) == 4096);
    /* Idempotent: already-elided copies aren't re-elided. */
    JC_CHECK(jc_compact_trim_superseded_reads(&h, 1, 2, NULL) == 0);
    jc_history_free(&h);

    /* A non-read_file large tool result is ignored by this pass (even if its id
     * repeats semantics) -- superseded detection is read_file-only. */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "run");
    jc_msg_add_tool_call(a, "c1", "run_terminal_command", "{\"command\":\"ls\"}");
    jc_history_add_tool_result(&h, "c1", big, 0);
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "run");
    jc_msg_add_tool_call(a, "c2", "run_terminal_command", "{\"command\":\"ls\"}");
    jc_history_add_tool_result(&h, "c2", big, 0);
    jc_history_add(&h, JC_ROLE_ASSISTANT, "done");
    JC_CHECK(jc_compact_trim_superseded_reads(&h, 1, 2, NULL) == 0);
    jc_history_free(&h);
}

/* M287: PAGING is not supersession, and this pass claims ZERO information loss.
 *
 * A model reading a large file in ranges -- lines 1-100, then 100-250 -- was
 * having its FIRST page elided when the second landed, because supersession
 * matched on `path` alone and the later read was assumed to "carry the current
 * content". For a paged read it carries DIFFERENT lines, so the pass was
 * silently discarding content the model still needed -- in the one pass whose
 * entire justification is that it discards none. One project's log holds 909
 * paged reads, and 82 of 142 advisory-firing re-reads immediately followed
 * another read_file, which is what a self-inflicted re-read loop looks like. */
static void test_superseded_reads_paging(void)
{
    struct jc_history h;
    char big[5000];
    struct jc_message *a;

    fill(big, 4096);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");                        /* 0 */
    /* Three PAGES of one file: distinct ranges, distinct content. */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                  /* 1 */
    jc_msg_add_tool_call(a, "p1", "read_file",
                         "{\"path\":\"big.zig\",\"limit\":100}");
    jc_history_add_tool_result(&h, "p1", big, 0);                    /* 2 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                  /* 3 */
    jc_msg_add_tool_call(a, "p2", "read_file",
                         "{\"path\":\"big.zig\",\"offset\":100,\"limit\":150}");
    jc_history_add_tool_result(&h, "p2", big, 0);                    /* 4 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                  /* 5 */
    jc_msg_add_tool_call(a, "p3", "read_file",
                         "{\"path\":\"big.zig\",\"offset\":250,\"limit\":100}");
    jc_history_add_tool_result(&h, "p3", big, 0);                    /* 6 */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "final");                  /* 7 */

    /* budget=1 => never early-stops, so anything eligible WOULD be elided.
     * Nothing is: no page supersedes another. */
    JC_CHECK(jc_compact_trim_superseded_reads(&h, 1, 1, NULL) == 0);
    JC_CHECK(strlen(jc_history_get(&h, 2)->content) == 4096);
    JC_CHECK(strlen(jc_history_get(&h, 4)->content) == 4096);
    JC_CHECK(strlen(jc_history_get(&h, 6)->content) == 4096);
    jc_history_free(&h);

    /* But re-reading the SAME range still supersedes -- the M93 case is intact. */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");                        /* 0 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                  /* 1 */
    jc_msg_add_tool_call(a, "q1", "read_file",
                         "{\"path\":\"big.zig\",\"limit\":100}");
    jc_history_add_tool_result(&h, "q1", big, 0);                    /* 2 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                  /* 3 */
    jc_msg_add_tool_call(a, "q2", "read_file",
                         "{\"path\":\"big.zig\",\"offset\":100,\"limit\":150}");
    jc_history_add_tool_result(&h, "q2", big, 0);                    /* 4 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                  /* 5 */
    jc_msg_add_tool_call(a, "q3", "read_file",   /* same range as q1 */
                         "{\"path\":\"big.zig\",\"limit\":100}");
    jc_history_add_tool_result(&h, "q3", big, 0);                    /* 6 */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "final");                  /* 7 */

    JC_CHECK(jc_compact_trim_superseded_reads(&h, 1, 1, NULL) == 1);
    JC_CHECK(strstr(jc_history_get(&h, 2)->content,
                    "elided: superseded") != NULL);
    JC_CHECK(strlen(jc_history_get(&h, 4)->content) == 4096); /* other page kept */
    JC_CHECK(strlen(jc_history_get(&h, 6)->content) == 4096); /* newest kept */
    jc_history_free(&h);

    /* A stringified range keys the same as a numeric one, so the shared lenient
     * parse (jc_json_get_num_lenient) cannot drift from the read tool's. */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");                        /* 0 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                  /* 1 */
    jc_msg_add_tool_call(a, "s1", "read_file",
                         "{\"path\":\"big.zig\",\"limit\":100}");
    jc_history_add_tool_result(&h, "s1", big, 0);                    /* 2 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");                  /* 3 */
    jc_msg_add_tool_call(a, "s2", "read_file",
                         "{\"path\":\"big.zig\",\"limit\":\"100.0\"}");
    jc_history_add_tool_result(&h, "s2", big, 0);                    /* 4 */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "final");                  /* 5 */
    JC_CHECK(jc_compact_trim_superseded_reads(&h, 1, 1, NULL) == 1);
    jc_history_free(&h);
}

/* M192: the same file read under two spellings is ONE file. Without `cwd` the raw
 * strcmp sees two, so each spelling keeps its own full copy -- measured in a
 * dogfood log as 4 files spelled two ways, ~94 KB held resident. */
static void test_superseded_reads_spelling(void)
{
    struct jc_history h;
    char big[5000];
    struct jc_message *a;
    int pass;

    /* pass 0: cwd given => the two spellings dedup. pass 1: NULL => they do not
     * (pinning the pre-M192 behaviour the NULL contract preserves). */
    for (pass = 0; pass < 2; pass++) {
        const char *cwd = (pass == 0) ? "/work" : NULL;
        fill(big, 4096);
        jc_history_init(&h);
        jc_history_add(&h, JC_ROLE_USER, "task");                    /* 0 */
        a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");              /* 1 */
        jc_msg_add_tool_call(a, "r1", "read_file",
                             "{\"path\":\"src/vm.zig\"}");
        jc_history_add_tool_result(&h, "r1", big, 0);                 /* 2 */
        a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");               /* 3 */
        jc_msg_add_tool_call(a, "r2", "read_file",
                             "{\"path\":\"/work/src/vm.zig\"}");
        jc_history_add_tool_result(&h, "r2", big, 0);                 /* 4 */
        jc_history_add(&h, JC_ROLE_ASSISTANT, "final");               /* 5 */

        JC_CHECK(jc_compact_trim_superseded_reads(&h, 1, 1, cwd) ==
                 (jc_size)(pass == 0 ? 1 : 0));
        if (pass == 0) {
            /* The earlier (relative) copy is elided; the newest one is kept. */
            JC_CHECK(strstr(jc_history_get(&h, 2)->content,
                            "elided: superseded") != NULL);
        } else {
            JC_CHECK(strlen(jc_history_get(&h, 2)->content) == 4096);
        }
        JC_CHECK(strlen(jc_history_get(&h, 4)->content) == 4096);
        jc_history_free(&h);
    }

    /* A ".." path is refused by jc_path_normalize, so it keeps its raw spelling
     * and simply fails to match -- a missed dedup, never a wrong one. */
    fill(big, 4096);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "r1", "read_file",
                         "{\"path\":\"src/../src/vm.zig\"}");
    jc_history_add_tool_result(&h, "r1", big, 0);
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "r2", "read_file",
                         "{\"path\":\"/work/src/vm.zig\"}");
    jc_history_add_tool_result(&h, "r2", big, 0);
    jc_history_add(&h, JC_ROLE_ASSISTANT, "final");
    JC_CHECK(jc_compact_trim_superseded_reads(&h, 1, 1, "/work") == 0);
    JC_CHECK(strlen(jc_history_get(&h, 2)->content) == 4096);
    jc_history_free(&h);
}

/* M218: mid-turn elision of assistant tool-call ARGUMENTS. A marathon single
 * turn (hundreds of write_file/apply_patch calls) grows the history through
 * the arguments side too -- each call's full file body lives in the assistant
 * message's arguments_json, which the result-side trims never touch, and
 * between-turn compaction never runs inside one turn. The replacement must be
 * a VALID JSON OBJECT: the Anthropic serializer re-parses arguments_json (an
 * invalid value degrades to _unparsed_arguments), the OpenAI one emits it
 * verbatim. */
static void test_midturn_trim_args(void)
{
    struct jc_history h;
    struct jc_message *a;
    char big[5000];
    char args[5200];
    jc_size elided;
    long before, after;

    fill(big, 4096);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "write the files");         /* 0 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);             /* 1 */
    jc_snprintf(args, sizeof(args),
                "{\"path\":\"src/foo.c\",\"content\":\"%s\"}", big);
    jc_msg_add_tool_call(a, "w1", "write_file", args);
    jc_history_add_tool_result(&h, "w1", "wrote src/foo.c", 0);  /* 2 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);             /* 3 */
    jc_msg_add_tool_call(a, "w2", "write_file", args);
    jc_history_add_tool_result(&h, "w2", "wrote src/foo.c", 0);  /* 4 */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);             /* 5 recent */
    jc_msg_add_tool_call(a, "r1", "read_file",
                         "{\"path\":\"src/foo.c\"}");
    jc_history_add_tool_result(&h, "r1", "int x;", 0);           /* 6 recent */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "done");               /* 7 recent */

    before = jc_compact_estimate_tokens(&h);
    elided = jc_compact_trim_tool_args(&h, 1, 3); /* keep last 3 */
    after = jc_compact_estimate_tokens(&h);

    JC_CHECK(elided == 2);
    JC_CHECK(after < before);
    {
        struct jc_tool_call *tc = jc_msg_tool_call_at(jc_history_get(&h, 1), 0);
        cJSON *o;
        if (JC_REQUIRE(tc != NULL)) {
            JC_CHECK(strlen(tc->arguments_json) < 1024);
        }
        /* Pairing/identity untouched. */
        JC_CHECK(strcmp(tc->id, "w1") == 0);
        JC_CHECK(strcmp(tc->name, "write_file") == 0);
        JC_CHECK(strcmp(jc_history_get(&h, 2)->tool_call_id, "w1") == 0);
        JC_CHECK(strcmp(jc_history_get(&h, 2)->content,
                        "wrote src/foo.c") == 0);
        /* The marker is a valid JSON object carrying the note and the original
         * path, so the model still knows WHAT it wrote. M289: the note is
         * DIRECTIVE, because the model reads this slot as an example of a call
         * to this tool and copied the whole object back as arguments (18 of 19
         * argument-shape failures on one run). It must say it is not arguments
         * and what to do instead -- asserted here rather than left to prose. */
        o = cJSON_Parse(tc->arguments_json);
        JC_CHECK(o != NULL && cJSON_IsObject(o));
        JC_CHECK(jc_json_get_str(o, JC_COMPACT_ELIDED_KEY, NULL) != NULL);
        {
            const char *n = jc_json_get_str(o, JC_COMPACT_ELIDED_KEY, "");
            JC_CHECK(strstr(n, "not arguments") != NULL);
            JC_CHECK(strstr(n, "cannot be recovered") != NULL);
            /* Still reports the size, which is what made it diagnosable. */
            JC_CHECK(strstr(n, "bytes") != NULL);
        }
        JC_CHECK(strcmp(jc_json_get_str(o, "path", ""), "src/foo.c") == 0);
        cJSON_Delete(o);
    }
    /* Small args (the read_file call) are never elided: they are under
     * ELIDE_MIN_BYTES, which also keeps the M94 dedup's path lookup intact. */
    {
        struct jc_tool_call *tc = jc_msg_tool_call_at(jc_history_get(&h, 5), 0);
        JC_CHECK(strcmp(tc->arguments_json, "{\"path\":\"src/foo.c\"}") == 0);
    }
    /* Idempotent: the marker itself is under ELIDE_MIN_BYTES. */
    JC_CHECK(jc_compact_trim_tool_args(&h, 1, 3) == 0);
    /* keep_recent >= len => no-op. */
    JC_CHECK(jc_compact_trim_tool_args(&h, 1, 100) == 0);
    jc_history_free(&h);
}

/* M218: apply_patch has no top-level path; the marker takes the first edit's. */
static void test_midturn_trim_args_patch(void)
{
    struct jc_history h;
    struct jc_message *a;
    char big[3000];
    char args[3300];
    struct jc_tool_call *tc;
    cJSON *o;

    fill(big, 2048);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "patch it");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_snprintf(args, sizeof(args),
                "{\"edits\":[{\"path\":\"src/bar.c\",\"old_string\":\"x\","
                "\"new_string\":\"%s\"}]}", big);
    jc_msg_add_tool_call(a, "p1", "apply_patch", args);
    jc_history_add_tool_result(&h, "p1", "ok", 0);
    jc_history_add(&h, JC_ROLE_ASSISTANT, "done");

    JC_CHECK(jc_compact_trim_tool_args(&h, 1, 1) == 1);
    tc = jc_msg_tool_call_at(jc_history_get(&h, 1), 0);
    o = cJSON_Parse(tc->arguments_json);
    JC_CHECK(o != NULL && cJSON_IsObject(o));
    JC_CHECK(strcmp(jc_json_get_str(o, "path", ""), "src/bar.c") == 0);
    cJSON_Delete(o);
    jc_history_free(&h);
}

/* M218: the eager dedup is gated on the caller's hint. A round that appended
 * no read_file result cannot have created a new superseded pair, so hint=0
 * must skip the pass entirely (bit-identical outcome, none of the cost);
 * hint=1 runs it as before. Exercised through jc_compact_midturn with a
 * minimal app (no config => unknown budget => only the eager pass can act). */
static void test_midturn_dedup_hint(void)
{
    struct jc_app app;
    struct jc_arena *arena = jc_arena_new(0);
    struct jc_history h;
    struct jc_message *a;
    char big[5000];
    jc_size dup = 0, args = 0;

    memset(&app, 0, sizeof(app));
    app.arena = arena;

    fill(big, 4096);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "r1", "read_file", "{\"path\":\"a.c\"}");
    jc_history_add_tool_result(&h, "r1", big, 0);          /* superseded */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "r2", "read_file", "{\"path\":\"a.c\"}");
    jc_history_add_tool_result(&h, "r2", big, 0);          /* the keeper */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "x");
    jc_history_add(&h, JC_ROLE_ASSISTANT, "x");
    jc_history_add(&h, JC_ROLE_ASSISTANT, "x");
    jc_history_add(&h, JC_ROLE_ASSISTANT, "x");
    jc_history_add(&h, JC_ROLE_ASSISTANT, "done");

    /* hint=0: the pair exists but the pass must not run. */
    {
        struct jc_midturn_report rep;
        JC_CHECK(jc_compact_midturn(&app, &h, NULL, &rep, 0, NULL) == 0);
        JC_CHECK(rep.dup == 0);
        JC_CHECK(strlen(jc_history_get(&h, 2)->content) == 4096);
        /* M323: the report is zeroed on entry, so every field is readable even
         * on the do-nothing path -- and `pressed` must be 0 there, or the caller
         * would emit an event (and warn) for a pass that never ran. */
        JC_CHECK(rep.pressed == 0);
        JC_CHECK(rep.elided == 0);

        /* hint=1: elided as before. */
        JC_CHECK(jc_compact_midturn(&app, &h, NULL, &rep, 1, NULL) == 1);
        JC_CHECK(rep.dup == 1);
        JC_CHECK(rep.elided == 1);
        JC_CHECK(strstr(jc_history_get(&h, 2)->content, "elided") != NULL);
        /* This history is far under any limit, so the eager dedup ran but the
         * pressure trims did not: `pressed` stays 0 and the M323 event/warning
         * paths stay quiet. The dedup is deliberately independent of pressure. */
        JC_CHECK(rep.pressed == 0);
    }
    (void)dup; (void)args;

    jc_history_free(&h);
    jc_arena_free(arena);
}

/* M218 regression for the backward originating-call scan: with two assistant
 * messages carrying DIFFERENT call ids, each result must resolve to ITS call
 * (b.c's read is not superseded and must survive; a.c's earlier read is). */
static void test_superseded_reads_interleaved_ids(void)
{
    struct jc_history h;
    struct jc_message *a;
    char big[5000];

    fill(big, 4096);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "r1", "read_file", "{\"path\":\"a.c\"}");
    jc_history_add_tool_result(&h, "r1", big, 0);          /* 2: superseded */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "r2", "read_file", "{\"path\":\"b.c\"}");
    jc_history_add_tool_result(&h, "r2", big, 0);          /* 4: unique */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "r3", "read_file", "{\"path\":\"a.c\"}");
    jc_history_add_tool_result(&h, "r3", big, 0);          /* 6: the keeper */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "done");

    JC_CHECK(jc_compact_trim_superseded_reads(&h, 1, 1, NULL) == 1);
    JC_CHECK(strstr(jc_history_get(&h, 2)->content, "elided") != NULL);
    JC_CHECK(strlen(jc_history_get(&h, 4)->content) == 4096); /* b.c intact */
    JC_CHECK(strlen(jc_history_get(&h, 6)->content) == 4096); /* keeper */
    jc_history_free(&h);
}

/* M358: the context gauge's pure halves. The pressure note renders from the
 * pressed pass's own numbers (so note and trigger cannot disagree), picks its
 * message by `reached` (elision coping vs elision cannot cope), and appends
 * NOTHING without real numbers -- the M355 armed-only rule. The explicit-limit
 * helper returns 0 when only the built-in default would apply, because the
 * prompt line states a fact and JC_COMPACT_DEFAULT_LIMIT is a guess. */
static void test_pressure_note(void)
{
    struct jc_sb sb;
    struct jc_app app;

    /* reached: coping -- names the elision and the reading habit. */
    jc_sb_init(&sb);
    jc_compact_pressure_note(8000L, 10000L, 1, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "[context]") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "~80%") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "~10000-token") != NULL);
    JC_CHECK(sb.data != NULL
             && strstr(sb.data, "elided to make room") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "offset/limit") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "could NOT") == NULL);
    jc_sb_free(&sb);

    /* !reached: cannot cope -- says so and asks for durable results NOW. */
    jc_sb_init(&sb);
    jc_compact_pressure_note(10400L, 10000L, 0, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "~104%") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "could NOT") != NULL);
    JC_CHECK(sb.data != NULL
             && strstr(sb.data, "write durable results now") != NULL);
    jc_sb_free(&sb);

    /* No real numbers => nothing (never "~0%" or a division by zero). */
    jc_sb_init(&sb);
    jc_compact_pressure_note(0L, 10000L, 1, &sb);
    JC_CHECK(sb.len == 0);
    jc_compact_pressure_note(8000L, 0L, 1, &sb);
    JC_CHECK(sb.len == 0);
    jc_compact_pressure_note(8000L, 10000L, 1, NULL); /* no crash */
    jc_sb_free(&sb);

    /* Explicit limit: config wins, then the model, then 0 -- NEVER the
     * built-in default (that is what separates it from _context_limit). */
    memset(&app, 0, sizeof(app));
    JC_CHECK(jc_compact_context_limit_explicit(&app) == 0);
    JC_CHECK(jc_compact_context_limit(&app) == JC_COMPACT_DEFAULT_LIMIT);
    app.config.model.context_limit = 7000;
    JC_CHECK(jc_compact_context_limit_explicit(&app) == 7000);
    app.config.context_limit = 5000;
    JC_CHECK(jc_compact_context_limit_explicit(&app) == 5000);
    JC_CHECK(jc_compact_context_limit_explicit(NULL) == 0);
}

/* M361: the exhaustion latch. The pure re-arm math first: a message at index
 * i is protected while len < i + keep + 1, so the oldest protected candidate
 * (tool result or tool-call arguments over min_bytes) fixes the exact length
 * that releases it; with no candidate the horizon is len + keep + 1, which
 * bounds EVERY latch to keep+1 appends -- the property that makes a
 * conservative candidate detector safe (it can delay one scan, never skip
 * one forever). Then the lifecycle through jc_compact_midturn itself: set on
 * a dry pressed pass, skip strictly below the re-arm length, run again AT it,
 * re-latch when still dry, and re-arm immediately when the history SHRANK
 * (stale index math after a truncation). */
static void test_midturn_latch(void)
{
    struct jc_app app;
    struct jc_arena *arena = jc_arena_new(0);
    struct jc_history h;
    struct jc_message *a;
    struct jc_midturn_report rep;
    struct jc_midturn_latch latch;
    char big[9000];
    char huge[20500];
    jc_size i;

    /* --- the pure re-arm math ------------------------------------------- */
    fill(big, 8192);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");            /* 0 */
    for (i = 0; i < 3; i++) {
        jc_history_add(&h, JC_ROLE_ASSISTANT, "x");      /* 1..3 */
    }
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");      /* 4 */
    jc_msg_add_tool_call(a, "c1", "read_file", "{\"path\":\"a\"}");
    jc_history_add_tool_result(&h, "c1", big, 0);        /* 5: candidate */
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");      /* 6 */
    jc_msg_add_tool_call(a, "c2", "read_file", "{\"path\":\"b\"}");
    jc_history_add_tool_result(&h, "c2", big, 0);        /* 7: candidate */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "x");          /* 8 */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "x");          /* 9 */
    /* len 10, keep 6: protected [4,10); oldest candidate is index 5. */
    JC_CHECK(jc_compact_rearm_len(&h, 6, 800) == 12);
    jc_history_free(&h);

    /* A large tool-call ARGUMENT is a candidate too: an assistant message at
     * index 4 carrying huge args is the oldest candidate (4 + 6 + 1 = 11). */
    fill(huge, 9000);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");            /* 0 */
    for (i = 0; i < 3; i++) {
        jc_history_add(&h, JC_ROLE_ASSISTANT, "x");      /* 1..3 */
    }
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");      /* 4 */
    jc_msg_add_tool_call(a, "w1", "write_file", huge);
    jc_history_add_tool_result(&h, "w1", "ok", 0);       /* 5 */
    for (i = 0; i < 4; i++) {
        jc_history_add(&h, JC_ROLE_ASSISTANT, "x");      /* 6..9 */
    }
    JC_CHECK(jc_compact_rearm_len(&h, 6, 800) == 11);
    jc_history_free(&h);

    /* No candidate anywhere: the horizon is len + keep + 1. */
    jc_history_init(&h);
    for (i = 0; i < 5; i++) {
        jc_history_add(&h, JC_ROLE_ASSISTANT, "x");
    }
    JC_CHECK(jc_compact_rearm_len(&h, 6, 800) == 12); /* 5 + 6 + 1 */
    jc_history_free(&h);

    /* --- the lifecycle through jc_compact_midturn ------------------------ */
    memset(&app, 0, sizeof(app));
    app.arena = arena;
    app.config.context_limit = 6000; /* high water 4800, target 3600 */
    latch.rearm_len = 0;
    latch.latch_len = 0;

    /* Pressure with a large USER message: it is never elidable, so the
     * lossy pass is dry by construction (and spills nothing -- hermetic). */
    fill(huge, 20480);
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, huge);              /* 0 */
    for (i = 0; i < 9; i++) {
        jc_history_add(&h, JC_ROLE_ASSISTANT, "x");      /* 1..9 */
    }
    /* len 10. Pass 1: pressed, dry -> sets the latch (no candidate:
     * 10 + 6 + 1 = 17). */
    jc_compact_midturn(&app, &h, NULL, &rep, 0, &latch);
    JC_CHECK(rep.pressed == 1);
    JC_CHECK(rep.latched == 0);
    JC_CHECK(rep.elided == 0);
    JC_CHECK(latch.rearm_len == 17);
    JC_CHECK(latch.latch_len == 10);

    /* Pass 2, same length: skipped -- pressure still reported truthfully. */
    jc_compact_midturn(&app, &h, NULL, &rep, 0, &latch);
    JC_CHECK(rep.pressed == 1);
    JC_CHECK(rep.latched == 1);
    JC_CHECK(rep.unrelieved == 1);

    /* One below the boundary: still latched. */
    while (jc_history_len(&h) < 16) {
        jc_history_add(&h, JC_ROLE_ASSISTANT, "x");
    }
    jc_compact_midturn(&app, &h, NULL, &rep, 0, &latch);
    JC_CHECK(rep.latched == 1);

    /* AT the boundary: the pass runs again, finds the range still dry, and
     * re-latches at the new horizon (17 + 7 = 24). */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "x");          /* len 17 */
    jc_compact_midturn(&app, &h, NULL, &rep, 0, &latch);
    JC_CHECK(rep.latched == 0);
    JC_CHECK(latch.rearm_len == 24);
    JC_CHECK(latch.latch_len == 17);

    /* A history that SHRANK re-arms immediately: stale index math. */
    jc_history_truncate(&h, 8);
    jc_compact_midturn(&app, &h, NULL, &rep, 0, &latch);
    JC_CHECK(rep.latched == 0);
    JC_CHECK(latch.latch_len == 8); /* re-latched from the new shape */

    /* NULL latch: feature off, behavior identical (back-compat). */
    jc_compact_midturn(&app, &h, NULL, &rep, 0, NULL);
    JC_CHECK(rep.latched == 0);

    jc_history_free(&h);
    jc_arena_free(arena);
}

void test_compact(void)
{
    test_pressure_note();
    test_midturn_latch();
    test_estimate();
    test_find_cut();
    test_render();
    test_window();
    test_drop_front();
    test_apply();
    test_midturn_trim();
    test_midturn_trim_args();
    test_midturn_trim_args_patch();
    test_superseded_reads();
    test_superseded_reads_paging();
    test_superseded_reads_spelling();
    test_superseded_reads_interleaved_ids();
    test_midturn_dedup_hint();
    test_elide_claim_ticket();
    test_elide_utf8_boundary();
    test_history_sanitizes_content();
}

/* M315: the per-message term the /context history breakdown attributes with must
 * be the EXACT term jc_compact_estimate_tokens sums. Exposed rather than
 * re-derived, because a breakdown whose parts do not sum to the line above them
 * is the drift M311/M312/M313 each had to undo -- and a second definition of
 * "message size" would drift silently, since both would look plausible.
 *
 * The identity is asserted over a history with every shape that contributes:
 * plain text, a message with tool calls (whose name + arguments count), an empty
 * message (overhead only), and tool results. */
void test_compact_message_estimate(void)
{
    struct jc_history h;
    struct jc_message *m;
    long sum = 0;
    jc_size i, n;

    jc_history_init(&h);
    JC_CHECK(jc_compact_estimate_message(NULL) == 0);
    /* An empty history: the identity holds trivially, and 0 == 0 matters because
     * the report prints "(empty)" off the back of it. */
    JC_CHECK(jc_compact_estimate_tokens(&h) == 0);

    jc_history_add(&h, JC_ROLE_USER, "please read the notes file");
    m = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    JC_CHECK(m != NULL);
    jc_msg_add_tool_call(m, "c1", "read_file", "{\"path\":\"notes.txt\"}");
    jc_history_add_tool_result(&h, "c1", "alpha\nbeta\ngamma\n", 0);
    jc_history_add(&h, JC_ROLE_ASSISTANT, "The file has three lines.");
    jc_history_add(&h, JC_ROLE_USER, "");

    n = jc_history_len(&h);
    JC_CHECK(n == 5);
    for (i = 0; i < n; i++) {
        long t = jc_compact_estimate_message(jc_history_get(&h, i));
        /* Every message costs at least the per-message overhead, so no part of
         * the breakdown can be 0 and vanish from a percentage. */
        JC_CHECK(t > 0);
        sum += t;
    }
    JC_CHECK(sum == jc_compact_estimate_tokens(&h));

    /* The tool call's name and arguments are counted, not just content: an
     * assistant message with no text but a call must cost more than an empty one.
     * (This is the part a re-derived estimate would most plausibly miss.) */
    JC_CHECK(jc_compact_estimate_message(jc_history_get(&h, 1)) >
             jc_compact_estimate_message(jc_history_get(&h, 4)));

    jc_history_free(&h);
}
