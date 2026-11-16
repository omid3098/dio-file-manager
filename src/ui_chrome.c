/* The window's chrome: sidebar, content header with the path bar, search bar, empty and error states, status pill. */
#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <cui_internal.h>
#include "ui.h"
#include "dlgtext.h"
#include "watch.h"
#include "sidebar.h"

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

/* ---- removing and adding sidebar rows ---- */

static void drive_key(const fs_drive *d, char out[8]) { snprintf(out, 8, "drive:%c", toupper((unsigned char)d->root[0])); }
static void place_key(int i, char out[8]) { snprintf(out, 8, "place%d", i); }

/* Whether a default row that is showing (not removed) is this folder. */
static bool default_row_has(const char *path) {
    char k[8];
    for (int i = 0; i < 7; i++) { place_key(i, k); if (A.places[i][0] && !sidebar_is_hidden(k) && sidebar_same_path(path, A.places[i])) return true; }
    for (int i = 0; i < A.drive_n; i++) { drive_key(&A.drives[i], k); if (!sidebar_is_hidden(k) && sidebar_same_path(path, A.drives[i].root)) return true; }
    return false;
}

/* A folder whose row was not saved (the file could not be written) takes another Add: that Add saves it. */
bool app_sidebar_has(const char *path) { return (sidebar_has(path) && !sidebar_unsaved()) || default_row_has(path); }

/* Brings the hidden default rows back. A folder added while its default row was hidden would show twice: its added row goes. */
static bool restore_hidden_rows(void) {
    if (!sidebar_show_hidden()) return false;
    bool ok = true;
    for (int i = sidebar_count() - 1; i >= 0; i--) {
        char p[FS_PATH_MAX];
        snprintf(p, sizeof p, "%s", sidebar_path(i));
        if (default_row_has(p) && !sidebar_remove(p)) ok = false;
    }
    return ok;
}

void app_sidebar_add(const char *path) {
    if (app_sidebar_has(path)) { cui_toast(A.ui, "Already in the Sidebar"); return; }
    if (sidebar_add(path)) cui_toast(A.ui, "Added to Sidebar");
    else cui_toast(A.ui, sidebar_has(path) ? "Could not save the sidebar" : "The sidebar is full", .tone = CUI_TONE_DANGER);
}

/* The right-click menu of a row: what it removes ("" for none), and whether it was opened this frame. */
static struct { bool open_here, hit; char key[FS_PATH_MAX]; bool added; } sbm;

/* Test state: every sidebar row of the last frame by its key, and the rows of the open row menu. */
static struct { char key[260]; cui_rect r; bool dim; } srow[96];
static int srow_n;
static cui_rect smenu_remove, smenu_show;

void ui_sidebar_rows_snapshot(FILE *f) {
    fprintf(f, ",\"sbrows\":[");
    for (int i = 0; i < srow_n; i++) {
        fprintf(f, "%s{\"key\":\"", i ? "," : "");
        for (const char *p = srow[i].key; *p; p++) { if (*p == '"' || *p == '\\') fputc('\\', f); fputc(*p, f); }
        fprintf(f, "\",\"dim\":%d,\"r\":[%.2f,%.2f,%.2f,%.2f]}", srow[i].dim, srow[i].r.x, srow[i].r.y, srow[i].r.w, srow[i].r.h);
    }
    fprintf(f, "],\"sbmenu\":{\"remove\":[%.2f,%.2f,%.2f,%.2f],\"show\":[%.2f,%.2f,%.2f,%.2f]}", smenu_remove.x, smenu_remove.y, smenu_remove.w, smenu_remove.h,
            smenu_show.x, smenu_show.y, smenu_show.w, smenu_show.h);
}

static bool in_rect(cui_rect r, cui_vec2 m) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; }

/* `over`: the pointer is on what was clicked. For a row that is cui_last_hovered: CUI's hit test is clipped by the list and its layer, a row's unbounded rectangle is not. */
static bool sb_right_click(bool over) {
    return over && cui_mouse_clicked(A.ui, CUI_MOUSE_RIGHT) && !act_modal_open() && !ui_popup_open_any(A.ui);
}

/* After a row: a right click on it opens the menu for it. A row with no key (Other Locations) only offers Show Hidden Places. */
static void row_rclick(cui_rect r, const char *key, bool added) {
    if (srow_n < 96) { snprintf(srow[srow_n].key, sizeof srow[0].key, "%s", key[0] ? key : "other"); srow[srow_n].dim = false; srow[srow_n++].r = r; }
    if (!sb_right_click(cui_last_hovered(A.ui))) return;
    sbm.hit = true;
    sbm.added = added;
    snprintf(sbm.key, sizeof sbm.key, "%s", key);
    sbm.open_here = key[0] || sidebar_any_hidden();
}

