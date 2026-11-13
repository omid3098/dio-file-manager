/* The window's header is its title bar, as in GNOME Files. The Windows caption is cut away by WM_NCCALCSIZE
 * (the style keeps WS_CAPTION and WS_THICKFRAME, so the resize borders, the shadow and Windows 11's rounded corners stay),
 * the two 46 px header rows become the top of the window, and hit-testing tells Windows which header space drags.
 * This file holds the Win32 side (it sits in front of CUI's window procedure) and the three window buttons. */
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

/* dwmapi.h imports visual-style macros that collide with our LOC_* enum. */
__declspec(dllimport) HRESULT WINAPI DwmSetWindowAttribute(HWND, DWORD, LPCVOID, DWORD);

enum { NODRAG_MAX = 48, BTN_MIN = 0, BTN_MAX = 1, BTN_CLOSE = 2 };

static struct {
    bool active;                    /* the window has the focus; dims the header's icons when not */
    bool installed;
    int min_w, min_h;               /* logical minimum size of the client */
    cui_rect nodrag[NODRAG_MAX];    /* header controls from the last frame, in points */
    int nodrag_n;
    cui_rect btn[3];                /* the three window buttons, in points */
    bool hover[3], pressed[3];      /* from the last frame's canvases */
    bool nc_hover, nc_pressed;      /* the maximize button while Windows owns the pointer (HTMAXBUTTON) */
    bool tracking;
    bool maximized;
} C = { .active = true };

static UINT (WINAPI *p_dpi)(HWND);
static int (WINAPI *p_metric)(int, UINT);
static BOOL (WINAPI *p_adjust)(LPRECT, DWORD, BOOL, DWORD, UINT);

static void resolve(void) {
    if (p_dpi) return;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    p_dpi = (UINT (WINAPI *)(HWND))(void *)GetProcAddress(u, "GetDpiForWindow");
    p_metric = (int (WINAPI *)(int, UINT))(void *)GetProcAddress(u, "GetSystemMetricsForDpi");
    p_adjust = (BOOL (WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT))(void *)GetProcAddress(u, "AdjustWindowRectExForDpi");
}

static UINT dpi_of(HWND h) { resolve(); return p_dpi ? p_dpi(h) : 96; }

static float scale_of(HWND h) {
    const char *env = getenv("CUI_SCALE");   /* the same override CUI's window honours */
    if (env) { float s = (float)atof(env); if (s > 0.25f && s < 8) return s; }
    return (float)dpi_of(h) / 96.0f;
}

static int metric(int what, UINT dpi) { resolve(); return p_metric ? p_metric(what, dpi) : GetSystemMetrics(what); }

static int frame_px(UINT dpi) { return metric(SM_CYSIZEFRAME, dpi) + metric(SM_CXPADDEDBORDER, dpi); }

/* The window rect around a client of cw x ch, with the caption left out. */
static void window_size(HWND h, int cw, int ch, int *ww, int *wh) {
    resolve();
    UINT dpi = dpi_of(h);
    RECT r = { 0, 0, cw, ch };
    DWORD style = (DWORD)GetWindowLongW(h, GWL_STYLE);
    if (p_adjust) p_adjust(&r, style, FALSE, 0, dpi); else AdjustWindowRectEx(&r, style, FALSE, 0);
    *ww = r.right - r.left;
    *wh = r.bottom;   /* the client starts at the window's top edge: only the bottom border is added */
}

static void set_icons(HWND h, UINT dpi) {
    HINSTANCE inst = GetModuleHandleW(NULL);
    int big = metric(SM_CXICON, dpi), small = metric(SM_CXSMICON, dpi);
    HANDLE b = LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, big, big, LR_SHARED);
    HANDLE s = LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, small, small, LR_SHARED);
    if (b) SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)b);
    if (s) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)s);
}

