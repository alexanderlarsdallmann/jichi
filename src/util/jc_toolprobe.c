/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_toolprobe.c - pure classification of a tool-calling probe (see header). */

#include "jc_toolprobe.h"

#include <string.h>

/* Case-insensitive substring search (the probe tool name is ASCII). */
static const char *ci_find(const char *hay, const char *ndl)
{
    jc_size hn, nn, i, j;
    if (hay == NULL || ndl == NULL) return NULL;
    hn = (jc_size)strlen(hay);
    nn = (jc_size)strlen(ndl);
    if (nn == 0 || hn < nn) return NULL;
    for (i = 0; i + nn <= hn; i++) {
        int ok = 1;
        for (j = 0; j < nn; j++) {
            char a = hay[i + j];
            char b = ndl[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) {
                ok = 0;
                break;
            }
        }
        if (ok) return hay + i;
    }
    return NULL;
}

enum jc_toolprobe_verdict jc_toolprobe_classify(int ncalls,
                                                const char *call_name,
                                                const char *text)
{
    if (ncalls > 0 && call_name != NULL &&
        strcmp(call_name, JC_TOOLPROBE_TOOL) == 0) {
        return JC_TOOLPROBE_NATIVE;
    }
    /* No native call for our tool. If the model wrote the tool's name into its
     * answer it understood the request and merely failed to invoke -- the M147
     * prose-call shape. Naming the tool is a high-precision signal: the name is
     * namespaced and appears nowhere else. */
    if (text != NULL && ci_find(text, JC_TOOLPROBE_TOOL) != NULL) {
        return JC_TOOLPROBE_TEXT;
    }
    /* M628: distinguish an ANSWER that made no call (a finding about the
     * model) from NO ANSWER (nothing observed). The empty reply used to fall
     * through to NONE and print "none" -- the else-branch as a finding that
     * CLAUDE.md forbids. A call to some OTHER tool is still NONE: that is an
     * answer, and it ignored the instruction. */
    if (ncalls == 0 && (text == NULL || text[0] == '\0')) {
        return JC_TOOLPROBE_UNKNOWN;
    }
    return JC_TOOLPROBE_NONE;
}

const char *jc_toolprobe_verdict_str(enum jc_toolprobe_verdict v)
{
    switch (v) {
    case JC_TOOLPROBE_NATIVE: return "native";
    case JC_TOOLPROBE_TEXT:    return "text";
    case JC_TOOLPROBE_UNKNOWN: return "unknown";
    case JC_TOOLPROBE_NONE:
    default:                   return "none";
    }
}

const char *jc_toolprobe_suggested_setting(enum jc_toolprobe_verdict v)
{
    /* M628: UNKNOWN observed nothing, and nothing is no reason to move off the
     * default. Recommending "none" here would be the M166 mistake with a new
     * name -- degrading a capable model on no evidence. */
    if (v == JC_TOOLPROBE_UNKNOWN) return "native";
    return (v == JC_TOOLPROBE_NATIVE) ? "native" : "none";
}

/* "" / NULL means the built-in default, which is native. */
static int configured_native(const char *configured)
{
    if (configured == NULL || configured[0] == '\0') return 1;
    return strcmp(configured, "native") == 0;
}

const char *jc_toolprobe_advice(enum jc_toolprobe_verdict observed,
                                const char *configured)
{
    int cfg_native = configured_native(configured);

    if (observed == JC_TOOLPROBE_NATIVE) {
        if (cfg_native) {
            return "native tool calling confirmed";
        }
        return "the model DOES call tools natively -- remove the "
               "`toolCalling` override to use it";
    }

    if (observed == JC_TOOLPROBE_TEXT) {
        if (cfg_native) {
            return "the model described the call instead of invoking it. The "
                   "M147 nudge retries this once per turn; if it recurs, check "
                   "the request is well-formed before setting "
                   "`toolCalling: \"none\"`";
        }
        return "the model describes calls rather than invoking them, matching "
               "the configured `toolCalling: \"none\"`";
    }

    if (observed == JC_TOOLPROBE_UNKNOWN) {
        /* M628: the empty reply. This text used to be NONE's; it describes an
         * empty reply, so it moved with the verdict that means one. */
        if (cfg_native) {
            return "the model answered a one-tool request with NOTHING. Suspect "
                   "jichi's request before the model: capture and replay it (see "
                   "docs/LOCAL_MODELS.md, \"When the model calls no tool at all\"). "
                   "This is what a malformed request looks like -- setting "
                   "`toolCalling: \"none\"` here would hide a bug, not fix one";
        }
        return "an empty reply is not evidence about tool calling either way; "
               "the request or the server is the thing to look at, not the "
               "`toolCalling` setting";
    }

    /* NONE: an answer, with neither a call nor prose about one. */
    if (cfg_native) {
        return "the model answered, but attempted no tool call and did not "
               "describe one. Check the request is well-formed first (capture "
               "and replay it, docs/LOCAL_MODELS.md); if it is, this model may "
               "not follow tool instructions -- `toolCalling: \"none\"` is the "
               "setting for that, and only after the request is cleared";
    }
    return "no tool call, as expected for `toolCalling: \"none\"`; the agent "
           "will rely on the prose-call nudge";
}

int jc_toolprobe_is_failure(enum jc_toolprobe_verdict observed,
                            const char *configured)
{
    /* M628: an empty reply (UNKNOWN) fails too -- not as a claim about the
     * model, but because the loop cannot run on it; the word changes, the
     * consequence does not. */
    return ((observed == JC_TOOLPROBE_NONE || observed == JC_TOOLPROBE_UNKNOWN)
            && configured_native(configured));
}
