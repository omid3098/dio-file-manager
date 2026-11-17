/* Checks of src/keymatch.c: the Either Keyboard Search matcher (typed text and its reading on the other keyboard). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "keymatch.h"

static int failures;
/* want: 0 no match, 1 typed text, 2 only the other reading. cross 0 turns the other reading off. */
static void check(const wchar_t *query, const wchar_t *name, int want, int cross, const char *what) {
    keymatch k; int alt = 0;
    km_init(&k, query, cross);
    int got = km_match(&k, name, &alt) ? (alt ? 2 : 1) : 0;
    if (got != want) { printf("FAIL %s: want %d got %d\n", what, want, got); failures++; }
}
#define SALAM L"\x0633\x0644\x0627\x0645"                                   /* \x0633\x0644\x0627\x0645 */
#define KHAHAM L"\x0645\x06CC\x200C\x062E\x0648\x0627\x0647\x0645"          /* \x0645\x06CC\x200C\x062E\x0648\x0627\x0647\x0645 (with ZWNJ) */
#define KHAHAM_NOZW L"\x0645\x06CC\x062E\x0648\x0627\x0647\x0645"           /* \x0645\x06CC\x062E\x0648\x0627\x0647\x0645 */

int main(void) {
    /* Persian name typed on the English layout: s g h l = \x0633 \x0644 \x0627 \x0645 */
    check(L"sghl", SALAM L".txt", 2, 1, "sghl finds the Persian name");
    check(L"sghl", SALAM L".txt", 0, 0, "sghl, cross off, finds nothing");
    check(L"SGHL", SALAM L".txt", 2, 1, "capitals read the same");
    /* English name typed on the Persian layout: readme = \x0642 \x062B \x0634 \x06CC \x067E \x062B */
    check(L"\x0642\x062B\x0634\x06CC\x067E\x062B", L"README.md", 2, 1, "Persian-layout readme finds README.md");
    check(L"\x0642\x062B\x0634\x06CC\x067E\x062B", L"README.md", 0, 0, "Persian-layout readme, cross off");
    /* The typed text still wins and is not marked as the other reading */
    check(SALAM, SALAM L".txt", 1, 1, "Persian typed on the Persian layout");
    check(L"readme", L"README.md", 1, 1, "English typed on the English layout");
    /* Zero-width non-joiner, in the name, in the query, in neither, in both */
    check(KHAHAM, KHAHAM L".txt", 1, 1, "ZWNJ both sides");
    check(KHAHAM_NOZW, KHAHAM L".txt", 1, 1, "query without ZWNJ, name with");
    check(KHAHAM, KHAHAM_NOZW L".txt", 1, 1, "query with ZWNJ, name without");
    /* Arabic yeh and kaf against Persian */
    check(L"\x06A9\x06CC\x0627\x0646", L"\x0643\x064A\x0627\x0646.pdf", 1, 1, "Persian kaf yeh finds Arabic");
    check(L"\x0643\x064A\x0627\x0646", L"\x06A9\x06CC\x0627\x0646.pdf", 1, 1, "Arabic kaf yeh finds Persian");
    check(L"\x0649", L"\x06CC.txt", 1, 1, "alef maksura finds Persian yeh");
    /* Digits */
    check(L"\x06F1\x06F2\x06F3", L"report 123.txt", 1, 1, "Persian digits find Latin digits");
    check(L"123", L"\x06F1\x06F2\x06F3.txt", 1, 1, "Latin digits find Persian digits");
    check(L"\x0661\x0662", L"a12.txt", 1, 1, "Arabic-Indic digits find Latin digits");
    check(L"12", L"\x0663.txt", 0, 1, "digits do not match other digits");
    check(L"123", L"readme.md", 0, 1, "digits only: no other reading, no hit");
    /* Separators and case */
    check(L"bank statement", L"bank-statement-10.pdf", 1, 1, "hyphen equals space");
    check(L"Bank_Statement", L"bank statement.pdf", 1, 1, "underscore and case");
    /* No false hits: nothing in either reading */
    check(L"zzqx", SALAM L".txt", 0, 1, "no hit in either reading");
    check(L"zzqx", L"README.md", 0, 1, "no hit in an English name");
    check(L"\x0698\x0698\x0698", L"README.md", 0, 1, "no hit for Persian letters in an English name");
    check(L"[1]", L"\x062C" L"1\x0686.txt", 0, 1, "punctuation and digits only: no other reading");
    check(L"", L"README.md", 0, 1, "empty query");
    check(L"\x200C", L"README.md", 0, 1, "query of ZWNJ only");
    /* Mixed scripts: no other reading */
    check(L"sg\x0644", L"\x0633\x0644.txt", 0, 1, "mixed scripts are not converted");
    /* Shifted Persian letters: shift+C is \x0698, shift+B the ZWNJ */
    check(L"\x0698", L"c.txt", 2, 1, "zh reads as c");
    /* Shift held among small letters is the shifted Persian letter: Hf = \x0622\x0628, Hl,ca = \x0622\x0645\x0648\x0632\x0634 */
    check(L"Hf", L"\x0622\x0628.txt", 2, 1, "Hf finds the name with alef madda");
    check(L"Hl,ca", L"\x0622\x0645\x0648\x0632\x0634.txt", 2, 1, "Hl,ca finds amuzesh");
    check(L"Sghl", L"\x0633\x0644\x0627\x0645.txt", 0, 1, "shift+S is a different letter, as typed");
    /* Case folding beyond ASCII */
    check(L"\x00C9T\x00C9", L"\x00E9t\x00E9.txt", 1, 1, "Latin-1 capitals");
    check(L"\x0416", L"\x0436.txt", 1, 1, "Cyrillic capital");

    /* The reading shown to the user */
    {
        wchar_t r[KM_CAP];
        if (!km_reading(L"sghl", r, KM_CAP) || wcscmp(r, SALAM)) { printf("FAIL reading of sghl\n"); failures++; }
        if (!km_reading(L"\x0642\x062B\x0634\x06CC\x067E\x062B", r, KM_CAP) || wcscmp(r, L"readme")) { printf("FAIL reading of Persian readme\n"); failures++; }
        if (km_reading(L"123", r, KM_CAP) || r[0]) { printf("FAIL reading of digits\n"); failures++; }
    }
    /* A name past the fold buffer is cut, never overrun */
    {
        wchar_t big[1200]; keymatch k; int i;
        for (i = 0; i < 1199; i++) big[i] = L'a';
        big[1199] = 0;
        km_init(&k, L"aaa", 1);
        if (!km_match(&k, big, NULL)) { printf("FAIL long name\n"); failures++; }
    }
    printf(failures ? "keymatch: %d failure(s)\n" : "keymatch: all passed\n", failures);
    /* Budget: matching one name costs at most 1 microsecond on average, so a walk of 500,000 names adds at most 0.5 s of worker
       CPU (the walk itself takes 25 s or more on a big tree). The names mix Latin, Persian and Arabic letters, as a folder does. */
    {
        static const wchar_t *names[] = { L"Screenshot 2026-11-16 221403.png", L"invoice-11-2026.pdf",
            L"\x0642\x0631\x0627\x0631\x062F\x0627\x062F \x0627\x062C\x0627\x0631\x0647.pdf",
            L"\x067E\x0627\x0633\x067E\x0648\x0631\x062A \x0643\x064A\x0627\x0646.jpg", L"node_modules", L"README.md", L"setup-x64.exe", L"photos.zip" };
        keymatch k; LARGE_INTEGER f, t0, t1; long long n = 1000000, hits = 0;
        km_init(&k, L"zzqxnohit", 1);
        QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
        for (long long i = 0; i < n; i++) hits += km_match(&k, names[i & 7], NULL);
        QueryPerformanceCounter(&t1);
        double us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)f.QuadPart / (double)n;
        printf("keymatch: %.3f microseconds per name (budget 1.000), %lld hits\n", us, hits);
        if (us > 1.0 || hits) { printf("FAIL matching speed\n"); failures++; }
    }
    return failures != 0;
}
