// ==========================================================================
// Scriptorium v2 - Entry Point & Verification Suite
//
// Purpose:
//   Main application entry point for Scriptorium v2.
//   - Parses command line: [--game <dir>] [--port N] [--no-window] [--selftest]
//   - Attaches to parent console (AttachConsole) so --selftest output prints
//   - Initializes zlib and catalog from game directory
//   - If --selftest: starts the server on an ephemeral port in a thread with
//     --no-window semantics, hits every endpoint over real sockets, verifies
//     byte-identity of GET then PUT Monklanians, and validates mod state.
//   - Otherwise: starts server, launches app-mode browser window (Edge/Chrome
//     or Wine 'start /unix'), and monitors heartbeat.
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "api.h"
#include "launcher.h"
#include "catalog.h"
#include "sc_zlib.h"
#include "modio.h"
#include "../src/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <windows.h>

static FILE *s_log_file = NULL;

static void sc_log_init(const char *game_dir) {
    if (!game_dir || !game_dir[0]) return;

    char log_dir[MAX_PATH];
    snprintf(log_dir, sizeof(log_dir), "%s/wololoader", game_dir);
    CreateDirectoryA(log_dir, NULL);

    char log_path[MAX_PATH];
    snprintf(log_path, sizeof(log_path), "%s/wololoader/scriptorium.log", game_dir);
    s_log_file = fopen(log_path, "a");
}

static void sc_log(const char *fmt, ...) {
    va_list a1, a2;
    va_start(a1, fmt);
    va_copy(a2, a1);

    vfprintf(stdout, fmt, a1);
    fflush(stdout);

    if (s_log_file) {
        vfprintf(s_log_file, fmt, a2);
        fflush(s_log_file);
    }

    va_end(a1);
    va_end(a2);
}

// ==========================================================================
// Socket Helper for --selftest
// ==========================================================================

static int http_raw_request(int port, const char *method, const char *path,
                            const char *token, const char *body,
                            int *out_status, char **out_body, size_t *out_len) {
    if (out_status) *out_status = 0;
    if (out_body) *out_body = NULL;
    if (out_len) *out_len = 0;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return -1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port        = htons((unsigned short)port);

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        closesocket(sock);
        return -1;
    }

    size_t body_len = body ? strlen(body) : 0;
    char req_buf[2048];
    int hlen = snprintf(req_buf, sizeof(req_buf),
        "%s %s HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "%s%s%s"
        "Content-Length: %zu\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n",
        method, path, port,
        token ? "X-Scriptorium-Token: " : "",
        token ? token : "",
        token ? "\r\n" : "",
        body_len);

    send(sock, req_buf, hlen, 0);
    if (body_len > 0) {
        send(sock, body, (int)body_len, 0);
    }

    // Read response until server closes connection
    size_t cap = 8192;
    size_t len = 0;
    char *resp = malloc(cap);
    if (!resp) { closesocket(sock); return -1; }

    while (1) {
        int n = recv(sock, resp + len, (int)(cap - len - 1), 0);
        if (n <= 0) break;
        len += (size_t)n;
        resp[len] = '\0';
        if (len + 1024 >= cap) {
            cap *= 2;
            char *nresp = realloc(resp, cap);
            if (!nresp) break;
            resp = nresp;
        }
    }
    closesocket(sock);

    // Parse HTTP status code from "HTTP/1.1 200 ..."
    int status = 0;
    char *space = strchr(resp, ' ');
    if (space) {
        status = atoi(space + 1);
    }
    if (out_status) *out_status = status;

    // Split headers and body at "\r\n\r\n"
    char *hdr_end = strstr(resp, "\r\n\r\n");
    if (hdr_end) {
        char *b_start = hdr_end + 4;
        size_t b_len = len - (size_t)(b_start - resp);
        if (out_body) {
            *out_body = malloc(b_len + 1);
            if (*out_body) {
                memcpy(*out_body, b_start, b_len);
                (*out_body)[b_len] = '\0';
            }
        }
        if (out_len) *out_len = b_len;
    }
    free(resp);
    return 0;
}

// Thread wrapper to run server in background during selftest
static DWORD WINAPI server_thread_proc(LPVOID lpParam) {
    ServerState *s = (ServerState *)lpParam;
    server_run(s);
    return 0;
}

