#pragma once
#include <stddef.h>

/* Text of dialogs that name a file: the name is cut in the middle, and a word wider than the line is broken
   between characters (GNOME Files: eel_str_middle_truncate at 50, Pango WRAP_WORD_CHAR). Pure string work. */
enum { DLG_NAME_MAX = 50 };

/* What CUI v1.4.0 keeps of a toast (cui_toast_item in src/cui_internal.h:419-420, copied by snprintf in cui_overlay.c:730-731):
   the title is cut at 127 bytes, the description at 255. Format a toast's text for these, never a bigger buffer:
   what CUI cuts is lost, whole message words included. */
enum { DLG_TOAST_TITLE_CAP = 128, DLG_TOAST_DESC_CAP = 256 };

/* `name` shortened to at most DLG_NAME_MAX characters, cut in the middle with an ellipsis, the extension kept.
   `out` may be `name` itself. A character is a base letter with its combining marks, joiners and variation
   selectors: they are never cut apart. A name too long for `cap` bytes loses whole characters the same way,
   the longer side first, so the ellipsis and the extension stay. */
void dlg_short_name(const char *name, char *out, size_t cap);

/* The last part of `path` (after the last backslash), shortened as dlg_short_name does. Take a dialog's file name
   from the path with this, never through a smaller buffer first: a buffer cuts a name of 2-byte letters in half. */
void dlg_short_leaf(const char *path, char *out, size_t cap);

/* `out` = a curly-quoted `dlg_short_leaf` of `path`, then `tail`. The name gives way to fit `cap`, never the tail:
   a toast keeps its words and its name stays valid UTF-8. */
void dlg_quoted_leaf(const char *path, const char *tail, char *out, size_t cap);

/* `path` as a toast description: whole when it fits `cap` bytes, else shortened in the middle (dlg_short_name) so its end
   stays and no character is cut apart. Give it DLG_TOAST_DESC_CAP; CUI would cut a longer path at a byte. */
void dlg_path_fit(const char *path, char *out, size_t cap);

/* Width in pixels of the first `len` bytes of `s`. */
typedef float (*dlg_measure_fn)(void *ctx, const char *s, int len);

/* Copies `in` to `out`, putting a line break inside every word wider than `max_w` so each piece fits. */
void dlg_break_words(const char *in, char *out, size_t cap, float max_w, dlg_measure_fn measure, void *ctx);
