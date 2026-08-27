/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* mm_core.c - pure core of mockmodel: reply-table parse/select/render and
 * the incremental HTTP request parser. Libc-only (no sockets, no pty); the
 * I/O shell is mockmodel.c. Unit-tested in tests/test_ttools.c. */

#include "mm_core.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

/* Hard cap on an accumulated request: far above any real jichi request
 * (a 5 MB image is ~6.7 MB base64) but low enough that a runaway peer
 * cannot exhaust the box the smoke tier exists to validate. */
#define MM_HTTP_MAX (32u * 1024u * 1024u)

/* Default usage when a rule carries no `usage` line -- the values the Python mock
 * drivers always sent, which headless drivers assert on. Shared by the text and
 * (since M441) the tool path, so "unspecified" means one thing. */
#define MM_USAGE_IN_DEFAULT  20
#define MM_USAGE_OUT_DEFAULT 5

/* --- small utilities ----------------------------------------------------- */

static char *mm_strdup_n(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (p == NULL)
        return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* Binary-safe substring search (hay may contain NULs). */
static const char *mm_find(const char *hay, size_t hay_len,
                           const char *needle, size_t needle_len)
{
    size_t i;
    if (needle_len == 0 || needle_len > hay_len)
        return NULL;
    for (i = 0; i + needle_len <= hay_len; i++) {
        if (hay[i] == needle[0] &&
            memcmp(hay + i, needle, needle_len) == 0)
            return hay + i;
    }
    return NULL;
}

/* Decode a double-quoted, C-escaped string starting at *p (which must point
 * at the opening quote). Supports \" \\ \n \r \t \xNN. On success returns a
 * malloc'd buffer (NUL-terminated; decoded length in *outlen), advances *p
 * past the closing quote. NULL on malformed input. */
static char *mm_unquote(const char **p, size_t *outlen)
{
    const char *s = *p;
    char *out;
    size_t cap, n = 0;

    if (*s != '"')
        return NULL;
    s++;
    cap = strlen(s) + 1;
    out = (char *)malloc(cap);
    if (out == NULL)
        return NULL;
    while (*s != '\0' && *s != '"') {
        char c = *s++;
        if (c == '\\') {
            char e = *s++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case '\\': c = '\\'; break;
            case '"': c = '"'; break;
            case 'x': {
                int hi, lo;
                hi = *s;
                if (hi >= '0' && hi <= '9') hi -= '0';
                else if (hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10;
                else if (hi >= 'A' && hi <= 'F') hi = hi - 'A' + 10;
                else { free(out); return NULL; }
                s++;
                lo = *s;
                if (lo >= '0' && lo <= '9') lo -= '0';
                else if (lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10;
                else if (lo >= 'A' && lo <= 'F') lo = lo - 'A' + 10;
                else { free(out); return NULL; }
                s++;
                c = (char)((hi << 4) | lo);
                break;
            }
            default:
                free(out);
                return NULL;
            }
        }
        out[n++] = c;
    }
    if (*s != '"') {           /* unterminated */
        free(out);
        return NULL;
    }
    s++;
    out[n] = '\0';
    *outlen = n;
    *p = s;
    return out;
}

/* --- script parsing ------------------------------------------------------ */

static void mm_rule_init(struct mm_rule *r)
{
    memset(r, 0, sizeof(*r));
    r->usage_in = -1;
    r->usage_out = -1;
}

static int mm_err(char *err, size_t errcap, int line, const char *msg)
{
    if (err != NULL && errcap > 0)
        jc_snprintf(err, errcap, "line %d: %s", line, msg);
    return -1;
}

/* Every parse-error exit routes through here: the IN-PROGRESS rule's
 * allocations must be freed too -- mm_script_free only knows committed
 * rules, so an early `return` after `text A` leaked that strdup (found by
 * the ASan ci gate, 2 bytes: "A\0"). */
static int mm_parse_fail(struct mm_rule *cur, struct mm_script *out,
                         char *err, size_t errcap, int line, const char *msg)
{
    int j;
    for (j = 0; j < cur->nmatch; j++)
        free(cur->match[j]);
    free(cur->arg1);
    free(cur->arg2);
    free(cur->body_file);
    free(cur->location);
    mm_rule_init(cur);
    mm_script_free(out);
    return mm_err(err, errcap, line, msg);
}

/* Trim leading spaces/tabs; returns the first non-blank char pointer. */
static const char *mm_skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

int mm_script_parse(const char *text, struct mm_script *out,
                    char *err, size_t errcap)
{
    const char *p = text;
    int line = 0;
    int in_rule = 0;
    struct mm_rule cur;

    out->rules = NULL;
    out->nrules = 0;
    if (err != NULL && errcap > 0)
        err[0] = '\0';
    mm_rule_init(&cur);

    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t linelen = (eol != NULL) ? (size_t)(eol - p) : strlen(p);
        char linebuf[1024];
        const char *s;

        line++;
        if (linelen >= sizeof(linebuf)) {
            return mm_parse_fail(&cur, out, err, errcap, line, "line too long");
        }
        memcpy(linebuf, p, linelen);
        linebuf[linelen] = '\0';
        p = (eol != NULL) ? eol + 1 : p + linelen;

        s = mm_skip_ws(linebuf);
        if (*s == '\0' || *s == '#')
            continue;

        if (strncmp(s, "wire ", 5) == 0) {
            const char *w = mm_skip_ws(s + 5);
            if (in_rule) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "wire inside a rule");
            }
            if (strncmp(w, "openai", 6) == 0)
                continue;
            if (strncmp(w, "anthropic", 9) == 0) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                              "wire anthropic not yet implemented "
                              "(Milestone B)");
            }
            return mm_parse_fail(&cur, out, err, errcap, line, "unknown wire");
        }

        if (strcmp(s, "rule") == 0) {
            if (in_rule) {
                struct mm_rule *n;
                if (cur.action == MM_ACT_NONE) {
                    return mm_parse_fail(&cur, out, err, errcap, line,
                                  "previous rule has no action");
                }
                n = (struct mm_rule *)realloc(out->rules,
                        (size_t)(out->nrules + 1) * sizeof(*n));
                if (n == NULL) {
                    return mm_parse_fail(&cur, out, err, errcap, line,
                                         "out of memory");
                }
                out->rules = n;
                out->rules[out->nrules++] = cur;
            }
            mm_rule_init(&cur);
            in_rule = 1;
            continue;
        }

        if (!in_rule) {
            return mm_parse_fail(&cur, out, err, errcap, line,
                                 "directive outside a rule");
        }

        if (strncmp(s, "count ", 6) == 0) {
            long v = strtol(s + 6, NULL, 10);
            if (v < 1) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "count must be >= 1");
            }
            cur.count = (int)v;
            continue;
        }
        if (strncmp(s, "match ", 6) == 0 || strncmp(s, "nomatch ", 8) == 0) {
            int neg = (s[0] == 'n');
            const char *q = mm_skip_ws(s + (neg ? 8 : 6));
            size_t plen = 0;
            char *pat;
            if (cur.nmatch >= MM_MAX_MATCH) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "too many match lines");
            }
            pat = mm_unquote(&q, &plen);
            if (pat == NULL || plen == 0) {
                free(pat);
                return mm_parse_fail(&cur, out, err, errcap, line,
                              "match needs a non-empty quoted string");
            }
            cur.match[cur.nmatch] = pat;
            cur.match_len[cur.nmatch] = plen;
            cur.match_neg[cur.nmatch] = neg;
            cur.nmatch++;
            continue;
        }
        if (strncmp(s, "larger ", 7) == 0) {
            long v = strtol(s + 7, NULL, 10);
            if (v < 1) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "larger must be >= 1");
            }
            cur.larger = v;
            continue;
        }
        if (strncmp(s, "delay ", 6) == 0) {
            long ms = strtol(s + 6, NULL, 10);
            if (ms < 1) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "delay must be >= 1ms");
            }
            cur.delay_ms = ms;
            continue;
        }
        if (strncmp(s, "usage ", 6) == 0) {
            char *end = NULL;
            long in_tok = strtol(s + 6, &end, 10);
            long out_tok = (end != NULL) ? strtol(end, NULL, 10) : -1;
            if (in_tok < 0 || out_tok < 0) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "usage needs IN OUT");
            }
            cur.usage_in = in_tok;
            cur.usage_out = out_tok;
            continue;
        }

        /* Actions: exactly one per rule. */
        /* A repeated `tool` line ADDS to the round (see MM_MAX_TOOLS); every
         * other action stays exactly one per rule. */
        if (cur.action != MM_ACT_NONE && strncmp(s, "tool ", 5) == 0 &&
            cur.action != MM_ACT_TOOL) {
            return mm_parse_fail(&cur, out, err, errcap, line,
                                 "rule already has an action");
        }
        if (cur.action != MM_ACT_NONE &&
            (strncmp(s, "text", 4) == 0 ||
             strncmp(s, "status ", 7) == 0 || strncmp(s, "stall ", 6) == 0 ||
             strncmp(s, "sse-file ", 9) == 0 ||
             strncmp(s, "embed ", 6) == 0)) {
            return mm_parse_fail(&cur, out, err, errcap, line,
                                 "rule already has an action");
        }
        if (strncmp(s, "text ", 5) == 0 || strcmp(s, "text") == 0) {
            const char *t = (s[4] == '\0') ? s + 4 : mm_skip_ws(s + 5);
            cur.action = MM_ACT_TEXT;
            cur.arg1 = mm_strdup_n(t, strlen(t));
            if (cur.arg1 == NULL) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "out of memory");
            }
            continue;
        }
        if (strncmp(s, "tool ", 5) == 0) {
            const char *t = mm_skip_ws(s + 5);
            const char *sp = strchr(t, ' ');
            if (sp == NULL || *mm_skip_ws(sp) == '\0') {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "tool needs NAME {json}");
            }
            if (cur.ntools >= MM_MAX_TOOLS) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "too many tool lines in one rule");
            }
            cur.action = MM_ACT_TOOL;
            cur.tool_name[cur.ntools] = mm_strdup_n(t, (size_t)(sp - t));
            cur.tool_args[cur.ntools] = mm_strdup_n(mm_skip_ws(sp),
                                                    strlen(mm_skip_ws(sp)));
            if (cur.tool_name[cur.ntools] == NULL ||
                cur.tool_args[cur.ntools] == NULL) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "out of memory");
            }
            /* arg1/arg2 keep naming the FIRST tool, so every existing rule and
             * unit test that reads them is unaffected. */
            if (cur.ntools == 0) {
                cur.arg1 = cur.tool_name[0];
                cur.arg2 = cur.tool_args[0];
            }
            cur.ntools++;
            continue;
        }
        if (strncmp(s, "status ", 7) == 0) {
            long v = strtol(s + 7, NULL, 10);
            if (v < 100 || v > 599) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "status must be 100..599");
            }
            cur.action = MM_ACT_STATUS;
            cur.status = (int)v;
            continue;
        }
        if (strncmp(s, "location ", 9) == 0) {
            if (cur.action != MM_ACT_STATUS) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "location only after status");
            }
            free(cur.location);
            cur.location = mm_strdup_n(mm_skip_ws(s + 9),
                                       strlen(mm_skip_ws(s + 9)));
            if (cur.location == NULL) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "out of memory");
            }
            continue;
        }
        if (strncmp(s, "body ", 5) == 0) {
            if (cur.action != MM_ACT_STATUS) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "body only after status");
            }
            free(cur.arg1);
            cur.arg1 = mm_strdup_n(mm_skip_ws(s + 5),
                                   strlen(mm_skip_ws(s + 5)));
            if (cur.arg1 == NULL) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "out of memory");
            }
            continue;
        }
        if (strncmp(s, "body-file ", 10) == 0) {
            if (cur.action != MM_ACT_STATUS) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "body-file only after status");
            }
            free(cur.body_file);
            cur.body_file = mm_strdup_n(mm_skip_ws(s + 10),
                                        strlen(mm_skip_ws(s + 10)));
            if (cur.body_file == NULL) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "out of memory");
            }
            continue;
        }
        if (strncmp(s, "stall ", 6) == 0) {
            const char *w = mm_skip_ws(s + 6);
            if (strcmp(w, "header") == 0)
                cur.action = MM_ACT_STALL_HEADER;
            else if (strcmp(w, "mid") == 0)
                cur.action = MM_ACT_STALL_MID;
            else {
                return mm_parse_fail(&cur, out, err, errcap, line,
                              "stall needs header|mid");
            }
            continue;
        }
        if (strncmp(s, "embed ", 6) == 0) {
            const char *w = mm_skip_ws(s + 6);
            if (*w == '\0') {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "embed needs at least one word");
            }
            cur.action = MM_ACT_EMBED;
            cur.arg1 = mm_strdup_n(w, strlen(w));
            if (cur.arg1 == NULL) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "out of memory");
            }
            continue;
        }
        if (strncmp(s, "sse-file ", 9) == 0) {
            cur.action = MM_ACT_SSE_FILE;
            cur.arg1 = mm_strdup_n(mm_skip_ws(s + 9),
                                   strlen(mm_skip_ws(s + 9)));
            if (cur.arg1 == NULL) {
                return mm_parse_fail(&cur, out, err, errcap, line,
                                     "out of memory");
            }
            continue;
        }

        return mm_parse_fail(&cur, out, err, errcap, line, "unknown directive");
    }

    if (in_rule) {
        struct mm_rule *n;
        if (cur.action == MM_ACT_NONE) {
            return mm_parse_fail(&cur, out, err, errcap, line,
                                 "last rule has no action");
        }
        n = (struct mm_rule *)realloc(out->rules,
                (size_t)(out->nrules + 1) * sizeof(*n));
        if (n == NULL) {
            return mm_parse_fail(&cur, out, err, errcap, line, "out of memory");
        }
        out->rules = n;
        out->rules[out->nrules++] = cur;
        in_rule = 0;
    }

    if (out->nrules == 0)
        return mm_err(err, errcap, line, "no rules");
    return 0;
}

