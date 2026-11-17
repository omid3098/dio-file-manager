/* Ghost names: an agent suggests better names for files with meaningless ones; the suggestion shows greyed out under each
   name, Tab accepts all (one Undo step), Esc drops them. Also the agent picker (agent and model together, chosen once) and
   the grey suggestion in the F2 rename box. The agent only reads (agent.c); every rename is the app's own rename job (ops). */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "agent.h"
#include "ops.h"

enum { GH_MAX = 50 };

typedef struct stamp { ULONGLONG written, size, id; DWORD volume; } stamp;   /* which file it was: the same file keeps its file index, time and size */

typedef struct batch {
    char dir[FS_PATH_MAX];
    int n;
    stamp st[GH_MAX];
    char from[GH_MAX][300], to[GH_MAX][300];
    bool ok[GH_MAX];
    bool undo;
} batch;

static struct {
    bool detected, picking, want_pick, after_pick;
    int pick_frames;
    agent_job *job, *f2job;
    int asked;                       /* files the running batch asked about */
    char asked_names[GH_MAX][300];
    stamp asked_st[GH_MAX], g_st[GH_MAX];   /* of the asked files, and of the suggestions' files */
    bool undo_taken;                 /* Ctrl+Z was taken this frame: the Trash undo stays out */
    char dir[FS_PATH_MAX];           /* the folder the run and the suggestions belong to */
    int n;
    agent_pair g[GH_MAX];            /* old_name -> new_name */
    char f2_path[FS_PATH_MAX], f2_name[300], f2_dir[FS_PATH_MAX], f2_sug[300];
    bool f2_ready;
    char click[300];                 /* a click on a grey suggestion, taken by the next frame */
    /* accepting and undoing: one worker, one batch */
    HANDLE worker;
    volatile LONG wstate;            /* 0 idle, 1 running, 2 done */
    batch *wb, *last;                /* last: what the Undo toast would put back */
    int undo_toast;
    double t_run;
    int runs;                        /* test state */
    const char *last_result;         /* test state: "ok", "broken", "timeout", "cancelled", "failed", "none" */
    cui_rect r_chip, r_accept, r_drop, r_cancel, r_bar, r_pick[16];
    int pick_n;
    int pop_open;
    bool tab_ok, f2_vis, f2_vis_next;   /* what a Tab press may take, as of the last frame */
    int tab_hit, tab_age;               /* a Tab the window procedure took: 1 accept all, 2 the F2 suggestion */
} G;   /* all zero, so it stays out of the exe file; undo_toast is the toast id + 1 (0: none), last_result NULL reads as "" */

static void toast_info(const char *title, const char *desc) { cui_toast(A.ui, title, .description = desc, .duration = 6); }
static void toast_bad(const char *title, const char *desc) { cui_toast(A.ui, title, .description = desc, .tone = CUI_TONE_DANGER, .duration = 8); }

bool ui_ghost_active(void) { return G.n > 0; }   /* grey names are on screen: the cells are a line taller */

const char *ui_ghost_for(const char *name) {
    if (!G.n || strcmp(G.dir, A.path)) return NULL;
    for (int i = 0; i < G.n; i++) if (!_stricmp(G.g[i].old_name, name)) return G.g[i].new_name;
    return NULL;
}

bool ui_ghost_bar_at(float x, float y) {   /* the bar floats over the file view: a press on it is the bar's, never a selection box */
    return G.r_bar.w > 0 && x >= G.r_bar.x && x < G.r_bar.x + G.r_bar.w && y >= G.r_bar.y && y < G.r_bar.y + G.r_bar.h;
}

bool ui_ghost_hit(const char *name, cui_rect r) {   /* a paint routine tells where it drew a grey name */
    cui_vec2 m = cui_mouse_pos(A.ui);
    if (cui_mouse_clicked(A.ui, CUI_MOUSE_LEFT) && m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h) {
        snprintf(G.click, sizeof G.click, "%s", name);
        return true;
    }
    return false;
}

static void clear_ghosts(void) { G.n = 0; G.dir[0] = 0; }

static void stop_run(void) {
    if (G.job) { agent_free(G.job); G.job = NULL; }
}

/* ---- names ---- */

