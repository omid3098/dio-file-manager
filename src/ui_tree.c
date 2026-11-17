/* The disk tree: the open folder as a treemap. Every child of the folder is a rectangle whose area is its size; a folder shows its own
 * children inside it, one level deeper. It reads what the list view reads (the listing, the folder-size engine of foldersize.c) and
 * adds only the listings of the big folders' children, which run on the listing's own worker threads. Selection, open, the context
 * menu, Delete and the clipboard are the list's: a rectangle is a list item (A.vis index). No walk or listing happens on the window's thread. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "ui.h"
#include "foldersize.h"
#include "treemap.h"

#define PAD 10.0f
#define HEAD_H 30.0f
#define TOP_MAX 600            /* rectangles at the first level; the rest join "N small items" */
#define SMALL_AREA 2400.0f     /* a rectangle below this many square points is not drawn on its own */
#define NEST_MIN_W 120.0f      /* a folder this big shows its children inside */
#define NEST_MIN_H 70.0f
#define NEST_HEAD 20.0f
#define NEST_SMALL 380.0f
#define NEST_MAX 48
#define POOL_MAX (NEST_MAX * 48)
#define SUBS_MAX 24
#define KEEP_FILES 64
#define KEEP_DIRS 256
#define LOAD_AT_ONCE 4

enum { TK_ITEM, TK_SMALL, TK_FREE, TK_OTHER };

typedef struct tnode { char *name; uint64_t size; uint8_t kind; bool dir; } tnode;   /* size: a file's; a folder's comes from the engine */
typedef struct sub {
    char path[FS_PATH_MAX];
    fs_list *l;                 /* while it is read */
    tnode *nodes; int n;
    uint64_t rest; int rest_n;  /* files that were not kept, and links */
    bool done;
} sub;

typedef struct nrect { tm_rect r; int node; uint64_t size; int small_n; } nrect;   /* node -1: the block of small items */
typedef struct titem { int kind, vis, sub, noff, ncnt, small_n; uint64_t size; tm_rect r; } titem;

static bool g_tree;
static sub S[SUBS_MAX];
static int sub_n;
static char sub_root[FS_PATH_MAX];
static fs_list *sub_list;
static unsigned sub_ver;
static bool was_shown;
static bool sub_hidden;
static unsigned sub_gen;

static titem T[TOP_MAX + 8];
static int T_n;
static nrect NP[POOL_MAX];
static int NP_n;
static char head_text[160];
static uint64_t g_total;
static int g_denied, g_zero, g_pending;
static bool g_root;
static uint64_t root_total, root_free;
static struct { float w, h; fs_list *list; int vis_n, sort_key; bool desc, hidden; unsigned fv, sv; bool valid; } sig;
static struct ent { uint64_t size; int vis; } *E;
static int E_cap;

bool ui_tree_shown(void) { return g_tree && A.loc == LOC_FOLDER && !A.searching; }
bool app_tree_on(void) { return g_tree; }

/* ---- the folder's children below the first level ---- */

static void sub_clear(sub *s) {
    if (s->l) fs_list_release(s->l);
    for (int i = 0; i < s->n; i++) free(s->nodes[i].name);
    free(s->nodes);
    memset(s, 0, sizeof *s);
}

static void subs_reset(void) {
    for (int i = 0; i < sub_n; i++) sub_clear(&S[i]);
    sub_n = 0; sub_ver++;
    sig.valid = false;
}

static void join(const char *dir, const char *name, char *out, size_t cap) {
    size_t n = strlen(dir);
    snprintf(out, cap, "%s%s%s", dir, n && dir[n - 1] == '\\' ? "" : "\\", name);
}

