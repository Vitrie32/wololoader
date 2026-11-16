// ==========================================================================
// Scriptorium - Mod and Civ IO Implementation
//
// Purpose:
//   Loads and saves Wololoader mods and civ JSON definitions. Ensures:
//   - Preservation of unknown and advanced keys ("tech_tree", "team_bonus", etc.)
//   - In-place key updates to retain original JSON key order
//   - Pretty-printed JSON output (2-space indentation)
//   - Atomic file saving (write to temp file, then replace target)
//   - Validation via datbuild.c (wl_dat_build)
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "modio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

// ==========================================================================
// Dynamic String Buffer for JSON Pretty-Printing
// ==========================================================================

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} ScBuf;

static void sc_buf_init(ScBuf *b) {
    b->cap = 1024;
    b->len = 0;
    b->data = malloc(b->cap);
    if (b->data) b->data[0] = '\0';
}

static void sc_buf_append(ScBuf *b, const char *s, size_t n) {
    if (!b->data) return;
    if (b->len + n + 1 >= b->cap) {
        size_t new_cap = (b->cap * 2 > b->len + n + 1024) ? b->cap * 2 : b->len + n + 1024;
        char *new_data = realloc(b->data, new_cap);
        if (!new_data) return;
        b->data = new_data;
        b->cap  = new_cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void sc_buf_puts(ScBuf *b, const char *s) {
    sc_buf_append(b, s, strlen(s));
}

static void sc_buf_indent(ScBuf *b, int indent) {
    for (int i = 0; i < indent; i++) {
        sc_buf_append(b, " ", 1);
    }
}

// ==========================================================================
// JSON AST Helpers (Tree Construction & Key Updates)
// ==========================================================================

static WlJsonVal *sc_json_alloc_val(WlJsonType type) {
    WlJsonVal *v = calloc(1, sizeof(WlJsonVal));
    if (v) v->type = type;
    return v;
}

static WlJsonVal *sc_json_new_bool(int b) {
    WlJsonVal *v = sc_json_alloc_val(WL_JSON_BOOL);
    if (v) v->u.bool_val = b ? 1 : 0;
    return v;
}

static WlJsonVal *sc_json_new_int(int64_t ival) {
    WlJsonVal *v = sc_json_alloc_val(WL_JSON_NUMBER);
    if (v) {
        v->u.num.ival = ival;
        v->u.num.dval = (double)ival;
        v->u.num.is_float = 0;
        snprintf(v->u.num.raw, sizeof(v->u.num.raw), "%lld", (long long)ival);
    }
    return v;
}

static WlJsonVal *sc_json_new_float(double dval) {
    WlJsonVal *v = sc_json_alloc_val(WL_JSON_NUMBER);
    if (v) {
        v->u.num.ival = (int64_t)dval;
        v->u.num.dval = dval;
        v->u.num.is_float = 1;
        snprintf(v->u.num.raw, sizeof(v->u.num.raw), "%.6g", dval);
        if (!strchr(v->u.num.raw, '.') && !strchr(v->u.num.raw, 'e') && !strchr(v->u.num.raw, 'E')) {
            strncat(v->u.num.raw, ".0", sizeof(v->u.num.raw) - strlen(v->u.num.raw) - 1);
        }
    }
    return v;
}

static WlJsonVal *sc_json_new_str(const char *s) {
    WlJsonVal *v = sc_json_alloc_val(WL_JSON_STRING);
    if (v) {
        v->u.str = strdup(s ? s : "");
    }
    return v;
}

static WlJsonVal *sc_json_new_arr(void) {
    WlJsonVal *v = sc_json_alloc_val(WL_JSON_ARRAY);
    return v;
}

static void sc_json_arr_append(WlJsonVal *arr, WlJsonVal *item) {
    if (!arr || arr->type != WL_JSON_ARRAY || !item) return;
    if (arr->u.arr.count >= arr->u.arr.cap) {
        size_t ncap = arr->u.arr.cap ? arr->u.arr.cap * 2 : 4;
        WlJsonVal **nitems = realloc(arr->u.arr.items, ncap * sizeof(WlJsonVal *));
        if (!nitems) return;
        arr->u.arr.items = nitems;
        arr->u.arr.cap   = ncap;
    }
    arr->u.arr.items[arr->u.arr.count++] = item;
}

static WlJsonVal *sc_json_new_obj(void) {
    WlJsonVal *v = sc_json_alloc_val(WL_JSON_OBJECT);
    return v;
}

static void sc_json_obj_set_val(WlJsonVal *obj, const char *key, WlJsonVal *val) {
    if (!obj || obj->type != WL_JSON_OBJECT || !key || !val) return;

    // Check if key already exists; if so, replace value in-place to preserve key order
    for (size_t i = 0; i < obj->u.obj.count; i++) {
        if (strcmp(obj->u.obj.members[i].key, key) == 0) {
            wl_json_free(obj->u.obj.members[i].val);
            obj->u.obj.members[i].val = val;
            return;
        }
    }

    // Append new member
    if (obj->u.obj.count >= obj->u.obj.cap) {
        size_t ncap = obj->u.obj.cap ? obj->u.obj.cap * 2 : 8;
        WlJsonMember *nmem = realloc(obj->u.obj.members, ncap * sizeof(WlJsonMember));
        if (!nmem) return;
        obj->u.obj.members = nmem;
        obj->u.obj.cap     = ncap;
    }
    obj->u.obj.members[obj->u.obj.count].key = strdup(key);
    obj->u.obj.members[obj->u.obj.count].val = val;
    obj->u.obj.count++;
}

static void sc_json_obj_set_str(WlJsonVal *obj, const char *key, const char *val) {
    sc_json_obj_set_val(obj, key, sc_json_new_str(val));
}

static void sc_json_obj_set_int(WlJsonVal *obj, const char *key, int64_t val) {
    sc_json_obj_set_val(obj, key, sc_json_new_int(val));
}

static void sc_json_obj_set_bool(WlJsonVal *obj, const char *key, int b) {
    sc_json_obj_set_val(obj, key, sc_json_new_bool(b));
}

static void sc_json_obj_remove(WlJsonVal *obj, const char *key) {
    if (!obj || obj->type != WL_JSON_OBJECT || !key) return;
    for (size_t i = 0; i < obj->u.obj.count; i++) {
        if (strcmp(obj->u.obj.members[i].key, key) == 0) {
            free(obj->u.obj.members[i].key);
            wl_json_free(obj->u.obj.members[i].val);
            for (size_t j = i; j + 1 < obj->u.obj.count; j++) {
                obj->u.obj.members[j] = obj->u.obj.members[j + 1];
            }
            obj->u.obj.count--;
            return;
        }
    }
}

// ==========================================================================
// Pretty-Printed JSON Serializer
// ==========================================================================

static void sc_write_escaped_string(ScBuf *b, const char *s) {
    sc_buf_puts(b, "\"");
    if (!s) {
        sc_buf_puts(b, "\"");
        return;
    }
    for (const char *p = s; *p; ) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  sc_buf_puts(b, "\\\""); p++; break;
            case '\\': sc_buf_puts(b, "\\\\"); p++; break;
            case '\b': sc_buf_puts(b, "\\b");  p++; break;
            case '\f': sc_buf_puts(b, "\\f");  p++; break;
            case '\n': sc_buf_puts(b, "\\n");  p++; break;
            case '\r': sc_buf_puts(b, "\\r");  p++; break;
            case '\t': sc_buf_puts(b, "\\t");  p++; break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    sc_buf_puts(b, esc);
                    p++;
                } else if (c < 0x80) {
                    sc_buf_append(b, p, 1);
                    p++;
                } else {
                    // Non-ASCII: json files are UTF-8, write the bytes unchanged
                    // (src/json.c now keeps raw UTF-8 bytes as they are).
                    sc_buf_append(b, p, 1);
                    p++;
                }
                break;
        }
    }
    sc_buf_puts(b, "\"");
}

