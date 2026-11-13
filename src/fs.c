#include <windows.h>
#include <shlobj.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fs.h"

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
    if (b < 1000) snprintf(out, cap, "%llu bytes", (unsigned long long)b);
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
    if (da != db) return da ? -1 : 1;
    int c = 0;
    switch (l->sort_key) {
    case FS_SORT_SIZE: if (!da) c = a->size < b->size ? -1 : a->size > b->size; break;
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
int fs_list_state(const fs_list *l) { return (int)l->state; }

static void list_free(fs_list *l) { free(l->items); free(l->names); free(l); }

void fs_list_release(fs_list *l) {
    if (!l) return;
    /* running -> abandoned: the worker frees it; finished -> free now */
    if (InterlockedCompareExchange(&l->owner, 2, 0) != 0) list_free(l);
}

static bool add_entry(fs_list *l, const WIN32_FIND_DATAW *f) {
    char name[1024];
    if (!fs_w_to_u8(f->cFileName, name, sizeof name)) return false;
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
    e->flags = 0;
    if (f->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) e->flags |= FS_DIR;
    if ((f->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || name[0] == '.') e->flags |= FS_HIDDEN;
    if (f->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) e->flags |= FS_LINK;
    e->size = ((uint64_t)f->nFileSizeHigh << 32) | f->nFileSizeLow;
    e->mtime = ((uint64_t)f->ftLastWriteTime.dwHighDateTime << 32) | f->ftLastWriteTime.dwLowDateTime;
    e->kind = (e->flags & FS_DIR) ? FS_KIND_FOLDER : kind_of(name);
    return true;
}

static unsigned __stdcall list_thread(void *arg) {
    fs_list *l = arg;
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
            if (l->owner == 2) break;   /* the window moved on */
            if (f.cFileName[0] == L'.' && (!f.cFileName[1] || (f.cFileName[1] == L'.' && !f.cFileName[2]))) continue;
            add_entry(l, &f);
        } while (FindNextFileW(h, &f));
        FindClose(h);
        if (l->owner != 2) fs_list_sort(l, l->sort_key, l->sort_desc);
    }
    l->state = result;
    if (InterlockedCompareExchange(&l->owner, 1, 0) == 2) list_free(l);
    return 0;
}

fs_list *fs_list_start(const char *path, int sort_key, bool desc) {
    fs_list *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    snprintf(l->path, sizeof l->path, "%s", path);
    l->sort_key = sort_key; l->sort_desc = desc;
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

int fs_drives(fs_drive *out, int max) {
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