static void extract(sub *s) {
    fs_list *l = s->l;
    tnode *top = calloc(KEEP_FILES + KEEP_DIRS, sizeof *top);
    int nf = 0, nd = 0;
    if (fs_list_state(l) == FS_DONE && top) {
        for (int i = 0; i < l->count; i++) {
            const fs_entry *e = &l->items[i];
            if ((e->flags & FS_HIDDEN) && !A.show_hidden) continue;
            if (e->flags & FS_LINK) { s->rest_n++; continue; }
            if (e->flags & FS_DIR) {
                if (nd < KEEP_DIRS) { top[KEEP_FILES + nd].name = _strdup(fs_name(l, i)); top[KEEP_FILES + nd].dir = true; top[KEEP_FILES + nd].kind = FS_KIND_FOLDER; nd++; }
                else s->rest_n++;
                continue;
            }
            int at = nf;   /* the KEEP_FILES biggest files, kept sorted from the biggest */
            if (nf == KEEP_FILES) {
                if (e->size <= top[nf - 1].size) { s->rest += e->size; s->rest_n++; continue; }
                s->rest += top[nf - 1].size; s->rest_n++; free(top[nf - 1].name); nf--; at = nf;
            }
            while (at > 0 && top[at - 1].size < e->size) { top[at] = top[at - 1]; at--; }
            top[at] = (tnode){ _strdup(fs_name(l, i)), e->size, e->kind, false };
            nf++;
        }
        s->nodes = calloc((size_t)(nf + nd) + 1, sizeof *s->nodes);
        if (s->nodes) {
            for (int i = 0; i < nf; i++) s->nodes[s->n++] = top[i];
            for (int i = 0; i < nd; i++) {
                s->nodes[s->n++] = top[KEEP_FILES + i];
                char p[FS_PATH_MAX + 300];
                join(s->path, top[KEEP_FILES + i].name, p, sizeof p);
                fsz_want(p);   /* the cache keeps this size while it is shown (and measures it again if it was dropped) */
            }
        }
    }
    free(top);
    fs_list_release(l);
    s->l = NULL; s->done = true;
    sub_ver++;
}

static int find_sub(const char *path) {
    for (int i = 0; i < sub_n; i++) if (!strcmp(S[i].path, path)) return i;
    return -1;
}

static void subs_frame(void) {
    if (A.list != sub_list || strcmp(sub_root, A.path) || sub_hidden != A.show_hidden || sub_gen != A.nav_gen) {   /* the listings follow Ctrl+H and F5 */
        subs_reset();
        sub_list = A.list; sub_hidden = A.show_hidden; sub_gen = A.nav_gen;
        snprintf(sub_root, sizeof sub_root, "%s", A.path);
        g_root = strlen(A.path) == 3 && A.path[1] == ':';
        root_total = root_free = 0;
        if (g_root) {
            wchar_t w[8];
            ULARGE_INTEGER fr, tot, tf;
            if (fs_u8_to_w(A.path, w, 8) > 0 && GetDiskFreeSpaceExW(w, &fr, &tot, &tf)) { root_total = tot.QuadPart; root_free = tf.QuadPart; }
        }
    }
    for (int i = 0; i < sub_n; i++)
        if (S[i].l && fs_list_state(S[i].l) != FS_LOADING) extract(&S[i]);
    for (int i = 0; i < sub_n; i++) if (S[i].l) { cui_request_wakeup(A.ui, 0.03f); break; }   /* a finished listing must repaint */
}

/* Start the listings of the biggest folders that are big enough to show their children. Called after the layout, largest first. */
static void subs_request(void) {
    int loading = 0;
    for (int i = 0; i < sub_n; i++) if (S[i].l) loading++;
    for (int k = 0; k < T_n && loading < LOAD_AT_ONCE && sub_n < SUBS_MAX; k++) {
        const titem *t = &T[k];
        if (t->kind != TK_ITEM || !(app_item(t->vis)->flags & FS_DIR) || t->r.w < NEST_MIN_W || t->r.h < NEST_MIN_H) continue;
        char p[FS_PATH_MAX + 300];
        join(A.path, app_item_name(t->vis), p, sizeof p);
        if (find_sub(p) >= 0 || strlen(p) >= FS_PATH_MAX) continue;
        sub *s = &S[sub_n];
        memset(s, 0, sizeof *s);
        snprintf(s->path, sizeof s->path, "%s", p);
        s->l = fs_list_start(p, FS_SORT_NAME, false);
        if (!s->l) { s->done = true; }
        sub_n++; loading++;
    }
}

