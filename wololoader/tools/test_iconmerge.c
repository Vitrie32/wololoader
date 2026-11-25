// Native Linux test for 32-bit SLP frame encoding (src/iconmerge.c).
// Build & run from wololoader/:
//   gcc -O2 -Wall -Wextra -std=c11 -Isrc -o /tmp/test_iconmerge tools/test_iconmerge.c src/iconmerge.c -lm && /tmp/test_iconmerge
#include "iconmerge.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL line %d: %s\n", __LINE__, #cond); \
        g_fail = 1; \
    } \
} while (0)

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int16_t rd_i16(const uint8_t *p) {
    return (int16_t)rd_u16(p);
}

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct {
    int w, h;
    uint32_t props;
    uint32_t pal_off;
    // Decoded premultiplied RGBA (r, g, b premultiplied by a/255; a = 0..255).
    uint8_t *pm_rgba;
    // Opcode counters for verification.
    int n_opaque_short;
    int n_opaque_big;
    int n_fill;
    int n_skip_short;
    int n_skip_big;
    int n_alpha;
    int n_empty_rows;
    int n_alpha_pixels;
    int n_opaque_pixels;
    int n_premul_overflow;
} DecodedFrame;

// Independent 32-bit SLP 2.0N decoder matching AoK HD.exe TShape::draw_32bpp (0x459f50)
// and row_32bpp_unclipped / row_32bpp_clipped (0x458bc0 / 0x4590b0).
static int decode_slp32_frame(const uint8_t *slp, size_t len, int frame_idx, DecodedFrame *out) {
    memset(out, 0, sizeof *out);
    if (len < 32 || memcmp(slp, "2.0N", 4) != 0) return -1;
    uint32_t nframes = rd_u32(slp + 4);
    if (frame_idx < 0 || (uint32_t)frame_idx >= nframes) return -2;
    size_t info_off = 32u + (size_t)frame_idx * 32u;
    if (info_off + 32u > len) return -3;

    uint32_t cmd_off = rd_u32(slp + info_off + 0);
    uint32_t out_off = rd_u32(slp + info_off + 4);
    uint32_t pal_off = rd_u32(slp + info_off + 8);
    uint32_t props   = rd_u32(slp + info_off + 12);
    int w            = (int)rd_u32(slp + info_off + 16);
    int h            = (int)rd_u32(slp + info_off + 20);
    if ((props & 7u) != 7u) return -4;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -5;
    if ((size_t)out_off + (size_t)h * 4u > len) return -6;
    if ((size_t)cmd_off + (size_t)h * 4u > len) return -7;

    uint8_t *pm = calloc((size_t)w * (size_t)h, 4);
    if (!pm) return -8;

    out->w = w;
    out->h = h;
    out->props = props;
    out->pal_off = pal_off;
    out->pm_rgba = pm;

    for (int y = 0; y < h; y++) {
        int16_t lp = rd_i16(slp + out_off + (size_t)y * 4u);
        int16_t rp = rd_i16(slp + out_off + (size_t)y * 4u + 2u);
        uint32_t ro = rd_u32(slp + cmd_off + (size_t)y * 4u);
        if (lp < 0) {
            out->n_empty_rows++;
            continue;
        }
        if (rp < 0 || lp + rp > w || ro >= len) { free(pm); out->pm_rgba = NULL; return -9; }

        size_t p = ro;
        int x = lp;
        int row_end = w - rp;
        int saw_eol = 0;

        while (p < len) {
            uint8_t cmd = slp[p++];
            uint8_t lo = cmd & 0x0fu;
            if (cmd == 0x0f) {
                saw_eol = 1;
                break;
            } else if ((cmd & 3u) == 0u) {
                int cnt = cmd >> 2;
                if (x + cnt > row_end || p + (size_t)cnt * 4u > len) { free(pm); out->pm_rgba = NULL; return -10; }
                out->n_opaque_short++;
                out->n_opaque_pixels += cnt;
                for (int i = 0; i < cnt; i++, x++) {
                    uint8_t *dst = pm + ((size_t)y * (size_t)w + (size_t)x) * 4u;
                    dst[2] = slp[p + 0]; // B
                    dst[1] = slp[p + 1]; // G
                    dst[0] = slp[p + 2]; // R
                    dst[3] = 255;        // game masks & 0xffffff (inv_alpha = 0 -> alpha = 255)
                    p += 4;
                }
            } else if ((cmd & 3u) == 1u) {
                int cnt = cmd >> 2;
                if (x + cnt > row_end) { free(pm); out->pm_rgba = NULL; return -11; }
                out->n_skip_short++;
                x += cnt;
            } else if (lo == 0x02u) {
                if (p >= len) { free(pm); out->pm_rgba = NULL; return -12; }
                int cnt = ((cmd & 0xf0) << 4) | slp[p++];
                if (x + cnt > row_end || p + (size_t)cnt * 4u > len) { free(pm); out->pm_rgba = NULL; return -13; }
                out->n_opaque_big++;
                out->n_opaque_pixels += cnt;
                for (int i = 0; i < cnt; i++, x++) {
                    uint8_t *dst = pm + ((size_t)y * (size_t)w + (size_t)x) * 4u;
                    dst[2] = slp[p + 0];
                    dst[1] = slp[p + 1];
                    dst[0] = slp[p + 2];
                    dst[3] = 255;
                    p += 4;
                }
            } else if (lo == 0x03u) {
                if (p >= len) { free(pm); out->pm_rgba = NULL; return -14; }
                int cnt = ((cmd & 0xf0) << 4) | slp[p++];
                if (x + cnt > row_end) { free(pm); out->pm_rgba = NULL; return -15; }
                out->n_skip_big++;
                x += cnt;
            } else if (lo == 0x07u) {
                int cnt = cmd >> 4;
                if (cnt == 0) {
                    if (p >= len) { free(pm); out->pm_rgba = NULL; return -16; }
                    cnt = slp[p++];
                }
                if (x + cnt > row_end || p + 4u > len) { free(pm); out->pm_rgba = NULL; return -17; }
                uint8_t b = slp[p + 0], g = slp[p + 1], r = slp[p + 2];
                p += 4;
                out->n_fill++;
                out->n_opaque_pixels += cnt;
                for (int i = 0; i < cnt; i++, x++) {
                    uint8_t *dst = pm + ((size_t)y * (size_t)w + (size_t)x) * 4u;
                    dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 255;
                }
            } else if (cmd == 0x9eu) {
                if (p >= len) { free(pm); out->pm_rgba = NULL; return -18; }
                int cnt = slp[p++];
                if (x + cnt > row_end || p + (size_t)cnt * 4u > len) { free(pm); out->pm_rgba = NULL; return -19; }
                out->n_alpha++;
                out->n_alpha_pixels += cnt;
                for (int i = 0; i < cnt; i++, x++) {
                    uint8_t pb = slp[p + 0];
                    uint8_t pg = slp[p + 1];
                    uint8_t pr = slp[p + 2];
                    uint8_t inv_a = slp[p + 3];
                    uint8_t a = (uint8_t)(255u - inv_a);
                    if (pb > a || pg > a || pr > a) out->n_premul_overflow++;
                    uint8_t *dst = pm + ((size_t)y * (size_t)w + (size_t)x) * 4u;
                    dst[0] = pr; dst[1] = pg; dst[2] = pb; dst[3] = a;
                    p += 4;
                }
            } else {
                free(pm); out->pm_rgba = NULL;
                return -20;
            }
        }
        if (!saw_eol || x != row_end) {
            free(pm); out->pm_rgba = NULL;
            return -21;
        }
    }
    return 0;
}

