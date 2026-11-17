/* Dio File Manager as Windows' default file manager: HKCU registration, the launcher, and the requests Explorer
 * would have received. Nothing here needs administrator rights. See docs/architecture.md. */
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "defaultfm.h"
#include "defaultfm_int.h"
#include "resident.h"
#include "fs.h"

enum { LOC_RECENT_ = 1, LOC_TRASH_ = 3, LOC_OTHER_ = 4 };   /* the values of LOC_* in ui.h */

static bool test_on;
static wchar_t test_log[MAX_PATH + 16], test_res_log[MAX_PATH + 16];
static wchar_t classes_base[260], state_base[260], adv_base[260], run_base[260], runonce_base[260], local_dir[MAX_PATH];
static wchar_t test_arg[MAX_PATH];   /* the --test-state text: a check's sign-in cleanup command carries it */

static const wchar_t *LAUNCHER_NAME = L"dio-file-manager-launcher.exe";
static const wchar_t *CLEANUP_VALUE = L"Dio File Manager launcher cleanup";   /* RunOnce: deletes the launcher at the next sign-in */
static const wchar_t *WIN_E = L"{52205fd8-5dfb-447d-801a-d0b52f2e83e1}";

/* The keys the registration writes, under HKCU\Software\Classes. */
static const struct { const wchar_t *path; bool folder; } TARGET[] = {
    { L"Folder\\shell\\open\\command", true },
    { L"Folder\\shell\\explore\\command", true },
    { L"CLSID\\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\\shell\\opennewwindow\\command", false },
    /* Windows' own copy of this class has "open" as well as "opennewwindow", both with a DelegateExecute of the class
     * itself, and "open" is the verb a launch without a verb gets: both are overridden, so neither reaches Explorer. */
    { L"CLSID\\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\\shell\\open\\command", false },
};
enum { TARGETS = 4 };

static void paths(void) {
    if (classes_base[0]) return;
    wchar_t root[200] = L"";
    if (test_on) GetEnvironmentVariableW(L"DFM_TEST_REGROOT", root, 200);
    if (root[0]) {
        _snwprintf_s(classes_base, 260, _TRUNCATE, L"%s\\Classes", root);
        _snwprintf_s(state_base, 260, _TRUNCATE, L"%s\\State", root);
        _snwprintf_s(adv_base, 260, _TRUNCATE, L"%s\\Advanced", root);
        _snwprintf_s(run_base, 260, _TRUNCATE, L"%s\\Run", root);
        _snwprintf_s(runonce_base, 260, _TRUNCATE, L"%s\\RunOnce", root);
    } else {
        wcscpy_s(classes_base, 260, L"Software\\Classes");
        wcscpy_s(state_base, 260, L"Software\\Dio File Manager\\DefaultFileManager");
        wcscpy_s(adv_base, 260, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced");
        wcscpy_s(run_base, 260, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
        wcscpy_s(runonce_base, 260, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce");
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
    wcsncpy_s(test_arg, MAX_PATH, test_on ? state_base_path : L"", _TRUNCATE);
    if (test_on) {
        _snwprintf_s(test_log, MAX_PATH + 16, _TRUNCATE, L"%s.opened", state_base_path);
        _snwprintf_s(test_res_log, MAX_PATH + 16, _TRUNCATE, L"%s.resident", state_base_path);
    }
    classes_base[0] = 0;
}

/* What resident.c needs: the (possibly redirected) registry bases and folder, the test log of the resident process,
 * and the small helpers it shares with this file. */
bool dfm_test_on(void) { return test_on; }
const wchar_t *dfm_state_base(void) { paths(); return state_base; }
const wchar_t *dfm_adv_base(void) { paths(); return adv_base; }
const wchar_t *dfm_run_base(void) { paths(); return run_base; }
const wchar_t *dfm_test_resident_log(void) { return test_on ? test_res_log : NULL; }

/* One line appended to a check's log. fopen_s opens a file for its writer alone, so a check that was reading the log at
 * that moment made the open fail and the line vanish (1 in 30 runs of the Win+E checks). The file is opened shared in
 * every way, and an open that still fails is tried again for a moment. Only checks write logs. */
void dfm_log_text(const wchar_t *file, const char *line) {
    HANDLE f = INVALID_HANDLE_VALUE;
    DWORD n;
    for (int tries = 0; tries < 100 && f == INVALID_HANDLE_VALUE; tries++) {
        f = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) Sleep(2);
    }
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, line, (DWORD)strlen(line), &n, NULL);
    CloseHandle(f);
}

static void log_line(const wchar_t *what, const wchar_t *a, const wchar_t *b) {
    wchar_t wide[2048];
    char line[4096];
    if (!test_on || !test_log[0]) return;
    _snwprintf_s(wide, 2048, _TRUNCATE, L"%s\t%s\t%s\n", what, a ? a : L"", b ? b : L"");
    /* The log is in the ANSI page; a letter the page lacks (CJK, in the long-name check) becomes '?'. snprintf's %ls gives up
     * at such a letter and returns an empty line, which made that arrival vanish. */
    if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, line, sizeof line, NULL, NULL)) return;
    dfm_log_text(test_log, line);
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

static bool clear_values(HKEY k) {
    DWORD names;
    if (RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &names, NULL, NULL, NULL)) return false;
    wchar_t *name = malloc((names + 2) * sizeof *name);
    bool ok = name != NULL;
    while (ok) {
        DWORD nl = names + 1;
        LONG r = RegEnumValueW(k, 0, name, &nl, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) break;
        ok = !r && !RegDeleteValueW(k, name);
    }
    free(name);
    return ok;
}

