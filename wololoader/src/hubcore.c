// ==========================================================================
// Wololoader - Shared Mod Hub Core Implementation
//
// Compiled into Scriptorium.exe, version.dll, and native Linux unit tests.
// ==========================================================================

#define _POSIX_C_SOURCE 200809L
#include "hubcore.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void hub_logf(HubLogFn log_cb, void *log_user, const char *fmt, ...) {
    if (!log_cb) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_cb(buf, log_user);
}

// ==========================================================================
// Dynamic Buffer & Portable String Helpers
// ==========================================================================

void hub_buf_init(HubBuf *b) {
    b->cap  = 2048;
    b->len  = 0;
    b->data = malloc(b->cap);
    if (b->data) b->data[0] = '\0';
}

int hub_buf_append(HubBuf *b, const char *s, size_t n) {
    if (!b->data) return 0;
    if (b->len + n + 1 >= b->cap) {
        size_t ncap = b->cap * 2;
        if (ncap < b->len + n + 1024) ncap = b->len + n + 1024;
        char *ndata = realloc(b->data, ncap);
        if (!ndata) return 0;
        b->data = ndata;
        b->cap  = ncap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

void hub_buf_puts(HubBuf *b, const char *s) {
    if (s) hub_buf_append(b, s, strlen(s));
}

void hub_buf_puts_escaped(HubBuf *b, const char *s) {
    hub_buf_puts(b, "\"");
    if (!s) {
        hub_buf_puts(b, "\"");
        return;
    }
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '\"': hub_buf_puts(b, "\\\""); break;
            case '\\': hub_buf_puts(b, "\\\\"); break;
            case '\b': hub_buf_puts(b, "\\b");  break;
            case '\f': hub_buf_puts(b, "\\f");  break;
            case '\n': hub_buf_puts(b, "\\n");  break;
            case '\r': hub_buf_puts(b, "\\r");  break;
            case '\t': hub_buf_puts(b, "\\t");  break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    hub_buf_puts(b, esc);
                } else {
                    hub_buf_append(b, (const char *)p, 1);
                }
                break;
        }
    }
    hub_buf_puts(b, "\"");
}

int hub_stricmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int hub_strnicmp(const char *a, const char *b, size_t n) {
    if (!a) a = "";
    if (!b) b = "";
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

// ==========================================================================
// Validation & Version Comparison
// ==========================================================================

int hub_is_valid_id(const char *id) {
    if (!id || !id[0]) return 0;
    if (strlen(id) > 120) return 0;
    if (strcmp(id, ".") == 0 || strcmp(id, "..") == 0) return 0;
    if (hub_stricmp(id, "WololoaderBase") == 0) return 0;
    for (const char *p = id; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '_' || c == '-' || c == ' ')) return 0;
    }
    if (id[0] == ' ' || id[strlen(id) - 1] == ' ') return 0;
    return 1;
}

int hub_compare_versions(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;

    const char *pa = a;
    const char *pb = b;
    while ((*pa && *pa != '-') || (*pb && *pb != '-')) {
        long va = 0, vb = 0;
        if (*pa && *pa != '-') {
            if (isdigit((unsigned char)*pa)) {
                while (isdigit((unsigned char)*pa)) {
                    va = va * 10 + (*pa - '0');
                    pa++;
                }
            }
            while (*pa && *pa != '.' && *pa != '-') pa++;
            if (*pa == '.') pa++;
        }
        if (*pb && *pb != '-') {
            if (isdigit((unsigned char)*pb)) {
                while (isdigit((unsigned char)*pb)) {
                    vb = vb * 10 + (*pb - '0');
                    pb++;
                }
            }
            while (*pb && *pb != '.' && *pb != '-') pb++;
            if (*pb == '.') pb++;
        }
        if (va < vb) return -1;
        if (va > vb) return 1;
    }

    int a_pre = (*pa == '-');
    int b_pre = (*pb == '-');
    if (!a_pre && b_pre) return 1;
    if (a_pre && !b_pre) return -1;
    if (a_pre && b_pre) return hub_stricmp(pa + 1, pb + 1);
    return 0;
}

// ==========================================================================
// SHA-256 (FIPS 180-4) & CRC-32 (IEEE 802.3)
// ==========================================================================

