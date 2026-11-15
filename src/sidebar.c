#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sidebar.h"
#include "fs.h"

enum { HIDE_MAX = 48, KEY_MAX = 16, ADD_MAX = 200 };
static char hidden[HIDE_MAX][KEY_MAX];
static int hidden_n;
static char *added[ADD_MAX];
static int added_n;
static bool gone_flag[ADD_MAX];
static ULONGLONG gone_at;   /* when the missing-folder flags were last read; 0 forces a read */

static void norm(const char *in, char *out, size_t cap) {
    snprintf(out, cap, "%s", in);
    size_t l = strlen(out);
    while (l > 3 && out[l - 1] == '\\') out[--l] = 0;
}

static int find_add(const char *p) {
    char n[FS_PATH_MAX];
    norm(p, n, sizeof n);
    for (int i = 0; i < added_n; i++) if (_stricmp(added[i], n) == 0) return i;
    return -1;
}

static int find_hidden(const char *key) {
    for (int i = 0; i < hidden_n; i++) if (!strcmp(hidden[i], key)) return i;
    return -1;
}

static bool file_path(wchar_t *path, size_t cap, const wchar_t *name) {
    wchar_t root[FS_PATH_MAX];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", root, FS_PATH_MAX);
    if (!n || n >= FS_PATH_MAX) return false;
    swprintf_s(path, cap, L"%s\\dio-file-manager", root);
    CreateDirectoryW(path, NULL);
    swprintf_s(path, cap, L"%s\\dio-file-manager\\%s", root, name);
    return true;
}

static bool save(void) {
    wchar_t path[FS_PATH_MAX], tmp[FS_PATH_MAX];
    if (!file_path(path, FS_PATH_MAX, L"sidebar.txt") || !file_path(tmp, FS_PATH_MAX, L"sidebar.tmp")) return false;
    FILE *f = _wfopen(tmp, L"wb");
    if (!f) return false;
    bool ok = true;
    for (int i = 0; i < hidden_n && ok; i++) ok = fprintf(f, "hide %s\n", hidden[i]) > 0;
    for (int i = 0; i < added_n && ok; i++) ok = fprintf(f, "add %s\n", added[i]) > 0;
    if (fclose(f)) ok = false;
    if (ok) ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp);
    return ok;
}

void sidebar_load(void) {
    wchar_t path[FS_PATH_MAX];
    hidden_n = added_n = 0; gone_at = 0;
    if (!file_path(path, FS_PATH_MAX, L"sidebar.txt")) return;
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    char line[FS_PATH_MAX + 8];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
        if (!strncmp(line, "hide ", 5) && strlen(line + 5) < KEY_MAX && hidden_n < HIDE_MAX && find_hidden(line + 5) < 0)
            snprintf(hidden[hidden_n++], KEY_MAX, "%s", line + 5);
        else if (!strncmp(line, "add ", 4) && line[4] && added_n < ADD_MAX && find_add(line + 4) < 0) {
            char n[FS_PATH_MAX];
            norm(line + 4, n, sizeof n);
            if ((added[added_n] = _strdup(n)) != NULL) added_n++;
        }
    }
    fclose(f);
}

bool sidebar_is_hidden(const char *key) { return find_hidden(key) >= 0; }
bool sidebar_any_hidden(void) { return hidden_n > 0; }

bool sidebar_hide(const char *key) {
    if (strlen(key) >= KEY_MAX || find_hidden(key) >= 0) return true;
    if (hidden_n == HIDE_MAX) return false;
    snprintf(hidden[hidden_n++], KEY_MAX, "%s", key);
    return save();
}

bool sidebar_show_hidden(void) {
    if (!hidden_n) return true;
    hidden_n = 0;
    return save();
}

int sidebar_count(void) { return added_n; }
const char *sidebar_path(int i) { return i >= 0 && i < added_n ? added[i] : ""; }
bool sidebar_has(const char *path) { return find_add(path) >= 0; }

bool sidebar_add(const char *path) {
    char n[FS_PATH_MAX];
    if (!path[0] || find_add(path) >= 0) return true;
    if (added_n == ADD_MAX) return false;
    norm(path, n, sizeof n);
    if (!(added[added_n] = _strdup(n))) return false;
    added_n++; gone_at = 0;
    return save();
}

bool sidebar_remove(const char *path) {
    int i = find_add(path);
    if (i < 0) return true;
    free(added[i]);
    memmove(&added[i], &added[i + 1], (size_t)(added_n - i - 1) * sizeof *added);
    added_n--; gone_at = 0;
    return save();
}

bool sidebar_gone(int i) {
    ULONGLONG now = GetTickCount64();
    if (!gone_at || now - gone_at >= 1000) {
        gone_at = now;
        for (int k = 0; k < added_n; k++) {
            wchar_t w[FS_PATH_MAX], root[4] = { 0, L':', L'\\', 0 };
            gone_flag[k] = false;
            if (!MultiByteToWideChar(CP_UTF8, 0, added[k], -1, w, FS_PATH_MAX)) continue;
            root[0] = w[0];
            /* a network drive or share may take seconds to answer: such a folder is never asked about */
            if (w[0] == L'\\' || (w[1] == L':' && GetDriveTypeW(root) == DRIVE_REMOTE)) continue;
            gone_flag[k] = GetFileAttributesW(w) == INVALID_FILE_ATTRIBUTES;
        }
    }
    return i >= 0 && i < added_n && gone_flag[i];
}

void sidebar_leaf(const char *path, char *out, size_t cap) {
    const char *leaf = strrchr(path, '\\');
    if (!leaf || strlen(path) <= 3) snprintf(out, cap, "%s", path);
    else snprintf(out, cap, "%s", leaf + 1);
}