/* ---- the layout, kept until something it depends on changes ---- */

static int cmp_ent(const void *a, const void *b) {
    const struct ent *x = a, *y = b;
    return x->size < y->size ? 1 : x->size > y->size ? -1 : x->vis - y->vis;
}
typedef struct lrow { uint64_t size; int kind, vis; } lrow;
static int cmp_row(const void *a, const void *b) {
    const lrow *x = a, *y = b;
    return x->size < y->size ? 1 : x->size > y->size ? -1 : x->vis - y->vis;
}
typedef struct nrow { uint64_t size; int node; } nrow;
static int cmp_nrow(const void *a, const void *b) {
    const nrow *x = a, *y = b;
    return x->size < y->size ? 1 : x->size > y->size ? -1 : x->node - y->node;
}

static void pct_text(char *out, size_t cap, uint64_t part, uint64_t whole) {
    double p = whole ? 100.0 * (double)part / (double)whole : 0;
    if (p >= 10) snprintf(out, cap, "%.0f%%", p);
    else if (p >= 0.1) snprintf(out, cap, "%.1f%%", p);
    else snprintf(out, cap, "<0.1%%");
}

static void nest_layout(titem *t, const sub *s) {
    tm_rect in = { t->r.x + 3, t->r.y + NEST_HEAD, t->r.w - 6, t->r.h - NEST_HEAD - 3 };
    t->noff = NP_n; t->ncnt = 0;
    if (in.w < 20 || in.h < 20 || s->n == 0) return;
    nrow rows[KEEP_FILES + KEEP_DIRS];
    int n = 0;
    uint64_t sum = 0, small = s->rest;
    int small_n = s->rest_n;
    char p[FS_PATH_MAX + 300];
    for (int i = 0; i < s->n; i++) {
        uint64_t v = s->nodes[i].size;
        if (s->nodes[i].dir) {
            join(s->path, s->nodes[i].name, p, sizeof p);
            if (fsz_get(p, &v) != FSZ_DONE) v = 0;
        }
        if (v) rows[n++] = (nrow){ v, i }; else small_n++;
        sum += v;
    }
    sum += small;
    if (!sum) return;
    uint64_t rem = t->size > sum ? t->size - sum : 0;   /* bytes the listing does not show (hidden files, what could not be read): empty space, so the shares stay the folder's */
    sum += rem;
    qsort(rows, (size_t)n, sizeof *rows, cmp_nrow);
    double area = (double)in.w * in.h;
    int keep = 0;
    while (keep < n && keep < NEST_MAX - 1 && (double)rows[keep].size / (double)sum * area >= NEST_SMALL) keep++;
    for (int i = keep; i < n; i++) { small += rows[i].size; small_n++; }
    double v[NEST_MAX + 1];
    nrect tmp[NEST_MAX + 1];
    int m = 0;
    for (int i = 0; i < keep; i++) { v[m] = (double)rows[i].size; tmp[m] = (nrect){ .node = rows[i].node, .size = rows[i].size }; m++; }
    for (int k = 0; k < 2; k++) {   /* the small items, then the unlisted remainder, sorted in among the others */
        uint64_t sz = k ? rem : small;
        if (!sz || m >= NEST_MAX) continue;
        int at = m;
        while (at > 0 && v[at - 1] < (double)sz) { v[at] = v[at - 1]; tmp[at] = tmp[at - 1]; at--; }
        v[at] = (double)sz; tmp[at] = (nrect){ .node = k ? -2 : -1, .size = sz, .small_n = k ? 0 : small_n }; m++;
    }
    tm_rect out[NEST_MAX + 1];
    tm_layout(v, m, in, out);
    for (int i = 0; i < m && NP_n < POOL_MAX; i++) { tmp[i].r = out[i]; NP[NP_n++] = tmp[i]; t->ncnt++; }
}

