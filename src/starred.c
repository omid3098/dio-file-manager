#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "starred.h"
#include "fs.h"

static CRITICAL_SECTION lock;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static char **paths;
static int n, cap;

static BOOL CALLBACK init_lock(PINIT_ONCE o, PVOID p, PVOID *c) { (void)o; (void)p; (void)c; InitializeCriticalSection(&lock); return TRUE; }
static void enter(void) { InitOnceExecuteOnce(&once, init_lock, NULL, NULL); EnterCriticalSection(&lock); }

static bool file_path(wchar_t *dir, wchar_t *file) {
    wchar_t base[MAX_PATH];
    DWORD k = GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH);
    if (!k || k >= MAX_PATH) return false;
    swprintf(dir, MAX_PATH, L"%ls\\dio-file-manager", base);
    swprintf(file, MAX_PATH, L"%ls\\starred.txt", dir);
    return true;
}

static int find(const char *p) {
    for (int i = 0; i < n; i++) if (_stricmp(paths[i], p) == 0) return i;
    return -1;
}

static void norm(const char *in, char *out, size_t cap_out) {
    snprintf(out, cap_out, "%s", in);
    size_t l = strlen(out);
    while (l > 3 && out[l - 1] == '\\') out[--l] = 0;
}

static void add_unlocked(const char *p) {
    if (!p[0] || find(p) >= 0) return;
    if (n == cap) {
        int nc = cap ? cap * 2 : 32;
        char **np = realloc(paths, (size_t)nc * sizeof *np);
        if (!np) return;
        paths = np; cap = nc;
    }
    paths[n] = _strdup(p);
    if (paths[n]) n++;
}

void starred_load(void) {
    wchar_t dir[MAX_PATH], file[MAX_PATH];
    enter();
    for (int i = 0; i < n; i++) free(paths[i]);
    n = 0;
    if (file_path(dir, file)) {
        HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD size = GetFileSize(h, NULL), got = 0;
            if (size != INVALID_FILE_SIZE && size < (16u << 20)) {
                char *buf = malloc((size_t)size + 1);
                if (buf && ReadFile(h, buf, size, &got, NULL)) {
                    buf[got] = 0;
                    char *s = buf;
                    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
                    for (char *line = strtok(s, "\n"); line; line = strtok(NULL, "\n")) {
                        size_t l = strlen(line);
                        while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
                        char p[FS_PATH_MAX];
                        norm(line, p, sizeof p);
                        add_unlocked(p);
                    }
                }
                free(buf);
            }
            CloseHandle(h);
        }
    }
    LeaveCriticalSection(&lock);
}

static bool save_unlocked(void) {
    wchar_t dir[MAX_PATH], file[MAX_PATH], tmp[MAX_PATH + 8];
    if (!file_path(dir, file)) return false;
    CreateDirectoryW(dir, NULL);
    swprintf(tmp, MAX_PATH + 8, L"%ls.tmp", file);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        DWORD w;
        size_t l = strlen(paths[i]);
        ok = WriteFile(h, paths[i], (DWORD)l, &w, NULL) && WriteFile(h, "\n", 1, &w, NULL);
    }
    CloseHandle(h);
    if (ok) ok = MoveFileExW(tmp, file, MOVEFILE_REPLACE_EXISTING) != 0;
    if (!ok) DeleteFileW(tmp);
    return ok;
}

bool starred_is(const char *path) {
    char p[FS_PATH_MAX];
    norm(path, p, sizeof p);
    enter();
    bool r = find(p) >= 0;
    LeaveCriticalSection(&lock);
    return r;
}

bool starred_set(const char *path, bool on) {
    char p[FS_PATH_MAX];
    norm(path, p, sizeof p);
    enter();
    int i = find(p);
    bool changed = false;
    if (on && i < 0) { add_unlocked(p); changed = true; }
    else if (!on && i >= 0) { free(paths[i]); memmove(&paths[i], &paths[i + 1], (size_t)(n - i - 1) * sizeof *paths); n--; changed = true; }
    bool ok = changed ? save_unlocked() : true;
    LeaveCriticalSection(&lock);
    return ok;
}

int starred_count(void) { enter(); int r = n; LeaveCriticalSection(&lock); return r; }

bool starred_get(int i, char *out, size_t cap_out) {
    enter();
    bool ok = i >= 0 && i < n;
    if (ok) snprintf(out, cap_out, "%s", paths[i]);
    LeaveCriticalSection(&lock);
    return ok;
}
