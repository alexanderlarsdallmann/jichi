/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_sha256.c - SHA-256 against the NIST / FIPS 180 test vectors (M851).
 *
 * The trust step binds trust to a file's content through this hash, so a wrong
 * digest would not fail loudly -- it would make every trusted file look changed
 * (annoying) or, worse, make a changed file look trusted only if two contents
 * happened to agree on a wrong implementation's output. The vectors pin it to
 * the standard: the empty message, "abc", the 448-bit message (56 bytes: the
 * padding must spill into a second block), and a million 'a's (many blocks,
 * fed in uneven pieces to exercise the buffering). */

#include "jc_test.h"
#include "jc_sha256.h"

#include <string.h>

void test_sha256(void)
{
    char hex[JC_SHA256_HEX_LEN + 1];
    struct jc_sha256 c;
    unsigned char d[JC_SHA256_DIGEST_LEN];
    static const char *const MSG448 =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    char chunk[997];
    long fed;
    int i;

    jc_sha256_hex("", 0, hex);
    JC_CHECK_STR(hex,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    jc_sha256_hex("abc", 3, hex);
    JC_CHECK_STR(hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    JC_CHECK(strlen(MSG448) == 56);
    jc_sha256_hex(MSG448, 56, hex);
    JC_CHECK_STR(hex,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* One million 'a's, fed in 997-byte pieces (a prime, so the piece
     * boundaries drift across every block offset). */
    memset(chunk, 'a', sizeof chunk);
    jc_sha256_init(&c);
    fed = 0;
    while (fed < 1000000L) {
        long n = 1000000L - fed;
        if (n > (long)sizeof chunk) {
            n = (long)sizeof chunk;
        }
        jc_sha256_update(&c, chunk, (jc_size)n);
        fed += n;
    }
    jc_sha256_final(&c, d);
    {
        static const char HEX[] = "0123456789abcdef";
        for (i = 0; i < JC_SHA256_DIGEST_LEN; i++) {
            hex[i * 2] = HEX[(d[i] >> 4) & 0xf];
            hex[i * 2 + 1] = HEX[d[i] & 0xf];
        }
        hex[JC_SHA256_HEX_LEN] = '\0';
    }
    JC_CHECK_STR(hex,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    /* One byte of difference is a different digest (the property the trust
     * binding relies on). */
    {
        char h2[JC_SHA256_HEX_LEN + 1];
        jc_sha256_hex("{\"mode\":\"plan\"}", 15, hex);
        jc_sha256_hex("{\"mode\":\"auto\"}", 15, h2);
        JC_CHECK(strcmp(hex, h2) != 0);
    }
}