static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform(HubSha256 *ctx, const uint8_t data[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; i++, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | (uint32_t)data[j + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(m[i - 15], 7) ^ rotr32(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr32(m[i - 2], 17) ^ rotr32(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + k[i] + m[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void hub_sha256_init(HubSha256 *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
}

void hub_sha256_update(HubSha256 *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void hub_sha256_final(HubSha256 *ctx, uint8_t hash[32]) {
    size_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    for (int j = 0; j < 8; j++) {
        ctx->data[63 - j] = (uint8_t)((ctx->bitlen >> (j * 8)) & 0xffu);
    }
    sha256_transform(ctx, ctx->data);
    for (int j = 0; j < 8; j++) {
        hash[j * 4 + 0] = (uint8_t)((ctx->state[j] >> 24) & 0xffu);
        hash[j * 4 + 1] = (uint8_t)((ctx->state[j] >> 16) & 0xffu);
        hash[j * 4 + 2] = (uint8_t)((ctx->state[j] >> 8) & 0xffu);
        hash[j * 4 + 3] = (uint8_t)(ctx->state[j] & 0xffu);
    }
}

void hub_sha256_hex(const uint8_t *data, size_t len, char out_hex[65]) {
    HubSha256 ctx;
    hub_sha256_init(&ctx);
    if (len > 0) hub_sha256_update(&ctx, data, len);
    uint8_t hash[32];
    hub_sha256_final(&ctx, hash);
    for (int i = 0; i < 32; i++) {
        sprintf(out_hex + i * 2, "%02x", hash[i]);
    }
    out_hex[64] = '\0';
}

int hub_sha256_file(const char *path, char out_hex[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    HubSha256 ctx;
    hub_sha256_init(&ctx);
    uint8_t buf[32768];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        hub_sha256_update(&ctx, buf, n);
    }
    int err = ferror(f);
    fclose(f);
    if (err) return -1;
    uint8_t hash[32];
    hub_sha256_final(&ctx, hash);
    for (int i = 0; i < 32; i++) {
        sprintf(out_hex + i * 2, "%02x", hash[i]);
    }
    out_hex[64] = '\0';
    return 0;
}

int hub_is_valid_sha256_hex(const char *s) {
    if (!s || strlen(s) != 64) return 0;
    for (int i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)s[i])) return 0;
    }
    return 1;
}

uint32_t hub_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

// ==========================================================================
// Filesystem & JSON Pretty-Print Helpers
// ==========================================================================

int hub_path_exists(const char *path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

int hub_dir_exists(const char *path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

int hub_file_exists(const char *path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

int hub_mkdir_p(const char *path) {
    char tmp[HUB_PATH_CAP];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *c = tmp; *c; c++) {
        if (*c != '/' && *c != '\\') continue;
        if (c == tmp) continue;
        if (c > tmp && c[-1] == ':') continue;
        char k = *c;
        *c = '\0';
#ifdef _WIN32
        CreateDirectoryA(tmp, NULL);
#else
        mkdir(tmp, 0755);
#endif
        *c = k;
    }
#ifdef _WIN32
    if (CreateDirectoryA(tmp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }
    return hub_dir_exists(tmp) ? 0 : -1;
#else
    if (mkdir(tmp, 0755) == 0 || errno == EEXIST) {
        return hub_dir_exists(tmp) ? 0 : -1;
    }
    return -1;
#endif
}

void hub_rmdir_recursive(const char *dir) {
#ifdef _WIN32
    char pat[HUB_PATH_CAP];
    snprintf(pat, sizeof(pat), "%s/*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char sub[HUB_PATH_CAP];
            snprintf(sub, sizeof(sub), "%s/%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                hub_rmdir_recursive(sub);
            } else {
                SetFileAttributesA(sub, FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(sub);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char sub[HUB_PATH_CAP];
            snprintf(sub, sizeof(sub), "%s/%s", dir, ent->d_name);
            struct stat st;
            if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode)) {
                hub_rmdir_recursive(sub);
            } else {
                unlink(sub);
            }
        }
        closedir(d);
    }
    rmdir(dir);
#endif
}

static int hub_move_path(const char *src, const char *dst) {
#ifdef _WIN32
    return MoveFileA(src, dst) ? 0 : -1;
#else
    return rename(src, dst);
#endif
}

int hub_atomic_write_file(const char *path, const void *data, size_t len,
                          char *err, size_t err_cap) {
    char tmp_path[HUB_PATH_CAP];
    int pn = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (pn < 0 || (size_t)pn >= sizeof(tmp_path)) {
        if (err && err_cap > 0) snprintf(err, err_cap, "Temp file path too long");
        return -1;
    }

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        if (err && err_cap > 0) {
            snprintf(err, err_cap, "Failed to create temp file '%s'", tmp_path);
        }
        return -1;
    }

    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fclose(f);
        remove(tmp_path);
        if (err && err_cap > 0) {
            snprintf(err, err_cap, "Failed writing data to '%s'", tmp_path);
        }
        return -1;
    }
    fflush(f);
    fclose(f);

#ifdef _WIN32
    if (!MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(path);
        if (!MoveFileA(tmp_path, path)) {
            remove(tmp_path);
            if (err && err_cap > 0) {
                snprintf(err, err_cap, "Failed replacing '%s' (Win32 %lu)",
                         path, (unsigned long)GetLastError());
            }
            return -1;
        }
    }
#else
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        if (err && err_cap > 0) {
            snprintf(err, err_cap, "Failed replacing '%s'", path);
        }
        return -1;
    }
#endif
    return 0;
}

static void json_indent(HubBuf *b, int indent) {
    for (int i = 0; i < indent; i++) hub_buf_puts(b, "  ");
}

static void json_write_val(HubBuf *b, const WlJsonVal *val, int indent) {
    if (!val) {
        hub_buf_puts(b, "null");
        return;
    }
    switch (val->type) {
        case WL_JSON_NULL:
            hub_buf_puts(b, "null");
            break;
        case WL_JSON_BOOL:
            hub_buf_puts(b, val->u.bool_val ? "true" : "false");
            break;
        case WL_JSON_NUMBER:
            if (val->u.num.raw[0]) {
                hub_buf_puts(b, val->u.num.raw);
            } else if (val->u.num.is_float) {
                char buf[64];
                if (floor(val->u.num.dval) == val->u.num.dval && fabs(val->u.num.dval) < 1e15) {
                    snprintf(buf, sizeof(buf), "%.1f", val->u.num.dval);
                } else {
                    snprintf(buf, sizeof(buf), "%.15g", val->u.num.dval);
                }
                hub_buf_puts(b, buf);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "%lld", (long long)val->u.num.ival);
                hub_buf_puts(b, buf);
            }
            break;
        case WL_JSON_STRING:
            hub_buf_puts_escaped(b, val->u.str);
            break;
        case WL_JSON_ARRAY:
            if (val->u.arr.count == 0) {
                hub_buf_puts(b, "[]");
            } else {
                hub_buf_puts(b, "[\n");
                for (size_t i = 0; i < val->u.arr.count; i++) {
                    json_indent(b, indent + 1);
                    json_write_val(b, val->u.arr.items[i], indent + 1);
                    hub_buf_puts(b, (i + 1 < val->u.arr.count) ? ",\n" : "\n");
                }
                json_indent(b, indent);
                hub_buf_puts(b, "]");
            }
            break;
        case WL_JSON_OBJECT:
            if (val->u.obj.count == 0) {
                hub_buf_puts(b, "{}");
            } else {
                hub_buf_puts(b, "{\n");
                for (size_t i = 0; i < val->u.obj.count; i++) {
                    json_indent(b, indent + 1);
                    hub_buf_puts_escaped(b, val->u.obj.members[i].key);
                    hub_buf_puts(b, ": ");
                    json_write_val(b, val->u.obj.members[i].val, indent + 1);
                    hub_buf_puts(b, (i + 1 < val->u.obj.count) ? ",\n" : "\n");
                }
                json_indent(b, indent);
                hub_buf_puts(b, "}");
            }
            break;
    }
}

char *hub_json_write_pretty(const WlJsonVal *val, size_t *out_len) {
    HubBuf b;
    hub_buf_init(&b);
    json_write_val(&b, val, 0);
    hub_buf_puts(&b, "\n");
    if (out_len) *out_len = b.len;
    return b.data;
}

// ==========================================================================
// URL Resolution & HTTP/HTTPS/Local Fetching
// ==========================================================================

void hub_resolve_index_url(const char *game_dir, char *eff_out, size_t eff_cap,
                           char *cfg_out, size_t cfg_cap, int *env_override) {
    if (cfg_out && cfg_cap > 0) cfg_out[0] = '\0';
    if (env_override) *env_override = 0;

    char cfg_url[HUB_PATH_CAP] = "";
    if (game_dir && game_dir[0]) {
        char spath[HUB_PATH_CAP];
        snprintf(spath, sizeof(spath), "%s/wololoader/scriptorium-settings.json", game_dir);
        char *stxt = wl_read_file(spath, NULL);
        if (stxt) {
            WlJsonVal *sv = wl_json_parse(stxt, NULL, 0);
            free(stxt);
            if (sv && sv->type == WL_JSON_OBJECT) {
                const char *u = wl_json_str(wl_json_get(sv, "hub_url"), "");
                if (u && u[0]) snprintf(cfg_url, sizeof(cfg_url), "%s", u);
            }
            wl_json_free(sv);
        }
    }
    if (cfg_out && cfg_cap > 0) snprintf(cfg_out, cfg_cap, "%s", cfg_url);

    const char *env_url = getenv("WL_HUB_URL");
    if (env_url && env_url[0]) {
        snprintf(eff_out, eff_cap, "%s", env_url);
        if (env_override) *env_override = 1;
    } else if (cfg_url[0]) {
        snprintf(eff_out, eff_cap, "%s", cfg_url);
    } else {
        snprintf(eff_out, eff_cap, "%s", HUB_DEFAULT_URL);
    }
}

void hub_resolve_rel_url(const char *base_url, const char *rel, char *out, size_t cap) {
    if (!rel || !rel[0]) {
        if (cap > 0) out[0] = '\0';
        return;
    }
    if (strstr(rel, "://") != NULL || rel[0] == '/' || rel[0] == '\\' ||
        (isalpha((unsigned char)rel[0]) && rel[1] == ':')) {
        snprintf(out, cap, "%s", rel);
        return;
    }
    if (strncmp(rel, "./", 2) == 0) rel += 2;
    const char *last_fwd = strrchr(base_url, '/');
    const char *last_bck = strrchr(base_url, '\\');
    const char *sep = (last_fwd && last_bck) ? (last_fwd > last_bck ? last_fwd : last_bck)
                                             : (last_fwd ? last_fwd : last_bck);
    if (!sep) {
        snprintf(out, cap, "%s", rel);
        return;
    }
    size_t prefix_len = (size_t)(sep - base_url + 1);
    if (prefix_len >= cap) prefix_len = cap - 1;
    memcpy(out, base_url, prefix_len);
    snprintf(out + prefix_len, cap - prefix_len, "%s", rel);
}

int hub_url_to_local_path(const char *url, char *out, size_t cap) {
    if (!url || !url[0]) return 0;
    if (hub_strnicmp(url, "http://", 7) == 0 || hub_strnicmp(url, "https://", 8) == 0) {
        return 0;
    }
    if (hub_strnicmp(url, "file://", 7) == 0) {
        const char *p = url + 7;
        if (hub_strnicmp(p, "localhost/", 10) == 0) p += 9;
        if (p[0] == '/' && isalpha((unsigned char)p[1]) && p[2] == ':') p++;
        size_t o = 0;
        for (size_t i = 0; p[i] && o + 1 < cap; i++) {
            if (p[i] == '%' && isxdigit((unsigned char)p[i + 1]) && isxdigit((unsigned char)p[i + 2])) {
                char hex[3] = { p[i + 1], p[i + 2], 0 };
                out[o++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else {
                out[o++] = p[i];
            }
        }
        out[o] = '\0';
        return 1;
    }
    snprintf(out, cap, "%s", url);
    return 1;
}

int hub_fetch_stream(const char *url, size_t max_bytes, FILE *fp,
                     char **out_buf, size_t *out_len,
                     HubProgressFn progress_cb, void *progress_user,
                     const char *progress_mod_id,
                     char *err, size_t err_cap) {
    if (out_buf) *out_buf = NULL;
    if (out_len) *out_len = 0;
    if (!url || !url[0]) {
        snprintf(err, err_cap, "Empty URL");
        return -1;
    }

    char local_path[HUB_PATH_CAP];
    if (hub_url_to_local_path(url, local_path, sizeof(local_path))) {
        FILE *src = fopen(local_path, "rb");
        if (!src) {
            snprintf(err, err_cap, "Cannot open local file '%s'", local_path);
            return -1;
        }
        fseek(src, 0, SEEK_END);
        long sz = ftell(src);
        fseek(src, 0, SEEK_SET);
        if (sz < 0 || (size_t)sz > max_bytes) {
            fclose(src);
            snprintf(err, err_cap, "Local file '%s' exceeds maximum allowed size (%zu bytes)", local_path, max_bytes);
            return -1;
        }
        if (fp) {
            uint8_t buf[32768];
            size_t total = 0, n;
            if (progress_cb) progress_cb("downloading", progress_mod_id, 0, (size_t)sz, progress_user);
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                total += n;
                if (total > max_bytes || fwrite(buf, 1, n, fp) != n) {
                    fclose(src);
                    snprintf(err, err_cap, "Failed writing downloaded file");
                    return -1;
                }
                if (progress_cb) progress_cb("downloading", progress_mod_id, total, (size_t)sz, progress_user);
            }
            fclose(src);
            if (out_len) *out_len = total;
            return 0;
        } else {
            char *mem = malloc((size_t)sz + 1);
            if (!mem) {
                fclose(src);
                snprintf(err, err_cap, "Out of memory");
                return -1;
            }
            size_t got = sz > 0 ? fread(mem, 1, (size_t)sz, src) : 0;
            fclose(src);
            mem[got] = '\0';
            if (out_buf) *out_buf = mem; else free(mem);
            if (out_len) *out_len = got;
            return 0;
        }
    }

#ifdef _WIN32
    wchar_t wurl[2048];
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, (int)(sizeof(wurl) / sizeof(wurl[0])))) {
        snprintf(err, err_cap, "Invalid URL encoding");
        return -1;
    }

    wchar_t whost[256] = L"";
    wchar_t wpath[1536] = L"";
    wchar_t wextra[512] = L"";
    URL_COMPONENTSW uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = whost;
    uc.dwHostNameLength = (DWORD)(sizeof(whost) / sizeof(whost[0]));
    uc.lpszUrlPath = wpath;
    uc.dwUrlPathLength = (DWORD)(sizeof(wpath) / sizeof(wpath[0]));
    uc.lpszExtraInfo = wextra;
    uc.dwExtraInfoLength = (DWORD)(sizeof(wextra) / sizeof(wextra[0]));

    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        snprintf(err, err_cap, "Invalid HTTP/HTTPS URL: %s", url);
        return -1;
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
        snprintf(err, err_cap, "Unsupported URL scheme (expected http/https/file)");
        return -1;
    }

    wchar_t wreq_path[2048];
    if (wpath[0] || wextra[0]) {
        swprintf(wreq_path, sizeof(wreq_path) / sizeof(wreq_path[0]), L"%ls%ls", wpath, wextra);
    } else {
        wcscpy(wreq_path, L"/");
    }

    HINTERNET hSession = WinHttpOpen(L"Wololoader-Hub/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        snprintf(err, err_cap, "WinHttpOpen failed (%lu)", (unsigned long)GetLastError());
        return -1;
    }

    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);

    HINTERNET hConnect = WinHttpConnect(hSession, whost, uc.nPort, 0);
    if (!hConnect) {
        snprintf(err, err_cap, "Could not connect to server (%lu)", (unsigned long)GetLastError());
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD req_flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", wreq_path, NULL,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, req_flags);
    if (!hReq) {
        snprintf(err, err_cap, "WinHttpOpenRequest failed (%lu)", (unsigned long)GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, NULL)) {
        DWORD le = GetLastError();
        snprintf(err, err_cap, "Network error fetching URL (WinHTTP error %lu)", (unsigned long)le);
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD status = 0;
    DWORD status_sz = sizeof(status);
    if (!WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz, WINHTTP_NO_HEADER_INDEX)) {
        snprintf(err, err_cap, "Failed reading HTTP status code");
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }
    if (status != 200) {
        snprintf(err, err_cap, "Server returned HTTP %lu", (unsigned long)status);
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD content_len = 0;
    DWORD cl_sz = sizeof(content_len);
    if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &content_len, &cl_sz, WINHTTP_NO_HEADER_INDEX)) {
        if ((size_t)content_len > max_bytes) {
            snprintf(err, err_cap, "Remote file exceeds maximum allowed size (%lu > %zu bytes)",
                     (unsigned long)content_len, max_bytes);
            WinHttpCloseHandle(hReq);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return -1;
        }
    }

    size_t total = 0;
    size_t mem_cap = 0;
    char *mem_buf = NULL;
    if (!fp) {
        mem_cap = (content_len > 0 && content_len < max_bytes) ? (size_t)content_len + 1 : 8192;
        mem_buf = malloc(mem_cap);
        if (!mem_buf) {
            snprintf(err, err_cap, "Out of memory");
            WinHttpCloseHandle(hReq);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return -1;
        }
        mem_buf[0] = '\0';
    }

    if (progress_cb) {
        progress_cb("downloading", progress_mod_id, 0, (size_t)content_len, progress_user);
    }

    int ok = 1;
    while (ok) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) {
            snprintf(err, err_cap, "Network read error (%lu)", (unsigned long)GetLastError());
            ok = 0;
            break;
        }
        if (avail == 0) break;

        while (avail > 0) {
            uint8_t tmp[32768];
            DWORD to_read = avail < sizeof(tmp) ? avail : (DWORD)sizeof(tmp);
            DWORD got = 0;
            if (!WinHttpReadData(hReq, tmp, to_read, &got)) {
                snprintf(err, err_cap, "Network read error (%lu)", (unsigned long)GetLastError());
                ok = 0;
                break;
            }
            if (got == 0) {
                avail = 0;
                break;
            }
            if (total + got > max_bytes) {
                snprintf(err, err_cap, "Remote file exceeds maximum allowed size (%zu bytes)", max_bytes);
                ok = 0;
                break;
            }
            if (fp) {
                if (fwrite(tmp, 1, got, fp) != got) {
                    snprintf(err, err_cap, "Disk write error");
                    ok = 0;
                    break;
                }
            } else {
                if (total + got + 1 > mem_cap) {
                    size_t ncap = mem_cap * 2;
                    if (ncap < total + got + 1) ncap = total + got + 1;
                    char *nb = realloc(mem_buf, ncap);
                    if (!nb) {
                        snprintf(err, err_cap, "Out of memory");
                        ok = 0;
                        break;
                    }
                    mem_buf = nb;
                    mem_cap = ncap;
                }
                memcpy(mem_buf + total, tmp, got);
                mem_buf[total + got] = '\0';
            }
            total += got;
            avail -= got;
            if (progress_cb) {
                progress_cb("downloading", progress_mod_id, total, (size_t)content_len, progress_user);
            }
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!ok) {
        free(mem_buf);
        return -1;
    }

    if (out_buf) *out_buf = mem_buf; else free(mem_buf);
    if (out_len) *out_len = total;
    return 0;
