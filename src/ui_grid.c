/* The icon grid: an app component. Rows are virtual (only the visible rows are built), each row is one canvas
 * that paints its cells; clicks are mapped to cells by position. */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"

static const float ZOOM[Z_COUNT] = { 16, 16, 32, 64, 96, 144 };
#define GPAD 16.0f
#define NAME_SIZE 13.0f
#define LINE_H 17.0f

static float icon_size(void) { return ZOOM[A.zoom]; }
static float min_cell_w(void) { return A.zoom == Z_SMALL ? 240 : icon_size() + 48; }
static float cell_height(void) { return A.zoom == Z_SMALL ? 48 : icon_size() + 12 + 8 + 2 * LINE_H + 10; }

/* ---- name layout: up to two lines, the second cut short with an ellipsis ---- */

typedef struct name_lines { char line[2][260]; int n; bool cut; } name_lines;

static float text_w(cui_ctx *ui, const char *s, int len) {
    return len <= 0 ? 0 : cui_measure_text(ui, CUI_FONT_BODY, NAME_SIZE, s, len).width;
}

static int utf8_len(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

/* Longest prefix of s (byte length, on a character boundary) that fits maxw. */
static int fit_prefix(cui_ctx *ui, const char *s, int len, float maxw) {
    int bounds[261], nb = 0;
    for (int i = 0; i < len && nb < 260; i += utf8_len((unsigned char)s[i])) bounds[nb++] = i;
    bounds[nb] = len;
    int lo = 0, hi = nb;   /* number of characters */
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (text_w(ui, s, bounds[mid]) <= maxw) lo = mid; else hi = mid - 1;
    }
    return bounds[lo];
}

static bool is_break(const char *s, int i) {
    unsigned char c = (unsigned char)s[i];
    if (c == ' ' || c == '-' || c == '_' || c == '.') return true;
    return c == 0xE2 && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x8C;   /* ZWNJ */
}

static void layout_name(cui_ctx *ui, const char *name, float maxw, name_lines *out) {
    char buf[260];
    snprintf(buf, sizeof buf, "%s", name);
    int len = (int)strlen(buf);
    out->n = 1; out->cut = false;
    if (text_w(ui, buf, len) <= maxw) { memcpy(out->line[0], buf, (size_t)len + 1); return; }
    int k = fit_prefix(ui, buf, len, maxw);
    if (k < 1) k = utf8_len((unsigned char)buf[0]);
    int brk = k;
    for (int i = k - 1; i > k / 2; i--)   /* prefer a break after a space, dash or ZWNJ */
        if (is_break(buf, i)) { brk = i + (buf[i] == (char)0xE2 ? 3 : 1); break; }
    memcpy(out->line[0], buf, (size_t)brk);
    out->line[0][brk] = 0;
    const char *rest = buf + brk;
    while (*rest == ' ') rest++;
    int rl = (int)strlen(rest);
    out->n = 2;
    if (text_w(ui, rest, rl) <= maxw) { memcpy(out->line[1], rest, (size_t)rl + 1); return; }
    int e = fit_prefix(ui, rest, rl, maxw - text_w(ui, "\xE2\x80\xA6", 3));
    memcpy(out->line[1], rest, (size_t)e);
    memcpy(out->line[1] + e, "\xE2\x80\xA6", 4);
    out->cut = true;
}

/* ---- painting ---- */

typedef struct row_data { int idx; ui_thumb thumb; bool has_thumb; } row_data;

