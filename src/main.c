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

app_t A;
static cui_app *g_app;

/* ---- small helpers ---- */

bool app_is_narrow(void) { return cui_viewport_size(A.ui).x < 640.0f; }

const fs_entry *app_item(int i) { return &A.list->items[A.vis[i]]; }
const char *app_item_name(int i) { return fs_name(A.list, A.vis[i]); }

void app_path_of(int i, char *out, size_t cap) {
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
    A.search_open = open;
    if (!open && A.search[0]) { A.search[0] = 0; app_rebuild_view(); }
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
            if (A.search[0] && !contains_nocase(fs_name(A.list, i), A.search)) continue;
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
}

/* ---- navigation ---- */

static void load_entry(const char *entry) {
    if (A.list) { fs_list_release(A.list); A.list = NULL; }
    A.list_seen = false; A.vis_n = 0; A.sel_n = 0; A.cursor = A.anchor = -1; A.reset_scroll = true;
    A.search[0] = 0; A.search_open = false; A.sidebar_open = false;
    if (entry[0] == '@') {
        A.loc = entry[1] - '0';
        A.path[0] = 0;
    } else {
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
void app_reload(void) { if (A.loc == LOC_FOLDER) load_entry(A.path); }

void app_up(void) {
    char p[FS_PATH_MAX];
    if (A.loc == LOC_FOLDER && fs_parent(A.path, p, sizeof p)) app_navigate(p);
}

void app_open_vis(int i) {
    if (i < 0 || i >= A.vis_n) return;
    char p[FS_PATH_MAX];
    app_path_of(i, p, sizeof p);
    if (app_item(i)->flags & FS_DIR) { app_navigate(p); return; }
    wchar_t w[FS_PATH_MAX];
    if (fs_u8_to_w(p, w, FS_PATH_MAX)) ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
}

void app_set_view(int v) {
    if (A.view != v) { A.view = v; A.reset_scroll = true; }
}

/* ---- keys ---- */

static void global_keys(void) {
    cui_ctx *ui = A.ui;
    int mods = cui_key_mods(ui);
    bool typing = cui_wants_keyboard(ui);
    if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_LEFT)) app_back();
    else if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_RIGHT)) app_forward();
    else if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_UP)) app_up();
    else if (cui_shortcut(ui, CUI_MOD_ALT, CUI_KEY_HOME)) { if (A.places[0][0]) app_navigate(A.places[0]); }
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_F)) app_set_search_open(true);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_1)) app_set_view(VIEW_GRID);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_2)) app_set_view(VIEW_LIST);
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_H)) { A.show_hidden = !A.show_hidden; app_rebuild_view(); }
    if (cui_key_pressed(ui, CUI_KEY_F5)) app_reload();
    if ((mods & CUI_MOD_CTRL) && cui_key_pressed(ui, CUI_KEY_EQUAL) && A.zoom < 3) A.zoom++;
    if ((mods & CUI_MOD_CTRL) && cui_key_pressed(ui, CUI_KEY_MINUS) && A.zoom > 0) A.zoom--;
    if (typing) {
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE) && A.search_open) app_set_search_open(false);
        return;
    }
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_A)) app_select_all();
    if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
        if (A.search_open) app_set_search_open(false); else app_clear_selection();
    }
    if (cui_key_pressed(ui, CUI_KEY_ENTER) && A.cursor >= 0) app_open_vis(A.cursor);
    if (cui_key_pressed(ui, CUI_KEY_BACKSPACE) && !A.search_open) app_up();
    if (A.view == VIEW_GRID) ui_grid_keys();
}

/* ---- the frame ---- */

static void other_locations(void) {
    cui_ctx *ui = A.ui;
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 24, .gap = 10, .scroll_y = true) {
        cui_text(ui, "Other Locations", .font = CUI_FONT_MEDIUM, .size = 18);
        for (int i = 0; i < A.drive_n; i++) {
            cui_push_id_int(ui, i);
            char sub[96] = "";
            if (A.drives[i].total) {
                char f[48], t[48];
                fs_format_size(f, sizeof f, A.drives[i].free);
                fs_format_size(t, sizeof t, A.drives[i].total);
                snprintf(sub, sizeof sub, "%s free of %s", f, t);
            }
            cui_row(ui, .width = CUI_FILL, .max_width = 560, .pad = 12, .gap = 12, .bg = CUI_COLOR_SURFACE_RAISED,
                    .radius = 14, .border = 1) {
                cui_icon(ui, CUI_ICON_SERVER, 28, CUI_COLOR_ACCENT);
                cui_column(ui, .width = CUI_FILL, .gap = 2) {
                    cui_text(ui, A.drives[i].name, .nowrap = true);
                    cui_text(ui, sub, .color = CUI_COLOR_TEXT_SECONDARY, .size = 12);
                }
                if (cui_button(ui, "Open", .size = CUI_SIZE_SM)) app_navigate(A.drives[i].root);
            }
            cui_pop_id(ui);
        }
    }
}