static void row_menu(void) {
    cui_ctx *ui = A.ui;
    memset(&smenu_remove, 0, sizeof smenu_remove); memset(&smenu_show, 0, sizeof smenu_show);
    if (!ctxmenu_begin(ui, "sbmenu", sbm.open_here)) return;
    if (sbm.key[0] && cui_menu_item(ui, "Remove from Sidebar", .icon = CUI_ICON_X)) {
        if (!(sbm.added ? sidebar_remove(sbm.key) : sidebar_hide(sbm.key))) cui_toast(ui, "Could not save the sidebar", .tone = CUI_TONE_DANGER);
    }
    if (sidebar_any_hidden() && cui_menu_item(ui, "Show Hidden Places", .icon = CUI_ICON_EYE)) {
        if (!restore_hidden_rows()) cui_toast(ui, "Could not save the sidebar", .tone = CUI_TONE_DANGER);
    }
    smenu_remove = cui_get_rect(ui, cui_make_id(ui, "Remove from Sidebar")); smenu_show = cui_get_rect(ui, cui_make_id(ui, "Show Hidden Places"));
    ctxmenu_end(ui);
}

/* ---- the header's width budget ----
 * The three window buttons never leave the window. In a wide window the path bar gives way first (down to PATH_PREF), then search
 * moves into the main menu. The view-options button (the view's icon and a chevron; it holds the grid and list choice) stays in the
 * header (a wide window is at least 443 px wide beside the sidebar). Back and Forward never go into the menu: GNOME Files 47 keeps
 * them in the header, and below its 682 px breakpoint (NARROW_BELOW) moves them, with the view controls, into a bar at the
 * window's bottom (ui_bottom_bar). A narrow header has the sidebar button, the path bar, search and the menu, and keeps search in
 * the header as GNOME does (nautilus-toolbar.ui): when it is tight, the path bar alone shrinks, below PATH_MIN too, to PATH_FLOOR. */
enum { PATH_PREF = 144, PATH_MIN = 116, PATH_FLOOR = 40, DRAG_MIN = 32, ICON_BTN_W = 36, VIEW_BTN_W = 66 };   /* VIEW_BTN_W: the icon, the chevron and the button padding */

/* The "Use as Default File Manager" item lives in the main menu. Its rectangles are kept for real-input checks. */
static struct { bool on, was_open; cui_rect button, item; } dfm_menu;

void ui_default_fm_snapshot(FILE *f) {
    fprintf(f, ",\"default_fm\":%d,\"menu_button\":[%.2f,%.2f,%.2f,%.2f],\"dfm_item\":[%.2f,%.2f,%.2f,%.2f]", app_default_fm(),
            dfm_menu.button.x, dfm_menu.button.y, dfm_menu.button.w, dfm_menu.button.h,
            dfm_menu.item.x, dfm_menu.item.y, dfm_menu.item.w, dfm_menu.item.h);
}

/* tier 0: everything in the header; 1: search in the menu (a wide window only: a narrow one is always tier 0). */
typedef struct header_plan { int tier; float path_w; bool menu_here, bottom; } header_plan;

static header_plan plan_header(void) {
    header_plan hp = { 0, 0, false, false };
    float vp = cui_viewport_size(A.ui).x;
    bool narrow = app_is_narrow();
    float avail = vp - A.sidebar_px;   /* the content pane: the whole window once the sidebar is gone */
    bool job = act_jobs_active();
    hp.menu_here = hp.bottom = narrow;
    int steps = narrow ? 1 : 2;
    for (int step = 0; step < steps; step++) {
        int right = narrow || step ? 1 : 2;   /* wide: search and view options, then view options alone; narrow: search */
        float right_w = (float)(narrow ? ICON_BTN_W : step ? VIEW_BTN_W : ICON_BTN_W + VIEW_BTN_W);
        int left = narrow ? 1 : 2;   /* the sidebar button, or Back and Forward */
        /* children of the header row: the left buttons, the path bar, a spacer, the job ring, the actions, the menu (narrow
         * only), a 4 px gap and the window buttons (84 px); every child is 4 px from the next and the row pads 8 px each side. */
        int kids = left + 1 + 1 + (job ? 1 : 0) + right + (narrow ? 1 : 0) + 1 + 1;
        float rest = 16 + 36.0f * left + (job ? 36 : 0) + right_w + (narrow ? 36 : 0) + 4 + 84 + 4.0f * (kids - 1) + 4;
        hp.tier = step;
        hp.path_w = avail - rest;
        if (hp.path_w >= (step < steps - 1 ? PATH_PREF : PATH_MIN)) break;
    }
    if (hp.path_w > DRAG_MIN + PATH_PREF) hp.path_w -= DRAG_MIN;   /* keep some empty header to drag the window by */
    if (hp.path_w < (narrow ? PATH_FLOOR : PATH_MIN)) hp.path_w = narrow ? (float)PATH_FLOOR : (float)PATH_MIN;
    return hp;
}

#define EXPAND_(x) x   /* MSVC passes __VA_ARGS__ on as one argument unless it is expanded once more */
#define ROW(label, ...) menu_row(label, EXPAND_(cui_menu_item(ui, label, __VA_ARGS__)))

