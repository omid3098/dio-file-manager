/* One scale shared by every place. Settings use APPDATA, including in sandbox checks. */
#include <windows.h>
#include <process.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "ui.h"

static bool settings_path(wchar_t *path, size_t cap, const wchar_t *name) {
    wchar_t root[FS_PATH_MAX];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", root, FS_PATH_MAX);
    if (!n || n >= FS_PATH_MAX) return false;
    swprintf_s(path, cap, L"%s\\dio-file-manager", root);
    CreateDirectoryW(path, NULL);
    swprintf_s(path, cap, L"%s\\dio-file-manager\\%s", root, name);
    return true;
}

static struct { HANDLE thread, wake; volatile LONG pending, failed, stop; } writer;

static bool save_view(long value) {
    char delay[16];
    if (GetEnvironmentVariableA("DFM_TEST_VIEW_SAVE_DELAY_MS", delay, sizeof delay)) Sleep((DWORD)atoi(delay));
    wchar_t path[FS_PATH_MAX], tmp[FS_PATH_MAX];
    if (!settings_path(path, FS_PATH_MAX, L"view.txt") || !settings_path(tmp, FS_PATH_MAX, L"view.tmp")) return false;
    FILE *f = _wfopen(tmp, L"w");
    if (!f) return false;
    bool ok = fprintf(f, "view-v2 %.3f %.3f %d\n", (float)(value % 1024) / 100.0f, (float)(value / 1024 % 1024) / 100.0f, (int)(value / 1048576)) > 0;
    if (fclose(f)) ok = false;
    if (ok) ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp);
    return ok;
}

static unsigned __stdcall write_views(void *unused) {
    (void)unused;
    for (;;) {
        WaitForSingleObject(writer.wake, INFINITE);
        /* A drag changes the value every frame: write once, 300 ms after it stopped moving (at once on exit). */
        while (!InterlockedCompareExchange(&writer.stop, 0, 0) && WaitForSingleObject(writer.wake, 300) != WAIT_TIMEOUT) {}
        LONG value;
        while ((value = InterlockedExchange(&writer.pending, 0)) != 0) {
            bool ok = false;   /* an indexer or scanner may hold the file for a moment: try again */
            for (int tries = 0; tries < 4 && !ok; tries++) {
                if (tries) Sleep(40);
                ok = save_view(value - 1);
            }
            if (!ok) InterlockedExchange(&writer.failed, 1);
        }
        if (InterlockedCompareExchange(&writer.stop, 0, 0) && !InterlockedCompareExchange(&writer.pending, 0, 0)) return 0;
    }
}

/* The value changes continuously and a disk flush can be slow: one writer saves the latest value once it settles. */
static void queue_view(void) {
    if (!writer.thread) {
        writer.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (writer.wake) writer.thread = (HANDLE)_beginthreadex(NULL, 0, write_views, NULL, 0, NULL);
        if (!writer.thread) {
            if (writer.wake) CloseHandle(writer.wake);
            writer.wake = NULL;
            InterlockedExchange(&writer.failed, 1);
            return;
        }
    }
    InterlockedExchange(&writer.pending, 1 + (LONG)(A.zv * 100.0f + 0.5f) + 1024 * (LONG)(A.last_icon_v * 100.0f + 0.5f) + 1048576 * (LONG)A.sidebar_w);   /* the sidebar width rides along: view-v2 keeps its first two numbers for older builds */
    SetEvent(writer.wake);
}

void app_flush_view(void) {
    if (!writer.thread) return;
    InterlockedExchange(&writer.stop, 1);
    SetEvent(writer.wake);
    WaitForSingleObject(writer.thread, INFINITE);
    CloseHandle(writer.thread);
    CloseHandle(writer.wake);
}

static int stop_of(float v) {
    if (v < 0.5f) return Z_DETAILS;
    if (v < 1.5f) return Z_LIST;
    if (v < 2.5f) return Z_SMALL;
    int s = (int)(v + 0.5f);
    return s > Z_EXTRA ? Z_EXTRA : s;
}