static int iabs_diff(int a, int b) { return a > b ? a - b : b - a; }

static void verify_roundtrip(const char *label, const uint8_t *rgba, int w, int h,
                             DecodedFrame *df_out) {
    long slp_len = 0;
    unsigned char *slp = wl_icon_encode_slp32(rgba, w, h, &slp_len);
    CHECK(slp != NULL && slp_len > 64);
    if (!slp) return;

    CHECK(memcmp(slp, "2.0N", 4) == 0);
    CHECK(rd_u32(slp + 4) == 1);
    CHECK(rd_u32(slp + 32 + 8) == 0);
    CHECK(rd_u32(slp + 32 + 12) == 0x07);
    CHECK((int)rd_u32(slp + 32 + 16) == w);
    CHECK((int)rd_u32(slp + 32 + 20) == h);

    DecodedFrame df;
    int rc = decode_slp32_frame(slp, (size_t)slp_len, 0, &df);
    CHECK(rc == 0);
    if (rc == 0) {
        CHECK(df.w == w && df.h == h);
        CHECK(df.n_premul_overflow == 0);

        int max_err = 0;
        int max_blend_err = 0;
        int mismatches = 0;

        for (int i = 0; i < w * h; i++) {
            const uint8_t *s = rgba + (size_t)i * 4u;
            const uint8_t *d = df.pm_rgba + (size_t)i * 4u;
            if (d[3] != s[3]) { mismatches++; continue; }

            if (s[3] == 0) {
                if (d[0] != 0 || d[1] != 0 || d[2] != 0) mismatches++;
            } else if (s[3] == 255) {
                if (d[0] != s[0] || d[1] != s[1] || d[2] != s[2]) mismatches++;
            } else {
                for (int c = 0; c < 3; c++) {
                    int exp_floor = (s[c] * s[3]) / 255;
                    int exp_round = (s[c] * s[3] + 127) / 255;
                    int e_r = iabs_diff(d[c], exp_round);
                    int e_f = iabs_diff(d[c], exp_floor);
                    if (e_f > max_err) max_err = e_f;
                    if (e_r > 1 || e_f > 1) mismatches++;

                    // Simulate AoK HD.exe 0x458bc0 blend over white (255) and mid-grey (128).
                    int inv_a = 255 - d[3];
                    int bgs[2] = { 255, 128 };
                    for (int bi = 0; bi < 2; bi++) {
                        int game_out = (bgs[bi] * inv_a) / 255 + d[c];
                        double ideal = (bgs[bi] * (255.0 - s[3]) + s[c] * (double)s[3]) / 255.0;
                        int be = iabs_diff(game_out, (int)lround(ideal));
                        if (game_out > 255) mismatches++;
                        if (be > max_blend_err) max_blend_err = be;
                        if (be > 1) mismatches++;
                    }
                }
            }
        }
        CHECK(mismatches == 0);
        printf("PASS %-22s: %dx%d (%ld B), ops{00:%d 02:%d 01:%d 03:%d 9e:%d empty:%d}, max_pm_err=%d, max_blend_err=%d\n",
               label, w, h, slp_len,
               df.n_opaque_short, df.n_opaque_big, df.n_skip_short, df.n_skip_big,
               df.n_alpha, df.n_empty_rows, max_err, max_blend_err);
        if (df_out) {
            *df_out = df;
            df_out->pm_rgba = NULL;
        }
        free(df.pm_rgba);
    }
    free(slp);
}

