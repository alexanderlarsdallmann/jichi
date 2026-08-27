/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_http.c - libcurl wrapper (see jc_http.h). */

#include "jc_http.h"
#include "jc_fault.h"
#include "jc_str.h"
#include "jc_log.h"
#include "jc_snprintf.h"
#include "jc_proc.h"   /* jc_fd_cloexec (M472) */

#include <stdlib.h>
#include <string.h>

/* The socket/inet headers are POSIX, not curl (M190): the pure SSRF
 * classifier below needs inet_pton in EVERY build. Only curl.h is
 * conditional. */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifdef JC_HAVE_CURL
#include <curl/curl.h>
#endif

/* --- SSRF address classification (pure; M131) ------------------------------ */

int jc_net_ipv4_blocked(unsigned int a, unsigned int b,
                        unsigned int c, unsigned int d)
{
    (void)d;
    if (a == 127) return 1;                       /* 127.0.0.0/8  loopback     */
    if (a == 10) return 1;                        /* 10.0.0.0/8   private      */
    if (a == 172 && b >= 16 && b <= 31) return 1; /* 172.16.0.0/12 private     */
    if (a == 192 && b == 168) return 1;           /* 192.168.0.0/16 private    */
    if (a == 169 && b == 254) return 1;           /* 169.254.0.0/16 link-local
                                                   * (incl. .169.254 metadata) */
    if (a == 0) return 1;                          /* 0.0.0.0/8   "this host"   */
    if (a == 100 && b >= 64 && b <= 127) return 1; /* 100.64.0.0/10 CGNAT       */
    if (a == 192 && b == 0 && c == 0) return 1;    /* 192.0.0.0/24  IETF        */
    if (a == 198 && (b == 18 || b == 19)) return 1;/* 198.18.0.0/15 benchmark   */
    if (a >= 224) return 1;                        /* multicast + reserved      */
    return 0;
}

