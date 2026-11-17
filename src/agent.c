/* The agent base. See agent.h. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "agent.h"

typedef struct agent_def {
    const char *id, *title, *url, *exe;
    int nmodels;
    const char *model[AGENT_MAX_MODELS][2];   /* title, value of the model flag ("" = the agent's own default) */
} agent_def;

static const agent_def DEFS[] = {
    { "claude", "Claude Code", "claude.com/claude-code", "claude", 4, { { "Default", "" }, { "Haiku", "haiku" }, { "Sonnet", "sonnet" }, { "Opus", "opus" } } },
    { "codex", "Codex", "github.com/openai/codex", "codex", 3, { { "Default", "" }, { "GPT-5", "gpt-5" }, { "GPT-5 mini", "gpt-5-mini" } } },
    { "gemini", "Gemini CLI", "github.com/google-gemini/gemini-cli", "gemini", 3, { { "Default", "" }, { "Gemini 2.5 Pro", "gemini-2.5-pro" }, { "Gemini 2.5 Flash", "gemini-2.5-flash" } } },
    { "ollama", "Ollama", "ollama.com", "ollama", 3, { { "Llama 3.2", "llama3.2" }, { "Qwen 2.5", "qwen2.5" }, { "Gemma 3", "gemma3" } } },
};
static const agent_def TEST_DEF = { "test", "Test agent", "", "", 1, { { "Default", "" } } };
enum { REAL = 4 };

static struct {
    bool found[REAL + 1];
    wchar_t exe[REAL + 1][MAX_PATH];
    wchar_t stub[1024];
    bool have_stub, hide_real, loaded;
    int choice_a, choice_m;   /* -1: none */
} S;   /* no initializer: a non-zero one would put the whole struct in the exe (.data) */

static const agent_def *def_of(int a) { return a == REAL ? &TEST_DEF : &DEFS[a]; }

/* ---- text helpers ---- */

