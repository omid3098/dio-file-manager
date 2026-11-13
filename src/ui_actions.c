/* Actions on files: context menus, rename and new-folder popovers, clipboard, trash with undo,
   the running-job button and its dialogs. The work itself is in ops, clip, trash and shell. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "ops.h"
#include "clip.h"
#include "trash.h"
#include "shell.h"
#include "starred.h"
#include "views.h"

enum { POP_NONE = 0, POP_RENAME, POP_NEWDIR };

static struct {
    ops_job *job;
    ops_state *st;
    ops_kind kind;
    char title[320], dir[FS_PATH_MAX], first[260];
    int n;
    bool conflict_open, apply_all;
    bool job_pop_open;

    int pop;                       /* POP_* currently shown */
    int want_pop;                  /* POP_* to open at the next anchor */
    int pop_frames;
    char name[300], orig[300], target[FS_PATH_MAX];
    int sel_a, sel_b;              /* byte range selected when the popover opens */

    bool delete_open;
    char delete_title[300];
    char **delete_paths;
    size_t delete_n;

    int undo_toast;
    HANDLE undo_thread;
    volatile LONG undo_state;      /* 0 idle, 1 running, 2 done */
    HRESULT undo_hr;

    char reselect[260];

    bool ctx_item;                 /* the open context menu is the item menu */
} G = { .undo_toast = -1 };

/* ---- small helpers ---- */

static HWND hwnd(void) { return (HWND)A.hwnd; }

static void error_text(DWORD code, const char *path, char *out, size_t cap) {
    wchar_t msg[256] = L"";
    if ((code >> 16) == 0x8007) code &= 0xFFFF;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, msg, 255, NULL);
    while (n && (msg[n - 1] == L'\r' || msg[n - 1] == L'\n' || msg[n - 1] == L' ' || msg[n - 1] == L'.')) msg[--n] = 0;
    char m[512];
    if (!n || !fs_w_to_u8(msg, m, sizeof m)) snprintf(m, sizeof m, "Error %lu", (unsigned long)code);
    const char *leaf = path ? strrchr(path, '\\') : NULL;
    leaf = leaf ? leaf + 1 : path;
    if (leaf && leaf[0]) snprintf(out, cap, "\xE2\x80\x9C%s\xE2\x80\x9D: %s", leaf, m);
    else snprintf(out, cap, "%s", m);
}

static void toast_error(const char *title, DWORD code, const char *path) {
    char d[700];
    error_text(code, path, d, sizeof d);
    cui_toast(A.ui, title, .description = d, .tone = CUI_TONE_DANGER, .duration = 7);
}

static bool files_view(void) { return A.loc != LOC_OTHER && A.loc != LOC_TRASH && A.list && fs_list_state(A.list) == FS_DONE; }
void act_reselect(const char *name) { snprintf(G.reselect, sizeof G.reselect, "%s", name); }
static bool in_folder(void) { return A.loc == LOC_FOLDER && !A.searching && A.list && fs_list_state(A.list) == FS_DONE; }

static int first_selected(void) {
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i]) return i;
    return -1;
}

/* Absolute paths of the selection; free with free_paths. */
static char **selected_paths(size_t *n) {
    char **p = calloc((size_t)A.sel_n + 1, sizeof *p);
    size_t k = 0;
    for (int i = 0; p && i < A.vis_n; i++) if (A.sel[i]) {
        p[k] = malloc(FS_PATH_MAX);
        if (!p[k]) break;
        app_path_of(i, p[k], FS_PATH_MAX);
        k++;
    }
    *n = k;
    return p;
}

static void free_paths(char **p, size_t n) {
    if (!p) return;
    for (size_t i = 0; i < n; i++) free(p[i]);
    free(p);
}

static void name_of(const char *path, char *out, size_t cap) {
    const char *l = strrchr(path, '\\');
    snprintf(out, cap, "%s", l ? l + 1 : path);
}

