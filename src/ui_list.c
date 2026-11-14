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
        add_col(s, C_LOC, "Location", A.searching ? 200.0f : 230.0f, CUI_TEXT_LEFT, 2);
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
    A.cols = 1; A.cell_h = 36;
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
        ui_band_begin();
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
            bool row_hot = ctxmenu_row_hovered(ui);
            if (r == first) A.first_rect = ctxmenu_row_rect(ui);
            if (row_hot && cui_mouse_clicked(ui, CUI_MOUSE_RIGHT)) { if (!A.sel[r]) app_select_only(r); A.cursor = r; A.ctx_item = true; }
            for (int c = 0; c < cs.n; c++) {
                char buf[FS_PATH_MAX];
                switch (cs.kind[c]) {
                case C_NAME:
                    cui_data_grid_cell(ui);
                    ui_set_dim(e->flags & FS_INCOMING);
                    cui_icon(ui, ui_kind_icon(e->kind), 16, ui_dimmed(ui, ui_icon_ink(ui, ui_kind_color(e->kind), 4.5f)));
                    cui_text(ui, app_item_name(r), .nowrap = true, .width = CUI_FILL, .color = ui_dimmed(ui, CUI_COLOR_FOREGROUND));
                    ui_set_dim(false);
                    break;
                case C_LOC:
                    if (A.searching) search_location_text(r, buf, sizeof buf); else view_location_text(r, buf, sizeof buf);
                    cui_data_grid_cell_text(ui, buf);
                    break;
                case C_SIZE:
                    if (e->flags & FS_DIR) {
                        int sk = sizes_cell(e, app_item_name(r), buf, sizeof buf);
                        if (sk == 1) cui_data_grid_cell_text(ui, buf);
                        else if (sk == 2) { cui_data_grid_cell(ui); cui_text(ui, "\xE2\x80\xA6", .color = CUI_COLOR_TEXT_MUTED, .align = CUI_TEXT_RIGHT, .width = CUI_FILL); }
                        else cui_data_grid_cell_text(ui, "");
                    } else if (e->flags & FS_INCOMING) cui_data_grid_cell_text(ui, "Copying...");
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

/* Loading placeholders for Details: the frame and header of the real grid, then rows of 36 px with a square and a bar in the
 * Name column and a bar in each other column. The columns are those of ui_list_view, and drop by priority as its grid does. */
void ui_list_skeleton(void) {
    cui_ctx *ui = A.ui;
    colset cs;
    columns_for(&cs);
    bool hide[6] = { false };
    float total = 0;
    for (int i = 0; i < cs.n; i++) total += cs.cols[i].width;
    for (int shown = cs.n; total > A.content_w - 2 && shown > 1; shown--) {
        int h = -1;
        for (int i = 0; i < cs.n; i++)
            if (!hide[i] && cs.cols[i].priority > 0 && (h < 0 || cs.cols[i].priority >= cs.cols[h].priority)) h = i;
        if (h < 0) break;
        hide[h] = true;
        total -= cs.cols[h].width;
    }
    int rows = (int)ceilf((A.content_h - 40) / 36.0f);
    if (rows > 40) rows = 40;
    float sm = cui_theme_get(ui)->text_sm;
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .border = 1, .radius = cui_theme_get(ui)->radius_control, .clip = true, .gap = 0) {
        cui_row(ui, .width = CUI_FILL, .height = CUI_PX(40), .bg = CUI_COLOR_SURFACE_MUTED, .align = CUI_ALIGN_CENTER) {
            for (int c = 0; c < cs.n; c++) {
                if (hide[c]) continue;
                bool last = cs.kind[c] == C_STAR;
                cui_row(ui, .width = last ? CUI_FILL : CUI_PX(cs.cols[c].width), .height = CUI_FILL, .align = CUI_ALIGN_CENTER, .pad_x = 12) {
                    if (cs.cols[c].title[0])
                        cui_text(ui, cs.cols[c].title, .font = CUI_FONT_MEDIUM, .size = sm, .color = CUI_COLOR_TEXT_SECONDARY, .nowrap = true,
                                 .align = cs.cols[c].align, .width = CUI_FILL);
                }
            }
        }
        cui_box(ui, .width = CUI_FILL, .height = CUI_PX(1), .bg = CUI_COLOR_BORDER) {}
        cui_skeleton_group(ui) {
            cui_column(ui, .width = CUI_FILL, .gap = 0) {
                for (int r = 0; r < rows; r++) {
                    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(36), .align = CUI_ALIGN_CENTER) {
                        for (int c = 0; c < cs.n; c++) {
                            if (hide[c]) continue;
                            float w = cs.cols[c].width, pct = 0.5f + 0.01f * (float)((r * 7) % 21);
                            if (cs.kind[c] == C_STAR) { cui_gap(ui, 0); continue; }
                            cui_row(ui, .width = CUI_PX(w), .height = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8, .pad_x = 12,
                                    .justify = cs.cols[c].align == CUI_TEXT_RIGHT ? CUI_JUSTIFY_END : CUI_JUSTIFY_START) {
                                if (cs.kind[c] == C_NAME) {
                                    cui_skeleton(ui, CUI_PX(16), 16);
                                    cui_skeleton(ui, CUI_PX((w - 24 - 24) * pct), 10);
                                } else {
                                    float bar = cs.kind[c] == C_SIZE ? 40 : (w - 24) * (0.4f + 0.01f * (float)((r * 5) % 31));
                                    cui_skeleton(ui, CUI_PX(bar), 10);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
