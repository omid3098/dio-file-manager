/* The Dio (Halo) theme from docs/design.md, light and dark, and the fonts. */
#include <windows.h>
#include <stdio.h>
#include "ui.h"

typedef struct palette {
    unsigned bg, surface, raised, fg, muted, subtle, line_base, accent, accent_fg;
    unsigned success, warn, danger, info;
} palette;

static const palette LIGHT = { 0xEDF0F8, 0xF7F8FC, 0xFFFFFF, 0x0E1424, 0x4A5570, 0x7A849C, 0x0F172A, 0x5B63E6, 0xFFFFFF,
                               0x0D9488, 0xD97706, 0xE11D48, 0x0284C7 };
static const palette DARK = { 0x060810, 0x10131C, 0x171B27, 0xEEF1FA, 0xB1B8CF, 0x767E9C, 0xFFFFFF, 0x8AB4FF, 0x0A0D1A,
                              0x5EEAD4, 0xFBBF24, 0xFC909E, 0x7DD3FC };

/* Written every frame: cui_set_theme and the system theme switch rebuild the stock colours. */
void theme_apply(cui_ctx *ui, bool dark) {
    const palette *p = dark ? &DARK : &LIGHT;
    cui_theme *t = cui_theme_get(ui);
    cui_color fg = CUI_RGB(p->fg), accent = CUI_RGB(p->accent), line = CUI_RGB(p->line_base);
    t->colors[CUI_TOKEN_BACKGROUND] = CUI_RGB(p->bg);
    t->colors[CUI_TOKEN_SURFACE] = CUI_RGB(p->surface);
    t->colors[CUI_TOKEN_SURFACE_RAISED] = CUI_RGB(p->raised);
    t->colors[CUI_TOKEN_SURFACE_MUTED] = cui_mix(CUI_RGB(p->bg), CUI_RGB(p->surface), 0.4f);
    t->colors[CUI_TOKEN_FOREGROUND] = fg;
    t->colors[CUI_TOKEN_TEXT_SECONDARY] = CUI_RGB(p->muted);
    t->colors[CUI_TOKEN_TEXT_MUTED] = CUI_RGB(p->subtle);
    t->colors[CUI_TOKEN_BORDER] = cui_alpha(line, 0.09f);
    t->colors[CUI_TOKEN_BORDER_SUBTLE] = cui_alpha(line, 0.09f);
    t->colors[CUI_TOKEN_BORDER_STRONG] = cui_alpha(line, 0.16f);
    t->colors[CUI_TOKEN_HOVER] = cui_alpha(fg, 0.06f);
    t->colors[CUI_TOKEN_ACTIVE] = cui_mix(CUI_RGB(p->surface), accent, dark ? 0.26f : 0.16f);
    t->colors[CUI_TOKEN_ACCENT] = accent;
    t->colors[CUI_TOKEN_ACCENT_STRONG] = cui_mix(accent, fg, 0.14f);
    t->colors[CUI_TOKEN_ACCENT_SUBTLE] = cui_alpha(accent, 0.16f);
    t->colors[CUI_TOKEN_ACCENT_FOREGROUND] = CUI_RGB(p->accent_fg);
    t->colors[CUI_TOKEN_SUCCESS] = CUI_RGB(p->success);
    t->colors[CUI_TOKEN_WARNING] = CUI_RGB(p->warn);
    t->colors[CUI_TOKEN_DANGER] = CUI_RGB(p->danger);
    t->colors[CUI_TOKEN_INFO] = CUI_RGB(p->info);
    t->colors[CUI_TOKEN_CONTROL_ON] = accent;
    t->colors[CUI_TOKEN_CONTROL_GLYPH] = CUI_RGB(p->accent_fg);
    t->colors[CUI_TOKEN_FOCUS_RING] = cui_alpha(accent, 0.55f);
    t->radius_control = 10;
    t->radius_panel = 14;
    t->radius_surface = 20;
    t->text_body = 14;
}

static bool file_exists(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES;
}

static cui_font load_first(cui_ctx *ui, const char *const *candidates, int n) {
    for (int i = 0; i < n; i++)
        if (file_exists(candidates[i])) {
            cui_font f = cui_font_load_file(ui, candidates[i]);
            if (f >= 0) return f;
        }
    return -1;
}

/* Segoe UI Variable for the interface; Vazirmatn (else Segoe UI) for Persian letters. Nothing is embedded. */
void theme_load_fonts(cui_ctx *ui) {
    char local[MAX_PATH] = "", v_reg[MAX_PATH], v_med[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof local);
    snprintf(v_reg, sizeof v_reg, "%s\\Microsoft\\Windows\\Fonts\\Vazirmatn-Regular.ttf", n ? local : "");
    snprintf(v_med, sizeof v_med, "%s\\Microsoft\\Windows\\Fonts\\Vazirmatn-Medium.ttf", n ? local : "");
    const char *body[] = { "C:\\Windows\\Fonts\\SegUIVar.ttf", "C:\\Windows\\Fonts\\segoeui.ttf" };
    const char *medium[] = { "C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\SegUIVar.ttf", "C:\\Windows\\Fonts\\segoeui.ttf" };
    const char *fa_body[] = { v_reg, "C:\\Windows\\Fonts\\Vazirmatn-Regular.ttf", "C:\\Windows\\Fonts\\segoeui.ttf" };
    const char *fa_med[] = { v_med, "C:\\Windows\\Fonts\\Vazirmatn-Medium.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf" };
    cui_font b = load_first(ui, body, 2), m = load_first(ui, medium, 3);
    cui_font fb = load_first(ui, fa_body, 3), fm = load_first(ui, fa_med, 3);
    if (b >= 0) cui_font_set_role(ui, CUI_FONT_BODY, b);
    if (m >= 0) cui_font_set_role(ui, CUI_FONT_MEDIUM, m);
    if (fb >= 0) cui_font_add_fallback(ui, CUI_FONT_BODY, fb);
    if (fm >= 0) cui_font_add_fallback(ui, CUI_FONT_MEDIUM, fm);
}