// ==========================================================================
// Automated Selftest Suite
// ==========================================================================

static int sc_run_selftest(const char *game_dir) {
    sc_log("====================================================\n");
    sc_log("[selftest] Scriptorium v2 Automated Test Suite\n");
    sc_log("[selftest] Game directory: '%s'\n", game_dir);
    sc_log("====================================================\n");

    int all_ok = 1;

    // 1. Initialize Zlib
    char err[256] = "";
    const WlZlib *z = sc_zlib_init(game_dir, err, sizeof(err));
    if (!z) {
        sc_log("[selftest] ERROR: zlib init failed: %s\n", err);
        return 1;
    }
    sc_log("[selftest] zlib initialized successfully.\n");

    // 2. Load Catalog
    ScCatalog cat;
    memset(&cat, 0, sizeof(cat));
    if (sc_catalog_load(z, game_dir, &cat, err, sizeof(err)) != 0) {
        sc_log("[selftest] ERROR: Catalog load failed: %s\n", err);
        return 1;
    }
    sc_log("[selftest] Catalog loaded: %zu civs, %zu units, %zu techs.\n",
           cat.civ_count, cat.unit_count, cat.tech_count);

    // 3. Start server in a background thread on a free port (port 0)
    ServerState server;
    memset(&server, 0, sizeof(server));
    server.ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (server_init(&server, game_dir, &cat, z, 0) != 0) {
        sc_log("[selftest] ERROR: server_init failed.\n");
        sc_catalog_free(&cat);
        return 1;
    }

    HANDLE hThread = CreateThread(NULL, 0, server_thread_proc, &server, 0, NULL);
    WaitForSingleObject(server.ready_event, 5000);
    CloseHandle(server.ready_event);

    int port = server.port;
    const char *token = server.token;
    sc_log("[selftest] Server listening on 127.0.0.1:%d with token %s\n", port, token);

    // Endpoint 1: GET /api/info
    {
        int status = 0;
        char *resp = NULL;
        if (http_raw_request(port, "GET", "/api/info", token, NULL, &status, &resp, NULL) != 0 || status != 200) {
            sc_log("[selftest] ERROR: GET /api/info failed (status %d)\n", status);
            all_ok = 0;
        } else {
            WlJsonVal *j = wl_json_parse(resp, NULL, 0);
            const WlJsonVal *v_ver = wl_json_get(j, "version");
            if (v_ver && v_ver->type == WL_JSON_STRING && strcmp(v_ver->u.str, "2.0") == 0) {
                sc_log("[selftest] [1/8] GET /api/info: OK (version 2.0)\n");
            } else {
                sc_log("[selftest] ERROR: GET /api/info version != 2.0\n");
                all_ok = 0;
            }
            wl_json_free(j);
        }
        if (resp) free(resp);
    }

    // Endpoint 2: GET /api/catalog
    {
        int status = 0;
        char *resp = NULL;
        if (http_raw_request(port, "GET", "/api/catalog", token, NULL, &status, &resp, NULL) != 0 || status != 200) {
            sc_log("[selftest] ERROR: GET /api/catalog failed (status %d)\n", status);
            all_ok = 0;
        } else {
            WlJsonVal *j = wl_json_parse(resp, NULL, 0);
            const WlJsonVal *vc = wl_json_get(j, "civs");
            const WlJsonVal *vu = wl_json_get(j, "units");
            const WlJsonVal *vt = wl_json_get(j, "techs");
            const WlJsonVal *vs = wl_json_get(j, "stats");
            if (vc && vu && vt && vs && vc->type == WL_JSON_ARRAY && vu->type == WL_JSON_ARRAY) {
                sc_log("[selftest] [2/8] GET /api/catalog: OK (%zu civs, %zu units, %zu techs, %zu stats)\n",
                       vc->u.arr.count, vu->u.arr.count, vt->u.arr.count, vs->u.arr.count);
            } else {
                sc_log("[selftest] ERROR: GET /api/catalog missing expected arrays\n");
                all_ok = 0;
            }
            wl_json_free(j);
        }
        if (resp) free(resp);
    }

    // Endpoint 3: GET /api/stats?civ=1&unit=125
    {
        int status = 0;
        char *resp = NULL;
        if (http_raw_request(port, "GET", "/api/stats?civ=1&unit=125", token, NULL, &status, &resp, NULL) != 0 || status != 200) {
            sc_log("[selftest] ERROR: GET /api/stats?civ=1&unit=125 failed (status %d)\n", status);
            all_ok = 0;
        } else {
            WlJsonVal *j = wl_json_parse(resp, NULL, 0);
            const WlJsonVal *v_hp = wl_json_get(j, "hit_points");
            if (v_hp && v_hp->type == WL_JSON_NUMBER) {
                sc_log("[selftest] [3/8] GET /api/stats?civ=1&unit=125: OK (hit_points=%lld)\n", (long long)v_hp->u.num.ival);
            } else {
                sc_log("[selftest] ERROR: GET /api/stats missing hit_points field\n");
                all_ok = 0;
            }
            wl_json_free(j);
        }
        if (resp) free(resp);
    }

    // Endpoint 4: GET /api/mods
    {
        int status = 0;
        char *resp = NULL;
        if (http_raw_request(port, "GET", "/api/mods", token, NULL, &status, &resp, NULL) != 0 || status != 200) {
            sc_log("[selftest] ERROR: GET /api/mods failed (status %d)\n", status);
            all_ok = 0;
        } else {
            WlJsonVal *j = wl_json_parse(resp, NULL, 0);
            if (j && j->type == WL_JSON_ARRAY && j->u.arr.count > 0) {
                sc_log("[selftest] [4/8] GET /api/mods: OK (%zu mod(s) detected)\n", j->u.arr.count);
            } else {
                sc_log("[selftest] ERROR: GET /api/mods returned 0 mods\n");
                all_ok = 0;
            }
            wl_json_free(j);
        }
        if (resp) free(resp);
    }

    // Endpoint 5: GET /api/mod/Monklanians then PUT the same data -> check files are byte-identical
    {
        char mjson_path[MAX_PATH];
        char cjson_path[MAX_PATH];
        snprintf(mjson_path, sizeof(mjson_path), "%s/wololoader/mods/Monklanians/mod.json", game_dir);
        snprintf(cjson_path, sizeof(cjson_path), "%s/wololoader/mods/Monklanians/civs/monklanians.json", game_dir);

        int status = 0;
        char *resp = NULL;
        if (http_raw_request(port, "GET", "/api/mod/Monklanians", token, NULL, &status, &resp, NULL) != 0 || status != 200) {
            sc_log("[selftest] ERROR: GET /api/mod/Monklanians failed (status %d)\n", status);
            all_ok = 0;
        } else {
            WlJsonVal *det = wl_json_parse(resp, NULL, 0);
            if (!det || det->type != WL_JSON_OBJECT) {
                sc_log("[selftest] ERROR: GET /api/mod/Monklanians returned invalid JSON\n");
                all_ok = 0;
            } else {
                const WlJsonVal *v_mod  = wl_json_get(det, "mod");
                const WlJsonVal *v_civs = wl_json_get(det, "civs");

                // Serialize mod and civs into PUT payload: { "mod": ..., "civs": ... }
                size_t mlen = 0, clen = 0;
                char *mstr = modio_json_write_pretty(v_mod, &mlen);
                char *cstr = modio_json_write_pretty(v_civs, &clen);

                // Strip trailing newline for clean JSON embedding
                while (mlen > 0 && (mstr[mlen - 1] == '\n' || mstr[mlen - 1] == '\r')) mstr[--mlen] = '\0';
                while (clen > 0 && (cstr[clen - 1] == '\n' || cstr[clen - 1] == '\r')) cstr[--clen] = '\0';

                size_t put_cap = mlen + clen + 128;
                char *put_json = malloc(put_cap);
                snprintf(put_json, put_cap, "{\"mod\":%s,\"civs\":%s}", mstr, cstr);
                free(mstr);
                free(cstr);

                // Read files before PUT
                size_t b_m_sz = 0, b_c_sz = 0;
                char *b_m_data = wl_read_file(mjson_path, &b_m_sz);
                char *b_c_data = wl_read_file(cjson_path, &b_c_sz);

                int put_status = 0;
                char *put_resp = NULL;
                if (http_raw_request(port, "PUT", "/api/mod/Monklanians", token, put_json, &put_status, &put_resp, NULL) != 0 || put_status != 200) {
                    sc_log("[selftest] ERROR: PUT /api/mod/Monklanians returned status %d\n", put_status);
                    all_ok = 0;
                } else {
                    // Read files after PUT
                    size_t a_m_sz = 0, a_c_sz = 0;
                    char *a_m_data = wl_read_file(mjson_path, &a_m_sz);
                    char *a_c_data = wl_read_file(cjson_path, &a_c_sz);

                    if (!a_m_data || !a_c_data || !b_m_data || !b_c_data) {
                        sc_log("[selftest] ERROR: Could not read Monklanians files on disk\n");
                        all_ok = 0;
                    } else if (b_m_sz != a_m_sz || memcmp(b_m_data, a_m_data, b_m_sz) != 0) {
                        sc_log("[selftest] ERROR: mod.json differed after PUT! (before %zu vs after %zu bytes)\n", b_m_sz, a_m_sz);
                        all_ok = 0;
                    } else if (b_c_sz != a_c_sz || memcmp(b_c_data, a_c_data, b_c_sz) != 0) {
                        sc_log("[selftest] ERROR: monklanians.json differed after PUT! (before %zu vs after %zu bytes)\n", b_c_sz, a_c_sz);
                        all_ok = 0;
                    } else {
                        sc_log("[selftest] [5/8] GET then PUT Monklanians: files are BYTE-IDENTICAL OK (mod=%zu, civ=%zu bytes)\n",
                               a_m_sz, a_c_sz);
                    }
                    if (a_m_data) free(a_m_data);
                    if (a_c_data) free(a_c_data);
                }
                if (b_m_data) free(b_m_data);
                if (b_c_data) free(b_c_data);
                if (put_resp) free(put_resp);
                free(put_json);
            }
            wl_json_free(det);
        }
        if (resp) free(resp);
    }

    // Endpoint 6: POST /api/validate
    {
        // Re-read Monklanians detail to validate it
        int status = 0;
        char *resp = NULL;
        http_raw_request(port, "GET", "/api/mod/Monklanians", token, NULL, &status, &resp, NULL);
        if (resp) {
            WlJsonVal *det = wl_json_parse(resp, NULL, 0);
            if (det) {
                const WlJsonVal *v_mod  = wl_json_get(det, "mod");
                const WlJsonVal *v_civs = wl_json_get(det, "civs");

                size_t mlen = 0, clen = 0;
                char *mstr = modio_json_write_pretty(v_mod, &mlen);
                char *cstr = modio_json_write_pretty(v_civs, &clen);
                while (mlen > 0 && (mstr[mlen - 1] == '\n' || mstr[mlen - 1] == '\r')) mstr[--mlen] = '\0';
                while (clen > 0 && (cstr[clen - 1] == '\n' || cstr[clen - 1] == '\r')) cstr[--clen] = '\0';

                size_t vcap = mlen + clen + 256;
                char *val_body = malloc(vcap);
                snprintf(val_body, vcap, "{\"folder\":\"Monklanians\",\"mod\":%s,\"civs\":%s}", mstr, cstr);
                free(mstr);
                free(cstr);

                int vstatus = 0;
                char *vresp = NULL;
                if (http_raw_request(port, "POST", "/api/validate", token, val_body, &vstatus, &vresp, NULL) != 0 || vstatus != 200) {
                    sc_log("[selftest] ERROR: POST /api/validate returned status %d\n", vstatus);
                    all_ok = 0;
                } else {
                    WlJsonVal *vj = wl_json_parse(vresp, NULL, 0);
                    const WlJsonVal *v_ok = wl_json_get(vj, "ok");
                    const WlJsonVal *v_cb = wl_json_get(vj, "civs_built");
                    if (v_ok && v_ok->type == WL_JSON_BOOL && v_ok->u.bool_val == 1) {
                        sc_log("[selftest] [6/8] POST /api/validate: OK (%lld civ(s) built)\n",
                               v_cb && v_cb->type == WL_JSON_NUMBER ? (long long)v_cb->u.num.ival : 1);
                    } else {
                        sc_log("[selftest] ERROR: POST /api/validate failed: %s\n", vresp);
                        all_ok = 0;
                    }
                    wl_json_free(vj);
                }
                if (vresp) free(vresp);
                free(val_body);
                wl_json_free(det);
            }
            free(resp);
        }
    }

    // Endpoint 7: POST /api/mods creates a temp mod then remove it
    {
        int status = 0;
        char *resp = NULL;
        const char *new_mod_payload = "{\"name\":\"TempSelftestMod\"}";
        if (http_raw_request(port, "POST", "/api/mods", token, new_mod_payload, &status, &resp, NULL) != 0 || status != 200) {
            sc_log("[selftest] ERROR: POST /api/mods returned status %d\n", status);
            all_ok = 0;
        } else {
            WlJsonVal *j = wl_json_parse(resp, NULL, 0);
            const WlJsonVal *vf = wl_json_get(j, "folder");
            if (vf && vf->type == WL_JSON_STRING && strcmp(vf->u.str, "TempSelftestMod") == 0) {
                // Verify directory and files exist
                char tdir[MAX_PATH];
                snprintf(tdir, sizeof(tdir), "%s/wololoader/mods/TempSelftestMod", game_dir);
                char tmpath[MAX_PATH];
                snprintf(tmpath, sizeof(tmpath), "%s/mod.json", tdir);
                char tcpath[MAX_PATH];
                snprintf(tcpath, sizeof(tcpath), "%s/civs", tdir);

                if (GetFileAttributesA(tmpath) != INVALID_FILE_ATTRIBUTES) {
                    // Remove temp mod folder and files
                    remove(tmpath);
                    RemoveDirectoryA(tcpath);
                    RemoveDirectoryA(tdir);

                    if (GetFileAttributesA(tdir) == INVALID_FILE_ATTRIBUTES) {
                        sc_log("[selftest] [7/8] POST /api/mods: OK (created and cleaned up TempSelftestMod)\n");
                    } else {
                        sc_log("[selftest] ERROR: Failed removing temporary mod folder\n");
                        all_ok = 0;
                    }
                } else {
                    sc_log("[selftest] ERROR: mod.json not found after POST /api/mods\n");
                    all_ok = 0;
                }
            } else {
                sc_log("[selftest] ERROR: POST /api/mods returned invalid folder: %s\n", resp);
                all_ok = 0;
            }
            wl_json_free(j);
        }
        if (resp) free(resp);
    }

    // Endpoint 8: Missing token -> 403 Forbidden
    {
        int status = 0;
        char *resp = NULL;
        if (http_raw_request(port, "GET", "/api/info", NULL, NULL, &status, &resp, NULL) != 0) {
            sc_log("[selftest] ERROR: Request failed\n");
            all_ok = 0;
        } else if (status == 403) {
            sc_log("[selftest] [8/10] Missing token rejected with HTTP 403 Forbidden: OK\n");
        } else {
            sc_log("[selftest] ERROR: Missing token returned status %d (expected 403)\n", status);
            all_ok = 0;
        }
        if (resp) free(resp);
    }

    // Endpoint 9: GET /asset/... (textures/ui or fonts) handled gracefully
    {
        int status = 0;
        char *resp = NULL;
        if (http_raw_request(port, "GET", "/asset/textures/ui/main-menu/default/background.jpg", NULL, NULL, &status, &resp, NULL) != 0) {
            sc_log("[selftest] ERROR: Asset request failed\n");
            all_ok = 0;
        } else if (status == 200 || status == 404) {
            sc_log("[selftest] [9/10] GET /asset/... handled gracefully: OK (status %d%s)\n",
                   status, (status == 200) ? ", asset loaded" : ", graceful 404 when missing");
        } else {
            sc_log("[selftest] ERROR: GET /asset/... returned unexpected status %d\n", status);
            all_ok = 0;
        }
        if (resp) free(resp);
    }

    // Endpoint 10: GET /asset/../traversal rejected
    {
        int status = 0;
        char *resp = NULL;
        if (http_raw_request(port, "GET", "/asset/../secret.txt", NULL, NULL, &status, &resp, NULL) != 0) {
            sc_log("[selftest] ERROR: Traversal request failed\n");
            all_ok = 0;
        } else if (status == 400 || status == 403) {
            sc_log("[selftest] [10/10] Directory traversal on /asset/ rejected (status %d): OK\n", status);
        } else {
            sc_log("[selftest] ERROR: Directory traversal on /asset/ returned status %d (expected 400 or 403)\n", status);
            all_ok = 0;
        }
        if (resp) free(resp);
    }

    // Teardown server thread
    server_stop(&server);
    WaitForSingleObject(hThread, 3000);
    CloseHandle(hThread);
    server_cleanup(&server);
    sc_catalog_free(&cat);

    sc_log("====================================================\n");
    if (all_ok) {
        sc_log("[selftest] ALL TESTS PASSED\n");
        sc_log("====================================================\n");
        return 0;
    } else {
        sc_log("[selftest] TESTS FAILED\n");
        sc_log("====================================================\n");
        return 1;
    }
}