static void main_menu(const header_plan *hp) {
    cui_ctx *ui = A.ui;
    bool search = hp->tier >= 1;   /* a narrow window is at tier 0: search stays in its header */
    cui_box(ui, .dir = CUI_STACK, .width = CUI_PX(36), .height = CUI_PX(36)) {
        bool menu_open = cui_menu_begin(ui, "Main Menu", &(cui_menu_opts){ .icon = CUI_ICON_MENU, .icon_only = true });
        dfm_menu.button = cui_last_rect(ui);
        if (menu_open) {
            if (!dfm_menu.was_open) dfm_menu.on = app_default_fm();   /* read again each time the menu opens */
            /* The menu grows when actions move into it: it scrolls where the window is too short for it, and follows the keyboard. */
            menu_rows_begin("menu-scroll");
            cui_column(ui, .id = "menu-scroll", .width = CUI_FILL, .max_height = cui_viewport_size(ui).y - HEADER_H - 24, .scroll_y = true) {
                if (search) {
                    if (ROW("Search Current Folder", .icon = CUI_ICON_SEARCH, .shortcut = "Ctrl F", .checked = A.search_open)) app_set_search_open(!A.search_open);
                }
                if (search) cui_menu_separator(ui);
                cui_menu_label(ui, "Theme");
                if (ROW("System", .icon = CUI_ICON_MONITOR, .checked = A.theme_pref == 0)) app_set_theme(0);
                if (ROW("Light", .icon = CUI_ICON_SUN, .checked = A.theme_pref == 1)) app_set_theme(1);
                if (ROW("Dark", .icon = CUI_ICON_MOON, .checked = A.theme_pref == 2)) app_set_theme(2);
                cui_menu_separator(ui);
                if (ROW("Show Hidden Files", .icon = CUI_ICON_EYE, .checked = A.show_hidden, .shortcut = "Ctrl H")) {
                    A.show_hidden = !A.show_hidden;
                    app_rebuild_view();
                }
                cui_menu_separator(ui);
                /* the blank shortcut keeps room for the switch */
                if (ROW("Use as Default File Manager", .icon = CUI_ICON_FOLDER_OPEN, .checked = dfm_menu.on)) {
                    app_toggle_default_fm();
                    dfm_menu.on = app_default_fm();
                }
                dfm_menu.item = cui_get_rect(ui, cui_make_id(ui, "Use as Default File Manager"));   /* a menu item does not set the last rect */
            }
            cui_menu_end(ui);
        }
        dfm_menu.was_open = menu_open;
        chrome_veil();
    }
}

/* ---- sidebar ---- */

void ui_sidebar_header(void) {
    cui_ctx *ui = A.ui;
    header_plan hp = plan_header();
    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(HEADER_H), .pad_x = 8, .gap = 4) {
        cui_spacer(ui);
        main_menu(&hp);
    }
}

/* The line between the sidebar and the content: a 6 px strip over it. Drag moves the line, a double click makes the sidebar 240 px again.
 * Called first inside the window's row (so the strip's position is the window's, and its press is taken before the file view's band selection looks at it). */
static cui_id edge_id;   /* the strip's id: the file view's band selection leaves a press the strip holds alone */
bool ui_sidebar_edge_held(void) { return edge_id && A.ui->active == edge_id; }

void ui_sidebar_edge(float window_w, float window_h) {
    cui_ctx *ui = A.ui;
    static float grab, press_x;
    static bool dragging;   /* a press that took the line to move it (a double click resets and moves nothing) */
    static bool moved;      /* the pointer left the press point: only then is the width set, so a click never changes the saved width */
    cui_id id = cui_make_id(ui, "sidebar-edge");
    edge_id = id;
    /* The hit test gives a press to the last control built over the point, and the Details table is built after this strip:
     * below the header the strip takes the press itself, so its content-side half resizes as the sidebar-side half does. */
    cui_vec2 m = cui_mouse_pos(ui);
    if (ui->mouse_inside && !ui->hot_layer && (!ui->active || ui->active == id) && m.y >= HEADER_H && fabsf(m.x - A.sidebar_px) <= 3.0f) ui->hot = id;
    cui_interact r = cui__interact(ui, id, CUI_IF_DRAG);
    if (r.active && cui_mouse_clicked(ui, CUI_MOUSE_LEFT)) { grab = cui_mouse_pos(ui).x - A.sidebar_px; press_x = cui_mouse_pos(ui).x; moved = false; dragging = !r.double_clicked; }   /* where on the strip it was taken */
    if (!cui_mouse_down(ui, CUI_MOUSE_LEFT)) dragging = false;
    float room = window_w - CONTENT_W_MIN;
    if (dragging && r.active && cui_mouse_pos(ui).x != press_x) moved = true;
    if (dragging && r.active && moved) {
        float w = cui_mouse_pos(ui).x - grab;
        if (w > room) w = room;   /* the line stays under the pointer: the content keeps its minimum */
        app_set_sidebar_w((int)floorf(w + 0.5f));
    }
    if (r.double_clicked) app_set_sidebar_w(SIDEBAR_W_DEFAULT);
    if (r.hovered || r.active) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
    float x = A.sidebar_px - 3.0f;
    int h = cui__absolute(ui, id, x, 0, 6, window_h);
    cui__mark_interactive(ui, h, CUI_IF_DRAG);
    ui->nodes[h].flags |= CUI_NF_ON_TOP;
}

/* Where the strip is, for the window's hit test: a press on it never drags the window (called after the headers, so their controls keep their order). */
void ui_sidebar_edge_nodrag(float window_h) { chrome_nodrag((cui_rect){ A.sidebar_px - 3.0f, 0, 6, window_h }); }

