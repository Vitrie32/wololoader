// ==========================================================================
// Wololoader - Minimal JSON Parser & File Reader Implementation
//
// Purpose:
//   Provides a self-contained, dependency-free JSON parser and portable file
//   reader for Wololoader (AoE2 HD 5.8 mod loader). This replaces Python's
//   `json.load()` in tools/build.py so that the game DLL (and standalone
//   builder tools) can parse mod manifests (mod.json) and civilization
//   definitions (<civ>.json) directly in C.
//
// Design & Mirroring tools/build.py:
//   - AST Model: Represents values as a recursive tagged-union tree (WlJsonVal).
//   - Number Semantics: Python's json parser differentiates integers from floats
//     and retains exact formatting. We parse numbers into double (`dval`),
//     int64_t (`ival`), a float flag (`is_float`), and the original literal
//     (`raw`). This allows datbuild.c to produce Python-identical error messages
//     (e.g., distinguishing integer vs float validation errors) without losing
//     the original textual format.
//   - Unicode / Escapes: Fully handles standard JSON escape sequences (\", \\,
//     \/, \b, \f, \n, \r, \t) and \uXXXX hex escapes, including UTF-16 surrogate
//     pairs (\uD800..\uDBFF followed by \uDC00..\uDFFF), encoding them into UTF-8.
//   - Exact Error Strings: Emits exact error strings expected by validation
//     and regression tests.
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

// ==========================================================================
// Memory Cleanup
// ==========================================================================

// Recursively frees a JSON value and all allocated descendants.
// Safe to invoke with NULL pointers.
void wl_json_free(WlJsonVal *v) {
    if (!v) return;
    if (v->type == WL_JSON_STRING) {
        free(v->u.str);
    } else if (v->type == WL_JSON_ARRAY) {
        for (size_t i = 0; i < v->u.arr.count; i++) {
            wl_json_free(v->u.arr.items[i]);
        }
        free(v->u.arr.items);
    } else if (v->type == WL_JSON_OBJECT) {
        for (size_t i = 0; i < v->u.obj.count; i++) {
            free(v->u.obj.members[i].key);
            wl_json_free(v->u.obj.members[i].val);
        }
        free(v->u.obj.members);
    }
    free(v);
}

// ==========================================================================
// Lexer & String Parsing Helpers
// ==========================================================================

// Advances the input pointer past any JSON whitespace characters.
static void wl_skip_ws(const char **p) {
    while (**p && (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')) {
        (*p)++;
    }
}

// Parses 4 hexadecimal digits from the current position into a 16-bit code point.
// Returns 1 on success, 0 on invalid hex character.
static int wl_parse_hex4(const char *p, uint32_t *out) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

// Encodes a Unicode code point (up to 21 bits) as UTF-8 bytes and appends it
// to a dynamic buffer, expanding capacity as needed.
static void wl_append_utf8(char **buf, size_t *len, size_t *cap, uint32_t cp) {
    unsigned char bytes[4];
    int n = 0;
    if (cp < 0x80) {
        bytes[0] = (unsigned char)cp;
        n = 1;
    } else if (cp < 0x800) {
        bytes[0] = (unsigned char)(0xC0 | (cp >> 6));
        bytes[1] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        bytes[0] = (unsigned char)(0xE0 | (cp >> 12));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 3;
    } else if (cp < 0x110000) {
        bytes[0] = (unsigned char)(0xF0 | (cp >> 18));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[3] = (unsigned char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    while (*len + (size_t)n + 1 > *cap) {
        size_t ncap = (*cap == 0) ? 64 : *cap * 2;
        char *nbuf = realloc(*buf, ncap);
        if (!nbuf) return;
        *buf = nbuf;
        *cap = ncap;
    }
    memcpy(*buf + *len, bytes, (size_t)n);
    *len += (size_t)n;
    (*buf)[*len] = '\0';
}

// Parses a quoted JSON string token starting at **p.
// Handles backslash escapes and surrogate pairs, returning a heap-allocated
// NUL-terminated UTF-8 string on success, or NULL on error.
static char *wl_parse_string_raw(const char **p, char *err, size_t errcap) {
    if (**p != '\"') {
        if (err && errcap > 0) snprintf(err, errcap, "expected '\"'");
        return NULL;
    }
    (*p)++;
    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        if (err && errcap > 0) snprintf(err, errcap, "out of memory");
        return NULL;
    }
    buf[0] = '\0';

    while (**p) {
        char c = **p;
        if (c == '\"') {
            (*p)++;
            return buf;
        }
        if (c == '\\') {
            (*p)++;
            char esc = **p;
            if (!esc) {
                if (err && errcap > 0) snprintf(err, errcap, "unterminated escape sequence");
                free(buf);
                return NULL;
            }
            (*p)++;
            switch (esc) {
                case '\"': case '\\': case '/':
                    wl_append_utf8(&buf, &len, &cap, (unsigned char)esc);
                    break;
                case 'b': wl_append_utf8(&buf, &len, &cap, '\b'); break;
                case 'f': wl_append_utf8(&buf, &len, &cap, '\f'); break;
                case 'n': wl_append_utf8(&buf, &len, &cap, '\n'); break;
                case 'r': wl_append_utf8(&buf, &len, &cap, '\r'); break;
                case 't': wl_append_utf8(&buf, &len, &cap, '\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!wl_parse_hex4(*p, &cp)) {
                        if (err && errcap > 0) snprintf(err, errcap, "invalid \\uXXXX escape");
                        free(buf);
                        return NULL;
                    }
                    *p += 4;
                    // Detect and assemble UTF-16 surrogate pairs:
                    // High surrogate: 0xD800..0xDBFF, Low surrogate: 0xDC00..0xDFFF
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (**p == '\\' && (*p)[1] == 'u') {
                            uint32_t cp2 = 0;
                            if (wl_parse_hex4(*p + 2, &cp2) && cp2 >= 0xDC00 && cp2 <= 0xDFFF) {
                                *p += 6;
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (cp2 - 0xDC00);
                            }
                        }
                    }
                    wl_append_utf8(&buf, &len, &cap, cp);
                    break;
                }
                default:
                    wl_append_utf8(&buf, &len, &cap, (unsigned char)esc);
                    break;
            }
        } else {
            // Raw bytes are already UTF-8 (json files are UTF-8): copy them as
            // they are. Encoding each byte >= 0x80 as its own code point here
            // double-encoded text ("\u00b7" became "\u00c2\u00b7", shown as "Â·").
            if ((unsigned char)c < 0x80) {
                wl_append_utf8(&buf, &len, &cap, (unsigned char)c);
            } else {
                if (len + 2 > cap) {
                    char *nbuf = realloc(buf, cap * 2);
                    if (!nbuf) { free(buf); if (err && errcap > 0) snprintf(err, errcap, "out of memory"); return NULL; }
                    buf = nbuf;
                    cap *= 2;
                }
                buf[len++] = c;
                buf[len] = '\0';
            }
            (*p)++;
        }
    }
    if (err && errcap > 0) snprintf(err, errcap, "unterminated string");
    free(buf);
    return NULL;
}

