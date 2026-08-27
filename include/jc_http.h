/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_http.h - libcurl wrapper for HTTPS requests and SSE streaming.
 *
 * Two entry points: jc_http_perform collects a full response body; jc_http_stream
 * delivers the response body to a chunk callback as it arrives (used to drive
 * the SSE parser). Both are blocking (easy interface). The interactive TUI's
 * concurrency is layered on top in a later milestone.
 */
#ifndef JC_HTTP_H
#define JC_HTTP_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

struct jc_http_headers {
    struct jc_vec items; /* of char* "Key: Value", heap-owned */
};

void jc_http_headers_init(struct jc_http_headers *h);
void jc_http_headers_add(struct jc_http_headers *h, const char *line);
void jc_http_headers_free(struct jc_http_headers *h);

/* Returns 0 to continue, non-zero to abort the transfer. */
typedef int (*jc_http_chunk_cb)(const char *buf, jc_size n, void *user);

/* M22a built-in defaults (seconds) for a streamed model call. Generous enough
 * not to trip a slow-but-progressing local generation; made configurable
 * (global + per-model) in M22b. The low-speed floor is 1 byte/sec, so any
 * genuine progress keeps the stall timer from firing. */
#define JC_HTTP_STALL_LOW_SPEED         1L
#define JC_HTTP_CONNECT_TIMEOUT_DEFAULT 30L
#define JC_HTTP_STALL_TIMEOUT_DEFAULT   30L

struct jc_http_request {
    const char *method;  /* "GET" or "POST"; NULL => GET            */
    const char *url;
    struct jc_http_headers *headers;
    const char *body;    /* request body (POST); may be NULL        */
    jc_size     body_len;
    /* When set (POST only), the body is uploaded via a read-callback and
     * **freed as soon as it is fully sent** -- so a large request body is not
     * held in memory while the (often long) response streams back. This
     * TRANSFERS OWNERSHIP of `body` to jc_http: it frees `body` (at upload-EOF
     * or after the transfer); the caller must not free it. A streamed body
     * cannot be rewound, so a rare mid-send retry fails the attempt and the
     * caller re-sends. 0 (default) keeps the zero-copy CURLOPT_POSTFIELDS path
     * with the body caller-owned. */
    int         stream_body;
    long        timeout_secs; /* hard CURLOPT_TIMEOUT; 0 => none     */
    /* M22: connect + stall (low-speed) timeouts. The connect timeout fails
     * fast on an endpoint that won't accept the TCP connection; the stall
     * timeout aborts a transfer whose throughput stays at/below
     * JC_HTTP_STALL_LOW_SPEED bytes/sec for stall_timeout_secs seconds --
     * catching a frozen stream without capping a slow-but-progressing
     * generation. Either at 0 leaves that bound to the library default
     * (i.e. disabled), so callers that don't set them are unchanged. */
    long        connect_timeout_secs; /* CURLOPT_CONNECTTIMEOUT; 0 => default */
    long        stall_timeout_secs;   /* CURLOPT_LOW_SPEED_TIME;  0 => off    */
    volatile int *abort_flag; /* if non-NULL and set, abort         */
    /* Optional liveness tick: when set, called periodically (from libcurl's
     * progress callback) during the transfer -- including while waiting for the
     * first response byte -- so a front-end can animate a "working" spinner.
     * Throttle inside the callback; curl may call it several times a second. */
    void        (*on_progress)(void *user);
    void         *progress_user;
    /* M321: on failure, a short human diagnosis of the TRANSPORT error is
     * written here (NUL-terminated, truncated to err_out_cap). NULL => not
     * wanted, and every existing caller is unchanged.
     *
     * Why this exists: a 34,216-event telemetry log from a large unattended
     * workload showed 2,402 model calls (15%) failing with nothing recorded but
     * `status: 0`. Their latencies clustered inside 2 ms of exactly 10 s -- the
     * default connect timeout firing -- but the log could not say so, and the
     * operator had raised `timeouts.stall` (the knob they had heard of) while
     * the wall was `timeouts.connect`. Six and a half hours of wall clock went
     * into a knob they did not know existed. curl_easy_strerror alone would not
     * have helped: for a timeout it says only "Timeout was reached", which does
     * not distinguish connect from stall. See
     * docs/analysis/2026-08-06-large-workload-telemetry.md. */
    char       *err_out;
    jc_size     err_out_cap;
    /* If non-NULL, each received response header line ("Key: Value", trimmed
     * of CRLF) is appended to this (already-initialised) header set. Used by
     * the MCP http transport to read back Mcp-Session-Id. */
    struct jc_http_headers *resp_headers;
    /* SSRF hardening (M131). When set, the transfer restricts protocols to
     * http/https, caps redirects, and refuses to connect to any loopback /
     * link-local / private / reserved IP -- for EVERY connection including
     * those reached via DNS resolution or a redirect, defeating DNS-rebinding
     * and redirect-to-internal bypasses. Set by fetch_url on model-supplied
     * URLs; left 0 for operator-configured endpoints (providers, MCP). */
    int         block_private_addrs;
    /* Follow 3xx redirects? DEFAULT 0 -- and that default is the security
     * property, so think before setting it (M472).
     *
     * A redirect moves a request to a host the user did not choose, and every
     * header goes with it -- including whatever authenticates the request.
     * libcurl removes `Authorization` on a cross-host hop, but it cannot know
     * that `x-api-key` (Anthropic, and so this project's own primary provider)
     * or an operator's `requestOptions.headers` are credentials too, so those
     * travel. Measured before this field existed: a bare `302` from the provider
     * endpoint resent `x-api-key: <the key>` verbatim to a different host.
     *
     * An API endpoint has no legitimate reason to 3xx, so the fix is not to
     * launder the headers per hop -- it is not to go. Set this only where
     * following a redirect IS the feature and no credential is attached, i.e.
     * `fetch_url`. A 3xx on a non-following request is reported with a
     * diagnostic naming the target rather than being silently obeyed.
     *
     * See docs/analysis/2026-08-17-source-hardening-audit.md §H1. */
    int         follow_redirects;
    /* Hard cap (bytes) on a collected (non-streaming) response body; 0 => no
     * cap. Aborts the transfer once exceeded, so a hostile endpoint can't drive
     * the process to OOM before the caller's post-hoc truncation runs. */
    long        max_bytes;
};

