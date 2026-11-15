/* The open folder follows the disk: one worker thread per watched folder holds a ReadDirectoryChangesW (no subtree) and
 * tells the window when a burst of changes has settled. Idle, the worker waits with no timeout. The folder's handle is
 * shared for delete, and it is closed on Windows' query-remove of its device (and when the folder is left), so it never
 * keeps a USB drive from being ejected. */
#include <windows.h>
#include <dbt.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "watch.h"
#include "fs.h"

#define SETTLE_MS 200     /* quiet time after the last change before the list is read again */
#define BURST_MS 500      /* in a long burst, read again at least this often */
#define BUF_BYTES 65536

typedef struct watch {
    volatile LONG refs;
    HANDLE stop, closed;
    HANDLE dir;                       /* owned by the worker */
    HDEVNOTIFY volatile dn;           /* registered by the worker; whoever swaps it out unregisters it */
    volatile LONG pending;            /* a settled change the window has not taken yet */
    volatile LONG removing;           /* Windows asked to remove the device: the registration stays for the answer */
    char path[FS_PATH_MAX];
    wchar_t wpath[FS_PATH_MAX + 4];
} watch;

static watch *cur;
static HWND notify_wnd;
static void (*notify_cb)(void);
static volatile LONG events;

void watch_init(void *hwnd, void (*notify)(void)) { notify_wnd = (HWND)hwnd; notify_cb = notify; }
unsigned watch_test_events(void) { return (unsigned)InterlockedCompareExchange(&events, 0, 0); }
static volatile LONGLONG burst_t0;
unsigned long long watch_test_t0(void) { return (unsigned long long)burst_t0; }

static void release(watch *w) {
    if (InterlockedDecrement(&w->refs) != 0) return;
    CloseHandle(w->stop); CloseHandle(w->closed);
    free(w);
}

static void unregister(watch *w) {
    HDEVNOTIFY h = (HDEVNOTIFY)InterlockedExchangePointer((PVOID volatile *)&w->dn, NULL);
    if (h) UnregisterDeviceNotification(h);
}

/* A copy in progress writes ".<name>.dfm-copy-<pid>-<n>.tmp" and grows it for a long time: those growth events alone
 * do not change the list (the file shows as "Copying..."), so they do not ask for a new reading. */
static bool copy_growth(const FILE_NOTIFY_INFORMATION *n) {
    if (n->Action != FILE_ACTION_MODIFIED) return false;
    size_t len = n->FileNameLength / sizeof(wchar_t);
    if (len < 18 || n->FileName[0] != L'.' || wcsncmp(n->FileName + len - 4, L".tmp", 4)) return false;
    for (size_t i = 1; i + 10 <= len; i++) if (!wcsncmp(n->FileName + i, L".dfm-copy-", 10)) return true;
    return false;
}

/* A folder's own "modified" time moves when something inside it changes; NTFS reports that late, in the folder that holds
 * it. It does not change what the list shows (names, kinds and the sizes of folders are measured on entry), so a batch of
 * only such events does not ask for a new reading. */
static bool folder_touch(const watch *w, const FILE_NOTIFY_INFORMATION *n) {
    if (n->Action != FILE_ACTION_MODIFIED) return false;
    size_t dl = wcslen(w->wpath), nl = n->FileNameLength / sizeof(wchar_t);
    if (dl + nl + 2 >= FS_PATH_MAX + 4 || nl == 0) return false;
    wchar_t full[FS_PATH_MAX + 8];
    wcscpy_s(full, FS_PATH_MAX + 8, w->wpath);
    if (full[dl - 1] != L'\\') full[dl++] = L'\\';
    memcpy(full + dl, n->FileName, nl * sizeof(wchar_t));
    full[dl + nl] = 0;
    DWORD at = GetFileAttributesW(full);
    return at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY) && !(at & FILE_ATTRIBUTE_REPARSE_POINT);
}

/* Does this batch hold a change that matters? An empty batch means Windows lost events (the buffer overflowed). */
static bool relevant(const watch *w, const BYTE *buf, DWORD got) {
    if (!got) return true;
    const BYTE *p = buf;
    int probes = 0;
    for (;;) {
        const FILE_NOTIFY_INFORMATION *n = (const FILE_NOTIFY_INFORMATION *)p;
        if (!copy_growth(n) && (++probes > 64 || !folder_touch(w, n))) return true;   /* a huge batch is not looked through */
        if (!n->NextEntryOffset) return false;
        p += n->NextEntryOffset;
    }
}

static void fire(watch *w) {
    InterlockedExchange(&w->pending, 1);
    InterlockedIncrement(&events);
    if (notify_cb) notify_cb();
}

