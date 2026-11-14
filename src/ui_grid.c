/* The icon grid: an app component. Rows are virtual (only the visible rows are built), each row is one canvas
 * that paints its cells; clicks are mapped to cells by position. */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"

#define GPAD 16.0f
#define NAME_SIZE 13.0f
#define LINE_H 17.0f
enum { NAME_LINES = 2 };   /* GNOME Files 47: lines = 3; with 3 the taller cell fails motion_smoke 7-reflow-by-slider (items overlap 135 px) */

/* The size is continuous: icon, tile width, label width and spacing follow the slider value. */
static float icon_size(void) { return app_icon_size(); }
static float min_cell_w(void) { return A.zoom == Z_SMALL ? 240 : icon_size() + 48; }
static float cell_height(void) { return A.zoom == Z_SMALL ? icon_size() + 16 : icon_size() + 12 + 8 + NAME_LINES * LINE_H + 10; }

/* ---- name layout: GNOME Files 47's grid label (nautilus-grid-cell.ui: wrap-mode word-char, ellipsize middle, lines 3;
 * here NAME_LINES). Every line but the last is whole; a name that needs more keeps its start and its end on its last line, so the
 * extension stays visible. ---- */

typedef struct name_lines { char line[NAME_LINES][UI_NAME_BYTES]; int n; bool cut; } name_lines;

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

/* Start (byte offset) of the longest suffix of s that fits maxw, on a character boundary. */
static int fit_suffix(cui_ctx *ui, const char *s, int len, float maxw) {
    int bounds[261], nb = 0;
    for (int i = 0; i < len && nb < 260; i += utf8_len((unsigned char)s[i])) bounds[nb++] = i;
    int lo = 0, hi = nb;   /* characters left out at the front: the fewer, the wider */
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (text_w(ui, s + bounds[mid], len - bounds[mid]) <= maxw) hi = mid; else lo = mid + 1;
    }
    return lo < nb ? bounds[lo] : len;
}

static bool is_break(const char *s, int i) {
    unsigned char c = (unsigned char)s[i];
    if (c == ' ' || c == '-' || c == '_' || c == '.') return true;
    return c == 0xE2 && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x8C;   /* ZWNJ */
}

/* The last line of a name too wide for it: its start, an ellipsis, its end (at least the extension, with its dot). */
static void middle_cut(cui_ctx *ui, const char *rest, int rl, float maxw, char *out) {
    float avail = maxw - text_w(ui, "\xE2\x80\xA6", 3);
    int t = fit_suffix(ui, rest, rl, avail * .5f);
    int dot = -1;
    for (int i = rl - 1; i > 0; i--) if (rest[i] == '.') { dot = i; break; }
    if (dot >= 0 && dot < t && text_w(ui, rest + dot, rl - dot) <= avail * .75f) t = dot;   /* the extension is the last thing to give up */
    int e = fit_prefix(ui, rest, t, avail - text_w(ui, rest + t, rl - t));
    memcpy(out, rest, (size_t)e);
    memcpy(out + e, "\xE2\x80\xA6", 3);
    memcpy(out + e + 3, rest + t, (size_t)(rl - t) + 1);
}

static void layout_name(cui_ctx *ui, const char *name, float maxw, name_lines *out) {
    const char *rest = name;   /* the name itself: a copy into a fixed buffer cut a long name in the middle of a letter */
    out->n = 0; out->cut = false;
    while (out->n < NAME_LINES) {
        int rl = (int)strlen(rest), k = out->n++;
        if (text_w(ui, rest, rl) <= maxw) { memcpy(out->line[k], rest, (size_t)rl + 1); return; }
        if (k == NAME_LINES - 1) { middle_cut(ui, rest, rl, maxw, out->line[k]); out->cut = true; return; }
        int f = fit_prefix(ui, rest, rl, maxw);
        if (f < 1) f = utf8_len((unsigned char)rest[0]);
        int brk = f;
        for (int i = f - 1; i > f / 2; i--)   /* prefer a break after a space, dash or ZWNJ */
            if (is_break(rest, i)) { brk = i + (rest[i] == (char)0xE2 ? 3 : 1); break; }
        memcpy(out->line[k], rest, (size_t)brk);
        out->line[k][brk] = 0;
        rest += brk;
        while (*rest == ' ') rest++;
    }
}

