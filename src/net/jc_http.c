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
#include <ctype.h>     /* tolower: M854 */

/* The socket/inet headers are POSIX, not curl (M190): the pure SSRF
 * classifier below needs inet_pton in EVERY build that has sockets. A build
 * with no socket library at all (JC_NO_SOCKETS, M835 -- DJGPP without a TCP/IP
 * stack) has no inet_pton either; there the classifier reads IPv4 literals with
 * jc_net_parse_ipv4 and refuses IPv6 ones, and it is still compiled, because
 * M190's lesson stands: guarding the classifier away dropped IP-literal SSRF
 * classification from the builds that lacked the guard's condition. */
#include <sys/types.h>
#ifndef JC_NO_SOCKETS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <unistd.h>

#ifdef JC_HAVE_CURL
#if defined(JC_NO_SOCKETS)
/* libcurl cannot exist without a socket API, so this pair means the socket
 * headers are somewhere the Makefile's probe did not look. Stopping is the only
 * safe answer: the connect-time SSRF guard (opensocket_guard) reads struct
 * sockaddr_in, and compiling it out would drop a fence without a word. */
#error "JC_NO_SOCKETS with libcurl: the socket headers exist but were not found; fix the include path"
#endif
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
    /* C-11 (M1123): the reserved/special-purpose ranges the classifier used to
     * let through.  The translation ranges (NAT64, 6to4, IPv4-compatible) embed
     * an IPv4 address, so the embedded v4 is classified -- a private embedded v4
     * is refused; the rest are refused outright (Teredo by its whole prefix,
     * M1123-f1, because its embedded v4 is obfuscated).  Every test below keys
     * on bytes no other test shares, so they are disjoint. */
    /* 1. NAT64: RFC 6052 well-known 64:ff9b::/96 and RFC 8215 local-use
     *    64:ff9b:1::/48 share the 64:ff9b::/24 prefix (bytes 0-2); both embed
     *    the IPv4 target in the last 32 bits (bytes 12-15) -- classify that v4. */
    if (b16[0] == 0x64 && b16[1] == 0xff && b16[2] == 0x9b) {
        return jc_net_ipv4_blocked(b16[12], b16[13], b16[14], b16[15]);
    }
    /* 2. 6to4: RFC 3056, 2002::/16.  The third 16-bit group (bytes 2-5) is the
     *    IPv4 address -- classify that v4. */
    if (b16[0] == 0x20 && b16[1] == 0x02) {
        return jc_net_ipv4_blocked(b16[2], b16[3], b16[4], b16[5]);
    }
    /* 3. Site-local: RFC 3879 (deprecated), fec0::/10 -- refuse. */
    if (b16[0] == 0xfe && (b16[1] & 0xc0) == 0xc0) return 1;
    /* 4. Multicast: RFC 4291, ff00::/8 -- not ordinary unicast, refuse. */
    if (b16[0] == 0xff) return 1;
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
    /* 5. IPv4-compatible: RFC 4291 (deprecated), ::/96 minus :: and ::1, which
     *    the checks above already return for.  Bytes 0-11 all zero and bytes
     *    12-15 non-zero -- classify the embedded v4.  Kept AFTER the v4-mapped
     *    test: the two are disjoint today (v4-mapped has bytes 10-11 = ff ff,
     *    v4-compatible has them 00 00), but v4-mapped is the more specific
     *    form, so mapped-first keeps a future edit of the v4-compatible test
     *    from accidentally widening into the mapped range. */
    {
        int compat = 1;
        for (i = 0; i < 12; i++) {
            if (b16[i] != 0) { compat = 0; break; }
        }
        if (compat && (b16[12] || b16[13] || b16[14] || b16[15])) {
            return jc_net_ipv4_blocked(b16[12], b16[13], b16[14], b16[15]);
        }
    }
    /* 6. Teredo: RFC 4380, 2001::/32 -- refuse all.  Layout: Prefix 0-3,
     *    obfuscated Server 4-7, Flags 8-9, obfuscated Port 10-11, obfuscated
     *    Client 12-15.  The client is XOR-0xFFFFFFFF-obfuscated, so
     *    classifying the raw bytes fails open -- a private client (127.0.0.1)
     *    is stored as 128.255.255.254 and reads as public.  The decision
     *    (M1123-f1) is to refuse the whole legacy 2001::/32 prefix rather
     *    than decode the obfuscation. */
    if (b16[0] == 0x20 && b16[1] == 0x01 && b16[2] == 0x00 && b16[3] == 0x00) {
        return 1;  /* 2001::/32 Teredo: refuse all */
    }
    return 0;
}

int jc_net_host_is_blocked(const char *host)
{
    jc_size n;
    unsigned char v4[4];
#ifndef JC_NO_SOCKETS
    unsigned char v6[16];
#endif
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
#ifdef JC_NO_SOCKETS
    if (jc_net_parse_ipv4(host, v4)) {
        return jc_net_ipv4_blocked(v4[0], v4[1], v4[2], v4[3]);
    }
    if (strchr(host, ':') != NULL) {
        return 1; /* an IPv6 literal nothing here can read: fail closed */
    }
#else
    if (inet_pton(AF_INET, host, v4) == 1) {
        return jc_net_ipv4_blocked(v4[0], v4[1], v4[2], v4[3]);
    }
    if (inet_pton(AF_INET6, host, v6) == 1) {
        return jc_net_ipv6_blocked(v6);
    }
#endif
    return 0;
}

/* M1132 (C-14): the loopback-only half of the address classification.
 *
 * The doctor's plaintext-transport check (M472) must exempt loopback -- an
 * endpoint that stays on this host carries no secret onto a sniffable network --
 * but NOT the private ranges.  jc_net_host_is_blocked above answers "loopback
 * OR private" (it is the SSRF guard), so reusing it as the loopback test
 * exempted 10/8, 172.16/12, 192.168/16 and 100.64/10 from the warning too, and
 * a key sent in clear over a LAN passed in silence.  This classifier answers
 * "is this loopback?" only: the loopback names (localhost, localhost.localdomain,
 * *.localhost), the string '::1', and any 127.0.0.0/8 IPv4 literal return 1;
 * every other host -- public AND private -- returns 0.
 *
 * Pure and always compiled: the 127/8 test reads the literal with
 * jc_net_parse_ipv4 (the reader jc_net_host_is_blocked already uses under
 * JC_NO_SOCKETS), and '::1' is matched as a string, so no inet_pton is needed. */
int jc_net_host_is_loopback(const char *host)
{
    jc_size n;
    unsigned char v4[4];
    if (host == NULL || host[0] == '\0') {
        return 1; /* no host: it never reaches the network -- not a remote leak */
    }
    if (strcmp(host, "localhost") == 0 ||
        strcmp(host, "localhost.localdomain") == 0) {
        return 1;
    }
    n = (jc_size)strlen(host);
    if (n >= 10 && strcmp(host + n - 10, ".localhost") == 0) {
        return 1;
    }
    if (strcmp(host, "::1") == 0) {
        return 1;
    }
    if (jc_net_parse_ipv4(host, v4) && v4[0] == 127) {
        return 1; /* 127.0.0.0/8 loopback */
    }
    return 0;
}

