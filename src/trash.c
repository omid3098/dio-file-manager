#define COBJMACROS
#define DFM_INTERNAL
#include "ops.h"
#include "trash.h"
#include <shlobj.h>
#include <shellapi.h>
#include <propsys.h>
#include <propkey.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Recycle Bin's published shell detail keys (not in all SDK propkey.h files). */
static const PROPERTYKEY deleted_from = {{0x9b174b33,0x40ff,0x11d2,{0xa2,0x7e,0x00,0xc0,0x4f,0xc3,0x08,0x71}},2};
static const PROPERTYKEY date_deleted = {{0x9b174b33,0x40ff,0x11d2,{0xa2,0x7e,0x00,0xc0,0x4f,0xc3,0x08,0x71}},3};
static SRWLOCK undo_lock = SRWLOCK_INIT;
static trash_items undo_items;

void trash_list_free(trash_items *list) {
    size_t i;
    for (i = 0; i < list->count; ++i) { free(list->items[i].id); free(list->items[i].name); free(list->items[i].original_path); }
    free(list->items); memset(list, 0, sizeof(*list));
}
static HRESULT list_impl(trash_items *list) {
    PIDLIST_ABSOLUTE bin = NULL; IShellFolder *folder = NULL; IEnumIDList *enumerator = NULL;
    PITEMID_CHILD child = NULL; HRESULT hr; size_t capacity = 0;
    memset(list, 0, sizeof(*list));
    hr = SHGetKnownFolderIDList(&FOLDERID_RecycleBinFolder, 0, NULL, &bin); if (FAILED(hr)) goto done;
    hr = SHBindToObject(NULL, bin, NULL, &IID_IShellFolder, (void **)&folder); if (FAILED(hr)) goto done;
    hr = IShellFolder_EnumObjects(folder, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &enumerator);
    if (FAILED(hr) || !enumerator) goto done;
    while ((hr = IEnumIDList_Next(enumerator, 1, &child, NULL)) == S_OK) {
        IShellItem2 *item = NULL; PWSTR id = NULL, name = NULL, from = NULL;
        trash_item record = {0}; wchar_t *original = NULL;
        HRESULT item_hr = SHCreateItemWithParent(bin, folder, child, &IID_IShellItem2, (void **)&item);
        CoTaskMemFree(child); child = NULL;
        if (FAILED(item_hr)) continue;
        item_hr = IShellItem2_GetDisplayName(item, SIGDN_DESKTOPABSOLUTEPARSING, &id);
        if (SUCCEEDED(item_hr)) item_hr = IShellItem2_GetDisplayName(item, SIGDN_PARENTRELATIVEEDITING, &name);
        if (SUCCEEDED(item_hr)) item_hr = IShellItem2_GetString(item, &deleted_from, &from);
        if (SUCCEEDED(item_hr)) {
            original = dfm_join(from, name);
            record.id = dfm_utf8(id); record.name = dfm_utf8(name); record.original_path = original ? dfm_utf8(original) : NULL;
            IShellItem2_GetUInt64(item, &PKEY_Size, &record.size);
            IShellItem2_GetFileTime(item, &date_deleted, &record.deleted);
            if (!record.id || !record.name || !record.original_path) item_hr = E_OUTOFMEMORY;
        }
        IShellItem2_Release(item); CoTaskMemFree(id); CoTaskMemFree(name); CoTaskMemFree(from); free(original);
        if (SUCCEEDED(item_hr) && list->count == capacity) {
            trash_item *p; capacity = capacity ? capacity * 2 : 32;
            p = realloc(list->items, capacity * sizeof(*p));
            if (!p) item_hr = E_OUTOFMEMORY; else list->items = p;
        }
        if (SUCCEEDED(item_hr)) list->items[list->count++] = record;
        else { free(record.id); free(record.name); free(record.original_path); if (item_hr == E_OUTOFMEMORY) { hr = item_hr; break; } }
    }
done:
    CoTaskMemFree(child); if (enumerator) IEnumIDList_Release(enumerator);
    if (folder) IShellFolder_Release(folder); CoTaskMemFree(bin);
    if (FAILED(hr)) trash_list_free(list);
    return FAILED(hr) ? hr : S_OK;
}
HRESULT trash_list(trash_items *list) {
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr;
    memset(list, 0, sizeof(*list));
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return init;
    hr = list_impl(list); if (SUCCEEDED(init)) CoUninitialize(); return hr;
}
static HRESULT operation(IFileOperation **op) {
    HRESULT hr = CoCreateInstance(&CLSID_FileOperation, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOperation, (void **)op);
    if (SUCCEEDED(hr)) hr = IFileOperation_SetOperationFlags(*op,
        FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOFX_EARLYFAILURE | FOFX_RECYCLEONDELETE | FOF_ALLOWUNDO);
    return hr;
}
static HRESULT perform(IFileOperation *op) {
    BOOL aborted = FALSE; HRESULT hr = IFileOperation_PerformOperations(op);
    if (SUCCEEDED(hr)) hr = IFileOperation_GetAnyOperationsAborted(op, &aborted);
    if (SUCCEEDED(hr) && aborted) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return hr;
}
/* One sink per item; the callback supplies the exact newly recycled shell ID. */
typedef struct sink {
    IFileOperationProgressSink iface;
    LONG refs;
    HRESULT result;
    char *id;
    volatile LONG *cancel;
} sink;
static HRESULT STDMETHODCALLTYPE qi(IFileOperationProgressSink *self, REFIID iid, void **out) {
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IFileOperationProgressSink)) { *out = self; IFileOperationProgressSink_AddRef(self); return S_OK; }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE addref(IFileOperationProgressSink *self) { return (ULONG)InterlockedIncrement(&((sink *)self)->refs); }
static ULONG STDMETHODCALLTYPE release(IFileOperationProgressSink *self) { return (ULONG)InterlockedDecrement(&((sink *)self)->refs); }
static HRESULT STDMETHODCALLTYPE noop(IFileOperationProgressSink *self) { (void)self; return S_OK; }
static HRESULT STDMETHODCALLTYPE finish(IFileOperationProgressSink *self, HRESULT hr) { (void)self; return hr; }
static HRESULT STDMETHODCALLTYPE pre_rename(IFileOperationProgressSink *s, DWORD f, IShellItem *i, LPCWSTR n) { (void)s; (void)f; (void)i; (void)n; return S_OK; }
static HRESULT STDMETHODCALLTYPE post_rename(IFileOperationProgressSink *s, DWORD f, IShellItem *i, LPCWSTR n, HRESULT hr, IShellItem *created) { (void)s; (void)f; (void)i; (void)n; (void)created; return hr; }
static HRESULT STDMETHODCALLTYPE pre_transfer(IFileOperationProgressSink *s, DWORD f, IShellItem *i, IShellItem *dest, LPCWSTR n) { (void)s; (void)f; (void)i; (void)dest; (void)n; return S_OK; }
static HRESULT STDMETHODCALLTYPE post_transfer(IFileOperationProgressSink *s, DWORD f, IShellItem *i, IShellItem *dest, LPCWSTR n, HRESULT hr, IShellItem *created) { (void)s; (void)f; (void)i; (void)dest; (void)n; (void)created; return hr; }
static HRESULT STDMETHODCALLTYPE pre_delete(IFileOperationProgressSink *s, DWORD f, IShellItem *i) {
    sink *p = (sink *)s; (void)f; (void)i;
    return p->cancel && InterlockedCompareExchange(p->cancel, 0, 0) ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK;
}
static HRESULT STDMETHODCALLTYPE post_delete(IFileOperationProgressSink *s, DWORD flags, IShellItem *item, HRESULT hr, IShellItem *created) {
    sink *p = (sink *)s; PWSTR id = NULL; (void)flags; (void)item;
    p->result = hr;
    if (SUCCEEDED(hr) && created) {
        p->result = IShellItem_GetDisplayName(created, SIGDN_DESKTOPABSOLUTEPARSING, &id);
        if (SUCCEEDED(p->result)) { p->id = dfm_utf8(id); if (!p->id) p->result = E_OUTOFMEMORY; }
        CoTaskMemFree(id);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pre_new(IFileOperationProgressSink *s, DWORD f, IShellItem *dest, LPCWSTR n) { (void)s; (void)f; (void)dest; (void)n; return S_OK; }
static HRESULT STDMETHODCALLTYPE post_new(IFileOperationProgressSink *s, DWORD f, IShellItem *dest, LPCWSTR n, LPCWSTR t, DWORD a, HRESULT hr, IShellItem *created) { (void)s; (void)f; (void)dest; (void)n; (void)t; (void)a; (void)created; return hr; }
static HRESULT STDMETHODCALLTYPE progress(IFileOperationProgressSink *s, UINT total, UINT done) { (void)s; (void)total; (void)done; return S_OK; }
static IFileOperationProgressSinkVtbl sink_vtable = {
    qi, addref, release, noop, finish, pre_rename, post_rename, pre_transfer, post_transfer,
    pre_transfer, post_transfer, pre_delete, post_delete, pre_new, post_new, progress, noop, noop, noop
};
/* Whether the shell will really recycle an item. Without FOF_WANTNUKEWARNING (which would draw the shell's own
   dialog) IFileOperation deletes what the bin cannot take for good, and says nothing. So ask first, and treat
   every "no" as a question for the user, never as a reason to delete. */
static int tree_over(const wchar_t *path, uint64_t limit, uint64_t *sum) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return 0;
    if (!(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        *sum += ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow; return *sum > limit;
    } else {
        wchar_t *pattern = dfm_join(path, L"*"); WIN32_FIND_DATAW d; HANDLE h; int over = 0;
        if (!pattern) return 0;
        h = FindFirstFileW(pattern, &d); free(pattern);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            if (wcscmp(d.cFileName, L".") && wcscmp(d.cFileName, L"..")) {
                wchar_t *c = dfm_join(path, d.cFileName);
                if (c) { over = tree_over(c, limit, sum); free(c); }
            }
        } while (!over && FindNextFileW(h, &d));
        FindClose(h); return over;
    }
}
HRESULT dfm_trash_blocker(const wchar_t *w) {
    wchar_t root[MAX_PATH], vol[64], key[200]; DWORD nuke = 0, mb = 0, n = sizeof(DWORD); uint64_t limit = 0, sum = 0;
    ULARGE_INTEGER total;
    /* Explorer's own rule: a path of MAX_PATH characters or more is deleted, not recycled. */
    if (wcslen(wcsncmp(w, L"\\\\?\\", 4) ? w : w + 4) >= MAX_PATH) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    /* Do not silently fall back to permanent deletion on network/removable drives. */
    if (!GetVolumePathNameW(w, root, MAX_PATH) || GetDriveTypeW(root) != DRIVE_FIXED) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if (!GetVolumeNameForVolumeMountPointW(root, vol, 64) || wcsncmp(vol, L"\\\\?\\Volume", 10)) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    _snwprintf_s(key, 200, _TRUNCATE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BitBucket\\Volume\\%.*s", (int)wcslen(vol + 10) - 1, vol + 10);
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"NukeOnDelete", RRF_RT_REG_DWORD, NULL, &nuke, &n) == ERROR_SUCCESS && nuke) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    n = sizeof(DWORD);
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"MaxCapacity", RRF_RT_REG_DWORD, NULL, &mb, &n) == ERROR_SUCCESS) limit = (uint64_t)mb << 20;
    else if (GetDiskFreeSpaceExW(root, NULL, &total, NULL)) limit = total.QuadPart / 20 / 10 * 9;   /* the shell's default is about 5% of the drive */
    if (limit && tree_over(w, limit, &sum)) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    return S_OK;
}
HRESULT dfm_trash_move_cancel(const char *const *paths, size_t count, volatile LONG *cancel) {
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr = S_OK;
    size_t i;
    if (!paths || !count) { if (SUCCEEDED(init)) CoUninitialize(); return E_INVALIDARG; }
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return init;
    AcquireSRWLockExclusive(&undo_lock); trash_list_free(&undo_items);
    undo_items.items = calloc(count, sizeof(*undo_items.items));
    if (!undo_items.items) { hr = E_OUTOFMEMORY; goto done; }
    for (i = 0; i < count; ++i) {
        IFileOperation *op = NULL; IShellItem *item = NULL; wchar_t *w = dfm_wide(paths[i]);
        sink s = {{&sink_vtable}, 1, E_FAIL, NULL, cancel};
        if (cancel && InterlockedCompareExchange(cancel, 0, 0)) { free(w); hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); break; }
        if (!w) { hr = E_INVALIDARG; break; }
        /* Never let the shell delete for good on a trash request; the caller asks the user about such items. */
        hr = dfm_trash_blocker(w); if (FAILED(hr)) { free(w); break; }
        hr = SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void **)&item); free(w);
        if (SUCCEEDED(hr)) hr = operation(&op);
        if (SUCCEEDED(hr)) hr = IFileOperation_DeleteItem(op, item, &s.iface);
        if (SUCCEEDED(hr)) hr = perform(op);
        if (SUCCEEDED(hr)) hr = s.result;
        if (s.id) {
            trash_item *r = &undo_items.items[undo_items.count++];
            r->id = s.id; r->original_path = _strdup(paths[i]);
            if (!r->original_path) hr = E_OUTOFMEMORY;
        } else if (SUCCEEDED(hr)) hr = E_FAIL;
        if (op) IFileOperation_Release(op);
        if (item) IShellItem_Release(item);
        if (FAILED(hr)) break;
    }