static wchar_t *u2w(const char *s, wchar_t *out, int cap) {
    out[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
    out[cap - 1] = 0;
    return out;
}

static bool exists_w(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

/* ---- agents ---- */

int agent_real_count(void) { return REAL; }
int agent_count(void) { return S.have_stub ? REAL + 1 : REAL; }
const char *agent_id(int a) { return def_of(a)->id; }
const char *agent_title(int a) { return def_of(a)->title; }
const char *agent_get_url(int a) { return def_of(a)->url; }
int agent_model_count(int a) { return def_of(a)->nmodels; }
const char *agent_model_title(int a, int m) { return def_of(a)->model[m][0]; }
bool agent_found(int a) { return a >= 0 && a < agent_count() && S.found[a]; }
int agent_found_count(void) { int n = 0; for (int a = 0; a < agent_count(); a++) n += S.found[a]; return n; }

static bool find_in(const wchar_t *dir, const wchar_t *name, wchar_t *out) {
    static const wchar_t *ext[] = { L".exe", L".cmd", L".bat" };
    for (int e = 0; e < 3; e++) {
        _snwprintf(out, MAX_PATH, L"%ls\\%ls%ls", dir, name, ext[e]);
        out[MAX_PATH - 1] = 0;
        if (exists_w(out)) return true;
    }
    return false;
}

static void env_dir(const wchar_t *var, const wchar_t *tail, wchar_t *out) {
    wchar_t b[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(var, b, MAX_PATH);
    out[0] = 0;
    if (n && n < MAX_PATH) _snwprintf(out, MAX_PATH, L"%ls%ls", b, tail);
    out[MAX_PATH - 1] = 0;
}

void agent_detect(void) {
    static const struct { const wchar_t *var, *tail; } extra[] = {
        { L"ProgramFiles", L"\\ClaudeCode" }, { L"USERPROFILE", L"\\.local\\bin" }, { L"USERPROFILE", L"\\.claude\\local" },
        { L"LOCALAPPDATA", L"\\Programs\\OpenAI\\Codex\\bin" }, { L"APPDATA", L"\\npm" }, { L"LOCALAPPDATA", L"\\Programs\\ollama" },
        { L"LOCALAPPDATA", L"\\Programs\\Ollama" }, { L"ProgramFiles", L"\\Ollama" }, { L"USERPROFILE", L"\\AppData\\Roaming\\npm" } };
    for (int a = 0; a < REAL; a++) {
        wchar_t name[32], path[MAX_PATH];
        S.found[a] = false;
        if (S.hide_real) continue;
        u2w(DEFS[a].exe, name, 32);
        static const wchar_t *ext[] = { L".exe", L".cmd", L".bat" };
        for (int e = 0; e < 3 && !S.found[a]; e++)
            if (SearchPathW(NULL, name, ext[e], MAX_PATH, path, NULL)) { S.found[a] = true; wcscpy_s(S.exe[a], MAX_PATH, path); }
        for (size_t k = 0; k < sizeof extra / sizeof *extra && !S.found[a]; k++) {
            wchar_t dir[MAX_PATH];
            env_dir(extra[k].var, extra[k].tail, dir);
            if (dir[0] && find_in(dir, name, path)) { S.found[a] = true; wcscpy_s(S.exe[a], MAX_PATH, path); }
        }
    }
    S.found[REAL] = S.have_stub;
}

static unsigned g_timeout_ms = 180000;
unsigned agent_timeout_ms(void) { return g_timeout_ms; }
void agent_test_timeout(unsigned ms) { if (ms) g_timeout_ms = ms; }

void agent_test_hooks(const char *stub_command, bool hide_real) {
    if (stub_command && stub_command[0]) { u2w(stub_command, S.stub, 1024); S.have_stub = true; }
    S.hide_real = hide_real;
}

/* ---- the choice, kept in agent.txt ---- */

static bool settings_path(wchar_t *out, size_t cap, const wchar_t *name) {
    wchar_t root[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", root, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    _snwprintf(out, cap, L"%ls\\dio-file-manager\\%ls", root, name);
    out[cap - 1] = 0;
    return true;
}

static void load(void) {
    if (S.loaded) return;
    S.loaded = true;
    S.choice_a = S.choice_m = -1;
    wchar_t p[MAX_PATH];
    FILE *f = NULL;
    if (!settings_path(p, MAX_PATH, L"agent.txt") || _wfopen_s(&f, p, L"rb") || !f) return;
    char line[160], id[32] = "", model[64] = "";
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!strncmp(line, "agent=", 6)) snprintf(id, sizeof id, "%s", line + 6);
        else if (!strncmp(line, "model=", 6)) snprintf(model, sizeof model, "%s", line + 6);
    }
    fclose(f);
    for (int a = 0; a < REAL + 1; a++) {
        if (strcmp(def_of(a)->id, id)) continue;
        for (int m = 0; m < def_of(a)->nmodels; m++) if (!strcmp(def_of(a)->model[m][1], model)) { S.choice_a = a; S.choice_m = m; }
        if (S.choice_a < 0) { S.choice_a = a; S.choice_m = 0; }
    }
}

bool agent_choice(int *a, int *m) {
    load();
    if (S.choice_a < 0 || S.choice_a >= agent_count()) return false;
    if (a) *a = S.choice_a;
    if (m) *m = S.choice_m;
    return true;
}

void agent_choice_set(int a, int m) {
    load();
    if (a < 0 || a >= agent_count() || m < 0 || m >= def_of(a)->nmodels) return;
    S.choice_a = a; S.choice_m = m;
    wchar_t p[MAX_PATH], tmp[MAX_PATH], dir[MAX_PATH];
    if (!settings_path(p, MAX_PATH, L"agent.txt") || !settings_path(tmp, MAX_PATH, L"agent.tmp") || !settings_path(dir, MAX_PATH, L"")) return;
    CreateDirectoryW(dir, NULL);
    FILE *f = NULL;
    if (_wfopen_s(&f, tmp, L"wb") || !f) return;
    fprintf(f, "agent=%s\nmodel=%s\n", def_of(a)->id, def_of(a)->model[m][1]);
    fclose(f);
    MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING);
}

void agent_choice_text(char *out, size_t cap) {
    int a, m;
    if (!agent_choice(&a, &m)) { out[0] = 0; return; }
    snprintf(out, cap, "%s \xC2\xB7 %s", def_of(a)->title, def_of(a)->model[m][0]);
}

/* ---- a job: one hidden process ---- */

struct agent_job {
    HANDLE thread, proc, jobobj;
    volatile LONG state, cancel, timeout;
    char *out;
    size_t n, cap;
    unsigned timeout_ms;
    DWORD t0, t1;
    wchar_t *cmd;
    char *prompt;
    wchar_t dir[MAX_PATH];
};

static void quote(wchar_t *dst, size_t cap, const wchar_t *s) {
    size_t n = wcslen(dst), l = wcslen(s), tail = 0;
    while (tail < l && s[l - 1 - tail] == L'\\') tail++;   /* backslashes before the closing quote are doubled (E:\ stays a folder) */
    if (n + l + tail + 3 >= cap) return;
    dst[n++] = L'"';
    memcpy(dst + n, s, l * sizeof *s); n += l;
    for (size_t i = 0; i < tail; i++) dst[n++] = L'\\';
    dst[n++] = L'"'; dst[n] = 0;
}
static void add(wchar_t *dst, size_t cap, const wchar_t *s) {
    size_t n = wcslen(dst);
    if (n + wcslen(s) + 2 < cap) _snwprintf(dst + n, cap - n, L" %ls", s);
}

static wchar_t *build_cmd(int a, int m, const wchar_t *dir) {
    wchar_t *c = calloc(4096, sizeof *c);
    wchar_t model[80];
    if (!c) return NULL;
    u2w(def_of(a)->model[m][1], model, 80);
    if (a == REAL) { wcscpy_s(c, 4096, S.stub); return c; }
    const wchar_t *x = S.exe[a];
    size_t xl = wcslen(x);
    bool script = xl > 4 && (!_wcsicmp(x + xl - 4, L".cmd") || !_wcsicmp(x + xl - 4, L".bat"));
    if (script) wcscpy_s(c, 4096, L"cmd.exe /d /s /c \"");
    quote(c, 4096, x);
    if (a == 0) {   /* claude: print mode, only the Read tool, nothing else loaded */
        add(c, 4096, L"-p --output-format text --tools Read --allowedTools Read --permission-mode dontAsk --no-session-persistence --disable-slash-commands --strict-mcp-config --safe-mode");
        if (model[0]) { add(c, 4096, L"--model"); add(c, 4096, model); }
        add(c, 4096, L"--add-dir"); wcscat_s(c, 4096, L" "); quote(c, 4096, dir);
    } else if (a == 1) {   /* codex: exec in a read-only sandbox, the prompt from stdin */
        add(c, 4096, L"exec --sandbox read-only --skip-git-repo-check --ephemeral --color never --ignore-user-config");
        if (model[0]) { add(c, 4096, L"-m"); add(c, 4096, model); }
        add(c, 4096, L"-C"); wcscat_s(c, 4096, L" "); quote(c, 4096, dir);
        add(c, 4096, L"-");
    } else if (a == 2) {   /* gemini: plan mode reads and does not write */
        add(c, 4096, L"--approval-mode plan -p \" \"");
        if (model[0]) { add(c, 4096, L"-m"); add(c, 4096, model); }
    } else {   /* ollama: a local model, no files; the prompt carries what it needs */
        add(c, 4096, L"run"); add(c, 4096, model[0] ? model : L"llama3.2");
    }
    if (script) wcscat_s(c, 4096, L"\"");
    return c;
}

static HWND g_notify;
void agent_set_notify(void *window) { g_notify = (HWND)window; }

typedef struct writer { HANDLE h; const char *q; } writer;
static DWORD WINAPI feed(LPVOID p) {   /* writes the prompt, then closes the pipe so the agent sees the end of its input */
    writer *w = p;
    size_t left = strlen(w->q);
    const char *q = w->q;
    while (left) {
        DWORD n = 0;
        if (!WriteFile(w->h, q, left > 32768 ? 32768 : (DWORD)left, &n, NULL) || !n) break;
        q += n; left -= n;
    }
    CloseHandle(w->h);
    return 0;
}

static DWORD WINAPI job_main(LPVOID p) {
    agent_job *j = p;
    HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL, nul = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    PROCESS_INFORMATION pi = { 0 };
    LONG end = AGENT_FAILED;
    HANDLE wt = NULL;
    writer wr = { 0 };
    SIZE_T asz = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST al = NULL;
    if (!CreatePipe(&in_r, &in_w, &sa, 0) || !CreatePipe(&out_r, &out_w, &sa, 0)) goto done;
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    HANDLE hs[3] = { in_r, out_w, nul };
    InitializeProcThreadAttributeList(NULL, 1, 0, &asz);
    al = malloc(asz);
    if (!al || !InitializeProcThreadAttributeList(al, 1, 0, &asz) ||
        !UpdateProcThreadAttribute(al, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hs, sizeof hs, NULL, NULL)) goto done;
    STARTUPINFOEXW si = { 0 };
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in_r; si.StartupInfo.hStdOutput = out_w; si.StartupInfo.hStdError = nul;
    si.lpAttributeList = al;
    if (!CreateProcessW(NULL, j->cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                        NULL, j->dir[0] ? j->dir : NULL, &si.StartupInfo, &pi)) goto done;
    j->jobobj = CreateJobObjectW(NULL, NULL);
    if (j->jobobj) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = { 0 };
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(j->jobobj, JobObjectExtendedLimitInformation, &li, sizeof li);
        AssignProcessToJobObject(j->jobobj, pi.hProcess);
    }
    j->proc = pi.hProcess;
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(in_r); in_r = NULL;
    CloseHandle(out_w); out_w = NULL;
    if (j->cancel || j->timeout) goto killed;
    {   /* the prompt goes in on standard input from a thread of its own, so an agent that answers before it has read everything cannot stall both sides */
        wr.h = in_w; wr.q = j->prompt;
        wt = CreateThread(NULL, 0, feed, &wr, 0, NULL);
        if (!wt) feed(&wr);
        in_w = NULL;
        char buf[8192];
        DWORD r;
        while (ReadFile(out_r, buf, sizeof buf, &r, NULL) && r) {
            if (j->n + r + 1 > j->cap) {
                if (j->cap > (4u << 20)) continue;   /* more than 4 MB of answer: the rest is dropped */
                size_t nc = j->cap ? j->cap * 2 : 16384;
                char *nb = realloc(j->out, nc);
                if (!nb) break;
                j->out = nb; j->cap = nc;
            }
            memcpy(j->out + j->n, buf, r);
            j->n += r;
            j->out[j->n] = 0;
        }
    }
killed:
    WaitForSingleObject(j->proc, j->cancel || j->timeout ? 2000 : 8000);
    if (wt) { if (WaitForSingleObject(wt, 3000) == WAIT_TIMEOUT) TerminateThread(wt, 0); CloseHandle(wt); }
    DWORD code = 1;
    GetExitCodeProcess(j->proc, &code);
    if (code == STILL_ACTIVE && j->jobobj) TerminateJobObject(j->jobobj, 1);
    end = j->cancel ? AGENT_CANCELLED : j->timeout ? AGENT_TIMEOUT : (code == 0 && j->n) ? AGENT_DONE : AGENT_FAILED;
done:
    if (al) { DeleteProcThreadAttributeList(al); free(al); }
    if (in_r) CloseHandle(in_r);
    if (in_w) CloseHandle(in_w);
    if (out_r) CloseHandle(out_r);
    if (out_w) CloseHandle(out_w);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    j->t1 = GetTickCount();
    InterlockedExchange(&j->state, end);
    if (g_notify) { InvalidateRect(g_notify, NULL, FALSE); PostMessageW(g_notify, WM_NULL, 0, 0); }   /* the window wakes up to take the answer */
    return 0;
}