static bool exists_path(const char *dir, const char *name) {
    char p[FS_PATH_MAX];
    size_t l = strlen(dir);
    snprintf(p, sizeof p, "%s%s%s", dir, (l && dir[l - 1] == '\\') ? "" : "\\", name);
    wchar_t w[FS_PATH_MAX];
    return fs_u8_to_w(p, w, FS_PATH_MAX) && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}

/* ---- jobs ---- */

static bool start_job(ops_kind kind, char **paths, size_t n, const char *dest, const char *name, const char *title) {
    if (G.job) { cui_toast(A.ui, "Another operation is still running", .tone = CUI_TONE_WARNING); return false; }
    G.job = ops_start(kind, (const char *const *)paths, n, dest, name);
    if (!G.job) { toast_error(title, GetLastError(), n ? paths[0] : name); return false; }
    if (!G.st) G.st = calloc(1, sizeof *G.st);
    G.kind = kind;
    G.n = (int)n;
    snprintf(G.title, sizeof G.title, "%s", title);
    snprintf(G.dir, sizeof G.dir, "%s", A.path);
    G.first[0] = 0;
    if (n) name_of(paths[0], G.first, sizeof G.first);
    G.conflict_open = false;
    return true;
}

static DWORD WINAPI undo_proc(LPVOID unused) {
    (void)unused;
    G.undo_hr = trash_undo();
    InterlockedExchange(&G.undo_state, 2);
    return 0;
}

static void start_undo(void) {
    if (InterlockedCompareExchange(&G.undo_state, 1, 0) == 0) G.undo_thread = CreateThread(NULL, 0, undo_proc, NULL, 0, NULL);
}

static void clear_cut_clipboard(void) {
    if (OpenClipboard(hwnd())) { EmptyClipboard(); CloseClipboard(); }
}

static void job_done(void) {
    unsigned errors = G.st->errors;
    DWORD code = G.st->error_code;
    char epath[FS_PATH_MAX];
    snprintf(epath, sizeof epath, "%s", G.st->error_path);
    bool cancelled = G.st->cancelled;
    ops_kind kind = G.kind;
    ops_free(G.job);
    G.job = NULL;
    G.conflict_open = false; G.job_pop_open = false;
    if (errors) {
        char t[400];
        snprintf(t, sizeof t, errors > 1 ? "%s: %u problems" : "%s failed", G.title, errors);
        toast_error(t, code, epath);
    }
    if (kind == OPS_MOVE && !errors && !cancelled) clear_cut_clipboard();
    if (kind == OPS_TRASH && !errors && !cancelled) {
        char t[400];
        if (G.n == 1) snprintf(t, sizeof t, "\xE2\x80\x9C%s\xE2\x80\x9D moved to Trash", G.first);
        else snprintf(t, sizeof t, "%d items moved to Trash", G.n);
        G.undo_toast = cui_toast(A.ui, t, .action = "Undo", .show_timer = true, .duration = 8);
    }
    if (strcmp(G.dir, A.path) == 0 && A.loc != LOC_OTHER) app_reload();
    A.trash_check_t = -100;
}

/* ---- commands ---- */

static void do_copy(bool cut) {
    if (!files_view() || A.sel_n == 0) return;
    size_t n;
    char **p = selected_paths(&n);
    DWORD e = p ? clip_write(hwnd(), (const char *const *)p, n, cut) : ERROR_NOT_ENOUGH_MEMORY;
    if (e) toast_error(cut ? "Could not cut" : "Could not copy", e, n ? p[0] : NULL);
    free_paths(p, n);
}

static void do_paste(void) {
    if (!in_folder()) return;
    clip_files f = { 0 };
    DWORD e = clip_read(hwnd(), &f);
    if (e == ERROR_SUCCESS && f.count) {
        start_job(f.cut ? OPS_MOVE : OPS_COPY, f.paths, f.count, A.path, NULL, f.cut ? "Moving files" : "Copying files");
    } else if (e && e != ERROR_NO_MORE_ITEMS && e != ERROR_CLIPBOARD_NOT_OPEN && e != ERROR_NOT_FOUND && e != ERROR_FILE_NOT_FOUND) {
        toast_error("Could not paste", e, NULL);
    }
    clip_free(&f);
}