// Determines if an array should be formatted on a single line
static int sc_is_inline_array(const WlJsonVal *arr) {
    if (arr->u.arr.count == 0) return 1;
    if (arr->u.arr.count > 16) return 0;
    for (size_t i = 0; i < arr->u.arr.count; i++) {
        WlJsonType t = arr->u.arr.items[i]->type;
        if (t == WL_JSON_ARRAY || t == WL_JSON_OBJECT) return 0;
    }
    return 1;
}

// Determines if an object should be formatted on a single line (e.g. effects or unit stats)
static int sc_is_inline_object(const WlJsonVal *obj) {
    if (obj->u.obj.count == 0) return 1;
    if (obj->u.obj.count > 6) return 0;
    for (size_t i = 0; i < obj->u.obj.count; i++) {
        WlJsonType t = obj->u.obj.members[i].val->type;
        if (t == WL_JSON_ARRAY || t == WL_JSON_OBJECT) return 0;
    }
    return 1;
}

static void sc_json_write_val(ScBuf *b, const WlJsonVal *v, int indent, int is_root) {
    if (!v) {
        sc_buf_puts(b, "null");
        return;
    }

    switch (v->type) {
        case WL_JSON_NULL:
            sc_buf_puts(b, "null");
            break;

        case WL_JSON_BOOL:
            sc_buf_puts(b, v->u.bool_val ? "true" : "false");
            break;

        case WL_JSON_NUMBER:
            if (v->u.num.raw[0] != '\0') {
                sc_buf_puts(b, v->u.num.raw);
            } else if (v->u.num.is_float) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%.6g", v->u.num.dval);
                if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'E')) {
                    strncat(tmp, ".0", sizeof(tmp) - strlen(tmp) - 1);
                }
                sc_buf_puts(b, tmp);
            } else {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%lld", (long long)v->u.num.ival);
                sc_buf_puts(b, tmp);
            }
            break;

        case WL_JSON_STRING:
            sc_write_escaped_string(b, v->u.str);
            break;

        case WL_JSON_ARRAY:
            if (v->u.arr.count == 0) {
                sc_buf_puts(b, "[]");
            } else if (sc_is_inline_array(v)) {
                sc_buf_puts(b, "[");
                for (size_t i = 0; i < v->u.arr.count; i++) {
                    if (i > 0) sc_buf_puts(b, ", ");
                    sc_json_write_val(b, v->u.arr.items[i], 0, 0);
                }
                sc_buf_puts(b, "]");
            } else {
                sc_buf_puts(b, "[\n");
                for (size_t i = 0; i < v->u.arr.count; i++) {
                    sc_buf_indent(b, indent + 2);
                    sc_json_write_val(b, v->u.arr.items[i], indent + 2, 0);
                    if (i + 1 < v->u.arr.count) {
                        sc_buf_puts(b, ",\n");
                    } else {
                        sc_buf_puts(b, "\n");
                    }
                }
                sc_buf_indent(b, indent);
                sc_buf_puts(b, "]");
            }
            break;

        case WL_JSON_OBJECT:
            if (v->u.obj.count == 0) {
                sc_buf_puts(b, "{}");
            } else if (!is_root && sc_is_inline_object(v)) {
                sc_buf_puts(b, "{ ");
                for (size_t i = 0; i < v->u.obj.count; i++) {
                    if (i > 0) sc_buf_puts(b, ", ");
                    sc_write_escaped_string(b, v->u.obj.members[i].key);
                    sc_buf_puts(b, ": ");
                    sc_json_write_val(b, v->u.obj.members[i].val, 0, 0);
                }
                sc_buf_puts(b, " }");
            } else {
                sc_buf_puts(b, "{\n");
                for (size_t i = 0; i < v->u.obj.count; i++) {
                    sc_buf_indent(b, indent + 2);
                    sc_write_escaped_string(b, v->u.obj.members[i].key);
                    sc_buf_puts(b, ": ");
                    sc_json_write_val(b, v->u.obj.members[i].val, indent + 2, 0);
                    if (i + 1 < v->u.obj.count) {
                        sc_buf_puts(b, ",\n");
                    } else {
                        sc_buf_puts(b, "\n");
                    }
                }
                sc_buf_indent(b, indent);
                sc_buf_puts(b, "}");
            }
            break;
    }
}

