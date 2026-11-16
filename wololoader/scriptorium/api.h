// ==========================================================================
// Scriptorium v2 - API Router & Endpoint Handlers
//
// Purpose:
//   Dispatches all /api/* HTTP requests to their respective implementations
//   per API.md.
// ==========================================================================

#ifndef SC_API_H
#define SC_API_H

#include "server.h"

// Handles all /api/* requests.
void api_handle_request(ServerState *s, const HttpRequest *req, HttpResponse *res);

#endif // SC_API_H