/* Icon edge: 32, 64, 96 and 144 px at the four icon stops, linear between them. */
float app_icon_size(void) {
    static const float px[4] = { 32, 64, 96, 144 };
    if (A.zv < 1.5f) return 16;
    float v = A.zv < Z_SMALL ? (float)Z_SMALL : A.zv > Z_EXTRA ? (float)Z_EXTRA : A.zv;
    int i = (int)(v - Z_SMALL);
    if (i >= 3) return px[3];
    return px[i] + (px[i + 1] - px[i]) * (v - (float)(Z_SMALL + i));
}

bool app_animations(void) {
    char t[16];
    if (A.frame_log && GetEnvironmentVariableA("DFM_TEST_ANIMATIONS", t, sizeof t) && !strcmp(t, "off")) return false;
    BOOL on = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) return true;
    return on != FALSE;
}

static void set_value(float v) {
    if (v < 0) v = 0;
    if (v > Z_COUNT - 1) v = Z_COUNT - 1;
    if (fabsf(v - A.zv) < 0.0005f) return;
    int old_stop = A.zoom, stop = stop_of(v);
    A.zv = v;
    A.zoom = stop;
    if (v >= 1.5f) A.last_icon_v = v < Z_SMALL ? (float)Z_SMALL : v;
    if (stop >= Z_SMALL) A.last_icon = stop;
    A.size_t = cui_time(A.ui);
    A.thumb_settled = false;
    if (stop != old_stop) {
        if (app_animations() && (old_stop == Z_SMALL) != (stop == Z_SMALL) && old_stop >= Z_SMALL && stop >= Z_SMALL) A.fade_t = A.size_t;   /* compact rows to tiles: one fade */
        bool list_kind = old_stop == Z_LIST || stop == Z_LIST;
        bool details_kind = old_stop == Z_DETAILS || stop == Z_DETAILS;
        A.view = stop == Z_DETAILS ? VIEW_LIST : VIEW_GRID;
        if (list_kind || details_kind) {   /* a different kind of view: Details, List or icons */
            ui_band_cancel();
            A.reset_scroll = true;
            A.last_click_idx = -1;
            if (app_animations()) A.fade_t = A.size_t;
        }
    }
    queue_view();
}

static struct { bool on; float from, to; double t0; } glide;
#define GLIDE_S 0.15
#define FADE_S 0.12

bool app_zoom_busy(void) { return glide.on; }

double app_glide_seconds(void) {
    char t[16];
    if (A.frame_log && GetEnvironmentVariableA("DFM_TEST_GLIDE_MS", t, sizeof t)) {
        int ms = atoi(t);
        if (ms >= 50 && ms <= 5000) return ms / 1000.0;
    }
    return GLIDE_S;
}

float app_view_fade(void) {
    if (A.fade_t <= 0) return 1;
    double t = (cui_time(A.ui) - A.fade_t) / FADE_S;
    if (t >= 1) { A.fade_t = 0; return 1; }
    cui_request_redraw(A.ui);
    return 0.15f + 0.85f * (float)t;
}

void app_set_zoomv(float v, bool animate) {
    if (v < 0) v = 0;
    if (v > Z_COUNT - 1) v = Z_COUNT - 1;
    if (animate && app_animations()) {
        if (fabsf(v - A.zv) < 0.0005f && !glide.on) return;
        glide.on = true; glide.from = A.zv; glide.to = v; glide.t0 = cui_time(A.ui);
        cui_request_redraw(A.ui);
        return;
    }
    glide.on = false;
    set_value(v);
}

void ui_zoom_tick(void) {
    if (!glide.on) return;
    double t = (cui_time(A.ui) - glide.t0) / app_glide_seconds();
    if (t >= 1) { glide.on = false; set_value(glide.to); return; }
    float e = 1.0f - (float)((1 - t) * (1 - t) * (1 - t));
    set_value(glide.from + (glide.to - glide.from) * e);
    cui_request_redraw(A.ui);
}

