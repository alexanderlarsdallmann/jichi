/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* cJSON.c - a minimal, cJSON-API-compatible JSON parser and serialiser.
 *
 * Part of jichi; the licence is the tree's, stated in the SPDX line above.
 *
 * PROVENANCE -- read this before assuming otherwise. This file is NOT upstream
 * cJSON and contains no third-party code. It is an original, from-scratch C89
 * implementation of the subset of the cJSON *API* that jichi uses. It lived under
 * `third_party/cjson/` until M171, which was doubly misleading: it implied we had
 * vendored someone else's MIT-licensed library (we had not), and an auditor
 * looking for cJSON's copyright notice would find none and reasonably conclude the
 * notice had been stripped. jichi vendors no third-party source at all.
 *
 * Only the API is shared, deliberately: the function names, the `cJSON` struct
 * layout and the double-based number model match upstream, so the real library
 * (github.com/DaveGamble/cJSON) can replace this file and cJSON.h as a pair
 * without touching anything else in the tree.
 *
 * Compact recursive-descent parser and serialiser. Compiles under the same
 * -std=c89 -pedantic -Wall -Wextra -Werror as every other translation unit; the
 * old -pedantic exemption existed only because of the mistaken provenance.
 */

#include "cJSON.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ===================== allocation helpers ============================== */

static cJSON *cJSON_New_Item(void)
{
    cJSON *node = (cJSON *)malloc(sizeof(cJSON));
    if (node != NULL) {
        memset(node, 0, sizeof(cJSON));
    }
    return node;
}

void cJSON_free(void *ptr)
{
    free(ptr);
}

void cJSON_Delete(cJSON *item)
{
    cJSON *next;
    while (item != NULL) {
        next = item->next;
        if (item->child != NULL) {
            cJSON_Delete(item->child);
        }
        if (item->valuestring != NULL) {
            free(item->valuestring);
        }
        if (item->string != NULL) {
            free(item->string);
        }
        free(item);
        item = next;
    }
}

/* ===================== parser ========================================== */

/* Hard bound on container nesting (M472). The input controls how deep this
 * parser recurses, so without a bound the input controls the stack: measured
 * SIGSEGV at ~75,000 levels (150 KB of '[') on an 8 MB stack, ~6,000 (12 KB) at
 * 512 KB, and ~2,000 (4 KB) at 128 KB -- the small-stack rows LOW_MEMORY.md
 * ships to, where a 4 KB message was enough to kill the agent. Reachable from
 * model tool-call arguments, MCP and LSP server replies, and the ACP channel,
 * i.e. mostly from OUTSIDE the trust boundary.
 *
 * JC_SSE_FIELD_MAX (4 MB) does not help: a byte cap and a depth cap are not
 * substitutes, and it sits ~27x above the threshold.
 *
 * 256 is generous by two orders of magnitude -- real provider, MCP and LSP
 * payloads nest under about 12 -- so no legitimate document is refused. A
 * coverage-guided fuzzer cannot find this class, because depth generates no new
 * coverage after the second '['; tests/fuzz/corpus/json/deep-nesting-* keep it
 * found. See docs/analysis/2026-08-17-source-hardening-audit.md §H4. */
#define JC_JSON_MAX_DEPTH 256

/* Hard bound on the number of nodes ONE parse allocates (M1089, A-06).
 * M472's depth cap bounds nesting, but not node count: every value is a
 * malloc'd 64-byte node, so a dense array of small numbers is one node per
 * element, and a large-but-valid reply expands ~40x its bytes into tree
 * memory while it is parsed. The lead confirmed on head: a 1,000,000-element
 * array of numbers (1.9 MB) parses into ~78 MB peak RSS, delta 77,952 KB.
 * The 4 MB per-SSE-field byte cap does not help -- it bounds the input, not
 * the allocation (docs/analysis/2026-10-17-hardening-02-input-and-memory.md,
 * A-06).
 *
 * 1,000,000 clears the largest legitimate parse -- an embeddings reply,
 * JC_EMBED_BATCH (64) items each carrying an index, a usage object (3
 * fields) and a vector of dim <= 8192 floats, ~524,000 nodes in all -- with
 * nearly double the headroom, while the finding's ~2,000,000-node 4 MB
 * dense array is refused. Enforced at parse-time allocation only
 * (parse_new_item below): the public constructors, through which jichi builds
 * its OWN request bodies, are not capped. Runtime-lowerable for the test
 * (cJSON_jc_set_max_nodes, declared in cJSON.h). */