static bool stamp_of(const char *dir, const char *name, stamp *out) {
    wchar_t w[FS_WPATH_MAX + 320];
    char p[FS_PATH_MAX + 320];
    BY_HANDLE_FILE_INFORMATION d;
    snprintf(p, sizeof p, "%s\\%s", dir, name);
    if (!fs_u8_to_w_long(p, w, (int)(sizeof w / sizeof *w))) return false;
    HANDLE h = CreateFileW(w, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    BOOL ok = GetFileInformationByHandle(h, &d);
    CloseHandle(h);
    if (!ok) return false;
    out->written = ((ULONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    out->size = ((ULONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    out->id = ((ULONGLONG)d.nFileIndexHigh << 32) | d.nFileIndexLow;
    out->volume = d.dwVolumeSerialNumber;
    return true;
}

static bool wide_exists(const char *dir, const char *name) {
    wchar_t w[FS_PATH_MAX + 320];
    char p[FS_PATH_MAX + 320];
    snprintf(p, sizeof p, "%s\\%s", dir, name);
    if (!MultiByteToWideChar(CP_UTF8, 0, p, -1, w, (int)(sizeof w / sizeof *w))) return true;   /* cannot tell: treat as taken */
    return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}

/* The name never overwrites: a number goes before the extension while the name is taken (on disk or by another suggestion). */
static void make_unique(const char *dir, const char *old_name, char *name, size_t cap, int upto) {
    char stem[300], ext[64];
    const char *d = strrchr(name, '.');
    size_t sl = d && d != name ? (size_t)(d - name) : strlen(name);
    snprintf(stem, sizeof stem, "%.*s", (int)sl, name);
    snprintf(ext, sizeof ext, "%s", name + sl);
    for (int k = 1; k < 1000; k++) {
        bool taken = _stricmp(name, old_name) && wide_exists(dir, name);
        for (int i = 0; i < upto && !taken; i++) if (!_stricmp(G.g[i].new_name, name)) taken = true;
        if (!taken) return;
        snprintf(name, cap, "%s %d%s", stem, k + 1, ext);
    }
}

/* ---- starting a run ---- */

static bool start_run(const char *dir, const char *const *names, int n, bool single) {
    char *prompt = malloc(200000);
    if (!prompt) return false;
    agent_build_prompt(dir, names, n, prompt, 200000);
    agent_job *j = agent_start(prompt, dir, agent_timeout_ms());
    free(prompt);
    if (!j) { toast_bad("Could not start the agent", "Choose another one from the menu."); return false; }
    G.runs++;
    if (single) { if (G.f2job) agent_free(G.f2job); G.f2job = j; }
    else { stop_run(); G.job = j; G.t_run = 0; }
    return true;
}

void ui_ghost_suggest(void) {
    if (A.loc != LOC_FOLDER || A.searching || !A.list || fs_list_state(A.list) != FS_DONE || A.sel_n < 1) return;
    int a, m, files = 0;
    if (!G.detected) { agent_detect(); G.detected = true; }
    if (!agent_choice(&a, &m) || !agent_found(a)) {   /* the first use: the picker opens, and the run starts after the choice */
        agent_detect();
        G.picking = G.want_pick = G.after_pick = true;
        return;
    }
    for (int i = 0; i < A.vis_n; i++) if (A.sel[i] && !(app_item(i)->flags & (FS_DIR | FS_INCOMING))) files++;
    if (!files) { toast_info("Select files, not folders", "Names are suggested for files."); return; }
    const char *names[GH_MAX];
    int n = 0;
    for (int i = 0; i < A.vis_n && n < GH_MAX; i++) if (A.sel[i] && !(app_item(i)->flags & (FS_DIR | FS_INCOMING))) {
        snprintf(G.asked_names[n], sizeof G.asked_names[n], "%s", app_item_name(i));
        if (!stamp_of(A.path, G.asked_names[n], &G.asked_st[n])) continue;   /* gone already: nothing to ask about */
        names[n] = G.asked_names[n];
        n++;
    }
    if (!n) return;
    if (files > GH_MAX) { char d[96]; snprintf(d, sizeof d, "Only the first %d of %d files were sent.", GH_MAX, files); toast_info("One run takes 50 files", d); }
    clear_ghosts();
    G.asked = n;
    snprintf(G.dir, sizeof G.dir, "%s", A.path);
    if (!start_run(A.path, names, n, false)) G.dir[0] = 0;
}

void ui_ghost_menu_label(char *out, size_t cap) {
    int a;
    if (agent_choice(&a, NULL)) snprintf(out, cap, "Suggest names \xC2\xB7 %s", agent_title(a));
    else snprintf(out, cap, "Suggest names\xE2\x80\xA6");
}

/* ---- F2 ---- */

void ui_ghost_f2(const char *path, const char *name) {
    int a;
    G.f2_ready = false; G.f2_sug[0] = 0;
    snprintf(G.f2_path, sizeof G.f2_path, "%s", path);
    snprintf(G.f2_name, sizeof G.f2_name, "%s", name);
    if (G.f2job) { agent_free(G.f2job); G.f2job = NULL; }
    const char *g = ui_ghost_for(name);
    if (g) { snprintf(G.f2_sug, sizeof G.f2_sug, "%s", g); G.f2_ready = true; return; }   /* a suggestion is already there */
    if (!agent_choice(&a, NULL) || !agent_looks_machine_made(name)) return;     /* F2 never opens the picker */
    if (!G.detected) { agent_detect(); G.detected = true; }
    if (!agent_found(a)) return;
    snprintf(G.f2_dir, sizeof G.f2_dir, "%s", A.path);
    const char *one[1] = { G.f2_name };
    start_run(A.path, one, 1, true);
}

const char *ui_ghost_f2_text(const char *path, bool *busy) {
    *busy = G.f2job && !strcmp(path, G.f2_path);
    if (!G.f2_ready || strcmp(path, G.f2_path)) return NULL;
    G.f2_vis_next = true;
    return G.f2_sug;
}

/* CUI turns every Tab into a focus move before the app reads keys, so the window procedure asks here first. */
bool ui_ghost_grab_tab(void) {
    if ((GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU) | GetKeyState(VK_SHIFT)) & 0x8000) return false;
    int hit = G.f2_vis ? 2 : G.tab_ok ? 1 : 0;
    if (!hit) return false;
    G.tab_hit = hit; G.tab_age = 0;
    cui_request_redraw(A.ui);
    return true;
}

bool ui_ghost_f2_tab(void) {   /* the F2 box takes the suggestion once per Tab */
    if (G.tab_hit != 2) return false;
    G.tab_hit = 0;
    return true;
}

/* ---- accept and undo: the app's own rename job, one name after the other on one worker ---- */

static DWORD WINAPI worker_main(LPVOID p) {
    batch *b = p;
    ops_state *st = malloc(sizeof *st);   /* large: heap */
    for (int i = 0; i < b->n && st; i++) {
        char src[FS_PATH_MAX + 320];
        snprintf(src, sizeof src, "%s\\%s", b->dir, b->from[i]);
        const char *sp = src;
        stamp now;
        if (!stamp_of(b->dir, b->from[i], &now) || now.written != b->st[i].written || now.size != b->st[i].size || now.id != b->st[i].id || now.volume != b->st[i].volume) continue;   /* another file stands there now */
        ops_job *j = ops_start(OPS_RENAME, &sp, 1, NULL, b->to[i]);
        if (!j) continue;
        for (;;) { ops_poll(j, st); if (st->finished) break; Sleep(4); }
        b->ok[i] = st->errors == 0;
        ops_free(j);
    }
    free(st);
    InterlockedExchange(&G.wstate, 2);
    return 0;
}

static void start_batch(batch *b) {
    if (G.wstate != 0) { free(b); return; }
    G.wb = b;
    InterlockedExchange(&G.wstate, 1);
    G.worker = CreateThread(NULL, 0, worker_main, b, 0, NULL);
    if (!G.worker) { InterlockedExchange(&G.wstate, 0); free(b); G.wb = NULL; }
}

static void accept_all(void) {
    if (!G.n || G.wstate != 0 || G.job) return;
    batch *b = calloc(1, sizeof *b);
    if (!b) return;
    snprintf(b->dir, sizeof b->dir, "%s", G.dir);
    for (int i = 0; i < G.n; i++) {
        snprintf(b->from[b->n], sizeof b->from[0], "%s", G.g[i].old_name);
        snprintf(b->to[b->n], sizeof b->to[0], "%s", G.g[i].new_name);
        b->st[b->n] = G.g_st[i];
        b->n++;
    }
    clear_ghosts();
    start_batch(b);
}

static void start_undo(void) {
    if (!G.last || G.wstate != 0) return;
    batch *u = calloc(1, sizeof *u);
    if (!u) return;
    snprintf(u->dir, sizeof u->dir, "%s", G.last->dir);
    for (int i = 0; i < G.last->n; i++) if (G.last->ok[i]) {   /* only what was renamed goes back, last first */
        int k = u->n++;
        snprintf(u->from[k], sizeof u->from[0], "%s", G.last->to[i]);
        snprintf(u->to[k], sizeof u->to[0], "%s", G.last->from[i]);
        u->st[k] = G.last->st[i];   /* a rename keeps the time and the size */
    }
    u->undo = true;
    free(G.last); G.last = NULL;
    if (u->n) start_batch(u); else free(u);
}

static void worker_poll(void) {
    if (G.wstate == 1) { cui_request_wakeup(A.ui, 0.05f); return; }
    if (G.wstate != 2) return;
    WaitForSingleObject(G.worker, INFINITE);
    CloseHandle(G.worker); G.worker = NULL;
    batch *b = G.wb; G.wb = NULL;
    int ok = 0;
    for (int i = 0; i < b->n; i++) ok += b->ok[i];
    InterlockedExchange(&G.wstate, 0);
    if (ok < b->n) {
        char d[120]; snprintf(d, sizeof d, "%d of %d could not be renamed (the name is taken, the file is in use or it was changed).", b->n - ok, b->n);
        toast_bad(b->undo ? "Could not undo every rename" : "Some renames failed", d);
    }
    if (b->undo) {
        if (ok) toast_info(ok == 1 ? "1 rename undone" : "Renames undone", "");
        if (ok < b->n) {   /* what is still under its new name can be undone again */
            batch *l = calloc(1, sizeof *l);
            if (l) {
                snprintf(l->dir, sizeof l->dir, "%s", b->dir);
                for (int i = 0; i < b->n; i++) if (!b->ok[i]) {
                    int k = l->n++;
                    snprintf(l->from[k], sizeof l->from[0], "%s", b->to[i]);
                    snprintf(l->to[k], sizeof l->to[0], "%s", b->from[i]);
                    l->st[k] = b->st[i]; l->ok[k] = true;
                }
                if (G.undo_toast) cui_toast_dismiss(A.ui, G.undo_toast - 1);
                G.undo_toast = cui_toast(A.ui, "Some renames are still not undone", .action = "Try again", .show_timer = true, .duration = 12) + 1;
                free(G.last); G.last = l;
            }
        }
        free(b);
    }
    else if (ok) {
        char t[96];
        snprintf(t, sizeof t, ok == 1 ? "%d file renamed" : "%d files renamed", ok);
        if (G.undo_toast) cui_toast_dismiss(A.ui, G.undo_toast - 1);
        G.undo_toast = cui_toast(A.ui, t, .action = "Undo", .show_timer = true, .duration = 12) + 1;
        free(G.last); G.last = b;
    } else free(b);
    if (A.loc == LOC_FOLDER && !A.searching) app_reload_quiet();
}

/* ---- keys ---- */

bool ui_ghost_took_undo(void) { return G.undo_taken; }

void ui_ghost_keys(void) {
    cui_ctx *ui = A.ui;
    G.undo_taken = false;
    if (cui_shortcut(ui, CUI_MOD_CTRL | CUI_MOD_SHIFT, CUI_KEY_R) && !cui_wants_keyboard(ui)) ui_ghost_suggest();
    if (ui_popup_open_any(ui) || cui_wants_keyboard(ui)) return;
    if ((G.n || G.job) && cui_key_pressed(ui, CUI_KEY_ESCAPE)) { stop_run(); clear_ghosts(); G.last_result = "dropped"; }
    if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_Z) && G.undo_toast) { cui_toast_dismiss(ui, G.undo_toast - 1); G.undo_toast = 0; G.undo_taken = true; start_undo(); }
}

/* ---- results ---- */

static void take_batch_result(int st, const char *text) {
    if (st == AGENT_CANCELLED) { G.last_result = "cancelled"; clear_ghosts(); return; }
    if (st == AGENT_TIMEOUT) { G.last_result = "timeout"; clear_ghosts(); toast_bad("The agent took too long", "It was stopped after 3 minutes. Try fewer files."); return; }
    if (st == AGENT_FAILED) { G.last_result = "failed"; clear_ghosts(); toast_bad("The agent did not answer", "Check that it is signed in, or choose another one."); return; }
    agent_pair *p = malloc(sizeof *p * GH_MAX);
    int np = p ? agent_parse_names(text, p, GH_MAX) : 0;
    if (!np) { free(p); G.last_result = "broken"; clear_ghosts(); toast_bad("The answer could not be read", "The agent did not send names in the expected form. Nothing was changed."); return; }
    G.n = 0;
    for (int k = 0; k < np && G.n < GH_MAX; k++) {
        const char *asked = NULL;
        for (int i = 0; i < G.asked; i++) if (!_stricmp(G.asked_names[i], p[k].old_name)) { asked = G.asked_names[i]; break; }
        char nn[300];
        if (!asked || !agent_clean_name(asked, p[k].new_name, nn, sizeof nn) || !strcmp(nn, asked)) continue;   /* a name the agent left as it was is no suggestion */
        bool dup = false;
        for (int i = 0; i < G.n; i++) if (!_stricmp(G.g[i].old_name, asked)) dup = true;
        if (dup) continue;
        make_unique(G.dir, asked, nn, sizeof nn, G.n);
        snprintf(G.g[G.n].old_name, sizeof G.g[G.n].old_name, "%s", asked);
        snprintf(G.g[G.n].new_name, sizeof G.g[G.n].new_name, "%s", nn);
        for (int i = 0; i < G.asked; i++) if (asked == G.asked_names[i]) G.g_st[G.n] = G.asked_st[i];
        G.n++;
    }
    free(p);
    G.last_result = G.n ? "ok" : "none";
    if (!G.n) { clear_ghosts(); toast_info("No better names", "The agent found nothing to change."); }
    cui_request_redraw(A.ui);
}

static void poll_jobs(void) {
    if (G.job) {
        const char *text; double sec;
        int st = agent_poll(G.job, &text, &sec);
        G.t_run = sec;
        if (st == AGENT_RUNNING) cui_request_wakeup(A.ui, 5.0f);
        else {
            if (strcmp(G.dir, A.path)) { clear_ghosts(); G.last_result = "left"; }   /* the folder was left: the answer is of no use */
            else take_batch_result(st, text);
            agent_free(G.job); G.job = NULL;
        }
    }
    if (G.f2job) {
        const char *text; double sec;
        int st = agent_poll(G.f2job, &text, &sec);
        if (st == AGENT_RUNNING) cui_request_wakeup(A.ui, 5.0f);
        else {
            agent_pair p[2];
            char nn[300];
            if (st == AGENT_DONE && agent_parse_names(text, p, 2) > 0 && !_stricmp(p[0].old_name, G.f2_name) &&
                agent_clean_name(G.f2_name, p[0].new_name, nn, sizeof nn) && !strcmp(G.f2_dir, A.path)) {
                make_unique(G.f2_dir, G.f2_name, nn, sizeof nn, 0);
                snprintf(G.f2_sug, sizeof G.f2_sug, "%s", nn);
                G.f2_ready = true;
            }
            agent_free(G.f2job); G.f2job = NULL;
            cui_request_redraw(A.ui);
        }
    }
}

/* ---- the bar, the picker ---- */

static void picker(void) {
    cui_ctx *ui = A.ui;
    int ca = -1, cm = -1;
    agent_choice(&ca, &cm);
    if (cui_popover_begin(ui, "agentpick", &(cui_popover_opts){ .placement = CUI_PLACE_TOP, .width = 260, .pad = 10, .gap = 8 })) {
        G.pop_open = 1; G.pick_n = 0;
        cui_column(ui, .width = CUI_FILL, .gap = 2) {
            cui_text(ui, "Agent and model", .font = CUI_FONT_MEDIUM, .size = 15);
            if (!agent_found_count()) {
                cui_text(ui, "No agent was found on this computer. Install one of these and open this menu again:", .color = CUI_COLOR_TEXT_MUTED);
                for (int a = 0; a < agent_real_count(); a++) { char l[160]; snprintf(l, sizeof l, "%s: %s", agent_title(a), agent_get_url(a)); cui_text(ui, l, .color = CUI_COLOR_TEXT_MUTED); }
            }
            for (int a = 0; a < agent_count(); a++) {
                if (!agent_found(a)) continue;
                for (int m = 0; m < agent_model_count(a); m++) {
                    char l[96];
                    snprintf(l, sizeof l, "%s \xC2\xB7 %s", agent_title(a), agent_model_title(a, m));
                    bool hit = cui_button(ui, l, .size = CUI_SIZE_SM, .variant = (a == ca && m == cm) ? CUI_BUTTON_SECONDARY : CUI_BUTTON_GHOST);
                    if (G.pick_n < 16) G.r_pick[G.pick_n++] = cui_last_rect(ui);
                    if (hit) {
                        agent_choice_set(a, m);
                        cui_popover_close(ui, "agentpick");
                        if (G.after_pick) { G.after_pick = false; G.picking = false; ui_ghost_suggest(); }
                        else G.picking = false;
                    }
                }
            }
            for (int a = 0; a < agent_count(); a++) if (!agent_found(a) && a < agent_real_count() && agent_found_count()) {
                char l[160]; snprintf(l, sizeof l, "%s is not installed: %s", agent_title(a), agent_get_url(a));
                cui_text(ui, l, .color = CUI_COLOR_TEXT_MUTED, .size = 12);
            }
        }
        cui_popover_end(ui);
        G.pick_frames++;
        cui_request_wakeup(ui, 0.0f);
    } else G.pop_open = 0;
    if (!cui_popover_is_open(ui, "agentpick") && G.pick_frames > 1) { G.picking = false; G.after_pick = false; G.pick_frames = 0; }
}

void ui_ghost_frame(void) {
    cui_ctx *ui = A.ui;
    if (strcmp(G.dir, A.path) && (G.n || G.job)) { stop_run(); clear_ghosts(); }   /* another folder: the suggestions are of the old one */
    if ((A.loc != LOC_FOLDER || A.searching) && (G.n || G.job)) { stop_run(); clear_ghosts(); }   /* search results come from many folders: names would be matched by name only */
    agent_set_notify(A.hwnd);
    poll_jobs();
    worker_poll();
    G.tab_ok = G.n && !G.job && !ui_popup_open_any(ui) && !cui_wants_keyboard(ui);   /* what a Tab press may take (see ui_ghost_grab_tab) */
    if (G.tab_hit == 1 && G.tab_ok) { G.tab_hit = 0; accept_all(); }
    if (G.tab_hit && ++G.tab_age > 2) G.tab_hit = 0;
    G.f2_vis = G.f2_vis_next; G.f2_vis_next = false;
    if (G.undo_toast && cui_toast_action_clicked(ui, G.undo_toast - 1)) { G.undo_toast = 0; start_undo(); }
    if (G.click[0]) {
        char nm[300] = "", sg[300];
        for (int i = 0; i < G.n; i++) if (!strcmp(G.g[i].new_name, G.click)) { snprintf(nm, sizeof nm, "%s", G.g[i].old_name); snprintf(sg, sizeof sg, "%s", G.g[i].new_name); }
        G.click[0] = 0;
        if (nm[0]) act_rename_edit(nm, sg);
    }
    memset(&G.r_bar, 0, sizeof G.r_bar); memset(&G.r_chip, 0, sizeof G.r_chip);
    memset(&G.r_accept, 0, sizeof G.r_accept); memset(&G.r_drop, 0, sizeof G.r_drop); memset(&G.r_cancel, 0, sizeof G.r_cancel);
    if (!G.picking) G.pop_open = 0;
    bool running = G.job != NULL, show = G.picking || running || G.n > 0;
    if (!show) return;
    char chip[96], status[96];
    agent_choice_text(chip, sizeof chip);
    if (!chip[0]) snprintf(chip, sizeof chip, "Choose an agent");
    if (G.picking && !running && !G.n) snprintf(status, sizeof status, "Which agent should suggest names?");
    else if (running) {   /* the text never changes width, so Cancel stays under the pointer */
        if (G.asked == 1) snprintf(status, sizeof status, "Reading 1 file\xE2\x80\xA6");
        else snprintf(status, sizeof status, "Reading %d files\xE2\x80\xA6", G.asked);
    } else snprintf(status, sizeof status, G.n == 1 ? "1 name suggested" : "%d names suggested", G.n);
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_START, .justify = CUI_JUSTIFY_END, .pad = 16) {
        cui_row(ui, .id = "ghostbar", .bg = CUI_COLOR_SURFACE_RAISED, .radius = 14, .border = 1, .border_color = CUI_COLOR_BORDER,
                .elevation = CUI_ELEVATION_FLOATING, .pad_x = 12, .pad_y = 7, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_SPARKLES, 16, cui_color_of(ui, CUI_COLOR_ACCENT));
            cui_text(ui, status, .nowrap = true);
            if (cui_button(ui, chip, .size = CUI_SIZE_SM, .variant = CUI_BUTTON_GHOST, .icon_right = CUI_ICON_CHEVRON_DOWN)) { G.picking = true; G.want_pick = true; agent_detect(); }
            G.r_chip = cui_last_rect(ui);
            if (G.want_pick) { G.want_pick = false; G.pick_frames = 0; cui_popover_open(ui, "agentpick"); }
            if (G.picking) picker();
            if (running) {
                if (cui_button(ui, "Cancel", .size = CUI_SIZE_SM)) { agent_cancel(G.job); }
                G.r_cancel = cui_last_rect(ui);
            } else if (G.n) {
                if (cui_button(ui, "Accept all", .size = CUI_SIZE_SM, .variant = CUI_BUTTON_PRIMARY)) accept_all();
                G.r_accept = cui_last_rect(ui);
                cui_text(ui, "Tab", .color = CUI_COLOR_TEXT_MUTED, .size = 12);
                if (cui_button(ui, "Drop", .size = CUI_SIZE_SM)) { clear_ghosts(); G.last_result = "dropped"; }
                G.r_drop = cui_last_rect(ui);
                cui_text(ui, "Esc", .color = CUI_COLOR_TEXT_MUTED, .size = 12);
            }
        }
        G.r_bar = cui_get_rect(ui, cui_make_id(ui, "ghostbar"));   /* the row, not its last button */
    }
}

