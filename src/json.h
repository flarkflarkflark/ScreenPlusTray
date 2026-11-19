/* Small, bounded JSON DOM parser and serializer.
 * Adapted from Screen+ (src/common/json.h, src/common/util.h), MIT License.
 * Copyright (c) 2026 Screen+ contributors. */
#pragma once
#include <stddef.h>

/* Growable UTF-8 buffer */
typedef struct StrBuf {
    char  *p;
    size_t len;
    size_t cap;
    int    failed;
} StrBuf;

void sb_append(StrBuf *b, const char *s, size_t n);
void sb_puts(StrBuf *b, const char *s);
void sb_printf(StrBuf *b, const char *fmt, ...);
void sb_free(StrBuf *b);

typedef enum JType { J_NULL = 0, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
struct JVal {
    JType   t;
    int     b;
    double  num;
    char   *str;      /* J_STR (UTF-8) */
    JVal   *kids;     /* J_ARR / J_OBJ children */
    char  **keys;     /* J_OBJ keys (UTF-8), parallel to kids */
    int     n, cap;
};

JVal       *json_parse(const char *text, size_t len);
void        json_free(JVal *v);
const JVal *json_get(const JVal *obj, const char *key);      /* case-sensitive */
const JVal *json_geti(const JVal *obj, const char *key);     /* ASCII case-insensitive */
const char *json_str(const JVal *v, const char *def);
double      json_num(const JVal *v, double def);
int         json_bool(const JVal *v, int def);
void        json_write(StrBuf *b, const JVal *v);            /* compact serialization */
void        json_write_string(StrBuf *b, const char *utf8);  /* quoted + escaped */
