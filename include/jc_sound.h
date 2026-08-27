/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sound.h - sound I/O tools (M163b): play_audio / record_audio.
 *
 * jichi's media tools are file-based (generate_audio writes a file,
 * transcribe_audio reads one) with no way to actually reach a speaker or
 * microphone. These two tools close that gap the M42 way -- by shelling OUT to
 * a configured external command (aplay/arecord/ffplay/...), never linking an
 * audio library. Registered only when the matching `sound` command is set.
 *
 * Both are MUTATING (not readonly): playback emits energy into a shared space
 * and recording captures third parties + writes a file. They are NOT kinetic
 * by default (spoken status is what an unattended loop wants) -- a siren-class
 * output should be wrapped in a kinetic user tool.
 *
 * This header exposes the pure helpers (unit-tested); the tools live in
 * src/tools/jc_tool_sound.c. */
#ifndef JC_SOUND_H
#define JC_SOUND_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

struct jc_sb; /* jc_str.h */

/* Built-in defaults when the config leaves them 0. */
#define JC_SOUND_PLAY_TIMEOUT_DEFAULT 120L
#define JC_SOUND_RECORD_DEFAULT       60L
#define JC_SOUND_RECORD_CAP           600L

struct jc_tool; /* jc_tool.h */

/* Clamp a requested record duration to [1, effective-max], where the effective
 * max is `max` (<=0 => JC_SOUND_RECORD_DEFAULT) capped at JC_SOUND_RECORD_CAP.
 * A req <= 0 yields the effective max. Pure. */
long jc_sound_clamp_seconds(long req, long max);

/* Write "<kind>-<ts>.wav" (ts truncated to whole seconds) into `out`. Pure. */
void jc_sound_default_name(const char *kind, double ts, char *out, jc_size cap);

/* Assemble a NULL-terminated argv: `command`, then the `nargs` `args`, then
 * (when non-NULL) `file` as the final element. Writes into `argv_out` (cap
 * slots incl. the NULL terminator); returns the element count (excluding the
 * terminator) or -1 if it would overflow. Borrowed pointers (not copied).
 * Pure. */
/* Run a configured play/record command (argv or shell form) with the audio env
 * contract ($JICHI_AUDIO_FILE, $JICHI_AUDIO_SECONDS when secs > 0). Returns the
 * child's exit code, or -1 when the argv could not be built. Shared by the
 * play_audio/record_audio tools and voice mode (M303), so both honour the same
 * env contract -- a second copy of it would drift. */
int jc_sound_run(const char *command, const struct jc_vec *args,
                 const char *shell, const char *file, long secs,
                 long timeout, struct jc_sb *out, volatile int *abort_flag);

int jc_sound_build_argv(const char *command, char *const *args, int nargs,
                        const char *file, char **argv_out, int cap);

/* The tools (static singletons). NULL is never returned. */
const struct jc_tool *jc_tool_play_audio(void);
const struct jc_tool *jc_tool_record_audio(void);

#ifdef __cplusplus
}
#endif
#endif /* JC_SOUND_H */