int jc_net_ipv6_blocked(const unsigned char *b16)
{
    int i;
    int all_zero = 1;
    if (b16 == NULL) return 0;
    for (i = 0; i < 15; i++) {
        if (b16[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero && b16[15] == 1) return 1;        /* ::1 loopback              */
    all_zero = 1;
    for (i = 0; i < 16; i++) {
        if (b16[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) return 1;                         /* :: unspecified           */
    if (b16[0] == 0xfe && (b16[1] & 0xc0) == 0x80) return 1;  /* fe80::/10 link */
    if ((b16[0] & 0xfe) == 0xfc) return 1;          /* fc00::/7 unique-local    */
    /* ::ffff:a.b.c.d v4-mapped -> classify the embedded v4. */
    {
        int mapped = 1;
        for (i = 0; i < 10; i++) {
            if (b16[i] != 0) { mapped = 0; break; }
        }
        if (mapped && b16[10] == 0xff && b16[11] == 0xff) {
            return jc_net_ipv4_blocked(b16[12], b16[13], b16[14], b16[15]);
        }
    }
    return 0;
}

int jc_net_host_is_blocked(const char *host)
{
    jc_size n;
    unsigned char v4[4];
    unsigned char v6[16];
    if (host == NULL || host[0] == '\0') {
        return 1; /* no host: refuse */
    }
    if (strcmp(host, "localhost") == 0 ||
        strcmp(host, "localhost.localdomain") == 0 ||
        strcmp(host, "metadata.google.internal") == 0) {
        return 1;
    }
    n = (jc_size)strlen(host);
    if (n >= 10 && strcmp(host + n - 10, ".localhost") == 0) {
        return 1;
    }
    /* M190: this was guarded on JC_HAVE_CURL, but inet_pton is POSIX --
     * the guard silently dropped IP-literal SSRF classification from
     * no-curl builds (caught by the no-curl test run, not by eyes). */
    if (inet_pton(AF_INET, host, v4) == 1) {
        return jc_net_ipv4_blocked(v4[0], v4[1], v4[2], v4[3]);
    }
    if (inet_pton(AF_INET6, host, v6) == 1) {
        return jc_net_ipv6_blocked(v6);
    }
    return 0;
}

jc_status jc_url_host(const char *url, char *buf, jc_size cap)
{
    const char *p;
    const char *at;
    const char *end;
    const char *scan;
    jc_size len;
    if (url == NULL || buf == NULL || cap == 0) {
        return JC_ERR_INVALID;
    }
    p = strstr(url, "://");
    if (p == NULL) {
        return JC_ERR_INVALID;
    }
    p += 3;
    /* Authority ends at the first '/', '?' or '#'. */
    end = p;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') {
        end++;
    }
    /* Strip userinfo (everything up to and including the last '@'). */
    at = NULL;
    for (scan = p; scan < end; scan++) {
        if (*scan == '@') {
            at = scan;
        }
    }
    if (at != NULL) {
        p = at + 1;
    }
    /* IPv6 literal in brackets: take what's inside. */
    if (p < end && *p == '[') {
        const char *rb = p + 1;
        while (rb < end && *rb != ']') {
            rb++;
        }
        len = (jc_size)(rb - (p + 1));
        if (len == 0 || len >= cap) {
            return JC_ERR_INVALID;
        }
        memcpy(buf, p + 1, len);
        buf[len] = '\0';
        return JC_OK;
    }
    /* Otherwise host ends at ':' (port) or the authority end. */
    {
        const char *h = end;
        for (scan = p; scan < end; scan++) {
            if (*scan == ':') { h = scan; break; }
        }
        len = (jc_size)(h - p);
    }
    if (len == 0 || len >= cap) {
        return JC_ERR_INVALID;
    }
    memcpy(buf, p, len);
    buf[len] = '\0';
    return JC_OK;
}

void jc_http_headers_init(struct jc_http_headers *h)
{
    jc_vec_init(&h->items, sizeof(char *));
}

void jc_http_headers_add(struct jc_http_headers *h, const char *line)
{
    char *copy = jc_strdup(line);
    if (copy != NULL) {
        jc_vec_push(&h->items, &copy);
    }
}

void jc_http_headers_free(struct jc_http_headers *h)
{
    jc_size i;
    for (i = 0; i < h->items.len; i++) {
        free(*(char **)jc_vec_at(&h->items, i));
    }
    jc_vec_free(&h->items);
}

jc_status jc_http_classify(int ok, int aborted, int timed_out, int connected)
{
    if (ok) {
        return JC_OK;
    }
    if (aborted) {
        return JC_ERR_ABORTED;
    }
    /* A timeout *after* the connection was established means the server took us
     * but then stopped sending -- a stalled/too-slow model. A timeout during
     * connect (or any other failure) is a transport/network condition. */
    if (timed_out && connected) {
        return JC_ERR_TIMEOUT;
    }
    return JC_ERR_HTTP;
}

void jc_http_describe_failure(const char *curl_err, int timed_out, int connected,
                              long connect_timeout_secs, char *buf, jc_size cap)
{
    const char *msg;

    if (buf == NULL || cap == 0) {
        return;
    }
    msg = (curl_err != NULL && curl_err[0] != '\0') ? curl_err : "transfer failed";
    if (timed_out && !connected) {
        /* The case that cost an operator 6.5 hours: curl says only "Timeout was
         * reached", which reads like a slow model. It is not -- the connection
         * was never established, so no request was even sent. */
        if (connect_timeout_secs > 0) {
            jc_snprintf(buf, cap,
                        "could not connect within %lds (timeouts.connect) -- "
                        "the request was never sent; raise timeouts.connect if "
                        "the endpoint is shared or far away [%s]",
                        connect_timeout_secs, msg);
        } else {
            jc_snprintf(buf, cap,
                        "could not connect (timeouts.connect) -- the request was "
                        "never sent [%s]", msg);
        }
        return;
    }
    if (timed_out) {
        jc_snprintf(buf, cap,
                    "connected, then the stream stalled (timeouts.stall) [%s]",
                    msg);
        return;
    }
    jc_snprintf(buf, cap, "%s%s", msg,
                connected ? " (after connecting)" : " (before connecting)");
}

/* Explain a 3xx that was deliberately NOT followed (M472).
 *
 * Without this the caller sees a bare "HTTP 302" and no reason: the transfer
 * SUCCEEDED as far as curl is concerned, so none of the failure text above
 * applies. The message has to say three things -- that jichi chose not to
 * follow, where the endpoint wanted to send the request, and that the choice is
 * about the key -- because the operator's first instinct on "302" is to make
 * the client follow it, which is the one thing they must not do.
 *
 * Pure, like the two above, so tests/test_http.c can link it in a curl-free
 * build (the M447 lesson: a pure function has no business inside the curl
 * guard). `location` may be NULL -- curl only reports CURLINFO_REDIRECT_URL
 * when it parsed one. */
void jc_http_describe_redirect(long status, const char *location,
                               char *buf, jc_size cap)
{
    if (buf == NULL || cap == 0) {
        return;
    }
    if (location != NULL && location[0] != '\0') {
        jc_snprintf(buf, cap,
                    "endpoint answered HTTP %ld (redirect) to %s -- not followed. "
                    "An API endpoint should not redirect, and following one would "
                    "resend this request's credentials to that host. Point the "
                    "config at the final URL instead.",
                    status, location);
    } else {
        jc_snprintf(buf, cap,
                    "endpoint answered HTTP %ld (redirect) -- not followed, and no "
                    "usable Location was given. Point the config at the final URL.",
                    status);
    }
}

/* Was the failure BEFORE a connection existed? Then nothing can have been
 * poisoned, and keeping the handle preserves its DNS and TLS-session caches --
 * which makes the retry cheaper in exactly the case that needs it most (M326v).
 *
 * Measured on a real workload: 14% of model calls (2,437 of 17,365) died on the
 * 10-second connect timeout, and `rc == CURLE_OK` dropped the warm handle every
 * one of those times. That forced the NEXT attempt to open a cold connection --
 * repeating the operation that had just timed out. A self-reinforcing loop, and
 * the reason the connect timeout mattered at all: with a live handle, most calls
 * never enter the connect phase.
 *
 * A failure AFTER connecting is a different animal -- the socket may be sitting
 * mid-stream or mid-TLS-record -- so that one still drops, which is what the
 * original comment was right about. `CURLINFO_CONNECT_TIME` is the discriminator
 * and both call sites already read it to classify the error.
 *
 * It lives OUTSIDE the JC_HAVE_CURL guard deliberately (M447): it is pure --
 * two ints in, one int out, no curl type and no curl call -- and
 * `tests/test_http.c` links it unconditionally, so guarding it broke
 * `make HAVE_CURL= run_tests` with an undefined symbol. That is the curl-free
 * build M189 already had to repair once, and the recipe it breaks is Tier V's
 * row V0 (`docs/DEPLOYMENT.md` §3e). A pure predicate has no business being
 * behind a dependency's ifdef. */
int jc_http_conn_reusable(int transfer_ok, int connected)
{
    if (transfer_ok) {
        return 1;
    }
    return !connected;
}

#ifdef JC_HAVE_CURL

/* The reused easy handle (M227) and the pid that owns it -- see the
 * connection-keep-alive note above http_handle_acquire. */
static CURL *g_reuse = NULL;
static pid_t g_reuse_pid = 0;

jc_status jc_http_global_init(void)
{
    if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
        return JC_ERR_HTTP;
    }
    return JC_OK;
}

void jc_http_global_cleanup(void)
{
    /* Only this process's own handle -- never a fork-inherited one (a
     * child never reaches here via the normal exit path with the parent's
     * handle, but the pid guard makes it explicit and harmless). */
    if (g_reuse != NULL && g_reuse_pid == getpid()) {
        curl_easy_cleanup(g_reuse);
    }
    g_reuse = NULL;
    g_reuse_pid = 0;
    curl_global_cleanup();
}

/* --- shared transfer state ------------------------------------------- */

struct collect_ctx {
    struct jc_sb buf;
    int          oom;
    long         max_bytes; /* 0 => uncapped */
    int          overflow;
};

static size_t write_collect(char *ptr, size_t size, size_t nmemb, void *ud)
{
    struct collect_ctx *c = (struct collect_ctx *)ud;
    size_t total = size * nmemb;
    if (c->max_bytes > 0 &&
        (long)c->buf.len + (long)total > c->max_bytes) {
        c->overflow = 1;
        return 0; /* abort: refuse to buffer past the cap */
    }
    if (jc_sb_append_n(&c->buf, ptr, (jc_size)total) != JC_OK) {
        c->oom = 1;
        return 0; /* abort */
    }
    return total;
}

/* Connect-time SSRF guard: refuse a socket to any blocked address. curl calls
 * this for every connection -- including after DNS resolution and on redirects
 * -- so it defeats DNS-rebinding and redirect-to-internal. */
/* Mark every socket libcurl opens close-on-exec (M472).
 *
 * CURLOPT_SOCKOPTFUNCTION is the only hook that sees ALL of them:
 * OPENSOCKETFUNCTION is installed only for the SSRF-guarded request, and the
 * connection that matters most here is the provider's. It matters because the
 * handle is a persistent reuse handle (g_reuse above), so the socket stays open
 * BETWEEN turns -- which is correct for latency and means every child forked
 * after the first model call inherited it. Measured: fd 7 in a model-issued
 * shell, read/write, pointing at the provider connection.
 *
 * Note this file already reasons about the same socket crossing a fork (see the
 * pid-stamped cache above, which handles spawn_parallel's forked children). That
 * argument was never extended to children that EXEC. */
static int sockopt_cloexec(void *clientp, curl_socket_t curlfd,
                           curlsocktype purpose)
{
    (void)clientp;
    (void)purpose;
    jc_fd_cloexec((int)curlfd);
    return CURL_SOCKOPT_OK;
}

static curl_socket_t opensocket_guard(void *clientp, curlsocktype purpose,
                                      struct curl_sockaddr *addr)
{
    (void)clientp;
    if (addr == NULL) {
        return CURL_SOCKET_BAD;
    }
    if (purpose == CURLSOCKTYPE_IPCXN) {
        if (addr->family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)(void *)&addr->addr;
            const unsigned char *o = (const unsigned char *)&s->sin_addr.s_addr;
            if (jc_net_ipv4_blocked(o[0], o[1], o[2], o[3])) {
                return CURL_SOCKET_BAD;
            }
        } else if (addr->family == AF_INET6) {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)(void *)&addr->addr;
            if (jc_net_ipv6_blocked((const unsigned char *)&s->sin6_addr)) {
                return CURL_SOCKET_BAD;
            }
        }
    }
    return socket(addr->family, addr->socktype, addr->protocol);
}

