/* Win+E for the default file manager. See docs/design.md, "Win+E". Windows' shell takes Win+E itself and never
 * runs the registered verb, so this exe has a second mode: started with --resident it has no window you can see,
 * no UI start-up and no COM, only one hidden window that owns the hotkey. Explorer leaves Win+E alone only when
 * its DisabledHotkeys value lists E, and reads that once when it starts; until then RegisterHotKey is refused,
 * and the resident process tries again whenever Explorer says it has restarted (the "TaskbarCreated" message).
 * When the switch is turned off after the process took the hotkey, Explorer still skips Win+E until it restarts, so
 * the process stays on as a bridge that opens Explorer, and ends at that restart. */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "resident.h"
#include "defaultfm.h"
#include "defaultfm_int.h"

static const wchar_t *CLASS_NAME = L"DioFileManagerResident";
static const wchar_t *HOTKEYS = L"DisabledHotkeys";
static const wchar_t *RUN_VALUE = L"Dio File Manager";
enum { HOTKEY_ID = 1, MSG_RELEASE = WM_APP + 1, MSG_END_BRIDGE = WM_APP + 2 };

static bool in_resident;          /* this process is the resident one: it never stops itself */
static HWND win;
static UINT taskbar_created;
static bool registered;
static bool bridge;               /* the switch is off, Win+E is still ours: it opens Explorer until Explorer restarts */
static HANDLE shell_proc;         /* the Explorer that owned the taskbar when the bridge began; NULL when there was none */
static wchar_t tail[1200];        /* what follows --resident: handed to the app as the launcher hands over its own */
static HWINEVENTHOOK show_hook;   /* new Explorer windows: see take_window */
static struct { HWND h; ULONGLONG t; } taken[16];   /* windows already handed to the app, with when */

static void log_line(const wchar_t *what, const wchar_t *a) {
    const wchar_t *file = dfm_test_resident_log();
    char line[1024];
    if (!file) return;
    _snprintf_s(line, sizeof line, _TRUNCATE, "%ls\t%ls\n", what, a ? a : L"");
    dfm_log_text(file, line);
}

/* One instance per user. A check adds its redirected registry root, so it never meets the owner's process. */
static void object_name(wchar_t *out, size_t cap, const wchar_t *prefix) {
    size_t n;
    wcscpy_s(out, cap, prefix);
    n = wcslen(out);
    if (dfm_test_on()) for (const wchar_t *b = dfm_state_base(); *b && n + 1 < cap; b++) out[n++] = iswalnum(*b) ? *b : L'_';
    out[n] = 0;
}

static void command_text(wchar_t *out, const wchar_t *launcher, const wchar_t *extra) {
    _snwprintf_s(out, 1200, _TRUNCATE, L"\"%s\" --resident%s%s", launcher, extra && extra[0] ? L" " : L"", extra ? extra : L"");
}

/* ---- the keys ---- */

static void record_path(wchar_t *out) { _snwprintf_s(out, 520, _TRUNCATE, L"%s\\Saved\\DisabledHotkeys", dfm_state_base()); }

static bool has_e(const wchar_t *s) {
    for (; *s; s++) if (*s == L'E' || *s == L'e') return true;
    return false;
}

bool resident_keys_set(const wchar_t *launcher, const wchar_t *extra) {
    HKEY adv, rec, run;
    wchar_t cur[260] = L"", path[520], want[1200], have[1200];
    DWORD type = REG_SZ, n = sizeof cur - sizeof *cur;
    LONG r;
    bool ok = true, existed;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, dfm_adv_base(), 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &adv, NULL)) return false;
    r = RegQueryValueExW(adv, HOTKEYS, NULL, &type, (BYTE *)cur, &n);
    existed = !r;
    if (dfm_is_missing(r)) type = REG_SZ;
    else if (r || (type != REG_SZ && type != REG_EXPAND_SZ)) { RegCloseKey(adv); return false; }   /* a value we cannot read is left alone */
    else cur[n / sizeof *cur] = 0;
    /* The old value is saved once. A record that is there already is never replaced, also when the value has changed since. */
    record_path(path);
    r = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &rec);
    if (!r) RegCloseKey(rec);
    else if (dfm_is_missing(r)) {
        DWORD e = existed;
        ok = !RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_WRITE, NULL, &rec, NULL);
        if (ok) {
            ok = !RegSetValueExW(rec, L"Existed", 0, REG_DWORD, (BYTE *)&e, sizeof e) &&
                 !RegSetValueExW(rec, L"Type", 0, REG_DWORD, (BYTE *)&type, sizeof type) && (!existed || dfm_set_string(rec, L"Value", cur));
            RegCloseKey(rec);
        }
        if (!ok) RegDeleteTreeW(HKEY_CURRENT_USER, path);   /* no record without a change, and no change without a record */
    } else ok = false;
    if (ok && !has_e(cur)) {
        size_t l = wcslen(cur);
        if (l + 2 > sizeof cur / sizeof *cur) ok = false;
        else {
            cur[l] = L'E'; cur[l + 1] = 0;
            ok = !RegSetValueExW(adv, HOTKEYS, 0, type, (BYTE *)cur, (DWORD)((l + 2) * sizeof *cur));
        }
    }
    RegCloseKey(adv);
    if (!ok) return false;
    command_text(want, launcher, extra);
    if (dfm_get_string(HKEY_CURRENT_USER, dfm_run_base(), RUN_VALUE, have, 1200) && !wcscmp(have, want)) return true;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, dfm_run_base(), 0, NULL, 0, KEY_SET_VALUE, NULL, &run, NULL)) return false;
    ok = dfm_set_string(run, RUN_VALUE, want);
    RegCloseKey(run);
    return ok;
}