#else
    (void)fp;
    (void)max_bytes;
    (void)progress_cb;
    (void)progress_user;
    (void)progress_mod_id;
    snprintf(err, err_cap, "HTTP/HTTPS fetching is only available on Windows builds (%s)", url);
    return -1;
#endif
}

// ==========================================================================
// Minimal Safe ZIP Reader & Extractor
// ==========================================================================

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct {
    char     norm[512];
    uint16_t method;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t local_off;
    int      is_dir;
} HubZipEntry;

int hub_validate_zip_path(const char *raw, size_t raw_len, uint16_t ver_made,
                          uint32_t ext_attr, char out_norm[512], int *out_is_dir) {
    if (raw_len == 0 || raw_len >= 512) return -1;

    uint8_t os = (uint8_t)(ver_made >> 8);
    uint32_t posix_type = (ext_attr >> 16) & 0xF000u;
    if (posix_type == 0xA000u) return -1; // POSIX symlink
    if ((os == 3 || os == 19) && posix_type != 0 &&
        posix_type != 0x8000u && posix_type != 0x4000u) {
        return -1; // FIFO, socket, device, etc.
    }
    if ((ext_attr & 0x0400u) != 0) return -1; // Windows FILE_ATTRIBUTE_REPARSE_POINT

    if (raw[0] == '/' || raw[0] == '\\') return -1;
    for (size_t i = 0; i < raw_len; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c == '\0' || c < 0x20 || c == ':') return -1;
        out_norm[i] = (c == '\\') ? '/' : (char)c;
    }
    out_norm[raw_len] = '\0';

    int is_dir = (out_norm[raw_len - 1] == '/') || ((ext_attr & 0x10u) != 0) || (posix_type == 0x4000u);
    while (raw_len > 0 && out_norm[raw_len - 1] == '/') {
        out_norm[--raw_len] = '\0';
        is_dir = 1;
    }
    if (raw_len == 0) return -1;

    const char *seg = out_norm;
    while (1) {
        const char *sl = strchr(seg, '/');
        size_t slen = sl ? (size_t)(sl - seg) : strlen(seg);
        if (slen == 0) return -1;
        if ((slen == 1 && seg[0] == '.') || (slen == 2 && seg[0] == '.' && seg[1] == '.')) {
            return -1;
        }
        if (!sl) break;
        seg = sl + 1;
    }

    *out_is_dir = is_dir;
    return 0;
}