static void compute(float w, float h) {
    unsigned fv = fsz_version();
    if (sig.valid && sig.w == w && sig.h == h && sig.list == A.list && sig.vis_n == A.vis_n && sig.sort_key == A.sort_key &&
        sig.desc == A.sort_desc && sig.hidden == A.show_hidden && sig.fv == fv && sig.sv == sub_ver) return;
    sig.w = w; sig.h = h; sig.list = A.list; sig.vis_n = A.vis_n; sig.sort_key = A.sort_key; sig.desc = A.sort_desc;
    sig.hidden = A.show_hidden; sig.fv = fv; sig.sv = sub_ver; sig.valid = true;
    T_n = 0; NP_n = 0; g_total = 0; g_denied = g_zero = g_pending = 0;
    head_text[0] = 0;
    tm_rect area = { PAD, HEAD_H + 2, w - 2 * PAD, h - HEAD_H - 2 - PAD };
    bool measured = fsz_eligible(A.path);
    if (area.w < 30 || area.h < 30) return;
    if (A.vis_n > E_cap) { free(E); E_cap = A.vis_n + 256; E = malloc((size_t)E_cap * sizeof *E); if (!E) { E_cap = 0; return; } }
    int en = 0;
    uint64_t sum = 0;
    char p[FS_PATH_MAX + 300];
    for (int i = 0; i < A.vis_n; i++) {
        const fs_entry *e = app_item(i);
        uint64_t v = e->size;
        if (e->flags & FS_DIR) {
            v = 0;
            if (!(e->flags & FS_LINK) && measured) {
                join(A.path, app_item_name(i), p, sizeof p);
                int r = fsz_get(p, &v);
                if (r == FSZ_DENIED) { g_denied++; v = 0; }
                else if (r != FSZ_DONE) { g_pending++; v = 0; }
            }
        }
        if (v) { E[en].size = v; E[en].vis = i; en++; sum += v; } else g_zero++;
    }
    qsort(E, (size_t)en, sizeof *E, cmp_ent);
    uint64_t other = 0, total = sum;
    if (g_root && root_total) {
        uint64_t used = root_total > root_free ? root_total - root_free : 0;
        other = used > sum ? used - sum : 0;
        total = sum + other + root_free;
    }
    g_total = total;
    /* the header line */
    char a[48], b[48];
    if (g_root && root_total) {
        char c[48];
        fs_format_size(a, sizeof a, root_total > root_free ? root_total - root_free : 0); fs_format_size(b, sizeof b, root_total);
        fs_format_size(c, sizeof c, root_free);
        snprintf(head_text, sizeof head_text, "%s of %s used \xC2\xB7 %s free", a, b, c);
    } else {
        fs_format_size(a, sizeof a, sum);
        snprintf(head_text, sizeof head_text, "%s in %d items", a, A.vis_n);
    }
    size_t l = strlen(head_text);
    if (!measured) snprintf(head_text + l, sizeof head_text - l, " \xC2\xB7 folder sizes are not measured on this drive");
    else if (g_pending) snprintf(head_text + l, sizeof head_text - l, " \xC2\xB7 measuring\xE2\x80\xA6");
    else if (g_denied) snprintf(head_text + l, sizeof head_text - l, " \xC2\xB7 %d folder%s can't be read", g_denied, g_denied == 1 ? "" : "s");
    if (!total) return;
    double acre = (double)area.w * area.h;
    int keep = 0;
    while (keep < en && keep < TOP_MAX - 4 && (double)E[keep].size / (double)total * acre >= SMALL_AREA) keep++;
    uint64_t small = 0;
    for (int i = keep; i < en; i++) small += E[i].size;
    int small_n = en - keep + g_zero;
    static lrow *rows; static int rows_cap;
    if (keep + 4 > rows_cap) { free(rows); rows_cap = keep + 64; rows = malloc((size_t)rows_cap * sizeof *rows); if (!rows) { rows_cap = 0; return; } }
    int m = 0;
    for (int i = 0; i < keep; i++) rows[m++] = (lrow){ E[i].size, TK_ITEM, E[i].vis };
    if (small) rows[m++] = (lrow){ small, TK_SMALL, -1 };
    if (g_root && root_free) rows[m++] = (lrow){ root_free, TK_FREE, -1 };
    if (other) rows[m++] = (lrow){ other, TK_OTHER, -1 };
    qsort(rows, (size_t)m, sizeof *rows, cmp_row);
    static double *vals; static tm_rect *rects; static int vals_cap;
    if (m > vals_cap) { free(vals); free(rects); vals_cap = m + 64; vals = malloc((size_t)vals_cap * sizeof *vals); rects = malloc((size_t)vals_cap * sizeof *rects); if (!vals || !rects) { vals_cap = 0; return; } }
    for (int i = 0; i < m; i++) vals[i] = (double)rows[i].size;
    tm_layout(vals, m, area, rects);
    for (int i = 0; i < m; i++) {
        titem *t = &T[T_n++];
        *t = (titem){ .kind = rows[i].kind, .vis = rows[i].vis, .sub = -1, .size = rows[i].size, .r = rects[i], .small_n = rows[i].kind == TK_SMALL ? small_n : 0 };
        if (t->kind == TK_ITEM && (app_item(t->vis)->flags & FS_DIR) && t->r.w >= NEST_MIN_W && t->r.h >= NEST_MIN_H) {
            join(A.path, app_item_name(t->vis), p, sizeof p);
            int si = find_sub(p);
            if (si >= 0 && S[si].done && S[si].n) { t->sub = si; nest_layout(t, &S[si]); }
        }
    }
}

