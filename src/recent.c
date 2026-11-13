#define COBJMACROS
#define DFM_INTERNAL
#include "ops.h"
#include "recent.h"
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>
static int newest(const void *a, const void *b) { return -CompareFileTime(&((const recent_item *)a)->modified, &((const recent_item *)b)->modified); }
void recent_free(recent_items *list) {
    size_t i; for (i = 0; i < list->count; ++i) free(list->items[i].path);
    free(list->items); memset(list, 0, sizeof(*list));
}
static char *test_folder;
void recent_test_source(const char *folder) { free(test_folder); test_folder = folder ? _strdup(folder) : NULL; }
static HRESULT scan(recent_items *list, const char *const *remove, size_t count) {
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr;
    PWSTR folder = NULL; wchar_t *pattern = NULL; WIN32_FIND_DATAW d; HANDLE h = INVALID_HANDLE_VALUE;
    HRESULT removal_hr = S_OK;
    IShellLinkW *link = NULL; IPersistFile *persist = NULL; size_t capacity = 0, i, out;
    memset(list, 0, sizeof(*list));
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return init;
    if (test_folder) { folder = dfm_wide(test_folder); hr = folder ? S_OK : E_OUTOFMEMORY; }
    else hr = SHGetKnownFolderPath(&FOLDERID_Recent, 0, NULL, &folder);
    if (FAILED(hr)) goto done;
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link); if (FAILED(hr)) goto done;
    hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&persist); if (FAILED(hr)) goto done;
    pattern = dfm_join(folder, L"*.lnk"); if (!pattern) { hr = E_OUTOFMEMORY; goto done; }
    h = FindFirstFileW(pattern, &d);
    if (h == INVALID_HANDLE_VALUE) { DWORD e = GetLastError(); hr = e == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(e); goto done; }
    do {
        wchar_t target[32768], *shortcut;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        shortcut = dfm_join(folder, d.cFileName);
        if (!shortcut) { hr = E_OUTOFMEMORY; goto done; }
        hr = IPersistFile_Load(persist, shortcut, STGM_READ);
        if (FAILED(hr)) { free(shortcut); continue; }
        target[0] = 0;
        if (FAILED(IShellLinkW_GetPath(link, target, 32768, NULL, SLGP_RAWPATH)) || !target[0]) { free(shortcut); continue; }
        if (remove) {
            for (i = 0; i < count; i++) {
                wchar_t *w = dfm_wide(remove[i]);
                int matches = w && CompareStringOrdinal(w, -1, target, -1, TRUE) == CSTR_EQUAL;
                free(w);
                if (matches) {
                    if (!DeleteFileW(shortcut)) removal_hr = HRESULT_FROM_WIN32(GetLastError());
                    break;
                }
            }
            free(shortcut); continue;
        }
        free(shortcut);
        if (GetFileAttributesW(target) == INVALID_FILE_ATTRIBUTES) continue;
        if (list->count == capacity) {
            recent_item *p; capacity = capacity ? capacity * 2 : 64;
            p = realloc(list->items, capacity * sizeof(*p)); if (!p) { hr = E_OUTOFMEMORY; goto done; } list->items = p;
        }
        list->items[list->count].path = dfm_utf8(target);
        if (!list->items[list->count].path) { hr = E_OUTOFMEMORY; goto done; }
        list->items[list->count++].modified = d.ftLastWriteTime;
    } while (FindNextFileW(h, &d));
    { DWORD e = GetLastError(); hr = e == ERROR_NO_MORE_FILES ? S_OK : HRESULT_FROM_WIN32(e); }
    if (remove) { if (SUCCEEDED(hr)) hr = removal_hr; goto done; }
    qsort(list->items, list->count, sizeof(*list->items), newest);
    out = 0;
    for (i = 0; i < list->count; ++i) {
        size_t k; int duplicate = 0;
        for (k = 0; k < out; ++k) {
            wchar_t *a = dfm_wide(list->items[k].path), *b = dfm_wide(list->items[i].path);
            if (a && b && CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL) duplicate = 1;
            free(a); free(b); if (duplicate) break;
        }
        if (duplicate || out == 200) free(list->items[i].path);
        else list->items[out++] = list->items[i];
    }
    list->count = out;
done:
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
    free(pattern); if (test_folder) free(folder); else CoTaskMemFree(folder);
    if (persist) IPersistFile_Release(persist);
    if (link) IShellLinkW_Release(link);
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr)) recent_free(list);
    return hr;
}

HRESULT recent_list(recent_items *list) { return scan(list, NULL, 0); }
HRESULT recent_remove(const char *const *paths, size_t count) {
    recent_items unused;
    return scan(&unused, paths, count);
}
