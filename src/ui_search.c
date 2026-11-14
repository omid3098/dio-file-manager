/* Search below the current folder, as in GNOME Files. The results stream in from search.h into a list of
 * their own that replaces the folder's list while the search is open; the folder's list is kept and comes back
 * when the search ends. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "search.h"

#define MAX_RESULTS 10000      /* a query such as "a" would otherwise list a whole drive */
#define PER_FRAME 150          /* results made into entries in one frame (each needs a stat call) */

static search_job *job;
static search_state st;
static bool running;           /* the worker has not finished */
static fs_list *saved;         /* the folder's list while a search is open */
static bool saved_seen;
static double due;             /* when the typed query starts a search; 0 for none */
static char **pend;
static size_t pend_n, pend_i, pend_cap;

static void free_pending(void) {
    for (size_t i = pend_i; i < pend_n; i++) free(pend[i]);
    free(pend);
    pend = NULL; pend_n = pend_i = pend_cap = 0;
}

static void stop_job(void) {
    if (job) { search_free(job); job = NULL; }
    free_pending();
    running = false;
}

bool fsearch_running(void) { return A.searching && (running || pend_i < pend_n); }

void fsearch_refresh(void) {
    if (!A.searching) return;
    fs_list_release(saved);
    saved = fs_list_start(A.path, A.sort_key, A.sort_desc);
    saved_seen = false;
    sizes_forget();   /* the folder's listing is made again: its children are measured again when search closes */
    int kept = 0;
    for (int i = 0; i < A.list->count; i++) {
        wchar_t w[FS_PATH_MAX];
        const char *p = fs_extra(A.list, A.list->items[i].path_off);
        if (fs_u8_to_w(p, w, FS_PATH_MAX) && GetFileAttributesW(w) == INVALID_FILE_ATTRIBUTES) {
            DWORD e = GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) continue;
        }
        A.list->items[kept++] = A.list->items[i];
    }
    A.list->count = kept;
    app_rebuild_view();
}

/* Ends the search and puts the folder's list back. The caller rebuilds the view. */
void fsearch_end(void) {
    due = 0;
    stop_job();
    if (!A.searching) return;
    fs_list_release(A.list);
    A.list = saved;
    A.list_seen = saved_seen;
    saved = NULL;
    A.searching = false;
    sizes_forget();   /* back in the folder: its children are measured again */
}

static void begin(void) {
    stop_job();
    fs_list *l = calloc(1, sizeof *l);
    if (!l) return;
    l->owner = 1; l->state = FS_DONE; l->mixed = true;
    l->sort_key = A.sort_key; l->sort_desc = A.sort_desc;
    snprintf(l->path, sizeof l->path, "%s", A.path);
    if (A.searching) fs_list_release(A.list);
    else { saved = A.list; saved_seen = A.list_seen; }
    A.list = l;
    A.list_seen = true;
    A.searching = true;
    A.vis_n = 0;
    if (A.sel && A.vis_cap) memset(A.sel, 0, (size_t)A.vis_cap * sizeof *A.sel);
    A.sel_n = 0; A.sel_bytes = 0; A.cursor = A.anchor = -1;
    A.reset_scroll = true;
    job = search_start(A.path, A.search, A.show_hidden);
    running = job != NULL;
}

/* The query changed (typing, paste, clear). Folders search after a short pause; the other places filter by name. */
void fsearch_changed(void) {
    if (A.loc != LOC_FOLDER) { app_rebuild_view(); return; }
    if (!A.search[0]) {
        bool was = A.searching;
        fsearch_end();
        if (was) app_rebuild_view();
        return;
    }
    due = cui_time(A.ui) + 0.25;
}

