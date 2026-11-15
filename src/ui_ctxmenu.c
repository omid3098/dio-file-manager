/* The context menu at the pointer, with a width of its own. CUI's cui_context_menu_begin takes the width of the
 * element it opens on, so in the list view and in an empty folder it spanned the whole view. This is the same
 * popup, opened at the pointer with no anchor element, so it is as narrow as the menu in the grid view.
 * It uses CUI's internal popup calls (CUI is pinned to one commit); it is kept in a file of its own for that, and it is the
 * only file that includes CUI's private header: every cui__ call of the app is here. */
#include <cui_internal.h>
#include <platform/cui_platform.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "ui.h"
#include "clip.h"
#include "dlgtext.h"

/* What CUI's own helpers do, kept here so a CUI update that renames one cannot break the build. */
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline bool rect_has(cui_rect r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

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

/* The hover tooltip of an item, CUI's own tooltip (its colours, fade and delay) shown as GTK shows one (gtktooltip.c,
 * gtk_tooltip_set_display_area): under the box of the name, centred on it, above it when there is no room below, and
 * never over the controls at the top of the window. CUI puts its tooltip above its anchor; a box that reaches from the
 * top of the window to 6 px above the wanted place makes it flip below the box, at that place. The name is broken into
 * its final lines here (at spaces, between characters inside a word wider than a line, as Pango WRAP_WORD_CHAR does),
 * each narrower than the 262 px CUI gives a line, so CUI never wraps again and the lines counted are the lines drawn. */
typedef struct { cui_ctx *ui; float size; } tip_measure;
static float tip_text_w(void *c, const char *s, int len) {
    tip_measure *m = c;
    return len <= 0 ? 0 : cui_measure_text(m->ui, CUI_FONT_MEDIUM, m->size, s, len).width;
}

void ui_item_tooltip(cui_ctx *ui, cui_id id, const char *name, cui_rect label) {
    if (!name) { if (ui->tooltip_id == id) ui->tooltip_id = 0; return; }   /* the pointer is not on this item */
    enum { CUI_TIP_TEXT = 256, WRAP_W = 240, GAP = 6, TIP_TOP = 46, EDGE = 8, TIP_PAD = 10 };   /* CUI keeps 255 bytes; its box is 280 px less 2 x 9 of padding */
    char shown[CUI_TIP_TEXT], text[CUI_TIP_TEXT];
    tip_measure m = { ui, cui__fs(ui, ui->theme.text_sm) };
    dlg_path_fit(name, shown, 200);   /* room for the line breaks; a longer name keeps its start and its end */
    int lines = dlg_break_lines(shown, text, sizeof text, WRAP_W, tip_text_w, &m, 1000);
    float lh = m.size * 1.35f, h = (float)lines * lh + TIP_PAD, pad = 4.0f, vh = cui_viewport_size(ui).y;
    float bottom = label.y + label.h + pad;
    cui_rect a = { label.x - pad, label.y, label.w + 2 * pad, 0 };   /* above the name */
    if (bottom + h + EDGE <= vh) { a.y = 0; a.h = bottom - GAP; }   /* below the name when it fits */
    else if (label.y - GAP - h < TIP_TOP) {   /* neither side fits: in the content area under the header, as many lines as fit */
        float top = TIP_TOP + EDGE;
        int room = (int)((vh - EDGE - top - TIP_PAD) / lh);
        if (room < 1) room = 1;
        if (lines > room) dlg_break_lines(shown, text, sizeof text, WRAP_W, tip_text_w, &m, room);
        a.y = 0; a.h = top - GAP;
    }
    cui__state_entry(ui, id)->rect = a;
    cui__tooltip_passive(ui, id, text);
}

/* Close every open CUI popup (the one call main.c needs, so cui__ calls stay in this file). */
void ui_popups_close_all(cui_ctx *ui) { cui__close_all_popups(ui); }

/* Any menu, popover, path-bar menu or view-options popup is open: one check over every CUI popup. */
bool ui_popup_open_any(cui_ctx *ui) {
    for (int i = 0; i < ui->popup_count; i++) if (ui->popups[i].open) return true;
    return false;
}

/* Inspect the rendered tooltip surface, including any fading tooltip. */
void ui_popup_snapshot(FILE *f) {
    cui_ctx *ui = A.ui;
    int open = 0, tooltip = 0, tipl = 0; float tipw = 0; cui_rect tipbox = { 0 }, tiprect = { 0 }; const char *tiptext = "";
    for (int i = 0; i < ui->popup_count; i++) open += ui->popups[i].open;
    for (int i = 0; i < ui->float_count; i++) {
        cui_node *n = &ui->nodes[ui->float_roots[i]];
        if (n->id == 0x7001718u && n->opacity > 0) {
            tooltip = 1;
            /* test probe: the box, and the widest line of the text as it is shaped (not as the box was measured) */
            float tw = 0; int tl = 0; const char *txt = ""; cui_rect trect = { 0 };
            for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
                cui_node *t = &ui->nodes[c];
                if (!(t->flags & CUI_NF_TEXT)) continue;
                txt = t->text;
                trect = t->rect;
                /* a line CUI wraps is cut at a space: only a word wider than the box can be clipped */
                for (const char *p = t->text; *p;) {
                    while (*p == ' ' || *p == '\n') p++;
                    const char *e = p;
                    while (*e && *e != ' ' && *e != '\n') e++;
                    float w = e > p ? cui_measure_text(ui, t->font, t->font_size, p, (int)(e - p)).width : 0;
                    if (w > tw) tw = w;
                    p = e;
                }
                tl = (int)(t->rect.h / (t->font_size * 1.35f) + .5f);
            }
            tipbox = n->rect; tipw = tw; tipl = tl; tiptext = txt; tiprect = trect;
        }
    }
    fprintf(f, ",\"popups\":%d,\"tooltip\":%d", open, tooltip);
    if (tooltip) {
        fprintf(f, ",\"tip\":{\"box\":[%.2f,%.2f,%.2f,%.2f],\"text_w\":%.2f,\"lines\":%d,\"text_rect\":[%.2f,%.2f,%.2f,%.2f],\"pad\":%.2f,\"text\":\"", tipbox.x, tipbox.y, tipbox.w, tipbox.h, tipw, tipl, tiprect.x, tiprect.y, tiprect.w, tiprect.h, 9.0f);
        for (const char *p = tiptext; *p; p++) {
            if (*p == '"' || *p == '\\') fputc('\\', f);
            if (*p == '\n') fputs("\\n", f); else fputc(*p, f);
        }
        fputs("\"}", f);
    }
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
    return cui_get_rect(ui, ui->nodes[cui__current(ui)].id);
}