/* ---- drawing ---- */

static cui_color tint(cui_ctx *ui, cui_color base, float t) { return cui_mix(cui_color_of(ui, CUI_COLOR_SURFACE), cui_color_of(ui, base), t); }

static void text_at(cui_ctx *ui, float x, float y, float size, cui_color c, const char *s) { cui_draw_text(ui, CUI_FONT_BODY, size, (cui_vec2){ x, y }, c, s, -1); }

/* name, then percent and size under it when the box is tall enough; nothing when it is too small to read */
static void label(cui_ctx *ui, cui_rect b, const char *name, const char *meta, float nsize) {
    float maxw = b.w - 12;
    if (maxw < 30 || b.h < 18) return;
    char out[UI_NAME_BYTES];
    cui_color fg = cui_color_of(ui, CUI_COLOR_FOREGROUND), sec = cui_color_of(ui, CUI_COLOR_TEXT_SECONDARY);
    cui_push_clip(ui, b);
    ui_fit_name(name, maxw, nsize, out);
    text_at(ui, b.x + 6, b.y + 3, nsize, fg, out);
    if (meta && b.h >= 38) {   /* the percent and the size whole, else the percent alone, else nothing: never cut in the middle */
        float ms = nsize - 1 < 12.0f ? 12.0f : nsize - 1;   /* never under 12 px */
        char pc[32];
        snprintf(pc, sizeof pc, "%s", meta);
        char *dot = strstr(pc, " \xC2\xB7 ");
        if (dot) *dot = 0;
        const char *show = ui_body_text_w(meta) * ms / 14.0f <= maxw ? meta : ui_body_text_w(pc) * ms / 14.0f <= maxw ? pc : NULL;
        if (show) text_at(ui, b.x + 6, b.y + 3 + nsize + 3, ms, sec, show);
    }
    cui_pop_clip(ui);
}

static void hatch(cui_ctx *ui, cui_rect b) {
    cui_color c = cui_alpha(cui_color_of(ui, CUI_COLOR_FOREGROUND), 0.10f);
    cui_push_clip(ui, b);
    for (float d = -b.h; d < b.w; d += 9)
        cui_draw_line(ui, (cui_vec2){ b.x + d, b.y + b.h }, (cui_vec2){ b.x + d + b.h, b.y }, 1.0f, c);
    cui_pop_clip(ui);
}