#define JC_JSON_MAX_NODES 1000000

struct parse_ctx {
    const char *p;    /* current position */
    const char *end;  /* one past the last byte */
    int         ok;
    int         depth; /* container nesting; bounds stack use (M472) */
    size_t      nodes; /* parse-time allocations; bounds heap use (M1089) */
};

/* The per-parse node cap, runtime-lowerable (M1089). Default is
 * JC_JSON_MAX_NODES; only tests lower it, for the duration of one parse, and
 * restore it -- the shape the existing test-only counters (g_jc_string_bytes,
 * g_jc_append_steps) set. */
static size_t g_cjson_max_nodes = JC_JSON_MAX_NODES;

void cJSON_jc_set_max_nodes(size_t n)
{
    g_cjson_max_nodes = n;
}

size_t cJSON_jc_get_max_nodes(void)
{
    return g_cjson_max_nodes;
}

/* M1094 (G3): 1 when the most recent cJSON_Parse call was refused by the
 * per-parse node cap above, else 0. Set 0 at the top of cJSON_Parse and 1
 * ONLY in parse_new_item's cap-refusal branch, so it distinguishes "the
 * document would exceed the node cap" from "the document is malformed JSON"
 * (which also returns NULL, but reads 0 here). It does not clear itself: a
 * parse is atomic, so the flag describes the most recent cJSON_Parse call.
 * The provider dialects consult it in on_event's root == NULL branch to mark
 * a cap-refused streamed event truncated instead of dropping it silently.
 * Only a parse can set it; the public constructors are untouched. */
static int g_cjson_last_parse_refused_by_cap = 0;

int cJSON_jc_parse_refused_by_cap(void)
{
    return g_cjson_last_parse_refused_by_cap;
}

/* Parse-time allocation, capped at g_cjson_max_nodes (M1089/A-06). The public
 * constructors keep calling cJSON_New_Item uncapped -- the finding is the
 * parser, and jichi builds its own request bodies through them. When the cap
 * is reached this sets c->ok = 0 and returns NULL, so the parse fails as a
 * whole: the enclosing containers cJSON_Delete themselves (and their
 * children) on the NULL they get back, exactly as they already do on any
 * child failure, so the partially built tree is not returned and nothing the
 * parse allocated leaks. */
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

static void skip_ws(struct parse_ctx *c)
{
    while (c->p < c->end) {
        char ch = *c->p;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            c->p++;
        } else {
            break;
        }
    }
}

static cJSON *parse_value(struct parse_ctx *c);

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

/* Encode a Unicode code point into UTF-8, appending to buf at *len. buf must
 * have room (caller sizes generously: <= 4 bytes per code point). */
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

typedef size_t jc_psize;

/* Parse a JSON string (c->p points at the opening quote). Returns a malloc'd
 * NUL-terminated decoded string, or NULL on error. */
/* Bytes parse_string has allocated, for the test that holds its sizing (M725). */
static size_t g_jc_string_bytes;

size_t cJSON_jc_string_bytes(void)
{
    return g_jc_string_bytes;
}

/* `next` steps cJSON_AddItemToArray's tail search has walked, for the test
 * that holds its O(1) append (M1085/F-05). Mirrors g_jc_string_bytes: a global
 * counter incremented at the hot site and read through a public accessor. */
static size_t g_jc_append_steps;

size_t cJSON_jc_append_steps(void)
{
    return g_jc_append_steps;
}

