#define DFM_INTERNAL
#include "ops.h"
#include "trash.h"
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
HRESULT dfm_trash_move_cancel(const char *const *paths, size_t count, volatile LONG *cancel, unsigned *recycled);
HRESULT dfm_trash_blocker(const wchar_t *w);

wchar_t *dfm_wide(const char *s) {
    int n; wchar_t *w;
    if (!s || !(n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0))) { SetLastError(ERROR_NO_UNICODE_TRANSLATION); return NULL; }
    w = malloc((size_t)n * sizeof(*w));
    if (!w) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n); return w;
}
char *dfm_utf8(const wchar_t *s) {
    int n; char *u;
    if (!s || !(n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, NULL, 0, NULL, NULL))) { SetLastError(ERROR_NO_UNICODE_TRANSLATION); return NULL; }
    u = malloc((size_t)n);
    if (!u) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, u, n, NULL, NULL); return u;
}
wchar_t *dfm_join(const wchar_t *a, const wchar_t *b) {
    size_t n = wcslen(a), m = wcslen(b); wchar_t *w = malloc((n + m + 2) * sizeof(*w));
    if (!w) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    memcpy(w, a, n * sizeof(*w));
    if (n && a[n-1] != L'\\' && a[n-1] != L'/') w[n++] = L'\\';
    memcpy(w + n, b, (m + 1) * sizeof(*w)); return w;
}
const wchar_t *dfm_leaf(const wchar_t *s) {
    const wchar_t *p = wcsrchr(s, L'\\'), *q = wcsrchr(s, L'/');
    if (!p || (q && q > p)) p = q;
    return p ? p + 1 : s;
}
/* Every path a job touches is made absolute and given the extended prefix in absolute(), the one place, so copy, move,
   delete, rename and new folder all work past 260 characters, whether from paste, drag or drop. Takes p, returns it or its
   replacement (NULL when out of memory). */