static unsigned __stdcall watch_thread(void *arg) {
    watch *w = arg;
    BYTE *buf = malloc(BUF_BYTES), *copy = malloc(BUF_BYTES);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!buf || !copy || !ev) goto out;
    w->dir = CreateFileW(w->wpath, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (w->dir == INVALID_HANDLE_VALUE) { w->dir = NULL; goto out; }
    if (WaitForSingleObject(w->stop, 0) == WAIT_OBJECT_0) goto out;
    if (notify_wnd) {
        DEV_BROADCAST_HANDLE f = { sizeof f, DBT_DEVTYP_HANDLE, 0, w->dir };
        w->dn = RegisterDeviceNotificationW(notify_wnd, &f, DEVICE_NOTIFY_WINDOW_HANDLE);
    }
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                         FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
    OVERLAPPED ov = { 0 };
    ov.hEvent = ev;
    bool burst = false;
    ULONGLONG t0 = 0, last = 0;
    bool armed = ReadDirectoryChangesW(w->dir, buf, BUF_BYTES, FALSE, filter, NULL, &ov, NULL) != 0;
    for (;;) {
        DWORD wait = INFINITE;
        if (burst) {
            ULONGLONG now = GetTickCount64();
            ULONGLONG a = last + SETTLE_MS, b = t0 + BURST_MS, due = a < b ? a : b;
            wait = due > now ? (DWORD)(due - now) : 0;
        }
        HANDLE hs[2] = { w->stop, ev };
        DWORD r = armed ? WaitForMultipleObjects(2, hs, FALSE, wait) : WaitForSingleObject(w->stop, wait);
        if (r == WAIT_OBJECT_0) break;
        if (armed && r == WAIT_OBJECT_0 + 1) {
            DWORD got = 0;
            BOOL ok = GetOverlappedResult(w->dir, &ov, &got, FALSE);
            if (ok) {
                memcpy(copy, buf, got);
                ResetEvent(ev);
                armed = ReadDirectoryChangesW(w->dir, buf, BUF_BYTES, FALSE, filter, NULL, &ov, NULL) != 0;   /* re-armed before the batch is read */
                if (relevant(w, copy, got)) { ULONGLONG now = GetTickCount64(); if (!burst) { burst = true; t0 = now; burst_t0 = (LONGLONG)now; } last = now; }
            } else {
                /* the folder went away (deleted, or its drive left): let the window read it, which shows what is wrong, and stop */
                fire(w);
                break;
            }
        } else if (r == WAIT_TIMEOUT || !armed) {
            if (!armed) { fire(w); break; }
        }
        if (burst) {
            ULONGLONG now = GetTickCount64();
            if (now - last >= SETTLE_MS || now - t0 >= BURST_MS) { burst = false; fire(w); }
        }
    }
    if (armed) { CancelIoEx(w->dir, &ov); DWORD got; GetOverlappedResult(w->dir, &ov, &got, TRUE); }
out:
    if (!w->removing) unregister(w);
    if (w->dir) { CloseHandle(w->dir); w->dir = NULL; }
    if (ev) CloseHandle(ev);
    free(buf); free(copy);
    SetEvent(w->closed);
    release(w);
    return 0;
}

void watch_close(bool wait) {
    watch *w = cur;
    if (!w) return;
    cur = NULL;
    SetEvent(w->stop);
    if (wait) WaitForSingleObject(w->closed, 3000);
    if (w->removing) unregister(w);
    release(w);
}

void watch_open(const char *dir) {
    watch_close(false);
    if (!dir || !dir[0]) return;
    watch *w = calloc(1, sizeof *w);
    if (!w) return;
    w->refs = 2;
    w->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->closed = CreateEventW(NULL, TRUE, FALSE, NULL);
    snprintf(w->path, sizeof w->path, "%s", dir);
    fs_u8_to_w(dir, w->wpath, FS_PATH_MAX);
    uintptr_t t = w->stop && w->closed ? _beginthreadex(NULL, 0, watch_thread, w, 0, NULL) : 0;
    if (!t) { CloseHandle(w->stop); CloseHandle(w->closed); free(w); return; }
    CloseHandle((HANDLE)t);
    cur = w;
}

bool watch_take(void) { return cur && InterlockedExchange(&cur->pending, 0) != 0; }
bool watch_is_open(void) { return cur != NULL; }

bool watch_device_event(uintptr_t wparam, intptr_t lparam) {
    watch *w = cur;
    if (!w || !lparam || (wparam != DBT_DEVICEQUERYREMOVE && wparam != DBT_DEVICEQUERYREMOVEFAILED && wparam != DBT_DEVICEREMOVECOMPLETE)) return false;
    const DEV_BROADCAST_HANDLE *h = (const DEV_BROADCAST_HANDLE *)lparam;
    if (h->dbch_devicetype != DBT_DEVTYP_HANDLE || !w->dn || h->dbch_hdevnotify != w->dn) return false;
    if (wparam == DBT_DEVICEQUERYREMOVE) {
        InterlockedExchange(&w->removing, 1);
        SetEvent(w->stop);
        WaitForSingleObject(w->closed, 3000);   /* the handle is closed before Windows is answered: nothing of ours holds the drive */
    } else if (wparam == DBT_DEVICEQUERYREMOVEFAILED) {
        char path[FS_PATH_MAX];
        snprintf(path, sizeof path, "%s", w->path);
        watch_open(path);                       /* the device stays: watch again, and read the folder once (it may have changed) */
        if (cur) fire(cur);
    } else {
        unregister(w);
    }
    return true;
}
