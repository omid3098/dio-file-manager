#define COBJMACROS
#include "imgdec.h"
#include "fs.h"
#include <windows.h>
#include <wincodec.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* The GUIDs are written out here so the exe needs no extra import library. */
static const GUID K_CLSID_FACTORY = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID K_IID_FACTORY = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
static const GUID K_FMT_PBGRA = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10 } };
static const GUID K_FMT_RGBA = { 0xf5c7ad2d, 0x6a8d, 0x43dd, { 0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9 } };

struct imgdec_job {
    volatile LONG refs;    /* the window and the thread each hold one */
    volatile LONG state;   /* 0 running, 1 done, -1 failed */
    wchar_t path[32768];
    int box_w, box_h;
    unsigned char *rgba;
    int w, h, src_w, src_h;
    bool has_alpha;
};

static void unref(imgdec_job *j) {
    if (InterlockedDecrement(&j->refs) == 0) { free(j->rgba); free(j); }
}

#define REL(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = NULL; } } while (0)

static bool decode(imgdec_job *j) {
    IWICImagingFactory *fac = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *to_pm = NULL, *to_straight = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICBitmapSource *src;
    UINT sw = 0, sh = 0;
    bool ok = false;
    if (FAILED(CoCreateInstance(&K_CLSID_FACTORY, NULL, CLSCTX_INPROC_SERVER, &K_IID_FACTORY, (void **)&fac))) goto done;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(fac, j->path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &frame))) goto done;
    if (FAILED(IWICBitmapFrameDecode_GetSize(frame, &sw, &sh)) || !sw || !sh || sw > 65536 || sh > 65536) goto done;
    j->src_w = (int)sw; j->src_h = (int)sh;
    double scale = 1.0;
    if (j->box_w > 0 && j->box_h > 0) {
        double a = (double)j->box_w / sw, b = (double)j->box_h / sh;
        scale = a < b ? a : b;
        if (scale > 1.0) scale = 1.0;
    }
    UINT tw = (UINT)floor(sw * scale + 0.5), th = (UINT)floor(sh * scale + 0.5);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    /* Premultiplied while scaling, so the edge of a transparent shape does not darken; straight again for the texture. */
    if (FAILED(IWICImagingFactory_CreateFormatConverter(fac, &to_pm))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(to_pm, (IWICBitmapSource *)frame, &K_FMT_PBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) goto done;
    src = (IWICBitmapSource *)to_pm;
    if (tw != sw || th != sh) {
        if (FAILED(IWICImagingFactory_CreateBitmapScaler(fac, &scaler))) goto done;
        if (FAILED(IWICBitmapScaler_Initialize(scaler, src, tw, th, WICBitmapInterpolationModeFant))) goto done;
        src = (IWICBitmapSource *)scaler;
    }
    if (FAILED(IWICImagingFactory_CreateFormatConverter(fac, &to_straight))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(to_straight, src, &K_FMT_RGBA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) goto done;
    size_t bytes = (size_t)tw * th * 4;
    j->rgba = malloc(bytes);
    if (!j->rgba) goto done;
    if (FAILED(IWICFormatConverter_CopyPixels(to_straight, NULL, tw * 4, (UINT)bytes, j->rgba))) { free(j->rgba); j->rgba = NULL; goto done; }
    j->w = (int)tw; j->h = (int)th;
    for (size_t i = 3; i < bytes; i += 4) if (j->rgba[i] != 255) { j->has_alpha = true; break; }
    ok = true;
done:
    REL(to_straight); REL(scaler); REL(to_pm); REL(frame); REL(dec); REL(fac);
    return ok;
}

static unsigned __stdcall run(void *arg) {
    imgdec_job *j = arg;
    HRESULT com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool ok = SUCCEEDED(com) && decode(j);
    if (SUCCEEDED(com)) CoUninitialize();
    InterlockedExchange(&j->state, ok ? 1 : -1);
    unref(j);
    return 0;
}

imgdec_job *imgdec_start(const char *path_utf8, int box_w, int box_h) {
    imgdec_job *j = calloc(1, sizeof *j);
    if (!j) return NULL;
    if (fs_u8_to_w_long(path_utf8, j->path, 32768) <= 0) { free(j); return NULL; }   /* the long-path form, as every other file call of the app */
    j->box_w = box_w; j->box_h = box_h;
    j->refs = 2;
    uintptr_t t = _beginthreadex(NULL, 0, run, j, 0, NULL);
    if (!t) { free(j); return NULL; }
    SetThreadPriority((HANDLE)t, THREAD_PRIORITY_BELOW_NORMAL);
    CloseHandle((HANDLE)t);
    return j;
}

int imgdec_poll(imgdec_job *j, imgdec_result *out) {
    LONG s = j ? j->state : -1;
    if (s == 1 && out) {
        out->rgba = j->rgba; out->w = j->w; out->h = j->h;
        out->src_w = j->src_w; out->src_h = j->src_h; out->has_alpha = j->has_alpha;
    }
    return (int)s;
}

void imgdec_release(imgdec_job *j) { if (j) unref(j); }
