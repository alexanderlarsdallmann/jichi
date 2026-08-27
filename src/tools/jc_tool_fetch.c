/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_fetch.c - the fetch_url tool (HTTP GET via libcurl). */

#include "jc_toolcaps.h"
#include "tool_util.h"
#include "jc_untrusted.h"
#include "jc_app.h"
#include "jc_http.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>


static cJSON *fetch_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "url", "The http/https URL to fetch", 1);
    return s;
}

static jc_status fetch_run(const cJSON *args, struct jc_tool_result *out,
                           struct jc_app *app)
{
    const char *url = tu_arg_str(args, "url");
    struct jc_http_request req;
    long status = 0;
    char *body = NULL;
    jc_size len = 0;
    jc_size cap = jc_config_cap(app->config.fetch_max_bytes, JC_CAP_FETCH_DEFAULT);
    jc_status st;
    char msg[1200];

    if (url == NULL) {
        tu_err(out, "error: 'url' argument is required");
        return JC_OK;
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        tu_err(out, "error: url must start with http:// or https://");
        return JC_OK;
    }
    /* SSRF guard (M131): refuse a URL whose host is a loopback / link-local /
     * private / reserved literal up front, with a clear message. The
     * connect-time block below additionally covers DNS-resolved names and any
     * redirect target. */
    {
        char host[256];
        if (jc_url_host(url, host, sizeof(host)) != JC_OK ||
            jc_net_host_is_blocked(host)) {
            /* M300 (the cozy half, from hitting this myself while writing the
             * M300 tests): the old message stated the refusal and stopped, so a
             * reader learned that jichi said no but not why it is right to, nor
             * what to do instead. An error that names the fix costs one line. */
            tu_err(out, "error: refusing to fetch a private, loopback or "
                        "link-local address. jichi will not fetch URLs that only "
                        "resolve inside this machine or network, because a page "
                        "can choose the next URL and that would turn the agent "
                        "into a probe for internal services. To read a local "
                        "file use read_file; to reach an internal service, have "
                        "the user fetch it and pass the content in.");
            return JC_OK;
        }
    }

    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = url;
    req.headers = NULL;
    req.timeout_secs = 30;
    req.abort_flag = &app->abort_flag;
    req.block_private_addrs = 1;
    /* The one caller that SHOULD follow a redirect (M472): fetching a page is
     * exactly the case where a 3xx is normal, and this request carries no
     * credential. Every hop is still re-checked by the M131 connect guard, and
     * capped at CURLOPT_MAXREDIRS. */
    req.follow_redirects = 1;
    /* OOM backstop well above the display cap: normal oversized pages still
     * download and are truncated below; only a pathological body aborts. */
    req.max_bytes = 32L * 1024 * 1024;

    st = jc_http_perform(&req, &status, &body, &len);
    if (st != JC_OK) {
        jc_snprintf(msg, sizeof(msg), "error: request failed (%s)",
                    jc_status_str(st));
        tu_err(out, msg);
        free(body);
        return JC_OK;
    }
    if (status >= 400) {
        jc_snprintf(msg, sizeof(msg), "error: HTTP %ld fetching %s",
                    status, url);
        tu_err(out, msg);
        free(body);
        return JC_OK;
    }

    /* Cap the body so a huge page cannot blow up the context, then fence it as
     * untrusted (M300). This is the highest-risk external channel in jichi: the
     * URL is chosen by the MODEL, and in AUTO mode the result reaches a model that
     * has a shell without any approval prompt in between. The fence is a
     * mitigation, not a fix -- see jc_untrusted.h. */
    {
        struct jc_sb sb;
        struct jc_sb inner;
        jc_sb_init(&inner);
        if (len > cap) {
            jc_sb_append_n(&inner, body, cap);
            jc_sb_append_fmt(&inner, "\n... [truncated, %lu bytes total]",
                             (unsigned long)len);
        } else {
            jc_sb_append_n(&inner, body, len);
        }
        free(body);
        jc_sb_init(&sb);
        jc_untrusted_wrap("web page", url,
                          inner.data != NULL ? inner.data : "", &sb);
        jc_sb_free(&inner);
        tu_ok_owned(out, jc_sb_finish(&sb));
        jc_sb_free(&sb);
    }
    return JC_OK;
}

static const struct jc_tool FETCH_TOOL = {
    "fetch_url",
    "Fetch the contents of an http/https URL and return the response body.",
    fetch_schema,
    1, /* readonly: it does not modify the filesystem */
    fetch_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_fetch(void)
{
    return &FETCH_TOOL;
}