int hub_extract_zip_safe(const char *zip_path, const char *dest_dir,
                         HubInflateFn inflate_fn, void *inflate_user,
                         char *err, size_t err_cap) {
    if (!inflate_fn) {
        snprintf(err, err_cap, "Internal error: missing inflate backend");
        return -1;
    }
    FILE *zf = fopen(zip_path, "rb");
    if (!zf) {
        snprintf(err, err_cap, "Cannot open downloaded zip '%s'", zip_path);
        return -1;
    }

    if (fseek(zf, 0, SEEK_END) != 0) {
        fclose(zf);
        snprintf(err, err_cap, "Failed seeking zip file");
        return -1;
    }
    long zip_sz_l = ftell(zf);
    if (zip_sz_l < 22 || (unsigned long)zip_sz_l > HUB_MAX_ZIP_BYTES) {
        fclose(zf);
        snprintf(err, err_cap, "Invalid zip file size");
        return -1;
    }
    size_t zip_sz = (size_t)zip_sz_l;

    size_t tail_sz = zip_sz < (65535u + 22u) ? zip_sz : (65535u + 22u);
    uint8_t *tail = malloc(tail_sz);
    if (!tail) {
        fclose(zf);
        snprintf(err, err_cap, "Out of memory");
        return -1;
    }
    fseek(zf, (long)(zip_sz - tail_sz), SEEK_SET);
    if (fread(tail, 1, tail_sz, zf) != tail_sz) {
        free(tail);
        fclose(zf);
        snprintf(err, err_cap, "Failed reading zip trailer");
        return -1;
    }

    long eocd_pos = -1;
    for (long i = (long)tail_sz - 22; i >= 0; i--) {
        if (rd_u32(tail + i) == 0x06054b50u) {
            uint16_t cmt_len = rd_u16(tail + i + 20);
            if ((size_t)i + 22u + cmt_len == tail_sz) {
                eocd_pos = i;
                break;
            }
        }
    }
    if (eocd_pos < 0) {
        free(tail);
        fclose(zf);
        snprintf(err, err_cap, "Invalid zip archive (missing central directory)");
        return -1;
    }

    uint16_t disk_no      = rd_u16(tail + eocd_pos + 4);
    uint16_t cd_disk      = rd_u16(tail + eocd_pos + 6);
    uint16_t disk_entries = rd_u16(tail + eocd_pos + 8);
    uint16_t total_ent    = rd_u16(tail + eocd_pos + 10);
    uint32_t cd_size      = rd_u32(tail + eocd_pos + 12);
    uint32_t cd_offset    = rd_u32(tail + eocd_pos + 16);
    size_t   eocd_file_off = zip_sz - tail_sz + (size_t)eocd_pos;
    free(tail);

    if (disk_no != 0 || cd_disk != 0 || disk_entries != total_ent ||
        total_ent == 0 || total_ent == 0xffffu ||
        cd_size == 0xffffffffu || cd_offset == 0xffffffffu) {
        fclose(zf);
        snprintf(err, err_cap, "Unsupported zip archive (multi-disk, empty, or Zip64)");
        return -1;
    }
    if (total_ent > HUB_MAX_ENTRIES) {
        fclose(zf);
        snprintf(err, err_cap, "Zip has too many entries (%u > %u)", total_ent, HUB_MAX_ENTRIES);
        return -1;
    }
    if ((uint64_t)cd_offset + (uint64_t)cd_size > eocd_file_off || cd_size < (uint32_t)total_ent * 46u) {
        fclose(zf);
        snprintf(err, err_cap, "Corrupt zip central directory bounds");
        return -1;
    }

    uint8_t *cd = malloc(cd_size);
    if (!cd) {
        fclose(zf);
        snprintf(err, err_cap, "Out of memory");
        return -1;
    }
    fseek(zf, (long)cd_offset, SEEK_SET);
    if (fread(cd, 1, cd_size, zf) != cd_size) {
        free(cd);
        fclose(zf);
        snprintf(err, err_cap, "Failed reading zip central directory");
        return -1;
    }

    HubZipEntry *entries = calloc(total_ent, sizeof(HubZipEntry));
    if (!entries) {
        free(cd);
        fclose(zf);
        snprintf(err, err_cap, "Out of memory");
        return -1;
    }

    size_t pos = 0;
    uint64_t total_uncomp = 0;
    for (uint16_t idx = 0; idx < total_ent; idx++) {
        if (pos + 46 > cd_size || rd_u32(cd + pos) != 0x02014b50u) {
            snprintf(err, err_cap, "Corrupt zip central directory entry");
            goto fail;
        }
        uint16_t ver_made   = rd_u16(cd + pos + 4);
        uint16_t flags      = rd_u16(cd + pos + 8);
        uint16_t method     = rd_u16(cd + pos + 10);
        uint32_t crc        = rd_u32(cd + pos + 16);
        uint32_t comp_sz    = rd_u32(cd + pos + 20);
        uint32_t uncomp_sz  = rd_u32(cd + pos + 24);
        uint16_t name_len   = rd_u16(cd + pos + 28);
        uint16_t extra_len  = rd_u16(cd + pos + 30);
        uint16_t cmt_len    = rd_u16(cd + pos + 32);
        uint32_t ext_attr   = rd_u32(cd + pos + 38);
        uint32_t local_off  = rd_u32(cd + pos + 42);

        if (pos + 46 + name_len + extra_len + cmt_len > cd_size) {
            snprintf(err, err_cap, "Truncated zip central directory entry");
            goto fail;
        }
        if (flags & 1u) {
            snprintf(err, err_cap, "Encrypted zip archives are not supported");
            goto fail;
        }
        if (method != 0 && method != 8) {
            snprintf(err, err_cap, "Unsupported zip compression method (%u)", method);
            goto fail;
        }
        if (uncomp_sz > HUB_MAX_ENTRY_BYTES) {
            snprintf(err, err_cap, "Zip entry exceeds maximum file size (%u bytes)", uncomp_sz);
            goto fail;
        }
        total_uncomp += uncomp_sz;
        if (total_uncomp > HUB_MAX_TOTAL_BYTES) {
            snprintf(err, err_cap, "Zip total uncompressed size exceeds maximum limit");
            goto fail;
        }

        HubZipEntry *e = &entries[idx];
        if (hub_validate_zip_path((const char *)(cd + pos + 46), name_len,
                                  ver_made, ext_attr, e->norm, &e->is_dir) != 0) {
            snprintf(err, err_cap, "Rejected unsafe path or symlink in zip archive");
            goto fail;
        }
        e->method      = method;
        e->crc32       = crc;
        e->comp_size   = comp_sz;
        e->uncomp_size = uncomp_sz;
        e->local_off   = local_off;

        pos += 46 + name_len + extra_len + cmt_len;
    }
    free(cd);
    cd = NULL;

    // Determine layout: either mod.json at root, or single top-level folder containing mod.json.
    char prefix[512] = "";
    int has_root_modjson = 0;
    for (uint16_t i = 0; i < total_ent; i++) {
        if (!entries[i].is_dir && strcmp(entries[i].norm, "mod.json") == 0) {
            has_root_modjson = 1;
            break;
        }
    }

    if (!has_root_modjson) {
        char top[512] = "";
        const char *sl = strchr(entries[0].norm, '/');
        if (sl) {
            size_t tlen = (size_t)(sl - entries[0].norm);
            memcpy(top, entries[0].norm, tlen);
            top[tlen] = '\0';
        } else {
            snprintf(top, sizeof(top), "%s", entries[0].norm);
        }
        size_t tlen = strlen(top);
        int all_under_top = (tlen > 0);
        int has_top_modjson = 0;
        for (uint16_t i = 0; i < total_ent && all_under_top; i++) {
            const char *p = entries[i].norm;
            if (strcmp(p, top) == 0) {
                if (!entries[i].is_dir) all_under_top = 0;
            } else if (strncmp(p, top, tlen) == 0 && p[tlen] == '/') {
                if (!entries[i].is_dir && strcmp(p + tlen + 1, "mod.json") == 0) {
                    has_top_modjson = 1;
                }
            } else {
                all_under_top = 0;
            }
        }
        if (!all_under_top || !has_top_modjson) {
            snprintf(err, err_cap, "Zip must contain mod.json at root or inside a single top-level folder");
            goto fail;
        }
        snprintf(prefix, sizeof(prefix), "%s/", top);
    }

    size_t prefix_len = strlen(prefix);
    size_t actual_total = 0;

    for (uint16_t i = 0; i < total_ent; i++) {
        HubZipEntry *e = &entries[i];
        const char *rel = e->norm;
        if (prefix_len > 0) {
            if (strncmp(e->norm, prefix, prefix_len) != 0) {
                continue;
            }
            rel = e->norm + prefix_len;
        }
        if (!rel[0]) continue;
        if (strcmp(rel, ".hub.json") == 0) continue;

        char out_path[HUB_PATH_CAP];
        int pn = snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, rel);
        if (pn < 0 || (size_t)pn >= sizeof(out_path)) {
            snprintf(err, err_cap, "Extracted path too long");
            goto fail;
        }

        if (e->is_dir) {
            if (hub_mkdir_p(out_path) != 0) {
                snprintf(err, err_cap, "Failed creating directory '%s'", out_path);
                goto fail;
            }
            continue;
        }

        if ((uint64_t)e->local_off + 30u > cd_offset) {
            snprintf(err, err_cap, "Corrupt zip local header offset");
            goto fail;
        }
        uint8_t lhdr[30];
        fseek(zf, (long)e->local_off, SEEK_SET);
        if (fread(lhdr, 1, 30, zf) != 30 || rd_u32(lhdr) != 0x04034b50u) {
            snprintf(err, err_cap, "Corrupt zip local file header");
            goto fail;
        }
        uint16_t l_nlen = rd_u16(lhdr + 26);
        uint16_t l_elen = rd_u16(lhdr + 28);
        uint64_t data_off = (uint64_t)e->local_off + 30u + l_nlen + l_elen;
        if (data_off + e->comp_size > cd_offset) {
            snprintf(err, err_cap, "Zip entry extends past central directory");
            goto fail;
        }

        uint8_t *comp_buf = malloc(e->comp_size ? e->comp_size : 1);
        if (!comp_buf) {
            snprintf(err, err_cap, "Out of memory");
            goto fail;
        }
        if (e->comp_size > 0) {
            fseek(zf, (long)data_off, SEEK_SET);
            if (fread(comp_buf, 1, e->comp_size, zf) != e->comp_size) {
                free(comp_buf);
                snprintf(err, err_cap, "Failed reading zip entry payload");
                goto fail;
            }
        }

        uint8_t *raw_buf = NULL;
        size_t raw_len = 0;
        if (e->method == 0) {
            if (e->comp_size != e->uncomp_size) {
                free(comp_buf);
                snprintf(err, err_cap, "Corrupt stored zip entry size");
                goto fail;
            }
            raw_buf = comp_buf;
            raw_len = e->comp_size;
            comp_buf = NULL;
        } else {
            int irc = inflate_fn(comp_buf, e->comp_size, HUB_MAX_ENTRY_BYTES,
                                 &raw_buf, &raw_len, inflate_user);
            free(comp_buf);
            if (irc != 0 || raw_len != e->uncomp_size) {
                free(raw_buf);
                snprintf(err, err_cap, "Zip decompression failed or size mismatch for '%s'", rel);
                goto fail;
            }
        }

        actual_total += raw_len;
        if (actual_total > HUB_MAX_TOTAL_BYTES) {
            free(raw_buf);
            snprintf(err, err_cap, "Extracted total size exceeds limit");
            goto fail;
        }

        if (hub_crc32(raw_buf, raw_len) != e->crc32) {
            free(raw_buf);
            snprintf(err, err_cap, "CRC-32 mismatch for '%s'", rel);
            goto fail;
        }

        char parent[HUB_PATH_CAP];
        snprintf(parent, sizeof(parent), "%s", out_path);
        char *sl = strrchr(parent, '/');
        if (sl) {
            *sl = '\0';
            if (hub_mkdir_p(parent) != 0) {
                free(raw_buf);
                snprintf(err, err_cap, "Failed creating parent directory for '%s'", rel);
                goto fail;
            }
        }

        FILE *outf = fopen(out_path, "wb");
        if (!outf) {
            free(raw_buf);
            snprintf(err, err_cap, "Failed writing extracted file '%s'", rel);
            goto fail;
        }
        if (raw_len > 0 && fwrite(raw_buf, 1, raw_len, outf) != raw_len) {
            fclose(outf);
            free(raw_buf);
            snprintf(err, err_cap, "Disk write failed for '%s'", rel);
            goto fail;
        }
        fclose(outf);
        free(raw_buf);
    }

    free(entries);
    fclose(zf);
    return 0;