int ui_grid_label_lines(int idx, char out[][UI_NAME_BYTES]) {
    name_lines nl;
    if (idx >= A.vis_n || A.cols < 1) return 0;
    layout_name(A.ui, app_item_name(idx), (A.content_w - 2 * GPAD) / (float)A.cols - 10, &nl);
    for (int k = 0; k < nl.n; k++) memcpy(out[k], nl.line[k], sizeof nl.line[k]);
    return nl.n;
}

/* ---- painting ---- */

typedef struct row_data { int idx; ui_thumb thumb; bool has_thumb; } row_data;

/* ---- reflow glide (150 ms) ----
 * When the column count changes, an item that stays in its row glides to its new place (cubic ease-out), and its width
 * and height glide with it. An item whose row changes never travels across the others: it crossfades. It fades out at
 * the place where it was (a ghost) and fades in at the place where it goes. The old copy leaves quickly (squared, gone at
 * half of the glide) and the new copy fades in over all of it (cubic ease-out), so duplicates show only in the first
 * third and the two copies add up to about 82% at the thinnest moment: the grid never dims below 75%. Only gliders and
 * settled items are at full opacity, and they never overlap; a ghost and an arrival are translucent while they share
 * places with others. A change mid-glide starts from what is drawn now.
 * Horizontal positions are document pixels. Vertical positions and heights are in rows (of the cell height), so a slider
 * that changes the row height while items glide moves ghosts and gliders together and they still do not meet. ---- */

#define GL_MAX 1024
#define GL_GHOSTS 2
#define GL_GHOST_END .5f   /* the old copy of an item that changes row has faded out at this share of the glide */

typedef struct cell_rec { int idx; row_data d; } cell_rec;
typedef struct ghost { float x, y, w, h, a; } ghost;   /* x, w in px; y (document) and h in rows */
typedef struct gitem {
    float ox, oy;       /* where the item was drawn when the glide started, relative to its final place (oy in rows) */
    float w0, h0;       /* its drawn size then (h0 in rows): the size glides too, so neighbours that touched keep touching */
    float a0;           /* its opacity then */
    bool fade;          /* fades in at its final place instead of gliding */
    int ng; ghost g[GL_GHOSTS];
} gitem;
static struct {
    bool valid, on, compact;
    unsigned frame;                     /* the frame that last drew the grid */
    const void *list; int vis_n;
    int cols; float cw, ch, sy, view_h; /* the layout and scroll drawn last frame */
    double t0;
    float rem, sl, gf;                  /* this frame: glide left, glide done (also the arrivals' opacity), the ghosts' opacity left */
    int base, cnt; gitem it[GL_MAX];
    cell_rec cells[GL_MAX]; int ncells;
    cell_rec extra[GL_MAX]; int nextra; /* items drawn only as ghosts: not built this frame */
    int anchor_frames; float anchor_ty;
    float label_cut;                    /* how much of the name of the copy being drawn is held back, 0 to 1 (0 outside a glide): see mask_labels */
    float force_sy;                     /* >= 0: the scroll this frame's glide is drawn with (see glide_plan), else the real one */
    bool collecting;
    bool hot, hot_click, have_org;      /* input while gliding: the pointer is over a cell; a click ended on one */
    float org_x, org_y;                 /* screen position of document (0, 0) */
} G;

bool ui_grid_gliding(void) { return G.on; }

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void glide_shape(float p) {
    float v = 1 - clamp01(p);
    G.rem = v * v * v; G.sl = 1 - G.rem;
    float q = 1 - clamp01(p / GL_GHOST_END);   /* an old copy is gone by the middle of the glide, the new copy takes all of it */
    G.gf = q * q;
}

static gitem *item_of(int i) { return G.on && i >= G.base && i < G.base + G.cnt ? &G.it[i - G.base] : NULL; }

static float main_alpha(const gitem *it) {
    if (!it) return 1;
    return it->a0 + (1 - it->a0) * G.sl;
}

/* Where item i is drawn now: place and size (document px). */
static cui_rect glide_rect(int i, int cols, float cw, float ch) {
    cui_rect r = { GPAD + (float)(i % cols) * cw, (float)(i / cols) * ch, cw, ch };
    gitem *it = item_of(i);
    if (it && !it->fade) {
        r.x += it->ox * G.rem; r.y += it->oy * ch * G.rem;
        r.w = it->w0 * G.rem + cw * G.sl; r.h = (it->h0 * G.rem + G.sl) * ch;
    }
    return r;
}