agent_job *agent_start(const char *prompt, const char *workdir, unsigned timeout_ms) {
    int a, m;
    if (!agent_choice(&a, &m) || !S.found[a]) return NULL;
    agent_job *j = calloc(1, sizeof *j);
    if (!j) return NULL;
    j->state = AGENT_RUNNING;
    j->timeout_ms = timeout_ms;
    j->prompt = _strdup(prompt);
    if (workdir) u2w(workdir, j->dir, MAX_PATH);
    j->cmd = build_cmd(a, m, j->dir);
    j->t0 = GetTickCount();
    if (!j->prompt || !j->cmd || !(j->thread = CreateThread(NULL, 0, job_main, j, 0, NULL))) { free(j->prompt); free(j->cmd); free(j); return NULL; }
    return j;
}

int agent_poll(agent_job *j, const char **text, double *seconds) {
    LONG s = j->state;
    if (s == AGENT_RUNNING && j->timeout_ms && GetTickCount() - j->t0 > j->timeout_ms && !j->timeout) {
        InterlockedExchange(&j->timeout, 1);
        if (j->jobobj) TerminateJobObject(j->jobobj, 1);
    }
    if (seconds) *seconds = ((s == AGENT_RUNNING ? GetTickCount() : j->t1) - j->t0) / 1000.0;
    if (text) *text = s == AGENT_RUNNING ? "" : (j->out ? j->out : "");
    return (int)s;
}

