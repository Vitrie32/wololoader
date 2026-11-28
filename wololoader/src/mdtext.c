// Markdown -> display lines (see mdtext.h). Used by changelog.c for CHANGELOG.md
// (shipped as wololoader\changelog.txt) and the GitHub release notes.
#include "mdtext.h"
#include <stdlib.h>
#include <string.h>

void md_add_line(MdLines *out, const char *text) {
    if (out->count == out->cap) {
        int cap = out->cap ? out->cap * 2 : 64;
        char **n = realloc(out->line, (size_t)cap * sizeof *n);
        if (!n) return;
        out->line = n;
        out->cap = cap;
    }
    size_t len = strlen(text);
    char *s = malloc(len + 1);
    if (!s) return;
    memcpy(s, text, len + 1);
    out->line[out->count++] = s;
}

void md_free(MdLines *l) {
    for (int i = 0; i < l->count; i++) free(l->line[i]);
    free(l->line);
    l->line = NULL;
    l->count = l->cap = 0;
}

static int last_blank(const MdLines *out) {
    return out->count == 0 || out->line[out->count - 1][0] == 0;
}

static void add_blank(MdLines *out) {
    if (!last_blank(out)) md_add_line(out, "");
}

// Length of a valid UTF-8 sequence at p, 0 if invalid.
static int utf8_len(const unsigned char *p) {
    if (p[0] < 0x80) return 1;
    if ((p[0] & 0xe0) == 0xc0 && p[0] >= 0xc2 && (p[1] & 0xc0) == 0x80) return 2;
    if ((p[0] & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) return 3;
    if ((p[0] & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) return 4;
    return 0;
}

// Inline markup removed, characters the game cannot show replaced. out has room for n + 1.
static void clean_inline(const char *in, size_t n, char *out) {
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in, *end = p + n;
    while (p < end) {
        if ((p[0] == '*' || p[0] == '_') && p + 1 < end && p[1] == p[0]) { p += 2; continue; }
        if (p[0] == '`') { p++; continue; }
        if (p[0] == '[') {                       // [text](url) -> text
            const unsigned char *close = memchr(p, ']', (size_t)(end - p));
            if (close && close + 1 < end && close[1] == '(') {
                const unsigned char *rp = memchr(close, ')', (size_t)(end - close));
                if (rp) {
                    char sub[1024];
                    size_t len = (size_t)(close - p - 1);
                    if (len >= sizeof sub) len = sizeof sub - 1;
                    clean_inline((const char *)p + 1, len, sub);
                    size_t sl = strlen(sub);
                    memcpy(out + o, sub, sl);
                    o += sl;
                    p = rp + 1;
                    continue;
                }
            }
        }
        if (p[0] == '<') { out[o++] = '('; p++; continue; }
        if (p[0] == '>') {                       // "->", "=>": drop it; else ')'
            if (!(o > 0 && (out[o - 1] == '-' || out[o - 1] == '='))) out[o++] = ')';
            p++;
            continue;
        }
        if (p[0] == '&') { out[o++] = '+'; p++; continue; }
        if (p[0] < 0x20) { out[o++] = ' '; p++; continue; }
        int l = utf8_len(p);
        if (l == 0 || p + l > end) { out[o++] = '?'; p++; continue; }
        memcpy(out + o, p, (size_t)l);
        o += (size_t)l;
        p += l;
    }
    out[o] = 0;
}

static int chars(const char *s, size_t n) {   // code points in s[0..n)
    int c = 0;
    for (size_t i = 0; i < n; i++) if (((unsigned char)s[i] & 0xc0) != 0x80) c++;
    return c;
}

// Word-wraps `text` into lines: the first starts with `first`, the others with `cont`.
static void wrap(MdLines *out, const char *text, int width, const char *first, const char *cont, int bold) {
    const char *prefix = first;
    const char *p = text;
    char line[2048];
    while (*p == ' ') p++;
    if (!*p) return;
    while (*p) {
        int avail = width - chars(prefix, strlen(prefix));
        if (avail < 10) avail = 10;
        size_t o = 0;
        int col = 0;
        const char *line_start = p;
        while (*p) {
            const char *w = p;
            while (*w == ' ') w++;
            const char *e = w;
            while (*e && *e != ' ') e++;
            int wl = chars(w, (size_t)(e - w));
            if (col > 0 && col + 1 + wl > avail) break;
            if (col > 0) { line[o++] = ' '; col++; }
            size_t bytes = (size_t)(e - w);
            if (o + bytes + 1 >= sizeof line - 16) bytes = sizeof line - 17 - o;
            memcpy(line + o, w, bytes);
            o += bytes;
            col += wl;
            p = e;
            while (*p == ' ') p++;
            if (col >= avail) break;           // one word longer than the line
        }
        if (p == line_start) break;            // nothing fits (cannot happen with avail >= 10)
        line[o] = 0;
        char full[2200];
        if (bold) {
            size_t pl = strlen(prefix);
            memcpy(full, prefix, pl);
            memcpy(full + pl, "<b>", 3);           // toggle tag: on ... off (see mdtext.h)
            memcpy(full + pl + 3, line, o);
            memcpy(full + pl + 3 + o, "<b>", 4);
        } else {
            size_t pl = strlen(prefix);
            memcpy(full, prefix, pl);
            memcpy(full + pl, line, o + 1);
        }
        md_add_line(out, full);
        prefix = cont;
    }
}

void md_to_lines(MdLines *out, const char *md, int width, int skip_h1) {
    if (!md) return;
    char cont[64] = "";        // indent of the current list item's wrapped lines
    int in_item = 0;
    const char *p = md;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        while (n && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        int indent = 0;
        while ((size_t)indent < n && (p[indent] == ' ' || p[indent] == '\t')) indent++;
        const char *s = p + indent;
        size_t sn = n - (size_t)indent;
        char *clean = malloc(sn * 3 + 1);      // '<' -> '(' etc. never grow; 3x is slack
        if (!clean) return;

        if (sn == 0) {
            add_blank(out);
            in_item = 0;
        } else if (s[0] == '#') {
            int level = 0;
            while ((size_t)level < sn && s[level] == '#') level++;
            size_t k = (size_t)level;
            while (k < sn && s[k] == ' ') k++;
            if (!(level == 1 && skip_h1)) {
                clean_inline(s + k, sn - k, clean);
                if (out->count) add_blank(out);
                wrap(out, clean, width, "", "", 1);
            }
            in_item = 0;
        } else if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && sn > 1 && s[1] == ' ') {
            int depth = indent / 2;
            if (depth > 4) depth = 4;
            char first[64];
            int pad = 2 + 4 * depth, q;
            for (q = 0; q < pad; q++) first[q] = ' ';
            first[q++] = '-';
            first[q++] = ' ';
            first[q] = 0;
            for (q = 0; q < pad + 2; q++) cont[q] = ' ';
            cont[q] = 0;
            clean_inline(s + 2, sn - 2, clean);
            wrap(out, clean, width, first, cont, 0);
            in_item = 1;
        } else if (s[0] == '-' && sn >= 3 && strspn(s, "-") == sn) {
            add_blank(out);                    // horizontal rule
            in_item = 0;
        } else {
            clean_inline(s, sn, clean);
            if (in_item && indent > 0) wrap(out, clean, width, cont, cont, 0);   // list item continued
            else { wrap(out, clean, width, "", "", 0); in_item = 0; }
        }
        free(clean);
        if (!eol) break;
        p = eol + 1;
    }
    while (out->count && out->line[out->count - 1][0] == 0) {   // no trailing blank lines
        free(out->line[--out->count]);
    }
}
