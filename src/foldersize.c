/* Folder sizes: see foldersize.h. Workers walk with NtQueryDirectoryFile (a big buffer, no per-file calls), keep every
 * nested folder's total, and never follow junctions or symlinks. One ReadDirectoryChangesW per drive marks the changed
 * folder and its parents stale; a manager thread coalesces the window's wake-ups (at most every 100 ms) and recomputes
 * the shown stale folders after about 1 s of quiet. Everything is guarded by one lock held only for table work. */
#include <windows.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "foldersize.h"

#define WORKERS 4
#define BUCKETS (1u << 17)
#define RING 256
#define DEPTH_MAX 1500
#define PATH_W 32768
#define WALK_BUF (128 * 1024)

typedef LONG (NTAPI *nt_query_t)(HANDLE, HANDLE, PVOID, PVOID, void *, PVOID, ULONG, int, BOOLEAN, PVOID, BOOLEAN);
typedef struct { LONG status; ULONG_PTR info; } iosb_t;
typedef struct {
    ULONG next, index; LARGE_INTEGER created, accessed, written, changed, eof, alloc; ULONG attrs, name_len; WCHAR name[1];
} dirinfo_t;   /* FILE_DIRECTORY_INFORMATION */

enum { ST_DONE, ST_STALE, ST_PENDING };
typedef struct entry {
    struct entry *hnext, *prev, *next;
    wchar_t *key;
    uint32_t hash, len;
    uint64_t size;
    LONG task_gen;
    uint8_t state, have, pinned, drive;
} entry;
typedef struct task { struct task *next; wchar_t *path; uint32_t len; LONG gen; } task;
typedef struct chg { LONGLONG seq; wchar_t path[260]; uint8_t trunc, all; } chg;
typedef struct drive {
    HANDLE h, ev, th;
    OVERLAPPED ov;
    DWORD buf[16384];
    bool broken;
} drive;

static struct {
    CRITICAL_SECTION cs;
    bool inited, started, stopping;
    fsz_notify_fn notify;
    nt_query_t nt;
    HANDLE sem, mgr_ev, stop_ev, mgr, workers[WORKERS];
    entry **buckets, *head, *tail;
    size_t count, bytes;
    task *qhead, *qtail;
    volatile LONG gen;
    wchar_t *parent;
    wchar_t **shown; size_t shown_n, shown_cap;
    long open;
    volatile LONG version;
    bool notify_pending;
    ULONGLONG notify_at, last_notify, requeue_at, dirty_since;
    LONGLONG seq;
    chg ring[RING];
    drive *drives[26];
    unsigned delay_ms;
} G;

/* ---- keys ---- */

static wchar_t lc(wchar_t c) { return c < 128 ? (c >= L'A' && c <= L'Z' ? (wchar_t)(c + 32) : c) : (wchar_t)towlower(c); }
static uint32_t hstep(uint32_t h, wchar_t c) { return (h ^ (uint32_t)lc(c)) * 16777619u; }
static uint32_t hash_of(const wchar_t *s, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) h = hstep(h, s[i]); return h; }
static bool keq(const wchar_t *a, const wchar_t *b, size_t n) { for (size_t i = 0; i < n; i++) if (lc(a[i]) != lc(b[i])) return false; return true; }

/* A path as a key: no trailing backslash, except a drive root ("C:\"). */
static size_t canon(wchar_t *p, size_t n) {
    while (n > 3 && p[n - 1] == L'\\') p[--n] = 0;
    return n;
}

static int drive_of(const wchar_t *k) { wchar_t c = lc(k[0]); return c >= L'a' && c <= L'z' && k[1] == L':' ? c - L'a' : -1; }

/* ---- the table (lock held) ---- */

static entry *find(const wchar_t *k, size_t n, uint32_t h) {
    for (entry *e = G.buckets[h & (BUCKETS - 1)]; e; e = e->hnext)
        if (e->hash == h && e->len == n && keq(e->key, k, n)) return e;
    return NULL;
}