/* Collect response header lines into a jc_http_headers set, stripped of the
 * trailing CRLF. The status line and the blank separator line are skipped. */
static size_t header_collect(char *ptr, size_t size, size_t nmemb, void *ud)
{
    struct jc_http_headers *h = (struct jc_http_headers *)ud;
    size_t total = size * nmemb;
    size_t n = total;
    char line[1024];

    while (n > 0 && (ptr[n - 1] == '\r' || ptr[n - 1] == '\n')) {
        n--;
    }
    if (n == 0 || n >= sizeof(line)) {
        return total; /* blank separator or oversized header: ignore */
    }
    /* Skip the "HTTP/x ..." status line (no colon before the first space). */
    if (n > 5 && strncmp(ptr, "HTTP/", 5) == 0) {
        return total;
    }
    memcpy(line, ptr, n);
    line[n] = '\0';
    jc_http_headers_add(h, line);
    return total;
}

struct stream_ctx {
    jc_http_chunk_cb cb;
    void            *user;
    int              aborted;
#ifdef JC_FAULT
    long             kill_after;  /* -1 = stream normally (M269) */
    long             delivered;   /* body bytes handed to the callback */
#endif
};

static size_t write_stream(char *ptr, size_t size, size_t nmemb, void *ud)
{
    struct stream_ctx *c = (struct stream_ctx *)ud;
    size_t total = size * nmemb;
#ifdef JC_FAULT
    /* M269: mid-stream connection death. Returning short (not 0-with-aborted)
     * makes curl fail the transfer with CURLE_WRITE_ERROR while `aborted` stays
     * clear, so the caller classifies it as a transport error on an ALREADY
     * CONNECTED transfer -- the failure mode M227's handle reuse introduced. */
    if (c->kill_after >= 0 && c->delivered >= c->kill_after) {
        return 0;
    }
    c->delivered += (long)total;
#endif
    if (c->cb(ptr, (jc_size)total, c->user) != 0) {
        c->aborted = 1;
        return 0; /* abort */
    }
    return total;
}

