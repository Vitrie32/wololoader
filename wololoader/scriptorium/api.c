// ==========================================================================
// Scriptorium v2 - API Endpoint Handlers Implementation
//
// Purpose:
//   Implements every REST endpoint defined in API.md:
//   - GET  /api/info
//   - GET  /api/catalog
//   - GET  /api/stats?civ=C&unit=U
//   - GET  /api/mods
//   - GET  /api/mod/<folder>
//   - PUT  /api/mod/<folder>
//   - POST /api/mods
//   - POST /api/validate
//   - GET  /api/icon/<folder>
//   - POST /api/upload?folder=F&name=N
//   - POST /api/open-folder
//   - POST /api/ping
//   - POST /api/quit
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "api.h"
#include "launcher.h"
#include "modio.h"
#include "catalog.h"
#include "../src/json.h"
#include "../src/datbuild.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// ==========================================================================
// Dynamic String Buffer for API JSON Responses
// ==========================================================================

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} ApiBuf;

static void buf_init(ApiBuf *b) {
    b->cap  = 1024;
    b->len  = 0;
    b->data = malloc(b->cap);
    if (b->data) b->data[0] = '\0';
}

static void buf_append(ApiBuf *b, const char *s, size_t n) {
    if (!b->data) return;
    if (b->len + n + 1 >= b->cap) {
        size_t ncap = (b->cap * 2 > b->len + n + 1024) ? b->cap * 2 : b->len + n + 1024;
        char *ndata = realloc(b->data, ncap);
        if (!ndata) return;
        b->data = ndata;
        b->cap  = ncap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_puts(ApiBuf *b, const char *s) {
    if (s) buf_append(b, s, strlen(s));
}

static void buf_puts_escaped(ApiBuf *b, const char *s) {
    buf_puts(b, "\"");
    if (!s) {
        buf_puts(b, "\"");
        return;
    }
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '\"': buf_puts(b, "\\\""); break;
            case '\\': buf_puts(b, "\\\\"); break;
            case '\b': buf_puts(b, "\\b");  break;
            case '\f': buf_puts(b, "\\f");  break;
            case '\n': buf_puts(b, "\\n");  break;
            case '\r': buf_puts(b, "\\r");  break;
            case '\t': buf_puts(b, "\\t");  break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    buf_puts(b, esc);
                } else {
                    buf_append(b, (const char *)p, 1);
                }
                break;
        }
    }
    buf_puts(b, "\"");
}

// ==========================================================================
// Parameter and Validation Helpers
// ==========================================================================

static int get_query_param(const char *query, const char *key, char *out, size_t out_cap) {
    if (!query || !key || !out || out_cap == 0) return 0;
    out[0] = '\0';

    size_t klen = strlen(key);
    const char *p = query;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val = p + klen + 1;
            const char *end = strchr(val, '&');
            size_t vlen = end ? (size_t)(end - val) : strlen(val);
            if (vlen >= out_cap) vlen = out_cap - 1;
            // Decode %XX URL encoding
            size_t o = 0;
            for (size_t i = 0; i < vlen; i++) {
                if (val[i] == '%' && i + 2 < vlen && isxdigit((unsigned char)val[i+1]) && isxdigit((unsigned char)val[i+2])) {
                    char hex[3] = { val[i+1], val[i+2], 0 };
                    out[o++] = (char)strtol(hex, NULL, 16);
                    i += 2;
                } else if (val[i] == '+') {
                    out[o++] = ' ';
                } else {
                    out[o++] = val[i];
                }
            }
            out[o] = '\0';
            return 1;
        }
        p = strchr(p, '&');
        if (!p) break;
        p++;
    }
    return 0;
}

// Validates folder name per API.md: ^[A-Za-z0-9_\- ]+$ (reject .., slashes, empty)
static int is_valid_folder_name(const char *folder) {
    if (!folder || !folder[0]) return 0;
    if (strcmp(folder, ".") == 0 || strcmp(folder, "..") == 0) return 0;
    for (const char *p = folder; *p; p++) {
        char c = *p;
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')) {
            return 0;
        }
    }
    return 1;
}