static void do_trash(void) {
    if (A.loc == LOC_TRASH) { views_ask_delete(); return; }
    if (!files_view() || A.sel_n == 0 || A.loc == LOC_RECENT) return;
    size_t n;
    char **p = selected_paths(&n);
    if (p && n) start_job(OPS_TRASH, p, n, NULL, NULL, "Moving to Trash");
    free_paths(p, n);
}

static void ask_delete_forever(void) {
    if (A.loc == LOC_TRASH) { views_ask_delete(); return; }
    if (!files_view() || A.sel_n == 0 || A.loc == LOC_RECENT) return;
    free_paths(G.delete_paths, G.delete_n);
    G.delete_paths = selected_paths(&G.delete_n);
    if (G.delete_n == 1) {
        char nm[260];
        name_of(G.delete_paths[0], nm, sizeof nm);
        snprintf(G.delete_title, sizeof G.delete_title, "Delete \xE2\x80\x9C%s\xE2\x80\x9D Permanently?", nm);
    } else snprintf(G.delete_title, sizeof G.delete_title, "Delete %d Items Permanently?", (int)G.delete_n);
    G.delete_open = true;
}

static void open_selected(void) {
    if (A.sel_n == 1) { app_open_vis(first_selected()); return; }
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i] && !(app_item(i)->flags & FS_DIR)) app_open_vis(i);
}

static void report(HRESULT hr, const char *title, const char *path) {
    if (FAILED(hr)) toast_error(title, (DWORD)hr, path);
}

static void open_with(void) {
    int i = first_selected();
    char p[FS_PATH_MAX];
    if (i < 0) return;
    app_path_of(i, p, sizeof p);
    report(shell_open_with(hwnd(), p), "Could not open", p);
}

static void copy_path(void) {
    int i = first_selected();
    char p[FS_PATH_MAX];
    if (i < 0) return;
    app_path_of(i, p, sizeof p);
    report(shell_copy_path(hwnd(), p), "Could not copy the path", p);
}

static void properties(bool item) {
    int i = item ? first_selected() : -1;
    char p[FS_PATH_MAX];
    if (item && i < 0) return;
    if (item) app_path_of(i, p, sizeof p); else snprintf(p, sizeof p, "%s", A.path);
    report(shell_properties(hwnd(), p), "Could not show properties", p);
}

static void begin_popover(int pop, const char *name, const char *target, int sel_a, int sel_b) {
    snprintf(G.name, sizeof G.name, "%s", name);
    snprintf(G.orig, sizeof G.orig, "%s", pop == POP_RENAME ? name : "");
    snprintf(G.target, sizeof G.target, "%s", target);
    G.sel_a = sel_a; G.sel_b = sel_b;
    G.want_pop = pop;
}

void act_rename(void) {
    if (!in_folder() || A.sel_n != 1) return;
    int i = first_selected();
    const char *nm = app_item_name(i);
    char p[FS_PATH_MAX];
    app_path_of(i, p, sizeof p);
    int len = (int)strlen(nm), stem = len;
    const char *dot = strrchr(nm, '.');
    if (!(app_item(i)->flags & FS_DIR) && dot && dot != nm) stem = (int)(dot - nm);
    begin_popover(POP_RENAME, nm, p, 0, stem);
}

void act_new_folder(void) {
    if (!in_folder()) return;
    begin_popover(POP_NEWDIR, "Untitled Folder", A.path, 0, 15);
}

void act_open_terminal(void) {
    if (A.loc == LOC_FOLDER) report(shell_terminal(A.path), "Could not open the terminal", A.path);
}

/* ---- keys and menus ---- */

bool act_modal_open(void) {
    return G.pop != POP_NONE || G.want_pop != POP_NONE || G.delete_open || G.conflict_open || views_modal_open();
}

