// iconcache.c: scaled PNG caching for mod icons using Windows Imaging Component (WIC).
#define COBJMACROS
#include "wololoader.h"
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_CACHED_PATHS 8

static BOOL scale_image_to_png(const char *src_png, const char *dst_path, int size) {
    HRESULT hr_co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL need_uninit = (hr_co == S_OK || hr_co == S_FALSE);
    if (FAILED(hr_co) && hr_co != RPC_E_CHANGED_MODE) {
        wl_log("iconcache: CoInitializeEx failed (0x%08lx)", (unsigned long)hr_co);
        return FALSE;
    }

    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *conv = NULL;
    IWICFormatConverter *pre_conv = NULL;
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame_encode = NULL;
    IPropertyBag2 *props = NULL;
    BYTE *scaled_pixels = NULL;
    BYTE *dst_pixels = NULL;
    BOOL success = FALSE;

    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr)) {
        wl_log("iconcache: CoCreateInstance(WICImagingFactory) failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    wchar_t src_w[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, src_png, -1, src_w, MAX_PATH)) {
        wl_log("iconcache: MultiByteToWideChar failed for '%s'", src_png);
        goto cleanup;
    }

    hr = IWICImagingFactory_CreateDecoderFromFilename(
        factory, src_w, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        wl_log("iconcache: CreateDecoderFromFilename failed for '%s' (0x%08lx)", src_png, (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr)) {
        wl_log("iconcache: GetFrame failed for '%s' (0x%08lx)", src_png, (unsigned long)hr);
        goto cleanup;
    }

    UINT orig_w = 0, orig_h = 0;
    hr = IWICBitmapFrameDecode_GetSize(frame, &orig_w, &orig_h);
    if (FAILED(hr) || orig_w == 0 || orig_h == 0) {
        wl_log("iconcache: invalid dimensions (%u x %u) for '%s'", orig_w, orig_h, src_png);
        goto cleanup;
    }

    UINT scaled_w, scaled_h;
    if (orig_w >= orig_h) {
        scaled_w = (UINT)size;
        scaled_h = (UINT)((orig_h * (unsigned long long)size + orig_w / 2) / orig_w);
        if (scaled_h < 1) scaled_h = 1;
        if (scaled_h > (UINT)size) scaled_h = (UINT)size;
    } else {
        scaled_h = (UINT)size;
        scaled_w = (UINT)((orig_w * (unsigned long long)size + orig_h / 2) / orig_h);
        if (scaled_w < 1) scaled_w = 1;
        if (scaled_w > (UINT)size) scaled_w = (UINT)size;
    }

    hr = IWICImagingFactory_CreateBitmapScaler(factory, &scaler);
    if (FAILED(hr)) {
        wl_log("iconcache: CreateBitmapScaler failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    IWICBitmapSource *scale_src = (IWICBitmapSource *)frame;
    hr = IWICBitmapScaler_Initialize(scaler, scale_src, scaled_w, scaled_h, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) {
        // Some formats (e.g. indexed) cannot be scaled directly; pre-convert to 32bppBGRA
        hr = IWICImagingFactory_CreateFormatConverter(factory, &pre_conv);
        if (SUCCEEDED(hr)) {
            hr = IWICFormatConverter_Initialize(pre_conv, (IWICBitmapSource *)frame,
                                                &GUID_WICPixelFormat32bppBGRA,
                                                WICBitmapDitherTypeNone, NULL, 0.0,
                                                WICBitmapPaletteTypeCustom);
        }
        if (SUCCEEDED(hr)) {
            scale_src = (IWICBitmapSource *)pre_conv;
            hr = IWICBitmapScaler_Initialize(scaler, scale_src, scaled_w, scaled_h, WICBitmapInterpolationModeFant);
        }
        if (FAILED(hr)) {
            wl_log("iconcache: BitmapScaler_Initialize failed (0x%08lx)", (unsigned long)hr);
            goto cleanup;
        }
    }

    hr = IWICImagingFactory_CreateFormatConverter(factory, &conv);
    if (FAILED(hr)) {
        wl_log("iconcache: CreateFormatConverter failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)scaler,
                                        &GUID_WICPixelFormat32bppBGRA,
                                        WICBitmapDitherTypeNone, NULL, 0.0,
                                        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        wl_log("iconcache: FormatConverter_Initialize failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    UINT scaled_stride = scaled_w * 4;
    UINT scaled_bytes = scaled_stride * scaled_h;
    scaled_pixels = (BYTE *)malloc(scaled_bytes);
    if (!scaled_pixels) {
        wl_log("iconcache: out of memory for scaled pixels");
        goto cleanup;
    }

    hr = IWICFormatConverter_CopyPixels(conv, NULL, scaled_stride, scaled_bytes, scaled_pixels);
    if (FAILED(hr)) {
        wl_log("iconcache: CopyPixels failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    UINT dst_stride = (UINT)size * 4;
    UINT dst_bytes = dst_stride * (UINT)size;
    dst_pixels = (BYTE *)calloc(1, dst_bytes);
    if (!dst_pixels) {
        wl_log("iconcache: out of memory for dst pixels");
        goto cleanup;
    }

    UINT off_x = ((UINT)size - scaled_w) / 2;
    UINT off_y = ((UINT)size - scaled_h) / 2;
    for (UINT row = 0; row < scaled_h; row++) {
        BYTE *src_row = scaled_pixels + row * scaled_stride;
        BYTE *dst_row = dst_pixels + (off_y + row) * dst_stride + off_x * 4;
        memcpy(dst_row, src_row, scaled_w * 4);
    }

    wchar_t dst_w[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, dst_path, -1, dst_w, MAX_PATH)) {
        wl_log("iconcache: MultiByteToWideChar failed for '%s'", dst_path);
        goto cleanup;
    }

    hr = IWICImagingFactory_CreateStream(factory, &stream);
    if (FAILED(hr)) {
        wl_log("iconcache: CreateStream failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICStream_InitializeFromFilename(stream, dst_w, GENERIC_WRITE);
    if (FAILED(hr)) {
        wl_log("iconcache: Stream InitializeFromFilename failed for '%s' (0x%08lx)", dst_path, (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
    if (FAILED(hr)) {
        wl_log("iconcache: CreateEncoder failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        wl_log("iconcache: BitmapEncoder Initialize failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame_encode, &props);
    if (FAILED(hr)) {
        wl_log("iconcache: CreateNewFrame failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICBitmapFrameEncode_Initialize(frame_encode, props);
    if (FAILED(hr)) {
        wl_log("iconcache: FrameEncode Initialize failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICBitmapFrameEncode_SetSize(frame_encode, (UINT)size, (UINT)size);
    if (FAILED(hr)) {
        wl_log("iconcache: FrameEncode SetSize failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    WICPixelFormatGUID pfg = GUID_WICPixelFormat32bppBGRA;
    hr = IWICBitmapFrameEncode_SetPixelFormat(frame_encode, &pfg);
    if (FAILED(hr)) {
        wl_log("iconcache: FrameEncode SetPixelFormat failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    if (IsEqualGUID(&pfg, &GUID_WICPixelFormat32bppRGBA)) {
        for (UINT i = 0; i < dst_bytes; i += 4) {
            BYTE tmp = dst_pixels[i];
            dst_pixels[i] = dst_pixels[i + 2];
            dst_pixels[i + 2] = tmp;
        }
    }

    hr = IWICBitmapFrameEncode_WritePixels(frame_encode, (UINT)size, dst_stride, dst_bytes, dst_pixels);
    if (FAILED(hr)) {
        wl_log("iconcache: FrameEncode WritePixels failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICBitmapFrameEncode_Commit(frame_encode);
    if (FAILED(hr)) {
        wl_log("iconcache: FrameEncode Commit failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    hr = IWICBitmapEncoder_Commit(encoder);
    if (FAILED(hr)) {
        wl_log("iconcache: BitmapEncoder Commit failed (0x%08lx)", (unsigned long)hr);
        goto cleanup;
    }

    success = TRUE;

cleanup:
    if (dst_pixels) free(dst_pixels);
    if (scaled_pixels) free(scaled_pixels);
    if (props) IPropertyBag2_Release(props);
    if (frame_encode) IWICBitmapFrameEncode_Release(frame_encode);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IWICStream_Release(stream);
    if (conv) IWICFormatConverter_Release(conv);
    if (scaler) IWICBitmapScaler_Release(scaler);
    if (pre_conv) IWICFormatConverter_Release(pre_conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    if (need_uninit) CoUninitialize();

    return success;
}

const char *wl_icon_scaled(const char *src_png, const char *mod_id, int size) {
    if (!src_png || !src_png[0] || !mod_id || !mod_id[0] || size <= 0 || !g_wl.root[0]) {
        return src_png;
    }

    static char s_out_paths[NUM_CACHED_PATHS][MAX_PATH + 64];
    static unsigned int s_out_idx = 0;

    char *dst_path = s_out_paths[(s_out_idx++) & (NUM_CACHED_PATHS - 1)];
    snprintf(dst_path, MAX_PATH + 64, "%s\\cache\\icons\\%s_%d.png", g_wl.root, mod_id, size);

    WIN32_FILE_ATTRIBUTE_DATA src_attr;
    if (!GetFileAttributesExA(src_png, GetFileExInfoStandard, &src_attr)) {
        wl_log("iconcache: source '%s' not accessible", src_png);
        return src_png;
    }

    WIN32_FILE_ATTRIBUTE_DATA dst_attr;
    if (GetFileAttributesExA(dst_path, GetFileExInfoStandard, &dst_attr)) {
        if (CompareFileTime(&dst_attr.ftLastWriteTime, &src_attr.ftLastWriteTime) >= 0) {
            return dst_path;
        }
    }

    char dir[MAX_PATH + 32];
    snprintf(dir, sizeof dir, "%s\\cache", g_wl.root);
    CreateDirectoryA(dir, NULL);
    snprintf(dir, sizeof dir, "%s\\cache\\icons", g_wl.root);
    CreateDirectoryA(dir, NULL);

    if (!scale_image_to_png(src_png, dst_path, size)) {
        DeleteFileA(dst_path);
        return src_png;
    }

    return dst_path;
}
