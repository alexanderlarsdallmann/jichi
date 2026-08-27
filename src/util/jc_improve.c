/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_improve.c - pure pass-rate history/trend for the improve loop
 * (see jc_improve.h). No I/O beyond parsing a string. */

#include "jc_improve.h"
#include "jc_json.h"

#include <string.h>

int jc_improve_last_pct(const char *history_jsonl)
{
    const char *p;
    const char *line_start;
    int last = -1;

    if (history_jsonl == NULL) {
        return -1;
    }
    /* Each line is a JSON object; keep the pct of the last parseable one. */
    p = history_jsonl;
    line_start = p;
    for (;;) {
        if (*p == '\n' || *p == '\0') {
            if (p > line_start) {
                /* NUL-terminate this line via a small stack copy is avoided;
                 * cJSON_ParseWithLength would be ideal, but jc_json_parse takes
                 * a C string. Copy the line into a bounded buffer. */
                char buf[512];
                jc_size n = (jc_size)(p - line_start);
                if (n < sizeof(buf)) {
                    cJSON *o;
                    memcpy(buf, line_start, n);
                    buf[n] = '\0';
                    o = jc_json_parse(buf);
                    if (o != NULL) {
                        double v = jc_json_get_num(o, "pct", -1.0);
                        if (v >= 0.0) {
                            last = (int)v;
                        }
                        cJSON_Delete(o);
                    }
                }
            }
            if (*p == '\0') {
                break;
            }
            line_start = p + 1;
        }
        p++;
    }
    return last;
}

const char *jc_improve_trend_word(int prev, int cur)
{
    if (prev < 0) {
        return "baseline";
    }
    if (cur > prev) {
        return "improved";
    }
    if (cur < prev) {
        return "regressed";
    }
    return "unchanged";
}