static char *parse_string(struct parse_ctx *c)
{
    const char *p;
    char *out;
    jc_psize cap;
    jc_psize len;

    if (c->p >= c->end || *c->p != '"') {
        c->ok = 0;
        return NULL;
    }
    c->p++; /* opening quote */
    p = c->p;

    /* Size the buffer to THIS string's span, found by a first pass to the
     * closing quote: the decoded string is never longer than its source,
     * because every escape decodes to no more bytes than it occupies (\uXXXX,
     * six, to at most three; a surrogate pair, twelve, to four). Until M725 the
     * bound was the REST OF THE INPUT, for every string, and the buffer was
     * kept at that size as the node's key or value -- so a document's memory
     * grew as strings x remaining bytes. An embeddings reply of ~1.5 MB with
     * ~260 strings held ~190 MB of it: the peak massif found in `jichi index`,
     * which M700's page credited to the float nodes. */
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
    len = 0;

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

    if (p >= c->end || *p != '"') {
        free(out);
        c->ok = 0;
        return NULL;
    }
    p++; /* closing quote */
    c->p = p;
    out[len] = '\0';
    return out;
}

/* The int view of a number, clamped: casting a double outside int's range to int is
 * undefined behaviour (UBSan: "outside the range of representable values of type
 * 'int'"). NaN reads as 0. Shared by the parser and cJSON_CreateNumber -- M866: the
 * parser had been fixed (below) and CreateNumber had kept the bare cast, until M861's
 * unit test built 5e9 and batch 4's make ci stopped on it. */
static int number_to_int(double val)
{
    if (val != val) {
        return 0;
    }
    if (val >= (double)INT_MAX) {
        return INT_MAX;
    }
    if (val <= (double)INT_MIN) {
        return INT_MIN;
    }
    return (int)val;
}

static cJSON *parse_number(struct parse_ctx *c)
{
    char buf[64];
    int i = 0;
    const char *p = c->p;
    cJSON *node;
    double val;

    while (p < c->end && i < (int)sizeof(buf) - 1) {
        char ch = *p;
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' ||
            ch == '.' || ch == 'e' || ch == 'E') {
            buf[i++] = ch;
            p++;
        } else {
            break;
        }
    }
    buf[i] = '\0';
    if (i == 0) {
        c->ok = 0;
        return NULL;
    }
    val = strtod(buf, NULL);
    node = parse_new_item(c);
    if (node == NULL) {
        c->ok = 0;
        return NULL;
    }
    node->type = cJSON_Number;
    node->valuedouble = val;
    /* CLAMP, do not cast. `(int)val` is UNDEFINED when val is outside int's
     * range (C89 6.2.1.3 / C99 6.3.1.4), and jichi parses JSON it did not
     * write: a model response, a fetched page, an MCP result. `{"x":1e300}` is
     * valid JSON and was enough to reach it.
     *
     * Found by the M469 architecture sweep on armeb, where zig cc's default
     * UB traps turned it into an abort in the middle of the unit suite, then
     * reproduced on x86-64 with `clang -fsanitize=undefined`:
     *
     *   src/json/cJSON.c:262:22: runtime error: 1e+300 is outside the range of
     *   representable values of type 'int'
     *
     * `make ci` runs ASan+UBSan and had never seen it, because nothing in the
     * unit corpus ever fed the parser a number outside int range -- the
     * sanitizer was there, the input was not.
     *
     * The NaN arm cannot be reached from JSON (no NaN literal, and strtod on a
     * numeric string does not produce one), and is written anyway because both
     * range comparisons are false for NaN and the fall-through would be the
     * same undefined cast this exists to remove. */
    node->valueint = number_to_int(val);
    c->p = p;
    return node;
}

static cJSON *parse_literal(struct parse_ctx *c, const char *word, int type)
{
    jc_psize n = strlen(word);
    cJSON *node;
    if ((jc_psize)(c->end - c->p) < n || strncmp(c->p, word, n) != 0) {
        c->ok = 0;
        return NULL;
    }
    node = parse_new_item(c);
    if (node == NULL) {
        c->ok = 0;
        return NULL;
    }
    node->type = type;
    c->p += n;
    return node;
}

