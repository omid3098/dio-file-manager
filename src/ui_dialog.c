#include "ui.h"
#include "dlgtext.h"
#include <math.h>

/* CUI sizes a text block no narrower than its widest word, and the dialog's title and description share one column:
   one long file name would stretch both past the dialog's edge. So every dialog is built here: words wider than
   the line are broken between characters first, as Pango's WRAP_WORD_CHAR does. */
typedef struct { cui_ctx *ui; cui_font font; float size; } measure_t;

static float measure_text(void *c, const char *s, int len) {
    measure_t *m = c;
    return cui_measure_text(m->ui, m->font, m->size, s, len).width;
}

bool ui_dialog_begin(cui_ctx *ui, const char *title, bool *open, const char *description, bool persistent, bool no_close_button) {
    enum { WIDTH = 440 };
    cui_theme *th = cui_theme_get(ui);
    float panel = fminf((float)WIDTH, cui_viewport_size(ui).x - 32.0f);
    float line = panel - 48.0f - 2.0f - (no_close_button ? 0.0f : 44.0f) - 4.0f;   /* panel padding, border, the X and its gap, a margin */
    static char t[640], d[1600];
    measure_t mt = { ui, CUI_FONT_MEDIUM, th->heading[3] * th->font_scale };
    measure_t md = { ui, CUI_FONT_BODY, th->text_body * th->font_scale };
    dlg_break_words(title ? title : "", t, sizeof t, line, measure_text, &mt);
    if (description) dlg_break_words(description, d, sizeof d, line, measure_text, &md);
    return cui_dialog_begin(ui, t, open, &(cui_dialog_opts){ .description = description ? d : NULL, .width = WIDTH,
                                                            .persistent = persistent, .no_close_button = no_close_button });
}
