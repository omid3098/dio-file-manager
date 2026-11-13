#define COBJMACROS
#include "thumbs.h"
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DFM_TEST_WORK
#define DFM_TEST_WORK "E:\\w\\dio-file-manager\\.agent-work\\console"
#endif
#define SANDBOX DFM_TEST_WORK "\\thumbs"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s (Win32 %lu)\n", \
    __LINE__, #c, GetLastError()); exit(1); } } while (0)
#define HR(call) CHECK(SUCCEEDED(call))

static void cleanup(void) {
    char path[1024];
    int i;
    DeleteFileA(SANDBOX "\\sample.png");
    DeleteFileA(SANDBOX "\\sample.jpg");
    DeleteFileA(SANDBOX "\\plain.txt");
    for (i = 0; i < 500; ++i) {
        (void)snprintf(path, sizeof(path), SANDBOX "\\image-%03d.png", i);
        DeleteFileA(path);
    }
    RemoveDirectoryA(SANDBOX);
}

static void wide(const char *path, wchar_t out[1024]) {
    CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, 1024));
}

/* Saturated top and bottom halves catch BGRA swaps and vertically flipped output.
   PNG also catches accidentally treating the Shell bitmap as premultiplied. */
static void write_image(IWICImagingFactory *wic, const char *path, int png) {
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *options = NULL;
    WICPixelFormatGUID format = png ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    UINT stride = 1024 * (png ? 4U : 3U), x, y;
    BYTE *pixels = malloc((size_t)stride * 512);
    wchar_t name[1024];
    CHECK(pixels);
    for (y = 0; y < 512; ++y) for (x = 0; x < 1024; ++x) {
        BYTE *p = pixels + (size_t)y * stride + x * (png ? 4U : 3U);
        p[0] = y < 256 ? 20 : 220;
        p[1] = 40;
        p[2] = y < 256 ? 220 : 20;
        if (png) p[3] = 128;
    }
    wide(path, name);
    HR(IWICImagingFactory_CreateStream(wic, &stream));
    HR(IWICStream_InitializeFromFilename(stream, name, GENERIC_WRITE));
    HR(IWICImagingFactory_CreateEncoder(wic, png ? &GUID_ContainerFormatPng : &GUID_ContainerFormatJpeg,
                                       NULL, &encoder));
    HR(IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache));
    HR(IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &options));
    HR(IWICBitmapFrameEncode_Initialize(frame, options));
    HR(IWICBitmapFrameEncode_SetSize(frame, 1024, 512));
    HR(IWICBitmapFrameEncode_SetPixelFormat(frame, &format));
    CHECK(IsEqualGUID(&format, png ? &GUID_WICPixelFormat32bppBGRA : &GUID_WICPixelFormat24bppBGR));
    HR(IWICBitmapFrameEncode_WritePixels(frame, 512, stride, stride * 512, pixels));
    HR(IWICBitmapFrameEncode_Commit(frame));
    HR(IWICBitmapEncoder_Commit(encoder));
    IPropertyBag2_Release(options);
    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IWICStream_Release(stream);
    free(pixels);
}