// Validates civ filename per API.md: ^[A-Za-z0-9_\-]+\.json$
static int is_valid_civ_filename(const char *name) {
    if (!name) return 0;
    size_t len = strlen(name);
    if (len < 6 || strcmp(name + len - 5, ".json") != 0) return 0;
    for (size_t i = 0; i < len - 5; i++) {
        char c = name[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

// Validates uploaded asset filename: safe alphanumeric + dot/dash/underscore, no paths
static int is_valid_upload_filename(const char *name) {
    if (!name || !name[0]) return 0;
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) return 0;
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) {
            return 0;
        }
    }
    return 1;
}

// ==========================================================================
// Endpoint Handlers
// ==========================================================================

// GET /api/info
static void handle_get_info(ServerState *s, HttpResponse *res) {
    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "{ \"game_dir\": ");
    buf_puts_escaped(&b, s->game_dir);
    buf_puts(&b, ", \"mods_dir\": ");
    buf_puts_escaped(&b, s->mods_dir);
    buf_puts(&b, ", \"version\": \"2.0\" }");

    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET /api/catalog
static void handle_get_catalog(ServerState *s, HttpResponse *res) {
    const ScCatalog *cat = s->catalog;
    if (!cat) {
        http_response_error(res, 500, "Catalog not loaded");
        return;
    }

    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "{\n  \"civs\": [");
    for (size_t i = 0; i < cat->civ_count; i++) {
        if (i > 0) buf_puts(&b, ", ");
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"id\":%d,\"name\":", cat->civs[i].id);
        buf_puts(&b, buf);
        buf_puts_escaped(&b, cat->civs[i].name);
        buf_puts(&b, "}");
    }
    buf_puts(&b, "],\n  \"units\": [");
    for (size_t i = 0; i < cat->unit_count; i++) {
        if (i > 0) buf_puts(&b, ", ");
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"id\":%d,\"type\":%d,\"class\":%d,\"internal\":",
                 cat->units[i].id, cat->units[i].type, cat->units[i].class_);
        buf_puts(&b, buf);
        buf_puts_escaped(&b, cat->units[i].internal);
        buf_puts(&b, ",\"name\":");
        buf_puts_escaped(&b, cat->units[i].name);
        buf_puts(&b, "}");
    }
    buf_puts(&b, "],\n  \"techs\": [");
    for (size_t i = 0; i < cat->tech_count; i++) {
        if (i > 0) buf_puts(&b, ", ");
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"id\":%d,\"internal\":", cat->techs[i].id);
        buf_puts(&b, buf);
        buf_puts_escaped(&b, cat->techs[i].internal);
        buf_puts(&b, ",\"name\":");
        buf_puts_escaped(&b, cat->techs[i].name);
        buf_puts(&b, "}");
    }
    buf_puts(&b, "],\n  \"stats\": [");
    for (int i = 0; i < SC_STAT_COUNT; i++) {
        if (i > 0) buf_puts(&b, ", ");
        buf_puts(&b, "{\"field\":");
        buf_puts_escaped(&b, SC_STAT_FIELD[i]);
        buf_puts(&b, ",\"label\":");
        buf_puts_escaped(&b, SC_STAT_LABEL[i]);
        buf_puts(&b, "}");
    }
    buf_puts(&b, "]\n}");

    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET /api/stats?civ=C&unit=U
static void handle_get_stats(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char civ_str[32]  = "";
    char unit_str[32] = "";
    if (!get_query_param(req->query, "civ", civ_str, sizeof(civ_str)) ||
        !get_query_param(req->query, "unit", unit_str, sizeof(unit_str))) {
        http_response_error(res, 400, "Missing civ or unit query parameter");
        return;
    }

    int civ_id  = atoi(civ_str);
    int unit_id = atoi(unit_str);

    ScUnitStats stats;
    if (sc_catalog_unit_stats(s->catalog, civ_id, unit_id, &stats) != 0) {
        http_response_error(res, 404, "Unit or civ not found");
        return;
    }

    int has_any = 0;
    for (int i = 0; i < SC_STAT_COUNT; i++) {
        if (stats.has[i]) { has_any = 1; break; }
    }
    if (!has_any) {
        http_response_error(res, 404, "No stats available for this unit");
        return;
    }

    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "{");
    int first = 1;
    for (int i = 0; i < SC_STAT_COUNT; i++) {
        if (stats.has[i]) {
            if (!first) buf_puts(&b, ", ");
            first = 0;
            buf_puts_escaped(&b, SC_STAT_FIELD[i]);
            buf_puts(&b, ": ");
            char num_buf[64];
            float v = stats.value[i];
            if (floorf(v) == v) {
                snprintf(num_buf, sizeof(num_buf), "%lld", (long long)v);
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.4g", v);
            }
            buf_puts(&b, num_buf);
        }
    }
    buf_puts(&b, "}");

    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET /api/mods
