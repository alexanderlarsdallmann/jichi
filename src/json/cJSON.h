/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* cJSON.h - the cJSON-compatible API subset jichi uses.
 *
 * Part of jichi; the licence is the tree's, stated in the SPDX line above.
 *
 * PROVENANCE: NOT upstream cJSON, and not third-party code. This is an original
 * C89 implementation of the subset of the cJSON API that jichi needs; see
 * cJSON.c for the full note. Only the API is shared, so upstream cJSON
 * (github.com/DaveGamble/cJSON) can replace this header and cJSON.c as a pair.
 *
 * Numbers are stored as double (valuedouble), with valueint as a truncated int,
 * matching upstream behaviour.
 */

#ifndef cJSON__h
#define cJSON__h

#ifdef __cplusplus
extern "C" {
#endif

/* Item types. */
#define cJSON_Invalid 0
#define cJSON_False   1
#define cJSON_True    2
#define cJSON_NULL    4
#define cJSON_Number  8
#define cJSON_String  16
#define cJSON_Array   32
#define cJSON_Object  64
#define cJSON_Raw     128

typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;   /* for Array/Object: first element */

    int type;

    char  *valuestring;    /* for String/Raw */
    int    valueint;       /* deprecated mirror of valuedouble */
    double valuedouble;    /* for Number */

    char  *string;         /* the key, if this item is in an Object */
} cJSON;

/* Parse / free. */
cJSON *cJSON_Parse(const char *value);
void   cJSON_Delete(cJSON *item);

/* Serialise. Returned buffer is malloc'd; free with cJSON_free. */
char *cJSON_Print(const cJSON *item);
char *cJSON_PrintUnformatted(const cJSON *item);
void  cJSON_free(void *ptr);

/* Object / array access. */
cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string);
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);
int    cJSON_GetArraySize(const cJSON *array);

/* Type predicates. */
int cJSON_IsInvalid(const cJSON *item);
int cJSON_IsFalse(const cJSON *item);
int cJSON_IsTrue(const cJSON *item);
int cJSON_IsBool(const cJSON *item);
int cJSON_IsNull(const cJSON *item);
int cJSON_IsNumber(const cJSON *item);
int cJSON_IsString(const cJSON *item);
int cJSON_IsArray(const cJSON *item);
int cJSON_IsObject(const cJSON *item);

/* Constructors. */
cJSON *cJSON_CreateNull(void);
cJSON *cJSON_CreateTrue(void);
cJSON *cJSON_CreateFalse(void);
cJSON *cJSON_CreateBool(int boolean);
cJSON *cJSON_CreateNumber(double num);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateObject(void);

/* Mutation. Items are owned by their parent after being added. */
int    cJSON_AddItemToArray(cJSON *array, cJSON *item);
int    cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
/* Replace the object member named `string` with `newitem` (which takes over the
 * key) and free the old item; returns 1 if a member was replaced, else 0. */
int    cJSON_ReplaceItemInObject(cJSON *object, const char *string,
                                 cJSON *newitem);

cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *string);
cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number);
cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int boolean);
cJSON *cJSON_AddNullToObject(cJSON *object, const char *name);
cJSON *cJSON_AddObjectToObject(cJSON *object, const char *name);
cJSON *cJSON_AddArrayToObject(cJSON *object, const char *name);

/* Iterate the elements of an Array or Object. */
#define cJSON_ArrayForEach(element, array) \
    for ((element) = ((array) != 0 ? (array)->child : 0); \
         (element) != 0; (element) = (element)->next)

#ifdef __cplusplus
}
#endif

#endif /* cJSON__h */