static bool key_exists(const wchar_t *path) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k)) return false;
    RegCloseKey(k);
    return true;
}

static bool key_empty(const wchar_t *path) {
    HKEY k;
    DWORD subkeys = 1, values = 1;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k)) return false;
    RegQueryInfoKeyW(k, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    return !subkeys && !values;
}

bool dfm_get_string(HKEY root, const wchar_t *path, const wchar_t *name, wchar_t *out, DWORD cap) {
    HKEY k;
    DWORD type, n = cap * sizeof *out;
    bool ok = !RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &k);
    if (!ok) return false;
    ok = !RegQueryValueExW(k, name, NULL, &type, (BYTE *)out, &n) && (type == REG_SZ || type == REG_EXPAND_SZ);
    RegCloseKey(k);
    if (ok) out[cap - 1] = 0;
    return ok;
}

/* A registry error that means "it is not there"; every other error means "could not look". */
bool dfm_is_missing(LONG r) { return r == ERROR_FILE_NOT_FOUND || r == ERROR_PATH_NOT_FOUND; }

/* Read a key's default string. 0: read. ERROR_FILE_NOT_FOUND: no key, no value, or not a string (too long counts
 * too: nothing of ours is that long). Any other code: the registry refused, so nothing is known. */
static LONG read_default(const wchar_t *path, wchar_t *out, DWORD cap) {
    HKEY k;
    DWORD type, n = cap * sizeof *out;
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &k);
    if (r) return dfm_is_missing(r) ? ERROR_FILE_NOT_FOUND : r;
    r = RegQueryValueExW(k, L"", NULL, &type, (BYTE *)out, &n);
    RegCloseKey(k);
    if (r == ERROR_MORE_DATA || (!r && type != REG_SZ && type != REG_EXPAND_SZ)) return ERROR_FILE_NOT_FOUND;
    if (!r) out[cap - 1] = 0;
    return dfm_is_missing(r) ? ERROR_FILE_NOT_FOUND : r;
}

bool dfm_set_string(HKEY k, const wchar_t *name, const wchar_t *v) {
    return !RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof *v));
}

static bool is_ours(const wchar_t *command) {
    wchar_t low[1200];
    wcsncpy_s(low, 1200, command, _TRUNCATE);
    _wcslwr_s(low, 1200);
    return wcsstr(low, L"-launcher.exe") != NULL;
}

/* True when a registered command carries the launcher's name. */
static bool any_ours(void) {
    wchar_t p[520], c[1200];
    paths();
    for (int i = 0; i < TARGETS; i++) {
        sub(p, classes_base, TARGET[i].path);
        if (!read_default(p, c, 1200) && is_ours(c)) return true;
    }
    return false;
}

/* The switch is on while any registered command is ours, and also while a record of the old values is left: a
 * turn-off that could not finish is still waiting for its retry, and a new turn-on must not replace that record. */
bool defaultfm_enabled(void) {
    HKEY k;
    LONG r;
    paths();
    if (any_ours()) return true;
    r = RegOpenKeyExW(HKEY_CURRENT_USER, state_base, 0, KEY_QUERY_VALUE, &k);
    if (!r) RegCloseKey(k);
    return !dfm_is_missing(r);
}