void mm_script_free(struct mm_script *s)
{
    int i, j;
    if (s == NULL || s->rules == NULL) {
        if (s != NULL)
            s->nrules = 0;
        return;
    }
    for (i = 0; i < s->nrules; i++) {
        for (j = 0; j < s->rules[i].nmatch; j++)
            free(s->rules[i].match[j]);
        free(s->rules[i].arg1);
        free(s->rules[i].arg2);
        free(s->rules[i].body_file);
        free(s->rules[i].location);
    }
    free(s->rules);
    s->rules = NULL;
    s->nrules = 0;
}

const struct mm_rule *mm_select(const struct mm_script *s,
                                const char *body, size_t body_len,
                                int req_index)
{
    int i, j;
    for (i = 0; i < s->nrules; i++) {
        const struct mm_rule *r = &s->rules[i];
        int hit = 1;
        if (r->count != 0 && r->count != req_index)
            continue;
        if (r->larger > 0 && body_len <= (size_t)r->larger)
            continue;
        for (j = 0; j < r->nmatch; j++) {
            int present = mm_find(body, body_len, r->match[j],
                                  r->match_len[j]) != NULL;
            if (r->match_neg[j] ? present : !present) {
                hit = 0;
                break;
            }
        }
        if (hit)
            return r;
    }
    return NULL;
}

