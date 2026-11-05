/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* prov_internal.h - shared scratch and helpers for provider implementations.
 *
 * Not a public header. Both jc_provider_openai.c and jc_provider_anthropic.c
 * accumulate streamed text and tool-call fragments into this structure, then
 * flush it into the caller's jc_message when the stream completes.
 */
#ifndef JC_PROV_INTERNAL_H
#define JC_PROV_INTERNAL_H

#include "jc_provider.h"
#include "jc_str.h"

#define JC_PROV_MAX_CALLS 32
#define JC_PROV_ID_LEN    96

/* M218: shrink bounds for the per-stream scratch. jc_prov_state_reset used a
 * plain jc_sb_clear, which keeps capacity -- correct for the warm path, but
 * the state lives as long as the PROVIDER, so one 4 MB message early in a
 * session parked its high-water (x2 for the doubling growth) until model
 * switch or exit. Above these bounds the reset releases the buffer instead;
 * a typical message re-grows to its real size in a handful of reallocs. */
#define JC_PROV_SB_KEEP_TEXT (64u * 1024u)
#define JC_PROV_SB_KEEP_ARGS (8u * 1024u)

/* A-07 / M1090: ceilings on the RUNNING TOTAL a single stream accumulates
 * (the SSE field cap bounds one event's data, not the sum across the stream).
 * 4 MiB of assistant text mirrors JC_SSE_FIELD_MAX; 1 MiB of arguments per tool
 * call is generous for any legit single call (file contents travel through
 * read_file, not inline args). Defaults for the two runtime caps the test
 * lowers via jc_prov_set_max_text/args. */
#define JC_PROV_MAX_TEXT (4L * 1024L * 1024L)
#define JC_PROV_MAX_ARGS (1L * 1024L * 1024L)

/* One in-progress tool call, keyed by stream index. */
struct prov_call {
    int          used;             /* slot is an active tool call */
    int          flushed;          /* already appended to out      */
    char         id[JC_PROV_ID_LEN];
    struct jc_sb name;
    struct jc_sb args;
};

struct prov_state {
    char endpoint[512];            /* precomputed full URL */
    char cache_key[40];            /* stable per-session prompt_cache_key (M31c;
                                    * OpenAI cache routing); "" when unset */
    /* per-turn streaming scratch */
    struct jc_sb     text;         /* accumulated assistant text */
    int              text_started; /* any text emitted yet       */
    int              saw_reasoning;/* model emitted reasoning_content this turn */
    int              hit_length_cap;/* M334: response cut at the output ceiling */
    /* A-07 / M1090: set by jc_prov_emit_text / jc_prov_append_args the first
     * time a per-stream cap actually clips a byte. Carried to out->truncated by
     * jc_prov_flush, alongside hit_length_cap -- different causes, separate flags. */
    int              truncated_reply;
    struct prov_call calls[JC_PROV_MAX_CALLS];
    int              done;
    double           usage_in;     /* input tokens for this stream  */
    double           usage_out;    /* output tokens for this stream */
    double           cache_read_in;  /* input tokens served from cache (M31a) */
    double           cache_write_in; /* input tokens written to cache (Anthropic) */
    char             stream_err[512]; /* M909: the server's in-stream error, or "" */
};

void jc_prov_state_init(struct prov_state *s);
void jc_prov_state_free(struct prov_state *s);
void jc_prov_state_reset(struct prov_state *s); /* per-turn scratch only */

/* Ensure slot `idx` exists; returns it or NULL if out of range. */
struct prov_call *jc_prov_call_slot(struct prov_state *s, int idx);

/* Stream a text delta: append to scratch (bounded by the M1090/A-07 text cap)
 * and notify the sink. Past-cap bytes are dropped and truncated_reply is set. */
void jc_prov_emit_text(struct prov_state *s, struct jc_stream_sink *sink,
                       const char *delta, jc_size n);

/* A-07 / M1090: append NUL-terminated tool-call arguments to `slot`, bounded
 * by the per-call args cap; sets s->truncated_reply when a fragment is
 * clipped. Shared by all four dialect append sites (openai stream + full,
 * anthropic stream + full). */
void jc_prov_append_args(struct prov_state *s, struct prov_call *slot,
                         const char *args);

