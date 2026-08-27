/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_jsonrepair.h - conservative repair of nearly-JSON tool arguments (M148).
 *
 * Small models emit almost-JSON: trailing commas, missing closing brackets,
 * Python literals (True/False/None -- very common from Python-trained
 * models), or single quotes throughout. Candidate #3 of the small-model
 * agentics proposal: repair conservatively, ONLY after a real parse failure,
 * and count every attempt -- never guess so hard that a wrong repair
 * executes a tool with arguments the model didn't mean.
 *
 * Repair classes (each pass is string-aware -- content inside JSON strings
 * is never touched):
 *   - single -> double quotes, ONLY when the input contains no '"' at all
 *     (otherwise the intent is ambiguous)
 *   - Python word literals True/False/None -> true/false/null
 *   - trailing commas before '}' / ']'
 *   - missing closing brackets/braces (appended in nesting order)
 * REJECTED (deliberately): unquoted keys -- the tokenizer ambiguity is too
 * high for a conservative repairer; revisit only with a failing corpus.
 *
 * Returns a malloc'd string that cJSON_Parse ACCEPTS (the repair is
 * validated before it is returned), or NULL when no conservative repair
 * produced valid JSON. Pure; unit-tested (tests/test_jsonrepair.c).
 */
#ifndef JC_JSONREPAIR_H
#define JC_JSONREPAIR_H


#ifdef __cplusplus
extern "C" {
#endif
char *jc_jsonrepair(const char *s);

#ifdef __cplusplus
}
#endif
#endif /* JC_JSONREPAIR_H */