void ui_sidebar_slide_begin(float shown, float full, float height) {
    cui_ctx *ui = A.ui;
    int a = cui__absolute(ui, cui_make_id(ui, "sidebar-slide"), shown - full, 0, full, height);
    ui->nodes[a].dir = CUI_COLUMN;
    cui__open(ui, a);
}

void ui_sidebar_slide_end(void) { cui__close(A.ui); }

static bool place_row(const char *label, const char *icon, bool selected) {
    /* Each selected place is its own group, so its highlight appears in place instead of gliding from the one before. */
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
                if (A.loc == LOC_FOLDER && !_strnicmp(A.path, eject.root, 3)) app_watch_resume();   /* the drive stays: follow the folder again */
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
    cui_rect pane = cui_get_rect(ui, cui_make_id(ui, "places"));   /* last frame's */
    float name_w = (pane.w > 0 ? pane.w : 240.0f) - 70.0f;   /* the label's room: the pane less its padding, the row's, the icon and the gap */
    char key[8];
    int top = 0;
    sbm.hit = sbm.open_here = false;
    srow_n = 0;
    cui_column(ui, .id = "places", .width = CUI_FILL, .height = CUI_FILL, .pad_x = 8, .pad_bottom = 8, .gap = 2, .scroll_y = true) {
        if (!sidebar_is_hidden("recent")) {
            top++;
            if (place_row("Recent", CUI_ICON_CLOCK, A.loc == LOC_RECENT)) app_navigate("@1");
            row_rclick(cui_last_rect(ui), "recent", false);
        }
        if (!sidebar_is_hidden("starred")) {
            top++;
            if (place_row("Starred", CUI_ICON_STAR, A.loc == LOC_STARRED)) app_navigate("@2");
            row_rclick(cui_last_rect(ui), "starred", false);
        }
        for (int i = 0; i < 7; i++) {
            place_key(i, key);
            if (!A.places[i][0] || sidebar_is_hidden(key)) continue;
            top++;
            if (place_row(PLACE_LABEL[i], PLACE_ICON[i], app_at_place(i))) app_navigate(A.places[i]);
            row_rclick(cui_last_rect(ui), key, false);
            dnd_row(A.places[i]);
        }
        if (!sidebar_is_hidden("trash")) {
            top++;
            char tb[16];
            snprintf(tb, sizeof tb, "%d", A.trash_n);
            if (cui_nav_item(ui, "Trash", .icon = CUI_ICON_TRASH_2, .selected = A.loc == LOC_TRASH, .group = A.loc == LOC_TRASH ? "Trash" : "places", .badge = A.trash_n > 0 ? tb : NULL)) app_navigate("@3");
            row_rclick(cui_last_rect(ui), "trash", false);
            dnd_row("@3");
        }
        for (int i = 0; i < sidebar_count(); i++) {   /* the folders the user added, in the order added; a missing one is dimmed but still opens */
            const char *p = sidebar_path(i);
            char leaf[FS_PATH_MAX], label[UI_NAME_BYTES];
            bool gone = sidebar_gone(i);
            top++;
            sidebar_leaf(p, leaf, sizeof leaf);
            ui_fit_name(leaf, name_w, 14.0f, label);
            cui_push_id_int(ui, 1000 + i);
            cui_row(ui, .width = CUI_FILL, .gap = 0) {
                if (gone) ui_node_opacity(0.5f);
                if (place_row(label, CUI_ICON_FOLDER, A.loc == LOC_FOLDER && sidebar_same_path(A.path, p))) app_navigate(p);
                cui_tooltip(ui, p);
                row_rclick(cui_last_rect(ui), p, true);
                if (srow_n) srow[srow_n - 1].dim = gone;
                if (!gone) dnd_row(p);   /* a drop goes into the folder; a missing one takes none */
            }
            cui_pop_id(ui);
        }
        if (top) {
            cui_gap(ui, 6);
            cui_separator(ui);
            cui_gap(ui, 6);
        }
        for (int i = 0; i < A.drive_n; i++) {
            bool here = A.loc == LOC_FOLDER && same_path(A.path, A.drives[i].root);
            drive_key(&A.drives[i], key);
            if (sidebar_is_hidden(key)) continue;
            cui_push_id_int(ui, i);
            cui_row(ui, .width = CUI_FILL, .gap = 0) {
                cui_box(ui, .dir = CUI_STACK, .width = CUI_FILL) {
                    if (place_row(A.drives[i].name, CUI_ICON_SERVER, here)) app_navigate(A.drives[i].root);
                    row_rclick(cui_last_rect(ui), key, false);
                    dnd_row(A.drives[i].root);
                    if (A.drives[i].total) {   /* how full the drive is: a thin bar under the label, in the Other Locations colours */
                        char tip[64];
                        drive_usage_text(&A.drives[i], tip, sizeof tip);
                        cui_tooltip(ui, tip);
                        float used = (float)(A.drives[i].total - A.drives[i].free) / (float)A.drives[i].total;
                        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad_left = 36, .pad_right = 10, .pad_top = 26) {
                            cui_progress(ui, used, .height = 3, .tone = used > 0.9f ? CUI_TONE_DANGER : CUI_TONE_ACCENT);
                        }
                    }
                }
                if (A.drives[i].type == DRIVE_REMOVABLE) {
                    cui_box(ui, .dir = CUI_STACK, .width = CUI_PX(36), .height = CUI_PX(36)) {
                        bool clicked = cui_icon_button(ui, "", .tooltip = "Eject Drive", .disabled = eject.thread != NULL);
                        eject.buttons[i] = cui_last_rect(ui);
                        cui_canvas_result icon = cui_canvas(ui, "ejecticon", CUI_FILL, CUI_FILL, paint_eject, NULL, 0);
                        if ((clicked || icon.clicked) && !eject.thread) {
                            snprintf(eject.root, sizeof eject.root, "%s", A.drives[i].root);
                            if (A.loc == LOC_FOLDER && !_strnicmp(A.path, eject.root, 3)) watch_close(true);   /* the open folder's watch must not hold the drive */
                            eject.thread = CreateThread(NULL, 0, eject_proc, NULL, 0, NULL);
                            if (!eject.thread) cui_toast(ui, "Could not eject drive", .tone = CUI_TONE_DANGER);
                        }
                    }

                }
            }
            cui_pop_id(ui);
        }
        if (place_row("Other Locations", CUI_ICON_PLUS, A.loc == LOC_OTHER)) app_navigate("@4");
        row_rclick(cui_last_rect(ui), "", false);
    }
    if (!sbm.hit && sidebar_any_hidden() && sb_right_click(in_rect(pane, cui_mouse_pos(ui)))) { sbm.hit = sbm.open_here = true; sbm.key[0] = 0; }   /* empty space in the pane */
    row_menu();
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