static cui_id menu_active;   /* the id of the row menu opened last, made inside the frame's id scope (a menu's id depends on the layout: the sidebar sheet makes its own); ctxmenu_is_open runs outside it */

bool ctxmenu_row_hovered(cui_ctx *ui) {
    /* CUI 1.4.0's data-grid row does not set last_id; use its clipped hit rectangle. */
    cui_id row = ui->nodes[cui__current(ui)].id;
    for (int i = ui->prev_hit_count - 1; i >= 0; i--) {
        const cui_hit *h = &ui->prev_hits[i];
        if (h->id == row && h->layer == ui->hot_layer)
            return rect_has(h->rect, ui->mx, ui->my) && ui->my >= 46.0f + 40.0f;
    }
    return false;
}

bool ctxmenu_begin(cui_ctx *ui, const char *id, bool open_here) {
    cui_id pid = cui_make_id(ui, id) ^ 0xC7Cu;
    if (open_here) {
        menu_active = pid;
        cui__close_all_popups(ui);
        cui__popup_set(ui, pid, true);
        cui_popup *p = cui__popup_get(ui, pid, false);
        if (p) { p->at_mouse = true; p->mouse = cui_mouse_pos(ui); }
    }
    if (!cui__popup_get(ui, pid, false)) return false;
    return cui__popup_begin(ui, pid, 0, CUI_PLACE_BOTTOM_START, 0, 6, 0, true);
}

/* The popover of CUI's cui_popover_begin, built as a menu popup so Up and Down move a highlight through its menu items. */
bool popover_menu_begin(cui_ctx *ui, const char *id, int placement, float width, float pad, float gap) {
    cui_id pid = cui_make_id(ui, id);
    cui_popup *p = cui__popup_get(ui, pid, false);
    if (!p) return false;
    if (!p->anchor || p->opened_frame + 1 >= ui->frame) p->anchor = ui->last_id;
    return cui__popup_begin(ui, pid, p->anchor, placement, width, pad, gap, true);
}

