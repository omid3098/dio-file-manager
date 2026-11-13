/* The window's chrome: sidebar, content header with the path bar, search bar, empty and error states, status pill. */
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

#define HEADER_H 46.0f

static const char *const PLACE_LABEL[7] = { "Home", "Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos" };
static const char *const PLACE_ICON[7] = { CUI_ICON_HOUSE, CUI_ICON_MONITOR, CUI_ICON_FILE_TEXT, CUI_ICON_DOWNLOAD,
                                           CUI_ICON_MUSIC, CUI_ICON_IMAGE, CUI_ICON_VIDEO };

static bool same_path(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    while (la > 3 && a[la - 1] == '\\') la--;
    while (lb > 3 && b[lb - 1] == '\\') lb--;
    return la == lb && _strnicmp(a, b, la) == 0;
}

bool app_at_place(int which) { return A.loc == LOC_FOLDER && A.places[which][0] && same_path(A.path, A.places[which]); }

/* ---- sidebar ---- */

void ui_sidebar_header(void) {
    cui_ctx *ui = A.ui;
    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(HEADER_H), .pad_x = 8, .gap = 4) {
        cui_spacer(ui);
        if (cui_menu_begin(ui, "Main Menu", &(cui_menu_opts){ .icon = CUI_ICON_MENU, .icon_only = true })) {
            cui_menu_label(ui, "Theme");
            if (cui_menu_item(ui, "System", .icon = CUI_ICON_MONITOR, .checked = A.theme_pref == 0)) app_set_theme(0);
            if (cui_menu_item(ui, "Light", .icon = CUI_ICON_SUN, .checked = A.theme_pref == 1)) app_set_theme(1);
            if (cui_menu_item(ui, "Dark", .icon = CUI_ICON_MOON, .checked = A.theme_pref == 2)) app_set_theme(2);
            cui_menu_separator(ui);
            if (cui_menu_item(ui, "Show Hidden Files", .icon = CUI_ICON_EYE, .checked = A.show_hidden, .shortcut = "Ctrl H")) {
                A.show_hidden = !A.show_hidden;
                app_rebuild_view();
            }
            cui_menu_end(ui);
        }
    }
}

static bool place_row(const char *label, const char *icon, bool selected) {
    /* CUI 1.4.0 keeps a group's last selection even when none is selected. */
    return cui_nav_item(A.ui, label, .icon = icon, .selected = selected, .group = selected ? label : "places");
}

static struct {
    HANDLE thread;
    volatile LONG done;
    DWORD error;
    char root[4];
    cui_rect buttons[26];
} eject;

static void paint_eject(cui_ctx *ui, cui_rect r, void *unused) {
    (void)unused;
    float x = r.x + r.w / 2, y = r.y + r.h / 2;
    cui_color c = cui_color_of(ui, CUI_COLOR_TEXT_SECONDARY);
    cui_vec2 triangle[3] = { {x, y - 6}, {x + 6, y + 1}, {x - 6, y + 1} };
    cui_draw_polygon(ui, triangle, 3, c);
    cui_draw_line(ui, (cui_vec2){x - 6, y + 5}, (cui_vec2){x + 6, y + 5}, 1.75f, c);
}

static DWORD WINAPI eject_proc(LPVOID unused) {
    (void)unused;
    eject.error = fs_eject(eject.root);
    InterlockedExchange(&eject.done, 1);
    return 0;
}

void ui_sidebar_shutdown(void) {
    if (eject.thread) { WaitForSingleObject(eject.thread, INFINITE); CloseHandle(eject.thread); eject.thread = NULL; }
}

void ui_sidebar_snapshot(FILE *f) {
    fprintf(f, ",\"drives\":[");
    for (int i = 0; i < A.drive_n; i++) {
        cui_rect r = eject.buttons[i];
        fprintf(f, "%s{\"root\":\"%c:\\\\\",\"type\":%d,\"eject\":[%.2f,%.2f,%.2f,%.2f]}",
                i ? "," : "", A.drives[i].root[0], A.drives[i].type, r.x, r.y, r.w, r.h);
    }
    fprintf(f, "]");
}

