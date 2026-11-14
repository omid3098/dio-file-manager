/* Console checks of the folder-size engine (src/foldersize.c) on a sandbox tree: exact sizes, nested totals kept,
 * a junction not followed, entering a folder again measures its children again (the old value stays readable, a folder
 * measured since the view began is not walked again, a child inside a walk that is still running waits for that
 * walk, a folder changed and entered again at once shows its new size), cancel on navigate and return at once, one big folder walked by several threads, the cache never dropping a shown folder, no work and no
 * directory read while nothing is measured, and the cost of ten thousand lookups. */
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
    fsz_view(root, true);
    const char *kids[] = { "a", "b", "d" };
    for (int i = 0; i < 3; i++) { char p[MAX_PATH]; path_of(p, kids[i]); fsz_want(p); }
    { int s; size_of("a", &s); CHECK(s == FSZ_PENDING || s == FSZ_DONE, "asked folders are pending or done at once (%d)", s); }
    CHECK(wait_open0(10000), "all asked folders finish");
    CHECK(size_of("a", NULL) == 130, "a = 130 (got %llu)", U(size_of("a", NULL)));
    CHECK(size_of("b", NULL) == 0, "empty b = 0");
    CHECK(size_of("d", NULL) == 7, "d = 7: the junction inside is not followed (got %llu)", U(size_of("d", NULL)));
    { int s; uint64_t v = size_of("a\\sub", &s); CHECK(s == FSZ_DONE && v == 100, "nested a\\sub = 100 kept (state %d, %llu)", s, U(v)); }
    CHECK(wakes > 0, "the window was told");

    /* the same view asked again: measured since it began, no new walk */
    unsigned r0 = fsz_test_reads();
    fsz_view(root, false);   /* listed again (a quiet reload), not entered again */
    for (int i = 0; i < 3; i++) { char p[MAX_PATH]; path_of(p, kids[i]); fsz_want(p); }
    Sleep(200);
    CHECK(fsz_test_reads() == r0 && fsz_open() == 0, "asked again at once: %u new directory reads, nothing open", fsz_test_reads() - r0);

    /* a file changed while another folder is shown: entering the parent again shows the new size; the old one stays
       readable (and marked as being measured) until it arrives */
    { char bp[MAX_PATH]; path_of(bp, "b"); fsz_view(bp, true); }
    write_file("a\\sub\\x", 50, true);
    fsz_test_delay(150);
    fsz_view(root, true);
    for (int i = 0; i < 3; i++) { char p[MAX_PATH]; path_of(p, kids[i]); fsz_want(p); }
    { char ap[MAX_PATH]; uint64_t v = 0; path_of(ap, "a"); int st = fsz_get(ap, &v);
      CHECK(st == FSZ_DONE && v == 130 && fsz_stale(ap), "the old size stays readable while it is measured again (state %d, %llu)", st, U(v)); }
    CHECK(fsz_open() > 0, "the view counts the folders still being measured (%d)", fsz_open());
    CHECK(wait_open0(10000), "all asked folders finish again");
    fsz_test_delay(0);
    CHECK(size_of("a", NULL) == 180, "a = 180 after it was entered again (got %llu)", U(size_of("a", NULL)));
    CHECK(size_of("a\\sub", NULL) == 150, "nested a\\sub = 150 (got %llu)", U(size_of("a\\sub", NULL)));

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
    fsz_view(sp, true);
    for (int i = 1; i <= 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "slow\\%d", i); path_of(p, r); fsz_want(p); }
    Sleep(60);
    CHECK(fsz_open() > 0, "work is running (%d)", fsz_open());
    fsz_view(root, true);   /* navigate away */
    CHECK(fsz_open() == 0, "leaving the folder ends the view's open work at once");
    Sleep(1500);
    int none = 0;
    for (int i = 1; i <= 3; i++) { char r[64]; int s; snprintf(r, sizeof r, "slow\\%d", i); size_of(r, &s); if (s == FSZ_NONE) none++; }
    CHECK(none == 3, "cancelled folders are not cached half done (%d of 3 gone)", none);
    fsz_test_delay(0);
    fsz_view(sp, true);
    for (int i = 1; i <= 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "slow\\%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(10000), "asked again, they finish");
    CHECK(size_of("slow\\2", NULL) == 33, "slow\\2 = 33 (got %llu)", U(size_of("slow\\2", NULL)));

    /* leave a folder whose children are being measured and come straight back: they are measured, not left unknown */
    mkdirs("back");
    for (int i = 1; i <= 3; i++) {
        char r[64];
        snprintf(r, sizeof r, "back\\%d", i); mkdirs(r);
        snprintf(r, sizeof r, "back\\%d\\x", i); mkdirs(r);
        snprintf(r, sizeof r, "back\\%d\\x\\f", i); write_file(r, 9, false);
    }
    char kp[MAX_PATH]; path_of(kp, "back");
    fsz_test_delay(150);
    fsz_view(kp, true);
    for (int i = 1; i <= 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "back\\%d", i); path_of(p, r); fsz_want(p); }
    Sleep(60);
    fsz_view(root, true);
    fsz_view(kp, true);
    for (int i = 1; i <= 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "back\\%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(20000), "left and entered again at once: the children finish");
    fsz_test_delay(0);
    { int ok = 0; for (int i = 1; i <= 3; i++) { char r[64]; int st; snprintf(r, sizeof r, "back\\%d", i); if (size_of(r, &st) == 9 && st == FSZ_DONE) ok++; }
      CHECK(ok == 3, "all three children = 9 after leaving and returning at once (%d of 3)", ok); }


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
    fsz_view(root, true);
    fsz_want(bp);
    CHECK(wait_open0(20000), "the big folder finishes");
    fsz_test_delay(0);
    { unsigned pk = fsz_test_peak(); CHECK(pk >= 3, "one shown folder is walked by several threads at once (peak %u)", pk); }
    CHECK(size_of("big", NULL) == 24 * 1000 + 276, "big = 24276 (got %llu)", U(size_of("big", NULL)));

    /* enter a folder and, right after, one inside it: the children are not walked a second time. Ten directories in all */
    mkdirs("tree");
    for (int i = 0; i < 3; i++) {
        char r[64];
        snprintf(r, sizeof r, "tree\\c%d", i); mkdirs(r);
        for (int j = 0; j < 2; j++) { snprintf(r, sizeof r, "tree\\c%d\\g%d", i, j); mkdirs(r); snprintf(r, sizeof r, "tree\\c%d\\g%d\\f", i, j); write_file(r, 10, false); }
    }
    char tp[MAX_PATH]; path_of(tp, "tree");
    fsz_test_delay(60);
    r0 = fsz_test_reads();
    fsz_view(root, true); fsz_want(tp);
    Sleep(100);
    fsz_view(tp, true);   /* the walk of tree is still running */
    for (int i = 0; i < 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "tree\\c%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(20000), "the children of a folder entered during its walk finish");
    fsz_test_delay(0);
    { unsigned d = fsz_test_reads() - r0; CHECK(d == 10, "tree and its children cost %u directory reads, not more than the 10 directories", d); }
    CHECK(size_of("tree\\c1", NULL) == 20 && size_of("tree", NULL) == 60, "tree = 60, c1 = 20 (got %llu, %llu)", U(size_of("tree", NULL)), U(size_of("tree\\c1", NULL)));
    /* the same folder reloaded: no other view in between, the new size shows */
    fsz_view(tp, true);
    for (int i = 0; i < 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "tree\\c%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(20000), "entered, the children finish");
    write_file("tree\\c0\\g0\\f", 5, true);
    fsz_view(tp, true);
    for (int i = 0; i < 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "tree\\c%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(20000), "reloaded, the children finish");
    CHECK(wait_size("tree\\c0", 25, 5000), "tree\\c0 = 25 after a reload (got %llu)", U(size_of("tree\\c0", NULL)));

    /* a file changes and the folder is left and entered again at once: no waiting time keeps the old size */
    write_file("tree\\c1\\g0\\f", 15, true);
    fsz_view(root, true); fsz_want(tp);
    fsz_view(tp, true);
    for (int i = 0; i < 3; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "tree\\c%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(20000), "entered again at once, the children finish");
    CHECK(wait_size("tree\\c1", 35, 5000), "tree\\c1 = 35 right after the change (got %llu)", U(size_of("tree\\c1", NULL)));

    /* a folder being read while the view leaves its walk is not cached afterwards */
    mkdirs("cx"); mkdirs("cx\\a"); mkdirs("cx\\a\\L"); write_file("cx\\a\\L\\f", 10, false);
    fsz_test_delay(200);
    fsz_view(root, true); { char cp[MAX_PATH]; path_of(cp, "cx"); fsz_want(cp); }
    Sleep(500);   /* cx and a are read, L is being read */
    fsz_view(sp, true);
    Sleep(800);
    fsz_test_delay(0);
    { int st; size_of("cx\\a\\L", &st); CHECK(st == FSZ_NONE, "a folder read after its walk was cancelled is not kept (state %d)", st); }

    /* a folder made after the walk of its parent read the parent is measured, not left empty */
    mkdirs("grow");
    for (int i = 0; i < 2; i++) {
        char r[64]; snprintf(r, sizeof r, "grow\\g%d", i); mkdirs(r);
        snprintf(r, sizeof r, "grow\\g%d\\a", i); mkdirs(r);
        snprintf(r, sizeof r, "grow\\g%d\\a\\b", i); mkdirs(r);
        snprintf(r, sizeof r, "grow\\g%d\\a\\b\\c", i); mkdirs(r);
        snprintf(r, sizeof r, "grow\\g%d\\a\\b\\c\\f", i); write_file(r, 4, false);
    }
    char gp[MAX_PATH]; path_of(gp, "grow");
    fsz_test_delay(120);
    fsz_view(root, true); fsz_want(gp);
    Sleep(250);   /* grow is read, the deep chains below it are still running */
    mkdirs("grow\\new"); write_file("grow\\new\\f", 7, false);
    fsz_view(gp, true);
    { const char *gk[] = { "grow\\g0", "grow\\g1", "grow\\new" };
      for (int i = 0; i < 3; i++) { char p[MAX_PATH]; path_of(p, gk[i]); fsz_want(p); } }
    CHECK(wait_open0(20000), "the folders of a walk that missed one finish");
    fsz_test_delay(0);
    CHECK(wait_size("grow\\new", 7, 5000), "the folder made after the walk read its parent = 7 (got %llu)", U(size_of("grow\\new", NULL)));
    CHECK(size_of("grow\\g1", NULL) == 4, "grow\\g1 = 4 (got %llu)", U(size_of("grow\\g1", NULL)));

    /* the cache never drops a shown folder, even when it is over its cap */
    mkdirs("many");
    for (int i = 0; i < 10; i++) { char r[64]; snprintf(r, sizeof r, "many\\m%d", i); mkdirs(r); snprintf(r, sizeof r, "many\\m%d\\f", i); write_file(r, 3, false); }
    char mp[MAX_PATH]; path_of(mp, "many");
    fsz_test_cap(4);
    fsz_view(mp, true);
    for (int i = 0; i < 10; i++) { char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "many\\m%d", i); path_of(p, r); fsz_want(p); }
    CHECK(wait_open0(10000), "the ten shown folders finish");
    { int done = 0; for (int i = 0; i < 10; i++) { char r[64]; int s; snprintf(r, sizeof r, "many\\m%d", i); if (size_of(r, &s) == 3 && s == FSZ_DONE) done++; }
      CHECK(done == 10, "all ten shown folders stay cached over a cap of 4 (%d)", done); }
    fsz_test_cap(0);

    /* sizes_fill asks for one folder at a time: ten thousand lookups must not eat the 50 ms budget */
    mkdirs("wide");
    for (int i = 0; i < 10000; i++) { char r[64]; snprintf(r, sizeof r, "wide\\w%05d", i); mkdirs(r); }
    char wp[MAX_PATH]; path_of(wp, "wide");
    fsz_view(wp, true);
    double worst_ms = 0;   /* the longest one call of the window thread took while the workers were busy */
    LARGE_INTEGER qf, q0, q1; QueryPerformanceFrequency(&qf);
    for (int i = 0; i < 10000; i++) {
        char r[64], p[MAX_PATH]; snprintf(r, sizeof r, "wide\\w%05d", i); path_of(p, r);
        QueryPerformanceCounter(&q0); fsz_want(p); QueryPerformanceCounter(&q1);
        double ms = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart; if (ms > worst_ms) worst_ms = ms;
    }
    { char probe[MAX_PATH]; path_of(probe, "wide\\w00000");
      for (int t = 0; t < 60000; t++) {
          uint64_t v; QueryPerformanceCounter(&q0); fsz_get(probe, &v); int open = fsz_open(); QueryPerformanceCounter(&q1);
          double ms = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart; if (ms > worst_ms) worst_ms = ms;
          if (!open) break;
          Sleep(1);
      } }
    CHECK(wait_open0(60000), "ten thousand shown folders finish");
    printf("longest window call during the walk: %.2f ms\n", worst_ms);
    CHECK(worst_ms < 20.0, "no call of the window waited for the measurement (longest %.2f ms, limit 20)", worst_ms);
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

    /* nothing is measured: a write anywhere reads no directory (there is no watcher) */
    { unsigned n0 = fsz_test_reads(); write_file("a\\f1", 5, true); write_file("wide\\w00001\\z", 5, false); Sleep(1500);
      CHECK(fsz_test_reads() == n0, "writes while nothing is measured read %u directories", fsz_test_reads() - n0); }

    fsz_stop();
    remove_tree(root);
    printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