static void paint_compact(cui_ctx *ui, cui_rect r, const row_data *d) {
    int i = d->idx;
    float icon = icon_size(), cy = r.y + r.h * 0.5f;
    if (A.sel[i] || A.hover == i)
        cui_draw_rect(ui, (cui_rect){ r.x + 4, r.y + 2, r.w - 8, r.h - 4 }, CUI_CORNERS(8), A.sel[i] ? CUI_TOKEN_COLOR(CUI_TOKEN_ACTIVE) : CUI_COLOR_HOVER);
    const fs_entry *e = app_item(i);
    float cx = r.x + 12 + icon * 0.5f;
    if (e->flags & FS_DIR) ui_draw_folder(ui, cx, cy, icon);
    else if (d->has_thumb && d->thumb.w && d->thumb.h) {
        float scale = icon / (float)(d->thumb.w > d->thumb.h ? d->thumb.w : d->thumb.h);
        float w = d->thumb.w * scale, h = d->thumb.h * scale;
        cui_draw_image(ui, d->thumb.tex, (cui_rect){ cx - w/2, cy - h/2, w, h }, (cui_rect){0,0,1,1}, CUI_CORNERS(2), (cui_color){0});
    } else ui_draw_file(ui, cx, cy, icon, app_item_name(i), e->kind);
    char name[264];
    const char *full = app_item_name(i);
    float width = r.w - icon - 36;
    int len = (int)strlen(full), n = fit_prefix(ui, full, len, width);
    if (n < len) n = fit_prefix(ui, full, len, width - text_w(ui, "\xE2\x80\xA6", 3));
    memcpy(name, full, (size_t)n); name[n] = 0;
    if (n < len) memcpy(name + n, "\xE2\x80\xA6", 4);
    cui_draw_text(ui, CUI_FONT_BODY, NAME_SIZE, (cui_vec2){ r.x + icon + 20, cy - 7 }, CUI_COLOR_FOREGROUND, name, -1);
}

static void paint_row(cui_ctx *ui, cui_rect r, void *user) {
    if (((row_data *)user)->idx >= A.vis_n) return;
    if (A.zoom <= Z_SMALL) { paint_compact(ui, r, user); return; }
    float icon = icon_size();
    float cw = r.w;   /* one canvas per cell: a context menu is as wide as the cell it opens on */
    do {
        int i = ((row_data *)user)->idx;
        if (i >= A.vis_n) break;
        const fs_entry *e = app_item(i);
        const char *name = app_item_name(i);
        float cx = r.x + cw * 0.5f;
        float top = r.y + 2;
        bool sel = A.sel[i], hov = A.hover == i;
        float box = icon + 16;
        cui_rect ib = { cx - box * 0.5f, top, box, box };
        if (sel) cui_draw_rect(ui, ib, CUI_CORNERS(14), CUI_TOKEN_COLOR(CUI_TOKEN_ACTIVE));
        else if (hov) cui_draw_rect(ui, ib, CUI_CORNERS(14), CUI_COLOR_HOVER);
        float icy = top + box * 0.5f;
        const ui_thumb *th = &((row_data *)user)->thumb;
        if (e->flags & FS_DIR) ui_draw_folder(ui, cx, icy, icon);
        else if (((row_data *)user)->has_thumb && th->w > 0 && th->h > 0) {
            /* the picture, with its aspect, inside the icon's box */
            float k = icon / (float)(th->w > th->h ? th->w : th->h);
            float dw = (float)th->w * k, dh = (float)th->h * k;
            cui_rect pr = { cx - dw * 0.5f, icy - dh * 0.5f, dw, dh };
            float rad = icon * 0.09f;
            cui_draw_shadow(ui, (cui_rect){ pr.x, pr.y + 1, pr.w, pr.h }, CUI_CORNERS(rad), icon * 0.05f, 0, (cui_vec2){ 0, 1 },
                            cui_alpha(cui_color_of(ui, CUI_COLOR_FOREGROUND), 0.18f));
            cui_draw_image(ui, th->tex, pr, (cui_rect){ 0, 0, 1, 1 }, CUI_CORNERS(rad), (cui_color){ 0 });
        }
        else ui_draw_file(ui, cx, icy, icon, name, e->kind);
        if (A.loc != LOC_TRASH && app_is_starred(i)) {
            cui_vec2 sp = { ib.x + ib.w - 8, ib.y + 12 };
            cui_draw_circle(ui, sp, 10, CUI_COLOR_SURFACE_RAISED);
            ui_draw_star(ui, sp.x, sp.y + 0.5f, 7.5f, CUI_COLOR_WARNING);
        }
        name_lines nl;
        layout_name(ui, name, cw - 10, &nl);
        float ty = top + box + 4;
        for (int k = 0; k < nl.n; k++) {
            float w = text_w(ui, nl.line[k], (int)strlen(nl.line[k]));
            cui_vec2 p = { cx - w * 0.5f, ty + k * LINE_H };
            if (sel) {
                cui_draw_rect(ui, (cui_rect){ p.x - 6, p.y - 1, w + 12, LINE_H }, CUI_CORNERS(9), CUI_COLOR_ACCENT);
                cui_draw_text(ui, CUI_FONT_BODY, NAME_SIZE, (cui_vec2){ p.x, p.y + 1 }, CUI_COLOR_ACCENT_FOREGROUND, nl.line[k], -1);
            } else {
                cui_draw_text(ui, CUI_FONT_BODY, NAME_SIZE, (cui_vec2){ p.x, p.y + 1 }, CUI_COLOR_FOREGROUND, nl.line[k], -1);
            }
        }
    } while (0);
}

