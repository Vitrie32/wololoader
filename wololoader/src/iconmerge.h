// Pure-C PNG decoding, aspect-preserving scaling, and 32-bit SLP 2.0N encoding
// shared between iconmerge.c (Win32 DLL) and native Linux tests (tools/test_iconmerge.c).
#pragma once
#include <stddef.h>
#include <stdint.h>

uint8_t *wl_icon_decode_png_rgba(const uint8_t *data, size_t len, int *out_w, int *out_h);
uint8_t *wl_icon_scale_rgba_fit(const uint8_t *src, int sw, int sh, int dw, int dh);
unsigned char *wl_icon_encode_slp32(const uint8_t *rgba, int w, int h, long *out_len);