static void handle_get_mods(ServerState *s, HttpResponse *res) {
    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s/*", s->mods_dir);

    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "[");

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    int first_mod = 1;

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                char mod_dir[MAX_PATH];
                snprintf(mod_dir, sizeof(mod_dir), "%s/%s", s->mods_dir, fd.cFileName);

                char mjson_path[MAX_PATH];
                snprintf(mjson_path, sizeof(mjson_path), "%s/mod.json", mod_dir);

                char *mjson_txt = wl_read_file(mjson_path, NULL);
                if (mjson_txt) {
                    WlJsonVal *m_ast = wl_json_parse(mjson_txt, NULL, 0);
                    free(mjson_txt);

                    if (m_ast && m_ast->type == WL_JSON_OBJECT) {
                        if (!first_mod) buf_puts(&b, ", ");
                        first_mod = 0;

                        const WlJsonVal *v_name = wl_json_get(m_ast, "name");
                        const WlJsonVal *v_ver  = wl_json_get(m_ast, "version");
                        const WlJsonVal *v_auth = wl_json_get(m_ast, "author");
                        const WlJsonVal *v_en   = wl_json_get(m_ast, "enabled");

                        buf_puts(&b, "{\"folder\":");
                        buf_puts_escaped(&b, fd.cFileName);
                        buf_puts(&b, ",\"name\":");
                        buf_puts_escaped(&b, v_name && v_name->type == WL_JSON_STRING ? v_name->u.str : fd.cFileName);
                        buf_puts(&b, ",\"version\":");
                        buf_puts_escaped(&b, v_ver && v_ver->type == WL_JSON_STRING ? v_ver->u.str : "0.1");
                        buf_puts(&b, ",\"author\":");
                        buf_puts_escaped(&b, v_auth && v_auth->type == WL_JSON_STRING ? v_auth->u.str : "");
                        buf_puts(&b, ",\"enabled\":");
                        int enabled = (v_en && v_en->type == WL_JSON_BOOL) ? v_en->u.bool_val : 1;
                        buf_puts(&b, enabled ? "true" : "false");

                        // Scan civs
                        buf_puts(&b, ",\"civs\":[");
                        char civs_pattern[MAX_PATH];
                        snprintf(civs_pattern, sizeof(civs_pattern), "%s/civs/*.json", mod_dir);
                        WIN32_FIND_DATAA cfd;
                        HANDLE hCivFind = FindFirstFileA(civs_pattern, &cfd);
                        int first_civ = 1;
                        if (hCivFind != INVALID_HANDLE_VALUE) {
                            do {
                                if (!(cfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                    if (!first_civ) buf_puts(&b, ", ");
                                    first_civ = 0;
                                    buf_puts_escaped(&b, cfd.cFileName);
                                }
                            } while (FindNextFileA(hCivFind, &cfd));
                            FindClose(hCivFind);
                        }
                        buf_puts(&b, "]");

                        // Icon check
                        char icon_path[MAX_PATH];
                        snprintf(icon_path, sizeof(icon_path), "%s/icon.png", mod_dir);
                        DWORD attr = GetFileAttributesA(icon_path);
                        int has_icon = (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
                        buf_puts(&b, ",\"has_icon\":");
                        buf_puts(&b, has_icon ? "true" : "false");
                        buf_puts(&b, "}");
                    }
                    wl_json_free(m_ast);
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    buf_puts(&b, "]");

    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET /api/mod/<folder>
static void handle_get_mod_detail(ServerState *s, const char *folder, HttpResponse *res) {
    if (!is_valid_folder_name(folder)) {
        http_response_error(res, 400, "Invalid folder name");
        return;
    }

    char mod_dir[MAX_PATH];
    snprintf(mod_dir, sizeof(mod_dir), "%s/%s", s->mods_dir, folder);

    char mjson_path[MAX_PATH];
    snprintf(mjson_path, sizeof(mjson_path), "%s/mod.json", mod_dir);

    char *mjson_txt = wl_read_file(mjson_path, NULL);
    if (!mjson_txt) {
        http_response_error(res, 404, "Mod not found");
        return;
    }

    WlJsonVal *m_ast = wl_json_parse(mjson_txt, NULL, 0);
    free(mjson_txt);
    if (!m_ast || m_ast->type != WL_JSON_OBJECT) {
        wl_json_free(m_ast);
        http_response_error(res, 500, "Malformed mod.json");
        return;
    }

    // Build civs object
    WlJsonVal *civs_obj = calloc(1, sizeof(WlJsonVal));
    civs_obj->type = WL_JSON_OBJECT;

    char civs_pattern[MAX_PATH];
    snprintf(civs_pattern, sizeof(civs_pattern), "%s/civs/*.json", mod_dir);
    WIN32_FIND_DATAA cfd;
    HANDLE hCivFind = FindFirstFileA(civs_pattern, &cfd);
    if (hCivFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(cfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char cpath[MAX_PATH];
                snprintf(cpath, sizeof(cpath), "%s/civs/%s", mod_dir, cfd.cFileName);
                char *ctxt = wl_read_file(cpath, NULL);
                if (ctxt) {
                    WlJsonVal *cast = wl_json_parse(ctxt, NULL, 0);
                    free(ctxt);
                    if (cast && cast->type == WL_JSON_OBJECT) {
                        // Append to civs_obj
                        if (civs_obj->u.obj.count >= civs_obj->u.obj.cap) {
                            size_t ncap = civs_obj->u.obj.cap ? civs_obj->u.obj.cap * 2 : 4;
                            WlJsonMember *nmem = realloc(civs_obj->u.obj.members, ncap * sizeof(WlJsonMember));
                            if (nmem) {
                                civs_obj->u.obj.members = nmem;
                                civs_obj->u.obj.cap     = ncap;
                            }
                        }
                        if (civs_obj->u.obj.count < civs_obj->u.obj.cap) {
                            civs_obj->u.obj.members[civs_obj->u.obj.count].key = strdup(cfd.cFileName);
                            civs_obj->u.obj.members[civs_obj->u.obj.count].val = cast;
                            civs_obj->u.obj.count++;
                        } else {
                            wl_json_free(cast);
                        }
                    } else {
                        wl_json_free(cast);
                    }
                }
            }
        } while (FindNextFileA(hCivFind, &cfd));
        FindClose(hCivFind);
    }

    // Wrap into { "folder": ..., "mod": ..., "civs": ... }
    WlJsonVal *root = calloc(1, sizeof(WlJsonVal));
    root->type = WL_JSON_OBJECT;
    root->u.obj.cap = 3;
    root->u.obj.members = calloc(3, sizeof(WlJsonMember));
    root->u.obj.count = 3;

    root->u.obj.members[0].key = strdup("folder");
    WlJsonVal *vf = calloc(1, sizeof(WlJsonVal));
    vf->type = WL_JSON_STRING;
    vf->u.str = strdup(folder);
    root->u.obj.members[0].val = vf;

    root->u.obj.members[1].key = strdup("mod");
    root->u.obj.members[1].val = m_ast;

    root->u.obj.members[2].key = strdup("civs");
    root->u.obj.members[2].val = civs_obj;

    size_t out_len = 0;
    char *out_json = modio_json_write_pretty(root, &out_len);
    wl_json_free(root);

    http_response_set_json(res, 200, out_json);
    free(out_json);
}

// PUT /api/mod/<folder>
static void handle_put_mod(ServerState *s, const char *folder, const HttpRequest *req, HttpResponse *res) {
    if (!is_valid_folder_name(folder)) {
        http_response_error(res, 400, "Invalid folder name");
        return;
    }

    char mod_dir[MAX_PATH];
    snprintf(mod_dir, sizeof(mod_dir), "%s/%s", s->mods_dir, folder);

    DWORD attr = GetFileAttributesA(mod_dir);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        http_response_error(res, 404, "Mod not found");
        return;
    }

    char err[256] = "";
    WlJsonVal *body = wl_json_parse(req->body, err, sizeof(err));
    if (!body || body->type != WL_JSON_OBJECT) {
        wl_json_free(body);
        http_response_error(res, 400, "Invalid JSON body");
        return;
    }

    const WlJsonVal *v_mod  = wl_json_get(body, "mod");
    const WlJsonVal *v_civs = wl_json_get(body, "civs");
    if (!v_mod || v_mod->type != WL_JSON_OBJECT || !v_civs || v_civs->type != WL_JSON_OBJECT) {
        wl_json_free(body);
        http_response_error(res, 400, "Expected 'mod' and 'civs' objects");
        return;
    }

    char civs_dir[MAX_PATH];
    snprintf(civs_dir, sizeof(civs_dir), "%s/civs", mod_dir);
    CreateDirectoryA(civs_dir, NULL);

    // Write mod.json
    char mjson_path[MAX_PATH];
    snprintf(mjson_path, sizeof(mjson_path), "%s/mod.json", mod_dir);
    size_t mlen = 0;
    char *mstr = modio_json_write_pretty(v_mod, &mlen);
    if (modio_atomic_write_file(mjson_path, mstr, mlen, err, sizeof(err)) != 0) {
        free(mstr);
        wl_json_free(body);
        http_response_error(res, 500, err);
        return;
    }
    free(mstr);

    // Write each civ in civs object
    for (size_t i = 0; i < v_civs->u.obj.count; i++) {
        const char *cfn = v_civs->u.obj.members[i].key;
        if (!is_valid_civ_filename(cfn)) {
            wl_json_free(body);
            http_response_error(res, 400, "Invalid civ filename");
            return;
        }

        char cpath[MAX_PATH];
        snprintf(cpath, sizeof(cpath), "%s/%s", civs_dir, cfn);

        size_t clen = 0;
        char *cstr = modio_json_write_pretty(v_civs->u.obj.members[i].val, &clen);
        if (modio_atomic_write_file(cpath, cstr, clen, err, sizeof(err)) != 0) {
            free(cstr);
            wl_json_free(body);
            http_response_error(res, 500, err);
            return;
        }
        free(cstr);
    }

    // Process deleted_civs if specified
    const WlJsonVal *v_del = wl_json_get(body, "deleted_civs");
    if (v_del && v_del->type == WL_JSON_ARRAY) {
        for (size_t i = 0; i < v_del->u.arr.count; i++) {
            const WlJsonVal *item = v_del->u.arr.items[i];
            if (item && item->type == WL_JSON_STRING && is_valid_civ_filename(item->u.str)) {
                char dpath[MAX_PATH];
                snprintf(dpath, sizeof(dpath), "%s/%s", civs_dir, item->u.str);
                remove(dpath);
            }
        }
    }

    wl_json_free(body);
    http_response_set_json(res, 200, "{\"ok\":true}");
}

// POST /api/mods
static void handle_post_mods(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char err[256] = "";
    WlJsonVal *body = wl_json_parse(req->body, err, sizeof(err));
    if (!body || body->type != WL_JSON_OBJECT) {
        wl_json_free(body);
        http_response_error(res, 400, "Invalid JSON body");
        return;
    }

    const WlJsonVal *v_name = wl_json_get(body, "name");
    if (!v_name || v_name->type != WL_JSON_STRING || !v_name->u.str[0]) {
        wl_json_free(body);
        http_response_error(res, 400, "Missing or invalid 'name'");
        return;
    }

    // Sanitize folder name from mod name (remove unsafe characters)
    char folder[MAX_PATH];
    size_t o = 0;
    for (const char *p = v_name->u.str; *p && o + 1 < sizeof(folder); p++) {
        char c = *p;
        if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ') {
            folder[o++] = c;
        }
    }
    // Trim spaces
    while (o > 0 && folder[o - 1] == ' ') o--;
    folder[o] = '\0';
    if (!folder[0]) snprintf(folder, sizeof(folder), "Mod");

    char target_dir[MAX_PATH];
    snprintf(target_dir, sizeof(target_dir), "%s/%s", s->mods_dir, folder);

    // 409 Conflict if directory already exists
    if (GetFileAttributesA(target_dir) != INVALID_FILE_ATTRIBUTES) {
        wl_json_free(body);
        http_response_error(res, 409, "Mod folder already exists");
        return;
    }

    CreateDirectoryA(s->mods_dir, NULL);
    CreateDirectoryA(target_dir, NULL);
    char civs_dir[MAX_PATH];
    snprintf(civs_dir, sizeof(civs_dir), "%s/civs", target_dir);
    CreateDirectoryA(civs_dir, NULL);

    // Construct minimal mod.json
    WlJsonVal *m_obj = calloc(1, sizeof(WlJsonVal));
    m_obj->type = WL_JSON_OBJECT;

    #define ADD_STR_MEM(k, v) do { \
        if (m_obj->u.obj.count >= m_obj->u.obj.cap) { \
            size_t ncap = m_obj->u.obj.cap ? m_obj->u.obj.cap * 2 : 4; \
            m_obj->u.obj.members = realloc(m_obj->u.obj.members, ncap * sizeof(WlJsonMember)); \
            m_obj->u.obj.cap = ncap; \
        } \
        m_obj->u.obj.members[m_obj->u.obj.count].key = strdup(k); \
        WlJsonVal *sv = calloc(1, sizeof(WlJsonVal)); \
        sv->type = WL_JSON_STRING; sv->u.str = strdup(v); \
        m_obj->u.obj.members[m_obj->u.obj.count].val = sv; \
        m_obj->u.obj.count++; \
    } while(0)

    ADD_STR_MEM("name", v_name->u.str);
    ADD_STR_MEM("version", "0.1");
    ADD_STR_MEM("author", "");
    ADD_STR_MEM("description", "");
    #undef ADD_STR_MEM

    size_t mlen = 0;
    char *mstr = modio_json_write_pretty(m_obj, &mlen);
    char mpath[MAX_PATH];
    snprintf(mpath, sizeof(mpath), "%s/mod.json", target_dir);
    modio_atomic_write_file(mpath, mstr, mlen, NULL, 0);
    free(mstr);
    wl_json_free(m_obj);
    wl_json_free(body);

    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "{\"folder\":");
    buf_puts_escaped(&b, folder);
    buf_puts(&b, "}");
    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// POST /api/validate
static void handle_post_validate(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char err[256] = "";
    WlJsonVal *body = wl_json_parse(req->body, err, sizeof(err));
    if (!body || body->type != WL_JSON_OBJECT) {
        wl_json_free(body);
        http_response_error(res, 400, "Invalid JSON body");
        return;
    }

    const WlJsonVal *v_folder = wl_json_get(body, "folder");
    const WlJsonVal *v_mod    = wl_json_get(body, "mod");
    const WlJsonVal *v_civs   = wl_json_get(body, "civs");

    if (!v_folder || v_folder->type != WL_JSON_STRING || !is_valid_folder_name(v_folder->u.str) ||
        !v_mod || v_mod->type != WL_JSON_OBJECT || !v_civs || v_civs->type != WL_JSON_OBJECT) {
        wl_json_free(body);
        http_response_error(res, 400, "Expected folder, mod, and civs objects");
        return;
    }

    // Create unique temporary workspace in <game_dir>/wololoader/temp_val_<pid>_<tick>
    char temp_dir[MAX_PATH];
    snprintf(temp_dir, sizeof(temp_dir), "%s/wololoader/sc_val_%lu_%lu",
             s->game_dir, (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    CreateDirectoryA(temp_dir, NULL);

    char temp_mods[MAX_PATH];
    snprintf(temp_mods, sizeof(temp_mods), "%s/mods", temp_dir);
    CreateDirectoryA(temp_mods, NULL);

    char temp_this[MAX_PATH];
    snprintf(temp_this, sizeof(temp_this), "%s/%s", temp_mods, v_folder->u.str);
    CreateDirectoryA(temp_this, NULL);

    char temp_civs[MAX_PATH];
    snprintf(temp_civs, sizeof(temp_civs), "%s/civs", temp_this);
    CreateDirectoryA(temp_civs, NULL);

    // Write temporary mod.json
    size_t mlen = 0;
    char *mstr = modio_json_write_pretty(v_mod, &mlen);
    char tmpath[MAX_PATH];
    snprintf(tmpath, sizeof(tmpath), "%s/mod.json", temp_this);
    modio_atomic_write_file(tmpath, mstr, mlen, NULL, 0);
    free(mstr);

    // Write temporary civ files
    for (size_t i = 0; i < v_civs->u.obj.count; i++) {
        const char *cfn = v_civs->u.obj.members[i].key;
        if (is_valid_civ_filename(cfn)) {
            char tcpath[MAX_PATH];
            snprintf(tcpath, sizeof(tcpath), "%s/%s", temp_civs, cfn);
            size_t clen = 0;
            char *cstr = modio_json_write_pretty(v_civs->u.obj.members[i].val, &clen);
            modio_atomic_write_file(tcpath, cstr, clen, NULL, 0);
            free(cstr);
        }
    }

    // Run wl_dat_build into temporary output dat
    char src_dat[MAX_PATH];
    snprintf(src_dat, sizeof(src_dat), "%s/resources/_common/dat/empires2_x2_p1.dat", s->game_dir);

    char out_dat[MAX_PATH];
    snprintf(out_dat, sizeof(out_dat), "%s/val_out.dat", temp_dir);

    char out_civs[MAX_PATH];
    snprintf(out_civs, sizeof(out_civs), "%s/val_civs.txt", temp_dir);

    char out_strings[MAX_PATH];
    snprintf(out_strings, sizeof(out_strings), "%s/val_strings.txt", temp_dir);

    char out_commands[MAX_PATH];
    snprintf(out_commands, sizeof(out_commands), "%s/val_commands.txt", temp_dir);

    char val_err[512] = "";
    int ret = wl_dat_build(s->zlib, src_dat, temp_mods, out_dat, out_civs, out_strings, out_commands, val_err, sizeof(val_err));

    // Cleanup all temporary files
    remove(out_dat);
    remove(out_civs);
    remove(out_strings);
    remove(out_commands);
    for (size_t i = 0; i < v_civs->u.obj.count; i++) {
        const char *cfn = v_civs->u.obj.members[i].key;
        char tcpath[MAX_PATH];
        snprintf(tcpath, sizeof(tcpath), "%s/%s", temp_civs, cfn);
        remove(tcpath);
    }
    RemoveDirectoryA(temp_civs);
    remove(tmpath);
    RemoveDirectoryA(temp_this);
    RemoveDirectoryA(temp_mods);
    RemoveDirectoryA(temp_dir);

    wl_json_free(body);

    ApiBuf b;
    buf_init(&b);
    if (ret >= 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"ok\":true,\"civs_built\":%d,\"error\":\"\"}", ret);
        buf_puts(&b, buf);
    } else {
        buf_puts(&b, "{\"ok\":false,\"civs_built\":0,\"error\":");
        buf_puts_escaped(&b, val_err);
        buf_puts(&b, "}");
    }
    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET /api/icon/<folder>
static void handle_get_icon(ServerState *s, const char *folder, HttpResponse *res) {
    if (!is_valid_folder_name(folder)) {
        http_response_error(res, 400, "Invalid folder name");
        return;
    }

    char icon_path[MAX_PATH];
    snprintf(icon_path, sizeof(icon_path), "%s/%s/icon.png", s->mods_dir, folder);

    FILE *f = fopen(icon_path, "rb");
    if (!f) {
        http_response_error(res, 404, "Icon not found");
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 10 * 1024 * 1024) {
        fclose(f);
        http_response_error(res, 500, "Invalid icon file");
        return;
    }

    char *data = malloc((size_t)sz);
    if (!data) {
        fclose(f);
        http_response_error(res, 500, "Out of memory");
        return;
    }

    fread(data, 1, (size_t)sz, f);
    fclose(f);

    http_response_free(res);
    res->status_code     = 200;
    res->status_text     = "OK";
    res->content_type    = "image/png";
    res->body            = data;
    res->body_len        = (size_t)sz;
    res->is_dynamic_body = 1;
}

// POST /api/upload?folder=F&name=N
static void handle_post_upload(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char folder[MAX_PATH] = "";
    char name[MAX_PATH]   = "";
    if (!get_query_param(req->query, "folder", folder, sizeof(folder)) ||
        !get_query_param(req->query, "name", name, sizeof(name))) {
        http_response_error(res, 400, "Missing folder or name query parameter");
        return;
    }

    if (!is_valid_folder_name(folder) || !is_valid_upload_filename(name)) {
        http_response_error(res, 400, "Invalid folder or file name");
        return;
    }

    if (req->content_length > 20 * 1024 * 1024) {
        http_response_error(res, 413, "File exceeds maximum size of 20 MB");
        return;
    }

    char mod_dir[MAX_PATH];
    snprintf(mod_dir, sizeof(mod_dir), "%s/%s", s->mods_dir, folder);
    if (GetFileAttributesA(mod_dir) == INVALID_FILE_ATTRIBUTES) {
        http_response_error(res, 404, "Mod folder not found");
        return;
    }

    char target_path[MAX_PATH];
    snprintf(target_path, sizeof(target_path), "%s/%s", mod_dir, name);

    char err[256];
    if (modio_atomic_write_file(target_path, req->body, req->content_length, err, sizeof(err)) != 0) {
        http_response_error(res, 500, err);
        return;
    }

    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "{\"file\":");
    buf_puts_escaped(&b, name);
    buf_puts(&b, "}");
    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// POST /api/open-folder
static void handle_post_open_folder(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char target[MAX_PATH];
    snprintf(target, sizeof(target), "%s", s->mods_dir);

    if (req->body && req->body[0]) {
        WlJsonVal *body = wl_json_parse(req->body, NULL, 0);
        if (body && body->type == WL_JSON_OBJECT) {
            const WlJsonVal *vf = wl_json_get(body, "folder");
            if (vf && vf->type == WL_JSON_STRING && is_valid_folder_name(vf->u.str)) {
                snprintf(target, sizeof(target), "%s/%s", s->mods_dir, vf->u.str);
            }
        }
        wl_json_free(body);
    }

    launcher_open_folder(target);
    http_response_set_json(res, 200, "{\"ok\":true}");
}

// ==========================================================================
// Main API Router
// ==========================================================================

void api_handle_request(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    if (!s || !req || !res) return;

    if (strcmp(req->path, "/api/info") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_info(s, res);
    } else if (strcmp(req->path, "/api/catalog") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_catalog(s, res);
    } else if (strcmp(req->path, "/api/stats") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_stats(s, req, res);
    } else if (strcmp(req->path, "/api/mods") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_mods(s, res);
    } else if (strncmp(req->path, "/api/mod/", 9) == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_mod_detail(s, req->path + 9, res);
    } else if (strncmp(req->path, "/api/mod/", 9) == 0 && strcmp(req->method, "PUT") == 0) {
        handle_put_mod(s, req->path + 9, req, res);
    } else if (strcmp(req->path, "/api/mods") == 0 && strcmp(req->method, "POST") == 0) {
        handle_post_mods(s, req, res);
    } else if (strcmp(req->path, "/api/validate") == 0 && strcmp(req->method, "POST") == 0) {
        handle_post_validate(s, req, res);
    } else if (strncmp(req->path, "/api/icon/", 10) == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_icon(s, req->path + 10, res);
    } else if (strcmp(req->path, "/api/upload") == 0 && strcmp(req->method, "POST") == 0) {
        handle_post_upload(s, req, res);
    } else if (strcmp(req->path, "/api/open-folder") == 0 && strcmp(req->method, "POST") == 0) {
        handle_post_open_folder(s, req, res);
    } else if (strcmp(req->path, "/api/ping") == 0 && strcmp(req->method, "POST") == 0) {
        launcher_notify_ping();
        http_response_set_json(res, 200, "{\"ok\":true}");
    } else if (strcmp(req->path, "/api/quit") == 0 && strcmp(req->method, "POST") == 0) {
        launcher_request_quit();
        s->exit_requested = 1;
        http_response_set_json(res, 200, "{\"ok\":true}");
    } else {
        http_response_error(res, 404, "Endpoint not found");
    }
}