static void lru_unlink(entry *e) {
    if (e->prev) e->prev->next = e->next; else G.head = e->next;
    if (e->next) e->next->prev = e->prev; else G.tail = e->prev;
    e->prev = e->next = NULL;
}
static void lru_front(entry *e) {
    e->prev = NULL; e->next = G.head;
    if (G.head) G.head->prev = e; else G.tail = e;
    G.head = e;
}
static void touch(entry *e) { if (G.head != e) { lru_unlink(e); lru_front(e); } }

static size_t cost_of(const entry *e) { return sizeof *e + ((size_t)e->len + 1) * sizeof(wchar_t); }

static void erase(entry *e) {
    entry **pp = &G.buckets[e->hash & (BUCKETS - 1)];
    while (*pp != e) pp = &(*pp)->hnext;
    *pp = e->hnext;
    lru_unlink(e);
    G.count--; G.bytes -= cost_of(e);
    free(e->key); free(e);
}

static void evict(void) {
    while (G.count > FSZ_CACHE_MAX || G.bytes > FSZ_CACHE_BYTES) {
        entry *v = G.tail;
        for (int i = 0; v && (v->pinned || v->state == ST_PENDING) && i < 64; i++) v = v->prev;
        if (!v || v->pinned || v->state == ST_PENDING) v = G.tail;   /* only pinned ones left near the end: the cap still holds */
        if (!v) return;
        erase(v);
    }
}

static entry *add(const wchar_t *k, size_t n, uint32_t h) {
    entry *e = calloc(1, sizeof *e);
    wchar_t *key = malloc((n + 1) * sizeof *key);
    if (!e || !key) { free(e); free(key); return NULL; }
    memcpy(key, k, n * sizeof *key); key[n] = 0;
    e->key = key; e->hash = h; e->len = (uint32_t)n; e->drive = (uint8_t)(drive_of(k) < 0 ? 0 : drive_of(k)); e->state = ST_PENDING;
    e->hnext = G.buckets[h & (BUCKETS - 1)]; G.buckets[h & (BUCKETS - 1)] = e;
    lru_front(e);
    G.count++; G.bytes += cost_of(e);
    evict();   /* the new entry is at the front and PENDING: it stays */
    return e;
}

/* ---- wake-ups (lock held) ---- */

static void request_notify(void) {
    if (G.notify_pending) return;
    ULONGLONG now = GetTickCount64();
    G.notify_pending = true;
    G.notify_at = G.last_notify + 100 > now ? G.last_notify + 100 : now;
    SetEvent(G.mgr_ev);
}

static void schedule_requeue(void) {
    ULONGLONG now = GetTickCount64();
    if (!G.requeue_at) G.dirty_since = now;
    ULONGLONG at = now + 1000, cap = G.dirty_since + 5000;
    G.requeue_at = at < cap ? at : cap;
    SetEvent(G.mgr_ev);
}

/* ---- changes (lock held) ---- */

static bool under(const wchar_t *folder, size_t fl, const wchar_t *p, size_t pl) {   /* p is folder or below it */
    if (pl < fl || !keq(folder, p, fl)) return false;
    return pl == fl || p[fl] == L'\\' || folder[fl - 1] == L'\\';
}

static bool changed_since(const wchar_t *folder, size_t fl, LONGLONG seq0) {
    if (G.seq == seq0) return false;
    if (G.seq - seq0 >= RING) return true;
    for (LONGLONG s = seq0 + 1; s <= G.seq; s++) {
        const chg *c = &G.ring[s % RING];
        if (c->all || c->trunc) return true;
        if (under(folder, fl, c->path, wcslen(c->path))) return true;
    }
    return false;
}

static bool mark_one(const wchar_t *p, size_t n, uint32_t h) {
    entry *e = find(p, n, h);
    if (e && e->state == ST_DONE) { e->state = ST_STALE; return true; }
    return false;
}

