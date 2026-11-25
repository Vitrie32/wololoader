// Extra icons: mods ship single-frame SLPs in <mod>\icons\<sheet>\<name>.slp (sheet = the game's
// interface SLP id, e.g. 50721 = command buttons; "buildings" = all four building icon sheets
// 50705..50708; "units" = 50730) OR PNG command icons in <mod>\icons\commands\<name>.png.
// At start (and Level 2 hot reload) the frames are appended to a copy of the game's sheet in
// <root>\cache\icons\<sheet>.slp and the game is redirected there (wl_add_redirect).
//
// Command PNGs (<mod>\icons\commands\<name>.png, any size) are decoded in pure C (no WIC/COM and
// no zlibwapi.dll, so DllMain stays safe per AGENTS.md rule 5), scaled to 50721.slp's frame size
// (36x36), encoded as a native 1-frame 32-bit SLP 2.0N (properties = 0x07, full colour + 8-bit
// premultiplied alpha via opcode 0x9e, no palette quantization — verified in TShape::draw_32bpp
// 0x459f50 -> 0x458bc0 / 0x4590b0 / 0x457440 and vanilla game_b*.slp / c_logo.slp), and appended
// to 50721.slp after any icons\50721\*.slp frames. The resulting table "<mod>\t<name>\t<frame>"
// is written to <root>\cache\cmdicons.txt and served in-memory via wl_cmd_icon_index(mod, name).
//
// Base sheet lookup checks resources\_common\drs\gamedata_x2\<sheet>.slp first (HD 5.8 loads
// gamedata_x2\50706.slp with 55 frames 0..54, including Aqueduct/Feitoria/Barricade) and falls
// back to resources\_common\drs\interface\<sheet>.slp (e.g. 50721.slp, 70 frames).
//
// New frames get deterministic indices after the base sheet's frames, in sorted mod-id order
// (strcmp) and sorted file-name order (strcmp) inside each mod — matching build.py and datbuild.c
// so custom icon indices agree between the builders and the merged SLP sheet.
// wl_icon_index(sheet, name) returns the index (-1 = not found).
//
// The original frames are not decoded (interface sheets use extended commands): only their
// offsets move. SLP 2.0N: 32-byte header (version, frame count at +4, comment), 32-byte frame
// infos (+0 command table offset, +4 outline table offset, +8 palette offset, +12 properties,
// +16 width, +20 height, +24/+28 hotspot), then data; the command table holds one absolute u32
// per row. Appending m frames moves everything after the header by 32*m.
// Runs from DllMain (plain file IO of our own CRT only).
#include "iconmerge.h"
#ifdef _WIN32
#include "wololoader.h"
#include <stdio.h>
#endif
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ICONS 512
#define MAX_COLLECT 256
#define MAX_PNG_DIM 4096
#define MAX_PNG_DECOMPRESSED (64u * 1024u * 1024u)

#ifdef _WIN32
static struct { char sheet[12]; char name[64]; int index; } g_icons[MAX_ICONS];
static int g_nicons;

static struct { char mod[64]; char name[64]; int index; } g_cmd_icons[MAX_COLLECT];
static int g_ncmd_icons;

int wl_icon_index(const char *sheet, const char *name) {
    for (int i = 0; i < g_nicons; i++)
        if (!_stricmp(g_icons[i].sheet, sheet) && !_stricmp(g_icons[i].name, name)) return g_icons[i].index;
    return -1;
}

int wl_cmd_icon_index(const char *mod, const char *name) {
    if (!mod || !mod[0] || !name || !name[0]) return -1;
    char stem[64];
    size_t len = strlen(name);
    if (len >= 4 && _stricmp(name + len - 4, ".png") == 0) len -= 4;
    if (len == 0 || len >= sizeof stem) return -1;
    memcpy(stem, name, len);
    stem[len] = '\0';
    for (int i = 0; i < g_ncmd_icons; i++) {
        if (strcmp(g_cmd_icons[i].mod, mod) == 0 && _stricmp(g_cmd_icons[i].name, stem) == 0)
            return g_cmd_icons[i].index;
    }
    return -1;
}

static unsigned char *read_all_min(const char *path, long min_len, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = (n >= min_len && n < 16 * 1024 * 1024) ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = n;
    return b;
}

static unsigned char *read_all(const char *path, long *len) {
    return read_all_min(path, 65, len);
}
#endif // _WIN32

// ============================================================================
// Pure-C RFC 1950 / 1951 zlib+deflate decompressor (DllMain-safe, no zlibwapi)
// ============================================================================

typedef struct {
    const uint8_t *in;
    size_t len;
    size_t pos;
    uint32_t bitbuf;
    int bitcnt;
} BitReader;

static int br_bits(BitReader *br, int n) {
    while (br->bitcnt < n) {
        if (br->pos >= br->len) return -1;
        br->bitbuf |= (uint32_t)br->in[br->pos++] << br->bitcnt;
        br->bitcnt += 8;
    }
    int v = (int)(br->bitbuf & ((1u << n) - 1u));
    br->bitbuf >>= n;
    br->bitcnt -= n;
    return v;
}

static int br_bit(BitReader *br) {
    if (br->bitcnt == 0) {
        if (br->pos >= br->len) return -1;
        br->bitbuf = br->in[br->pos++];
        br->bitcnt = 8;
    }
    int v = (int)(br->bitbuf & 1u);
    br->bitbuf >>= 1;
    br->bitcnt--;
    return v;
}

typedef struct {
    uint16_t count[16];
    uint16_t sym[288];
} HuffTree;

static int huff_build(HuffTree *t, const uint8_t *lengths, int nsyms) {
    memset(t->count, 0, sizeof t->count);
    for (int i = 0; i < nsyms; i++) {
        if (lengths[i] > 15) return -1;
        t->count[lengths[i]]++;
    }
    if (t->count[0] == nsyms) return 0; // empty tree (legal for unused distance table)
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= t->count[len];
        if (left < 0) return -1; // over-subscribed
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + t->count[len]);
    for (int i = 0; i < nsyms; i++) {
        if (lengths[i] != 0) t->sym[offs[lengths[i]]++] = (uint16_t)i;
    }
    return 0;
}

static int huff_decode(BitReader *br, const HuffTree *t) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int bit = br_bit(br);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        int cnt = t->count[len];
        if (code - cnt < first) return t->sym[index + (code - first)];
        index += cnt;
        first = (first + cnt) << 1;
    }
    return -1;
}

