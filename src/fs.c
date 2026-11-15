#include <windows.h>
#include <shlobj.h>
#include <process.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fs.h"
#include "views.h"

int fs_u8_to_w(const char *s, wchar_t *w, int cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, w, cap);
    if (n <= 0 && cap > 0) w[0] = 0;
    return n;
}
int fs_w_to_u8(const wchar_t *w, char *s, int cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, s, cap, NULL, NULL);
    if (n <= 0 && cap > 0) s[0] = 0;
    return n;
}

/* ---- names, kinds, text ---- */

const char *fs_ext_of(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name || !dot[1]) return "";
    return dot + 1;
}

static const struct { const char *ext; uint8_t kind; } kinds[] = {
    {"png", FS_KIND_IMAGE}, {"jpg", FS_KIND_IMAGE}, {"jpeg", FS_KIND_IMAGE}, {"gif", FS_KIND_IMAGE}, {"bmp", FS_KIND_IMAGE},
    {"webp", FS_KIND_IMAGE}, {"svg", FS_KIND_IMAGE}, {"ico", FS_KIND_IMAGE}, {"tif", FS_KIND_IMAGE}, {"heic", FS_KIND_IMAGE},
    {"mp3", FS_KIND_AUDIO}, {"wav", FS_KIND_AUDIO}, {"flac", FS_KIND_AUDIO}, {"ogg", FS_KIND_AUDIO}, {"m4a", FS_KIND_AUDIO}, {"opus", FS_KIND_AUDIO},
    {"mp4", FS_KIND_VIDEO}, {"mkv", FS_KIND_VIDEO}, {"avi", FS_KIND_VIDEO}, {"mov", FS_KIND_VIDEO}, {"webm", FS_KIND_VIDEO},
    {"zip", FS_KIND_ARCHIVE}, {"7z", FS_KIND_ARCHIVE}, {"rar", FS_KIND_ARCHIVE}, {"tar", FS_KIND_ARCHIVE}, {"gz", FS_KIND_ARCHIVE}, {"xz", FS_KIND_ARCHIVE},
    {"pdf", FS_KIND_DOC}, {"doc", FS_KIND_DOC}, {"docx", FS_KIND_DOC}, {"odt", FS_KIND_DOC}, {"ppt", FS_KIND_DOC}, {"pptx", FS_KIND_DOC}, {"epub", FS_KIND_DOC},
    {"txt", FS_KIND_TEXT}, {"md", FS_KIND_TEXT}, {"log", FS_KIND_TEXT}, {"rtf", FS_KIND_TEXT},
    {"c", FS_KIND_CODE}, {"h", FS_KIND_CODE}, {"cpp", FS_KIND_CODE}, {"js", FS_KIND_CODE}, {"ts", FS_KIND_CODE}, {"py", FS_KIND_CODE},
    {"json", FS_KIND_CODE}, {"html", FS_KIND_CODE}, {"css", FS_KIND_CODE}, {"rs", FS_KIND_CODE}, {"go", FS_KIND_CODE}, {"cs", FS_KIND_CODE},
    {"xls", FS_KIND_SHEET}, {"xlsx", FS_KIND_SHEET}, {"csv", FS_KIND_SHEET}, {"ods", FS_KIND_SHEET},
    {"exe", FS_KIND_EXEC}, {"msi", FS_KIND_EXEC}, {"bat", FS_KIND_EXEC}, {"cmd", FS_KIND_EXEC}, {"ps1", FS_KIND_EXEC},
};

static uint8_t kind_of(const char *name) {
    const char *e = fs_ext_of(name);
    if (!*e) return FS_KIND_FILE;
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (_stricmp(kinds[i].ext, e) == 0) return kinds[i].kind;
    return FS_KIND_FILE;
}

const char *fs_kind_name(int kind) {
    static const char *names[FS_KIND_COUNT] = { "Folder", "File", "Image", "Audio", "Video", "Archive",
                                                "Document", "Text", "Code", "Spreadsheet", "Program" };
    return kind >= 0 && kind < FS_KIND_COUNT ? names[kind] : "File";
}

void fs_format_size(char *out, size_t cap, uint64_t b) {
    if (b < 1000) snprintf(out, cap, b == 1 ? "1 byte" : "%llu bytes", (unsigned long long)b);
    else if (b < 1000000) snprintf(out, cap, "%.1f kB", b / 1e3);
    else if (b < 1000000000) snprintf(out, cap, "%.1f MB", b / 1e6);
    else snprintf(out, cap, "%.1f GB", b / 1e9);
}

