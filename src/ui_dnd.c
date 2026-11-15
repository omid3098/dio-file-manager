/* Dragging files out of the window and dropping them into it, with OLE (docs/design.md, "Drag and drop").
 *
 * Out: a press on an item's icon or name that moves past the system drag distance drags the selection (DoDragDrop) as CF_HDROP
 * plus "Preferred DropEffect" (the same two blocks the clipboard gets, clip.c), with a drag image of the first item's icon and,
 * for several, their count. In: the window is the drop target of every program, itself and other windows of this app included.
 * What is under the pointer is decided from the frame the window draws: a drag over the window runs a frame (ui_live_frame)
 * with the pointer where the drag is, so the item and the sidebar row that show the hover are the ones that take the drop.
 * Every copy and move is a file job of ui_actions.c: nothing here copies a file. */
#define COBJMACROS
#define DFM_INTERNAL   /* dfm_utf8 of ops.h: a path of any length */
#include <windows.h>
#include "ui.h"   /* before shlobj.h: its commctrl.h defines VIEW_LIST */
#include <objbase.h>
#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clip.h"
#include "ops.h"

static const GUID DFM_CLSID_DragDropHelper = { 0x4657278A, 0x411B, 0x11D2, { 0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } };
static const GUID DFM_IID_IDropTargetHelper = { 0x4657278B, 0x411B, 0x11D2, { 0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } };
static const GUID DFM_IID_IDragSourceHelper = { 0xDE5BF786, 0x477A, 0x11D2, { 0x83, 0x9D, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } };

enum { K_NONE = 0, K_BG, K_FOLDER, K_PLACE, K_TRASH };   /* what a drop would go to */
enum { ROWS_MAX = 64, ENTRIES_MAX = 16, EDGE = 32 };

/* ---- the data object ---- */

typedef struct dent { FORMATETC fe; STGMEDIUM sm; } dent;
typedef struct dobj { IDataObject iface; LONG ref; dent e[ENTRIES_MAX]; int n; } dobj;

static HGLOBAL copy_global(HGLOBAL h) {
    SIZE_T n = GlobalSize(h);
    HGLOBAL c = GlobalAlloc(GMEM_MOVEABLE, n);
    void *a = c ? GlobalLock(c) : NULL, *b = GlobalLock(h);
    if (!a || !b) { if (a) GlobalUnlock(c); if (b) GlobalUnlock(h); if (c) GlobalFree(c); return NULL; }
    memcpy(a, b, n); GlobalUnlock(c); GlobalUnlock(h);
    return c;
}

static dent *find_entry(dobj *o, const FORMATETC *fe) {
    for (int i = 0; i < o->n; i++)
        if (o->e[i].fe.cfFormat == fe->cfFormat && (o->e[i].fe.tymed & fe->tymed) && o->e[i].fe.dwAspect == fe->dwAspect) return &o->e[i];
    return NULL;
}

