/* Dio File Manager: window, navigation, selection and the command line for screenshots. */
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <cui/cui_render.h>
#include "ui.h"
#include "shell.h"
#include "starred.h"
#include "recent.h"
#include "ops.h"

/* dwmapi.h imports visual-style macros that collide with our LOC_* enum. */
__declspec(dllimport) HRESULT WINAPI DwmSetWindowAttribute(HWND, DWORD, LPCVOID, DWORD);

app_t A;
static cui_app *g_app;
static WNDPROC test_window_proc;

/* An inactive desktop has no physical pointer. Its automatic leave messages
 * would overwrite the driver's real mouse messages before the frame runs. */
static bool test_mode;
static wchar_t typed[16];       /* letters typed while no field has the keyboard; they open the search */
static int typed_n;
static float zoom_wheel;

float app_zoom_wheel(void) { float dy = zoom_wheel; zoom_wheel = 0; return dy; }

static LRESULT CALLBACK test_input_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSELEAVE && test_mode) return 0;
    if (m == WM_SETTINGCHANGE && A.ui && A.theme_pref == 0) {
        app_set_theme(0);
        cui_request_redraw(A.ui);
    }
    /* Preserve Ctrl at event time, even if it is released before the next frame. */
    if (m == WM_MOUSEWHEEL && (LOWORD(w) & MK_CONTROL) && A.ui) {
        zoom_wheel += (float)(short)HIWORD(w) / WHEEL_DELTA;
        cui_request_redraw(A.ui);
        return 0;
    }
    if (m == WM_CHAR && w > 0x20 && w != 0x7F && typed_n < 15 &&
        !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) typed[typed_n++] = (wchar_t)w;
    if (m == WM_KEYDOWN || m == WM_KEYUP) {
        if (w == VK_ADD) w = VK_OEM_PLUS;
        else if (w == VK_SUBTRACT) w = VK_OEM_MINUS;
    }
    return CallWindowProcW(test_window_proc, h, m, w, l);
}

/* ---- small helpers ---- */

bool app_is_narrow(void) { return cui_viewport_size(A.ui).x < 640.0f; }

const fs_entry *app_item(int i) { return &A.list->items[A.vis[i]]; }
const char *app_item_name(int i) { return fs_name(A.list, A.vis[i]); }

void app_path_of(int i, char *out, size_t cap) {
    if (A.loc != LOC_FOLDER || A.searching) { snprintf(out, cap, "%s", fs_extra(A.list, app_item(i)->path_off)); return; }
    size_t bl = strlen(A.path);
    snprintf(out, cap, "%s%s%s", A.path, (bl && A.path[bl - 1] == '\\') ? "" : "\\", app_item_name(i));
}

/* CUI 1.4.0 can only follow the system theme from the start, so the three choices are applied here. */
static bool system_is_dark(void) {
    DWORD v = 1, n = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &n) != ERROR_SUCCESS) return false;
    return v == 0;
}

void app_set_theme(int pref) {
    A.theme_pref = pref;
    bool dark = pref == 0 ? system_is_dark() : pref == 2;
    if ((cui_get_theme_mode(A.ui) == CUI_THEME_DARK) != dark) cui_set_theme(A.ui, dark ? CUI_THEME_DARK : CUI_THEME_LIGHT);
    BOOL caption_dark = dark;
    if (FAILED(DwmSetWindowAttribute((HWND)A.hwnd, 20, &caption_dark, sizeof caption_dark)))
        DwmSetWindowAttribute((HWND)A.hwnd, 19, &caption_dark, sizeof caption_dark);
}

void app_copy_text(const char *utf8) {
    wchar_t *w = malloc((strlen(utf8) + 1) * sizeof *w);
    if (!w || !fs_u8_to_w(utf8, w, (int)strlen(utf8) + 1)) { free(w); return; }
    size_t bytes = (wcslen(w) + 1) * sizeof *w;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h && OpenClipboard(NULL)) {
        memcpy(GlobalLock(h), w, bytes);
        GlobalUnlock(h);
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
        CloseClipboard();
    } else if (h) GlobalFree(h);
    free(w);
}

void app_set_search_open(bool open) {
    if (open) A.search_focus = true;
    A.search_open = open;
    if (!open) {
        bool was = A.searching || A.search[0];
        fsearch_end();
        A.search[0] = 0;
        if (was) app_rebuild_view();
    }
}

