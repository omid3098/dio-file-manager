/* The list view: CUI's data grid. Columns depend on the place: folders show Name, Size, Modified, Type;
 * Recent and Starred add Location; Trash shows Name, Original Location, Date Deleted, Size. The last column
 * of folders, Recent and Starred is the star toggle. */
#include <stdio.h>
#include <string.h>
#include "ui.h"

enum { C_NAME, C_LOC, C_SIZE, C_TIME, C_TYPE, C_STAR };

typedef struct colset { cui_grid_column cols[6]; int kind[6]; int n; } colset;

static int sort_key_of(int kind) {
    return kind == C_NAME ? FS_SORT_NAME : kind == C_SIZE ? FS_SORT_SIZE : kind == C_TIME ? FS_SORT_MODIFIED : kind == C_TYPE ? FS_SORT_TYPE : -1;
}

static void add_col(colset *s, int kind, const char *title, float w, cui_text_align al, int prio) {
    s->cols[s->n] = (cui_grid_column){ title, w, al, sort_key_of(kind) >= 0, kind == C_STAR, prio };
    s->kind[s->n++] = kind;
}

static void columns_for(colset *s) {
    memset(s, 0, sizeof *s);
    if (A.loc == LOC_TRASH) {
        add_col(s, C_NAME, "Name", 300, CUI_TEXT_LEFT, 0);
        add_col(s, C_LOC, "Original Location", 260, CUI_TEXT_LEFT, 1);
        add_col(s, C_TIME, "Date Deleted", 160, CUI_TEXT_LEFT, 0);
        add_col(s, C_SIZE, "Size", 100, CUI_TEXT_RIGHT, 2);
    } else if (A.loc == LOC_FOLDER && !A.searching) {
        add_col(s, C_NAME, "Name", 380, CUI_TEXT_LEFT, 0);
        add_col(s, C_SIZE, "Size", 100, CUI_TEXT_RIGHT, 1);
        add_col(s, C_TIME, "Modified", 140, CUI_TEXT_LEFT, 2);
        add_col(s, C_TYPE, "Type", 120, CUI_TEXT_LEFT, 3);
        add_col(s, C_STAR, "", 44, CUI_TEXT_CENTER, 0);
    } else {
        add_col(s, C_NAME, "Name", 300, CUI_TEXT_LEFT, 0);
        add_col(s, C_LOC, "Location", 230, CUI_TEXT_LEFT, 2);
        add_col(s, C_SIZE, "Size", 100, CUI_TEXT_RIGHT, 1);
        add_col(s, C_TIME, A.loc == LOC_RECENT ? "Opened" : "Modified", 140, CUI_TEXT_LEFT, 0);
        add_col(s, C_TYPE, "Type", 110, CUI_TEXT_LEFT, 3);
        add_col(s, C_STAR, "", 44, CUI_TEXT_CENTER, 0);
    }
}

typedef struct star_paint { bool starred, show, hot; } star_paint;

static void paint_star(cui_ctx *ui, cui_rect r, void *user) {
    const star_paint *p = user;
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    if (p->starred) ui_draw_star(ui, cx, cy, 8.5f, CUI_COLOR_WARNING);
    else if (p->show) cui_draw_icon(ui, CUI_ICON_STAR, (cui_rect){ cx - 8, cy - 8, 16, 16 }, 16, p->hot ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);
}

static bool star_hot_now, star_hot_prev;   /* the pointer was over a star button: a click there is not a double click */

void ui_list_view(void) {
    cui_ctx *ui = A.ui;
    colset cs;
    columns_for(&cs);
    int cur = 0;
    for (int i = 0; i < cs.n; i++) if (sort_key_of(cs.kind[i]) == A.sort_key) cur = i;
    cui_table_sort sort = { cur, A.sort_desc };
    int first, last, star_click = -1;
    star_hot_prev = star_hot_now; star_hot_now = false;
    if (A.reset_scroll) { cui_scroll_to(ui, "list", 0); A.reset_scroll = false; }
    if (cui_data_grid_begin(ui, "list", cs.cols, cs.n, A.vis_n, &first, &last, .row_height = 36, .selected = A.sel,
                            .hide_checkboxes = true, .sort = &sort, .cursor = &A.cursor, .anchor = &A.anchor)) {
        A.first_row = first; A.last_row = last;
        for (int r = first; r < last; r++) {
            const fs_entry *e = app_item(r);
            if (cui_data_grid_row(ui, r)) {
                double now = cui_time(ui);
                if (r == A.last_click_idx && now - A.last_click_t < 0.45 && !star_hot_prev && !(cui_key_mods(ui) & (CUI_MOD_SHIFT | CUI_MOD_CTRL))) {
                    A.last_click_idx = -1;
                    app_open_vis(r);
                    break;
                }
                A.last_click_idx = r;
                A.last_click_t = now;
            }
            bool row_hot = cui_last_hovered(ui);
            if (row_hot && cui_mouse_clicked(ui, CUI_MOUSE_RIGHT)) { if (!A.sel[r]) app_select_only(r); A.cursor = r; A.ctx_item = true; }
            cui_push_id_int(ui, r);
            cui_pop_id(ui);
            for (int c = 0; c < cs.n; c++) {
                char buf[FS_PATH_MAX];
                switch (cs.kind[c]) {
                case C_NAME:
                    cui_data_grid_cell(ui);
                    cui_icon(ui, ui_kind_icon(e->kind), 16, ui_kind_color(e->kind));
                    cui_text(ui, app_item_name(r), .nowrap = true, .width = CUI_FILL);
                    break;
                case C_LOC:
                    view_location_text(r, buf, sizeof buf);
                    cui_data_grid_cell_text(ui, buf);
                    break;
                case C_SIZE:
                    if (e->flags & FS_DIR) cui_data_grid_cell_text(ui, "");
                    else { fs_format_size(buf, sizeof buf, e->size); cui_data_grid_cell_text(ui, buf); }
                    break;
                case C_TIME:
                    fs_format_time(buf, sizeof buf, e->mtime);
                    cui_data_grid_cell_text(ui, buf);
                    break;
                case C_TYPE:
                    cui_data_grid_cell_text(ui, fs_kind_name(e->kind));
                    break;
                case C_STAR: {
                    cui_data_grid_cell(ui);
                    cui_push_id_int(ui, r);
                    star_paint sp = { app_is_starred(r), row_hot || A.sel[r], false };
                    cui_canvas_result sc = cui_canvas(ui, "star", CUI_PX(30), CUI_PX(30), paint_star, &sp, sizeof sp);
                    if (sc.hovered) star_hot_now = true;
                    if (sc.clicked) star_click = r;
                    cui_pop_id(ui);
                    break;
                }
                }
            }
        }
        cui_data_grid_end(ui);
    }
    if (star_click >= 0) app_star_toggle_row(star_click);
    int nk = sort_key_of(cs.kind[sort.column < cs.n ? sort.column : 0]);
    if (nk >= 0 && (nk != A.sort_key || sort.descending != A.sort_desc)) {
        A.sort_key = nk;
        A.sort_desc = sort.descending;
        app_resort();
    }
}
