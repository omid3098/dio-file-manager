/* Blink Compare: two selected pictures on top of each other in the content pane. A divider wipes from the older (A) to
 * the newer (B); holding Space shows B alone, the Blink button flips A and B twice a second. Windows' own decoder (imgdec.c)
 * reads each picture on a worker thread, scaled to the pixels the stage shows, so an 8K picture costs a stage's worth of memory. */
#include <math.h>
#include <string.h>
#include <windows.h>
#include "ui.h"
#include "imgdec.h"

#define BLINK_S 0.5
#define REDECODE_WAIT_S 0.25
#define MAX_EDGE 4096

typedef struct side {
    char path[FS_PATH_MAX], name[UI_NAME_BYTES];
    uint64_t size, mtime;
    imgdec_job *job;
    int job_w, job_h;            /* the box the running job was asked for */
    cui_texture tex;
    int tex_w, tex_h;            /* decoded pixels */
    int src_w, src_h;            /* the picture's own pixels */
    int box_w, box_h;            /* the box the texture was decoded for */
    bool alpha, failed;
} side;

static struct {
    bool open, held, blink;
    float split;                 /* 0..1: where A ends and B begins */
    double blink_t0, stage_t;
    side s[2];                   /* [0] older = A, [1] newer = B */
    cui_rect stage;              /* the stage's rectangle of the last frame, points */
    int want_w, want_h;          /* the box in device pixels the stage asks for */
    int shown;                   /* 0 split, 1 A alone, 2 B alone: what the last frame drew */
    LARGE_INTEGER b_qpc;         /* test: when a frame that drew B alone ended */
    unsigned frames;
} C;

bool cmp_open(void) { return C.open; }

static bool is_picture(int vis) {
    const fs_entry *e = app_item(vis);
    if (!e || (e->flags & (FS_DIR | FS_INCOMING)) || e->kind != FS_KIND_IMAGE) return false;
    return _stricmp(fs_ext_of(app_item_name(vis)), "svg") != 0;
}

/* The two selected pictures, in view order; false unless it is exactly two. */
static bool two_selected(int out[2]) {
    int n = 0;
    if (A.loc == LOC_TRASH || A.loc == LOC_OTHER || A.sel_n != 2) return false;
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i]) { if (n == 2 || !is_picture(i)) return false; out[n++] = i; }
    return n == 2;
}

bool cmp_can_open(void) { int v[2]; return !C.open && two_selected(v); }

static void side_free(side *s) {
    if (s->job) imgdec_release(s->job);
    if (s->tex) cui_image_destroy(A.ui, s->tex);
    memset(s, 0, sizeof *s);
}

static void side_start(side *s, int w, int h) {
    if (s->job) { imgdec_release(s->job); s->job = NULL; }
    s->job_w = w; s->job_h = h;
    s->job = imgdec_start(s->path, w, h);
    if (!s->job) s->failed = true;
    else cui_request_wakeup(A.ui, 0.03f);   /* the worker tells nobody: the window looks again soon */
}

void cmp_close(void) {
    if (!C.open) return;
    side_free(&C.s[0]); side_free(&C.s[1]);
    memset(&C, 0, sizeof C);
    cui_request_redraw(A.ui);
}

void cmp_open_selection(void) {
    int v[2];
    if (!two_selected(v)) return;
    cmp_close();
    for (int k = 0; k < 2; k++) {
        side *s = &C.s[k];
        app_path_of(v[k], s->path, sizeof s->path);
        snprintf(s->name, sizeof s->name, "%s", app_item_name(v[k]));
        s->size = app_item(v[k])->size; s->mtime = app_item(v[k])->mtime;
    }
    if (C.s[1].mtime < C.s[0].mtime) { side t = C.s[0]; C.s[0] = C.s[1]; C.s[1] = t; }   /* A is the older one */
    C.open = true; C.split = 0.5f;
    ui_popups_close_all(A.ui);
    cui_request_redraw(A.ui);
}

/* ---- text for the two files ---- */