char *modio_json_write_pretty(const WlJsonVal *val, size_t *out_len) {
    ScBuf b;
    sc_buf_init(&b);
    sc_json_write_val(&b, val, 0, 1);
    sc_buf_puts(&b, "\n");
    if (out_len) *out_len = b.len;
    return b.data;
}

// ==========================================================================
// Semantic JSON AST Equality Check
// ==========================================================================

int modio_json_equal(const WlJsonVal *a, const WlJsonVal *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->type != b->type) return 0;

    switch (a->type) {
        case WL_JSON_NULL:
            return 1;

        case WL_JSON_BOOL:
            return a->u.bool_val == b->u.bool_val;

        case WL_JSON_NUMBER: {
            if (a->u.num.raw[0] && b->u.num.raw[0] && strcmp(a->u.num.raw, b->u.num.raw) == 0) {
                return 1;
            }
            if (a->u.num.is_float || b->u.num.is_float) {
                return fabs(a->u.num.dval - b->u.num.dval) < 1e-5;
            }
            return a->u.num.ival == b->u.num.ival;
        }

        case WL_JSON_STRING:
            return strcmp(a->u.str, b->u.str) == 0;

        case WL_JSON_ARRAY:
            if (a->u.arr.count != b->u.arr.count) return 0;
            for (size_t i = 0; i < a->u.arr.count; i++) {
                if (!modio_json_equal(a->u.arr.items[i], b->u.arr.items[i])) return 0;
            }
            return 1;

        case WL_JSON_OBJECT: {
            if (a->u.obj.count != b->u.obj.count) return 0;
            for (size_t i = 0; i < a->u.obj.count; i++) {
                const char *key = a->u.obj.members[i].key;
                const WlJsonVal *b_val = wl_json_get(b, key);
                if (!b_val) return 0;
                if (!modio_json_equal(a->u.obj.members[i].val, b_val)) return 0;
            }
            return 1;
        }
    }
    return 0;
}

// ==========================================================================
// Atomic File Writer
// ==========================================================================

int modio_atomic_write_file(const char *path, const char *data, size_t len, char *err, size_t errcap) {
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        if (err && errcap > 0) {
            snprintf(err, errcap, "Failed to create temp file '%s'", tmp_path);
        }
        return -1;
    }

    if (len > 0) {
        if (fwrite(data, 1, len, f) != len) {
            fclose(f);
            remove(tmp_path);
            if (err && errcap > 0) {
                snprintf(err, errcap, "Failed writing data to '%s'", tmp_path);
            }
            return -1;
        }
    }
    fflush(f);
    fclose(f);

    // Atomically replace destination file
    if (!MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // Fallback for older systems or wine if destination exists
        remove(path);
        if (!MoveFileA(tmp_path, path)) {
            remove(tmp_path);
            if (err && errcap > 0) {
                snprintf(err, errcap, "Failed to replace '%s' with '%s' (Win32 error %lu)",
                         path, tmp_path, (unsigned long)GetLastError());
            }
            return -1;
        }
    }

    return 0;
}

// ==========================================================================
// Parsing & Deserialization
// ==========================================================================

