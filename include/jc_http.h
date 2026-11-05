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
 * (global + per-model) in M22b.
 *
 * The "genuine progress" assumption was measured wrong: the low-speed floor
 * (1 byte/sec) does reset the timer while the RESPONSE streams, but it cannot
 * help while the server PREFILLS -- the model is working at ~200 tokens/sec,
 * yet zero bytes have arrived on the socket. A ~12k-token prefill on a
 * mid-size local model takes ~36 s, so the 30 s default cuts 4 of 4 first
 * attempts (2026-10-15, the boards' LM Studio logs). `timeouts.stall` is a
 * CAP on silence, not a fence on a dead stream: off (0) for measurement
 * runs; `timeouts.connect` and `timeouts.deadStream` are the fences. */
#define JC_HTTP_STALL_LOW_SPEED         1L
#define JC_HTTP_CONNECT_TIMEOUT_DEFAULT 30L
#define JC_HTTP_STALL_TIMEOUT_DEFAULT   30L

/* The default ceiling (bytes) for a collected (non-streaming) response when a
 * caller passes no max_bytes (0). Grounded as the proven fetch_url OOM-backstop
 * value (32 MiB, src/tools/jc_tool_fetch.c) -- the largest class of model-
 * supplied collected response, bounded for the 256 MB board. This is a
 * CEILING, not a truncation cap: it ABORTS the transfer once exceeded (so a
 * hostile endpoint cannot drive the process to OOM before a caller's post-hoc
 * check runs) rather than silently truncating a body the caller would then
 * misread. Set on every collected jc_http_perform call that does not name its
 * own cap (C-06): embeddings, reachable, model lists, image gen, transcription,
 * web search, MCP-over-HTTP, compaction, one-shot and audio all grow without
 * bound otherwise. The streaming model path (write_stream) is unaffected. */
#define JC_HTTP_DEFAULT_RESP_BYTES (32L * 1024 * 1024)

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
     * JC_HTTP_STALL_LOW_SPEED bytes/sec for stall_timeout_secs seconds.
     * It CAPS SILENCE, so a healthy but slow prefill -- which sends zero
     * bytes until the first token (M959, the boards' LM Studio logs measured
     * a 12k-token prefill cut at 30 s) -- is indistinguishable from a frozen
     * stream and gets cut too; `deadStream` (below) is the fence that bounds
     * a truly dead stream. Either at 0 leaves that bound to the library
     * default (i.e. disabled), so callers that don't set them are unchanged. */
    long        connect_timeout_secs; /* CURLOPT_CONNECTTIMEOUT; 0 => default */
    long        stall_timeout_secs;   /* CURLOPT_LOW_SPEED_TIME;  0 => off    */
    /* M-L2: the dead-stream FENCE, seconds. curl has ONE low-speed window per
     * handle, so this and stall share it: the effective LOW_SPEED_TIME is
     * min(stall, dead) when stall is on, else dead. Because the floor is 1 B/s,
     * the window only fires when nothing is arriving -- so this stays ON even at
     * `stall 0` (a fence), bounding a call whose stream died. 0 => no fence. */
    long        dead_stream_secs;
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
    /* SSRF hardening of the REDIRECT hops only (C-08, M1120). The FIRST IP
     * connection of the transfer goes to a URL a human named: a legitimate
     * internal feed or documentation URL is allowed even if private
     * (HARDENING.md §6b's asymmetry), so that hop passes any address.
     * Every LATER connection (a redirect target) is server-chosen and must
     * not reach a private / link-local / reserved address. A request sets ONE
     * of the two independent per-hop policies, not both: block_private_addrs
     * refuses every hop including the first; this one allows the first hop and
     * refuses the redirect hops. No caller sets both (fetch_url and the image
     * reply-URL set block_private_addrs; the @rss: feed fetch and the docs URL
     * fetch set this one). */
    int         block_redirect_private;
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
    /* Hard cap (bytes) on a collected (non-streaming) response body; 0 => the
     * caller passes no cap and jc_http_perform applies JC_HTTP_DEFAULT_RESP_BYTES
     * (C-06) so the transfer is always bounded. Aborts the transfer once
     * exceeded (overflow, below), so a hostile endpoint can't drive the
     * process to OOM before the caller's post-hoc truncation runs. */
    long        max_bytes;
    /* C-06: set to 1 by jc_http_perform when the collect ceiling (max_bytes, or
     * JC_HTTP_DEFAULT_RESP_BYTES when max_bytes is 0) aborted the transfer; 0
     * otherwise (and left 0 on any other failure). A caller that reads it can
     * tell "the response was too big and the transfer was cut" apart from a
     * transport failure, and report the cap rather than a generic error. */
    int         overflow;
    /* M1039: on failure, a stable token naming the failure class so an
     * operator can grep outage logs. NULL => not wanted. The classifier
     * derives it from the same structured inputs that decide jc_http_classify
     * and jc_http_describe_failure, so the prose and the class never disagree.
     * Vocabulary: connect, timeout-connect, timeout-tls, timeout-stall, dns,
     * refused, http-status, provider-error, aborted. */
    char *fail_class;
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
 * (the connect-time check catches that); this is the fast, clear-error path.
 * On a build without sockets (JC_NO_SOCKETS, M835) an IPv4 literal is read by
 * jc_net_parse_ipv4 and any IPv6 literal is refused: there is no inet_pton to read
 * it, and such a build cannot connect anywhere, so refusing costs nothing. */
int jc_net_host_is_blocked(const char *host);

/* 1 if `host` is loopback -- the loopback names (localhost, localhost.
 * localdomain, *.localhost), the string "::1", or a 127.0.0.0/8 IPv4 literal.
 * 0 for everything else, INCLUDING the private ranges: this is the NARROW
 * exemption the doctor's plaintext-transport check (M472, fixed M1132/C-14)
 * needs, as opposed to jc_net_host_is_blocked above, which answers "loopback
 * OR private" and is the SSRF guard (it keeps blocking private). A NULL or
 * empty host returns 1 (it never reaches the network, so it is not a remote
 * cleartext target). Pure and always compiled: the 127/8 test uses
 * jc_net_parse_ipv4 and "::1" is matched as a string, so no inet_pton. */
int jc_net_host_is_loopback(const char *host);

/* Parse a dotted-quad IPv4 literal exactly as inet_pton(AF_INET) does -- four
 * decimal octets 0..255, no leading zeros, nothing before or after -- into
 * `out[4]`. Returns 1 on success, 0 otherwise. Pure and always compiled: the
 * SSRF classifier uses it where there is no socket library (M835), and the unit
 * suite holds it equal to inet_pton over a corpus on every row that has both. */
int jc_net_parse_ipv4(const char *s, unsigned char out[4]);

/* Extract the host (no scheme, no userinfo, no port, IPv6 brackets stripped)
 * from `url` into `buf`. JC_ERR_INVALID if it can't be parsed. */
jc_status jc_url_host(const char *url, char *buf, jc_size cap);

/* Map a finished transfer's outcome to a status code (pure; M22c). Precedence:
 * an abort wins; then a timeout *after a connection was established* is a stall
 * (JC_ERR_TIMEOUT => "the model accepted us then froze / was too slow"); any
 * other failure -- including a connect-phase timeout, which is a network/
 * unreachable condition -- is JC_ERR_HTTP. `ok` short-circuits to JC_OK. */
jc_status jc_http_classify(int ok, int aborted, int timed_out, int connected);

/* M1039: classify a failed transfer into a stable token for telemetry.
 * Returns one of: "connect", "timeout-connect", "timeout-tls", "timeout-stall",
 * "dns", "refused", "http-status", "provider-error", "aborted".
 * `curl_err` is the libcurl CURLcode (may be 0), `http_status` the response
 * code (0 if not received), and the remaining booleans are from the transfer.
 * The classifier is pure and must not call anything from curl. */
char *jc_http_fail_class(int curl_err, int http_status, int aborted,
                         int timed_out, int connected, int handshaking,
                         int fence_only);

/* M854 (N24c): 1 when an error response body says the request exceeded the
 * model's context window -- case-insensitively "ContextWindowExceeded" (LiteLLM's
 * error class: the demos drive's 400s, measured), "context_length_exceeded" or
 * "maximum context length" (the OpenAI-compatible spellings; not measured here).
 * `body` need not be NUL-terminated; NULL is 0. Pure; unit-tested. The caller
 * still checks the status: a body alone proves nothing. */
int jc_http_body_ctx_overflow(const char *body, jc_size len);

/* May the pooled connection be reused after this transfer? (M326v, M1062)
 *
 * A failure BEFORE a connection existed cannot have poisoned one, and keeping
 * the handle preserves its DNS + TLS-session caches -- which makes the retry
 * cheaper in exactly the case that needs it. Dropping it instead forced the
 * next attempt to open a cold connection, i.e. to repeat the very operation
 * that had just timed out; a real workload showed 2,437 connect timeouts each
 * throwing away a warm connection. A failure AFTER connecting still drops:
 * that socket may be sitting mid-stream.
 *
 * M1062: `connected` alone is not enough. On a REUSED (keep-alive) connection
 * libcurl leaves CURLINFO_CONNECT_TIME at 0 (the M880 note), so a mid-transfer
 * death on a pooled socket reads `connected == 0` and -- with two args -- would
 * be kept, letting the retry ride a dead socket. `reused` is that third answer
 * (a timeout on the persistent handle with connect_time == 0 came from the
 * pool): an established connection, reused OR fresh, that died mid-transfer is
 * dropped. Semantics: transfer_ok -> keep; !connected && !reused -> keep;
 * connected || reused after a failed transfer -> drop. Pure; int-only so
 * no-curl builds can declare it, like jc_http_classify above. */
int jc_http_conn_reusable(int transfer_ok, int connected, int reused);

/* M877 (the demos drive's N47 (d)): is a failed `https` transfer still in its TLS
 * handshake -- TCP connected (`connect_time` > 0) and the handshake not finished
 * (`appconnect_time` == 0, CURLINFO_APPCONNECT_TIME)? libcurl counts the handshake
 * against CURLOPT_CONNECTTIMEOUT, so a slow one ends the call inside the connect
 * phase with the request never sent -- measured on 2026-10-02: TCP up in 0.002 s,
 * the handshake 20.8 s, a 20 s bound. jichi used to read "TCP connected" as
 * "connected", and said the stream had stalled. An `http` URL has no handshake,
 * and is never in one. M880: `tcp_connected` is jc_sockwatch_tcp_up's answer, not
 * CURLINFO_CONNECT_TIME, which libcurl 8.14 and later leave at 0 until the whole
 * connection, TLS included, is up. Pure; no-curl builds declare it. */
int jc_http_in_handshake(const char *url, int tcp_connected,
                         double appconnect_time);

/* M880: which sockets a transfer opened, and whether one of them had a peer -- a
 * TCP connection -- when libcurl closed it. On a TLS handshake that times out,
 * libcurl 8.5.0 reports CURLINFO_CONNECT_TIME > 0 and 8.14.1 and 8.18.0 report 0,
 * exactly as for a TCP connect that never happened; in all three, the transfer's
 * socket still had its peer when libcurl closed it, before curl_easy_perform
 * returned (measured 2026-10-02 with getpeername from CURLOPT_CLOSESOCKETFUNCTION).
 * Pure bookkeeping: the caller asks the socket and passes `has_peer`. */
#define JC_SOCKWATCH_MAX 8
struct jc_sockwatch {
    int fds[JC_SOCKWATCH_MAX];
    int nfds;
    int closed;      /* this transfer's sockets libcurl closed */
    int connected;   /* ... of which had a peer when it did */
};
void jc_sockwatch_opened(struct jc_sockwatch *w, int fd);
void jc_sockwatch_closing(struct jc_sockwatch *w, int fd, int has_peer);
/* Was TCP connected? The watch decides once it saw this transfer's sockets close;
 * otherwise -- a reused connection, or a libcurl before 7.21.7 without the close
 * hook -- CURLINFO_CONNECT_TIME does, as before M880. */
int jc_sockwatch_tcp_up(const struct jc_sockwatch *w, double connect_time);

/* M1062-c corrected: was this a REUSED transfer -- one that inherited its
 * socket from the pool? The empty per-transfer watch (w->nfds == 0 &&
 * w->closed == 0: the transfer opened no socket, and jc_sockwatch_closing
 * counts only sockets THIS transfer opened, so even libcurl closing the pooled
 * socket at the transfer's end leaves it empty) plus connect_time == 0 plus
 * `sent` is the discriminator. M1062 (the -b) used the first two; M1062-c
 * (SV:1141) adds the third because a COLD transfer whose DNS resolve times out
 * ALSO gives an empty watch and connect_time == 0 -- the -b's exact inputs --
 * and the -b read it as reused. The third input is the only one that
 * separates the two: `sent` is whether the request was actually uploaded
 * (CURLINFO_REQUEST_SIZE > 0, read at the failure site and passed in by the
 * caller): 0 on a resolve timeout (nothing was ever sent, so NOT reused -- it
 * is a kept, pre-TCP cold dial that reads timeout-connect on the connect bound)
 * and > 0 on a reused-then-dead transfer (the request WAS uploaded, so reused).
 * A cold dial that FAILED pre-TCP is a third case: it opened and libcurl
 * closed a socket, so its watch is non-empty -> not reused (M326v tooth)
 * regardless of `sent`. connect_time alone is insufficient, and `curl ==
 * g_reuse` is a tautology (there is one persistent handle, returned in every
 * path). w == NULL is a no-close-callback build: there is no per-transfer
 * watch data, so it cannot be asserted that nothing was opened -- return 0,
 * conservatively (such a build keeps the old classification). Pure; outside
 * the curl guard so no-curl builds and test_http.c link it. */
int jc_http_reused_conn(const struct jc_sockwatch *w, double connect_time, int sent);

/* C-15 (M1133): what the LINKED libcurl can actually do, in three facts, read
 * in ONE `curl_version_info(CURLVERSION_NOW)` call (the version string, the
 * ssl_version string and the library default CA path). The operator used to
 * see none of them until one of them cost: a TLS-free libcurl dies at the first
 * https call, an old libcurl behaves by surprise, a static binary may carry no
 * CA bundle at all. Each output is NUL-terminated ("" when cap is 0); a NULL
 * ssl_version / absent CA path is normalised to "(none)" for version and ssl
 * and to "" for cainfo. On a no-curl build every field is set to "" and the
 * call simply returns (no global state, callable from `--version` before any
 * init and from doctor). The default CA path is queried through a throwaway
 * handle + CURLINFO_CAINFO, available since libcurl 7.84.0 (0x075400) --
 * guarded, "" on older libraries. */
void jc_net_curl_snapshot(char *ver, jc_size vc, char *ssl, jc_size sc,
                          char *cainfo, jc_size cc);

/* C-15 (M1133): PURE, always-compiled (no curl types, so the curl-free unit
 * suite links them -- the M447 precedent). 1 iff `ssl` names a real TLS
 * backend: non-NULL, non-empty, not the "(none)" placeholder, and NOT
 * containing "no tls" case-insensitively -- a TLS-free libcurl reports its
 * ssl_version as "No TLS". Under JC_FAULT it first consults the no-TLS probe
 * (jc_fault_no_tls, env JICHI_FAULT_NO_TLS) and answers 0 when armed, so the
 * https-meets-no-TLS doctor failure arm is reachable on a TLS host; in a
 * release build the probe is a constant 0 and the environment is ignored. */
int jc_net_curl_has_tls(const char *ssl);

/* C-15 (M1133): the process-global CA bundle, set ONCE at startup from the
 * config's `caBundle` (NULL when the key is absent) and read by apply_common,
 * which then sets CURLOPT_CAINFO on every transfer. Storing a POINTER (never
 * a copy): the caller's string (on the session arena) outlives the process,
 * so the default CA and the configured bundle agree for every transfer, and
 * doctor's offline report matches the live transfers. The no-TLS probe is NOT
 * read here. */
const char *jc_net_curl_ca_bundle_set(const char *p);

/* C-15 (M1133): read the CA bundle set by jc_net_curl_ca_bundle_set (NULL when
 * unset). apply_common uses it to arm CURLOPT_CAINFO. */
const char *jc_net_curl_ca_bundle_get(void);

/* Describe a failed transfer for a human, into `buf` (always NUL-terminated when
 * cap > 0). `curl_err` is curl_easy_strerror's text (may be NULL), `timed_out`
 * and `connected` are as for jc_http_classify, and `connect_timeout_secs` is the
 * limit that was in force (0 = library default).
 *
 * The point is to NAME THE KNOB. A timeout that fired before the connection was
 * established is a `timeouts.connect` problem; one after it is `timeouts.stall` --
 * or, when the stall cap is off and only the M-L2 dead-stream fence was in force
 * (`fence_only`), `timeouts.deadStream`. curl cannot tell them apart in its message
 * and jichi can, because it already asks CURLINFO_CONNECT_TIME. Pure; unit-tested. */
void jc_http_describe_failure(const char *curl_err, int timed_out, int connected,
                              int handshaking,
                              long connect_timeout_secs, int fence_only,
                              char *buf, jc_size cap);

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
jc_status jc_http_perform(struct jc_http_request *req,
                          long *http_status, char **out, jc_size *out_len);

/* Streaming. Invokes `cb` for each received chunk. */
jc_status jc_http_stream(struct jc_http_request *req,
                         long *http_status,
                         jc_http_chunk_cb cb, void *user);

#ifdef __cplusplus
}
#endif
#endif /* JC_HTTP_H */
