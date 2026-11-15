#include <windows.h>
#include <shobjidl.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shortcut.h"
#include "fs.h"

struct shortcut_job {
    volatile LONG refs;      /* the window and the worker each hold one */
    volatile LONG done;
    HANDLE event;
    bool read;               /* read the shortcut, or check the target */
    bool exists, is_dir;
    char in[FS_PATH_MAX], target[FS_PATH_MAX];
};

static void trim_slashes(char *p) {   /* "C:\dir\" becomes "C:\dir"; "C:\" stays */
    size_t n = strlen(p);
    while (n > 3 && p[n - 1] == '\\') n--;
    p[n] = 0;
}

static void read_shortcut(shortcut_job *j) {
    wchar_t wl[FS_PATH_MAX], path[2048] = L"";
    j->target[0] = 0;
    if (!fs_u8_to_w(j->in, wl, FS_PATH_MAX)) return;
    IShellLinkW *link = NULL;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link)) || !link) return;
    IPersistFile *file = NULL;
    if (SUCCEEDED(link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&file)) && file) {
        /* Load only reads the file; Resolve (which can search and rewrite the shortcut) is never called. */
        if (SUCCEEDED(file->lpVtbl->Load(file, wl, STGM_READ)) &&
            link->lpVtbl->GetPath(link, path, 2048, NULL, 0) == S_OK && path[0] &&
            fs_w_to_u8(path, j->target, FS_PATH_MAX)) trim_slashes(j->target);
        else j->target[0] = 0;
        file->lpVtbl->Release(file);
    }
    link->lpVtbl->Release(link);
}

static void check_target(shortcut_job *j) {
    wchar_t w[FS_PATH_MAX];
    j->exists = j->is_dir = false;
    if (!fs_u8_to_w_long(j->in, w, FS_PATH_MAX)) return;
    DWORD a = GetFileAttributesW(w);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    j->exists = true;
    j->is_dir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static void job_unref(shortcut_job *j) {
    if (InterlockedDecrement(&j->refs) == 0) { CloseHandle(j->event); free(j); }
}

static unsigned __stdcall worker(void *arg) {
    shortcut_job *j = arg;
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (j->read) read_shortcut(j); else check_target(j);
    if (SUCCEEDED(co)) CoUninitialize();
    InterlockedExchange(&j->done, 1);
    SetEvent(j->event);
    job_unref(j);
    return 0;
}

static shortcut_job *start(const char *in, bool read) {
    shortcut_job *j = calloc(1, sizeof *j);
    if (!j) return NULL;
    j->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    j->read = read;
    snprintf(j->in, sizeof j->in, "%s", in);
    j->refs = 2;
    uintptr_t t = j->event ? _beginthreadex(NULL, 0, worker, j, 0, NULL) : 0;
    if (!t) { if (j->event) CloseHandle(j->event); free(j); return NULL; }
    CloseHandle((HANDLE)t);
    return j;
}

shortcut_job *shortcut_read_start(const char *lnk) { return start(lnk, true); }
shortcut_job *shortcut_check_start(const char *target) { return start(target, false); }

bool shortcut_wait(shortcut_job *j, DWORD ms) {
    if (!j) return false;
    if (!InterlockedCompareExchange(&j->done, 0, 0) && ms) WaitForSingleObject(j->event, ms);
    return InterlockedCompareExchange(&j->done, 0, 0) != 0;
}

bool shortcut_done(shortcut_job *j) { return shortcut_wait(j, 0); }
const char *shortcut_input(const shortcut_job *j) { return j->in; }
const char *shortcut_target(const shortcut_job *j) { return j->target; }
bool shortcut_exists(const shortcut_job *j) { return j->exists; }
bool shortcut_is_dir(const shortcut_job *j) { return j->is_dir; }

void shortcut_release(shortcut_job *j) { if (j) job_unref(j); }

void shortcut_place(const char *target, char *folder, size_t fcap, char *leaf, size_t lcap) {
    const char *p = strrchr(target, '\\');
    bool unc = target[0] == '\\' && target[1] == '\\';
    int seps = 0;
    for (const char *c = target + (unc ? 2 : 0); *c; c++) if (*c == '\\') seps++;
    if (!p || (unc && seps <= 1) || !fs_parent(target, folder, fcap)) {   /* a drive or share root: nothing holds it */
        snprintf(folder, fcap, "%s", target);
        if (lcap) leaf[0] = 0;
        return;
    }
    snprintf(leaf, lcap, "%s", p + 1);
}