static void sc_parse_civ_data(ScCivData *civ, const WlJsonVal *root) {
    // Basic identity
    const WlJsonVal *v_name = wl_json_get(root, "name");
    if (v_name && v_name->type == WL_JSON_STRING) {
        snprintf(civ->name, sizeof(civ->name), "%s", v_name->u.str);
    }
    const WlJsonVal *v_dat = wl_json_get(root, "dat_name");
    if (v_dat && v_dat->type == WL_JSON_STRING) {
        snprintf(civ->dat_name, sizeof(civ->dat_name), "%s", v_dat->u.str);
    }
    const WlJsonVal *v_base = wl_json_get(root, "base");
    if (v_base && v_base->type == WL_JSON_NUMBER) {
        civ->base = (int)v_base->u.num.ival;
    } else {
        civ->base = 1;
    }

    const WlJsonVal *v_desc = wl_json_get(root, "description");
    if (v_desc && v_desc->type == WL_JSON_STRING) {
        civ->description = strdup(v_desc->u.str);
    } else {
        civ->description = strdup("");
    }

    const WlJsonVal *v_snd = wl_json_get(root, "sound");
    if (v_snd && v_snd->type == WL_JSON_STRING) {
        snprintf(civ->sound, sizeof(civ->sound), "%s", v_snd->u.str);
    }
    const WlJsonVal *v_art = wl_json_get(root, "interface_slp");
    if (v_art && v_art->type == WL_JSON_STRING) {
        snprintf(civ->interface_slp, sizeof(civ->interface_slp), "%s", v_art->u.str);
    }

    // AI names
    const WlJsonVal *v_ai = wl_json_get(root, "ai_names");
    if (v_ai && v_ai->type == WL_JSON_ARRAY) {
        civ->ai_name_count = v_ai->u.arr.count;
        civ->ai_names = calloc(civ->ai_name_count, sizeof(char *));
        for (size_t i = 0; i < civ->ai_name_count; i++) {
            const WlJsonVal *item = v_ai->u.arr.items[i];
            civ->ai_names[i] = strdup(item && item->type == WL_JSON_STRING ? item->u.str : "");
        }
    }

    // Unique techs
    civ->unique_techs[0] = -1;
    civ->unique_techs[1] = -1;
    const WlJsonVal *v_utech = wl_json_get(root, "unique_techs");
    if (v_utech && v_utech->type == WL_JSON_ARRAY) {
        if (v_utech->u.arr.count > 0 && v_utech->u.arr.items[0]->type == WL_JSON_NUMBER) {
            civ->unique_techs[0] = (int)v_utech->u.arr.items[0]->u.num.ival;
        }
        if (v_utech->u.arr.count > 1 && v_utech->u.arr.items[1]->type == WL_JSON_NUMBER) {
            civ->unique_techs[1] = (int)v_utech->u.arr.items[1]->u.num.ival;
        }
    }

    // Int array helper
    #define PARSE_INT_ARR(key, out_arr, out_cnt) do { \
        const WlJsonVal *v_arr = wl_json_get(root, key); \
        if (v_arr && v_arr->type == WL_JSON_ARRAY) { \
            out_cnt = v_arr->u.arr.count; \
            out_arr = calloc(out_cnt, sizeof(int)); \
            for (size_t k = 0; k < out_cnt; k++) { \
                if (v_arr->u.arr.items[k]->type == WL_JSON_NUMBER) { \
                    out_arr[k] = (int)v_arr->u.arr.items[k]->u.num.ival; \
                } \
            } \
        } \
    } while(0)

    PARSE_INT_ARR("enabled_techs", civ->enabled_techs, civ->enabled_tech_count);
    PARSE_INT_ARR("disabled_techs", civ->disabled_techs, civ->disabled_tech_count);
    PARSE_INT_ARR("enabled_units", civ->enabled_units, civ->enabled_unit_count);
    PARSE_INT_ARR("disabled_units", civ->disabled_units, civ->disabled_unit_count);
    #undef PARSE_INT_ARR

    // Effects helper
    #define PARSE_EFFECTS(key, out_eff, out_cnt) do { \
        const WlJsonVal *v_eff = wl_json_get(root, key); \
        if (v_eff && v_eff->type == WL_JSON_ARRAY) { \
            out_cnt = v_eff->u.arr.count; \
            out_eff = calloc(out_cnt, sizeof(ScEffect)); \
            for (size_t k = 0; k < out_cnt; k++) { \
                const WlJsonVal *item = v_eff->u.arr.items[k]; \
                if (item && item->type == WL_JSON_OBJECT) { \
                    const WlJsonVal *v_t = wl_json_get(item, "type"); \
                    const WlJsonVal *v_a = wl_json_get(item, "a"); \
                    const WlJsonVal *v_b = wl_json_get(item, "b"); \
                    const WlJsonVal *v_c = wl_json_get(item, "c"); \
                    const WlJsonVal *v_d = wl_json_get(item, "d"); \
                    out_eff[k].type = (v_t && v_t->type == WL_JSON_NUMBER) ? (int)v_t->u.num.ival : 0; \
                    out_eff[k].a    = (v_a && v_a->type == WL_JSON_NUMBER) ? (int)v_a->u.num.ival : 0; \
                    out_eff[k].b    = (v_b && v_b->type == WL_JSON_NUMBER) ? (int)v_b->u.num.ival : 0; \
                    out_eff[k].c    = (v_c && v_c->type == WL_JSON_NUMBER) ? (int)v_c->u.num.ival : 0; \
                    out_eff[k].d    = (v_d && v_d->type == WL_JSON_NUMBER) ? v_d->u.num.dval : 0.0; \
                } \
            } \
        } \
    } while(0)

    PARSE_EFFECTS("effects", civ->effects, civ->effect_count);
    PARSE_EFFECTS("team_bonus_effects", civ->team_bonus_effects, civ->team_bonus_effect_count);
    #undef PARSE_EFFECTS

    // Unit overrides
    const WlJsonVal *v_units = wl_json_get(root, "units");
    if (v_units && v_units->type == WL_JSON_OBJECT) {
        civ->unit_override_count = v_units->u.obj.count;
        civ->unit_overrides = calloc(civ->unit_override_count, sizeof(ScUnitOverride));
        for (size_t i = 0; i < v_units->u.obj.count; i++) {
            civ->unit_overrides[i].unit_id = atoi(v_units->u.obj.members[i].key);
            const WlJsonVal *u_stats = v_units->u.obj.members[i].val;
            if (u_stats && u_stats->type == WL_JSON_OBJECT) {
                for (int s = 0; s < SC_STAT_COUNT; s++) {
                    const WlJsonVal *f_val = wl_json_get(u_stats, SC_STAT_FIELD[s]);
                    if (f_val && f_val->type == WL_JSON_NUMBER) {
                        civ->unit_overrides[i].has[s]   = 1;
                        civ->unit_overrides[i].value[s] = (float)f_val->u.num.dval;
                    }
                }
            }
        }
    }
}

// ==========================================================================
// Syncing In-Memory Changes to JSON AST
// ==========================================================================

