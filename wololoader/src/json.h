// ==========================================================================
// Wololoader - Minimal JSON Parser and File Reader
//
// Purpose:
//   Lightweight, self-contained JSON parser and binary file reader for the
//   Wololoader DLL and its dat builder (mirroring Python's json.load() and
//   file reading in tools/build.py).
//
// Why a custom parser:
//   Wololoader runs as a 32-bit version.dll proxy injected into AoK HD.exe.
//   To keep the DLL self-contained and free of external dependencies, we use
//   this clean, zero-dependency C11 parser.
// ==========================================================================

#ifndef WL_JSON_H
#define WL_JSON_H

#include <stddef.h>
#include <stdint.h>

// Kind / type enumeration for JSON values
typedef enum {
    WL_JSON_NULL,
    WL_JSON_BOOL,
    WL_JSON_NUMBER,
    WL_JSON_STRING,
    WL_JSON_ARRAY,
    WL_JSON_OBJECT
} WlJsonType;

typedef WlJsonType WlJsonKind;

typedef struct WlJsonVal WlJsonVal;

typedef struct {
    char *key;
    WlJsonVal *val;
} WlJsonMember;

// JSON value node
// Number representations preserve both numeric and raw string forms so error
// messages can match Python's exact repr() format.
struct WlJsonVal {
    WlJsonType type;
    union {
        int bool_val;
        struct {
            double dval;
            int64_t ival;
            int is_float;
            char raw[64];
        } num;
        char *str;
        struct {
            WlJsonVal **items;
            size_t count;
            size_t cap;
        } arr;
        struct {
            WlJsonMember *members;
            size_t count;
            size_t cap;
        } obj;
    } u;
};

// ==========================================================================
// Public JSON API
// ==========================================================================

// Parse JSON text into an allocated WlJsonVal tree.
// Returns the root value on success, or NULL on error (err is populated).
WlJsonVal *wl_json_parse(const char *text, char *err, size_t errcap);

// Recursively free a WlJsonVal tree. Safe to call with NULL.
void wl_json_free(WlJsonVal *v);

// Look up a key in a JSON object.
// Returns a pointer to the value, or NULL if missing or not an object.
const WlJsonVal *wl_json_get(const WlJsonVal *obj, const char *key);

// Convenience accessors and type helpers:
// Returns the string pointer if v is a string; otherwise returns fallback.
const char *wl_json_str(const WlJsonVal *v, const char *fallback);

// Returns 1 if v is a boolean with value true; otherwise returns 0.
int wl_json_is_true(const WlJsonVal *v);

// Returns the element count if arr is an array; otherwise returns 0.
size_t wl_json_len(const WlJsonVal *arr);

// Returns the element at index i if arr is an array and i < len; otherwise NULL.
const WlJsonVal *wl_json_at(const WlJsonVal *arr, size_t i);

// Returns the numeric value (as double) if v is a number; otherwise fallback.
double wl_json_num(const WlJsonVal *v, double fallback);

// ==========================================================================
// Portable File IO Helper
// ==========================================================================

// Reads an entire file into a malloc'd, NUL-terminated buffer.
// Portable across POSIX and Windows (uses standard C binary fopen/fread).
// If len is non-NULL, *len is set to the byte size (excluding NUL terminator).
// Returns NULL on error with errno preserved.
char *wl_read_file(const char *path, size_t *len);

#endif // WL_JSON_H
