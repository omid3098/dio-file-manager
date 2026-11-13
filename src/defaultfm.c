/* Dio File Manager as Windows' default file manager: HKCU registration, the launcher, and the requests Explorer
 * would have received. Nothing here needs administrator rights. See docs/architecture.md. */
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "defaultfm.h"
#include "fs.h"

enum { LOC_RECENT_ = 1, LOC_TRASH_ = 3, LOC_OTHER_ = 4 };   /* the values of LOC_* in ui.h */

static bool test_on;
static wchar_t test_log[MAX_PATH + 16];
static wchar_t classes_base[260], state_base[260], local_dir[MAX_PATH];

static const wchar_t *LAUNCHER_NAME = L"dio-file-manager-launcher.exe";
static const wchar_t *WIN_E = L"{52205fd8-5dfb-447d-801a-d0b52f2e83e1}";

/* The keys the registration writes, under HKCU\Software\Classes. */
static const struct { const wchar_t *path; bool folder; } TARGET[] = {
    { L"Folder\\shell\\open\\command", true },
    { L"Folder\\shell\\explore\\command", true },
    { L"CLSID\\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\\shell\\opennewwindow\\command", false },
};
enum { TARGETS = 3 };

static void paths(void) {
    if (classes_base[0]) return;
    wchar_t root[200] = L"";
    if (test_on) GetEnvironmentVariableW(L"DFM_TEST_REGROOT", root, 200);
    if (root[0]) {
        _snwprintf_s(classes_base, 260, _TRUNCATE, L"%s\\Classes", root);
        _snwprintf_s(state_base, 260, _TRUNCATE, L"%s\\State", root);
    } else {
        wcscpy_s(classes_base, 260, L"Software\\Classes");
        wcscpy_s(state_base, 260, L"Software\\Dio File Manager\\DefaultFileManager");
    }
    DWORD n = 0;
    if (test_on) n = GetEnvironmentVariableW(L"DFM_TEST_LOCAL", local_dir, MAX_PATH);
    if (!n) {
        n = GetEnvironmentVariableW(L"LOCALAPPDATA", local_dir, MAX_PATH);
        if (n && n < MAX_PATH - 20) wcscat_s(local_dir, MAX_PATH, L"\\dio-file-manager");
        else local_dir[0] = 0;
    }
}

void defaultfm_test_mode(const wchar_t *state_base_path) {
    test_on = state_base_path && state_base_path[0];
    if (test_on) _snwprintf_s(test_log, MAX_PATH + 16, _TRUNCATE, L"%s.opened", state_base_path);
    classes_base[0] = 0;
}

static void log_line(const wchar_t *what, const wchar_t *a, const wchar_t *b) {
    FILE *f;
    if (!test_on || !test_log[0] || _wfopen_s(&f, test_log, L"ab") || !f) return;
    fprintf(f, "%ls\t%ls\t%ls\n", what, a ? a : L"", b ? b : L"");   /* the %ls output is the wide text in the ANSI page */
    fclose(f);
}

/* ---- registry helpers ---- */

static void sub(wchar_t *out, const wchar_t *base, const wchar_t *rel) {
    _snwprintf_s(out, 520, _TRUNCATE, rel && rel[0] ? L"%s\\%s" : L"%s", base, rel);
}

static bool copy_values(HKEY from, HKEY to) {
    DWORD names, values;
    if (RegQueryInfoKeyW(from, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &names, &values, NULL, NULL)) return false;
    wchar_t *name = malloc((names + 2) * sizeof *name);
    BYTE *data = malloc(values + 2);
    bool ok = name && data;
    for (DWORD i = 0; ok; i++) {
        DWORD nl = names + 1, dl = values + 1, type;
        LONG r = RegEnumValueW(from, i, name, &nl, NULL, &type, data, &dl);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r || RegSetValueExW(to, name, 0, type, data, dl)) ok = false;
    }
    free(name); free(data);
    return ok;
}

static void clear_values(HKEY k) {
    DWORD names;
    if (RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &names, NULL, NULL, NULL)) return;
    wchar_t *name = malloc((names + 2) * sizeof *name);
    while (name) {
        DWORD nl = names + 1;
        if (RegEnumValueW(k, 0, name, &nl, NULL, NULL, NULL, NULL)) break;
        if (RegDeleteValueW(k, name)) break;
    }
    free(name);
}

static bool key_exists(const wchar_t *path) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k)) return false;
    RegCloseKey(k);
    return true;
}