static void when_text(uint64_t ft, char *out, size_t cap) {
    static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    FILETIME f = { (DWORD)ft, (DWORD)(ft >> 32) }, l;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&f, &l) || !FileTimeToSystemTime(&l, &st)) { out[0] = 0; return; }
    snprintf(out, cap, "%d %s %d, %02d:%02d", st.wDay, mon[(st.wMonth - 1) % 12], st.wYear, st.wHour, st.wMinute);
}

static void delta_text(char *out, size_t cap) {
    char sz[48], gap[48];
    int64_t d = (int64_t)C.s[1].size - (int64_t)C.s[0].size;
    int64_t sec = (int64_t)((C.s[1].mtime - C.s[0].mtime) / 10000000ull);
    fs_format_size(sz, sizeof sz, (uint64_t)(d < 0 ? -d : d));
    if (sec < 90) snprintf(gap, sizeof gap, "%lld s later", (long long)sec);
    else if (sec < 5400) snprintf(gap, sizeof gap, "%lld min later", (long long)(sec / 60));
    else if (sec < 129600) snprintf(gap, sizeof gap, "%lld h later", (long long)(sec / 3600));
    else snprintf(gap, sizeof gap, "%lld days later", (long long)(sec / 86400));
    if (d == 0) snprintf(out, cap, "same size \xC2\xB7 %s", gap);
    else snprintf(out, cap, "B is %s %s \xC2\xB7 %s", sz, d < 0 ? "smaller" : "larger", gap);
}

/* ---- the stage ---- */

typedef struct paint_arg { int shown; float split; } paint_arg;

static cui_rect fit(cui_rect r, int w, int h) {
    if (w < 1 || h < 1) return r;
    float s = fminf(r.w / (float)w, r.h / (float)h);
    float fw = (float)w * s, fh = (float)h * s;
    return (cui_rect){ r.x + (r.w - fw) * 0.5f, r.y + (r.h - fh) * 0.5f, fw, fh };
}

static void checker(cui_ctx *ui, cui_rect r) {
    const float t = 12.0f;
    cui_push_clip(ui, r);
    cui_draw_rect(ui, r, CUI_CORNERS(0), CUI_RGB(0x2a2f35));
    for (float y = 0; y < r.h; y += t)
        for (float x = ((int)(y / t) & 1) ? 0.0f : t; x < r.w; x += 2 * t)
            cui_draw_rect(ui, (cui_rect){ r.x + x, r.y + y, t, t }, CUI_CORNERS(0), CUI_RGB(0x3b4148));
    cui_pop_clip(ui);
}

static void draw_side(cui_ctx *ui, cui_rect stage, const side *s) {
    cui_rect r = fit(stage, s->src_w, s->src_h);
    if (!s->tex) return;
    if (s->alpha) checker(ui, r);
    cui_draw_image(ui, s->tex, r, (cui_rect){ 0, 0, 1, 1 }, CUI_CORNERS(0), CUI_RGB(0xffffff));
}

static void label(cui_ctx *ui, cui_rect stage, const char *text, bool right) {
    cui_text_metrics m = cui_measure_text(ui, CUI_FONT_BODY, 12, text, -1);
    cui_rect r = { right ? stage.x + stage.w - m.width - 28 : stage.x + 12, stage.y + 12, m.width + 16, 24 };
    cui_draw_rect_border(ui, r, CUI_CORNERS(4), CUI_RGBA(0x111b24df), CUI_RGBA(0xffffff40), 1);
    cui_draw_text(ui, CUI_FONT_BODY, 12, (cui_vec2){ r.x + 8, r.y + 4 }, CUI_RGB(0xf6f8ff), text, -1);
}

static void centered(cui_ctx *ui, cui_rect r, float dy, const char *text, cui_color c) {
    cui_text_metrics m = cui_measure_text(ui, CUI_FONT_BODY, 14, text, -1);
    cui_draw_text(ui, CUI_FONT_BODY, 14, (cui_vec2){ r.x + (r.w - m.width) * 0.5f, r.y + r.h * 0.5f + dy }, c, text, -1);
}

