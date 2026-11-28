// ==========================================================================
// Scriptorium v2 - Winsock HTTP/1.1 Server Implementation
//
// Purpose:
//   Lightweight, single-threaded HTTP/1.1 server bound strictly to 127.0.0.1.
//   Enforces:
//   - Host header validation against 127.0.0.1:PORT (DNS rebinding protection)
//   - X-Scriptorium-Token header validation on all /api/* routes (403 if missing/invalid)
//   - Static file delivery from embedded UI resources (ui_files.h)
//   - Request routing to api.c
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "api.h"
#include "launcher.h"
#include "ui_files.h"
#include "icons_files.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ==========================================================================
// HTTP Response Helpers
// ==========================================================================

void http_response_init(HttpResponse *res) {
    memset(res, 0, sizeof(*res));
    res->status_code    = 200;
    res->status_text    = "OK";
    res->content_type   = "application/json; charset=utf-8";
    res->body           = NULL;
    res->body_len       = 0;
    res->is_dynamic_body= 0;
}

void http_response_free(HttpResponse *res) {
    if (res && res->is_dynamic_body && res->body) {
        free(res->body);
        res->body = NULL;
    }
}

static const char *get_status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        default:  return "Response";
    }
}

void http_response_set_json(HttpResponse *res, int status, const char *json_str) {
    http_response_free(res);
    res->status_code     = status;
    res->status_text     = get_status_text(status);
    res->content_type    = "application/json; charset=utf-8";
    res->body            = strdup(json_str ? json_str : "{}");
    res->body_len        = res->body ? strlen(res->body) : 0;
    res->is_dynamic_body = 1;
}

void http_response_set_binary(HttpResponse *res, int status, const char *content_type, const void *data, size_t len) {
    http_response_free(res);
    res->status_code     = status;
    res->status_text     = get_status_text(status);
    res->content_type    = content_type ? content_type : "application/octet-stream";
    res->body            = (char *)data;
    res->body_len        = len;
    res->is_dynamic_body = 0;
}

void http_response_error(HttpResponse *res, int status, const char *msg) {
    const char *src = msg ? msg : "An error occurred";
    char buf[1024];
    size_t o = (size_t)snprintf(buf, sizeof(buf), "{\"error\":\"");
    for (const char *p = src; *p && o + 8 < sizeof(buf); p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            buf[o++] = '\\';
            buf[o++] = (char)c;
        } else if (c == '\n') {
            buf[o++] = '\\'; buf[o++] = 'n';
        } else if (c == '\r') {
            buf[o++] = '\\'; buf[o++] = 'r';
        } else if (c == '\t') {
            buf[o++] = '\\'; buf[o++] = 't';
        } else if (c >= 0x20) {
            buf[o++] = (char)c;
        }
    }
    snprintf(buf + o, sizeof(buf) - o, "\"}");
    http_response_set_json(res, status, buf);
}

// ==========================================================================
// Random Token Generation (16 random bytes -> 32 hex chars)
// ==========================================================================

static void generate_token(char out_token[33]) {
    unsigned char bytes[16];
    int ok = 0;

    HCRYPTPROV hProv;
    if (CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptGenRandom(hProv, sizeof(bytes), bytes)) {
            ok = 1;
        }
        CryptReleaseContext(hProv, 0);
    }

    if (!ok) {
        // Fallback PRNG mixing system timer and process/thread IDs
        DWORD tick = GetTickCount();
        DWORD pid  = GetCurrentProcessId();
        DWORD tid  = GetCurrentThreadId();
        for (int i = 0; i < 16; i++) {
            tick = tick * 1103515245 + 12345 + pid + (tid << i);
            bytes[i] = (unsigned char)(tick >> 16);
        }
    }

    for (int i = 0; i < 16; i++) {
        sprintf(out_token + (i * 2), "%02x", bytes[i]);
    }
    out_token[32] = '\0';
}

// ==========================================================================
// Initialization & Teardown
// ==========================================================================