static void test_36x36_icon(void) {
    const int W = 36, H = 36;
    uint8_t *rgba = calloc((size_t)W * (size_t)H, 4);
    CHECK(rgba != NULL);
    if (!rgba) return;

    // Rows 0..1 and 34..35: completely transparent (empty row 0x8000).
    for (int y = 2; y < 34; y++) {
        for (int x = 2; x < 34; x++) {
            uint8_t *p = rgba + ((size_t)y * (size_t)W + (size_t)x) * 4u;
            // Interior transparent hole in the center (exercises 0x01 skip).
            if (y >= 14 && y < 22 && x >= 14 && x < 22) continue;

            if (y < 10) {
                // Full 24-bit colour opaque gradient (no palette quantization).
                p[0] = (uint8_t)((x * 7 + y * 13) & 0xff);
                p[1] = (uint8_t)((x * 11 + y * 5) & 0xff);
                p[2] = (uint8_t)((255 - x * 6 + y * 3) & 0xff);
                p[3] = 255;
            } else if (y < 26) {
                // Semi-transparent ramp covering alpha 1..254 with vibrant colours.
                int idx = (y - 10) * 32 + (x - 2);
                p[0] = (uint8_t)((idx * 17) & 0xff);
                p[1] = (uint8_t)((idx * 31 + 40) & 0xff);
                p[2] = (uint8_t)((idx * 53 + 90) & 0xff);
                p[3] = (uint8_t)(1 + (idx % 254));
            } else {
                // Alternating opaque, semi-transparent, and transparent on the same row.
                int m = (x + y) % 3;
                if (m == 0) {
                    p[0] = 240; p[1] = 17; p[2] = 199; p[3] = 255;
                } else if (m == 1) {
                    p[0] = 255; p[1] = 200; p[2] = 40; p[3] = (uint8_t)(10 + (x * 7) % 240);
                } // m == 2 stays transparent (0)
            }
        }
    }

    DecodedFrame stats;
    verify_roundtrip("36x36_mixed_icon", rgba, W, H, &stats);
    CHECK(stats.n_empty_rows == 4);
    CHECK(stats.n_opaque_short > 0);
    CHECK(stats.n_skip_short > 0);
    CHECK(stats.n_alpha > 0);
    free(rgba);
}

