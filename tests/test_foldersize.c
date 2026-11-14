/* Console checks of the folder-size engine (src/foldersize.c) on a sandbox tree: exact sizes, nested totals kept,
 * a junction not followed, a change making a folder stale and then right again, a move, and cancel on navigate. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "foldersize.h"

#ifndef DFM_TEST_WORK
#define DFM_TEST_WORK ".agent-work\\console"
#endif

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   %s\n", #c); } while (0)

static char root[MAX_PATH];

static void path_of(char *out, const char *rel) { snprintf(out, MAX_PATH, "%s\\%s", root, rel); }
static void mkdirs(const char *rel) { char p[MAX_PATH]; path_of(p, rel); CreateDirectoryA(p, NULL); }
static void write_file(const char *rel, size_t n, bool append) {
    char p[MAX_PATH]; path_of(p, rel);
    HANDLE h = CreateFileA(p, append ? FILE_APPEND_DATA : GENERIC_WRITE, 0, NULL, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    char *b = calloc(1, n ? n : 1); DWORD w;
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, b, (DWORD)n, &w, NULL); CloseHandle(h); }
    free(b);
}
static void run_hidden(const char *cmd) {
    char c[2048]; snprintf(c, sizeof c, "%s", cmd);
    STARTUPINFOA si = { sizeof si }; PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}
static void remove_tree(const char *dir) {
    char c[2048];
    snprintf(c, sizeof c, "cmd.exe /c rmdir /s /q \"%s\"", dir);
    run_hidden(c);
}

static bool wait_open0(int ms) {
    for (int t = 0; t < ms; t += 10) { if (fsz_open() == 0) return true; Sleep(10); }
    return false;
}
static uint64_t size_of(const char *rel, int *state) { char p[MAX_PATH]; path_of(p, rel); uint64_t v = 0; int s = fsz_get(p, &v); if (state) *state = s; return v; }
static bool wait_size(const char *rel, uint64_t want, int ms) {
    char p[MAX_PATH]; path_of(p, rel);
    for (int t = 0; t < ms; t += 20) {
        uint64_t v = 0;
        if (fsz_get(p, &v) == FSZ_DONE && v == want && !fsz_stale(p)) return true;
        Sleep(20);
    }
    return false;
}
static volatile LONG wakes;
static void on_notify(void) { InterlockedIncrement(&wakes); }
#define U(x) ((unsigned long long)(x))

int main(void) {
    char full[MAX_PATH];
    const char *work = getenv("DFM_FSZ_WORK");   /* a check may name its own sandbox parent */
    snprintf(root, sizeof root, "%s\\foldersize", work ? work : DFM_TEST_WORK);
    GetFullPathNameA(root, MAX_PATH, full, NULL); snprintf(root, sizeof root, "%s", full);
    remove_tree(root);
    { char c[2048]; snprintf(c, sizeof c, "cmd.exe /c mkdir \"%s\"", root); run_hidden(c); }
    mkdirs("a"); mkdirs("a\\sub"); mkdirs("b"); mkdirs("d");
    write_file("top.bin", 5, false);
    write_file("a\\f1", 10, false); write_file("a\\f2", 20, false); write_file("a\\sub\\x", 100, false);
    write_file("d\\f", 7, false);
    { char p[MAX_PATH], t[MAX_PATH], c[2048]; path_of(p, "d\\link"); path_of(t, "a");
      snprintf(c, sizeof c, "cmd.exe /c mklink /J \"%s\" \"%s\"", p, t); run_hidden(c); }

    CHECK(fsz_eligible(root), "the sandbox drive is eligible: %s", root);
    CHECK(!fsz_eligible("\\\\server\\share\\x"), "a network path is not");
    fsz_start(on_notify);
    fsz_view(root);
    const char *kids[] = { "a", "b", "d" };
    for (int i = 0; i < 3; i++) { char p[MAX_PATH]; path_of(p, kids[i]); fsz_want(p); }
    { int s; size_of("a", &s); CHECK(s == FSZ_PENDING || s == FSZ_DONE, "asked folders are pending or done at once (%d)", s); }
    CHECK(wait_open0(10000), "all asked folders finish");
    CHECK(size_of("a", NULL) == 130, "a = 130 (got %llu)", U(size_of("a", NULL)));
    CHECK(size_of("b", NULL) == 0, "empty b = 0");
    CHECK(size_of("d", NULL) == 7, "d = 7: the junction inside is not followed (got %llu)", U(size_of("d", NULL)));
    { int s; uint64_t v = size_of("a\\sub", &s); CHECK(s == FSZ_DONE && v == 100, "nested a\\sub = 100 kept (state %d, %llu)", s, U(v)); }
    CHECK(wakes > 0, "the window was told");

    /* a change after caching: stale at once, right again after about a second of quiet */
    write_file("a\\sub\\x", 50, true);
    char ap[MAX_PATH]; path_of(ap, "a");
    bool saw_stale = false;
    for (int t = 0; t < 600 && !saw_stale; t += 10) { saw_stale = fsz_stale(ap); Sleep(10); }
    CHECK(saw_stale, "a goes stale after a file below it grew");
    CHECK(wait_size("a", 180, 6000), "a recomputes to 180 (got %llu)", U(size_of("a", NULL)));
    CHECK(size_of("a\\sub", NULL) == 150, "nested a\\sub = 150 after the recompute (got %llu)", U(size_of("a\\sub", NULL)));

    /* a moved folder: its old parent shrinks, and it is measured at the new place */
    { char from[MAX_PATH], to[MAX_PATH]; path_of(from, "a\\sub"); path_of(to, "sub_moved"); MoveFileA(from, to); }
    CHECK(wait_size("a", 30, 6000), "a = 30 after sub moved out (got %llu)", U(size_of("a", NULL)));
    {
        char to[MAX_PATH]; path_of(to, "sub_moved");
        fsz_view(root);
        for (int i = 0; i < 3; i++) { char p[MAX_PATH]; path_of(p, kids[i]); fsz_want(p); }
        fsz_want(to);
    }
    CHECK(wait_size("sub_moved", 150, 6000), "sub_moved = 150 (got %llu)", U(size_of("sub_moved", NULL)));

    /* cancel on navigate: slow folders, leave the view, nothing of the old view finishes */
    mkdirs("slow");
    for (int i = 1; i <= 3; i++) {
        char r[64];
        snprintf(r, sizeof r, "slow\\%d", i); mkdirs(r);
        for (int j = 1; j <= 3; j++) {
            snprintf(r, sizeof r, "slow\\%d\\%d", i, j); mkdirs(r);
            snprintf(r, sizeof r, "slow\\%d\\%d\\f", i, j); write_file(r, 11, false);
        }
    }
    char sp[MAX_PATH]; path_of(sp, "slow");
    fsz_test_delay(150);
    fsz_view(sp);
    for (int i = 1; i <= 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "slow\\%d", i); path_of(p, r); fsz_want(p); }
    Sleep(60);
    CHECK(fsz_open() > 0, "work is running (%d)", fsz_open());
    fsz_view(root);   /* navigate away */
    CHECK(fsz_open() == 0, "leaving the folder ends the view's open work at once");
    Sleep(1500);
    int none = 0;
    for (int i = 1; i <= 3; i++) { char r[64]; int s; snprintf(r, sizeof r, "slow\\%d", i); size_of(r, &s); if (s == FSZ_NONE) none++; }
    CHECK(none == 3, "cancelled folders are not cached half done (%d of 3 gone)", none);
    fsz_test_delay(0);
    fsz_view(sp);
    for (int i = 1; i <= 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "slow\\%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(10000), "asked again, they finish");
    CHECK(size_of("slow\\2", NULL) == 33, "slow\\2 = 33 (got %llu)", U(size_of("slow\\2", NULL)));

    fsz_stop();
    remove_tree(root);
    printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