void act_keys(void) {
    cui_ctx *ui = A.ui;
    if (act_modal_open()) return;
    if (cui_key_pressed(ui, CUI_KEY_F2)) act_rename();
    if (cui_shortcut(ui, CUI_MOD_CTRL | CUI_MOD_SHIFT, CUI_KEY_N)) act_new_folder();
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_C)) do_copy(false);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_X)) do_copy(true);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_V)) do_paste();
    if (cui_shortcut(ui, 0, CUI_KEY_DELETE)) do_trash();
    if (cui_shortcut(ui, CUI_MOD_SHIFT, CUI_KEY_DELETE)) ask_delete_forever();
    if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_ENTER)) properties(A.sel_n > 0);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_Z) && G.undo_toast >= 0) { cui_toast_dismiss(ui, G.undo_toast); G.undo_toast = -1; start_undo(); }
}

/* Call at the start of each frame: a new right click has no item until a view claims it. */
void act_pre_frame(void) {
    if (cui_mouse_clicked(A.ui, CUI_MOUSE_RIGHT)) A.ctx_item = false;
}

static void menu_items(void) {
    cui_ctx *ui = A.ui;
    if (A.loc == LOC_TRASH) { views_trash_menu(); return; }
    bool folder_ok = in_folder();
    bool virt = A.loc != LOC_FOLDER || A.searching;   /* Recent, Starred or search results: items from many folders */
    if (G.ctx_item && A.sel_n > 0) {
        bool one = A.sel_n == 1;
        if (cui_menu_item(ui, "Open", .icon = CUI_ICON_FOLDER_OPEN, .shortcut = "Enter")) open_selected();
        if (cui_menu_item(ui, "Open With\xE2\x80\xA6", .icon = CUI_ICON_EXTERNAL_LINK, .disabled = !one)) open_with();
        if (virt && cui_menu_item(ui, "Open Item Location", .icon = CUI_ICON_MAP_PIN, .disabled = !one)) views_open_location();
        cui_menu_separator(ui);
        if (!virt && cui_menu_item(ui, "Cut", .icon = CUI_ICON_SCISSORS, .shortcut = "Ctrl X")) do_copy(true);
        if (cui_menu_item(ui, "Copy", .icon = CUI_ICON_COPY, .shortcut = "Ctrl C")) do_copy(false);
        cui_menu_separator(ui);
        if (!virt && cui_menu_item(ui, "Rename\xE2\x80\xA6", .icon = CUI_ICON_PENCIL, .shortcut = "F2", .disabled = !one)) act_rename();
        if (A.loc != LOC_RECENT && cui_menu_item(ui, "Move to Trash", .icon = CUI_ICON_TRASH_2, .shortcut = "Delete")) do_trash();
        cui_menu_separator(ui);
        if (cui_menu_item(ui, app_selection_all_starred() ? "Unstar" : "Star", .icon = CUI_ICON_STAR)) app_star_toggle_selection();
        if (cui_menu_item(ui, "Copy Path", .icon = CUI_ICON_LINK, .disabled = !one)) copy_path();
        if (cui_menu_item(ui, "Properties", .icon = CUI_ICON_INFO, .shortcut = "Alt Enter", .disabled = !one)) properties(true);
    } else if (virt) {
        if (cui_menu_item(ui, "Select All", .icon = CUI_ICON_CHECK_CHECK, .shortcut = "Ctrl A", .disabled = A.vis_n == 0)) app_select_all();
    } else {
        bool can_paste = folder_ok && IsClipboardFormatAvailable(CF_HDROP);
        if (cui_menu_item(ui, "New Folder", .icon = CUI_ICON_PLUS, .shortcut = "Ctrl Shift N", .disabled = !folder_ok)) act_new_folder();
        if (cui_menu_item(ui, "Paste", .icon = CUI_ICON_CLIPBOARD, .shortcut = "Ctrl V", .disabled = !can_paste)) do_paste();
        cui_menu_separator(ui);
        if (cui_menu_item(ui, "Select All", .icon = CUI_ICON_CHECK_CHECK, .shortcut = "Ctrl A", .disabled = A.vis_n == 0)) app_select_all();
        cui_menu_separator(ui);
        if (cui_menu_item(ui, "Open in Terminal", .icon = CUI_ICON_TERMINAL)) act_open_terminal();
        if (cui_menu_item(ui, "Properties", .icon = CUI_ICON_INFO)) properties(false);
    }
}