/* Where item i is drawn now (document px): clicks and the selection band use this, not the final place. */
cui_rect ui_grid_item_rect(int i, float width) {
    return glide_rect(i, A.cols, (width - 2 * GPAD) / (float)A.cols, A.cell_h);
}

static row_data make_data(int idx) {
    row_data d = { idx };
    if (idx < A.vis_n && ui_thumb_wanted(app_item(idx), app_item_name(idx))) {
        char path[FS_PATH_MAX]; app_path_of(idx, path, sizeof path);
        d.has_thumb = ui_thumb_lookup(path, app_item(idx)->mtime, ui_thumb_px(icon_size()), &d.thumb);
    }
    return d;
}

static const row_data *data_of(int idx) {
    if (G.ncells && idx >= G.cells[0].idx && idx - G.cells[0].idx < G.ncells) return &G.cells[idx - G.cells[0].idx].d;
    for (int k = 0; k < G.nextra; k++) if (G.extra[k].idx == idx) return &G.extra[k].d;
    return NULL;
}

static void paint_row(cui_ctx *ui, cui_rect r, void *user);

static void paint_faded(cui_ctx *ui, cui_rect r, const row_data *d, float alpha) {
    if (alpha < 0.01f) return;
    bool fx = alpha < 0.999f;
    if (fx) ui_push_alpha(alpha);
    paint_row(ui, r, (void *)d);
    if (fx) ui_pop_alpha();
}

/* ---- names that never overprint ----
 * Two copies at different opacities may share places while they crossfade, but their names must not print over each
 * other. The copy that is less opaque holds its name back where it comes within 12 px of the name of a more opaque copy
 * (fully at 4 px or less): a glider keeps its name whole and a fading copy yields. The icon and the fade are untouched. */

typedef struct copy { int idx, ghost; cui_rect r; float a, cut; cui_rect lb; } copy;   /* r, lb: view-relative px */
static copy CP[GL_MAX * (GL_GHOSTS + 1)];

/* The box of the name inside the cell r, as paint_row lays it out (a selected name has its pill: 6 px more each side). */
static cui_rect label_box(cui_ctx *ui, int idx, cui_rect r) {
    if (idx >= A.vis_n) return (cui_rect){ 0 };
    const char *name = app_item_name(idx);
    float pad = A.sel[idx] ? 6.0f : 0.0f;
    if (A.zoom <= Z_SMALL) {
        float w = text_w(ui, name, (int)strlen(name)), room = r.w - icon_size() - 36;
        if (w > room) w = room;
        return (cui_rect){ r.x + icon_size() + 20 - pad, r.y + r.h * .5f - 7, w + 2 * pad, LINE_H };
    }
    name_lines nl;
    layout_name(ui, name, r.w - 10, &nl);
    float w = 0;
    for (int k = 0; k < nl.n; k++) { float lw = text_w(ui, nl.line[k], (int)strlen(nl.line[k])); if (lw > w) w = lw; }
    return (cui_rect){ r.x + (r.w - w) * .5f - pad, r.y + 2 + icon_size() + 16 + 4, w + 2 * pad, nl.n * LINE_H };
}

static float box_gap(cui_rect a, cui_rect b) {   /* px between two boxes (negative: how deep they overlap) */
    float dx = fmaxf(b.x - (a.x + a.w), a.x - (b.x + b.w)), dy = fmaxf(b.y - (a.y + a.h), a.y - (b.y + b.h));
    return fmaxf(dx, dy);
}

static void mask_labels(cui_ctx *ui, int n) {
    for (int c = 0; c < n; c++) CP[c].lb = label_box(ui, CP[c].idx, CP[c].r);
    for (int c = 0; c < n; c++) {
        CP[c].cut = 0;
        if (CP[c].a >= .999f || CP[c].lb.w <= 0) continue;   /* gliders and settled items keep their names */
        for (int o = 0; o < n; o++) {
            if (o == c || CP[o].a < .02f || CP[o].lb.w <= 0) continue;
            if (CP[o].a < CP[c].a || (CP[o].a == CP[c].a && o > c)) continue;   /* only a more opaque copy holds the name back */
            float cut = 1 - clamp01((box_gap(CP[c].lb, CP[o].lb) - 4) / 8);
            if (cut > CP[c].cut) CP[c].cut = cut;
        }
    }
}