void fs_format_time(char *out, size_t cap, uint64_t ft) {
    static const char *mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    FILETIME f = { (DWORD)(ft & 0xffffffffu), (DWORD)(ft >> 32) }, lf;
    SYSTEMTIME s, now;
    if (!ft || !FileTimeToLocalFileTime(&f, &lf) || !FileTimeToSystemTime(&lf, &s)) { snprintf(out, cap, "-"); return; }
    GetLocalTime(&now);
    if (s.wYear == now.wYear && s.wMonth == now.wMonth && s.wDay == now.wDay) snprintf(out, cap, "%02d:%02d", s.wHour, s.wMinute);
    else if (s.wYear == now.wYear) snprintf(out, cap, "%s %d", mon[(s.wMonth + 11) % 12], s.wDay);
    else snprintf(out, cap, "%s %d, %d", mon[(s.wMonth + 11) % 12], s.wDay, s.wYear);
}

const char *fs_error_text(int code) {
    switch (code) {
    case ERROR_ACCESS_DENIED: return "Permission denied";
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return "The folder does not exist";
    case ERROR_NOT_READY: return "The drive is not ready";
    case ERROR_DIRECTORY: case ERROR_BAD_PATHNAME: return "Not a folder";
    default: return "The folder could not be read";
    }
}

/* ---- sorting ---- */

int fs_natural_cmp(const char *a, const char *b) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (*x && *y) {
        if (isdigit(*x) && isdigit(*y)) {
            const unsigned char *xs = x, *ys = y;
            while (*xs == '0') xs++;
            while (*ys == '0') ys++;
            const unsigned char *xe = xs, *ye = ys;
            while (isdigit(*xe)) xe++;
            while (isdigit(*ye)) ye++;
            ptrdiff_t lx = xe - xs, ly = ye - ys;
            if (lx != ly) return lx < ly ? -1 : 1;
            int c = memcmp(xs, ys, (size_t)lx);
            if (c) return c < 0 ? -1 : 1;
            x = xe; y = ye;
            continue;
        }
        int cx = *x < 128 ? tolower(*x) : *x, cy = *y < 128 ? tolower(*y) : *y;
        if (cx != cy) return cx < cy ? -1 : 1;
        x++; y++;
    }
    return *x ? 1 : (*y ? -1 : 0);
}

static fs_list *sort_list;   /* qsort has no context argument; the lock below keeps one sort at a time */
static int cmp_entries(const void *pa, const void *pb) {
    const fs_entry *a = pa, *b = pb;
    fs_list *l = sort_list;
    bool da = (a->flags & FS_DIR) != 0, db = (b->flags & FS_DIR) != 0;
    if (da != db && !l->mixed) return da ? -1 : 1;
    int c = 0;
    switch (l->sort_key) {
    case FS_SORT_SIZE: c = a->size < b->size ? -1 : a->size > b->size; break;   /* a folder carries its measured size (0 until known) and sorts by it among folders */
    case FS_SORT_MODIFIED: c = a->mtime < b->mtime ? -1 : a->mtime > b->mtime; break;
    case FS_SORT_TYPE:
        c = a->kind != b->kind ? (int)a->kind - (int)b->kind
                               : _stricmp(fs_ext_of(l->names + a->name_off), fs_ext_of(l->names + b->name_off));
        break;
    default: break;
    }
    if (c) return l->sort_desc ? -c : c;
    c = fs_natural_cmp(l->names + a->name_off, l->names + b->name_off);
    return l->sort_desc && l->sort_key == FS_SORT_NAME ? -c : c;
}

static CRITICAL_SECTION sort_lock;
static INIT_ONCE sort_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK sort_init(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&sort_lock);
    return TRUE;
}

void fs_list_sort(fs_list *l, int key, bool desc) {
    InitOnceExecuteOnce(&sort_once, sort_init, NULL, NULL);
    EnterCriticalSection(&sort_lock);
    l->sort_key = key; l->sort_desc = desc;
    sort_list = l;
    qsort(l->items, (size_t)l->count, sizeof(fs_entry), cmp_entries);
    LeaveCriticalSection(&sort_lock);
}

/* ---- listing on a worker thread ---- */

const char *fs_name(const fs_list *l, int i) { return l->names + l->items[i].name_off; }
int fs_list_state(const fs_list *l) { return (int)InterlockedCompareExchange((volatile LONG *)&l->state, 0, 0); }