static void draw_nested(cui_ctx *ui, const titem *t, cui_rect ox, const sub *s) {
    for (int i = 0; i < t->ncnt; i++) {
        const nrect *n = &NP[t->noff + i];
        cui_rect b = { ox.x + n->r.x + 0.5f, ox.y + n->r.y + 0.5f, n->r.w - 1, n->r.h - 1 };
        if (b.w < 2 || b.h < 2 || n->node == -2) continue;   /* -2: the unlisted remainder stays empty */
        char meta[64], pc[16], sz[40], nm[64];
        pct_text(pc, sizeof pc, n->size, t->size); fs_format_size(sz, sizeof sz, n->size);
        snprintf(meta, sizeof meta, "%s \xC2\xB7 %s", pc, sz);
        if (n->node < 0) {
            cui_draw_rect(ui, b, CUI_CORNERS(3), tint(ui, CUI_COLOR_FOREGROUND, 0.10f));
            snprintf(nm, sizeof nm, "%d small items", n->small_n);
            label(ui, b, nm, meta, 12.0f);
        } else {
            const tnode *nd = &s->nodes[n->node];
            cui_color base = nd->dir ? CUI_COLOR_ACCENT : ui_kind_color(nd->kind);
            cui_draw_rect(ui, b, CUI_CORNERS(3), tint(ui, base, nd->dir ? 0.30f : 0.40f));
            label(ui, b, nd->name, meta, 12.0f);
        }
    }
}

static void draw_item(cui_ctx *ui, const titem *t, cui_rect ox) {
    cui_rect b = { ox.x + t->r.x + 1, ox.y + t->r.y + 1, t->r.w - 2, t->r.h - 2 };
    if (b.w < 2 || b.h < 2) return;
    char meta[64], pc[16], sz[40], nm[64];
    pct_text(pc, sizeof pc, t->size, g_total); fs_format_size(sz, sizeof sz, t->size);
    snprintf(meta, sizeof meta, "%s \xC2\xB7 %s", pc, sz);
    if (t->kind == TK_FREE) {
        cui_draw_rect(ui, b, CUI_CORNERS(6), tint(ui, CUI_COLOR_FOREGROUND, 0.04f));
        hatch(ui, b);
        label(ui, b, "Free space", meta, 13.0f);
        return;
    }
    if (t->kind == TK_OTHER) {
        cui_draw_rect(ui, b, CUI_CORNERS(6), tint(ui, CUI_COLOR_FOREGROUND, 0.10f));
        label(ui, b, "System and unreadable", meta, 13.0f);
        return;
    }
    if (t->kind == TK_SMALL) {
        cui_draw_rect(ui, b, CUI_CORNERS(6), tint(ui, CUI_COLOR_FOREGROUND, 0.10f));
        snprintf(nm, sizeof nm, "%d small items", t->small_n);
        label(ui, b, nm, meta, 13.0f);
        return;
    }
    const fs_entry *e = app_item(t->vis);
    bool dir = (e->flags & FS_DIR) != 0, sel = A.sel[t->vis], hov = A.hover == t->vis;
    cui_color base = dir ? CUI_COLOR_ACCENT : ui_kind_color(e->kind);
    cui_color fill = tint(ui, base, dir ? 0.13f : 0.36f);
    if (sel) fill = tint(ui, base, dir ? 0.28f : 0.52f);
    else if (hov) fill = cui_mix(fill, cui_color_of(ui, CUI_COLOR_FOREGROUND), 0.07f);
    if (sel) cui_draw_rect_border(ui, b, CUI_CORNERS(6), fill, cui_color_of(ui, CUI_COLOR_ACCENT), 2.0f);
    else cui_draw_rect(ui, b, CUI_CORNERS(6), fill);
    if (t->sub >= 0) {
        draw_nested(ui, t, ox, &S[t->sub]);
        label(ui, (cui_rect){ b.x, b.y, b.w, NEST_HEAD + 2 }, app_item_name(t->vis), NULL, 12.5f);
        if (b.w > 220) {   /* the meta on the right of the strip, when the name leaves room */
            float mw = ui_body_text_w(meta) * 12.0f / 14.0f + 16;
            float nw = ui_body_text_w(app_item_name(t->vis)) * 12.5f / 14.0f + 20;
            if (nw + mw < b.w) text_at(ui, b.x + b.w - mw + 4, b.y + 3, 12.0f, cui_color_of(ui, CUI_COLOR_TEXT_SECONDARY), meta);
        }
    } else label(ui, b, app_item_name(t->vis), meta, 12.5f);
}