/* A-07 / M1090: the two per-stream caps are runtime-adjustable so the test can
 * lower them to a few bytes without a 4 MiB fixture. Defaults are
 * JC_PROV_MAX_TEXT / JC_PROV_MAX_ARGS; getters report the live values, setters
 * override, resetters restore the defaults. */
size_t jc_prov_get_max_text(void);
size_t jc_prov_get_max_args(void);
void jc_prov_set_max_text(size_t n);
void jc_prov_set_max_args(size_t n);
void jc_prov_reset_max_text(void);
void jc_prov_reset_max_args(void);

/* Flush accumulated text + tool calls into `out`. Called on stream done. */
void jc_prov_flush(struct prov_state *s, struct jc_message *out);

/* Shared get_usage vtable implementation (reads self->state). */
void jc_prov_get_usage(struct jc_provider *self, double *in_tok,
                       double *out_tok);

/* Shared stream_started vtable implementation (M897): text, reasoning or a
 * tool call seen in the current stream. */
int jc_prov_stream_started(struct jc_provider *self);

/* M909: the shared stream_error vtable implementation, and the setter both
 * dialects' error branches call: it keeps the server's message (cut to fit,
 * on a UTF-8 boundary) and emits nothing as answer text. */
const char *jc_prov_stream_error(struct jc_provider *self);
void jc_prov_set_stream_error(struct prov_state *s, const char *msg);

/* Shared get_cache_usage vtable implementation (reads self->state). */
void jc_prov_get_cache_usage(struct jc_provider *self, double *read_in,
                             double *write_in);

/* Build the neutral message/tools serialisation shared shapes. */
/* (Each provider has its own message mapping; tools mapping helpers live in
 *  the provider files since the key names differ.) */

/* True for the in-flight assistant placeholder: the empty assistant message
 * `run_agent_loop` appends to stream into, before any byte has arrived. It
 * carries no content and no tool calls, so serialising it puts a content-free
 * assistant turn at the very end of the request. Remote models shrug that off;
 * a small local model reads it as "the assistant turn already happened" and
 * closes the turn with a single end-of-turn token, never calling a tool. Pure;
 * unit-tested in tests/test_provider.c. See docs/ANECDOTES.md #19. */
int jc_prov_msg_is_placeholder(const struct jc_message *m);

/* Print `root` as the request body, replacing any ill-formed UTF-8 byte with
 * U+FFFD (M191). Sets *body_out to a malloc'd string (caller owns) and returns
 * JC_OK, or JC_ERR_OOM. Does NOT delete `root`.
 *
 * The wire-side backstop for the guarantee jc_history_add makes on the ingest
 * side: the system prompt (whose instruction-file and repo-map sections are
 * byte-capped) and tool-call argument JSON never pass through a message, so this
 * is the last point where every byte of a request is visible at once. A strict
 * server rejects the entire request over one split character -- and since the
 * offending byte persists in the history, every subsequent turn as well, which
 * is a wedged run rather than a glitch. See docs/ANECDOTES.md #22. */
jc_status jc_prov_print_body(cJSON *root, char **body_out);

/* Return a WIRE-SAFE form of a tool call's `arguments` string: malloc'd, caller
 * owns, never NULL for a non-NULL input (falls back to "{}").
 *
 * OpenAI-style `arguments` is a STRING whose contents the server parses as JSON.
 * cJSON escapes it correctly, so an unparseable blob still yields a valid
 * request document -- and a lenient server accepts it -- but a strict one
 * (litellm in front of vLLM) rejects the whole request with HTTP 400
 * "Unterminated string", which is NOT transient, so the retry ladder cannot help
 * and the turn dies. The malformed text also persists in the history, so every
 * later turn dies the same way: a wedged run, exactly the shape of
 * docs/ANECDOTES.md #22.
 *
 * When the text does not parse, wrap it as {"_unparsed_arguments": "<raw>"} --
 * the same decision the Anthropic provider's args_to_object made at M145, for
 * the same reason: silently substituting {} would show the model a clean empty
 * input right beside a tool_result saying its arguments failed to parse,
 * contradicting the evidence it needs to self-correct. Valid AND truthful.
 * Pure; unit-tested in tests/test_provider.c. (M269) */
char *jc_prov_args_wire(const char *args_json);

#endif /* JC_PROV_INTERNAL_H */