static int out_ensure(uint8_t **buf, size_t *cap, size_t need, size_t max_out) {
    if (need > max_out) return -1;
    if (need <= *cap) return 0;
    size_t ncap = *cap ? *cap : 4096;
    while (ncap < need) {
        if (ncap > max_out / 2) { ncap = max_out; break; }
        ncap *= 2;
    }
    uint8_t *nb = realloc(*buf, ncap);
    if (!nb) return -1;
    *buf = nb;
    *cap = ncap;
    return 0;
}

static int inflate_zlib(const uint8_t *in, size_t in_len, size_t max_out,
                        uint8_t **out_buf, size_t *out_len) {
    static const uint16_t len_base[29] = {
        3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31,
        35,43,51,59, 67,83,99,115, 131,163,195,227, 258
    };
    static const uint8_t len_extra[29] = {
        0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2,
        3,3,3,3, 4,4,4,4, 5,5,5,5, 0
    };
    static const uint16_t dist_base[30] = {
        1,2,3,4, 5,7,9,13, 17,25,33,49, 65,97,129,193,
        257,385,513,769, 1025,1537,2049,3073, 4097,6145,8193,12289, 16385,24577
    };
    static const uint8_t dist_extra[30] = {
        0,0,0,0, 1,1,2,2, 3,3,4,4, 5,5,6,6,
        7,7,8,8, 9,9,10,10, 11,11,12,12, 13,13
    };
    static const uint8_t cl_order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    if (in_len < 6) return -1;
    uint8_t cmf = in[0], flg = in[1];
    if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7) return -1;
    if ((((unsigned)cmf << 8) | flg) % 31 != 0) return -1;
    if (flg & 0x20) return -1; // preset dictionary not used in PNG

    BitReader br = { in + 2, in_len - 2, 0, 0, 0 };
    uint8_t *out = NULL;
    size_t cap = 0, len = 0;

    int bfinal = 0;
    while (!bfinal) {
        bfinal = br_bit(&br);
        int btype = br_bits(&br, 2);
        if (bfinal < 0 || btype < 0 || btype == 3) goto fail;

        if (btype == 0) {
            int drop = br.bitcnt & 7;
            br.bitbuf >>= drop;
            br.bitcnt -= drop;
            int blen = br_bits(&br, 16);
            int nlen = br_bits(&br, 16);
            if (blen < 0 || nlen < 0 || ((blen ^ 0xffff) != nlen)) goto fail;
            if (br.pos + (size_t)blen > br.len) goto fail;
            if (out_ensure(&out, &cap, len + (size_t)blen, max_out) != 0) goto fail;
            if (blen > 0) {
                memcpy(out + len, br.in + br.pos, (size_t)blen);
                br.pos += (size_t)blen;
                len += (size_t)blen;
            }
            continue;
        }

        HuffTree lit_tree, dist_tree;
        if (btype == 1) {
            uint8_t ll[288], dl[32];
            int i = 0;
            for (; i < 144; i++) ll[i] = 8;
            for (; i < 256; i++) ll[i] = 9;
            for (; i < 280; i++) ll[i] = 7;
            for (; i < 288; i++) ll[i] = 8;
            for (i = 0; i < 32; i++) dl[i] = 5;
            if (huff_build(&lit_tree, ll, 288) != 0 || huff_build(&dist_tree, dl, 32) != 0) goto fail;
        } else {
            int hlit = br_bits(&br, 5);
            int hdist = br_bits(&br, 5);
            int hclen = br_bits(&br, 4);
            if (hlit < 0 || hdist < 0 || hclen < 0) goto fail;
            hlit += 257;
            hdist += 1;
            hclen += 4;
            if (hlit > 286 || hdist > 30) goto fail;

            uint8_t cl_lens[19];
            memset(cl_lens, 0, sizeof cl_lens);
            for (int i = 0; i < hclen; i++) {
                int v = br_bits(&br, 3);
                if (v < 0) goto fail;
                cl_lens[cl_order[i]] = (uint8_t)v;
            }
            HuffTree cl_tree;
            if (huff_build(&cl_tree, cl_lens, 19) != 0) goto fail;

            uint8_t lens[286 + 30];
            int total = hlit + hdist;
            int idx = 0;
            while (idx < total) {
                int sym = huff_decode(&br, &cl_tree);
                if (sym < 0) goto fail;
                if (sym < 16) {
                    lens[idx++] = (uint8_t)sym;
                } else {
                    uint8_t rep_val = 0;
                    int rep_cnt = 0;
                    if (sym == 16) {
                        if (idx == 0) goto fail;
                        int ex = br_bits(&br, 2);
                        if (ex < 0) goto fail;
                        rep_val = lens[idx - 1];
                        rep_cnt = 3 + ex;
                    } else if (sym == 17) {
                        int ex = br_bits(&br, 3);
                        if (ex < 0) goto fail;
                        rep_cnt = 3 + ex;
                    } else if (sym == 18) {
                        int ex = br_bits(&br, 7);
                        if (ex < 0) goto fail;
                        rep_cnt = 11 + ex;
                    } else {
                        goto fail;
                    }
                    if (idx + rep_cnt > total) goto fail;
                    for (int k = 0; k < rep_cnt; k++) lens[idx++] = rep_val;
                }
            }
            if (lens[256] == 0) goto fail; // end-of-block symbol must be present
            if (huff_build(&lit_tree, lens, hlit) != 0 ||
                huff_build(&dist_tree, lens + hlit, hdist) != 0) goto fail;
        }

        for (;;) {
            int sym = huff_decode(&br, &lit_tree);
            if (sym < 0) goto fail;
            if (sym < 256) {
                if (out_ensure(&out, &cap, len + 1, max_out) != 0) goto fail;
                out[len++] = (uint8_t)sym;
            } else if (sym == 256) {
                break;
            } else {
                int li = sym - 257;
                if (li < 0 || li >= 29) goto fail;
                int ex_l = len_extra[li] ? br_bits(&br, len_extra[li]) : 0;
                if (ex_l < 0) goto fail;
                int match_len = len_base[li] + ex_l;

                int di = huff_decode(&br, &dist_tree);
                if (di < 0 || di >= 30) goto fail;
                int ex_d = dist_extra[di] ? br_bits(&br, dist_extra[di]) : 0;
                if (ex_d < 0) goto fail;
                size_t dist = (size_t)(dist_base[di] + ex_d);
                if (dist == 0 || dist > len) goto fail;

                if (out_ensure(&out, &cap, len + (size_t)match_len, max_out) != 0) goto fail;
                for (int k = 0; k < match_len; k++) {
                    out[len] = out[len - dist];
                    len++;
                }
            }
        }
    }

    *out_buf = out;
    *out_len = len;
    return 0;