fail:
    free(cd);
    free(entries);
    fclose(zf);
    return -1;
}

// ==========================================================================
// Index Fetch / Parse & Local Mod Inspection
// ==========================================================================

WlJsonVal *hub_fetch_and_parse_index(const char *url, char *err, size_t err_cap) {
    char *raw = NULL;
    size_t raw_len = 0;
    if (hub_fetch_stream(url, HUB_MAX_INDEX_BYTES, NULL, &raw, &raw_len,
                         NULL, NULL, NULL, err, err_cap) != 0) {
        return NULL;
    }

    char jerr[256] = "";
    WlJsonVal *parsed = wl_json_parse(raw, jerr, sizeof(jerr));
    free(raw);
    if (!parsed || parsed->type != WL_JSON_OBJECT) {
        wl_json_free(parsed);
        snprintf(err, err_cap, "Invalid hub index JSON: %s", jerr[0] ? jerr : "expected object");
        return NULL;
    }

    const WlJsonVal *v_sv = wl_json_get(parsed, "schema_version");
    if (!v_sv || v_sv->type != WL_JSON_NUMBER || v_sv->u.num.ival != 1) {
        wl_json_free(parsed);
        snprintf(err, err_cap, "Unsupported hub index schema_version (expected 1)");
        return NULL;
    }

    const WlJsonVal *v_mods = wl_json_get(parsed, "mods");
    if (!v_mods || v_mods->type != WL_JSON_ARRAY) {
        wl_json_free(parsed);
        snprintf(err, err_cap, "Hub index missing 'mods' array");
        return NULL;
    }

    return parsed;
}