int server_init(ServerState *s, const char *game_dir, const ScCatalog *catalog, const WlZlib *zlib, int port) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[server] WSAStartup failed.\n");
        return -1;
    }

    s->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s->listen_fd == INVALID_SOCKET) {
        fprintf(stderr, "[server] Failed to create socket.\n");
        WSACleanup();
        return -1;
    }

    int opt = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 strictly
    sin.sin_port        = htons((unsigned short)port);

    if (bind(s->listen_fd, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        fprintf(stderr, "[server] Failed to bind to 127.0.0.1:%d\n", port);
        closesocket(s->listen_fd);
        WSACleanup();
        return -1;
    }

    if (listen(s->listen_fd, SOMAXCONN) != 0) {
        fprintf(stderr, "[server] listen() failed.\n");
        closesocket(s->listen_fd);
        WSACleanup();
        return -1;
    }

    // Determine actual bound port
    struct sockaddr_in bound_addr;
    int bound_len = sizeof(bound_addr);
    if (getsockname(s->listen_fd, (struct sockaddr *)&bound_addr, &bound_len) == 0) {
        s->port = ntohs(bound_addr.sin_port);
    } else {
        s->port = port;
    }

    generate_token(s->token);

    snprintf(s->game_dir, sizeof(s->game_dir), "%s", game_dir ? game_dir : "");
    snprintf(s->mods_dir, sizeof(s->mods_dir), "%s/wololoader/mods", s->game_dir);
    s->catalog = catalog;
    s->zlib    = zlib;
    s->running = 1;

    return 0;
}

void server_stop(ServerState *s) {
    if (s) {
        s->running        = 0;
        s->exit_requested = 1;
    }
}

void server_cleanup(ServerState *s) {
    if (s && s->listen_fd != INVALID_SOCKET) {
        closesocket(s->listen_fd);
        s->listen_fd = INVALID_SOCKET;
        WSACleanup();
    }
}

// ==========================================================================
// Static UI File Delivery
// ==========================================================================

static int server_serve_static(const char *path, HttpResponse *res) {
    const char *target = path;
    if (strcmp(target, "/") == 0) {
        target = "/index.html";
    }

    for (size_t i = 0; i < g_ui_file_count; i++) {
        if (strcmp(g_ui_files[i].path, target) == 0) {
            http_response_set_binary(res, 200, g_ui_files[i].content_type, g_ui_files[i].data, g_ui_files[i].len);
            return 1;
        }
    }
    return 0; // Not found
}

// ==========================================================================
// Icon library (GET /api/icon-library, GET /api/icon-library/<category>/<id>.png)
// Embedded from scriptorium/icon_library (build/icons_files.h); the token was already checked.
// ==========================================================================

static int server_serve_icon_library(const char *path, HttpResponse *res) {
    const char *rest = path + strlen("/api/icon-library");
    if (*rest == '\0' || strcmp(rest, "/") == 0) rest = "/catalog.json";
    // Only plain "/<category>/<file>" names from the embedded table can match; anything else is 404.
    for (size_t i = 0; i < g_icons_file_count; i++) {
        if (strcmp(g_icons_files[i].path, rest) == 0) {
            http_response_set_binary(res, 200, g_icons_files[i].content_type, g_icons_files[i].data, g_icons_files[i].len);
            return 1;
        }
    }
    return 0;
}

// ==========================================================================
// Game Asset Delivery (GET /asset/<path>)
// Serves files ONLY from <game>/resources/_common/textures/ui/ and <game>/resources/_common/fonts/
// No token needed for GET of assets, but Host header has already been validated.
// Rejects '..', backslashes, colons, absolute paths.
// Content-types supported: png, jpg/jpeg, ttf.
// ==========================================================================