static void add_result(char *path) {
    wchar_t w[FS_PATH_MAX];
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!fs_u8_to_w(path, w, FS_PATH_MAX) || !GetFileAttributesExW(w, GetFileExInfoStandard, &d)) return;
    const char *leaf = strrchr(path, '\\');
    leaf = leaf && leaf[1] ? leaf + 1 : path;
    uint32_t flags = 0;
    if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) flags |= FS_DIR;
    if (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) flags |= FS_HIDDEN;
    uint64_t size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    uint64_t mtime = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    if (!fs_list_add(A.list, leaf, flags, (flags & FS_DIR) ? 0 : size, mtime, path, NULL)) return;
    if (A.vis_n >= A.vis_cap) {
        int nc = A.vis_cap * 2 + 256;
        int *nv = realloc(A.vis, (size_t)nc * sizeof *nv);
        if (nv) A.vis = nv;
        bool *ns = realloc(A.sel, (size_t)nc * sizeof *ns);
        if (ns) { memset(ns + A.vis_cap, 0, (size_t)(nc - A.vis_cap) * sizeof *ns); A.sel = ns; }
        if (!nv || !ns) { A.list->count--; return; }
        A.vis_cap = nc;
    }
    A.vis[A.vis_n] = A.list->count - 1;
    A.sel[A.vis_n] = false;
    A.vis_n++;
}

/* Every frame: start a search whose pause is over, and turn what the worker found into entries. */
void fsearch_frame(void) {
    cui_ctx *ui = A.ui;
    if (due) {
        if (cui_time(ui) >= due) { due = 0; if (A.search[0] && A.loc == LOC_FOLDER && A.list) begin(); }
        else cui_request_wakeup(ui, 0.05f);
    }
    if (!A.searching) return;
    if (job) {
        size_t n = 0;
        char **got = search_drain(job, &n, &st);
        if (n) {
            if (pend_n + n > pend_cap) {
                size_t nc = pend_cap ? pend_cap : 256;
                while (nc < pend_n + n) nc *= 2;
                char **np = realloc(pend, nc * sizeof *np);
                if (np) { pend = np; pend_cap = nc; }
            }
            for (size_t i = 0; i < n; i++) {
                if (pend && pend_n < pend_cap) pend[pend_n++] = got[i]; else free(got[i]);
            }
        }
        free(got);
        if (st.finished) running = false;
    }
    int made = 0;
    while (pend_i < pend_n && made < PER_FRAME) {
        char *p = pend[pend_i++];
        add_result(p);
        pend[pend_i - 1] = NULL;
        free(p);
        made++;
    }
    if (pend_i == pend_n) { pend_n = pend_i = 0; }
    if (A.list->count >= MAX_RESULTS && job) stop_job();
    if (running || pend_i < pend_n) cui_request_wakeup(ui, 0.03f);
}

/* The empty view of a search: a spinner while it runs, "No Results Found" when it has ended. */
void fsearch_empty_view(void) {
    cui_ctx *ui = A.ui;
    if (fsearch_running()) {
        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER, .gap = 14) {
            cui_spinner(ui, 32, .style = CUI_SPINNER_RADAR, .color = CUI_COLOR_ACCENT);
            cui_text(ui, "Searching\xE2\x80\xA6", .color = CUI_COLOR_TEXT_SECONDARY, .align = CUI_TEXT_CENTER, .width = CUI_FILL);
        }
        return;
    }
    ui_state_view(CUI_ICON_SEARCH, "No Results Found", "Try a different search.", NULL);
}

/* The Location column of the results, as in GNOME Files: the folder of the result below the searched folder
 * ("sub\deeper"), or the searched folder's own name when the result sits directly in it. */
void search_location_text(int vis, char *out, size_t cap) {
    char p[FS_PATH_MAX], d[FS_PATH_MAX];
    app_path_of(vis, p, sizeof p);
    if (!fs_parent(p, d, sizeof d)) { snprintf(out, cap, "%s", p); return; }
    size_t rl = strlen(A.path);
    while (rl > 0 && A.path[rl - 1] == '\\') rl--;
    if (rl && _strnicmp(d, A.path, rl) == 0 && d[rl] == '\\') snprintf(out, cap, "%s", d + rl + 1);
    else if (rl && _strnicmp(d, A.path, rl) == 0 && d[rl] == 0) {
        const char *leaf = strrchr(A.path, '\\');
        snprintf(out, cap, "%s", leaf && leaf[1] ? leaf + 1 : A.path);
    } else snprintf(out, cap, "%s", d);
}
