#define COBJMACROS
#define DFM_INTERNAL
#include "ops.h"
#include "shell.h"
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private-desktop checks must not activate a broker on the user's desktop. */
static HRESULT test_launch(const wchar_t *setting, const wchar_t *args, const wchar_t *dir) {
    wchar_t command[32768];
    DWORD n = GetEnvironmentVariableW(setting, command, 32768);
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {0};
    if (!n) return S_FALSE;
    if (n >= 32768 || wcslen(args) + n + 2 >= 32768) return E_INVALIDARG;
    wcscat_s(command, 32768, L" "); wcscat_s(command, 32768, args);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi))
        return HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return S_OK;
}

/* Test mode without a stand-in: opening a file only writes this log. Checks never start an app on the owner's
 * desktop, where "Pick an app" and Notepad windows from every run piled up. */
static const char *test_log;
void shell_test_log(const char *path) { test_log = path; }
static BOOL logged(const char *verb, const char *path) {
    FILE *f;
    if (!test_log) return FALSE;
    if (!fopen_s(&f, test_log, "a") && f) { fprintf(f, "%s\t%s\n", verb, path); fclose(f); }
    return TRUE;
}

static HRESULT execute(HWND owner, const char *path, const wchar_t *verb, ULONG mask) {
    wchar_t *w = dfm_wide(path);
    SHELLEXECUTEINFOW info = { sizeof(info) };
    BOOL ok;
    if (!w) return HRESULT_FROM_WIN32(GetLastError());
    if (!wcscmp(verb, L"open")) {
        wchar_t args[32768];
        _snwprintf_s(args, 32768, _TRUNCATE, L"\"%s\"", w);
        HRESULT test = test_launch(L"DFM_TEST_OPEN", args, NULL);
        if (test != S_FALSE) { free(w); return test; }
        if (logged("open", path)) { free(w); return S_OK; }
    }
    info.hwnd = owner; info.lpFile = w; info.lpVerb = verb;
    info.nShow = SW_SHOWNORMAL; info.fMask = mask | SEE_MASK_FLAG_NO_UI;
    ok = ShellExecuteExW(&info); free(w);
    return ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}
HRESULT shell_open(HWND owner, const char *path) { return execute(owner, path, L"open", 0); }
HRESULT shell_properties(HWND owner, const char *path) { return execute(owner, path, L"properties", SEE_MASK_INVOKEIDLIST); }
HRESULT shell_open_with(HWND owner, const char *path) {
    wchar_t *w = dfm_wide(path);
    OPENASINFO info = {0}; HRESULT hr;
    if (!w) return HRESULT_FROM_WIN32(GetLastError());
    wchar_t args[32768];
    _snwprintf_s(args, 32768, _TRUNCATE, L"\"%s\"", w);
    hr = test_launch(L"DFM_TEST_OPEN_WITH", args, NULL);
    if (hr != S_FALSE) { free(w); return hr; }
    if (logged("open-with", path)) { free(w); return S_OK; }
    info.pcszFile = w; info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_EXEC;
    hr = SHOpenWithDialog(owner, &info); free(w); return hr;
}
HRESULT shell_terminal(const char *folder) {
    wchar_t *w = dfm_wide(folder), *cmd, *terminal_dir, wt[MAX_PATH], comspec[32768];
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {0};
    BOOL ok; size_t n;
    if (!w) return HRESULT_FROM_WIN32(GetLastError());
    if (GetFileAttributesW(w) == INVALID_FILE_ATTRIBUTES) { free(w); return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND); }
    n = wcslen(w) + MAX_PATH + 32; cmd = malloc(n * sizeof(wchar_t));
    terminal_dir = dfm_join(w, L".");
    if (!cmd || !terminal_dir) { free(cmd); free(terminal_dir); free(w); return E_OUTOFMEMORY; }
    _snwprintf_s(cmd, n, _TRUNCATE, L"-d \"%s\"", terminal_dir);
    HRESULT test = test_launch(L"DFM_TEST_TERMINAL", cmd, w);
    if (test != S_FALSE) { free(cmd); free(terminal_dir); free(w); return test; }
    if (SearchPathW(NULL, L"wt.exe", NULL, MAX_PATH, wt, NULL)) {
        /* The launcher is hidden. Windows Terminal creates the requested GUI. */
        _snwprintf_s(cmd, n, _TRUNCATE, L"\"%s\" -d \"%s\"", wt, terminal_dir);
        ok = CreateProcessW(wt, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, w, &si, &pi);
    } else ok = FALSE;
    if (!ok) {
        DWORD len = GetEnvironmentVariableW(L"COMSPEC", comspec, 32768);
        if (!len || len >= 32768) wcscpy_s(comspec, 32768, L"C:\\Windows\\System32\\cmd.exe");
        /* A visible console is the requested terminal, never a helper console. */
        ok = CreateProcessW(comspec, NULL, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, w, &si, &pi);
    }
    { DWORD error = GetLastError(); free(cmd); free(terminal_dir); free(w);
      if (!ok) return HRESULT_FROM_WIN32(error); }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return S_OK;
}
HRESULT shell_copy_path(HWND owner, const char *path) {
    wchar_t *w = dfm_wide(path); HGLOBAL data; void *p; DWORD error = 0;
    if (!w) return HRESULT_FROM_WIN32(GetLastError());
    data = GlobalAlloc(GMEM_MOVEABLE, (wcslen(w) + 1) * sizeof(wchar_t));
    if (!data) { free(w); return E_OUTOFMEMORY; }
    p = GlobalLock(data);
    if (!p) { free(w); GlobalFree(data); return E_OUTOFMEMORY; }
    memcpy(p, w, (wcslen(w) + 1) * sizeof(wchar_t)); GlobalUnlock(data); free(w);
    if (!OpenClipboard(owner)) error = GetLastError();
    else {
        if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, data)) error = GetLastError();
        else data = NULL;
        CloseClipboard();
    }
    if (data) GlobalFree(data);
    return error ? HRESULT_FROM_WIN32(error) : S_OK;
}
