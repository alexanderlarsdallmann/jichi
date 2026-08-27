/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_neterr.h - render a media-backend HTTP failure as a message that names a
 * way forward (M500).
 *
 * THE DEFECT THIS EXISTS FOR. `generate_audio` reported exactly
 * "error: audio generation request failed" whatever happened. Measured against a
 * gateway whose TTS model answers HTTP 500: the model retried "several
 * variations (different path, .wav vs .mp3, an explicit voice)" -- 428 output
 * tokens and 27 seconds -- and then told the operator to check an API key and
 * endpoint that were both correct. The status code was in the LOG the whole
 * time and never reached the model.
 *
 * That is the M342 message class this project hunts: a refusal that states a
 * cause with no way forward amplifies retry loops. The status class is what
 * separates "your arguments are wrong" (retry differently) from "the server is
 * broken" (stop), and only the second is true of a 5xx.
 *
 * Pure: no I/O, no allocation. Unit-tested offline. */
#ifndef JC_NETERR_H
#define JC_NETERR_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Write into `buf` a one-line explanation of a failed media request.
 *
 * `what`   - the operation, for the reader ("audio generation", "transcription")
 * `http`   - the HTTP status, or 0 when no response arrived at all
 * `where`  - the endpoint path ("/audio/speech"), may be NULL
 *
 * Always NUL-terminates when cap > 0. Returns buf. */
const char *jc_neterr_render(char *buf, jc_size cap, const char *what,
                             long http, const char *where);

#ifdef __cplusplus
}
#endif
#endif /* JC_NETERR_H */
