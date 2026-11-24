// ==========================================================================
// Scriptorium - Zlib Wrapper Implementation
//
// Purpose:
//   Wraps AoE2 HD's zlibwapi.dll for raw DEFLATE streaming (-15 window bits).
//   AoE2 HD 5.8 dat files are compressed with raw DEFLATE without zlib/gzip
//   headers. zlibwapi.dll is built with ZLIB_WINAPI (__stdcall convention).
// ==========================================================================

#include "sc_zlib.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 32-bit z_stream structure layout (56 bytes on 32-bit x86)
typedef struct {
    const unsigned char *next_in;
    unsigned int         avail_in;
    unsigned long        total_in;
    unsigned char       *next_out;
    unsigned int         avail_out;
    unsigned long        total_out;
    const char          *msg;
    void                *state;
    void                *zalloc;
    void                *zfree;
    void                *opaque;
    int                  data_type;
    unsigned long        adler;
    unsigned long        reserved;
} ScZStream;

#define SC_Z_OK 0
#define SC_Z_STREAM_END 1
#define SC_Z_FINISH 4
#define SC_Z_NO_FLUSH 0
#define SC_Z_DEFLATED 8
#define SC_Z_DEFAULT_COMPRESSION (-1)
#define SC_Z_DEFAULT_STRATEGY 0

typedef int (__stdcall *ScInflateInit2_t)(ScZStream *, int window_bits, const char *version, int size);
typedef int (__stdcall *ScInflate_t)(ScZStream *, int flush);
typedef int (__stdcall *ScInflateEnd_t)(ScZStream *);
typedef int (__stdcall *ScDeflateInit2_t)(ScZStream *, int level, int method, int window_bits,
                                         int mem_level, int strategy, const char *version, int size);
typedef int (__stdcall *ScDeflate_t)(ScZStream *, int flush);
typedef int (__stdcall *ScDeflateEnd_t)(ScZStream *);
typedef unsigned long (__stdcall *ScDeflateBound_t)(ScZStream *, unsigned long source_len);

static struct {
    HMODULE           module;
    ScInflateInit2_t  inflate_init2;
    ScInflate_t       inflate;
    ScInflateEnd_t    inflate_end;
    ScDeflateInit2_t  deflate_init2;
    ScDeflate_t       deflate;
    ScDeflateEnd_t    deflate_end;
    ScDeflateBound_t  deflate_bound;
} s_sc_z;

// Raw inflate with output size cap: window_bits = -15 specifies raw DEFLATE format.
int sc_inflate_raw_bounded(const unsigned char *in, size_t in_len, size_t max_out,
                           unsigned char **out, size_t *out_len) {
    if (!s_sc_z.inflate) return -1;

    ScZStream s;
    memset(&s, 0, sizeof(s));
    if (s_sc_z.inflate_init2(&s, -15, "1.2.7", sizeof(s)) != SC_Z_OK) return -1;

    size_t cap = in_len * 4;
    if (cap < 4096) cap = 4096;
    if (max_out > 0 && cap > max_out) cap = max_out;
    if (max_out == 0) cap = 64;

    size_t len = 0;
    unsigned char *buf = malloc(cap);
    if (!buf) {
        s_sc_z.inflate_end(&s);
        return -1;
    }

    s.next_in = in;
    s.avail_in = (unsigned int)in_len;
    int rc = SC_Z_OK;

    while (buf && rc == SC_Z_OK) {
        if (len == cap) {
            if (len >= max_out) {
                free(buf);
                buf = NULL;
                break;
            }
            size_t next_cap = cap * 2;
            if (next_cap < cap || next_cap > max_out) next_cap = max_out;
            unsigned char *bigger = realloc(buf, next_cap);
            if (!bigger) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = bigger;
            cap = next_cap;
        }
        size_t space = cap - len;
        if (max_out == 0) space = cap; // allow detecting non-empty output when max_out == 0
        s.next_out = buf + len;
        s.avail_out = (unsigned int)space;
        rc = s_sc_z.inflate(&s, SC_Z_NO_FLUSH);
        len = cap - s.avail_out;
        if (len > max_out) {
            free(buf);
            buf = NULL;
            break;
        }
    }

    s_sc_z.inflate_end(&s);
    if (!buf || rc != SC_Z_STREAM_END || len > max_out) {
        free(buf);
        return -1;
    }

    *out = buf;
    *out_len = len;
    return 0;
}

