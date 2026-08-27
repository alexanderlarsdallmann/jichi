/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_message.h - the in-memory chat data model.
 *
 * This is the provider-independent representation the agent loop manipulates
 * and that providers serialise to / parse from their own wire formats.
 *
 * Ownership: messages and their fields are heap-allocated (jc_strdup /
 * malloc) and owned by the jc_history that holds them. jc_history_free
 * releases everything transitively.
 */
#ifndef JC_MESSAGE_H
#define JC_MESSAGE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

typedef enum {
    JC_ROLE_SYSTEM,
    JC_ROLE_USER,
    JC_ROLE_ASSISTANT,
    JC_ROLE_TOOL
} jc_role;

/* A single tool call requested by the assistant. */
struct jc_tool_call {
    char *id;             /* provider-assigned id (may be synthesised)     */
    char *name;           /* tool name                                     */
    char *arguments_json; /* arguments as a JSON object string             */
};

/* An image attachment on a user message (M29). `data` is the base64-encoded
 * bytes; `media_type` is an IANA type like "image/png". */
struct jc_image {
    char *media_type;
    char *data;
};

struct jc_message {
    jc_role role;
    char   *content;        /* text content; may be NULL                   */
    struct jc_vec tool_calls; /* of struct jc_tool_call (assistant only)   */
    struct jc_vec images;   /* of struct jc_image (user only; M29)         */
    char   *tool_call_id;   /* for JC_ROLE_TOOL: the call being answered   */
    int     is_error;       /* for JC_ROLE_TOOL: result was an error       */
    int     truncated;      /* M334: the provider stopped this assistant
                             * message at the model's output-token ceiling
                             * (OpenAI finish_reason "length", Anthropic
                             * stop_reason "max_tokens"). A tool call in a
                             * truncated message is CUT OFF, not malformed. */
};

struct jc_history {
    struct jc_vec messages; /* of struct jc_message */
    unsigned long gen;      /* M218: bumped on every structural mutation
                             * (add / add_tool_result / drop_front / truncate)
                             * and by the mid-turn elisions' content edits, so
                             * jc_session_save can skip a byte-identical
                             * rewrite. Content set outside those paths always
                             * co-occurs with an append in the same turn. */
};

void jc_history_init(struct jc_history *h);
void jc_history_free(struct jc_history *h);
jc_size jc_history_len(const struct jc_history *h);
struct jc_message *jc_history_get(struct jc_history *h, jc_size i);

/* Append a new message of `role`; returns it for further population.
 * `content` is copied (may be NULL). */
struct jc_message *jc_history_add(struct jc_history *h, jc_role role,
                                  const char *content);

/* Append mid-run steering as ONE user-role message carrying the "[operator] "
 * provenance prefix -- the single convention for text that arrives DURING a
 * turn rather than as a fresh user turn, shared by the control channel (M159)
 * and the TUI type-ahead queue (M254). One message, appended at a tool-call
 * boundary, so the cached request prefix (M31) stays byte-stable. Returns the
 * message, or NULL. */
struct jc_message *jc_history_add_operator(struct jc_history *h,
                                           const char *text);

/* Convenience: append a tool result message. `content` and `tool_call_id`
 * are copied. */
struct jc_message *jc_history_add_tool_result(struct jc_history *h,
                                              const char *tool_call_id,
                                              const char *content,
                                              int is_error);

/* Append a tool call to an (assistant) message. All strings are copied. */
jc_status jc_msg_add_tool_call(struct jc_message *m, const char *id,
                               const char *name, const char *arguments_json);

jc_size jc_msg_tool_call_count(const struct jc_message *m);
struct jc_tool_call *jc_msg_tool_call_at(struct jc_message *m, jc_size i);

/* Attach an image (M29). `media_type` and `data` (base64) are copied. */
jc_status jc_msg_add_image(struct jc_message *m, const char *media_type,
                           const char *data);

/* Attach an image, taking ownership of `data` (a malloc'd base64 string freed
 * with the message) instead of copying it -- avoids a second copy of a
 * potentially multi-MB payload. `media_type` is still copied. On success
 * ownership of `data` transfers to the message; on JC_ERR_OOM it stays with the
 * caller (who must free it). */
jc_status jc_msg_add_image_owned(struct jc_message *m, const char *media_type,
                                 char *data);
jc_size jc_msg_image_count(const struct jc_message *m);
struct jc_image *jc_msg_image_at(struct jc_message *m, jc_size i);

/* Set/replace a message's content (copies). */
jc_status jc_msg_set_content(struct jc_message *m, const char *content);

/* Set/replace a tool call's arguments (copies; sanitized like content).
 * On JC_ERR_OOM the old value is kept. Used by the M218 mid-turn argument
 * elision; callers must keep `args_json` a valid JSON object -- the Anthropic
 * serializer re-parses it. */
jc_status jc_msg_tool_call_set_args(struct jc_tool_call *tc,
                                    const char *args_json);

/* Drop the first `count` messages, freeing them, and shift the rest down.
 * `count` is clamped to the history length. Used by auto-compaction to discard
 * the summarized prefix. */
void jc_history_drop_front(struct jc_history *h, jc_size count);

/* Drop every message from index `len` to the end, freeing them, so the history
 * length becomes `len` (a no-op if `len >= length`). Used to discard an
 * incomplete trailing turn before retrying it (e.g. stall escalation). */
void jc_history_truncate(struct jc_history *h, jc_size len);

/* M364: the wire-shape validator. The contract both provider serializers
 * rely on -- every tool call answered exactly once by the tool-result run
 * immediately following it, every result claiming a call from its own round,
 * the first non-system message a user turn, no empty user messages -- was
 * folklore spread across the ~69 history-mutation sites; this is the checker
 * that did not exist. Returns the violation count (0 = well-formed) and
 * appends up to 8 sample lines + "(+N more)" to `out` when given. Pure. */
struct jc_sb;
int jc_history_check(const struct jc_history *hist, struct jc_sb *out);

const char *jc_role_str(jc_role r);

#ifdef __cplusplus
}
#endif
#endif /* JC_MESSAGE_H */