static void paint_glide(cui_ctx *ui, cui_rect view, cui_vec2 scroll) {
    if (G.force_sy >= 0) scroll.y = G.force_sy;
    float cw = (A.content_w - 2 * GPAD) / (float)A.cols;
    FILE *log = (FILE *)A.frame_log;   /* test mode: where everything is drawn this frame, for the overlap and click checks */
    int n = 0;
    for (int k = 0; k < G.cnt; k++) {   /* ghosts first, under everything */
        const gitem *it = &G.it[k];
        if (!it->ng || !data_of(G.base + k)) continue;
        for (int g = 0; g < it->ng; g++) {
            float a = it->g[g].a * G.gf;
            if (a < .01f) continue;
            CP[n++] = (copy){ G.base + k, 1, { it->g[g].x - scroll.x, it->g[g].y * A.cell_h - scroll.y, it->g[g].w, it->g[g].h * A.cell_h }, a };
        }
    }
    for (int k = 0; k < G.ncells; k++) {
        int idx = G.cells[k].idx;
        cui_rect r = glide_rect(idx, A.cols, cw, A.cell_h);
        CP[n++] = (copy){ idx, 0, { r.x - scroll.x, r.y - scroll.y, r.w, r.h }, main_alpha(item_of(idx)) };
    }
    mask_labels(ui, n);
    if (log) fprintf(log, "D %u %.1f %.1f %.1f %.1f", A.frame_no, view.h, scroll.y, view.w, A.content_w);
    for (int c = 0; c < n; c++) {
        const copy *p = &CP[c];
        G.label_cut = p->cut;
        paint_faded(ui, (cui_rect){ view.x + p->r.x, view.y + p->r.y, p->r.w, p->r.h }, data_of(p->idx), p->a);
        if (log) fprintf(log, " %c%d:%.1f:%.1f:%.3f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.3f", p->ghost ? 'g' : 'm', p->idx, p->r.x + scroll.x, p->r.y, p->a,
                         p->r.w, p->r.h, p->lb.x, p->lb.y, p->lb.w, p->lb.h, p->a * (1 - p->cut));
    }
    G.label_cut = 0;
    if (log) { fputc('\n', log); fflush(log); }
}

static void add_ghost(gitem *n, float x, float y, float w, float h, float a) {
    int at = n->ng;
    if (at == GL_GHOSTS) {   /* keep the two brightest */
        at = 0;
        for (int k = 1; k < GL_GHOSTS; k++) if (n->g[k].a < n->g[at].a) at = k;
        if (n->g[at].a >= a) return;
    } else n->ng++;
    n->g[at] = (ghost){ x, y, w, h, a };
}

/* Where item i, as it was drawn at the end of last frame, is: position and opacity. */
static void drawn_before(int i, float *x, float *y, float *w, float *h, float *a) {
    cui_rect r = glide_rect(i, G.cols, G.cw, G.ch);
    *x = r.x; *y = r.y; *w = r.w; *h = r.h; *a = 1;
    const gitem *it = item_of(i);
    if (it) *a = main_alpha(it);
}