static HRESULT STDMETHODCALLTYPE do_qi(IDataObject *t, REFIID r, void **out) {
    if (IsEqualIID(r, &IID_IUnknown) || IsEqualIID(r, &IID_IDataObject)) { *out = t; IDataObject_AddRef(t); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE do_addref(IDataObject *t) { return (ULONG)InterlockedIncrement(&((dobj *)t)->ref); }
static ULONG STDMETHODCALLTYPE do_release(IDataObject *t) {
    dobj *o = (dobj *)t;
    LONG n = InterlockedDecrement(&o->ref);
    if (n == 0) { for (int i = 0; i < o->n; i++) ReleaseStgMedium(&o->e[i].sm); free(o); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE do_get(IDataObject *t, FORMATETC *fe, STGMEDIUM *sm) {
    dent *e = find_entry((dobj *)t, fe);
    if (!e) return (fe->tymed & TYMED_HGLOBAL) ? DV_E_FORMATETC : DV_E_TYMED;
    HGLOBAL c = copy_global(e->sm.hGlobal);
    if (!c) return E_OUTOFMEMORY;
    memset(sm, 0, sizeof *sm); sm->tymed = TYMED_HGLOBAL; sm->hGlobal = c;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE do_get_here(IDataObject *t, FORMATETC *fe, STGMEDIUM *sm) { (void)t; (void)fe; (void)sm; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE do_query(IDataObject *t, FORMATETC *fe) {
    if (!(fe->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
    return find_entry((dobj *)t, fe) ? S_OK : DV_E_FORMATETC;
}
static HRESULT STDMETHODCALLTYPE do_canon(IDataObject *t, FORMATETC *in, FORMATETC *out) { (void)t; (void)in; out->ptd = NULL; return DATA_S_SAMEFORMATETC; }
static HRESULT STDMETHODCALLTYPE do_set(IDataObject *t, FORMATETC *fe, STGMEDIUM *sm, BOOL release) {
    dobj *o = (dobj *)t;
    if (sm->tymed != TYMED_HGLOBAL || !sm->hGlobal) return DV_E_TYMED;   /* what the drag image helper stores is all in global memory */
    dent *e = NULL;
    for (int i = 0; i < o->n; i++) if (o->e[i].fe.cfFormat == fe->cfFormat) e = &o->e[i];
    if (!e) { if (o->n >= ENTRIES_MAX) return E_OUTOFMEMORY; e = &o->e[o->n++]; }
    else ReleaseStgMedium(&e->sm);
    e->fe = *fe; e->fe.tymed = TYMED_HGLOBAL; e->fe.ptd = NULL;
    memset(&e->sm, 0, sizeof e->sm); e->sm.tymed = TYMED_HGLOBAL;
    if (release) { e->sm.hGlobal = sm->hGlobal; }   /* ours now: the caller's medium is not released again */
    else if (!(e->sm.hGlobal = copy_global(sm->hGlobal))) { o->n--; return E_OUTOFMEMORY; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE do_enum(IDataObject *t, DWORD dir, IEnumFORMATETC **out) {
    dobj *o = (dobj *)t;
    FORMATETC list[ENTRIES_MAX];
    if (dir != DATADIR_GET) return E_NOTIMPL;
    for (int i = 0; i < o->n; i++) list[i] = o->e[i].fe;
    return SHCreateStdEnumFmtEtc((UINT)o->n, list, out);
}
static HRESULT STDMETHODCALLTYPE do_advise(IDataObject *t, FORMATETC *f, DWORD a, IAdviseSink *s, DWORD *c) { (void)t; (void)f; (void)a; (void)s; (void)c; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE do_unadvise(IDataObject *t, DWORD c) { (void)t; (void)c; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE do_enum_advise(IDataObject *t, IEnumSTATDATA **e) { (void)t; (void)e; return OLE_E_ADVISENOTSUPPORTED; }

static IDataObjectVtbl dobj_vtbl = { do_qi, do_addref, do_release, do_get, do_get_here, do_query, do_canon, do_set, do_enum, do_advise, do_unadvise, do_enum_advise };

static bool dobj_add(dobj *o, UINT cf, HGLOBAL h) {
    if (!h || o->n >= ENTRIES_MAX) { if (h) GlobalFree(h); return false; }
    dent *e = &o->e[o->n++];
    e->fe = (FORMATETC){ (CLIPFORMAT)cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    memset(&e->sm, 0, sizeof e->sm); e->sm.tymed = TYMED_HGLOBAL; e->sm.hGlobal = h;
    return true;
}

static UINT cf_effect(void) { static UINT f; if (!f) f = RegisterClipboardFormatW(L"Preferred DropEffect"); return f; }

/* A data object that carries the files: CF_HDROP and "Preferred DropEffect". NULL on failure. */
static IDataObject *make_files(const char *const *paths, size_t n, DWORD preferred) {
    DWORD err;
    dobj *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    o->iface.lpVtbl = &dobj_vtbl; o->ref = 1;
    if (!dobj_add(o, CF_HDROP, clip_make_hdrop(paths, n, &err)) || !dobj_add(o, cf_effect(), clip_make_effect(preferred))) {
        do_release(&o->iface);
        return NULL;
    }
    return &o->iface;
}

/* The paths a data object offers as CF_HDROP; false when it has none. Free with act_free_paths. */
static bool read_paths(IDataObject *d, char ***out, size_t *count) {
    FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM sm;
    *out = NULL; *count = 0;
    if (FAILED(IDataObject_GetData(d, &fe, &sm))) return false;
    HDROP drop = (HDROP)GlobalLock(sm.hGlobal);
    UINT n = drop ? DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0) : 0;
    char **p = calloc(n ? n : 1, sizeof *p);
    size_t k = 0;
    bool whole = true;   /* a drop with an item that cannot be read is refused whole: no item drops out unseen */
    for (UINT i = 0; p && i < n && whole; i++) {
        UINT len = DragQueryFileW(drop, i, NULL, 0);
        wchar_t *w = malloc(((size_t)len + 1) * sizeof *w);
        if (!w) { whole = false; break; }
        DragQueryFileW(drop, i, w, len + 1);
        p[k] = dfm_utf8(w);
        if (p[k]) k++; else whole = false;
        free(w);
    }
    if (drop) GlobalUnlock(sm.hGlobal);
    ReleaseStgMedium(&sm);
    *out = p; *count = k;
    return p && whole && k > 0;
}

static DWORD read_effect(IDataObject *d) {
    FORMATETC fe = { (CLIPFORMAT)cf_effect(), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM sm;
    DWORD v = 0;
    if (FAILED(IDataObject_GetData(d, &fe, &sm))) return 0;
    DWORD *p = GlobalLock(sm.hGlobal);
    if (p) { v = *p; GlobalUnlock(sm.hGlobal); }
    ReleaseStgMedium(&sm);
    return v;
}

/* ---- state ---- */

static struct {
    HWND hwnd;
    bool test;
    IDropTargetHelper *th;
    IDragSourceHelper *sh;
    IDropTarget target;
    int press; cui_vec2 press_at;       /* an item the left button went down on (icon or name), and where */
    bool drag_posted;
    /* a drag over the window */
    bool session, dragging;             /* a drag is over the window; this window started the drag that is running */
    char **src; size_t src_n;           /* what the drag carries */
    POINT at; DWORD keys, allowed;      /* the pointer in the client (px), the modifier keys and the effects the source allows */
    cui_vec2 pt;                        /* the pointer in points */
    int kind; char dest[FS_PATH_MAX]; int item; DWORD effect;
    int drawn_item;                     /* the folder item the last frame showed with the hover fill */
    int frame_hover;                    /* the item under the pointer in the last frame, before the fill was applied */
    ULONGLONG scroll_t;
    /* this frame's layout */
    cui_rect content;
    cui_rect row[ROWS_MAX]; char row_to[ROWS_MAX][FS_PATH_MAX]; int row_n;
    /* test state */
    IDataObject *test_obj;
    int drops, drags; DWORD last_effect, last_preferred; int last_n; char last_dest[FS_PATH_MAX]; bool last_move;
    char **drag_paths; size_t drag_n; char **drag_back; size_t drag_back_n;
} D = { .press = -1, .item = -1, .drawn_item = -1, .frame_hover = -1 };

bool dnd_active(void) { return D.session; }

static float pt_scale(void) {
    RECT r;
    cui_vec2 vp = cui_viewport_size(A.ui);
    if (!D.hwnd || !GetClientRect(D.hwnd, &r) || vp.x <= 0) return 1.0f;
    return (float)r.right / vp.x;
}

static bool has(cui_rect r, cui_vec2 p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

/* ---- the frame's side ---- */

void dnd_frame_begin(void) {
    cui_vec2 vp = cui_viewport_size(A.ui);
    bool narrow = vp.x < NARROW_BELOW;
    D.row_n = 0;
    D.content = (cui_rect){ narrow ? 0 : 240.0f, HEADER_H, A.content_w, A.content_h };
    if (A.sidebar_open) D.content.w = 0;   /* the sidebar sheet covers the folder */
}

void dnd_row(const char *target) {
    if (D.row_n >= ROWS_MAX || !target || !target[0]) return;
    D.row[D.row_n] = cui_last_rect(A.ui);
    snprintf(D.row_to[D.row_n], sizeof D.row_to[0], "%s", target);
    D.row_n++;
}

void dnd_press(int idx) {
    if (idx < 0 || idx >= A.vis_n || A.loc == LOC_TRASH || A.loc == LOC_OTHER || D.session || D.drag_posted) return;
    if (app_item(idx)->flags & FS_INCOMING) return;
    D.press = idx;
    D.press_at = cui_mouse_pos(A.ui);
}

void dnd_frame_end(void) {
    cui_ctx *ui = A.ui;
    if (D.session) {
        D.frame_hover = A.hover;
        A.hover = D.kind == K_FOLDER ? D.item : -1;   /* the folder the drop would go into takes the hover fill */
        D.drawn_item = A.hover;
    }
    if (D.press < 0) return;
    if (!cui_mouse_down(ui, CUI_MOUSE_LEFT) || D.press >= A.vis_n || !act_drop_allowed() || ui_band_dragging()) { D.press = -1; return; }
    cui_vec2 m = cui_mouse_pos(ui);
    float k = pt_scale();
    if (fabsf(m.x - D.press_at.x) * k < (float)GetSystemMetrics(SM_CXDRAG) && fabsf(m.y - D.press_at.y) * k < (float)GetSystemMetrics(SM_CYDRAG)) return;
    /* The press selected nothing yet (a click does, on release): the item under it is what drags, as in Explorer. */
    int i = D.press;
    D.press = -1;
    if (!A.sel[i]) {
        if (cui_key_mods(ui) & CUI_MOD_CTRL) { A.sel[i] = true; A.anchor = A.cursor = i; app_count_selection(); }
        else { app_select_only(i); A.cursor = i; }
    }
    D.drag_posted = true;
    PostMessageW(D.hwnd, WM_DFM_DRAG, 0, 0);
}

/* ---- deciding what a drop does ---- */

static bool folder_view(void) { return A.loc == LOC_FOLDER && !A.searching; }

static int path_len(const char *p) { int n = (int)strlen(p); while (n > 0 && p[n - 1] == '\\') n--; return n; }

/* b is a, or inside a */
static bool inside_or_same(const char *a, const char *b) {
    int la = path_len(a), lb = path_len(b);
    return lb >= la && _strnicmp(a, b, (size_t)la) == 0 && (lb == la || b[la] == '\\' || (la > 0 && a[la - 1] == ':'));
}

static bool parent_is(const char *file, const char *dir) {
    const char *cut = strrchr(file, '\\');
    int ld = path_len(dir);
    if (!cut) return false;
    int lp = (int)(cut - file);
    if (lp == 2 && file[1] == ':') lp = 3;   /* "C:\x": its folder is the root "C:\" */
    if (lp < ld && ld == 3 && dir[1] == ':') return false;
    int lf = lp > 0 && file[lp - 1] == '\\' ? lp - 1 : lp;
    return lf == ld && _strnicmp(file, dir, (size_t)ld) == 0;
}

/* "C:" or "\\server\share": two paths on the same volume move, on different ones copy. */
static void volume_of(const char *p, char *out, size_t cap) {
    size_t n = 0;
    if (p[0] == '\\' && p[1] == '\\') {
        int slashes = 0;
        while (p[n] && n + 1 < cap) { if (p[n] == '\\' && ++slashes == 4) break; n++; }
    } else if (p[0] && p[1] == ':') n = 2;
    for (size_t i = 0; i < n; i++) out[i] = (char)toupper((unsigned char)p[i]);
    out[n] = 0;
}

static bool same_volume_all(const char *dest) {
    char a[FS_PATH_MAX], b[FS_PATH_MAX];
    volume_of(dest, a, sizeof a);
    for (size_t i = 0; i < D.src_n; i++) {
        volume_of(D.src[i], b, sizeof b);
        if (strcmp(a, b) != 0) return false;
    }
    return true;
}

/* The effect Explorer would give: the same volume moves, another copies, Shift forces a move, Ctrl a copy; only what the
 * source allows. A folder into itself or one of its folders, and a move into the folder an item already sits in, are refused. */
static DWORD effect_for(int kind, const char *dest, DWORD keys, DWORD allowed) {
    if (kind == K_NONE || !D.src_n) return DROPEFFECT_NONE;
    DWORD want;
    if (kind == K_TRASH) want = DROPEFFECT_MOVE;
    else {
        bool shift = (keys & MK_SHIFT) != 0, ctrl = (keys & MK_CONTROL) != 0;
        if (shift && ctrl) return DROPEFFECT_NONE;   /* a link: not made */
        want = shift ? DROPEFFECT_MOVE : ctrl ? DROPEFFECT_COPY : same_volume_all(dest) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        for (size_t i = 0; i < D.src_n; i++) if (inside_or_same(D.src[i], dest)) return DROPEFFECT_NONE;
    }
    /* A source that offers one effect only: without a modifier the drop takes that one; a Shift or Ctrl the source does not allow is refused. */
    if (!(allowed & want) && kind != K_TRASH && !(keys & (MK_SHIFT | MK_CONTROL))) want = want == DROPEFFECT_MOVE ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
    if (!(allowed & want)) return DROPEFFECT_NONE;
    if (want == DROPEFFECT_MOVE && kind != K_TRASH) {
        bool any = false;
        for (size_t i = 0; i < D.src_n; i++) if (!parent_is(D.src[i], dest)) any = true;
        if (!any) return DROPEFFECT_NONE;   /* every item is already in that folder */
    }
    return want;
}

/* Where the pointer is, after a frame that had the pointer there: the item, the sidebar row or the folder's own background. */
static void resolve(void) {
    D.kind = K_NONE; D.dest[0] = 0; D.item = -1;
    if (!act_drop_allowed()) return;
    if (D.pt.y >= HEADER_H) {
        for (int i = D.row_n - 1; i >= 0; i--) {
            if (!has(D.row[i], D.pt)) continue;
            if (!strcmp(D.row_to[i], "@3")) D.kind = K_TRASH;
            else if (D.row_to[i][0] != '@') { D.kind = K_PLACE; snprintf(D.dest, sizeof D.dest, "%s", D.row_to[i]); }
            break;
        }
    }
    if (D.kind == K_NONE && has(D.content, D.pt)) {
        int h = D.frame_hover;
        if (h >= 0 && h < A.vis_n && (app_item(h)->flags & FS_DIR) && !(app_item(h)->flags & FS_INCOMING) && A.loc != LOC_TRASH && A.loc != LOC_OTHER) {
            D.kind = K_FOLDER; D.item = h;
            app_path_of(h, D.dest, sizeof D.dest);
        } else if (folder_view() && A.list && fs_list_state(A.list) == FS_DONE) {
            D.kind = K_BG; snprintf(D.dest, sizeof D.dest, "%s", A.path);
        }
    }
    D.effect = effect_for(D.kind, D.dest, D.keys, D.allowed);
    if (D.effect == DROPEFFECT_NONE) { D.kind = K_NONE; D.item = -1; }   /* refused: no hover fill, no drop */
}

static void edge_scroll(void) {
    if (!has(D.content, D.pt) || A.sidebar_open) { D.scroll_t = 0; return; }
    ULONGLONG now = GetTickCount64();
    float dt = D.scroll_t ? (float)(now - D.scroll_t) / 1000.0f : 0.0f;
    D.scroll_t = now;
    if (dt > 0.1f) dt = 0.1f;
    bool across = A.zoom == Z_LIST;   /* the columns of List run sideways */
    float pos = across ? D.pt.x : D.pt.y;
    float low = across ? D.content.x : D.content.y + (A.zoom == Z_DETAILS ? 40.0f : 0.0f);
    float high = across ? D.content.x + D.content.w : D.content.y + D.content.h;
    float speed = pos < low + EDGE ? -(low + EDGE - pos) / EDGE : pos > high - EDGE ? (pos - high + EDGE) / EDGE : 0;
    if (speed > 1) speed = 1;
    if (speed < -1) speed = -1;
    if (speed == 0 || dt == 0) return;
    const char *id = across ? "columns" : A.view == VIEW_GRID ? "grid" : "list";
    ui_scroll_by(id, across ? speed * 720.0f * dt : 0, across ? 0 : speed * 720.0f * dt);
}

/* ---- one drag over the window ---- */

static void session_end(void) {
    D.session = false;
    act_free_paths(D.src, D.src_n); D.src = NULL; D.src_n = 0;
    D.kind = K_NONE; D.item = -1; D.effect = DROPEFFECT_NONE; D.dest[0] = 0; D.scroll_t = 0;
    if (A.ui) { A.hover = -1; cui_input_mouse_leave(A.ui); cui_request_redraw(A.ui); }
}

static bool session_begin(IDataObject *d) {
    FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (!d || IDataObject_QueryGetData(d, &fe) != S_OK) return false;
    if (D.session) session_end();
    if (!read_paths(d, &D.src, &D.src_n)) { act_free_paths(D.src, D.src_n); D.src = NULL; D.src_n = 0; return false; }
    D.session = true;
    return true;
}

/* The pointer is at client px (x, y). Returns the effect to show. */
static DWORD session_over(int x, int y, DWORD keys, DWORD allowed) {
    D.at = (POINT){ x, y }; D.keys = keys; D.allowed = allowed;
    float k = pt_scale();
    D.pt = (cui_vec2){ (float)x / k, (float)y / k };
    cui_input_mouse_move(A.ui, D.pt.x, D.pt.y);
    edge_scroll();
    ui_live_frame();   /* the frame the pointer is in: its hover is what takes the drop */
    int was = D.drawn_item;
    resolve();
    if (D.kind == K_FOLDER ? D.item != was : was >= 0) ui_live_frame();   /* the hover fill follows the decision */
    return D.effect;
}

static void perform_drop(void) {
    if (D.kind == K_NONE || D.effect == DROPEFFECT_NONE) return;
    bool move = D.effect == DROPEFFECT_MOVE;
    size_t n = 0;
    char **p = calloc(D.src_n + 1, sizeof *p);
    for (size_t i = 0; p && i < D.src_n; i++) {
        if (move && D.kind != K_TRASH && parent_is(D.src[i], D.dest)) continue;   /* already there */
        p[n] = _strdup(D.src[i]);
        if (p[n]) n++;
    }
    bool ok = n && act_drop_files(p, n, D.kind == K_TRASH ? NULL : D.dest, move);
    act_free_paths(p, n);
    D.drops += ok ? 1 : 0;
    D.last_effect = D.effect; D.last_n = (int)n; D.last_move = move;
    snprintf(D.last_dest, sizeof D.last_dest, "%s", D.kind == K_TRASH ? "@3" : D.dest);
    cui_request_redraw(A.ui);
}

/* ---- the drop target ---- */

static HRESULT STDMETHODCALLTYPE dt_qi(IDropTarget *t, REFIID r, void **out) {
    if (IsEqualIID(r, &IID_IUnknown) || IsEqualIID(r, &IID_IDropTarget)) { *out = t; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dt_addref(IDropTarget *t) { (void)t; return 2; }
static ULONG STDMETHODCALLTYPE dt_release(IDropTarget *t) { (void)t; return 1; }

static void to_client(POINTL pt, int *x, int *y) {
    POINT p = { pt.x, pt.y };
    ScreenToClient(D.hwnd, &p);
    *x = p.x; *y = p.y;
}

static HRESULT STDMETHODCALLTYPE dt_enter(IDropTarget *t, IDataObject *d, DWORD keys, POINTL pt, DWORD *effect) {
    (void)t;
    int x, y;
    to_client(pt, &x, &y);
    if (!session_begin(d)) { *effect = DROPEFFECT_NONE; return S_OK; }
    if (D.th) { POINT p = { pt.x, pt.y }; IDropTargetHelper_DragEnter(D.th, D.hwnd, d, &p, *effect); }
    *effect = session_over(x, y, keys, *effect);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_over(IDropTarget *t, DWORD keys, POINTL pt, DWORD *effect) {
    (void)t;
    int x, y;
    to_client(pt, &x, &y);
    if (!D.session) { *effect = DROPEFFECT_NONE; return S_OK; }
    if (D.th) { POINT p = { pt.x, pt.y }; IDropTargetHelper_DragOver(D.th, &p, *effect); }
    *effect = session_over(x, y, keys, *effect);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_leave(IDropTarget *t) {
    (void)t;
    if (D.th) IDropTargetHelper_DragLeave(D.th);
    if (D.session) session_end();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_drop(IDropTarget *t, IDataObject *d, DWORD keys, POINTL pt, DWORD *effect) {
    (void)t;
    int x, y;
    to_client(pt, &x, &y);
    if (!D.session) { *effect = DROPEFFECT_NONE; return S_OK; }
    if (D.th) { POINT p = { pt.x, pt.y }; IDropTargetHelper_Drop(D.th, d, &p, *effect); }
    DWORD e = session_over(x, y, keys, *effect);
    perform_drop();
    session_end();
    /* The move was made here, through a job: the source must not delete anything itself, so it is told nothing moved. */
    *effect = e == DROPEFFECT_COPY ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (e == DROPEFFECT_MOVE) {
        FORMATETC fe = { (CLIPFORMAT)RegisterClipboardFormatW(L"Performed DropEffect"), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM sm = { TYMED_HGLOBAL };
        sm.hGlobal = clip_make_effect(DROPEFFECT_MOVE);
        if (sm.hGlobal && IDataObject_SetData(d, &fe, &sm, TRUE) != S_OK) GlobalFree(sm.hGlobal);
    }
    return S_OK;
}

static IDropTargetVtbl target_vtbl = { dt_qi, dt_addref, dt_release, dt_enter, dt_over, dt_leave, dt_drop };

/* ---- the drag source ---- */

typedef struct dsrc { IDropSource iface; } dsrc;
static HRESULT STDMETHODCALLTYPE ds_qi(IDropSource *t, REFIID r, void **out) {
    if (IsEqualIID(r, &IID_IUnknown) || IsEqualIID(r, &IID_IDropSource)) { *out = t; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ds_addref(IDropSource *t) { (void)t; return 2; }
static ULONG STDMETHODCALLTYPE ds_release(IDropSource *t) { (void)t; return 1; }
static HRESULT STDMETHODCALLTYPE ds_continue(IDropSource *t, BOOL esc, DWORD keys) {
    (void)t;
    if (esc) return DRAGDROP_S_CANCEL;
    if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ds_feedback(IDropSource *t, DWORD effect) { (void)t; (void)effect; return DRAGDROP_S_USEDEFAULTCURSORS; }
static IDropSourceVtbl source_vtbl = { ds_qi, ds_addref, ds_release, ds_continue, ds_feedback };
static dsrc source = { { &source_vtbl } };

/* The drag image: the first item's icon, and for several a round count on its corner, drawn twice (on black and on white)
 * so the alpha of what GDI drew can be worked out. */
static HBITMAP drag_bitmap(const char *first, size_t count, SIZE *size) {
    wchar_t w[FS_PATH_MAX];
    SHFILEINFOW sfi;
    int ic = GetSystemMetrics(SM_CXICON), pad = count > 1 ? 10 : 0, W = ic + pad, H = ic + pad;
    if (!fs_u8_to_w(first, w, FS_PATH_MAX) || !SHGetFileInfoW(w, 0, &sfi, sizeof sfi, SHGFI_ICON | SHGFI_LARGEICON) || !sfi.hIcon) return NULL;
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB } };
    void *bits[2] = { 0 };
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bm[2] = { CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits[0], NULL, 0), CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits[1], NULL, 0) };
    HBITMAP out = NULL;
    if (bm[0] && bm[1] && dc) {
        unsigned on, accent = theme_accent(A.dark, &on);
        char txt[16];
        snprintf(txt, sizeof txt, "%d", count > 99 ? 99 : (int)count);
        wchar_t wt[16];
        MultiByteToWideChar(CP_UTF8, 0, txt, -1, wt, 16);
        HFONT font = CreateFontW(-13, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        for (int pass = 0; pass < 2; pass++) {
            HGDIOBJ old = SelectObject(dc, bm[pass]);
            RECT all = { 0, 0, W, H };
            HBRUSH back = CreateSolidBrush(pass ? RGB(255, 255, 255) : RGB(0, 0, 0));
            FillRect(dc, &all, back); DeleteObject(back);
            DrawIconEx(dc, 0, pad, sfi.hIcon, ic, ic, 0, NULL, DI_NORMAL);
            if (count > 1) {
                int d = 22;
                HBRUSH fill = CreateSolidBrush(RGB((accent >> 16) & 255, (accent >> 8) & 255, accent & 255));
                HGDIOBJ ob = SelectObject(dc, fill), op = SelectObject(dc, GetStockObject(NULL_PEN));
                Ellipse(dc, W - d, 0, W + 1, d + 1);
                SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(fill);
                HGDIOBJ of = SelectObject(dc, font);
                RECT tr = { W - d, 0, W + 1, d };
                SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB((on >> 16) & 255, (on >> 8) & 255, on & 255));
                DrawTextW(dc, wt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(dc, of);
            }
            SelectObject(dc, old);
        }
        DeleteObject(font);
        unsigned char *b = bits[0], *wh = bits[1];
        for (int i = 0; i < W * H; i++) {   /* white-on-black difference is the transparency */
            int diff = 255 - (wh[i * 4] - b[i * 4]);   /* blue channel: 0 transparent, 255 solid */
            if (diff < 0) diff = 0; if (diff > 255) diff = 255;
            if (!diff) { b[i * 4] = b[i * 4 + 1] = b[i * 4 + 2] = 0; }
            b[i * 4 + 3] = (unsigned char)diff;
            /* the black render is already premultiplied (colour over black) */
        }
        out = bm[0]; bm[0] = NULL;
        size->cx = W; size->cy = H;
    }
    if (bm[0]) DeleteObject(bm[0]);
    if (bm[1]) DeleteObject(bm[1]);
    if (dc) DeleteDC(dc);
    DestroyIcon(sfi.hIcon);
    return out;
}

/* The data of a drag of the selection; the paths are kept for the test snapshot. NULL when nothing can be dragged. */
static IDataObject *make_drag_data(char ***paths, size_t *n) {
    *paths = act_selected_paths(n);
    if (!*paths || !*n) { act_free_paths(*paths, *n); *paths = NULL; *n = 0; return NULL; }
    IDataObject *d = make_files((const char *const *)*paths, *n, DROPEFFECT_COPY);
    if (!d) { act_free_paths(*paths, *n); *paths = NULL; *n = 0; }
    return d;
}

void dnd_run(void) {
    D.drag_posted = false;
    if (A.loc == LOC_TRASH || A.loc == LOC_OTHER || !act_drop_allowed()) return;
    char **paths; size_t n;
    IDataObject *d = make_drag_data(&paths, &n);
    if (!d) return;
    ui_press_release();   /* the drag took the press: it must not end as a click on the item */
    if (D.test) {   /* the hidden desktop has no mouse to drag with: a check reads what would be dragged */
        act_free_paths(D.drag_paths, D.drag_n);
        act_free_paths(D.drag_back, D.drag_back_n);
        D.drag_paths = paths; D.drag_n = n; D.drags++;
        read_paths(d, &D.drag_back, &D.drag_back_n);
        D.last_preferred = read_effect(d);
        IDataObject_Release(d);
        return;
    }
    if (D.sh) {
        SIZE sz;
        HBITMAP bm = drag_bitmap(paths[0], n, &sz);
        if (bm) {
            SHDRAGIMAGE di = { sz, { sz.cx / 2, sz.cy / 2 }, bm, 0xFFFFFFFF };
            if (FAILED(IDragSourceHelper_InitializeFromBitmap(D.sh, &di, d))) DeleteObject(bm);
        }
    }
    DWORD effect = DROPEFFECT_NONE;
    D.dragging = true;
    DoDragDrop(d, &source.iface, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
    /* Whatever the target did, the originals stay: a move was made by the target itself. */
    IDataObject_Release(d);
    act_free_paths(paths, n);
    D.dragging = false;
    ui_press_release();
    if (A.ui) cui_request_redraw(A.ui);
}

/* ---- setup ---- */

void dnd_init(void *hwnd, bool test_mode) {
    D.hwnd = (HWND)hwnd; D.test = test_mode;
    D.target.lpVtbl = &target_vtbl;
    CoCreateInstance(&DFM_CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER, &DFM_IID_IDropTargetHelper, (void **)&D.th);
    CoCreateInstance(&DFM_CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER, &DFM_IID_IDragSourceHelper, (void **)&D.sh);
    DragAcceptFiles(D.hwnd, FALSE);   /* CUI asked for WM_DROPFILES: the OLE target takes every drop instead */
    RegisterDragDrop(D.hwnd, &D.target);
}

void dnd_shutdown(void) {
    if (D.hwnd) RevokeDragDrop(D.hwnd);
    if (D.th) IDropTargetHelper_Release(D.th);
    if (D.sh) IDragSourceHelper_Release(D.sh);
    D.th = NULL; D.sh = NULL;
    if (D.test_obj) { IDataObject_Release(D.test_obj); D.test_obj = NULL; }
    act_free_paths(D.src, D.src_n); D.src = NULL; D.src_n = 0;
    act_free_paths(D.drag_paths, D.drag_n); act_free_paths(D.drag_back, D.drag_back_n);
    D.drag_paths = D.drag_back = NULL; D.drag_n = D.drag_back_n = 0;
}

/* ---- test mode ----
 * WM_COPYDATA with dwData 0x444E44 and a UTF-8 text: the command, then its fields, separated by tabs.
 *   enter  x y keys allowed path...   a drag from another program arrives over client pixel (x, y) with these files
 *   over   x y keys allowed           it moves
 *   drop   x y keys allowed           it is dropped
 *   leave                             it goes away
 *   drag                              the selection is dragged (as if the press had moved far enough): the data it carries is read back
 * x and y are client pixels, keys MK_SHIFT (4) and MK_CONTROL (8), allowed the DROPEFFECT mask. The state is in the snapshot's "dnd". */
bool dnd_test_message(void *copydata) {
    COPYDATASTRUCT *cd = copydata;
    if (!D.test || !cd || cd->dwData != 0x444E44 || !cd->lpData) return false;
    char *text = malloc(cd->cbData + 1);
    if (!text) return true;
    memcpy(text, cd->lpData, cd->cbData); text[cd->cbData] = 0;
    char *f[64], *save = NULL;
    int n = 0;
    for (char *s = strtok_s(text, "\t", &save); s && n < 64; s = strtok_s(NULL, "\t", &save)) f[n++] = s;
    if (n >= 1 && !strcmp(f[0], "enter") && n >= 6) {
        if (D.test_obj) { IDataObject_Release(D.test_obj); D.test_obj = NULL; }
        D.test_obj = make_files((const char *const *)&f[5], (size_t)(n - 5), DROPEFFECT_COPY);
        DWORD e = (DWORD)atoi(f[4]);
        if (D.test_obj && session_begin(D.test_obj)) D.effect = session_over(atoi(f[1]), atoi(f[2]), (DWORD)atoi(f[3]), e);
    } else if (n >= 5 && !strcmp(f[0], "over") && D.session) {
        session_over(atoi(f[1]), atoi(f[2]), (DWORD)atoi(f[3]), (DWORD)atoi(f[4]));
    } else if (n >= 5 && !strcmp(f[0], "drop") && D.session) {
        DWORD e = session_over(atoi(f[1]), atoi(f[2]), (DWORD)atoi(f[3]), (DWORD)atoi(f[4]));
        perform_drop();
        session_end();
        if (D.test_obj) { IDataObject_Release(D.test_obj); D.test_obj = NULL; }
        D.last_preferred = e;
    } else if (!strcmp(f[0], "leave") && D.session) {
        session_end();
        if (D.test_obj) { IDataObject_Release(D.test_obj); D.test_obj = NULL; }
    } else if (!strcmp(f[0], "drag")) {
        dnd_run();
    }
    free(text);
    cui_request_redraw(A.ui);
    return true;
}

static void json_list(FILE *f, char **p, size_t n) {
    fputc('[', f);
    for (size_t i = 0; i < n; i++) {
        fputc(i ? ',' : ' ', f); fputc('"', f);
        for (const char *c = p[i]; *c; c++) { if (*c == '\\' || *c == '"') fputc('\\', f); fputc(*c, f); }
        fputc('"', f);
    }
    fputc(']', f);
}

void dnd_snapshot(FILE *f) {
    float ty = ui_scroll_pos(A.zoom == Z_LIST ? "columns" : A.zoom == Z_DETAILS ? "list" : "grid");
    fprintf(f, ",\"dnd\":{\"session\":%d,\"kind\":%d,\"item\":%d,\"effect\":%lu,\"drops\":%d,\"drags\":%d,\"last_effect\":%lu,\"last_n\":%d,\"last_move\":%d,\"preferred\":%lu,\"scroll_y\":%.2f,\"dest\":\"",
            D.session, D.kind, D.item, (unsigned long)D.effect, D.drops, D.drags, (unsigned long)D.last_effect, D.last_n, D.last_move, (unsigned long)D.last_preferred, ty);
    for (const char *c = D.dest; *c; c++) { if (*c == '\\' || *c == '"') fputc('\\', f); fputc(*c, f); }
    fprintf(f, "\",\"last_dest\":\"");
    for (const char *c = D.last_dest; *c; c++) { if (*c == '\\' || *c == '"') fputc('\\', f); fputc(*c, f); }
    fprintf(f, "\",\"drag\":"); json_list(f, D.drag_paths, D.drag_n);
    fprintf(f, ",\"drag_data\":"); json_list(f, D.drag_back, D.drag_back_n);
    fprintf(f, ",\"rows\":[");
    for (int i = 0; i < D.row_n; i++) {   /* the sidebar rows that take a drop: [x, y, w, h, where] */
        fprintf(f, "%s[%.1f,%.1f,%.1f,%.1f,\"", i ? "," : "", D.row[i].x, D.row[i].y, D.row[i].w, D.row[i].h);
        for (const char *c = D.row_to[i]; *c; c++) { if (*c == '\\' || *c == '"') fputc('\\', f); fputc(*c, f); }
        fprintf(f, "\"]");
    }
    fprintf(f, "],\"content\":[%.1f,%.1f,%.1f,%.1f]}", D.content.x, D.content.y, D.content.w, D.content.h);
}