static void paint_stage(cui_ctx *ui, cui_rect r, void *user) {
    paint_arg a = *(paint_arg *)user;
    cui_push_clip(ui, r);
    cui_draw_rect(ui, r, CUI_CORNERS(10), CUI_RGB(0x14181c));
    if (a.shown == 0) {
        float sx = r.x + r.w * a.split;
        draw_side(ui, r, &C.s[1]);
        cui_rect left = { r.x, r.y, sx - r.x, r.h };
        cui_push_clip(ui, left);
        cui_draw_rect(ui, left, CUI_CORNERS(0), CUI_RGB(0x14181c));   /* where A is clear, B must not show beneath */
        draw_side(ui, r, &C.s[0]);
        cui_pop_clip(ui);
        cui_draw_rect(ui, (cui_rect){ sx - 1, r.y, 2, r.h }, CUI_CORNERS(0), CUI_RGB(0xffffff));
        cui_rect knob = { sx - 20, r.y + r.h * 0.5f - 20, 40, 40 };
        cui_draw_rect(ui, knob, CUI_CORNERS(10), CUI_RGB(0xffffff));
        cui_draw_icon(ui, CUI_ICON_CHEVRONS_LEFT, (cui_rect){ knob.x, knob.y, 20, 40 }, 18, CUI_RGB(0x111b24));
        cui_draw_icon(ui, CUI_ICON_CHEVRONS_RIGHT, (cui_rect){ knob.x + 20, knob.y, 20, 40 }, 18, CUI_RGB(0x111b24));
        if (sx - r.x >= 100) label(ui, r, "A \xC2\xB7 older", false);   /* a label stays only while its side shows */
        if (r.x + r.w - sx >= 100) label(ui, r, "B \xC2\xB7 newer", true);
    } else {
        draw_side(ui, r, &C.s[a.shown == 1 ? 0 : 1]);
        label(ui, r, a.shown == 1 ? "A \xC2\xB7 older" : "B \xC2\xB7 newer", a.shown == 2);
    }
    if ((!C.s[0].tex && !C.s[0].failed) || (!C.s[1].tex && !C.s[1].failed))
        centered(ui, r, -8, "Reading the pictures\xE2\x80\xA6", CUI_RGB(0xaeb6bf));
    for (int k = 0; k < 2; k++)
        if (C.s[k].failed) {
            char msg[UI_NAME_BYTES + 48];
            snprintf(msg, sizeof msg, "Windows can't read \xE2\x80\x9C%s\xE2\x80\x9D as a picture.", C.s[k].name);
            centered(ui, r, 12 + 24 * (float)k, msg, CUI_RGB(0xff9a8a));
        }
    cui_pop_clip(ui);
}

/* ---- loading: upload what a worker finished, ask again when the stage grew ---- */

static void poll_sides(double now) {
    for (int k = 0; k < 2; k++) {
        side *s = &C.s[k];
        if (s->job) {
            imgdec_result res;
            int st = imgdec_poll(s->job, &res);
            if (st == 1) {
                cui_texture t = cui_image_create(A.ui, res.w, res.h, res.rgba);
                if (t) {
                    if (s->tex) cui_image_destroy(A.ui, s->tex);
                    s->tex = t; s->tex_w = res.w; s->tex_h = res.h; s->src_w = res.src_w; s->src_h = res.src_h;
                    s->alpha = res.has_alpha; s->box_w = s->job_w; s->box_h = s->job_h;
                } else if (!s->tex) s->failed = true;
                else { s->box_w = s->job_w; s->box_h = s->job_h; }
                imgdec_release(s->job); s->job = NULL;
                cui_request_redraw(A.ui);
            } else if (st < 0) {
                if (!s->tex) s->failed = true;
                else { s->box_w = s->job_w; s->box_h = s->job_h; }   /* keep the picture we have; do not try this size again */
                imgdec_release(s->job); s->job = NULL;
                cui_request_redraw(A.ui);
            } else cui_request_wakeup(A.ui, 0.03f);
        }
        if (s->failed || s->job) continue;
        int w = C.want_w, h = C.want_h;
        if (w < 1 || h < 1) continue;
        if (w > MAX_EDGE) w = MAX_EDGE;
        if (h > MAX_EDGE) h = MAX_EDGE;
        if (s->tex && s->box_w >= w && s->box_h >= h) continue;   /* sharp enough: the stage only shrank or stayed */
        if (s->tex && s->tex_w >= s->src_w && s->tex_h >= s->src_h) continue;   /* already at the picture's own size */
        if (s->tex && now - C.stage_t < REDECODE_WAIT_S) { cui_request_wakeup(A.ui, 0.05f); continue; }   /* still being resized */
        side_start(s, w, h);
    }
}