/* --- SSRF address classification (pure, unit-tested; M131) ----------------- */

/* 1 if the dotted-quad octets name a loopback / link-local (incl. the
 * 169.254.169.254 cloud-metadata address) / private / reserved IPv4 host that
 * a model-supplied URL must not reach. */
int jc_net_ipv4_blocked(unsigned int a, unsigned int b,
                        unsigned int c, unsigned int d);

/* 1 if the 16 raw bytes name a blocked IPv6 host (::1, ::, fe80::/10 link-local,
 * fc00::/7 ULA, or a ::ffff: v4-mapped address whose v4 is blocked). */
int jc_net_ipv6_blocked(const unsigned char *b16);

/* 1 if `host` is a loopback name (localhost, *.localhost) or a literal IP in a
 * blocked range. A plain DNS name resolving to a private address returns 0 here
 * (the connect-time check catches that); this is the fast, clear-error path. */
int jc_net_host_is_blocked(const char *host);

/* Extract the host (no scheme, no userinfo, no port, IPv6 brackets stripped)
 * from `url` into `buf`. JC_ERR_INVALID if it can't be parsed. */
jc_status jc_url_host(const char *url, char *buf, jc_size cap);

/* Map a finished transfer's outcome to a status code (pure; M22c). Precedence:
 * an abort wins; then a timeout *after a connection was established* is a stall
 * (JC_ERR_TIMEOUT => "the model accepted us then froze / was too slow"); any
 * other failure -- including a connect-phase timeout, which is a network/
 * unreachable condition -- is JC_ERR_HTTP. `ok` short-circuits to JC_OK. */
jc_status jc_http_classify(int ok, int aborted, int timed_out, int connected);

/* May the pooled connection be reused after this transfer? (M326v)
 *
 * A failure BEFORE a connection existed cannot have poisoned one, and keeping
 * the handle preserves its DNS + TLS-session caches -- which makes the retry
 * cheaper in exactly the case that needs it. Dropping it instead forced the
 * next attempt to open a cold connection, i.e. to repeat the very operation
 * that had just timed out; a real workload showed 2,437 connect timeouts each
 * throwing away a warm connection. A failure AFTER connecting still drops:
 * that socket may be sitting mid-stream. Pure; int-only so no-curl builds can
 * declare it, like jc_http_classify above. */
int jc_http_conn_reusable(int transfer_ok, int connected);

/* Describe a failed transfer for a human, into `buf` (always NUL-terminated when
 * cap > 0). `curl_err` is curl_easy_strerror's text (may be NULL), `timed_out`
 * and `connected` are as for jc_http_classify, and `connect_timeout_secs` is the
 * limit that was in force (0 = library default).
 *
 * The point is to NAME THE KNOB. A timeout that fired before the connection was
 * established is a `timeouts.connect` problem; one after it is `timeouts.stall`.
 * curl cannot tell them apart in its message and jichi can, because it already
 * asks CURLINFO_CONNECT_TIME. Pure; unit-tested. */
void jc_http_describe_failure(const char *curl_err, int timed_out, int connected,
                              long connect_timeout_secs, char *buf, jc_size cap);

/* Explain a 3xx that was deliberately not followed (M472; see follow_redirects
 * above). A bare "HTTP 302" tells the operator nothing, and their first instinct
 * -- make the client follow it -- is the one move that leaks the key, so the
 * message names the target and says why jichi stopped. `location` may be NULL.
 * Pure; unit-tested; outside the curl guard so curl-free builds link. */
void jc_http_describe_redirect(long status, const char *location,
                               char *buf, jc_size cap);

jc_status jc_http_global_init(void);
void      jc_http_global_cleanup(void);

/* Non-streaming. *http_status receives the response code. *out receives a
 * malloc'd NUL-terminated body (caller frees); *out_len its length. */
jc_status jc_http_perform(const struct jc_http_request *req,
                          long *http_status, char **out, jc_size *out_len);

/* Streaming. Invokes `cb` for each received chunk. */
jc_status jc_http_stream(const struct jc_http_request *req,
                         long *http_status,
                         jc_http_chunk_cb cb, void *user);

#ifdef __cplusplus
}
#endif
#endif /* JC_HTTP_H */