static cui_rect view_mode_rect;   /* the grid/list toggles while the view-options menu is open (test state) */

void ui_view_mode_snapshot(FILE *f) {
    fprintf(f, ",\"view_mode\":[%.2f,%.2f,%.2f,%.2f]", view_mode_rect.x, view_mode_rect.y, view_mode_rect.w, view_mode_rect.h);
}

static void view_options_menu(cui_placement where) {
    cui_ctx *ui = A.ui;
    static const char *const KEYS[4] = { "Name", "Size", "Modified", "Type" };
    static const char *const names[Z_COUNT] = { "Details", "List", "Small icons", "Medium icons", "Large icons", "Extra large icons" };
    /* GNOME Files 43's view menu button: the current view's icon and a down chevron (the sliders icon looked like the main menu's settings). */
    bool open = ui_view_options_open();   /* the popover's id is outside the button's scope below */
    cui_push_id(ui, "view-options-button");
    bool clicked = cui_button(ui, "", .variant = CUI_BUTTON_GHOST, .icon = A.zoom <= Z_LIST ? CUI_ICON_LIST : CUI_ICON_GRID_3X3, .icon_right = CUI_ICON_CHEVRON_DOWN,
                              .tooltip = "View options", .selected = open, .color = chrome_icon_color());
    cui_pop_id(ui);
    if (clicked) cui_popover_toggle(ui, "view-options");
    chrome_nodrag_last();
    /* A menu popup, so the arrow keys move a highlight through its rows; the rows scroll in a window too short for all of them. */
    if (!popover_menu_begin(ui, "view-options", where, 300, 12, 4)) return;
    ui_view_options_surface();
    cui_column(ui, .id = "view-scroll", .width = CUI_FILL, .gap = 4, .max_height = cui_viewport_size(ui).y - 80, .scroll_y = true) {
    /* Grid or list: two linked toggles, the current one lit. A click switches the view at once and the menu stays open. */
    cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_CENTER) {
        static const char *const modes[2] = { "Grid view", "List view" };
        static const char *const mode_icons[2] = { CUI_ICON_GRID_3X3, CUI_ICON_LIST };
        bool on[2] = { A.zoom >= Z_SMALL, A.zoom <= Z_LIST };
        if (cui_toggle_group(ui, "viewmode", modes, 2, on, .icons = mode_icons, .exclusive = true, .icon_only = true)) {
            app_set_view(on[0] ? VIEW_GRID : VIEW_LIST);
            ui_menu_eat_activation();   /* a key that pressed a toggle must not also pick the sort row the arrow keys highlight */
        }
        view_mode_rect = cui_get_rect(ui, cui_make_id(ui, "viewmode"));
    }
    float value = A.zv;   /* continuous: the ticks only mark the six stops */
    if (cui_slider(ui, "View size", &value, 0, Z_COUNT - 1, .width = CUI_FILL)) app_set_zoomv(value, false);
    A.zoom_rect = cui_last_rect(ui);
    cui_canvas(ui, "zoom-ticks", CUI_FILL, CUI_PX(4), paint_zoom_ticks, NULL, 0);
    cui_text(ui, names[A.zoom], .width = CUI_FILL, .align = CUI_TEXT_CENTER, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
    cui_menu_separator(ui);
    cui_menu_label(ui, "Sort by");
    menu_rows_begin("view-scroll");
    for (int i = 0; i < 4; i++)
        if (ROW(KEYS[i], .checked = A.sort_key == i)) { A.sort_key = i; app_resort(); }
    cui_menu_separator(ui);
    if (ROW("Reversed Order", .checked = A.sort_desc)) { A.sort_desc = !A.sort_desc; app_resort(); }
    if (ROW("Show Hidden Files", .checked = A.show_hidden, .shortcut = "Ctrl H")) { A.show_hidden = !A.show_hidden; app_rebuild_view(); }
    }
    cui_popover_end(ui);
}