int jc_net_parse_ipv4(const char *s, unsigned char out[4])
{
    int part = 0;
    int digits = 0;
    unsigned int val = 0;

    if (s == NULL) {
        return 0;
    }
    for (;; s++) {
        char ch = *s;
        if (ch >= '0' && ch <= '9') {
            if (digits > 0 && val == 0) {
                return 0;  /* a leading zero: inet_pton refuses "01" */
            }
            val = val * 10u + (unsigned int)(ch - '0');
            digits++;
            if (val > 255u || digits > 3) {
                return 0;
            }
        } else if (ch == '.' || ch == '\0') {
            if (digits == 0 || part == 4) {
                return 0;  /* an empty octet, or a fifth one */
            }
            out[part++] = (unsigned char)val;
            if (ch == '\0') {
                return part == 4;
            }
            val = 0;
            digits = 0;
        } else {
            return 0;
        }
    }
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

/* M854 (N24c): see jc_http.h. */
int jc_http_body_ctx_overflow(const char *body, jc_size len)
{
    static const char *const marks[] = {
        "contextwindowexceeded", "context_length_exceeded",
        "maximum context length"
    };
    jc_size k;

    if (body == NULL) {
        return 0;
    }
    for (k = 0; k < sizeof marks / sizeof marks[0]; k++) {
        jc_size n = strlen(marks[k]);
        jc_size i;
        for (i = 0; i + n <= len; i++) {
            jc_size j = 0;
            while (j < n &&
                   tolower((unsigned char)body[i + j]) == (unsigned char)marks[k][j]) {
                j++;
            }
            if (j == n) {
                return 1;
            }
        }
    }
    return 0;
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

/* M1039: stable failure class for the model_call telemetry row. */
char *jc_http_fail_class(int curl_err, int http_status, int aborted,
                         int timed_out, int connected, int handshaking,
                         int fence_only)
{
    (void)fence_only;  /* M-L2: the stall vs dead-stream distinction */

    /* Decision order matches jc_http_classify and jc_http_describe_failure. */
    if (http_status >= 400) {
        /* The server spoke: an HTTP error response. */
        return "http-status";
    }
    if (http_status > 0) {
        /* M909: error inside a stream that began with HTTP 200. */
        return "provider-error";
    }
    if (aborted) {
        /* The only place that says it was a jichi abort. */
        return "aborted";
    }
    if (timed_out && !connected && handshaking) {
        /* M877: TCP connected, but the TLS handshake didn't finish. */
        return "timeout-tls";
    }
    if (timed_out && !connected) {
        /* Connect timeout before TCP. */
        return "timeout-connect";
    }
    if (timed_out) {
        /* M-L2: connected, then the stream went dead/stalled. */
        return "timeout-stall";
    }
    if (curl_err == 6) {  /* CURLE_COULDNT_RESOLVE_HOST */
        return "dns";
    }
    if (curl_err == 7) {  /* CURLE_COULDNT_CONNECT */
        return "refused";
    }
    /* Never connected, not a timeout. */
    return "connect";
}

void jc_http_describe_failure(const char *curl_err, int timed_out, int connected,
                              int handshaking,
                              long connect_timeout_secs, int fence_only,
                              char *buf, jc_size cap)
{
    const char *msg;

    if (buf == NULL || cap == 0) {
        return;
    }
    msg = (curl_err != NULL && curl_err[0] != '\0') ? curl_err : "transfer failed";
    if (timed_out && !connected && handshaking) {
        /* M877 (N47 (d)): TCP connected and the TLS handshake ran out of the
         * connect bound -- a listening server, slow to shake hands. Saying "could
         * not connect" sends a reader looking for a dead endpoint; saying
         * "stalled" (what jichi said before) blames the model. */
        if (connect_timeout_secs > 0) {
            jc_snprintf(buf, cap,
                        "TCP connected, but the TLS handshake did not finish "
                        "within %lds (timeouts.connect covers it) -- the request "
                        "was never sent; raise timeouts.connect if the endpoint "
                        "is shared or slow [%s]", connect_timeout_secs, msg);
        } else {
            jc_snprintf(buf, cap,
                        "TCP connected, but the TLS handshake did not finish "
                        "(timeouts.connect) -- the request was never sent [%s]",
                        msg);
        }
        return;
    }
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
        /* M-L2: name the knob that fired (M325). With stall off, the bound came
         * from the dead-stream FENCE, not the stall cap the operator never set. */
        if (fence_only) {
            jc_snprintf(buf, cap,
                        "connected, then the stream went dead -- nothing arrived "
                        "(timeouts.deadStream) [%s]", msg);
        } else {
            jc_snprintf(buf, cap,
                        "connected, then the stream stalled (timeouts.stall) [%s]",
                        msg);
        }
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
 * M1062: a failure that DIES mid-transfer on a REUSED (keep-alive) connection
 * is the same mid-stream animal, but `CURLINFO_CONNECT_TIME` lies about it --
 * libcurl leaves it at 0 on a pooled connection (the M880 note), so `connected`
 * alone would read "pre-connect" and KEEP a socket that was actually
 * established and then died. `reused` is that third answer: a connection that
 * came from the pool and died mid-transfer is poisoned too, so it drops. The
 * two call sites pass the same `(tcp_up || reused)` value for both booleans --
 * one answer to the same question.
 *
 * It lives OUTSIDE the JC_HAVE_CURL guard deliberately (M447): it is pure --
 * three ints in, one int out, no curl type and no curl call -- and
 * `tests/test_http.c` links it unconditionally, so guarding it broke
 * `make HAVE_CURL= run_tests` with an undefined symbol. That is the curl-free
 * build M189 already had to repair once, and the recipe it breaks is Tier V's
 * row V0 (`docs/DEPLOYMENT.md` §3e). A pure predicate has no business being
 * behind a dependency's ifdef. */
int jc_http_conn_reusable(int transfer_ok, int connected, int reused)
{
    if (transfer_ok) {
        return 1;
    }
    /* Failed transfer: keep only if neither signal says the connection was
     * established (a fresh dial) or pooled (a reused keep-alive handle). A
     * pre-connect timeout on a cold dial preserves the DNS/TLS caches (M326v);
     * anything that was or came from an established socket is dropped. */
    return !connected && !reused;
}

int jc_http_in_handshake(const char *url, int tcp_connected,
                         double appconnect_time)
{
    static const char https[] = "https://";
    jc_size i;
    if (url == NULL || !tcp_connected || appconnect_time > 0.0) {
        return 0;
    }
    for (i = 0; i < sizeof(https) - 1; i++) {
        char c = url[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != https[i]) {
            return 0;
        }
    }
    return 1;
}

void jc_sockwatch_opened(struct jc_sockwatch *w, int fd)
{
    if (w != NULL && fd >= 0 && w->nfds < JC_SOCKWATCH_MAX) {
        w->fds[w->nfds++] = fd;
    }
}

void jc_sockwatch_closing(struct jc_sockwatch *w, int fd, int has_peer)
{
    int i;
    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->nfds; i++) {
        if (w->fds[i] == fd) {
            w->fds[i] = w->fds[--w->nfds];   /* the number may be reused */
            w->closed++;
            if (has_peer) {
                w->connected++;
            }
            return;
        }
    }
}

int jc_sockwatch_tcp_up(const struct jc_sockwatch *w, double connect_time)
{
    if (w != NULL && w->closed > 0) {
        return w->connected > 0;
    }
    return connect_time > 0.0;
}

/* M1062-c corrected: the reused-transfer discriminator (see the header doc).
 * M1062 (the -b) read connect_time == 0 + an EMPTY per-transfer watch as
 * "reused". M1062-c (SV:1141) corrects that: a COLD transfer whose DNS resolve
 * times out opens NO socket (empty watch) and leaves connect_time 0 -- the -b's
 * exact two inputs -- yet NOTHING was sent, so it is not a reused connection.
 * Only the third input, `sent` (was the request actually uploaded, i.e.
 * CURLINFO_REQUEST_SIZE > 0), separates the two: 0 on a resolve timeout, > 0 on
 * a reused-then-dead transfer. A NULL watch has no data and answers 0. Outside
 * the JC_HAVE_CURL guard so no-curl builds link it. */
int jc_http_reused_conn(const struct jc_sockwatch *w, double connect_time, int sent)
{
    return connect_time == 0.0 && w != NULL &&
           w->nfds == 0 && w->closed == 0 && sent;
}

/* C-15 (M1133): the capability snapshot. ONE curl_version_info(CURLVERSION_NOW)
 * call supplies the version and ssl_version strings (each NULL -> "(none)");
 * the library default CA path needs its own throwaway handle +
 * CURLINFO_CAINFO, which exists only on libcurl >= 7.84.0 (0x075400, the
 * refinement's verified value) -- on older libraries, or when the handle or
 * the query fails, cainfo stays "". The outputs are plain strings, so this
 * compiles and links in a no-curl build too (every field "" there). No global
 * state: it is callable from `--version` before any init and from doctor.
 * Pure string handling, so test_http.c (curl-free) could link the helpers that
 * consume these strings; the snapshot itself is only exercised via the
 * binary-level driver. */
void jc_net_curl_snapshot(char *ver, jc_size vc, char *ssl, jc_size sc,
                          char *cainfo, jc_size cc)
{
    if (ver != NULL && vc > 0) ver[0] = '\0';
    if (ssl != NULL && sc > 0) ssl[0] = '\0';
    if (cainfo != NULL && cc > 0) cainfo[0] = '\0';
#ifdef JC_HAVE_CURL
    {
        const curl_version_info_data *info =
            curl_version_info(CURLVERSION_NOW);
        const char *v = (info != NULL && info->version != NULL)
                            ? info->version : "(none)";
        const char *s = (info != NULL && info->ssl_version != NULL)
                            ? info->ssl_version : "(none)";
        jc_snprintf(ver, vc, "%s", v);
        jc_snprintf(ssl, sc, "%s", s);
#if LIBCURL_VERSION_NUM >= 0x075400
        {
            CURL *h = curl_easy_init();
            if (h != NULL) {
                const char *ci = NULL;
                if (curl_easy_getinfo(h, CURLINFO_CAINFO, &ci) == CURLE_OK &&
                    ci != NULL && ci[0] != '\0') {
                    jc_snprintf(cainfo, cc, "%s", ci);
                }
                curl_easy_cleanup(h);
            }
        }
#endif
    }
#endif
}

/* C-15 (M1133): does `ssl` name a real TLS backend? A TLS-free libcurl
 * reports its ssl_version as "No TLS", so the test is: non-NULL, non-empty,
 * not the "(none)" placeholder, and NOT containing "no tls" case-insensitively
 * (to cover "No TLS", "no tls", "NO TLS", and any "OpenSSL ... " variant that
 * a hypothetical build might phrase as "<backend> (no tls)"). Under JC_FAULT
 * the no-TLS probe (jc_fault_no_tls, env JICHI_FAULT_NO_TLS) wins first -- that
 * is the only way the https-meets-no-TLS doctor arm is reachable on a TLS host
 * (this bench's libcurl HAS OpenSSL), and it is the same deterministic class
 * as JICHI_FAULT_NET_AFTER. A release build never reads the env. Pure (no curl
 * types) so test_http.c links it. */
int jc_net_curl_has_tls(const char *ssl)
{
    int len;
    int i;
#ifdef JC_FAULT
    if (jc_fault_no_tls() != 0) {
        return 0;
    }
#endif
    if (ssl == NULL || ssl[0] == '\0') {
        return 0;
    }
    if (strcmp(ssl, "(none)") == 0) {
        return 0;
    }
    len = (int)strlen(ssl);
    for (i = 0; i + 6 <= len; i++) {
        int n;
        for (n = 0; n < 6; n++) {
            char c = (char)tolower((unsigned char)ssl[i + n]);
            if (c != "no tls"[n]) {
                break;
            }
        }
        if (n == 6) {
            return 0;
        }
    }
    return 1;
}

/* C-15 (M1133): the process-global CA bundle. main.c sets it ONCE at startup
 * from the config's `ca_bundle` (NULL when the key is absent); apply_common
 * reads it and, when non-NULL, sets CURLOPT_CAINFO so every transfer trusts the
 * named bundle. It STORES A POINTER and never copies -- the value points into
 * the session arena (see jc_config), which outlives the process, so the
 * pointer stays valid for every transfer and doctor's offline report agrees
 * with the live ones. Defined outside the JC_HAVE_CURL guard so it links in a
 * no-curl build (there apply_common does not exist to read it, but the symbol
 * must still resolve for main.c). */
static const char *g_curl_ca_bundle = NULL;

const char *jc_net_curl_ca_bundle_set(const char *p)
{
    g_curl_ca_bundle = p;
    return g_curl_ca_bundle;
}

const char *jc_net_curl_ca_bundle_get(void)
{
    return g_curl_ca_bundle;
}

/* C-08 (M1124): the hop-aware redirect private-address guard's per-transfer
 * state and decision. Outside the JC_HAVE_CURL guard (like the other pure
 * predicates in this file) so no-curl builds -- and the unit test, which links
 * jc_http.c directly -- compile and link them (the M447 lesson). In the curl
 * build the libcurl callbacks (opensocket_redirect_guard, redirect_hop_header)
 * use them; on the exotic no-socket build (JC_NO_SOCKETS) the family constants
 * are read by number so the classifier still compiles.
 *
 * g_redirect_hop: the count of 3xx STATUS LINES seen this transfer, advanced by
 * redirect_hop_header (a header callback installed for block_redirect_private
 * requests). libcurl delivers a 3xx response's status line BEFORE it follows
 * the Location, so by the time the redirect hop's opensocket fires, g_redirect_hop
 * already reads the hop number (measured on this host, libcurl 8.18.0). It is
 * what the old per-socket counter (M1120's g_redirect_ip_conn) miscounted as
 * "the transfer's connections": a self-redirect to the SAME host:port and a
 * dual-stack name (a dead ::1 then 127.0.0.1) each open a second socket that is
 * really still hop 0.
 *
 * g_redirect_hop0 / g_redirect_hop0_n: the set of (family, address, port) keys
 * opened at hop 0. redirect_guard_decide_set (via the exported
 * jc_http_redirect_guard_decide) records into it and consults it. The PORT is
 * part of the key deliberately (SV:1437): a feed on 127.0.0.1:FEEDPORT 302ing
 * to 127.0.0.1:TGTPORT must stay refused (a different port on a private host is
 * a server-chosen address -- the M1120 threat), while a self-redirect to the
 * SAME host:port is allowed (the C-08 false refusal). An IP-only key would let
 * the cross-port case through because 127.0.0.1 is in the set, reopening M1120.
 * Bounded at RD_SET_CAP; on overflow an address is not added, and that alone is
 * never a ground to refuse (see redirect_guard_decide_set). */
#ifndef JC_NO_SOCKETS
#define RD_FAM_INET  AF_INET
#define RD_FAM_INET6 AF_INET6
#else
#define RD_FAM_INET  2   /* AF_INET, read by number on a socket-less build */
#define RD_FAM_INET6 10  /* AF_INET6, read by number on a socket-less build */
#endif
static int g_redirect_hop;
struct rd_set_entry { int fam; unsigned char addr[16]; unsigned int port; };
#define RD_SET_CAP 16
static struct rd_set_entry g_redirect_hop0[RD_SET_CAP];
static int g_redirect_hop0_n;

/* C-08 (M1124): forward declarations for the guard's exported pair. The unit
 * test (tests/test_http.c) declares the same prototypes itself; jc_http.h does
 * not (it is not this task's file to edit). Both are pure: the decision reads
 * only its arguments and the per-transfer set, and the reset touches only the
 * guard's own state. */
int  jc_http_redirect_guard_decide(int hop, int fam, const unsigned char *addr,
                                   unsigned int port);
void jc_http_redirect_guard_reset(void);

/* C-08 (M1124): the hop-aware decision for the redirect guard. PURE and set-
 * parameterised: every input is an argument -- the 3xx hop count, the address
 * family, the address bytes (4 for AF_INET, 16 for AF_INET6), the port, and the
 * per-transfer hop-0 set with its count and capacity. It mutates only the set
 * it is handed (it records a hop-0 address into it) and reads no global. The
 * exported jc_http_redirect_guard_decide below is its single production caller
 * (via the file-static set); the unit test drives that EXPORTED decide --
 * bound to the same file-static set -- calling
 * jc_http_redirect_guard_reset() between scenarios for a fresh set, and it is
 * exactly that exported decide + reset pair (no sockets, no libcurl in the
 * call path) that keeps the decision logic unit-testable. Returns 1 = allow,
 * 0 = refuse.
 *
 * M1124-b: corrected the qwen27b review point 7 of the M1124 merge (audited
 * commits 810eabb8/9cfdaaa6) -- this comment used to say the test "drives
 * this core with a set it owns and a fresh hop number per scenario", which
 * was false: it never touches this set-parameterised core directly.
 *
 * hop == 0 (no 3xx seen yet): the URL a human named (HARDENING.md §6b) may be
 * private. Record (fam, addr, port) in the set and allow. A duplicate
 * (the dual-stack retry of an already-recorded address) is allowed without a
 * second slot; past set_cap a new address is NOT recorded, but that alone is
 * never a refusal.
 *
 * hop >= 1 (a 3xx was seen): allow iff (fam, addr, port) is already in the set
 * -- the SAME host:port the transfer opened at hop 0, i.e. a self-redirect
 * (allowed: the C-08 false refusal); else refuse iff the address is private by
 * the classifier, else allow (a public redirect target is fine). */
static int redirect_guard_decide_set(int hop, int fam, const unsigned char *addr,
                                     unsigned int port, struct rd_set_entry *set,
                                     int *set_n, int set_cap)
{
    int i;
    int an = (fam == RD_FAM_INET) ? 4 : 16;
    if (addr == NULL || set == NULL || set_n == NULL || set_cap <= 0) {
        return 0; /* degenerate inputs: fail closed */
    }
    if (hop == 0) {
        for (i = 0; i < *set_n; i++) {
            if (set[i].fam == fam && set[i].port == port &&
                memcmp(set[i].addr, addr, (size_t)an) == 0) {
                return 1; /* dual-stack retry of an already-recorded address */
            }
        }
        if (*set_n < set_cap) {
            set[*set_n].fam = fam;
            memcpy(set[*set_n].addr, addr, (size_t)an);
            memset(set[*set_n].addr + an, 0, (size_t)(16 - an));
            set[*set_n].port = port;
            (*set_n)++;
        }
        /* On overflow the address is not added, but hop 0 is allowed anyway --
         * the user named it, so a full set is not a ground to refuse. */
        return 1;
    }
    for (i = 0; i < *set_n; i++) {
        if (set[i].fam == fam && set[i].port == port &&
            memcmp(set[i].addr, addr, (size_t)an) == 0) {
            return 1; /* self-redirect to the SAME host:port opened at hop 0 */
        }
    }
    if (fam == RD_FAM_INET) {
        return jc_net_ipv4_blocked(addr[0], addr[1], addr[2], addr[3]) ? 0 : 1;
    }
    if (fam == RD_FAM_INET6) {
        return jc_net_ipv6_blocked(addr) ? 0 : 1;
    }
    return 1; /* an opensocket IPCXN family is always AF_INET / AF_INET6 */
}

/* C-08 (M1124): the production entry point for the decision -- the same pure
 * core bound to the per-transfer file-static set (g_redirect_hop0), which is
 * what opensocket_redirect_guard consults. The caller passes g_redirect_hop
 * (the 3xx count) as `hop`, so the production path is a single call site with
 * one hop source. Returns 1 = allow, 0 = refuse. */
int jc_http_redirect_guard_decide(int hop, int fam, const unsigned char *addr,
                                  unsigned int port)
{
    return redirect_guard_decide_set(hop, fam, addr, port,
                                     g_redirect_hop0, &g_redirect_hop0_n,
                                     RD_SET_CAP);
}

/* C-08 (M1124): the per-transfer reset for the guard's state -- the 3xx hop
 * count and the hop-0 set. apply_common calls it next to the g_sockwatch reset
 * so every guarded request starts at hop 0 with an empty set; the unit test
 * calls it between scenarios to get a fresh set without touching any other
 * global. */
void jc_http_redirect_guard_reset(void)
{
    g_redirect_hop = 0;
    g_redirect_hop0_n = 0;
    memset(g_redirect_hop0, 0, sizeof(g_redirect_hop0));
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
    long         max_bytes; /* 0 => uncapped (jc_http_perform always sets a
                              * ceiling here, default or caller's) */
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
/* CURL_SOCKOPT_OK arrived in libcurl 7.21.5; before it the callback returned a
 * plain 0 for the same meaning. The documented floor is 7.19.4, and CentOS 6's
 * 7.19.7 did not compile this file until the ladder measured it (2026-09-24). */
#ifndef CURL_SOCKOPT_OK
#define CURL_SOCKOPT_OK 0
#endif

static int sockopt_cloexec(void *clientp, curl_socket_t curlfd,
                           curlsocktype purpose)
{
    (void)purpose;
    jc_fd_cloexec((int)curlfd);
    /* M880: and note it as this transfer's, for jc_sockwatch_tcp_up */
    jc_sockwatch_opened((struct jc_sockwatch *)clientp, (int)curlfd);
    return CURL_SOCKOPT_OK;
}

/* M880: libcurl 8.14 and later leave CURLINFO_CONNECT_TIME at 0 when a TLS
 * handshake times out, so whether TCP was up is asked of the socket itself as
 * libcurl closes it: a peer means the connection was made.
 *
 * M1062-c corrected (was M1062 corrected): the SAME connect_time == 0 means
 * something else on a REUSED (keep-alive) transfer -- the connection came from
 * the pool, so libcurl never dialed it this transfer and the watch (M882,
 * per-transfer) has closed == 0 too. Both inputs of jc_sockwatch_tcp_up go
 * quiet, so a socket that was established and had carried earlier calls reads
 * tcp_up == 0. The discriminator is
 * jc_http_reused_conn(&g_sockwatch, connect_time, sent): an EMPTY per-transfer
 * watch (nfds == 0 && closed == 0, the transfer opened none) plus connect_time
 * == 0 plus `sent` (the request was uploaded). M1062 (the -b) used the first
 * two; M1062-c adds the third because a COLD transfer whose DNS resolve times
 * out ALSO gives an empty watch and connect_time == 0 -- the -b's exact inputs
 * -- so the -b read a resolve timeout as reused. A cold dial that FAILED pre-TCP
 * opened and libcurl closed a socket, so its watch is non-empty (M326v tooth)
 * regardless of `sent`; jc_sockwatch_closing counts only sockets THIS transfer
 * opened, so even a pooled socket closed at the transfer's end is a foreign fd
 * and leaves the watch empty. connect_time alone is insufficient, and `curl ==
 * g_reuse` is a tautology (one persistent handle, returned in every path). The
 * helper is consulted ONLY for timeouts, so a reused transfer that fails for
 * any other reason is unchanged from pre-M1062 behaviour. At the two failure
 * sites `reused` becomes a third, truthful answer to "was it connected?" and
 * they pass `tcp_up || reused`.
 *
 * M882: THE WATCH IS STATIC, and must be. libcurl copies the close-socket data
 * pointer into each connection when it creates it, and closes a connection the
 * server kept open long after the transfer that opened it -- at a later transfer,
 * or at exit. M880 kept the watch in the transfer's frame and detached it from the
 * handle after curl_easy_perform; the detach never reached the connection, and
 * the close wrote into a returned frame (AddressSanitizer: stack-use-after-return
 * in jc_sockwatch_closing, one turn against LM Studio; on the Pi 400 both of the
 * rig's turns died of it). apply_common resets it per transfer, so a transfer only
 * counts the sockets it opened; a kept connection closed during a later transfer
 * is not in that transfer's set, and one closed at exit counts for nobody. One
 * watch serves the process because its HTTP runs on one reuse handle, one transfer
 * at a time; a second thread doing HTTP would need a watch per handle.
 * CURLOPT_CLOSESOCKETFUNCTION arrived in libcurl 7.21.7; before it the watch sees
 * no close and CURLINFO_CONNECT_TIME decides. */
static struct jc_sockwatch g_sockwatch;

#if LIBCURL_VERSION_NUM >= 0x071507
static int closesocket_watch(void *clientp, curl_socket_t item)
{
    if (clientp != NULL) {
        struct sockaddr_storage peer;
        socklen_t len = (socklen_t)sizeof(peer);
        jc_sockwatch_closing((struct jc_sockwatch *)clientp, (int)item,
                             getpeername((int)item, (struct sockaddr *)&peer,
                                         &len) == 0);
    }
    return close((int)item);
}
#endif

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

/* C-08 (M1124, was M1120): the REDIRECT-hop private-address guard, now hop-
 * aware. The decision lives in redirect_guard_decide_set (via
 * jc_http_redirect_guard_decide): hop 0 (no 3xx seen yet -- the URL a human
 * named, allowed to be internal per HARDENING.md §6b) is allowed and recorded
 * in the per-transfer (family, address, port) set; a hop after a 3xx is allowed
 * only if it is the SAME host:port the transfer opened at hop 0 (a self-
 * redirect), else refused at a private / link-local / reserved address.
 *
 * This guard replaces M1120's per-socket counter (g_redirect_ip_conn), which
 * miscounted the two C-08 false refusals: a feed that 302s to ITSELF (the
 * follow-up is a second socket, counted as private hop 2) and a dual-stack name
 * (a dead ::1 attempt, then 127.0.0.1; the ::1 attempt counted first, so the
 * host's own v4 became "hop 2"). The hop is now counted by the 3xx STATUS
 * LINES in redirect_hop_header (a header callback installed for this request):
 * libcurl delivers a redirect's status line before it opens the redirect
 * socket, so g_redirect_hop already reads the hop number when this fires
 * (measured on this host, libcurl 8.18.0). FRESH_CONNECT/FORBID_REUSE (set
 * alongside this guard) keep it well defined -- a pooled connection would skip
 * both the opensocket and the header callback. */
static curl_socket_t opensocket_redirect_guard(void *clientp, curlsocktype purpose,
                                               struct curl_sockaddr *addr)
{
    (void)clientp;
    if (addr == NULL) {
        return CURL_SOCKET_BAD;
    }
    if (purpose == CURLSOCKTYPE_IPCXN) {
        unsigned char o[16];
        unsigned int port = 0;
        if (addr->family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)(void *)&addr->addr;
            const unsigned char *a = (const unsigned char *)&s->sin_addr.s_addr;
            memcpy(o, a, 4);
            memset(o + 4, 0, 12);
            port = (unsigned int)ntohs(s->sin_port);
        } else if (addr->family == AF_INET6) {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)(void *)&addr->addr;
            memcpy(o, (const unsigned char *)&s->sin6_addr, 16);
            port = (unsigned int)ntohs(s->sin6_port);
        } else {
            memset(o, 0, sizeof(o));
        }
        if (jc_http_redirect_guard_decide(g_redirect_hop, addr->family, o, port) == 0) {
            return CURL_SOCKET_BAD;
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

/* C-08 (M1124): the 3xx-status-line hop counter. A CURLOPT_HEADERFUNCTION
 * callback installed when block_redirect_private is set. It looks for an HTTP
 * status line (a header chunk whose stripped text begins with "HTTP/") and, if
 * the three status digits are exactly 3xx (300-399), increments g_redirect_hop
 * -- the redirect hop opensocket_redirect_guard then consults via
 * jc_http_redirect_guard_decide. (M1124-b: this line named the stale
 * jc_http_redirect_decide; the real symbol is jc_http_redirect_guard_decide.
 * qwen27b review point 7 of the M1124 merge, audited commits
 * 810eabb8/9cfdaaa6.)
 *
 * The 3xx-only rule: a 1xx informational (e.g. 100 Continue) is not a hop, and
 * a 401/407 auth retry is not a hop either -- neither moves the request to a
 * server-chosen target, so counting them would advance g_redirect_hop for a
 * connection that is still the user-named hop 0 and false-refuse it.
 *
 * libcurl delivers a redirect's status line in the header callback BEFORE it
 * opens the redirect hop's socket, so by the time opensocket_redirect_guard
 * fires for that hop g_redirect_hop already reads 1 (measured on this host,
 * libcurl 8.18.0). That ordering is the load-bearing fact the hop count relies
 * on. The chunk may carry the status line without a trailing CRLF, so the code
 * scans the first "HTTP/" token and reads its status digits off that. */
static size_t redirect_hop_header(char *ptr, size_t size, size_t nmemb, void *ud)
{
    size_t total = size * nmemb;
    char line[64];
    size_t n = total;
    const char *p;
    const char *d;
    (void)ud;
    while (n > 0 && (ptr[n - 1] == '\r' || ptr[n - 1] == '\n')) {
        n--;
    }
    if (n == 0 || n >= sizeof(line)) {
        return total; /* blank or oversized: ignore */
    }
    memcpy(line, ptr, n);
    line[n] = '\0';
    /* The status line is the first "HTTP/" in the chunk; curl hands it as its
     * own chunk, so it is line[0] in practice, but find it to be safe. The form
     * is "HTTP/<version> <code> <reason>", e.g. "HTTP/1.1 302 Found": after
     * "HTTP/" comes the version (digits and a dot: "1.1", "2"), a single space,
     * then the three status digits. Skip the version and the space to reach
     * the code. */
    p = (const char *)strstr(line, "HTTP/");
    if (p == NULL) {
        return total; /* not a status line */
    }
    d = p + 5;
    while (*d >= '0' && *d <= '9') {
        d++; /* skip the major version digit(s) */
    }
    if (*d == '.') {
        d++;
        while (*d >= '0' && *d <= '9') {
            d++; /* skip the minor version digit(s) */
        }
    }
    if (*d == ' ') {
        d++; /* the space before the status code */
    }
    if (d[0] == '3' && d[1] >= '0' && d[1] <= '9' && d[2] >= '0' && d[2] <= '9' &&
        (d[3] == ' ' || d[3] == '\0')) {
        g_redirect_hop++; /* a 3xx status line: one redirect hop delivered */
    }
    return total;
}

/* C-08 (M1124): run BOTH the header collector (when a caller wants response
 * headers back) and the 3xx hop counter. header_collect alone is installed
 * only when req->resp_headers != NULL; redirect_hop_header alone is installed
 * only when block_redirect_private is set; when a future caller sets both, this
 * wrapper keeps each behaviour rather than letting one overwrite the other
 * (CURLOPT_HEADERFUNCTION takes a single callback). */
struct header_both_ctx {
    struct jc_http_headers *h;
};
static size_t header_collect_and_hop(char *ptr, size_t size, size_t nmemb, void *ud)
{
    struct header_both_ctx *c = (struct header_both_ctx *)ud;
    size_t total = size * nmemb;
    /* header_collect collects the response header; redirect_hop_header counts
     * the 3xx status line. Both return total (a header callback's success
     * value) and share no state, so run both and hand libcurl the same total. */
    header_collect(ptr, size, nmemb, c->h);
    redirect_hop_header(ptr, size, nmemb, NULL);
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
 * transfer cleanly (the caller re-sends) rather than reusing freed memory.
 * CURL_SEEKFUNC_CANTSEEK is 7.19.5; on the documented floor, 7.19.4, any
 * non-zero return fails the seek, which is the behaviour wanted, so the
 * constant's value stands in for it there. */
#ifndef CURL_SEEKFUNC_CANTSEEK
#define CURL_SEEKFUNC_CANTSEEK 2
#endif
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

#ifdef JC_FAULT
#if LIBCURL_VERSION_NUM >= 0x073b00
/* M1062-e (SV:1657, card 97): a FAULT-only resolver-start stall. A resolver
 * that outlives the connect bound -- curl rc=28 "Timeout was reached" with
 * connect_time 0.0, request_size 0, and NO socket ever opened (fd delta 0,
 * measured on this host with a standalone curl: 3000 ms sleep, 1000 ms bound)
 * -- is exactly the shape M1062-c corrects: the -b read empty-watch +
 * connect_time 0 as a REUSED stream death (timeout-stall), and the -c reads it
 * as a connect failure (timeout-connect). The netns e2e cell (SV:1141) skips
 * under make ci where AppArmor blocks unprivileged userns and its in-container
 * shape is the rc=6 fast-fail one the -b is not red on, so this hook is the
 * hang-shape tooth that runs on the gate host: no user namespace, FAULT=1
 * binary, the resolver sleeps past the connect bound. The callback sleeps the
 * ms value JICHI_FAULT_RESOLVE_STALL_MS names (re-read here, so it is the same
 * number the hook armed -- the accessor jc_fault_resolve_stall_ms, which does
 * not itself sleep). Unset it is -1 and the callback returns at once, so a
 * FAULT=1 build with no stall armed behaves exactly like the default.
 * jc_sleep_ms (jc_platform.h, via jc_http.h) is this tree's portable sleep --
 * it slices internally (50 ms, honours JC_NO_NANOSLEEP), so no usleep, no
 * useconds_t, no long long: 3000 ms is ~60 slices. */
static int resolve_stall_cb(void *resolver_state, void *reserved, void *userdata)
{
    long ms = jc_fault_resolve_stall_ms();
    (void)resolver_state;
    (void)reserved;
    (void)userdata;
    if (ms > 0) {
        (void)jc_sleep_ms(ms, NULL);
    }
    return 0;
}
#endif
#endif

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
    /* M880: and note them as this transfer's, in a watch that outlives every
     * connection (M882: static, reset here per transfer) */
    memset(&g_sockwatch, 0, sizeof(g_sockwatch));
    jc_http_redirect_guard_reset(); /* C-08 (M1124): hop 0, empty set, per transfer */
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, sockopt_cloexec);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA, (void *)&g_sockwatch);
#if LIBCURL_VERSION_NUM >= 0x071507
    /* M880: and ask each of this transfer's sockets, as it closes, whether TCP
     * was up -- the one signal that survives a TLS handshake timeout on libcurl
     * 8.14 and later */
    curl_easy_setopt(curl, CURLOPT_CLOSESOCKETFUNCTION, closesocket_watch);
    curl_easy_setopt(curl, CURLOPT_CLOSESOCKETDATA, (void *)&g_sockwatch);
#endif

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
     * regression dressed as hardening. TLS 1.2 is from 2008 and every supported
     * backend can meet it.
     *
     * VERIFYHOST is 2L, not 1L: 1 was "check the name exists" and has been a
     * synonym for 2 since 7.28.1, but naming 2 says the intent. Anyone who ever
     * needs to turn these off should have to delete a line that says why. */
    /* ...where libcurl can say so. CURL_SSLVERSION_TLSv1_2 is 7.34.0 (2013), not
     * 2008: the protocol is older than the constant, and on the documented floor
     * (7.19.4) and on Debian 7's 7.26 the name does not exist, so the file did
     * not compile there (the userland ladder, 2026-09-24). TLSv1 is the most such a
     * libcurl can ask for -- any TLS, never an SSL fallback; the TLS library still
     * negotiates the highest version both ends have. */
#if LIBCURL_VERSION_NUM >= 0x072200
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
#else
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1);
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    /* C-15 (M1133): the config's CA bundle, if any. The process-global is set
     * once at startup from the config's `caBundle` (NULL when the key is
     * absent), so every transfer -- and the pooled g_reuse handle that
     * curl_easy_reset clears between them, which apply_common re-arms --
     * trusts the SAME roots the doctor reported. CURLOPT_CAINFO is a plain
     * string setopt available since 7.19.0, the tree's build floor, so it is
     * unguarded here (like CURLOPT_SSL_VERIFYPEER just above). It only
     * replaces the trust ANCHORS, not verification: PEER/HOST stay on, so a
     * named bundle can never loosen a check, only repoint it. */
    {
        const char *ca = jc_net_curl_ca_bundle_get();
        if (ca != NULL) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
        }
    }

    if (req->block_private_addrs) {
        /* SSRF hardening (M131): a connect-time private-IP block that also
         * covers every redirect target and every DNS resolution.
         *
         * M1026/C-01: A guarded request must open (and be checked on) a fresh
         * socket and must not reuse a pooled connection. curl_easy_reset on the
         * shared g_reuse handle keeps its connection cache, and opensocket_guard
         * runs only on socket open. Without these two options a fetch to the
         * provider's own host:port skips the connect-time check (measured:
         * POST; GET /probe; POST on one connection, is_error false).
         * Provider (unguarded) requests are untouched and keep their M227
         * one-reused-connection latency win.
         *
         * M1027/C-02: A guarded request must ignore any environment proxy
         * (http_proxy, https_proxy, ALL_PROXY, NO_PROXY) because the SSRF guard
         * judges the proxy's address, not the target's. When a loopback proxy
         * is set, the guard would see 127.0.0.1 and approve it; setting
         * CURLOPT_PROXY to "" makes libcurl go direct, so the guard sees the
         * target. Measured: curl 8.18.0 treats CURLOPT_PROXY="" as "no proxy". */
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, opensocket_guard);
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, (void *)NULL);
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
    }
    if (req->block_redirect_private) {
        /* C-08 (M1124, was M1120): the feed/doc redirect-hop guard, hop-aware.
         * Hop 0 (no 3xx seen yet -- the URL a human named, allowed to be internal
         * per HARDENING.md §6b) is allowed and its (family, address, port) is
         * recorded; a hop after a 3xx is allowed only if it is the SAME host:port
         * recorded at hop 0 (a self-redirect), else refused at a private /
         * link-local / reserved address. The hop number is the 3xx STATUS LINES
         * seen this transfer (g_redirect_hop, advanced by the redirect_hop_header
         * callback installed in the perform/stream path below) -- not the count of
         * sockets, which M1120's g_redirect_ip_conn used and which miscounted a
         * self-redirect (a second socket) and a dual-stack name (a dead ::1
         * attempt) as redirect hops.
         *
         * Parallel to, not inside, the block_private_addrs branch: the two
         * guards are independent, and this one must not refuse hop 0. The three
         * authority options keep the hop well defined: FRESH_CONNECT +
         * FORBID_REUSE (M1026) so the first hop opens a fresh, counted socket
         * rather than riding a pooled connection that would skip BOTH the
         * opensocket callback and the header callback that counts the hop; PROXY
         * "" (M1027) so it sees the target, not a proxy. The
         * REDIR_PROTOCOLS/MAXREDIRS lines are applied to every request
         * unconditionally above.
         * Note: applied AFTER the block_private_addrs branch, so if a request ever
         * set both, this (weaker, first-hop-allowed) guard would win the opensocket
         * callback; no current caller sets both. */
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, opensocket_redirect_guard);
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, (void *)NULL);
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
    }
    if (req->timeout_secs > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, req->timeout_secs);
    }
    if (req->connect_timeout_secs > 0) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                         req->connect_timeout_secs);
    }
