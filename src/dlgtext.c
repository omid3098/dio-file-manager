#include "dlgtext.h"
#include <stdint.h>
#include <string.h>

static int decode(const unsigned char *s, size_t left, uint32_t *cp) {
    unsigned c = s[0];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if ((size_t)n > left) n = 1;
    if (n == 1) { *cp = c < 0x80 ? c : 0xFFFD; return 1; }
    uint32_t v = c & (0xFFu >> (n + 1));
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (s[i] & 0x3F);
    }
    *cp = v;
    return n;
}

/* A mark that stays with the letter before it. */
static int attaches(uint32_t c) {
    return (c >= 0x300 && c <= 0x36F) || (c >= 0x483 && c <= 0x489) || (c >= 0x591 && c <= 0x5BD) || (c >= 0x610 && c <= 0x61A) ||
           (c >= 0x64B && c <= 0x65F) || c == 0x670 || (c >= 0x6D6 && c <= 0x6DC) || (c >= 0x6DF && c <= 0x6E4) ||
           (c >= 0x6E7 && c <= 0x6E8) || (c >= 0x6EA && c <= 0x6ED) || (c >= 0x900 && c <= 0x903) || (c >= 0x93A && c <= 0x94F) ||
           (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) || c == 0x200C || c == 0x200D ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xFE20 && c <= 0xFE2F) ||
           (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0100 && c <= 0xE01EF);
}

/* Byte offsets where a character starts, then the end offset; returns the count. A joiner also holds the character after it. */
static size_t units(const char *s, size_t len, size_t *at, size_t cap) {
    size_t n = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        int w = decode((const unsigned char *)s + i, len - i, &cp);
        if (!(n && (attaches(cp) || prev == 0x200D)) && n < cap) at[n++] = i;
        prev = cp;
        i += (size_t)w;
    }
    at[n] = len;
    return n;
}

static void copy_cut(const char *name, size_t len, char *out, size_t cap) {
    size_t c = len < cap - 1 ? len : cap - 1;
    while (c && c < len && ((unsigned char)name[c] & 0xC0) == 0x80) c--;   /* a small buffer still gets whole characters */
    memmove(out, name, c);
    out[c] = 0;
}

void dlg_short_name(const char *name, char *out, size_t cap) {
    if (!cap) return;
    enum { LIMIT = 4096 };
    static size_t at[LIMIT + 1];   /* the dialogs are built on one thread */
    size_t len = strlen(name);
    if (len > LIMIT) len = LIMIT;
    size_t n = units(name, len, at, LIMIT);
    if (n <= DLG_NAME_MAX) { copy_cut(name, len, out, cap); return; }
    size_t ext = 0;   /* characters of the extension, with its dot */
    for (size_t u = n; u-- > 1;) if (name[at[u]] == '.') { ext = n - u; break; }
    size_t tail = ext > 24 ? (ext < 30 ? ext : 30) : 24;
    size_t head = DLG_NAME_MAX - 1 - tail;
    size_t hb = at[head], tb = at[n - tail];
    if (hb + 3 + (len - tb) > cap - 1) { copy_cut(name, len, out, cap); return; }
    memmove(out + hb + 3, name + tb, len - tb);   /* `out` may be `name` itself: the tail moves first */
    memcpy(out + hb, "\xE2\x80\xA6", 3);
    memmove(out, name, hb);
    out[hb + 3 + len - tb] = 0;
}

void dlg_break_words(const char *in, char *out, size_t cap, float max_w, dlg_measure_fn measure, void *ctx) {
    if (!cap) return;
    size_t o = 0, len = strlen(in);
    for (size_t i = 0; i < len && o + 1 < cap;) {
        size_t j = i;
        while (j < len && in[j] != ' ' && in[j] != '\n' && in[j] != '\t') j++;
        if (j > i) {
            enum { LIMIT = 512 };
            size_t at[LIMIT + 1];
            size_t n = units(in + i, j - i, at, LIMIT);
            if (n >= LIMIT || measure(ctx, in + i, (int)(j - i)) <= max_w) {
                for (size_t k = i; k < j && o + 1 < cap; k++) out[o++] = in[k];
            } else {
                for (size_t start = 0; start < n;) {   /* the longest piece that fits, at least one character */
                    size_t e = start + 1;
                    while (e < n && measure(ctx, in + i + at[start], (int)(at[e + 1] - at[start])) <= max_w) e++;
                    for (size_t k = at[start]; k < at[e] && o + 1 < cap; k++) out[o++] = in[i + k];
                    if (e < n && o + 1 < cap) out[o++] = '\n';
                    start = e;
                }
            }
        }
        while (j < len && o + 1 < cap && (in[j] == ' ' || in[j] == '\n' || in[j] == '\t')) out[o++] = in[j++];
        i = j;
    }
    out[o] = 0;
}