/* --- rendering ----------------------------------------------------------- */

/* Append src to a growable buffer. Returns 0 / -1. */
struct mm_buf {
    char *data;
    size_t len;
    size_t cap;
};

static int mm_buf_add(struct mm_buf *b, const char *src, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t ncap = (b->cap == 0) ? 256 : b->cap;
        char *nd;
        while (ncap < b->len + n + 1)
            ncap *= 2;
        nd = (char *)realloc(b->data, ncap);
        if (nd == NULL)
            return -1;
        b->data = nd;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int mm_buf_adds(struct mm_buf *b, const char *s)
{
    return mm_buf_add(b, s, strlen(s));
}

/* Append s JSON-string-escaped (without surrounding quotes). */
static int mm_buf_add_json(struct mm_buf *b, const char *s)
{
    while (*s != '\0') {
        unsigned char c = (unsigned char)*s;
        char tmp[8];
        if (c == '"' || c == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            if (mm_buf_add(b, tmp, 2) != 0)
                return -1;
        } else if (c < 0x20) {
            int n = jc_snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)c);
            if (n < 0 || mm_buf_add(b, tmp, (size_t)n) != 0)
                return -1;
        } else {
            tmp[0] = (char)c;
            if (mm_buf_add(b, tmp, 1) != 0)
                return -1;
        }
        s++;
    }
    return 0;
}

