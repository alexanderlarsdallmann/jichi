/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_base64.c - base64 encoder + image media-type detection (M29a). */

#include "jc_test.h"
#include "jc_base64.h"
#include "jc_image.h"

#include <string.h>

static void check(const char *in, const char *want)
{
    char out[128];
    jc_size n = (jc_size)strlen(in);
    JC_CHECK(jc_base64_encode((const unsigned char *)in, n, out,
                              sizeof(out)) == JC_OK);
    JC_CHECK_STR(out, want);
    JC_CHECK(jc_base64_encoded_len(n) == (jc_size)strlen(want));
}

static void test_base64(void)
{
    /* RFC 4648 test vectors. */
    check("", "");
    check("f", "Zg==");
    check("fo", "Zm8=");
    check("foo", "Zm9v");
    check("foob", "Zm9vYg==");
    check("fooba", "Zm9vYmE=");
    check("foobar", "Zm9vYmFy");

    /* A byte with the high bit set encodes cleanly (unsigned handling). */
    {
        unsigned char raw[3];
        char out[8];
        raw[0] = 0xFF; raw[1] = 0x00; raw[2] = 0x80;
        JC_CHECK(jc_base64_encode(raw, 3, out, sizeof(out)) == JC_OK);
        JC_CHECK_STR(out, "/wCA");
    }

    /* Too-small buffer is reported, not overflowed; output left empty. */
    {
        char tiny[3];
        JC_CHECK(jc_base64_encode((const unsigned char *)"foo", 3, tiny,
                                  sizeof(tiny)) == JC_ERR_TOOBIG);
        JC_CHECK(tiny[0] == '\0');
    }

    /* Exact-fit boundary: "foo" needs 4 chars + NUL = 5. */
    {
        char buf[5];
        JC_CHECK(jc_base64_encode((const unsigned char *)"foo", 3, buf,
                                  sizeof(buf)) == JC_OK);
        JC_CHECK_STR(buf, "Zm9v");
    }
}

/* Decode `enc` and assert it yields exactly `want_len` bytes equal to `want`. */
static void check_decode(const char *enc, const char *want, jc_size want_len)
{
    unsigned char buf[128];
    jc_size len = 99;
    JC_CHECK(jc_base64_decode(enc, buf, sizeof(buf), &len) == JC_OK);
    JC_CHECK(len == want_len);
    JC_CHECK(memcmp(buf, want, want_len) == 0);
}

static void test_base64_decode(void)
{
    /* Round-trip the RFC 4648 vectors (covers 0/1/2-byte tails). */
    check_decode("", "", 0);
    check_decode("Zg==", "f", 1);
    check_decode("Zm8=", "fo", 2);
    check_decode("Zm9v", "foo", 3);
    check_decode("Zm9vYg==", "foob", 4);
    check_decode("Zm9vYmE=", "fooba", 5);
    check_decode("Zm9vYmFy", "foobar", 6);

    /* Line-wrapped base64 (embedded whitespace) is tolerated. */
    check_decode("Zm9v\nYmFy", "foobar", 6);
    check_decode("  Zm9v Ym Fy ", "foobar", 6);

    /* Binary with an embedded NUL round-trips and is length-reported (not
     * strlen-truncated). 0xFF 0x00 0x80 encodes to "/wCA". */
    {
        unsigned char buf[8];
        jc_size len = 0;
        JC_CHECK(jc_base64_decode("/wCA", buf, sizeof(buf), &len) == JC_OK);
        JC_CHECK(len == 3);
        JC_CHECK(buf[0] == 0xFF && buf[1] == 0x00 && buf[2] == 0x80);
    }

    /* A non-alphabet character is rejected. */
    {
        unsigned char buf[8];
        jc_size len = 0;
        JC_CHECK(jc_base64_decode("Zm9v!!!!", buf, sizeof(buf), &len)
                 == JC_ERR_INVALID);
    }

    /* A buffer too small to hold the decoded bytes is reported. */
    {
        unsigned char tiny[2];
        jc_size len = 0;
        JC_CHECK(jc_base64_decode("Zm9vYmFy", tiny, sizeof(tiny), &len)
                 == JC_ERR_TOOBIG);
    }

    /* The upper-bound sizer never under-counts the real decoded length. */
    JC_CHECK(jc_base64_decoded_len(8) >= 6);  /* "Zm9vYmFy" -> 6 bytes */
    JC_CHECK(jc_base64_decoded_len(4) >= 3);  /* "Zm9v" -> 3 bytes */
}

static void test_gen_format(void)
{
    JC_CHECK_STR(jc_image_gen_format("a.png"), "png");
    JC_CHECK_STR(jc_image_gen_format("dir/IMG.PNG"), "png");
    JC_CHECK_STR(jc_image_gen_format("x.jpg"), "jpeg");
    JC_CHECK_STR(jc_image_gen_format("x.jpeg"), "jpeg");
    JC_CHECK_STR(jc_image_gen_format("x.webp"), "webp");
    JC_CHECK(jc_image_gen_format("x.gif") == NULL);   /* not an output format */
    JC_CHECK(jc_image_gen_format("notes.txt") == NULL);
    JC_CHECK(jc_image_gen_format(NULL) == NULL);
}

static void test_media_type(void)
{
    JC_CHECK_STR(jc_image_media_type("a.png"), "image/png");
    JC_CHECK_STR(jc_image_media_type("dir/sub/IMG.PNG"), "image/png");
    JC_CHECK_STR(jc_image_media_type("photo.jpg"), "image/jpeg");
    JC_CHECK_STR(jc_image_media_type("photo.JPEG"), "image/jpeg");
    JC_CHECK_STR(jc_image_media_type("anim.gif"), "image/gif");
    JC_CHECK_STR(jc_image_media_type("x.webp"), "image/webp");

    /* Non-images and edge cases return NULL. */
    JC_CHECK(jc_image_media_type("notes.txt") == NULL);
    JC_CHECK(jc_image_media_type("Makefile") == NULL);
    JC_CHECK(jc_image_media_type("noext.") == NULL);
    JC_CHECK(jc_image_media_type("trailing.png/") == NULL);
    JC_CHECK(jc_image_media_type(".png") == NULL); /* hidden file, no name */
    JC_CHECK(jc_image_media_type(NULL) == NULL);
}

void test_base64_image(void)
{
    test_base64();
    test_base64_decode();
    test_media_type();
    test_gen_format();
}
