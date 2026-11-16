// ==========================================================================
// Scriptorium v2 - HTTP Server Header
//
// Purpose:
//   Defines the local Winsock HTTP/1.1 server, request/response models,
//   and core lifecycle routines.
// ==========================================================================

#ifndef SC_SERVER_H
#define SC_SERVER_H

#include <winsock2.h>
#include <windows.h>
#include "catalog.h"
#include "sc_zlib.h"

// HTTP Request
typedef struct {
    char   method[16];
    char   path[512];
    char   query[512];
    char   host[256];
    char   token[128];
    char   content_type[128];
    size_t content_length;
    char  *body;
} HttpRequest;

// HTTP Response
typedef struct {
    int         status_code;
    const char *status_text;
    const char *content_type;
    char       *body;
    size_t      body_len;
    int         is_dynamic_body;
} HttpResponse;

typedef struct ServerState {
    SOCKET           listen_fd;
    int              port;
    char             token[33];
    int              running;
    int              exit_requested;
    int              ready;
    HANDLE           ready_event;

    char             game_dir[MAX_PATH];
    char             mods_dir[MAX_PATH];
    const ScCatalog *catalog;
    const WlZlib    *zlib;
} ServerState;

void http_response_init(HttpResponse *res);
void http_response_free(HttpResponse *res);
void http_response_set_json(HttpResponse *res, int status, const char *json_str);
void http_response_set_binary(HttpResponse *res, int status, const char *content_type, const void *data, size_t len);
void http_response_error(HttpResponse *res, int status, const char *msg);

// Initializes the HTTP server on 127.0.0.1 (binds port, generates 32-hex token).
// If port is 0, an ephemeral port is selected and stored in s->port.
int server_init(ServerState *s, const char *game_dir, const ScCatalog *catalog, const WlZlib *zlib, int port);

// Runs the server event loop. Blocks until shutdown.
int server_run(ServerState *s);

// Stops the server event loop.
void server_stop(ServerState *s);

// Cleans up Winsock resources.
void server_cleanup(ServerState *s);

#endif // SC_SERVER_H