// Forward declaration of recursive value parser
static WlJsonVal *wl_json_parse_val(const char **p, char *err, size_t errcap);

// Parses a JSON object starting at '{'.
static WlJsonVal *wl_json_parse_object(const char **p, char *err, size_t errcap) {
    (*p)++; // skip '{'
    WlJsonVal *obj = calloc(1, sizeof(WlJsonVal));
    if (!obj) {
        if (err && errcap > 0) snprintf(err, errcap, "out of memory");
        return NULL;
    }
    obj->type = WL_JSON_OBJECT;
    obj->u.obj.cap = 8;
    obj->u.obj.members = malloc(obj->u.obj.cap * sizeof(WlJsonMember));
    if (!obj->u.obj.members) {
        free(obj);
        if (err && errcap > 0) snprintf(err, errcap, "out of memory");
        return NULL;
    }

    wl_skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return obj;
    }

    while (**p) {
        wl_skip_ws(p);
        if (**p != '\"') {
            if (err && errcap > 0) snprintf(err, errcap, "expected string key in object");
            wl_json_free(obj);
            return NULL;
        }
        char *key = wl_parse_string_raw(p, err, errcap);
        if (!key) {
            wl_json_free(obj);
            return NULL;
        }
        wl_skip_ws(p);
        if (**p != ':') {
            if (err && errcap > 0) snprintf(err, errcap, "expected ':' after key in object");
            free(key);
            wl_json_free(obj);
            return NULL;
        }
        (*p)++;
        WlJsonVal *val = wl_json_parse_val(p, err, errcap);
        if (!val) {
            free(key);
            wl_json_free(obj);
            return NULL;
        }

        if (obj->u.obj.count >= obj->u.obj.cap) {
            obj->u.obj.cap *= 2;
            WlJsonMember *new_m = realloc(obj->u.obj.members, obj->u.obj.cap * sizeof(WlJsonMember));
            if (!new_m) {
                free(key);
                wl_json_free(val);
                wl_json_free(obj);
                if (err && errcap > 0) snprintf(err, errcap, "out of memory");
                return NULL;
            }
            obj->u.obj.members = new_m;
        }
        obj->u.obj.members[obj->u.obj.count].key = key;
        obj->u.obj.members[obj->u.obj.count].val = val;
        obj->u.obj.count++;

        wl_skip_ws(p);
        if (**p == '}') {
            (*p)++;
            return obj;
        }
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (err && errcap > 0) snprintf(err, errcap, "expected ',' or '}' in object");
        wl_json_free(obj);
        return NULL;
    }
    if (err && errcap > 0) snprintf(err, errcap, "unterminated object");
    wl_json_free(obj);
    return NULL;
}