/* Streaming request-body upload (CURLOPT_READFUNCTION). When `own` is set, the
 * buffer is freed the moment it is fully uploaded, so a large request body is
 * not held in memory while the response streams back. */
struct body_reader {
    char   *data;
    jc_size len;
    jc_size off;
    int     own;
    int     freed;
};

static size_t body_read_cb(char *buf, size_t size, size_t nmemb, void *ud)
{
    struct body_reader *br = (struct body_reader *)ud;
    size_t want = size * nmemb;
    size_t avail = (size_t)(br->len - br->off);
    size_t n = (avail < want) ? avail : want;
    if (n > 0) {
        memcpy(buf, br->data + br->off, n);
        br->off += (jc_size)n;
    }
    /* Reclaim the body as soon as it is fully sent (before the response). */
    if (br->own && !br->freed && br->off >= br->len) {
        free(br->data);
        br->data = NULL;
        br->freed = 1;
    }
    return n;
}

/* A freed/streamed body cannot be rewound; refuse a seek so curl fails the
 * transfer cleanly (the caller re-sends) rather than reusing freed memory. */
static int body_seek_cb(void *ud, curl_off_t offset, int origin)
{
    (void)ud; (void)offset; (void)origin;
    return CURL_SEEKFUNC_CANTSEEK;
}