/* The folder at p and every folder above it. */
static bool mark_chain(const wchar_t *p, size_t pl) {
    bool any = false;
    uint32_t h = 2166136261u;
    for (size_t k = 0; k <= pl; k++) {
        if (k >= 2 && (p[k] == L'\\' || p[k] == 0)) {
            if (k == 2) any |= mark_one(p, 3, hstep(h, L'\\'));
            else any |= mark_one(p, k, h);
        }
        if (k < pl) h = hstep(h, p[k]);
    }
    return any;
}

static bool mark_drive(int d) {
    bool any = false;
    for (entry *e = G.head; e; e = e->next) if (e->drive == d && e->state == ST_DONE) { e->state = ST_STALE; any = true; }
    return any;
}

static void ring_put(const wchar_t *p, size_t pl, bool all) {
    chg *c = &G.ring[++G.seq % RING];
    c->seq = G.seq; c->all = all; c->trunc = pl >= 260;
    if (pl >= 260) pl = 0;
    memcpy(c->path, p, pl * sizeof *p); c->path[pl] = 0;
}

typedef struct fni { DWORD next, action, name_len; WCHAR name[1]; } fni;

static void process_batch(int d, bool overflow) {
    bool any = false;
    wchar_t root[4] = { (wchar_t)(L'A' + d), L':', L'\\', 0 };
    wchar_t *structural[16]; size_t sl[16]; int ns = 0; bool many = false;
    EnterCriticalSection(&G.cs);
    if (overflow) {
        ring_put(root, 0, true);
        any = mark_drive(d);
    } else {
        const BYTE *b = (const BYTE *)G.drives[d]->buf;   /* the paths are copied out of the buffer below */
        for (const fni *f = (const fni *)b;;) {
            size_t nl = f->name_len / 2;
            if (nl && nl < PATH_W - 4) {
                wchar_t *p = malloc((nl + 4) * sizeof *p);
                if (p) {
                    memcpy(p, root, 3 * sizeof *p); memcpy(p + 3, f->name, nl * sizeof *p); p[3 + nl] = 0;
                    size_t pl = canon(p, 3 + nl);
                    ring_put(p, pl, false);
                    any |= mark_chain(p, pl);
                    if (f->action != FILE_ACTION_MODIFIED) {
                        if (ns < 16) { structural[ns] = p; sl[ns++] = pl; p = NULL; } else many = true;
                    }
                    free(p);
                }
            }
            if (!f->next) break;
            f = (const fni *)((const BYTE *)f + f->next);
        }
        if (many) { ring_put(root, 0, true); any |= mark_drive(d); }
        else if (ns) {
            for (entry *e = G.head; e; e = e->next) {
                if (e->drive != d || e->state != ST_DONE) continue;
                for (int i = 0; i < ns; i++)
                    if (under(structural[i], sl[i], e->key, e->len)) { e->state = ST_STALE; any = true; break; }
            }
        }
        for (int i = 0; i < ns; i++) free(structural[i]);
    }
    if (any) schedule_requeue();
    LeaveCriticalSection(&G.cs);
}

static unsigned __stdcall watch_thread(void *arg) {
    int d = (int)(INT_PTR)arg;
    drive *w = G.drives[d];
    for (;;) {
        HANDLE hs[2] = { w->ev, G.stop_ev };
        if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) != WAIT_OBJECT_0) break;
        DWORD n = 0;
        BOOL ok = GetOverlappedResult(w->h, &w->ov, &n, FALSE);
        bool overflow = !ok || n == 0;
        if (!ok && GetLastError() != ERROR_NOTIFY_ENUM_DIR) { w->broken = true; }
        process_batch(d, overflow);
        if (w->broken) break;
        ResetEvent(w->ev);
        if (!ReadDirectoryChangesW(w->h, w->buf, sizeof w->buf, TRUE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE,
                                   NULL, &w->ov, NULL)) { w->broken = true; process_batch(d, true); break; }
        if (WaitForSingleObject(G.stop_ev, 100) == WAIT_OBJECT_0) break;   /* a busy drive is read ten times a second */
    }
    return 0;
}