/* ---- test state ---- */

static void jt(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) { if (*s == '"' || *s == '\\') fputc('\\', f); if ((unsigned char)*s >= 32) fputc(*s, f); }
    fputc('"', f);
}

void ui_ghost_snapshot(FILE *f) {
    char c[96];
    agent_choice_text(c, sizeof c);
    fprintf(f, ",\"ghost\":{\"choice\":"); jt(f, c);
    fprintf(f, ",\"found\":[");
    int k = 0;
    for (int a = 0; a < agent_count(); a++) if (agent_found(a)) { fprintf(f, "%s", k++ ? "," : ""); jt(f, agent_id(a)); }
    fprintf(f, "],\"running\":%d,\"picking\":%d,\"pop\":%d,\"runs\":%d,\"result\":", G.job != NULL, G.picking, G.pop_open, G.runs);
    jt(f, G.last_result ? G.last_result : "");
    fprintf(f, ",\"f2\":{\"busy\":%d,\"text\":", G.f2job != NULL);
    jt(f, G.f2_ready ? G.f2_sug : "");
    fprintf(f, "},\"batch\":%d,\"undo\":%d,\"pairs\":[", G.wstate != 0, G.undo_toast != 0);
    for (int i = 0; i < G.n; i++) { fprintf(f, "%s[", i ? "," : ""); jt(f, G.g[i].old_name); fputc(',', f); jt(f, G.g[i].new_name); fputc(']', f); }
    cui_rect r[] = { G.r_chip, G.r_accept, G.r_drop, G.r_cancel, G.r_pick[0], G.r_pick[1], G.r_pick[2], G.r_pick[3], G.r_bar };
    fprintf(f, "],\"rects\":[");
    for (int i = 0; i < 9; i++) fprintf(f, "%s[%.1f,%.1f,%.1f,%.1f]", i ? "," : "", r[i].x, r[i].y, r[i].w, r[i].h);
    fprintf(f, "]}");
}

void ui_ghost_shutdown(void) {
    stop_run();
    if (G.f2job) { agent_free(G.f2job); G.f2job = NULL; }
    if (G.worker) { WaitForSingleObject(G.worker, INFINITE); CloseHandle(G.worker); G.worker = NULL; }
    free(G.last); G.last = NULL;
}
