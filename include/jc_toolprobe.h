/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_toolprobe.h - classify how a model answers a tool-calling request (M167).
 *
 * `doctor --live` sends one minimal request that advertises a single trivial
 * tool and asks the model to call it, then classifies the answer: a native tool
 * call, the call described in prose, or nothing at all. That replaces a
 * hand-set per-model `toolCalling` flag with an observation.
 *
 * The probe's more valuable job, and the reason it was promoted from the
 * deferred list, is that it is an end-to-end self-test of *jichi's own request
 * construction*. M166 made every request end with a content-free assistant turn;
 * a small local model then answered a one-tool request with a single
 * end-of-turn token. Run against that build, this probe reports `none` for a
 * model that demonstrably supports native calling -- a loud, specific signal on
 * day one, instead of a day of bisecting. So when the observed verdict is worse
 * than the configured `toolCalling`, the advice points at the request first and
 * the model second (see jc_toolprobe_advice).
 *
 * This header is the pure core: classification and advice, no I/O and no cJSON.
 * The request/dispatch shell lives in main.c on top of jc_oneshot_probe().
 */
#ifndef JC_TOOLPROBE_H
#define JC_TOOLPROBE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* The tool the probe advertises. Deliberately namespaced and useless, so it
 * cannot collide with a real tool and a model cannot "helpfully" do something
 * else with it. */
#define JC_TOOLPROBE_TOOL "jichi_probe_echo"

/* The user message the probe sends. Imperative and unambiguous: any capable
 * model should answer with a call, so a non-call answer is informative. */
#define JC_TOOLPROBE_PROMPT \
    "Call the tool `" JC_TOOLPROBE_TOOL "` with text set to \"ping\". " \
    "Reply with the tool call only."

/* Ordered by capability, so a caller can compare observed vs configured. */
enum jc_toolprobe_verdict {
    JC_TOOLPROBE_NONE   = 0, /* neither a call nor a description of one   */
    JC_TOOLPROBE_TEXT   = 1, /* described the call in prose (M147 shape)  */
    JC_TOOLPROBE_NATIVE = 2  /* emitted a native tool call               */
};

/* Classify one probe answer. Pure.
 *
 * `ncalls`/`call_name` describe the natively-parsed tool calls (call_name may be
 * NULL); `text` is the assistant's text (may be NULL). A native call counts only
 * when it names the probe tool -- a model that calls something else did not
 * follow the instruction and must not be scored as working. */
enum jc_toolprobe_verdict jc_toolprobe_classify(int ncalls,
                                                const char *call_name,
                                                const char *text);

/* "native" | "text" | "none" for display. Pure. */
const char *jc_toolprobe_verdict_str(enum jc_toolprobe_verdict v);

/* Map an observed verdict to a `toolCalling` config value ("native"/"none").
 * There is no "text" setting today -- the text protocol is unbuilt (a reserved
 * enum) -- so a TEXT verdict recommends "none", which is what the M147 nudge
 * already handles. Pure. */
const char *jc_toolprobe_suggested_setting(enum jc_toolprobe_verdict v);

/* Operator-facing advice for an observed verdict against the configured
 * `toolCalling` string (may be NULL/empty => treated as the "native" default).
 * Returns a static string; never NULL. Pure.
 *
 * The ordering rule this encodes: when a model configured `native` observes
 * worse than native, the *request* is the first suspect, not the model. Telling
 * the operator to set `toolCalling: "none"` there would degrade a fully capable
 * model to work around a bug in jichi -- which is exactly what happened before
 * M166 was found. */
const char *jc_toolprobe_advice(enum jc_toolprobe_verdict observed,
                                const char *configured);

/* 1 when the observation contradicts the configuration badly enough that
 * `doctor` should FAIL rather than warn: configured native, observed none. That
 * combination is either a broken request or an unusable model, and both make the
 * agent loop useless, so it is not a soft warning. Pure. */
int jc_toolprobe_is_failure(enum jc_toolprobe_verdict observed,
                            const char *configured);

#ifdef __cplusplus
}
#endif
#endif /* JC_TOOLPROBE_H */