static void paint(cui_ctx *ui, cui_rect r, void *user) {
    (void)user;
    if (r.w < 2 || r.h < 2) return;
    compute(r.w, r.h);
    cui_color sec = cui_color_of(ui, CUI_COLOR_TEXT_SECONDARY);
    text_at(ui, r.x + PAD + 2, r.y + 7, 13.0f, sec, head_text);
    if (!T_n) {
        const char *msg = g_pending ? "Measuring\xE2\x80\xA6" : "Nothing to measure here";
        float tw = ui_body_text_w(msg);
        text_at(ui, r.x + (r.w - tw) * 0.5f, r.y + r.h * 0.5f - 8, 14.0f, sec, msg);
        return;
    }
    for (int i = 0; i < T_n; i++) draw_item(ui, &T[i], r);
}

/* ---- input ---- */

static int hit_at(cui_vec2 m) {
    for (int i = 0; i < T_n; i++) {
        const tm_rect *q = &T[i].r;
        if (m.x >= q->x && m.x < q->x + q->w && m.y >= q->y && m.y < q->y + q->h) return i;
    }
    return -1;
}

static cui_rect g_origin;

void ui_tree_view(void) {
    cui_ctx *ui = A.ui;
    subs_frame();
    cui_canvas_result c = cui_canvas(ui, "tree", CUI_FILL, CUI_FILL, paint, NULL, 0);
    g_origin = c.rect;
    if (c.rect.w < 2 || c.rect.h < 2) { A.hover = -1; return; }
    compute(c.rect.w, c.rect.h);
    subs_request();
    cui_vec2 mp = cui_mouse_pos(ui), m = { mp.x - c.rect.x, mp.y - c.rect.y };
    int hit = c.hovered ? hit_at(m) : -1;
    int vis = hit >= 0 && T[hit].kind == TK_ITEM ? T[hit].vis : -1;
    A.hover = vis;
    int mods = cui_key_mods(ui);
    if (vis >= 0 && cui_mouse_clicked(ui, CUI_MOUSE_LEFT)) dnd_press(vis);
    if (c.clicked) {
        if (vis >= 0) {
            double now = cui_time(ui);
            if (vis == A.last_click_idx && now - A.last_click_t < 0.45 && !(mods & (CUI_MOD_SHIFT | CUI_MOD_CTRL))) {
                A.last_click_idx = -1;
                app_open_vis(vis);
                return;
            }
            if (mods & CUI_MOD_SHIFT) { if (A.anchor < 0) A.anchor = vis; app_select_range(A.anchor, vis); }
            else if (mods & CUI_MOD_CTRL) { A.sel[vis] = !A.sel[vis]; A.anchor = vis; }
            else app_select_only(vis);
            A.cursor = vis;
            A.last_click_idx = vis; A.last_click_t = now;
        } else if (!(mods & (CUI_MOD_SHIFT | CUI_MOD_CTRL))) app_clear_selection();
    }
    if (vis >= 0 && cui_mouse_clicked(ui, CUI_MOUSE_RIGHT)) {
        if (!A.sel[vis]) { app_select_only(vis); A.cursor = vis; }
        A.ctx_item = true;
    }
    if (vis >= 0) {
        char tip[UI_NAME_BYTES + 80], pc[16], sz[40];
        pct_text(pc, sizeof pc, T[hit].size, g_total); fs_format_size(sz, sizeof sz, T[hit].size);
        snprintf(tip, sizeof tip, "%s \xE2\x80\x94 %s, %s", app_item_name(vis), sz, pc);
        const tm_rect *q = &T[hit].r;
        ui_item_tooltip(ui, 0x7C200000u + (cui_id)vis, tip, (cui_rect){ c.rect.x + q->x, c.rect.y + q->y, q->w, q->h });
    }
}