// Parses a JSON array starting at '['.
static WlJsonVal *wl_json_parse_array(const char **p, char *err, size_t errcap) {
    (*p)++; // skip '['
    WlJsonVal *arr = calloc(1, sizeof(WlJsonVal));
    if (!arr) {
        if (err && errcap > 0) snprintf(err, errcap, "out of memory");
        return NULL;
    }
    arr->type = WL_JSON_ARRAY;
    arr->u.arr.cap = 8;
    arr->u.arr.items = malloc(arr->u.arr.cap * sizeof(WlJsonVal *));
    if (!arr->u.arr.items) {
        free(arr);
        if (err && errcap > 0) snprintf(err, errcap, "out of memory");
        return NULL;
    }

    wl_skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return arr;
    }

    while (**p) {
        WlJsonVal *val = wl_json_parse_val(p, err, errcap);
        if (!val) {
            wl_json_free(arr);
            return NULL;
        }

        if (arr->u.arr.count >= arr->u.arr.cap) {
            arr->u.arr.cap *= 2;
            WlJsonVal **new_items = realloc(arr->u.arr.items, arr->u.arr.cap * sizeof(WlJsonVal *));
            if (!new_items) {
                wl_json_free(val);
                wl_json_free(arr);
                if (err && errcap > 0) snprintf(err, errcap, "out of memory");
                return NULL;
            }
            arr->u.arr.items = new_items;
        }
        arr->u.arr.items[arr->u.arr.count++] = val;

        wl_skip_ws(p);
        if (**p == ']') {
            (*p)++;
            return arr;
        }
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (err && errcap > 0) snprintf(err, errcap, "expected ',' or ']' in array");
        wl_json_free(arr);
        return NULL;
    }
    if (err && errcap > 0) snprintf(err, errcap, "unterminated array");
    wl_json_free(arr);
    return NULL;
}

// Parses an arbitrary JSON value (object, array, string, number, bool, null).
static WlJsonVal *wl_json_parse_val(const char **p, char *err, size_t errcap) {
    wl_skip_ws(p);
    char c = **p;
    if (c == '{') {
        return wl_json_parse_object(p, err, errcap);
    }
    if (c == '[') {
        return wl_json_parse_array(p, err, errcap);
    }
    if (c == '\"') {
        char *s = wl_parse_string_raw(p, err, errcap);
        if (!s) return NULL;
        WlJsonVal *val = calloc(1, sizeof(WlJsonVal));
        if (!val) {
            free(s);
            if (err && errcap > 0) snprintf(err, errcap, "out of memory");
            return NULL;
        }
        val->type = WL_JSON_STRING;
        val->u.str = s;
        return val;
    }
    if (strncmp(*p, "true", 4) == 0 && !isalnum((unsigned char)(*p)[4])) {
        *p += 4;
        WlJsonVal *val = calloc(1, sizeof(WlJsonVal));
        if (!val) {
            if (err && errcap > 0) snprintf(err, errcap, "out of memory");
            return NULL;
        }
        val->type = WL_JSON_BOOL;
        val->u.bool_val = 1;
        return val;
    }
    if (strncmp(*p, "false", 5) == 0 && !isalnum((unsigned char)(*p)[5])) {
        *p += 5;
        WlJsonVal *val = calloc(1, sizeof(WlJsonVal));
        if (!val) {
            if (err && errcap > 0) snprintf(err, errcap, "out of memory");
            return NULL;
        }
        val->type = WL_JSON_BOOL;
        val->u.bool_val = 0;
        return val;
    }
    if (strncmp(*p, "null", 4) == 0 && !isalnum((unsigned char)(*p)[4])) {
        *p += 4;
        WlJsonVal *val = calloc(1, sizeof(WlJsonVal));
        if (!val) {
            if (err && errcap > 0) snprintf(err, errcap, "out of memory");
            return NULL;
        }
        val->type = WL_JSON_NULL;
        return val;
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        const char *start = *p;
        int is_float = 0;
        char raw[64];
        size_t rlen = 0;

        if (**p == '-') {
            if (rlen < sizeof(raw) - 1) raw[rlen++] = **p;
            (*p)++;
        }
        while (isdigit((unsigned char)**p)) {
            if (rlen < sizeof(raw) - 1) raw[rlen++] = **p;
            (*p)++;
        }
        if (**p == '.') {
            is_float = 1;
            if (rlen < sizeof(raw) - 1) raw[rlen++] = **p;
            (*p)++;
            while (isdigit((unsigned char)**p)) {
                if (rlen < sizeof(raw) - 1) raw[rlen++] = **p;
                (*p)++;
            }
        }
        if (**p == 'e' || **p == 'E') {
            is_float = 1;
            if (rlen < sizeof(raw) - 1) raw[rlen++] = **p;
            (*p)++;
            if (**p == '+' || **p == '-') {
                if (rlen < sizeof(raw) - 1) raw[rlen++] = **p;
                (*p)++;
            }
            while (isdigit((unsigned char)**p)) {
                if (rlen < sizeof(raw) - 1) raw[rlen++] = **p;
                (*p)++;
            }
        }
        raw[rlen] = '\0';

        char *endptr = NULL;
        double d = strtod(start, &endptr);
        int64_t i = (int64_t)strtoll(start, NULL, 10);

        WlJsonVal *val = calloc(1, sizeof(WlJsonVal));
        if (!val) {
            if (err && errcap > 0) snprintf(err, errcap, "out of memory");
            return NULL;
        }
        val->type = WL_JSON_NUMBER;
        val->u.num.dval = d;
        val->u.num.ival = i;
        val->u.num.is_float = is_float;
        memcpy(val->u.num.raw, raw, rlen + 1);
        return val;
    }

    if (!c) {
        if (err && errcap > 0) snprintf(err, errcap, "unexpected end of input");
    } else {
        if (err && errcap > 0) snprintf(err, errcap, "unexpected character '%c'", c);
    }
    return NULL;
}