void ctxmenu_end(cui_ctx *ui) { cui__popup_end(ui); }

bool ctxmenu_is_open(cui_ctx *ui) {
    cui_popup *p = menu_active ? cui__popup_get(ui, menu_active, false) : NULL;
    return p && p->open;
}

void ui_search_focus(void) { A.ui->focus = cui_make_id(A.ui, "searchfield"); }

void ui_menu_eat_activation(void) { A.ui->kpressed[CUI_KEY_ENTER] = A.ui->kpressed[CUI_KEY_SPACE] = 0; }

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
    float x = r.x + 3 + clampf(st->x / max, 0, 1) * (r.w - w - 6);
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
        cui_rect pr = cui_get_rect(ui, id);
        st->x = st->tx = clampf((ui->mx - pr.x - 3 - thumb / 2) / CUI_MAX(1, track), 0, 1) * (st->content_w - st->view_w);
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

/* CUI's data grid scrolls a child state at its id xor 0x5C011: cui_scroll_to on the grid's own id writes into its column widths. */
void ui_list_scroll_to(const char *id, float y) {
    cui_scroll_state *st = cui_state(A.ui, cui_make_id(A.ui, id) ^ 0x5C011u, sizeof *st);
    st->ty = y < 0 ? 0 : y;
}

float ui_list_column_width(int column) {
    float *widths = cui_state(A.ui, cui_make_id(A.ui, "list"), 32 * sizeof(float));   /* the first member of CUI's grid state */
    return widths[column];
}

/* The anchor and item rectangles are in document coordinates, including virtual rows. */
static struct {
    bool pending, dragging, taken;   /* taken: the box owns the press (from the start on the background, from the first 4 px on an item's blank part) */
    bool *saved;
    cui_vec2 anchor, press;
    cui_rect box;
    cui_id owner;
} band;

bool ui_band_dragging(void) { return band.dragging; }

float ui_body_text_w(const char *s) {   /* the width cui_text gives s at the theme's body size */
    return cui_measure_text(A.ui, CUI_FONT_BODY, cui__fs(A.ui, A.ui->theme.text_body), s, (int)strlen(s)).width;
}

void ui_band_cancel(void) {
    if (A.ui && A.ui->active == band.owner) A.ui->active = 0;
    free(band.saved);
    memset(&band, 0, sizeof band);
}