#ifdef JC_FAULT
#if LIBCURL_VERSION_NUM >= 0x073b00
    /* M1062-e (SV:1657, card 97): the FAULT-only resolver-stall hook. Armed when
     * JICHI_FAULT_RESOLVE_STALL_MS names a positive duration -- the callback
     * above then sleeps the resolver-start past the connect bound and the
     * transfer reads rc=28 with connect_time 0, request_size 0, no socket. The
     * value is re-read per call (the accessor), so it is always the number this
     * hook armed. Unset it returns -1 and the hook does nothing: a FAULT=1
     * build with no stall behaves exactly like the default. The 7.59.0 guard is
     * house style (CURLOPT_RESOLVER_START_FUNCTION's floor); the mincurl floor
     * is 8.18.0, so it always compiles in here. Non-FAULT builds are
     * byte-identical: the whole hook is compiled out. */
    {
        long stall_ms = jc_fault_resolve_stall_ms();
        if (stall_ms > 0) {
            curl_easy_setopt(curl, CURLOPT_RESOLVER_START_FUNCTION,
                             resolve_stall_cb);
            curl_easy_setopt(curl, CURLOPT_RESOLVER_START_DATA, (void *)NULL);
        }
    }
#endif
#endif
    {
        /* M-L2: curl has ONE low-speed window per handle, so the stall CAP and the
         * dead-stream FENCE are one setting. The window is min(stall, dead) when
         * stall is on, else dead -- so the fence bounds a call even at `stall 0`
         * (a measurement run). The floor (1 B/s) means it fires only when nothing
         * is arriving, which is what makes the fence safe to leave on. */
        long low = 0;
        if (req->stall_timeout_secs > 0 && req->dead_stream_secs > 0) {
            low = req->stall_timeout_secs < req->dead_stream_secs
                      ? req->stall_timeout_secs : req->dead_stream_secs;
        } else if (req->stall_timeout_secs > 0) {
            low = req->stall_timeout_secs;
        } else if (req->dead_stream_secs > 0) {
            low = req->dead_stream_secs;
        }
        if (low > 0) {
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT,
                             JC_HTTP_STALL_LOW_SPEED);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, low);
        }
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