static void server_serve_asset(ServerState *s, const HttpRequest *req, HttpResponse *res) {
    if (strcmp(req->method, "GET") != 0 && strcmp(req->method, "HEAD") != 0) {
        http_response_error(res, 405, "Method Not Allowed");
        return;
    }

    if (strncmp(req->path, "/asset/", 7) != 0) {
        http_response_error(res, 404, "Not Found");
        return;
    }

    const char *subpath = req->path + 7;

    // Security validation: reject traversal, backslashes, colons, leading slash
    if (subpath[0] == '\0' ||
        subpath[0] == '/' ||
        strstr(subpath, "..") != NULL ||
        strchr(subpath, '\\') != NULL ||
        strchr(subpath, ':') != NULL) {
        http_response_error(res, 400, "Bad Request: Traversal or invalid characters detected");
        return;
    }

    // Whitelist check: only textures/ui/ or fonts/
    int is_textures = (strncmp(subpath, "textures/ui/", 12) == 0);
    int is_fonts    = (strncmp(subpath, "fonts/", 6) == 0);
    if (!is_textures && !is_fonts) {
        http_response_error(res, 403, "Forbidden: Path not in whitelisted roots");
        return;
    }

    // Content-type whitelist: png, jpg/jpeg, ttf
    const char *dot = strrchr(subpath, '.');
    const char *content_type = NULL;
    if (dot) {
        if (_stricmp(dot, ".png") == 0) {
            content_type = "image/png";
        } else if (_stricmp(dot, ".jpg") == 0 || _stricmp(dot, ".jpeg") == 0) {
            content_type = "image/jpeg";
        } else if (_stricmp(dot, ".ttf") == 0) {
            content_type = "font/ttf";
        }
    }

    if (!content_type) {
        http_response_error(res, 403, "Forbidden: Unsupported asset type");
        return;
    }

    // Construct full path: <game_dir>/resources/_common/<subpath>
    char full_path[MAX_PATH * 2];
    snprintf(full_path, sizeof(full_path), "%s/resources/_common/%s", s->game_dir, subpath);

    FILE *f = fopen(full_path, "rb");
    if (!f) {
        http_response_error(res, 404, "Asset not found");
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0 || sz > 25 * 1024 * 1024) { // 25 MB max
        fclose(f);
        http_response_error(res, 500, "Asset file invalid or too large");
        return;
    }

    char *data = malloc((size_t)sz + 1);
    if (!data) {
        fclose(f);
        http_response_error(res, 500, "Out of memory");
        return;
    }

    if (sz > 0) {
        size_t read_bytes = fread(data, 1, (size_t)sz, f);
        (void)read_bytes;
    }
    data[sz] = '\0';
    fclose(f);

    http_response_free(res);
    res->status_code     = 200;
    res->status_text     = "OK";
    res->content_type    = content_type;
    res->body            = data;
    res->body_len        = (size_t)sz;
    res->is_dynamic_body = 1;
}

// ==========================================================================
// HTTP Request Dispatcher
// ==========================================================================

