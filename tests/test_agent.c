/* Console checks of the agent base (src/agent.c): what a coding agent's answer may do to a file name (illegal characters,
 * device names, a path smuggled into a name, the extension kept, a name the same as the old one), reading the answer
 * (fences, chatter, a cut-off answer, \u escapes, Persian), which names count as machine-made, the prompt carrying the
 * start of a Persian text file, and the process runner: a big prompt and a big answer through the pipes without a
 * deadlock, Cancel and the timeout ending the process, a missing program failing. The runner uses findstr as the stand-in
 * agent (it copies its input to its output); the choice file goes to a sandbox APPDATA, never the real one. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent.h"

#ifndef DFM_TEST_WORK
#define DFM_TEST_WORK ".agent-work\\console"
#endif

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   %s\n", #c); } while (0)

static bool clean(const char *old, const char *sug, char *out) { return agent_clean_name(old, sug, out, 300); }

static void names_cleaned(void) {
    char o[300];
    CHECK(clean("IMG_1.jpg", "Beach holiday.jpg", o) && !strcmp(o, "Beach holiday.jpg"), "plain name: %s", o);
    CHECK(clean("IMG_1.jpg", "Beach holiday", o) && !strcmp(o, "Beach holiday.jpg"), "the old extension is kept: %s", o);
    CHECK(clean("IMG_1.jpg", "Beach holiday.png", o) && !strcmp(o + strlen(o) - 4, ".jpg"), "a changed extension goes back: %s", o);
    CHECK(clean("IMG_1.jpg", "Trip: \"Rome\" <1> | what?*.jpg", o) && !strpbrk(o, "\\/:*?\"<>|"), "illegal characters gone: %s", o);
    CHECK(clean("IMG_1.jpg", "..\\..\\evil\\Trip.jpg", o) && !strpbrk(o, "\\/") && strcmp(o, "..") != 0, "a path becomes a leaf: %s", o);
    CHECK(clean("IMG_1.jpg", "C:\\Windows\\win.ini", o) && !strpbrk(o, "\\/:"), "a drive path becomes a leaf: %s", o);
    CHECK(clean("IMG_1.jpg", "CON.jpg", o) && _stricmp(o, "CON.jpg") != 0, "a device name is avoided: %s", o);
    CHECK(clean("IMG_1.jpg", "nul", o) && _stricmp(o, "nul.jpg") != 0, "a device name without extension: %s", o);
    CHECK(clean("IMG_1.jpg", "Holiday . .jpg", o) && o[strlen(o) - 5] != ' ' && o[strlen(o) - 5] != '.', "trailing dots and spaces before the extension: %s", o);
    CHECK(!clean("IMG_1.jpg", "IMG_1.jpg", o), "the same name is no suggestion");
    CHECK(!clean("IMG_1.jpg", "", o), "an empty name is no suggestion");
    CHECK(!clean("IMG_1.jpg", " . ", o) && !clean("IMG_1.jpg", "///", o), "a name of nothing is no suggestion");
    char big[800];
    memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
    CHECK(clean("IMG_1.jpg", big, o) && strlen(o) < 300 && !strcmp(o + strlen(o) - 4, ".jpg"), "an overlong name is cut, extension kept (%zu)", strlen(o));
    CHECK(clean("tmp7731.txt", "گزارش فروش ماه آبان.txt", o) && !strcmp(o, "گزارش فروش ماه آبان.txt"), "a Persian name stays as it is: %s", o);
    CHECK(clean("tmp7731.txt", "گزارش\xE2\x80\xAE فروش.txt", o) && !strstr(o, "\xE2\x80\xAE"), "a right-to-left override is dropped: %s", o);
}

static void machine_made(void) {
    const char *yes[] = { "IMG_4821.jpg", "Screenshot 2026-11-17 093012.png", "a8f3c2e1.pdf", "Untitled", "Untitled.docx", "download (3).pdf", "DSC01234.JPG", "WhatsApp Image 2026-11-17 at 09.30.12.jpeg" };
    const char *no[] = { "report-final.docx", "Lasagna recipe.md", "گزارش فروش.txt", "Invoice Nordlicht 2026-114.pdf", "notes.txt", "Boarding pass Berlin Lisbon.png" };
    for (size_t i = 0; i < sizeof yes / sizeof *yes; i++) CHECK(agent_looks_machine_made(yes[i]), "machine-made: %s", yes[i]);
    for (size_t i = 0; i < sizeof no / sizeof *no; i++) CHECK(!agent_looks_machine_made(no[i]), "a real name: %s", no[i]);
}

static void answers_read(void) {
    agent_pair p[8];
    int n = agent_parse_names("{\"names\":[{\"old\":\"a.jpg\",\"new\":\"Beach.jpg\"},{\"old\":\"b.pdf\",\"new\":\"Invoice.pdf\"}]}", p, 8);
    CHECK(n == 2 && !strcmp(p[0].old_name, "a.jpg") && !strcmp(p[1].new_name, "Invoice.pdf"), "plain JSON (%d)", n);
    n = agent_parse_names("Here you go:\n```json\n{\"names\":[{\"old\":\"a.jpg\",\"new\":\"Beach.jpg\"}]}\n```\nAnything else?", p, 8);
    CHECK(n == 1 && !strcmp(p[0].new_name, "Beach.jpg"), "fence and chatter (%d)", n);
    n = agent_parse_names("{\"names\":[{\"old\":\"a.jpg\",\"new\":\"Beach", p, 8);
    CHECK(n == 0, "an answer cut off in the middle gives nothing (%d)", n);
    n = agent_parse_names("I could not do that.", p, 8);
    CHECK(n == 0, "no JSON at all (%d)", n);
    n = agent_parse_names("{\"names\":[{\"old\":\"a.jpg\",\"new\":\"Caf\\u00e9 \\u06af\\u0632\\u0627\\u0631\\u0634 \\\"x\\\".jpg\"}]}", p, 8);
    CHECK(n == 1 && !strcmp(p[0].new_name, "Café گزارش \"x\".jpg"), "\\u escapes and a quote: %s", n ? p[0].new_name : "");
    n = agent_parse_names("{\"names\":[{\"old\":\"a.jpg\",\"new\":\"x\\uD800\\u", p, 8);
    CHECK(n == 0, "a surrogate escape cut off at the end reads nothing past the text (%d)", n);
    n = agent_parse_names("{\"names\":[{\"old\":\"a.jpg\",\"new\":\"1.jpg\"},{\"old\":\"b.jpg\",\"new\":\"2.jpg\"},{\"old\":\"c.jpg\",\"new\":\"3.jpg\"}]}", p, 2);
    CHECK(n == 2, "never more than the room given (%d)", n);
}

static char sandbox[MAX_PATH];

static void write_text(const char *name, const char *text) {
    char p[MAX_PATH]; FILE *f;
    snprintf(p, sizeof p, "%s\\%s", sandbox, name);
    if (!fopen_s(&f, p, "wb")) { fputs(text, f); fclose(f); }
}

static void prompt_carries_content(void) {
    const char *names[] = { "tmp7731.txt", "IMG_1.jpg" };
    char *pr = malloc(200000);
    agent_build_prompt(sandbox, names, 2, pr, 200000);
    char *t = strstr(pr, "1. tmp7731.txt");
    CHECK(t && strstr(t, "the file starts with: گزارش فروش") != NULL, "a Persian text file's start is in the prompt");
    char *img = strstr(pr, "2. IMG_1.jpg");
    CHECK(img && !strstr(img, "the file starts with"), "a picture carries no snippet");
    CHECK(strstr(pr, "only read") != NULL, "the prompt tells the agent it may only read");
    free(pr);
}

static void run_to_end(agent_job *j, int *state, const char **text) {
    DWORD t = GetTickCount();
    while ((*state = agent_poll(j, text, NULL)) == AGENT_RUNNING && GetTickCount() - t < 60000) Sleep(20);
}

static void runner(void) {
    agent_test_hooks("findstr \"^\"", true);
    agent_detect();
    CHECK(agent_found_count() == 1, "only the stand-in is found (%d)", agent_found_count());
    agent_choice_set(agent_count() - 1, 0);
    int a, m;
    CHECK(agent_choice(&a, &m) && a == agent_count() - 1, "the choice is kept");
    char c[96];
    agent_choice_text(c, sizeof c);
    CHECK(c[0] != 0, "the choice has a text");

    size_t big = 600000;   /* a prompt and an answer much bigger than a pipe: neither side may wait for the other */
    char *prompt = malloc(big + 1);
    for (size_t i = 0; i < big; i++) prompt[i] = (i % 80 == 79) ? '\n' : (char)('a' + i % 26);
    prompt[big] = 0;
    agent_job *j = agent_start(prompt, sandbox, 60000);
    int st; const char *out;
    CHECK(j != NULL, "the job starts");
    if (j) {
        run_to_end(j, &st, &out);
        CHECK(st == AGENT_DONE, "a big prompt and a big answer finish (%d)", st);
        size_t got = 0; for (const char *q = out; *q; q++) if (*q != '\r' && *q != '\n') got++;
        size_t want = 0; for (size_t i = 0; i < big; i++) if (prompt[i] != '\n') want++;
        CHECK(got == want, "the whole answer came back (%zu of %zu)", got, want);
        agent_free(j);
    }
    free(prompt);

    agent_test_hooks("cmd /d /c ping -n 30 127.0.0.1 >nul", true);
    agent_detect();
    j = agent_start("x", sandbox, 400);
    if (j) { run_to_end(j, &st, &out); CHECK(st == AGENT_TIMEOUT, "a slow agent is stopped by the timeout (%d)", st); agent_free(j); }
    j = agent_start("x", sandbox, 60000);
    if (j) {
        Sleep(300);
        DWORD t = GetTickCount();
        agent_cancel(j);
        run_to_end(j, &st, &out);
        CHECK(st == AGENT_CANCELLED && GetTickCount() - t < 5000, "Cancel ends it at once (%d, %lu ms)", st, (unsigned long)(GetTickCount() - t));
        agent_free(j);
    }

    agent_test_hooks("definitely-not-a-program-xyz", true);
    agent_detect();
    j = agent_start("x", sandbox, 20000);
    if (j) { run_to_end(j, &st, &out); CHECK(st == AGENT_FAILED, "a missing program is a failure, not a hang (%d)", st); agent_free(j); }
}