/* The watcher must be reading before the first walk of that drive starts. Window thread only. */
static drive *ensure_watcher(int d) {
    if (d < 0 || d > 25) return NULL;
    if (G.drives[d]) return G.drives[d];
    drive *w = calloc(1, sizeof *w);
    wchar_t root[4] = { (wchar_t)(L'A' + d), L':', L'\\', 0 };
    if (!w) return NULL;
    w->h = CreateFileW(root, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    w->ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->ov.hEvent = w->ev;
    if (w->h == INVALID_HANDLE_VALUE || !w->ev ||
        !ReadDirectoryChangesW(w->h, w->buf, sizeof w->buf, TRUE,
                               FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE, NULL, &w->ov, NULL)) {
        w->broken = true;   /* sizes are still computed, but nothing is reused: nothing would tell us it went stale */
        if (w->h != INVALID_HANDLE_VALUE) CloseHandle(w->h);
        if (w->ev) CloseHandle(w->ev);
        w->h = NULL; w->ev = NULL;
        G.drives[d] = w;
        return w;
    }
    G.drives[d] = w;
    w->th = (HANDLE)_beginthreadex(NULL, 0, watch_thread, (void *)(INT_PTR)d, 0, NULL);
    return w;
}

/* ---- the walk ---- */

typedef struct walk { LONG gen; wchar_t *path; BYTE *buf; bool cancelled; int drive; } walk;

static bool cancelled(const walk *w) { return InterlockedCompareExchange(&G.gen, 0, 0) != w->gen; }

static void store(const walk *w, size_t pl, uint32_t h, uint64_t total, LONGLONG seq0, bool top) {
    EnterCriticalSection(&G.cs);
    entry *e = find(w->path, pl, h);
    bool chg_ = changed_since(w->path, pl, seq0);
    if (e && e->state == ST_PENDING && (!top || e->task_gen != w->gen)) { LeaveCriticalSection(&G.cs); return; }   /* another task owns it */
    if (!e) { e = add(w->path, pl, h); if (!e) { LeaveCriticalSection(&G.cs); return; } e->task_gen = w->gen; }
    e->size = total; e->have = 1;
    e->state = chg_ ? ST_STALE : ST_DONE;
    if (chg_) schedule_requeue();
    touch(e);
    if (top) { InterlockedIncrement(&G.version); request_notify(); }
    LeaveCriticalSection(&G.cs);
}

static uint64_t walk_dir(walk *w, size_t pl, int depth) {
    if (cancelled(w)) { w->cancelled = true; return 0; }
    uint32_t h = hash_of(w->path, pl);
    if (depth > 0) {   /* a nested folder that is already known and fresh */
        EnterCriticalSection(&G.cs);
        entry *e = find(w->path, pl, h);
        uint64_t v = 0; bool hit = false;
        if (e && e->state == ST_DONE && !(G.drives[w->drive] && G.drives[w->drive]->broken)) { v = e->size; hit = true; touch(e); }
        LeaveCriticalSection(&G.cs);
        if (hit) return v;
    }
    EnterCriticalSection(&G.cs);
    LONGLONG seq0 = G.seq;
    LeaveCriticalSection(&G.cs);
    uint64_t total = 0;
    wchar_t *names = NULL; size_t nlen = 0, ncap = 0;
    if (G.delay_ms) Sleep(G.delay_ms);
    wchar_t *full = malloc((pl + 8) * sizeof *full);
    if (full) {
        memcpy(full, L"\\\\?\\", 4 * sizeof *full); memcpy(full + 4, w->path, pl * sizeof *full); full[4 + pl] = 0;
        HANDLE hd = CreateFileW(full, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
        free(full);
        if (hd != INVALID_HANDLE_VALUE) {
            iosb_t io; BOOLEAN first = TRUE;
            for (;;) {
                LONG st = G.nt(hd, NULL, NULL, NULL, &io, w->buf, WALK_BUF, 1 /* FileDirectoryInformation */, FALSE, NULL, first);
                first = FALSE;
                if (st != 0) break;
                for (const dirinfo_t *e = (const dirinfo_t *)w->buf;;) {
                    size_t nl = e->name_len / 2;
                    bool dot = e->name[0] == L'.' && (nl == 1 || (nl == 2 && e->name[1] == L'.'));
                    if (!dot) {
                        if (e->attrs & FILE_ATTRIBUTE_DIRECTORY) {
                            if (!(e->attrs & FILE_ATTRIBUTE_REPARSE_POINT) && depth < DEPTH_MAX) {
                                if (nlen + nl + 1 > ncap) {
                                    size_t nc = ncap ? ncap * 2 : 512;
                                    while (nc < nlen + nl + 1) nc *= 2;
                                    wchar_t *nn = realloc(names, nc * sizeof *nn);
                                    if (!nn) break;
                                    names = nn; ncap = nc;
                                }
                                memcpy(names + nlen, e->name, nl * sizeof *names); nlen += nl; names[nlen++] = 0;
                            }
                        } else total += (uint64_t)e->eof.QuadPart;
                    }
                    if (!e->next) break;
                    e = (const dirinfo_t *)((const BYTE *)e + e->next);
                }
            }
            CloseHandle(hd);
        }
    }
    for (size_t at = 0; at < nlen && !w->cancelled; ) {
        size_t nl = wcslen(names + at);
        if (pl + 1 + nl < PATH_W - 8) {
            size_t k = pl;
            if (w->path[k - 1] != L'\\') w->path[k++] = L'\\';
            memcpy(w->path + k, names + at, nl * sizeof *names); k += nl; w->path[k] = 0;
            total += walk_dir(w, k, depth + 1);
            w->path[pl] = 0;
        }
        at += nl + 1;
    }
    free(names);
    if (w->cancelled) return 0;
    store(w, pl, h, total, seq0, depth == 0);
    return total;
}

/* A task that will not finish: its entry goes back to what it was (lock held). */
static void drop_locked(const task *t) {
    entry *e = find(t->path, t->len, hash_of(t->path, t->len));
    if (!e || e->state != ST_PENDING || e->task_gen != t->gen) return;
    if (e->have) e->state = ST_STALE; else erase(e);
}

static void enqueue_locked(const wchar_t *path, size_t n, entry *e) {
    task *t = calloc(1, sizeof *t);
    wchar_t *p = malloc((n + 1) * sizeof *p);
    if (!t || !p) { free(t); free(p); e->state = ST_STALE; return; }
    memcpy(p, path, n * sizeof *p); p[n] = 0;
    t->path = p; t->len = (uint32_t)n; t->gen = G.gen;
    e->state = ST_PENDING; e->task_gen = t->gen;
    if (G.qtail) G.qtail->next = t; else G.qhead = t;
    G.qtail = t;
    G.open++;
    ReleaseSemaphore(G.sem, 1, NULL);
}

static unsigned __stdcall worker_thread(void *arg) {
    (void)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    walk w = { 0 };
    w.buf = malloc(WALK_BUF);
    w.path = malloc(PATH_W * sizeof *w.path);
    while (w.buf && w.path) {
        WaitForSingleObject(G.sem, INFINITE);
        if (G.stopping) break;
        EnterCriticalSection(&G.cs);
        task *t = G.qhead;
        if (t) { G.qhead = t->next; if (!G.qhead) G.qtail = NULL; }
        if (t && t->gen != G.gen) { drop_locked(t); free(t->path); free(t); t = NULL; }
        LeaveCriticalSection(&G.cs);
        if (!t) continue;
        memcpy(w.path, t->path, ((size_t)t->len + 1) * sizeof *w.path);
        w.gen = t->gen; w.cancelled = false; w.drive = drive_of(t->path) < 0 ? 0 : drive_of(t->path);
        walk_dir(&w, t->len, 0);
        EnterCriticalSection(&G.cs);
        if (w.cancelled) drop_locked(t);
        if (t->gen == G.gen) { G.open--; request_notify(); }
        LeaveCriticalSection(&G.cs);
        free(t->path); free(t);
    }
    free(w.buf); free(w.path);
    return 0;
}

/* Queued tasks of an older view are dropped by the workers when they pop them; running ones stop at the next folder. */

static unsigned __stdcall manager_thread(void *arg) {
    (void)arg;
    for (;;) {
        DWORD wait = INFINITE;
        EnterCriticalSection(&G.cs);
        ULONGLONG now = GetTickCount64();
        if (G.notify_pending) wait = G.notify_at > now ? (DWORD)(G.notify_at - now) : 0;
        if (G.requeue_at) { DWORD r = G.requeue_at > now ? (DWORD)(G.requeue_at - now) : 0; if (r < wait) wait = r; }
        LeaveCriticalSection(&G.cs);
        WaitForSingleObject(G.mgr_ev, wait);
        if (G.stopping) break;
        bool fire = false;
        EnterCriticalSection(&G.cs);
        now = GetTickCount64();
        if (G.notify_pending && now >= G.notify_at) { G.notify_pending = false; G.last_notify = now; fire = true; }
        if (G.requeue_at && now >= G.requeue_at) {   /* quiet: the shown folders that went stale are measured again */
            G.requeue_at = 0;
            for (size_t i = 0; i < G.shown_n; i++) {
                size_t n = wcslen(G.shown[i]);
                entry *e = find(G.shown[i], n, hash_of(G.shown[i], n));
                if (e && e->state == ST_STALE) enqueue_locked(G.shown[i], n, e);
            }
        }
        LeaveCriticalSection(&G.cs);
        if (fire && G.notify) G.notify();
    }
    return 0;
}

/* ---- the interface ---- */

static int to_wide(const char *s, wchar_t *w, int cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, w, cap);
    return n > 0 ? n - 1 : -1;
}

void fsz_start(fsz_notify_fn notify) {
    if (G.inited) return;
    InitializeCriticalSection(&G.cs);
    G.inited = true; G.notify = notify;
    G.nt = (nt_query_t)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryDirectoryFile");
}

static void ensure_threads(void) {
    if (G.started || !G.nt) return;
    G.buckets = calloc(BUCKETS, sizeof *G.buckets);
    G.sem = CreateSemaphoreW(NULL, 0, 1 << 20, NULL);
    G.mgr_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    G.stop_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!G.buckets || !G.sem || !G.mgr_ev || !G.stop_ev) return;
    for (int i = 0; i < WORKERS; i++) G.workers[i] = (HANDLE)_beginthreadex(NULL, 0, worker_thread, NULL, 0, NULL);
    G.mgr = (HANDLE)_beginthreadex(NULL, 0, manager_thread, NULL, 0, NULL);
    G.started = true;
}