static cui_rect band_item(int i, float width) {
    if (A.zoom == Z_DETAILS) return (cui_rect){0, 40 + i * 36.0f, width, 36};
    if (A.zoom == Z_LIST) return (cui_rect){(i / A.column_rows) * 240.0f, 16 + (i % A.column_rows) * 32.0f, 240, 32};
    return ui_grid_item_rect(i, width);   /* where the item is drawn now, also while it glides */
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

void ui_scroll_get(const char *id, float *y, float *ty, float *view_h) {
    cui_scroll_state *st = cui_state(A.ui, cui_make_id(A.ui, id), sizeof *st);
    *y = st->y; *ty = st->ty; *view_h = st->view_h;
}

void ui_scroll_jump(const char *id, float y) {
    cui_scroll_state *st = cui_state(A.ui, cui_make_id(A.ui, id), sizeof *st);
    st->y = st->ty = y < 0 ? 0 : y;
}

bool ui_wheel_scrolling(void) { return A.ui->scroll_dy != 0 || A.ui->scroll_dx != 0; }
void ui_node_opacity(float o) { A.ui->nodes[cui__current(A.ui)].opacity = o; }
void ui_push_alpha(float a) { cui__push_xform(A.ui, 1, 0, 0, a, 0, 0); }
void ui_pop_alpha(void) { cui__pop_xform(A.ui); }

static void (*overlay_fn)(cui_ctx *, cui_rect, cui_vec2);

static void paint_overlay(cui_ctx *ui, cui_node *n, void *data) {
    if (overlay_fn) {
        cui_push_clip(ui, n->rect);
        overlay_fn(ui, n->rect, (cui_vec2){ n->scroll_x, n->scroll_y });
        cui_pop_clip(ui);
    }
    paint_band(ui, n, data);
}

void ui_overlay_set(void (*fn)(cui_ctx *, cui_rect, cui_vec2)) {
    cui_ctx *ui = A.ui;
    int scroll = cui__current(ui);
    while (scroll >= 0 && !(ui->nodes[scroll].flags & CUI_NF_SCROLL_Y)) scroll = ui->nodes[scroll].parent;
    if (scroll < 0) return;
    overlay_fn = fn;
    ui->nodes[scroll].paint = paint_overlay;
    ui->nodes[scroll].flags |= CUI_NF_PAINT_AFTER;
}

void ui_band_begin(void) {
    cui_ctx *ui = A.ui;
    int scroll = cui__current(ui);
    while (scroll >= 0 && !(ui->nodes[scroll].flags & CUI_NF_SCROLL_Y)) scroll = ui->nodes[scroll].parent;
    if (scroll < 0) return;
    cui_node *n = &ui->nodes[scroll];
    cui_rect view = cui_get_rect(ui, n->id), clip = view;
    if (view.w <= 0 || view.h <= 0) return;
    if (A.zoom == Z_DETAILS) { clip.y += 40; clip.h -= 40; }
    bool blocked = act_modal_open() || views_modal_open() || ctxmenu_is_open(ui) || ui_view_options_open() || A.sidebar_open;
    if (blocked || cui_key_pressed(ui, CUI_KEY_ESCAPE) || (band.pending && !ui->mdown[0] && !ui->mreleased[0])) {
        ui_band_cancel(); return;
    }
    cui_vec2 p = { ui->mx - view.x + n->scroll_x, ui->my - view.y + n->scroll_y };
    if (ui->mpressed[0] && rect_has(clip, ui->mx, ui->my)) {
        /* A press on an item's icon or name text is the item's own; a press on the rest of an item (a row's blank part, a cell's
         * padding) is an item press until the pointer moves, then it becomes the box (see below), so a click and a double click
         * there act as they always did. */
        int hit = -1;
        bool content = false;
        for (int i = 0; i < A.vis_n; i++) {
            cui_rect ir = band_item(i, view.w);
            if (!rect_has(ir, p.x, p.y)) continue;
            hit = i;
            content = A.zoom == Z_DETAILS ? ui_list_content_at(i, ui->mx) : ui_grid_content_at(i, ir, p);
            break;
        }
        /* Scrollbar and floating controls keep their own press, even over empty space. */
        bool bar = (A.zoom == Z_LIST && ui->my >= view.y + view.h - 12) ||
                   (A.zoom != Z_LIST && ui->mx >= view.x + view.w - 12);
        if (!content && !bar && !ui_sidebar_edge_held() && (!ui->hot_layer) && (hit >= 0 || !ui->active || ui->hot == n->id || A.zoom == Z_DETAILS)) {
            ui_band_cancel();
            band.saved = calloc((size_t)A.vis_n, sizeof *band.saved);
            if (!band.saved) return;
            bool add = (ui->mods & (CUI_MOD_CTRL | CUI_MOD_SHIFT)) != 0;
            if (add) memcpy(band.saved, A.sel, (size_t)A.vis_n * sizeof *A.sel);
            band.pending = true; band.anchor = p; band.press = (cui_vec2){ui->mx, ui->my};
            band.owner = n->id ^ 0xBA4Du;
            if (hit < 0) { if (!add) app_clear_selection(); band.taken = true; ui->active = band.owner; ui->focus = 0; A.last_click_idx = -1; }
        }
    }
    if (!band.pending) return;
    if (fabsf(ui->mx - band.press.x) >= 4 || fabsf(ui->my - band.press.y) >= 4) band.dragging = true;
    if (band.dragging && !band.taken) {   /* the press began on an item's blank part: the box takes the press from the item now */
        band.taken = true;
        if (!(ui->mods & (CUI_MOD_CTRL | CUI_MOD_SHIFT))) app_clear_selection();
        ui->active = band.owner; ui->focus = 0; A.last_click_idx = -1;
    }
    if (band.dragging) {
        p.x = clampf(ui->mx, clip.x, clip.x + clip.w) - view.x + n->scroll_x;
        p.y = clampf(ui->my, clip.y, clip.y + clip.h) - view.y + n->scroll_y;
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
            float speed = edge < low+32 ? -clampf((low+32-edge)/32, 0, 1) :
                          edge > high-32 ? clampf((edge-high+32)/32, 0, 1) : 0;
            float dt = CUI_MIN(ui->dt, .05f);
            if (A.zoom == Z_LIST) st->tx = clampf(st->tx + speed*720*dt, 0, CUI_MAX(0, st->content_w-st->view_w));
            else st->ty = clampf(st->ty + speed*720*dt, 0, CUI_MAX(0, st->content_h-st->view_h));
            cui_request_wakeup(ui, 1.0f/60);
        }
        n->paint = paint_band;
        n->flags |= CUI_NF_PAINT_AFTER;
    }
    if (ui->mreleased[0]) ui_band_cancel();
}