static int lines_in(const char *name) {   /* how many times a stand-in ran: each run adds a line to its own file */
    char p[MAX_PATH], b[256] = ""; FILE *f; int n = 0;
    snprintf(p, sizeof p, "%s\\%s", sandbox, name);
    if (!fopen_s(&f, p, "rb")) { size_t r = fread(b, 1, sizeof b - 1, f); fclose(f); for (size_t i = 0; i < r; i++) n += b[i] == '\n'; }
    return n;
}

/* An older Claude Code does not know --safe-mode and says so. The job then runs once more without the flag, with the hooks
   switched off by the setting instead; any other failure is not run twice. The stand-ins are batch files that act as the CLI. */
static void older_claude(void) {
    char cmd[MAX_PATH * 2], p[MAX_PATH];
    int st; const char *out;
    write_text("old.cmd", "@echo off\necho x>>\"%~dp0old.runs\"\necho %* | findstr /c:\"safe-mode\" >nul && (echo error: unknown option '--safe-mode' 1>&2 & exit /b 1)\n"
                          "echo %* | findstr /c:\"disableAllHooks\" >nul || exit /b 3\necho hooks-off\nfindstr \"^\"\n");
    write_text("bad.cmd", "@echo off\necho x>>\"%~dp0bad.runs\"\necho Please log in first 1>&2\nexit /b 1\n");
    write_text("new.cmd", "@echo off\necho x>>\"%~dp0new.runs\"\necho %* | findstr /c:\"safe-mode\" >nul || exit /b 3\necho accepted\nfindstr \"^\"\n");
    const char *names[] = { "old", "bad", "new" };
    for (int k = 0; k < 3; k++) {
        snprintf(p, sizeof p, "%s\\%s.runs", sandbox, names[k]); DeleteFileA(p);
        snprintf(cmd, sizeof cmd, "cmd /d /s /c \"\"%s\\%s.cmd\" --safe-mode\"", sandbox, names[k]);
        agent_test_hooks(cmd, true);
        agent_detect();
        agent_job *j = agent_start("the prompt", sandbox, 30000);
        CHECK(j != NULL, "the %s stand-in starts", names[k]);
        if (!j) continue;
        run_to_end(j, &st, &out);
        if (k == 0) CHECK(st == AGENT_DONE && strstr(out, "hooks-off") && strstr(out, "the prompt") && !strstr(out, "unknown option") && lines_in("old.runs") == 2,
                          "a refused flag: once more without it, hooks off by the setting, the complaint not in the answer (%d, %d runs)", st, lines_in("old.runs"));
        if (k == 1) CHECK(st == AGENT_FAILED && lines_in("bad.runs") == 1, "another failure is not run twice (%d, %d runs)", st, lines_in("bad.runs"));
        if (k == 2) CHECK(st == AGENT_DONE && strstr(out, "accepted") && lines_in("new.runs") == 1, "a flag that is known: one run (%d, %d runs)", st, lines_in("new.runs"));
        agent_free(j);
    }
}

int main(void) {
    char appdata[MAX_PATH];
    snprintf(sandbox, sizeof sandbox, "%s\\agent", DFM_TEST_WORK);
    CreateDirectoryA(DFM_TEST_WORK, NULL);
    CreateDirectoryA(sandbox, NULL);
    snprintf(appdata, sizeof appdata, "%s\\appdata", sandbox);
    CreateDirectoryA(appdata, NULL);
    SetEnvironmentVariableA("APPDATA", appdata);   /* the choice file never goes to the real settings */
    write_text("tmp7731.txt", "گزارش فروش ماه آبان: فروش ده درصد بیشتر شد.\n");
    names_cleaned();
    machine_made();
    answers_read();
    prompt_carries_content();
    runner();
    older_claude();
    printf(failures ? "%d FAILED\n" : "all agent checks passed\n", failures);
    return failures ? 1 : 0;
}
