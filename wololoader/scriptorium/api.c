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
//   - GET  /api/game-file?path=P, GET /api/game-list?dir=D (read-only game resources)
//   - GET  /api/mod-tree?folder=F (every file of a mod folder), GET/PUT/DELETE /api/file
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "api.h"
#include "launcher.h"
#include "modio.h"
#include "catalog.h"
#include "hub.h"
#include "../src/json.h"
#include "../src/datbuild.h"
#include "gamepath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// Implemented in launcher.c
extern int  launcher_is_game_running(void);
extern void launcher_launch_steam_game(void);

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
        snprintf(buf, sizeof(buf), "{\"id\":%d,\"type\":%d,\"class\":%d,\"icon\":%d,\"internal\":",
                 cat->units[i].id, cat->units[i].type, cat->units[i].class_, cat->units[i].icon);
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
        snprintf(buf, sizeof(buf), "{\"id\":%d,\"icon\":%d,\"internal\":", cat->techs[i].id, cat->techs[i].icon);
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

// Last write time of one file as a FILETIME count (0 when the file is missing).
static ULONGLONG file_write_time(const char *path) {
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(path, &f);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    ULARGE_INTEGER t;
    t.LowPart = f.ftLastWriteTime.dwLowDateTime;
    t.HighPart = f.ftLastWriteTime.dwHighDateTime;
    return t.QuadPart;
}

