#include "dlgtext.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

/* The prefix of `name` that fits `cap` bytes, in whole characters. `at` holds the `n` character starts. */
static void copy_fit(const char *name, const size_t *at, size_t n, char *out, size_t cap) {
    size_t u = n;
    while (u && at[u] > cap - 1) u--;
    memmove(out, name, at[u]);
    out[at[u]] = 0;
}

void dlg_short_name(const char *name, char *out, size_t cap) {
    if (!cap) return;
    enum { LIMIT = 4096 };
    static size_t at[LIMIT + 1];   /* the dialogs are built on one thread */
    size_t len = strlen(name);
    if (len > LIMIT) len = LIMIT;
    size_t n = units(name, len, at, LIMIT);
    if (n <= DLG_NAME_MAX && len <= cap - 1) { memmove(out, name, len); out[len] = 0; return; }
    if (cap < 4) { copy_fit(name, at, n, out, cap); return; }   /* no room for the ellipsis */
    size_t ext = 0;   /* characters of the extension, with its dot */
    for (size_t u = n; u-- > 1;) if (name[at[u]] == '.') { ext = n - u; break; }
    size_t keep = ext < 30 ? ext : 30;   /* the extension is the last thing to give up */
    size_t tail = ext > 24 ? keep : 24, head = DLG_NAME_MAX - 1 - tail;
    if (tail > n - 1) tail = n - 1;
    if (head > n - 1 - tail) head = n - 1 - tail;   /* a name of few characters but many bytes: it still loses at least one */
    /* Over the byte capacity: the longer side loses whole characters first, the extension last. */
    while (at[head] + 3 + (len - at[n - tail]) > cap - 1 && (head || tail)) {
        size_t spare = tail > keep ? at[n - keep] - at[n - tail] : 0;   /* bytes of the tail before the extension */
        if (head && at[head] >= spare) head--;
        else tail--;
    }
    size_t hb = at[head], tb = at[n - tail];
    memmove(out + hb + 3, name + tb, len - tb);   /* `out` may be `name` itself: the tail moves first */
    memcpy(out + hb, "\xE2\x80\xA6", 3);
    memmove(out, name, hb);
    out[hb + 3 + len - tb] = 0;
}

void dlg_short_leaf(const char *path, char *out, size_t cap) {
    const char *l = strrchr(path, '\\');
    dlg_short_name(l ? l + 1 : path, out, cap);   /* straight from the path: no buffer in between can cut the name */
}

void dlg_path_fit(const char *path, char *out, size_t cap) {
    if (strlen(path) < cap) memmove(out, path, strlen(path) + 1);
    else dlg_short_name(path, out, cap);
}

void dlg_quoted_leaf(const char *path, const char *tail, char *out, size_t cap) {
    enum { QUOTES = 6 };   /* two curly quotes of 3 bytes */
    size_t room = strlen(tail) + QUOTES + 1;
    char name[256];
    if (!cap) return;
    if (cap <= room + 3) {   /* no room for a name: the words stay, cut at a whole character if even they are too long */
        snprintf(out, cap, "%s", tail);
        size_t l = strlen(out), t = strlen(tail);
        if (l < t) { while (l && ((unsigned char)tail[l] & 0xC0) == 0x80) l--; out[l] = 0; }
        return;
    }
    dlg_short_leaf(path, name, cap - room < sizeof name ? cap - room : sizeof name);
    snprintf(out, cap, "\xE2\x80\x9C%s\xE2\x80\x9D%s", name, tail);
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

typedef struct { size_t at, len; } span;

int dlg_break_lines(const char *in, char *out, size_t cap, float max_w, dlg_measure_fn measure, void *ctx, int max_lines) {
    enum { MAXL = 300, LIMIT = 512 };
    static span line[MAXL];   /* the dialogs are built on one thread */
    size_t at[LIMIT + 1], len = strlen(in);
    int n = 0, shown;
    size_t ls = 0, le = 0;   /* the line being built: [ls, le) */
    bool have = false;
    if (!cap) return 0;
    for (size_t i = 0; i < len;) {
        while (i < len && (in[i] == ' ' || in[i] == '\t' || in[i] == '\n')) i++;
        size_t ws = i, we = i;
        while (we < len && in[we] != ' ' && in[we] != '\t' && in[we] != '\n') we++;
        if (we == ws) break;
        i = we;
        if (have && measure(ctx, in + ls, (int)(we - ls)) <= max_w) { le = we; continue; }
        if (have && n < MAXL) { line[n].at = ls; line[n++].len = le - ls; }
        have = true; ls = ws; le = we;   /* a new line starts with this word */
        size_t u = units(in + ws, we - ws, at, LIMIT);
        if (u >= LIMIT || measure(ctx, in + ws, (int)(we - ws)) <= max_w) continue;
        for (size_t s = 0; s < u;) {   /* the word is wider than a line: the longest piece that fits, at least one character */
            size_t e = s + 1;
            while (e < u && measure(ctx, in + ws + at[s], (int)(at[e + 1] - at[s])) <= max_w) e++;
            if (e < u) { if (n < MAXL) { line[n].at = ws + at[s]; line[n++].len = at[e] - at[s]; } }
            else { ls = ws + at[s]; le = we; }
            s = e;
        }
    }
    if (have && n < MAXL) { line[n].at = ls; line[n++].len = le - ls; }
    shown = n < max_lines ? n : max_lines;
    size_t o = 0;
    for (int k = 0; k < shown; k++) {
        size_t l = line[k].len;
        const char *s = in + line[k].at;
        char buf[LIMIT + 4];
        bool ell = k == shown - 1 && n > shown;
        if (l >= LIMIT) l = LIMIT - 1;
        if (ell) {   /* the last line that fits: whole characters, then the ellipsis, inside the line */
            size_t keep = units(s, l, at, LIMIT - 1);
            for (;;) {
                size_t b = at[keep];
                while (b && s[b - 1] == ' ') b--;
                memcpy(buf, s, b); memcpy(buf + b, "\xE2\x80\xA6", 3);
                if (keep == 0 || measure(ctx, buf, (int)(b + 3)) <= max_w) { l = b + 3; break; }
                keep--;
            }
            s = buf;
        }
        if (k && o + 1 < cap) out[o++] = '\n';
        for (size_t j = 0; j < l && o + 1 < cap; j++) out[o++] = s[j];
    }
    out[o] = 0;
    return shown;
}
