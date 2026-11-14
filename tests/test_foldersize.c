/* Console checks of the folder-size engine (src/foldersize.c) on a sandbox tree: exact sizes, nested totals kept,
 * a junction not followed, a change moving up through the ancestors with one directory read, a move, cancel on navigate,
 * one big folder walked by several threads, a new and a removed subfolder, the cache never dropping a shown folder,
 * a change under a part that is no longer cached only marks the folder stale: it reads nothing until the folder is shown again, and the cost of ten thousand lookups. */
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
static void wait_quiet(void) {   /* the events of earlier writes are read: no directory read for 800 ms */
    unsigned n0 = fsz_test_reads();
    for (int w = 0, quiet = 0; quiet < 8 && w < 100; w++) { Sleep(100); unsigned n = fsz_test_reads(); if (n == n0) quiet++; else { quiet = 0; n0 = n; } }
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

    /* a change after caching: the changed folder is read again and the difference moves up, no walk of the rest */
    write_file("a\\sub\\x", 50, true);
    CHECK(wait_size("a", 180, 6000), "a recomputes to 180 (got %llu)", U(size_of("a", NULL)));
    CHECK(size_of("a\\sub", NULL) == 150, "nested a\\sub = 150 after the change (got %llu)", U(size_of("a\\sub", NULL)));

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


    /* a big tree is walked by several threads at once: one shown folder, many subfolders */
    mkdirs("big");
    for (int i = 0; i < 24; i++) {
        char r[64];
        snprintf(r, sizeof r, "big\\s%d", i); mkdirs(r);
        snprintf(r, sizeof r, "big\\s%d\\f", i); write_file(r, 1000 + (size_t)i, false);
    }
    char bp[MAX_PATH]; path_of(bp, "big");
    fsz_test_delay(40);
    fsz_test_peak();
    fsz_view(root);
    fsz_want(bp);
    CHECK(wait_open0(20000), "the big folder finishes");
    fsz_test_delay(0);
    { unsigned pk = fsz_test_peak(); CHECK(pk >= 3, "one shown folder is walked by several threads at once (peak %u)", pk); }
    CHECK(size_of("big", NULL) == 24 * 1000 + 276, "big = 24276 (got %llu)", U(size_of("big", NULL)));

    /* a change in a deep folder moves up through its ancestors without reading its siblings */
    mkdirs("deep"); mkdirs("deep\\a"); mkdirs("deep\\a\\b"); mkdirs("deep\\a\\b\\c");
    write_file("deep\\a\\b\\c\\f", 100, false); write_file("deep\\a\\g", 10, false);
    for (int i = 0; i < 12; i++) { char r[64]; snprintf(r, sizeof r, "deep\\s%d", i); mkdirs(r); snprintf(r, sizeof r, "deep\\s%d\\f", i); write_file(r, 5, false); }
    char dp[MAX_PATH]; path_of(dp, "deep");
    fsz_view(root); fsz_want(dp);
    CHECK(wait_open0(10000), "deep finishes");
    CHECK(size_of("deep", NULL) == 170, "deep = 170 (got %llu)", U(size_of("deep", NULL)));
    /* the events of the set-up writes above are read after the walk (one read per folder): wait until they are done */
    unsigned r0 = fsz_test_reads();
    for (int w = 0, quiet = 0; quiet < 8 && w < 100; w++) { Sleep(100); unsigned n = fsz_test_reads(); if (n == r0) quiet++; else { quiet = 0; r0 = n; } }
    write_file("deep\\a\\b\\c\\f", 40, true);
    CHECK(wait_size("deep", 210, 6000), "deep = 210 after the deep file grew (got %llu)", U(size_of("deep", NULL)));
    CHECK(size_of("deep\\a", NULL) == 150 && size_of("deep\\a\\b", NULL) == 140 && size_of("deep\\a\\b\\c", NULL) == 140,
          "the folders between are right too (%llu %llu %llu)", U(size_of("deep\\a", NULL)), U(size_of("deep\\a\\b", NULL)), U(size_of("deep\\a\\b\\c", NULL)));
    Sleep(600);
    { unsigned d = fsz_test_reads() - r0; CHECK(d <= 3, "the change read %u directories, not the whole tree (16 folders)", d); }
    /* a new subfolder is walked on its own, a removed one is subtracted */
    r0 = fsz_test_reads();
    mkdirs("deep\\a\\n"); write_file("deep\\a\\n\\z", 33, false);
    CHECK(wait_size("deep", 243, 6000), "deep = 243 with a new subfolder (got %llu)", U(size_of("deep", NULL)));
    { char r[MAX_PATH]; path_of(r, "deep\\s3"); char c[2048]; snprintf(c, sizeof c, "cmd.exe /c rmdir /s /q \"%s\"", r); run_hidden(c); }
    CHECK(wait_size("deep", 238, 6000), "deep = 238 after a subfolder was removed (got %llu)", U(size_of("deep", NULL)));
    Sleep(600);
    { unsigned d = fsz_test_reads() - r0; CHECK(d <= 6, "a new and a removed subfolder read %u directories", d); }

    /* the cache never drops a shown folder, even when it is over its cap */
    mkdirs("many");
    for (int i = 0; i < 10; i++) { char r[64]; snprintf(r, sizeof r, "many\\m%d", i); mkdirs(r); snprintf(r, sizeof r, "many\\m%d\\f", i); write_file(r, 3, false); }
    char mp[MAX_PATH]; path_of(mp, "many");
    fsz_test_cap(4);
    fsz_view(mp);
    for (int i = 0; i < 10; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "many\\m%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(10000), "the ten shown folders finish");
    { int done = 0; for (int i = 0; i < 10; i++) { char r[64]; int s; snprintf(r, sizeof r, "many\\m%d", i); if (size_of(r, &s) == 3 && s == FSZ_DONE) done++; }
      CHECK(done == 10, "all ten shown folders stay cached over a cap of 4 (%d)", done); }
    fsz_test_cap(0);

    /* a tree bigger than the cache: a change under a part that is not cached marks the shown folder stale and reads
     * nothing while it stays shown; it is walked again when it is shown again, no sooner than a second after its last walk */
    mkdirs("miss"); mkdirs("miss\\x"); mkdirs("miss\\x\\y"); write_file("miss\\x\\y\\f", 10, false);
    char xp[MAX_PATH]; path_of(xp, "miss");
    fsz_test_cap(1);
    fsz_view(root); fsz_want(xp);
    CHECK(wait_open0(10000), "miss finishes with an almost empty cache");
    fsz_test_cap(0);
    { int s; size_of("miss\\x\\y", &s); CHECK(s == FSZ_NONE, "the nested folders were not kept (state %d)", s); }
    wait_quiet();
    r0 = fsz_test_reads();
    DWORD t0 = GetTickCount();
    write_file("miss\\x\\y\\f", 5, true);
    Sleep(1000);
    CHECK(fsz_stale(xp), "a change under an uncached part marks the shown folder stale");
    Sleep(1800);   /* longer than the one second a walk would wait */
    CHECK(size_of("miss", NULL) == 10, "no walk while it stays shown (got %llu)", U(size_of("miss", NULL)));
    CHECK(fsz_test_reads() == r0, "zero directory reads for the change (%u)", fsz_test_reads() - r0);
    CHECK(fsz_stale(xp), "it is still marked stale");
    unsigned r1 = fsz_test_reads();
    fsz_view(root); fsz_want(xp);   /* shown again */
    CHECK(wait_size("miss", 15, 6000), "miss = 15 after it was shown again (got %llu)", U(size_of("miss", NULL)));
    CHECK(GetTickCount() - t0 >= 2500, "it was walked only when shown again, long after the change (%lu ms)", (unsigned long)(GetTickCount() - t0));
    CHECK(fsz_test_reads() - r1 == 3, "one walk of the three folders: %u reads", fsz_test_reads() - r1);
    { int s; size_of("miss\\x\\y", &s); CHECK(s == FSZ_DONE, "the walk cached the changed folder (state %d)", s); }
    Sleep(300);
    r0 = fsz_test_reads();
    write_file("miss\\x\\y\\f", 5, true);
    CHECK(wait_size("miss", 20, 6000), "the next change is a cheap read: miss = 20 (got %llu)", U(size_of("miss", NULL)));
    Sleep(500);
    { unsigned d = fsz_test_reads() - r0; CHECK(d <= 2, "the next change read %u directories", d); }
    /* shown again right after a walk: it waits for the rate limit, then walks once */
    fsz_test_cap(1);
    mkdirs("miss2"); mkdirs("miss2\\x"); write_file("miss2\\x\\f", 1, false);
    char x2[MAX_PATH]; path_of(x2, "miss2");
    fsz_view(root); fsz_want(x2);
    CHECK(wait_open0(10000), "miss2 finishes");
    fsz_test_cap(0);
    wait_quiet();
    write_file("miss2\\x\\f", 1, true);
    Sleep(1000);
    CHECK(fsz_stale(x2), "miss2 is stale after the change");
    fsz_view(root); fsz_want(x2);   /* shown again at once: not sooner than max(1 s, 3 x the last walk) after the last one */
    CHECK(wait_size("miss2", 2, 6000), "miss2 = 2 at last (got %llu)", U(size_of("miss2", NULL)));

    /* sizes_fill asks for one folder at a time: ten thousand lookups must not eat the 50 ms budget */
    mkdirs("wide");
    for (int i = 0; i < 10000; i++) { char r[64]; snprintf(r, sizeof r, "wide\\w%05d", i); mkdirs(r); }
    char wp[MAX_PATH]; path_of(wp, "wide");
    fsz_view(wp);
    for (int i = 0; i < 10000; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "wide\\w%05d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(60000), "ten thousand shown folders finish");
    {
        LARGE_INTEGER f, a0, a1; QueryPerformanceFrequency(&f);
        char (*paths)[MAX_PATH] = malloc(10000 * sizeof *paths);
        for (int i = 0; i < 10000; i++) { char r[64]; snprintf(r, sizeof r, "wide\\w%05d", i); path_of(paths[i], r); }
        uint64_t sum = 0;
        QueryPerformanceCounter(&a0);
        for (int i = 0; i < 10000; i++) { uint64_t v = 0; if (fsz_get(paths[i], &v) == FSZ_DONE) sum++; }
        QueryPerformanceCounter(&a1);
        free(paths);
        double ms = (double)(a1.QuadPart - a0.QuadPart) * 1000.0 / (double)f.QuadPart;
        printf("10,000 lookups: %.1f ms\n", ms);
        CHECK(sum == 10000 && ms < 25.0, "ten thousand lookups take %.1f ms (limit 25)", ms);
    }

    fsz_stop();
    remove_tree(root);
    printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