void ui_sidebar_places(void) {
    cui_ctx *ui = A.ui;
    if (eject.thread) {
        cui_request_wakeup(ui, .05f);
        if (InterlockedCompareExchange(&eject.done, 0, 0)) {
            ui_sidebar_shutdown(); InterlockedExchange(&eject.done, 0);
            if (eject.error) {
                cui_toast(ui, "Could not eject drive", .description = "The drive may be in use. Close its files and try again.", .tone = CUI_TONE_DANGER);
            } else {
                for (int i = 0; i < A.drive_n; i++) if (!strcmp(A.drives[i].root, eject.root)) {
                    memmove(&A.drives[i], &A.drives[i + 1], (A.drive_n - i - 1) * sizeof A.drives[0]); A.drive_n--; break;
                }
                if (A.loc == LOC_FOLDER && !_strnicmp(A.path, eject.root, 3)) app_navigate(A.places[0]);
                cui_toast(ui, "Drive ejected");
            }
        }
    }
    memset(eject.buttons, 0, sizeof eject.buttons);
    cui_column(ui, .id = "places", .width = CUI_FILL, .height = CUI_FILL, .pad_x = 8, .pad_bottom = 8, .gap = 2, .scroll_y = true) {
        if (place_row("Recent", CUI_ICON_CLOCK, A.loc == LOC_RECENT)) app_navigate("@1");
        if (place_row("Starred", CUI_ICON_STAR, A.loc == LOC_STARRED)) app_navigate("@2");
        for (int i = 0; i < 7; i++) {
            if (!A.places[i][0]) continue;
            if (place_row(PLACE_LABEL[i], PLACE_ICON[i], app_at_place(i))) app_navigate(A.places[i]);
        }
        char tb[16];
        snprintf(tb, sizeof tb, "%d", A.trash_n);
        if (cui_nav_item(ui, "Trash", .icon = CUI_ICON_TRASH_2, .selected = A.loc == LOC_TRASH, .group = A.loc == LOC_TRASH ? "Trash" : "places", .badge = A.trash_n > 0 ? tb : NULL)) app_navigate("@3");
        cui_gap(ui, 6);
        cui_separator(ui);
        cui_gap(ui, 6);
        for (int i = 0; i < A.drive_n; i++) {
            bool here = A.loc == LOC_FOLDER && same_path(A.path, A.drives[i].root);
            cui_push_id_int(ui, i);
            cui_row(ui, .width = CUI_FILL, .gap = 0) {
                if (place_row(A.drives[i].name, CUI_ICON_SERVER, here)) app_navigate(A.drives[i].root);
                if (A.drives[i].type == DRIVE_REMOVABLE) {
                    cui_box(ui, .dir = CUI_STACK, .width = CUI_PX(36), .height = CUI_PX(36)) {
                        bool clicked = cui_icon_button(ui, "", .tooltip = "Eject Drive", .disabled = eject.thread != NULL);
                        eject.buttons[i] = cui_last_rect(ui);
                        cui_canvas_result icon = cui_canvas(ui, "ejecticon", CUI_FILL, CUI_FILL, paint_eject, NULL, 0);
                        if ((clicked || icon.clicked) && !eject.thread) {
                            snprintf(eject.root, sizeof eject.root, "%s", A.drives[i].root);
                            eject.thread = CreateThread(NULL, 0, eject_proc, NULL, 0, NULL);
                            if (!eject.thread) cui_toast(ui, "Could not eject drive", .tone = CUI_TONE_DANGER);
                        }
                    }

                }
            }
            cui_pop_id(ui);
        }
        if (place_row("Other Locations", CUI_ICON_PLUS, A.loc == LOC_OTHER)) app_navigate("@4");
    }
}

/* ---- content header ---- */

static char seg_label[32][128];
static char seg_path[32][FS_PATH_MAX];

static void join_path(char *out, size_t cap, const char *base, const char *name) {
    size_t bl = strlen(base);
    snprintf(out, cap, "%s%s%s", base, (bl && base[bl - 1] == '\\') ? "" : "\\", name);
}

static int build_segments(bool *home_first) {
    int n = 0;
    *home_first = false;
    if (A.loc != LOC_FOLDER) {
        const char *t = A.loc == LOC_RECENT ? "Recent" : A.loc == LOC_STARRED ? "Starred" : A.loc == LOC_TRASH ? "Trash" : "Other Locations";
        snprintf(seg_label[0], sizeof seg_label[0], "%s", t);
        seg_path[0][0] = 0;
        return 1;
    }
    const char *rest = A.path;
    size_t hl = A.places[0][0] ? strlen(A.places[0]) : 0;
    if (hl && _strnicmp(A.path, A.places[0], hl) == 0 && (A.path[hl] == 0 || A.path[hl] == '\\')) {
        snprintf(seg_label[0], sizeof seg_label[0], "Home");
        snprintf(seg_path[0], sizeof seg_path[0], "%s", A.places[0]);
        *home_first = true;
        n = 1;
        rest = A.path + hl;
    } else {
        const char *e = strchr(A.path, '\\');
        size_t l = e ? (size_t)(e - A.path) : strlen(A.path);
        if (l > 120) l = 120;
        memcpy(seg_label[0], A.path, l);
        seg_label[0][l] = 0;
        snprintf(seg_path[0], sizeof seg_path[0], "%s\\", seg_label[0]);
        n = 1;
        rest = e ? e : A.path + strlen(A.path);
    }
    while (*rest == '\\') rest++;
    while (*rest && n < 31) {
        const char *e = strchr(rest, '\\');
        size_t l = e ? (size_t)(e - rest) : strlen(rest);
        if (l > 127) l = 127;
        memcpy(seg_label[n], rest, l);
        seg_label[n][l] = 0;
        join_path(seg_path[n], sizeof seg_path[n], seg_path[n - 1], seg_label[n]);
        n++;
        rest = e ? e + 1 : rest + strlen(rest);
        while (*rest == '\\') rest++;
    }
    return n;
}