static wchar_t *extended(wchar_t *p) {
    wchar_t *x; size_t n;
    if (!wcsncmp(p, L"\\\\?\\", 4) || !wcsncmp(p, L"\\\\.\\", 4)) return p;
    n = wcslen(p) + 8; x = malloc(n * sizeof(*x));
    if (!x) { free(p); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    if (p[0] == L'\\') _snwprintf_s(x, n, _TRUNCATE, L"\\\\?\\UNC\\%s", p + 2);   /* \\server\share becomes \\?\UNC\server\share */
    else _snwprintf_s(x, n, _TRUNCATE, L"\\\\?\\%s", p);
    free(p); return x;
}
/* The same path as a person and the shell know it: X:\... or \\server\share, without the extended prefix. */
static wchar_t *plain_dup(const wchar_t *w) {
    if (!wcsncmp(w, L"\\\\?\\UNC\\", 8)) {
        size_t n = wcslen(w) - 6 + 1; wchar_t *p = malloc(n * sizeof(*p));
        if (p) { p[0] = L'\\'; wcscpy_s(p + 1, n - 1, w + 7); }
        return p;
    }
    return _wcsdup(!wcsncmp(w, L"\\\\?\\", 4) && w[4] && w[5] == L':' ? w + 4 : w);
}
static wchar_t *absolute(const char *s) {
    wchar_t *w = dfm_wide(s), *p; DWORD n;
    if (!w) return NULL;
    if (!((w[0] && w[1] == L':' && (w[2] == L'\\' || w[2] == L'/')) || (w[0] == L'\\' && w[1] == L'\\'))) {
        free(w); SetLastError(ERROR_BAD_PATHNAME); return NULL;
    }
    n = GetFullPathNameW(w, 0, NULL, NULL);
    p = n ? malloc((size_t)n * sizeof(*p)) : NULL;
    if (p) GetFullPathNameW(w, n, p, NULL);
    free(w);
    if (!p) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    n = (DWORD)wcslen(p);
    while (n > 3 && (p[n-1] == L'\\' || p[n-1] == L'/')) p[--n] = 0;
    return extended(p);
}
wchar_t *dfm_wide_long(const char *s) { return absolute(s); }
wchar_t *dfm_plain(const wchar_t *w) { return plain_dup(w); }
const char *ops_name_error(const char *name) {
    wchar_t *w = dfm_wide(name), base[256]; size_t n, i, b;
    const char *error = NULL;
    if (!w) return "Name is not valid UTF-8.";
    n = wcslen(w);
    if (!n) error = "Enter a name.";
    else if (n > 255) error = "Name is too long.";
    else if (w[n-1] == L'.' || w[n-1] == L' ') error = "Name cannot end with a dot or space.";
    else {
        for (i = 0; i < n; ++i) if (w[i] < 32 || wcschr(L"<>:\"/\\|?*", w[i])) { error = "Name contains a character Windows does not allow."; break; }
        b = wcscspn(w, L".");
        while (b && w[b-1] == L' ') --b;
        if (b < 256) {
            memcpy(base, w, b * sizeof(*w)); base[b] = 0;
            if (!_wcsicmp(base, L"CON") || !_wcsicmp(base, L"PRN") || !_wcsicmp(base, L"AUX") || !_wcsicmp(base, L"NUL") || !_wcsicmp(base, L"CONIN$") || !_wcsicmp(base, L"CONOUT$") ||
                (b == 4 && (!_wcsnicmp(base, L"COM", 3) || !_wcsnicmp(base, L"LPT", 3)) && ((base[3] >= L'1' && base[3] <= L'9') || base[3] == 0xb9 || base[3] == 0xb2 || base[3] == 0xb3))) error = "This name is reserved by Windows.";
        }
    }
    free(w); return error;
}

struct ops_job {
    CRITICAL_SECTION lock;
    HANDLE thread, answer_event;
    volatile LONG cancel;
    ops_kind kind;
    wchar_t **sources, *destination, *name;
    size_t count;
    ops_state state;
    int answer, all_answer;
    uint64_t copy_base;
    DWORD test_copy_delay;
};
static int cancelled(ops_job *j) { return InterlockedCompareExchange(&j->cancel, 0, 0) != 0; }
static void text_path(char *out, size_t size, const wchar_t *w) {
    wchar_t *x = plain_dup(w); char *u = x ? dfm_utf8(x) : NULL;   /* shown without the extended prefix */
    if (u) strncpy_s(out, size, u, _TRUNCATE); else out[0] = 0;
    free(u); free(x);
}
static void fail(ops_job *j, const wchar_t *path, DWORD error) {
    EnterCriticalSection(&j->lock); ++j->state.errors; j->state.error_code = error;
    text_path(j->state.error_path, sizeof(j->state.error_path), path);
    LeaveCriticalSection(&j->lock);
}
static int same(const wchar_t *a, const wchar_t *b) { return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL; }
static int inside(const wchar_t *parent, const wchar_t *child) {
    size_t n = wcslen(parent), m = wcslen(child);
    return m >= n && CompareStringOrdinal(parent, (int)n, child, (int)n, TRUE) == CSTR_EQUAL && (m == n || child[n] == L'\\' || parent[n-1] == L'\\');
}
static wchar_t *real_folder(const wchar_t *path) {
    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    wchar_t *result = NULL; DWORD n;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    n = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED);
    if (n) { result = malloc(((size_t)n + 1) * sizeof(*result)); if (result && !GetFinalPathNameByHandleW(h, result, n + 1, FILE_NAME_NORMALIZED)) { free(result); result = NULL; } }
    CloseHandle(h); return result;
}
static void scan(ops_job *j, const wchar_t *path, uint64_t *files, uint64_t *bytes) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (cancelled(j)) return;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return;
    ++*files;
    if (!(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) *bytes += ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    else if (!(a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        wchar_t *pattern = dfm_join(path, L"*"); WIN32_FIND_DATAW d; HANDLE h;
        if (!pattern) return;
        h = FindFirstFileW(pattern, &d); free(pattern);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (wcscmp(d.cFileName, L".") && wcscmp(d.cFileName, L"..")) {
                wchar_t *p = dfm_join(path, d.cFileName);
                if (p) { scan(j, p, files, bytes); free(p); }
            }
        } while (!cancelled(j) && FindNextFileW(h, &d));
        FindClose(h);
    }
}
static void completed(ops_job *j, uint64_t files, uint64_t bytes) {
    EnterCriticalSection(&j->lock); j->state.files_done += files; j->state.bytes_done += bytes; LeaveCriticalSection(&j->lock);
}
static int conflict(ops_job *j, const wchar_t *src, const wchar_t *dst, int cannot_trash) {
    int answer;
    EnterCriticalSection(&j->lock);
    if (j->all_answer >= 0) { answer = j->all_answer; LeaveCriticalSection(&j->lock); return answer; }
    ResetEvent(j->answer_event); j->answer = -1; j->state.waiting_conflict = 1; j->state.cannot_trash = cannot_trash;
    text_path(j->state.conflict_source, sizeof(j->state.conflict_source), src);
    text_path(j->state.conflict_target, sizeof(j->state.conflict_target), dst);
    LeaveCriticalSection(&j->lock);
    WaitForSingleObject(j->answer_event, INFINITE);
    EnterCriticalSection(&j->lock); answer = j->answer; j->state.waiting_conflict = 0; j->state.cannot_trash = 0; LeaveCriticalSection(&j->lock);
    return answer;
}
static wchar_t *copy_name(const wchar_t *path, int directory) {
    const wchar_t *leaf = dfm_leaf(path), *ext = directory ? NULL : wcsrchr(leaf, L'.');
    size_t stem, n = wcslen(path) + 40; unsigned i;
    wchar_t *p = malloc(n * sizeof(*p));
    if (!p) return NULL;
    if (!ext || ext == leaf) ext = path + wcslen(path);
    stem = (size_t)(ext - path);
    for (i = 1; i < 1000000; ++i) {
        if (i == 1) _snwprintf_s(p, n, _TRUNCATE, L"%.*s (copy)%s", (int)stem, path, ext);
        else _snwprintf_s(p, n, _TRUNCATE, L"%.*s (copy %u)%s", (int)stem, path, i, ext);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return p;
    }
    free(p); return NULL;
}
static DWORD CALLBACK copy_progress(LARGE_INTEGER total, LARGE_INTEGER done, LARGE_INTEGER stream_size,
    LARGE_INTEGER stream_done, DWORD stream, DWORD reason, HANDLE src, HANDLE dst, LPVOID context) {
    ops_job *j = context;
    (void)total; (void)stream_size; (void)stream_done; (void)stream; (void)reason; (void)src; (void)dst;
    EnterCriticalSection(&j->lock); j->state.bytes_done = j->copy_base + (uint64_t)done.QuadPart; LeaveCriticalSection(&j->lock);
    if (j->test_copy_delay) Sleep(j->test_copy_delay);
    return cancelled(j) ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}