static void server_handle_client(ServerState *s, SOCKET client_fd) {
    // Set 5-second socket timeout to prevent hung connections
    DWORD timeout_ms = 5000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));

    // Read headers into dynamic buffer
    size_t buf_cap = 8192;
    size_t buf_len = 0;
    char *buf = malloc(buf_cap);
    if (!buf) {
        closesocket(client_fd);
        return;
    }

    char *hdr_end = NULL;
    while (!hdr_end && buf_len + 1 < 65536) {
        int n = recv(client_fd, buf + buf_len, (int)(buf_cap - buf_len - 1), 0);
        if (n <= 0) break;
        buf_len += (size_t)n;
        buf[buf_len] = '\0';
        hdr_end = strstr(buf, "\r\n\r\n");
        if (!hdr_end && buf_len + 1024 >= buf_cap) {
            buf_cap *= 2;
            char *nbuf = realloc(buf, buf_cap);
            if (!nbuf) break;
            buf = nbuf;
        }
    }

    if (!hdr_end) {
        free(buf);
        closesocket(client_fd);
        return;
    }

    size_t header_len = (size_t)(hdr_end + 4 - buf);
    size_t body_bytes_in_buf = buf_len - header_len;

    // Parse Request Line: METHOD PATH HTTP/1.1
    HttpRequest req;
    memset(&req, 0, sizeof(req));

    char *line_end = strstr(buf, "\r\n");
    if (!line_end) {
        free(buf);
        closesocket(client_fd);
        return;
    }
    *line_end = '\0';

    char *p_method = buf;
    char *p_space1 = strchr(p_method, ' ');
    if (!p_space1) { free(buf); closesocket(client_fd); return; }
    *p_space1 = '\0';
    snprintf(req.method, sizeof(req.method), "%s", p_method);

    char *p_uri = p_space1 + 1;
    char *p_space2 = strchr(p_uri, ' ');
    if (p_space2) *p_space2 = '\0';

    char *p_qmark = strchr(p_uri, '?');
    if (p_qmark) {
        *p_qmark = '\0';
        snprintf(req.path, sizeof(req.path), "%s", p_uri);
        snprintf(req.query, sizeof(req.query), "%s", p_qmark + 1);
    } else {
        snprintf(req.path, sizeof(req.path), "%s", p_uri);
    }

    // Parse Headers
    char *hstart = line_end + 2;
    while (hstart < hdr_end) {
        char *hend = strstr(hstart, "\r\n");
        if (!hend) break;
        *hend = '\0';

        char *colon = strchr(hstart, ':');
        if (colon) {
            *colon = '\0';
            char *hname = hstart;
            char *hval  = colon + 1;
            while (*hval == ' ' || *hval == '\t') hval++;

            if (_stricmp(hname, "Host") == 0) {
                snprintf(req.host, sizeof(req.host), "%s", hval);
            } else if (_stricmp(hname, "X-Scriptorium-Token") == 0) {
                snprintf(req.token, sizeof(req.token), "%s", hval);
            } else if (_stricmp(hname, "Content-Length") == 0) {
                req.content_length = (size_t)strtoull(hval, NULL, 10);
            } else if (_stricmp(hname, "Content-Type") == 0) {
                snprintf(req.content_type, sizeof(req.content_type), "%s", hval);
            }
        }
        hstart = hend + 2;
    }

    HttpResponse res;
    http_response_init(&res);

    // 1. Host Validation (API.md: "Reject requests whose Host is not 127.0.0.1:PORT (DNS rebinding)")
    char expected_host[64];
    snprintf(expected_host, sizeof(expected_host), "127.0.0.1:%d", s->port);
    if (_stricmp(req.host, expected_host) != 0 &&
        !(s->port == 80 && _stricmp(req.host, "127.0.0.1") == 0)) {
        http_response_error(&res, 400, "Invalid Host header");
        goto send_and_close;
    }

    // 2. Read Body if Content-Length > 0
    if (req.content_length > 0) {
        // Enforce max upload limit of 25MB
        if (req.content_length > 25 * 1024 * 1024) {
            http_response_error(&res, 413, "Payload Too Large");
            goto send_and_close;
        }

        req.body = malloc(req.content_length + 1);
        if (!req.body) {
            http_response_error(&res, 500, "Out of memory");
            goto send_and_close;
        }

        // Copy already-buffered body bytes
        size_t copied = (body_bytes_in_buf < req.content_length) ? body_bytes_in_buf : req.content_length;
        if (copied > 0) {
            memcpy(req.body, hdr_end + 4, copied);
        }

        // Read remaining body bytes from socket
        while (copied < req.content_length) {
            int n = recv(client_fd, req.body + copied, (int)(req.content_length - copied), 0);
            if (n <= 0) break;
            copied += (size_t)n;
        }
        req.body[copied] = '\0';
    }

    // 3. CORS Pre-flight Options Handling
    if (strcmp(req.method, "OPTIONS") == 0) {
        res.status_code = 204;
        res.status_text = "No Content";
        res.body        = NULL;
        res.body_len    = 0;
        goto send_and_close;
    }

    // 4. API Requests vs Static Files
    if (strncmp(req.path, "/api/", 5) == 0) {
        // Token Validation: API.md requires X-Scriptorium-Token on every /api/* call
        if (strcmp(req.token, s->token) != 0) {
            http_response_error(&res, 403, "Forbidden: invalid or missing token");
            goto send_and_close;
        }

        // Dispatch to API implementation
        if (strcmp(req.method, "GET") == 0 &&
            (strcmp(req.path, "/api/icon-library") == 0 || strncmp(req.path, "/api/icon-library/", 18) == 0)) {
            if (!server_serve_icon_library(req.path, &res)) http_response_error(&res, 404, "Icon not found");
        } else {
            api_handle_request(s, &req, &res);
        }
    } else if (strncmp(req.path, "/asset/", 7) == 0) {
        // Asset delivery from game resources (no token required; Host check already verified)
        server_serve_asset(s, &req, &res);
    } else {
        // Static files (no token check required per API.md)
        if (!server_serve_static(req.path, &res)) {
            http_response_error(&res, 404, "Not Found");
        }
    }

send_and_close: {
    // Format and send HTTP response
    char resp_header[1024];
    int hlen = snprintf(resp_header, sizeof(resp_header),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Access-Control-Allow-Headers: Content-Type, X-Scriptorium-Token\r\n"
                        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                        "\r\n",
                        res.status_code, res.status_text, res.content_type, res.body_len);

    send(client_fd, resp_header, hlen, 0);
    if (res.body && res.body_len > 0) {
        send(client_fd, res.body, (int)res.body_len, 0);
    }
}

    if (req.body) free(req.body);
    http_response_free(&res);
    free(buf);
    closesocket(client_fd);
}

// ==========================================================================
// Main Server Event Loop
// ==========================================================================

int server_run(ServerState *s) {
    if (!s || s->listen_fd == INVALID_SOCKET) return -1;

    s->running = 1;
    if (s->ready_event) {
        SetEvent(s->ready_event);
    }

    while (s->running && !s->exit_requested && !launcher_should_exit()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s->listen_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 100000; // 100 ms timeout to allow checking lifetime flags

        int sel = select(0, &read_fds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(s->listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            int client_len = sizeof(client_addr);
            SOCKET client_fd = accept(s->listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd != INVALID_SOCKET) {
                server_handle_client(s, client_fd);
            }
        }
    }

    return 0;
}