static void paint_none(cui_ctx *ui, cui_rect r, void *user) { (void)ui; (void)r; (void)user; }

/* Any right click in the content pane opens the menu at the pointer: on an item the item menu, anywhere else
 * the background menu (also in the empty space below the grid rows). Its width is its own (ui_ctxmenu.c). */
static void context_menu(void) {
    cui_ctx *ui = A.ui;
    cui_vec2 m = cui_mouse_pos(ui), vp = cui_viewport_size(ui);
    float x0 = app_is_narrow() ? 0.0f : 240.0f, y0 = 46.0f;
    bool in_header = A.view == VIEW_LIST && A.vis_n > 0 && m.y < y0 + 36.0f;
    bool open_here = cui_mouse_clicked(ui, CUI_MOUSE_RIGHT) && A.loc != LOC_OTHER && !G.delete_open && !G.conflict_open &&
                     !A.sidebar_open && m.x >= x0 && m.x < vp.x && m.y >= y0 && m.y < vp.y && !in_header;
    if (open_here) G.ctx_item = A.ctx_item;
    if (ctxmenu_begin(ui, "ctxmenu", open_here)) {
        menu_items();
        ctxmenu_end(ui);
    }
}

/* ---- popovers and dialogs ---- */

static const char *name_problem(void) {
    const char *e = ops_name_error(G.name);
    if (e) return e;
    if (G.pop == POP_RENAME && !strcmp(G.name, G.orig)) return NULL;
    char dir[FS_PATH_MAX];
    if (G.pop == POP_RENAME) {
        if (!fs_parent(G.target, dir, sizeof dir)) return NULL;
    } else snprintf(dir, sizeof dir, "%s", G.target);
    if (exists_path(dir, G.name)) return "A file with that name already exists.";
    return NULL;
}

static void submit_popover(void) {
    if (G.pop == POP_RENAME) {
        if (strcmp(G.name, G.orig) != 0) {
            char *p = G.target;
            if (start_job(OPS_RENAME, &p, 1, NULL, G.name, "Rename")) snprintf(G.reselect, sizeof G.reselect, "%s", G.name);
        }
    } else {
        if (start_job(OPS_NEW_FOLDER, NULL, 0, G.target, G.name, "New Folder")) snprintf(G.reselect, sizeof G.reselect, "%s", G.name);
    }
    cui_popover_close(A.ui, "actpop");
}

static void name_popover(void) {
    cui_ctx *ui = A.ui;
    if (G.want_pop) {
        G.pop = G.want_pop; G.want_pop = POP_NONE; G.pop_frames = 0;
        cui_popover_open(ui, "actpop");
    }
    if (G.pop == POP_NONE) return;
    if (cui_popover_begin(ui, "actpop", &(cui_popover_opts){ .placement = CUI_PLACE_BOTTOM, .width = 320, .pad = 14, .gap = 6 })) {
        bool rename = G.pop == POP_RENAME;
        cui_column(ui, .width = CUI_FILL, .gap = 10) {
            cui_text(ui, rename ? "Rename" : "New Folder", .font = CUI_FONT_MEDIUM, .size = 15);
            if (G.pop_frames == 1) {   /* the field has its first frame behind it: select the part to replace */
                int *s = cui_state(ui, cui_make_id(ui, "popname"), 2 * sizeof(int));
                s[0] = G.sel_b; s[1] = G.sel_a;
            }
            const char *err = name_problem();
            int f = cui_input(ui, "popname", G.name, sizeof G.name, .width = CUI_FILL, .autofocus = true, .error = err);
            bool unchanged = rename && !strcmp(G.name, G.orig);
            bool ok = !err && !unchanged;
            cui_row(ui, .width = CUI_FILL, .gap = 8, .justify = CUI_JUSTIFY_END) {
                if (cui_button(ui, "Cancel", .size = CUI_SIZE_SM)) cui_popover_close(ui, "actpop");
                if (cui_button(ui, rename ? "Rename" : "Create", .size = CUI_SIZE_SM, .variant = CUI_BUTTON_PRIMARY, .disabled = !ok)) submit_popover();
            }
            if ((f & CUI_SUBMITTED) && ok) submit_popover();
            else if ((f & CUI_SUBMITTED) && unchanged) cui_popover_close(ui, "actpop");
        }
        cui_popover_end(ui);
        G.pop_frames++;
        cui_request_wakeup(ui, 0.0f);
    }
    if (!cui_popover_is_open(ui, "actpop") && G.pop_frames > 0) G.pop = POP_NONE;
}