static int remove_node(ops_job *j, const wchar_t *path, int progress) {
    WIN32_FILE_ATTRIBUTE_DATA a; int ok = 1;
    if (cancelled(j)) return 0;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) { fail(j, path, GetLastError()); return 0; }
    if ((a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        wchar_t *pattern = dfm_join(path, L"*"); WIN32_FIND_DATAW d; HANDLE h;
        if (!pattern) { fail(j, path, ERROR_NOT_ENOUGH_MEMORY); return 0; }
        h = FindFirstFileW(pattern, &d); free(pattern);
        if (h == INVALID_HANDLE_VALUE) { fail(j, path, GetLastError()); return 0; }
        do {
            if (wcscmp(d.cFileName, L".") && wcscmp(d.cFileName, L"..")) {
                wchar_t *p = dfm_join(path, d.cFileName);
                if (!p) { fail(j, path, ERROR_NOT_ENOUGH_MEMORY); ok = 0; }
                else { if (!remove_node(j, p, progress)) ok = 0; free(p); }
            }
        } while (!cancelled(j) && FindNextFileW(h, &d));
        { DWORD error = GetLastError(); if (!cancelled(j) && error != ERROR_NO_MORE_FILES) { fail(j, path, error); ok = 0; } }
        FindClose(h);
    }
    if (!ok || cancelled(j)) return 0;
    ok = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(path) : DeleteFileW(path);
    if (!ok) fail(j, path, GetLastError());
    else if (progress) completed(j, 1, (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow);
    return ok;
}
static int delete_item(ops_job *j, const wchar_t *path) { return remove_node(j, path, 1); }   /* absolute() already gave the extended prefix */
static int copy_node(ops_job *j, const wchar_t *src, const wchar_t *requested, int move) {
    WIN32_FILE_ATTRIBUTE_DATA a; DWORD target_attrs, error; wchar_t *dst = NULL; int replace = 0, ok = 1;
    if (cancelled(j)) return 0;
    if (!GetFileAttributesExW(src, GetFileExInfoStandard, &a)) { fail(j, src, GetLastError()); return 0; }
    if (a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { fail(j, src, ERROR_NOT_SUPPORTED); return 0; }
    EnterCriticalSection(&j->lock); text_path(j->state.current_name, sizeof(j->state.current_name), dfm_leaf(src)); LeaveCriticalSection(&j->lock);
    dst = _wcsdup(requested);
    if (!dst) { fail(j, src, ERROR_NOT_ENOUGH_MEMORY); return 0; }
    if (same(src, dst)) {
        if (move) { free(dst); return 1; }
        free(dst); dst = copy_name(requested, (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        if (!dst) { fail(j, requested, ERROR_NOT_ENOUGH_MEMORY); return 0; }
    }
retry_target:
    target_attrs = GetFileAttributesW(dst);
    if (target_attrs != INVALID_FILE_ATTRIBUTES) {
        int answer = conflict(j, src, dst, 0);
        if (cancelled(j) || answer < 0 || answer == OPS_SKIP) { free(dst); return 0; }
        if (answer == OPS_KEEP_BOTH) {
            free(dst); dst = copy_name(requested, (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
            if (!dst) { fail(j, requested, ERROR_NOT_ENOUGH_MEMORY); return 0; }
            target_attrs = INVALID_FILE_ATTRIBUTES;
        } else replace = 1;
    }
    if (target_attrs != INVALID_FILE_ATTRIBUTES && ((a.dwFileAttributes ^ target_attrs) & FILE_ATTRIBUTE_DIRECTORY)) { fail(j, dst, ERROR_ALREADY_EXISTS); free(dst); return 0; }
    if (move && !(target_attrs != INVALID_FILE_ATTRIBUTES && (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))) {
        if (MoveFileExW(src, dst, replace ? MOVEFILE_REPLACE_EXISTING : 0)) {
            uint64_t files = 0, bytes = 0; scan(j, dst, &files, &bytes); completed(j, files, bytes); free(dst); return 1;
        }
        error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) goto retry_target;
        if (error != ERROR_NOT_SAME_DEVICE) { fail(j, dst, error); free(dst); return 0; }
    }
    if (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        wchar_t *pattern; WIN32_FIND_DATAW d; HANDLE h;
        if (target_attrs == INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(dst, NULL)) {
            error = GetLastError();
            if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) goto retry_target;
            fail(j, dst, error); free(dst); return 0;
        }
        pattern = dfm_join(src, L"*");
        if (!pattern) { fail(j, src, ERROR_NOT_ENOUGH_MEMORY); free(dst); return 0; }
        h = FindFirstFileW(pattern, &d); free(pattern);
        if (h == INVALID_HANDLE_VALUE) { fail(j, src, GetLastError()); free(dst); return 0; }
        do {
            if (wcscmp(d.cFileName, L".") && wcscmp(d.cFileName, L"..")) {
                wchar_t *s = dfm_join(src, d.cFileName), *t = dfm_join(dst, d.cFileName);
                if (!s || !t) { fail(j, src, ERROR_NOT_ENOUGH_MEMORY); ok = 0; }
                else if (!copy_node(j, s, t, 0)) ok = 0;
                free(s); free(t);
            }
        } while (!cancelled(j) && FindNextFileW(h, &d));
        error = GetLastError(); FindClose(h);
        if (!cancelled(j) && error != ERROR_NO_MORE_FILES) { fail(j, src, error); ok = 0; }
        if (ok && !cancelled(j)) completed(j, 1, 0); else ok = 0;
    } else {
        static volatile LONG serial;
        int skipped = 0;
        size_t n = wcslen(dst) + 64; wchar_t *temp = malloc(n * sizeof(*temp)); HANDLE h = INVALID_HANDLE_VALUE; unsigned attempt;
        if (!temp) { fail(j, dst, ERROR_NOT_ENOUGH_MEMORY); free(dst); return 0; }
        for (attempt = 0; attempt < 100; ++attempt) {
            /* The final name is part of the temp name so the folder window can list the file while it copies. */
            const wchar_t *leaf = dfm_leaf(dst);
            _snwprintf_s(temp, n, _TRUNCATE, wcslen(leaf) <= 200 ? L"%.*s.%s.dfm-copy-%lu-%ld.tmp" : L"%.*s%s.dfm-copy-%lu-%ld.tmp",
                         (int)(leaf - dst), dst, wcslen(leaf) <= 200 ? leaf : L"", GetCurrentProcessId(), InterlockedIncrement(&serial));
            h = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
        }
        if (h == INVALID_HANDLE_VALUE) { fail(j, dst, GetLastError()); free(temp); free(dst); return 0; }
        CloseHandle(h);
        EnterCriticalSection(&j->lock); j->copy_base = j->state.bytes_done; LeaveCriticalSection(&j->lock);
        ok = CopyFileExW(src, temp, copy_progress, j, NULL, 0);
        error = GetLastError();
        if (ok && !cancelled(j)) {
            for (;;) {
                ok = MoveFileExW(temp, dst, replace ? MOVEFILE_REPLACE_EXISTING : 0); error = GetLastError();
                if (ok || cancelled(j) || (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)) break;
                int answer = conflict(j, src, dst, 0);
                if (cancelled(j) || answer < 0 || answer == OPS_SKIP) { skipped = 1; break; }
                if (answer == OPS_REPLACE) {
                    DWORD attrs = GetFileAttributesW(dst);
                    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) { error = ERROR_ALREADY_EXISTS; break; }
                    replace = 1;
                } else {
                    wchar_t *both = copy_name(requested, 0);
                    if (!both) { error = ERROR_NOT_ENOUGH_MEMORY; break; }
                    free(dst); dst = both; replace = 0;
                }
            }
        }
        else ok = 0;
        if (!ok) {
            SetFileAttributesW(temp, FILE_ATTRIBUTE_NORMAL); DeleteFileW(temp);
            EnterCriticalSection(&j->lock); j->state.bytes_done = j->copy_base; LeaveCriticalSection(&j->lock);
            if (!cancelled(j) && !skipped) fail(j, dst, error);
        } else completed(j, 1, 0);
        free(temp);
    }
    if (ok && move) ok = remove_node(j, src, 0);
    free(dst); return ok;
}
static unsigned __stdcall run(void *context) {
    ops_job *j = context; size_t i; uint64_t files = 0, bytes = 0;
    for (i = 0; i < j->count; ++i) scan(j, j->sources[i], &files, &bytes);
    if (j->kind == OPS_NEW_FOLDER) files = 1;
    EnterCriticalSection(&j->lock); j->state.files_total = files; j->state.bytes_total = bytes; LeaveCriticalSection(&j->lock);
    if (j->kind == OPS_NEW_FOLDER) {
        wchar_t *p = dfm_join(j->destination, j->name);
        if (!p) fail(j, j->destination, ERROR_NOT_ENOUGH_MEMORY);
        else { if (!cancelled(j)) { if (!CreateDirectoryW(p, NULL)) fail(j, p, GetLastError()); else completed(j, 1, 0); } free(p); }
    } else if (j->kind == OPS_TRASH && !cancelled(j)) {
        char **paths = calloc(j->count, sizeof(*paths)); HRESULT hr = E_OUTOFMEMORY; size_t n = 0;
        if (paths) {
            hr = S_OK;
            /* An item the bin cannot take is the user's call: Delete removes it for good, Skip and Cancel leave it. */
            for (i = 0; i < j->count && !cancelled(j); ++i) {
                if (SUCCEEDED(dfm_trash_blocker(j->sources[i]))) {   /* the shell and the Undo list know the plain path */
                    wchar_t *x = plain_dup(j->sources[i]); paths[n] = x ? dfm_utf8(x) : NULL; free(x);
                    if (!paths[n]) { hr = E_OUTOFMEMORY; break; }
                    ++n;
                }
                else if (conflict(j, j->sources[i], L"", 1) == OPS_DELETE_NOW && !cancelled(j)) {
                    if (delete_item(j, j->sources[i])) { EnterCriticalSection(&j->lock); ++j->state.deleted; LeaveCriticalSection(&j->lock); }
                }
            }
            if (SUCCEEDED(hr) && n && !cancelled(j)) {
                unsigned recycled = 0;
                hr = dfm_trash_move_cancel((const char *const *)paths, n, &j->cancel, &recycled);
                /* Count what really reached the bin, so Undo is offered for it even when a later item failed or the job was cancelled. */
                EnterCriticalSection(&j->lock); j->state.trashed = recycled; LeaveCriticalSection(&j->lock);
            }
            for (i = 0; i < n; ++i) free(paths[i]);
            free(paths);
        }
        if (FAILED(hr)) fail(j, j->sources[0], (DWORD)hr);
        else completed(j, files, bytes);
    } else for (i = 0; i < j->count && !cancelled(j); ++i) {
        if (j->kind == OPS_DELETE) delete_item(j, j->sources[i]);
        else if (j->kind == OPS_RENAME) {
            const wchar_t *leaf = dfm_leaf(j->sources[i]); size_t n = (size_t)(leaf - j->sources[i]);
            wchar_t *parent = _wcsdup(j->sources[i]), *dst;
            if (!parent) { fail(j, j->sources[i], ERROR_NOT_ENOUGH_MEMORY); continue; }
            parent[n] = 0; dst = dfm_join(parent, j->name); free(parent);
            if (!dst) fail(j, j->sources[i], ERROR_NOT_ENOUGH_MEMORY);
            else { if (!MoveFileExW(j->sources[i], dst, 0)) fail(j, dst, GetLastError()); else completed(j, files, bytes); free(dst); }
        } else {
            wchar_t *dst = dfm_join(j->destination, dfm_leaf(j->sources[i]));
            DWORD attr = GetFileAttributesW(j->sources[i]);
            wchar_t *real_src = real_folder(j->sources[i]), *real_dst = real_folder(j->destination);
            int nested = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) &&
                (inside(j->sources[i], j->destination) || (real_src && real_dst && inside(real_src, real_dst)));
            free(real_src); free(real_dst);
            if (nested) fail(j, j->sources[i], ERROR_INVALID_PARAMETER);
            else if (!dst) fail(j, j->sources[i], ERROR_NOT_ENOUGH_MEMORY);
            else copy_node(j, j->sources[i], dst, j->kind == OPS_MOVE);
            free(dst);
        }
    }
    EnterCriticalSection(&j->lock); j->state.cancelled = cancelled(j); j->state.finished = 1; LeaveCriticalSection(&j->lock);
    return 0;
}
ops_job *ops_start(ops_kind kind, const char *const *sources, size_t count, const char *destination, const char *name) {
    ops_job *j; size_t i;
    if (kind < OPS_COPY || kind > OPS_NEW_FOLDER || (kind == OPS_NEW_FOLDER ? count != 0 : (!sources || !count)) ||
        (kind == OPS_RENAME && count != 1) || ((kind == OPS_RENAME || kind == OPS_NEW_FOLDER) && ops_name_error(name))) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    j = calloc(1, sizeof(*j)); if (!j) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    InitializeCriticalSection(&j->lock); j->kind = kind; j->count = count; j->all_answer = -1;
    /* Slow real CopyFileEx callbacks only when a private-desktop check requests it. */
    char delay[16];
    DWORD delay_n = GetEnvironmentVariableA("DFM_TEST_COPY_DELAY_MS", delay, sizeof delay);
    if (delay_n && delay_n < sizeof delay) {
        unsigned long ms = strtoul(delay, NULL, 10);
        if (ms <= 500) j->test_copy_delay = (DWORD)ms;   /* 128 MiB then lasts about 4 s per 100 ms */
    }
    j->answer_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    j->sources = calloc(count ? count : 1, sizeof(*j->sources));
    if (!j->answer_event || !j->sources) goto error;
    for (i = 0; i < count; ++i) if (!(j->sources[i] = absolute(sources[i]))) goto error;
    if (kind == OPS_COPY || kind == OPS_MOVE || kind == OPS_NEW_FOLDER) {
        DWORD attr;
        j->destination = absolute(destination); if (!j->destination) goto error;
        attr = GetFileAttributesW(j->destination);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) { SetLastError(ERROR_PATH_NOT_FOUND); goto error; }
    }
    if (name && !(j->name = dfm_wide(name))) goto error;
    j->thread = (HANDLE)_beginthreadex(NULL, 0, run, j, 0, NULL);
    if (!j->thread) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); goto error; }
    return j;