void fsz_stop(void) {
    if (!G.started) return;
    G.stopping = true;
    InterlockedIncrement(&G.gen);
    SetEvent(G.stop_ev);
    ReleaseSemaphore(G.sem, WORKERS, NULL);
    SetEvent(G.mgr_ev);
    for (int i = 0; i < 26; i++) if (G.drives[i] && G.drives[i]->h) CancelIoEx(G.drives[i]->h, NULL);
    HANDLE all[WORKERS + 1 + 26]; int n = 0;
    for (int i = 0; i < WORKERS; i++) if (G.workers[i]) all[n++] = G.workers[i];
    if (G.mgr) all[n++] = G.mgr;
    for (int i = 0; i < 26; i++) if (G.drives[i] && G.drives[i]->th) all[n++] = G.drives[i]->th;
    for (int i = 0; i < n; i += MAXIMUM_WAIT_OBJECTS) WaitForMultipleObjects((DWORD)(n - i < MAXIMUM_WAIT_OBJECTS ? n - i : MAXIMUM_WAIT_OBJECTS), all + i, TRUE, 1500);
    /* nothing is freed: the process is ending */
}

bool fsz_eligible(const char *path) {
    wchar_t w[8];
    if (!path || !path[0] || !path[1] || path[1] != ':' || (path[2] && path[2] != '\\')) return false;
    w[0] = (wchar_t)path[0]; w[1] = L':'; w[2] = L'\\'; w[3] = 0;
    UINT t = GetDriveTypeW(w);
    return t == DRIVE_FIXED || t == DRIVE_REMOVABLE;
}

