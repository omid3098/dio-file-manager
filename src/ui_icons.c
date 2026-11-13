/* Drawn file and folder icons for the grid view (not outline icons) and small helpers for the list. */
#include <ctype.h>
#include <string.h>
#include "ui.h"

const char *ui_kind_icon(int kind) {
    switch (kind) {
    case FS_KIND_FOLDER: return CUI_ICON_FOLDER;
    case FS_KIND_IMAGE: return CUI_ICON_IMAGE;
    case FS_KIND_AUDIO: return CUI_ICON_MUSIC;
    case FS_KIND_VIDEO: return CUI_ICON_VIDEO;
    case FS_KIND_ARCHIVE: return CUI_ICON_ARCHIVE;
    case FS_KIND_DOC: case FS_KIND_TEXT: return CUI_ICON_FILE_TEXT;
    case FS_KIND_CODE: return CUI_ICON_CODE;
    case FS_KIND_SHEET: return CUI_ICON_GRID_3X3;
    case FS_KIND_EXEC: return CUI_ICON_TERMINAL;
    default: return CUI_ICON_FILE;
    }
}

cui_color ui_kind_color(int kind) {
    switch (kind) {
    case FS_KIND_FOLDER: return CUI_COLOR_ACCENT;
    case FS_KIND_IMAGE: return CUI_COLOR_SERIES(2);
    case FS_KIND_AUDIO: return CUI_COLOR_SERIES(3);
    case FS_KIND_VIDEO: return CUI_COLOR_SERIES(5);
    case FS_KIND_ARCHIVE: return CUI_COLOR_WARNING;
    case FS_KIND_DOC: return CUI_COLOR_DANGER;
    case FS_KIND_CODE: return CUI_COLOR_SERIES(0);
    case FS_KIND_SHEET: return CUI_COLOR_SUCCESS;
    case FS_KIND_EXEC: return CUI_COLOR_INFO;
    default: return CUI_COLOR_TEXT_SECONDARY;
    }
}

/* A folder as in GNOME Files: a back flap with a tab, and a lighter front, in the accent colour. */
void ui_draw_folder(cui_ctx *ui, float cx, float cy, float s) {
    cui_color accent = cui_color_of(ui, CUI_COLOR_ACCENT);
    cui_color fg = cui_color_of(ui, CUI_COLOR_FOREGROUND);
    cui_color back = cui_mix(accent, fg, 0.22f);
    cui_color top = cui_mix(accent, CUI_RGB(0xFFFFFF), 0.12f);
    float x = cx - s * 0.5f, y = cy - s * 0.5f;
    float r = s * 0.08f;
    cui_draw_rect(ui, (cui_rect){ x + s * 0.05f, y + s * 0.16f, s * 0.38f, s * 0.2f },
                  (cui_corners){ r, r * 1.6f, 0, 0 }, back);
    cui_draw_rect(ui, (cui_rect){ x + s * 0.05f, y + s * 0.24f, s * 0.9f, s * 0.58f }, CUI_CORNERS(r), back);
    cui_draw_rect_gradient(ui, (cui_rect){ x + s * 0.05f, y + s * 0.36f, s * 0.9f, s * 0.5f }, CUI_CORNERS(r), top, accent);
}

/* A page with a folded corner and a small type label. */
void ui_draw_file(cui_ctx *ui, float cx, float cy, float s, const char *name, int kind) {
    float w = s * 0.68f, h = s * 0.9f, f = s * 0.24f;
    float x0 = cx - w * 0.5f, y0 = cy - h * 0.5f, x1 = x0 + w, y1 = y0 + h;
    cui_color page = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED);
    cui_color edge = cui_color_of(ui, CUI_COLOR_BORDER_STRONG);
    cui_color tone = cui_color_of(ui, ui_kind_color(kind));
    cui_vec2 body[5] = { { x0, y0 }, { x1 - f, y0 }, { x1, y0 + f }, { x1, y1 }, { x0, y1 } };
    cui_draw_shadow(ui, (cui_rect){ x0, y0 + s * 0.02f, w, h }, CUI_CORNERS(s * 0.05f), s * 0.05f, 0, (cui_vec2){ 0, s * 0.02f },
                    cui_alpha(cui_color_of(ui, CUI_COLOR_FOREGROUND), 0.10f));
    cui_draw_polygon(ui, body, 5, page);
    cui_draw_polyline(ui, body, 5, 1.0f, edge, true);
    cui_vec2 fold[3] = { { x1 - f, y0 }, { x1 - f, y0 + f }, { x1, y0 + f } };
    cui_draw_polygon(ui, fold, 3, cui_alpha(tone, 0.28f));
    cui_draw_polyline(ui, fold, 3, 1.0f, edge, true);
    /* type label */
    char label[8] = "";
    const char *ext = fs_ext_of(name);
    size_t n = strlen(ext);
    if (n > 0 && n <= 4) {
        for (size_t i = 0; i < n; i++) label[i] = (char)toupper((unsigned char)ext[i]);
        label[n] = 0;
    }
    if (s >= 40) {
        if (label[0]) {
            float fs = s * (n > 3 ? 0.14f : 0.17f);
            cui_text_metrics m = cui_measure_text(ui, CUI_FONT_MEDIUM, fs, label, -1);
            cui_draw_text(ui, CUI_FONT_MEDIUM, fs, (cui_vec2){ cx - m.width * 0.5f, y0 + h * 0.58f }, tone, label, -1);
        } else {
            cui_draw_icon(ui, ui_kind_icon(kind), (cui_rect){ cx - s * 0.16f, y0 + h * 0.5f, s * 0.32f, s * 0.32f }, s * 0.3f, tone);
        }
        cui_draw_rect(ui, (cui_rect){ x0 + w * 0.2f, y0 + h * 0.32f, w * 0.6f, s * 0.03f }, CUI_CORNERS(s * 0.015f), cui_alpha(edge, 1.0f));
    }
}