bool resident_keys_restore(void) {
    HKEY rec, adv, run;
    wchar_t path[520], saved[260] = L"";
    DWORD existed = 0, type = REG_SZ, t, n;
    LONG r;
    bool ok = true;
    record_path(path);
    r = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &rec);
    if (r) ok = dfm_is_missing(r);            /* no record: nothing here was changed by us */
    else {
        n = sizeof existed;
        ok = !RegQueryValueExW(rec, L"Existed", NULL, &t, (BYTE *)&existed, &n) && t == REG_DWORD;
        n = sizeof type;
        r = RegQueryValueExW(rec, L"Type", NULL, &t, (BYTE *)&type, &n);
        if (dfm_is_missing(r) || (!r && t != REG_DWORD)) type = REG_SZ; else ok = ok && !r;
        if (ok && existed) {
            n = sizeof saved - sizeof *saved;
            ok = !RegQueryValueExW(rec, L"Value", NULL, &t, (BYTE *)saved, &n) && t == REG_SZ;
            if (ok) saved[n / sizeof *saved] = 0;   /* n is the size needed, not the size read, when the value is too long */
        }
        RegCloseKey(rec);
        if (ok && !RegCreateKeyExW(HKEY_CURRENT_USER, dfm_adv_base(), 0, NULL, 0, KEY_SET_VALUE, NULL, &adv, NULL)) {
            if (existed) ok = !RegSetValueExW(adv, HOTKEYS, 0, type, (BYTE *)saved, (DWORD)((wcslen(saved) + 1) * sizeof *saved));
            else { r = RegDeleteValueW(adv, HOTKEYS); ok = !r || dfm_is_missing(r); }
            RegCloseKey(adv);
        } else ok = false;
    }
    if (!ok) return false;   /* E is still in DisabledHotkeys: the Run entry stays, so the next sign-in still starts the process */
    r = RegOpenKeyExW(HKEY_CURRENT_USER, dfm_run_base(), 0, KEY_SET_VALUE, &run);
    if (!r) {
        r = RegDeleteValueW(run, RUN_VALUE);
        if (r && !dfm_is_missing(r)) ok = false;
        RegCloseKey(run);
    } else if (!dfm_is_missing(r)) ok = false;
    return ok;
}

/* ---- the process, seen from the app ---- */

bool resident_running(void) {
    wchar_t name[300];
    HANDLE m;
    object_name(name, 300, L"Local\\DioFmResident");
    m = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (m) CloseHandle(m);
    return m != NULL;
}

