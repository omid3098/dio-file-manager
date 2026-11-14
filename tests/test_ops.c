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
static wchar_t *extended(const char *plain);
static void cleanup(void) {
    if (sandbox_created) { wchar_t *x = extended(ROOT "\\long"); if (GetFileAttributesW(x) != INVALID_FILE_ATTRIBUTES) remove_tree(x); free(x); }
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
/* The `$I` metadata file beside a recycled `$R` item; a restore must not leave it behind. */
static int bin_meta_exists(const char *id) {
    wchar_t *w = wide(id), *leaf; int found;
    CHECK(w); leaf = wcsrchr(w, L'\\'); CHECK(leaf && leaf[1] == L'$' && leaf[2] == L'R'); leaf[2] = L'I';
    found = GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES; free(w); return found;
}
static void recycle(void) {
    const char *paths[] = {ROOT "\\recycle-me.txt"}; HRESULT hr; trash_items list; size_t i; int found = 0; char *undo_id = NULL;
    file(paths[0], "undo me"); hr = trash_move(paths, 1); if (FAILED(hr)) fprintf(stderr, "trash HRESULT %08lx\n", (unsigned long)hr); CHECK(SUCCEEDED(hr)); CHECK(!exists(paths[0]));
    CHECK(SUCCEEDED(trash_list(&list)));
    for(i=0;i<list.count;++i) if(!strcmp(list.items[i].original_path, paths[0])) { found = 1; CHECK(!strcmp(list.items[i].name, "recycle-me.txt")); CHECK(list.items[i].size == 7); }
    CHECK(found); trash_list_free(&list);
    CHECK(SUCCEEDED(trash_list(&list))); found = 0;
    for(i=0;i<list.count;++i) if(!strcmp(list.items[i].original_path, paths[0])) { found = 1; CHECK(bin_meta_exists(list.items[i].id)); undo_id = _strdup(list.items[i].id); CHECK(undo_id); }
    CHECK(found); trash_list_free(&list);
    hr = trash_undo(); if (FAILED(hr)) fprintf(stderr, "undo HRESULT %08lx\n", (unsigned long)hr); CHECK(SUCCEEDED(hr)); content(paths[0], "undo me");
    /* Undo and Restore both leave no `$I` orphan in the bin. */
    CHECK(!bin_meta_exists(undo_id)); free(undo_id);
    hr = trash_move(paths, 1); CHECK(SUCCEEDED(hr)); CHECK(SUCCEEDED(trash_list(&list))); found = 0;
    for(i=0;i<list.count;++i) if(!strcmp(list.items[i].original_path, paths[0])) {
        const char *id = list.items[i].id; found = 1; CHECK(bin_meta_exists(id));
        hr = trash_restore(&id, 1); CHECK(SUCCEEDED(hr)); content(paths[0], "undo me"); CHECK(!bin_meta_exists(id)); break;
    }
    CHECK(found); trash_list_free(&list);
    puts("PASS sandbox-only Recycle Bin listing, trash and exact-item undo/restore");
}
/* A path of 260+ characters cannot go to the Recycle Bin: the shell then deletes it for good and says nothing.
   Trash must leave such an item alone until the user answers Delete, and must still recycle ordinary ones. */
static wchar_t *extended(const char *plain) {
    size_t n_; wchar_t *w = wide(plain), *x = malloc((wcslen(w) + 8) * sizeof(*x)); CHECK(x); n_ = wcslen(w) + 8; wcscpy_s(x, n_, L"\\\\?\\"); wcscat_s(x, n_, w); free(w); return x;
}
/* Makes ROOT\long\dddd...\ so that `name` inside it has exactly `total` characters. Returns the plain UTF-8 path. */
static char *long_file(size_t total, const char *name, const char *content_text) {
    char dir[1024]; size_t n = strlen(ROOT "\\long"); wchar_t *x; char *path; DWORD written; HANDLE h;
    mkdir_u(ROOT "\\long"); strcpy_s(dir, sizeof dir, ROOT "\\long");
    { long d = (long)total - (long)(n + 1 + strlen(name));
      while (d > 0) {
        long seg = d - 1 > 60 ? 60 : d - 1;
        CHECK(d >= 2); if (d - 1 - seg == 1) seg--;   /* a leftover gap of one character could not be filled */
        dir[n++] = '\\'; memset(dir + n, 'd', (size_t)seg); n += (size_t)seg; dir[n] = 0; d -= 1 + seg;
        x = extended(dir); CHECK(CreateDirectoryW(x, NULL) || GetLastError() == ERROR_ALREADY_EXISTS); free(x);
      }
      CHECK(d == 0); }
    path = malloc(n + strlen(name) + 2); CHECK(path); sprintf_s(path, n + strlen(name) + 2, "%s\\%s", dir, name);
    CHECK(strlen(path) == total);
    x = extended(path); h = CreateFileW(x, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); free(x);
    CHECK(h != INVALID_HANDLE_VALUE); CHECK(WriteFile(h, content_text, (DWORD)strlen(content_text), &written, NULL)); CloseHandle(h);
    return path;
}
static int long_exists(const char *plain) { wchar_t *x = extended(plain); int yes = GetFileAttributesW(x) != INVALID_FILE_ATTRIBUTES; free(x); return yes; }
static int in_bin(const char *leaf) {
    trash_items list; size_t i; int found = 0;
    CHECK(SUCCEEDED(trash_list(&list)));
    for (i = 0; i < list.count; ++i) if (!strcmp(list.items[i].name, leaf)) found = 1;
    trash_list_free(&list); return found;
}
/* Runs a trash job; answers the first cannot-trash question with `answer` (or cancels on -1) and returns the final state. */
static void trash_job(const char *const *paths, size_t count, int answer) {
    ULONGLONG start = GetTickCount64(); ops_job *j = ops_start(OPS_TRASH, paths, count, NULL, NULL); int asked = 0;
    CHECK(j);
    for (;;) {
        ops_poll(j, state);
        if (state->waiting_conflict) {
            CHECK(state->cannot_trash); asked++;
            if (answer < 0) ops_cancel(j); else ops_resolve(j, (ops_answer)answer, 0);
        }
        if (state->finished) break;
        CHECK(GetTickCount64() - start < 30000); Sleep(1);
    }
    CHECK(asked >= 1); ops_free(j);
}
static void unrecyclable(void) {
    char *bad = long_file(285, "dfm-nuke-probe-a.txt", "keep me");
    char *edge = long_file(259, "dfm-nuke-probe-e.txt", "edge");
    char *bad2 = long_file(260, "dfm-nuke-probe-b.txt", "exactly 260");
    const char *one[1], *mixed[2]; HRESULT hr;
    /* The API itself refuses and touches nothing. */
    one[0] = bad; hr = trash_move(one, 1); CHECK(FAILED(hr)); CHECK(long_exists(bad)); CHECK(!in_bin("dfm-nuke-probe-a.txt"));
    one[0] = bad2; hr = trash_move(one, 1); CHECK(FAILED(hr)); CHECK(long_exists(bad2)); CHECK(!in_bin("dfm-nuke-probe-b.txt"));
    /* 259 characters is still recycled (and undone), so the guard does not over-block. */
    one[0] = edge; hr = trash_move(one, 1); CHECK(SUCCEEDED(hr)); CHECK(!long_exists(edge)); CHECK(in_bin("dfm-nuke-probe-e.txt"));
    CHECK(SUCCEEDED(trash_undo())); CHECK(long_exists(edge)); CHECK(!in_bin("dfm-nuke-probe-e.txt"));
    /* The job asks; Skip and Cancel leave the item, Delete removes it for good. */
    one[0] = bad; trash_job(one, 1, OPS_SKIP); CHECK(long_exists(bad)); CHECK(state->trashed == 0 && state->deleted == 0 && !state->errors);
    trash_job(one, 1, -1); CHECK(long_exists(bad)); CHECK(state->cancelled && state->deleted == 0);
    /* A mixed batch: the ordinary item goes to the bin, the long one stays. */
    file(ROOT "\\ordinary-trash-me.txt", "ordinary"); mixed[0] = bad; mixed[1] = ROOT "\\ordinary-trash-me.txt";
    trash_job(mixed, 2, OPS_SKIP); CHECK(long_exists(bad)); CHECK(!exists(mixed[1])); CHECK(state->trashed == 1 && !state->errors);
    CHECK(in_bin("ordinary-trash-me.txt")); CHECK(SUCCEEDED(trash_undo())); content(mixed[1], "ordinary");
    one[0] = bad; trash_job(one, 1, OPS_DELETE_NOW); CHECK(!long_exists(bad)); CHECK(state->deleted == 1 && state->trashed == 0 && !state->errors);
    CHECK(!in_bin("dfm-nuke-probe-a.txt"));
    { wchar_t *x = extended(ROOT "\\long"); remove_tree(x); free(x); }
    free(bad); free(edge); free(bad2);
    puts("PASS a trash request never deletes an item the Recycle Bin cannot take: it asks, and Skip/Cancel keep it");
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
    basic(); conflicts(); names(); cancellation(); searching(); clipboard(); recycle(); unrecyclable(); cross_volume();
    cleanup(); free(root); free(state); puts("ALL CHECKS PASSED; sandboxes removed"); return 0;
}