error:
    { DWORD error = GetLastError(); ops_free(j); SetLastError(error); return NULL; }
}
void ops_poll(ops_job *j, ops_state *state) { EnterCriticalSection(&j->lock); *state = j->state; LeaveCriticalSection(&j->lock); }
void ops_cancel(ops_job *j) { InterlockedExchange(&j->cancel, 1); if (j->answer_event) SetEvent(j->answer_event); }
void ops_resolve(ops_job *j, ops_answer answer, int all) {
    EnterCriticalSection(&j->lock);
    if (j->state.waiting_conflict && j->answer < 0 && answer >= OPS_REPLACE && answer <= OPS_DELETE_NOW) {
        j->answer = answer; if (all) j->all_answer = answer; SetEvent(j->answer_event);
    }
    LeaveCriticalSection(&j->lock);
}
void ops_free(ops_job *j) {
    size_t i; if (!j) return;
    ops_cancel(j);
    if (j->thread) { WaitForSingleObject(j->thread, INFINITE); CloseHandle(j->thread); }
    if (j->answer_event) CloseHandle(j->answer_event);
    if (j->sources) { for (i = 0; i < j->count; ++i) free(j->sources[i]); free(j->sources); }
    free(j->destination); free(j->name); DeleteCriticalSection(&j->lock); free(j);
}
