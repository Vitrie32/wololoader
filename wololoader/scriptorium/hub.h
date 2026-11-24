// ==========================================================================
// Scriptorium v2 - Mod Hub Backend
//
// Purpose:
//   Implements Mod Hub index fetching (10-minute cache, WinHTTP HTTPS/HTTP +
//   file:// / local paths), safe ZIP verification & extraction (SHA-256,
//   CRC-32, path traversal / symlink / zip-bomb rejection), dependency-first
//   installation, enabled-state preservation on update, .hub.json tracking,
//   and trash-based uninstallation.
// ==========================================================================

#ifndef SC_HUB_H
#define SC_HUB_H

#include "server.h"

void hub_handle_index(ServerState *s, const HttpRequest *req, HttpResponse *res);
void hub_handle_install(ServerState *s, const HttpRequest *req, HttpResponse *res);
void hub_handle_uninstall(ServerState *s, const HttpRequest *req, HttpResponse *res);
void hub_handle_icon(ServerState *s, const HttpRequest *req, HttpResponse *res);

#endif // SC_HUB_H