bool ui_view_options_open(void) { return cui_popover_is_open(A.ui, "view-options"); }

static void paint_zoom_ticks(cui_ctx *ui, cui_rect r, void *user) {
    (void)user;
    for (int i = 0; i < Z_COUNT; i++) {
        float x = r.x + 9 + (r.w - 18) * i / (Z_COUNT - 1);
        cui_draw_rect(ui, (cui_rect){ x - 1, r.y, 2, 4 }, CUI_CORNERS(1), CUI_COLOR_TEXT_MUTED);
    }
}

static void view_options_menu(void) {
    cui_ctx *ui = A.ui;
    static const char *const KEYS[4] = { "Name", "Size", "Modified", "Type" };
    static const char *const names[Z_COUNT] = { "Details", "List", "Small icons", "Medium icons", "Large icons", "Extra large icons" };
    if (cui_icon_button(ui, CUI_ICON_SLIDERS_HORIZONTAL, .tooltip = "View options", .selected = ui_view_options_open())) cui_popover_toggle(ui, "view-options");
    if (!cui_popover_begin(ui, "view-options", &(cui_popover_opts){ .placement = CUI_PLACE_BOTTOM_END, .width = 300, .pad = 12, .gap = 4 })) return;
    ui_view_options_surface();
    float stop = (float)A.zoom;
    if (cui_slider(ui, "View size", &stop, 0, Z_COUNT - 1, .step = 1, .width = CUI_FILL)) app_set_zoom((int)(stop + 0.5f));
    A.zoom_rect = cui_last_rect(ui);
    cui_canvas(ui, "zoom-ticks", CUI_FILL, CUI_PX(4), paint_zoom_ticks, NULL, 0);
    cui_text(ui, names[A.zoom], .width = CUI_FILL, .align = CUI_TEXT_CENTER, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
    cui_menu_separator(ui);
    cui_menu_label(ui, "Sort by");
    for (int i = 0; i < 4; i++)
        if (cui_menu_item(ui, KEYS[i], .checked = A.sort_key == i)) { A.sort_key = i; app_resort(); }
    cui_menu_separator(ui);
    if (cui_menu_item(ui, "Reversed Order", .checked = A.sort_desc)) { A.sort_desc = !A.sort_desc; app_resort(); }
    if (cui_menu_item(ui, "Show Hidden Files", .checked = A.show_hidden, .shortcut = "Ctrl H")) { A.show_hidden = !A.show_hidden; app_rebuild_view(); }
    cui_popover_end(ui);
}

void ui_content_header(void) {
    cui_ctx *ui = A.ui;
    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(HEADER_H), .pad_x = 8, .gap = 4) {
        if (app_is_narrow() && cui_icon_button(ui, CUI_ICON_PANEL_LEFT, .tooltip = "Sidebar")) A.sidebar_open = true;
        if (cui_icon_button(ui, CUI_ICON_CHEVRON_LEFT, .tooltip = "Back", .shortcut = "Alt Left", .disabled = A.hist_pos <= 0)) app_back();
        if (cui_icon_button(ui, CUI_ICON_CHEVRON_RIGHT, .tooltip = "Forward", .shortcut = "Alt Right", .disabled = A.hist_pos >= A.hist_n - 1)) app_forward();
        cui_gap(ui, 4);
        bool home_first;
        int n = build_segments(&home_first);
        if (A.search_open) {   /* the search field takes the place of the path bar */
            if (A.search_focus) { ui_search_focus(); A.search_focus = false; }
            int f = cui_search(ui, "searchfield", A.search, sizeof A.search, .placeholder = "Search Current Folder", .width = CUI_FILL, .autofocus = true, .clearable = true);
            if (f & CUI_CHANGED) fsearch_changed();
            if (fsearch_running()) cui_spinner(ui, 18);
        } else {
        const char *segs[32];
        for (int i = 0; i < n; i++) segs[i] = seg_label[i];
        cui_path_bar_result pr = cui_path_bar(ui, "pathbar", segs, n, .icon = home_first ? CUI_ICON_HOUSE : NULL,
                                              .menus = A.loc == LOC_FOLDER, .no_edit = A.loc != LOC_FOLDER, .text = A.path);
        if (A.loc == LOC_FOLDER) {
            if (pr.clicked >= 0 && pr.clicked < n - 1) app_navigate(seg_path[pr.clicked]);
            else if (pr.clicked == n - 1) app_reload();
            if (pr.submitted && pr.submitted[0]) {
                if (fs_is_dir(pr.submitted)) app_navigate(pr.submitted);
                else cui_toast(ui, "That folder does not exist", .description = pr.submitted, .tone = CUI_TONE_WARNING);
            }
            int seg;
            if (cui_path_bar_menu_begin(ui, "pathbar", &seg)) {
                static char names[24][128];
                int m = (seg >= 0 && seg < n) ? fs_subdirs(seg_path[seg], names, 24) : 0;
                if (m == 0) cui_menu_label(ui, "No folders");
                for (int i = 0; i < m; i++) {
                    cui_push_id_int(ui, i);
                    if (cui_menu_item(ui, names[i], .icon = CUI_ICON_FOLDER)) {
                        char p[FS_PATH_MAX];
                        join_path(p, sizeof p, seg_path[seg], names[i]);
                        app_navigate(p);
                    }
                    cui_pop_id(ui);
                }
                cui_path_bar_menu_end(ui);
            }
        }
        }
        cui_gap(ui, 4);
        act_job_button();
        if (cui_icon_button(ui, CUI_ICON_SEARCH, .tooltip = "Search", .shortcut = "Ctrl F", .selected = A.search_open)) app_set_search_open(!A.search_open);
        if (cui_icon_button(ui, CUI_ICON_GRID_3X3, .tooltip = "Grid view", .shortcut = "Ctrl 1", .selected = A.zoom >= Z_SMALL)) app_set_view(VIEW_GRID);
        if (cui_icon_button(ui, CUI_ICON_LIST, .tooltip = "List view", .shortcut = "Ctrl 2", .selected = A.zoom <= Z_LIST)) app_set_view(VIEW_LIST);
        view_options_menu();
    }
}