fail:
    free(out);
    return -1;
}

// ============================================================================
// Pure-C PNG decoder (color types 0/2/3/4/6, bit depths 1/2/4/8/16, Adam7)
// ============================================================================

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int paeth_pred(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int png_unfilter(uint8_t *dst, const uint8_t *src, int h, size_t row_bytes, size_t bpp) {
    for (int y = 0; y < h; y++) {
        uint8_t ftype = src[(size_t)y * (row_bytes + 1)];
        const uint8_t *in_row = src + (size_t)y * (row_bytes + 1) + 1;
        uint8_t *cur = dst + (size_t)y * row_bytes;
        const uint8_t *prev = y > 0 ? dst + (size_t)(y - 1) * row_bytes : NULL;
        for (size_t x = 0; x < row_bytes; x++) {
            uint8_t raw = in_row[x];
            uint8_t a = x >= bpp ? cur[x - bpp] : 0;
            uint8_t b = prev ? prev[x] : 0;
            uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (ftype) {
                case 0: cur[x] = raw; break;
                case 1: cur[x] = (uint8_t)(raw + a); break;
                case 2: cur[x] = (uint8_t)(raw + b); break;
                case 3: cur[x] = (uint8_t)(raw + ((a + b) >> 1)); break;
                case 4: cur[x] = (uint8_t)(raw + paeth_pred(a, b, c)); break;
                default: return -1;
            }
        }
    }
    return 0;
}

typedef struct {
    int w, h;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t pal[256][3];
    int pal_len;
    uint8_t trns[256];
    int trns_len;
    int has_trns_key;
    uint16_t trns_r, trns_g, trns_b;
} PngMeta;

static void png_decode_pass_rgba(const PngMeta *m, const uint8_t *unfilt, int pw, int ph,
                                 size_t row_bytes, int x0, int y0, int dx, int dy, uint8_t *rgba) {
    int bd = m->bit_depth;
    int ct = m->color_type;
    for (int py = 0; py < ph; py++) {
        const uint8_t *row = unfilt + (size_t)py * row_bytes;
        int oy = y0 + py * dy;
        for (int px = 0; px < pw; px++) {
            int ox = x0 + px * dx;
            uint8_t *dst = rgba + ((size_t)oy * (size_t)m->w + (size_t)ox) * 4;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (ct == 3) { // Indexed
                int idx = 0;
                if (bd == 8) {
                    idx = row[px];
                } else {
                    int ppb = 8 / bd;
                    int shift = (ppb - 1 - (px % ppb)) * bd;
                    idx = (row[px / ppb] >> shift) & ((1 << bd) - 1);
                }
                if (idx < m->pal_len) {
                    r = m->pal[idx][0];
                    g = m->pal[idx][1];
                    b = m->pal[idx][2];
                }
                if (idx < m->trns_len) a = m->trns[idx];
            } else if (ct == 0) { // Grayscale
                uint16_t v16 = 0;
                uint8_t v8 = 0;
                if (bd == 16) {
                    v16 = be16(row + px * 2);
                    v8 = (uint8_t)(v16 >> 8);
                } else if (bd == 8) {
                    v16 = row[px];
                    v8 = (uint8_t)v16;
                } else {
                    int ppb = 8 / bd;
                    int shift = (ppb - 1 - (px % ppb)) * bd;
                    int mask = (1 << bd) - 1;
                    v16 = (uint16_t)((row[px / ppb] >> shift) & mask);
                    v8 = (uint8_t)((v16 * 255 + mask / 2) / mask);
                }
                r = g = b = v8;
                if (m->has_trns_key && v16 == m->trns_r) a = 0;
            } else if (ct == 2) { // RGB
                if (bd == 16) {
                    uint16_t rr = be16(row + px * 6);
                    uint16_t gg = be16(row + px * 6 + 2);
                    uint16_t bb = be16(row + px * 6 + 4);
                    r = (uint8_t)(rr >> 8); g = (uint8_t)(gg >> 8); b = (uint8_t)(bb >> 8);
                    if (m->has_trns_key && rr == m->trns_r && gg == m->trns_g && bb == m->trns_b) a = 0;
                } else {
                    r = row[px * 3]; g = row[px * 3 + 1]; b = row[px * 3 + 2];
                    if (m->has_trns_key && r == m->trns_r && g == m->trns_g && b == m->trns_b) a = 0;
                }
            } else if (ct == 4) { // Gray + Alpha
                if (bd == 16) {
                    r = g = b = row[px * 4];
                    a = row[px * 4 + 2];
                } else {
                    r = g = b = row[px * 2];
                    a = row[px * 2 + 1];
                }
            } else if (ct == 6) { // RGBA
                if (bd == 16) {
                    r = row[px * 8]; g = row[px * 8 + 2]; b = row[px * 8 + 4]; a = row[px * 8 + 6];
                } else {
                    r = row[px * 4]; g = row[px * 4 + 1]; b = row[px * 4 + 2]; a = row[px * 4 + 3];
                }
            }
            dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
        }
    }
}

uint8_t *wl_icon_decode_png_rgba(const uint8_t *data, size_t len, int *out_w, int *out_h) {
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (len < 33 || memcmp(data, sig, 8) != 0) return NULL;

    PngMeta m;
    memset(&m, 0, sizeof m);
    memset(m.trns, 255, sizeof m.trns);
    int seen_ihdr = 0, seen_iend = 0, interlace = 0;
    uint8_t *idat = NULL;
    size_t idat_len = 0, idat_cap = 0;

    size_t pos = 8;
    while (pos + 12 <= len) {
        uint32_t clen = be32(data + pos);
        const uint8_t *ctype = data + pos + 4;
        if (clen > len - pos - 12) { free(idat); return NULL; }
        const uint8_t *cdata = data + pos + 8;
        pos += 12 + (size_t)clen;

        if (memcmp(ctype, "IHDR", 4) == 0) {
            if (seen_ihdr || clen != 13) { free(idat); return NULL; }
            uint32_t w = be32(cdata), h = be32(cdata + 4);
            uint8_t bd = cdata[8], ct = cdata[9], comp = cdata[10], filt = cdata[11], il = cdata[12];
            if (w == 0 || h == 0 || w > MAX_PNG_DIM || h > MAX_PNG_DIM) { free(idat); return NULL; }
            if (comp != 0 || filt != 0 || (il != 0 && il != 1)) { free(idat); return NULL; }
            int ok_fmt = 0;
            if (ct == 0 && (bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16)) ok_fmt = 1;
            else if (ct == 2 && (bd == 8 || bd == 16)) ok_fmt = 1;
            else if (ct == 3 && (bd == 1 || bd == 2 || bd == 4 || bd == 8)) ok_fmt = 1;
            else if ((ct == 4 || ct == 6) && (bd == 8 || bd == 16)) ok_fmt = 1;
            if (!ok_fmt) { free(idat); return NULL; }
            m.w = (int)w; m.h = (int)h;
            m.bit_depth = bd; m.color_type = ct;
            interlace = il;
            seen_ihdr = 1;
        } else if (memcmp(ctype, "PLTE", 4) == 0) {
            if (!seen_ihdr || (clen % 3) != 0 || clen > 768) { free(idat); return NULL; }
            m.pal_len = (int)(clen / 3);
            for (int i = 0; i < m.pal_len; i++) {
                m.pal[i][0] = cdata[i * 3];
                m.pal[i][1] = cdata[i * 3 + 1];
                m.pal[i][2] = cdata[i * 3 + 2];
            }
        } else if (memcmp(ctype, "tRNS", 4) == 0) {
            if (!seen_ihdr) { free(idat); return NULL; }
            if (m.color_type == 3) {
                m.trns_len = (int)(clen > 256 ? 256 : clen);
                memcpy(m.trns, cdata, (size_t)m.trns_len);
            } else if (m.color_type == 0 && clen == 2) {
                m.has_trns_key = 1;
                m.trns_r = be16(cdata);
            } else if (m.color_type == 2 && clen == 6) {
                m.has_trns_key = 1;
                m.trns_r = be16(cdata);
                m.trns_g = be16(cdata + 2);
                m.trns_b = be16(cdata + 4);
            }
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            if (!seen_ihdr) { free(idat); return NULL; }
            if (clen > 0) {
                if (idat_len + clen > idat_cap) {
                    size_t ncap = idat_cap ? idat_cap * 2 : 4096;
                    while (ncap < idat_len + clen) ncap *= 2;
                    uint8_t *ni = realloc(idat, ncap);
                    if (!ni) { free(idat); return NULL; }
                    idat = ni; idat_cap = ncap;
                }
                memcpy(idat + idat_len, cdata, clen);
                idat_len += clen;
            }
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            seen_iend = 1;
            break;
        }
    }
    if (!seen_ihdr || !seen_iend || !idat || (m.color_type == 3 && m.pal_len == 0)) {
        free(idat);
        return NULL;
    }

    uint8_t *raw = NULL;
    size_t raw_len = 0;
    int inf_rc = inflate_zlib(idat, idat_len, MAX_PNG_DECOMPRESSED, &raw, &raw_len);
    free(idat);
    if (inf_rc != 0) return NULL;

    int channels = (m.color_type == 0 || m.color_type == 3) ? 1
                 : (m.color_type == 2) ? 3
                 : (m.color_type == 4) ? 2 : 4;
    size_t bpp = ((size_t)channels * m.bit_depth + 7) / 8;

    uint8_t *rgba = calloc((size_t)m.w * (size_t)m.h, 4);
    if (!rgba) { free(raw); return NULL; }

    if (interlace == 0) {
        size_t row_bytes = ((size_t)m.w * (size_t)channels * m.bit_depth + 7) / 8;
        size_t need = (size_t)m.h * (row_bytes + 1);
        uint8_t *unfilt = raw_len >= need ? malloc((size_t)m.h * row_bytes) : NULL;
        if (!unfilt || png_unfilter(unfilt, raw, m.h, row_bytes, bpp) != 0) {
            free(unfilt); free(raw); free(rgba); return NULL;
        }
        png_decode_pass_rgba(&m, unfilt, m.w, m.h, row_bytes, 0, 0, 1, 1, rgba);
        free(unfilt);
    } else {
        static const int x0[7] = { 0, 4, 0, 2, 0, 1, 0 };
        static const int y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
        static const int dx[7] = { 8, 8, 4, 4, 2, 2, 1 };
        static const int dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
        size_t off = 0;
        for (int p = 0; p < 7; p++) {
            if (m.w <= x0[p] || m.h <= y0[p]) continue;
            int pw = (m.w - x0[p] + dx[p] - 1) / dx[p];
            int ph = (m.h - y0[p] + dy[p] - 1) / dy[p];
            size_t row_bytes = ((size_t)pw * (size_t)channels * m.bit_depth + 7) / 8;
            size_t need = (size_t)ph * (row_bytes + 1);
            if (off + need > raw_len) { free(raw); free(rgba); return NULL; }
            uint8_t *unfilt = malloc((size_t)ph * row_bytes);
            if (!unfilt || png_unfilter(unfilt, raw + off, ph, row_bytes, bpp) != 0) {
                free(unfilt); free(raw); free(rgba); return NULL;
            }
            png_decode_pass_rgba(&m, unfilt, pw, ph, row_bytes, x0[p], y0[p], dx[p], dy[p], rgba);
            free(unfilt);
            off += need;
        }
    }

    free(raw);
    *out_w = m.w;
    *out_h = m.h;
    return rgba;
}

// ============================================================================
// Aspect-preserving premultiplied-alpha scaler (box/area downscale, bilinear up)
// ============================================================================

static uint8_t clamp_u8(double v) {
    if (v <= 0.0) return 0;
    if (v >= 255.0) return 255;
    return (uint8_t)(v + 0.5);
}

uint8_t *wl_icon_scale_rgba_fit(const uint8_t *src, int sw, int sh, int dw, int dh) {
    uint8_t *dst = calloc((size_t)dw * (size_t)dh, 4);
    if (!dst) return NULL;
    if (sw == dw && sh == dh) {
        memcpy(dst, src, (size_t)dw * (size_t)dh * 4);
        return dst;
    }

    // Preserve aspect ratio and center inside (dw, dh) with transparent padding.
    int fw, fh;
    if ((long long)sw * dh >= (long long)sh * dw) {
        fw = dw;
        fh = (int)(((long long)sh * dw + sw / 2) / sw);
        if (fh < 1) fh = 1;
        if (fh > dh) fh = dh;
    } else {
        fh = dh;
        fw = (int)(((long long)sw * dh + sh / 2) / sh);
        if (fw < 1) fw = 1;
        if (fw > dw) fw = dw;
    }
    int ox = (dw - fw) / 2;
    int oy = (dh - fh) / 2;

    if (sw >= fw && sh >= fh) {
        // Area averaging in premultiplied alpha avoids dark halos on transparent edges.
        double sx_scale = (double)sw / (double)fw;
        double sy_scale = (double)sh / (double)fh;
        for (int y = 0; y < fh; y++) {
            double y0 = y * sy_scale, y1 = (y + 1) * sy_scale;
            int iy0 = (int)y0, iy1 = (int)ceil(y1);
            if (iy1 > sh) iy1 = sh;
            for (int x = 0; x < fw; x++) {
                double x0 = x * sx_scale, x1 = (x + 1) * sx_scale;
                int ix0 = (int)x0, ix1 = (int)ceil(x1);
                if (ix1 > sw) ix1 = sw;
                double sr = 0.0, sg = 0.0, sb = 0.0, sa = 0.0, area = 0.0;
                for (int sy = iy0; sy < iy1; sy++) {
                    double wy = (sy + 1.0 < y1 ? sy + 1.0 : y1) - (sy > y0 ? sy : y0);
                    if (wy <= 0.0) continue;
                    for (int sx = ix0; sx < ix1; sx++) {
                        double wx = (sx + 1.0 < x1 ? sx + 1.0 : x1) - (sx > x0 ? sx : x0);
                        if (wx <= 0.0) continue;
                        double w = wx * wy;
                        const uint8_t *p = src + ((size_t)sy * (size_t)sw + (size_t)sx) * 4;
                        double a = p[3] / 255.0;
                        sr += p[0] * a * w;
                        sg += p[1] * a * w;
                        sb += p[2] * a * w;
                        sa += a * w;
                        area += w;
                    }
                }
                uint8_t *q = dst + ((size_t)(oy + y) * (size_t)dw + (size_t)(ox + x)) * 4;
                if (sa > 1e-6 && area > 0.0) {
                    q[0] = clamp_u8(sr / sa);
                    q[1] = clamp_u8(sg / sa);
                    q[2] = clamp_u8(sb / sa);
                    q[3] = clamp_u8((sa / area) * 255.0);
                }
            }
        }
    } else {
        // Premultiplied-alpha bilinear for upscaling or mixed-axis scaling.
        for (int y = 0; y < fh; y++) {
            double fy = ((y + 0.5) * sh / (double)fh) - 0.5;
            if (fy < 0.0) fy = 0.0;
            if (fy > sh - 1.0) fy = sh - 1.0;
            int y0 = (int)fy, y1 = y0 + 1 < sh ? y0 + 1 : y0;
            double ty = fy - y0;
            for (int x = 0; x < fw; x++) {
                double fx = ((x + 0.5) * sw / (double)fw) - 0.5;
                if (fx < 0.0) fx = 0.0;
                if (fx > sw - 1.0) fx = sw - 1.0;
                int x0 = (int)fx, x1 = x0 + 1 < sw ? x0 + 1 : x0;
                double tx = fx - x0;
                double ws[4] = { (1.0 - tx) * (1.0 - ty), tx * (1.0 - ty),
                                 (1.0 - tx) * ty, tx * ty };
                const uint8_t *ps[4] = {
                    src + ((size_t)y0 * (size_t)sw + (size_t)x0) * 4,
                    src + ((size_t)y0 * (size_t)sw + (size_t)x1) * 4,
                    src + ((size_t)y1 * (size_t)sw + (size_t)x0) * 4,
                    src + ((size_t)y1 * (size_t)sw + (size_t)x1) * 4
                };
                double sr = 0.0, sg = 0.0, sb = 0.0, sa = 0.0;
                for (int k = 0; k < 4; k++) {
                    double a = ps[k][3] / 255.0;
                    sr += ps[k][0] * a * ws[k];
                    sg += ps[k][1] * a * ws[k];
                    sb += ps[k][2] * a * ws[k];
                    sa += a * ws[k];
                }
                uint8_t *q = dst + ((size_t)(oy + y) * (size_t)dw + (size_t)(ox + x)) * 4;
                if (sa > 1e-6) {
                    q[0] = clamp_u8(sr / sa);
                    q[1] = clamp_u8(sg / sa);
                    q[2] = clamp_u8(sb / sa);
                    q[3] = clamp_u8(sa * 255.0);
                }
            }
        }
    }
    return dst;
}

// ============================================================================
// Single-frame 32-bit SLP 2.0N encoder for UI icons (full colour + alpha)
// ============================================================================
//
// Verified against AoK HD.exe 5.8:
// - TShape::draw_32bpp (0x459f50): checks (frame_info->properties & 7) == 7 per frame;
//   skips rows where outline.left < 0 (0x8000).
// - row_32bpp_unclipped (0x458bc0) & row_32bpp_clipped (0x4590b0 -> 0x457bb0 / 0x459bb0 / 0x457440):
//   * 0x00 (count << 2) / 0x02 (((count >> 8) << 4) | 2, count & 0xff): opaque 32-bit BGRA runs
//     (vanilla game_b*.slp / c_logo.slp store byte 3 = 0xff; both game paths mask & 0xffffff).
//   * 0x01 (count << 2 | 1) / 0x03 (((count >> 8) << 4) | 3, count & 0xff): transparent skip.
//   * 0x9e <u8 count 1..255> <count x u32 premultiplied BGRA with byte 3 = 255 - alpha>:
//     per-pixel alpha blend ((dst * inv_a) / 255 + src in 0x458bc0; >> 8 in 0x457440).
//     Because addition does not saturate, premul_C must be <= alpha; (C * A + 127) / 255 guarantees this.
//   * 0x0f: mandatory end-of-row byte (0x458fdb asserts *p == 0x0f).
// - Never emit player-colour (0x06/0x0a) or shadow (0x0b triggers int3 in 0x4590b0) in 32-bit frames.

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)(v >> 24);
}

