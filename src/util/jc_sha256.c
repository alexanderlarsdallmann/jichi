/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sha256.c - SHA-256 from FIPS 180-4 (see jc_sha256.h). Pure. */
#include "jc_sha256.h"

#define M32 0xFFFFFFFFUL
#define ROTR(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & M32)
#define CH(x, y, z)  ((((x) & (y)) ^ ((~(x)) & (z))) & M32)
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x) (ROTR((x), 2) ^ ROTR((x), 13) ^ ROTR((x), 22))
#define BSIG1(x) (ROTR((x), 6) ^ ROTR((x), 11) ^ ROTR((x), 25))
#define SSIG0(x) (ROTR((x), 7) ^ ROTR((x), 18) ^ ((x) >> 3))
#define SSIG1(x) (ROTR((x), 17) ^ ROTR((x), 19) ^ ((x) >> 10))

/* FIPS 180-4 section 4.2.2: the first 32 bits of the fractional parts of the
 * cube roots of the first 64 primes. */
static const unsigned long K[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

static void transform(struct jc_sha256 *c, const unsigned char *blk)
{
    unsigned long w[64];
    unsigned long a, b, d, e, f, g, h, cc, t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (((unsigned long)blk[i * 4] << 24) |
                ((unsigned long)blk[i * 4 + 1] << 16) |
                ((unsigned long)blk[i * 4 + 2] << 8) |
                (unsigned long)blk[i * 4 + 3]) & M32;
    }
    for (i = 16; i < 64; i++) {
        w[i] = (SSIG1(w[i - 2]) + w[i - 7] + SSIG0(w[i - 15]) + w[i - 16])
               & M32;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        t1 = (h + BSIG1(e) + CH(e, f, g) + K[i] + w[i]) & M32;
        t2 = (BSIG0(a) + MAJ(a, b, cc)) & M32;
        h = g; g = f; f = e;
        e = (d + t1) & M32;
        d = cc; cc = b; b = a;
        a = (t1 + t2) & M32;
    }
    c->h[0] = (c->h[0] + a) & M32;
    c->h[1] = (c->h[1] + b) & M32;
    c->h[2] = (c->h[2] + cc) & M32;
    c->h[3] = (c->h[3] + d) & M32;
    c->h[4] = (c->h[4] + e) & M32;
    c->h[5] = (c->h[5] + f) & M32;
    c->h[6] = (c->h[6] + g) & M32;
    c->h[7] = (c->h[7] + h) & M32;
}

void jc_sha256_init(struct jc_sha256 *c)
{
    /* FIPS 180-4 section 5.3.3: the first 32 bits of the fractional parts of
     * the square roots of the first 8 primes. */
    c->h[0] = 0x6a09e667UL; c->h[1] = 0xbb67ae85UL;
    c->h[2] = 0x3c6ef372UL; c->h[3] = 0xa54ff53aUL;
    c->h[4] = 0x510e527fUL; c->h[5] = 0x9b05688cUL;
    c->h[6] = 0x1f83d9abUL; c->h[7] = 0x5be0cd19UL;
    c->buflen = 0;
    c->bytes_lo = 0;
    c->bytes_hi = 0;
}

void jc_sha256_update(struct jc_sha256 *c, const void *data, jc_size len)
{
    const unsigned char *p = (const unsigned char *)data;

    while (len > 0) {
        c->buf[c->buflen++] = *p++;
        len--;
        c->bytes_lo = (c->bytes_lo + 1UL) & M32;
        if (c->bytes_lo == 0) {
            c->bytes_hi = (c->bytes_hi + 1UL) & M32;
        }
        if (c->buflen == 64) {
            transform(c, c->buf);
            c->buflen = 0;
        }
    }
}

void jc_sha256_final(struct jc_sha256 *c,
                     unsigned char out[JC_SHA256_DIGEST_LEN])
{
    /* The message length in BITS, captured before padding (padding is not
     * part of the message), as a 64-bit big-endian value. */
    unsigned long bits_hi = ((c->bytes_hi << 3) | (c->bytes_lo >> 29)) & M32;
    unsigned long bits_lo = (c->bytes_lo << 3) & M32;
    int i;

    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 56) {
        while (c->buflen < 64) {
            c->buf[c->buflen++] = 0;
        }
        transform(c, c->buf);
        c->buflen = 0;
    }
    while (c->buflen < 56) {
        c->buf[c->buflen++] = 0;
    }
    c->buf[56] = (unsigned char)((bits_hi >> 24) & 0xff);
    c->buf[57] = (unsigned char)((bits_hi >> 16) & 0xff);
    c->buf[58] = (unsigned char)((bits_hi >> 8) & 0xff);
    c->buf[59] = (unsigned char)(bits_hi & 0xff);
    c->buf[60] = (unsigned char)((bits_lo >> 24) & 0xff);
    c->buf[61] = (unsigned char)((bits_lo >> 16) & 0xff);
    c->buf[62] = (unsigned char)((bits_lo >> 8) & 0xff);
    c->buf[63] = (unsigned char)(bits_lo & 0xff);
    transform(c, c->buf);
    c->buflen = 0;
    for (i = 0; i < 8; i++) {
        out[i * 4]     = (unsigned char)((c->h[i] >> 24) & 0xff);
        out[i * 4 + 1] = (unsigned char)((c->h[i] >> 16) & 0xff);
        out[i * 4 + 2] = (unsigned char)((c->h[i] >> 8) & 0xff);
        out[i * 4 + 3] = (unsigned char)(c->h[i] & 0xff);
    }
}

void jc_sha256_hex(const void *data, jc_size len, char *out)
{
    static const char HEX[] = "0123456789abcdef";
    struct jc_sha256 c;
    unsigned char d[JC_SHA256_DIGEST_LEN];
    int i;

    jc_sha256_init(&c);
    jc_sha256_update(&c, data, len);
    jc_sha256_final(&c, d);
    for (i = 0; i < JC_SHA256_DIGEST_LEN; i++) {
        out[i * 2] = HEX[(d[i] >> 4) & 0xf];
        out[i * 2 + 1] = HEX[d[i] & 0xf];
    }
    out[JC_SHA256_HEX_LEN] = '\0';
}