/* ---- selection ---- */

void app_count_selection(void) {
    A.sel_n = 0; A.sel_bytes = 0;
    for (int i = 0; i < A.vis_n; i++)
        if (A.sel[i]) { A.sel_n++; if (!(app_item(i)->flags & FS_DIR)) A.sel_bytes += app_item(i)->size; }
}

void app_clear_selection(void) {
    if (A.sel && A.vis_n) memset(A.sel, 0, (size_t)A.vis_n * sizeof *A.sel);
    A.sel_n = 0; A.sel_bytes = 0; A.cursor = -1; A.anchor = -1;
}

void app_select_only(int idx) {
    if (idx < 0 || idx >= A.vis_n) return;
    memset(A.sel, 0, (size_t)A.vis_n * sizeof *A.sel);
    A.sel[idx] = true;
    A.cursor = idx; A.anchor = idx;
}

void app_select_range(int from, int to) {
    if (from > to) { int t = from; from = to; to = t; }
    memset(A.sel, 0, (size_t)A.vis_n * sizeof *A.sel);
    for (int i = from; i <= to && i < A.vis_n; i++) if (i >= 0) A.sel[i] = true;
}

void app_select_all(void) {
    if (A.vis_n) memset(A.sel, 1, (size_t)A.vis_n * sizeof *A.sel);
    A.cursor = A.vis_n ? 0 : -1;
}

/* ---- the filtered view of a listing ---- */

static bool contains_nocase(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    if (!nl) return true;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i]) {
            unsigned char a = (unsigned char)hay[i], b = (unsigned char)needle[i];
            if (a < 0x80) a = (unsigned char)tolower(a);
            if (b < 0x80) b = (unsigned char)tolower(b);
            if (a != b) break;
            i++;
        }
        if (i == nl) return true;
    }
    return false;
}

void app_rebuild_view(void) {
    ui_band_cancel();
    A.vis_n = 0;
    if (A.list && fs_list_state(A.list) == FS_DONE) {
        if (A.list->count > A.vis_cap) {
            int nc = A.list->count + 64;
            A.vis = realloc(A.vis, (size_t)nc * sizeof *A.vis);
            A.sel = realloc(A.sel, (size_t)nc * sizeof *A.sel);
            A.vis_cap = nc;
        }
        for (int i = 0; i < A.list->count; i++) {
            const fs_entry *e = &A.list->items[i];
            if ((e->flags & FS_HIDDEN) && !A.show_hidden) continue;
            if (A.search[0] && A.loc != LOC_FOLDER && !contains_nocase(fs_name(A.list, i), A.search)) continue;
            A.vis[A.vis_n++] = i;
        }
    }
    if (A.sel) memset(A.sel, 0, (size_t)(A.vis_cap) * sizeof *A.sel);
    A.sel_n = 0; A.sel_bytes = 0; A.cursor = -1; A.anchor = -1;
    snprintf(A.search_applied, sizeof A.search_applied, "%s", A.search);
    A.reset_scroll = true;
}

void app_resort(void) {
    if (A.list && fs_list_state(A.list) == FS_DONE) {
        fs_list_sort(A.list, A.sort_key, A.sort_desc);
        app_rebuild_view();
    }
}

static void poll_list(void) {
    if (!A.list || A.list_seen || fs_list_state(A.list) == FS_LOADING) return;
    A.list_seen = true;
    if (fs_list_state(A.list) == FS_DONE &&
        (A.list->sort_key != A.sort_key || A.list->sort_desc != A.sort_desc))
        fs_list_sort(A.list, A.sort_key, A.sort_desc);
    app_rebuild_view();
    act_list_ready();
}

/* ---- navigation ---- */