void agent_cancel(agent_job *j) {
    if (!j) return;
    InterlockedExchange(&j->cancel, 1);
    if (j->jobobj) TerminateJobObject(j->jobobj, 1);
}

void agent_free(agent_job *j) {
    if (!j) return;
    agent_cancel(j);
    if (j->thread) { WaitForSingleObject(j->thread, 10000); CloseHandle(j->thread); }
    if (j->proc) CloseHandle(j->proc);
    if (j->jobobj) CloseHandle(j->jobobj);
    free(j->out); free(j->cmd); free(j->prompt); free(j);
}

/* ---- the answer ---- */

static const char *read_string(const char *p, char *out, size_t cap) {   /* p is at the opening quote; NULL when the string is cut */
    size_t n = 0;
    p++;
    while (*p && *p != '"') {
        unsigned cp = 0;
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'u' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]) && isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
                char h[5] = { p[1], p[2], p[3], p[4], 0 };
                cp = (unsigned)strtoul(h, NULL, 16);
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u' && isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4]) && isxdigit((unsigned char)p[5]) && isxdigit((unsigned char)p[6])) {
                    char l[5] = { p[3], p[4], p[5], p[6], 0 };
                    unsigned lo = (unsigned)strtoul(l, NULL, 16);
                    if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                }
            } else cp = *p == 'n' || *p == 't' || *p == 'r' ? ' ' : (unsigned char)*p;
            p++;
            char u[4];
            int k = cp < 0x80 ? (u[0] = (char)cp, 1) : cp < 0x800 ? (u[0] = (char)(0xC0 | cp >> 6), u[1] = (char)(0x80 | (cp & 63)), 2)
                  : cp < 0x10000 ? (u[0] = (char)(0xE0 | cp >> 12), u[1] = (char)(0x80 | ((cp >> 6) & 63)), u[2] = (char)(0x80 | (cp & 63)), 3)
                  : (u[0] = (char)(0xF0 | cp >> 18), u[1] = (char)(0x80 | ((cp >> 12) & 63)), u[2] = (char)(0x80 | ((cp >> 6) & 63)), u[3] = (char)(0x80 | (cp & 63)), 4);
            if (n + (size_t)k < cap) { memcpy(out + n, u, (size_t)k); n += (size_t)k; }
        } else {
            if (n + 1 < cap) out[n++] = *p;
            p++;
        }
    }
    if (*p != '"') return NULL;
    out[n] = 0;
    return p + 1;
}