/* ---- a scrolling menu follows the keyboard ----
 * CUI highlights the row the arrow keys reach but never scrolls to it, so in a menu that scrolls (a short window) Enter could
 * pick a row nobody sees. menu_rows_begin() names the scrolling column; menu_row() wraps each cui_menu_item with its label, counting rows the
 * way CUI does, and moves the column's scroll target just far enough to show the keyboard's row. */
static cui_id rows_scroll;
static int rows_seen;
static cui_rect hl_row, hl_view;   /* the highlighted row and the scrolling column, last frame they were built */
enum { ROWS_MAX = 16 };
static cui_rect all_rows[ROWS_MAX], all_view;   /* every row of the open menu and its scrolling column (test state) */
static int all_rows_n, hl_index;

void menu_frame_begin(void) { memset(&hl_row, 0, sizeof hl_row); memset(&hl_view, 0, sizeof hl_view); memset(all_rows, 0, sizeof all_rows); memset(&all_view, 0, sizeof all_view); all_rows_n = 0; hl_index = -1; }

void menu_rows_begin(const char *scroll_id) { rows_scroll = cui_make_id(A.ui, scroll_id); rows_seen = 0; }

bool menu_row(const char *label, bool chosen) {
    cui_ctx *ui = A.ui;
    int index = rows_seen++;
    if (ui->pb_depth <= 0 || !rows_scroll) return chosen;
    cui__menu_nav *nav = cui_state(ui, ui->pb_stack[ui->pb_depth - 1].id ^ 0x4E4156u, sizeof *nav);
    cui_rect row = cui_get_rect(ui, cui_make_id(ui, label)), view = cui_get_rect(ui, rows_scroll);
    if (index < ROWS_MAX) { all_rows[index] = row; all_rows_n = index + 1; }
    all_view = view;
    if (nav->highlight != index) return chosen;
    hl_index = index;
    hl_row = row; hl_view = view;
    if (!nav->keyboard) return chosen;
    if (row.h <= 0 || view.h <= 0) return chosen;
    cui_scroll_state *st = cui_state(ui, rows_scroll, sizeof *st);
    if (row.y < view.y) st->ty = st->y - (view.y - row.y) - 4;
    else if (row.y + row.h > view.y + view.h) st->ty = st->y + (row.y + row.h - view.y - view.h) + 4;
    st->last_activity = ui->time;
    return chosen;
}

/* Test state: where the menu's highlighted row is and the part of the menu that shows rows. */
void menu_snapshot(FILE *f) {
    fprintf(f, ",\"menu_row\":[%.2f,%.2f,%.2f,%.2f],\"menu_view\":[%.2f,%.2f,%.2f,%.2f],\"menu_index\":%d,\"menu_scroll\":[%.2f,%.2f,%.2f,%.2f],\"menu_rows\":[",
            hl_row.x, hl_row.y, hl_row.w, hl_row.h, hl_view.x, hl_view.y, hl_view.w, hl_view.h, hl_index, all_view.x, all_view.y, all_view.w, all_view.h);
    for (int i = 0; i < all_rows_n; i++) fprintf(f, "%s[%.2f,%.2f,%.2f,%.2f]", i ? "," : "", all_rows[i].x, all_rows[i].y, all_rows[i].w, all_rows[i].h);
    fputc(']', f);
}