// Parses JSON text into a WlJsonVal tree.
// Trailing whitespace is permitted, but extra characters result in an error.
WlJsonVal *wl_json_parse(const char *text, char *err, size_t errcap) {
    if (err && errcap > 0) err[0] = '\0';
    if (!text) {
        if (err && errcap > 0) snprintf(err, errcap, "unexpected end of input");
        return NULL;
    }
    const char *p = text;
    WlJsonVal *v = wl_json_parse_val(&p, err, errcap);
    if (!v) return NULL;
    wl_skip_ws(&p);
    if (*p != '\0') {
        if (err && errcap > 0) snprintf(err, errcap, "extra trailing characters after JSON");
        wl_json_free(v);
        return NULL;
    }
    return v;
}

// ==========================================================================
// Public JSON Accessors and Helpers
// ==========================================================================

// Look up a key in a JSON object.
// Returns a pointer to the value, or NULL if missing or not an object.
const WlJsonVal *wl_json_get(const WlJsonVal *obj, const char *key) {
    if (!obj || obj->type != WL_JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->u.obj.count; i++) {
        if (strcmp(obj->u.obj.members[i].key, key) == 0) {
            return obj->u.obj.members[i].val;
        }
    }
    return NULL;
}

// Returns the string pointer if v is a string; otherwise returns fallback.
const char *wl_json_str(const WlJsonVal *v, const char *fallback) {
    if (!v || v->type != WL_JSON_STRING || !v->u.str) return fallback;
    return v->u.str;
}

// Returns 1 if v is a boolean with value true; otherwise returns 0.
int wl_json_is_true(const WlJsonVal *v) {
    if (!v || v->type != WL_JSON_BOOL) return 0;
    return v->u.bool_val != 0;
}

// Returns the element count if arr is an array; otherwise returns 0.
size_t wl_json_len(const WlJsonVal *arr) {
    if (!arr || arr->type != WL_JSON_ARRAY) return 0;
    return arr->u.arr.count;
}

// Returns the element at index i if arr is an array and i < len; otherwise NULL.
const WlJsonVal *wl_json_at(const WlJsonVal *arr, size_t i) {
    if (!arr || arr->type != WL_JSON_ARRAY || i >= arr->u.arr.count) return NULL;
    return arr->u.arr.items[i];
}

// Returns the numeric value (as double) if v is a number; otherwise fallback.
double wl_json_num(const WlJsonVal *v, double fallback) {
    if (!v || v->type != WL_JSON_NUMBER) return fallback;
    return v->u.num.dval;
}

// ==========================================================================
// Portable File IO Helper
// ==========================================================================

// Reads an entire file into a malloc'd, NUL-terminated buffer.
// Returns NULL on error with errno preserved from fopen / ftell / malloc.
char *wl_read_file(const char *path, size_t *len) {
    if (len) *len = 0;
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        errno = ENOMEM;
        return NULL;
    }

    if (sz > 0) {
        size_t nread = fread(buf, 1, (size_t)sz, f);
        if (nread != (size_t)sz) {
            fclose(f);
            free(buf);
            return NULL;
        }
    }
    buf[sz] = '\0';
    fclose(f);

    if (len) *len = (size_t)sz;
    return buf;
}
