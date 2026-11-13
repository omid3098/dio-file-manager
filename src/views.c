#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "views.h"
#include "recent.h"
#include "trash.h"
#include "starred.h"

static const char *leaf_of(const char *p) {
    const char *l = strrchr(p, '\\');
    return l && l[1] ? l + 1 : p;
}

static uint64_t ft64(FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static bool stat_path(const char *p, WIN32_FILE_ATTRIBUTE_DATA *d) {
    wchar_t w[FS_PATH_MAX];
    return fs_u8_to_w(p, w, FS_PATH_MAX) && GetFileAttributesExW(w, GetFileExInfoStandard, d);
}

static long fill_recent(fs_list *l) {
    recent_items r = { 0 };
    if (FAILED(recent_list(&r))) { l->error = -1; return FS_FAILED; }
    for (size_t i = 0; i < r.count && !l->abandoned; i++) {
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!stat_path(r.items[i].path, &d)) continue;
        bool dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        fs_list_add(l, leaf_of(r.items[i].path), dir ? FS_DIR : 0, dir ? 0 : ((uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow),
                    ft64(r.items[i].modified), r.items[i].path, NULL);
    }
    recent_free(&r);
    return FS_DONE;
}

static long fill_starred(fs_list *l) {
    char p[FS_PATH_MAX];
    for (int i = 0; starred_get(i, p, sizeof p) && !l->abandoned; i++) {
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!stat_path(p, &d)) continue;   /* gone: not shown, the line stays in the file */
        bool dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        fs_list_add(l, leaf_of(p), dir ? FS_DIR : 0, dir ? 0 : ((uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow),
                    ft64(d.ftLastWriteTime), p, NULL);
    }
    return FS_DONE;
}

static long fill_trash(fs_list *l) {
    trash_items t = { 0 };
    if (FAILED(trash_list(&t))) { l->error = -1; return FS_FAILED; }
    for (size_t i = 0; i < t.count && !l->abandoned; i++) {
        WIN32_FILE_ATTRIBUTE_DATA d;
        bool dir = stat_path(t.items[i].id, &d) && (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        fs_list_add(l, t.items[i].name, dir ? FS_DIR : 0, dir ? 0 : t.items[i].size, ft64(t.items[i].deleted),
                    t.items[i].original_path, t.items[i].id);
    }
    trash_list_free(&t);
    return FS_DONE;
}

long views_fill(fs_list *l) {
    switch (l->vkind) {
    case FS_V_RECENT: return fill_recent(l);
    case FS_V_STARRED: return fill_starred(l);
    case FS_V_TRASH: return fill_trash(l);
    }
    return FS_FAILED;
}

static volatile LONG count_busy;
static volatile LONG64 trash_count;
static HANDLE count_event;

static unsigned __stdcall count_trash(void *unused) {
    (void)unused;
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    /* Keep the shell's per-thread cache: rebuilding it on every poll costs CPU. */
    while (WaitForSingleObject(count_event, INFINITE) == WAIT_OBJECT_0) {
        SHQUERYRBINFO q = { sizeof q };
        if (SUCCEEDED(SHQueryRecycleBinW(NULL, &q))) InterlockedExchange64(&trash_count, q.i64NumItems);
        InterlockedExchange(&count_busy, 0);
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return 0;
}

long long views_trash_count(void) {
    /* This read-only worker and its event live until the process exits. */
    if (!count_event) {
        count_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (count_event) {
            HANDLE th = (HANDLE)_beginthreadex(NULL, 0, count_trash, NULL, 0, NULL);
            if (th) CloseHandle(th);
            else { CloseHandle(count_event); count_event = NULL; }
        }
    }
    if (count_event && InterlockedCompareExchange(&count_busy, 1, 0) == 0)
        if (!SetEvent(count_event)) InterlockedExchange(&count_busy, 0);
    return InterlockedCompareExchange64(&trash_count, 0, 0);
}

bool views_trash_count_poll(int *count) {
    *count = (int)InterlockedCompareExchange64(&trash_count, 0, 0);
    return InterlockedCompareExchange(&count_busy, 0, 0) != 0;
}

/* ---- Recycle Bin tasks: one at a time on a worker ---- */

static struct { HANDLE th; volatile LONG state; int kind; char **ids; size_t n; HRESULT hr; } T;   /* state 0 idle, 1 running, 2 finished */

static HRESULT delete_forever(char **ids, size_t n) {
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr;
    IFileOperation *op = NULL;
    hr = CoCreateInstance(&CLSID_FileOperation, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOperation, (void **)&op);
    if (SUCCEEDED(hr)) {
        IFileOperation_SetOperationFlags(op, FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT);
        for (size_t i = 0; i < n && SUCCEEDED(hr); i++) {
            wchar_t w[FS_PATH_MAX];
            IShellItem *item = NULL;
            fs_u8_to_w(ids[i], w, FS_PATH_MAX);
            hr = SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void **)&item);
            if (SUCCEEDED(hr)) { hr = IFileOperation_DeleteItem(op, item, NULL); IShellItem_Release(item); }
        }
        if (SUCCEEDED(hr)) hr = IFileOperation_PerformOperations(op);
        IFileOperation_Release(op);
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return hr;
}

static unsigned __stdcall task_proc(void *unused) {
    (void)unused;
    if (T.kind == TK_RESTORE) T.hr = trash_restore((const char *const *)T.ids, T.n);
    else if (T.kind == TK_EMPTY) T.hr = trash_empty();
    else T.hr = delete_forever(T.ids, T.n);
    InterlockedExchange(&T.state, 2);
    return 0;
}

bool views_task_start(int kind, char *const *ids, size_t n) {
    if (T.state != 0) return false;
    T.ids = calloc(n + 1, sizeof *T.ids);
    for (size_t i = 0; T.ids && i < n; i++) T.ids[i] = _strdup(ids[i]);
    T.n = n; T.kind = kind; T.hr = S_OK;
    InterlockedExchange(&T.state, 1);
    T.th = (HANDLE)_beginthreadex(NULL, 0, task_proc, NULL, 0, NULL);
    if (!T.th) { InterlockedExchange(&T.state, 2); T.hr = E_FAIL; }
    return true;
}

bool views_task_busy(void) { return T.state == 1; }

bool views_task_take(int *kind, long *hr) {
    if (T.state != 2) return false;
    if (T.th) { WaitForSingleObject(T.th, INFINITE); CloseHandle(T.th); T.th = NULL; }
    for (size_t i = 0; T.ids && i < T.n; i++) free(T.ids[i]);
    free(T.ids); T.ids = NULL;
    *kind = T.kind; *hr = (long)T.hr;
    InterlockedExchange(&T.state, 0);
    return true;
}
