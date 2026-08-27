/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_voice.h - speaking jichi's output aloud (M303).
 *
 * WHY IT IS A MODE AND NOT A TOOL. Every primitive already shipped: `generate_audio`
 * (TTS, M32), `transcribe_audio` (STT, M33), `play_audio`/`record_audio` (M163b),
 * and `@audio:` / ACP audio blocks route speech INTO a turn. What did not exist was
 * an interaction mode: nothing spoke a reply, and nothing listened for one. A tool
 * cannot fill that gap, because the model has to choose to call a tool -- and the
 * things a screen-less user most needs spoken are the ones the model never narrates:
 * an approval prompt, an error, the fact that work is still running.
 *
 * ACCESSIBILITY SETS THE REQUIREMENT, and it is stricter than "read the answer
 * aloud". If the screen is not being read, then anything jichi *waits on* must be
 * audible or the session simply stops with no explanation. So voice mode speaks the
 * reply, the approval question, and errors -- not just prose.
 *
 * WHAT IT CANNOT DO, stated because a silent failure here is worse than a refusal:
 *
 *   - **No bundled TTS.** Synthesis needs a model with the `audio` role and
 *     playback needs a configured `sound.play` command. Missing either is reported
 *     once, in words, on stderr -- never by simply not speaking.
 *   - **No silence detection.** `record_audio` takes a fixed `seconds` (enforced by
 *     a timeout), so listening is a fixed window, not "until you stop talking".
 *     That is the shipped primitive's contract, not an oversight to be hidden.
 *   - **No barge-in.** Playback is a child process run to completion; there is no
 *     way to interrupt a spoken reply mid-sentence except Ctrl-C. So long output is
 *     truncated for speech rather than read in full.
 */
#ifndef JC_VOICE_H
#define JC_VOICE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_app; /* jc_app.h */
struct jc_sb;  /* jc_str.h */

/* Longest utterance sent to TTS. Beyond this the text is cut at a sentence
 * boundary and a spoken note says so -- with no barge-in, a five-minute
 * monologue is a trap, not a feature. */
#define JC_VOICE_MAX_CHARS 1200

/* Reduce `text` to something worth hearing, appending to `out`. Pure.
 *
 * A screen reader can skim; a speaker cannot. So this is lossy on purpose:
 *   - a fenced code block becomes "(code block, N lines)" -- reading C aloud
 *     line by line is not accessibility, it is punishment;
 *   - markdown decoration (backticks, *, _, #, list bullets) is dropped, since
 *     "star star important star star" is noise;
 *   - runs of whitespace collapse to one space, and blank lines to one break;
 *   - the result is capped at JC_VOICE_MAX_CHARS at a sentence boundary when one
 *     is available, with a spoken "(truncated)" note.
 * `text` NULL/empty appends nothing. Unit-tested. */
void jc_voice_speakable(const char *text, struct jc_sb *out);

/* Can jichi speak at all? Returns 1 when a model declares the `audio` role AND a
 * `sound.play` command is configured. When it returns 0 and `why` is non-NULL, a
 * one-line reason is written there (which half is missing, and how to fix it) so a
 * caller can SAY it rather than fall silent. */
int jc_voice_available(struct jc_app *app, char *why, jc_size cap);

/* Speak `text`: reduce it, synthesize via the audio-role model, write the bytes to
 * a temp file under the private sink dir, and play it with `sound.play`.
 * Best-effort and synchronous -- returns JC_OK when the utterance played, and a
 * status otherwise WITHOUT disturbing the turn: voice is an accessibility layer,
 * so a broken speaker must never fail the work. Honours app->abort_flag. */
jc_status jc_voice_say(struct jc_app *app, const char *text);

#ifdef __cplusplus
}
#endif
#endif /* JC_VOICE_H */