static void quiet_cleanup(void);
static void load_entry(const char *entry) {
    ui_band_cancel();
    quiet_cleanup();
    fsearch_end();
    if (A.list) { fs_list_release(A.list); A.list = NULL; }
    A.last_click_idx = -1;
    A.list_seen = false; A.vis_n = 0; A.sel_n = 0; A.cursor = A.anchor = -1; A.reset_scroll = true;
    A.search[0] = 0; A.search_open = false; A.sidebar_open = false;
    if (entry[0] == '@') {
        A.loc = entry[1] - '0';
        A.path[0] = 0;
    } else {
        A.loc = LOC_FOLDER;
    }
    bool by_date = A.loc == LOC_RECENT || A.loc == LOC_TRASH;
    if (by_date && !A.sort_swapped) {
        A.saved_key = A.sort_key; A.saved_desc = A.sort_desc; A.sort_swapped = true;
        A.sort_key = FS_SORT_MODIFIED; A.sort_desc = true;
    } else if (!by_date && A.sort_swapped) {
        A.sort_key = A.saved_key; A.sort_desc = A.saved_desc; A.sort_swapped = false;
    }
    if (A.loc == LOC_RECENT) A.list = fs_list_start_virtual(FS_V_RECENT, A.sort_key, A.sort_desc);
    else if (A.loc == LOC_STARRED) A.list = fs_list_start_virtual(FS_V_STARRED, A.sort_key, A.sort_desc);
    else if (A.loc == LOC_TRASH) A.list = fs_list_start_virtual(FS_V_TRASH, A.sort_key, A.sort_desc);
    else if (A.loc == LOC_OTHER) A.drive_n = fs_drives(A.drives, 26);
    else {
        A.loc = LOC_FOLDER;
        snprintf(A.path, sizeof A.path, "%s", entry);
        A.list = fs_list_start(A.path, A.sort_key, A.sort_desc);
    }
}

static void push_history(const char *entry) {
    if (A.hist_n > 0 && strcmp(A.hist[A.hist_pos], entry) == 0) return;
    A.hist_n = A.hist_pos + 1;
    if (A.hist_n == HIST_MAX) {
        memmove(A.hist[0], A.hist[1], (size_t)(HIST_MAX - 1) * sizeof A.hist[0]);
        A.hist_n--;
    }
    snprintf(A.hist[A.hist_n], sizeof A.hist[0], "%s", entry);
    A.hist_pos = A.hist_n;
    A.hist_n++;
}

void app_navigate(const char *target) {
    char entry[FS_PATH_MAX];
    snprintf(entry, sizeof entry, "%s", target);
    if (entry[0] != '@') {
        size_t l = strlen(entry);
        while (l > 3 && entry[l - 1] == '\\') entry[--l] = 0;
        if (l == 2 && entry[1] == ':') { entry[2] = '\\'; entry[3] = 0; }
        if (!fs_is_dir(entry)) { cui_toast(A.ui, "That folder does not exist", .description = entry, .tone = CUI_TONE_WARNING); return; }
    }
    push_history(entry);
    load_entry(entry);
}

static void go_hist(int pos) {
    if (pos < 0 || pos >= A.hist_n) return;
    A.hist_pos = pos;
    load_entry(A.hist[pos]);
}

void app_back(void) { go_hist(A.hist_pos - 1); }
void app_forward(void) { go_hist(A.hist_pos + 1); }
void app_reload(void) {
    if (A.searching) { fsearch_refresh(); fsearch_changed(); return; }
    if (A.loc == LOC_FOLDER) load_entry(A.path);
    else if (A.loc != LOC_OTHER) { char e[3] = { '@', (char)('0' + A.loc), 0 }; load_entry(e); }
}

/* ---- a quiet reload: the folder shows new files while a copy runs, and the selection and scroll stay ---- */

static fs_list *next_list;

static void quiet_cleanup(void) { if (next_list) { fs_list_release(next_list); next_list = NULL; } }

