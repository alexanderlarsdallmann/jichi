/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_neterr.c - see jc_neterr.h (M500). */

#include "jc_neterr.h"

#include <stddef.h>

#include "jc_snprintf.h"

/* The advice per status class. Each says what the READER should do next, which
 * is the whole point: a model told "the server failed" stops, a model told
 * nothing tries the same call with a different file extension. */
static const char *advice_for(long http)
{
    if (http == 0) {
        return "the request never reached a server (network, DNS, TLS or a "
               "wrong apiBase) -- check the endpoint, not the arguments";
    }
    if (http == 401 || http == 403) {
        return "the server rejected the credentials -- fix the API key, not "
               "the arguments";
    }
    if (http == 404) {
        return "the server has no such endpoint or model -- check apiBase and "
               "the model id";
    }
    if (http == 413) {
        return "the payload was too large for the server -- send less audio";
    }
    if (http == 429) {
        return "the server is rate-limiting -- wait and retry the same call";
    }
    if (http >= 500) {
        return "this is a SERVER failure, not the arguments -- retrying with a "
               "different path, format or voice will not change it";
    }
    if (http >= 400) {
        return "the server refused the request -- the arguments or the model "
               "id are the likely cause";
    }
    return "the response was not usable";
}

const char *jc_neterr_render(char *buf, jc_size cap, const char *what,
                             long http, const char *where)
{
    if (buf == NULL || cap == 0) {
        return buf;
    }
    if (what == NULL) {
        what = "the request";
    }
    if (http == 0) {
        if (where != NULL && where[0] != '\0') {
            jc_snprintf(buf, cap, "error: %s failed with no response from %s: %s",
                        what, where, advice_for(http));
        } else {
            jc_snprintf(buf, cap, "error: %s failed with no response: %s",
                        what, advice_for(http));
        }
        return buf;
    }
    if (where != NULL && where[0] != '\0') {
        jc_snprintf(buf, cap, "error: %s failed -- the server answered HTTP %ld "
                              "on %s: %s", what, http, where, advice_for(http));
    } else {
        jc_snprintf(buf, cap, "error: %s failed -- the server answered HTTP "
                              "%ld: %s", what, http, advice_for(http));
    }
    return buf;
}