void modio_sync_civ_to_json(ScCivData *civ) {
    if (!civ) return;
    if (!civ->root_json) {
        civ->root_json = sc_json_new_obj();
    }

    sc_json_obj_set_str(civ->root_json, "name", civ->name);
    sc_json_obj_set_str(civ->root_json, "dat_name", civ->dat_name);
    sc_json_obj_set_int(civ->root_json, "base", civ->base);
    sc_json_obj_set_str(civ->root_json, "description", civ->description ? civ->description : "");

    // AI names
    WlJsonVal *ai_arr = sc_json_new_arr();
    for (size_t i = 0; i < civ->ai_name_count; i++) {
        sc_json_arr_append(ai_arr, sc_json_new_str(civ->ai_names[i]));
    }
    sc_json_obj_set_val(civ->root_json, "ai_names", ai_arr);

    // Sound / Art
    if (civ->sound[0]) {
        sc_json_obj_set_str(civ->root_json, "sound", civ->sound);
    } else {
        sc_json_obj_remove(civ->root_json, "sound");
    }

    if (civ->interface_slp[0]) {
        sc_json_obj_set_str(civ->root_json, "interface_slp", civ->interface_slp);
    } else {
        sc_json_obj_remove(civ->root_json, "interface_slp");
    }

    // Enabled/Disabled techs and units
    #define SYNC_INT_ARR(key, in_arr, in_cnt) do { \
        if (in_cnt > 0 || wl_json_get(civ->root_json, key)) { \
            WlJsonVal *arr = sc_json_new_arr(); \
            for (size_t k = 0; k < in_cnt; k++) { \
                sc_json_arr_append(arr, sc_json_new_int(in_arr[k])); \
            } \
            sc_json_obj_set_val(civ->root_json, key, arr); \
        } \
    } while(0)

    SYNC_INT_ARR("enabled_techs", civ->enabled_techs, civ->enabled_tech_count);
    SYNC_INT_ARR("disabled_techs", civ->disabled_techs, civ->disabled_tech_count);
    SYNC_INT_ARR("enabled_units", civ->enabled_units, civ->enabled_unit_count);
    SYNC_INT_ARR("disabled_units", civ->disabled_units, civ->disabled_unit_count);
    #undef SYNC_INT_ARR

    // Effects
    #define SYNC_EFFECTS(key, in_eff, in_cnt) do { \
        if (in_cnt > 0 || wl_json_get(civ->root_json, key)) { \
            WlJsonVal *arr = sc_json_new_arr(); \
            for (size_t k = 0; k < in_cnt; k++) { \
                WlJsonVal *e_obj = sc_json_new_obj(); \
                sc_json_obj_set_int(e_obj, "type", in_eff[k].type); \
                sc_json_obj_set_int(e_obj, "a", in_eff[k].a); \
                sc_json_obj_set_int(e_obj, "b", in_eff[k].b); \
                sc_json_obj_set_int(e_obj, "c", in_eff[k].c); \
                sc_json_obj_set_val(e_obj, "d", sc_json_new_float(in_eff[k].d)); \
                sc_json_arr_append(arr, e_obj); \
            } \
            sc_json_obj_set_val(civ->root_json, key, arr); \
        } \
    } while(0)

    SYNC_EFFECTS("effects", civ->effects, civ->effect_count);
    SYNC_EFFECTS("team_bonus_effects", civ->team_bonus_effects, civ->team_bonus_effect_count);
    #undef SYNC_EFFECTS

    // Unique techs
    if (civ->unique_techs[0] >= 0 || civ->unique_techs[1] >= 0 || wl_json_get(civ->root_json, "unique_techs")) {
        WlJsonVal *u_arr = sc_json_new_arr();
        sc_json_arr_append(u_arr, sc_json_new_int(civ->unique_techs[0]));
        sc_json_arr_append(u_arr, sc_json_new_int(civ->unique_techs[1]));
        sc_json_obj_set_val(civ->root_json, "unique_techs", u_arr);
    }

    // Units
    if (civ->unit_override_count > 0 || wl_json_get(civ->root_json, "units")) {
        WlJsonVal *u_obj = sc_json_new_obj();
        for (size_t i = 0; i < civ->unit_override_count; i++) {
            char u_key[32];
            snprintf(u_key, sizeof(u_key), "%d", civ->unit_overrides[i].unit_id);
            WlJsonVal *stat_obj = sc_json_new_obj();
            for (int s = 0; s < SC_STAT_COUNT; s++) {
                if (civ->unit_overrides[i].has[s]) {
                    float fval = civ->unit_overrides[i].value[s];
                    if (floorf(fval) == fval) {
                        sc_json_obj_set_int(stat_obj, SC_STAT_FIELD[s], (int64_t)fval);
                    } else {
                        sc_json_obj_set_val(stat_obj, SC_STAT_FIELD[s], sc_json_new_float(fval));
                    }
                }
            }
            sc_json_obj_set_val(u_obj, u_key, stat_obj);
        }
        sc_json_obj_set_val(civ->root_json, "units", u_obj);
    }
}

void modio_sync_mod_to_json(ScModData *mod) {
    if (!mod) return;
    if (!mod->root_json) {
        mod->root_json = sc_json_new_obj();
    }
    sc_json_obj_set_str(mod->root_json, "name", mod->name);
    sc_json_obj_set_str(mod->root_json, "version", mod->version);
    sc_json_obj_set_str(mod->root_json, "author", mod->author);
    sc_json_obj_set_str(mod->root_json, "description", mod->description ? mod->description : "");
    if (mod->has_enabled) {
        sc_json_obj_set_bool(mod->root_json, "enabled", mod->enabled);
    }
}

// ==========================================================================
// Mod & Civ Loading / Saving
// ==========================================================================

