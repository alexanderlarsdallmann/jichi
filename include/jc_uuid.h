/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_uuid.h - generate a UUID-v4-shaped identifier for session ids.
 *
 * Reads /dev/urandom when available, and falls back to rand() seeded from the
 * clock and pid when it is not (M472). The fallback is not cryptographically
 * strong, so treat an id from it as unique-per-machine rather than unguessable.
 *
 * None of the five consumers is an authentication token, so the weak source was
 * never the vulnerability it would be in a web application. It was upgraded for
 * two of them: the MULTIPART BOUNDARY (a delimiter whose payload is a
 * model-chosen file, and jc_multipart_file does not check that the payload avoids
 * it) and session ids, which name files.
 */
#ifndef JC_UUID_H
#define JC_UUID_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Write a 36-char canonical UUID plus NUL into `out` (needs >= 37 bytes). */
void jc_uuid_v4(char *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_UUID_H */