void resident_start(const wchar_t *launcher, const wchar_t *extra) {
    wchar_t cmd[1200], sys[MAX_PATH];
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (in_resident || resident_running()) return;
    command_text(cmd, launcher, extra);
    GetSystemDirectoryW(sys, MAX_PATH);   /* it never holds a folder of ours open */
    if (CreateProcessW(launcher, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, sys, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}

static HWND resident_window(void) {
    wchar_t title[300];
    object_name(title, 300, L"DioFmResident");
    return FindWindowW(CLASS_NAME, title);
}

/* Send the process one of our messages and wait for its answer (1 or 0); -1 when it is not there or does not answer. */
static int ask_process(UINT msg) {
    HWND w = resident_window();
    DWORD_PTR r = 0;
    if (!w || !SendMessageTimeoutW(w, msg, 0, 0, SMTO_ABORTIFHUNG, 3000, &r)) return -1;
    return (int)r;
}

/* The bridge ends when the Explorer that was running at turn-off ends: the new Explorer registers its hotkeys while it
 * starts, before it says "TaskbarCreated" (which is also sent when the display scale changes), so the hotkey must be let go
 * before it starts. With no Explorer at turn-off, the first "TaskbarCreated" names the one to watch: it has already lost
 * Win+E to the bridge, which keeps serving it until that Explorer ends. */
static void watch_shell(void) {
    DWORD p = 0;
    HWND t = FindWindowW(L"Shell_TrayWnd", NULL);
    if (t) GetWindowThreadProcessId(t, &p);
    if (p) shell_proc = OpenProcess(SYNCHRONIZE, FALSE, p);
}

static bool start_bridge(void) {
    if (bridge) return true;
    if (!registered) return false;
    bridge = true;
    watch_shell();
    return true;
}

bool resident_release(void) {
    if (in_resident) return start_bridge();   /* the process itself turned the switch off (the app is gone) */
    return ask_process(MSG_RELEASE) == 1;
}

void resident_end_bridge(void) {
    if (!in_resident && ask_process(MSG_END_BRIDGE) == 1) resident_stop();
}

/* Ask it to close and wait: a running exe cannot be replaced or deleted. A process that does not answer is ended. */
void resident_stop(void) {
    if (in_resident) return;
    for (int tries = 0; tries < 40 && resident_running(); tries++) {
        HWND w = resident_window();
        DWORD pid = 0;
        if (w) GetWindowThreadProcessId(w, &pid);
        if (pid) {
            HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
            PostMessageW(w, WM_CLOSE, 0, 0);
            if (p) {
                if (WaitForSingleObject(p, 3000) != WAIT_OBJECT_0) { TerminateProcess(p, 1); WaitForSingleObject(p, 2000); }
                CloseHandle(p);
            }
        } else Sleep(50);   /* just started: its window is not there yet */
    }
}

/* ---- the resident process ---- */

static void try_register(void) {
    wchar_t e[16];
    if (registered) return;
    /* Windows keeps Win+E for Explorer on every desktop of the session, so a check, which runs on a private desktop
     * beside the owner's, takes a combination nobody presses instead. */
    UINT mods = dfm_test_on() ? MOD_WIN | MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT : MOD_WIN | MOD_NOREPEAT;
    if (RegisterHotKey(win, HOTKEY_ID, mods, dfm_test_on() ? VK_F23 : 'E')) { registered = true; log_line(L"hotkey-ok", NULL); return; }
    _snwprintf_s(e, 16, _TRUNCATE, L"%lu", GetLastError());   /* 1409: Explorer still holds it */
    log_line(L"hotkey-busy", e);
}

/* Open a new window in Home as the launcher does: the registered app, started with the same arguments. When the app
 * is gone the keys go back, this press opens Explorer's window, and the process stays on as a bridge. After a
 * turn-off every press opens Explorer's window. */
static void open_home(void) {
    wchar_t app[MAX_PATH], cmd[1500];
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (bridge) { dfm_explorer_open(); log_line(L"bridge-open", NULL); return; }
    if (defaultfm_enabled() && dfm_get_string(HKEY_CURRENT_USER, dfm_state_base(), L"App", app, MAX_PATH) &&
        GetFileAttributesW(app) != INVALID_FILE_ATTRIBUTES) {
        _snwprintf_s(cmd, 1500, _TRUNCATE, L"\"%s\"%s%s", app, tail[0] ? L" " : L"", tail);
        if (CreateProcessW(app, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            AllowSetForegroundWindow(pi.dwProcessId);   /* the hotkey was the last input: the new window may take the front */
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            log_line(L"hotkey-open", NULL);
            return;
        }
    }
    defaultfm_disable();   /* in this process: it becomes the bridge; when the keys cannot be put back it stays as it is and tries again at the next press */
    dfm_explorer_open();
    log_line(L"hotkey-gone", NULL);
}

/* A program's "show in folder" and explorer.exe started directly open Explorer's own window without the registered
 * command (docs/design.md, "Show in folder"). Each new Explorer window is handed to the app with --take-window; the
 * app reads its folder and selection, closes it and opens the same place, or leaves it alone (Home, Control Panel).
 * Only while the switch is on: a bridge, or a switch turned off elsewhere, leaves every window to Explorer. A check
 * watches its stand-in class only, so it never touches the owner's Explorer windows. */
static void take_window(HWND h) {
    wchar_t app[MAX_PATH], cmd[1500];
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    ULONGLONG now = GetTickCount64();
    int slot = 0;
    for (int i = 0; i < 16; i++) {
        if (taken[i].h == h && now - taken[i].t < 30000) return;   /* shown again (the app left it): once is enough */
        if (taken[i].t < taken[slot].t) slot = i;
    }
    taken[slot].h = h; taken[slot].t = now;
    if (bridge || !defaultfm_enabled() || !dfm_get_string(HKEY_CURRENT_USER, dfm_state_base(), L"App", app, MAX_PATH) ||
        GetFileAttributesW(app) == INVALID_FILE_ATTRIBUTES) return;
    _snwprintf_s(cmd, 1500, _TRUNCATE, L"\"%s\" --take-window %llx%s%s", app, (unsigned long long)(ULONG_PTR)h, tail[0] ? L" " : L"", tail);
    if (CreateProcessW(app, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        log_line(L"take-window", NULL);
    }
}

static void CALLBACK on_show(HWINEVENTHOOK hook, DWORD ev, HWND h, LONG obj, LONG child, DWORD thread, DWORD t) {
    wchar_t c[32];
    (void)hook; (void)ev; (void)thread; (void)t;
    if (obj != OBJID_WINDOW || child != CHILDID_SELF || !h || bridge) return;
    if (!GetClassNameW(h, c, 32) || wcscmp(c, dfm_test_on() ? L"DioTestCabinetWClass" : L"CabinetWClass")) return;
    if (GetAncestor(h, GA_PARENT) != GetDesktopWindow()) return;   /* top-level windows only */
    take_window(h);
}

static void end_bridge(void) {
    log_line(L"bridge-exit", NULL);
    if (registered) { UnregisterHotKey(win, HOTKEY_ID); registered = false; }
    if (show_hook) { UnhookWinEvent(show_hook); show_hook = NULL; }
    defaultfm_bridge_ended();
    DestroyWindow(win);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_HOTKEY && w == HOTKEY_ID) { open_home(); return 0; }
    if (m == MSG_RELEASE) {
        if (start_bridge()) { log_line(L"bridge", NULL); return 1; }
        PostMessageW(h, WM_CLOSE, 0, 0);   /* Explorer still owns Win+E, nothing to bridge */
        return 0;
    }
    if (m == MSG_END_BRIDGE) {
        if (!bridge) return 0;
        PostMessageW(h, WM_CLOSE, 0, 0);
        return 1;
    }
    if (m == taskbar_created && m) {
        log_line(L"taskbar-created", NULL);
        if (!bridge) try_register();
        else if (!shell_proc) { watch_shell(); if (!shell_proc) end_bridge(); }   /* a bridge that watches Explorer's process ends when that process does */
        return 0;
    }
    if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) {
        if (registered) UnregisterHotKey(h, HOTKEY_ID);
        if (show_hook) { UnhookWinEvent(show_hook); show_hook = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool resident_main(int *rc) {
    int argc = 0;
    wchar_t **argv, name[300], title[300], pid[16];
    const wchar_t *t;
    HANDLE mutex;
    WNDCLASSW wc = { 0 };
    MSG msg;
    if (!wcsstr(GetCommandLineW(), L"--resident")) return false;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    if (argc < 2 || wcscmp(argv[1], L"--resident")) { LocalFree(argv); return false; }
    for (int i = 2; i + 1 < argc; i++) if (!wcscmp(argv[i], L"--test-state")) defaultfm_test_mode(argv[i + 1]);
    LocalFree(argv);
    *rc = 0;
    in_resident = true;
    t = dfm_command_tail();
    if (!wcsncmp(t, L"--resident", 10)) t += 10;
    while (*t == L' ') t++;
    wcsncpy_s(tail, 1200, t, _TRUNCATE);
    if (!defaultfm_enabled()) return true;                       /* a leftover Run entry after a turn-off */
    object_name(name, 300, L"Local\\DioFmResident");
    mutex = CreateMutexW(NULL, FALSE, name);
    if (!mutex) { *rc = 1; return true; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return true; }
    object_name(title, 300, L"DioFmResident");
    wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassW(&wc)) { *rc = 1; return true; }
    /* a top-level window that is never shown: Explorer's restart message is a broadcast, which message-only windows do not get */
    win = CreateWindowExW(WS_EX_TOOLWINDOW, CLASS_NAME, title, WS_POPUP, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!win) { *rc = 1; return true; }
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    _snwprintf_s(pid, 16, _TRUNCATE, L"%lu", GetCurrentProcessId());
    log_line(L"pid", pid);
    try_register();
    show_hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, on_show, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);   /* give back what start-up touched */
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(shell_proc ? 1 : 0, shell_proc ? &shell_proc : NULL, FALSE, INFINITE, QS_ALLINPUT);
        if (shell_proc && r == WAIT_OBJECT_0) {   /* the old Explorer ended: let go of Win+E before the new one starts */
            CloseHandle(shell_proc);
            shell_proc = NULL;
            end_bridge();
        }
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { CloseHandle(mutex); return true; }
            DispatchMessageW(&msg);
        }
    }
}