static void list_free(fs_list *l) { free(l->items); free(l->names); free(l->extra); free(l); }

void fs_list_release(fs_list *l) {
    if (!l) return;
    InterlockedExchange(&l->abandoned, 1);
    if (InterlockedDecrement(&l->owner) == 0) list_free(l);
}

static uint32_t extra_add(fs_list *l, const char *s) {
    if (!s || !s[0]) return 0;
    size_t len = strlen(s) + 1;
    if (!l->extra) {
        l->extra = calloc(1, 4096);
        if (!l->extra) return 0;
        l->extra_cap = 4096; l->extra_len = 1;
    }
    if (l->extra_len + len > l->extra_cap) {
        size_t nc = l->extra_cap * 2;
        while (nc < l->extra_len + len) nc *= 2;
        char *nn = realloc(l->extra, nc);
        if (!nn) return 0;
        l->extra = nn; l->extra_cap = nc;
    }
    uint32_t off = (uint32_t)l->extra_len;
    memcpy(l->extra + off, s, len);
    l->extra_len += len;
    return off;
}

const char *fs_extra(const fs_list *l, uint32_t off) { return l->extra && off < l->extra_len ? l->extra + off : ""; }

bool fs_list_add(fs_list *l, const char *name, uint32_t flags, uint64_t size, uint64_t mtime, const char *path, const char *id) {
    size_t len = strlen(name) + 1;
    if (l->count == l->cap) {
        int nc = l->cap ? l->cap * 2 : 1024;
        fs_entry *ni = realloc(l->items, (size_t)nc * sizeof *ni);
        if (!ni) return false;
        l->items = ni; l->cap = nc;
    }
    if (l->names_len + len > l->names_cap) {
        size_t nc = l->names_cap ? l->names_cap * 2 : 32768;
        while (nc < l->names_len + len) nc *= 2;
        char *nn = realloc(l->names, nc);
        if (!nn) return false;
        l->names = nn; l->names_cap = nc;
    }
    fs_entry *e = &l->items[l->count++];
    e->name_off = (uint32_t)l->names_len;
    memcpy(l->names + l->names_len, name, len);
    l->names_len += len;
    e->flags = flags;
    e->size = size;
    e->mtime = mtime;
    e->kind = (flags & FS_DIR) ? FS_KIND_FOLDER : kind_of(name);
    e->path_off = extra_add(l, path);
    e->id_off = extra_add(l, id);
    return true;
}

/* The copy engine writes ".<final name>.dfm-copy-<pid>-<n>.tmp" and renames it when the copy is done. While the
 * process that owns it is alive, the file is listed under <final name> as an incoming file, as GNOME Files shows
 * a file that is being copied. Leftovers of a dead process stay ordinary hidden files. */
static bool incoming_name(const wchar_t *w, wchar_t *final, size_t cap) {
    size_t n = wcslen(w);
    if (n < 18 || w[0] != L'.' || wcscmp(w + n - 4, L".tmp")) return false;
    size_t tag = n - 4;
    while (tag > 1 && w[tag - 1] != L'-') tag--;               /* serial */
    size_t serial_at = tag;
    if (serial_at >= n - 4 || tag < 3) return false;
    for (size_t i = serial_at; i < n - 4; i++) if (w[i] < L'0' || w[i] > L'9') return false;
    size_t pid_at = tag - 1;                                    /* the dash before the serial */
    while (pid_at > 1 && w[pid_at - 1] != L'-') pid_at--;
    if (pid_at >= tag - 1) return false;
    unsigned long pid = 0;
    for (size_t i = pid_at; i < tag - 1; i++) { if (w[i] < L'0' || w[i] > L'9') return false; pid = pid * 10 + (unsigned long)(w[i] - L'0'); }
    static const wchar_t mark[] = L".dfm-copy-";
    size_t ml = wcslen(mark);
    if (pid_at < ml + 2 || wcsncmp(w + pid_at - ml, mark, ml)) return false;
    size_t fl = pid_at - ml - 1;                                /* length of the final name */
    if (fl == 0 || fl >= cap) return false;
    if (pid != GetCurrentProcessId()) {
        /* a live process with that number must also be this program: a crashed copy's number may be reused */
        HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!p) return false;
        wchar_t theirs[1024], ours[1024];
        DWORD tn = 1024;
        bool mine = WaitForSingleObject(p, 0) == WAIT_TIMEOUT && QueryFullProcessImageNameW(p, 0, theirs, &tn) &&
                    GetModuleFileNameW(NULL, ours, 1024) < 1024 && !_wcsicmp(theirs, ours);
        CloseHandle(p);
        if (!mine) return false;
    }
    memcpy(final, w + 1, fl * sizeof *w); final[fl] = 0;
    return true;
}