/* Back and Forward, in the header of a wide window and in the bottom bar of a narrow one. */
static void history_buttons(cui_color dim) {
    cui_ctx *ui = A.ui;
    if (cui_icon_button(ui, CUI_ICON_CHEVRON_LEFT, .tooltip = "Back", .shortcut = "Alt Left", .disabled = A.hist_pos <= 0, .color = dim)) app_back();
    chrome_nodrag_last();
    if (cui_icon_button(ui, CUI_ICON_CHEVRON_RIGHT, .tooltip = "Forward", .shortcut = "Alt Right", .disabled = A.hist_pos >= A.hist_n - 1, .color = dim)) app_forward();
    chrome_nodrag_last();
}

/* GNOME Files' action bar: below the narrow breakpoint, history on the left and the view options on the right. */
void ui_bottom_bar(void) {
    cui_ctx *ui = A.ui;
    cui_color dim = chrome_icon_color();
    cui_box(ui, .width = CUI_FILL, .height = CUI_PX(1), .bg = CUI_COLOR_BORDER_SUBTLE) {}
    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(BAR_H - 1), .pad_x = 8, .gap = 4, .align = CUI_ALIGN_CENTER) {
        history_buttons(dim);
        cui_spacer(ui);
        view_options_menu(CUI_PLACE_TOP_END);
    }
}