/* ---- keys: while the pictures are open, only these work ---- */

void cmp_keys(void) {
    cui_ctx *ui = A.ui;
    if (!C.open) return;
    if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) { cmp_close(); return; }
    if (cui_key_pressed(ui, CUI_KEY_SPACE)) ui_menu_eat_activation();   /* a focused button must not click on Space */
    C.held = cui_key_down(ui, CUI_KEY_SPACE) && chrome_is_active();
    float step = (cui_key_mods(ui) & CUI_MOD_SHIFT) ? 0.10f : 0.02f;
    if (cui_key_pressed(ui, CUI_KEY_LEFT)) { C.split = fmaxf(0, C.split - step); C.blink = false; }
    if (cui_key_pressed(ui, CUI_KEY_RIGHT)) { C.split = fminf(1, C.split + step); C.blink = false; }
    if (cui_key_pressed(ui, CUI_KEY_HOME)) { C.split = 0; C.blink = false; }
    if (cui_key_pressed(ui, CUI_KEY_END)) { C.split = 1; C.blink = false; }
}

/* ---- the view ---- */

static void file_block(int k, bool right) {
    cui_ctx *ui = A.ui;
    const side *s = &C.s[k];
    char meta[160], when[48], sz[48], dims[48] = "";
    fs_format_size(sz, sizeof sz, s->size);
    when_text(s->mtime, when, sizeof when);
    if (s->src_w) snprintf(dims, sizeof dims, "%d \xC3\x97 %d \xC2\xB7 ", s->src_w, s->src_h);
    snprintf(meta, sizeof meta, "%s%s \xC2\xB7 %s", dims, sz, when);
    cui_column(ui, .width = CUI_FILL, .gap = 4) {
        cui_text(ui, s->name, .font = CUI_FONT_MEDIUM, .nowrap = true, .width = CUI_FILL, .align = right ? CUI_TEXT_RIGHT : CUI_TEXT_LEFT);
        cui_text(ui, meta, .size = 12, .color = CUI_COLOR_TEXT_MUTED, .nowrap = true, .width = CUI_FILL, .align = right ? CUI_TEXT_RIGHT : CUI_TEXT_LEFT);
    }
}