/* Remember how one target looked: whether it existed, the first key that did not, and its values. */
static bool save_target(HKEY saved, int i) {
    wchar_t name[16], rel[520], full[520];
    _snwprintf_s(name, 16, _TRUNCATE, L"%d", i);
    HKEY k, v;
    if (RegCreateKeyExW(saved, name, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return false;
    bool ok = dfm_set_string(k, L"Path", TARGET[i].path);
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
        ok = ok && dfm_set_string(k, L"Top", rel);
    }
    RegCloseKey(k);
    return ok;
}

/* Remove one registered command that carries the launcher's name; false when it is there and cannot be removed,
 * or when it cannot be read. */
static bool remove_ours(int i) {
    wchar_t p[520], c[1200];
    LONG r;
    sub(p, classes_base, TARGET[i].path);
    r = read_default(p, c, 1200);
    if (r) return r == ERROR_FILE_NOT_FOUND;
    if (!is_ours(c)) return true;
    r = RegDeleteTreeW(HKEY_CURRENT_USER, p);
    return !r || dfm_is_missing(r);
}

/* Put one target back. A target that did not exist loses only the keys the registration made: its own key, then
 * each parent down to Top while it is empty, so another program's entries beside ours stay. Only a record that is
 * not there ends in "remove what is ours"; a record that cannot be read or written is a failure. */
static bool restore_target(HKEY saved, int i) {
    wchar_t name[16], rel[520], top[520] = L"", full[520];
    HKEY k;
    DWORD existed = 0, n = sizeof existed, type;
    LONG r;
    _snwprintf_s(name, 16, _TRUNCATE, L"%d", i);
    r = RegOpenKeyExW(saved, name, 0, KEY_READ, &k);
    if (dfm_is_missing(r)) return remove_ours(i);                   /* no record of this target */
    if (r) return false;
    r = RegQueryValueExW(k, L"Existed", NULL, &type, (BYTE *)&existed, &n);
    bool ok = !r && type == REG_DWORD;
    if (ok && !existed) {
        DWORD tn = sizeof top - sizeof *top;
        r = RegQueryValueExW(k, L"Top", NULL, &type, (BYTE *)top, &tn);
        if (dfm_is_missing(r) || (!r && type != REG_SZ)) top[0] = 0;
        else ok = !r;
        top[519] = 0;
    }
    wcscpy_s(rel, 520, TARGET[i].path);
    sub(full, classes_base, rel);
    if (ok && existed) {
        HKEY leaf, v;
        ok = !RegOpenKeyExW(k, L"Values", 0, KEY_READ, &v);     /* read the record first: nothing is cleared without it */
        if (ok) {
            ok = !RegCreateKeyExW(HKEY_CURRENT_USER, full, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &leaf, NULL);
            if (ok) {
                ok = clear_values(leaf) && copy_values(v, leaf);
                RegCloseKey(leaf);
            }
            RegCloseKey(v);
        }
    } else if (ok) {
        r = RegDeleteTreeW(HKEY_CURRENT_USER, full);
        ok = !r || dfm_is_missing(r);
        for (wchar_t *cut; ok && wcslen(rel) > wcslen(top) && (cut = wcsrchr(rel, L'\\')); ) {
            *cut = 0;
            sub(full, classes_base, rel);
            if (key_empty(full)) {
                r = RegDeleteKeyW(HKEY_CURRENT_USER, full);
                ok = !r || dfm_is_missing(r);
            } else break;
        }
    }
    RegCloseKey(k);
    return ok;
}

/* Without a record, remove only what carries the launcher's name. */
static bool remove_all_ours(void) {
    bool ok = true;
    for (int i = 0; i < TARGETS; i++) ok = remove_ours(i) && ok;
    return ok;
}

static void quoted_command(wchar_t *out, const wchar_t *launcher, const wchar_t *extra, bool folder) {
    _snwprintf_s(out, 1200, _TRUNCATE, L"\"%s\"%s%s%s", launcher, extra && extra[0] ? L" " : L"",
                 extra ? extra : L"", folder ? L" \"%1\"" : L"");
}

static bool write_command(int i, const wchar_t *launcher, const wchar_t *extra) {
    wchar_t p[520], c[1200];
    HKEY k;
    sub(p, classes_base, TARGET[i].path);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, p, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return false;
    quoted_command(c, launcher, extra, TARGET[i].folder);
    bool ok = dfm_set_string(k, L"", c) && dfm_set_string(k, L"DelegateExecute", L"");
    RegCloseKey(k);
    return ok;
}

static bool write_commands(const wchar_t *launcher, const wchar_t *extra) {
    for (int i = 0; i < TARGETS; i++) if (!write_command(i, launcher, extra)) return false;
    return true;
}

/* A registration made by an older version has no record for the targets added since. Each such target that is not
 * ours is saved as it is now and then registered, so turning off puts it back like the others. */
static bool register_missing(const wchar_t *launcher, const wchar_t *extra) {
    wchar_t p[520], c[1200];
    HKEY saved;
    sub(p, state_base, L"Saved");
    bool ok = true;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, p, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &saved, NULL)) return false;
    for (int i = 0; i < TARGETS; i++) {
        wchar_t name[16];
        HKEY k;
        _snwprintf_s(name, 16, _TRUNCATE, L"%d", i);
        if (!RegOpenKeyExW(saved, name, 0, KEY_READ, &k)) { RegCloseKey(k); continue; }
        sub(p, classes_base, TARGET[i].path);
        if (!read_default(p, c, 1200) && is_ours(c)) continue;
        if (!save_target(saved, i) || !write_command(i, launcher, extra)) {   /* no record without a registration: the next try starts over */
            RegDeleteTreeW(saved, name);
            ok = false;
        }
    }
    RegCloseKey(saved);
    return ok;
}