static bool get_string(HKEY root, const wchar_t *path, const wchar_t *name, wchar_t *out, DWORD cap) {
    HKEY k;
    DWORD type, n = cap * sizeof *out;
    bool ok = !RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &k);
    if (!ok) return false;
    ok = !RegQueryValueExW(k, name, NULL, &type, (BYTE *)out, &n) && (type == REG_SZ || type == REG_EXPAND_SZ);
    RegCloseKey(k);
    if (ok) out[cap - 1] = 0;
    return ok;
}

static bool set_string(HKEY k, const wchar_t *name, const wchar_t *v) {
    return !RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof *v));
}

static bool is_ours(const wchar_t *command) {
    wchar_t low[1200];
    wcsncpy_s(low, 1200, command, _TRUNCATE);
    _wcslwr_s(low, 1200);
    return wcsstr(low, L"-launcher.exe") != NULL;
}

bool defaultfm_enabled(void) {
    wchar_t p[520], c[1200];
    paths();
    sub(p, classes_base, TARGET[0].path);
    return get_string(HKEY_CURRENT_USER, p, L"", c, 1200) && is_ours(c);
}

/* Remember how one target looked: whether it existed, the first key that did not, and its values. */
static bool save_target(HKEY saved, int i) {
    wchar_t name[16], rel[520], full[520];
    _snwprintf_s(name, 16, _TRUNCATE, L"%d", i);
    HKEY k, v;
    if (RegCreateKeyExW(saved, name, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return false;
    bool ok = set_string(k, L"Path", TARGET[i].path);
    sub(full, classes_base, TARGET[i].path);
    HKEY leaf;
    DWORD existed = !RegOpenKeyExW(HKEY_CURRENT_USER, full, 0, KEY_READ, &leaf);
    ok = ok && !RegSetValueExW(k, L"Existed", 0, REG_DWORD, (BYTE *)&existed, sizeof existed);
    if (existed) {
        ok = ok && !RegCreateKeyExW(k, L"Values", 0, NULL, 0, KEY_WRITE, NULL, &v, NULL);
        if (ok) { ok = copy_values(leaf, v); RegCloseKey(v); }
        RegCloseKey(leaf);
    } else {
        /* the top of the chain of keys that the registration will create */
        size_t len = wcslen(TARGET[i].path);
        rel[0] = 0;
        for (size_t j = 0; j <= len; j++) {
            if (TARGET[i].path[j] != L'\\' && TARGET[i].path[j]) continue;
            wcsncpy_s(rel, 520, TARGET[i].path, j);
            sub(full, classes_base, rel);
            if (!key_exists(full)) break;
        }
        ok = ok && set_string(k, L"Top", rel);
    }
    RegCloseKey(k);
    return ok;
}

static void restore_target(HKEY saved, int i) {
    wchar_t name[16], path[520], top[520], full[520];
    _snwprintf_s(name, 16, _TRUNCATE, L"%d", i);
    HKEY k;
    DWORD existed = 0, n = sizeof existed;
    if (!RegOpenKeyExW(saved, name, 0, KEY_READ, &k)) {
        RegQueryValueExW(k, L"Existed", NULL, NULL, (BYTE *)&existed, &n);
        DWORD tn = sizeof top, tt;
        if (RegQueryValueExW(k, L"Top", NULL, &tt, (BYTE *)top, &tn) || tt != REG_SZ) top[0] = 0;
        sub(path, classes_base, TARGET[i].path);
        if (existed) {
            HKEY leaf, v;
            if (!RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &leaf, NULL)) {
                clear_values(leaf);
                if (!RegOpenKeyExW(k, L"Values", 0, KEY_READ, &v)) { copy_values(v, leaf); RegCloseKey(v); }
                RegCloseKey(leaf);
            }
        } else {
            sub(full, classes_base, top[0] ? top : TARGET[i].path);
            RegDeleteTreeW(HKEY_CURRENT_USER, full);
        }
        RegCloseKey(k);
    }
}

/* Without a record, remove only what carries the launcher's name. */
static void remove_ours_without_record(void) {
    for (int i = 0; i < TARGETS; i++) {
        wchar_t p[520], c[1200];
        sub(p, classes_base, TARGET[i].path);
        if (get_string(HKEY_CURRENT_USER, p, L"", c, 1200) && is_ours(c)) RegDeleteTreeW(HKEY_CURRENT_USER, p);
    }
}

static void quoted_command(wchar_t *out, const wchar_t *launcher, const wchar_t *extra, bool folder) {
    _snwprintf_s(out, 1200, _TRUNCATE, L"\"%s\"%s%s%s", launcher, extra && extra[0] ? L" " : L"",
                 extra ? extra : L"", folder ? L" \"%1\"" : L"");
}

static bool write_commands(const wchar_t *launcher, const wchar_t *extra) {
    for (int i = 0; i < TARGETS; i++) {
        wchar_t p[520], c[1200];
        HKEY k;
        sub(p, classes_base, TARGET[i].path);
        if (RegCreateKeyExW(HKEY_CURRENT_USER, p, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return false;
        quoted_command(c, launcher, extra, TARGET[i].folder);
        bool ok = set_string(k, L"", c) && set_string(k, L"DelegateExecute", L"");
        RegCloseKey(k);
        if (!ok) return false;
    }
    return true;
}

static bool launcher_path(wchar_t *out, size_t cap) {
    paths();
    if (!local_dir[0]) return false;
    _snwprintf_s(out, cap, _TRUNCATE, L"%s\\%s", local_dir, LAUNCHER_NAME);
    return true;
}

static bool same_size(const wchar_t *a, const wchar_t *b) {
    WIN32_FILE_ATTRIBUTE_DATA x, y;
    return GetFileAttributesExW(a, GetFileExInfoStandard, &x) && GetFileAttributesExW(b, GetFileExInfoStandard, &y) &&
           x.nFileSizeLow == y.nFileSizeLow && x.nFileSizeHigh == y.nFileSizeHigh;
}

static bool copy_launcher(const wchar_t *self, const wchar_t *launcher) {
    CreateDirectoryW(local_dir, NULL);
    if (CopyFileW(self, launcher, FALSE)) return true;
    return same_size(self, launcher);    /* a launcher in use is the same program */
}

bool defaultfm_enable(const wchar_t *extra_args) {
    wchar_t self[MAX_PATH], launcher[MAX_PATH];
    paths();
    if (defaultfm_is_launcher() || !GetModuleFileNameW(NULL, self, MAX_PATH) || !launcher_path(launcher, MAX_PATH)) return false;
    HKEY state;
    if (!defaultfm_enabled()) {      /* the keys are not ours: save what is there now */
        RegDeleteTreeW(HKEY_CURRENT_USER, state_base);
        HKEY saved = NULL;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, state_base, 0, NULL, 0, KEY_WRITE, NULL, &state, NULL)) return false;
        bool ok = !RegCreateKeyExW(state, L"Saved", 0, NULL, 0, KEY_WRITE, NULL, &saved, NULL);
        for (int i = 0; ok && i < TARGETS; i++) ok = save_target(saved, i);
        if (saved) RegCloseKey(saved);
        RegCloseKey(state);
        if (!ok) { RegDeleteTreeW(HKEY_CURRENT_USER, state_base); return false; }
    } else if (!key_exists(state_base)) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, state_base, 0, NULL, 0, KEY_WRITE, NULL, &state, NULL)) return false;
        RegCloseKey(state);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, state_base, 0, KEY_WRITE, &state)) return false;
    bool ok = set_string(state, L"App", self) && set_string(state, L"Launcher", launcher) &&
              set_string(state, L"Args", extra_args ? extra_args : L"");
    RegCloseKey(state);
    ok = ok && copy_launcher(self, launcher) && write_commands(launcher, extra_args);
    if (!ok) defaultfm_disable();
    return ok;
}