void cmp_view(void) {
    cui_ctx *ui = A.ui;
    if (!C.open) return;
    double now = cui_time(ui);
    bool both = C.s[0].tex && C.s[1].tex;
    int shown = 0;
    if (C.held && both) shown = 2;
    else if (C.blink && both) {
        if (C.blink_t0 == 0) C.blink_t0 = now;
        shown = (((int)floor((now - C.blink_t0) / BLINK_S)) & 1) ? 1 : 2;
        cui_request_wakeup(ui, (float)(BLINK_S - fmod(now - C.blink_t0, BLINK_S)) + 0.005f);
    }
    paint_arg pa = { shown, C.split };
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 16, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_IMAGE, 18, CUI_COLOR_FOREGROUND);
            cui_text(ui, "Compare", .font = CUI_FONT_MEDIUM, .size = 16);
            cui_spacer(ui);
            if (cui_button(ui, "Close", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_X, .shortcut = "Esc")) cmp_close();
        }
        if (C.open) {
            cui_row(ui, .width = CUI_FILL, .gap = 24) {
                file_block(0, false);
                file_block(1, true);
            }
            cui_canvas_result sr = cui_canvas(ui, "cmp-stage", CUI_FILL, CUI_FILL, paint_stage, &pa, sizeof pa);
            if (sr.rect.w > 1) {
                if (fabsf(sr.rect.w - C.stage.w) > 0.5f || fabsf(sr.rect.h - C.stage.h) > 0.5f) C.stage_t = now;
                C.stage = sr.rect;
                float sc = cui_dpi_scale(ui);
                C.want_w = (int)ceilf(sr.rect.w * sc); C.want_h = (int)ceilf(sr.rect.h * sc);
            }
            if (sr.pressed && sr.rect.w > 1) {   /* a press or a drag anywhere on the stage moves the divider there */
                C.split = fminf(1, fmaxf(0, sr.mouse.x / sr.rect.w));
                C.blink = false;
            }
            cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "Split", .variant = C.blink ? CUI_BUTTON_GHOST : CUI_BUTTON_SECONDARY, .icon = CUI_ICON_PANEL_LEFT, .tooltip = "Drag across the pictures")) C.blink = false;
                if (cui_button(ui, "Blink", .variant = C.blink ? CUI_BUTTON_SECONDARY : CUI_BUTTON_GHOST, .icon = CUI_ICON_EYE, .tooltip = "Flip between A and B")) { C.blink = true; C.blink_t0 = 0; }
                cui_gap(ui, 8);
                cui_text(ui, C.held ? "Space: release to go back" : "Space: hold to see B", .size = 13, .color = CUI_COLOR_TEXT_MUTED);
                cui_spacer(ui);
                char d[160];
                delta_text(d, sizeof d);
                cui_text(ui, d, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                if (C.s[0].src_w && C.s[1].src_w && (C.s[0].src_w != C.s[1].src_w || C.s[0].src_h != C.s[1].src_h))
                    cui_text(ui, "\xC2\xB7 sizes differ, each fitted to the pane", .size = 12, .color = CUI_COLOR_WARNING);
            }
        }
    }
    poll_sides(now);
    C.shown = shown;
    C.frames++;
}

/* The end of a frame. Test: when the first frame that shows B alone (after A or the split) was built. */
void cmp_frame_end(void) {
    static int prev;
    if (C.open && C.shown == 2 && prev != 2) QueryPerformanceCounter(&C.b_qpc);
    prev = C.open ? C.shown : 0;
}

void cmp_snapshot(FILE *f) {
    fprintf(f, ",\"cmp\":{\"open\":%d,\"can\":%d,\"shown\":%d,\"held\":%d,\"blink\":%d,\"split\":%.4f,\"b_qpc\":%lld,\"stage\":[%.1f,%.1f,%.1f,%.1f],\"want\":[%d,%d],\"frames\":%u,\"side\":[",
            C.open, cmp_can_open(), C.shown, C.held, C.blink, C.split, (long long)C.b_qpc.QuadPart, C.stage.x, C.stage.y, C.stage.w, C.stage.h, C.want_w, C.want_h, C.frames);
    for (int k = 0; k < 2; k++)
        fprintf(f, "%s{\"tex\":%d,\"tw\":%d,\"th\":%d,\"sw\":%d,\"sh\":%d,\"alpha\":%d,\"failed\":%d,\"busy\":%d}", k ? "," : "",
                C.s[k].tex != 0, C.s[k].tex_w, C.s[k].tex_h, C.s[k].src_w, C.s[k].src_h, C.s[k].alpha, C.s[k].failed, C.s[k].job != NULL);
    fprintf(f, "]}");
}

/* Test mode: WM_COPYDATA with dwData 0x444D43 and the text "open" or "close". */
bool cmp_test_message(void *copydata) {
    COPYDATASTRUCT *cd = copydata;
    if (!cd || cd->dwData != 0x444D43 || !cd->lpData) return false;
    if (!strncmp(cd->lpData, "open", 4)) cmp_open_selection();
    else if (!strncmp(cd->lpData, "close", 5)) cmp_close();
    return true;
}