static cJSON *parse_array(struct parse_ctx *c)
{
    cJSON *arr;
    cJSON *tail = NULL;

    arr = parse_new_item(c);
    if (arr == NULL) {
        c->ok = 0;
        return NULL;
    }
    arr->type = cJSON_Array;
    c->p++; /* '[' */
    skip_ws(c);
    if (c->p < c->end && *c->p == ']') {
        c->p++;
        return arr;
    }
    for (;;) {
        cJSON *item;
        skip_ws(c);
        item = parse_value(c);
        if (!c->ok || item == NULL) {
            c->ok = 0;
            cJSON_Delete(arr);
            return NULL;
        }
        if (tail == NULL) {
            arr->child = item;
        } else {
            tail->next = item;
            item->prev = tail;
        }
        tail = item;
        arr->tail = item;   /* M1085/F-05: keep the container's cached tail */
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
}

static cJSON *parse_object(struct parse_ctx *c)
{
    cJSON *obj;
    cJSON *tail = NULL;

    obj = parse_new_item(c);
    if (obj == NULL) {
        c->ok = 0;
        return NULL;
    }
    obj->type = cJSON_Object;
    c->p++; /* '{' */
    skip_ws(c);
    if (c->p < c->end && *c->p == '}') {
        c->p++;
        return obj;
    }
    for (;;) {
        char *key;
        cJSON *item;
        skip_ws(c);
        key = parse_string(c);
        if (!c->ok || key == NULL) {
            c->ok = 0;
            cJSON_Delete(obj);
            return NULL;
        }
        skip_ws(c);
        if (c->p >= c->end || *c->p != ':') {
            free(key);
            c->ok = 0;
            cJSON_Delete(obj);
            return NULL;
        }
        c->p++; /* ':' */
        skip_ws(c);
        item = parse_value(c);
        if (!c->ok || item == NULL) {
            free(key);
            c->ok = 0;
            cJSON_Delete(obj);
            return NULL;
        }
        item->string = key;
        if (tail == NULL) {
            obj->child = item;
        } else {
            tail->next = item;
            item->prev = tail;
        }
        tail = item;
        obj->tail = item;   /* M1085/F-05: keep the container's cached tail */
        skip_ws(c);
        if (c->p < c->end && *c->p == ',') {
            c->p++;
            continue;
        }
        if (c->p < c->end && *c->p == '}') {
            c->p++;
            return obj;
        }
        c->ok = 0;
        cJSON_Delete(obj);
        return NULL;
    }
}

static cJSON *parse_value(struct parse_ctx *c)
{
    skip_ws(c);
    if (c->p >= c->end) {
        c->ok = 0;
        return NULL;
    }
    switch (*c->p) {
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
        case 't': return parse_literal(c, "true",  cJSON_True);
        case 'f': return parse_literal(c, "false", cJSON_False);
        case 'n': return parse_literal(c, "null",  cJSON_NULL);
        default:  return parse_number(c);
    }
}

cJSON *cJSON_Parse(const char *value)
{
    struct parse_ctx c;
    cJSON *root;

    if (value == NULL) {
        return NULL;
    }
    g_cjson_last_parse_refused_by_cap = 0; /* M1094 (G3) */
    c.p = value;
    c.end = value + strlen(value);
    c.ok = 1;
    c.depth = 0;
    c.nodes = 0; /* fresh per-parse counter; capped by parse_new_item (M1089) */
    root = parse_value(&c);
    if (c.ok) {
        /* A-08: refuse anything after the root value. The parser used to
         * return here with the first value, leaving trailing garbage
         * silently accepted ({"a":1}} -> an object). Same skip_ws the rest
         * of the parser uses between tokens, then the cursor must be at the
         * end of the input. This makes cJSON (and every caller of it,
         * including jc_jsonrepair's final validation and jc_prov_args_wire)
         * stricter: a document is one value plus nothing else. Upstream cJSON
         * accepts the trailing data; jichi refuses it on purpose. */
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

/* ===================== accessors ======================================= */

/* Duplicate-key policy (A-13, M1113): LAST-WRITE-WINS. When an object carries
 * two members with the same key, this accessor returns the LAST one, matching
 * most JSON consumers and servers; RFC 8259 leaves the policy to the consumer
 * (its "names SHOULD be unique" is a SHOULD). jichi's parser keeps every
 * member it sees -- parse_object appends with no duplicate check, because a
 * parse-time replace would be O(n^2) per object (the M1089 node-cap era) --
 * and this one accessor is where the read resolves: every jc_json_get_*
 * routes through it (jc_json.c:22/31/63/159/169/192), so a duplicate key reads
 * as the last value written.
 *
 * Boundary: jichi-built objects always carry unique keys -- the build path
 * (cJSON_AddItemToObject) is deliberately untouched, and this function is a
 * no-op for them. The residual is the shape, not the read: a duplicate object
 * still iterates (ForeachObject) and re-prints BOTH members.
 *
 * That residual matters, not just as a shape: a re-serialization of a peer
 * object -- the tool-arg re-print in jc_tool.c (unwrap_self_named :490,
 * tcr_try_candidate :979, tcr_try_tagged_fence :1038) -- and an iteration
 * (jc_tool_unstring_args :527/:535) BOTH keep every duplicate member in their
 * output. What saves the invariant is that EVERY value read goes through this
 * accessor, so the re-printed member set and the value jichi acts on resolve
 * the same (last) member. The measured consequence (SV:2259): the PreToolUse
 * hook -- which parses the re-printed event JSON last-wins, like any real
 * parser -- now AGREES with the write. Pre-M1113, with the first-wins
 * accessor, a duplicate-keyed model reply {"path":"BLOCKME.txt",...,"path":
 * "ok.txt"} let the hook judge ok.txt (allowed) while jichi wrote BLOCKME.txt
 * (the first) -- a PreToolUse-hook fence bypass. This last-wins accessor is
 * what closes it.
 */
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

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItemCaseSensitive(object, string);
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
    cJSON *child;
    if (array == NULL || index < 0) {
        return NULL;
    }
    child = array->child;
    while (child != NULL && index > 0) {
        child = child->next;
        index--;
    }
    return child;
}

int cJSON_GetArraySize(const cJSON *array)
{
    int n = 0;
    cJSON *child;
    if (array == NULL) {
        return 0;
    }
    child = array->child;
    while (child != NULL) {
        n++;
        child = child->next;
    }
    return n;
}

int cJSON_IsInvalid(const cJSON *item) { return item == NULL || item->type == cJSON_Invalid; }
int cJSON_IsFalse(const cJSON *item)   { return item != NULL && item->type == cJSON_False; }
int cJSON_IsTrue(const cJSON *item)    { return item != NULL && item->type == cJSON_True; }
int cJSON_IsBool(const cJSON *item)    { return item != NULL && (item->type == cJSON_True || item->type == cJSON_False); }
int cJSON_IsNull(const cJSON *item)    { return item != NULL && item->type == cJSON_NULL; }
int cJSON_IsNumber(const cJSON *item)  { return item != NULL && item->type == cJSON_Number; }
int cJSON_IsString(const cJSON *item)  { return item != NULL && item->type == cJSON_String; }
int cJSON_IsArray(const cJSON *item)   { return item != NULL && item->type == cJSON_Array; }
int cJSON_IsObject(const cJSON *item)  { return item != NULL && item->type == cJSON_Object; }

/* ===================== constructors ==================================== */

cJSON *cJSON_CreateNull(void)
{
    cJSON *n = cJSON_New_Item();
    if (n != NULL) n->type = cJSON_NULL;
    return n;
}

cJSON *cJSON_CreateTrue(void)
{
    cJSON *n = cJSON_New_Item();
    if (n != NULL) n->type = cJSON_True;
    return n;
}

cJSON *cJSON_CreateFalse(void)
{
    cJSON *n = cJSON_New_Item();
    if (n != NULL) n->type = cJSON_False;
    return n;
}

cJSON *cJSON_CreateBool(int boolean)
{
    return boolean ? cJSON_CreateTrue() : cJSON_CreateFalse();
}

cJSON *cJSON_CreateNumber(double num)
{
    cJSON *n = cJSON_New_Item();
    if (n != NULL) {
        n->type = cJSON_Number;
        n->valuedouble = num;
        n->valueint = number_to_int(num);   /* M866: as the parser does */
    }
    return n;
}

cJSON *cJSON_CreateString(const char *string)
{
    cJSON *n = cJSON_New_Item();
    if (n != NULL) {
        n->type = cJSON_String;
        if (string == NULL) {
            string = "";
        }
        n->valuestring = (char *)malloc(strlen(string) + 1);
        if (n->valuestring == NULL) {
            free(n);
            return NULL;
        }
        strcpy(n->valuestring, string);
    }
    return n;
}

cJSON *cJSON_CreateArray(void)
{
    cJSON *n = cJSON_New_Item();
    if (n != NULL) n->type = cJSON_Array;
    return n;
}

cJSON *cJSON_CreateObject(void)
{
    cJSON *n = cJSON_New_Item();
    if (n != NULL) n->type = cJSON_Object;
    return n;
}

/* ===================== mutation ======================================== */

int cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    cJSON *child;
    if (array == NULL || item == NULL) {
        return 0;
    }
    child = array->child;
    if (child == NULL) {
        array->child = item;
        array->tail = item;
        return 1;
    }
    /* Fast path (M1085/F-05): the container caches its tail in array->tail, so
     * appending is O(1). The cache can be NULL (the tail was never maintained,
     * e.g. the item was spliced in by code outside this file) or stale (a detach
     * path unlinked the cached tail without updating it). In either case the
     * fallback walks to the real tail and repoints the cache -- so the tree is
     * always correct, and a one-off stale tail costs one walk, not one per append.
     * The walk is counted (cJSON_jc_append_steps) and must stay O(1) per test:
     * a healthy tail means zero counted steps. */
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
}

int cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    if (object == NULL || item == NULL || string == NULL) {
        return 0;
    }
    if (item->string != NULL) {
        free(item->string);
    }
    item->string = (char *)malloc(strlen(string) + 1);
    if (item->string == NULL) {
        return 0;
    }
    strcpy(item->string, string);
    return cJSON_AddItemToArray(object, item);
}

