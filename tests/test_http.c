/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_http.c - the pure pieces of the HTTP layer (no network). */

#include "jc_test.h"
#include "jc_http.h"

void test_http(void)
{
    /* jc_http_classify (M22c): map a finished transfer to a status code.
     * ok=1 short-circuits to JC_OK regardless of the other flags. */
    JC_CHECK(jc_http_classify(1, 0, 0, 0) == JC_OK);
    JC_CHECK(jc_http_classify(1, 1, 1, 1) == JC_OK);

    /* An abort wins over everything else. */
    JC_CHECK(jc_http_classify(0, 1, 0, 0) == JC_ERR_ABORTED);

    /* jc_http_conn_reusable (M326v): keep the pooled connection unless a
     * transfer failed AFTER one was established.
     *
     * The defect this encodes: `reusable = (rc == CURLE_OK)` dropped the warm
     * handle on a CONNECT timeout too, so the retry had to handshake cold --
     * repeating the operation that had just timed out. 2,437 of 17,365 model
     * calls in one measured workload took that path. */
    JC_CHECK(jc_http_conn_reusable(1, 1) == 1);  /* ok, connected      -> keep  */
    JC_CHECK(jc_http_conn_reusable(1, 0) == 1);  /* ok, no connect info-> keep  */
    JC_CHECK(jc_http_conn_reusable(0, 0) == 1);  /* THE FIX: failed before
                                                  * connecting        -> keep  */
    JC_CHECK(jc_http_conn_reusable(0, 1) == 0);  /* failed after connect-> drop */
    JC_CHECK(jc_http_classify(0, 1, 1, 1) == JC_ERR_ABORTED);

    /* A timeout AFTER a connection was established is a stall. */
    JC_CHECK(jc_http_classify(0, 0, 1, 1) == JC_ERR_TIMEOUT);

    /* A timeout with no connection (connect-phase) is a transport error,
     * not a stall -- the server was unreachable, a "network blip". */
    JC_CHECK(jc_http_classify(0, 0, 1, 0) == JC_ERR_HTTP);

    /* Any other failure (refused, DNS, reset) is a transport error. */
    JC_CHECK(jc_http_classify(0, 0, 0, 1) == JC_ERR_HTTP);
    JC_CHECK(jc_http_classify(0, 0, 0, 0) == JC_ERR_HTTP);

    /* The status string is distinct + descriptive. */
    JC_CHECK_STR(jc_status_str(JC_ERR_TIMEOUT), "model stalled (timed out)");

    /* --- SSRF classification (M131) --- */

    /* IPv4: loopback / private / link-local / reserved are blocked; a public
     * address is allowed. */
    JC_CHECK(jc_net_ipv4_blocked(127, 0, 0, 1) == 1);
    JC_CHECK(jc_net_ipv4_blocked(10, 1, 2, 3) == 1);
    JC_CHECK(jc_net_ipv4_blocked(172, 16, 0, 1) == 1);
    JC_CHECK(jc_net_ipv4_blocked(172, 31, 255, 255) == 1);
    JC_CHECK(jc_net_ipv4_blocked(172, 15, 0, 1) == 0); /* just below /12 */
    JC_CHECK(jc_net_ipv4_blocked(192, 168, 0, 1) == 1);
    JC_CHECK(jc_net_ipv4_blocked(169, 254, 169, 254) == 1); /* cloud metadata */
    JC_CHECK(jc_net_ipv4_blocked(0, 0, 0, 0) == 1);
    JC_CHECK(jc_net_ipv4_blocked(100, 64, 0, 1) == 1); /* CGNAT */
    JC_CHECK(jc_net_ipv4_blocked(8, 8, 8, 8) == 0);
    JC_CHECK(jc_net_ipv4_blocked(1, 1, 1, 1) == 0);

    /* IPv6: ::1, ::, link-local, ULA, and v4-mapped private are blocked. */
    {
        unsigned char loop[16]; unsigned char ll[16];
        unsigned char ula[16]; unsigned char mapped[16];
        unsigned char pub6[16];
        int i;
        for (i = 0; i < 16; i++) { loop[i]=0; ll[i]=0; ula[i]=0; mapped[i]=0; pub6[i]=0; }
        loop[15] = 1;                       /* ::1 */
        ll[0] = 0xfe; ll[1] = 0x80;         /* fe80:: */
        ula[0] = 0xfd;                      /* fd00:: */
        mapped[10]=0xff; mapped[11]=0xff; mapped[12]=127; mapped[15]=1; /* ::ffff:127.0.0.1 */
        pub6[0] = 0x20; pub6[1] = 0x01;     /* 2001:: (public-ish) */
        JC_CHECK(jc_net_ipv6_blocked(loop) == 1);
        JC_CHECK(jc_net_ipv6_blocked(ll) == 1);
        JC_CHECK(jc_net_ipv6_blocked(ula) == 1);
        JC_CHECK(jc_net_ipv6_blocked(mapped) == 1);
        JC_CHECK(jc_net_ipv6_blocked(pub6) == 0);
    }

    /* Host string classification: names + literals. */
    JC_CHECK(jc_net_host_is_blocked("localhost") == 1);
    JC_CHECK(jc_net_host_is_blocked("app.localhost") == 1);
    JC_CHECK(jc_net_host_is_blocked("metadata.google.internal") == 1);
    JC_CHECK(jc_net_host_is_blocked("127.0.0.1") == 1);
    JC_CHECK(jc_net_host_is_blocked("169.254.169.254") == 1);
    JC_CHECK(jc_net_host_is_blocked("example.com") == 0);
    JC_CHECK(jc_net_host_is_blocked("") == 1);

    /* Host extraction from a URL: scheme, userinfo, port, IPv6 brackets. */
    {
        char h[256];
        JC_CHECK(jc_url_host("http://example.com/path", h, sizeof(h)) == JC_OK);
        JC_CHECK_STR(h, "example.com");
        JC_CHECK(jc_url_host("https://user:pw@10.0.0.5:8080/x", h, sizeof(h)) == JC_OK);
        JC_CHECK_STR(h, "10.0.0.5");
        JC_CHECK(jc_url_host("http://[::1]:9000/", h, sizeof(h)) == JC_OK);
        JC_CHECK_STR(h, "::1");
        JC_CHECK(jc_url_host("http://169.254.169.254/latest/meta-data", h, sizeof(h)) == JC_OK);
        JC_CHECK(jc_net_host_is_blocked(h) == 1);
        JC_CHECK(jc_url_host("not-a-url", h, sizeof(h)) == JC_ERR_INVALID);
    }
}