void ui_search_bar(void) {}   /* the field lives in the content header now */

/* ---- states and the status pill ---- */

void ui_state_view(const char *icon, const char *title, const char *detail) {
    cui_ctx *ui = A.ui;
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER, .gap = 10, .pad = 24) {
        cui_icon(ui, icon, 96, cui_alpha(CUI_COLOR_TEXT_MUTED, 0.7f));
        cui_gap(ui, 6);
        cui_text(ui, title, .font = CUI_FONT_MEDIUM, .size = 22, .align = CUI_TEXT_CENTER, .width = CUI_FILL);
        if (detail && detail[0]) cui_text(ui, detail, .color = CUI_COLOR_TEXT_SECONDARY, .align = CUI_TEXT_CENTER, .width = CUI_FILL);
    }
}

void ui_status_pill(void) {
    cui_ctx *ui = A.ui;
    char buf[400] = "";
    bool loading = A.list && fs_list_state(A.list) == FS_LOADING;
    if (A.sel_n == 1) {
        int idx = -1;
        for (int i = 0; i < A.vis_n; i++) if (A.sel[i]) { idx = i; break; }
        if (idx >= 0) {
            const fs_entry *e = app_item(idx);
            if (e->flags & FS_DIR) snprintf(buf, sizeof buf, "\xE2\x80\x9C%s\xE2\x80\x9D selected", app_item_name(idx));
            else {
                char sz[48];
                fs_format_size(sz, sizeof sz, e->size);
                snprintf(buf, sizeof buf, "\xE2\x80\x9C%s\xE2\x80\x9D selected (%s)", app_item_name(idx), sz);
            }
        }
    } else if (A.sel_n > 1) {
        char sz[48];
        fs_format_size(sz, sizeof sz, A.sel_bytes);
        if (A.sel_bytes) snprintf(buf, sizeof buf, "%d items selected (%s)", A.sel_n, sz);
        else snprintf(buf, sizeof buf, "%d items selected", A.sel_n);
    } else if (loading) {
        snprintf(buf, sizeof buf, "Loading\xE2\x80\xA6");
    }
    if (!buf[0]) return;
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_END, .justify = CUI_JUSTIFY_END, .pad = 14) {
        cui_row(ui, .bg = CUI_COLOR_SURFACE_RAISED, .radius = 14, .border = 1, .border_color = CUI_COLOR_BORDER,
                .elevation = CUI_ELEVATION_FLOATING, .pad_x = 14, .pad_y = 8, .max_width = 520) {
            cui_text(ui, buf, .nowrap = true);
        }
    }
}