bool dfm_launcher_path(wchar_t *out, size_t cap) {
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

/* Same bytes: the sizes match and every 64 KB block matches. A file that cannot be read counts as different. */
static bool same_content(const wchar_t *a, const wchar_t *b) {
    HANDLE x = CreateFileW(a, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    HANDLE y = CreateFileW(b, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    static BYTE bx[65536], by[65536];
    LARGE_INTEGER sx, sy;
    bool same = x != INVALID_HANDLE_VALUE && y != INVALID_HANDLE_VALUE && GetFileSizeEx(x, &sx) && GetFileSizeEx(y, &sy) && sx.QuadPart == sy.QuadPart;
    for (DWORD nx, ny; same; ) {
        if (!ReadFile(x, bx, sizeof bx, &nx, NULL) || !ReadFile(y, by, sizeof by, &ny, NULL)) same = false;
        else if (nx != ny || memcmp(bx, by, nx)) same = false;
        else if (!nx) break;
    }
    if (x != INVALID_HANDLE_VALUE) CloseHandle(x);
    if (y != INVALID_HANDLE_VALUE) CloseHandle(y);
    return same;
}

/* The same file by its full path, whatever the spelling: relative parts, case, short names. */
static bool same_path(const wchar_t *a, const wchar_t *b) {
    wchar_t x[MAX_PATH], y[MAX_PATH], t[MAX_PATH];
    if (!GetFullPathNameW(a, MAX_PATH, x, NULL) || !GetFullPathNameW(b, MAX_PATH, y, NULL)) return false;
    if (GetLongPathNameW(x, t, MAX_PATH)) wcscpy_s(x, MAX_PATH, t);
    if (GetLongPathNameW(y, t, MAX_PATH)) wcscpy_s(y, MAX_PATH, t);
    return !_wcsicmp(x, y);
}

static bool copy_launcher(const wchar_t *self, const wchar_t *launcher) {
    CreateDirectoryW(local_dir, NULL);
    if (CopyFileW(self, launcher, FALSE)) return true;
    return same_size(self, launcher);    /* a launcher in use is the same program */
}

static void clear_cleanup_entry(void);

bool defaultfm_enable(const wchar_t *extra_args) {
    wchar_t self[MAX_PATH], launcher[MAX_PATH];
    paths();
    if (defaultfm_is_launcher() || !GetModuleFileNameW(NULL, self, MAX_PATH) || !dfm_launcher_path(launcher, MAX_PATH)) return false;
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
    if (!register_missing(launcher, extra_args ? extra_args : L"")) return false;   /* an explicit turn-on also repairs a record left without its open verb */
    if (RegOpenKeyExW(HKEY_CURRENT_USER, state_base, 0, KEY_WRITE, &state)) return false;
    bool ok = dfm_set_string(state, L"App", self) && dfm_set_string(state, L"Launcher", launcher) &&
              dfm_set_string(state, L"Args", extra_args ? extra_args : L"");
    RegCloseKey(state);
    clear_cleanup_entry();      /* the launcher is in use again */
    resident_end_bridge();      /* a process left over from a turn-off is replaced by a fresh one below */
    if (ok && !same_content(self, launcher)) resident_stop();   /* a running launcher cannot be replaced */
    ok = ok && copy_launcher(self, launcher) && write_commands(launcher, extra_args);
    ok = ok && resident_keys_set(launcher, extra_args ? extra_args : L"");
    if (!ok) defaultfm_disable();
    else resident_start(launcher, extra_args ? extra_args : L"");
    return ok;
}

/* The launcher copy is deleted when nothing runs from it. A running process keeps it alive: that deletion waits for
 * the next sign-in (a RunOnce entry that starts this app with --cleanup-launcher, which has no window). */
static bool remove_launcher(wchar_t *launcher) {
    const wchar_t *base = wcsrchr(launcher, L'\\');
    if (!base || _wcsicmp(base + 1, LAUNCHER_NAME) || defaultfm_is_launcher() || !DeleteFileW(launcher)) return false;
    wchar_t part[MAX_PATH + 8];
    _snwprintf_s(part, MAX_PATH + 8, _TRUNCATE, L"%s.new", launcher);
    DeleteFileW(part);
    launcher[base - launcher] = 0;
    RemoveDirectoryW(launcher);                  /* only when nothing else is in it */
    return true;
}

static bool write_cleanup_entry(const wchar_t *app) {
    wchar_t cmd[1200];
    HKEY k;
    bool ok;
    if (test_on && test_arg[0]) _snwprintf_s(cmd, 1200, _TRUNCATE, L"\"%s\" --cleanup-launcher --test-state \"%s\"", app, test_arg);
    else _snwprintf_s(cmd, 1200, _TRUNCATE, L"\"%s\" --cleanup-launcher", app);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, runonce_base, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL)) return false;
    ok = dfm_set_string(k, CLEANUP_VALUE, cmd);
    RegCloseKey(k);
    return ok;
}

static void clear_cleanup_entry(void) {
    HKEY k;
    paths();
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runonce_base, 0, KEY_SET_VALUE, &k)) return;
    RegDeleteValueW(k, CLEANUP_VALUE);
    RegCloseKey(k);
}

