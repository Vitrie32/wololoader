// Mod settings (see modsettings.h). Plain file + JSON code only, so script.c's
// native test can link it too.
#include "modsettings.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32                       // native test (tools/test_script.c)
#include <stdio.h>
#include <sys/stat.h>
#define CreateDirectoryA(d, x) mkdir((d), 0755)
#define MoveFileExA(a, b, f) (rename((a), (b)) == 0)
#define DeleteFileA(p) remove(p)
#endif

void wl_settings_path(const WlMod *m, char *out, size_t cap) {
    snprintf(out, cap, "%s\\settings\\%s.json", g_wl.root, m->id);
}

static WlJsonVal *read_json(const char *path) {
    size_t len = 0;
    char *text = wl_read_file(path, &len);
    if (!text) return NULL;
    char err[128];
    WlJsonVal *v = wl_json_parse(text, err, sizeof err);
    free(text);
    return v;
}

static int is_type(const WlJsonVal *v, WlJsonType t) { return v && v->type == t; }

static int find_choice(const WlSetting *s, const char *name) {
    for (int i = 0; i < s->nchoices; i++)
        if (strcmp(s->choices[i], name) == 0) return i;
    return -1;
}

static double clamp_num(const WlSetting *s, double v) {
    if (v < s->min) v = s->min;
    if (v > s->max) v = s->max;
    return v;
}

// Applies a JSON value (from the default or the player's file) to s.
static void apply_value(WlSetting *s, const WlJsonVal *v, int is_default) {
    if (!v) return;
    if (s->type == WL_SET_BOOL && is_type(v, WL_JSON_BOOL)) {
        s->on = v->u.bool_val ? 1 : 0;
        if (is_default) s->def_on = s->on;
    } else if (s->type == WL_SET_NUMBER && is_type(v, WL_JSON_NUMBER)) {
        s->num = clamp_num(s, v->u.num.dval);
        if (is_default) s->def_num = s->num;
    } else if (s->type == WL_SET_CHOICE && is_type(v, WL_JSON_STRING)) {
        int c = find_choice(s, v->u.str);
        if (c >= 0) { s->choice = c; if (is_default) s->def_choice = c; }
    }
}

// Multiplayer session values: the lobby host's settings (mpcheck.c), used instead of the player's
// own file until the player is back at the main menu. The player's files are never changed.
#define SESSION_MAX 32
static struct { char id[64]; char *json; } g_session[SESSION_MAX];
static int g_session_n;

void wl_settings_session_set(const char *mod_id, const char *json) {
    int i;
    for (i = 0; i < g_session_n; i++) if (strcmp(g_session[i].id, mod_id) == 0) break;
    if (i == g_session_n) {
        if (g_session_n >= SESSION_MAX) return;
        snprintf(g_session[i].id, sizeof g_session[i].id, "%s", mod_id);
        g_session[i].json = NULL;
        g_session_n++;
    }
    free(g_session[i].json);
    size_t n = strlen(json);
    g_session[i].json = (char *)malloc(n + 1);
    if (g_session[i].json) memcpy(g_session[i].json, json, n + 1);
}

int wl_settings_session_clear(void) {
    int had = g_session_n;
    for (int i = 0; i < g_session_n; i++) free(g_session[i].json);
    g_session_n = 0;
    return had;
}

static const char *session_json(const char *mod_id) {
    for (int i = 0; i < g_session_n; i++)
        if (strcmp(g_session[i].id, mod_id) == 0) return g_session[i].json;
    return NULL;
}