void chrome_init(void *hwnd, int client_w, int client_h, int min_w, int min_h) {
    HWND h = hwnd;
    C.min_w = min_w; C.min_h = min_h;
    C.installed = true;
    set_icons(h, dpi_of(h));
    SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    float s = scale_of(h);
    int ww, wh;
    window_size(h, (int)(client_w * s), (int)(client_h * s), &ww, &wh);
    SetWindowPos(h, NULL, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void chrome_set_active(bool active) { C.active = active; if (A.ui) cui_request_redraw(A.ui); }
bool chrome_is_active(void) { return C.active; }

/* The border Windows draws around the window: our hairline, so no Windows grey shows, also when inactive. */
void chrome_set_theme(bool dark) {
    if (!A.hwnd) return;
    unsigned rgb = theme_hairline(dark);
    COLORREF border = RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
    DwmSetWindowAttribute((HWND)A.hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof border);
    BOOL immersive = dark;   /* the system menu follows the theme */
    if (FAILED(DwmSetWindowAttribute((HWND)A.hwnd, 20, &immersive, sizeof immersive)))
        DwmSetWindowAttribute((HWND)A.hwnd, 19, &immersive, sizeof immersive);
}

/* ---- what the header tells Windows ---- */

void chrome_begin_frame(void) { C.nodrag_n = 0; }

void chrome_nodrag(cui_rect r) {
    if (r.w > 0 && r.h > 0 && C.nodrag_n < NODRAG_MAX) C.nodrag[C.nodrag_n++] = r;
}

void chrome_nodrag_last(void) { chrome_nodrag(cui_last_rect(A.ui)); }

cui_color chrome_icon_color(void) {
    if (C.active) return (cui_color){ 0, 0, 0, 0 };
    return CUI_COLOR_TEXT_MUTED;
}

static bool in_rect(cui_rect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static LRESULT hit_test(HWND h, LPARAM l) {
    LRESULT d = DefWindowProcW(h, WM_NCHITTEST, 0, l);
    if (IsIconic(h) || !A.ui) return d;
    POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
    RECT wr;
    GetWindowRect(h, &wr);
    float s = scale_of(h);
    bool max = IsZoomed(h) != 0;
    if (!max) {
        int corner = (int)(12 * s + 0.5f), strip = (int)(5 * s + 0.5f);
        if (p.y >= wr.top && p.y < wr.top + corner) {
            if (d == HTLEFT) return HTTOPLEFT;
            if (d == HTRIGHT) return HTTOPRIGHT;
        }
        if (p.y >= wr.top && p.y < wr.top + strip && (d == HTCLIENT || d == HTCAPTION || d == HTTOP)) return HTTOP;
    }
    if (d != HTCLIENT && d != HTCAPTION) return d;
    POINT c = p;
    ScreenToClient(h, &c);
    float x = (float)c.x / s, y = (float)c.y / s;
    if (y < 0 || y >= HEADER_H) return HTCLIENT;
    if (in_rect(C.btn[BTN_MAX], x, y)) return HTMAXBUTTON;
    if (in_rect(C.btn[BTN_MIN], x, y) || in_rect(C.btn[BTN_CLOSE], x, y)) return HTCLIENT;
    for (int i = 0; i < C.nodrag_n; i++) if (in_rect(C.nodrag[i], x, y)) return HTCLIENT;
    return HTCAPTION;
}

static void toggle_maximize(HWND h) { ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE); }

static void redraw(void) { if (A.ui) cui_request_redraw(A.ui); }

/* Windows posts the press to us, not to CUI: give CUI the click so an open popover or a text field lets go. */
static void feed_click(HWND h, LPARAM screen) {
    if (!A.ui) return;
    POINT p = { GET_X_LPARAM(screen), GET_Y_LPARAM(screen) };
    ScreenToClient(h, &p);
    float s = scale_of(h);
    cui_input_mouse_move(A.ui, (float)p.x / s, (float)p.y / s);
    cui_input_mouse_button(A.ui, CUI_MOUSE_LEFT, true);
    cui_input_mouse_button(A.ui, CUI_MOUSE_LEFT, false);
    cui_request_redraw(A.ui);
}

bool chrome_message(void *hwnd, unsigned msg, uintptr_t w, intptr_t l, intptr_t *result) {
    HWND h = hwnd;
    switch (msg) {
    case WM_NCCALCSIZE: {
        if (!C.installed) return false;
        RECT *r = w ? &((NCCALCSIZE_PARAMS *)l)->rgrc[0] : (RECT *)l;
        LONG top = r->top;
        LRESULT res = DefWindowProcW(h, msg, w, l);
        r->top = top;
        if (IsZoomed(h)) r->top += frame_px(dpi_of(h));   /* a maximized window overhangs the screen by its frame */
        *result = res;
        return true;
    }
    case WM_NCHITTEST:
        if (!C.installed) return false;
        *result = hit_test(h, l);
        return true;
    case WM_GETMINMAXINFO: {
        if (!C.installed) return false;
        DefWindowProcW(h, msg, w, l);
        MINMAXINFO *mm = (MINMAXINFO *)l;
        float s = scale_of(h);
        int ww, wh;
        window_size(h, (int)(C.min_w * s), (int)(C.min_h * s), &ww, &wh);
        mm->ptMinTrackSize.x = ww;
        mm->ptMinTrackSize.y = wh;
        *result = 0;
        return true;
    }
    case WM_ACTIVATE:
        chrome_set_active(LOWORD(w) != WA_INACTIVE);
        return false;
    case WM_DPICHANGED:
        set_icons(h, HIWORD(w));
        return false;
    case WM_SYSKEYDOWN:
        if (w == VK_SPACE && (GetKeyState(VK_MENU) & 0x8000)) {   /* CUI swallows system keys; Alt+Space opens the system menu */
            SendMessageW(h, WM_SYSCOMMAND, SC_KEYMENU, ' ');
            *result = 0;
            return true;
        }
        return false;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (w == HTMAXBUTTON) { C.nc_pressed = true; redraw(); *result = 0; return true; }
        if (w == HTCAPTION && msg == WM_NCLBUTTONDOWN) feed_click(h, l);
        return false;
    case WM_NCLBUTTONUP:
        if (w == HTMAXBUTTON) {
            bool was = C.nc_pressed;
            C.nc_pressed = false;
            redraw();
            if (was) toggle_maximize(h);
            *result = 0;
            return true;
        }
        return false;
    case WM_NCMOUSEMOVE: {
        bool over = w == HTMAXBUTTON;
        if (over && !C.tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE | TME_NONCLIENT, h, 0 };
            TrackMouseEvent(&t);
            C.tracking = true;
        }
        if (over != C.nc_hover) { C.nc_hover = over; if (!over) C.nc_pressed = false; redraw(); }
        return false;   /* Windows 11 shows its snap layouts from here */
    }
    case WM_NCMOUSELEAVE:
        C.tracking = false;
        if (C.nc_hover || C.nc_pressed) { C.nc_hover = C.nc_pressed = false; redraw(); }
        return false;
    default:
        return false;
    }
}

/* ---- the three window buttons ---- */

typedef struct wb_paint { int kind; bool hover, pressed, active, maximized; } wb_paint;

static void paint_wb(cui_ctx *ui, cui_rect r, void *user) {
    const wb_paint *p = user;
    float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    cui_color fg = cui_color_of(ui, CUI_COLOR_FOREGROUND);
    if (p->pressed) cui_draw_rect(ui, r, CUI_CORNERS(r.w / 2), cui_alpha(fg, 0.16f));
    else if (p->hover) cui_draw_rect(ui, r, CUI_CORNERS(r.w / 2), cui_alpha(fg, 0.10f));
    cui_color ink = cui_color_of(ui, (p->hover || p->pressed) ? CUI_COLOR_FOREGROUND : p->active ? CUI_COLOR_TEXT_SECONDARY : CUI_COLOR_TEXT_MUTED);
    const float t = 1.5f;
    if (p->kind == BTN_MIN) {
        cui_draw_line(ui, (cui_vec2){ cx - 4.5f, cy + 3.0f }, (cui_vec2){ cx + 4.5f, cy + 3.0f }, t, ink);
    } else if (p->kind == BTN_MAX && !p->maximized) {
        cui_draw_rect_border(ui, (cui_rect){ cx - 4.5f, cy - 4.5f, 9, 9 }, CUI_CORNERS(1.5f), (cui_color){ 0, 0, 0, 0 }, ink, t);
    } else if (p->kind == BTN_MAX) {
        cui_draw_rect_border(ui, (cui_rect){ cx - 5.0f, cy - 2.5f, 7.5f, 7.5f }, CUI_CORNERS(1.5f), (cui_color){ 0, 0, 0, 0 }, ink, t);
        cui_vec2 back[5] = { { cx - 2.5f, cy - 2.5f }, { cx - 2.5f, cy - 5.0f }, { cx + 5.0f, cy - 5.0f }, { cx + 5.0f, cy + 2.5f }, { cx + 2.5f, cy + 2.5f } };
        cui_draw_polyline(ui, back, 5, t, ink, false);
    } else {
        cui_draw_line(ui, (cui_vec2){ cx - 4.0f, cy - 4.0f }, (cui_vec2){ cx + 4.0f, cy + 4.0f }, t, ink);
        cui_draw_line(ui, (cui_vec2){ cx + 4.0f, cy - 4.0f }, (cui_vec2){ cx - 4.0f, cy + 4.0f }, t, ink);
    }
}

void chrome_window_buttons(void) {
    cui_ctx *ui = A.ui;
    HWND h = (HWND)A.hwnd;
    static const char *const ids[3] = { "win-minimize", "win-maximize", "win-close" };
    C.maximized = IsZoomed(h) != 0;
    cui_gap(ui, 4);   /* the row's own gap of 4 on each side makes 12 after the view-options button */
    cui_row(ui, .height = CUI_FILL, .gap = 6, .align = CUI_ALIGN_CENTER) {
        for (int i = 0; i < 3; i++) {
            wb_paint p = { i, C.hover[i] || (i == BTN_MAX && C.nc_hover), C.pressed[i] || (i == BTN_MAX && C.nc_pressed),
                           C.active, C.maximized };
            cui_canvas_result res = cui_canvas(ui, ids[i], CUI_PX(24), CUI_PX(24), paint_wb, &p, sizeof p);
            C.btn[i] = res.rect;
            if (res.hovered != C.hover[i] || res.pressed != C.pressed[i]) cui_request_redraw(ui);
            C.hover[i] = res.hovered; C.pressed[i] = res.pressed;
            if (res.clicked) {
                if (i == BTN_MIN) ShowWindow(h, SW_MINIMIZE);
                else if (i == BTN_MAX) toggle_maximize(h);
                else PostMessageW(h, WM_CLOSE, 0, 0);
            }
        }
    }
}

/* A pane-coloured veil over a header icon that has no colour option (the main menu button). Clicks go through it. */
static void paint_veil(cui_ctx *ui, cui_rect r, void *user) {
    (void)user;
    if (!C.active) cui_draw_rect(ui, r, CUI_CORNERS(8), cui_alpha(cui_color_of(ui, CUI_COLOR_BACKGROUND), 0.42f));
}

void chrome_veil(void) {
    cui_canvas_result v = cui_canvas(A.ui, "icon-veil", CUI_FILL, CUI_FILL, paint_veil, NULL, 0);
    ui_anchor_passthrough(A.ui);
    chrome_nodrag(v.rect);
}

void chrome_snapshot(FILE *f) {
    HWND h = (HWND)A.hwnd;
    RECT wr, cr;
    GetWindowRect(h, &wr);
    GetClientRect(h, &cr);
    POINT o = { 0, 0 };
    ClientToScreen(h, &o);
    fprintf(f, ",\"chrome\":{\"active\":%d,\"maximized\":%d,\"scale\":%.4f,\"window\":[%ld,%ld,%ld,%ld],\"client\":[%ld,%ld,%ld,%ld],\"buttons\":[",
            C.active, IsZoomed(h) != 0, scale_of(h), wr.left, wr.top, wr.right, wr.bottom, o.x, o.y, o.x + cr.right, o.y + cr.bottom);
    for (int i = 0; i < 3; i++)
        fprintf(f, "%s[%.2f,%.2f,%.2f,%.2f]", i ? "," : "", C.btn[i].x, C.btn[i].y, C.btn[i].w, C.btn[i].h);
    fprintf(f, "],\"nodrag\":[");
    for (int i = 0; i < C.nodrag_n; i++)
        fprintf(f, "%s[%.2f,%.2f,%.2f,%.2f]", i ? "," : "", C.nodrag[i].x, C.nodrag[i].y, C.nodrag[i].w, C.nodrag[i].h);
    fprintf(f, "]}");
}