/* M321: a transport failure must name the knob that caused it.
 *
 * From a 34,216-event workload log: 2,402 model calls (15%) failed with nothing
 * recorded but `status: 0`, their latencies inside 2 ms of exactly 10 s -- the
 * default connect timeout. The operator had raised `timeouts.stall`, because
 * that is the knob they had heard of and nothing pointed at the other one.
 * curl_easy_strerror cannot help: for both timeouts it says "Timeout was
 * reached". jichi CAN tell them apart, because it already asks
 * CURLINFO_CONNECT_TIME -- it just never said so. */
void test_http_describe_failure(void)
{
    char b[256];

    /* Timed out BEFORE connecting: the request was never sent, and the message
     * must say `timeouts.connect` and the limit that fired. */
    b[0] = 'x';
    jc_http_describe_failure("Timeout was reached", 1, 0, 10L, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.connect") != NULL);
    JC_CHECK(strstr(b, "10s") != NULL);
    JC_CHECK(strstr(b, "never sent") != NULL);
    /* and it must NOT blame the stall knob, which is the whole point */
    JC_CHECK(strstr(b, "timeouts.stall") == NULL);

    /* Timed out AFTER connecting: a stalled stream, the other knob. */
    jc_http_describe_failure("Timeout was reached", 1, 1, 10L, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.stall") != NULL);
    JC_CHECK(strstr(b, "stalled") != NULL);
    JC_CHECK(strstr(b, "timeouts.connect") == NULL);

    /* No connect limit configured: still names the knob, without a bogus "0s". */
    jc_http_describe_failure("Timeout was reached", 1, 0, 0L, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.connect") != NULL);
    JC_CHECK(strstr(b, "0s") == NULL);

    /* A non-timeout failure keeps curl's own words, plus whether the connection
     * had been established -- which is what separates "endpoint refused us" from
     * "endpoint dropped us mid-answer". */
    jc_http_describe_failure("Recv failure: Connection reset by peer", 0, 1, 10L,
                             b, sizeof(b));
    JC_CHECK(strstr(b, "Connection reset") != NULL);
    JC_CHECK(strstr(b, "after connecting") != NULL);
    jc_http_describe_failure("Couldn't connect to server", 0, 0, 10L, b,
                             sizeof(b));
    JC_CHECK(strstr(b, "before connecting") != NULL);

    /* Degenerate inputs must not crash or write past the buffer. */
    jc_http_describe_failure(NULL, 0, 0, 10L, b, sizeof(b));
    JC_CHECK(b[0] != '\0');
    jc_http_describe_failure("", 1, 0, 10L, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.connect") != NULL);
    jc_http_describe_failure("x", 1, 0, 10L, NULL, 0);   /* must not crash */
    b[0] = 'Z';
    jc_http_describe_failure("x", 1, 0, 10L, b, 0);      /* cap 0: untouched */
    JC_CHECK(b[0] == 'Z');
    {
        char tiny[8];
        jc_http_describe_failure("Timeout was reached", 1, 0, 10L, tiny,
                                 sizeof(tiny));
        JC_CHECK(strlen(tiny) < sizeof(tiny));           /* NUL-terminated */
    }
}

/* M472: a 3xx that was deliberately NOT followed has to explain itself. The
 * caller sees a successful transfer with a redirect status, so none of the
 * failure text above applies, and a bare "HTTP 302" would send the operator
 * looking for the switch that makes the client follow it -- which is the one
 * move that hands the key to the redirect target. So the message must name the
 * target AND the reason. */
void test_http_describe_redirect(void)
{
    char b[512];

    jc_http_describe_redirect(302, "http://elsewhere.example/v1", b, sizeof(b));
    JC_CHECK(strstr(b, "302") != NULL);
    JC_CHECK(strstr(b, "http://elsewhere.example/v1") != NULL);
    JC_CHECK(strstr(b, "not followed") != NULL);
    /* The reason, in the words that stop the operator from "fixing" it: */
    JC_CHECK(strstr(b, "credentials") != NULL);

    /* Every redirect status, not just 302 -- 307/308 preserve the method and
     * are the ones a gateway is most likely to emit. */
    jc_http_describe_redirect(307, "https://x.example/", b, sizeof(b));
    JC_CHECK(strstr(b, "307") != NULL);
    JC_CHECK(strstr(b, "https://x.example/") != NULL);

    /* curl reports no CURLINFO_REDIRECT_URL when it could not parse one; the
     * message still has to be useful rather than saying "(null)". */
    jc_http_describe_redirect(302, NULL, b, sizeof(b));
    JC_CHECK(strstr(b, "302") != NULL);
    JC_CHECK(strstr(b, "null") == NULL);
    JC_CHECK(strstr(b, "no usable Location") != NULL);
    jc_http_describe_redirect(302, "", b, sizeof(b));
    JC_CHECK(strstr(b, "no usable Location") != NULL);

    /* Degenerate inputs must not crash or write past the buffer -- same
     * contract as describe_failure above. */
    jc_http_describe_redirect(302, "http://x/", NULL, 0);   /* must not crash */
    b[0] = 'Z';
    jc_http_describe_redirect(302, "http://x/", b, 0);      /* cap 0: untouched */
    JC_CHECK(b[0] == 'Z');
    {
        char tiny[8];
        jc_http_describe_redirect(302, "http://a-very-long-host.example/path",
                                  tiny, sizeof(tiny));
        JC_CHECK(strlen(tiny) < sizeof(tiny));             /* NUL-terminated */
    }
}