int wl_settings_load(const WlMod *m, WlSetting *out, int max) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\mod.json", m->dir);
    WlJsonVal *root = read_json(path);
    if (!root) return 0;
    const WlJsonVal *arr = wl_json_get(root, "settings");
    int n = 0;
    size_t count = is_type(arr, WL_JSON_ARRAY) ? wl_json_len(arr) : 0;
    for (size_t i = 0; i < count && n < max; i++) {
        const WlJsonVal *d = wl_json_at(arr, i);
        const char *id = wl_json_str(wl_json_get(d, "id"), "");
        if (!id[0] || strlen(id) >= sizeof out[0].id) continue;
        WlSetting *s = &out[n];
        memset(s, 0, sizeof *s);
        snprintf(s->id, sizeof s->id, "%s", id);
        snprintf(s->name, sizeof s->name, "%s", wl_json_str(wl_json_get(d, "name"), id));
        const char *type = wl_json_str(wl_json_get(d, "type"), "bool");
        if (strcmp(type, "number") == 0) {
            s->type = WL_SET_NUMBER;
            s->min = wl_json_num(wl_json_get(d, "min"), 0);
            s->max = wl_json_num(wl_json_get(d, "max"), 100);
            s->step = wl_json_num(wl_json_get(d, "step"), 1);
            if (s->max < s->min) s->max = s->min;
            if (s->step <= 0) s->step = 1;
            s->num = s->def_num = s->min;
        } else if (strcmp(type, "choice") == 0) {
            s->type = WL_SET_CHOICE;
            const WlJsonVal *ch = wl_json_get(d, "choices");
            size_t nc = is_type(ch, WL_JSON_ARRAY) ? wl_json_len(ch) : 0;
            for (size_t k = 0; k < nc && s->nchoices < WL_MAX_CHOICES; k++) {
                const char *c = wl_json_str(wl_json_at(ch, k), NULL);
                if (c) snprintf(s->choices[s->nchoices++], sizeof s->choices[0], "%s", c);
            }
            if (s->nchoices == 0) continue;     // a choice without choices is skipped
        } else {
            s->type = WL_SET_BOOL;
        }
        apply_value(s, wl_json_get(d, "default"), 1);
        n++;
    }
    wl_json_free(root);

    const char *sj = session_json(m->id);
    WlJsonVal *user = NULL;
    if (sj) {
        char err[64];
        user = wl_json_parse(sj, err, sizeof err);   // the lobby host's values
    } else {
        wl_settings_path(m, path, sizeof path);
        user = read_json(path);
    }
    if (user) {
        for (int i = 0; i < n; i++) apply_value(&out[i], wl_json_get(user, out[i].id), 0);
        wl_json_free(user);
    }
    return n;
}

// The effective values as compact JSON ({"id":value,...}, settings order); "" if the mod has none.
int wl_settings_compact(const WlMod *m, char *out, size_t cap) {
    static WlSetting set[WL_MAX_SETTINGS];
    int n = wl_settings_load(m, set, WL_MAX_SETTINGS);
    size_t len = 0;
    out[0] = 0;
    if (n <= 0) return 0;
    len += snprintf(out + len, cap - len, "{");
    for (int i = 0; i < n && len + 8 < cap; i++) {
        len += snprintf(out + len, cap - len, "%s\"%s\":", i ? "," : "", set[i].id);
        if (len >= cap) break;
        if (set[i].type == WL_SET_BOOL) len += snprintf(out + len, cap - len, "%s", set[i].on ? "true" : "false");
        else if (set[i].type == WL_SET_NUMBER) len += snprintf(out + len, cap - len, "%.10g", set[i].num);
        else len += snprintf(out + len, cap - len, "\"%s\"", set[i].choices[set[i].choice]);
        if (len >= cap) break;
    }
    if (len + 2 > cap) { out[0] = 0; return 0; }
    len += snprintf(out + len, cap - len, "}");
    return (int)len;
}

static void put_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

int wl_settings_save(const WlMod *m, const WlSetting *s, int n) {
    char dir[MAX_PATH], path[MAX_PATH], tmp[MAX_PATH + 8];
    snprintf(dir, sizeof dir, "%s\\settings", g_wl.root);
    CreateDirectoryA(dir, NULL);
    wl_settings_path(m, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    fputs("{\n", f);
    for (int i = 0; i < n; i++) {
        fputs("  ", f);
        put_json_string(f, s[i].id);
        fputs(": ", f);
        if (s[i].type == WL_SET_BOOL) fputs(s[i].on ? "true" : "false", f);
        else if (s[i].type == WL_SET_NUMBER) fprintf(f, "%.10g", s[i].num);
        else put_json_string(f, s[i].choices[s[i].choice]);
        fputs(i + 1 < n ? ",\n" : "\n", f);
    }
    fputs("}\n", f);
    int ok = fclose(f) == 0;
    if (ok) ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
    if (!ok) DeleteFileA(tmp);
    return ok;
}

void wl_setting_text(const WlSetting *s, char *out, size_t cap) {
    if (s->type == WL_SET_BOOL) snprintf(out, cap, "%s", s->on ? "On" : "Off");
    else if (s->type == WL_SET_NUMBER) snprintf(out, cap, "%.10g", s->num);
    else snprintf(out, cap, "%s", s->choices[s->choice]);
}

void wl_setting_step(WlSetting *s, int dir) {
    if (s->type == WL_SET_BOOL) s->on = !s->on;
    else if (s->type == WL_SET_NUMBER) s->num = clamp_num(s, s->num + dir * s->step);
    else if (s->nchoices > 0) s->choice = (s->choice + (dir > 0 ? 1 : s->nchoices - 1)) % s->nchoices;
}
