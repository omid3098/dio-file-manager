/* The context menu at the pointer, with a width of its own. CUI's cui_context_menu_begin takes the width of the
 * element it opens on, so in the list view and in an empty folder it spanned the whole view. This is the same
 * popup, opened at the pointer with no anchor element, so it is as narrow as the menu in the grid view.
 * It uses CUI's internal popup calls (CUI is pinned to v1.4.0); it is kept in a file of its own for that. */
#include <cui_internal.h>
#include <platform/cui_platform.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "ui.h"
#include "clip.h"

/* The clipboard hooks of CUI's text fields (rename, path, search: Ctrl+C, Ctrl+X, Ctrl+V). CUI's own hooks call
 * OpenClipboard once and give up when another program holds the clipboard, so a copy or paste was silently
 * dropped. These do the same work through clip_open, which retries for up to 300 ms. */
static void text_set_clip(void *user, const char *utf8) {
    (void)user;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) return;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n * sizeof(WCHAR));
    if (!mem) return;
    WCHAR *dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); return; }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, n);
    GlobalUnlock(mem);
    if (!clip_open((HWND)A.hwnd)) { GlobalFree(mem); return; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    CloseClipboard();
}

static const char *text_get_clip(void *user) {
    static char *cache;   /* valid until the next call, as CUI's hook promises */
    char *buf = NULL;
    (void)user;
    if (!clip_open((HWND)A.hwnd)) return NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const WCHAR *src = h ? GlobalLock(h) : NULL;
    if (src) {
        int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
        if (n > 0 && (buf = malloc((size_t)n)) != NULL) {
            WideCharToMultiByte(CP_UTF8, 0, src, -1, buf, n, NULL, NULL);
            char *o = buf;   /* drop carriage returns */
            for (char *p = buf; *p; p++) if (*p != '\r') *o++ = *p;
            *o = 0;
        }
        GlobalUnlock(h);
    }
    CloseClipboard();
    if (!buf) return NULL;
    free(cache);
    return cache = buf;
}

void ui_text_clipboard(cui_ctx *ui) {
    cui_platform_iface pi = ui->platform;   /* keep CUI's cursor hook */
    pi.set_clipboard = text_set_clip;
    pi.get_clipboard = text_get_clip;
    cui_set_platform(ui, &pi);
}

/* CUI 1.4.0 keeps a tooltip's fade state when its header popup opens. */
void ui_popup_tooltips(cui_ctx *ui) {
    if (ui->hot_layer) return; /* Controls inside the popup keep their own tooltips. */
    for (int i = 0; i < ui->popup_count; i++) if (ui->popups[i].open) {
        cui_state_entry *tip = cui__state_entry(ui, 0x7001717u);
        if (tip->data) memset(tip->data, 0, tip->size);
        ui->tooltip_id = 0;
        break;
    }
}

/* Inspect the rendered tooltip surface, including any fading tooltip. */
void ui_popup_snapshot(FILE *f) {
    cui_ctx *ui = A.ui;
    int open = 0, tooltip = 0;
    for (int i = 0; i < ui->popup_count; i++) open += ui->popups[i].open;
    for (int i = 0; i < ui->float_count; i++) {
        cui_node *n = &ui->nodes[ui->float_roots[i]];
        if (n->id == 0x7001718u && n->opacity > 0) tooltip = 1;
    }
    fprintf(f, ",\"popups\":%d,\"tooltip\":%d", open, tooltip);
}

/* A geometry-only popover anchor must not cover the selected row's hit target. */
void ui_anchor_passthrough(cui_ctx *ui) {
    ui->nodes[ui->node_count - 1].flags &= ~(CUI_NF_INTERACTIVE | CUI_NF_HIT_OPAQUE);
}

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

void ui_search_focus(void) { A.ui->focus = cui_make_id(A.ui, "searchfield"); }

void ui_view_options_surface(void) {
    cui_node *n = &A.ui->nodes[cui__current(A.ui)];
    n->opacity = 1; n->scale = 1; n->ty = 0;
}

