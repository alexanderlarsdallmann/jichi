/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* keyprobe - print the per-workspace store key a path derives (M1097, D-09).
 *
 *   keyprobe sha256 PATH    the 64-hex lowercase SHA-256 of the path string,
 *                           the key the fixed derivation uses
 *   keyprobe djb2 PATH      the unsigned long djb2 (h*33+c, h=5381) of the
 *                           path string, the key the OLD derivation used
 *
 * The checkpoint shadow repo, the run lease and the telemetry log are all
 * named by one derivation of the workspace path; D-09 was that derivation
 * (djb2) colliding on names differing by (+1,-33) in adjacent bytes, so two
 * workspaces shared one store. This is the probe the
 * checkpoint_key_collision driver uses to show the collision pair's old keys
 * are EQUAL while its new keys are 64-hex and DISTINCT. It links the in-tree
 * SHA-256 core (src/util/jc_sha256.c), the same code the product uses, so the
 * probe cannot drift from the implementation it is measuring. Test-only;
 * never installed.
 */
#include <stdio.h>
#include <string.h>

#include "jc_sha256.h"

static unsigned long djb2(const char *s)
{
    unsigned long h = 5381UL;
    while (*s != '\0') {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    return h;
}

int main(int argc, char **argv)
{
    char hex[JC_SHA256_HEX_LEN + 1];

    if (argc != 3) {
        fprintf(stderr, "usage: keyprobe {sha256|djb2} PATH\n");
        return 2;
    }
    if (strcmp(argv[1], "sha256") == 0) {
        jc_sha256_hex(argv[2], strlen(argv[2]), hex);
        printf("%s\n", hex);
        return 0;
    }
    if (strcmp(argv[1], "djb2") == 0) {
        printf("%lu\n", djb2(argv[2]));
        return 0;
    }
    fprintf(stderr, "usage: keyprobe {sha256|djb2} PATH\n");
    return 2;
}
