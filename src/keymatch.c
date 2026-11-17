#include <windows.h>
#include <string.h>
#include "keymatch.h"

/* Windows' standard Persian layout, unshifted, on the keys of the US layout. */
static const wchar_t EN[] = L"qwertyuiop[]asdfghjkl;'zxcvbnm,./";
static const wchar_t FA[] = L"\x0636\x0635\x062B\x0642\x0641\x063A\x0639\x0647\x062E\x062D\x062C\x0686"
                            L"\x0634\x0633\x06CC\x0628\x0644\x0627\x062A\x0646\x0645\x06A9\x06AF"
                            L"\x0638\x0637\x0632\x0631\x0630\x062F\x067E\x0648./";

static wchar_t fold_ch(wchar_t c) {   /* 0 drops the character */
    if (c < 0x80) {
        if (c >= L'A' && c <= L'Z') return (wchar_t)(c + 32);
        if (c == L'-' || c == L'_') return L' ';
        return c;
    }
    if (c == 0x200C || c == 0x0640 || (c >= 0x064B && c <= 0x065F)) return 0;
    if (c == 0x064A || c == 0x0649) return 0x06CC;
    if (c == 0x0643) return 0x06A9;
    if (c >= 0x0660 && c <= 0x0669) return (wchar_t)(L'0' + c - 0x0660);
    if (c >= 0x06F0 && c <= 0x06F9) return (wchar_t)(L'0' + c - 0x06F0);
    if ((c >= 0xC0 && c <= 0x58F) || (c >= 0x1E00 && c <= 0x1FFF) || (c >= 0xFF21 && c <= 0xFF3A))
        return (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)c);
    return c;
}

size_t km_fold(const wchar_t *src, wchar_t *dst, size_t cap) {
    size_t n = 0;
    if (!cap) return 0;
    for (; *src && n + 1 < cap; src++) {
        wchar_t c = fold_ch(*src);
        if (c) dst[n++] = c;
    }
    dst[n] = 0;
    return n;
}

static int persian_letter(wchar_t c) { return c >= 0x0621 && c <= 0x06D3 && c != 0x0640 && !(c >= 0x064B && c <= 0x065F); }
static int ascii_letter(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }

/* What a Persian-layout letter is on the key it sits on (the shifted ones too). 0 when it is none of them. */
static wchar_t persian_key(wchar_t c) {
    switch (c) {
    case 0x064A: case 0x0649: return L'd';   /* Arabic yeh: shift+D */
    case 0x0643: return L'z';                /* Arabic kaf: shift+Z */
    case 0x0698: return L'c';                /* \x0698 */
    case 0x0624: return L'a';                /* \x0624 */
    case 0x0626: return L's';                /* \x0626 */
    case 0x0625: return L'f';                /* \x0625 */
    case 0x0623: return L'g';                /* \x0623 */
    case 0x0622: return L'h';                /* \x0622 */
    case 0x0629: return L'j';                /* \x0629 */
    case 0x0621: return L'm';                /* \x0621 */
    case 0x200C: return L'b';                /* the zero-width non-joiner: shift+B */
    }
    for (size_t i = 0; FA[i]; i++) if (FA[i] == c) return EN[i];
    return 0;
}

int km_reading(const wchar_t *q, wchar_t *out, size_t cap) {
    int latin = 0, persian = 0;
    size_t n = 0;
    if (!cap) return 0;
    out[0] = 0;
    for (const wchar_t *p = q; *p; p++) { if (ascii_letter(*p)) latin = 1; else if (persian_letter(*p)) persian = 1; }
    if (latin == persian) return 0;   /* no letters, or both scripts: no reading */
    for (; *q && n + 1 < cap; q++) {
        wchar_t c = *q, m = c;
        if (latin) {
            wchar_t l = ascii_letter(c) ? (wchar_t)(c | 0x20) : c;
            for (size_t i = 0; EN[i]; i++) if (EN[i] == l) { m = FA[i]; break; }
        } else {
            wchar_t k = persian_key(c);
            if (k) m = k;
        }
        out[n++] = m;
    }
    out[n] = 0;
    return n > 0;
}

void km_init(keymatch *k, const wchar_t *query, int cross) {
    wchar_t raw[KM_CAP];
    k->alt[0] = 0;
    km_fold(query, k->typed, KM_CAP);
    if (cross && km_reading(query, raw, KM_CAP)) {
        km_fold(raw, k->alt, KM_CAP);
        if (!wcscmp(k->alt, k->typed)) k->alt[0] = 0;
    }
}

int km_match(const keymatch *k, const wchar_t *name, int *via_alt) {
    wchar_t f[2 * KM_CAP];
    if (via_alt) *via_alt = 0;
    if (!k->typed[0]) return 0;
    km_fold(name, f, sizeof f / sizeof *f);
    if (wcsstr(f, k->typed)) return 1;
    if (k->alt[0] && wcsstr(f, k->alt)) { if (via_alt) *via_alt = 1; return 1; }
    return 0;
}