/* The standard SSE response head. When body_len is (size_t)-1, no
 * Content-Length is emitted (the stall cases: the peer must keep waiting). */
static int mm_head_sse(struct mm_buf *b, size_t body_len)
{
    char tmp[128];
    if (mm_buf_adds(b, "HTTP/1.1 200 OK\r\n"
                       "Content-Type: text/event-stream\r\n") != 0)
        return -1;
    if (body_len != (size_t)-1) {
        int n = jc_snprintf(tmp, sizeof(tmp),
                            "Connection: close\r\nContent-Length: %lu\r\n",
                            (unsigned long)body_len);
        if (n < 0 || mm_buf_add(b, tmp, (size_t)n) != 0)
            return -1;
    }
    return mm_buf_adds(b, "\r\n");
}

/* One content-delta SSE event ("data: {...}\n\n"). */
static int mm_sse_text_delta(struct mm_buf *b, const char *content)
{
    if (mm_buf_adds(b, "data: {\"id\":\"1\",\"object\":"
                       "\"chat.completion.chunk\",\"choices\":[{\"index\":0,"
                       "\"delta\":{\"role\":\"assistant\",\"content\":\"") != 0)
        return -1;
    if (mm_buf_add_json(b, content) != 0)
        return -1;
    return mm_buf_adds(b, "\"},\"finish_reason\":null}]}\n\n");
}