static void test_wide_runs(void) {
    const int W = 320, H = 5;
    uint8_t *rgba = calloc((size_t)W * (size_t)H, 4);
    CHECK(rgba != NULL);
    if (!rgba) return;

    // Row 0: 320 opaque pixels (exercises 0x02 big copy).
    for (int x = 0; x < W; x++) {
        uint8_t *p = rgba + (size_t)x * 4u;
        p[0] = (uint8_t)(x & 0xff);
        p[1] = (uint8_t)((x * 3) & 0xff);
        p[2] = (uint8_t)(255 - (x & 0xff));
        p[3] = 255;
    }
    // Row 1: 1 opaque, 100 transparent (0x03 big skip), 219 opaque (0x02 big copy).
    rgba[(size_t)W * 4u + 0] = 12; rgba[(size_t)W * 4u + 1] = 34; rgba[(size_t)W * 4u + 2] = 56; rgba[(size_t)W * 4u + 3] = 255;
    for (int x = 101; x < W; x++) {
        uint8_t *p = rgba + ((size_t)W + (size_t)x) * 4u;
        p[0] = 200; p[1] = 100; p[2] = 50; p[3] = 255;
    }
    // Row 2: 300 semi-transparent pixels (splits 0x9e into 255 + 45).
    for (int x = 10; x < 310; x++) {
        uint8_t *p = rgba + ((size_t)2 * (size_t)W + (size_t)x) * 4u;
        p[0] = (uint8_t)((x * 5) & 0xff);
        p[1] = (uint8_t)((x * 9) & 0xff);
        p[2] = 255;
        p[3] = (uint8_t)(1 + (x % 254));
    }
    // Row 3: all transparent (empty row 0x8000).
    // Row 4: boundary alpha values (0, 1, 127, 128, 254, 255) with extreme RGB.
    static const uint8_t alphas[6] = { 0, 1, 127, 128, 254, 255 };
    for (int x = 0; x < 36; x++) {
        uint8_t *p = rgba + ((size_t)4 * (size_t)W + (size_t)x) * 4u;
        p[0] = (x & 1) ? 255 : 0;
        p[1] = (x & 2) ? 255 : 173;
        p[2] = (x & 4) ? 255 : 91;
        p[3] = alphas[x % 6];
    }

    DecodedFrame stats;
    verify_roundtrip("320x5_wide_runs", rgba, W, H, &stats);
    CHECK(stats.n_opaque_big >= 2);
    CHECK(stats.n_skip_big >= 1);
    CHECK(stats.n_alpha >= 2);
    CHECK(stats.n_empty_rows == 1);
    free(rgba);
}

