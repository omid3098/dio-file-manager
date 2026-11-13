/* The context menu at the pointer, with a width of its own. CUI's cui_context_menu_begin takes the width of the
 * element it opens on, so in the list view and in an empty folder it spanned the whole view. This is the same
 * popup, opened at the pointer with no anchor element, so it is as narrow as the menu in the grid view.
 * It uses CUI's internal popup calls (CUI is pinned to v1.4.0); it is kept in a file of its own for that. */
#include <cui_internal.h>
#include "ui.h"

static cui_id menu_pid;   /* the id made inside the frame's id scope; ctxmenu_is_open runs outside it */

bool ctxmenu_row_hovered(cui_ctx *ui) {
    /* CUI 1.4.0's data-grid row does not set last_id; use its clipped hit rectangle. */
    cui_id row = ui->nodes[cui__current(ui)].id;
    for (int i = ui->prev_hit_count - 1; i >= 0; i--) {
        const cui_hit *h = &ui->prev_hits[i];
        if (h->id == row && h->layer == ui->hot_layer)
            return cui__rect_contains(h->rect, ui->mx, ui->my) && ui->my >= 46.0f + 40.0f;
    }
    return false;
}

bool ctxmenu_begin(cui_ctx *ui, const char *id, bool open_here) {
    cui_id pid = cui_make_id(ui, id) ^ 0xC7Cu;
    menu_pid = pid;
    if (open_here) {
        cui__close_all_popups(ui);
        cui__popup_set(ui, pid, true);
        cui_popup *p = cui__popup_get(ui, pid, false);
        if (p) { p->at_mouse = true; p->mouse = cui_mouse_pos(ui); }
    }
    if (!cui__popup_get(ui, pid, false)) return false;
    return cui__popup_begin(ui, pid, 0, CUI_PLACE_BOTTOM_START, 0, 6, 0, true);
}

void ctxmenu_end(cui_ctx *ui) { cui__popup_end(ui); }

bool ctxmenu_is_open(cui_ctx *ui) {
    cui_popup *p = menu_pid ? cui__popup_get(ui, menu_pid, false) : NULL;
    return p && p->open;
}
