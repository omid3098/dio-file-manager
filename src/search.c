#define DFM_INTERNAL
#include "ops.h"
#include "search.h"
#include "keymatch.h"
#include <process.h>
#include <stdlib.h>
#include <string.h>
struct search_job {
    CRITICAL_SECTION lock;
    HANDLE thread;
    volatile LONG cancel;
    wchar_t *folder, *query;
    keymatch km;
    int hidden;
    char **paths; size_t count, capacity;
    search_state state;
};
static int stopped(search_job *j) { return InterlockedCompareExchange(&j->cancel, 0, 0) != 0; }
static void error(search_job *j, DWORD code) {
    EnterCriticalSection(&j->lock); ++j->state.errors; j->state.last_error = code; LeaveCriticalSection(&j->lock);
}
/* The folder searched is read with the extended prefix past 240 characters, so it works past 260; the paths it finds are reported plain. */
static wchar_t *long_wide(const char *u8) {
    wchar_t *w = dfm_wide(u8), *x;
    if (!w || wcslen(w) < 240) return w;
    x = dfm_wide_long(u8);
    if (!x) return w;
    free(w); return x;
}
static void emit(search_job *j, const wchar_t *path) {
    wchar_t *plain = dfm_plain(path); char *u = dfm_utf8(plain ? plain : path);
    free(plain);
    if (!u) { error(j, GetLastError()); return; }
    EnterCriticalSection(&j->lock);
    if (j->count == j->capacity) {
        size_t cap = j->capacity ? j->capacity * 2 : 64;
        char **p = realloc(j->paths, cap * sizeof(*p));
        if (!p) { free(u); ++j->state.errors; j->state.last_error = ERROR_NOT_ENOUGH_MEMORY; InterlockedExchange(&j->cancel, 1); LeaveCriticalSection(&j->lock); return; }
        j->paths = p; j->capacity = cap;
    }
    j->paths[j->count++] = u; LeaveCriticalSection(&j->lock);
}
static void walk(search_job *j, const wchar_t *folder) {
    wchar_t *pattern; WIN32_FIND_DATAW d; HANDLE h; DWORD e;
    if (stopped(j)) return;
    pattern = dfm_join(folder, L"*");
    if (!pattern) { error(j, ERROR_NOT_ENOUGH_MEMORY); return; }
    h = FindFirstFileW(pattern, &d); free(pattern);
    if (h == INVALID_HANDLE_VALUE) { error(j, GetLastError()); return; }
    do {
        wchar_t *p;
        if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..")) continue;
        if (!j->hidden && (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        p = dfm_join(folder, d.cFileName);
        if (!p) { error(j, ERROR_NOT_ENOUGH_MEMORY); break; }
        int alt;
        if (km_match(&j->km, d.cFileName, &alt)) {
            if (alt) { EnterCriticalSection(&j->lock); ++j->state.alt_hits; LeaveCriticalSection(&j->lock); }
            emit(j, p);
        }
        if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) walk(j, p);
        free(p);
    } while (!stopped(j) && FindNextFileW(h, &d));
    e = GetLastError(); FindClose(h);
    if (!stopped(j) && e != ERROR_NO_MORE_FILES) error(j, e);
}
static unsigned __stdcall run_search(void *arg) {
    search_job *j = arg; walk(j, j->folder);
    EnterCriticalSection(&j->lock); j->state.cancelled = stopped(j); j->state.finished = 1; LeaveCriticalSection(&j->lock); return 0;
}
search_job *search_start(const char *folder, const char *query, int hidden) { return search_start_ex(folder, query, hidden, 1); }
search_job *search_start_ex(const char *folder, const char *query, int hidden, int cross) {
    search_job *j = calloc(1, sizeof(*j)); DWORD attr;
    if (!j) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    InitializeCriticalSection(&j->lock); j->folder = long_wide(folder); j->query = dfm_wide(query); j->hidden = hidden;
    if (!j->folder || !j->query || !*j->query) goto failure;
    km_init(&j->km, j->query, cross);
    attr = GetFileAttributesW(j->folder);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) goto failure;
    j->thread = (HANDLE)_beginthreadex(NULL, 0, run_search, j, 0, NULL);
    if (!j->thread) goto failure;
    return j;
failure:
    search_free(j); SetLastError(ERROR_INVALID_PARAMETER); return NULL;
}
char **search_drain(search_job *j, size_t *count, search_state *state) {
    char **result;
    EnterCriticalSection(&j->lock); result = j->paths; *count = j->count; *state = j->state;
    j->paths = NULL; j->count = j->capacity = 0; LeaveCriticalSection(&j->lock); return result;
}
void search_cancel(search_job *j) { InterlockedExchange(&j->cancel, 1); }
void search_free(search_job *j) {
    size_t i; if (!j) return;
    search_cancel(j);
    if (j->thread) { WaitForSingleObject(j->thread, INFINITE); CloseHandle(j->thread); }
    for (i = 0; i < j->count; ++i) free(j->paths[i]);
    free(j->paths); free(j->folder); free(j->query); DeleteCriticalSection(&j->lock); free(j);
}