jc_status jc_http_perform(struct jc_http_request *req,
                          long *http_status, char **out, jc_size *out_len)
{
    CURL *curl;
    struct curl_slist *hdrs;
    struct collect_ctx ctx;
    struct body_reader br;
    struct xfer_ctx xc;
    struct header_both_ctx hb; /* C-08 (M1124): only used when resp_headers && block_redirect_private */
    CURLcode rc;
    jc_status st = JC_OK;
    double connect_time = 0.0;   /* >0 once a connection was established */
    double app_time = 0.0;       /* M877: >0 once a TLS handshake finished */
    int shaking = 0;             /* M877: TCP up, the TLS handshake not done */
    int tcp_up = 0;              /* M880: TCP was connected (jc_sockwatch_tcp_up) */
    int reused = 0;              /* M1062: the connection came from the pool */
    long request_size = 0;      /* M1062-c: bytes uploaded (0 => nothing sent) */

    curl = http_handle_acquire();
    if (curl == NULL) {
        if (req->stream_body && req->body != NULL) {
            free((char *)req->body); /* ownership was transferred to us */
        }
        return JC_ERR_HTTP;
    }
    jc_sb_init(&ctx.buf);
    ctx.oom = 0;
    /* C-06: a caller that passes no cap (0) is still bounded -- the default
     * ceiling, not "grow until OOM". A cap already set is honoured verbatim.
     * The ceiling is a CEILING (write_collect aborts past it); it never
     * truncates a body the caller would then misread. */
    ctx.max_bytes = (req->max_bytes > 0) ? req->max_bytes
                                         : JC_HTTP_DEFAULT_RESP_BYTES;
    ctx.overflow = 0;
    req->overflow = 0;
    memset(&br, 0, sizeof(br));
    memset(&xc, 0, sizeof(xc));
    hdrs = build_slist(req->headers);

    apply_common(curl, req, hdrs, &br, &xc);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    /* C-08 (M1124): install the 3xx hop counter (redirect_hop_header) when the
     * redirect guard is on, coexisting with header_collect (response headers)
     * when a caller wants both. CURLOPT_HEADERFUNCTION takes one callback, so
     * the wrapper header_collect_and_hop runs each without dropping the other. */
    if (req->resp_headers != NULL) {
        if (req->block_redirect_private) {
            hb.h = req->resp_headers;
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_collect_and_hop);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hb);
        } else {
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_collect);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, req->resp_headers);
        }
    } else if (req->block_redirect_private) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, redirect_hop_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)NULL);
    }

    rc = curl_easy_perform(curl);
    {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (http_status != NULL) {
            *http_status = code;
        }
        if (rc != CURLE_OK) {
            /* M1039: classify the failure for telemetry */
            curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
            curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &app_time);
            /* M1062-c: was the request actually uploaded? 0 on a resolve
             * timeout (nothing was sent) and > 0 on a reused-then-dead transfer.
             * CURLINFO_REQUEST_SIZE = CURLINFO_LONG + 12: takes a pointer to long. */
            curl_easy_getinfo(curl, CURLINFO_REQUEST_SIZE, &request_size);
            /* M877 (N47 (d)): a TLS handshake not finished is still connecting;
             * M880: whether TCP was up is the socket's answer, not CONNECT_TIME's */
            tcp_up = jc_sockwatch_tcp_up(&g_sockwatch, connect_time);
            /* M1062-c corrected: a TIMED-OUT transfer with connect_time == 0 on
             * an EMPTY per-transfer watch AND nothing sent is a cold dial that
             * never got a socket (a resolve timeout) -- kept, not reused. The
             * -b claimed connect_time == 0 + empty watch separates reused from a
             * pre-TCP cold dial, which is what this corrects: a resolve timeout
             * gives both and was misread as reused. The third input -- bytes
             * sent -- separates them: 0 on a resolve timeout, > 0 on a
             * reused-then-dead transfer. The helper is consulted only for
             * timeouts, so a reused transfer that fails for any other reason is
             * unchanged from pre-M1062. "connected" here is tcp_up || reused,
             * and a reused transfer's dead stream reads as a stream death, not
             * a connect timeout. (M326v/M880/M882 teeth unchanged: a cold dial
             * that opened a socket before TCP has a non-empty watch, so it is
             * never reused.) */
            reused = (rc == CURLE_OPERATION_TIMEDOUT) &&
                     jc_http_reused_conn(&g_sockwatch, connect_time,
                                         request_size > 0);
            shaking = jc_http_in_handshake(req->url, tcp_up, app_time);
            req->fail_class = jc_http_fail_class(rc, code,
                                                 rc == CURLE_ABORTED_BY_CALLBACK,
                                                 rc == CURLE_OPERATION_TIMEDOUT,
                                                 (tcp_up || reused) && !shaking,
                                                 shaking,
                                                 req->stall_timeout_secs == 0);
            jc_http_describe_failure(curl_easy_strerror(rc),
                                     rc == CURLE_OPERATION_TIMEDOUT,
                                     (tcp_up || reused) && !shaking, shaking,
                                     req->connect_timeout_secs,
                                     req->stall_timeout_secs == 0, /* M-L2: fence only */
                                     req->err_out, req->err_out_cap);
            jc_logf(JC_LOG_ERROR, "http: %s",
                    (req->err_out != NULL && req->err_out[0] != '\0')
                        ? req->err_out : curl_easy_strerror(rc));
            st = jc_http_classify(0, rc == CURLE_ABORTED_BY_CALLBACK,
                                  rc == CURLE_OPERATION_TIMEDOUT,
                                  (tcp_up || reused) && !shaking);
            /* C-06: hand the caller the "cut at the ceiling" fact (the abort
             * only happens when write_collect refused to buffer past the cap);
             * 0 on every other failure class. */
            req->overflow = ctx.overflow;
        } else {
            http_note_unfollowed_redirect(curl, req, code);
            if (out != NULL) {
                if (out_len != NULL) {
                    *out_len = ctx.buf.len;
                }
                *out = jc_sb_finish(&ctx.buf);
            }
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
    /* M1062: on a failed transfer, tcp_up || reused says whether the connection
     * was established (fresh dial) or pooled (reused) -- an either-or death
     * drops the handle so the retry re-dials instead of riding a dead socket.
     * On success rc == CURLE_OK, the predicate returns 1 before reading them. */
    http_handle_release(curl,
                        jc_http_conn_reusable(rc == CURLE_OK,
                                              tcp_up || reused,
                                              tcp_up || reused));
    return st;
}