void hub_inspect_local_mod(const char *mods_dir, const char *id, HubLocalState *out) {
    memset(out, 0, sizeof(*out));
    out->enabled = 1;
    if (!mods_dir || !hub_is_valid_id(id)) return;

    char mpath[HUB_PATH_CAP];
    snprintf(mpath, sizeof(mpath), "%s/%s/mod.json", mods_dir, id);
    char *txt = wl_read_file(mpath, NULL);
    if (!txt) return;

    WlJsonVal *mj = wl_json_parse(txt, NULL, 0);
    free(txt);
    if (!mj || mj->type != WL_JSON_OBJECT) {
        wl_json_free(mj);
        return;
    }

    out->installed = 1;
    const char *ver = wl_json_str(wl_json_get(mj, "version"), "");
    snprintf(out->version, sizeof(out->version), "%s", ver ? ver : "");

    const WlJsonVal *ven = wl_json_get(mj, "enabled");
    if (ven && ven->type == WL_JSON_BOOL) {
        out->has_enabled = 1;
        out->enabled = ven->u.bool_val;
    }
    wl_json_free(mj);

    char hpath[HUB_PATH_CAP];
    snprintf(hpath, sizeof(hpath), "%s/%s/.hub.json", mods_dir, id);
    out->hub_managed = hub_file_exists(hpath);
}

const WlJsonVal *hub_find_mod_in_index(const WlJsonVal *root, const char *id) {
    const WlJsonVal *mods = wl_json_get(root, "mods");
    if (!mods || mods->type != WL_JSON_ARRAY || !id) return NULL;
    for (size_t i = 0; i < mods->u.arr.count; i++) {
        const WlJsonVal *m = mods->u.arr.items[i];
        if (!m || m->type != WL_JSON_OBJECT) continue;
        const char *mid = wl_json_str(wl_json_get(m, "id"), NULL);
        if (mid && strcmp(mid, id) == 0) return m;
    }
    return NULL;
}

// ==========================================================================
// Installer & Uninstaller
// ==========================================================================

static int hub_preserve_enabled_in_modjson(const char *modjson_path, int enabled,
                                           char *err, size_t err_cap) {
    char *txt = wl_read_file(modjson_path, NULL);
    if (!txt) {
        snprintf(err, err_cap, "Extracted mod is missing mod.json");
        return -1;
    }
    WlJsonVal *mj = wl_json_parse(txt, NULL, 0);
    free(txt);
    if (!mj || mj->type != WL_JSON_OBJECT) {
        wl_json_free(mj);
        snprintf(err, err_cap, "Extracted mod.json is malformed JSON");
        return -1;
    }

    int found = 0;
    for (size_t i = 0; i < mj->u.obj.count; i++) {
        if (strcmp(mj->u.obj.members[i].key, "enabled") == 0) {
            wl_json_free(mj->u.obj.members[i].val);
            WlJsonVal *bv = calloc(1, sizeof(WlJsonVal));
            bv->type = WL_JSON_BOOL;
            bv->u.bool_val = enabled ? 1 : 0;
            mj->u.obj.members[i].val = bv;
            found = 1;
            break;
        }
    }
    if (!found) {
        if (mj->u.obj.count >= mj->u.obj.cap) {
            size_t ncap = mj->u.obj.cap ? mj->u.obj.cap * 2 : 8;
            WlJsonMember *nm = realloc(mj->u.obj.members, ncap * sizeof(WlJsonMember));
            if (nm) {
                mj->u.obj.members = nm;
                mj->u.obj.cap = ncap;
            }
        }
        if (mj->u.obj.count < mj->u.obj.cap) {
            WlJsonVal *bv = calloc(1, sizeof(WlJsonVal));
            bv->type = WL_JSON_BOOL;
            bv->u.bool_val = enabled ? 1 : 0;
            size_t klen = strlen("enabled");
            char *kdup = malloc(klen + 1);
            if (kdup && bv) {
                memcpy(kdup, "enabled", klen + 1);
                mj->u.obj.members[mj->u.obj.count].key = kdup;
                mj->u.obj.members[mj->u.obj.count].val = bv;
                mj->u.obj.count++;
            } else {
                free(kdup);
                free(bv);
            }
        }
    }

    size_t out_len = 0;
    char *out_str = hub_json_write_pretty(mj, &out_len);
    wl_json_free(mj);
    if (!out_str) {
        snprintf(err, err_cap, "Out of memory serializing mod.json");
        return -1;
    }
    int rc = hub_atomic_write_file(modjson_path, out_str, out_len, err, err_cap);
    free(out_str);
    return rc;
}

static int hub_write_dot_hub_json(const char *dir, const char *id, const char *source_url,
                                  const char *download_url, const char *version,
                                  const char *sha256, char *err, size_t err_cap) {
    time_t now = time(NULL);
    char ts[32] = "";
#ifdef _WIN32
    struct tm *utc = gmtime(&now);
    if (utc) strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);
#else
    struct tm utc_tm;
    if (gmtime_r(&now, &utc_tm)) strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
#endif

    HubBuf b;
    hub_buf_init(&b);
    hub_buf_puts(&b, "{\n  \"id\": ");
    hub_buf_puts_escaped(&b, id);
    hub_buf_puts(&b, ",\n  \"source_url\": ");
    hub_buf_puts_escaped(&b, source_url);
    hub_buf_puts(&b, ",\n  \"download\": ");
    hub_buf_puts_escaped(&b, download_url);
    hub_buf_puts(&b, ",\n  \"version\": ");
    hub_buf_puts_escaped(&b, version);
    hub_buf_puts(&b, ",\n  \"sha256\": ");
    hub_buf_puts_escaped(&b, sha256);
    hub_buf_puts(&b, ",\n  \"installed_at\": ");
    hub_buf_puts_escaped(&b, ts);
    hub_buf_puts(&b, "\n}\n");

    char path[HUB_PATH_CAP];
    snprintf(path, sizeof(path), "%s/.hub.json", dir);
    int rc = hub_atomic_write_file(path, b.data, b.len, err, err_cap);
    free(b.data);
    return rc;
}

