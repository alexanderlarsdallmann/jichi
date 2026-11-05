/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_http.c - the pure pieces of the HTTP layer (no network). */

#include "jc_test.h"
#include "jc_http.h"

#include <string.h>
#ifndef JC_NO_SOCKETS
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>   /* inet_pton: the oracle for jc_net_parse_ipv4 (M835) */
#define TG_INET  AF_INET
#define TG_INET6 AF_INET6
#else
/* M1124: the no-socket lint compiles this file with -DJC_NO_SOCKETS; the guard
 * test names the families by number there, as jc_http.c does for them. */
#define TG_INET  2
#define TG_INET6 10
#endif

/* C-08 (M1124): the redirect guard's exported decision + reset, defined in
 * src/net/jc_http.c (which this file links directly, like the other pure
 * predicates in that file). jc_http.h does not declare them (the header is
 * not this task's to edit); the prototypes mirror the file-local forward
 * declarations there. Pure, so no-curl builds link them. */
int  jc_http_redirect_guard_decide(int hop, int fam, const unsigned char *addr,
                                   unsigned int port);
void jc_http_redirect_guard_reset(void);

void test_http(void)
{
    /* jc_http_classify (M22c): map a finished transfer to a status code.
     * ok=1 short-circuits to JC_OK regardless of the other flags. */
    JC_CHECK(jc_http_classify(1, 0, 0, 0) == JC_OK);
    JC_CHECK(jc_http_classify(1, 1, 1, 1) == JC_OK);

    /* An abort wins over everything else. */
    JC_CHECK(jc_http_classify(0, 1, 0, 0) == JC_ERR_ABORTED);

    /* jc_http_conn_reusable (M326v, M1062): keep the pooled connection unless a
     * transfer failed AFTER one was established -- whether it was a fresh dial
     * (connected) or a reused keep-alive handle (reused; libcurl leaves
     * CONNECT_TIME at 0 on a pooled connection, the M880 note).
     *
     * The defect M326v encodes: `reusable = (rc == CURLE_OK)` dropped the warm
     * handle on a CONNECT timeout too, so the retry had to handshake cold --
     * repeating the operation that had just timed out. 2,437 of 17,365 model
     * calls in one measured workload took that path. M1062 is the mirror: a
     * mid-transfer death on a REUSED connection was KEPT, letting the retry
     * ride the dead socket. */
    JC_CHECK(jc_http_conn_reusable(1, 1, 1) == 1);  /* ok, connected, reused  -> keep */
    JC_CHECK(jc_http_conn_reusable(1, 0, 0) == 1);  /* ok, no connect info    -> keep */
    JC_CHECK(jc_http_conn_reusable(0, 0, 0) == 1);  /* THE M326v FIX: failed before
                                                    * connecting (cold dial)   -> keep */
    JC_CHECK(jc_http_conn_reusable(0, 1, 0) == 0);  /* failed after a FRESH connect -> drop */
    JC_CHECK(jc_http_conn_reusable(0, 0, 1) == 0);  /* M1062: reused mid-transfer death
                                                    * (CONNECT_TIME is 0)       -> drop */
    JC_CHECK(jc_http_conn_reusable(0, 1, 1) == 0);  /* M1062: both signals agree (died
                                                    * mid-transfer, reused)      -> drop */
    JC_CHECK(jc_http_classify(0, 1, 1, 1) == JC_ERR_ABORTED);

    /* jc_http_body_ctx_overflow (M854, N24c): the body of a context-window 400.
     * The first is the demos drive's gateway (LiteLLM), as measured; the others
     * are the OpenAI-compatible spellings. A malformed request's 400 is NOT one. */
    {
        const char *b1 = "{\"error\":{\"message\":\"litellm.ContextWindowExceededError: "
                         "litellm.BadRequestError\"}}";
        const char *b2 = "This model's maximum context length is 8192 tokens.";
        const char *b3 = "{\"code\":\"context_length_exceeded\"}";
        const char *b4 = "{\"error\":\"invalid tool schema: missing type\"}";
        JC_CHECK(jc_http_body_ctx_overflow(b1, strlen(b1)) == 1);
        JC_CHECK(jc_http_body_ctx_overflow(b2, strlen(b2)) == 1);
        JC_CHECK(jc_http_body_ctx_overflow(b3, strlen(b3)) == 1);
        JC_CHECK(jc_http_body_ctx_overflow(b4, strlen(b4)) == 0);
        JC_CHECK(jc_http_body_ctx_overflow(NULL, 10) == 0);
        /* bounded by len: the mark lies past it */
        JC_CHECK(jc_http_body_ctx_overflow(b1, 20) == 0);
    }

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

    /* --- M1039: jc_http_fail_class (M22c, M321, M877, M-L2, M909) --- */
    /* http_status >= 400 -> "http-status" (the server spoke) */
    JC_CHECK_STR(jc_http_fail_class(0, 500, 0, 0, 0, 0, 0), "http-status");
    JC_CHECK_STR(jc_http_fail_class(0, 401, 0, 0, 0, 0, 0), "http-status");
    /* http_status > 0 and < 400 -> "provider-error" (M909: error in 2xx stream) */
    JC_CHECK_STR(jc_http_fail_class(0, 200, 0, 0, 0, 0, 0), "provider-error");
    /* aborted -> "aborted" (result stays the only place that says jichi abort) */
    JC_CHECK_STR(jc_http_fail_class(0, 0, 1, 0, 0, 0, 0), "aborted");
    /* timed_out && !connected && handshaking -> "timeout-tls" (M877) */
    JC_CHECK_STR(jc_http_fail_class(28, 0, 0, 1, 0, 1, 0), "timeout-tls");
    /* timed_out && !connected -> "timeout-connect" */
    JC_CHECK_STR(jc_http_fail_class(28, 0, 0, 1, 0, 0, 0), "timeout-connect");
    /* timed_out && connected && !handshaking -> "timeout-stall" (M-L2) */
    JC_CHECK_STR(jc_http_fail_class(28, 0, 0, 1, 1, 0, 0), "timeout-stall");
    /* timed_out && connected && !handshaking && fence_only -> "timeout-stall" */
    JC_CHECK_STR(jc_http_fail_class(28, 0, 0, 1, 1, 0, 1), "timeout-stall");
    /* curl_err == 6 (CURLE_COULDNT_RESOLVE_HOST) -> "dns" */
    JC_CHECK_STR(jc_http_fail_class(6, 0, 0, 0, 0, 0, 0), "dns");
    /* curl_err == 7 (CURLE_COULDNT_CONNECT) -> "refused" */
    JC_CHECK_STR(jc_http_fail_class(7, 0, 0, 0, 0, 0, 0), "refused");
    /* curl_err == 56 (CURLE_RECV_ERROR) -> "connect" (never connected, not a timeout) */
    JC_CHECK_STR(jc_http_fail_class(56, 0, 0, 0, 0, 0, 0), "connect");
    /* timed_out wins over curl_err code: timeout-connect (no connection was established) */
    JC_CHECK_STR(jc_http_fail_class(56, 0, 0, 1, 0, 0, 0), "timeout-connect");

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
        /* pub6: 2606:4700:4700::1111, a genuine public address.  It used to be
         * 2001:: ("public-ish"), which the C-11 fix turns into the Teredo
         * prefix -- so the positive control must move to a real public range. */
        pub6[0]=0x26; pub6[1]=0x06;
        pub6[2]=0x47; pub6[3]=0x00;
        pub6[4]=0x47; pub6[5]=0x00;
        pub6[14]=0x11; pub6[15]=0x11;
        JC_CHECK(jc_net_ipv6_blocked(loop) == 1);
        JC_CHECK(jc_net_ipv6_blocked(ll) == 1);
        JC_CHECK(jc_net_ipv6_blocked(ula) == 1);
        JC_CHECK(jc_net_ipv6_blocked(mapped) == 1);
        JC_CHECK(jc_net_ipv6_blocked(pub6) == 0);
    }
    /* C-11 (M1123): the reserved/special-purpose IPv6 ranges that the classifier
     * used to let through.  The translation ranges (NAT64, 6to4, IPv4-compatible)
     * embed an IPv4 address -- the embedded v4 is classified, so the
     * private-embedded rows are refused; site-local and multicast are refused
     * outright.  Teredo (2001::/32) is refused as a whole (M1123-f1): the
     * embedded client address is stored XOR-0xFFFFFFFF-obfuscated, so classifying
     * the raw bytes fails open (a private client reads as public).  Both teredo
     * rows are therefore refused by the prefix, whatever the client shape. */
    {
        unsigned char nat64_loop[16];   /* 64:ff9b::7f00:1      (NAT64, 127.0.0.1) */
        unsigned char nat64_priv[16];   /* 64:ff9b::a00:1       (NAT64, 10.0.0.1)  */
        unsigned char nat64_localuse[16]; /* 64:ff9b:1::7f00:1  (NAT64 local-use)  */
        unsigned char sixto4_priv[16];  /* 2002:0a00:0001::     (6to4, 10.0.0.1)   */
        unsigned char sitelocal[16];    /* fec0::1              (site-local)       */
        unsigned char multicast[16];    /* ff02::1              (multicast)        */
        unsigned char v4compat_priv[16];/* ::a00:1              (v4-compat, 10.0.0.1) */
        unsigned char teredo_loop[16];  /* 2001:0000:8000::...  (Teredo, 2001::/32 prefix) */
        unsigned char teredo_pub[16];   /* 2001:0000:8000:0000:0000:0000:f7f7:f7f7
                                          (Teredo, obfuscated client 8.8.8.8)          */
        unsigned char teredo_neg_dns[16];   /* 2001:4860:4860::8888 (Google public DNS)   */
        unsigned char teredo_neg_outside_a[16]; /* 2001:1::1  (just outside the /32)      */
        unsigned char teredo_neg_outside_b[16]; /* 2001:100::1 (just outside the /32)     */
        unsigned char teredo_neg_doc[16];   /* 2001:db8::1 (RFC 3849 documentation range) */
        unsigned char teredo_edge_low[16];  /* 2001:0:0:0::1 (the /32's low edge)         */
        unsigned char teredo_edge_high[16]; /* 2001:0:ffff:...:ffff (the /32's high edge) */
        int k;
        unsigned char *arr[15];
        arr[0]=nat64_loop;   arr[1]=nat64_priv;   arr[2]=nat64_localuse;
        arr[3]=sixto4_priv;  arr[4]=sitelocal;    arr[5]=multicast;
        arr[6]=v4compat_priv;arr[7]=teredo_loop;  arr[8]=teredo_pub;
        arr[9]=teredo_neg_dns;  arr[10]=teredo_neg_outside_a;
        arr[11]=teredo_neg_outside_b; arr[12]=teredo_neg_doc;
        arr[13]=teredo_edge_low; arr[14]=teredo_edge_high;
        for (k = 0; k < 15; k++) { int i; for (i = 0; i < 16; i++) arr[k][i] = 0; }
        nat64_loop[0]=0x64; nat64_loop[1]=0xff; nat64_loop[2]=0x9b; nat64_loop[3]=0x00;
        nat64_loop[12]=127; nat64_loop[15]=1;               /* embedded 127.0.0.1 */
        nat64_priv[0]=0x64; nat64_priv[1]=0xff; nat64_priv[2]=0x9b; nat64_priv[3]=0x00;
        nat64_priv[12]=10; nat64_priv[15]=1;                /* embedded 10.0.0.1  */
        nat64_localuse[0]=0x64; nat64_localuse[1]=0xff; nat64_localuse[2]=0x9b;
        nat64_localuse[3]=0x01; nat64_localuse[4]=0x00;
        nat64_localuse[12]=127; nat64_localuse[15]=1;       /* embedded 127.0.0.1 */
        sixto4_priv[0]=0x20; sixto4_priv[1]=0x02;
        sixto4_priv[2]=10; sixto4_priv[5]=1;                /* embedded 10.0.0.1  */
        sitelocal[0]=0xfe; sitelocal[1]=0xc0; sitelocal[15]=1; /* fec0::1         */
        multicast[0]=0xff; multicast[1]=0x02; multicast[15]=1;  /* ff02::1       */
        v4compat_priv[12]=10; v4compat_priv[15]=1;           /* ::10.0.0.1        */
        /* teredo_loop: refused by the 2001::/32 prefix (M1123-f1), not by any
         * "client" read -- the bytes below sit where the pre-f1 code read the
         * client (offset 8, wrong per RFC 4380) and made this row pass for
         * the wrong reason.  Expectation == 1 is unchanged by the fix. */
        teredo_loop[0]=0x20; teredo_loop[1]=0x01;
        teredo_loop[8]=127; teredo_loop[11]=1;               /* pre-f1: misread as 127.0.0.1 */
        /* teredo_pub: a GENUINE public-client Teredo address in the RFC 4380
         * layout -- bytes 0-1 the 2001::/32 prefix, bytes 8-9 the cone-server
         * flag (0x80 0x00), and the client in its CORRECT offset, bytes 12-15,
         * stored XOR-0xFFFFFFFF-obfuscated: 8.8.8.8 ^ 0xFFFFFFFF = f7 f7 f7 f7.
         * A public client is still refused -- the prefix is a blanket refuse
         * (M1123-f1).  RED-first: pre-f1 the code reads bytes 8-11
         * (0x80 0x00 0xF7 0xF7 = 128.0.247.247) and ALLOWS it, so == 1 fails
         * on the committed (unfixed) tree; post-f1 == 1 holds by the prefix. */
        teredo_pub[0]=0x20; teredo_pub[1]=0x01;
        teredo_pub[8]=0x80; teredo_pub[9]=0x00;
        teredo_pub[12]=0xF7; teredo_pub[13]=0xF7;
        teredo_pub[14]=0xF7; teredo_pub[15]=0xF7; /* client 8.8.8.8 obfuscated */
        /* M1123-f2 (SV:1117): the refuse-all Teredo test must match the /32
         * (bytes 0-3 = 20 01 00 00), not the /16 it claimed -- the /16
         * over-refused 2001:4860::/32 (Google public DNS) and every other
         * 2001:xx address. The negatives are what the /16 was wrongly
         * refusing; the two edge positives pin the /32's exact extent. */
        teredo_neg_dns[0]=0x20; teredo_neg_dns[1]=0x01;
        teredo_neg_dns[2]=0x48; teredo_neg_dns[3]=0x60;
        teredo_neg_dns[4]=0x48; teredo_neg_dns[5]=0x60;
        teredo_neg_dns[14]=0x08; teredo_neg_dns[15]=0x88; /* 2001:4860:4860::8888 */
        teredo_neg_outside_a[0]=0x20; teredo_neg_outside_a[1]=0x01;
        teredo_neg_outside_a[3]=0x01;
        teredo_neg_outside_a[15]=0x01;                      /* 2001:1::1          */
        teredo_neg_outside_b[0]=0x20; teredo_neg_outside_b[1]=0x01;
        teredo_neg_outside_b[2]=0x01; teredo_neg_outside_b[3]=0x00;
        teredo_neg_outside_b[15]=0x01;                      /* 2001:100::1        */
        teredo_neg_doc[0]=0x20; teredo_neg_doc[1]=0x01;
        teredo_neg_doc[2]=0x0d; teredo_neg_doc[3]=0xb8;
        teredo_neg_doc[15]=0x01;                            /* 2001:db8::1        */
        /* teredo_edge_low: 2001:0:0:0::1 -- the /32's low edge, inside it. */
        teredo_edge_low[0]=0x20; teredo_edge_low[1]=0x01;
        teredo_edge_low[15]=0x01;
        /* teredo_edge_high: 2001:0:ffff:ffff:ffff:ffff:ffff:ffff -- the /32's
         * high edge, still inside the /32 (bytes 2-3 remain 00 00). */
        teredo_edge_high[0]=0x20; teredo_edge_high[1]=0x01;
        {
            int e;
            for (e = 4; e < 16; e++) {
                teredo_edge_high[e] = 0xff;
            }
        }
        JC_CHECK(jc_net_ipv6_blocked(nat64_loop) == 1);
        JC_CHECK(jc_net_ipv6_blocked(nat64_priv) == 1);
        JC_CHECK(jc_net_ipv6_blocked(nat64_localuse) == 1);
        JC_CHECK(jc_net_ipv6_blocked(sixto4_priv) == 1);
        JC_CHECK(jc_net_ipv6_blocked(sitelocal) == 1);
        JC_CHECK(jc_net_ipv6_blocked(multicast) == 1);
        JC_CHECK(jc_net_ipv6_blocked(v4compat_priv) == 1);
        JC_CHECK(jc_net_ipv6_blocked(teredo_loop) == 1);
        JC_CHECK(jc_net_ipv6_blocked(teredo_pub) == 1);
        /* M1123-f2 (SV:1117) negatives: allowed by the /32, wrongly refused by
         * the /16 (RED on the unfixed tree). */
        JC_CHECK(jc_net_ipv6_blocked(teredo_neg_dns) == 0);   /* 2001:4860:4860::8888 */
        JC_CHECK(jc_net_ipv6_blocked(teredo_neg_outside_a) == 0); /* 2001:1::1 */
        JC_CHECK(jc_net_ipv6_blocked(teredo_neg_outside_b) == 0); /* 2001:100::1 */
        /* RFC 3849 documentation range: allowed. The classifier is an SSRF
         * guard by address range, not an IANA special-purpose registry --
         * 2001:db8::/32 is not a private read target, so allowing it is the
         * intended behaviour, recorded here so a future "hardening" does not
         * add it by accident without saying so. */
        JC_CHECK(jc_net_ipv6_blocked(teredo_neg_doc) == 0);   /* 2001:db8::1 */
        /* M1123-f2 (SV:1117) edge positives: both edges of the /32 are inside
         * it and must be refused (GREEN both ways -- they pin the /32's extent). */
        JC_CHECK(jc_net_ipv6_blocked(teredo_edge_low) == 1);  /* 2001:0:0:0::1 */
        JC_CHECK(jc_net_ipv6_blocked(teredo_edge_high) == 1); /* 2001:0:ffff:...:ffff */
    }

    /* Host string classification: names + literals. */
    JC_CHECK(jc_net_host_is_blocked("localhost") == 1);
    JC_CHECK(jc_net_host_is_blocked("app.localhost") == 1);
    JC_CHECK(jc_net_host_is_blocked("metadata.google.internal") == 1);
    JC_CHECK(jc_net_host_is_blocked("127.0.0.1") == 1);
    JC_CHECK(jc_net_host_is_blocked("169.254.169.254") == 1);
    JC_CHECK(jc_net_host_is_blocked("example.com") == 0);
    JC_CHECK(jc_net_host_is_blocked("") == 1);

    /* M1132 (C-14): the loopback-ONLY classifier the doctor's plaintext check
     * uses to decide the exemption.  It must agree with jc_net_host_is_blocked
     * on loopback (the names, 127/8, ::1) but DIVERGE on the private ranges:
     * a private address is NOT loopback, so the doctor warns about it. */
    JC_CHECK(jc_net_host_is_loopback("127.0.0.1") == 1);
    JC_CHECK(jc_net_host_is_loopback("127.255.0.1") == 1);   /* any 127/8 */
    JC_CHECK(jc_net_host_is_loopback("localhost") == 1);
    JC_CHECK(jc_net_host_is_loopback("localhost.localdomain") == 1);
    JC_CHECK(jc_net_host_is_loopback("app.localhost") == 1);
    JC_CHECK(jc_net_host_is_loopback("::1") == 1);
    JC_CHECK(jc_net_host_is_loopback("") == 1);
    JC_CHECK(jc_net_host_is_loopback(NULL) == 1);
    /* Private ranges are NOT loopback: the doctor must warn about these. */
    JC_CHECK(jc_net_host_is_loopback("10.0.0.5") == 0);
    JC_CHECK(jc_net_host_is_loopback("192.168.1.10") == 0);
    JC_CHECK(jc_net_host_is_loopback("172.16.4.9") == 0);
    JC_CHECK(jc_net_host_is_loopback("100.64.128.1") == 0);   /* CGNAT */
    JC_CHECK(jc_net_host_is_loopback("169.254.169.254") == 0); /* link-local */
    /* Public and non-loopback names are not loopback either. */
    JC_CHECK(jc_net_host_is_loopback("8.8.8.8") == 0);
    JC_CHECK(jc_net_host_is_loopback("example.com") == 0);
    JC_CHECK(jc_net_host_is_loopback("fd00::1") == 0);        /* ULA, not ::1 */
    JC_CHECK(jc_net_host_is_loopback("fe80::1") == 0);        /* link-local v6 */
    /* Regression: the SSRF guard is untouched -- private is still blocked. */
    JC_CHECK(jc_net_host_is_blocked("10.0.0.5") == 1);
    JC_CHECK(jc_net_host_is_blocked("192.168.1.10") == 1);

    /* M835: jc_net_parse_ipv4, the classifier's IPv4 reader where there is no
     * socket library. Known answers run on every row; the differential half holds
     * it equal to inet_pton(AF_INET) -- same accept/reject AND same bytes -- over
     * a corpus of the edge cases, wherever inet_pton exists to be the oracle. */
    {
        static const char *const corpus[] = {
            "0.0.0.0", "1.2.3.4", "127.0.0.1", "255.255.255.255", "10.0.0.1",
            "192.168.1.1", "8.8.8.8", "100.64.0.1", "169.254.169.254", "9.19.199.249",
            "", "1", "1.2", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.256.1.1",
            "1.1.1.256", "01.2.3.4", "1.02.3.4", "1.2.3.04", "00.0.0.0", "1..2.3",
            ".1.2.3", "1.2.3.", "1.2.3.4 ", " 1.2.3.4", "1.2.3.-1", "1.2.3.a",
            "0x7f.0.0.1", "1234.1.1.1", "1.2.3.4/8", "::1", "localhost",
            "999.999.999.999", "4294967295", "1.2.3.4.", "+1.2.3.4", NULL
        };
        unsigned char a[4];
        int i;
        JC_CHECK(jc_net_parse_ipv4("10.1.2.3", a) == 1);
        JC_CHECK(a[0] == 10 && a[1] == 1 && a[2] == 2 && a[3] == 3);
        JC_CHECK(jc_net_parse_ipv4("255.0.255.0", a) == 1 && a[0] == 255 && a[3] == 0);
        JC_CHECK(jc_net_parse_ipv4("01.1.1.1", a) == 0);   /* leading zero */
        JC_CHECK(jc_net_parse_ipv4("1.1.1.256", a) == 0);  /* out of range */
        JC_CHECK(jc_net_parse_ipv4("1.1.1", a) == 0);      /* three octets */
        JC_CHECK(jc_net_parse_ipv4("1.1.1.1.1", a) == 0);  /* five */
        JC_CHECK(jc_net_parse_ipv4(NULL, a) == 0);
#ifndef JC_NO_SOCKETS
        for (i = 0; corpus[i] != NULL; i++) {
            unsigned char b[4];
            int ours = jc_net_parse_ipv4(corpus[i], a);
            int libc = (inet_pton(AF_INET, corpus[i], b) == 1);
            JC_CHECK(ours == libc);
            if (ours && libc) {
                JC_CHECK(memcmp(a, b, 4) == 0);
            }
        }
#else
        for (i = 0; corpus[i] != NULL; i++) {
            (void)jc_net_parse_ipv4(corpus[i], a);  /* no oracle here: must not crash */
        }
#endif
    }

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
    jc_http_describe_failure("Timeout was reached", 1, 0, 0, 10L, 0, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.connect") != NULL);
    JC_CHECK(strstr(b, "10s") != NULL);
    JC_CHECK(strstr(b, "never sent") != NULL);
    /* and it must NOT blame the stall knob, which is the whole point */
    JC_CHECK(strstr(b, "timeouts.stall") == NULL);

    /* Timed out AFTER connecting, stall cap in force: a stalled stream. */
    jc_http_describe_failure("Timeout was reached", 1, 1, 0, 10L, 0, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.stall") != NULL);
    JC_CHECK(strstr(b, "stalled") != NULL);
    JC_CHECK(strstr(b, "timeouts.connect") == NULL);

    /* M-L2: timed out AFTER connecting with the stall cap OFF -- the dead-stream
     * FENCE fired, so name deadStream, not the stall knob the operator never set. */
    jc_http_describe_failure("Timeout was reached", 1, 1, 0, 10L, 1, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.deadStream") != NULL);
    JC_CHECK(strstr(b, "went dead") != NULL);
    JC_CHECK(strstr(b, "timeouts.stall") == NULL);

    /* No connect limit configured: still names the knob, without a bogus "0s". */
    jc_http_describe_failure("Timeout was reached", 1, 0, 0, 0L, 0, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.connect") != NULL);
    JC_CHECK(strstr(b, "0s") == NULL);

    /* A non-timeout failure keeps curl's own words, plus whether the connection
     * had been established -- which is what separates "endpoint refused us" from
     * "endpoint dropped us mid-answer". */
    jc_http_describe_failure("Recv failure: Connection reset by peer", 0, 1, 0, 10L,
                             0, b, sizeof(b));
    JC_CHECK(strstr(b, "Connection reset") != NULL);
    JC_CHECK(strstr(b, "after connecting") != NULL);
    jc_http_describe_failure("Couldn't connect to server", 0, 0, 0, 10L, 0, b,
                             sizeof(b));
    JC_CHECK(strstr(b, "before connecting") != NULL);

    /* Degenerate inputs must not crash or write past the buffer. */
    jc_http_describe_failure(NULL, 0, 0, 0, 10L, 0, b, sizeof(b));
    JC_CHECK(b[0] != '\0');
    jc_http_describe_failure("", 1, 0, 0, 10L, 0, b, sizeof(b));
    JC_CHECK(strstr(b, "timeouts.connect") != NULL);
    jc_http_describe_failure("x", 1, 0, 0, 10L, 0, NULL, 0);   /* must not crash */
    b[0] = 'Z';
    jc_http_describe_failure("x", 1, 0, 0, 10L, 0, b, 0);      /* cap 0: untouched */
    JC_CHECK(b[0] == 'Z');
    {
        char tiny[8];
        jc_http_describe_failure("Timeout was reached", 1, 0, 0, 10L, 0, tiny,
                                 sizeof(tiny));
        JC_CHECK(strlen(tiny) < sizeof(tiny));           /* NUL-terminated */
    }

    /* M877 (the demos drive's N47 (d)): TCP connected and the TLS handshake ran
     * out of the connect bound -- named as that, not as a stalled stream (what
     * jichi said, measured) nor as a dead endpoint. */
    jc_http_describe_failure("Timeout was reached", 1, 0, 1, 20L, 0, b, sizeof(b));
    JC_CHECK(strstr(b, "TCP connected, but the TLS handshake did not finish "
                       "within 20s") != NULL);
    JC_CHECK(strstr(b, "never sent") != NULL && strstr(b, "stall") == NULL);
    /* a connection that finished is unchanged: a timeout after it is a stall */
    jc_http_describe_failure("Timeout was reached", 1, 1, 0, 20L, 0, b, sizeof(b));
    JC_CHECK(strstr(b, "TLS handshake") == NULL && strstr(b, "stalled") != NULL);
    /* jc_http_in_handshake: https, TCP up, the handshake not done (M880: TCP up
     * is the socket watch's answer, an int, no longer CONNECT_TIME) */
    JC_CHECK(jc_http_in_handshake("https://gw.example/v1", 1, 0.0) == 1);
    JC_CHECK(jc_http_in_handshake("HTTPS://gw.example/v1", 1, 0.0) == 1);
    JC_CHECK(jc_http_in_handshake("https://gw.example/v1", 1, 20.8) == 0);
    JC_CHECK(jc_http_in_handshake("https://gw.example/v1", 0, 0.0) == 0);
    JC_CHECK(jc_http_in_handshake("http://127.0.0.1:1234/v1", 1, 0.0) == 0);
    JC_CHECK(jc_http_in_handshake(NULL, 1, 0.0) == 0);

    /* M880: the socket watch. libcurl 8.14 and later leave CONNECT_TIME at 0 on a
     * TLS handshake timeout, so whether TCP was up is what a socket of this
     * transfer said -- a peer, or none -- when libcurl closed it. */
    {
        struct jc_sockwatch w;
        int i;
        memset(&w, 0, sizeof(w));
        /* nothing closed yet: CONNECT_TIME decides, as before M880 */
        JC_CHECK(jc_sockwatch_tcp_up(&w, 0.0) == 0);
        JC_CHECK(jc_sockwatch_tcp_up(&w, 0.002) == 1);
        /* a socket this transfer did not open is not its evidence */
        jc_sockwatch_opened(&w, 7);
        jc_sockwatch_closing(&w, 9, 1);
        JC_CHECK(w.closed == 0 && jc_sockwatch_tcp_up(&w, 0.0) == 0);
        /* its own socket had a peer: TCP was up, whatever CONNECT_TIME says */
        jc_sockwatch_closing(&w, 7, 1);
        JC_CHECK(w.closed == 1 && jc_sockwatch_tcp_up(&w, 0.0) == 1);
        /* its own socket had none: the watch decides over CONNECT_TIME */
        memset(&w, 0, sizeof(w));
        jc_sockwatch_opened(&w, 7);
        jc_sockwatch_closing(&w, 7, 0);
        JC_CHECK(jc_sockwatch_tcp_up(&w, 0.002) == 0);
        /* two sockets (two address families), one connected */
        memset(&w, 0, sizeof(w));
        jc_sockwatch_opened(&w, 7);
        jc_sockwatch_opened(&w, 8);
        jc_sockwatch_closing(&w, 8, 0);
        jc_sockwatch_closing(&w, 7, 1);
        JC_CHECK(w.closed == 2 && w.connected == 1 && jc_sockwatch_tcp_up(&w, 0.0) == 1);
        /* a number reused within the transfer is each socket's own */
        memset(&w, 0, sizeof(w));
        jc_sockwatch_opened(&w, 7);
        jc_sockwatch_closing(&w, 7, 0);
        jc_sockwatch_opened(&w, 7);
        jc_sockwatch_closing(&w, 7, 1);
        JC_CHECK(w.closed == 2 && w.connected == 1 && w.nfds == 0);
        /* past its capacity the watch ignores a socket, and never overflows */
        memset(&w, 0, sizeof(w));
        for (i = 0; i < JC_SOCKWATCH_MAX + 3; i++) {
            jc_sockwatch_opened(&w, 10 + i);
        }
        JC_CHECK(w.nfds == JC_SOCKWATCH_MAX);
        /* no watch: nothing recorded, CONNECT_TIME decides */
        jc_sockwatch_opened(NULL, 7);
        jc_sockwatch_closing(NULL, 7, 1);
        JC_CHECK(jc_sockwatch_tcp_up(NULL, 0.002) == 1);
        JC_CHECK(jc_sockwatch_tcp_up(NULL, 0.0) == 0);
    }

    /* M1062-c corrected: jc_http_reused_conn -- the empty per-transfer watch
     * plus connect_time == 0 plus the request was SENT (the third input, bytes
     * sent) separates a REUSED transfer from the two cases that share the -b's
     * two inputs. connect_time == 0 alone cannot (a failed dial also gives 0);
     * the empty watch + connect_time == 0 cannot either (a COLD transfer whose
     * DNS resolve times out opens NO socket, so its watch is empty AND its
     * connect_time is 0 -- the -b's exact inputs -- yet nothing was sent). Only
     * "was the request actually uploaded" (CURLINFO_REQUEST_SIZE > 0) separates
     * the resolve timeout (sent 0) from the reused-then-dead transfer (sent > 0).
     * curl == g_reuse is a tautology (http_handle_acquire returns the single
     * persistent handle in every path), so it never carried a test. */
    {
        struct jc_sockwatch w;
        memset(&w, 0, sizeof(w));
        /* (a) a reused transfer inherited its socket from the pool and opened
         * none, so the watch stays empty (even after libcurl closes the pooled
         * socket, jc_sockwatch_closing counts only foreign fds), connect_time
         * is 0, and the request WAS uploaded (sent -> 1) -> 1. The case the
         * finding is about. */
        JC_CHECK(jc_http_reused_conn(&w, 0.0, 1) == 1);
        /* M1062-c: THE RED CELL -- a resolve timeout never got a socket, so the
         * watch is empty AND connect_time is 0 (the -b's two inputs), but
         * NOTHING was sent (sent 0) -> 0, NOT a reused connection. It is a kept,
         * pre-TCP cold dial that reads timeout-connect on the connect bound. On
         * the unfixed 2-arg predicate this same shape answers 1 (reused), so the
         * pre-fix row jc_http_reused_conn(&w, 0.0) == 0 failed -- the committed
         * red. */
        JC_CHECK(jc_http_reused_conn(&w, 0.0, 0) == 0);
        /* (b) a connected dial has a positive connect_time -> not reused,
         * whatever was sent. */
        JC_CHECK(jc_http_reused_conn(&w, 0.002, 1) == 0);
        /* (c) THE M326v regression tooth: a cold dial that FAILED pre-TCP opened
         * and libcurl closed a socket, so the watch is non-empty (closed >= 1)
         * -> 0, even though connect_time is 0 and nothing was sent. Must NOT
         * read as reused (the first attempt's connect_time == 0.0 alone let it
         * through). sent is 0 here -- the socket was closed before a connection,
         * so nothing was uploaded. */
        memset(&w, 0, sizeof(w));
        jc_sockwatch_opened(&w, 5);
        jc_sockwatch_closing(&w, 5, 0);   /* the socket had no peer: never connected */
        JC_CHECK(jc_http_reused_conn(&w, 0.0, 0) == 0);
        /* (d) non-empty watch + a positive connect_time -> 0. */
        JC_CHECK(jc_http_reused_conn(&w, 0.002, 1) == 0);
        /* NULL watch: a no-close-callback build has no per-transfer watch data, so
         * it cannot be asserted that nothing was opened -> 0 (conservative; keep
         * the old classification), whatever was sent. */
        JC_CHECK(jc_http_reused_conn(NULL, 0.0, 0) == 0);
    }

    /* C-15 (M1133): jc_net_curl_has_tls -- does a ssl_version string name a
     * real TLS backend? A TLS-free libcurl reports "No TLS", and the match is
     * case-insensitive containment, so any of "No TLS"/"no tls"/"NO TLS" is
     * no-TLS, and the "(none)" placeholder (jc_net_curl_snapshot's normalised
     * NULL) is no-TLS too. NULL and empty are no-TLS (the no-curl build and a
     * probe failure). A real backend names its library (OpenSSL/GnuTLS/...)
     * and has TLS. These rows are the unit teeth; the binary-level driver
     * (tests/smoke/libcurl_capability.sh) exercises the snapshot that feeds
     * them. */
    JC_CHECK(jc_net_curl_has_tls("OpenSSL/3.5.5") == 1);
    JC_CHECK(jc_net_curl_has_tls("GnuTLS/3.8.3") == 1);
    JC_CHECK(jc_net_curl_has_tls("SecureTransport") == 1);
    JC_CHECK(jc_net_curl_has_tls("No TLS") == 0);
    JC_CHECK(jc_net_curl_has_tls("no tls") == 0);
    JC_CHECK(jc_net_curl_has_tls("NO TLS") == 0);
    JC_CHECK(jc_net_curl_has_tls("") == 0);
    JC_CHECK(jc_net_curl_has_tls("(none)") == 0);
    JC_CHECK(jc_net_curl_has_tls(NULL) == 0);
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

/* M1124: the hop-aware redirect guard's decision, pinned through the measured
 * sequences (LOG-3, Run 0138). It drives the exported
 * jc_http_redirect_guard_decide, which consults the per-transfer (family,
 * address, port) set inside jc_http.c; jc_http_redirect_guard_reset gives each
 * scenario a fresh set. The `hop` argument is the 3xx STATUS-LINE count
 * (what redirect_hop_header maintains at runtime) -- 0 for the first connection,
 * >= 1 for a connection opened after a 3xx.
 *
 * The two C-08 false refusals (the old per-socket counter) and the two M1120
 * threats (the guard must still refuse) are the load-bearing rows. */
void test_http_redirect_guard_decision(void);

void test_http_redirect_guard_decision(void)
{
    unsigned char v4[4];
    unsigned char v6[16];
    int i;
    int allow;

    /* (a) dual-stack, no redirect: BOTH families at hop 0 are allowed (the user
     * named the host). The old counter refused the second socket (the dead ::1
     * attempt counted first) -- that is the false refusal this fixes. */
    jc_http_redirect_guard_reset();
    memset(v6, 0, 16); v6[15] = 1;                        /* ::1 */
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET6, v6, 80) == 1);
    v4[0]=127; v4[1]=0; v4[2]=0; v4[3]=1;                 /* 127.0.0.1 */
    allow = jc_http_redirect_guard_decide(0, TG_INET, v4, 80);
    JC_CHECK(allow == 1);   /* the host's own v4 is still hop 0: allowed */

    /* (b) self-redirect to the SAME host:port: hop 0 records, hop 1 (after a
     * 3xx) is in the set -> allowed. The old counter saw a second socket and
     * refused -- the other C-08 false refusal. */
    jc_http_redirect_guard_reset();
    v4[0]=127; v4[1]=0; v4[2]=0; v4[3]=1;
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 18080) == 1);
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 18080) == 1);

    /* (c) cross-port private (the M1120 threat, must be REFUSED): a different
     * port on the same private ip is a server-chosen address, not the named
     * feed. The (ip, port) key is what keeps 127.0.0.1:P's set-entry from
     * letting 127.0.0.1:Q through. A fresh private ip is refused too. */
    jc_http_redirect_guard_reset();
    v4[0]=127; v4[1]=0; v4[2]=0; v4[3]=1;
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 18080) == 1);
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 9999) == 0); /* Q != P */
    v4[0]=10; v4[1]=0; v4[2]=0; v4[3]=5;                   /* 10.0.0.5 */
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 80) == 0);

    /* (d) http->https on the same ip, different port (SV:1437, option a): the
     * documented limitation. The feed at ip:80 301s to ip:443; 443 is not in
     * the set (the key includes the port) and the ip is private -> REFUSED.
     * Not fixed here, pinned here so a future key change must re-decide it. */
    jc_http_redirect_guard_reset();
    v4[0]=10; v4[1]=1; v4[2]=2; v4[3]=3;                   /* 10.1.2.3 */
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 80) == 1);
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 443) == 0);

    /* (e) public redirect: hop 0 records a private host, hop 1 to a public
     * address is not in the set but is public -> allowed. */
    jc_http_redirect_guard_reset();
    v4[0]=127; v4[1]=0; v4[2]=0; v4[3]=1;
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 18080) == 1);
    v4[0]=8; v4[1]=8; v4[2]=8; v4[3]=8;                    /* 8.8.8.8 */
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 443) == 1);

    /* (f) v6 self-redirect: hop 0 records ::1:P, hop 1 is the same key ->
     * allowed (the set lookup, not the v4 classifier, is what decides). */
    jc_http_redirect_guard_reset();
    memset(v6, 0, 16); v6[15] = 1;                         /* ::1 */
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET6, v6, 8443) == 1);
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET6, v6, 8443) == 1);

    /* (g) v6 cross-port private (the (ip, port) key holds for v6 too): a
     * different port on the same private v6 host is refused. */
    jc_http_redirect_guard_reset();
    memset(v6, 0, 16); v6[15] = 1;
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET6, v6, 8443) == 1);
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET6, v6, 8080) == 0);

    /* (h) a public v6 hop-1 target is allowed even though the set is empty of
     * it (the classifier, not the set, is what decides). */
    jc_http_redirect_guard_reset();
    {
        unsigned char pub6[16];
        memset(pub6, 0, 16);
        pub6[0]=0x26; pub6[1]=0x06; pub6[2]=0x47; pub6[3]=0x00;
        pub6[4]=0x47; pub6[5]=0x00; pub6[14]=0x11; pub6[15]=0x11;
        JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET6, pub6, 443) == 1);
    }

    /* (i) the set is bounded: 17 distinct hop-0 addresses are all allowed (a
     * full set is never a refusal on its own), and the 18th is still allowed
     * at hop 0 even though it could not be recorded. The loop uses distinct
     * v4s to exercise the record path. */
    jc_http_redirect_guard_reset();
    for (i = 0; i < 17; i++) {
        v4[0]=192; v4[1]=168; v4[2]=(unsigned char)i; v4[3]=1;
        JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 80) == 1);
    }
    v4[0]=10; v4[1]=0; v4[2]=0; v4[3]=9; /* not recorded (set full) but hop 0 */
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 80) == 1);

    /* (j) a hop-0 public address, then a hop-1 redirect to a DIFFERENT public
     * address: not in the set and public -> allowed (the set only grants the
     * self-redirect privilege; it never refuses a public target). */
    jc_http_redirect_guard_reset();
    v4[0]=8; v4[1]=8; v4[2]=8; v4[3]=8;
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 80) == 1);
    v4[0]=1; v4[1]=1; v4[2]=1; v4[3]=1;
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 443) == 1);

    /* (k) a hop-0 DUPLICATE consumes no second slot (M1124-b, the design's
     * own claim, jc_http.c redirect_guard_decide_set: the dual-stack retry of
     * an already-recorded address is allowed without a second entry). The set
     * size is not directly readable through the exported API, so pin it by
     * filling: after the duplicate, 15 distinct private hop-0 addresses
     * occupy exactly the 15 remaining slots (the cap is 16); if the duplicate
     * had wrongly added a second slot, the last filler could not be recorded
     * and its hop-1 consult below would be refused. Both the duplicate's key
     * and every filler are then consulted (allowed) at hop 1, while a
     * different port on the duplicate's ip is refused -- the set holds exactly
     * the one merged entry, and the fillers prove no slot was lost. */
    jc_http_redirect_guard_reset();
    v4[0]=127; v4[1]=0; v4[2]=0; v4[3]=1;
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 18080) == 1); /* record */
    JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 18080) == 1); /* duplicate: allowed, no second slot */
    for (i = 0; i < 15; i++) {
        v4[0]=192; v4[1]=168; v4[2]=0; v4[3]=(unsigned char)i;
        JC_CHECK(jc_http_redirect_guard_decide(0, TG_INET, v4, 80) == 1); /* fill the 15 free slots */
    }
    v4[0]=127; v4[1]=0; v4[2]=0; v4[3]=1;
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 18080) == 1); /* in set: self-redirect */
    JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 9999) == 0); /* same ip, other port: not in set */
    for (i = 0; i < 15; i++) {
        v4[0]=192; v4[1]=168; v4[2]=0; v4[3]=(unsigned char)i;
        JC_CHECK(jc_http_redirect_guard_decide(1, TG_INET, v4, 80) == 1); /* each filler was recorded */
    }
}
