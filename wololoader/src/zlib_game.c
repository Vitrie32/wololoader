// Raw deflate/inflate for datbuild.c, using the game's own zlibwapi.dll.
// mingw has no zlib here, and AoK HD.exe already imports zlibwapi.dll from the
// game folder, so it is always loaded. zlibwapi is zlib 1.2.7 built with
// ZLIB_WINAPI: every export is __stdcall (verified: deflateInit2_ ret 0x20,
// inflate ret 8, deflateEnd/inflateEnd ret 4).
#include "wololoader.h"
#include "datbuild.h"
#include <stdlib.h>
#include <string.h>

// z_stream as laid out by zlib 1.2.x on 32-bit (56 bytes).
typedef struct {
    const unsigned char *next_in; unsigned avail_in; unsigned long total_in;
    unsigned char *next_out; unsigned avail_out; unsigned long total_out;
    const char *msg; void *state;
    void *zalloc; void *zfree; void *opaque;   // NULL = zlib's default allocator
    int data_type; unsigned long adler; unsigned long reserved;
} ZStream;

#define Z_OK 0
#define Z_STREAM_END 1
#define Z_FINISH 4
#define Z_NO_FLUSH 0
#define Z_DEFLATED 8
#define Z_DEFAULT_COMPRESSION (-1)
#define Z_DEFAULT_STRATEGY 0

typedef int (__stdcall *InflateInit2_t)(ZStream *, int window_bits, const char *version, int size);
typedef int (__stdcall *Inflate_t)(ZStream *, int flush);
typedef int (__stdcall *InflateEnd_t)(ZStream *);
typedef int (__stdcall *DeflateInit2_t)(ZStream *, int level, int method, int window_bits,
                                       int mem_level, int strategy, const char *version, int size);
typedef int (__stdcall *Deflate_t)(ZStream *, int flush);
typedef int (__stdcall *DeflateEnd_t)(ZStream *);
typedef unsigned long (__stdcall *DeflateBound_t)(ZStream *, unsigned long source_len);

static struct {
    InflateInit2_t inflate_init2; Inflate_t inflate; InflateEnd_t inflate_end;
    DeflateInit2_t deflate_init2; Deflate_t deflate; DeflateEnd_t deflate_end;
    DeflateBound_t deflate_bound;
} z;

static BOOL load_zlib(void) {
    if (z.inflate) return TRUE;
    HMODULE m = GetModuleHandleA("zlibwapi.dll");
    if (!m) m = LoadLibraryA("zlibwapi.dll");
    if (!m) return FALSE;
    z.inflate_init2 = (InflateInit2_t)GetProcAddress(m, "inflateInit2_");
    z.inflate = (Inflate_t)GetProcAddress(m, "inflate");
    z.inflate_end = (InflateEnd_t)GetProcAddress(m, "inflateEnd");
    z.deflate_init2 = (DeflateInit2_t)GetProcAddress(m, "deflateInit2_");
    z.deflate = (Deflate_t)GetProcAddress(m, "deflate");
    z.deflate_end = (DeflateEnd_t)GetProcAddress(m, "deflateEnd");
    z.deflate_bound = (DeflateBound_t)GetProcAddress(m, "deflateBound");
    return z.inflate_init2 && z.inflate && z.inflate_end && z.deflate_init2 &&
           z.deflate && z.deflate_end && z.deflate_bound;
}

// window bits -15 = raw deflate stream, no zlib header (that is the dat format).
static int inflate_raw(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    if (!load_zlib()) return -1;
    ZStream s;
    memset(&s, 0, sizeof s);
    if (z.inflate_init2(&s, -15, "1.2.7", sizeof s) != Z_OK) return -1;
    size_t cap = in_len * 8 + 1024, len = 0;
    unsigned char *buf = malloc(cap);
    s.next_in = in;
    s.avail_in = (unsigned)in_len;
    int rc = Z_OK;
    while (buf && rc == Z_OK) {
        if (len == cap) {
            unsigned char *bigger = realloc(buf, cap * 2);
            if (!bigger) { free(buf); buf = NULL; break; }
            buf = bigger;
            cap *= 2;
        }
        s.next_out = buf + len;
        s.avail_out = (unsigned)(cap - len);
        rc = z.inflate(&s, Z_NO_FLUSH);
        len = cap - s.avail_out;
    }
    z.inflate_end(&s);
    if (!buf || rc != Z_STREAM_END) { free(buf); return -1; }
    *out = buf;
    *out_len = len;
    return 0;
}

static int deflate_raw(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    if (!load_zlib()) return -1;
    ZStream s;
    memset(&s, 0, sizeof s);
    if (z.deflate_init2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY,
                        "1.2.7", sizeof s) != Z_OK)
        return -1;
    size_t cap = z.deflate_bound(&s, (unsigned long)in_len) + 64;
    unsigned char *buf = malloc(cap);
    if (!buf) { z.deflate_end(&s); return -1; }
    s.next_in = in;
    s.avail_in = (unsigned)in_len;
    s.next_out = buf;
    s.avail_out = (unsigned)cap;
    int rc = z.deflate(&s, Z_FINISH);
    size_t len = cap - s.avail_out;
    z.deflate_end(&s);
    if (rc != Z_STREAM_END) { free(buf); return -1; }
    *out = buf;
    *out_len = len;
    return 0;
}

const WlZlib *wl_game_zlib(void) {
    static const WlZlib zl = { inflate_raw, deflate_raw };
    return &zl;
}
