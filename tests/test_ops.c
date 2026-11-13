#define COBJMACROS
#define DFM_INTERNAL
#include "ops.h"
#include "clip.h"
#include "search.h"
#include "trash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winioctl.h>
#include <ole2.h>

#ifndef DFM_TEST_WORK
#define DFM_TEST_WORK "E:\\w\\dio-file-manager\\.agent-work\\console"
#endif
#define ROOT DFM_TEST_WORK "\\ops\\verification"
static ops_state *state;
static int sandbox_created;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s (Win32 %lu)\n", __LINE__, #x, GetLastError()); exit(1); } } while (0)
static wchar_t *wide(const char *p) { wchar_t *w = dfm_wide(p); CHECK(w); return w; }
static void mkdir_u(const char *p) { wchar_t *w = wide(p); CHECK(CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS); free(w); }
static void file(const char *p, const char *content) {
    wchar_t *w = wide(p); DWORD n; HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); free(w);
    CHECK(h != INVALID_HANDLE_VALUE); CHECK(WriteFile(h, content, (DWORD)strlen(content), &n, NULL)); CHECK(n == strlen(content)); CloseHandle(h);
}
static int exists(const char *p) { wchar_t *w = wide(p); int yes = GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES; free(w); return yes; }
static void content(const char *p, const char *expected) {
    char buf[128] = {0}; DWORD n; wchar_t *w = wide(p); HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); free(w);
    CHECK(h != INVALID_HANDLE_VALUE); CHECK(ReadFile(h, buf, sizeof(buf)-1, &n, NULL)); CloseHandle(h); CHECK(!strcmp(buf, expected));
}
static void finish(ops_job *j, int answer, int all) {
    ULONGLONG start = GetTickCount64(); CHECK(j);
    for (;;) {
        ops_poll(j, state);
        if (state->waiting_conflict) { CHECK(answer >= 0); ops_resolve(j, (ops_answer)answer, all); }
        if (state->finished) break;
        CHECK(GetTickCount64() - start < 30000); Sleep(1);
    }
    if (state->errors) fprintf(stderr, "ops error %u %s\n", state->error_code, state->error_path);
    CHECK(!state->errors); CHECK(!state->cancelled); ops_free(j);
}
static void remove_tree(const wchar_t *path) {
    WIN32_FIND_DATAW d; wchar_t *pattern = dfm_join(path, L"*"); HANDLE h = FindFirstFileW(pattern, &d); free(pattern);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t *p;
            if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..")) continue;
            p = dfm_join(path, d.cFileName); CHECK(p);
            if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) remove_tree(p);
            else { SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL); CHECK((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(p) : DeleteFileW(p)); }
            free(p);
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    CHECK(RemoveDirectoryW(path));
}
static void cleanup(void) {
    if (sandbox_created) { wchar_t *w = wide(ROOT); sandbox_created = 0; remove_tree(w); free(w); }
}
static void basic(void) {
    const char *src[] = { ROOT "\\source\\یادداشت سفر.txt" };
    mkdir_u(ROOT "\\source"); mkdir_u(ROOT "\\target"); file(src[0], "journey");
    finish(ops_start(OPS_COPY, src, 1, ROOT "\\target", NULL), -1, 0);
    CHECK(state->files_total == 1 && state->files_done == 1 && state->bytes_total == 7 && state->bytes_done == 7);
    content(ROOT "\\target\\یادداشت سفر.txt", "journey");
    finish(ops_start(OPS_COPY, src, 1, ROOT "\\source", NULL), -1, 0);
    finish(ops_start(OPS_COPY, src, 1, ROOT "\\source", NULL), -1, 0);
    content(ROOT "\\source\\یادداشت سفر (copy).txt", "journey"); content(ROOT "\\source\\یادداشت سفر (copy 2).txt", "journey");
    { const char *p[] = { ROOT "\\source\\یادداشت سفر (copy).txt" };
      finish(ops_start(OPS_MOVE, p, 1, ROOT "\\target", NULL), -1, 0); CHECK(!exists(p[0])); CHECK(state->bytes_done == 7); }
    file(ROOT "\\source\\data.bin", "123456789"); mkdir_u(ROOT "\\source\\nested"); file(ROOT "\\source\\nested\\deep.txt", "deep");
    { const char *p[] = {ROOT "\\source"};
      finish(ops_start(OPS_COPY, p, 1, ROOT "\\target", NULL), -1, 0);
      CHECK(state->bytes_total == state->bytes_done && state->files_total == state->files_done);
      content(ROOT "\\target\\source\\nested\\deep.txt", "deep"); }
    puts("PASS copy/move, recursive sizes/progress, same-folder names, Persian names");
}
static void conflicts(void) {
    const char *src[] = {ROOT "\\source\\data.bin"};
    file(ROOT "\\target\\data.bin", "old");
    finish(ops_start(OPS_COPY, src, 1, ROOT "\\target", NULL), OPS_SKIP, 0); content(ROOT "\\target\\data.bin", "old");
    finish(ops_start(OPS_COPY, src, 1, ROOT "\\target", NULL), OPS_KEEP_BOTH, 0); content(ROOT "\\target\\data (copy).bin", "123456789");
    finish(ops_start(OPS_COPY, src, 1, ROOT "\\target", NULL), OPS_REPLACE, 0); content(ROOT "\\target\\data.bin", "123456789");
    { const char *batch[] = {src[0], ROOT "\\source\\یادداشت سفر.txt"}; ops_job *j; ULONGLONG start; int answers = 0;
      j = ops_start(OPS_COPY, batch, 2, ROOT "\\target", NULL); CHECK(j); start = GetTickCount64();
      for (;;) { ops_poll(j, state); if (state->waiting_conflict) { ops_resolve(j, OPS_REPLACE, 1); ++answers; }
        if (state->finished) break; CHECK(GetTickCount64()-start < 30000); Sleep(1); }
      CHECK(answers == 1 && !state->errors && state->files_done == 2); ops_free(j); }
    puts("PASS Replace/Skip/Keep Both and Apply to all");
}
static void names(void) {
    const char *bad[] = {"", ".", "..", "x.", "x ", "a/b", "a\\b", "a:b", "a?b", "a*b", "a\"b", "a<b", "a>b", "a|b", "CON", "nul.txt", "COM9", "Lpt1.log", "COM¹.txt", "CONIN$"}; size_t i;
    const char *src[] = {ROOT "\\source\\data.bin"};
    for (i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) { CHECK(ops_name_error(bad[i])); CHECK(!ops_start(OPS_RENAME, src, 1, NULL, bad[i])); CHECK(!ops_start(OPS_NEW_FOLDER, NULL, 0, ROOT, bad[i])); }
    CHECK(!ops_name_error("یادداشت سفر.txt")); CHECK(!ops_name_error("console.txt"));
    finish(ops_start(OPS_RENAME, src, 1, NULL, "renamed.bin"), -1, 0); CHECK(!exists(src[0])); content(ROOT "\\source\\renamed.bin", "123456789");
    finish(ops_start(OPS_NEW_FOLDER, NULL, 0, ROOT, "پوشه تازه"), -1, 0); CHECK(exists(ROOT "\\پوشه تازه"));
    puts("PASS rename/new-folder success and Windows name refusals");
}
static void cancellation(void) {
    const char *src[] = {ROOT "\\source\\large.bin"};
    wchar_t *w = wide(src[0]); LARGE_INTEGER size; HANDLE h; DWORD dummy; ops_job *j; ULONGLONG start;
    h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); free(w); CHECK(h != INVALID_HANDLE_VALUE);
    CHECK(DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &dummy, NULL));
    size.QuadPart = 1024LL*1024*1024; CHECK(SetFilePointerEx(h, size, NULL, FILE_BEGIN) && SetEndOfFile(h)); CloseHandle(h);
    j = ops_start(OPS_COPY, src, 1, ROOT "\\target", NULL); CHECK(j); start = GetTickCount64();
    for (;;) { ops_poll(j, state); CHECK(!state->finished); if (state->bytes_done > 0) break; CHECK(GetTickCount64()-start < 30000); Sleep(0); }
    ops_cancel(j);
    do { ops_poll(j, state); CHECK(GetTickCount64()-start < 30000); Sleep(1); } while (!state->finished);
    CHECK(state->cancelled && !state->errors); ops_free(j); CHECK(exists(src[0]) && !exists(ROOT "\\target\\large.bin"));
    { const char *p[] = {ROOT "\\target"}; finish(ops_start(OPS_DELETE, p, 1, NULL, NULL), -1, 0); mkdir_u(ROOT "\\target"); }
    puts("PASS cancellation during 1 GiB copy preserves source and removes partial output; recursive delete");
}
static size_t find(const char *query, int hidden) {
    search_job *j = search_start(ROOT, query, hidden); search_state s; size_t total = 0; ULONGLONG start = GetTickCount64(); CHECK(j);
    do { size_t n, i; char **p = search_drain(j, &n, &s); total += n; for (i=0;i<n;++i) free(p[i]); free(p); CHECK(GetTickCount64()-start < 30000); Sleep(1); } while (!s.finished);
    CHECK(!s.errors); search_free(j); return total;
}
static void searching(void) {
    wchar_t *w;
    file(ROOT "\\English-MIXED.txt", "x"); file(ROOT "\\hidden-English.txt", "x");
    mkdir_u(ROOT "\\hidden-folder"); file(ROOT "\\hidden-folder\\English-child.txt", "x");
    w = wide(ROOT "\\hidden-English.txt"); CHECK(SetFileAttributesW(w, FILE_ATTRIBUTE_HIDDEN)); free(w);
    w = wide(ROOT "\\hidden-folder"); CHECK(SetFileAttributesW(w, FILE_ATTRIBUTE_HIDDEN)); free(w);
    CHECK(find("english", 0) == 1); CHECK(find("ENGLISH", 1) == 3); CHECK(find("سفر", 0) == 2);
    { search_job *j = search_start(ROOT, "txt", 1); search_state s; size_t n, i; char **p; CHECK(j); search_cancel(j);
      search_free(j); /* joining after cancel must not deadlock */
      j = search_start(ROOT, "txt", 1); CHECK(j); search_cancel(j);
      do { p = search_drain(j, &n, &s); for(i=0;i<n;++i) free(p[i]); free(p); Sleep(1); } while(!s.finished);
      CHECK(s.cancelled); search_free(j); }
    puts("PASS streaming Persian/English search, Unicode case folding, hidden pruning, cancel");
}
static void clipboard(void) {
    const char *paths[] = {ROOT "\\source\\یادداشت سفر.txt", ROOT "\\source\\renamed.bin"}; clip_files f; int cut; DWORD error;
    struct { UINT format; HANDLE data; } saved[256]; size_t count = 0, i; UINT format = 0;
    /* Snapshot real handles before replacing clipboard data; OleGetClipboard is
       a live proxy and cannot restore data after the clipboard changes. */
    CHECK(OpenClipboard(NULL));
    while ((format = EnumClipboardFormats(format)) != 0) {
        HANDLE data = GetClipboardData(format), copy;
        CHECK(count < 256 && data); copy = OleDuplicateData(data, (CLIPFORMAT)format, 0); CHECK(copy);
        saved[count].format = format; saved[count++].data = copy;
    }
    CloseClipboard();
    for (cut=0;cut<=1;++cut) {
        error = clip_write(NULL, paths, 2, cut); CHECK(!error); CHECK(!clip_read(NULL, &f));
        CHECK(f.count == 2 && f.cut == cut && !strcmp(f.paths[0], paths[0]) && !strcmp(f.paths[1], paths[1])); clip_free(&f);
    }
    CHECK(OpenClipboard(NULL)); CHECK(EmptyClipboard());
    for (i = 0; i < count; ++i) CHECK(SetClipboardData(saved[i].format, saved[i].data));
    CloseClipboard();
    puts("PASS Explorer-compatible CF_HDROP/Preferred DropEffect copy and cut round trips");
}
static void recycle(void) {
    const char *paths[] = {ROOT "\\recycle-me.txt"}; HRESULT hr; trash_items list; size_t i; int found = 0;
    file(paths[0], "undo me"); hr = trash_move(paths, 1); if (FAILED(hr)) fprintf(stderr, "trash HRESULT %08lx\n", (unsigned long)hr); CHECK(SUCCEEDED(hr)); CHECK(!exists(paths[0]));
    CHECK(SUCCEEDED(trash_list(&list)));
    for(i=0;i<list.count;++i) if(!strcmp(list.items[i].original_path, paths[0])) { found = 1; CHECK(!strcmp(list.items[i].name, "recycle-me.txt")); CHECK(list.items[i].size == 7); }
    CHECK(found); trash_list_free(&list);
    hr = trash_undo(); if (FAILED(hr)) fprintf(stderr, "undo HRESULT %08lx\n", (unsigned long)hr); CHECK(SUCCEEDED(hr)); content(paths[0], "undo me");
    puts("PASS sandbox-only Recycle Bin listing, trash and exact-item undo/restore");
}
static void cross_volume(void) {
    wchar_t drives[512], *p; wchar_t our_root[MAX_PATH]; int ran = 0;
    CHECK(GetVolumePathNameW(L"E:\\w\\dio-file-manager", our_root, MAX_PATH)); CHECK(GetLogicalDriveStringsW(512, drives));
    for(p=drives;*p;p+=wcslen(p)+1) if(GetDriveTypeW(p)==DRIVE_FIXED && _wcsicmp(p, our_root)) {
        wchar_t dest[MAX_PATH]; char *u; const char *src[] = {ROOT "\\cross-volume.txt"};
        _snwprintf_s(dest, MAX_PATH, _TRUNCATE, L"%sdio-file-manager-test-%lu", p, GetCurrentProcessId());
        if(!CreateDirectoryW(dest, NULL)) continue;
        u = dfm_utf8(dest); CHECK(u); file(src[0], "cross drive");
        finish(ops_start(OPS_MOVE, src, 1, u, NULL), -1, 0); CHECK(!exists(src[0])); CHECK(state->bytes_done == 11 && state->files_done == 1);
        { size_t n = strlen(u)+32; char *target = malloc(n); CHECK(target); sprintf_s(target,n,"%s\\cross-volume.txt",u); content(target,"cross drive"); free(target); }
        remove_tree(dest); free(u); ran=1; break;
    }
    puts(ran ? "PASS cross-volume move copies then removes source; second-drive sandbox removed" : "SKIP cross-volume move: no second writable fixed drive");
}
int main(void) {
    wchar_t *root = wide(ROOT);
    mkdir_u(DFM_TEST_WORK);
    mkdir_u(DFM_TEST_WORK "\\ops");
    CHECK(!exists(ROOT)); CHECK(CreateDirectoryW(root, NULL)); sandbox_created = 1; atexit(cleanup); state = calloc(1, sizeof(*state)); CHECK(state);
    basic(); conflicts(); names(); cancellation(); searching(); clipboard(); recycle(); cross_volume();
    cleanup(); free(root); free(state); puts("ALL CHECKS PASSED; sandboxes removed"); return 0;
}