static void test_scale_and_encode(void) {
    // 72x72 soft-edged coloured disc downscaled to 36x36 must preserve smooth semi-transparent alpha.
    const int SW = 72, SH = 72, DW = 36, DH = 36;
    uint8_t *src = calloc((size_t)SW * (size_t)SH, 4);
    CHECK(src != NULL);
    if (!src) return;
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            double dx = (x + 0.5) - SW * 0.5;
            double dy = (y + 0.5) - SH * 0.5;
            double dist = sqrt(dx * dx + dy * dy);
            uint8_t *p = src + ((size_t)y * (size_t)SW + (size_t)x) * 4u;
            if (dist < 28.0) {
                p[0] = 220; p[1] = 60; p[2] = 180; p[3] = 255;
            } else if (dist < 32.0) {
                double t = (32.0 - dist) / 4.0;
                p[0] = 220; p[1] = 60; p[2] = 180;
                p[3] = (uint8_t)(t * 255.0 + 0.5);
            }
        }
    }
    uint8_t *scaled = wl_icon_scale_rgba_fit(src, SW, SH, DW, DH);
    free(src);
    CHECK(scaled != NULL);
    if (!scaled) return;

    DecodedFrame stats;
    verify_roundtrip("72->36_scaled_disc", scaled, DW, DH, &stats);
    CHECK(stats.n_alpha_pixels > 0);
    CHECK(stats.n_opaque_pixels > 0);
    free(scaled);
}

static void test_vanilla_game_b1(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAIL: cannot open vanilla 32-bit SLP: %s\n", path);
        g_fail = 1;
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 64 ? malloc((size_t)n) : NULL;
    CHECK(buf != NULL);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(buf); g_fail = 1; return;
    }
    fclose(f);

    CHECK(memcmp(buf, "2.0N", 4) == 0);
    uint32_t nframes = rd_u32(buf + 4);
    CHECK(nframes >= 1);

    int total_opaque = 0, total_alpha = 0, total_fills = 0;
    for (uint32_t fi = 0; fi < nframes; fi++) {
        DecodedFrame df;
        int rc = decode_slp32_frame(buf, (size_t)n, (int)fi, &df);
        CHECK(rc == 0);
        if (rc == 0) {
            CHECK((df.props & 7u) == 7u);
            total_opaque += df.n_opaque_pixels;
            total_alpha += df.n_alpha_pixels;
            total_fills += df.n_fill;
            if (fi < 5) {
                printf("PASS game_b1.slp frame %u : %dx%d props=0x%x ops{00:%d 02:%d 07:%d 01:%d 03:%d 9e:%d} px{op:%d a:%d}\n",
                       fi, df.w, df.h, df.props,
                       df.n_opaque_short, df.n_opaque_big, df.n_fill,
                       df.n_skip_short, df.n_skip_big, df.n_alpha,
                       df.n_opaque_pixels, df.n_alpha_pixels);
            }
            free(df.pm_rgba);
        }
    }
    CHECK(total_opaque > 0);
    CHECK(total_alpha > 0);
    CHECK(total_fills > 0);
    printf("PASS vanilla game_b1.slp: decoded all %u frames (opaque_px=%d, alpha_px=%d, fill_ops=%d)\n",
           nframes, total_opaque, total_alpha, total_fills);
    free(buf);
}

int main(int argc, char **argv) {
    const char *vanilla_slp = argc > 1
        ? argv[1]
        : "<game dir>/resources/_common/slp/game_b1.slp";

    test_36x36_icon();
    test_wide_runs();
    test_scale_and_encode();
    test_vanilla_game_b1(vanilla_slp);

    if (g_fail) {
        printf("SOME TESTS FAILED\n");
        return 1;
    }
    printf("ALL ICONMERGE 32-BIT SLP TESTS PASSED\n");
    return 0;
}