unsigned char *wl_icon_encode_slp32(const uint8_t *rgba, int w, int h, long *out_len) {
    if (!rgba || !out_len || w <= 0 || h <= 0 || w > MAX_PNG_DIM || h > MAX_PNG_DIM) return NULL;

    // Worst-case row command bytes: alternating 1-px 0x9e (6 bytes/px) + 1 EOL (<= 6*w + 8).
    size_t max_cmd = (size_t)h * ((size_t)w * 6 + 8);
    uint8_t *cmd_buf = malloc(max_cmd);
    uint16_t *lefts = malloc((size_t)h * sizeof *lefts);
    uint16_t *rights = malloc((size_t)h * sizeof *rights);
    uint32_t *row_offs = malloc((size_t)h * sizeof *row_offs);
    if (!cmd_buf || !lefts || !rights || !row_offs) {
        free(cmd_buf); free(lefts); free(rights); free(row_offs);
        return NULL;
    }

    uint32_t outline_off = 64;
    uint32_t cmdtab_off = outline_off + 4u * (uint32_t)h;
    uint32_t data_base = cmdtab_off + 4u * (uint32_t)h;
    size_t cpos = 0;

    for (int y = 0; y < h; y++) {
        const uint8_t *row = rgba + (size_t)y * (size_t)w * 4;
        row_offs[y] = data_base + (uint32_t)cpos;

        int left = 0;
        while (left < w && row[(size_t)left * 4 + 3] == 0) left++;
        if (left == w) {
            lefts[y] = 0x8000;
            rights[y] = 0x8000;
            cmd_buf[cpos++] = 0x0f;
            continue;
        }
        int right = 0;
        while (row[(size_t)(w - 1 - right) * 4 + 3] == 0) right++;
        lefts[y] = (uint16_t)left;
        rights[y] = (uint16_t)right;

        int x = left, end = w - right;
        while (x < end) {
            uint8_t a0 = row[(size_t)x * 4 + 3];
            if (a0 == 0) {
                int start = x;
                while (x < end && row[(size_t)x * 4 + 3] == 0) x++;
                int n = x - start;
                while (n > 0) {
                    int k = n < 4095 ? n : 4095;
                    if (k < 64) {
                        cmd_buf[cpos++] = (uint8_t)((k << 2) | 1);
                    } else {
                        cmd_buf[cpos++] = (uint8_t)(((k >> 8) << 4) | 3);
                        cmd_buf[cpos++] = (uint8_t)(k & 0xff);
                    }
                    n -= k;
                }
            } else if (a0 == 255) {
                int start = x;
                while (x < end && row[(size_t)x * 4 + 3] == 255) x++;
                int n = x - start;
                int off = 0;
                while (off < n) {
                    int k = (n - off) < 4095 ? (n - off) : 4095;
                    if (k < 64) {
                        cmd_buf[cpos++] = (uint8_t)(k << 2);
                    } else {
                        cmd_buf[cpos++] = (uint8_t)(((k >> 8) << 4) | 2);
                        cmd_buf[cpos++] = (uint8_t)(k & 0xff);
                    }
                    for (int j = 0; j < k; j++) {
                        const uint8_t *p = row + (size_t)(start + off + j) * 4;
                        cmd_buf[cpos++] = p[2]; // B
                        cmd_buf[cpos++] = p[1]; // G
                        cmd_buf[cpos++] = p[0]; // R
                        cmd_buf[cpos++] = 0xff; // matches vanilla game_b*.slp; game masks & 0xffffff
                    }
                    off += k;
                }
            } else {
                int start = x;
                while (x < end) {
                    uint8_t a = row[(size_t)x * 4 + 3];
                    if (a == 0 || a == 255) break;
                    x++;
                }
                int n = x - start;
                int off = 0;
                while (off < n) {
                    int k = (n - off) < 255 ? (n - off) : 255;
                    cmd_buf[cpos++] = 0x9e;
                    cmd_buf[cpos++] = (uint8_t)k;
                    for (int j = 0; j < k; j++) {
                        const uint8_t *p = row + (size_t)(start + off + j) * 4;
                        uint32_t a = p[3];
                        cmd_buf[cpos++] = (uint8_t)((p[2] * a + 127u) / 255u); // premul B
                        cmd_buf[cpos++] = (uint8_t)((p[1] * a + 127u) / 255u); // premul G
                        cmd_buf[cpos++] = (uint8_t)((p[0] * a + 127u) / 255u); // premul R
                        cmd_buf[cpos++] = (uint8_t)(255u - a);                 // inverted alpha
                    }
                    off += k;
                }
            }
        }
        cmd_buf[cpos++] = 0x0f;
    }

    size_t total = data_base + cpos;
    unsigned char *slp = calloc(1, total);
    if (slp) {
        memcpy(slp, "2.0N", 4);
        put_u32(slp + 4, 1);
        memcpy(slp + 8, "Wololoader", 10);
        // Frame info: properties = 0x07 selects native 32-bit BGRA+alpha in TShape::draw_32bpp (0x459f50).
        put_u32(slp + 32 + 0, cmdtab_off);
        put_u32(slp + 32 + 4, outline_off);
        put_u32(slp + 32 + 8, 0);
        put_u32(slp + 32 + 12, 0x07);
        put_u32(slp + 32 + 16, (uint32_t)w);
        put_u32(slp + 32 + 20, (uint32_t)h);
        put_u32(slp + 32 + 24, 0);
        put_u32(slp + 32 + 28, 0);
        for (int y = 0; y < h; y++) {
            put_u16(slp + outline_off + (size_t)y * 4, lefts[y]);
            put_u16(slp + outline_off + (size_t)y * 4 + 2, rights[y]);
            put_u32(slp + cmdtab_off + (size_t)y * 4, row_offs[y]);
        }
        memcpy(slp + data_base, cmd_buf, cpos);
        *out_len = (long)total;
    }

    free(cmd_buf); free(lefts); free(rights); free(row_offs);
    return slp;
}

