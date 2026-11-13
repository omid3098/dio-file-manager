/* Reading folders, the places (known folders) and the drives. UTF-8 inside, UTF-16 only at Win32 calls. */
#ifndef DFM_FS_H
#define DFM_FS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FS_PATH_MAX 1040

enum { FS_DIR = 1, FS_HIDDEN = 2, FS_LINK = 4 };
typedef enum {
    FS_KIND_FOLDER, FS_KIND_FILE, FS_KIND_IMAGE, FS_KIND_AUDIO, FS_KIND_VIDEO, FS_KIND_ARCHIVE,
    FS_KIND_DOC, FS_KIND_TEXT, FS_KIND_CODE, FS_KIND_SHEET, FS_KIND_EXEC, FS_KIND_COUNT
} fs_kind;
typedef enum { FS_SORT_NAME, FS_SORT_SIZE, FS_SORT_MODIFIED, FS_SORT_TYPE } fs_sort_key;
typedef enum { FS_LOADING = 0, FS_DONE = 1, FS_FAILED = 2 } fs_state;

typedef struct fs_entry {
    uint32_t name_off;   /* offset into fs_list.names */
    uint32_t flags;      /* FS_DIR | FS_HIDDEN | FS_LINK */
    uint64_t size;
    uint64_t mtime;      /* FILETIME */
    uint8_t kind;        /* fs_kind */
    uint32_t path_off;   /* virtual lists: full path, offset into fs_list.extra (0 = none) */
    uint32_t id_off;     /* virtual lists: Recycle Bin item id */
} fs_entry;

/* One folder listing. A worker thread fills it; the window reads it only after fs_list_state() is not FS_LOADING. */
typedef struct fs_list {
    fs_entry *items;
    int count, cap;
    char *names;
    size_t names_len, names_cap;
    char path[FS_PATH_MAX];
    int error;           /* Win32 error code when FS_FAILED */
    int sort_key;
    bool sort_desc;
    volatile long state; /* fs_state */
    volatile long owner; /* references held by the window and listing worker */
    volatile long abandoned;
    char *extra;         /* second string pool for virtual lists; offset 0 is the empty string */
    size_t extra_len, extra_cap;
    int vkind;           /* 0 a folder; else FS_V_* (Recent, Starred, Trash) */
    bool mixed;          /* sort without putting folders first */
} fs_list;
enum { FS_V_NONE = 0, FS_V_RECENT = 1, FS_V_STARRED = 2, FS_V_TRASH = 3 };

fs_list *fs_list_start(const char *path, int sort_key, bool sort_desc);  /* returns at once */
fs_list *fs_list_start_virtual(int vkind, int sort_key, bool sort_desc); /* Recent, Starred or Trash; filled by views_fill */
bool fs_list_add(fs_list *l, const char *name, uint32_t flags, uint64_t size, uint64_t mtime, const char *path, const char *id);
const char *fs_extra(const fs_list *l, uint32_t off);
int fs_list_state(const fs_list *l);
void fs_list_release(fs_list *l);               /* frees now, or when the worker finishes */
const char *fs_name(const fs_list *l, int i);
void fs_list_sort(fs_list *l, int sort_key, bool desc);
int fs_natural_cmp(const char *a, const char *b);
const char *fs_error_text(int code);

const char *fs_kind_name(int kind);
const char *fs_ext_of(const char *name);        /* extension without the dot, or "" */
void fs_format_size(char *out, size_t cap, uint64_t bytes);
void fs_format_time(char *out, size_t cap, uint64_t filetime);

/* Places */
typedef struct fs_drive {
    char root[4];        /* "C:\" */
    char name[96];       /* "Local Disk (C:)" */
    int type;            /* DRIVE_* */
    uint64_t total, free;
} fs_drive;
bool fs_known(int which, char *out, size_t cap);   /* 0 home, 1 desktop, 2 documents, 3 downloads, 4 music, 5 pictures, 6 videos */
int fs_drives(fs_drive *out, int max);
/* Safe PnP removal. Fixed/network roots are refused even when called directly. Blocking. */
unsigned long fs_eject(const char *root);
/* Only main's --test-state path enables the sandbox list and eject stand-in. */
void fs_test_drives(const char *list, const char *eject_log);
bool fs_is_dir(const char *path);
bool fs_parent(const char *path, char *out, size_t cap);   /* false at a drive root */
int fs_subdirs(const char *path, char names[][128], int max);  /* a few child folders, for path bar menus */

/* UTF-8 and UTF-16 */
int fs_u8_to_w(const char *s, wchar_t *w, int cap);
int fs_w_to_u8(const wchar_t *w, char *s, int cap);
#endif