static void unpin_shown_locked(void) {
    for (size_t i = 0; i < G.shown_n; i++) {
        size_t n = wcslen(G.shown[i]);
        entry *e = find(G.shown[i], n, hash_of(G.shown[i], n));
        if (e) e->pinned = 0;
        free(G.shown[i]);
    }
    G.shown_n = 0;
}

void fsz_view(const char *parent) {
    wchar_t w[2200] = L"";
    size_t n = 0;
    if (!G.inited) return;
    ensure_threads();
    if (!G.started) return;
    if (parent && parent[0]) { int k = to_wide(parent, w, 2200); if (k < 0) return; n = canon(w, (size_t)k); }
    EnterCriticalSection(&G.cs);
    bool same = G.parent && n && wcslen(G.parent) == n && keq(G.parent, w, n);
    if (!same) {
        InterlockedIncrement(&G.gen);
        G.open = 0;
        free(G.parent);
        G.parent = n ? _wcsdup(w) : NULL;
    }
    unpin_shown_locked();
    LeaveCriticalSection(&G.cs);
}

void fsz_want(const char *path) {
    wchar_t w[2200];
    if (!G.started) return;
    int k = to_wide(path, w, 2200);
    if (k <= 0) return;
    size_t n = canon(w, (size_t)k);
    drive *dr = ensure_watcher(drive_of(w));
    if (!dr) return;
    uint32_t h = hash_of(w, n);
    EnterCriticalSection(&G.cs);
    if (G.shown_n == G.shown_cap) {
        size_t nc = G.shown_cap ? G.shown_cap * 2 : 256;
        wchar_t **ns = realloc(G.shown, nc * sizeof *ns);
        if (!ns) { LeaveCriticalSection(&G.cs); return; }
        G.shown = ns; G.shown_cap = nc;
    }
    G.shown[G.shown_n++] = _wcsdup(w);
    entry *e = find(w, n, h);
    if (!e) {
        e = add(w, n, h);
        if (e) enqueue_locked(w, n, e);
    } else if (e->state == ST_STALE || (e->state == ST_DONE && dr->broken) || (e->state == ST_PENDING && e->task_gen != G.gen)) {
        enqueue_locked(w, n, e);
    }
    if (e) e->pinned = 1;
    LeaveCriticalSection(&G.cs);
}