typedef struct {
    char  stack[HUB_MAX_DEP_DEPTH][128];
    int   depth;
    char *installed_deps[64];
    int   installed_dep_count;
} HubInstallCtx;

static int hub_install_single(const char *game_dir, const char *mods_dir,
                              const WlJsonVal *index_root, const char *index_url,
                              const char *id, int overwrite, int is_dep,
                              const char *wololoader_version,
                              HubInflateFn inflate_fn, void *inflate_user,
                              HubProgressFn progress_cb, void *progress_user,
                              HubLogFn log_cb, void *log_user,
                              HubInstallCtx *ctx,
                              int *out_updated, char *out_version, size_t ver_cap,
                              int *out_http_status, char *err, size_t err_cap) {
    *out_http_status = 400;
    if (!hub_is_valid_id(id)) {
        snprintf(err, err_cap, "Invalid mod id '%s'", id ? id : "");
        return -1;
    }

    for (int i = 0; i < ctx->depth; i++) {
        if (strcmp(ctx->stack[i], id) == 0) {
            snprintf(err, err_cap, "Circular dependency detected involving '%s'", id);
            return -1;
        }
    }
    if (ctx->depth >= HUB_MAX_DEP_DEPTH) {
        snprintf(err, err_cap, "Dependency chain too deep");
        return -1;
    }

    const WlJsonVal *m = hub_find_mod_in_index(index_root, id);
    if (!m) {
        *out_http_status = 404;
        snprintf(err, err_cap, is_dep
                 ? "Missing dependency '%s' in hub index"
                 : "Mod '%s' not found in hub index", id);
        return -1;
    }

    const char *version  = wl_json_str(wl_json_get(m, "version"), "0.1");
    const char *dl_raw   = wl_json_str(wl_json_get(m, "download"), "");
    const char *sha_exp  = wl_json_str(wl_json_get(m, "sha256"), "");
    const char *req_wl   = wl_json_str(wl_json_get(m, "requires_wololoader"), "");
    const WlJsonVal *v_sz = wl_json_get(m, "size");
    int64_t expected_sz  = (v_sz && v_sz->type == WL_JSON_NUMBER) ? v_sz->u.num.ival : 0;

    if (!dl_raw[0]) {
        snprintf(err, err_cap, "Mod '%s' has no download URL in index", id);
        return -1;
    }
    if (!hub_is_valid_sha256_hex(sha_exp)) {
        snprintf(err, err_cap, "Mod '%s' has missing or invalid sha256 in index", id);
        return -1;
    }
    if (expected_sz <= 0 || expected_sz > (int64_t)HUB_MAX_ZIP_BYTES) {
        snprintf(err, err_cap, "Mod '%s' has invalid size in index", id);
        return -1;
    }
    if (req_wl && req_wl[0] && wololoader_version &&
        hub_compare_versions(wololoader_version, req_wl) < 0) {
        snprintf(err, err_cap, "Mod '%s' requires Wololoader >= %s (current: %s)",
                 id, req_wl, wololoader_version);
        return -1;
    }

    HubLocalState local;
    hub_inspect_local_mod(mods_dir, id, &local);
    char target_dir[HUB_PATH_CAP];
    snprintf(target_dir, sizeof(target_dir), "%s/%s", mods_dir, id);
    int dir_exists = hub_path_exists(target_dir);

    if (dir_exists && !local.hub_managed && !overwrite) {
        *out_http_status = 409;
        snprintf(err, err_cap,
                 "Mod '%s' already exists locally and was not installed from Mod Hub", id);
        return -1;
    }

    // Install dependencies first
    snprintf(ctx->stack[ctx->depth], sizeof(ctx->stack[ctx->depth]), "%s", id);
    ctx->depth++;

    const WlJsonVal *v_deps = wl_json_get(m, "depends");
    if (v_deps && v_deps->type == WL_JSON_ARRAY) {
        for (size_t i = 0; i < v_deps->u.arr.count; i++) {
            const char *dep_id = wl_json_str(v_deps->u.arr.items[i], NULL);
            if (!dep_id || !dep_id[0]) continue;
            HubLocalState dep_local;
            hub_inspect_local_mod(mods_dir, dep_id, &dep_local);
            if (!dep_local.installed) {
                hub_logf(log_cb, log_user, "hub: installing dependency '%s' for '%s'", dep_id, id);
                int dep_updated = 0, dep_status = 0;
                char dep_ver[64] = "";
                if (hub_install_single(game_dir, mods_dir, index_root, index_url, dep_id,
                                       0, 1, wololoader_version, inflate_fn, inflate_user,
                                       progress_cb, progress_user, log_cb, log_user, ctx,
                                       &dep_updated, dep_ver, sizeof(dep_ver),
                                       &dep_status, err, err_cap) != 0) {
                    ctx->depth--;
                    *out_http_status = dep_status;
                    return -1;
                }
                if (ctx->installed_dep_count < 64) {
                    size_t dlen = strlen(dep_id);
                    char *ddup = malloc(dlen + 1);
                    if (ddup) {
                        memcpy(ddup, dep_id, dlen + 1);
                        ctx->installed_deps[ctx->installed_dep_count++] = ddup;
                    }
                }
            }
        }
    }
    ctx->depth--;

    char dl_url[HUB_PATH_CAP];
    hub_resolve_rel_url(index_url, dl_raw, dl_url, sizeof(dl_url));

    char wl_dir[HUB_PATH_CAP];
    snprintf(wl_dir, sizeof(wl_dir), "%s/wololoader", game_dir);
    hub_mkdir_p(wl_dir);
    hub_mkdir_p(mods_dir);

    char tmp_zip[HUB_PATH_CAP];
#ifdef _WIN32
    snprintf(tmp_zip, sizeof(tmp_zip), "%s/hub_dl_%lu_%lu.zip",
             wl_dir, (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
#else
    snprintf(tmp_zip, sizeof(tmp_zip), "%s/hub_dl_%lu_%lu.zip",
             wl_dir, (unsigned long)getpid(), (unsigned long)time(NULL));
#endif

    hub_logf(log_cb, log_user, "hub: downloading '%s' v%s from %s", id, version, dl_url);
    FILE *zfp = fopen(tmp_zip, "wb");
    if (!zfp) {
        *out_http_status = 500;
        snprintf(err, err_cap, "Failed creating temporary zip '%s'", tmp_zip);
        return -1;
    }

    size_t dl_bytes = 0;
    if (hub_fetch_stream(dl_url, HUB_MAX_ZIP_BYTES, zfp, NULL, &dl_bytes,
                         progress_cb, progress_user, id, err, err_cap) != 0) {
        fclose(zfp);
        remove(tmp_zip);
        *out_http_status = 502;
        return -1;
    }
    fclose(zfp);

    if ((int64_t)dl_bytes != expected_sz) {
        remove(tmp_zip);
        *out_http_status = 400;
        snprintf(err, err_cap, "Size mismatch for '%s': expected %lld bytes, got %zu bytes",
                 id, (long long)expected_sz, dl_bytes);
        return -1;
    }

    if (progress_cb) progress_cb("verifying", id, dl_bytes, dl_bytes, progress_user);
    char sha_got[65];
    if (hub_sha256_file(tmp_zip, sha_got) != 0) {
        remove(tmp_zip);
        *out_http_status = 500;
        snprintf(err, err_cap, "Failed computing SHA-256 of '%s'", id);
        return -1;
    }
    if (hub_stricmp(sha_got, sha_exp) != 0) {
        remove(tmp_zip);
        *out_http_status = 400;
        snprintf(err, err_cap, "SHA-256 checksum mismatch for '%s'", id);
        return -1;
    }

    char stage_dir[HUB_PATH_CAP];
    snprintf(stage_dir, sizeof(stage_dir), "%s/%s.tmp", mods_dir, id);
    hub_rmdir_recursive(stage_dir);
    if (hub_mkdir_p(stage_dir) != 0) {
        remove(tmp_zip);
        *out_http_status = 500;
        snprintf(err, err_cap, "Failed creating staging folder '%s'", stage_dir);
        return -1;
    }

    if (progress_cb) progress_cb("extracting", id, 0, 0, progress_user);
    hub_logf(log_cb, log_user, "hub: extracting '%s' to %s", id, stage_dir);
    if (hub_extract_zip_safe(tmp_zip, stage_dir, inflate_fn, inflate_user, err, err_cap) != 0) {
        remove(tmp_zip);
        hub_rmdir_recursive(stage_dir);
        *out_http_status = 400;
        return -1;
    }
    remove(tmp_zip);

    char stage_modjson[HUB_PATH_CAP];
    snprintf(stage_modjson, sizeof(stage_modjson), "%s/mod.json", stage_dir);
    if (local.installed && local.has_enabled) {
        if (hub_preserve_enabled_in_modjson(stage_modjson, local.enabled, err, err_cap) != 0) {
            hub_rmdir_recursive(stage_dir);
            *out_http_status = 400;
            return -1;
        }
    } else {
        char *mj_txt = wl_read_file(stage_modjson, NULL);
        WlJsonVal *mj = mj_txt ? wl_json_parse(mj_txt, NULL, 0) : NULL;
        free(mj_txt);
        int ok = (mj && mj->type == WL_JSON_OBJECT);
        wl_json_free(mj);
        if (!ok) {
            hub_rmdir_recursive(stage_dir);
            *out_http_status = 400;
            snprintf(err, err_cap, "Mod archive '%s' has missing or invalid mod.json", id);
            return -1;
        }
    }

    if (hub_write_dot_hub_json(stage_dir, id, index_url, dl_url, version, sha_got, err, err_cap) != 0) {
        hub_rmdir_recursive(stage_dir);
        *out_http_status = 500;
        return -1;
    }

    if (progress_cb) progress_cb("installing", id, 0, 0, progress_user);
    char old_dir[HUB_PATH_CAP];
    snprintf(old_dir, sizeof(old_dir), "%s/%s.old", mods_dir, id);
    hub_rmdir_recursive(old_dir);

    int had_existing = hub_path_exists(target_dir);
    if (had_existing) {
        if (hub_move_path(target_dir, old_dir) != 0) {
            hub_rmdir_recursive(stage_dir);
            *out_http_status = 500;
            snprintf(err, err_cap, "Failed moving previous mod folder aside");
            return -1;
        }
    }

    if (hub_move_path(stage_dir, target_dir) != 0) {
        if (had_existing) hub_move_path(old_dir, target_dir);
        hub_rmdir_recursive(stage_dir);
        *out_http_status = 500;
        snprintf(err, err_cap, "Failed installing staged mod folder");
        return -1;
    }

    if (had_existing) hub_rmdir_recursive(old_dir);

    hub_logf(log_cb, log_user, "hub: %s '%s' v%s", local.installed ? "updated" : "installed", id, version);
    *out_updated = local.installed ? 1 : 0;
    snprintf(out_version, ver_cap, "%s", version);
    *out_http_status = 200;
    return 0;
}

void hub_install_result_free(HubInstallResult *res) {
    if (!res) return;
    for (int i = 0; i < res->installed_dep_count; i++) {
        free(res->installed_deps[i]);
        res->installed_deps[i] = NULL;
    }
    res->installed_dep_count = 0;
}

int hub_install_mod(const char *game_dir, const char *mods_dir,
                    const WlJsonVal *index_root, const char *index_url,
                    const char *id, int overwrite,
                    const char *wololoader_version,
                    HubInflateFn inflate_fn, void *inflate_user,
                    HubProgressFn progress_cb, void *progress_user,
                    HubLogFn log_cb, void *log_user,
                    HubInstallResult *out) {
    memset(out, 0, sizeof(*out));
    out->http_status = 400;

    HubInstallCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    int rc = hub_install_single(game_dir, mods_dir, index_root, index_url, id,
                                overwrite, 0, wololoader_version,
                                inflate_fn, inflate_user,
                                progress_cb, progress_user,
                                log_cb, log_user, &ctx,
                                &out->updated, out->version, sizeof(out->version),
                                &out->http_status, out->err, sizeof(out->err));
    out->installed_dep_count = ctx.installed_dep_count;
    for (int i = 0; i < ctx.installed_dep_count; i++) {
        out->installed_deps[i] = ctx.installed_deps[i];
    }
    if (rc != 0) {
        hub_install_result_free(out);
    }
    return rc;
}

int hub_uninstall_mod(const char *game_dir, const char *mods_dir,
                      const char *id, char *out_trash_path, size_t trash_cap,
                      int *out_http_status, char *err, size_t err_cap) {
    if (out_trash_path && trash_cap > 0) out_trash_path[0] = '\0';
    if (out_http_status) *out_http_status = 400;
    if (!hub_is_valid_id(id)) {
        snprintf(err, err_cap, "Missing or invalid mod id");
        return -1;
    }

    char mod_dir[HUB_PATH_CAP];
    snprintf(mod_dir, sizeof(mod_dir), "%s/%s", mods_dir, id);
    if (!hub_dir_exists(mod_dir)) {
        if (out_http_status) *out_http_status = 404;
        snprintf(err, err_cap, "Mod is not installed");
        return -1;
    }

    char hub_meta[HUB_PATH_CAP];
    snprintf(hub_meta, sizeof(hub_meta), "%s/.hub.json", mod_dir);
    if (!hub_file_exists(hub_meta)) {
        if (out_http_status) *out_http_status = 403;
        snprintf(err, err_cap, "Only mods installed through Mod Hub can be uninstalled here");
        return -1;
    }

    char trash_root[HUB_PATH_CAP];
    snprintf(trash_root, sizeof(trash_root), "%s/wololoader/trash", game_dir);
    if (hub_mkdir_p(trash_root) != 0) {
        if (out_http_status) *out_http_status = 500;
        snprintf(err, err_cap, "Failed creating trash directory");
        return -1;
    }

    time_t now = time(NULL);
    char stamp[32] = "00000000-000000";
#ifdef _WIN32
    struct tm *lt = localtime(&now);
    if (lt) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", lt);
#else
    struct tm lt_tm;
    if (localtime_r(&now, &lt_tm)) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &lt_tm);
#endif

    char trash_dir[HUB_PATH_CAP];
    snprintf(trash_dir, sizeof(trash_dir), "%s/%s-%s", trash_root, id, stamp);
    for (int suffix = 1; hub_path_exists(trash_dir) && suffix < 100; suffix++) {
        snprintf(trash_dir, sizeof(trash_dir), "%s/%s-%s-%d", trash_root, id, stamp, suffix);
    }

    if (hub_move_path(mod_dir, trash_dir) != 0) {
        if (out_http_status) *out_http_status = 500;
        snprintf(err, err_cap, "Failed moving mod to trash");
        return -1;
    }

    if (out_trash_path && trash_cap > 0) {
        snprintf(out_trash_path, trash_cap, "%s", trash_dir);
    }
    if (out_http_status) *out_http_status = 200;
    return 0;
}
