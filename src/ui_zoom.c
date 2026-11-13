/* One scale shared by every place. Settings use APPDATA, including in sandbox checks. */
#include <windows.h>
#include <stdio.h>
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

static void save_view(void) {
    wchar_t path[FS_PATH_MAX], tmp[FS_PATH_MAX];
    if (!settings_path(path, FS_PATH_MAX, L"view.txt") || !settings_path(tmp, FS_PATH_MAX, L"view.tmp")) return;
    FILE *f = _wfopen(tmp, L"w");
    if (!f) return;
    bool ok = fprintf(f, "view-v1 %d %d\n", A.zoom, A.last_icon) > 0;
    if (fclose(f)) ok = false;
    if (ok) ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) { DeleteFileW(tmp); cui_toast(A.ui, "Could not save the view", .tone = CUI_TONE_WARNING); }
}

void app_load_view(void) {
    A.zoom = A.last_icon = Z_MEDIUM;
    wchar_t path[FS_PATH_MAX];
    if (settings_path(path, FS_PATH_MAX, L"view.txt")) {
        FILE *f = _wfopen(path, L"r");
        int stop, icon;
        if (f) {
            if (fscanf(f, "view-v1 %d %d", &stop, &icon) == 2 && stop >= 0 && stop < Z_COUNT && icon >= Z_SMALL && icon < Z_COUNT) {
                A.zoom = stop; A.last_icon = icon;
            }
            fclose(f);
        }
    }
    A.view = A.zoom == Z_DETAILS ? VIEW_LIST : VIEW_GRID;
}

void app_set_zoom(int stop) {
    if (stop < 0) stop = 0;
    if (stop >= Z_COUNT) stop = Z_COUNT - 1;
    if (stop == A.zoom) return;
    A.zoom = stop;
    A.view = stop == Z_DETAILS ? VIEW_LIST : VIEW_GRID;
    if (stop >= Z_SMALL) A.last_icon = stop;
    A.reset_scroll = true;
    A.last_click_idx = -1;
    save_view();
}

void app_set_view(int view) { app_set_zoom(view == VIEW_LIST ? Z_DETAILS : A.last_icon); }

void ui_zoom_keys(void) {
    cui_ctx *ui = A.ui;
    static float wheel;
    float dy = app_zoom_wheel();
    if (cui_wants_keyboard(ui) || act_modal_open() || ctxmenu_is_open(ui) || ui_view_options_open()) { wheel = 0; return; }
    if (cui_key_mods(ui) & CUI_MOD_CTRL) {
        if (cui_key_pressed(ui, CUI_KEY_EQUAL)) app_set_zoom(A.zoom + 1);
        if (cui_key_pressed(ui, CUI_KEY_MINUS)) app_set_zoom(A.zoom - 1);
        if (cui_key_pressed(ui, CUI_KEY_0)) app_set_zoom(Z_MEDIUM);
    }
    wheel += dy;
    while (wheel >= 1) { app_set_zoom(A.zoom + 1); wheel--; }
    while (wheel <= -1) { app_set_zoom(A.zoom - 1); wheel++; }
}