int cJSON_ReplaceItemInObject(cJSON *object, const char *string,
                              cJSON *newitem)
{
    cJSON *child;
    if (object == NULL || string == NULL || newitem == NULL) {
        return 0;
    }
    child = object->child;
    while (child != NULL) {
        if (child->string != NULL && strcmp(child->string, string) == 0) {
            /* newitem inherits the member key from the old item. */
            if (newitem->string != NULL) {
                free(newitem->string);
            }
            newitem->string = child->string;
            child->string = NULL;
            newitem->prev = child->prev;
            newitem->next = child->next;
            if (child->prev != NULL) {
                child->prev->next = newitem;
            } else {
                object->child = newitem;
            }
            if (child->next != NULL) {
                child->next->prev = newitem;
            } else {
                /* child was the tail of object; newitem takes its place, so it
                 * is the new tail (M1085/F-05). The count is unchanged -- this is
                 * an in-place replacement, not a detach. */
                object->tail = newitem;
            }
            child->next = NULL;
            child->prev = NULL;
            cJSON_Delete(child);
            return 1;
        }
        child = child->next;
    }
    return 0;
}

cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *string)
{
    cJSON *s = cJSON_CreateString(string);
    if (s == NULL) return NULL;
    if (!cJSON_AddItemToObject(object, name, s)) {
        cJSON_Delete(s);
        return NULL;
    }
    return s;
}

cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number)
{
    cJSON *n = cJSON_CreateNumber(number);
    if (n == NULL) return NULL;
    if (!cJSON_AddItemToObject(object, name, n)) {
        cJSON_Delete(n);
        return NULL;
    }
    return n;
}

cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int boolean)
{
    cJSON *b = cJSON_CreateBool(boolean);
    if (b == NULL) return NULL;
    if (!cJSON_AddItemToObject(object, name, b)) {
        cJSON_Delete(b);
        return NULL;
    }
    return b;
}

cJSON *cJSON_AddNullToObject(cJSON *object, const char *name)
{
    cJSON *n = cJSON_CreateNull();
    if (n == NULL) return NULL;
    if (!cJSON_AddItemToObject(object, name, n)) {
        cJSON_Delete(n);
        return NULL;
    }
    return n;
}

cJSON *cJSON_AddObjectToObject(cJSON *object, const char *name)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) return NULL;
    if (!cJSON_AddItemToObject(object, name, o)) {
        cJSON_Delete(o);
        return NULL;
    }
    return o;
}

cJSON *cJSON_AddArrayToObject(cJSON *object, const char *name)
{
    cJSON *a = cJSON_CreateArray();
    if (a == NULL) return NULL;
    if (!cJSON_AddItemToObject(object, name, a)) {
        cJSON_Delete(a);
        return NULL;
    }
    return a;
}

