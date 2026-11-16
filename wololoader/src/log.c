// Plain text log at <game>\wololoader\wololoader.log (truncated each run).
#include "wololoader.h"
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log;

void wl_log_open(void) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\wololoader.log", g_wl.root);
    g_log = fopen(path, "w");
}

void wl_log(const char *fmt, ...) {
    if (!g_log) return;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d] %s\n", t.wHour, t.wMinute, t.wSecond, line);
    fflush(g_log);
    wl_loadscreen_add_log(line);
}