static void content_body(void) {
    cui_ctx *ui = A.ui;
    if (A.loc == LOC_RECENT) { ui_state_view(CUI_ICON_CLOCK, "No Recent Files", "Files you open will show up here."); return; }
    if (A.loc == LOC_STARRED) { ui_state_view(CUI_ICON_STAR, "No Starred Files", "Star a file or folder to find it quickly."); return; }
    if (A.loc == LOC_TRASH) { ui_state_view(CUI_ICON_TRASH_2, "Trash is Empty", NULL); return; }
    if (A.loc == LOC_OTHER) { other_locations(); return; }
    int st = A.list ? fs_list_state(A.list) : FS_FAILED;
    if (st == FS_FAILED) {
        const char *base = strrchr(A.path, '\\');
        char t[300];
        snprintf(t, sizeof t, "Unable to access \xE2\x80\x9C%s\xE2\x80\x9D", (base && base[1]) ? base + 1 : A.path);
        ui_state_view(CUI_ICON_LOCK, t, A.list ? fs_error_text(A.list->error) : "");
        return;
    }
    if (st == FS_LOADING) return;
    if (A.list->count == 0) { ui_state_view(CUI_ICON_FOLDER_OPEN, "Folder is Empty", NULL); return; }
    if (A.vis_n == 0) {
        ui_state_view(CUI_ICON_SEARCH, A.search[0] ? "No Results" : "Nothing to Show",
                      A.search[0] ? "Try a different search." : "Only hidden items are here. Press Ctrl+H to show them.");
        return;
    }
    (void)ui;
    if (A.view == VIEW_GRID) ui_grid_view(); else ui_list_view();
}

static void frame(void) {
    cui_ctx *ui = A.ui;
    static int tick;
    if (A.theme_pref == 0 && ++tick % 30 == 0) app_set_theme(0);
    A.dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;
    theme_apply(ui, A.dark);
    poll_list();
    global_keys();
    cui_vec2 vp = cui_viewport_size(ui);
    bool narrow = vp.x < 640.0f;
    A.content_w = narrow ? vp.x : vp.x - 240.0f;
    A.content_h = vp.y - 46.0f - (A.search_open ? 54.0f : 0.0f);
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .bg = CUI_COLOR_BACKGROUND) {
        if (!narrow) {
            cui_column(ui, .width = CUI_PX(240), .height = CUI_FILL, .bg = CUI_COLOR_BACKGROUND) {
                ui_sidebar_header();
                ui_sidebar_places();
            }
        }
        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .border_color = CUI_COLOR_BORDER_SUBTLE) {
            ui_content_header();
            ui_search_bar();
            cui_box(ui, .dir = CUI_STACK, .width = CUI_FILL, .height = CUI_FILL, .clip = true) {
                content_body();
                app_count_selection();
                ui_status_pill();
            }
        }
    }
    if (narrow && A.sidebar_open) {
        if (cui_sheet_begin(ui, "Dio File Manager", &A.sidebar_open, &(cui_sheet_opts){ .side = CUI_SIDE_LEFT, .size = 270 })) {
            ui_sidebar_places();
            cui_sheet_end(ui);
        }
    }
}

/* ---- start ---- */

typedef struct options {
    char shot[FS_PATH_MAX], path[FS_PATH_MAX], select[260];
    int view, theme, w, h;
} options;

static void arg_u8(const wchar_t *w, char *out, size_t cap) { fs_w_to_u8(w, out, (int)cap); }

static void parse_args(options *o) {
    o->view = VIEW_GRID; o->theme = 0; o->w = 1080; o->h = 720;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; i++) {
        const wchar_t *a = argv[i];
        bool more = i + 1 < argc;
        if (!wcscmp(a, L"--shot") && more) arg_u8(argv[++i], o->shot, sizeof o->shot);
        else if (!wcscmp(a, L"--path") && more) arg_u8(argv[++i], o->path, sizeof o->path);
        else if (!wcscmp(a, L"--select") && more) arg_u8(argv[++i], o->select, sizeof o->select);
        else if (!wcscmp(a, L"--view") && more) o->view = !wcscmp(argv[++i], L"list") ? VIEW_LIST : VIEW_GRID;
        else if (!wcscmp(a, L"--theme") && more) { ++i; o->theme = !wcscmp(argv[i], L"dark") ? 2 : !wcscmp(argv[i], L"light") ? 1 : 0; }
        else if (!wcscmp(a, L"--size") && more) { ++i; int w, h; if (swscanf_s(argv[i], L"%dx%d", &w, &h) == 2) { o->w = w; o->h = h; } }
        else if (a[0] != L'-' && !o->path[0]) arg_u8(a, o->path, sizeof o->path);
    }
}

static bool write_shot(cui_app *app, const char *path) {
    const cui_draw_data *dd = cui_get_draw_data(cui_app_ui(app));
    int w = dd->width_px, h = dd->height_px;
    unsigned char *px = malloc((size_t)w * (size_t)h * 4);
    bool ok = px && cui_app_screenshot(app, px, w, h) && cui_write_png(path, px, w, h, w * 4);
    free(px);
    return ok;
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
    memset(&A, 0, sizeof A);
    A.ui = cui_app_ui(g_app);
    A.view = o.view; A.zoom = 1; A.cursor = A.anchor = -1; A.hover = -1; A.last_click_idx = -1;
    A.sort_key = FS_SORT_NAME;
    for (int i = 0; i < 7; i++) fs_known(i, A.places[i], sizeof A.places[i]);
    A.drive_n = fs_drives(A.drives, 26);
    theme_load_fonts(A.ui);
    app_set_theme(o.theme);
    const char *start = o.path[0] ? o.path : A.places[0];
    if (!start[0] || !fs_is_dir(start)) start = A.drive_n ? A.drives[0].root : "C:\\";
    app_navigate(start);

    int frames = 0, rc = 0;
    while (cui_app_frame(g_app)) {
        frame();
        frames++;
        if (shot && frames > 45 && A.list && fs_list_state(A.list) != FS_LOADING && A.list_seen) {
            if (o.select[0] && A.sel_n == 0)
                for (int i = 0; i < A.vis_n; i++)
                    if (!strcmp(app_item_name(i), o.select)) { app_select_only(i); app_count_selection(); break; }
            if (!o.select[0] || A.sel_n > 0) {
                if (frames > 48) { rc = write_shot(g_app, o.shot) ? 0 : 2; break; }
            }
        }
        if (shot && frames > 600) { rc = 3; break; }
    }
    cui_app_destroy(g_app);
    return rc;
}