static void conflict_dialog(void) {
    cui_ctx *ui = A.ui;
    if (!G.conflict_open || !G.job) return;
    char nm[260], dir[FS_PATH_MAX], t[400], d[700];
    name_of(G.st->conflict_target, nm, sizeof nm);
    if (!fs_parent(G.st->conflict_target, dir, sizeof dir)) dir[0] = 0;
    const char *dn = strrchr(dir, '\\');
    snprintf(t, sizeof t, "Replace \xE2\x80\x9C%s\xE2\x80\x9D?", nm);
    snprintf(d, sizeof d, "Another file with the same name already exists in \xE2\x80\x9C%s\xE2\x80\x9D.", dn && dn[1] ? dn + 1 : dir);
    bool open = true;
    if (cui_dialog_begin(ui, t, &open, &(cui_dialog_opts){ .description = d, .width = 440, .persistent = true, .no_close_button = true })) {
        cui_checkbox(ui, "Apply this action to all files", &G.apply_all);
        cui_dialog_footer_begin(ui);
        ops_answer ans = (ops_answer)-1;
        if (cui_button(ui, "Skip")) ans = OPS_SKIP;
        if (cui_button(ui, "Keep Both")) ans = OPS_KEEP_BOTH;
        if (cui_button(ui, "Replace", .variant = CUI_BUTTON_DANGER)) ans = OPS_REPLACE;
        cui_dialog_footer_end(ui);
        cui_dialog_end(ui);
        if ((int)ans >= 0) { ops_resolve(G.job, ans, G.apply_all); G.conflict_open = false; }
    }
}

static void delete_dialog(void) {
    cui_ctx *ui = A.ui;
    if (!G.delete_open) return;
    bool open = true;
    if (cui_dialog_begin(ui, G.delete_title, &open, &(cui_dialog_opts){ .description = "If you delete an item, it will be permanently lost.", .width = 440 })) {
        cui_dialog_footer_begin(ui);
        if (cui_button(ui, "Cancel")) open = false;
        if (cui_button(ui, "Delete", .variant = CUI_BUTTON_DANGER)) {
            start_job(OPS_DELETE, G.delete_paths, G.delete_n, NULL, NULL, "Deleting");
            open = false;
        }
        cui_dialog_footer_end(ui);
        cui_dialog_end(ui);
    }
    if (!open) { G.delete_open = false; free_paths(G.delete_paths, G.delete_n); G.delete_paths = NULL; G.delete_n = 0; }
}