/* Progress-callback context: abort flag + an optional liveness tick. Lives on
 * the perform/stream caller's stack across the transfer. */
struct xfer_ctx {
    volatile int *abort_flag;
    void        (*on_progress)(void *user);
    void         *progress_user;
};

/* CURLOPT_XFERINFOFUNCTION arrived in libcurl 7.32.0 (2013). Older libcurl --
 * notably 7.29 on CentOS/RHEL 7, still in the field -- has only the deprecated
 * CURLOPT_PROGRESSFUNCTION, whose callback takes doubles instead of curl_off_t.
 * The callback ignores every one of those arguments (it exists to poll the
 * abort flag and tick the front-end), so the old API is a drop-in: same body,
 * different parameter types. Found by the M264 V2a row -- jichi did not build
 * at all against 7.29 before this. */
#if LIBCURL_VERSION_NUM >= 0x072000
#define JC_XFER_ARG curl_off_t
#define JC_XFER_OPT_FUNC CURLOPT_XFERINFOFUNCTION
#define JC_XFER_OPT_DATA CURLOPT_XFERINFODATA
#else
#define JC_XFER_ARG double
#define JC_XFER_OPT_FUNC CURLOPT_PROGRESSFUNCTION
#define JC_XFER_OPT_DATA CURLOPT_PROGRESSDATA
#endif

static int xferinfo_cb(void *ud, JC_XFER_ARG dltotal, JC_XFER_ARG dlnow,
                       JC_XFER_ARG ultotal, JC_XFER_ARG ulnow)
{
    struct xfer_ctx *xc = (struct xfer_ctx *)ud;
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    if (xc == NULL) {
        return 0;
    }
    if (xc->abort_flag != NULL && *xc->abort_flag) {
        return 1; /* non-zero aborts the transfer */
    }
    if (xc->on_progress != NULL) {
        xc->on_progress(xc->progress_user);
    }
    return 0;
}

/* Build a curl_slist from our header vector. Caller frees with
 * curl_slist_free_all. */
static struct curl_slist *build_slist(const struct jc_http_headers *h)
{
    struct curl_slist *list = NULL;
    jc_size i;
    /* Suppress Expect: 100-continue (M273). libcurl adds that header to any
     * request body over 1 KB and then WAITS for a "100 Continue" the server
     * may never send -- CURLOPT_EXPECT_100_TIMEOUT_MS, one full second by
     * default. Every model call past a short history is over that threshold,
     * so against a server that does not answer it (llama.cpp, LocalAI, a
     * simple proxy, jichi's own test mock) each call paid a second of dead
     * wait: measured on libcurl 7.52 as 201 calls in 200 s, versus 0.09 s per
     * call on 8.18 -- which does not send the header at all. Suppressing it
     * makes every libcurl jichi supports behave like the newest one, rather
     * than inventing a third behaviour. An empty header value is curl's
     * documented way to remove a header it would otherwise add, and it is
     * appended first so a caller could still override it. */
    list = curl_slist_append(list, "Expect:");
    if (h == NULL) {
        return list;
    }
    for (i = 0; i < h->items.len; i++) {
        const char *line = *(char **)jc_vec_at((struct jc_vec *)&h->items, i);
        list = curl_slist_append(list, line);
    }
    return list;
}

/* Connection keep-alive (M227): one cached easy handle per process so the TLS
 * connection to a model server is reused across the thousands of calls a long
 * session makes, instead of a fresh handshake each time. curl_easy_reset keeps
 * the handle's live-connection/DNS/session caches while clearing per-request
 * options, so acquire-reset-configure reuses the socket transparently.
 *
 * Fork safety (the whole reason this waited): a forked child (spawn_parallel)
 * inherits the parent's handle AND its open socket fd. Cleaning that handle in
 * the child would send TLS close-notify on the parent's connection; USING it
 * would interleave on the parent's fd. So the cache is pid-stamped: a process
 * whose pid does not match the stamp ABANDONS the inherited handle without
 * cleanup (the COW copy is reclaimed when the short-lived child exits) and
 * makes its own. The parent's handle is never touched post-fork. */