// GET /api/mods (each mod: folder, name, version, author, enabled, civs, has_icon, modified)
// WololoaderBase (Wololoader's own feature switches, not a mod to make) is left out unless
// ?all=1 asks for it. Scriptorium's UI never lists it, so it cannot be opened there.
static void handle_get_mods(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char all_flag[8] = "";
    int include_system = get_query_param(req->query, "all", all_flag, sizeof all_flag) && all_flag[0] == '1';

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
                strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0 &&
                (include_system || _stricmp(fd.cFileName, "WololoaderBase") != 0)) {
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

                        // Newest write time of mod.json and civs/*.json (the mod list shows "last changed")
                        ULONGLONG newest = file_write_time(mjson_path);

                        // Scan civs
                        buf_puts(&b, ",\"civs\":[");
                        char civs_pattern[MAX_PATH];
                        snprintf(civs_pattern, sizeof(civs_pattern), "%s/civs/*.json", mod_dir);
                        WIN32_FIND_DATAA cfd;
                        HANDLE hCivFind = FindFirstFileA(civs_pattern, &cfd);
                        int first_civ = 1;
                        if (hCivFind != INVALID_HANDLE_VALUE) {
                            do {
                                ULARGE_INTEGER ct;
                                ct.LowPart = cfd.ftLastWriteTime.dwLowDateTime;
                                ct.HighPart = cfd.ftLastWriteTime.dwHighDateTime;
                                if (ct.QuadPart > newest) newest = ct.QuadPart;
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
                        // Unix seconds of the last change (0 = unknown); the UI shows it on the mod card
                        long long unix_s = newest ? (long long)((newest - 116444736000000000ULL) / 10000000ULL) : 0;
                        char mtime[40];
                        snprintf(mtime, sizeof mtime, ",\"modified\":%lld", unix_s);
                        buf_puts(&b, mtime);
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

// ---- mod files (scripts, graphs, art) ---------------------------------------
// GET/PUT/DELETE /api/file?folder=F&path=P, GET /api/files?folder=F&dir=D, GET /api/mod-tree?folder=F.
// P: 1..6 segments of [A-Za-z0-9_.- ] anywhere in the mod folder (sounds and taunts sit at the root
// or in their own folders), except that "mod.json" and the "civs" folder are managed through
// PUT /api/mod/<folder>, so they are refused here. No "..", no absolute paths. Max 20 MB.
static int is_valid_mod_path(const char *p, int is_dir) {
    if (!p || !p[0] || strlen(p) > 200 || strstr(p, "..")) return 0;
    if (!is_dir && strcmp(p, "scriptorium.json") == 0) return 1;
    if (!is_dir && strcmp(p, "mod.json") == 0) return 0;
    if (!strncmp(p, "civs", 4) && (p[4] == '/' || !p[4])) return 0;
    int segs = 0, seglen = 0;
    for (const char *c = p; ; c++) {
        if (*c == '/' || *c == 0) {
            if (seglen == 0) return 0;
            segs++;
            seglen = 0;
            if (!*c) break;
            continue;
        }
        if (!(isalnum((unsigned char)*c) || *c == '_' || *c == '-' || *c == '.' || *c == ' ')) return 0;
        seglen++;
    }
    if (segs > 6) return 0;   // resources/_common/drs/graphics/<n>.slp has 5
    return 1;
}

static int mod_file_path(ServerState *s, const HttpRequest *req, const char *key, int is_dir,
                         char *out, size_t cap, HttpResponse *res) {
    char folder[MAX_PATH] = "", rel[MAX_PATH] = "";
    if (!get_query_param(req->query, "folder", folder, sizeof folder) ||
        !get_query_param(req->query, key, rel, sizeof rel)) {
        http_response_error(res, 400, "Missing folder or path");
        return 0;
    }
    if (!is_valid_folder_name(folder) || !is_valid_mod_path(rel, is_dir)) {
        http_response_error(res, 400, "Invalid folder or path");
        return 0;
    }
    snprintf(out, cap, "%s/%s/%s", s->mods_dir, folder, rel);
    return 1;
}

static void make_parent_dirs(char *path) {
    for (char *c = path; *c; c++) {
        if (*c != '/' && *c != '\\') continue;
        char k = *c;
        *c = 0;
        if (c > path && path[strlen(path) - 1] != ':') CreateDirectoryA(path, NULL);
        *c = k;
    }
}

static void handle_get_file(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char path[MAX_PATH];
    if (!mod_file_path(s, req, "path", 0, path, sizeof path, res)) return;
    FILE *f = fopen(path, "rb");
    if (!f) { http_response_error(res, 404, "File not found"); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 20 * 1024 * 1024) { fclose(f); http_response_error(res, 500, "File too large"); return; }
    char *data = malloc((size_t)sz + 1);
    if (!data) { fclose(f); http_response_error(res, 500, "Out of memory"); return; }
    size_t got = fread(data, 1, (size_t)sz, f);
    fclose(f);
    http_response_free(res);
    res->status_code     = 200;
    res->status_text     = "OK";
    res->content_type    = "application/octet-stream";
    res->body            = data;
    res->body_len        = got;
    res->is_dynamic_body = 1;
}

static void handle_put_file(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char path[MAX_PATH];
    if (!mod_file_path(s, req, "path", 0, path, sizeof path, res)) return;
    if (req->content_length > 20 * 1024 * 1024) { http_response_error(res, 413, "File exceeds 20 MB"); return; }
    make_parent_dirs(path);
    char err[256];
    if (modio_atomic_write_file(path, req->body ? req->body : "", req->content_length, err, sizeof err) != 0) {
        http_response_error(res, 500, err);
        return;
    }
    http_response_set_json(res, 200, "{\"ok\":true}");
}

static void handle_delete_file(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char path[MAX_PATH];
    if (!mod_file_path(s, req, "path", 0, path, sizeof path, res)) return;
    if (!DeleteFileA(path) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        http_response_error(res, 500, "Cannot delete the file");
        return;
    }
    http_response_set_json(res, 200, "{\"ok\":true}");
}

static void handle_get_files(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char dir[MAX_PATH], pat[MAX_PATH + 4];
    if (!mod_file_path(s, req, "dir", 1, dir, sizeof dir, res)) return;
    snprintf(pat, sizeof pat, "%s/*", dir);
    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "[");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (n++) buf_puts(&b, ",");
            buf_puts_escaped(&b, fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    buf_puts(&b, "]");
    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET /api/mod-tree?folder=F: every file of the mod folder, as [{path, size}] with "/" separators
// (the Files section and the item counts of the editor). Hidden entries (".x") and the "civs"
// folder and "mod.json" (managed through /api/mod) are left out; max 4000 files, 6 folder levels.
#define MOD_TREE_MAX_FILES 4000
#define MOD_TREE_MAX_DEPTH 6

typedef struct {
    ApiBuf *out;
    int     count;
} ModTreeWalk;

static void mod_tree_walk(ModTreeWalk *w, const char *dir, const char *rel, int depth) {
    if (depth > MOD_TREE_MAX_DEPTH) return;
    char pat[MAX_PATH + 4];
    snprintf(pat, sizeof pat, "%s/*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = fd.cFileName;
        if (name[0] == '.') continue;
        if (depth == 0 && (strcmp(name, "civs") == 0 || strcmp(name, "mod.json") == 0)) continue;
        char full[MAX_PATH], sub[MAX_PATH];
        snprintf(full, sizeof full, "%s/%s", dir, name);
        if (rel[0]) snprintf(sub, sizeof sub, "%s/%s", rel, name);
        else        snprintf(sub, sizeof sub, "%s", name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            mod_tree_walk(w, full, sub, depth + 1);
        } else {
            if (w->count >= MOD_TREE_MAX_FILES) return;
            if (w->count++) buf_puts(w->out, ",");
            char num[32];
            snprintf(num, sizeof num, "%lu", (unsigned long)fd.nFileSizeLow);
            buf_puts(w->out, "{\"path\":");
            buf_puts_escaped(w->out, sub);
            buf_puts(w->out, ",\"size\":");
            buf_puts(w->out, num);
            buf_puts(w->out, "}");
        }
    } while (FindNextFileA(h, &fd) && w->count < MOD_TREE_MAX_FILES);
    FindClose(h);
}

static void handle_get_mod_tree(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char folder[MAX_PATH] = "";
    if (!get_query_param(req->query, "folder", folder, sizeof folder)) {
        http_response_error(res, 400, "Missing folder");
        return;
    }
    if (!is_valid_folder_name(folder)) {
        http_response_error(res, 400, "Invalid folder");
        return;
    }
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s/%s", s->mods_dir, folder);
    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "[");
    ModTreeWalk w = { &b, 0 };
    mod_tree_walk(&w, dir, "", 0);
    buf_puts(&b, "]");
    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET /api/palette: the game palette (resources/_common/drs/interface/50500.bina, JASC-PAL:
// "JASC-PAL", "0100", "256", then 256 lines "r g b") as [[r,g,b], ...] for the SLP creator.
static void handle_get_palette(ServerState *s, HttpResponse *res) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/resources/_common/drs/interface/50500.bina", s->game_dir);
    FILE *f = fopen(path, "rb");
    if (!f) { http_response_error(res, 404, "Game palette 50500.bina not found"); return; }
    char line[128];
    int n = -3;
    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "[");
    while (fgets(line, sizeof line, f) && n < 256) {
        if (n < 0) { n++; continue; }   // header lines
        int r, g, bl;
        if (sscanf(line, "%d %d %d", &r, &g, &bl) != 3) break;
        char item[32];
        snprintf(item, sizeof item, "%s[%d,%d,%d]", n ? "," : "", r, g, bl);
        buf_puts(&b, item);
        n++;
    }
    fclose(f);
    buf_puts(&b, "]");
    if (n != 256) { free(b.data); http_response_error(res, 500, "Bad game palette"); return; }
    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// ---- game files (read only) ----------------------------------------------------
// GET /api/game-file?path=P: one file of the game's resources\ tree (SLP sprites for the viewer
// and the icons). GET /api/game-list?dir=D: its files as [{name, size}] (allowed extensions only).
// P and D are relative to <game>\resources\ with forward slashes; the rules are in gamepath.c.
// Nothing here writes: the game folder is read only for Scriptorium.
#define GAME_FILE_MAX_BYTES (20 * 1024 * 1024)
#define GAME_LIST_MAX       20000     // more entries than this: "truncated": true

static const char *game_file_content_type(const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot && _stricmp(dot, ".png") == 0) return "image/png";
    if (dot && _stricmp(dot, ".mp3") == 0) return "audio/mpeg";
    if (dot && _stricmp(dot, ".wav") == 0) return "audio/wav";
    return "application/octet-stream";   // .slp, .bina
}

static void handle_get_game_file(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char rel[256] = "", path[1024];
    if (!get_query_param(req->query, "path", rel, sizeof rel) || !sc_game_rel_path_ok(rel, 0)) {
        http_response_error(res, 400, "Invalid game file path");
        return;
    }
    snprintf(path, sizeof path, "%s/resources/%s", s->game_dir, rel);
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        http_response_error(res, 404, "Game file not found");
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) { http_response_error(res, 404, "Game file not found"); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > GAME_FILE_MAX_BYTES) { fclose(f); http_response_error(res, 500, "File too large"); return; }
    char *data = malloc((size_t)sz + 1);
    if (!data) { fclose(f); http_response_error(res, 500, "Out of memory"); return; }
    size_t got = fread(data, 1, (size_t)sz, f);
    fclose(f);
    http_response_free(res);
    res->status_code     = 200;
    res->status_text     = "OK";
    res->content_type    = game_file_content_type(rel);
    res->body            = data;
    res->body_len        = got;
    res->is_dynamic_body = 1;
}

static void handle_get_game_list(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char dir[256] = "", pat[1024];
    if (!get_query_param(req->query, "dir", dir, sizeof dir) || !sc_game_rel_path_ok(dir, 1)) {
        http_response_error(res, 400, "Invalid game folder");
        return;
    }
    snprintf(pat, sizeof pat, "%s/resources/%s/*", s->game_dir, dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { http_response_error(res, 404, "Game folder not found"); return; }
    ApiBuf b;
    buf_init(&b);
    buf_puts(&b, "{\"files\":[");
    int n = 0, truncated = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!sc_game_file_name_ok(fd.cFileName)) continue;
        if (n == GAME_LIST_MAX) { truncated = 1; break; }
        if (n++) buf_puts(&b, ",");
        buf_puts(&b, "{\"name\":");
        buf_puts_escaped(&b, fd.cFileName);
        char size[64];
        snprintf(size, sizeof size, ",\"size\":%llu}",
                 ((unsigned long long)fd.nFileSizeHigh << 32) | (unsigned long long)fd.nFileSizeLow);
        buf_puts(&b, size);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    buf_puts(&b, truncated ? "],\"truncated\":true}" : "],\"truncated\":false}");
    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET/PUT /api/settings: Scriptorium's own settings (window state, ...) in
// <game>/wololoader/scriptorium-settings.json, a JSON object (the page's port, and so its
// browser storage, changes at every start). Max 64 KB.
static void settings_path(ServerState *s, char *out, size_t cap) {
    snprintf(out, cap, "%s/wololoader/scriptorium-settings.json", s->game_dir);
}
static void handle_get_settings(ServerState *s, HttpResponse *res) {
    char path[MAX_PATH];
    settings_path(s, path, sizeof path);
    FILE *f = fopen(path, "rb");
    char buf[65536];
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    WlJsonVal *v = n ? wl_json_parse(buf, NULL, 0) : NULL;
    int ok = v && v->type == WL_JSON_OBJECT;
    wl_json_free(v);
    http_response_set_json(res, 200, ok ? buf : "{}");
}
static void handle_put_settings(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    if (!req->body || req->content_length > 65535) { http_response_error(res, 400, "Settings must be a JSON object"); return; }
    WlJsonVal *v = wl_json_parse(req->body, NULL, 0);
    int ok = v && v->type == WL_JSON_OBJECT;
    wl_json_free(v);
    if (!ok) { http_response_error(res, 400, "Settings must be a JSON object"); return; }
    char path[MAX_PATH], err[256];
    settings_path(s, path, sizeof path);
    if (modio_atomic_write_file(path, req->body, req->content_length, err, sizeof err) != 0) {
        http_response_error(res, 500, err);
        return;
    }
    http_response_set_json(res, 200, "{\"ok\":true}");
}

// ==========================================================================
// POST /api/testit — Write <game>/wololoader/autostart.json & launch AoE2 HD
// ==========================================================================

static int lookup_civ_in_cache(const char *game_dir, const char *mod_folder,
                               const char *civ_file, const char *civ_name) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/wololoader/cache/civs.txt", game_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    int by_mod_file = -1, by_name = -1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *f_id   = strtok(line, "\t");
        char *f_base = strtok(NULL, "\t");
        char *f_name = strtok(NULL, "\t");
        char *f_mod  = strtok(NULL, "\t");
        char *f_json = strtok(NULL, "\t");
        if (!f_id || !f_base || !f_name) continue;
        int id = atoi(f_id);
        if (id < 35) continue;
        if (mod_folder && mod_folder[0] && civ_file && civ_file[0] && f_mod && f_json &&
            _stricmp(f_mod, mod_folder) == 0 && _stricmp(f_json, civ_file) == 0) {
            by_mod_file = id;
            break;
        }
        if (civ_name && civ_name[0] && by_name < 0 && _stricmp(f_name, civ_name) == 0) {
            by_name = id;
        }
    }
    fclose(f);
    return (by_mod_file >= 35) ? by_mod_file : by_name;
}

static const char *catalog_civ_name(const ScCatalog *cat, int id) {
    if (!cat) return NULL;
    for (size_t i = 0; i < cat->civ_count; i++) {
        if (cat->civs[i].id == id) return cat->civs[i].name;
    }
    return NULL;
}

static int ensure_mod_enabled(const char *mods_dir, const char *folder, int *was_disabled,
                              char *err, size_t err_cap) {
    *was_disabled = 0;
    char mjson_path[MAX_PATH];
    snprintf(mjson_path, sizeof(mjson_path), "%s/%s/mod.json", mods_dir, folder);
    char *txt = wl_read_file(mjson_path, NULL);
    if (!txt) {
        snprintf(err, err_cap, "Mod not found");
        return -1;
    }
    WlJsonVal *m_ast = wl_json_parse(txt, NULL, 0);
    free(txt);
    if (!m_ast || m_ast->type != WL_JSON_OBJECT) {
        wl_json_free(m_ast);
        snprintf(err, err_cap, "Malformed mod.json");
        return -1;
    }

    int enabled = 1;
    int found_idx = -1;
    for (size_t i = 0; i < m_ast->u.obj.count; i++) {
        if (strcmp(m_ast->u.obj.members[i].key, "enabled") == 0) {
            found_idx = (int)i;
            if (m_ast->u.obj.members[i].val && m_ast->u.obj.members[i].val->type == WL_JSON_BOOL) {
                enabled = m_ast->u.obj.members[i].val->u.bool_val;
            }
            break;
        }
    }

    if (!enabled) {
        *was_disabled = 1;
        if (found_idx >= 0) {
            wl_json_free(m_ast->u.obj.members[found_idx].val);
            WlJsonVal *bv = calloc(1, sizeof(WlJsonVal));
            bv->type = WL_JSON_BOOL;
            bv->u.bool_val = 1;
            m_ast->u.obj.members[found_idx].val = bv;
        }
        size_t out_len = 0;
        char *out_str = modio_json_write_pretty(m_ast, &out_len);
        int wrc = modio_atomic_write_file(mjson_path, out_str, out_len, err, err_cap);
        free(out_str);
        wl_json_free(m_ast);
        return wrc;
    }

    wl_json_free(m_ast);
    return 0;
}

// POST /api/testit
// Body: { mod, civ (filename, name or id), map, size, ai, ai_civ, difficulty,
//         resources, reveal_map, cheats, launch (optional bool, default true) }
static void handle_post_testit(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char err[256] = "";
    WlJsonVal *body = wl_json_parse(req->body ? req->body : "", err, sizeof(err));
    if (!body || body->type != WL_JSON_OBJECT) {
        wl_json_free(body);
        http_response_error(res, 400, "Invalid JSON body");
        return;
    }

    const char *mod_folder = wl_json_str(wl_json_get(body, "mod"), NULL);
    const WlJsonVal *v_civ = wl_json_get(body, "civ");
    if (!mod_folder || !is_valid_folder_name(mod_folder)) {
        wl_json_free(body);
        http_response_error(res, 400, "Missing or invalid 'mod'");
        return;
    }
    if (!v_civ || (v_civ->type != WL_JSON_STRING && v_civ->type != WL_JSON_NUMBER)) {
        wl_json_free(body);
        http_response_error(res, 400, "Missing or invalid 'civ'");
        return;
    }

    // 1. Ensure the mod exists and is enabled (same mod.json write as the UI).
    int mod_was_disabled = 0;
    if (ensure_mod_enabled(s->mods_dir, mod_folder, &mod_was_disabled, err, sizeof(err)) != 0) {
        wl_json_free(body);
        http_response_error(res, strcmp(err, "Mod not found") == 0 ? 404 : 500, err);
        return;
    }

    // 2. Resolve the civ (extend id, cache\civs.txt id, or name/file for DLL resolution after build).
    int civ_id = -1;
    char civ_file[MAX_PATH] = "";
    char civ_name[128] = "";

    if (v_civ->type == WL_JSON_NUMBER) {
        civ_id = (int)v_civ->u.num.ival;
        const char *cn = catalog_civ_name(s->catalog, civ_id);
        if (cn) snprintf(civ_name, sizeof(civ_name), "%s", cn);
    } else {
        const char *req_civ = v_civ->u.str;
        if (is_valid_civ_filename(req_civ)) {
            snprintf(civ_file, sizeof(civ_file), "%s", req_civ);
        } else {
            // Try matching a civs/*.json file by filename or "name" field.
            snprintf(civ_name, sizeof(civ_name), "%s", req_civ);
            char pat[MAX_PATH];
            snprintf(pat, sizeof(pat), "%s/%s/civs/*.json", s->mods_dir, mod_folder);
            WIN32_FIND_DATAA cfd;
            HANDLE hf = FindFirstFileA(pat, &cfd);
            if (hf != INVALID_HANDLE_VALUE) {
                do {
                    if (cfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    char cpath[MAX_PATH];
                    snprintf(cpath, sizeof(cpath), "%s/%s/civs/%s", s->mods_dir, mod_folder, cfd.cFileName);
                    char *ctxt = wl_read_file(cpath, NULL);
                    if (!ctxt) continue;
                    WlJsonVal *cast = wl_json_parse(ctxt, NULL, 0);
                    free(ctxt);
                    if (cast && cast->type == WL_JSON_OBJECT) {
                        const char *n = wl_json_str(wl_json_get(cast, "name"), NULL);
                        const WlJsonVal *ext = wl_json_get(cast, "extend");
                        const char *en = (ext && ext->type == WL_JSON_NUMBER)
                                       ? catalog_civ_name(s->catalog, (int)ext->u.num.ival) : NULL;
                        if ((n && _stricmp(n, req_civ) == 0) || (en && _stricmp(en, req_civ) == 0)) {
                            snprintf(civ_file, sizeof(civ_file), "%s", cfd.cFileName);
                            wl_json_free(cast);
                            break;
                        }
                    }
                    wl_json_free(cast);
                } while (FindNextFileA(hf, &cfd));
                FindClose(hf);
            }
        }

        if (civ_file[0]) {
            char cpath[MAX_PATH];
            snprintf(cpath, sizeof(cpath), "%s/%s/civs/%s", s->mods_dir, mod_folder, civ_file);
            char *ctxt = wl_read_file(cpath, NULL);
            if (!ctxt) {
                wl_json_free(body);
                http_response_error(res, 404, "Civilization file not found in mod");
                return;
            }
            WlJsonVal *cast = wl_json_parse(ctxt, NULL, 0);
            free(ctxt);
            if (cast && cast->type == WL_JSON_OBJECT) {
                const WlJsonVal *ext = wl_json_get(cast, "extend");
                if (ext && ext->type == WL_JSON_NUMBER) {
                    civ_id = (int)ext->u.num.ival;
                    const char *cn = catalog_civ_name(s->catalog, civ_id);
                    snprintf(civ_name, sizeof(civ_name), "%s", cn ? cn : civ_file);
                } else {
                    const char *n = wl_json_str(wl_json_get(cast, "name"), NULL);
                    if (n && n[0]) snprintf(civ_name, sizeof(civ_name), "%s", n);
                    civ_id = lookup_civ_in_cache(s->game_dir, mod_folder, civ_file, civ_name);
                }
            }
            wl_json_free(cast);
        } else {
            // Check cache\civs.txt by name, then vanilla catalog by name
            civ_id = lookup_civ_in_cache(s->game_dir, mod_folder, NULL, req_civ);
            if (civ_id < 0 && s->catalog) {
                for (size_t i = 0; i < s->catalog->civ_count; i++) {
                    if (_stricmp(s->catalog->civs[i].name, req_civ) == 0) {
                        civ_id = s->catalog->civs[i].id;
                        snprintf(civ_name, sizeof(civ_name), "%s", s->catalog->civs[i].name);
                        break;
                    }
                }
            }
            if (civ_id < 0) {
                wl_json_free(body);
                http_response_error(res, 404, "Civilization not found");
                return;
            }
        }
    }

    // 3. Extract match options with sensible defaults.
    const WlJsonVal *v_map     = wl_json_get(body, "map");
    const char *size_str       = wl_json_str(wl_json_get(body, "size"), "tiny");
    const WlJsonVal *v_ai      = wl_json_get(body, "ai");
    int ai_count               = (v_ai && v_ai->type == WL_JSON_NUMBER) ? (int)v_ai->u.num.ival : 1;
    if (ai_count < 0) ai_count = 0;
    if (ai_count > 7) ai_count = 7;
    const WlJsonVal *v_aiciv   = wl_json_get(body, "ai_civ");
    const char *diff_str       = wl_json_str(wl_json_get(body, "difficulty"), "easiest");
    const char *res_str        = wl_json_str(wl_json_get(body, "resources"), "standard");
    const WlJsonVal *v_reveal  = wl_json_get(body, "reveal_map");
    const WlJsonVal *v_cheats  = wl_json_get(body, "cheats");
    int cheats                 = (v_cheats && v_cheats->type == WL_JSON_BOOL) ? v_cheats->u.bool_val : 1;
    const WlJsonVal *v_launch  = wl_json_get(body, "launch");
    int want_launch            = (v_launch && v_launch->type == WL_JSON_BOOL) ? v_launch->u.bool_val : 1;

    // 4. Write <game>/wololoader/autostart.json atomically.
    char wl_dir[MAX_PATH], as_path[MAX_PATH];
    snprintf(wl_dir, sizeof(wl_dir), "%s/wololoader", s->game_dir);
    CreateDirectoryA(wl_dir, NULL);
    snprintf(as_path, sizeof(as_path), "%s/autostart.json", wl_dir);

    ApiBuf ab;
    buf_init(&ab);
    buf_puts(&ab, "{\n");
    if (civ_id >= 1) {
        char num[32];
        snprintf(num, sizeof(num), "  \"civ\": %d,\n", civ_id);
        buf_puts(&ab, num);
    }
    buf_puts(&ab, "  \"mod\": ");
    buf_puts_escaped(&ab, mod_folder);
    buf_puts(&ab, ",\n");
    if (civ_file[0]) {
        buf_puts(&ab, "  \"civ_file\": ");
        buf_puts_escaped(&ab, civ_file);
        buf_puts(&ab, ",\n");
    }
    if (civ_name[0]) {
        buf_puts(&ab, "  \"civ_name\": ");
        buf_puts_escaped(&ab, civ_name);
        buf_puts(&ab, ",\n");
    }
    buf_puts(&ab, "  \"map\": ");
    if (v_map && v_map->type == WL_JSON_NUMBER) {
        char num[32];
        snprintf(num, sizeof(num), "%d", (int)v_map->u.num.ival);
        buf_puts(&ab, num);
    } else {
        buf_puts_escaped(&ab, (v_map && v_map->type == WL_JSON_STRING && v_map->u.str[0]) ? v_map->u.str : "Arabia");
    }
    buf_puts(&ab, ",\n  \"size\": ");
    buf_puts_escaped(&ab, size_str);
    {
        char num[32];
        snprintf(num, sizeof(num), ",\n  \"ai\": %d,\n", ai_count);
        buf_puts(&ab, num);
    }
    buf_puts(&ab, "  \"ai_civ\": ");
    if (v_aiciv && v_aiciv->type == WL_JSON_NUMBER) {
        char num[32];
        snprintf(num, sizeof(num), "%d", (int)v_aiciv->u.num.ival);
        buf_puts(&ab, num);
    } else {
        buf_puts_escaped(&ab, (v_aiciv && v_aiciv->type == WL_JSON_STRING && v_aiciv->u.str[0]) ? v_aiciv->u.str : "random");
    }
    buf_puts(&ab, ",\n  \"difficulty\": ");
    buf_puts_escaped(&ab, diff_str);
    buf_puts(&ab, ",\n  \"resources\": ");
    buf_puts_escaped(&ab, res_str);
    buf_puts(&ab, ",\n  \"reveal_map\": ");
    if (v_reveal && v_reveal->type == WL_JSON_STRING) {
        buf_puts_escaped(&ab, v_reveal->u.str);
    } else if (v_reveal && v_reveal->type == WL_JSON_NUMBER) {
        char num[32];
        snprintf(num, sizeof(num), "%d", (int)v_reveal->u.num.ival);
        buf_puts(&ab, num);
    } else {
        buf_puts(&ab, (v_reveal && v_reveal->type == WL_JSON_BOOL && v_reveal->u.bool_val) ? "true" : "false");
    }
    buf_puts(&ab, ",\n  \"cheats\": ");
    buf_puts(&ab, cheats ? "true" : "false");
    buf_puts(&ab, "\n}\n");

    int wrc = modio_atomic_write_file(as_path, ab.data, ab.len, err, sizeof(err));
    free(ab.data);
    wl_json_free(body);
    if (wrc != 0) {
        http_response_error(res, 500, err);
        return;
    }

    // 5. Launch the game through Steam if not already running (and launch != false).
    int game_running = launcher_is_game_running();
    int launched = 0;
    const char *status = "ready";
    const char *message = "Autostart written.";

    if (game_running) {
        status = "waiting_main_menu";
        message = "Game is already running. Return to the Main Menu to start the test match.";
    } else if (want_launch) {
        launcher_launch_steam_game();
        launched = 1;
        status = "starting";
        message = "Starting the game...";
    } else {
        status = "ready";
        message = "Autostart written (Steam launch skipped).";
    }

    ApiBuf rb;
    buf_init(&rb);
    char hdr[128];
    snprintf(hdr, sizeof(hdr), "{\"ok\":true,\"civ_id\":%d,\"mod_was_enabled\":%s,\"game_running\":%s,\"launched\":%s",
             civ_id,
             mod_was_disabled ? "true" : "false",
             game_running ? "true" : "false",
             launched ? "true" : "false");
    buf_puts(&rb, hdr);
    buf_puts(&rb, ",\"civ_name\":");
    buf_puts_escaped(&rb, civ_name);
    buf_puts(&rb, ",\"status\":");
    buf_puts_escaped(&rb, status);
    buf_puts(&rb, ",\"message\":");
    buf_puts_escaped(&rb, message);
    buf_puts(&rb, "}");

    http_response_set_json(res, 200, rb.data);
    free(rb.data);
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
        handle_get_mods(s, req, res);
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
    } else if (strcmp(req->path, "/api/testit") == 0 && strcmp(req->method, "POST") == 0) {
        handle_post_testit(s, req, res);
    } else if (strcmp(req->path, "/api/palette") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_palette(s, res);
    } else if (strcmp(req->path, "/api/game-file") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_game_file(s, req, res);
    } else if (strcmp(req->path, "/api/game-list") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_game_list(s, req, res);
    } else if (strcmp(req->path, "/api/settings") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_settings(s, res);
    } else if (strcmp(req->path, "/api/settings") == 0 && strcmp(req->method, "PUT") == 0) {
        handle_put_settings(s, req, res);
    } else if (strcmp(req->path, "/api/file") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_file(s, req, res);
    } else if (strcmp(req->path, "/api/file") == 0 && strcmp(req->method, "PUT") == 0) {
        handle_put_file(s, req, res);
    } else if (strcmp(req->path, "/api/file") == 0 && strcmp(req->method, "DELETE") == 0) {
        handle_delete_file(s, req, res);
    } else if (strcmp(req->path, "/api/files") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_files(s, req, res);
    } else if (strcmp(req->path, "/api/mod-tree") == 0 && strcmp(req->method, "GET") == 0) {
        handle_get_mod_tree(s, req, res);
    } else if (strcmp(req->path, "/api/hub/index") == 0 && strcmp(req->method, "GET") == 0) {
        hub_handle_index(s, req, res);
    } else if (strcmp(req->path, "/api/hub/icon") == 0 && strcmp(req->method, "GET") == 0) {
        hub_handle_icon(s, req, res);
    } else if (strcmp(req->path, "/api/hub/install") == 0 && strcmp(req->method, "POST") == 0) {
        hub_handle_install(s, req, res);
    } else if (strcmp(req->path, "/api/hub/uninstall") == 0 && strcmp(req->method, "POST") == 0) {
        hub_handle_uninstall(s, req, res);
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