static int mm_body_text(struct mm_buf *b, const struct mm_rule *r)
{
    char tmp[160];
    long in_tok = (r->usage_in >= 0) ? r->usage_in : MM_USAGE_IN_DEFAULT;
    long out_tok = (r->usage_out >= 0) ? r->usage_out : MM_USAGE_OUT_DEFAULT;
    int n;

    if (mm_sse_text_delta(b, r->arg1) != 0)
        return -1;
    if (mm_buf_adds(b, "data: {\"id\":\"1\",\"object\":"
                       "\"chat.completion.chunk\",\"choices\":[{\"index\":0,"
                       "\"delta\":{},\"finish_reason\":\"stop\"}],") != 0)
        return -1;
    n = jc_snprintf(tmp, sizeof(tmp),
                    "\"usage\":{\"prompt_tokens\":%ld,"
                    "\"completion_tokens\":%ld}}\n\n", in_tok, out_tok);
    if (n < 0 || mm_buf_add(b, tmp, (size_t)n) != 0)
        return -1;
    return mm_buf_adds(b, "data: [DONE]\n\n");
}

/* One SSE chunk carrying r->ntools entries in a single tool_calls array -- the
 * shape a real provider sends when the model calls several tools in one
 * assistant message. With ntools == 1 the bytes are identical to the
 * single-tool form this replaced. */