#ifdef _WIN32
// ============================================================================
// Sheet merging and command PNG collection
// ============================================================================

#define U32(p) (*(unsigned int *)(p))

// Old frame whose data (and command table) already moved by `delta` in the output buffer:
// add `delta` to its two table offsets and to every row entry of its command table.
static int shift_frame(unsigned char *file, long len, unsigned char *info, long delta) {
    unsigned int cmd = U32(info) + (unsigned int)delta, outl = U32(info + 4) + (unsigned int)delta;
    int h = *(int *)(info + 20);
    if (h < 0 || h > 4096 || cmd + 4u * (unsigned)h > (unsigned long)len) return 0;
    for (int y = 0; y < h; y++) U32(file + cmd + 4 * y) += (unsigned int)delta;
    U32(info) = cmd;
    U32(info + 4) = outl;
    return 1;
}

typedef struct {
    char mod[64];
    char name[64];
    unsigned char *data;
    long len;
    int is_cmd_png;
} Frame;

static unsigned char *read_base_sheet(const char *sheet, long *olen, char *src_out, size_t src_cap) {
    // Prefer gamedata_x2 (HD 5.8 building icons 50705..50708 have 55 frames there),
    // falling back to interface (50721.slp).
    snprintf(src_out, src_cap, "%s\\resources\\_common\\drs\\gamedata_x2\\%s.slp", g_wl.game_dir, sheet);
    unsigned char *orig = read_all(src_out, olen);
    if (!orig) {
        snprintf(src_out, src_cap, "%s\\resources\\_common\\drs\\interface\\%s.slp", g_wl.game_dir, sheet);
        orig = read_all(src_out, olen);
    }
    return orig;
}