static void select_click(int idx, int mods) {
    if (mods & CUI_MOD_SHIFT) {
        if (A.anchor < 0) A.anchor = idx;
        app_select_range(A.anchor, idx);
    } else if (mods & CUI_MOD_CTRL) {
        A.sel[idx] = !A.sel[idx];
        A.anchor = idx;
    } else {
        app_select_only(idx);
    }
    A.cursor = idx;
}

static int build_cell(int idx) {
    cui_ctx *ui = A.ui;
    row_data d = { idx };
    if (idx < A.vis_n && ui_thumb_wanted(app_item(idx), app_item_name(idx))) {
        char path[FS_PATH_MAX]; app_path_of(idx, path, sizeof path);
        d.has_thumb = ui_thumb_lookup(path, app_item(idx)->mtime, ui_thumb_px(icon_size()), &d.thumb);
    }
    cui_canvas_result c = cui_canvas(ui, "cell", CUI_FILL, CUI_PX(A.cell_h), paint_row, &d, sizeof d);
    if ((A.zoom == Z_LIST && idx == A.first_row * A.column_rows) || (A.zoom != Z_LIST && idx == A.first_row * A.cols)) {
        A.first_rect = (cui_rect){ c.rect.x - GPAD, c.rect.y, c.rect.w * (float)A.cols + 2 * GPAD, c.rect.h };
    }
    bool on_item = c.hovered && idx < A.vis_n;
    if (on_item) A.hover = idx;
    if (c.clicked) {
        if (on_item) {
            double now = cui_time(ui);
            if (idx == A.last_click_idx && now - A.last_click_t < 0.45 && !(cui_key_mods(ui) & (CUI_MOD_SHIFT | CUI_MOD_CTRL))) {
                A.last_click_idx = -1;
                return idx;
            } else {
                select_click(idx, cui_key_mods(ui));
                A.last_click_idx = idx;
                A.last_click_t = now;
            }
        } else if (!(cui_key_mods(ui) & (CUI_MOD_SHIFT | CUI_MOD_CTRL))) {
            app_clear_selection();
        }
    }
    if (on_item && cui_mouse_clicked(ui, CUI_MOUSE_RIGHT)) { if (!A.sel[idx]) select_click(idx, 0); A.ctx_item = true; }
    if (on_item && A.zoom <= Z_SMALL) cui_tooltip(ui, app_item_name(idx));
    else if (on_item) {
        name_lines nl;
        layout_name(ui, app_item_name(idx), c.rect.w - 10, &nl);
        if (nl.cut) cui_tooltip(ui, app_item_name(idx));
    }
    return -1;
}

void ui_grid_view(void) {
    cui_ctx *ui = A.ui;
    float cwmin = min_cell_w();
    int cols = (int)floorf((A.content_w - 2 * GPAD) / cwmin);
    if (cols < 1) cols = 1;
    A.cols = cols;
    A.cell_h = cell_height();
    int rows = (A.vis_n + cols - 1) / cols;
    int first, last;
    A.hover = -1;
    if (A.reset_scroll) { cui_scroll_to(ui, "grid", 0); A.reset_scroll = false; }
    ui_thumb_begin();
    cui_virtual_list_begin(ui, "grid", rows, A.cell_h, CUI_FILL, &first, &last);
    A.first_row = first; A.last_row = last;
    for (int r = first; r < last; r++) {
        int open_idx = -1;
        cui_push_id_int(ui, r);
        cui_row(ui, .width = CUI_FILL, .height = CUI_PX(A.cell_h), .pad_x = GPAD, .gap = 0) {
            for (int col = 0; col < cols; col++) {
                int idx = r * cols + col;
                cui_push_id_int(ui, col);
                int opened = build_cell(idx);
                if (opened >= 0) open_idx = opened;
                cui_pop_id(ui);
            }
        }
        cui_pop_id(ui);
        if (open_idx >= 0) { app_open_vis(open_idx); break; }
    }
    cui_virtual_list_end(ui);
    ui_thumb_end();
}