static int cmp_name(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

void app_reload_quiet(void) {
    if (A.loc != LOC_FOLDER || A.searching || !A.list || !A.list_seen || next_list || fs_list_state(A.list) != FS_DONE) return;
    next_list = fs_list_start(A.path, A.sort_key, A.sort_desc);
}

/* The selection, cursor and anchor are read when the new listing replaces the old one, not when it was asked for:
 * the user may have clicked meanwhile. Names point into the old listing, which lives until they are used. */
static void poll_next(void) {
    if (!next_list) return;
    if (fs_list_state(next_list) == FS_LOADING) { cui_request_wakeup(A.ui, 0.03f); return; }
    /* a check holds the swap while this file exists, to change the selection during a slow listing */
    const char *gate = test_mode ? getenv("DFM_TEST_HOLD_RELOAD") : NULL;
    if (gate && GetFileAttributesA(gate) != INVALID_FILE_ATTRIBUTES) { cui_request_wakeup(A.ui, 0.03f); return; }
    if (A.loc != LOC_FOLDER || A.searching || strcmp(next_list->path, A.path) != 0 || fs_list_state(next_list) != FS_DONE) {
        quiet_cleanup(); return;
    }
    const char **keep = malloc(((size_t)A.sel_n + 1) * sizeof *keep);
    const char *cur = A.cursor >= 0 && A.cursor < A.vis_n ? app_item_name(A.cursor) : NULL;
    const char *anc = A.anchor >= 0 && A.anchor < A.vis_n ? app_item_name(A.anchor) : NULL;
    int n = 0, first = -1;
    for (int i = 0; keep && i < A.vis_n; i++)
        if (A.sel[i] && n < A.sel_n) keep[n++] = app_item_name(i);
    if (n) qsort(keep, (size_t)n, sizeof *keep, cmp_name);
    fs_list *old = A.list;
    ui_band_cancel();
    A.list = next_list; next_list = NULL;
    if (A.list->sort_key != A.sort_key || A.list->sort_desc != A.sort_desc) fs_list_sort(A.list, A.sort_key, A.sort_desc);
    app_rebuild_view();
    A.reset_scroll = false;
    for (int i = 0; i < A.vis_n; i++) {
        const char *name = app_item_name(i);
        if (n && bsearch(&name, keep, (size_t)n, sizeof *keep, cmp_name)) { A.sel[i] = true; if (first < 0) first = i; }
        if (cur && A.cursor < 0 && !strcmp(name, cur)) A.cursor = i;
        if (anc && A.anchor < 0 && !strcmp(name, anc)) A.anchor = i;
    }
    if (A.cursor < 0) A.cursor = first;
    if (n) app_count_selection();
    free(keep);
    fs_list_release(old);
}

void app_up(void) {
    char p[FS_PATH_MAX];
    if (A.loc == LOC_FOLDER && fs_parent(A.path, p, sizeof p)) app_navigate(p);
}

void app_open_vis(int i) {
    if (i < 0 || i >= A.vis_n || A.loc == LOC_TRASH || (app_item(i)->flags & FS_INCOMING)) return;
    char p[FS_PATH_MAX];
    app_path_of(i, p, sizeof p);
    if (app_item(i)->flags & FS_DIR) { app_navigate(p); return; }
    HRESULT hr = shell_open((HWND)A.hwnd, p);
    if (FAILED(hr)) cui_toast(A.ui, "Could not open the file", .description = p, .tone = CUI_TONE_DANGER);
}


/* ---- keys ---- */

static bool path_key;           /* Ctrl+L was pressed this frame: the path field takes the keyboard in it */
static bool field_keys;         /* a text field held the keyboard at the end of the last frame */

/* A letter typed in the file view starts the search, as in GNOME Files. It is decided at the end of the frame,
 * when every field has shown whether it holds the keyboard: a path field that Ctrl+L just opened, a rename or
 * new-folder popover, the search field and an open menu keep their letters. */
static void typed_letters(void) {
    if (!typed_n) return;
    if (!path_key && !cui_wants_keyboard(A.ui) && !A.search_open && !act_modal_open() && !ctxmenu_is_open(A.ui) &&
        A.loc != LOC_OTHER && A.list && A.list_seen) {
        typed[typed_n] = 0;
        fs_w_to_u8(typed, A.search, (int)sizeof A.search);
        A.search_open = true;
        fsearch_changed();
    }
    typed_n = 0;
}

static void global_keys(void) {
    path_key = false;
    cui_ctx *ui = A.ui;
    int mods = cui_key_mods(ui);
    /* CUI forgets the field that holds the keyboard when a frame begins and learns it again as the fields are
     * drawn, so before the fields only the last frame knows. Asking CUI here always said no: Enter in the path
     * field or a popover also opened the selected file, and Delete or Ctrl+V in a field acted on files. */
    bool typing = field_keys;
    if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_LEFT)) app_back();
    else if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_RIGHT)) app_forward();
    else if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_UP)) app_up();
    else if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_HOME)) { if (A.places[0][0]) app_navigate(A.places[0]); }
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_F)) app_set_search_open(true);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_L)) path_key = true;
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_1)) app_set_view(VIEW_GRID);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_2)) app_set_view(VIEW_LIST);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_H)) { A.show_hidden = !A.show_hidden; app_rebuild_view(); }
    if (cui_key_pressed(ui, CUI_KEY_F5)) app_reload();
    ui_zoom_keys();
    if (ctxmenu_is_open(ui) || ui_view_options_open()) return;   /* an open menu has Up, Down, Enter and Esc; the view and the search field do not */
    if (typing) {
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE) && A.search_open) app_set_search_open(false);
        return;
    }
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_A)) app_select_all();
    act_keys();
    if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
        if (A.search_open) app_set_search_open(false); else app_clear_selection();
    }
    if (cui_shortcut(ui, 0, CUI_KEY_ENTER) && A.cursor >= 0) app_open_vis(A.cursor);
    if (A.view == VIEW_GRID) ui_grid_keys();
}

