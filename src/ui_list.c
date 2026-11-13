/* The list view: CUI's data grid with the columns Name, Size, Modified, Type. */
#include <stdio.h>
#include "ui.h"

void ui_list_view(void) {
    cui_ctx *ui = A.ui;
    static const cui_grid_column cols[] = {
        { "Name", 380, CUI_TEXT_LEFT, true },
        { "Size", 100, CUI_TEXT_RIGHT, true, false, 1 },
        { "Modified", 140, CUI_TEXT_LEFT, true, false, 2 },
        { "Type", 120, CUI_TEXT_LEFT, true, false, 3 },
    };
    cui_table_sort sort = { A.sort_key, A.sort_desc };
    int first, last;
    if (A.reset_scroll) { cui_scroll_to(ui, "list", 0); A.reset_scroll = false; }
    if (cui_data_grid_begin(ui, "list", cols, 4, A.vis_n, &first, &last, .row_height = 36, .selected = A.sel,
                            .hide_checkboxes = true, .sort = &sort, .cursor = &A.cursor, .anchor = &A.anchor)) {
        A.first_row = first; A.last_row = last;
        for (int r = first; r < last; r++) {
            const fs_entry *e = app_item(r);
            if (cui_data_grid_row(ui, r)) {
                double now = cui_time(ui);
                if (r == A.last_click_idx && now - A.last_click_t < 0.45 && !(cui_key_mods(ui) & (CUI_MOD_SHIFT | CUI_MOD_CTRL))) {
                    A.last_click_idx = -1;
                    app_open_vis(r);
                    break;
                }
                A.last_click_idx = r;
                A.last_click_t = now;
            }
            if (cui_last_hovered(ui) && cui_mouse_clicked(ui, CUI_MOUSE_RIGHT)) { if (!A.sel[r]) app_select_only(r); A.cursor = r; A.ctx_item = true; }
            cui_push_id_int(ui, r);
            act_context_menu();
            cui_pop_id(ui);
            cui_data_grid_cell(ui);
            cui_icon(ui, ui_kind_icon(e->kind), 16, ui_kind_color(e->kind));
            cui_text(ui, app_item_name(r), .nowrap = true, .width = CUI_FILL);
            char buf[48];
            if (e->flags & FS_DIR) cui_data_grid_cell_text(ui, "");
            else { fs_format_size(buf, sizeof buf, e->size); cui_data_grid_cell_text(ui, buf); }
            fs_format_time(buf, sizeof buf, e->mtime);
            cui_data_grid_cell_text(ui, buf);
            cui_data_grid_cell_text(ui, fs_kind_name(e->kind));
        }
        cui_data_grid_end(ui);
    }
    if (sort.column != A.sort_key || sort.descending != A.sort_desc) {
        A.sort_key = sort.column;
        A.sort_desc = sort.descending;
        app_resort();
    }
}