int modio_load_mod(const char *mod_dir, ScModData *out, char *err, size_t errcap) {
    if (!mod_dir || !out) return -1;
    memset(out, 0, sizeof(*out));

    snprintf(out->dir_path, sizeof(out->dir_path), "%s", mod_dir);

    // Extract folder name from path
    const char *last_slash = strrchr(mod_dir, '/');
    if (!last_slash) last_slash = strrchr(mod_dir, '\\');
    snprintf(out->folder_name, sizeof(out->folder_name), "%s", last_slash ? last_slash + 1 : mod_dir);

    // Read mod.json
    char mod_json_path[MAX_PATH];
    snprintf(mod_json_path, sizeof(mod_json_path), "%s/mod.json", mod_dir);

    size_t fsize = 0;
    char *text = wl_read_file(mod_json_path, &fsize);
    if (!text) {
        if (err && errcap > 0) snprintf(err, errcap, "Missing or unreadable '%s'", mod_json_path);
        return -1;
    }

    out->root_json = wl_json_parse(text, err, errcap);
    free(text);
    if (!out->root_json || out->root_json->type != WL_JSON_OBJECT) {
        if (err && errcap > 0 && !err[0]) snprintf(err, errcap, "Malformed JSON in '%s'", mod_json_path);
        return -1;
    }

    const WlJsonVal *v_name = wl_json_get(out->root_json, "name");
    snprintf(out->name, sizeof(out->name), "%s", v_name && v_name->type == WL_JSON_STRING ? v_name->u.str : out->folder_name);

    const WlJsonVal *v_ver = wl_json_get(out->root_json, "version");
    snprintf(out->version, sizeof(out->version), "%s", v_ver && v_ver->type == WL_JSON_STRING ? v_ver->u.str : "0.1");

    const WlJsonVal *v_auth = wl_json_get(out->root_json, "author");
    snprintf(out->author, sizeof(out->author), "%s", v_auth && v_auth->type == WL_JSON_STRING ? v_auth->u.str : "");

    const WlJsonVal *v_desc = wl_json_get(out->root_json, "description");
    out->description = strdup(v_desc && v_desc->type == WL_JSON_STRING ? v_desc->u.str : "");

    const WlJsonVal *v_en = wl_json_get(out->root_json, "enabled");
    if (v_en && v_en->type == WL_JSON_BOOL) {
        out->has_enabled = 1;
        out->enabled     = v_en->u.bool_val;
    } else {
        out->has_enabled = 0;
        out->enabled     = 1;
    }

    // Scan civs directory
    char civs_dir[MAX_PATH];
    snprintf(civs_dir, sizeof(civs_dir), "%s/civs", mod_dir);

    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s/*.json", civs_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        size_t cap = 4;
        out->civs = calloc(cap, sizeof(ScCivData));

        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (out->civ_count >= cap) {
                    cap *= 2;
                    ScCivData *ncivs = realloc(out->civs, cap * sizeof(ScCivData));
                    if (!ncivs) break;
                    out->civs = ncivs;
                }
                ScCivData *civ = &out->civs[out->civ_count];
                memset(civ, 0, sizeof(ScCivData));
                snprintf(civ->file_name, sizeof(civ->file_name), "%s", fd.cFileName);
                snprintf(civ->file_path, sizeof(civ->file_path), "%s/%s", civs_dir, fd.cFileName);

                char *civ_text = wl_read_file(civ->file_path, NULL);
                if (civ_text) {
                    civ->root_json = wl_json_parse(civ_text, NULL, 0);
                    free(civ_text);
                    if (civ->root_json && civ->root_json->type == WL_JSON_OBJECT) {
                        sc_parse_civ_data(civ, civ->root_json);
                        out->civ_count++;
                    } else {
                        wl_json_free(civ->root_json);
                    }
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    out->dirty = 0;
    return 0;
}

int modio_scan_mods(const char *game_dir, ScModData **out_mods, size_t *out_count, char *err, size_t errcap) {
    if (!game_dir || !out_mods || !out_count) return -1;
    *out_mods  = NULL;
    *out_count = 0;

    char mods_path[MAX_PATH];
    snprintf(mods_path, sizeof(mods_path), "%s/wololoader/mods", game_dir);

    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s/*", mods_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0; // No mods folder yet, return empty list
    }

    size_t cap = 8;
    ScModData *mods = calloc(cap, sizeof(ScModData));
    size_t count = 0;

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
            char mdir[MAX_PATH];
            snprintf(mdir, sizeof(mdir), "%s/%s", mods_path, fd.cFileName);

            char mjson[MAX_PATH];
            snprintf(mjson, sizeof(mjson), "%s/mod.json", mdir);

            DWORD attr = GetFileAttributesA(mjson);
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                if (count >= cap) {
                    cap *= 2;
                    ScModData *nmods = realloc(mods, cap * sizeof(ScModData));
                    if (!nmods) break;
                    mods = nmods;
                }
                if (modio_load_mod(mdir, &mods[count], err, errcap) == 0) {
                    count++;
                }
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    *out_mods  = mods;
    *out_count = count;
    return 0;
}

int modio_save_mod(ScModData *mod, const char *dest_dir, char *err, size_t errcap) {
    if (!mod || !dest_dir) return -1;

    // Create dest folder and civs subfolder
    CreateDirectoryA(dest_dir, NULL);
    char civs_dir[MAX_PATH];
    snprintf(civs_dir, sizeof(civs_dir), "%s/civs", dest_dir);
    CreateDirectoryA(civs_dir, NULL);

    // Sync in-memory modifications to AST if dirty
    if (mod->dirty) {
        modio_sync_mod_to_json(mod);
    }

    // Save mod.json
    size_t mod_len = 0;
    char *mod_json_str = modio_json_write_pretty(mod->root_json, &mod_len);
    char mod_json_path[MAX_PATH];
    snprintf(mod_json_path, sizeof(mod_json_path), "%s/mod.json", dest_dir);

    if (modio_atomic_write_file(mod_json_path, mod_json_str, mod_len, err, errcap) != 0) {
        free(mod_json_str);
        return -1;
    }
    free(mod_json_str);

    // Save each civ
    for (size_t i = 0; i < mod->civ_count; i++) {
        ScCivData *civ = &mod->civs[i];
        if (civ->dirty) {
            modio_sync_civ_to_json(civ);
        }

        size_t civ_len = 0;
        char *civ_json_str = modio_json_write_pretty(civ->root_json, &civ_len);
        char civ_path[MAX_PATH];
        snprintf(civ_path, sizeof(civ_path), "%s/%s", civs_dir, civ->file_name);

        if (modio_atomic_write_file(civ_path, civ_json_str, civ_len, err, errcap) != 0) {
            free(civ_json_str);
            return -1;
        }
        free(civ_json_str);

        if (strcmp(mod->dir_path, dest_dir) == 0) {
            civ->dirty = 0;
        }
    }

    if (strcmp(mod->dir_path, dest_dir) == 0) {
        mod->dirty = 0;
    }

    return 0;
}

int modio_create_mod(const char *game_dir, const char *name, char *out_folder, size_t folder_cap, char *err, size_t errcap) {
    if (!game_dir || !name || !name[0]) return -1;

    // Filter folder name to safe alphanumeric characters
    char safe_folder[MAX_PATH];
    size_t k = 0;
    for (size_t i = 0; name[i] && k + 1 < sizeof(safe_folder); i++) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            safe_folder[k++] = c;
        } else if (c == ' ') {
            safe_folder[k++] = '_';
        }
    }
    safe_folder[k] = '\0';
    if (!safe_folder[0]) strcpy(safe_folder, "CustomMod");

    if (out_folder && folder_cap > 0) {
        snprintf(out_folder, folder_cap, "%s", safe_folder);
    }

    char mod_dir[MAX_PATH];
    snprintf(mod_dir, sizeof(mod_dir), "%s/wololoader/mods/%s", game_dir, safe_folder);

    // Create mod directory structure
    char parent_mods[MAX_PATH];
    snprintf(parent_mods, sizeof(parent_mods), "%s/wololoader/mods", game_dir);
    CreateDirectoryA(parent_mods, NULL);
    CreateDirectoryA(mod_dir, NULL);

    char civs_dir[MAX_PATH];
    snprintf(civs_dir, sizeof(civs_dir), "%s/civs", mod_dir);
    CreateDirectoryA(civs_dir, NULL);

    // Write initial mod.json
    WlJsonVal *m_root = sc_json_new_obj();
    sc_json_obj_set_str(m_root, "name", name);
    sc_json_obj_set_str(m_root, "version", "0.1");
    sc_json_obj_set_str(m_root, "author", "");
    sc_json_obj_set_str(m_root, "description", "Created with Scriptorium.");

    size_t mlen = 0;
    char *mstr = modio_json_write_pretty(m_root, &mlen);
    wl_json_free(m_root);

    char mod_json_path[MAX_PATH];
    snprintf(mod_json_path, sizeof(mod_json_path), "%s/mod.json", mod_dir);
    if (modio_atomic_write_file(mod_json_path, mstr, mlen, err, errcap) != 0) {
        free(mstr);
        return -1;
    }
    free(mstr);

    // Write initial civ
    WlJsonVal *c_root = sc_json_new_obj();
    sc_json_obj_set_str(c_root, "name", name);
    sc_json_obj_set_str(c_root, "dat_name", safe_folder);
    sc_json_obj_set_int(c_root, "base", 1);
    char initial_desc[256];
    snprintf(initial_desc, sizeof(initial_desc), "<b>%s<b>\n\nCustom civilization created with Scriptorium.", name);
    sc_json_obj_set_str(c_root, "description", initial_desc);
    sc_json_obj_set_val(c_root, "ai_names", sc_json_new_arr());

    size_t clen = 0;
    char *cstr = modio_json_write_pretty(c_root, &clen);
    wl_json_free(c_root);

    char civ_path[MAX_PATH];
    snprintf(civ_path, sizeof(civ_path), "%s/%s.json", civs_dir, safe_folder);
    if (modio_atomic_write_file(civ_path, cstr, clen, err, errcap) != 0) {
        free(cstr);
        return -1;
    }
    free(cstr);

    return 0;
}

int modio_create_civ(ScModData *mod, const char *civ_name, char *err, size_t errcap) {
    if (!mod || !civ_name || !civ_name[0]) return -1;

    char safe_fn[MAX_PATH];
    size_t k = 0;
    for (size_t i = 0; civ_name[i] && k + 1 < sizeof(safe_fn); i++) {
        char c = civ_name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            safe_fn[k++] = c;
        } else if (c == ' ') {
            safe_fn[k++] = '_';
        }
    }
    safe_fn[k] = '\0';
    if (!safe_fn[0]) strcpy(safe_fn, "custom_civ");

    char file_name[MAX_PATH];
    snprintf(file_name, sizeof(file_name), "%s.json", safe_fn);

    // Expand civs array
    ScCivData *ncivs = realloc(mod->civs, (mod->civ_count + 1) * sizeof(ScCivData));
    if (!ncivs) return -1;
    mod->civs = ncivs;

    ScCivData *civ = &mod->civs[mod->civ_count];
    memset(civ, 0, sizeof(ScCivData));

    snprintf(civ->file_name, sizeof(civ->file_name), "%s", file_name);
    snprintf(civ->file_path, sizeof(civ->file_path), "%s/civs/%s", mod->dir_path, file_name);
    snprintf(civ->name, sizeof(civ->name), "%s", civ_name);
    snprintf(civ->dat_name, sizeof(civ->dat_name), "%.19s", safe_fn);
    civ->base = 1;

    char desc_buf[256];
    snprintf(desc_buf, sizeof(desc_buf), "<b>%s<b>\n\nCustom civilization created with Scriptorium.", civ_name);
    civ->description = strdup(desc_buf);

    civ->unique_techs[0] = -1;
    civ->unique_techs[1] = -1;
    civ->dirty = 1;

    mod->civ_count++;
    mod->dirty = 1;

    return modio_save_mod(mod, mod->dir_path, err, errcap);
}

int modio_delete_civ(ScModData *mod, size_t civ_idx, char *err, size_t errcap) {
    if (!mod || civ_idx >= mod->civ_count) return -1;

    (void)err;
    (void)errcap;

    // Delete file from disk
    remove(mod->civs[civ_idx].file_path);

    // Free civ memory
    modio_free_civ(&mod->civs[civ_idx]);

    // Shift remaining civs
    for (size_t i = civ_idx; i + 1 < mod->civ_count; i++) {
        mod->civs[i] = mod->civs[i + 1];
    }
    mod->civ_count--;
    mod->dirty = 1;

    return 0;
}

// ==========================================================================
// Mod Validation via datbuild.c
// ==========================================================================

int modio_validate_mod(const ScModData *mod, const char *game_dir, const WlZlib *z, char *err, size_t errcap) {
    if (!mod || !game_dir || !z) {
        if (err && errcap > 0) snprintf(err, errcap, "Invalid arguments for validation");
        return -1;
    }

    char temp_root[MAX_PATH];
    DWORD tlen = GetTempPathA(sizeof(temp_root), temp_root);
    if (tlen == 0 || tlen >= sizeof(temp_root)) {
        strcpy(temp_root, "C:\\Temp");
    }

    char temp_dir[MAX_PATH];
    snprintf(temp_dir, sizeof(temp_dir), "%s/sc_val_%lu_%s", temp_root, (unsigned long)GetCurrentProcessId(), mod->folder_name);
    CreateDirectoryA(temp_dir, NULL);

    char temp_mods_dir[MAX_PATH];
    snprintf(temp_mods_dir, sizeof(temp_mods_dir), "%s/mods", temp_dir);
    CreateDirectoryA(temp_mods_dir, NULL);

    char this_mod_dir[MAX_PATH];
    snprintf(this_mod_dir, sizeof(this_mod_dir), "%s/%s", temp_mods_dir, mod->folder_name);

    // Save current mod to temporary mods folder
    ScModData temp_copy = *mod;
    char save_err[256];
    if (modio_save_mod(&temp_copy, this_mod_dir, save_err, sizeof(save_err)) != 0) {
        if (err && errcap > 0) snprintf(err, errcap, "Validation setup failed: %s", save_err);
        return -1;
    }

    char src_dat[MAX_PATH];
    snprintf(src_dat, sizeof(src_dat), "%s/resources/_common/dat/empires2_x2_p1.dat", game_dir);

    char out_dat[MAX_PATH];
    snprintf(out_dat, sizeof(out_dat), "%s/val_out.dat", temp_dir);

    char out_civs_txt[MAX_PATH];
    snprintf(out_civs_txt, sizeof(out_civs_txt), "%s/val_civs.txt", temp_dir);

    char out_strings_txt[MAX_PATH];
    snprintf(out_strings_txt, sizeof(out_strings_txt), "%s/val_strings.txt", temp_dir);

    char out_commands_txt[MAX_PATH];
    snprintf(out_commands_txt, sizeof(out_commands_txt), "%s/val_commands.txt", temp_dir);

    int ret = wl_dat_build(z, src_dat, temp_mods_dir, out_dat, out_civs_txt, out_strings_txt, out_commands_txt, err, errcap);

    // Cleanup temp files
    remove(out_dat);
    remove(out_civs_txt);
    remove(out_strings_txt);
    remove(out_commands_txt);

    // Remove temp civ files and folder
    char temp_civs_dir[MAX_PATH];
    snprintf(temp_civs_dir, sizeof(temp_civs_dir), "%s/civs", this_mod_dir);
    for (size_t i = 0; i < mod->civ_count; i++) {
        char cf[MAX_PATH];
        snprintf(cf, sizeof(cf), "%s/%s", temp_civs_dir, mod->civs[i].file_name);
        remove(cf);
    }
    RemoveDirectoryA(temp_civs_dir);

    char mjson[MAX_PATH];
    snprintf(mjson, sizeof(mjson), "%s/mod.json", this_mod_dir);
    remove(mjson);
    RemoveDirectoryA(this_mod_dir);
    RemoveDirectoryA(temp_mods_dir);
    RemoveDirectoryA(temp_dir);

    return ret;
}

// ==========================================================================
// Memory Cleanup
// ==========================================================================

void modio_free_civ(ScCivData *civ) {
    if (!civ) return;
    wl_json_free(civ->root_json);
    free(civ->description);
    if (civ->ai_names) {
        for (size_t i = 0; i < civ->ai_name_count; i++) {
            free(civ->ai_names[i]);
        }
        free(civ->ai_names);
    }
    free(civ->enabled_techs);
    free(civ->disabled_techs);
    free(civ->enabled_units);
    free(civ->disabled_units);
    free(civ->effects);
    free(civ->team_bonus_effects);
    free(civ->unit_overrides);
    memset(civ, 0, sizeof(*civ));
}

void modio_free_mod(ScModData *mod) {
    if (!mod) return;
    wl_json_free(mod->root_json);
    free(mod->description);
    if (mod->civs) {
        for (size_t i = 0; i < mod->civ_count; i++) {
            modio_free_civ(&mod->civs[i]);
        }
        free(mod->civs);
    }
    memset(mod, 0, sizeof(*mod));
}

void modio_free_mods(ScModData *mods, size_t count) {
    if (!mods) return;
    for (size_t i = 0; i < count; i++) {
        modio_free_mod(&mods[i]);
    }
    free(mods);
}