/* ---- the frame ---- */

static void list_body(void) {
    int st = A.list ? fs_list_state(A.list) : FS_FAILED;
    if (st == FS_FAILED) {
        if (A.loc != LOC_FOLDER) { ui_state_view(CUI_ICON_LOCK, "Unable to load this view", "Try again in a moment."); return; }
        const char *base = strrchr(A.path, '\\');
        char t[300];
        snprintf(t, sizeof t, "Unable to access \xE2\x80\x9C%s\xE2\x80\x9D", (base && base[1]) ? base + 1 : A.path);
        ui_state_view(CUI_ICON_LOCK, t, A.list ? fs_error_text(A.list->error) : "");
        return;
    }
    if (st == FS_LOADING) return;
    if (A.list->count == 0 && A.searching) { fsearch_empty_view(); return; }
    if (A.list->count == 0) {
        if (A.loc == LOC_RECENT) ui_state_view(CUI_ICON_CLOCK, "No Recent Files", "Files you open will show up here.");
        else if (A.loc == LOC_STARRED) ui_state_view(CUI_ICON_STAR, "No Starred Files", "Star a file or folder to find it quickly.");
        else if (A.loc == LOC_TRASH) ui_state_view(CUI_ICON_TRASH_2, "Trash is Empty", "Items you move to the Trash will show up here.");
        else { ui_state_view(CUI_ICON_FOLDER_OPEN, "Folder is Empty", NULL); }
        return;
    }
    if (A.vis_n == 0 && A.searching) { fsearch_empty_view(); return; }
    if (A.vis_n == 0) {
        ui_state_view(CUI_ICON_SEARCH, A.search[0] ? "No Results" : "Nothing to Show",
                      A.search[0] ? "Try a different search." : "Only hidden items are here. Press Ctrl+H to show them.");
        return;
    }
    if (A.zoom == Z_LIST) ui_column_view();
    else if (A.view == VIEW_GRID) ui_grid_view(); else ui_list_view();
}

static void content_body(void) {
    cui_ctx *ui = A.ui;
    if (A.loc == LOC_OTHER) { ui_other_locations(); return; }
    if (A.loc == LOC_TRASH) {
        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL) { ui_trash_bar(); list_body(); }
        return;
    }
    list_body();
}

static void frame(void) {
    cui_ctx *ui = A.ui;
    A.dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;
    theme_apply(ui, A.dark);
    act_pre_frame();
    poll_list();
    poll_next();
    fsearch_frame();
    if (A.list && fs_list_state(A.list) == FS_LOADING) cui_request_wakeup(ui, 0.02f);
    global_keys();
    cui_vec2 vp = cui_viewport_size(ui);
    bool narrow = vp.x < 640.0f;
    A.content_w = narrow ? vp.x : vp.x - 240.0f;
    A.content_h = vp.y - 46.0f;
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .bg = CUI_COLOR_BACKGROUND) {
        if (!narrow) {
            cui_column(ui, .width = CUI_PX(240), .height = CUI_FILL, .bg = CUI_COLOR_BACKGROUND) {
                ui_sidebar_header();
                ui_sidebar_places();
            }
        }
        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .border_color = CUI_COLOR_BORDER_SUBTLE) {
            ui_content_header();
            cui_box(ui, .dir = CUI_STACK, .width = CUI_FILL, .height = CUI_FILL, .clip = true) {
                content_body();
                app_count_selection();
                ui_status_pill();
                act_frame();
                views_frame();
            }
        }
    }
    if (narrow && A.sidebar_open) {
        if (cui_sheet_begin(ui, "Dio File Manager", &A.sidebar_open, &(cui_sheet_opts){ .side = CUI_SIDE_LEFT, .size = 270 })) {
            ui_sidebar_places();
            cui_sheet_end(ui);
        }
    }
    typed_letters();
    field_keys = cui_wants_keyboard(ui) || path_key;
    ui_popup_tooltips(ui);
}