/* ===================== printer ========================================= */

struct print_buf {
    char  *data;
    size_t len;
    size_t cap;
    int    fmt;   /* pretty-print if non-zero */
    int    oom;
};

static void pb_ensure(struct print_buf *b, size_t extra)
{
    size_t need = b->len + extra + 1;
    size_t nc;
    char *p;
    if (b->oom) {
        return;
    }
    if (need <= b->cap) {
        return;
    }
    nc = (b->cap == 0) ? 256 : b->cap;
    while (nc < need) {
        nc *= 2;
    }
    p = (char *)realloc(b->data, nc);
    if (p == NULL) {
        b->oom = 1;
        return;
    }
    b->data = p;
    b->cap = nc;
}

static void pb_putc(struct print_buf *b, char c)
{
    pb_ensure(b, 1);
    if (b->oom) return;
    b->data[b->len++] = c;
}

static void pb_puts(struct print_buf *b, const char *s)
{
    size_t n = strlen(s);
    pb_ensure(b, n);
    if (b->oom) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void pb_indent(struct print_buf *b, int depth)
{
    int i;
    if (!b->fmt) return;
    for (i = 0; i < depth; i++) {
        pb_puts(b, "  ");
    }
}

/* M1084 (F-06) fast-path hook, for the parity test (tests/test_cjson_print_parity.c):
 * the content bytes the no-escape run below has copied. The test resets it
 * before each print and asserts every no-escape byte went through the memcpy
 * -- the fast path proven to fire, not just present. Prototypes declared here
 * (same shape as cJSON_jc_string_bytes' test-only exposure, but no public
 * header change) so -Wmissing-prototypes stays clean. */
static size_t g_pb_run_bytes;

void    cJSON_pb_run_reset(void);
size_t  cJSON_pb_run_bytes(void);

void cJSON_pb_run_reset(void)
{
    g_pb_run_bytes = 0;
}

size_t cJSON_pb_run_bytes(void)
{
    return g_pb_run_bytes;
}

static void pb_put_string(struct print_buf *b, const char *s)
{
    pb_putc(b, '"');
    if (s != NULL) {
        const unsigned char *p = (const unsigned char *)s;
        while (*p) {
            unsigned char ch = *p;
            /* Fast path (F-06): a run of bytes that need no escape -- not a
             * control byte (< 0x20), not a quote (0x22), not a backslash
             * (0x5c); a byte >= 0x80 is never escaped here, and the printer
             * treats it as an ordinary content byte -- is copied with one
             * pb_ensure + memcpy instead of one pb_putc per byte. Byte-
             * identical to the per-byte switch below, which remains the slow
             * path; tests/test_cjson_print_parity.c holds it to that against
             * a verbatim copy of the pre-fix loop. */
            if (ch >= 0x20 && ch != 0x22 && ch != 0x5c) {
                const unsigned char *r = p;
                size_t run;
                while (*r && *r >= 0x20 && *r != 0x22 && *r != 0x5c) {
                    r++;
                }
                run = (size_t)(r - p);
                /* pb_ensure(run) is the same growth step pb_putc(1) would take
                 * for each of these bytes; on OOM it sets b->oom and every
                 * later pb_* call no-ops, so returning early changes nothing
                 * observable (print_with still returns NULL on b.oom). */
                pb_ensure(b, run);
                if (b->oom) return;
                memcpy(b->data + b->len, p, run);
                b->len += run;
                g_pb_run_bytes += run;
                p = r;
                continue;
            }
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
            p++;
        }
    }
    pb_putc(b, '"');
}

static void pb_put_number(struct print_buf *b, double d)
{
    char tmp[64];
    if (d != d || d > 1.0e300 || d < -1.0e300) {
        /* NaN/Inf are not valid JSON; emit null. */
        pb_puts(b, "null");
        return;
    }
    if (d == floor(d) && fabs(d) < 9007199254740992.0) {
        /* Integral with |d| < 2^53: %.0f is exact and involves NO cast.
         * The old (long)d was undefined behaviour once |d| outgrew long,
         * and on ILP32 (32-bit long) it printed 3000000000 as
         * -2147483648 -- A-09/L-04, pinned by test_json_print_exact
         * (M1076) and a -m32 scratch build (L-09: no ci stage builds
         * -m32, so the scratch is the tooth). */
        sprintf(tmp, "%.0f", d);
    } else {
        /* Fractional, or integral at/above 2^53 where %.0f would print a
         * value the double is not: %.15g first, and only when 15 digits
         * do not read back to the same double, %.17g (upstream cJSON's
         * two-step). The round-trip check is what makes the output
         * exact: re-parsing it yields d, bit for bit. */
        double rt;
        char *end;
        sprintf(tmp, "%.15g", d);
        rt = strtod(tmp, &end);
        if (rt != d) {
            sprintf(tmp, "%.17g", d);
        }
    }
    pb_puts(b, tmp);
}

/* This recurses too, and deliberately carries NO depth bound (M472). The audit
 * asked the question, so here is the answer rather than a second clamp: nothing
 * can hand it a deep tree. Every tree jichi prints is either built by this
 * file's construction API at a nesting the source fixes (a request body nests
 * about six), or parsed -- and the parser now refuses past JC_JSON_MAX_DEPTH, so
 * a parsed subtree grafted into a built one (jc_prov_args_wire, the MCP schema
 * copies) reaches 256 plus a small constant. 260 levels of print_value is three
 * orders of magnitude below the ~75,000 that crashed. A bound here would buy
 * nothing and could refuse legitimate output, which is the worse failure. If a
 * caller ever prints a tree whose depth an input controls, that changes and this
 * comment is wrong: bound it then. */
static void print_value(struct print_buf *b, const cJSON *item, int depth)
{
    if (item == NULL) {
        pb_puts(b, "null");
        return;
    }
    switch (item->type) {
        case cJSON_NULL:   pb_puts(b, "null");  break;
        case cJSON_False:  pb_puts(b, "false"); break;
        case cJSON_True:   pb_puts(b, "true");  break;
        case cJSON_Number: pb_put_number(b, item->valuedouble); break;
        case cJSON_Raw:
            if (item->valuestring) pb_puts(b, item->valuestring);
            else pb_puts(b, "null");
            break;
        case cJSON_String: pb_put_string(b, item->valuestring); break;
        case cJSON_Array: {
            cJSON *child = item->child;
            pb_putc(b, '[');
            if (child != NULL && b->fmt) pb_putc(b, '\n');
            while (child != NULL) {
                pb_indent(b, depth + 1);
                print_value(b, child, depth + 1);
                if (child->next != NULL) pb_putc(b, ',');
                if (b->fmt) pb_putc(b, '\n');
                child = child->next;
            }
            if (item->child != NULL) pb_indent(b, depth);
            pb_putc(b, ']');
            break;
        }
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
        default:
            pb_puts(b, "null");
            break;
    }
}

static char *print_with(const cJSON *item, int fmt)
{
    struct print_buf b;
    b.data = NULL;
    b.len = 0;
    b.cap = 0;
    b.fmt = fmt;
    b.oom = 0;
    print_value(&b, item, 0);
    if (b.oom) {
        free(b.data);
        return NULL;
    }
    pb_ensure(&b, 1);
    if (b.oom) {
        free(b.data);
        return NULL;
    }
    b.data[b.len] = '\0';
    return b.data;
}

char *cJSON_Print(const cJSON *item)
{
    return print_with(item, 1);
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
    return print_with(item, 0);
}