static bool cleanup_pending(void) {
    wchar_t c[8];
    DWORD n = sizeof c;
    HKEY k;
    LONG r;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runonce_base, 0, KEY_QUERY_VALUE, &k)) return false;
    r = RegQueryValueExW(k, CLEANUP_VALUE, NULL, NULL, (BYTE *)c, &n);
    RegCloseKey(k);
    return r == 0 || r == ERROR_MORE_DATA;
}

/* The Win+E process ended after a turn-off (Explorer restarted): when no sign-in cleanup is waiting for the launcher
 * (the app was gone, so there was nothing to run it with), the launcher removes itself. */
void defaultfm_bridge_ended(void) {
    paths();
    if (!defaultfm_enabled() && !cleanup_pending()) defaultfm_delete_self_later();
}

/* Sign-in cleanup, started by RunOnce: delete the launcher when the switch is still off and nothing runs from it. */
bool defaultfm_cleanup_main(int *rc) {
    int argc = 0;
    wchar_t **argv, launcher[MAX_PATH];
    if (!wcsstr(GetCommandLineW(), L"--cleanup-launcher")) return false;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    if (argc < 2 || wcscmp(argv[1], L"--cleanup-launcher")) { LocalFree(argv); return false; }
    for (int i = 2; i + 1 < argc; i++) if (!wcscmp(argv[i], L"--test-state")) defaultfm_test_mode(argv[i + 1]);
    LocalFree(argv);
    *rc = 0;
    paths();
    if (!defaultfm_enabled() && !resident_running() && dfm_launcher_path(launcher, MAX_PATH)) remove_launcher(launcher);
    return true;
}

/* Nothing is thrown away until every target is back: a failed restore keeps the record and the launcher the
 * registered commands point to, and the next turn-off tries again. Every registry error except "not there" is a
 * failed restore; the record is only trusted or replaced by a restore that finished. */
bool defaultfm_disable(void) {
    HKEY state, saved;
    wchar_t launcher[MAX_PATH] = L"", app[MAX_PATH] = L"";
    bool ok = true;
    LONG r;
    paths();
    r = RegOpenKeyExW(HKEY_CURRENT_USER, state_base, 0, KEY_READ, &state);
    if (r) ok = dfm_is_missing(r) && remove_all_ours();
    else {
        DWORD type, n = sizeof launcher - sizeof *launcher;
        if (RegQueryValueExW(state, L"Launcher", NULL, &type, (BYTE *)launcher, &n) || type != REG_SZ) launcher[0] = 0;
        dfm_get_string(HKEY_CURRENT_USER, state_base, L"App", app, MAX_PATH);
        r = RegOpenKeyExW(state, L"Saved", 0, KEY_READ, &saved);
        if (!r) {
            for (int i = TARGETS - 1; i >= 0; i--) ok = restore_target(saved, i) && ok;
            RegCloseKey(saved);
        } else ok = dfm_is_missing(r) && remove_all_ours();
        RegCloseKey(state);
    }
    ok = ok && resident_keys_restore();       /* the hotkey value and the Run entry go back last: a turn-off that fails keeps them, so the process and the next sign-in still hold Win+E for the commands that stay */
    if (!ok || any_ours()) return false;
    /* Explorer reads DisabledHotkeys only when it starts, so when it started with E in it, Win+E has no owner now.
     * The Win+E process that holds the hotkey stays on as a bridge (it opens Explorer on Win+E) until Explorer
     * restarts; a process that does not hold it ends. The launcher goes at once, or at the next sign-in. */
    if (!resident_release()) {
        resident_stop();                                 /* a running exe cannot be deleted */
        remove_launcher(launcher);                       /* the copy goes too; a running one stays */
    } else if (app[0] && GetFileAttributesW(app) != INVALID_FILE_ATTRIBUTES) write_cleanup_entry(app);
    r = RegDeleteTreeW(HKEY_CURRENT_USER, state_base);
    if (r && !dfm_is_missing(r)) return false;           /* the record stays, so the switch still reads as waiting */
    if (!wcscmp(state_base, L"Software\\Dio File Manager\\DefaultFileManager")) RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Dio File Manager");
    return true;
}

/* The compare reads 0.5 MB twice (53 ms on the first start after the exe was written, under 1 ms later), so it runs
 * beside the window. The new copy is written under another name and renamed over the launcher, so closing the app
 * in the middle never leaves a half launcher. */
static DWORD WINAPI refresh_launcher(void *arg) {
    wchar_t *three = arg, *self = three, *launcher = three + MAX_PATH, *args = three + 2 * MAX_PATH, part[MAX_PATH + 8];
    if (!same_content(self, launcher)) {
        bool was = resident_running();
        if (was) resident_stop();            /* the resident process runs from the launcher: it must let go before the new copy lands */
        _snwprintf_s(part, MAX_PATH + 8, _TRUNCATE, L"%s.new", launcher);
        CreateDirectoryW(local_dir, NULL);
        if (!CopyFileW(self, part, FALSE) || !MoveFileExW(part, launcher, MOVEFILE_REPLACE_EXISTING)) DeleteFileW(part);
    }
    /* Win+E keys and process: an install made before they existed gets them here; a pending turn-off adds none */
    if (any_ours() && resident_keys_set(launcher, args)) resident_start(launcher, args);
    free(three);
    return 0;
}