/* Return a handle ready for apply_common, reusing this process's cached one. */
static CURL *http_handle_acquire(void)
{
    pid_t me = getpid();
    if (g_reuse != NULL && g_reuse_pid != me) {
        g_reuse = NULL; /* inherited across fork: abandon, do NOT cleanup */
    }
    if (g_reuse == NULL) {
        g_reuse = curl_easy_init();
        g_reuse_pid = me;
    } else {
        curl_easy_reset(g_reuse); /* clears options, KEEPS the connection cache */
    }
    return g_reuse;
}

/* Done with `curl`. `reusable` keeps it cached for the next request's
 * connection reuse; a transfer error AFTER a connection was established may
 * have poisoned it, so the handle is dropped instead (the next request starts
 * clean). See http_conn_reusable for why a pre-connection failure does not. */
static void http_handle_release(CURL *curl, int reusable)
{
    if (curl == NULL) {
        return;
    }
    if (reusable && curl == g_reuse) {
        return; /* keep the warm connection */
    }
    if (curl == g_reuse) {
        g_reuse = NULL;
        g_reuse_pid = 0;
    }
    curl_easy_cleanup(curl);
}

/* A 3xx arrived on a request that does not follow redirects (M472). Record why
 * in req->err_out and log it, so "HTTP 302" is not the whole story the operator
 * gets. CURLINFO_REDIRECT_URL is populated even with FOLLOWLOCATION off -- it is
 * the URL curl WOULD have gone to -- which is exactly the fact worth reporting.
 *
 * Silent for a followed request (fetch_url), where a 3xx is the normal case and
 * curl has already resolved it, and for any non-redirect status. */
static void http_note_unfollowed_redirect(CURL *curl,
                                          const struct jc_http_request *req,
                                          long code)
{
    char *location = NULL;

    if (req->follow_redirects || code < 300 || code > 399) {
        return;
    }
    curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location);
    jc_http_describe_redirect(code, location, req->err_out, req->err_out_cap);
    if (req->err_out != NULL && req->err_out[0] != '\0') {
        jc_logf(JC_LOG_WARN, "http: %s", req->err_out);
    } else {
        /* No err_out buffer from this caller: say it anyway, in one line. */
        jc_logf(JC_LOG_WARN,
                "http: endpoint answered HTTP %ld (redirect) to %s -- not "
                "followed; following it would resend this request's credentials "
                "to that host",
                code, (location != NULL && location[0] != '\0') ? location
                                                                : "(no Location)");
    }
}

/* Common setup applied to both perform and stream. `br` (zeroed by the caller)
 * backs a streaming body upload when req->stream_body is set; it must outlive
 * curl_easy_perform. */
static void apply_common(CURL *curl, const struct jc_http_request *req,
                         struct curl_slist *hdrs, struct body_reader *br,
                         struct xfer_ctx *xc)
{
    const char *method = (req->method != NULL) ? req->method : "GET";

    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "jichi/0.1");

    /* Redirects are followed ONLY where a caller asked (M472): otherwise a 3xx
     * moves the request -- and the key attached to it -- to a host nobody chose.
     * See the field comment in jc_http.h for the measurement. */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
                     req->follow_redirects ? 1L : 0L);

    /* The protocol restriction and the hop cap apply to EVERY request, not just
     * the SSRF-guarded one (M472). They cost nothing where redirects are off,
     * and M131 had them only under block_private_addrs -- which is set by the
     * one caller that carries no credential, so every credentialed request was
     * following unlimited redirects into any protocol libcurl would accept. */