done:
    ReleaseSRWLockExclusive(&undo_lock); if (SUCCEEDED(init)) CoUninitialize(); return hr;
}
HRESULT trash_move(const char *const *paths, size_t count) { return dfm_trash_move_cancel(paths, count, NULL); }
/* The shell keeps each recycled item as a `$R<id>` payload plus a `$I<id>` metadata
   file beside it. Moving the payload out leaves `$I` behind as a hidden orphan, so
   drop it once the restore has succeeded (Explorer does the same). */
static void drop_metadata(const wchar_t *id) {
    wchar_t *meta = _wcsdup(id); const wchar_t *leaf;
    if (!meta) return;
    leaf = dfm_leaf(meta);
    if (leaf[0] == L'$' && leaf[1] == L'R' && leaf[2] && wcsstr(meta, L"$RECYCLE.BIN")) {
        meta[leaf - meta + 1] = L'I';
        SetFileAttributesW(meta, FILE_ATTRIBUTE_NORMAL); DeleteFileW(meta);
    }
    free(meta);
}
static HRESULT restore_one(const char *id, const char *original) {
    wchar_t *w = dfm_wide(id), *path = dfm_wide(original), *parent = NULL;
    IShellItem *item = NULL, *folder = NULL; IFileOperation *op = NULL; HRESULT hr = E_OUTOFMEMORY;
    if (!w || !path) goto done;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) { hr = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS); goto done; }
    parent = _wcsdup(path); if (!parent) goto done;
    { const wchar_t *leaf = dfm_leaf(parent); if (leaf == parent || !*leaf) { hr = E_INVALIDARG; goto done; } parent[leaf - parent] = 0; }
    hr = SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void **)&item);
    if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(parent, NULL, &IID_IShellItem, (void **)&folder);
    if (SUCCEEDED(hr)) hr = operation(&op);
    if (SUCCEEDED(hr)) hr = IFileOperation_SetOperationFlags(op, FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION | FOF_RENAMEONCOLLISION | FOFX_EARLYFAILURE);
    if (SUCCEEDED(hr)) hr = IFileOperation_MoveItem(op, item, folder, dfm_leaf(path), NULL);
    if (SUCCEEDED(hr)) hr = perform(op);
    if (SUCCEEDED(hr)) drop_metadata(w);