/* Called before the cells are built: anchor the scroll, and start or retarget the glide. */
static void glide_plan(cui_ctx *ui, int cols) {
    float cur_y, cur_ty, vh;
    ui_scroll_get("grid", &cur_y, &cur_ty, &vh);
    double now = cui_time(ui);
    float cw = (A.content_w - 2 * GPAD) / (float)cols;
    bool same = G.valid && G.frame + 1 == A.frame_no && G.list == (const void *)A.list && G.vis_n == A.vis_n && !A.reset_scroll;
    G.force_sy = -1;
    G.ncells = 0; G.nextra = 0; G.hot = G.hot_click = G.have_org = false;
    if (!same) { G.on = false; G.anchor_frames = 0; return; }
    bool kind = (A.zoom == Z_SMALL) != G.compact;   /* compact rows to tiles: the whole view fades instead (ui_zoom.c) */
    bool changed = cols != G.cols || fabsf(A.cell_h - G.ch) > 0.01f;
    if (changed) {   /* keep the item at the top of the view at the top */
        float row_f = G.sy / G.ch, whole = floorf(row_f);
        int a = (int)whole * G.cols;
        G.anchor_ty = ((float)(a / cols) + (row_f - whole)) * A.cell_h;
        G.anchor_frames = 2;
    }
    if (G.anchor_frames > 0 && ui_wheel_scrolling()) G.anchor_frames = 0;
    bool jumped = false;
    if (G.anchor_frames > 0) { ui_scroll_jump("grid", G.anchor_ty); cur_ty = G.anchor_ty; G.anchor_frames--; jumped = true; }
    if (G.on) {
        double t = (now - G.t0) / app_glide_seconds();
        if (t >= 1) G.on = false; else glide_shape((float)t);
    }
    if (kind) G.on = false;
    if (cols != G.cols && !kind && A.vis_n > 0 && app_animations()) {
        float view_h = G.view_h > 0 ? G.view_h : A.content_h, sy_new = cur_ty;
        int i0 = (int)floorf(sy_new / A.cell_h) - 1, i1 = (int)ceilf((sy_new + view_h) / A.cell_h) + 1;
        if (i0 < 0) i0 = 0;
        i0 *= cols; i1 *= cols;
        int o0 = (int)floorf(G.sy / G.ch) * G.cols, o1 = ((int)ceilf((G.sy + view_h) / G.ch)) * G.cols;
        if (o0 < i0) i0 = o0;
        if (o1 > i1) i1 = o1;
        if (i0 < 0) i0 = 0;
        if (i1 > A.vis_n) i1 = A.vis_n;
        if (i1 - i0 > GL_MAX) i1 = i0 + GL_MAX;
        static gitem nt[GL_MAX];
        bool any = false;
        for (int i = i0; i < i1; i++) {
            gitem n = { 0 };
            n.a0 = 1;
            float x0, y0, w0, h0, a0;
            drawn_before(i, &x0, &y0, &w0, &h0, &a0);
            n.w0 = cw; n.h0 = 1;
            float screen_y = y0 - G.sy;
            bool old_vis = screen_y + G.ch > 0 && screen_y < view_h;
            float dx = GPAD + (float)(i % cols) * cw, dy = (float)(i / cols) * A.cell_h;
            bool new_vis = dy - sy_new + A.cell_h > 0 && dy - sy_new < view_h;
            const gitem *old = item_of(i);
            if (old) for (int g = 0; g < old->ng; g++) {   /* a ghost still fading carries on from its opacity now */
                float a = old->g[g].a * G.gf;
                /* it stays where it is on the screen: its row is rebased on the scroll the anchor has just moved to */
                float gy = old->g[g].y * G.ch - G.sy;
                if (a > 0.02f) add_ghost(&n, old->g[g].x, (gy + sy_new) / A.cell_h, old->g[g].w, old->g[g].h, a);
            }
            if (old_vis && !(old && old->fade) && i / G.cols == i / cols) {   /* stays in its row (and was not just fading in): glides */
                n.ox = x0 - dx; n.oy = ((screen_y + sy_new) - dy) / A.cell_h; n.a0 = a0; n.w0 = w0; n.h0 = h0 / A.cell_h;
            } else {   /* changes row: never travels across the others */
                if (old_vis && a0 > 0.02f) add_ghost(&n, x0, (screen_y + sy_new) / A.cell_h, w0, h0 / A.cell_h, a0);
                if (new_vis) { n.fade = true; n.a0 = 0; }
            }
            if (n.ng || n.fade || fabsf(n.ox) > 0.5f || fabsf(n.oy) * A.cell_h > 0.5f || n.a0 < 0.99f) any = true;
            nt[i - i0] = n;
        }
        G.on = any;
        if (any) {
            memcpy(G.it, nt, sizeof(gitem) * (size_t)(i1 - i0));
            G.base = i0; G.cnt = i1 - i0; G.t0 = now;
            glide_shape(0);
        }
    }
    /* The list clamps the jump to the content height it measured last frame, so on the frame of a change the scroll can fall
     * short of the anchor (by hundreds of px when the columns shrink, which makes the list longer) and only reaches it a frame
     * later. The glide draws everything, so it draws with the anchored scroll on those frames: nothing jumps. */
    if (G.on && jumped) G.force_sy = cur_ty;
    if (G.on) cui_request_redraw(ui);
}

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
    } else { ui_set_dim(e->flags & FS_INCOMING); ui_draw_file(ui, cx, cy, icon, app_item_name(i), e->kind); ui_set_dim(false); }
    char name[UI_NAME_BYTES];
    const char *full = app_item_name(i);
    float width = r.w - icon - 36;
    int len = (int)strlen(full), n = fit_prefix(ui, full, len, width);
    if (n < len) n = fit_prefix(ui, full, len, width - text_w(ui, "\xE2\x80\xA6", 3));
    memcpy(name, full, (size_t)n); name[n] = 0;
    if (n < len) memcpy(name + n, "\xE2\x80\xA6", 4);
    if (G.label_cut > .99f) return;
    bool lfx = G.label_cut > .001f;
    if (lfx) ui_push_alpha(1 - G.label_cut);
    ui_set_dim(e->flags & FS_INCOMING);
    cui_draw_text(ui, CUI_FONT_BODY, NAME_SIZE, (cui_vec2){ r.x + icon + 20, cy - 7 }, ui_dimmed(ui, CUI_COLOR_FOREGROUND), name, -1);
    ui_set_dim(false);
    if (lfx) ui_pop_alpha();
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
        else { ui_set_dim(e->flags & FS_INCOMING); ui_draw_file(ui, cx, icy, icon, name, e->kind); ui_set_dim(false); }
        if (A.loc != LOC_TRASH && app_is_starred(i)) {
            cui_vec2 sp = { ib.x + ib.w - 8, ib.y + 12 };
            cui_draw_circle(ui, sp, 10, CUI_COLOR_SURFACE_RAISED);
            ui_draw_star(ui, sp.x, sp.y + 0.5f, 7.5f, CUI_COLOR_WARNING);
        }
        if (G.label_cut > .99f) break;
        bool lfx = G.label_cut > .001f;
        if (lfx) ui_push_alpha(1 - G.label_cut);
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
                cui_draw_text(ui, CUI_FONT_BODY, NAME_SIZE, (cui_vec2){ p.x, p.y + 1 },
                              (e->flags & FS_INCOMING) ? cui_alpha(cui_color_of(ui, CUI_COLOR_FOREGROUND), 0.5f) : cui_color_of(ui, CUI_COLOR_FOREGROUND), nl.line[k], -1);
            }
        }
        if (lfx) ui_pop_alpha();
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

