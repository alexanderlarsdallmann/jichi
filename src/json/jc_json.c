/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_json.c - cJSON convenience layer (see jc_json.h). */

#include "jc_json.h"
#include "jc_str.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

cJSON *jc_json_parse(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    return cJSON_Parse(text);
}

const char *jc_json_get_str(const cJSON *o, const char *key, const char *dflt)
{
    cJSON *item = cJSON_GetObjectItem(o, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return dflt;
}

double jc_json_get_num(const cJSON *o, const char *key, double dflt)
{
    cJSON *item = cJSON_GetObjectItem(o, key);
    if (cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    return dflt;
}

/* strtod, but the WHOLE string must be the number (trailing whitespace aside).
 * "200" and "200.0" parse; "200 lines" does not. */
static int json_str_to_double(const char *s, double *out)
{
    char *end = NULL;
    double v;
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

double jc_json_get_num_lenient(const cJSON *o, const char *key, double dflt)
{
    cJSON *item = cJSON_GetObjectItem(o, key);
    double v;
    if (cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        json_str_to_double(item->valuestring, &v)) {
        return v;
    }
    return dflt;
}

/* Untrusted-double clamps (A-02, M1077).
 *
 * Converting a double that does not fit the target integer type with a plain
 * cast is undefined behaviour -- UBSan: "1e+10 is outside the range of
 * representable values of type 'int'" (measured end-to-end at
 * src/tools/tool_util.c:123, the tu_arg_int path). These take the value a JSON
 * number reader returned -- jc_json_get_num, or a node's ->valuedouble -- and
 * clamp it into the target's range, so the caller never performs that cast on
 * an out-of-range double.
 *
 * The shape is cJSON's own number_to_int (src/json/cJSON.c:282), extended to the
 * other widths. The rules, for every one of the four:
 *   NaN            -> 0
 *   +infinity      -> the type's max
 *   -infinity      -> the type's min
 *   otherwise      -> clamp to [min, max]. An in-range value passes EXACTLY:
 *                     every integral double at 2^53 and below is exact, so a
 *                     number the sender could actually write is never
 *                     distorted; a fractional value is truncated, as the plain
 *                     C cast would be.
 *
 * A node-taking key variant is deliberately NOT added (the lead's decision,
 * recorded in LOG-3, run 0091): the 117-site sweep uses these four double-clamps
 * at both the jc_json_get_num sites and the ->valuedouble sites.
 */

int jc_json_clamp_int(double val)
{
    if (val != val) {                 /* NaN */
        return 0;
    }
    if (val >= (double)INT_MAX) {     /* +Inf, and everything above it */
        return INT_MAX;
    }
    if (val <= (double)INT_MIN) {     /* -Inf, and everything below it */
        return INT_MIN;
    }
    return (int)val;                  /* defined: the two guards bound val */
}

long jc_json_clamp_long(double val)
{
    if (val != val) {
        return 0;
    }
    if (val >= (double)LONG_MAX) {
        return LONG_MAX;
    }
    if (val <= (double)LONG_MIN) {
        return LONG_MIN;
    }
    return (long)val;
}

unsigned int jc_json_clamp_uint(double val)
{
    if (val != val) {                 /* NaN */
        return 0;
    }
    if (val <= 0.0) {                 /* -Inf, and every negative value -> min */
        return 0;
    }
    if (val >= (double)UINT_MAX) {    /* +Inf, and everything above it */
        return UINT_MAX;
    }
    return (unsigned int)val;
}

jc_size jc_json_clamp_size(double val)
{
    if (val != val) {                 /* NaN */
        return 0;
    }
    if (val <= 0.0) {                 /* -Inf, and every negative value -> min */
        return 0;
    }
    if (val >= (double)~(jc_size)0) { /* +Inf, and everything above it */
        return ~(jc_size)0;
    }
    return (jc_size)val;
}

int jc_json_get_bool(const cJSON *o, const char *key, int dflt)
{
    cJSON *item = cJSON_GetObjectItem(o, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item) ? 1 : 0;
    }
    return dflt;
}


int jc_json_get_bool_lenient(const cJSON *o, const char *key, int dflt)
{
    cJSON *item = cJSON_GetObjectItem(o, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item) ? 1 : 0;
    }
    if (cJSON_IsNumber(item)) {
        return item->valuedouble != 0.0 ? 1 : 0;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        /* M534: one dialect, shared with the YAML frontmatter readers -- and it
         * now includes on/off, because `config set` BLESSES that spelling while
         * no reader accepted it, so `"pathFence": "on"` written by hand (or
         * copied from jichi's own output) turned the fence OFF. A spelling the
         * tool writes must be a spelling the tool reads. */
        int b = 0;
        if (jc_bool_from_word(item->valuestring, &b)) {
            return b;
        }
    }
    return dflt;
}

cJSON *jc_json_get_obj(const cJSON *o, const char *key)
{
    return cJSON_GetObjectItem(o, key);
}

char *jc_json_dup_str(const cJSON *o, const char *key, struct jc_arena *a)
{
    const char *s = jc_json_get_str(o, key, NULL);
    if (s == NULL) {
        return NULL;
    }
    return jc_arena_strdup(a, s);
}

char *jc_json_print(const cJSON *o)
{
    return cJSON_PrintUnformatted(o);
}