done:
    if (op) IFileOperation_Release(op); if (folder) IShellItem_Release(folder); if (item) IShellItem_Release(item);
    free(w); free(path); free(parent); return hr;
}
HRESULT trash_restore(const char *const *ids, size_t count) {
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr; trash_items list; size_t i, k;
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return init;
    hr = list_impl(&list);
    for (i = 0; SUCCEEDED(hr) && i < count; ++i) {
        for (k = 0; k < list.count; ++k) if (!strcmp(ids[i], list.items[k].id)) break;
        hr = k == list.count ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND) : restore_one(ids[i], list.items[k].original_path);
    }
    trash_list_free(&list); if (SUCCEEDED(init)) CoUninitialize(); return hr;
}
HRESULT trash_undo(void) {
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr = S_OK; size_t i;
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return init;
    AcquireSRWLockExclusive(&undo_lock);
    for (i = 0; i < undo_items.count; ++i) {
        trash_item *r = &undo_items.items[i]; HRESULT one;
        if (!r->id || !r->original_path) continue;
        one = restore_one(r->id, r->original_path);
        if (FAILED(one)) hr = one;
        else { free(r->id); r->id = NULL; }
    }
    if (SUCCEEDED(hr)) trash_list_free(&undo_items);
    ReleaseSRWLockExclusive(&undo_lock); if (SUCCEEDED(init)) CoUninitialize(); return hr;
}
HRESULT trash_empty(void) {
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr;
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return init;
    AcquireSRWLockExclusive(&undo_lock);
    hr = SHEmptyRecycleBinW(NULL, NULL, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    if (SUCCEEDED(hr)) trash_list_free(&undo_items);
    ReleaseSRWLockExclusive(&undo_lock); if (SUCCEEDED(init)) CoUninitialize(); return hr;
}