/* Hover, click, right click and tooltip for the item under the pointer; returns the item to open on a double click. */
#define TIP_ID(idx) (0x7C100000u + (cui_id)(idx))

static int item_input(int idx, bool on_item, bool clicked, cui_rect cell) {
    cui_ctx *ui = A.ui;
    if (on_item) A.hover = idx;
    if (clicked) {
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
    if (on_item) {   /* GNOME Files: the name's tooltip only when the name is cut short (on_label_query_tooltip) */
        name_lines nl = { .cut = true };
        if (A.zoom > Z_SMALL) layout_name(ui, app_item_name(idx), cell.w - 10, &nl);
        if (nl.cut) ui_item_tooltip(ui, TIP_ID(idx), app_item_name(idx), label_box(ui, idx, cell));
    } else if (idx >= 0) ui_item_tooltip(ui, TIP_ID(idx), NULL, (cui_rect){ 0 });   /* the next hover waits its delay again */
    return -1;
}

/* While items glide, their canvases sit at the final places but the items are drawn elsewhere: the pointer is
 * tested against what is drawn. The canvases only tell that the pointer is over a cell and that a click ended there. */
static int glide_input(void) {
    cui_vec2 m = cui_mouse_pos(A.ui);
    int hit = -1;
    cui_rect hit_r = { 0 };
    if (G.hot && G.have_org) {
        float cw = (A.content_w - 2 * GPAD) / (float)A.cols;
        float best = -1;
        for (int k = G.ncells - 1; k >= 0; k--) {   /* the most opaque item under the pointer; of equals, the one drawn last */
            int idx = G.cells[k].idx;
            if (idx >= A.vis_n) continue;
            cui_rect r = glide_rect(idx, A.cols, cw, A.cell_h);
            r.x += G.org_x; r.y += G.org_y;
            float a = main_alpha(item_of(idx));
            if (a > best + 0.001f && m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h) { hit = idx; best = a; hit_r = r; }
        }
    }
    return item_input(hit, hit >= 0, G.hot_click, hit_r);
}

static int build_cell(int idx) {
    cui_ctx *ui = A.ui;
    row_data d = make_data(idx);
    cui_canvas_result c;
    if (G.collecting && G.ncells < GL_MAX) {   /* drawn by the overlay at its gliding place; the canvas only takes input */
        G.cells[G.ncells++] = (cell_rec){ idx, d };
        c = cui_canvas(ui, "cell", CUI_FILL, CUI_PX(A.cell_h), NULL, NULL, 0);
    } else c = cui_canvas(ui, "cell", CUI_FILL, CUI_PX(A.cell_h), paint_row, &d, sizeof d);
    /* the test probe is the last built item that is on screen in its final place, so a glide moves it */
    if (A.frame_log && idx >= A.probe_idx && !(item_of(idx) && item_of(idx)->fade) && (G.view_h <= 0 || (float)(idx / A.cols) * A.cell_h + A.cell_h * .5f < G.sy + G.view_h)) {
        cui_rect r = glide_rect(idx, A.cols, (A.content_w - 2 * GPAD) / (float)A.cols, A.cell_h);
        A.probe_idx = idx; A.probe_x = r.x; A.probe_y = r.y;
    }
    if ((A.zoom == Z_LIST && idx == A.first_row * A.column_rows) || (A.zoom != Z_LIST && idx == A.first_row * A.cols)) {
        A.first_rect = (cui_rect){ c.rect.x - GPAD, c.rect.y, c.rect.w * (float)A.cols + 2 * GPAD, c.rect.h };
    }
    if (G.collecting) {
        G.org_x = c.rect.x - (GPAD + (float)(idx % A.cols) * ((A.content_w - 2 * GPAD) / (float)A.cols));
        G.org_y = c.rect.y - (float)(idx / A.cols) * A.cell_h;
        G.have_org = true;
        if (c.hovered) G.hot = true;
        if (c.clicked) G.hot_click = true;
        return -1;
    }
    return item_input(idx, c.hovered && idx < A.vis_n, c.clicked, c.rect);
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
    double now = cui_time(ui);
    A.thumb_settled = A.size_t <= 0 || now - A.size_t >= 0.2;   /* sharper thumbnails only once the size stays still */
    if (!A.thumb_settled) cui_request_wakeup(ui, (float)(0.2 - (now - A.size_t)) + 0.01f);
    glide_plan(ui, cols);
    if (A.reset_scroll) { cui_scroll_to(ui, "grid", 0); A.reset_scroll = false; }
    G.collecting = G.on;
    A.probe_idx = -1;
    ui_thumb_begin();
    cui_virtual_list_begin(ui, "grid", rows, A.cell_h, CUI_FILL, &first, &last);
    A.first_row = first; A.last_row = last;
    ui_band_begin();
    if (G.on) ui_overlay_set(paint_glide);
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
    if (G.collecting) {
        for (int k = 0; k < G.cnt; k++)   /* items that are only drawn as ghosts still need their picture data */
            if (G.it[k].ng && !data_of(G.base + k) && G.nextra < GL_MAX) G.extra[G.nextra++] = (cell_rec){ G.base + k, make_data(G.base + k) };
        int opened = glide_input();
        if (opened >= 0) app_open_vis(opened);
    }
    ui_thumb_end();
    G.collecting = false;
    G.valid = true; G.compact = A.zoom == Z_SMALL; G.frame = A.frame_no; G.list = A.list; G.vis_n = A.vis_n;
    G.cols = cols; G.cw = (A.content_w - 2 * GPAD) / (float)cols; G.ch = A.cell_h;
    { float y, ty; ui_scroll_get("grid", &y, &ty, &G.view_h); G.sy = G.on && G.force_sy >= 0 ? G.force_sy : y; }   /* the scroll as drawn */
}

void ui_column_view(void) {
    cui_ctx *ui = A.ui;
    int rows, first, last, opened = -1;
    ui_columns_begin(&rows, &first, &last);
    A.column_rows = rows; A.cols = 1; A.cell_h = 32;
    G.valid = false; G.on = false; G.collecting = false;
    A.first_row = first; A.last_row = last; A.hover = -1;
    ui_band_begin();
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