void ui_column_view(void) {
    cui_ctx *ui = A.ui;
    int rows, first, last, opened = -1;
    ui_columns_begin(&rows, &first, &last);
    A.column_rows = rows; A.cols = 1; A.cell_h = 32;
    A.first_row = first; A.last_row = last; A.hover = -1;
    ui_thumb_begin();
    for (int col = first; col < last; col++) {
        cui_push_id_int(ui, col);
        cui_column(ui, .width = CUI_PX(240), .height = CUI_FILL) {
            for (int row = 0; row < rows; row++) {
                int idx = col * rows + row;
                if (idx >= A.vis_n) break;
                cui_push_id_int(ui, row);
                int item = build_cell(idx);
                if (item >= 0) opened = item;
                cui_pop_id(ui);
            }
        }
        cui_pop_id(ui);
    }
    ui_columns_end();
    ui_thumb_end();
    if (opened >= 0) app_open_vis(opened);
}

/* ---- keyboard ---- */

static void move_cursor(int to, bool extend) {
    if (A.vis_n == 0) return;
    if (to < 0) to = 0;
    if (to >= A.vis_n) to = A.vis_n - 1;
    if (extend) {
        if (A.anchor < 0) A.anchor = A.cursor >= 0 ? A.cursor : to;
        app_select_range(A.anchor, to);
    } else {
        app_select_only(to);
        A.anchor = to;
    }
    A.cursor = to;
    if (A.zoom == Z_LIST) { ui_columns_reveal(to / (A.column_rows > 0 ? A.column_rows : 1)); return; }
    int row = to / A.cols;
    float ch = A.cell_h;
    float view_h = A.content_h > 50 ? A.content_h : 400;
    if (row <= A.first_row + 2) cui_scroll_to(A.ui, "grid", (float)row * ch);
    else if (row >= A.last_row - 3) cui_scroll_to(A.ui, "grid", (float)(row + 1) * ch - view_h);
}

void ui_grid_keys(void) {
    cui_ctx *ui = A.ui;
    if (A.vis_n == 0 || (cui_key_mods(ui) & (CUI_MOD_CTRL | CUI_MOD_ALT))) return;
    bool shift = (cui_key_mods(ui) & CUI_MOD_SHIFT) != 0;
    int cur = A.cursor >= 0 ? A.cursor : -1;
    int rows_per_page = (int)(A.content_h / A.cell_h);
    if (rows_per_page < 1) rows_per_page = 1;
    if (A.zoom == Z_LIST) {
        int rows = A.column_rows > 0 ? A.column_rows : 1;
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) move_cursor(cur < 0 ? 0 : cur + rows, shift);
        else if (cui_key_pressed(ui, CUI_KEY_LEFT)) move_cursor(cur < 0 ? 0 : cur - rows, shift);
        else if (cui_key_pressed(ui, CUI_KEY_DOWN)) move_cursor(cur + 1, shift);
        else if (cui_key_pressed(ui, CUI_KEY_UP)) move_cursor(cur - 1, shift);
        else if (cui_key_pressed(ui, CUI_KEY_HOME)) move_cursor(0, shift);
        else if (cui_key_pressed(ui, CUI_KEY_END)) move_cursor(A.vis_n - 1, shift);
        else if (cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) move_cursor(cur + rows * (int)(A.content_w / 240), shift);
        else if (cui_key_pressed(ui, CUI_KEY_PAGE_UP)) move_cursor(cur - rows * (int)(A.content_w / 240), shift);
        return;
    }
    if (cui_key_pressed(ui, CUI_KEY_RIGHT)) move_cursor(cur + 1, shift);
    else if (cui_key_pressed(ui, CUI_KEY_LEFT)) move_cursor(cur < 0 ? 0 : cur - 1, shift);
    else if (cui_key_pressed(ui, CUI_KEY_DOWN)) move_cursor(cur < 0 ? 0 : cur + A.cols, shift);
    else if (cui_key_pressed(ui, CUI_KEY_UP)) move_cursor(cur < 0 ? 0 : cur - A.cols, shift);
    else if (cui_key_pressed(ui, CUI_KEY_HOME)) move_cursor(0, shift);
    else if (cui_key_pressed(ui, CUI_KEY_END)) move_cursor(A.vis_n - 1, shift);
    else if (cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) move_cursor(cur + rows_per_page * A.cols, shift);
    else if (cui_key_pressed(ui, CUI_KEY_PAGE_UP)) move_cursor(cur - rows_per_page * A.cols, shift);
}