/* The small progress ring in the header; a popover shows the job and cancels it. */
void act_job_button(void) {
    cui_ctx *ui = A.ui;
    if (!G.job || !G.st) return;
    float v = G.st->bytes_total ? (float)((double)G.st->bytes_done / (double)G.st->bytes_total)
                                : (G.st->files_total ? (float)((double)G.st->files_done / (double)G.st->files_total) : 0.0f);
    if (v < 0.04f) v = 0.04f;
    if (v > 1) v = 1;
    if (cui_icon_button(ui, CUI_ICON_LOADER_CIRCLE, .tooltip = G.title, .selected = G.job_pop_open)) G.job_pop_open = !G.job_pop_open;
    cui_progress_ring(ui, "", v, 18);
    if (G.job_pop_open) cui_popover_open(ui, "jobpop");
    if (cui_popover_begin(ui, "jobpop", &(cui_popover_opts){ .placement = CUI_PLACE_BOTTOM_END, .width = 300, .pad = 14 })) {
        cui_column(ui, .width = CUI_FILL, .gap = 8) {
            cui_text(ui, G.title, .font = CUI_FONT_MEDIUM);
            if (G.st->current_name[0]) cui_text(ui, G.st->current_name, .nowrap = true, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_progress(ui, v, .width = CUI_FILL);
            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_END) {
                if (cui_button(ui, "Cancel", .size = CUI_SIZE_SM)) ops_cancel(G.job);
            }
        }
        cui_popover_end(ui);
    }
    if (!cui_popover_is_open(ui, "jobpop")) G.job_pop_open = false;
}

/* Runs every frame, inside the content stack: it owns the anchor of the popovers. */
void act_frame(void) {
    cui_ctx *ui = A.ui;
    if (G.job) {
        ops_poll(G.job, G.st);
        if (G.st->waiting_conflict && !G.conflict_open) { G.conflict_open = true; G.apply_all = false; }
        else if (!G.st->waiting_conflict) G.conflict_open = false;
        if (G.st->finished) job_done(); else cui_request_wakeup(ui, 0.05f);
    }
    if (G.undo_toast >= 0 && cui_toast_action_clicked(ui, G.undo_toast)) {
        G.undo_toast = -1;
        start_undo();
    }
    if (G.undo_state == 1) cui_request_wakeup(ui, 0.05f);
    if (G.undo_state == 2) {
        WaitForSingleObject(G.undo_thread, INFINITE);
        CloseHandle(G.undo_thread);
        G.undo_thread = NULL;
        InterlockedExchange(&G.undo_state, 0);
        if (FAILED(G.undo_hr)) toast_error("Could not undo", (DWORD)G.undo_hr, NULL);
        if (A.loc != LOC_OTHER) app_reload();
        A.trash_check_t = -100;
    }
    /* the anchor of the popovers: a hairline just under the row of the selected item */
    float below = 0;
    if (A.cursor >= 0) {
        int row = A.view == VIEW_GRID && A.cols > 0 ? A.cursor / A.cols : A.cursor + 1;
        float rh = A.view == VIEW_GRID ? A.cell_h : 36.0f;
        below = (float)(row - A.first_row + (A.view == VIEW_GRID ? 1 : 0)) * rh;
        if (below < 0) below = 0;
        if (below > A.content_h - 150) below = A.content_h - 150;
    }
    /* under the item itself: its cell in the grid, the name column in the list */
    float left = 8, aw = 360;
    if (A.view == VIEW_GRID && A.cols > 0 && A.first_rect.w > 64) {
        aw = (A.first_rect.w - 32) / (float)A.cols;
        left = 16 + (float)(A.cursor >= 0 ? A.cursor % A.cols : 0) * aw;
    }
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad_top = below, .pad_left = left) {
        cui_canvas(ui, "popanchor", CUI_PX(aw), CUI_PX(1), paint_none, NULL, 0);
    }
    name_popover();
    context_menu();
    conflict_dialog();
    delete_dialog();
}

/* A finished listing may hold the item a rename or new folder just made. */
void act_list_ready(void) {
    if (!G.reselect[0]) return;
    for (int i = 0; i < A.vis_n; i++)
        if (!strcmp(app_item_name(i), G.reselect)) { app_select_only(i); A.cursor = i; break; }
    G.reselect[0] = 0;
}

int act_state(void) {
    return (G.pop != POP_NONE ? 1 : 0) | (G.job ? 2 : 0) | (G.delete_open ? 4 : 0) | (G.conflict_open ? 8 : 0) | (G.undo_state ? 16 : 0) | (views_state() << 5);
}
