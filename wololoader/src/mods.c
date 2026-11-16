// Mod discovery: every folder in <game>\wololoader\mods with a mod.json.
// A mod is disabled when mod.json contains "enabled": false.
#include "wololoader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 1 << 20) { fclose(f); return NULL; }
    char *buf = malloc(n + 1);
    if (buf) { buf[fread(buf, 1, n, f)] = 0; }
    fclose(f);
    return buf;
}

// Minimal lookup of a top-level "key": "string" or "key": true/false.
// Enough for mod.json metadata; the real data patches are handled elsewhere.
static BOOL json_get(const char *json, const char *key, char *out, size_t cap) {
    char pat[80];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return FALSE;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;
    size_t i = 0;
    if (*p == '"') {
        for (p++; *p && *p != '"' && i + 1 < cap; p++) {
            if (*p == '\\' && p[1]) p++;
            out[i++] = *p;
        }
    } else {
        while (*p && strchr(",}\r\n \t", *p) == NULL && i + 1 < cap) out[i++] = *p++;
    }
    out[i] = 0;
    return TRUE;
}

BOOL g_wl_vanilla;

static void vanilla_flag_path(char *out) {
    snprintf(out, MAX_PATH, "%s\\vanilla.flag", g_wl.root);
}

BOOL wl_vanilla_flag_exists(void) {
    char p[MAX_PATH];
    vanilla_flag_path(p);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

BOOL wl_vanilla_set(BOOL on) {
    char p[MAX_PATH];
    vanilla_flag_path(p);
    if (!on) return DeleteFileA(p) || GetLastError() == ERROR_FILE_NOT_FOUND;
    FILE *f = fopen(p, "wb");
    if (!f) return FALSE;
    fputs("Wololoader Vanilla Mode: mods and patches are not loaded while this file exists.\r\n", f);
    fclose(f);
    return TRUE;
}

void wl_scan_mods(void) {
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\mods\\*", g_wl.root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    g_wl.mod_count = 0;
    g_wl.listed_count = 0;
    if (h == INVALID_HANDLE_VALUE) { wl_log("No mods folder."); return; }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        if (g_wl.listed_count >= WL_MAX_MODS) { wl_log("Mod limit %d reached.", WL_MAX_MODS); break; }

        WlMod *m = &g_wl.mods[g_wl.listed_count];
        memset(m, 0, sizeof *m);
        snprintf(m->dir, MAX_PATH, "%s\\mods\\%s", g_wl.root, fd.cFileName);
        snprintf(m->id, sizeof m->id, "%s", fd.cFileName);

        char manifest[MAX_PATH];
        snprintf(manifest, MAX_PATH, "%s\\mod.json", m->dir);
        char *json = read_file(manifest);
        if (!json) { wl_log("  skip '%s': no mod.json", m->id); continue; }

        char flag[16] = "";
        m->enabled = !(json_get(json, "enabled", flag, sizeof flag) && strcmp(flag, "false") == 0);
        if (!json_get(json, "name", m->name, sizeof m->name)) lstrcpynA(m->name, m->id, sizeof m->name);
        json_get(json, "version", m->version, sizeof m->version);
        json_get(json, "author", m->author, sizeof m->author);
        json_get(json, "description", m->description, sizeof m->description);
        free(json);

        char civs[MAX_PATH];
        WIN32_FIND_DATAA cf;
        snprintf(civs, MAX_PATH, "%s\\civs\\*.json", m->dir);
        HANDLE ch = FindFirstFileA(civs, &cf);
        if (ch != INVALID_HANDLE_VALUE) {
            do m->civ_count++; while (FindNextFileA(ch, &cf));
            FindClose(ch);
        }

        char icon_path[MAX_PATH];
        snprintf(icon_path, MAX_PATH, "%s\\icon.png", m->dir);
        DWORD attr = GetFileAttributesA(icon_path);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            lstrcpynA(m->icon, icon_path, sizeof m->icon);
        } else {
            m->icon[0] = 0;
        }

        wl_log("  %s '%s' v%s (%s)", m->enabled ? "load" : "off ", m->name,
               m->version[0] ? m->version : "?", m->id);
        if (m->enabled) g_wl.mod_count++;
        g_wl.listed_count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    // "Wololoader Base" (built-in features, basemod.c) is always listed first.
    // It has no civs or resources, so its position does not change any build order.
    for (int i = 1; i < g_wl.listed_count; i++) {
        if (_stricmp(g_wl.mods[i].id, "WololoaderBase") != 0) continue;
        WlMod base = g_wl.mods[i];
        memmove(&g_wl.mods[1], &g_wl.mods[0], (size_t)i * sizeof g_wl.mods[0]);
        g_wl.mods[0] = base;
        break;
    }
    wl_log("%d mod(s) loaded.", g_wl.mod_count);
}

BOOL wl_set_mod_enabled(const char *mod_dir, BOOL enabled) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\mod.json", mod_dir);

    FILE *f = fopen(path, "rb");
    if (!f) return FALSE;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) { fclose(f); return FALSE; }

    char *buf = malloc(sz + 256);
    if (!buf) { fclose(f); return FALSE; }

    size_t n = fread(buf, 1, sz, f);
    buf[n] = '\0';
    fclose(f);

    const char *key = "\"enabled\"";
    char *p = strstr(buf, key);

    char *new_content = NULL;
    size_t new_len = 0;

    if (p) {
        char *colon = strchr(p + strlen(key), ':');
        if (!colon) { free(buf); return FALSE; }

        char *val_start = colon + 1;
        while (*val_start == ' ' || *val_start == '\t' || *val_start == '\r' || *val_start == '\n')
            val_start++;

        char *val_end = val_start;
        while (*val_end && *val_end != ',' && *val_end != '}' && *val_end != ' ' && *val_end != '\t' && *val_end != '\r' && *val_end != '\n')
            val_end++;

        const char *rep = enabled ? "true" : "false";
        new_len = (val_start - buf) + strlen(rep) + strlen(val_end);
        new_content = malloc(new_len + 1);
        if (!new_content) { free(buf); return FALSE; }

        size_t prefix_len = val_start - buf;
        memcpy(new_content, buf, prefix_len);
        strcpy(new_content + prefix_len, rep);
        strcat(new_content, val_end);
    } else {
        char *brace = strchr(buf, '{');
        if (!brace) { free(buf); return FALSE; }

        const char *rep = enabled ? "\n  \"enabled\": true," : "\n  \"enabled\": false,";
        size_t prefix_len = (brace - buf) + 1;
        new_len = strlen(buf) + strlen(rep);
        new_content = malloc(new_len + 1);
        if (!new_content) { free(buf); return FALSE; }

        memcpy(new_content, buf, prefix_len);
        strcpy(new_content + prefix_len, rep);
        strcat(new_content, brace + 1);
    }

    free(buf);

    f = fopen(path, "wb");
    if (!f) { free(new_content); return FALSE; }
    fwrite(new_content, 1, strlen(new_content), f);
    fclose(f);
    free(new_content);
    return TRUE;
}
