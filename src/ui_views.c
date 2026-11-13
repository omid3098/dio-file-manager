/* The Starred, Trash and Other Locations parts of the window: stars, the Trash bar and its dialogs, the drive list. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "views.h"
#include "starred.h"

static const float PI_F = 3.14159265f;

/* ---- stars ---- */

void ui_draw_star(cui_ctx *ui, float cx, float cy, float r, cui_color c) {
    cui_vec2 o[5], in[5];
    for (int i = 0; i < 5; i++) {
        float a = -PI_F / 2 + (float)i * 2 * PI_F / 5, b = a + PI_F / 5;
        o[i] = (cui_vec2){ cx + r * cosf(a), cy + r * sinf(a) };
        in[i] = (cui_vec2){ cx + r * 0.42f * cosf(b), cy + r * 0.42f * sinf(b) };
    }
    cui_draw_polygon(ui, in, 5, c);
    for (int i = 0; i < 5; i++) {
        cui_vec2 t[3] = { in[(i + 4) % 5], o[i], in[i] };
        cui_draw_polygon(ui, t, 3, c);
    }
}

bool app_is_starred(int vis) {
    char p[FS_PATH_MAX];
    app_path_of(vis, p, sizeof p);
    return starred_is(p);
}

static void after_star_change(void) {
    if (A.loc == LOC_STARRED) app_reload();
}

static void warn_save(bool ok) {
    if (!ok) cui_toast(A.ui, "Could not save the starred list", .tone = CUI_TONE_DANGER);
}

void app_star_toggle_row(int vis) {
    if (vis < 0 || vis >= A.vis_n) return;
    char p[FS_PATH_MAX];
    app_path_of(vis, p, sizeof p);
    warn_save(starred_set(p, !starred_is(p)));
    after_star_change();
}

/* Star the selection, or unstar it when every selected item is starred already. */
void app_star_toggle_selection(void) {
    bool all = A.sel_n > 0;
    char p[FS_PATH_MAX];
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i]) { app_path_of(i, p, sizeof p); if (!starred_is(p)) all = false; }
    bool ok = true;
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i]) { app_path_of(i, p, sizeof p); ok &= starred_set(p, !all); }
    warn_save(ok);
    after_star_change();
}

bool app_selection_all_starred(void) {
    if (A.sel_n == 0) return false;
    char p[FS_PATH_MAX];
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i]) { app_path_of(i, p, sizeof p); if (!starred_is(p)) return false; }
    return true;
}

/* The folder an item lives in, for the Location columns: Home is shortened like in the path bar. */
void view_location_text(int vis, char *out, size_t cap) {
    char p[FS_PATH_MAX], d[FS_PATH_MAX];
    app_path_of(vis, p, sizeof p);
    if (!fs_parent(p, d, sizeof d)) snprintf(d, sizeof d, "%s", p);
    size_t hl = A.places[0][0] ? strlen(A.places[0]) : 0;
    if (hl && _strnicmp(d, A.places[0], hl) == 0 && (d[hl] == 0 || d[hl] == '\\')) snprintf(out, cap, "Home%s", d + hl);
    else snprintf(out, cap, "%s", d);
}

/* Open Item Location: the folder that holds the item, with the item selected. */
void views_open_location(void) {
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i]) {
        char p[FS_PATH_MAX], d[FS_PATH_MAX];
        app_path_of(i, p, sizeof p);
        if (!fs_parent(p, d, sizeof d)) return;
        const char *leaf = strrchr(p, '\\');
        act_reselect(leaf ? leaf + 1 : p);
        app_navigate(d);
        return;
    }
}

/* ---- the Trash ---- */

static struct { bool delete_open, empty_open; char title[320]; char **ids; size_t n; } V;

static void toast_hr(const char *title, long hr) {
    wchar_t msg[256] = L"";
    DWORD code = (DWORD)hr;
    if ((code >> 16) == 0x8007) code &= 0xFFFF;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, msg, 255, NULL);
    while (n && (msg[n - 1] == L'\r' || msg[n - 1] == L'\n' || msg[n - 1] == L' ' || msg[n - 1] == L'.')) msg[--n] = 0;
    char m[512];
    if (!n || !fs_w_to_u8(msg, m, sizeof m)) snprintf(m, sizeof m, "Error 0x%08lx", (unsigned long)hr);
    cui_toast(A.ui, title, .description = m, .tone = CUI_TONE_DANGER, .duration = 7);
}