#if LIBCURL_VERSION_NUM >= 0x075500 /* 7.85.0: the _STR options */
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                     (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                     (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    /* Every socket, every request: the provider connection outlives a turn and
     * must not travel into a model-issued shell (M472). */
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_cloexec);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA, (void *)NULL);

    /* TLS floor and verification, stated in the source rather than inherited
     * (M472). libcurl's defaults are already correct -- peer and host
     * verification on, and a modern libcurl floors at TLS 1.2 -- so none of this
     * changes behaviour on this machine. It is here because H1 was a lesson about
     * relying on a libcurl default we neither request nor test: this project
     * probes for vsnprintf and malloc_trim rather than assuming them, and its
     * platform matrix reaches back to libcurl 7.19.4 (2009), where the floor is
     * TLS 1.0 and the SSLv3 fallback was still a thing.
     *
     * TLSv1_2 rather than TLSv1_3: 1.3 needs libcurl 7.52 with a TLS backend that
     * has it, and refusing to connect on an older row would be a portability
     * regression dressed as hardening. 1.2 is 2008 and is the floor every
     * supported backend can meet.
     *
     * VERIFYHOST is 2L, not 1L: 1 was "check the name exists" and has been a
     * synonym for 2 since 7.28.1, but naming 2 says the intent. Anyone who ever
     * needs to turn these off should have to delete a line that says why. */
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    if (req->block_private_addrs) {
        /* SSRF hardening (M131): a connect-time private-IP block that also
         * covers every redirect target and every DNS resolution. */
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, opensocket_guard);
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, (void *)NULL);
    }
    if (req->timeout_secs > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, req->timeout_secs);
    }
    if (req->connect_timeout_secs > 0) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                         req->connect_timeout_secs);
    }
    if (req->stall_timeout_secs > 0) {
        /* Abort if throughput stays at/below the floor for this many seconds:
         * catches a frozen stream without capping a slow, progressing one. */
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, JC_HTTP_STALL_LOW_SPEED);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, req->stall_timeout_secs);
    }
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
        if (req->stream_body && br != NULL && req->body != NULL &&
            req->body_len > 0) {
            /* Stream the body and free it the moment it is fully uploaded. */
            br->data = (char *)req->body;
            br->len = req->body_len;
            br->off = 0;
            br->own = 1;
            br->freed = 0;
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, body_read_cb);
            curl_easy_setopt(curl, CURLOPT_READDATA, br);
            curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, body_seek_cb);
            curl_easy_setopt(curl, CURLOPT_SEEKDATA, br);
        } else {
            /* Zero-copy: curl references our buffer (caller keeps ownership). */
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
        }
    }
    if ((req->abort_flag != NULL || req->on_progress != NULL) && xc != NULL) {
        xc->abort_flag = req->abort_flag;
        xc->on_progress = req->on_progress;
        xc->progress_user = req->progress_user;
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, JC_XFER_OPT_FUNC, xferinfo_cb);
        curl_easy_setopt(curl, JC_XFER_OPT_DATA, (void *)xc);
    }
}

jc_status jc_http_perform(const struct jc_http_request *req,
                          long *http_status, char **out, jc_size *out_len)
{
    CURL *curl;
    struct curl_slist *hdrs;
    struct collect_ctx ctx;
    struct body_reader br;
    struct xfer_ctx xc;
    CURLcode rc;
    jc_status st = JC_OK;
    double connect_time = 0.0;   /* >0 once a connection was established */

    curl = http_handle_acquire();
    if (curl == NULL) {
        if (req->stream_body && req->body != NULL) {
            free((char *)req->body); /* ownership was transferred to us */
        }
        return JC_ERR_HTTP;
    }
    jc_sb_init(&ctx.buf);
    ctx.oom = 0;
    ctx.max_bytes = req->max_bytes;
    ctx.overflow = 0;
    memset(&br, 0, sizeof(br));
    memset(&xc, 0, sizeof(xc));
    hdrs = build_slist(req->headers);

    apply_common(curl, req, hdrs, &br, &xc);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    if (req->resp_headers != NULL) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_collect);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, req->resp_headers);
    }

    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
        jc_http_describe_failure(curl_easy_strerror(rc),
                                 rc == CURLE_OPERATION_TIMEDOUT,
                                 connect_time > 0.0,
                                 req->connect_timeout_secs,
                                 req->err_out, req->err_out_cap);
        jc_logf(JC_LOG_ERROR, "http: %s",
                (req->err_out != NULL && req->err_out[0] != '\0')
                    ? req->err_out : curl_easy_strerror(rc));
        st = jc_http_classify(0, rc == CURLE_ABORTED_BY_CALLBACK,
                              rc == CURLE_OPERATION_TIMEDOUT,
                              connect_time > 0.0);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (http_status != NULL) {
            *http_status = code;
        }
        http_note_unfollowed_redirect(curl, req, code);
        if (out != NULL) {
            if (out_len != NULL) {
                *out_len = ctx.buf.len;
            }
            *out = jc_sb_finish(&ctx.buf);
        }
    }

    if (out == NULL || st != JC_OK) {
        jc_sb_free(&ctx.buf);
    }
    /* We own a streamed body; free it unless the upload already did at EOF. */
    if (req->stream_body && req->body != NULL && !br.freed) {
        free((char *)req->body);
    }
    curl_slist_free_all(hdrs);
    http_handle_release(curl,
                        jc_http_conn_reusable(rc == CURLE_OK,
                                              connect_time > 0.0));
    return st;
}