/* ---- start ---- */

typedef struct options {
    char shot[FS_PATH_MAX], path[FS_PATH_MAX], select[260], search[256];
    char test_state[FS_PATH_MAX];
    int loc;                       /* 0 folder, else LOC_* for --loc */
    int view, theme, w, h;
    int act;                       /* 0 none, 1 item menu, 2 background menu, 3 rename popover, 4 new-folder popover, 5 and 6 the same two menus in the list view */
} options;

static void arg_u8(const wchar_t *w, char *out, size_t cap) { fs_w_to_u8(w, out, (int)cap); }

static void parse_args(options *o) {
    o->view = -1; o->theme = 0; o->w = 1080; o->h = 720;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; i++) {
        const wchar_t *a = argv[i];
        bool more = i + 1 < argc;
        if (!wcscmp(a, L"--shot") && more) arg_u8(argv[++i], o->shot, sizeof o->shot);
        else if (!wcscmp(a, L"--test-state") && more) arg_u8(argv[++i], o->test_state, sizeof o->test_state);
        else if (!wcscmp(a, L"--path") && more) arg_u8(argv[++i], o->path, sizeof o->path);
        else if (!wcscmp(a, L"--loc") && more) { ++i; o->loc = !wcscmp(argv[i], L"recent") ? LOC_RECENT : !wcscmp(argv[i], L"starred") ? LOC_STARRED : !wcscmp(argv[i], L"trash") ? LOC_TRASH : !wcscmp(argv[i], L"other") ? LOC_OTHER : 0; }
        else if (!wcscmp(a, L"--select") && more) arg_u8(argv[++i], o->select, sizeof o->select);
        else if (!wcscmp(a, L"--search") && more) arg_u8(argv[++i], o->search, sizeof o->search);
        else if (!wcscmp(a, L"--act") && more) { ++i; o->act = !wcscmp(argv[i], L"menu") ? 1 : !wcscmp(argv[i], L"bgmenu") ? 2 : !wcscmp(argv[i], L"rename") ? 3 : !wcscmp(argv[i], L"newdir") ? 4 : !wcscmp(argv[i], L"listmenu") ? 5 : !wcscmp(argv[i], L"listbg") ? 6 : 0; }
        else if (!wcscmp(a, L"--view") && more) { ++i; o->view = !wcscmp(argv[i], L"list") ? VIEW_LIST : !wcscmp(argv[i], L"grid") ? VIEW_GRID : -1; }
        else if (!wcscmp(a, L"--theme") && more) { ++i; o->theme = !wcscmp(argv[i], L"dark") ? 2 : !wcscmp(argv[i], L"light") ? 1 : 0; }
        else if (!wcscmp(a, L"--size") && more) { ++i; int w, h; if (swscanf_s(argv[i], L"%dx%d", &w, &h) == 2) { o->w = w; o->h = h; } }
        else if (a[0] != L'-' && !o->path[0]) arg_u8(a, o->path, sizeof o->path);
    }
}

static void json_text(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fputc('\\', f);
        if (c < 32) fprintf(f, "\\u%04x", c); else fputc(c, f);
    }
    fputc('"', f);
}

/* Test actions arrive through the real Win32 window; this only captures state.
   A listing counts as finished only once the window has applied it, so state and count agree. */
