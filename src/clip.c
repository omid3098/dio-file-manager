#define DFM_INTERNAL
#include "ops.h"
#include "clip.h"
#include <shellapi.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>

void clip_free(clip_files *f) {
    size_t i;
    for (i = 0; i < f->count; ++i) free(f->paths[i]);
    free(f->paths); memset(f, 0, sizeof(*f));
}
DWORD clip_write(HWND owner, const char *const *paths, size_t count, int cut) {
    size_t i, chars = 1, offset = 0;
    wchar_t **wide;
    HGLOBAL data = NULL, effect = NULL;
    DROPFILES *drop;
    UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
    DWORD error = ERROR_NOT_ENOUGH_MEMORY;
    if (!count || !paths || !format) return ERROR_INVALID_PARAMETER;
    wide = calloc(count, sizeof(*wide));
    if (!wide) return error;
    for (i = 0; i < count; ++i) {
        wide[i] = dfm_wide(paths[i]);
        if (!wide[i]) { error = GetLastError(); goto done; }
        chars += wcslen(wide[i]) + 1;
    }
    data = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
    effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!data || !effect) goto done;
    drop = GlobalLock(data);
    if (!drop) goto done;
    drop->pFiles = sizeof(DROPFILES); drop->fWide = TRUE;
    for (i = 0; i < count; ++i) {
        size_t n = wcslen(wide[i]) + 1;
        memcpy((wchar_t *)(drop + 1) + offset, wide[i], n * sizeof(wchar_t)); offset += n;
    }
    GlobalUnlock(data);
    { DWORD *p = GlobalLock(effect); if (!p) goto done; *p = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY; GlobalUnlock(effect); }
    if (!OpenClipboard(owner)) { error = GetLastError(); goto done; }
    if (!EmptyClipboard()) error = GetLastError();
    else if (!SetClipboardData(CF_HDROP, data)) error = GetLastError();
    else {
        data = NULL;
        if (SetClipboardData(format, effect)) { effect = NULL; error = 0; }
        else error = GetLastError();
    }
    CloseClipboard();
done:
    if (data) GlobalFree(data);
    if (effect) GlobalFree(effect);
    for (i = 0; i < count; ++i) free(wide[i]);
    free(wide); return error;
}
DWORD clip_read(HWND owner, clip_files *f) {
    HDROP drop;
    UINT i, count;
    DWORD error = 0;
    memset(f, 0, sizeof(*f));
    if (!OpenClipboard(owner)) return GetLastError();
    drop = GetClipboardData(CF_HDROP);
    if (!drop) { error = ERROR_NOT_FOUND; goto done; }
    count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    f->paths = calloc(count ? count : 1, sizeof(*f->paths));
    if (!f->paths) { error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    for (i = 0; i < count; ++i) {
        UINT n = DragQueryFileW(drop, i, NULL, 0);
        wchar_t *w = malloc(((size_t)n + 1) * sizeof(wchar_t));
        if (!w) { error = ERROR_NOT_ENOUGH_MEMORY; break; }
        DragQueryFileW(drop, i, w, n + 1);
        f->paths[i] = dfm_utf8(w); free(w);
        if (!f->paths[i]) { error = GetLastError(); break; }
        ++f->count;
    }
    { HGLOBAL h = GetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"));
      if (h && GlobalSize(h) >= sizeof(DWORD)) {
          DWORD *p = GlobalLock(h); if (p) { f->cut = (*p & DROPEFFECT_MOVE) != 0; GlobalUnlock(h); }
      } }
done:
    CloseClipboard(); if (error) clip_free(f); return error;
}
