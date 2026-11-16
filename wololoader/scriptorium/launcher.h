// ==========================================================================
// Scriptorium v2 - Application Window Launcher & Lifetime Manager
//
// Purpose:
//   Manages launching the frontend HTML application in app-mode browser window
//   on Windows (Edge / Chrome) and Linux under Wine (native Linux browsers
//   invoked via 'start /unix', or winebrowser fallback). Tracks ping heartbeat
//   and controls server lifetime.
// ==========================================================================

#ifndef SC_LAUNCHER_H
#define SC_LAUNCHER_H

#include <stddef.h>

// Checks whether Scriptorium is running under Wine/Proton.
int launcher_is_wine(void);

// Launches the app-mode browser window pointing to url.
void launcher_open_browser(const char *url);

// Opens a local file or folder path in system shell (winebrowser under Wine).
void launcher_open_folder(const char *path);

// Heartbeat handling: marks that a ping was received from the frontend page.
void launcher_notify_ping(void);

// Heartbeat handling: requests graceful shutdown (e.g. from /api/quit).
void launcher_request_quit(void);

// Returns 1 if server should terminate (heartbeat expired or quit requested).
int launcher_should_exit(void);

#endif // SC_LAUNCHER_H