static const char *key_value(const char *p, const char *key, char *out, size_t cap, const char *limit) {   /* the string after "key": before limit */
    char pat[16];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *k = strstr(p, pat);
    if (!k || (limit && k >= limit)) return NULL;
    k += strlen(pat);
    while (*k == ' ' || *k == '\t' || *k == '\r' || *k == '\n') k++;
    if (*k != ':') return NULL;
    k++;
    while (*k == ' ' || *k == '\t' || *k == '\r' || *k == '\n') k++;
    if (*k != '"') return NULL;
    return read_string(k, out, cap);
}

int agent_parse_names(const char *text, agent_pair *out, int cap) {
    int n = 0;
    const char *p = text;
    while (n < cap) {
        const char *o = strstr(p, "\"old\"");
        if (!o) break;
        const char *next = strstr(o + 5, "\"old\"");
        char a[300], b[300];
        const char *e = key_value(o, "old", a, sizeof a, NULL);
        if (!e) break;
        if (!key_value(e, "new", b, sizeof b, next)) { p = e; continue; }
        snprintf(out[n].old_name, sizeof out[n].old_name, "%s", a);
        snprintf(out[n].new_name, sizeof out[n].new_name, "%s", b);
        n++;
        p = e;
    }
    return n;
}

/* ---- names ---- */