static int mm_body_tool(struct mm_buf *b, const struct mm_rule *r)
{
    int i;
    int n = (r->ntools > 0) ? r->ntools : 1;

    if (mm_buf_adds(b, "data: {\"id\":\"1\",\"object\":"
                       "\"chat.completion.chunk\",\"choices\":[{\"index\":0,"
                       "\"delta\":{\"role\":\"assistant\",\"tool_calls\":"
                       "[") != 0)
        return -1;
    for (i = 0; i < n; i++) {
        const char *nm = (r->ntools > 0) ? r->tool_name[i] : r->arg1;
        const char *ag = (r->ntools > 0) ? r->tool_args[i] : r->arg2;
        char idbuf[16];

        if (i > 0 && mm_buf_adds(b, ",") != 0)
            return -1;
        /* index must be the position in the array; the id merely has to be
         * distinct, so it is derived from it. */
        idbuf[0] = 'c';
        idbuf[1] = (char)('1' + i);
        idbuf[2] = '\0';
        if (mm_buf_adds(b, "{\"index\":") != 0)
            return -1;
        {
            char nbuf[8];
            nbuf[0] = (char)('0' + i);
            nbuf[1] = '\0';
            if (mm_buf_adds(b, nbuf) != 0)
                return -1;
        }
        if (mm_buf_adds(b, ",\"id\":\"") != 0)
            return -1;
        if (mm_buf_adds(b, idbuf) != 0)
            return -1;
        if (mm_buf_adds(b, "\",\"type\":\"function\",\"function\":"
                           "{\"name\":\"") != 0)
            return -1;
        if (mm_buf_add_json(b, nm) != 0)
            return -1;
        if (mm_buf_adds(b, "\",\"arguments\":\"") != 0)
            return -1;
        if (mm_buf_add_json(b, ag) != 0)
            return -1;
        if (mm_buf_adds(b, "\"}}") != 0)
            return -1;
    }
    /* M441: the tool path reports usage too, in a SEPARATE final chunk -- the
     * shape a real OpenAI-compatible provider sends (usage arrives on a chunk
     * whose `choices` is empty, after the content/tool chunks).
     *
     * It emitted none before, so in a mocked run `tokens_used` stayed 0 through
     * every tool round and only moved on the final text response. Every token
     * figure the smoke tier asserted was therefore jichi's own byte-estimate
     * rather than a wire value, and anything keyed off real usage -- the M431f
     * budget panel's spend RATE, most visibly -- could not be tested at all.
     *
     * Same defaults as the text path, so a `usage IN OUT` line overrides both
     * and the two paths cannot drift on what "unspecified" means. */
    if (mm_buf_adds(b, "]},\"finish_reason\":\"tool_calls\"}]}\n\n") != 0)
        return -1;
    {
        char tmp[160];
        long in_tok = (r->usage_in >= 0) ? r->usage_in : MM_USAGE_IN_DEFAULT;
        long out_tok = (r->usage_out >= 0) ? r->usage_out : MM_USAGE_OUT_DEFAULT;
        int nn = jc_snprintf(tmp, sizeof(tmp),
                            "data: {\"id\":\"1\",\"object\":"
                            "\"chat.completion.chunk\",\"choices\":[],"
                            "\"usage\":{\"prompt_tokens\":%ld,"
                            "\"completion_tokens\":%ld}}\n\n",
                            in_tok, out_tok);
        if (nn < 0 || mm_buf_add(b, tmp, (size_t)nn) != 0)
            return -1;
    }
    if (mm_buf_adds(b, "data: [DONE]\n\n") != 0)
        return -1;
    return 0;
}

