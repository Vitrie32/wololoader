// ==========================================================================
// Scriptorium v2 - Mod Hub HTTP Adapter
//
// Purpose:
//   Implements Scriptorium's /api/hub/* HTTP routes on top of the shared
//   hubcore module (src/hubcore.c), maintaining 10-minute index caching,
//   X-Scriptorium-Token protection, and identical JSON responses.
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "hub.h"
#include "sc_zlib.h"
#include "../src/hubcore.h"
#include "../src/json.h"
#include "../src/wololoader.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int get_qparam(const char *query, const char *key, char *out, size_t out_cap) {
    if (!query || !key || !out || out_cap == 0) return 0;
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *p = query;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val = p + klen + 1;
            const char *end = strchr(val, '&');
            size_t vlen = end ? (size_t)(end - val) : strlen(val);
            size_t o = 0;
            for (size_t i = 0; i < vlen && o + 1 < out_cap; i++) {
                if (val[i] == '%' && i + 2 < vlen &&
                    isxdigit((unsigned char)val[i + 1]) &&
                    isxdigit((unsigned char)val[i + 2])) {
                    char hex[3] = { val[i + 1], val[i + 2], 0 };
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

static int sc_inflate_cb(const unsigned char *in, size_t in_len, size_t max_out,
                         unsigned char **out, size_t *out_len, void *user) {
    (void)user;
    return sc_inflate_raw_bounded(in, in_len, max_out, out, out_len);
}

typedef struct {
    int        valid;
    char       url[HUB_PATH_CAP];
    time_t     fetched_at;
    WlJsonVal *root;
} HubCache;

static HubCache g_hub_cache;

static int hub_get_index(ServerState *s, int force_refresh, WlJsonVal **out_root,
                         char *out_eff_url, size_t eff_cap,
                         char *out_cfg_url, size_t cfg_cap,
                         int *out_env_override, int *out_cached,
                         time_t *out_fetched_at,
                         char *err, size_t err_cap) {
    char eff_url[HUB_PATH_CAP];
    hub_resolve_index_url(s->game_dir, eff_url, sizeof(eff_url), out_cfg_url, cfg_cap, out_env_override);
    if (out_eff_url && eff_cap > 0) snprintf(out_eff_url, eff_cap, "%s", eff_url);

    time_t now = time(NULL);
    if (!force_refresh && g_hub_cache.valid && g_hub_cache.root &&
        strcmp(g_hub_cache.url, eff_url) == 0 &&
        (now - g_hub_cache.fetched_at) < HUB_CACHE_TTL_SEC) {
        *out_root = g_hub_cache.root;
        if (out_cached) *out_cached = 1;
        if (out_fetched_at) *out_fetched_at = g_hub_cache.fetched_at;
        return 0;
    }

    WlJsonVal *parsed = hub_fetch_and_parse_index(eff_url, err, err_cap);
    if (!parsed) return -1;

    wl_json_free(g_hub_cache.root);
    g_hub_cache.root       = parsed;
    g_hub_cache.valid      = 1;
    g_hub_cache.fetched_at = now;
    snprintf(g_hub_cache.url, sizeof(g_hub_cache.url), "%s", eff_url);

    *out_root = parsed;
    if (out_cached) *out_cached = 0;
    if (out_fetched_at) *out_fetched_at = now;
    return 0;
}

// GET /api/hub/index[?refresh=1]
void hub_handle_index(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char refresh_str[8] = "";
    int force_refresh = get_qparam(req->query, "refresh", refresh_str, sizeof(refresh_str)) &&
                        refresh_str[0] == '1';

    WlJsonVal *root = NULL;
    char eff_url[HUB_PATH_CAP] = "";
    char cfg_url[HUB_PATH_CAP] = "";
    int env_override = 0, cached = 0;
    time_t fetched_at = 0;
    char err[512] = "";

    if (hub_get_index(s, force_refresh, &root, eff_url, sizeof(eff_url),
                      cfg_url, sizeof(cfg_url), &env_override, &cached,
                      &fetched_at, err, sizeof(err)) != 0) {
        http_response_error(res, 502, err);
        return;
    }

    HubBuf b;
    hub_buf_init(&b);
    hub_buf_puts(&b, "{\n  \"schema_version\": 1,\n  \"source_url\": ");
    hub_buf_puts_escaped(&b, eff_url);
    hub_buf_puts(&b, ",\n  \"configured_url\": ");
    hub_buf_puts_escaped(&b, cfg_url);
    hub_buf_puts(&b, ",\n  \"default_url\": ");
    hub_buf_puts_escaped(&b, HUB_DEFAULT_URL);
    hub_buf_puts(&b, ",\n  \"env_override\": ");
    hub_buf_puts(&b, env_override ? "true" : "false");
    hub_buf_puts(&b, ",\n  \"cached\": ");
    hub_buf_puts(&b, cached ? "true" : "false");
    {
        char num[128];
        snprintf(num, sizeof(num), ",\n  \"fetched_at\": %lld,\n  \"wololoader_version\": ",
                 (long long)fetched_at);
        hub_buf_puts(&b, num);
        hub_buf_puts_escaped(&b, WOLOLOADER_VERSION);
    }
    const char *idx_updated = wl_json_str(wl_json_get(root, "updated"), "");
    hub_buf_puts(&b, ",\n  \"updated\": ");
    hub_buf_puts_escaped(&b, idx_updated);

    hub_buf_puts(&b, ",\n  \"mods\": [");
    const WlJsonVal *mods = wl_json_get(root, "mods");
    int first = 1;
    for (size_t i = 0; mods && i < mods->u.arr.count; i++) {
        const WlJsonVal *m = mods->u.arr.items[i];
        if (!m || m->type != WL_JSON_OBJECT) continue;

        const char *id = wl_json_str(wl_json_get(m, "id"), "");
        if (!hub_is_valid_id(id)) continue;

        const char *name   = wl_json_str(wl_json_get(m, "name"), id);
        const char *ver    = wl_json_str(wl_json_get(m, "version"), "0.1");
        const char *author = wl_json_str(wl_json_get(m, "author"), "");
        const char *desc   = wl_json_str(wl_json_get(m, "description"), "");
        const char *icon   = wl_json_str(wl_json_get(m, "icon"), "");
        const char *dl     = wl_json_str(wl_json_get(m, "download"), "");
        const char *sha    = wl_json_str(wl_json_get(m, "sha256"), "");
        const char *req_wl = wl_json_str(wl_json_get(m, "requires_wololoader"), "");
        const char *upd    = wl_json_str(wl_json_get(m, "updated"), "");
        const WlJsonVal *v_sz = wl_json_get(m, "size");
        int64_t sz = (v_sz && v_sz->type == WL_JSON_NUMBER) ? v_sz->u.num.ival : 0;

        HubLocalState local;
        hub_inspect_local_mod(s->mods_dir, id, &local);
        int update_avail = local.installed && (hub_compare_versions(ver, local.version) > 0);
        int compat = (!req_wl[0] || hub_compare_versions(WOLOLOADER_VERSION, req_wl) >= 0);

        if (!first) hub_buf_puts(&b, ", ");
        first = 0;

        hub_buf_puts(&b, "{\"id\":");          hub_buf_puts_escaped(&b, id);
        hub_buf_puts(&b, ",\"name\":");       hub_buf_puts_escaped(&b, name);
        hub_buf_puts(&b, ",\"version\":");    hub_buf_puts_escaped(&b, ver);
        hub_buf_puts(&b, ",\"author\":");     hub_buf_puts_escaped(&b, author);
        hub_buf_puts(&b, ",\"description\":"); hub_buf_puts_escaped(&b, desc);
        hub_buf_puts(&b, ",\"icon\":");       hub_buf_puts_escaped(&b, icon);
        hub_buf_puts(&b, ",\"download\":");   hub_buf_puts_escaped(&b, dl);
        hub_buf_puts(&b, ",\"sha256\":");     hub_buf_puts_escaped(&b, sha);
        {
            char sbuf[48];
            snprintf(sbuf, sizeof(sbuf), ",\"size\":%lld", (long long)sz);
            hub_buf_puts(&b, sbuf);
        }
        hub_buf_puts(&b, ",\"requires_wololoader\":"); hub_buf_puts_escaped(&b, req_wl);
        hub_buf_puts(&b, ",\"updated\":");    hub_buf_puts_escaped(&b, upd);

        hub_buf_puts(&b, ",\"tags\":[");
        const WlJsonVal *v_tags = wl_json_get(m, "tags");
        if (v_tags && v_tags->type == WL_JSON_ARRAY) {
            int ft = 1;
            for (size_t k = 0; k < v_tags->u.arr.count; k++) {
                const char *t = wl_json_str(v_tags->u.arr.items[k], NULL);
                if (!t) continue;
                if (!ft) hub_buf_puts(&b, ",");
                ft = 0;
                hub_buf_puts_escaped(&b, t);
            }
        }
        hub_buf_puts(&b, "]");

        hub_buf_puts(&b, ",\"depends\":[");
        const WlJsonVal *v_deps = wl_json_get(m, "depends");
        if (v_deps && v_deps->type == WL_JSON_ARRAY) {
            int fd = 1;
            for (size_t k = 0; k < v_deps->u.arr.count; k++) {
                const char *d = wl_json_str(v_deps->u.arr.items[k], NULL);
                if (!d) continue;
                if (!fd) hub_buf_puts(&b, ",");
                fd = 0;
                hub_buf_puts_escaped(&b, d);
            }
        }
        hub_buf_puts(&b, "]");

        hub_buf_puts(&b, ",\"installed\":");
        hub_buf_puts(&b, local.installed ? "true" : "false");
        hub_buf_puts(&b, ",\"installed_version\":");
        hub_buf_puts_escaped(&b, local.installed ? local.version : "");
        hub_buf_puts(&b, ",\"installed_enabled\":");
        hub_buf_puts(&b, (local.installed && !local.enabled) ? "false" : "true");
        hub_buf_puts(&b, ",\"hub_managed\":");
        hub_buf_puts(&b, local.hub_managed ? "true" : "false");
        hub_buf_puts(&b, ",\"update_available\":");
        hub_buf_puts(&b, update_avail ? "true" : "false");
        hub_buf_puts(&b, ",\"compatible\":");
        hub_buf_puts(&b, compat ? "true" : "false");
        hub_buf_puts(&b, "}");
    }
    hub_buf_puts(&b, "]\n}");

    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// GET /api/hub/icon?id=<mod_id>
void hub_handle_icon(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char id[128] = "";
    if (!get_qparam(req->query, "id", id, sizeof(id)) || !hub_is_valid_id(id)) {
        http_response_error(res, 400, "Missing or invalid mod id");
        return;
    }

    WlJsonVal *root = NULL;
    char eff_url[HUB_PATH_CAP] = "";
    char err[256] = "";
    if (hub_get_index(s, 0, &root, eff_url, sizeof(eff_url), NULL, 0,
                      NULL, NULL, NULL, err, sizeof(err)) != 0) {
        http_response_error(res, 502, err);
        return;
    }

    const WlJsonVal *m = hub_find_mod_in_index(root, id);
    if (!m) {
        http_response_error(res, 404, "Mod not found in hub index");
        return;
    }
    const char *icon_raw = wl_json_str(wl_json_get(m, "icon"), "");
    if (!icon_raw || !icon_raw[0]) {
        http_response_error(res, 404, "No icon URL for this mod");
        return;
    }

    char icon_url[HUB_PATH_CAP];
    hub_resolve_rel_url(eff_url, icon_raw, icon_url, sizeof(icon_url));

    char *data = NULL;
    size_t len = 0;
    if (hub_fetch_stream(icon_url, HUB_MAX_ICON_BYTES, NULL, &data, &len,
                         NULL, NULL, NULL, err, sizeof(err)) != 0) {
        http_response_error(res, 404, err);
        return;
    }

    const char *ctype = "image/png";
    if (len >= 3 && (unsigned char)data[0] == 0xff && (unsigned char)data[1] == 0xd8) {
        ctype = "image/jpeg";
    } else if (len >= 4 && memcmp(data, "GIF8", 4) == 0) {
        ctype = "image/gif";
    } else if (len >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
        ctype = "image/webp";
    }

    http_response_free(res);
    res->status_code     = 200;
    res->status_text     = "OK";
    res->content_type    = ctype;
    res->body            = data;
    res->body_len        = len;
    res->is_dynamic_body = 1;
}

// POST /api/hub/install { "id": "...", "overwrite"?: bool }
void hub_handle_install(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    char jerr[256] = "";
    WlJsonVal *body = wl_json_parse(req->body ? req->body : "", jerr, sizeof(jerr));
    if (!body || body->type != WL_JSON_OBJECT) {
        wl_json_free(body);
        http_response_error(res, 400, "Invalid JSON body");
        return;
    }

    const char *id = wl_json_str(wl_json_get(body, "id"), NULL);
    int overwrite  = wl_json_is_true(wl_json_get(body, "overwrite"));
    if (!id || !hub_is_valid_id(id)) {
        wl_json_free(body);
        http_response_error(res, 400, "Missing or invalid mod id");
        return;
    }

    char id_copy[128];
    snprintf(id_copy, sizeof(id_copy), "%s", id);
    wl_json_free(body);

    WlJsonVal *root = NULL;
    char eff_url[HUB_PATH_CAP] = "";
    char err[512] = "";
    if (hub_get_index(s, 0, &root, eff_url, sizeof(eff_url), NULL, 0,
                      NULL, NULL, NULL, err, sizeof(err)) != 0) {
        http_response_error(res, 502, err);
        return;
    }

    HubInstallResult ir;
    if (hub_install_mod(s->game_dir, s->mods_dir, root, eff_url, id_copy, overwrite,
                        WOLOLOADER_VERSION, sc_inflate_cb, NULL,
                        NULL, NULL, NULL, NULL, &ir) != 0) {
        http_response_error(res, ir.http_status ? ir.http_status : 400, ir.err);
        return;
    }

    HubBuf b;
    hub_buf_init(&b);
    hub_buf_puts(&b, "{\"ok\":true,\"id\":");
    hub_buf_puts_escaped(&b, id_copy);
    hub_buf_puts(&b, ",\"version\":");
    hub_buf_puts_escaped(&b, ir.version);
    hub_buf_puts(&b, ",\"updated\":");
    hub_buf_puts(&b, ir.updated ? "true" : "false");
    hub_buf_puts(&b, ",\"installed_depends\":[");
    for (int i = 0; i < ir.installed_dep_count; i++) {
        if (i > 0) hub_buf_puts(&b, ",");
        hub_buf_puts_escaped(&b, ir.installed_deps[i]);
    }
    hub_buf_puts(&b, "]}");

    hub_install_result_free(&ir);
    http_response_set_json(res, 200, b.data);
    free(b.data);
}

// POST /api/hub/uninstall { "id": "..." }
void hub_handle_uninstall(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    WlJsonVal *body = wl_json_parse(req->body ? req->body : "", NULL, 0);
    if (!body || body->type != WL_JSON_OBJECT) {
        wl_json_free(body);
        http_response_error(res, 400, "Invalid JSON body");
        return;
    }

    const char *id = wl_json_str(wl_json_get(body, "id"), NULL);
    if (!id || !hub_is_valid_id(id)) {
        wl_json_free(body);
        http_response_error(res, 400, "Missing or invalid mod id");
        return;
    }

    char id_copy[128];
    snprintf(id_copy, sizeof(id_copy), "%s", id);
    wl_json_free(body);

    char trash_dir[HUB_PATH_CAP] = "";
    char err[256] = "";
    int status = 500;
    if (hub_uninstall_mod(s->game_dir, s->mods_dir, id_copy, trash_dir, sizeof(trash_dir),
                          &status, err, sizeof(err)) != 0) {
        http_response_error(res, status, err);
        return;
    }

    HubBuf b;
    hub_buf_init(&b);
    hub_buf_puts(&b, "{\"ok\":true,\"id\":");
    hub_buf_puts_escaped(&b, id_copy);
    hub_buf_puts(&b, ",\"trash_path\":");
    hub_buf_puts_escaped(&b, trash_dir);
    hub_buf_puts(&b, "}");

    http_response_set_json(res, 200, b.data);
    free(b.data);
}