int fsz_get(const char *path, uint64_t *size) {
    wchar_t w[2200];
    if (!G.started) return FSZ_NONE;
    int k = to_wide(path, w, 2200);
    if (k <= 0) return FSZ_NONE;
    size_t n = canon(w, (size_t)k);
    uint32_t h = hash_of(w, n);
    int r = FSZ_NONE;
    EnterCriticalSection(&G.cs);
    entry *e = find(w, n, h);
    if (e) {
        touch(e);
        if (e->have) { if (size) *size = e->size; r = FSZ_DONE; } else r = FSZ_PENDING;
    }
    LeaveCriticalSection(&G.cs);
    return r;
}

int fsz_open(void) {
    if (!G.started) return 0;
    EnterCriticalSection(&G.cs);
    int r = G.open > 0 ? (int)G.open : 0;
    LeaveCriticalSection(&G.cs);
    return r;
}

unsigned fsz_version(void) { return (unsigned)InterlockedCompareExchange(&G.version, 0, 0); }

bool fsz_stale(const char *path) {
    wchar_t w[2200];
    if (!G.started) return false;
    int k = to_wide(path, w, 2200);
    if (k <= 0) return false;
    size_t n = canon(w, (size_t)k);
    EnterCriticalSection(&G.cs);
    entry *e = find(w, n, hash_of(w, n));
    bool r = e && (e->state == ST_STALE || (e->state == ST_PENDING && e->have));
    LeaveCriticalSection(&G.cs);
    return r;
}

void fsz_test_delay(unsigned ms) { G.delay_ms = ms; }
