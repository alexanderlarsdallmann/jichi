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
    return JC_TOOLPROBE_NONE;
}

const char *jc_toolprobe_verdict_str(enum jc_toolprobe_verdict v)
{
    switch (v) {
    case JC_TOOLPROBE_NATIVE: return "native";
    case JC_TOOLPROBE_TEXT:   return "text";
    case JC_TOOLPROBE_NONE:
    default:                  return "none";
    }
}

const char *jc_toolprobe_suggested_setting(enum jc_toolprobe_verdict v)
{
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

    /* NONE. The interesting case, and the one that must not misdirect. */
    if (cfg_native) {
        return "the model answered a one-tool request with NOTHING. Suspect "
               "jichi's request before the model: capture and replay it (see "
               "docs/LOCAL_MODELS.md, \"When the model calls no tool at all\"). "
               "This is what a malformed request looks like -- setting "
               "`toolCalling: \"none\"` here would hide a bug, not fix one";
    }
    return "no tool call, as expected for `toolCalling: \"none\"`; the agent "
           "will rely on the prose-call nudge";
}

int jc_toolprobe_is_failure(enum jc_toolprobe_verdict observed,
                            const char *configured)
{
    return (observed == JC_TOOLPROBE_NONE && configured_native(configured));
}