static bool add_entry(fs_list *l, const WIN32_FIND_DATAW *f) {
    char name[1024];
    wchar_t final[300];
    uint32_t flags = 0;
    if (!(f->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && incoming_name(f->cFileName, final, 300) && fs_w_to_u8(final, name, sizeof name))
        return fs_list_add(l, name, FS_INCOMING, ((uint64_t)f->nFileSizeHigh << 32) | f->nFileSizeLow,
                           ((uint64_t)f->ftLastWriteTime.dwHighDateTime << 32) | f->ftLastWriteTime.dwLowDateTime, NULL, NULL);
    if (!fs_w_to_u8(f->cFileName, name, sizeof name)) return false;
    if (f->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) flags |= FS_DIR;
    if ((f->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || name[0] == '.') flags |= FS_HIDDEN;
    if (f->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) flags |= FS_LINK;
    return fs_list_add(l, name, flags, ((uint64_t)f->nFileSizeHigh << 32) | f->nFileSizeLow,
                       ((uint64_t)f->ftLastWriteTime.dwHighDateTime << 32) | f->ftLastWriteTime.dwLowDateTime, NULL, NULL);
}

static unsigned test_list_delay_ms;
void fs_test_list_delay(unsigned ms) { test_list_delay_ms = ms; }

static unsigned __stdcall list_thread(void *arg) {
    fs_list *l = arg;
    for (unsigned waited = 0; waited < test_list_delay_ms && !l->abandoned; waited += 20) Sleep(20);
    if (l->vkind) {
        long r = views_fill(l);
        if (!l->abandoned) fs_list_sort(l, l->sort_key, l->sort_desc);
        InterlockedExchange(&l->state, r);
        if (InterlockedDecrement(&l->owner) == 0) list_free(l);
        return 0;
    }
    wchar_t wpath[FS_PATH_MAX + 4];
    fs_u8_to_w(l->path, wpath, FS_PATH_MAX);
    size_t n = wcslen(wpath);
    if (n && wpath[n - 1] != L'\\') wpath[n++] = L'\\';
    wpath[n++] = L'*'; wpath[n] = 0;
    WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileExW(wpath, FindExInfoBasic, &f, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    long result = FS_DONE;
    if (h == INVALID_HANDLE_VALUE) {
        l->error = (int)GetLastError();
        result = FS_FAILED;
    } else {
        do {
            if (l->abandoned) break;   /* the window moved on */
            if (f.cFileName[0] == L'.' && (!f.cFileName[1] || (f.cFileName[1] == L'.' && !f.cFileName[2]))) continue;
            add_entry(l, &f);
        } while (FindNextFileW(h, &f));
        FindClose(h);
        /* An incoming file whose final name already exists (a replace in progress) is not listed twice. */
        for (int i = l->count - 1; i >= 0 && !l->abandoned; i--) {
            if (!(l->items[i].flags & FS_INCOMING)) continue;
            wchar_t full[FS_PATH_MAX + 300], leaf[300];
            fs_u8_to_w(l->names + l->items[i].name_off, leaf, 300);
            wcscpy_s(full, FS_PATH_MAX + 300, wpath);
            full[n - 1] = 0;
            wcscat_s(full, FS_PATH_MAX + 300, leaf);
            if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) l->items[i] = l->items[--l->count];
        }
        if (!l->abandoned) fs_list_sort(l, l->sort_key, l->sort_desc);
    }
    /* Publishing DONE must not let navigation free the worker's last writes. */
    InterlockedExchange(&l->state, result);
    if (InterlockedDecrement(&l->owner) == 0) list_free(l);
    return 0;
}

fs_list *fs_list_start(const char *path, int sort_key, bool desc) {
    fs_list *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    snprintf(l->path, sizeof l->path, "%s", path);
    l->sort_key = sort_key; l->sort_desc = desc;
    l->owner = 2;
    uintptr_t t = _beginthreadex(NULL, 0, list_thread, l, 0, NULL);
    if (!t) { l->state = FS_FAILED; l->error = -1; l->owner = 1; return l; }
    CloseHandle((HANDLE)t);
    return l;
}

fs_list *fs_list_start_virtual(int vkind, int sort_key, bool desc) {
    fs_list *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->vkind = vkind; l->mixed = true;
    l->sort_key = sort_key; l->sort_desc = desc;
    l->owner = 2;
    uintptr_t t = _beginthreadex(NULL, 0, list_thread, l, 0, NULL);
    if (!t) { l->state = FS_FAILED; l->error = -1; l->owner = 1; return l; }
    CloseHandle((HANDLE)t);
    return l;
}

/* ---- places and drives ---- */

bool fs_known(int which, char *out, size_t cap) {
    const GUID *ids[7];
    ids[0] = &FOLDERID_Profile; ids[1] = &FOLDERID_Desktop; ids[2] = &FOLDERID_Documents; ids[3] = &FOLDERID_Downloads;
    ids[4] = &FOLDERID_Music; ids[5] = &FOLDERID_Pictures; ids[6] = &FOLDERID_Videos;
    PWSTR w = NULL;
    if (which < 0 || which > 6 || FAILED(SHGetKnownFolderPath(ids[which], 0, NULL, &w))) return false;
    bool ok = fs_w_to_u8(w, out, (int)cap) > 0;
    CoTaskMemFree(w);
    return ok;
}

static char *test_drives, *test_eject_log;
static DWORD test_ejected;
void fs_test_drives(const char *list, const char *log) {
    test_drives = list ? _strdup(list) : NULL;
    test_eject_log = log ? _strdup(log) : NULL;
}

int fs_drives(fs_drive *out, int max) {
    if (test_drives) {
        FILE *f = fopen(test_drives, "r");
        int n = 0, type; char letter, label[80];
        if (!f) return 0;
        while (n < max && fscanf(f, " %c %d %79[^\n]", &letter, &type, label) == 3) {
            if (letter < 'A' || letter > 'Z' || (test_ejected & (1u << (letter - 'A')))) continue;
            fs_drive *d = &out[n++]; memset(d, 0, sizeof *d);
            snprintf(d->root, sizeof d->root, "%c:\\", letter);
            char *bar = strchr(label, '|');   /* test only: "Label|total bytes|free bytes" gives the drive a size */
            if (bar) { *bar = 0; d->total = _strtoui64(bar + 1, &bar, 10); if (*bar == '|') d->free = _strtoui64(bar + 1, NULL, 10); }
            snprintf(d->name, sizeof d->name, "%s (%c:)", label, letter); d->type = type;
        }
        fclose(f); return n;
    }
    UINT old = SetErrorMode(SEM_FAILCRITICALERRORS);
    DWORD mask = GetLogicalDrives();
    int n = 0;
    for (int i = 0; i < 26 && n < max; i++) {
        if (!(mask & (1u << i))) continue;
        fs_drive *d = &out[n];
        memset(d, 0, sizeof *d);
        d->root[0] = (char)('A' + i); d->root[1] = ':'; d->root[2] = '\\';
        wchar_t wr[4] = { (wchar_t)('A' + i), L':', L'\\', 0 };
        d->type = (int)GetDriveTypeW(wr);
        if (d->type == DRIVE_NO_ROOT_DIR) continue;
        wchar_t label[64] = L"";
        if (d->type == DRIVE_FIXED || d->type == DRIVE_REMOVABLE) {
            GetVolumeInformationW(wr, label, 64, NULL, NULL, NULL, NULL, 0);
            ULARGE_INTEGER fa, tb, tf;
            if (GetDiskFreeSpaceExW(wr, &fa, &tb, &tf)) { d->total = tb.QuadPart; d->free = tf.QuadPart; }
        }
        char lab[96] = "";
        if (label[0]) fs_w_to_u8(label, lab, sizeof lab);
        else snprintf(lab, sizeof lab, "%s", d->type == DRIVE_REMOVABLE ? "Removable Disk" : d->type == DRIVE_CDROM ? "Optical Drive"
                                              : d->type == DRIVE_REMOTE ? "Network Drive" : "Local Disk");
        snprintf(d->name, sizeof d->name, "%.80s (%c:)", lab, 'A' + i);
        n++;
    }
    SetErrorMode(old);
    return n;
}

/* PnP performs the safe-removal negotiation, including vetoes from open files. */
unsigned long fs_eject(const char *root) {
    if (!root || strlen(root) != 3 || root[0] < 'A' || root[0] > 'Z' || root[1] != ':' || root[2] != '\\') return ERROR_INVALID_PARAMETER;
    if (test_drives) {
        fs_drive drives[26]; int n = fs_drives(drives, 26), i;
        for (i = 0; i < n; i++) if (!strcmp(root, drives[i].root)) break;
        if (i == n || drives[i].type != DRIVE_REMOVABLE) return ERROR_NOT_SUPPORTED;
        const char *hold = getenv("DFM_TEST_EJECT_HOLD");   /* a check keeps the eject pending while this file exists, to look at the window meanwhile */
        for (int w = 0; hold && w < 300 && GetFileAttributesA(hold) != INVALID_FILE_ATTRIBUTES; w++) Sleep(50);
        if (!test_eject_log) return ERROR_ACCESS_DENIED;
        FILE *f = fopen(test_eject_log, "a");
        if (!f) return ERROR_WRITE_FAULT;
        int ok = fprintf(f, "%s\n", root) > 0;
        if (fclose(f)) ok = 0;
        if (ok) test_ejected |= 1u << (root[0] - 'A');
        return ok ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
    }
    wchar_t wr[4], volume[] = L"\\\\.\\X:";
    fs_u8_to_w(root, wr, 4);
    if (GetDriveTypeW(wr) != DRIVE_REMOVABLE) return ERROR_NOT_SUPPORTED;
    volume[4] = wr[0];
    HANDLE h = CreateFileW(volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    STORAGE_DEVICE_NUMBER wanted; DWORD bytes;
    BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &wanted, sizeof wanted, &bytes, NULL);
    DWORD error = ok ? ERROR_NOT_FOUND : GetLastError();
    CloseHandle(h); if (!ok) return error;
    static const GUID disk = {0x53f56307,0xb6bf,0x11d0,{0x94,0xf2,0x00,0xa0,0xc9,0x1e,0xfb,0x8b}};
    HDEVINFO devices = SetupDiGetClassDevsW(&disk, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return GetLastError();
    SP_DEVICE_INTERFACE_DATA iface = { sizeof iface };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devices, NULL, &disk, i, &iface); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &iface, NULL, 0, &need, NULL);
        if (!need) continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = malloc(need);
        if (!detail) { error = ERROR_NOT_ENOUGH_MEMORY; break; }
        detail->cbSize = sizeof *detail;
        SP_DEVINFO_DATA dev = { sizeof dev };
        ok = SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, need, NULL, &dev);
        h = ok ? CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL) : INVALID_HANDLE_VALUE;
        free(detail);
        if (h == INVALID_HANDLE_VALUE) continue;
        STORAGE_DEVICE_NUMBER got;
        ok = DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &got, sizeof got, &bytes, NULL);
        CloseHandle(h);
        if (!ok || got.DeviceType != wanted.DeviceType || got.DeviceNumber != wanted.DeviceNumber) continue;
        PNP_VETO_TYPE veto; wchar_t name[MAX_PATH];
        CONFIGRET cr = CM_Request_Device_EjectW(dev.DevInst, &veto, name, MAX_PATH, 0);
        error = cr == CR_SUCCESS ? ERROR_SUCCESS : cr == CR_REMOVE_VETOED ? ERROR_BUSY : CM_MapCrToWin32Err(cr, ERROR_GEN_FAILURE);
        break;
    }
    SetupDiDestroyDeviceInfoList(devices);
    return error;
}

