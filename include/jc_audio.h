/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_audio.h - audio file helpers for transcription input (M33).
 *
 * The mirror of jc_image_media_type for audio: maps a path's extension to an
 * IANA audio media type, used to label the multipart file part sent to a
 * speech-to-text endpoint. Pure and unit-tested.
 */
#ifndef JC_AUDIO_H
#define JC_AUDIO_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Map `path`'s extension to an audio media type (a static string literal), or
 * NULL when the extension is not a supported audio type. Case-insensitive.
 * Supported (the OpenAI/Whisper set): .mp3/.mpga/.mpeg, .wav, .m4a/.mp4,
 * .flac, .ogg/.oga, .webm. Pure. */
const char *jc_audio_media_type(const char *path);

#ifdef __cplusplus
}
#endif
#endif /* JC_AUDIO_H */