static void test_snapshot(const char *base) {
    char request[FS_PATH_MAX + 16], output[FS_PATH_MAX + 16];
    snprintf(request, sizeof request, "%s.request", base);
    if (GetFileAttributesA(request) == INVALID_FILE_ATTRIBUTES) return;
    snprintf(output, sizeof output, "%s.png", base);
    ui_window_screenshot(g_app, output);
    snprintf(output, sizeof output, "%s.json", base);
    FILE *f = fopen(output, "w");
    if (!f) return;
    fprintf(f, "{\"loc\":%d,\"trash\":%d,\"views\":%d,\"path\":", A.loc, A.trash_n, views_state()); json_text(f, A.path);
    fprintf(f, ",\"view\":%d,\"state\":%d,\"count\":%d,\"selected\":%d,\"cursor\":%d,"
               "\"first\":%d,\"last\":%d,\"cols\":%d,\"hidden\":%d,\"narrow\":%d,\"overlay\":%d,"
               "\"typing\":%d,\"ctx\":%d,\"search\":%d,\"searching\":%d,\"act\":%d,\"row\":[%.2f,%.2f,%.2f,%.2f],\"selection\":[",
            A.view, A.list ? (A.list_seen ? fs_list_state(A.list) : FS_LOADING) : -1, A.vis_n, A.sel_n, A.cursor,
            A.first_row, A.last_row, A.cols, A.show_hidden, app_is_narrow(), A.sidebar_open,
            cui_wants_keyboard(A.ui), ctxmenu_is_open(A.ui), A.search_open, A.searching, act_state(), A.first_rect.x, A.first_rect.y, A.first_rect.w, A.first_rect.h);
    bool comma = false;
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i]) {
        if (comma) fputc(',', f);
        json_text(f, app_item_name(i)); comma = true;
    }
    fprintf(f, "],\"listed\":%d,\"incoming\":[", A.list ? A.list->count : 0);
    comma = false;
    for (int i = 0; i < A.vis_n; i++) if (app_item(i)->flags & FS_INCOMING) {
        if (comma) fputc(',', f);
        json_text(f, app_item_name(i)); comma = true;
    }
    fprintf(f, "],\"names\":[");
    for (int i = 0; i < A.vis_n && i < 300; i++) { if (i) fputc(',', f); json_text(f, app_item_name(i)); }
    fprintf(f, "],\"band\":%d,\"zoom\":%d,\"icon\":%d,\"column_rows\":%d,\"slider\":[%.2f,%.2f,%.2f,%.2f],\"query\":", ui_band_dragging(), A.zoom, A.last_icon, A.column_rows, A.zoom_rect.x, A.zoom_rect.y, A.zoom_rect.w, A.zoom_rect.h); json_text(f, A.search);
    fprintf(f, ",\"documents\":"); json_text(f, A.places[2]);
    fprintf(f, ",\"place\":%d,\"drive\":", app_at_place(2));
    json_text(f, A.drive_n ? A.drives[0].root : "");
    act_test_snapshot(f);
    act_jobs_snapshot(f);
    ui_sidebar_snapshot(f);
    ui_popup_snapshot(f);
    fprintf(f, "}"); fclose(f);
    DeleteFileA(request);
}

static void window_frame(cui_ctx *ui, void *user) {
    (void)ui;
    static int frames;
    frame();
    if (test_mode && ++frames > 3) test_snapshot(user);
}

/* Screenshot of an action: the same real events a person would cause, at set frames. */
static void shot_action(const options *o, int k) {
    if (!o->act) return;
    float x = A.first_rect.x + 16, y = A.first_rect.y + 40;
    if (A.cursor >= 0 && A.cols > 0) {
        int col = A.cursor % A.cols;
        x += (col + 0.5f) * (A.first_rect.w - 32) / A.cols;
        y += (float)((A.cursor / A.cols) - A.first_row) * A.first_rect.h;
    }
    if (o->act == 2) { x = A.first_rect.x + A.first_rect.w - 60; y = A.first_rect.y + A.first_rect.h * 3 + 30; }
    if (o->act == 5) { x = 500; y = 46 + 36 + 36 + 18; }
    if (o->act == 6) { x = 500; y = 600; }
    HWND w = (HWND)A.hwnd;
    float scale = (float)GetDpiForWindow(w) / 96.0f;
    LPARAM at = (LPARAM)((int)(x * scale) | ((int)(y * scale) << 16));
    if (o->act >= 5) {
        if (k == 3) SendMessageW(w, WM_MOUSEMOVE, 0, at);
        if (k == 6) SendMessageW(w, WM_RBUTTONDOWN, MK_RBUTTON, at);
        if (k == 7) SendMessageW(w, WM_RBUTTONUP, 0, at);
    }
    if ((o->act == 1 || o->act == 2) && k == 3) SendMessageW(w, WM_MOUSEMOVE, 0, at);
    if ((o->act == 1 || o->act == 2) && k == 6) SendMessageW(w, WM_RBUTTONDOWN, MK_RBUTTON, at);
    if ((o->act == 1 || o->act == 2) && k == 7) SendMessageW(w, WM_RBUTTONUP, 0, at);
    if (o->act == 3 && k == 3) act_rename();
    if (o->act == 4 && k == 3) act_new_folder();
}