// Raw inflate: window_bits = -15 specifies raw DEFLATE format
static int sc_inflate_raw(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    return sc_inflate_raw_bounded(in, in_len, 256u * 1024u * 1024u, out, out_len);
}

// Raw deflate: window_bits = -15 specifies raw DEFLATE stream without headers
static int sc_deflate_raw(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    if (!s_sc_z.deflate) return -1;

    ScZStream s;
    memset(&s, 0, sizeof(s));
    if (s_sc_z.deflate_init2(&s, SC_Z_DEFAULT_COMPRESSION, SC_Z_DEFLATED, -15, 8,
                             SC_Z_DEFAULT_STRATEGY, "1.2.7", sizeof(s)) != SC_Z_OK) {
        return -1;
    }

    size_t cap = s_sc_z.deflate_bound(&s, (unsigned long)in_len) + 64;
    unsigned char *buf = malloc(cap);
    if (!buf) {
        s_sc_z.deflate_end(&s);
        return -1;
    }

    s.next_in = in;
    s.avail_in = (unsigned int)in_len;
    s.next_out = buf;
    s.avail_out = (unsigned int)cap;

    int rc = s_sc_z.deflate(&s, SC_Z_FINISH);
    size_t len = cap - s.avail_out;
    s_sc_z.deflate_end(&s);

    if (rc != SC_Z_STREAM_END) {
        free(buf);
        return -1;
    }

    *out = buf;
    *out_len = len;
    return 0;
}

static const WlZlib s_sc_wl_zlib = {
    .inflate_raw = sc_inflate_raw,
    .deflate_raw = sc_deflate_raw
};

const WlZlib *sc_zlib_init(const char *game_dir, char *err, size_t errcap) {
    if (s_sc_z.inflate) {
        return &s_sc_wl_zlib;
    }

    HMODULE m = NULL;
    if (game_dir && game_dir[0]) {
        char dll_path[MAX_PATH];
        snprintf(dll_path, sizeof(dll_path), "%s/zlibwapi.dll", game_dir);
        m = LoadLibraryA(dll_path);
        if (!m) {
            snprintf(dll_path, sizeof(dll_path), "%s\\zlibwapi.dll", game_dir);
            m = LoadLibraryA(dll_path);
        }
    }
    if (!m) {
        m = LoadLibraryA("zlibwapi.dll");
    }

    if (!m) {
        if (err && errcap > 0) {
            snprintf(err, errcap, "Failed to load zlibwapi.dll from '%s'", game_dir ? game_dir : ".");
        }
        return NULL;
    }

    s_sc_z.module        = m;
    s_sc_z.inflate_init2 = (ScInflateInit2_t)GetProcAddress(m, "inflateInit2_");
    s_sc_z.inflate       = (ScInflate_t)GetProcAddress(m, "inflate");
    s_sc_z.inflate_end   = (ScInflateEnd_t)GetProcAddress(m, "inflateEnd");
    s_sc_z.deflate_init2 = (ScDeflateInit2_t)GetProcAddress(m, "deflateInit2_");
    s_sc_z.deflate       = (ScDeflate_t)GetProcAddress(m, "deflate");
    s_sc_z.deflate_end   = (ScDeflateEnd_t)GetProcAddress(m, "deflateEnd");
    s_sc_z.deflate_bound = (ScDeflateBound_t)GetProcAddress(m, "deflateBound");

    if (!s_sc_z.inflate_init2 || !s_sc_z.inflate || !s_sc_z.inflate_end ||
        !s_sc_z.deflate_init2 || !s_sc_z.deflate || !s_sc_z.deflate_end ||
        !s_sc_z.deflate_bound) {
        if (err && errcap > 0) {
            snprintf(err, errcap, "zlibwapi.dll is missing required exports");
        }
        FreeLibrary(m);
        memset(&s_sc_z, 0, sizeof(s_sc_z));
        return NULL;
    }

    return &s_sc_wl_zlib;
}