// Reads frame 0's (width, height) from `sheet` (e.g. 50721.slp -> 36x36), falling back to 36x36.
static void sheet_frame_size(const char *sheet, int *out_w, int *out_h) {
    *out_w = 36;
    *out_h = 36;
    char src[MAX_PATH];
    long olen = 0;
    unsigned char *orig = read_base_sheet(sheet, &olen, src, sizeof src);
    if (orig && olen >= 64 && memcmp(orig, "2.0N", 4) == 0 && *(int *)(orig + 4) > 0) {
        int w = *(int *)(orig + 32 + 16);
        int h = *(int *)(orig + 32 + 20);
        if (w > 0 && w <= 256 && h > 0 && h <= 256) {
            *out_w = w;
            *out_h = h;
        }
    }
    free(orig);
}

static void merge_sheet(const char *sheet, Frame *fr, int n) {
    char src[MAX_PATH], dst[MAX_PATH];
    long olen = 0;
    unsigned char *orig = read_base_sheet(sheet, &olen, src, sizeof src);
    if (!orig || memcmp(orig, "2.0N", 4) != 0) { wl_log("icons: cannot read %s", src); free(orig); return; }
    int count = *(int *)(orig + 4);
    long add = 32L * n, data_len = olen - 32 - 32L * count;
    long total = olen + add;
    for (int i = 0; i < n; i++) total += fr[i].len - 64;
    unsigned char *out = calloc(1, (size_t)total);
    if (!out) { free(orig); return; }
    memcpy(out, orig, 32);
    *(int *)(out + 4) = count + n;
    memcpy(out + 32, orig + 32, 32L * count);
    memcpy(out + 32 + 32L * (count + n), orig + 32 + 32L * count, (size_t)data_len);
    int ok = 1;
    for (int i = 0; i < count && ok; i++)              // old frames: everything moved by `add`
        ok = shift_frame(out, total, out + 32 + 32L * i, add);
    long pos = 32 + 32L * (count + n) + data_len;
    for (int i = 0; i < n && ok; i++) {
        unsigned char *info = out + 32 + 32L * (count + i);
        memcpy(info, fr[i].data + 32, 32);                // the frame's own info (1-frame file)
        memcpy(out + pos, fr[i].data + 64, (size_t)(fr[i].len - 64));
        long delta = pos - 64;                            // its data started at 64 in its file
        unsigned int cmd = U32(info) + (unsigned int)delta;
        int h = *(int *)(info + 20);
        if (h < 0 || h > 4096 || cmd + 4u * (unsigned)h > (unsigned long)total) { ok = 0; break; }
        U32(info) = cmd; U32(info + 4) += (unsigned int)delta;
        for (int y = 0; y < h; y++) U32(out + cmd + 4 * y) += (unsigned int)delta;
        pos += fr[i].len - 64;
        if (g_nicons < MAX_ICONS) {
            lstrcpynA(g_icons[g_nicons].sheet, sheet, sizeof g_icons[0].sheet);
            lstrcpynA(g_icons[g_nicons].name, fr[i].name, sizeof g_icons[0].name);
            g_icons[g_nicons].index = count + i;
            g_nicons++;
        }
        if (fr[i].is_cmd_png && g_ncmd_icons < MAX_COLLECT) {
            lstrcpynA(g_cmd_icons[g_ncmd_icons].mod, fr[i].mod, sizeof g_cmd_icons[0].mod);
            lstrcpynA(g_cmd_icons[g_ncmd_icons].name, fr[i].name, sizeof g_cmd_icons[0].name);
            g_cmd_icons[g_ncmd_icons].index = count + i;
            g_ncmd_icons++;
        }
    }
    free(orig);
    if (!ok) { wl_log("icons: %s has a bad frame table, not merged", sheet); free(out); return; }
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\cache", g_wl.root);
    CreateDirectoryA(dir, NULL);
    snprintf(dir, sizeof dir, "%s\\cache\\icons", g_wl.root);
    CreateDirectoryA(dir, NULL);
    snprintf(dst, sizeof dst, "%s\\%s.slp", dir, sheet);
    FILE *f = fopen(dst, "wb");
    if (f) { fwrite(out, 1, (size_t)total, f); fclose(f); }
    free(out);
    char fname[32];
    snprintf(fname, sizeof fname, "%s.slp", sheet);
    wl_add_redirect(fname, dst);
    wl_log("icons: %s.slp (%d base) + %d icon(s), first new index %d", sheet, count, n, count);
}