jc_status jc_http_stream(struct jc_http_request *req,
                         long *http_status,
                         jc_http_chunk_cb cb, void *user)
{
    CURL *curl;
    struct curl_slist *hdrs;
    struct stream_ctx ctx;
    struct body_reader br;
    struct xfer_ctx xc;
    struct header_both_ctx hb; /* C-08 (M1124): only used when resp_headers && block_redirect_private */
    CURLcode rc;
    jc_status st = JC_OK;
    double connect_time = 0.0;   /* >0 once a connection was established */
    double app_time = 0.0;       /* M877: >0 once a TLS handshake finished */
    int shaking = 0;             /* M877: TCP up, the TLS handshake not done */
    int tcp_up = 0;              /* M880: TCP was connected (jc_sockwatch_tcp_up) */
    int reused = 0;              /* M1062: the connection came from the pool */
    long request_size = 0;      /* M1062-c: bytes uploaded (0 => nothing sent) */

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
    /* C-08 (M1124): mirror the perform path -- install the 3xx hop counter when
     * the redirect guard is on, coexisting with header_collect for a caller that
     * wants response headers back (none does on a guarded stream today, but the
     * wrapper keeps the two behaviours independent). */
    if (req->resp_headers != NULL) {
        if (req->block_redirect_private) {
            hb.h = req->resp_headers;
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_collect_and_hop);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hb);
        } else {
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_collect);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, req->resp_headers);
        }
    } else if (req->block_redirect_private) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, redirect_hop_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)NULL);
    }

    rc = curl_easy_perform(curl);
    {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (http_status != NULL) {
            *http_status = code;
        }
        if (rc == CURLE_OK) {
            http_note_unfollowed_redirect(curl, req, code);
            /* M1042: if the server replied with an error status (>= 400), the
             * writer is now the classifier -- jc_http_fail_class's first branch
             * returns "http-status", so that branch is reached at runtime. */
            if (code >= 400) {
                req->fail_class = jc_http_fail_class(0, (int)code, 0, 0, 1, 0,
                                                     req->stall_timeout_secs == 0);
            }
        } else if (!ctx.aborted) {
            /* M1039: classify the failure for telemetry.
             * Skip aborted transfers - the caller (agent) sets fail_class="aborted". */
            curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
            curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &app_time);
            /* M1062-c: was the request actually uploaded? 0 on a resolve
             * timeout (nothing was sent) and > 0 on a reused-then-dead transfer.
             * CURLINFO_REQUEST_SIZE = CURLINFO_LONG + 12: takes a pointer to long. */
            curl_easy_getinfo(curl, CURLINFO_REQUEST_SIZE, &request_size);
            /* M877 (N47 (d)): a TLS handshake not finished is still connecting;
             * M880: whether TCP was up is the socket's answer, not CONNECT_TIME's */
            tcp_up = jc_sockwatch_tcp_up(&g_sockwatch, connect_time);
            /* M1062-c corrected: a TIMED-OUT transfer with connect_time == 0 on
             * an EMPTY per-transfer watch AND nothing sent is a cold dial that
             * never got a socket (a resolve timeout) -- kept, not reused. The
             * -b claimed connect_time == 0 + empty watch separates reused from a
             * pre-TCP cold dial, which is what this corrects: a resolve timeout
             * gives both and was misread as reused. The third input -- bytes
             * sent -- separates them: 0 on a resolve timeout, > 0 on a
             * reused-then-dead transfer. The helper is consulted only for
             * timeouts, so a reused transfer that fails for any other reason is
             * unchanged from pre-M1062. "connected" here is tcp_up || reused,
             * and a reused transfer's dead stream reads as a stream death, not
             * a connect timeout. (M326v/M880/M882 teeth unchanged: a cold dial
             * that opened a socket before TCP has a non-empty watch, so it is
             * never reused.) */
            reused = (rc == CURLE_OPERATION_TIMEDOUT) &&
                     jc_http_reused_conn(&g_sockwatch, connect_time,
                                         request_size > 0);
            shaking = jc_http_in_handshake(req->url, tcp_up, app_time);
            req->fail_class = jc_http_fail_class(rc, code,
                                                 rc == CURLE_ABORTED_BY_CALLBACK,
                                                 rc == CURLE_OPERATION_TIMEDOUT,
                                                 (tcp_up || reused) && !shaking,
                                                 shaking,
                                                 req->stall_timeout_secs == 0);
            jc_http_describe_failure(curl_easy_strerror(rc),
                                     rc == CURLE_OPERATION_TIMEDOUT,
                                     (tcp_up || reused) && !shaking, shaking,
                                     req->connect_timeout_secs,
                                     req->stall_timeout_secs == 0, /* M-L2: fence only */
                                     req->err_out, req->err_out_cap);
            jc_logf(JC_LOG_ERROR, "http stream: %s",
                    (req->err_out != NULL && req->err_out[0] != '\0')
                        ? req->err_out : curl_easy_strerror(rc));
            st = jc_http_classify(0, rc == CURLE_ABORTED_BY_CALLBACK,
                                  rc == CURLE_OPERATION_TIMEDOUT,
                                  (tcp_up || reused) && !shaking);
        }
    }

    /* We own a streamed body; free it unless the upload already did at EOF. */
    if (req->stream_body && req->body != NULL && !br.freed) {
        free((char *)req->body);
    }
    curl_slist_free_all(hdrs);
    /* M1062: on a failed transfer, tcp_up || reused says whether the connection
     * was established (fresh dial) or pooled (reused) -- an either-or death
     * drops the handle so the retry re-dials instead of riding a dead socket.
     * On success rc == CURLE_OK, the predicate returns 1 before reading them. */
    http_handle_release(curl,
                        jc_http_conn_reusable(rc == CURLE_OK,
                                              tcp_up || reused,
                                              tcp_up || reused));
    return st;
}

#else /* !JC_HAVE_CURL : networking unavailable */

jc_status jc_http_global_init(void) { return JC_OK; }
void      jc_http_global_cleanup(void) {}

jc_status jc_http_perform(struct jc_http_request *req,
                          long *http_status, char **out, jc_size *out_len)
{
    (void)http_status; (void)out; (void)out_len;
    if (req->stream_body && req->body != NULL) {
        free((char *)req->body); /* honor the ownership-transfer contract */
    }
    jc_logf(JC_LOG_ERROR, "built without libcurl: networking unavailable");
    return JC_ERR_HTTP;
}

jc_status jc_http_stream(struct jc_http_request *req,
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