void ui_content_header(void) {
    cui_ctx *ui = A.ui;
    header_plan hp = plan_header();
    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(HEADER_H), .pad_x = 8, .gap = 4) {
        cui_color dim = chrome_icon_color();
        if (app_is_narrow()) {
            if (cui_icon_button(ui, CUI_ICON_PANEL_LEFT, .tooltip = "Sidebar", .color = dim)) A.sidebar_open = true;
            chrome_nodrag_last();
        }
        if (!hp.bottom) history_buttons(dim);
        bool home_first;
        int n = build_segments(&home_first);
        /* The path bar and the search field take the width between Forward and the actions, less some empty header to drag the
         * window by, and shrink first when the header is tight (plan_header keeps the window buttons inside the window). */
        cui_row(ui, .width = CUI_PX(hp.path_w), .gap = 4) {
        if (A.search_open) {   /* the search field takes the place of the path bar */
            if (A.search_focus) { ui_search_focus(); A.search_focus = false; }
            int f = cui_search(ui, "searchfield", A.search, sizeof A.search, .placeholder = "Search Current Folder", .width = CUI_FILL, .autofocus = true, .clearable = true);
            chrome_nodrag_last();
            if (f & CUI_CHANGED) fsearch_changed();
            if (fsearch_running()) cui_spinner(ui, 18);
        } else {
        const char *segs[32];
        for (int i = 0; i < n; i++) segs[i] = seg_label[i];
        cui_path_bar_result pr = cui_path_bar(ui, "pathbar", segs, n, .icon = home_first ? CUI_ICON_HOUSE : NULL, .width = CUI_PX(hp.path_w),
                                              .menus = A.loc == LOC_FOLDER, .no_edit = A.loc != LOC_FOLDER, .text = A.path);
        cui_rect bar = cui_last_rect(ui);   /* CUI reports the bar's full width, also the part its row cuts off */
        if (bar.w > hp.path_w) bar.w = hp.path_w;
        chrome_nodrag(bar);
        if (A.loc == LOC_FOLDER) {
            if (pr.clicked >= 0 && pr.clicked < n - 1) app_navigate(seg_path[pr.clicked]);
            else if (pr.clicked == n - 1) app_reload();
            if (pr.submitted && pr.submitted[0]) {
                if (fs_is_dir(pr.submitted)) app_navigate(pr.submitted);
                else {
                    char d[DLG_TOAST_DESC_CAP];   /* CUI keeps 255 bytes of a description */
                    dlg_path_fit(pr.submitted, d, sizeof d);
                    cui_toast(ui, "That folder does not exist", .description = d, .tone = CUI_TONE_WARNING);
                }
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
        }   /* the capped row */
        cui_spacer(ui);
        act_job_button();
        if (hp.tier < 1) {
            if (cui_icon_button(ui, CUI_ICON_SEARCH, .tooltip = "Search", .shortcut = "Ctrl F", .selected = A.search_open, .color = dim)) app_set_search_open(!A.search_open);
            chrome_nodrag_last();
        }
        if (!hp.bottom) view_options_menu(CUI_PLACE_BOTTOM_END);
        if (hp.menu_here) main_menu(&hp);
        chrome_window_buttons();
    }
}

void ui_search_bar(void) {}   /* the field lives in the content header now */

/* ---- states and the status pill ---- */

/* A whole view with nothing to show. Only an unreadable folder passes an action ("Try Again"): a normal button under the reason. */
bool ui_state_view(const char *icon, const char *title, const char *detail, const char *action) {
    cui_ctx *ui = A.ui;
    bool tight = A.content_h < 260;   /* a short window (360x280 leaves 188 px): a smaller picture, so the text still fits */
    bool clicked = false;
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER, .gap = 10, .pad = tight ? 8.0f : 24.0f) {
        if (!(tight && action)) cui_icon(ui, icon, tight ? 56.0f : 96.0f, cui_alpha(CUI_COLOR_TEXT_MUTED, 0.7f));   /* a short window: the button needs the room */
        cui_gap(ui, tight ? 0.0f : 6.0f);
        cui_text(ui, title, .font = CUI_FONT_MEDIUM, .size = 22, .align = CUI_TEXT_CENTER, .width = CUI_FILL);
        if (detail && detail[0]) cui_text(ui, detail, .color = CUI_COLOR_TEXT_SECONDARY, .align = CUI_TEXT_CENTER, .width = CUI_FILL);
        if (action) clicked = cui_button(ui, action, .icon = CUI_ICON_REFRESH_CW);
    }
    return clicked;
}

/* ---- loading placeholders ----
 * A listing that has run for 200 ms shows shapes in the form of the view that will replace them; a quicker one never flashes.
 * Windows' "Animation effects" off (or DFM_TEST_ANIMATIONS=off) leaves the shapes still: CUI's reduced motion holds the shine
 * for as long as they are on screen, and the next frame gives it back. */
#define LOAD_SHOW_S 0.2
static bool skel_still;

void ui_loading_frame_begin(void) {
    if (skel_still) { cui_set_reduced_motion(A.ui, false); skel_still = false; }
}

void ui_loading_view(void) {
    cui_ctx *ui = A.ui;
    if (cui_time(ui) - A.load_t < LOAD_SHOW_S) return;   /* main.c's frame already wakes every 20 ms while a listing runs */
    A.skel_shown = true; A.skel_frames++;
    if (!app_animations()) { cui_set_reduced_motion(ui, true); skel_still = true; }
    ui_loading_shapes(1);
}

/* The shapes in the form of the current view, at the given opacity (below 1: fading out under the items that replace them). */
void ui_loading_shapes(float alpha) {
    cui_box(A.ui, .dir = CUI_STACK, .width = CUI_FILL, .height = CUI_FILL) {
        if (alpha < 1) ui_node_opacity(alpha);
        if (A.zoom == Z_LIST) ui_columns_skeleton();
        else if (A.view == VIEW_GRID) ui_grid_skeleton();
        else ui_list_skeleton();
    }
}

static char pill_buf[400];
const char *ui_pill_text(void) { return pill_buf; }

/* ---- the notice and the selection label never meet ----
 * CUI keeps its toasts in the window's bottom-right corner, 16 px from the edges, and the status pill sits in the same corner.
 * GNOME Files keeps its toast (bottom centre) and its floating status label apart, so a notice never cuts the label; here the
 * toast cannot move, so the pill rises above the whole stack, including the stack opened by hovering it. This one place covers
 * every cui_toast of the app. */
#define TOAST_MARGIN 16.0f   /* CUI's own gap between a toast and the window edge */
#define TOAST_GAP 8.0f       /* CUI's gap between stacked toasts, and what the pill keeps above them */
#define PILL_PAD 14.0f

static float toast_lift;       /* the pill's rise above its resting place, eased */
static float pill_r[4], toast_r[4];

/* The height of the stack of toasts, as CUI lays it out opened. A toast on its way out still counts until CUI removes it, so the
 * pill never slides under one that is still fading. */
static float toast_stack_h(void) {
    cui_ctx *ui = A.ui;
    float h = 0; int n = 0;
    for (int i = 0; i < ui->toast_count; i++) {
        h += ui->toasts[i].height + (n ? TOAST_GAP : 0.0f);
        n++;
    }
    return h;
}

/* For the checks: where the toasts really are (x, y, w, h of all of them; zeros with none) and where the pill is. */
void ui_toast_snapshot(FILE *f) {
    fprintf(f, ",\"toast_rect\":[%.1f,%.1f,%.1f,%.1f],\"pill_rect\":[%.1f,%.1f,%.1f,%.1f],\"vw\":%.1f,\"vh\":%.1f",
            toast_r[0], toast_r[1], toast_r[2], toast_r[3], pill_r[0], pill_r[1], pill_r[2], pill_r[3], A.ui->vw, A.ui->vh);
}

static void measure_toasts(void) {
    cui_ctx *ui = A.ui;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    bool any = false;
    float W = ui->vw - 32 < 360.0f ? ui->vw - 32 : 360.0f;
    int vis = 0;
    for (int i = 0; i < ui->toast_count; i++) if (!ui->toasts[i].dismissed) vis++;
    float front_h = ui->toast_count ? ui->toasts[ui->toast_count - 1].height : 0;
    float H = front_h + 10.0f * (float)(vis > 3 ? 2 : (vis > 0 ? vis - 1 : 0));   /* CUI's collapsed stack */
    float top = ui->vh - TOAST_MARGIN - H;
    for (int i = 0; i < ui->toast_count; i++) {
        float y = top + ui->toasts[i].y.x, h = ui->toasts[i].height, x = ui->vw - W - TOAST_MARGIN;
        any = true;
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x + W > x1) x1 = x + W;
        if (y + h > y1) y1 = y + h;
    }
    toast_r[0] = any ? x0 : 0; toast_r[1] = any ? y0 : 0; toast_r[2] = any ? x1 - x0 : 0; toast_r[3] = any ? y1 - y0 : 0;
}

