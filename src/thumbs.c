#define COBJMACROS
#include "thumbs.h"
#include <windows.h>
#include <shobjidl.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_CAP 256
enum { EMPTY, QUEUED, RUNNING, NONE, READY };
typedef struct thumb_entry {
    char *path;
    uint64_t mtime, used, frame;
    size_t bytes, path_bytes;
    thumbs_image image;
    int size, state, cancelled;
} thumb_entry;
struct thumbs_cache {
    CRITICAL_SECTION lock;
    HANDLE wake, thread;
    int stop;
    unsigned queued;
    uint64_t clock, frame;
    size_t bytes;
    thumb_entry entries[ENTRY_CAP];
};

static void discard(thumbs_cache *c, thumb_entry *e) {
    if (e->state == QUEUED) --c->queued;
    c->bytes -= e->bytes + e->path_bytes;
    free((void *)e->image.rgba);
    free(e->path);
    memset(e, 0, sizeof(*e));
}

static thumb_entry *find(thumbs_cache *c, const char *path, int size, uint64_t mtime) {
    int i;
    for (i = 0; i < ENTRY_CAP; ++i) {
        thumb_entry *e = &c->entries[i];
        if (e->state && e->size == size && e->mtime == mtime && !strcmp(e->path, path))
            return e;
    }
    return NULL;
}

static thumb_entry *oldest(thumbs_cache *c, int queued_only) {
    thumb_entry *best = NULL;
    int i;
    for (i = 0; i < ENTRY_CAP; ++i) {
        thumb_entry *e = &c->entries[i];
        if (!e->state || e->state == RUNNING || (queued_only && e->state != QUEUED)) continue;
        if (!best || e->used < best->used) best = e;
    }
    return best;
}