int mm_render_response(const struct mm_rule *r, char **out, size_t *outlen)
{
    struct mm_buf head, body;
    int rc = -1;

    memset(&head, 0, sizeof(head));
    memset(&body, 0, sizeof(body));
    *out = NULL;
    *outlen = 0;

    switch (r->action) {
    case MM_ACT_TEXT:
        if (mm_body_text(&body, r) != 0)
            goto done;
        if (mm_head_sse(&head, body.len) != 0)
            goto done;
        break;
    case MM_ACT_TOOL:
        if (mm_body_tool(&body, r) != 0)
            goto done;
        if (mm_head_sse(&head, body.len) != 0)
            goto done;
        break;
    case MM_ACT_STATUS: {
        char tmp[160];
        const char *b = (r->arg1 != NULL) ? r->arg1 : "";
        int n;
        if (mm_buf_adds(&body, b) != 0)
            goto done;
        n = jc_snprintf(tmp, sizeof(tmp),
                        "HTTP/1.1 %d Status\r\n"
                        "Content-Type: application/json\r\n",
                        r->status);
        if (n < 0 || mm_buf_add(&head, tmp, (size_t)n) != 0)
            goto done;
        /* Location, for a 3xx (M472). Emitted before the closing CRLF so it
         * joins the same header block; a long URL is truncated by jc_snprintf
         * rather than overrunning, and a truncated Location simply makes the
         * redirect unfollowable, which fails the driver loudly. */
        if (r->location != NULL && r->location[0] != '\0') {
            n = jc_snprintf(tmp, sizeof(tmp), "Location: %s\r\n", r->location);
            if (n < 0 || mm_buf_add(&head, tmp, (size_t)n) != 0)
                goto done;
        }
        n = jc_snprintf(tmp, sizeof(tmp),
                        "Connection: close\r\nContent-Length: %lu\r\n\r\n",
                        (unsigned long)body.len);
        if (n < 0 || mm_buf_add(&head, tmp, (size_t)n) != 0)
            goto done;
        break;
    }
    case MM_ACT_STALL_HEADER:
        if (mm_head_sse(&head, (size_t)-1) != 0)
            goto done;
        break;
    case MM_ACT_STALL_MID:
        if (mm_head_sse(&head, (size_t)-1) != 0)
            goto done;
        if (mm_sse_text_delta(&body, "partial") != 0)
            goto done;
        break;
    case MM_ACT_SSE_FILE:
    case MM_ACT_EMBED:
    case MM_ACT_NONE:
        goto done;
    }

    if (body.len > 0 && mm_buf_add(&head, body.data, body.len) != 0)
        goto done;
    *out = head.data;
    *outlen = head.len;
    head.data = NULL;
    rc = 0;
done:
    free(head.data);
    free(body.data);
    return rc;
}

int mm_render_status_body(int status, const char *body, size_t body_len,
                          char **out, size_t *outlen)
{
    struct mm_buf b;
    char tmp[160];
    int n;

    memset(&b, 0, sizeof(b));
    *out = NULL;
    *outlen = 0;
    n = jc_snprintf(tmp, sizeof(tmp),
                    "HTTP/1.1 %d Status\r\n"
                    "Content-Type: application/octet-stream\r\n"
                    "Connection: close\r\nContent-Length: %lu\r\n\r\n",
                    status, (unsigned long)body_len);
    if (n < 0 || mm_buf_add(&b, tmp, (size_t)n) != 0 ||
        mm_buf_add(&b, body, body_len) != 0) {
        free(b.data);
        return -1;
    }
    *out = b.data;
    *outlen = b.len;
    return 0;
}

int mm_render_sse_body(const char *body, size_t body_len,
                       char **out, size_t *outlen)
{
    struct mm_buf b;
    memset(&b, 0, sizeof(b));
    *out = NULL;
    *outlen = 0;
    if (mm_head_sse(&b, body_len) != 0 ||
        mm_buf_add(&b, body, body_len) != 0) {
        free(b.data);
        return -1;
    }
    *out = b.data;
    *outlen = b.len;
    return 0;
}

long mm_count_ci(const char *hay, const char *needle)
{
    size_t nlen = strlen(needle);
    long n = 0;
    if (nlen == 0)
        return 0;
    while (*hay != '\0') {
        size_t j;
        for (j = 0; j < nlen; j++) {
            char a = hay[j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a == '\0' || a != b)
                break;
        }
        if (j == nlen) {
            n++;
            hay += nlen;    /* non-overlapping, like Python str.count */
        } else {
            hay++;
        }
    }
    return n;
}

/* --- incremental HTTP request parser ------------------------------------- */

void mm_http_init(struct mm_http *h)
{
    memset(h, 0, sizeof(*h));
    h->content_length = -1;
    h->state = MM_HTTP_NEED_MORE;
}

void mm_http_free(struct mm_http *h)
{
    free(h->buf);
    memset(h, 0, sizeof(*h));
    h->content_length = -1;
}

/* Case-insensitive search for a header name at line starts within the head;
 * returns the value's numeric parse or -1. */