void app_load_view(void) {
    A.zv = (float)Z_MEDIUM; A.last_icon_v = (float)Z_MEDIUM; A.sidebar_w = SIDEBAR_W_DEFAULT;
    wchar_t path[FS_PATH_MAX];
    if (settings_path(path, FS_PATH_MAX, L"view.txt")) {
        FILE *f = _wfopen(path, L"r");
        float zv, icon;
        int stop, istop, sw = 0;
        if (f) {
            int got = fscanf(f, "view-v2 %f %f %d", &zv, &icon, &sw);
            if (got >= 2 && zv >= 0 && zv <= Z_COUNT - 1 && icon >= Z_SMALL && icon <= Z_COUNT - 1) {
                A.zv = zv; A.last_icon_v = icon;
                if (got == 3 && sw >= SIDEBAR_W_MIN && sw <= SIDEBAR_W_MAX) A.sidebar_w = sw;
            } else {
                rewind(f);
                if (fscanf(f, "view-v1 %d %d", &stop, &istop) == 2 && stop >= 0 && stop < Z_COUNT && istop >= Z_SMALL && istop < Z_COUNT) {
                    A.zv = (float)stop; A.last_icon_v = (float)istop;
                }
            }
            fclose(f);
        }
    }
    A.zoom = stop_of(A.zv);
    A.last_icon = stop_of(A.last_icon_v);
    A.view = A.zoom == Z_DETAILS ? VIEW_LIST : VIEW_GRID;
}

void app_set_sidebar_w(int w) {
    if (w < SIDEBAR_W_MIN) w = SIDEBAR_W_MIN;
    if (w > SIDEBAR_W_MAX) w = SIDEBAR_W_MAX;
    if (w == A.sidebar_w) return;
    A.sidebar_w = w;
    queue_view();
}

void app_set_zoom(int stop) {
    if (stop < 0) stop = 0;
    if (stop >= Z_COUNT) stop = Z_COUNT - 1;
    app_set_zoomv((float)stop, false);
}

void app_set_view(int view) { app_set_zoomv(view == VIEW_LIST ? 0.0f : A.last_icon_v, false); }

/* One step: the next stop in that direction (a whole step from a stop, the nearest stop between two). */
static float next_stop(float v, int dir) {
    float r = roundf(v);
    if (fabsf(v - r) < 0.02f) return r + (float)dir;
    return dir > 0 ? ceilf(v) : floorf(v);
}

void ui_zoom_keys(void) {
    cui_ctx *ui = A.ui;
    if (InterlockedExchange(&writer.failed, 0)) cui_toast(ui, "Could not save the view", .tone = CUI_TONE_WARNING);
    static float wheel;
    float dy = app_zoom_wheel();
    if (cui_wants_keyboard(ui) || act_modal_open() || ctxmenu_is_open(ui) || ui_view_options_open()) { wheel = 0; return; }
    if (cui_key_mods(ui) & CUI_MOD_CTRL) {
        if (cui_key_pressed(ui, CUI_KEY_EQUAL)) app_set_zoomv(next_stop(glide.on ? glide.to : A.zv, 1), true);
        if (cui_key_pressed(ui, CUI_KEY_MINUS)) app_set_zoomv(next_stop(glide.on ? glide.to : A.zv, -1), true);
        if (cui_key_pressed(ui, CUI_KEY_0)) app_set_zoomv((float)Z_MEDIUM, true);
    }
    wheel += dy;
    while (wheel >= 1) { app_set_zoomv(next_stop(glide.on ? glide.to : A.zv, 1), true); wheel--; }
    while (wheel <= -1) { app_set_zoomv(next_stop(glide.on ? glide.to : A.zv, -1), true); wheel++; }
}