bool defaultfm_disable(void) {
    HKEY state, saved;
    wchar_t launcher[MAX_PATH] = L"";
    paths();
    if (get_string(HKEY_CURRENT_USER, state_base, L"Launcher", launcher, MAX_PATH)) {   /* the copy goes too; a running one stays */
        const wchar_t *base = wcsrchr(launcher, L'\\');
        if (base && !_wcsicmp(base + 1, LAUNCHER_NAME) && !defaultfm_is_launcher() && DeleteFileW(launcher)) {
            launcher[base - launcher] = 0;
            RemoveDirectoryW(launcher);                  /* only when nothing else is in it */
        }
    }
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, state_base, 0, KEY_READ, &state)) {
        if (!RegOpenKeyExW(state, L"Saved", 0, KEY_READ, &saved)) {
            for (int i = TARGETS - 1; i >= 0; i--) restore_target(saved, i);
            RegCloseKey(saved);
        } else remove_ours_without_record();
        RegCloseKey(state);
        RegDeleteTreeW(HKEY_CURRENT_USER, state_base);
        if (!wcscmp(state_base, L"Software\\Dio File Manager\\DefaultFileManager")) RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Dio File Manager");
    } else remove_ours_without_record();
    return !defaultfm_enabled();
}

void defaultfm_sync(void) {
    wchar_t self[MAX_PATH], app[MAX_PATH], launcher[MAX_PATH];
    paths();
    if (!defaultfm_enabled() || !GetModuleFileNameW(NULL, self, MAX_PATH) || !launcher_path(launcher, MAX_PATH)) return;
    if (get_string(HKEY_CURRENT_USER, state_base, L"App", app, MAX_PATH) && _wcsicmp(app, self) &&
        GetFileAttributesW(app) == INVALID_FILE_ATTRIBUTES) {   /* the exe moved: follow it */
        HKEY k;
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, state_base, 0, KEY_WRITE, &k)) { set_string(k, L"App", self); RegCloseKey(k); }
    }
    if (!same_size(self, launcher)) copy_launcher(self, launcher);
}