static long mm_http_header_num(const char *head, size_t head_len,
                               const char *name)
{
    size_t i = 0;
    size_t nlen = strlen(name);
    while (i < head_len) {
        /* start of a line */
        size_t j;
        int is = 1;
        for (j = 0; j < nlen; j++) {
            char a, b;
            if (i + j >= head_len) { is = 0; break; }
            a = head[i + j];
            b = name[j];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b) { is = 0; break; }
        }
        if (is && i + nlen < head_len && head[i + nlen] == ':')
            return strtol(head + i + nlen + 1, NULL, 10);
        /* advance to the next line */
        while (i < head_len && head[i] != '\n')
            i++;
        i++;
    }
    return -1;
}

/* Is Transfer-Encoding: chunked declared? (jichi never sends it; treat as
 * a hard error so a foreign peer fails loudly, not silently truncated.) */
static int mm_http_is_chunked(const char *head, size_t head_len)
{
    size_t i;
    static const char te[] = "transfer-encoding";
    size_t tlen = sizeof(te) - 1;
    for (i = 0; i + tlen < head_len; i++) {
        size_t j;
        int is = 1;
        for (j = 0; j < tlen; j++) {
            char a = head[i + j];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (a != te[j]) { is = 0; break; }
        }
        if (is)
            return 1;
    }
    return 0;
}

int mm_http_feed(struct mm_http *h, const char *bytes, size_t n)
{
    if (h->state != MM_HTTP_NEED_MORE)
        return h->state;

    if (h->len + n + 1 > h->cap) {
        size_t ncap = (h->cap == 0) ? 4096 : h->cap;
        char *nb;
        while (ncap < h->len + n + 1)
            ncap *= 2;
        if (ncap > MM_HTTP_MAX) {
            h->state = MM_HTTP_ERROR;
            return h->state;
        }
        nb = (char *)realloc(h->buf, ncap);
        if (nb == NULL) {
            h->state = MM_HTTP_ERROR;
            return h->state;
        }
        h->buf = nb;
        h->cap = ncap;
    }
    memcpy(h->buf + h->len, bytes, n);
    h->len += n;
    h->buf[h->len] = '\0';

    if (h->head_end == 0) {
        /* look for the end of head: \r\n\r\n (tolerate bare \n\n) */
        size_t i;
        for (i = 0; i + 1 < h->len; i++) {
            if (h->buf[i] == '\n' &&
                (h->buf[i + 1] == '\n' ||
                 (i + 2 < h->len && h->buf[i + 1] == '\r' &&
                  h->buf[i + 2] == '\n'))) {
                h->head_end = (h->buf[i + 1] == '\n') ? i + 2 : i + 3;
                break;
            }
        }
        if (h->head_end == 0)
            return h->state;    /* still inside the head */
        if (mm_http_is_chunked(h->buf, h->head_end)) {
            h->state = MM_HTTP_ERROR;
            return h->state;
        }
        h->content_length = mm_http_header_num(h->buf, h->head_end,
                                               "content-length");
    }

    /* COMPLETE only when the declared body has fully arrived. A missing
     * Content-Length means no body (GET): complete at end-of-head. This is
     * the invariant that makes the ANECDOTES #18 truncation bug impossible:
     * a pause in the byte stream can only ever yield NEED_MORE. */
    if (h->content_length < 0) {
        h->state = MM_HTTP_COMPLETE;
    } else if (h->len >= h->head_end + (size_t)h->content_length) {
        h->state = MM_HTTP_COMPLETE;
    }
    return h->state;
}

const char *mm_http_head(const struct mm_http *h, size_t *len)
{
    if (len != NULL)
        *len = h->head_end;
    return h->buf;
}

const char *mm_http_body(const struct mm_http *h, size_t *len)
{
    size_t blen = 0;
    if (h->head_end > 0 && h->len >= h->head_end)
        blen = h->len - h->head_end;
    if (h->content_length >= 0 && blen > (size_t)h->content_length)
        blen = (size_t)h->content_length;
    if (len != NULL)
        *len = blen;
    return (h->buf != NULL) ? h->buf + h->head_end : NULL;
}