static uint64_t modified(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    wchar_t name[1024];
    wide(path, name);
    CHECK(GetFileAttributesExW(name, GetFileExInfoStandard, &data));
    return ((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
}

static thumbs_result wait_image(thumbs_cache *c, const char *path, int size,
                                 uint64_t mtime, thumbs_image *out) {
    ULONGLONG start = GetTickCount64();
    for (;;) {
        thumbs_result result;
        (void)thumbs_request(c, path, size, mtime);
        result = thumbs_get(c, path, size, mtime, out);
        if (result == THUMBS_READY || result == THUMBS_NONE) return result;
        CHECK(GetTickCount64() - start < 15000);
        Sleep(1);
    }
}

static thumbs_stats stats(thumbs_cache *c) {
    thumbs_stats s;
    ULONGLONG start = GetTickCount64();
    while (!thumbs_get_stats(c, &s)) { CHECK(GetTickCount64() - start < 1000); Sleep(1); }
    CHECK(s.bytes <= THUMBS_MEMORY_CAP);
    CHECK(s.queued <= THUMBS_QUEUE_CAP);
    CHECK(s.running <= 1);
    return s;
}

static void check_pixels(const thumbs_image *image, int size, int png) {
    const BYTE *top, *bottom;
    CHECK(image->rgba && image->width == size && image->height == size / 2);
    top = image->rgba + 4 * ((size_t)(image->height / 4) * (size_t)image->width + (size_t)(image->width / 2));
    bottom = image->rgba + 4 * ((size_t)(image->height * 3 / 4) * (size_t)image->width + (size_t)(image->width / 2));
    if (!(top[0] > 190 && top[1] < 65 && top[2] < 65))
        fprintf(stderr, "top RGBA = %u,%u,%u,%u; bottom = %u,%u,%u,%u\n",
                top[0], top[1], top[2], top[3], bottom[0], bottom[1], bottom[2], bottom[3]);
    CHECK(top[0] > 190 && top[1] < 65 && top[2] < 65);
    CHECK(bottom[2] > 190 && bottom[0] < 65);
    CHECK(png ? (top[3] >= 120 && top[3] <= 136) : top[3] == 255);
}

int main(void) {
    IWICImagingFactory *wic = NULL;
    thumbs_cache *c;
    thumbs_image image;
    thumbs_stats s;
    size_t peak = 0;
    char path[1024];
    wchar_t target[1024], source[1024];
    HANDLE text;
    DWORD written;
    uint64_t png_time;
    int i;
    CHECK(CreateDirectoryA(DFM_TEST_WORK, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(CreateDirectoryA(SANDBOX, NULL)); /* Refuse to reuse any pre-existing files. */
    CHECK(atexit(cleanup) == 0);
    HR(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    HR(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                        &IID_IWICImagingFactory, (void **)&wic));
    write_image(wic, SANDBOX "\\sample.png", 1);
    write_image(wic, SANDBOX "\\sample.jpg", 0);
    IWICImagingFactory_Release(wic);
    text = CreateFileA(SANDBOX "\\plain.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(text != INVALID_HANDLE_VALUE);
    CHECK(WriteFile(text, "plain text", 10, &written, NULL) && written == 10);
    CloseHandle(text);
    c = thumbs_create();
    CHECK(c);
    png_time = modified(SANDBOX "\\sample.png");
    CHECK(wait_image(c, SANDBOX "\\sample.png", 64, png_time, &image) == THUMBS_READY);
    check_pixels(&image, 64, 1);
    CHECK(wait_image(c, SANDBOX "\\sample.jpg", 128, modified(SANDBOX "\\sample.jpg"), &image) == THUMBS_READY);
    check_pixels(&image, 128, 0);
    CHECK(wait_image(c, SANDBOX "\\plain.txt", 64, modified(SANDBOX "\\plain.txt"), &image) == THUMBS_NONE);
    CHECK(!image.rgba && !image.width && !image.height);
    CHECK(thumbs_get(c, SANDBOX "\\sample.png", 64, png_time + 1, &image) == THUMBS_MISSING);
    puts("PASS PNG/JPEG dimensions, top-down straight RGBA, text has no thumbnail, mtime key");

    wide(SANDBOX "\\sample.png", source);
    /* Unique names in one folder defeat path cache hits. Decode enough pixels to
       exceed the cache several times, not merely enqueue and drop 500 requests. */
    for (i = 0; i < 500; ++i) {
        CHECK(snprintf(path, sizeof(path), SANDBOX "\\image-%03d.png", i) > 0);
        wide(path, target);
        CHECK(CreateHardLinkW(target, source, NULL));
        CHECK(wait_image(c, path, 512, png_time, &image) == THUMBS_READY);
        check_pixels(&image, 512, 1);
        s = stats(c);
        if (s.bytes > peak) peak = s.bytes;
    }
    CHECK(peak > THUMBS_MEMORY_CAP / 2);
    CHECK(s.cached < 500);
    CHECK(thumbs_get(c, SANDBOX "\\image-000.png", 512, png_time, &image) == THUMBS_MISSING);
    printf("PASS 500 thumbnails, eviction, peak owned/reserved bytes %zu <= %zu\n", peak, THUMBS_MEMORY_CAP);

    for (i = 0; i < 500; ++i) {
        CHECK(snprintf(path, sizeof(path), SANDBOX "\\image-%03d.png", i) > 0);
        (void)thumbs_request(c, path, 1024, png_time);
        (void)stats(c);
    }
    while (!thumbs_drop_unrequested(c)) Sleep(1);
    /* An empty next frame withdraws all queued work; only an active Shell call
       is allowed to finish. No UI call waits for that extraction. */
    while (!thumbs_drop_unrequested(c)) Sleep(1);
    s = stats(c);
    CHECK(s.queued == 0);
    puts("PASS rapid scrolling stays bounded and off-screen requests are dropped");
    thumbs_destroy(c);
    CoUninitialize();
    return 0;
}
