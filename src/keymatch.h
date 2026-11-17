#ifndef DFM_KEYMATCH_H
#define DFM_KEYMATCH_H
/* Name matching that works from either keyboard. A search typed on the wrong layout (Latin letters meant as Persian, or
   Persian letters meant as a Latin name) still finds the file: the typed text and its reading on the other layout are both
   looked for, as a substring, in the folded name. The other layout is the standard Windows Persian layout (KBDFA: q = \x0636,
   w = \x0635, ... ; = \x06A9, ' = \x06AF, shifted C = \x0698). Folding makes Arabic \x064A \x0649 and \x0643 equal to Persian \x06CC and \x06A9, Persian and Arabic-Indic
   digits equal to 0-9, drops the zero-width non-joiner and the tatweel and Arabic vowel marks, treats - _ and space as one,
   and ignores case. Pure functions: no allocation, no I/O, safe on any thread. */
#include <wchar.h>
#include <stddef.h>
#define KM_CAP 260          /* wide characters of a query or of one folded name (longer names are folded up to this) */
typedef struct keymatch {
    wchar_t typed[KM_CAP];  /* the folded query */
    wchar_t alt[KM_CAP];    /* the folded reading on the other layout; empty when there is none */
} keymatch;
/* Folds src into dst (cap includes the terminator); returns the folded length. */
size_t km_fold(const wchar_t *src, wchar_t *dst, size_t cap);
/* The query read on the other layout, unfolded (out is empty when the query has no such reading: it has no letters, or it
   mixes both scripts). Returns 1 when out is not empty. */
int km_reading(const wchar_t *query, wchar_t *out, size_t cap);
/* cross 0 looks for the typed text only. */
void km_init(keymatch *k, const wchar_t *query, int cross);
/* 1 when name contains the typed text or its other reading; *via_alt is 1 when only the other reading is in it. */
int km_match(const keymatch *k, const wchar_t *name, int *via_alt);
#endif