static bool reserved(const char *s, size_t n) {
    static const char *r[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    for (size_t i = 0; i < sizeof r / sizeof *r; i++) if (strlen(r[i]) == n && !_strnicmp(s, r[i], n)) return true;
    return false;
}

static size_t ext_at(const char *name) {   /* byte index of the extension's dot, or strlen */
    const char *d = strrchr(name, '.');
    return d && d != name && d[1] ? (size_t)(d - name) : strlen(name);
}

bool agent_clean_name(const char *old_name, const char *suggested, char *out, size_t cap) {
    char s[400];
    size_t n = 0;
    const char *leaf = suggested, *c;
    for (c = suggested; *c; c++) if (*c == '\\' || *c == '/') leaf = c + 1;
    for (c = leaf; *c && n + 1 < sizeof s; c++) {
        unsigned char ch = (unsigned char)*c;
        /* U+202A..202E and U+2066..2069 turn the text around: "gnp.exe" would show as "exe.png"; never kept */
        if (ch == 0xE2 && ((c[1] == (char)0x80 && (unsigned char)c[2] >= 0xAA && (unsigned char)c[2] <= 0xAE) || (c[1] == (char)0x81 && (unsigned char)c[2] >= 0xA6 && (unsigned char)c[2] <= 0xA9))) { c += 2; continue; }
        s[n++] = (ch < 32 || strchr("<>:\"|?*", ch)) ? '-' : (char)ch;
    }
    s[n] = 0;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '.')) s[--n] = 0;
    size_t st = 0;
    while (s[st] == ' ') st++;
    memmove(s, s + st, n - st + 1);
    n -= st;
    size_t oe = ext_at(old_name);
    const char *ext = old_name + oe;   /* "" or ".jpg" */
    size_t el = strlen(ext);
    if (el && n >= el && !_stricmp(s + n - el, ext)) { n -= el; s[n] = 0; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '.')) s[--n] = 0;
    if (n > 120) { n = 120; while (n && ((unsigned char)s[n] & 0xC0) == 0x80) n--; s[n] = 0; }
    if (!n) return false;
    if (reserved(s, n)) { memmove(s + 1, s, n + 1); s[0] = '_'; n++; }
    if (n + el + 1 > cap) return false;
    snprintf(out, cap, "%s%s", s, ext);
    return _stricmp(out, old_name) != 0;
}

/* The stem without a trailing " (3)", "-2", " 2" or "_2". */
static size_t strip_count(const char *s, size_t n) {
    size_t e = n;
    if (e && s[e - 1] == ')') { size_t i = e - 1; while (i && isdigit((unsigned char)s[i - 1])) i--; if (i < e - 1 && i >= 2 && s[i - 1] == '(' ) { e = i - 1; while (e && s[e - 1] == ' ') e--; return e; } }
    size_t i = e;
    while (i && isdigit((unsigned char)s[i - 1])) i--;
    if (i < e && i > 0 && (s[i - 1] == ' ' || s[i - 1] == '-' || s[i - 1] == '_')) return i - 1;
    return e;
}

