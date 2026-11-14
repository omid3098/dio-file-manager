/* Folder sizes in the window: asks the engine (foldersize.c) for the shown folder's child folders, reads their sizes
 * for the Size column, the sort and the status pill. The window never walks a folder. */
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "foldersize.h"

static fs_list *req_list;        /* the listing the engine was asked about */
static bool req_done;            /* its sizes have all arrived and the one re-sort has happened */
static char elig_path[FS_PATH_MAX];
static bool elig;

static bool sizes_on(void) {
    if (A.loc != LOC_FOLDER || A.searching || !A.list || !A.list_seen || fs_list_state(A.list) != FS_DONE) return false;
    if (strcmp(elig_path, A.path)) { snprintf(elig_path, sizeof elig_path, "%s", A.path); elig = fsz_eligible(A.path); }
    return elig;
}

static bool wanted(void) { return sizes_on() && ((A.zoom != Z_LIST && A.view != VIEW_GRID) || A.sort_key == FS_SORT_SIZE); }

static void join(const char *name, char *out, size_t cap) {
    size_t n = strlen(A.path);
    snprintf(out, cap, "%s%s%s", A.path, n && A.path[n - 1] == '\\' ? "" : "\\", name);
}

/* Folders sort by their size among themselves: copy what is known into the entries before a size sort. */
void sizes_fill(void) {
    if (A.sort_key != FS_SORT_SIZE || !sizes_on()) return;
    char p[FS_PATH_MAX + 300];
    for (int i = 0; i < A.list->count; i++) {
        fs_entry *e = &A.list->items[i];
        if (!(e->flags & FS_DIR) || (e->flags & FS_LINK)) continue;
        uint64_t v = 0;
        join(fs_name(A.list, i), p, sizeof p);
        e->size = fsz_get(p, &v) == FSZ_DONE ? v : 0;
    }
}

int sizes_cell(const fs_entry *e, const char *name, char *buf, size_t cap) {
    if (!(e->flags & FS_DIR) || (e->flags & FS_LINK) || !sizes_on()) return 0;
    char p[FS_PATH_MAX + 300];
    uint64_t v = 0;
    join(name, p, sizeof p);
    if (fsz_get(p, &v) == FSZ_DONE) { fs_format_size(buf, cap, v); return 1; }
    return 2;
}

void sizes_selection(uint64_t *bytes, bool *pending) {
    static struct { uint64_t key; unsigned ver; fs_list *list; int vis_n, n; uint64_t bytes; bool pending; } c;
    uint64_t key = 0; int n = 0;
    *bytes = 0; *pending = false;
    if (!sizes_on()) return;
    for (int i = 0; i < A.vis_n; i++)
        if (A.sel[i] && (app_item(i)->flags & FS_DIR) && !(app_item(i)->flags & FS_LINK)) { n++; key += (uint64_t)(i + 1) * 0x9E3779B97F4A7C15ull; }
    if (!n) return;
    unsigned ver = fsz_version();
    if (c.key == key && c.ver == ver && c.list == A.list && c.vis_n == A.vis_n && c.n == n) { *bytes = c.bytes; *pending = c.pending; return; }
    char p[FS_PATH_MAX + 300];
    for (int i = 0; i < A.vis_n; i++) {
        const fs_entry *e = app_item(i);
        if (!A.sel[i] || !(e->flags & FS_DIR) || (e->flags & FS_LINK)) continue;
        uint64_t v = 0;
        join(app_item_name(i), p, sizeof p);
        if (fsz_get(p, &v) == FSZ_DONE) *bytes += v; else *pending = true;
    }
    c.key = key; c.ver = ver; c.list = A.list; c.vis_n = A.vis_n; c.n = n; c.bytes = *bytes; c.pending = *pending;
}

void sizes_forget(void) { req_list = NULL; }   /* the listing was dropped: a new one at the same address is a new listing */

void sizes_frame(void) {
    if (!wanted()) return;
    if (req_list != A.list) {
        req_list = A.list; req_done = false;
        fsz_view(A.path);
        char p[FS_PATH_MAX + 300];
        for (int i = 0; i < A.list->count; i++) {   /* the listing's order: the folders nearest the top of the view first */
            const fs_entry *e = &A.list->items[i];
            if (!(e->flags & FS_DIR) || (e->flags & FS_LINK)) continue;
            join(fs_name(A.list, i), p, sizeof p);
            fsz_want(p);
        }
    }
    if (!req_done && fsz_open() == 0) {   /* all in: one re-sort, the selection and the scroll kept */
        req_done = true;
        if (A.sort_key == FS_SORT_SIZE) app_resort_keep();
    }
}

bool sizes_settled(void) { return !wanted() || req_list != A.list || fsz_open() == 0 || getenv("DFM_TEST_FSZ_DELAY_MS"); }
int sizes_open(void) { return fsz_open(); }
