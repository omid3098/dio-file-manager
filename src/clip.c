#define DFM_INTERNAL
#include "ops.h"
#include "clip.h"
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void clip_free(clip_files *f) {
    size_t i;
    for (i = 0; i < f->count; ++i) free(f->paths[i]);
    free(f->paths); memset(f, 0, sizeof(*f));
}
/* Test mode only (clip_test_handshake): lets a check order its steps exactly instead of guessing timings. */
static wchar_t hs_base[160];
#define HS_WAIT_MS 5000
/* The check arms this by creating two events, <base>-blocked and <base>-released, while its other program holds the
   clipboard. The first refused OpenClipboard then signals "blocked" and waits (at most HS_WAIT_MS) for "released",
   which the other program sets after it has written and closed the clipboard. When "released" does not come in time,
   the app sets <base>-timedout (if the other program created it), so the check fails instead of passing by luck.
   Used once; TRUE when it ran. */
static BOOL hs_run(void) {
    wchar_t name[200];
    HANDLE blocked, released;
    BOOL ran = FALSE;
    wchar_t base[160];
    wcscpy_s(base, 160, hs_base);
    swprintf(name, 200, L"%ls-blocked", hs_base);
    blocked = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    swprintf(name, 200, L"%ls-released", hs_base);
    released = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (blocked && released) {
        hs_base[0] = 0; SetEvent(blocked); ran = TRUE;
        if (WaitForSingleObject(released, HS_WAIT_MS) != WAIT_OBJECT_0) {
            HANDLE timedout;
            swprintf(name, 200, L"%ls-timedout", base);
            timedout = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
            if (timedout) { SetEvent(timedout); CloseHandle(timedout); }
        }
    }
    if (blocked) CloseHandle(blocked);
    if (released) CloseHandle(released);
    return ran;
}
void clip_test_handshake(const char *base) {
    hs_base[0] = 0;
    if (base && base[0] && MultiByteToWideChar(CP_UTF8, 0, base, -1, hs_base, 120) == 0) hs_base[0] = 0;
}
/* Other programs (clipboard history, sync tools, another window) open the clipboard for a few
   milliseconds after each change. Windows then refuses ours, so retry for up to 300 ms. */
BOOL clip_open(HWND owner) {
    ULONGLONG end = GetTickCount64() + 300;
    for (;;) {
        if (OpenClipboard(owner)) return TRUE;
        if (GetLastError() != ERROR_ACCESS_DENIED) return FALSE;
        if (hs_base[0] && hs_run()) end = GetTickCount64() + 300;   /* the 300 ms count from the other program's release */
        if (GetTickCount64() >= end) return FALSE;
        Sleep(1);
    }
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
    if (!clip_open(owner)) { error = GetLastError(); goto done; }
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
    if (!clip_open(owner)) return GetLastError();
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