static char **selected_ids(size_t *n) {
    char **p = calloc((size_t)A.sel_n + 1, sizeof *p);
    size_t k = 0;
    for (int i = 0; p && i < A.vis_n; i++) if (A.sel[i]) {
        const char *id = fs_extra(A.list, app_item(i)->id_off);
        if (id[0]) p[k++] = _strdup(id);
    }
    *n = k;
    return p;
}

static void free_ids(char **p, size_t n) {
    if (!p) return;
    for (size_t i = 0; i < n; i++) free(p[i]);
    free(p);
}

void views_restore_selection(void) {
    if (A.loc != LOC_TRASH || A.sel_n == 0 || views_task_busy()) return;
    size_t n;
    char **ids = selected_ids(&n);
    if (ids && n) views_task_start(TK_RESTORE, ids, n);
    free_ids(ids, n);
}

void views_ask_delete(void) {
    if (A.loc != LOC_TRASH || A.sel_n == 0 || views_task_busy()) return;
    free_ids(V.ids, V.n);
    V.ids = selected_ids(&V.n);
    if (V.n == 1) {
        int i = -1;
        for (int k = 0; k < A.vis_n; k++) if (A.sel[k]) { i = k; break; }
        snprintf(V.title, sizeof V.title, "Delete \xE2\x80\x9C%s\xE2\x80\x9D Permanently?", i >= 0 ? app_item_name(i) : "");
    } else snprintf(V.title, sizeof V.title, "Delete %d Items Permanently?", (int)V.n);
    V.delete_open = true;
}

void views_ask_empty(void) {
    if (A.loc == LOC_TRASH && !views_task_busy()) V.empty_open = true;
}

void ui_trash_bar(void) {
    cui_ctx *ui = A.ui;
    bool busy = views_task_busy();
    bool loaded = A.list && fs_list_state(A.list) == FS_DONE;
    cui_row(ui, .width = CUI_FILL, .pad_x = 16, .pad_y = 8, .gap = 8, .align = CUI_ALIGN_CENTER) {
        char t[64] = "";
        if (loaded) snprintf(t, sizeof t, A.vis_n == 1 ? "1 item" : "%d items", A.vis_n);
        cui_text(ui, t, .color = CUI_COLOR_TEXT_SECONDARY, .width = CUI_FILL);
        if (cui_button(ui, "Restore", .size = CUI_SIZE_SM, .disabled = A.sel_n == 0 || busy)) views_restore_selection();
        if (cui_button(ui, "Empty Trash\xE2\x80\xA6", .size = CUI_SIZE_SM, .variant = CUI_BUTTON_DANGER, .disabled = !loaded || A.list->count == 0 || busy)) views_ask_empty();
    }
    cui_separator(ui);
}

void views_trash_menu(void) {
    cui_ctx *ui = A.ui;
    bool busy = views_task_busy();
    if (A.ctx_item && A.sel_n > 0) {
        if (cui_menu_item(ui, "Restore", .icon = CUI_ICON_UNDO_2, .disabled = busy)) views_restore_selection();
        cui_menu_separator(ui);
        if (cui_menu_item(ui, "Delete Permanently", .icon = CUI_ICON_TRASH_2, .shortcut = "Delete", .disabled = busy)) views_ask_delete();
    } else {
        if (cui_menu_item(ui, "Select All", .icon = CUI_ICON_CHECK_CHECK, .shortcut = "Ctrl A", .disabled = A.vis_n == 0)) app_select_all();
        if (cui_menu_item(ui, "Empty Trash\xE2\x80\xA6", .icon = CUI_ICON_TRASH_2, .disabled = A.vis_n == 0 || busy)) views_ask_empty();
    }
}