static void refresh_launcher_later(const wchar_t *self, const wchar_t *launcher, const wchar_t *args) {
    wchar_t *three = malloc(3 * MAX_PATH * sizeof *three);
    HANDLE t;
    if (!three) return;
    wcscpy_s(three, MAX_PATH, self);
    wcscpy_s(three + MAX_PATH, MAX_PATH, launcher);
    wcsncpy_s(three + 2 * MAX_PATH, MAX_PATH, args, _TRUNCATE);
    t = CreateThread(NULL, 0, refresh_launcher, three, 0, NULL);
    if (t) CloseHandle(t); else free(three);
}

void defaultfm_sync(void) {
    wchar_t self[MAX_PATH], app[MAX_PATH], launcher[MAX_PATH];
    paths();
    if (!defaultfm_enabled() || !GetModuleFileNameW(NULL, self, MAX_PATH) || !dfm_launcher_path(launcher, MAX_PATH)) return;
    /* Only the registered app (the saved App path) keeps the launcher and the keys up to date. A test build, a copy
     * elsewhere or a moved exe changes nothing; turning the switch on from that copy registers it. */
    if (!dfm_get_string(HKEY_CURRENT_USER, state_base, L"App", app, MAX_PATH) || !same_path(app, self)) return;
    wchar_t args[520] = L"";
    dfm_get_string(HKEY_CURRENT_USER, state_base, L"Args", args, 520);
    if (!any_ours()) return;   /* recovery state alone (a turn-off whose restore failed): the running Win+E process keeps its launcher, nothing is registered */
    refresh_launcher_later(self, launcher, args);
    register_missing(launcher, args);
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

/* The request is a folder, and a name in it to select, when both fit the UTF-8 fields; otherwise Explorer gets it. */
static void folder_request(dfm_request *out, const wchar_t *folder, const wchar_t *select) {
    out->kind = REQ_FOLDER;
    if (!fs_w_to_u8(folder, out->path, FS_PATH_MAX) || (select && !fs_w_to_u8(select, out->select, FS_PATH_MAX))) {
        memset(out, 0, sizeof *out);
        out->kind = REQ_FALLBACK;
    }
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
        if (l <= 3 || !slash) { folder_request(out, full, NULL); return; }   /* a drive root */
        wcsncpy_s(select, DFM_REQ_PATH, slash + 1, _TRUNCATE);
        if (slash == full + 2) full[3] = 0; else *slash = 0;        /* a file in a drive root is selected in that root */
        folder_request(out, full, select);
        return;
    }
    int loc;
    if (is_home(target)) { out->kind = REQ_HOME; return; }
    if (place_of(target, &loc)) { out->kind = REQ_PLACE; out->loc = loc; return; }
    if (!_wcsnicmp(target, L"shell:", 6) || !wcsncmp(target, L"::{", 3)) {
        if (known_place_path(target, full)) folder_request(out, full, NULL);
        return;
    }
    if (full_path(target, full) && dir_exists(full)) folder_request(out, full, NULL);
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

/* ---- an Explorer window that opened anyway ---- */

/* SHOpenFolderAndSelectItems (Electron's "show in folder", VS Code's "Reveal in File Explorer") is answered inside
 * Explorer, and explorer.exe started directly reads no verb: both open Explorer's own window and never run the
 * registered command. So the Win+E process watches for new Explorer windows and starts the app on each one; here the
 * app reads the window's folder and selection through the shell's window list, closes it, and opens the same place. */

/* What the window shows: found is false until the window is listed with a view; folder is empty for a place that is
 * not a folder on disk; sel is the first selected item's full path, or empty. */
static void shell_window_read(HWND h, bool *found, wchar_t *folder, wchar_t *sel) {
    IShellWindows *sw = NULL;
    long n = 0;
    *found = false; folder[0] = sel[0] = 0;
    if (FAILED(CoCreateInstance(&CLSID_ShellWindows, NULL, CLSCTX_ALL, &IID_IShellWindows, (void **)&sw)) || !sw) return;
    sw->lpVtbl->get_Count(sw, &n);
    for (long i = 0; i < n && !*found; i++) {
        VARIANT v;
        IDispatch *d = NULL;
        IWebBrowser2 *wb = NULL;
        IServiceProvider *sp = NULL;
        IShellBrowser *sb = NULL;
        IShellView *view = NULL;
        IFolderView2 *fv = NULL;
        SHANDLE_PTR wh = 0;
        VariantInit(&v); v.vt = VT_I4; v.lVal = i;
        if (FAILED(sw->lpVtbl->Item(sw, v, &d)) || !d) continue;
        if (SUCCEEDED(d->lpVtbl->QueryInterface(d, &IID_IWebBrowser2, (void **)&wb)) && wb &&
            SUCCEEDED(wb->lpVtbl->get_HWND(wb, &wh)) && (HWND)wh == h &&
            SUCCEEDED(wb->lpVtbl->QueryInterface(wb, &IID_IServiceProvider, (void **)&sp)) && sp &&
            SUCCEEDED(sp->lpVtbl->QueryService(sp, &SID_STopLevelBrowser, &IID_IShellBrowser, (void **)&sb)) && sb &&
            SUCCEEDED(sb->lpVtbl->QueryActiveShellView(sb, &view)) && view &&
            SUCCEEDED(view->lpVtbl->QueryInterface(view, &IID_IFolderView2, (void **)&fv)) && fv) {
            IPersistFolder2 *pf = NULL;
            IShellItemArray *items = NULL;
            *found = true;
            if (SUCCEEDED(fv->lpVtbl->GetFolder(fv, &IID_IPersistFolder2, (void **)&pf)) && pf) {
                PIDLIST_ABSOLUTE pidl = NULL;
                if (SUCCEEDED(pf->lpVtbl->GetCurFolder(pf, &pidl)) && pidl) {
                    if (!SHGetPathFromIDListEx(pidl, folder, DFM_REQ_PATH, GPFIDL_DEFAULT)) folder[0] = 0;
                    CoTaskMemFree(pidl);
                }
                pf->lpVtbl->Release(pf);
            }
            if (SUCCEEDED(fv->lpVtbl->GetSelection(fv, FALSE, &items)) && items) {
                IShellItem *it = NULL;
                DWORD count = 0;
                if (SUCCEEDED(items->lpVtbl->GetCount(items, &count)) && count &&
                    SUCCEEDED(items->lpVtbl->GetItemAt(items, 0, &it)) && it) {
                    wchar_t *p = NULL;
                    if (SUCCEEDED(it->lpVtbl->GetDisplayName(it, SIGDN_FILESYSPATH, &p)) && p) {
                        wcsncpy_s(sel, DFM_REQ_PATH, p, _TRUNCATE);
                        CoTaskMemFree(p);
                    }
                    it->lpVtbl->Release(it);
                }
                items->lpVtbl->Release(items);
            }
        }
        if (fv) fv->lpVtbl->Release(fv);
        if (view) view->lpVtbl->Release(view);
        if (sb) sb->lpVtbl->Release(sb);
        if (sp) sp->lpVtbl->Release(sp);
        if (wb) wb->lpVtbl->Release(wb);
        d->lpVtbl->Release(d);
    }
    sw->lpVtbl->Release(sw);
}

/* A check's stand-in window (class DioTestCabinetWClass, on the check's own desktop) carries "folder" or
 * "folder<TAB>selected path" as its title; an empty title is a window that is not listed yet, "-" one that is not a
 * folder on disk. */
static void test_window_read(HWND h, bool *found, wchar_t *folder, wchar_t *sel) {
    wchar_t t[2 * DFM_REQ_PATH + 2] = L"";
    *found = false; folder[0] = sel[0] = 0;
    GetWindowTextW(h, t, 2 * DFM_REQ_PATH + 2);
    if (!t[0]) return;
    *found = true;
    if (!wcscmp(t, L"-")) return;
    wchar_t *tab = wcschr(t, L'\t');
    if (tab) { *tab = 0; wcsncpy_s(sel, DFM_REQ_PATH, tab + 1, _TRUNCATE); }
    wcsncpy_s(folder, DFM_REQ_PATH, t, _TRUNCATE);
}

bool defaultfm_take_window(HWND h, dfm_request *out) {
    wchar_t folder[DFM_REQ_PATH], sel[DFM_REQ_PATH], full[DFM_REQ_PATH];
    bool found = false;
    memset(out, 0, sizeof *out);
    out->kind = REQ_FALLBACK;
    if (!defaultfm_enabled() || !IsWindow(h)) return false;
    HRESULT co = test_on ? E_FAIL : CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   /* this runs before the window's own COM start */
    void (*read)(HWND, bool *, wchar_t *, wchar_t *) = test_on ? test_window_read : shell_window_read;
    /* The window is listed in the shell's window list once its first folder is shown, and a "show in folder" selects
     * the item a moment later. Wait up to 3 s for the listing and then up to 0.4 s for a selection. */
    ULONGLONG start = GetTickCount64(), listed = 0;
    for (;;) {
        if (!IsWindow(h)) break;
        read(h, &found, folder, sel);
        ULONGLONG now = GetTickCount64();
        if (found && !listed) listed = now;
        if (found && (!folder[0] || sel[0] || now - listed >= 400)) break;
        if (!found && now - start >= 3000) break;
        Sleep(30);
    }
    if (SUCCEEDED(co)) CoUninitialize();
    if (!found || !folder[0] || !IsWindow(h) || !dir_exists(folder)) { log_line(L"take-left", folder, NULL); return false; }
    /* The selected item is opened selected when it is in that folder; otherwise the folder opens alone. */
    const wchar_t *name = NULL;
    if (sel[0] && full_path(sel, full)) {
        wchar_t *slash = wcsrchr(full, L'\\');
        size_t fl = wcslen(folder);
        while (fl > 3 && folder[fl - 1] == L'\\') folder[--fl] = 0;
        if (slash && slash[1] && (size_t)(slash - full) == (fl == 3 ? 2 : fl) && !_wcsnicmp(full, folder, fl == 3 ? 2 : fl))
            name = slash + 1;
    }
    folder_request(out, folder, name);
    /* the switch may have been turned off while this waited: then the window stays Explorer's */
    if (out->kind != REQ_FOLDER || !defaultfm_enabled()) { log_line(L"take-left", folder, NULL); return false; }
    PostMessageW(h, WM_CLOSE, 0, 0);
    log_line(L"take-closed", folder, name);
    return true;
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

/* Start Explorer with this command text; a check starts its stand-in with the same arguments instead. skip is the
 * length of the Windows folder: the arguments follow "<windir>\explorer.exe" and its two quotes (15 characters). */
static bool start_explorer(wchar_t *cmd, size_t skip) {
    wchar_t stand_in[1024];
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    log_line(L"explorer", cmd, NULL);
    if (test_on) {   /* a check never starts the real Explorer */
        wchar_t run[4096];
        if (!GetEnvironmentVariableW(L"DFM_TEST_EXPLORER", stand_in, 1024)) return true;
        _snwprintf_s(run, 4096, _TRUNCATE, L"%s%s", stand_in, cmd + skip + 15);
        if (!CreateProcessW(NULL, run, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return true;
    }
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

bool defaultfm_fallback(wchar_t **args, int n) {
    wchar_t cmd[4096], windir[MAX_PATH];
    if (!GetWindowsDirectoryW(windir, MAX_PATH)) return false;
    _snwprintf_s(cmd, 4096, _TRUNCATE, L"\"%s\\explorer.exe\"", windir);
    for (int i = 0; i < n; i++) add_arg(cmd, 4096, args[i]);
    if (!guard_allows(cmd + wcslen(windir) + 15)) { log_line(L"refused", cmd, NULL); return false; }
    return start_explorer(cmd, wcslen(windir));
}

/* Explorer's own window in Home, for the Win+E process once the switch is off. No loop guard: Explorer never calls
 * back into this program, and a second press means a second window. */
bool dfm_explorer_open(void) {
    wchar_t cmd[MAX_PATH + 24], windir[MAX_PATH];
    if (!GetWindowsDirectoryW(windir, MAX_PATH)) return false;
    _snwprintf_s(cmd, MAX_PATH + 24, _TRUNCATE, L"\"%s\\explorer.exe\"", windir);
    return start_explorer(cmd, wcslen(windir));
}

/* ---- the launcher ---- */

bool defaultfm_is_launcher(void) {
    wchar_t self[MAX_PATH];
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return false;
    const wchar_t *base = wcsrchr(self, L'\\');
    return base && !_wcsicmp(base + 1, LAUNCHER_NAME);
}

const wchar_t *dfm_command_tail(void) {
    const wchar_t *c = GetCommandLineW();
    if (*c == L'"') { c = wcschr(c + 1, L'"'); if (c) c++; }
    else while (*c && *c != L' ') c++;
    while (c && *c == L' ') c++;
    return c ? c : L"";
}

/* A running exe cannot delete itself: a hidden cmd waits a few seconds and does it. A check never deletes anything this way. */
void defaultfm_delete_self_later(void) {
    wchar_t self[MAX_PATH];
    if (!GetModuleFileNameW(NULL, self, MAX_PATH) || test_on) return;
    wchar_t c[MAX_PATH + 80], sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    _snwprintf_s(c, MAX_PATH + 80, _TRUNCATE, L"\"%s\\cmd.exe\" /d /c ping -n 4 127.0.0.1 >nul & del /f /q \"%s\"", sys, self);
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}

int defaultfm_launcher_main(void) {
    int n = 0, kept = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &n), **args;
    wchar_t app[MAX_PATH], cmd[32768];
    const wchar_t *tail = dfm_command_tail();
    if (!argv) return 1;
    args = calloc((size_t)n + 1, sizeof *args);
    for (int i = 1; i < n; i++) {
        if (!wcscmp(argv[i], L"--test-state") && i + 1 < n) { defaultfm_test_mode(argv[i + 1]); i++; continue; }
        args[kept++] = argv[i];
    }
    paths();
    if (dfm_get_string(HKEY_CURRENT_USER, state_base, L"App", app, MAX_PATH) && GetFileAttributesW(app) != INVALID_FILE_ATTRIBUTES) {
        _snwprintf_s(cmd, 32768, _TRUNCATE, L"\"%s\" %s", app, tail);
        STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
        if (CreateProcessW(app, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 0; }
    }
    /* The app is gone: give Explorer its keys back first, then hand it the request. Nothing can call back here. */
    defaultfm_disable();
    log_line(L"restored", NULL, NULL);
    bool ok = defaultfm_fallback(args, kept);
    defaultfm_delete_self_later();
    free(args); LocalFree(argv);
    return ok ? 0 : 4;
}