/* Arrow keys move to the rectangle nearest in that direction. */
void ui_tree_keys(void) {
    cui_ctx *ui = A.ui;
    if (cui_key_mods(ui)) return;
    int dx = (cui_key_pressed(ui, CUI_KEY_RIGHT) ? 1 : 0) - (cui_key_pressed(ui, CUI_KEY_LEFT) ? 1 : 0);
    int dy = (cui_key_pressed(ui, CUI_KEY_DOWN) ? 1 : 0) - (cui_key_pressed(ui, CUI_KEY_UP) ? 1 : 0);
    if (!dx && !dy) return;
    int cur = -1, best = -1;
    for (int i = 0; i < T_n; i++) if (T[i].kind == TK_ITEM && T[i].vis == A.cursor) cur = i;
    if (cur < 0) { for (int i = 0; i < T_n; i++) if (T[i].kind == TK_ITEM) { best = i; break; } }
    else {
        float cx = T[cur].r.x + T[cur].r.w * .5f, cy = T[cur].r.y + T[cur].r.h * .5f, bs = 1e30f;
        for (int i = 0; i < T_n; i++) {
            if (i == cur || T[i].kind != TK_ITEM) continue;
            float ex = T[i].r.x + T[i].r.w * .5f - cx, ey = T[i].r.y + T[i].r.h * .5f - cy;
            float along = dx ? ex * (float)dx : ey * (float)dy, perp = dx ? fabsf(ey) : fabsf(ex);
            if (along <= 1) continue;
            float score = along + 2 * perp;
            if (score < bs) { bs = score; best = i; }
        }
    }
    if (best >= 0) { app_select_only(T[best].vis); A.cursor = T[best].vis; }
}

bool ui_tree_anchor(int vis, cui_rect *r) {
    for (int i = 0; i < T_n; i++)
        if (T[i].kind == TK_ITEM && T[i].vis == vis) { *r = (cui_rect){ T[i].r.x, T[i].r.y, T[i].r.w, T[i].r.h }; return true; }
    return false;
}

void app_set_tree(bool on) {
    if (g_tree == on) return;
    g_tree = on;
    sig.valid = false;
    if (on) sizes_forget();
}

void ui_tree_after_frame(void) {   /* once a frame: leaving the view drops what it holds */
    bool shown = ui_tree_shown();
    if (was_shown && !shown) { subs_reset(); sub_list = NULL; sub_root[0] = 0; T_n = 0; NP_n = 0; g_total = 0; }
    was_shown = shown;
}

static void jstr(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) { if (*s == '"' || *s == '\\' || (unsigned char)*s < 32) fputc('_', f); else fputc(*s, f); }
    fputc('"', f);
}

void ui_tree_snapshot(FILE *f) {
    fprintf(f, ",\"tree\":{\"on\":%d,\"shown\":%d,\"n\":%d,\"ox\":%.1f,\"oy\":%.1f,\"w\":%.1f,\"h\":%.1f,\"subs\":%d,\"open\":%d,\"total\":%llu,\"head\":",
            g_tree, ui_tree_shown(), T_n, g_origin.x, g_origin.y, g_origin.w, g_origin.h, sub_n, fsz_open(), (unsigned long long)g_total);
    jstr(f, head_text);
    fprintf(f, ",\"items\":[");
    for (int i = 0; i < T_n && i < 80; i++) {
        fprintf(f, "%s{\"k\":%d,\"v\":%d,\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f,\"size\":%llu,\"nest\":%d,\"n\":", i ? "," : "", T[i].kind, T[i].vis,
                T[i].r.x, T[i].r.y, T[i].r.w, T[i].r.h, (unsigned long long)T[i].size, T[i].ncnt);
        jstr(f, T[i].kind == TK_ITEM && ui_tree_shown() && T[i].vis < A.vis_n ? app_item_name(T[i].vis) : "");
        fprintf(f, ",\"nr\":[");   /* the nested rectangles: x, y, w, h, size, node (-1 small items, -2 unlisted remainder) */
        for (int k = 0; k < T[i].ncnt; k++) {
            const nrect *q = &NP[T[i].noff + k];
            fprintf(f, "%s[%.1f,%.1f,%.1f,%.1f,%llu,%d]", k ? "," : "", q->r.x, q->r.y, q->r.w, q->r.h, (unsigned long long)q->size, q->node);
        }
        fputc(']', f);
        fputc('}', f);
    }
    fprintf(f, "]}");
}