jc_status jc_http_stream(const struct jc_http_request *req,
                         long *http_status,
                         jc_http_chunk_cb cb, void *user)
{
    CURL *curl;
    struct curl_slist *hdrs;
    struct stream_ctx ctx;
    struct body_reader br;
    struct xfer_ctx xc;
    CURLcode rc;
    jc_status st = JC_OK;
    double connect_time = 0.0;   /* >0 once a connection was established */

    /* M219: deterministic transport-failure injection (FAULT=1 builds only)
     * -- BEFORE any bytes move, so it exercises exactly the retry ladder's
     * transient path. Must honor the M20e ownership transfer like every
     * early-error exit here. */
    if (JC_FAULT_HIT(JC_FAULT_NET)) {
        if (req->stream_body && req->body != NULL) {
            free((char *)req->body); /* ownership was transferred to us */
        }
        return JC_ERR_HTTP;
    }

    curl = http_handle_acquire();
    if (curl == NULL) {
        if (req->stream_body && req->body != NULL) {
            free((char *)req->body); /* ownership was transferred to us */
        }
        return JC_ERR_HTTP;
    }
    ctx.cb = cb;
    ctx.user = user;
    ctx.aborted = 0;
#ifdef JC_FAULT
    /* Exactly once per transfer: the accessor counts transfers. */
    ctx.kill_after = jc_fault_stream_kill_after();
    ctx.delivered = 0;
#endif
    memset(&br, 0, sizeof(br));
    memset(&xc, 0, sizeof(xc));
    hdrs = build_slist(req->headers);

    apply_common(curl, req, hdrs, &br, &xc);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_stream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    rc = curl_easy_perform(curl);
    {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (http_status != NULL) {
            *http_status = code;
        }
        if (rc == CURLE_OK) {
            http_note_unfollowed_redirect(curl, req, code);
        }
    }
    if (rc != CURLE_OK && !ctx.aborted) {
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
        jc_http_describe_failure(curl_easy_strerror(rc),
                                 rc == CURLE_OPERATION_TIMEDOUT,
                                 connect_time > 0.0,
                                 req->connect_timeout_secs,
                                 req->err_out, req->err_out_cap);
        jc_logf(JC_LOG_ERROR, "http stream: %s",
                (req->err_out != NULL && req->err_out[0] != '\0')
                    ? req->err_out : curl_easy_strerror(rc));
        st = jc_http_classify(0, rc == CURLE_ABORTED_BY_CALLBACK,
                              rc == CURLE_OPERATION_TIMEDOUT,
                              connect_time > 0.0);
    }

    /* We own a streamed body; free it unless the upload already did at EOF. */
    if (req->stream_body && req->body != NULL && !br.freed) {
        free((char *)req->body);
    }
    curl_slist_free_all(hdrs);
    http_handle_release(curl,
                        jc_http_conn_reusable(rc == CURLE_OK,
                                              connect_time > 0.0));
    return st;
}

#else /* !JC_HAVE_CURL : networking unavailable */

jc_status jc_http_global_init(void) { return JC_OK; }
void      jc_http_global_cleanup(void) {}

jc_status jc_http_perform(const struct jc_http_request *req,
                          long *http_status, char **out, jc_size *out_len)
{
    (void)http_status; (void)out; (void)out_len;
    if (req->stream_body && req->body != NULL) {
        free((char *)req->body); /* honor the ownership-transfer contract */
    }
    jc_logf(JC_LOG_ERROR, "built without libcurl: networking unavailable");
    return JC_ERR_HTTP;
}

jc_status jc_http_stream(const struct jc_http_request *req,
                         long *http_status, jc_http_chunk_cb cb, void *user)
{
    (void)http_status; (void)cb; (void)user;
    if (req->stream_body && req->body != NULL) {
        free((char *)req->body); /* honor the ownership-transfer contract */
    }
    jc_logf(JC_LOG_ERROR, "built without libcurl: networking unavailable");
    return JC_ERR_HTTP;
}

#endif /* JC_HAVE_CURL */