/* ---- the requests ---- */

static bool eq_nocase(const wchar_t *a, const wchar_t *b) { return !_wcsicmp(a, b); }

static bool place_of(const wchar_t *s, int *loc) {
    static const struct { const wchar_t *name; int loc; } P[] = {
        { L"::{645FF040-5081-101B-9F08-00AA002F954E}", LOC_TRASH_ }, { L"shell:::{645FF040-5081-101B-9F08-00AA002F954E}", LOC_TRASH_ },
        { L"shell:RecycleBinFolder", LOC_TRASH_ },
        { L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", LOC_OTHER_ }, { L"shell:::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", LOC_OTHER_ },
        { L"shell:MyComputerFolder", LOC_OTHER_ },
        { L"shell:Recent", LOC_RECENT_ },
    };
    for (size_t i = 0; i < sizeof P / sizeof *P; i++) if (eq_nocase(s, P[i].name)) { *loc = P[i].loc; return true; }
    return false;
}

/* The Win+E target arrives as the empty command, or as its own class name. */
static bool is_home(const wchar_t *s) {
    wchar_t a[80], b[80];
    _snwprintf_s(a, 80, _TRUNCATE, L"::%s", WIN_E);
    _snwprintf_s(b, 80, _TRUNCATE, L"shell:::%s", WIN_E);
    return !s[0] || eq_nocase(s, a) || eq_nocase(s, b);
}

static bool dir_exists(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* A name the file system can open: absolute, with a drive letter or a share. */
static bool full_path(const wchar_t *in, wchar_t *out) {
    wchar_t tmp[DFM_REQ_PATH];
    wcsncpy_s(tmp, DFM_REQ_PATH, in, _TRUNCATE);
    size_t l = wcslen(tmp);
    if (l == 2 && tmp[1] == L':') { tmp[2] = L'\\'; tmp[3] = 0; }
    DWORD n = GetFullPathNameW(tmp, DFM_REQ_PATH, out, NULL);
    return n && n < DFM_REQ_PATH;
}

static bool known_place_path(const wchar_t *s, wchar_t *out) {
    PIDLIST_ABSOLUTE pidl = NULL;
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   /* this runs before the window's own COM start */
    bool ok = false;
    if (SUCCEEDED(SHParseDisplayName(s, NULL, &pidl, 0, NULL)) && pidl) {
        ok = SHGetPathFromIDListEx(pidl, out, DFM_REQ_PATH, GPFIDL_DEFAULT) && dir_exists(out);
        CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return ok;
}

void defaultfm_request(wchar_t **args, int n, dfm_request *out) {
    wchar_t target[DFM_REQ_PATH] = L"", select[DFM_REQ_PATH] = L"", full[DFM_REQ_PATH];
    bool select_wanted = false, have_target = false;
    memset(out, 0, sizeof *out);
    out->kind = REQ_FALLBACK;
    for (int i = 0; i < n; i++) {
        const wchar_t *a = args[i];
        while (a && (a[0] == L'/' || a[0] == L'-')) {   /* a comma may carry another switch: /e,/select,C:\file */
            const wchar_t *comma = wcschr(a, L',');
            size_t len = comma ? (size_t)(comma - a) : wcslen(a);
            const wchar_t *rest = comma ? comma + 1 : L"";
            bool sel = len == 7 && !_wcsnicmp(a + 1, L"select", 6);
            bool root = len == 5 && !_wcsnicmp(a + 1, L"root", 4);
            bool e = len == 2 && (a[1] == L'e' || a[1] == L'E');
            if (sel || root || e) {
                if (!rest[0] && sel && i + 1 < n) rest = args[++i];      /* "/select, C:\file" in two arguments */
                if (e && (rest[0] == L'/' || rest[0] == L'-')) { a = rest; continue; }
                if (rest[0]) { wcsncpy_s(sel ? select : target, DFM_REQ_PATH, rest, _TRUNCATE); if (sel) select_wanted = true; else have_target = true; }
                a = NULL;
                continue;
            }
            if (len == 2 && (a[1] == L'n' || a[1] == L'N')) { a = NULL; continue; }
            if (!_wcsicmp(a, L"/separate") || !_wcsicmp(a, L"-Embedding")) { a = NULL; continue; }
            return;                                                    /* a switch we do not know */
        }
        if (!a) continue;
        if (!have_target) { wcsncpy_s(target, DFM_REQ_PATH, a, _TRUNCATE); have_target = true; }
        else return;
    }
    /* The registered "%1" for C:\ is "C:\": the backslash escapes the closing quote and the argument is C:" */
    { size_t l = wcslen(target); while (l && target[l - 1] == L'"') target[--l] = 0; }
    { size_t l = wcslen(select); while (l && select[l - 1] == L'"') select[--l] = 0; }
    if (select_wanted) {
        if (!full_path(select, full) || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) return;
        size_t l = wcslen(full);
        while (l > 3 && full[l - 1] == L'\\') full[--l] = 0;
        wchar_t *slash = wcsrchr(full, L'\\');
        if (l <= 3 || !slash) { out->kind = REQ_FOLDER; fs_w_to_u8(full, out->path, DFM_REQ_PATH); return; }   /* a drive root */
        wcsncpy_s(select, DFM_REQ_PATH, slash + 1, _TRUNCATE);
        if (slash == full + 2) full[3] = 0; else *slash = 0;        /* a file in a drive root is selected in that root */
        out->kind = REQ_FOLDER;
        fs_w_to_u8(full, out->path, DFM_REQ_PATH);
        fs_w_to_u8(select, out->select, DFM_REQ_PATH);
        return;
    }
    int loc;
    if (is_home(target)) { out->kind = REQ_HOME; return; }
    if (place_of(target, &loc)) { out->kind = REQ_PLACE; out->loc = loc; return; }
    if (!_wcsnicmp(target, L"shell:", 6) || !wcsncmp(target, L"::{", 3)) {
        if (known_place_path(target, full)) { out->kind = REQ_FOLDER; fs_w_to_u8(full, out->path, DFM_REQ_PATH); }
        return;
    }
    if (full_path(target, full) && dir_exists(full)) { out->kind = REQ_FOLDER; fs_w_to_u8(full, out->path, DFM_REQ_PATH); }
}

void defaultfm_arrival(const dfm_request *r) {
    wchar_t path[DFM_REQ_PATH] = L"", select[DFM_REQ_PATH] = L"", loc[8];
    if (!test_on) return;
    fs_u8_to_w(r->path, path, DFM_REQ_PATH);
    fs_u8_to_w(r->select, select, DFM_REQ_PATH);
    _snwprintf_s(loc, 8, _TRUNCATE, L"%d", r->loc);
    { wchar_t pid[16]; _snwprintf_s(pid, 16, _TRUNCATE, L"%lu", GetCurrentProcessId()); log_line(L"pid", pid, NULL); }
    if (r->kind == REQ_FOLDER) log_line(L"arrive-folder", path, select);
    else if (r->kind == REQ_PLACE) log_line(L"arrive-place", loc, NULL);
    else log_line(L"arrive-home", NULL, NULL);
}

/* ---- the real Explorer ---- */

static unsigned hash_text(const wchar_t *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned)*s) * 16777619u;
    return h;
}

static void add_arg(wchar_t *cmd, size_t cap, const wchar_t *a) {
    wcscat_s(cmd, cap, L" ");
    const wchar_t *comma = a[0] == L'/' ? wcschr(a, L',') : NULL;
    if (comma && comma[1] && !wcschr(a, L'"')) {                    /* /select,"C:\a b" keeps its comma outside the quotes */
        wcsncat_s(cmd, cap, a, (size_t)(comma - a) + 1);
        wcscat_s(cmd, cap, L"\""); wcscat_s(cmd, cap, comma + 1); wcscat_s(cmd, cap, L"\"");
    } else if (a[0] == L'/' || a[0] == L'-') wcscat_s(cmd, cap, a);
    else { wcscat_s(cmd, cap, L"\""); wcscat_s(cmd, cap, a); wcscat_s(cmd, cap, L"\""); }
}

/* The loop guard. Explorer does not call back into this app when it opens a place, and that is what the checks
 * prove with a stand-in that does call back. A call back would come within milliseconds, so the same place is
 * handed to Explorer once per two seconds at most; the second request is refused, not forwarded. */
static bool guard_allows(const wchar_t *cmd) {
    wchar_t file[MAX_PATH + 24];
    paths();
    if (!local_dir[0]) return false;
    CreateDirectoryW(local_dir, NULL);
    _snwprintf_s(file, MAX_PATH + 24, _TRUNCATE, L"%s\\fallback.txt", local_dir);
    unsigned long long now = GetTickCount64(), then = 0;
    unsigned h = hash_text(cmd), old = 0;
    FILE *f;
    if (!_wfopen_s(&f, file, L"rb") && f) {
        if (fscanf_s(f, "%llu %u", &then, &old) != 2) then = 0;
        fclose(f);
    }
    if (then && old == h && now >= then && now - then < 2000) return false;
    if (_wfopen_s(&f, file, L"wb") || !f) return false;
    fprintf(f, "%llu %u", now, h);
    fclose(f);
    return true;
}

bool defaultfm_fallback(wchar_t **args, int n) {
    wchar_t cmd[4096], windir[MAX_PATH], stand_in[1024];
    if (!GetWindowsDirectoryW(windir, MAX_PATH)) return false;
    _snwprintf_s(cmd, 4096, _TRUNCATE, L"\"%s\\explorer.exe\"", windir);
    for (int i = 0; i < n; i++) add_arg(cmd, 4096, args[i]);
    if (!guard_allows(cmd + wcslen(windir) + 15)) { log_line(L"refused", cmd, NULL); return false; }
    log_line(L"explorer", cmd, NULL);
    if (test_on) {   /* a check never starts the real Explorer */
        if (!GetEnvironmentVariableW(L"DFM_TEST_EXPLORER", stand_in, 1024)) return true;
        wchar_t run[4096];
        _snwprintf_s(run, 4096, _TRUNCATE, L"%s%s", stand_in, cmd + wcslen(windir) + 15);
        STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
        if (!CreateProcessW(NULL, run, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return true;
    }
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

/* ---- the launcher ---- */

bool defaultfm_is_launcher(void) {
    wchar_t self[MAX_PATH];
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return false;
    const wchar_t *base = wcsrchr(self, L'\\');
    return base && !_wcsicmp(base + 1, LAUNCHER_NAME);
}

static const wchar_t *command_tail(void) {
    const wchar_t *c = GetCommandLineW();
    if (*c == L'"') { c = wcschr(c + 1, L'"'); if (c) c++; }
    else while (*c && *c != L' ') c++;
    while (c && *c == L' ') c++;
    return c ? c : L"";
}

int defaultfm_launcher_main(void) {
    int n = 0, kept = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &n), **args;
    wchar_t app[MAX_PATH], cmd[32768];
    const wchar_t *tail = command_tail();
    if (!argv) return 1;
    args = calloc((size_t)n + 1, sizeof *args);
    for (int i = 1; i < n; i++) {
        if (!wcscmp(argv[i], L"--test-state") && i + 1 < n) { defaultfm_test_mode(argv[i + 1]); i++; continue; }
        args[kept++] = argv[i];
    }
    paths();
    if (get_string(HKEY_CURRENT_USER, state_base, L"App", app, MAX_PATH) && GetFileAttributesW(app) != INVALID_FILE_ATTRIBUTES) {
        _snwprintf_s(cmd, 32768, _TRUNCATE, L"\"%s\" %s", app, tail);
        STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
        if (CreateProcessW(app, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 0; }
    }
    /* The app is gone: give Explorer its keys back first, then hand it the request. Nothing can call back here. */
    defaultfm_disable();
    log_line(L"restored", NULL, NULL);
    bool ok = defaultfm_fallback(args, kept);
    wchar_t self[MAX_PATH];
    if (GetModuleFileNameW(NULL, self, MAX_PATH) && !test_on) {
        wchar_t c[MAX_PATH + 80], sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        _snwprintf_s(c, MAX_PATH + 80, _TRUNCATE, L"\"%s\\cmd.exe\" /d /c ping -n 4 127.0.0.1 >nul & del /f /q \"%s\"", sys, self);
        STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    }
    free(args); LocalFree(argv);
    return ok ? 0 : 4;
}