static thumbs_image extract(const wchar_t *path, int size) {
    IShellItemImageFactory *factory = NULL;
    HBITMAP bitmap = NULL;
    BITMAP bm;
    BITMAPINFO info;
    HDC dc;
    SIZE requested = { size, size };
    thumbs_image image = {0};
    unsigned char *pixels = NULL;
    size_t count, i;
    HRESULT hr = SHCreateItemFromParsingName(path, NULL, &IID_IShellItemImageFactory, (void **)&factory);
    if (FAILED(hr)) return image;
    hr = IShellItemImageFactory_GetImage(factory, requested, SIIGBF_THUMBNAILONLY, &bitmap);
    IShellItemImageFactory_Release(factory);
    if (FAILED(hr) || !bitmap) goto done;
    if (!GetObjectW(bitmap, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0 ||
        bm.bmWidth > size || bm.bmHeight > size) goto done;
    count = (size_t)bm.bmWidth * (size_t)bm.bmHeight;
    pixels = malloc(count * 4);
    if (!pixels) goto done;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = bm.bmWidth;
    info.bmiHeader.biHeight = -bm.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(NULL);
    if (!dc) goto done;
    i = (size_t)GetDIBits(dc, bitmap, 0, (UINT)bm.bmHeight, pixels, &info, DIB_RGB_COLORS);
    DeleteDC(dc);
    if (i != (size_t)bm.bmHeight) goto done;
    /* Shell thumbnails expose straight BGRA. Preserve even wholly transparent
       32-bit images; only 24-bit sources need their unused alpha filled in. */
    for (i = 0; i < count; ++i) {
        unsigned char *p = pixels + 4 * i;
        unsigned char blue = p[0];
        p[0] = p[2];
        p[2] = blue;
        if (bm.bmBitsPixel < 32) p[3] = 255;
    }
    image.rgba = pixels;
    image.width = bm.bmWidth;
    image.height = bm.bmHeight;
    pixels = NULL;
done:
    free(pixels);
    if (bitmap) DeleteObject(bitmap);
    return image;
}

static unsigned __stdcall run(void *arg) {
    thumbs_cache *c = arg;
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    for (;;) {
        thumb_entry *e = NULL;
        thumbs_image image = {0};
        wchar_t path[32768];
        int i, valid;
        EnterCriticalSection(&c->lock);
        if (c->stop) { LeaveCriticalSection(&c->lock); break; }
        for (i = 0; i < ENTRY_CAP; ++i) {
            thumb_entry *candidate = &c->entries[i];
            if (candidate->state == QUEUED && (!e || candidate->used > e->used)) e = candidate;
        }
        if (!e) {
            LeaveCriticalSection(&c->lock);
            WaitForSingleObject(c->wake, INFINITE);
            continue;
        }
        e->state = RUNNING;
        --c->queued;
        valid = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, e->path, -1, path, 32768);
        i = e->size;
        LeaveCriticalSection(&c->lock);
        if (valid && SUCCEEDED(com)) image = extract(path, i);
        EnterCriticalSection(&c->lock);
        if (c->stop || e->cancelled) {
            free((void *)image.rgba);
            discard(c, e);
        } else {
            size_t bytes = (size_t)image.width * (size_t)image.height * 4;
            c->bytes -= e->bytes - bytes;
            e->bytes = bytes;
            e->image = image;
            e->state = image.rgba ? READY : NONE;
        }
        LeaveCriticalSection(&c->lock);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

thumbs_cache *thumbs_create(void) {
    thumbs_cache *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    InitializeCriticalSection(&c->lock);
    c->bytes = sizeof(*c);
    c->frame = 1;
    c->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (c->wake) c->thread = (HANDLE)_beginthreadex(NULL, 0, run, c, 0, NULL);
    if (!c->thread) { thumbs_destroy(c); return NULL; }
    return c;
}

void thumbs_destroy(thumbs_cache *c) {
    int i;
    if (!c) return;
    EnterCriticalSection(&c->lock);
    c->stop = 1;
    LeaveCriticalSection(&c->lock);
    if (c->wake) SetEvent(c->wake);
    if (c->thread) { WaitForSingleObject(c->thread, INFINITE); CloseHandle(c->thread); }
    if (c->wake) CloseHandle(c->wake);
    for (i = 0; i < ENTRY_CAP; ++i) if (c->entries[i].state) discard(c, &c->entries[i]);
    DeleteCriticalSection(&c->lock);
    free(c);
}

int thumbs_request(thumbs_cache *c, const char *path, int size, uint64_t mtime) {
    thumb_entry *e;
    size_t len, reservation;
    int i;
    if (!c || !path || !*path || size < 1 || size > 1024) return 0;
    len = strnlen(path, 131069);
    if (len == 131069 || !TryEnterCriticalSection(&c->lock)) return 0;
    e = find(c, path, size, mtime);
    if (e) {
        e->used = ++c->clock;
        e->frame = c->frame;
        e->cancelled = 0;
        LeaveCriticalSection(&c->lock);
        return 1;
    }
    if (c->queued == THUMBS_QUEUE_CAP) discard(c, oldest(c, 1));
    reservation = (size_t)size * (size_t)size * 4;
    for (;;) {
        e = NULL;
        for (i = 0; i < ENTRY_CAP; ++i) if (!c->entries[i].state) { e = &c->entries[i]; break; }
        if (e && c->bytes + reservation + len + 1 <= THUMBS_MEMORY_CAP) break;
        e = oldest(c, 0);
        if (!e) { LeaveCriticalSection(&c->lock); return 0; }
        discard(c, e);
    }
    e->path = malloc(len + 1);
    if (!e->path) { LeaveCriticalSection(&c->lock); return 0; }
    memcpy(e->path, path, len + 1);
    e->path_bytes = len + 1;
    e->bytes = reservation;
    e->mtime = mtime;
    e->size = size;
    e->used = ++c->clock;
    e->frame = c->frame;
    e->state = QUEUED;
    c->bytes += reservation + len + 1;
    ++c->queued;
    LeaveCriticalSection(&c->lock);
    SetEvent(c->wake);
    return 1;
}

thumbs_result thumbs_get(thumbs_cache *c, const char *path, int size, uint64_t mtime, thumbs_image *out) {
    thumb_entry *e;
    thumbs_result result = THUMBS_MISSING;
    if (out) memset(out, 0, sizeof(*out));
    if (!c || !path || !out) return result;
    if (!TryEnterCriticalSection(&c->lock)) return THUMBS_PENDING;
    e = find(c, path, size, mtime);
    if (e) {
        e->used = ++c->clock;
        result = e->state == READY ? THUMBS_READY : e->state == NONE ? THUMBS_NONE : THUMBS_PENDING;
        if (result == THUMBS_READY) *out = e->image;
    }
    LeaveCriticalSection(&c->lock);
    return result;
}

int thumbs_drop_unrequested(thumbs_cache *c) {
    int i;
    if (!c || !TryEnterCriticalSection(&c->lock)) return 0;
    for (i = 0; i < ENTRY_CAP; ++i) {
        thumb_entry *e = &c->entries[i];
        if (e->frame == c->frame) continue;
        if (e->state == QUEUED) discard(c, e);
        else if (e->state == RUNNING) e->cancelled = 1;
    }
    ++c->frame;
    LeaveCriticalSection(&c->lock);
    return 1;
}

int thumbs_get_stats(thumbs_cache *c, thumbs_stats *out) {
    int i;
    if (!c || !out || !TryEnterCriticalSection(&c->lock)) return 0;
    memset(out, 0, sizeof(*out));
    out->bytes = c->bytes;
    out->queued = c->queued;
    for (i = 0; i < ENTRY_CAP; ++i) {
        int state = c->entries[i].state;
        if (state == READY || state == NONE) ++out->cached;
        if (state == RUNNING) ++out->running;
    }
    LeaveCriticalSection(&c->lock);
    return 1;
}