static float pill_text_w(const char *s) {
    cui_theme *th = cui_theme_get(A.ui);
    return cui_measure_text(A.ui, CUI_FONT_BODY, th->text_body * th->font_scale, s, (int)strlen(s)).width;
}

/* The label is one line and never wider than the pane: a long name gives way at whole characters until the whole label fits, the words
 * after the name never. (A narrow window used to push a long name's label past the window edge, its ending cut off.) */
static float pill_room(void) { return fminf(A.content_w - 2 * PILL_PAD, 520.0f) - 30.0f; }   /* the pane less its margins, at most the row's 520; 30: its padding and border */

static void pill_fit_leaf(const char *path, const char *tail, char *out, size_t cap) {
    dlg_quoted_leaf(path, tail, out, cap);
    if (pill_text_w(out) <= pill_room()) return;
    size_t lo = strlen(tail) + 6 + 1 + 4, hi = cap;   /* lo: the quotes, the tail and a name of one character; hi: too wide */
    if (lo >= hi) lo = hi - 1;
    while (hi - lo > 1) {   /* the widest cap that still fits */
        size_t mid = (lo + hi) / 2;
        dlg_quoted_leaf(path, tail, out, mid);
        if (pill_text_w(out) <= pill_room()) lo = mid; else hi = mid;
    }
    dlg_quoted_leaf(path, tail, out, lo);
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
            char tail[64] = " selected";   /* the name gives way at whole characters, never these words */
            if (!(e->flags & FS_DIR)) {
                char sz[48];
                fs_format_size(sz, sizeof sz, e->size);
                snprintf(tail, sizeof tail, " selected (%s)", sz);
            } else if (A.sel_pending) {
                snprintf(tail, sizeof tail, " selected (\xE2\x80\xA6)");   /* the folder is still being measured */
            } else if (A.sel_bytes) {
                char sz[48];
                fs_format_size(sz, sizeof sz, A.sel_bytes);
                snprintf(tail, sizeof tail, " selected (%s)", sz);
            }
            pill_fit_leaf(app_item_name(idx), tail, buf, sizeof buf);
        }
    } else if (A.sel_n > 1) {
        char sz[48];
        fs_format_size(sz, sizeof sz, A.sel_bytes);
        if (A.sel_pending) snprintf(buf, sizeof buf, A.sel_bytes ? "%d items selected (%s\xE2\x80\xA6)" : "%d items selected (\xE2\x80\xA6)", A.sel_n, sz);
        else if (A.sel_bytes) snprintf(buf, sizeof buf, "%d items selected (%s)", A.sel_n, sz);
        else snprintf(buf, sizeof buf, "%d items selected", A.sel_n);
    } else if (loading) {
        snprintf(buf, sizeof buf, "Loading\xE2\x80\xA6");
    }
    snprintf(pill_buf, sizeof pill_buf, "%s", buf);
    measure_toasts();
    /* The pill rests PILL_PAD above the content's bottom edge, which is the window's, or the bottom bar's top line when narrow. */
    float below = app_is_narrow() ? BAR_H : 0.0f, stack = toast_stack_h();
    float want = stack > 0 ? TOAST_MARGIN + stack + TOAST_GAP - below - PILL_PAD : 0.0f;
    cui__tween(ui, &toast_lift, want > 0 ? want : 0.0f, CUI__T_STATE);
    cui_theme *th = cui_theme_get(ui);
    float line = cui_measure_text(ui, CUI_FONT_BODY, th->text_body * th->font_scale, buf, (int)strlen(buf)).height, h = line + 18.0f;
    /* Many toasts in a short window can fill the pane: the label then waits (it never sits under a toast) and returns when they go. */
    if (!buf[0] || 2 * PILL_PAD + want + h > A.content_h) { pill_r[0] = pill_r[1] = pill_r[2] = pill_r[3] = 0; return; }
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_END, .justify = CUI_JUSTIFY_END, .pad = PILL_PAD) {
        cui_row(ui, .bg = CUI_COLOR_SURFACE_RAISED, .radius = 14, .border = 1, .border_color = CUI_COLOR_BORDER,
                .elevation = CUI_ELEVATION_FLOATING, .pad_x = 14, .pad_y = 8, .max_width = pill_room() + 30.0f) {
            cui_text(ui, buf, .nowrap = true);
        }
        if (toast_lift > 0.5f) cui_gap(ui, toast_lift);
        /* Where the pill is drawn, from its own parts (the row's rect is not kept): the text and 14 + 1 px / 8 + 1 px around it, in the
         * content pane's bottom-right corner, PILL_PAD in and the lift up. */
        float w = fminf(pill_text_w(buf) + 30.0f, pill_room() + 30.0f);
        pill_r[0] = ui->vw - PILL_PAD - w; pill_r[2] = w; pill_r[3] = h;
        pill_r[1] = ui->vh - below - PILL_PAD - (toast_lift > 0.5f ? toast_lift : 0.0f) - h;
    }
}