static void paint_columns_bar(cui_ctx *ui, cui_node *n, void *data) {
    (void)data;
    cui_scroll_state *st = cui_state(ui, n->id, sizeof *st);
    cui_rect r = n->rect;
    float max = st->content_w - r.w;
    if (max <= 0) return;
    float w = CUI_MAX(28.0f, r.w * r.w / st->content_w);
    float x = r.x + 3 + cui__clampf(st->x / max, 0, 1) * (r.w - w - 6);
    cui_draw_rect(ui, (cui_rect){x, r.y + r.h - 9, w, 6}, CUI_CORNERS(3), cui_alpha(cui_color_of(ui, CUI_COLOR_FOREGROUND), .26f));
    cui__paint_add_hit(ui, n->id ^ 0x48534352u, (cui_rect){r.x, r.y + r.h - 12, r.w, 12}, CUI_NF_INTERACTIVE);
    st->bar_t = 0; /* This bar stays visible; suppress CUI's fading duplicate. */
}

void ui_columns_begin(int *rows, int *first, int *last) {
    cui_ctx *ui = A.ui;
    cui_id id = cui_make_id(ui, "columns");
    cui_scroll_state *st = cui_state(ui, id, sizeof *st);
    *rows = (int)((st->view_h > 32 ? st->view_h : A.content_h) - 32) / 32;
    if (*rows < 1) *rows = 1;
    int count = (A.vis_n + *rows - 1) / *rows;
    if (A.reset_scroll) { st->x = st->tx = 0; A.reset_scroll = false; }
    if (ui->scroll_target == id) { ui->scroll_dx += ui->scroll_dy * 3; ui->scroll_dy = 0; }
    cui_box box = { .width = CUI_FILL, .height = CUI_FILL };
    int node = cui__scroll_begin_ex(ui, id, &box, true);
    cui_interact bar = cui__interact(ui, id ^ 0x48534352u, CUI_IF_DRAG | CUI_IF_KEEP_FOCUS);
    if (bar.active && ui->mdown[0] && st->content_w > st->view_w) {
        float thumb = CUI_MAX(28.0f, st->view_w * st->view_w / st->content_w);
        float track = st->view_w - thumb - 6;
        cui_rect pr = cui__prev_rect(ui, id);
        st->x = st->tx = cui__clampf((ui->mx - pr.x - 3 - thumb / 2) / CUI_MAX(1, track), 0, 1) * (st->content_w - st->view_w);
        ui->nodes[node].scroll_x = st->x;
    }
    ui->nodes[node].paint = paint_columns_bar;
    ui->nodes[node].flags |= CUI_NF_PAINT_AFTER;
    float x = ui->nodes[node].scroll_x;
    *first = (int)floorf(x / 240);
    *last = (int)ceilf((x + A.content_w) / 240) + 1;
    if (*first > count) *first = count;
    if (*last > count) *last = count;
    cui_box_begin(ui, &(cui_box){ .dir = CUI_ROW, .width = CUI_PX(count * 240.0f), .height = CUI_FILL, .pad_y = 16 });
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

/* The anchor and item rectangles are in document coordinates, including virtual rows. */
static struct {
    bool pending, dragging;
    bool *saved;
    cui_vec2 anchor, press;
    cui_rect box;
    cui_id owner;
} band;

bool ui_band_dragging(void) { return band.dragging; }

void ui_band_cancel(void) {
    if (A.ui && A.ui->active == band.owner) A.ui->active = 0;
    free(band.saved);
    memset(&band, 0, sizeof band);
}

static cui_rect band_item(int i, float width) {
    if (A.zoom == Z_DETAILS) return (cui_rect){0, 40 + i * 36.0f, width, 36};
    if (A.zoom == Z_LIST) return (cui_rect){(i / A.column_rows) * 240.0f, 16 + (i % A.column_rows) * 32.0f, 240, 32};
    float w = (width - 32) / A.cols;
    return (cui_rect){16 + (i % A.cols) * w, (i / A.cols) * A.cell_h, w, A.cell_h};
}

static void paint_band(cui_ctx *ui, cui_node *n, void *data) {
    if (A.zoom == Z_LIST) paint_columns_bar(ui, n, data);
    if (!band.dragging) return;
    cui_rect clip = n->rect, box = band.box;
    if (A.zoom == Z_DETAILS) { clip.y += 40; clip.h -= 40; }
    box.x += n->rect.x - n->scroll_x;
    box.y += n->rect.y - n->scroll_y;
    cui_color accent = cui_color_of(ui, CUI_COLOR_ACCENT);
    cui_push_clip(ui, clip);
    cui_draw_rect_border(ui, box, CUI_CORNERS(0), cui_alpha(accent, .18f), accent, 1.0f / ui->scale);
    cui_pop_clip(ui);
}

void ui_band_begin(void) {
    cui_ctx *ui = A.ui;
    int scroll = cui__current(ui);
    while (scroll >= 0 && !(ui->nodes[scroll].flags & CUI_NF_SCROLL_Y)) scroll = ui->nodes[scroll].parent;
    if (scroll < 0) return;
    cui_node *n = &ui->nodes[scroll];
    cui_rect view = cui__prev_rect(ui, n->id), clip = view;
    if (view.w <= 0 || view.h <= 0) return;
    if (A.zoom == Z_DETAILS) { clip.y += 40; clip.h -= 40; }
    bool blocked = act_modal_open() || views_modal_open() || ctxmenu_is_open(ui) || ui_view_options_open() || A.sidebar_open;
    if (blocked || cui_key_pressed(ui, CUI_KEY_ESCAPE) || (band.pending && !ui->mdown[0] && !ui->mreleased[0])) {
        ui_band_cancel(); return;
    }
    cui_vec2 p = { ui->mx - view.x + n->scroll_x, ui->my - view.y + n->scroll_y };
    if (ui->mpressed[0] && cui__rect_contains(clip, ui->mx, ui->my)) {
        bool item = false;
        for (int i = 0; i < A.vis_n; i++)
            if (cui__rect_contains(band_item(i, view.w), p.x, p.y)) { item = true; break; }
        /* Scrollbar and floating controls keep their own press, even over empty space. */
        bool bar = (A.zoom == Z_LIST && ui->my >= view.y + view.h - 12) ||
                   (A.zoom != Z_LIST && ui->mx >= view.x + view.w - 12);
        if (!item && !bar && (!ui->hot_layer) && (!ui->active || ui->hot == n->id || A.zoom == Z_DETAILS)) {
            ui_band_cancel();
            band.saved = calloc((size_t)A.vis_n, sizeof *band.saved);
            if (!band.saved) return;
            if (ui->mods & (CUI_MOD_CTRL | CUI_MOD_SHIFT)) memcpy(band.saved, A.sel, (size_t)A.vis_n * sizeof *A.sel);
            else app_clear_selection();
            band.pending = true; band.anchor = p; band.press = (cui_vec2){ui->mx, ui->my};
            band.owner = n->id ^ 0xBA4Du;
            ui->active = band.owner; ui->focus = 0; A.last_click_idx = -1;
        }
    }
    if (!band.pending) return;
    if (fabsf(ui->mx - band.press.x) >= 4 || fabsf(ui->my - band.press.y) >= 4) band.dragging = true;
    if (band.dragging) {
        p.x = cui__clampf(ui->mx, clip.x, clip.x + clip.w) - view.x + n->scroll_x;
        p.y = cui__clampf(ui->my, clip.y, clip.y + clip.h) - view.y + n->scroll_y;
        band.box = (cui_rect){ CUI_MIN(p.x, band.anchor.x), CUI_MIN(p.y, band.anchor.y), fabsf(p.x-band.anchor.x), fabsf(p.y-band.anchor.y) };
        for (int i = 0; i < A.vis_n; i++) {
            cui_rect r = band_item(i, view.w), b = band.box;
            A.sel[i] = band.saved[i] || (b.x <= r.x+r.w && b.x+b.w >= r.x && b.y <= r.y+r.h && b.y+b.h >= r.y);
        }
        if (ui->mdown[0]) {
            cui_scroll_state *st = cui_state(ui, n->id, sizeof *st);
            float edge = A.zoom == Z_LIST ? ui->mx : ui->my;
            float low = A.zoom == Z_LIST ? clip.x : clip.y;
            float high = low + (A.zoom == Z_LIST ? clip.w : clip.h);
            float speed = edge < low+32 ? -cui__clampf((low+32-edge)/32, 0, 1) :
                          edge > high-32 ? cui__clampf((edge-high+32)/32, 0, 1) : 0;
            float dt = CUI_MIN(ui->dt, .05f);
            if (A.zoom == Z_LIST) st->tx = cui__clampf(st->tx + speed*720*dt, 0, CUI_MAX(0, st->content_w-st->view_w));
            else st->ty = cui__clampf(st->ty + speed*720*dt, 0, CUI_MAX(0, st->content_h-st->view_h));
            cui_request_wakeup(ui, 1.0f/60);
        }
        n->paint = paint_band;
        n->flags |= CUI_NF_PAINT_AFTER;
    }
    if (ui->mreleased[0]) ui_band_cancel();
}
