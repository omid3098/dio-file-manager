/* The context menu at the pointer, with a width of its own. CUI's cui_context_menu_begin takes the width of the
 * element it opens on, so in the list view and in an empty folder it spanned the whole view. This is the same
 * popup, opened at the pointer with no anchor element, so it is as narrow as the menu in the grid view.
 * It uses CUI's internal popup calls (CUI is pinned to v1.4.0); it is kept in a file of its own for that. */
#include <cui_internal.h>
#include <platform/cui_platform.h>
#include <stdlib.h>
#include <math.h>
#include "ui.h"

/* CUI 1.4.0 exposes callbacks only through create-and-run. Keep our screenshot
 * loop, but give its native modal resize loop the same frame callback. */
void ui_window_callback(cui_app *app, void (*frame)(cui_ctx *, void *), void *user) {
    app->frame_cb = frame;
    app->frame_user = user;
}

bool ui_window_screenshot(cui_app *app, const char *path) {
    /* Finish layout before sizing the buffer: a resize changes the draw size. */
    cui__app_render(app);
    const cui_draw_data *dd = cui_get_draw_data(app->ui);
    int w = dd->width_px, h = dd->height_px;
    unsigned char *px = malloc((size_t)w * (size_t)h * 4);
    bool ok = px && cui_app_screenshot(app, px, w, h) && cui_write_png(path, px, w, h, w * 4);
    free(px);
    return ok;
}

cui_rect ctxmenu_row_rect(cui_ctx *ui) {
    return cui__prev_rect(ui, ui->nodes[cui__current(ui)].id);
}

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

/* Keep the pinned CUI internals here: consume zoom before a scroll area sees it. */
float ui_zoom_wheel(cui_ctx *ui) {
    if (ui->hot_layer != 0) return 0;
    float dy = ui->scroll_dy;
    ui->scroll_dy = 0;
    return dy;
}

void ui_columns_begin(int *rows, int *first, int *last) {
    cui_ctx *ui = A.ui;
    cui_id id = cui_make_id(ui, "columns");
    cui_scroll_state *st = cui_state(ui, id, sizeof *st);
    *rows = (int)((st->view_h > 32 ? st->view_h : A.content_h) - 16) / 32;
    if (*rows < 1) *rows = 1;
    int count = (A.vis_n + *rows - 1) / *rows;
    if (A.reset_scroll) { st->x = st->tx = 0; A.reset_scroll = false; }
    if (ui->scroll_target == id) { ui->scroll_dx += ui->scroll_dy * 3; ui->scroll_dy = 0; }
    cui_box box = { .width = CUI_FILL, .height = CUI_FILL };
    int node = cui__scroll_begin_ex(ui, id, &box, true);
    float x = ui->nodes[node].scroll_x;
    *first = (int)floorf(x / 240);
    *last = (int)ceilf((x + A.content_w) / 240) + 1;
    if (*first > count) *first = count;
    if (*last > count) *last = count;
    cui_box_begin(ui, &(cui_box){ .dir = CUI_ROW, .width = CUI_PX(count * 240.0f), .height = CUI_FILL });
    if (*first) cui__leaf(ui, 0, CUI_PX(*first * 240.0f), CUI_PX(1));
}

void ui_columns_end(void) {
    int count = (A.vis_n + A.column_rows - 1) / A.column_rows;
    if (A.last_row < count) cui__leaf(A.ui, 0, CUI_PX((count - A.last_row) * 240.0f), CUI_PX(1));
    cui_box_end(A.ui);
    cui__scroll_end_ex(A.ui);
}

void ui_columns_reveal(int column) {
    cui_scroll_state *st = cui_state(A.ui, cui_make_id(A.ui, "columns"), sizeof *st);
    float x = column * 240.0f;
    if (x < st->tx) st->tx = x;
    else if (x + 240 > st->tx + st->view_w) st->tx = x + 240 - st->view_w;
}