int main(void) {
    options o;
    memset(&o, 0, sizeof o);
    parse_args(&o);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool shot = o.shot[0] != 0;
    g_app = cui_app_create(&(cui_app_desc){ .title = "Dio File Manager", .width = o.w, .height = o.h, .min_width = 360, .min_height = 280,
                                            .hidden = shot, .continuous = shot });
    if (!g_app) return 1;
    test_mode = o.test_state[0] != 0;
    if (test_mode) {   /* checks never start another app on the owner's desktop; they read what would open */
        static char opened[FS_PATH_MAX + 16];
        snprintf(opened, sizeof opened, "%s.opened", o.test_state);
        shell_test_log(opened);
        recent_test_source(getenv("DFM_TEST_RECENT"));
        fs_test_drives(getenv("DFM_TEST_DRIVES"), getenv("DFM_TEST_EJECT_LOG"));
    }
    if (!shot)
        test_window_proc = (WNDPROC)SetWindowLongPtrW((HWND)cui_app_native_window(g_app), GWLP_WNDPROC, (LONG_PTR)test_input_proc);
    memset(&A, 0, sizeof A);
    A.ui = cui_app_ui(g_app);
    A.hwnd = cui_app_native_window(g_app);
    app_load_view();
    if (o.view >= 0) app_set_view(o.view);
    A.cursor = A.anchor = -1; A.hover = -1; A.last_click_idx = -1;
    A.sort_key = FS_SORT_NAME;
    A.trash_check_t = -100;
    starred_load();
    for (int i = 0; i < 7; i++) fs_known(i, A.places[i], sizeof A.places[i]);
    A.drive_n = fs_drives(A.drives, 26);
    theme_load_fonts(A.ui);
    app_set_theme(o.theme);
    const char *start = o.path[0] ? o.path : A.places[0];
    if (!start[0] || !fs_is_dir(start)) start = A.drive_n ? A.drives[0].root : "C:\\";
    app_navigate(start);
    if (o.loc) { char e[3] = { '@', (char)('0' + o.loc), 0 }; app_navigate(e); }
    ui_window_callback(g_app, window_frame, o.test_state);

    int frames = 0, rc = 0, ready = 0, ready_search = 0;
    bool searched = false;
    while (cui_app_frame(g_app)) {
        window_frame(A.ui, o.test_state);
        frames++;
        if (shot && frames > 45 && ((A.list && fs_list_state(A.list) != FS_LOADING && A.list_seen) || A.loc == LOC_OTHER)) {
            if (o.select[0] && A.sel_n == 0)
                for (int i = 0; i < A.vis_n; i++)
                    if (!strcmp(app_item_name(i), o.select)) { app_select_only(i); app_count_selection(); break; }
            if (o.search[0] && !searched) {   /* the same entry point as Ctrl+F and typing */
                searched = true;
                snprintf(A.search, sizeof A.search, "%s", o.search);
                A.search_open = true;
                fsearch_changed();
            }
            bool search_ok = !o.search[0] || (searched && A.searching && !fsearch_running() && A.vis_n >= 0 && frames > ready_search + 60);
            if (searched && !ready_search) ready_search = frames;
            if (search_ok && (!o.select[0] || A.sel_n > 0)) {
                if (!ready) ready = frames;
                int k = frames - ready;
                shot_action(&o, k);
                if (k >= (o.act ? 40 : 3)) { rc = ui_window_screenshot(g_app, o.shot) ? 0 : 2; break; }
            }
        }
        if (shot && frames > 600) { rc = 3; break; }
    }
    act_shutdown();
    ui_sidebar_shutdown();
    app_flush_view();
    cui_app_destroy(g_app);
    return rc;
}
