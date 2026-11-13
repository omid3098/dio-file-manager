#define COBJMACROS
#define DFM_INTERNAL
#include "ops.h"
#include "shell.h"
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HRESULT execute(HWND owner, const char *path, const wchar_t *verb, ULONG mask) {
    wchar_t *w = dfm_wide(path);
    SHELLEXECUTEINFOW info = { sizeof(info) };
    BOOL ok;
    if (!w) return HRESULT_FROM_WIN32(GetLastError());
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