bool agent_looks_machine_made(const char *name) {
    size_t n = ext_at(name);
    char s[300];
    if (!n || n >= sizeof s) return false;
    memcpy(s, name, n); s[n] = 0;
    int hex = 0, digits = 0, letters = 0, other = 0, flips = 0, prev = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        int cls = isdigit(c) ? 1 : isalpha(c) ? 2 : 0;
        if (isxdigit(c)) hex++;
        if (cls == 1) digits++; else if (cls == 2) letters++; else other++;
        if (prev && cls && cls != prev) flips++;
        if (cls) prev = cls;
        if (c >= 0x80) return false;   /* a name in a real language is somebody's */
    }
    if (n >= 8 && hex + (int)other >= (int)n && hex >= 8 && digits >= 1) return true;                  /* a8f3c2e1, a GUID */
    if (digits == (int)n && n >= 6) return true;                                                          /* 20261117093012 */
    static const char *pre[] = { "IMG", "DSC", "DSCN", "DSCF", "PXL", "VID", "MVI", "MOV", "IMAGE", "PHOTO", "SCAN", "SCREENSHOT", "SCREEN SHOT",
                                 "CAPTURE", "WHATSAPP IMAGE", "WHATSAPP VIDEO", "SIGNAL", "RECORDING", "AUDIO", "P" };
    for (size_t i = 0; i < sizeof pre / sizeof *pre; i++) {
        size_t pl = strlen(pre[i]);
        if (n > pl && !_strnicmp(s, pre[i], pl) && !isalpha((unsigned char)s[pl])) {
            int d = 0, l = 0;
            for (size_t k = pl; k < n; k++) { if (isdigit((unsigned char)s[k])) d++; else if (isalpha((unsigned char)s[k])) l++; }
            if (d >= 3 && l <= 3) return true;
        }
    }
    size_t e = strip_count(s, n);
    static const char *gen[] = { "untitled", "new document", "new text document", "download", "document", "file", "image", "photo", "scan", "unnamed", "new file", "noname" };
    for (size_t i = 0; i < sizeof gen / sizeof *gen; i++) if (strlen(gen[i]) == e && !_strnicmp(s, gen[i], e)) return true;
    if (n >= 10 && other == 0 && digits >= 3 && letters >= 3 && flips >= 4) return true;                  /* Qm9kx2Lp7Z */
    return false;
}

/* ---- the prompt ---- */

static bool text_ext(const char *name) {
    static const char *e[] = { ".txt", ".md", ".csv", ".json", ".log", ".xml", ".html", ".htm", ".srt", ".ini", ".yaml", ".yml", ".tex", ".rst", ".py", ".js", ".c", ".h" };
    size_t d = ext_at(name);
    for (size_t i = 0; i < sizeof e / sizeof *e; i++) if (!_stricmp(name + d, e[i])) return true;
    return false;
}

static size_t snippet(const char *dir, const char *name, char *out, size_t cap) {
    wchar_t w[MAX_PATH * 2], nm[400];
    char buf[1600];
    wchar_t d[MAX_PATH];
    u2w(dir, d, MAX_PATH); u2w(name, nm, 400);
    _snwprintf(w, MAX_PATH * 2, L"\\\\?\\%ls\\%ls", d, nm);
    w[MAX_PATH * 2 - 1] = 0;
    HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD r = 0;
    BOOL ok = ReadFile(h, buf, 1500, &r, NULL);
    CloseHandle(h);
    if (!ok || !r) return 0;
    size_t s = 0, n = r;
    if (n >= 3 && !memcmp(buf, "\xEF\xBB\xBF", 3)) s = 3;
    for (size_t i = s; i < n; i++) if (!buf[i]) return 0;   /* not plain text */
    while (n > s && ((unsigned char)buf[n - 1] & 0xC0) == 0x80) n--;   /* a cut character goes */
    if (n > s) n--;
    size_t o = 0;
    for (size_t i = s; i < n && o + 2 < cap; i++) out[o++] = (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') ? ' ' : buf[i];
    out[o] = 0;
    return o;
}

size_t agent_build_prompt(const char *dir, const char *const *names, int n, char *out, size_t cap) {
    size_t o = (size_t)snprintf(out, cap,
        "Rename suggestions for files in this folder: %s\n"
        "You may only read. Do not change, create or delete anything. Open the listed files that you need to see (pictures, PDFs, documents) to learn what each one is.\n"
        "For each file give a short, clear name that says what the file is about. Write the name in the language of the file's own content "
        "(a Persian text gets a Persian name); when the language is unclear, use English. Keep each file's extension. "
        "At most 60 characters before the extension. Never use these characters: \\ / : * ? \" < > |\n"
        "Reply with JSON only, no other text: {\"names\":[{\"old\":\"<the exact current name>\",\"new\":\"<the new name with its extension>\"}]}\n"
        "Files:\n", dir);
    for (int i = 0; i < n && o + 2200 < cap; i++) {
        o += (size_t)snprintf(out + o, cap - o, "%d. %s\n", i + 1, names[i]);
        if (text_ext(names[i])) {
            char sn[1700];
            if (snippet(dir, names[i], sn, sizeof sn)) o += (size_t)snprintf(out + o, cap - o, "   the file starts with: %s\n", sn);
        }
    }
    return o;
}