bool fs_is_dir(const char *path) {
    wchar_t w[FS_PATH_MAX];
    fs_u8_to_w(path, w, FS_PATH_MAX);
    DWORD a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool fs_parent(const char *path, char *out, size_t cap) {
    size_t n = strlen(path);
    while (n > 3 && path[n - 1] == '\\') n--;
    if (n <= 3) return false;   /* "C:\" */
    while (n > 0 && path[n - 1] != '\\') n--;
    if (n <= 3) n = 3;
    else n--;
    if (n >= cap) return false;
    memcpy(out, path, n);
    out[n] = 0;
    return true;
}

int fs_subdirs(const char *path, char names[][128], int max) {
    wchar_t wp[FS_PATH_MAX + 4];
    fs_u8_to_w(path, wp, FS_PATH_MAX);
    size_t n = wcslen(wp);
    if (n && wp[n - 1] != L'\\') wp[n++] = L'\\';
    wp[n++] = L'*'; wp[n] = 0;
    WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileExW(wp, FindExInfoBasic, &f, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    int count = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (f.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (f.cFileName[0] == L'.') continue;
        if (count < max) fs_w_to_u8(f.cFileName, names[count++], 128);
    } while (count < max && FindNextFileW(h, &f));
    FindClose(h);
    return count;
}