// ==========================================================================
// Main Application Entry Point
// ==========================================================================

int main(int argc, char **argv) {
    // Attach to parent console if available so console output works with -mwindows
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    char game_dir[MAX_PATH] = "";
    int  port = 0;
    int  no_window = 0;
    int  selftest = 0;
    int  open_hub = 0;   // --hub: open the Mod Hub view (in-game Mods screen button)
    char open_mod[64] = "";   // --mod <id>: open that mod's editor (in-game "Edit in Scriptorium")

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--selftest") == 0) {
            selftest = 1;
        } else if (strcmp(argv[i], "--hub") == 0) {
            open_hub = 1;
        } else if (strcmp(argv[i], "--mod") == 0 && i + 1 < argc) {
            // Only plain folder names go into the URL (letters, digits, '_', '-', '.').
            const char *id = argv[++i];
            size_t n = strlen(id), ok = n > 0 && n < sizeof(open_mod);
            for (size_t k = 0; ok && k < n; k++) {
                char c = id[k];
                ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '_' || c == '-' || c == '.';
            }
            if (ok) snprintf(open_mod, sizeof(open_mod), "%s", id);
        } else if (strcmp(argv[i], "--no-window") == 0) {
            no_window = 1;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            snprintf(game_dir, sizeof(game_dir), "%s", argv[++i]);
        }
    }

    // Auto-detect game directory if not passed
    if (!game_dir[0]) {
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
        char *last_slash = strrchr(exe_path, '\\');
        if (!last_slash) last_slash = strrchr(exe_path, '/');
        if (last_slash) *last_slash = '\0';

        char *w_slash = strrchr(exe_path, '\\');
        if (!w_slash) w_slash = strrchr(exe_path, '/');
        if (w_slash && (strcmp(w_slash + 1, "wololoader") == 0 || strcmp(w_slash + 1, "build") == 0)) {
            *w_slash = '\0';
            snprintf(game_dir, sizeof(game_dir), "%s", exe_path);
        } else {
            snprintf(game_dir, sizeof(game_dir), "%s", exe_path);
        }
    }

    sc_log_init(game_dir);

    if (selftest) {
        return sc_run_selftest(game_dir);
    }

    // Normal startup:
    printf("[scriptorium] Starting Scriptorium v2...\n");
    printf("[scriptorium] Game directory: '%s'\n", game_dir);

    char err[256] = "";
    const WlZlib *z = sc_zlib_init(game_dir, err, sizeof(err));
    if (!z) {
        fprintf(stderr, "[scriptorium] ERROR: zlib init failed: %s\n", err);
        return 1;
    }

    ScCatalog cat;
    memset(&cat, 0, sizeof(cat));
    if (sc_catalog_load(z, game_dir, &cat, err, sizeof(err)) != 0) {
        fprintf(stderr, "[scriptorium] ERROR: Catalog load failed: %s\n", err);
        return 1;
    }

    ServerState server;
    if (server_init(&server, game_dir, &cat, z, port) != 0) {
        fprintf(stderr, "[scriptorium] ERROR: Server init failed.\n");
        sc_catalog_free(&cat);
        return 1;
    }

    char url[384];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/#token=%s%s%s%s", server.port, server.token,
             open_hub ? "&hub=1" : "",    // app.js reads hub=1 and mod=<id> from the hash
             open_mod[0] ? "&mod=" : "", open_mod);

    printf("Scriptorium v2 running at %s\n", url);
    printf("Token: %s\n", server.token);
    fflush(stdout);

    if (!no_window) {
        launcher_open_browser(url);
    }

    // Run server event loop
    server_run(&server);

    // Shutdown
    server_cleanup(&server);
    sc_catalog_free(&cat);
    if (s_log_file) fclose(s_log_file);

    return 0;
}