static int frame_cmp(const void *a, const void *b) { return strcmp(((const Frame *)a)->name, ((const Frame *)b)->name); }
static int mod_ptr_cmp(const void *a, const void *b) {
    const WlMod *ma = *(const WlMod *const *)a, *mb = *(const WlMod *const *)b;
    return strcmp(ma->id, mb->id);
}

static int sorted_enabled_mods(const WlMod **out) {
    int nmods = 0;
    for (int m = 0; m < g_wl.listed_count && nmods < WL_MAX_MODS; m++) {
        if (g_wl.mods[m].enabled) out[nmods++] = &g_wl.mods[m];
    }
    if (nmods > 1) qsort(out, (size_t)nmods, sizeof *out, mod_ptr_cmp);
    return nmods;
}

// Collects <mod>\icons\<folder>\*.slp of every enabled mod in deterministic sorted mod-id
// and filename order (matching tools/build.py and src/datbuild.c).
static int collect(const char *folder, Frame *fr, int max) {
    const WlMod *sorted_mods[WL_MAX_MODS];
    int nmods = sorted_enabled_mods(sorted_mods);

    int n = 0;
    for (int m = 0; m < nmods && n < max; m++) {
        const WlMod *mod = sorted_mods[m];
        char pat[MAX_PATH];
        snprintf(pat, sizeof pat, "%s\\icons\\%s\\*.slp", mod->dir, folder);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        int first = n;
        do {
            if (n >= max) break;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char path[MAX_PATH];
            snprintf(path, sizeof path, "%s\\icons\\%s\\%s", mod->dir, folder, fd.cFileName);
            long len;
            unsigned char *d = read_all(path, &len);
            int fh = (d && len >= 64) ? *(int *)(d + 32 + 20) : -1;
            if (!d || memcmp(d, "2.0N", 4) != 0 || *(int *)(d + 4) != 1 ||
                U32(d + 32) < 64 || U32(d + 36) < 64 || fh <= 0 || fh > 4096 ||
                U32(d + 32) + 4u * (unsigned)fh > (unsigned long)len) {
                wl_log("icons: %s is not a valid 1-frame SLP, skipped", path); free(d); continue;
            }
            memset(&fr[n], 0, sizeof fr[n]);
            lstrcpynA(fr[n].mod, mod->id, sizeof fr[n].mod);
            lstrcpynA(fr[n].name, fd.cFileName, sizeof fr[n].name);
            char *dot = strrchr(fr[n].name, '.'); if (dot) *dot = 0;
            fr[n].data = d; fr[n].len = len; fr[n].is_cmd_png = 0; n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        if (n - first > 1) qsort(fr + first, (size_t)(n - first), sizeof *fr, frame_cmp);
    }
    return n;
}

typedef struct { char stem[64]; char path[MAX_PATH]; } PngEntry;
static int png_entry_cmp(const void *a, const void *b) {
    return strcmp(((const PngEntry *)a)->stem, ((const PngEntry *)b)->stem);
}

// Collects <mod>\icons\commands\*.png of every enabled mod in deterministic sorted mod-id
// and stem order (matching build.py and datbuild.c), converting each PNG into a 1-frame
// 32-bit SLP (properties = 0x07, full colour + alpha) scaled to (dst_w, dst_h).
static int collect_cmd_pngs(Frame *fr, int max, int dst_w, int dst_h) {
    const WlMod *sorted_mods[WL_MAX_MODS];
    int nmods = sorted_enabled_mods(sorted_mods);

    int n = 0;
    for (int m = 0; m < nmods && n < max; m++) {
        const WlMod *mod = sorted_mods[m];
        char pat[MAX_PATH];
        snprintf(pat, sizeof pat, "%s\\icons\\commands\\*.png", mod->dir);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        PngEntry entries[MAX_COLLECT];
        int ne = 0;
        do {
            if (ne >= MAX_COLLECT) break;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            size_t fl = strlen(fd.cFileName);
            if (fl < 5 || _stricmp(fd.cFileName + fl - 4, ".png") != 0) continue;
            size_t sl = fl - 4;
            if (sl >= sizeof entries[0].stem) sl = sizeof entries[0].stem - 1;
            memcpy(entries[ne].stem, fd.cFileName, sl);
            entries[ne].stem[sl] = '\0';
            snprintf(entries[ne].path, sizeof entries[ne].path, "%s\\icons\\commands\\%s", mod->dir, fd.cFileName);
            ne++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        if (ne == 0) continue;
        if (ne > 1) qsort(entries, (size_t)ne, sizeof *entries, png_entry_cmp);

        for (int i = 0; i < ne && n < max; i++) {
            long plen = 0;
            unsigned char *pbytes = read_all_min(entries[i].path, 33, &plen);
            if (!pbytes) {
                wl_log("icons: cannot read PNG %s, skipped", entries[i].path);
                continue;
            }
            int sw = 0, sh = 0;
            uint8_t *rgba = wl_icon_decode_png_rgba(pbytes, (size_t)plen, &sw, &sh);
            free(pbytes);
            if (!rgba) {
                wl_log("icons: invalid PNG %s, skipped", entries[i].path);
                continue;
            }
            uint8_t *scaled = wl_icon_scale_rgba_fit(rgba, sw, sh, dst_w, dst_h);
            free(rgba);
            if (!scaled) continue;
            long slp_len = 0;
            unsigned char *slp = wl_icon_encode_slp32(scaled, dst_w, dst_h, &slp_len);
            free(scaled);
            if (!slp) continue;

            memset(&fr[n], 0, sizeof fr[n]);
            lstrcpynA(fr[n].mod, mod->id, sizeof fr[n].mod);
            lstrcpynA(fr[n].name, entries[i].stem, sizeof fr[n].name);
            fr[n].data = slp;
            fr[n].len = slp_len;
            fr[n].is_cmd_png = 1;
            n++;
        }
    }
    return n;
}

static void write_cmdicons_txt(void) {
    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\cache", g_wl.root);
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof path, "%s\\cmdicons.txt", dir);
    FILE *f = fopen(path, "wb");
    if (!f) { wl_log("icons: cannot write %s", path); return; }
    for (int i = 0; i < g_ncmd_icons; i++) {
        fprintf(f, "%s\t%s\t%d\n", g_cmd_icons[i].mod, g_cmd_icons[i].name, g_cmd_icons[i].index);
    }
    fclose(f);
    if (g_ncmd_icons > 0)
        wl_log("icons: wrote %d command PNG icon(s) to cache\\cmdicons.txt", g_ncmd_icons);
}

void wl_icons_build(void) {
    g_nicons = 0;
    g_ncmd_icons = 0;
    static Frame fr[MAX_COLLECT];
    static const char *const bldg[] = { "50705", "50706", "50707", "50708" };

    // 50721 (command buttons): existing <mod>\icons\50721\*.slp first, then
    // converted <mod>\icons\commands\*.png frames.
    int n_slp = collect("50721", fr, MAX_COLLECT);
    int fw = 36, fh = 36;
    sheet_frame_size("50721", &fw, &fh);
    int n_png = collect_cmd_pngs(fr + n_slp, MAX_COLLECT - n_slp, fw, fh);
    int n_50721 = n_slp + n_png;
    if (n_50721 > 0) merge_sheet("50721", fr, n_50721);
    for (int i = 0; i < n_50721; i++) free(fr[i].data);
    write_cmdicons_txt();

    int n = collect("buildings", fr, MAX_COLLECT);
    if (n) for (size_t s = 0; s < 4; s++) merge_sheet(bldg[s], fr, n);
    for (int i = 0; i < n; i++) free(fr[i].data);

    n = collect("units", fr, MAX_COLLECT);
    if (n) merge_sheet("50730", fr, n);
    for (int i = 0; i < n; i++) free(fr[i].data);
}
#endif // _WIN32