/* Inside the content stack, every frame: the finished task, the dialogs, the sidebar icon. */
void views_frame(void) {
    cui_ctx *ui = A.ui;
    int kind;
    long hr;
    if (views_task_take(&kind, &hr)) {
        if (FAILED(hr)) toast_hr(kind == TK_RESTORE ? "Could not restore" : kind == TK_EMPTY ? "Could not empty the Trash" : "Could not delete", hr);
        A.trash_check_t = -100;
        if (A.loc == LOC_TRASH) app_reload();
    }
    if (views_task_busy()) cui_request_wakeup(ui, 0.05f);
    if (cui_time(ui) - A.trash_check_t > 2.0) {
        A.trash_n = (int)views_trash_count();
        A.trash_check_t = cui_time(ui);
        cui_request_wakeup(ui, 2.5f);
    }
    if (views_trash_count_poll(&A.trash_n)) cui_request_wakeup(ui, 0.05f);
    if (V.delete_open) {
        bool open = true;
        if (cui_dialog_begin(ui, V.title, &open, &(cui_dialog_opts){ .description = "If you delete an item, it will be permanently lost.", .width = 440 })) {
            cui_dialog_footer_begin(ui);
            if (cui_button(ui, "Cancel")) open = false;
            if (cui_button(ui, "Delete", .variant = CUI_BUTTON_DANGER)) {
                views_task_start(TK_DELETE, V.ids, V.n);
                open = false;
            }
            cui_dialog_footer_end(ui);
            cui_dialog_end(ui);
        }
        if (!open) { V.delete_open = false; free_ids(V.ids, V.n); V.ids = NULL; V.n = 0; }
    }
    if (V.empty_open) {
        bool open = true;
        if (cui_dialog_begin(ui, "Empty All Items from Trash?", &open, &(cui_dialog_opts){ .description = "All items in the Trash will be permanently deleted.", .width = 440 })) {
            cui_dialog_footer_begin(ui);
            if (cui_button(ui, "Cancel")) open = false;
            if (cui_button(ui, "Empty", .variant = CUI_BUTTON_DANGER)) { views_task_start(TK_EMPTY, NULL, 0); open = false; }
            cui_dialog_footer_end(ui);
            cui_dialog_end(ui);
        }
        if (!open) V.empty_open = false;
    }
}

bool views_modal_open(void) { return V.delete_open || V.empty_open; }
int views_state(void) { return (V.delete_open ? 1 : 0) | (V.empty_open ? 2 : 0) | (views_task_busy() ? 4 : 0); }

/* ---- Other Locations ---- */

static int hover_drive = -1;

static void paint_drive(cui_ctx *ui, cui_rect r, void *user) {
    int i = *(int *)user;
    const fs_drive *d = &A.drives[i];
    cui_draw_rect_border(ui, r, CUI_CORNERS(14), CUI_COLOR_SURFACE_RAISED, CUI_COLOR_BORDER_SUBTLE, 1);
    if (hover_drive == i) cui_draw_rect(ui, r, CUI_CORNERS(14), CUI_COLOR_HOVER);
    cui_draw_icon(ui, CUI_ICON_SERVER, (cui_rect){ r.x + 18, r.y + (r.h - 32) * 0.5f, 32, 32 }, 32, CUI_COLOR_ACCENT);
    float tx = r.x + 68;
    cui_draw_text(ui, CUI_FONT_MEDIUM, 14, (cui_vec2){ tx, r.y + 12 }, CUI_COLOR_FOREGROUND, d->name, -1);
    if (d->total) {
        float bw = r.w - 68 - 20;
        if (bw > 420) bw = 420;
        float used = (float)((double)(d->total - d->free) / (double)d->total);
        cui_rect track = { tx, r.y + 38, bw, 6 };
        cui_draw_rect(ui, track, CUI_CORNERS(3), CUI_COLOR_BORDER_STRONG);
        if (used > 0.004f) cui_draw_rect(ui, (cui_rect){ tx, track.y, bw * used, 6 }, CUI_CORNERS(3), used > 0.9f ? CUI_COLOR_DANGER : CUI_COLOR_ACCENT);
        char f[48], t[48], line[120];
        fs_format_size(f, sizeof f, d->free);
        fs_format_size(t, sizeof t, d->total);
        snprintf(line, sizeof line, "%s free of %s", f, t);
        cui_draw_text(ui, CUI_FONT_BODY, 12, (cui_vec2){ tx, r.y + 50 }, CUI_COLOR_TEXT_SECONDARY, line, -1);
    } else {
        const char *kind = d->type == DRIVE_CDROM ? "Optical drive" : d->type == DRIVE_REMOTE ? "Network location" : "Not available";
        cui_draw_text(ui, CUI_FONT_BODY, 12, (cui_vec2){ tx, r.y + 36 }, CUI_COLOR_TEXT_SECONDARY, kind, -1);
    }
}

void ui_other_locations(void) {
    cui_ctx *ui = A.ui;
    hover_drive = -1;
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 24, .scroll_y = true, .align = CUI_ALIGN_CENTER) {
        cui_column(ui, .width = CUI_FILL, .max_width = 720, .gap = 8) {
            cui_text(ui, "Devices", .font = CUI_FONT_MEDIUM, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_gap(ui, 2);
            for (int i = 0; i < A.drive_n; i++) {
                cui_push_id_int(ui, i);
                cui_canvas_result c = cui_canvas(ui, "drive", CUI_FILL, CUI_PX(76), paint_drive, &i, sizeof i);
                if (c.hovered) hover_drive = i;
                if (c.clicked) app_navigate(A.drives[i].root);
                cui_pop_id(ui);
            }
        }
    }
}
