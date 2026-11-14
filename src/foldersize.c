/* Folder sizes: see foldersize.h. Entering a folder measures every child folder again; nothing is kept up to date
 * while a folder stays open, and nothing runs when nothing is being measured (no watcher, no timer).
 * Every directory found is its own task in one pool of 4 below-normal workers (NtQueryDirectoryFile, a big buffer, no
 * per-file calls); a folder's total is final when its own files and all its subfolders are done. Junctions and symlinks
 * are not followed. Every finished folder keeps its total in an LRU cache; a shown folder is never evicted. A child that
 * was measured a few seconds ago (by itself, or inside the walk of a folder above it) is not walked again, and a child
 * inside a walk that is still running waits for that walk. Everything is guarded by one lock held only for table work. */
#include <windows.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "foldersize.h"

#define WORKERS 8
#define BUCKETS (1u << 17)
#define DEPTH_MAX 1500
#define PATH_W 32768
#define WALK_BUF (128 * 1024)

typedef LONG (NTAPI *nt_query_t)(HANDLE, HANDLE, PVOID, PVOID, void *, PVOID, ULONG, int, BOOLEAN, PVOID, BOOLEAN);
typedef struct { LONG status; ULONG_PTR info; } iosb_t;
typedef struct {
    ULONG next, index; LARGE_INTEGER created, accessed, written, changed, eof, alloc; ULONG attrs, name_len; WCHAR name[1];
} dirinfo_t;   /* FILE_DIRECTORY_INFORMATION */

enum { ST_DONE, ST_PENDING };   /* PENDING: being measured (its own walk, or the walk of a folder above it: adopt) */

typedef struct walk {            /* one shown folder's measurement, shared by its tasks */
    struct walk *next;
    wchar_t *root;
    size_t len;
    volatile LONG cancel;
    uint32_t start;
} walk;

typedef struct entry {
    struct entry *hnext, *prev, *next;
    wchar_t *key;
    uint32_t hash, len;
    uint64_t size;               /* the folder's total; the last known one while it is measured again */
    uint32_t walk_ms, last_end;  /* how long its last walk took, and when it ended (ms ticks, low 32 bits) */
    uint32_t tid;                /* the task that owns its PENDING state (0: waits for a walk above it) */
    LONG open_gen;               /* the view generation that counts this folder as open work (0: not counted) */
    uint8_t state, have, pinned, inlru, adopt;
} entry;

typedef struct task {
    struct task *next, *parent;
    wchar_t *path;
    uint32_t len, id, depth;
    LONG pend;                   /* unfinished children */
    uint64_t sum;
    ULONGLONG t0;
    walk *w;
    uint8_t root, failed;
} task;

static struct {
    CRITICAL_SECTION cs;
    bool inited, started, stopping;
    fsz_notify_fn notify;
    nt_query_t nt;
    HANDLE sem, mgr_ev, mgr, workers[WORKERS];
    entry **buckets, *head, *tail;
    size_t count, bytes;
    size_t cap_entries;
    task *qhead, *qtail;
    walk *walks;
    LONG gen;
    long adopters;
    uint32_t tid, recent_ms;
    wchar_t *parent;
    wchar_t **shown; size_t shown_n, shown_cap;
    long open;
    volatile LONG version;
    bool notify_pending;
    ULONGLONG notify_at, last_notify;
    unsigned delay_ms;
    volatile LONG reads, active, peak;
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
static uint32_t tick32(void) { return (uint32_t)GetTickCount64(); }

/* p is below root (a proper descendant). */
static bool under(const wchar_t *root, size_t rl, const wchar_t *p, size_t n) {
    return n > rl && keq(root, p, rl) && (rl == 3 || p[rl] == L'\\');
}

/* The folders on the way down to a path: "C:\", "C:\a", "C:\a\b" ... with their key hashes. */
typedef struct { const wchar_t *p; size_t pl, k; uint32_t h; } chain;
static void chain_init(chain *c, const wchar_t *p, size_t pl) { c->p = p; c->pl = pl; c->k = 0; c->h = 2166136261u; }
static bool chain_next(chain *c, size_t *len, uint32_t *hash) {
    while (c->k <= c->pl) {
        size_t k = c->k;
        uint32_t h = c->h;
        bool boundary = k >= 2 && (k == c->pl || c->p[k] == L'\\') && !(k == c->pl && c->pl == 3);
        if (k < c->pl) c->h = hstep(c->h, c->p[k]);
        c->k++;
        if (boundary) {
            if (k == 2) { *len = 3; *hash = hstep(h, L'\\'); } else { *len = k; *hash = h; }
            return true;
        }
    }
    return false;
}

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
/* Only entries that may leave are in the LRU list: not shown, not being measured. */
static void relist(entry *e) {
    bool want = !e->pinned && e->state != ST_PENDING;
    if (want && !e->inlru) { lru_front(e); e->inlru = 1; }
    else if (!want && e->inlru) { lru_unlink(e); e->inlru = 0; }
}
static void touch(entry *e) { if (e->inlru && G.head != e) { lru_unlink(e); lru_front(e); } }

static size_t cost_of(const entry *e) { return sizeof *e + ((size_t)e->len + 1) * sizeof(wchar_t); }

static void erase(entry *e) {
    entry **pp = &G.buckets[e->hash & (BUCKETS - 1)];
    while (*pp != e) pp = &(*pp)->hnext;
    *pp = e->hnext;
    if (e->inlru) lru_unlink(e);
    G.count--; G.bytes -= cost_of(e);
    free(e->key); free(e);
}

static void evict(void) {
    while ((G.count > G.cap_entries || G.bytes > FSZ_CACHE_BYTES) && G.tail) erase(G.tail);
}

static entry *add(const wchar_t *k, size_t n, uint32_t h) {
    entry *e = calloc(1, sizeof *e);
    wchar_t *key = malloc((n + 1) * sizeof *key);
    if (!e || !key) { free(e); free(key); return NULL; }
    memcpy(key, k, n * sizeof *key); key[n] = 0;
    e->key = key; e->hash = h; e->len = (uint32_t)n; e->state = ST_PENDING;
    e->hnext = G.buckets[h & (BUCKETS - 1)]; G.buckets[h & (BUCKETS - 1)] = e;
    G.count++; G.bytes += cost_of(e);
    return e;   /* PENDING: not in the LRU list, so the eviction cannot take it */
}

/* ---- wake-ups (lock held) ---- */

static void request_notify(void) {
    if (G.notify_pending) return;
    ULONGLONG now = GetTickCount64();
    G.notify_pending = true;
    G.notify_at = G.last_notify + 100 > now ? G.last_notify + 100 : now;
    SetEvent(G.mgr_ev);
}
static void bump(void) { InterlockedIncrement(&G.version); request_notify(); }

/* The folder's measurement is over: it no longer counts as open work of the view. */
static void open_done(entry *e) {
    if (e->open_gen == G.gen) { G.open--; e->open_gen = 0; request_notify(); }
}
static void open_count(entry *e) {
    if (e->open_gen != G.gen) { e->open_gen = G.gen; G.open++; }
}

/* ---- tasks ---- */

typedef struct listing { uint64_t own; wchar_t *names; size_t nlen, ncap, n; } listing;

/* One directory read: the size of its files, and the names of its real subfolders. */
static void read_dir(const wchar_t *path, size_t pl, BYTE *buf, listing *L, wchar_t *scratch) {
    L->own = 0; L->nlen = 0; L->n = 0;
    InterlockedIncrement(&G.reads);
    LONG a = InterlockedIncrement(&G.active), pk;
    while (a > (pk = G.peak)) InterlockedCompareExchange(&G.peak, a, pk);
    if (G.delay_ms) Sleep(G.delay_ms);
    memcpy(scratch, L"\\\\?\\", 4 * sizeof *scratch); memcpy(scratch + 4, path, pl * sizeof *scratch); scratch[4 + pl] = 0;
    HANDLE hd = CreateFileW(scratch, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hd != INVALID_HANDLE_VALUE) {
        iosb_t io; BOOLEAN first = TRUE;
        for (;;) {
            LONG st = G.nt(hd, NULL, NULL, NULL, &io, buf, WALK_BUF, 1 /* FileDirectoryInformation */, FALSE, NULL, first);
            first = FALSE;
            if (st != 0) break;
            for (const dirinfo_t *e = (const dirinfo_t *)buf;;) {
                size_t nl = e->name_len / 2;
                bool dot = e->name[0] == L'.' && (nl == 1 || (nl == 2 && e->name[1] == L'.'));
                if (!dot) {
                    if (e->attrs & FILE_ATTRIBUTE_DIRECTORY) {
                        if (!(e->attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                            if (L->nlen + nl + 1 > L->ncap) {
                                size_t nc = L->ncap ? L->ncap * 2 : 512;
                                while (nc < L->nlen + nl + 1) nc *= 2;
                                wchar_t *nn = realloc(L->names, nc * sizeof *nn);
                                if (!nn) break;
                                L->names = nn; L->ncap = nc;
                            }
                            memcpy(L->names + L->nlen, e->name, nl * sizeof *L->names); L->nlen += nl; L->names[L->nlen++] = 0; L->n++;
                        }
                    } else L->own += (uint64_t)e->eof.QuadPart;
                }
                if (!e->next) break;
                e = (const dirinfo_t *)((const BYTE *)e + e->next);
            }
        }
        CloseHandle(hd);
    }
    InterlockedDecrement(&G.active);
}

static task *new_task(const wchar_t *base, size_t bl, const wchar_t *name, size_t nl) {   /* base\name, or base alone */
    task *t = calloc(1, sizeof *t);
    size_t extra = nl ? 1 + nl : 0;
    wchar_t *p = malloc((bl + extra + 1) * sizeof *p);
    if (!t || !p) { free(t); free(p); return NULL; }
    memcpy(p, base, bl * sizeof *p);
    size_t k = bl;
    if (nl) { if (p[k - 1] != L'\\') p[k++] = L'\\'; memcpy(p + k, name, nl * sizeof *p); k += nl; }
    p[k] = 0;
    t->path = p; t->len = (uint32_t)k;
    return t;
}

static void push_front(task *first, task *last) {
    last->next = G.qhead; G.qhead = first;
    if (!G.qtail) G.qtail = last;
}
static void push_back(task *t) {
    t->next = NULL;
    if (G.qtail) G.qtail->next = t; else G.qhead = t;
    G.qtail = t;
}

/* A shown folder is measured: its entry becomes PENDING (a value it had stays readable) and its root task is queued (lock held). */
static void enqueue_root(entry *e) {
    task *t = new_task(e->key, e->len, NULL, 0);
    walk *w = calloc(1, sizeof *w);
    wchar_t *rp = _wcsdup(e->key);
    if (!t || !w || !rp) { if (t) { free(t->path); free(t); } free(w); free(rp); e->state = ST_DONE; relist(e); return; }
    w->root = rp; w->len = e->len; w->start = tick32(); w->next = G.walks; G.walks = w;
    t->w = w; t->root = 1;
    t->id = e->tid = ++G.tid;
    e->state = ST_PENDING; e->adopt = 0;
    open_count(e);
    relist(e);
    push_back(t);
    ReleaseSemaphore(G.sem, 1, NULL);
}

/* A finished folder goes into the cache (lock held). */
static void store_dir(task *t, uint64_t total) {
    uint32_t h = hash_of(t->path, t->len);
    entry *e = find(t->path, t->len, h);
    if (t->root) {
        if (!e || e->tid != t->id || e->state != ST_PENDING) return;
    } else if (!e) {
        e = add(t->path, t->len, h); if (!e) return;
    } else if (e->state == ST_PENDING && !e->adopt) return;   /* it has a walk of its own running */
    if (e->adopt) { e->adopt = 0; G.adopters--; }
    e->size = total; e->have = 1; e->state = ST_DONE;
    e->walk_ms = (uint32_t)(GetTickCount64() - t->t0); e->last_end = tick32();
    open_done(e);
    relist(e); touch(e);
    if (t->root || e->pinned) bump();
    evict();
}

/* A root that will not finish: its entry goes back to what it was (lock held). */
static void drop_root(task *t) {
    entry *e = find(t->path, t->len, hash_of(t->path, t->len));
    if (!e || e->state != ST_PENDING || e->tid != t->id) return;
    open_done(e);
    if (e->have) { e->state = ST_DONE; relist(e); } else erase(e);
}

/* The walk is over: children that waited for it and were not reached go back to what they were (lock held). */
static void end_walk(walk *w) {
    for (walk **pp = &G.walks; *pp; pp = &(*pp)->next) if (*pp == w) { *pp = w->next; break; }
    if (G.adopters > 0) {
        for (size_t b = 0; b < BUCKETS && G.adopters > 0; b++)
            for (entry *e = G.buckets[b], *nx; e; e = nx) {
                nx = e->hnext;
                if (!e->adopt || !under(w->root, w->len, e->key, e->len)) continue;
                e->adopt = 0; G.adopters--;
                open_done(e);
                if (e->have) { e->state = ST_DONE; relist(e); } else erase(e);
            }
    }
    free(w->root); free(w);
}

/* t has no unfinished children: its total is final, and so may be its parent's (lock held). */
static void finalize(task *t) {
    for (;;) {
        task *par = t->parent;
        uint64_t total = t->sum;
        bool failed = t->failed;
        if (!failed) store_dir(t, total); else if (t->root) drop_root(t);
        if (t->root) { request_notify(); end_walk(t->w); }
        free(t->path); free(t);
        if (!par) break;
        par->sum += total; par->failed = (uint8_t)(par->failed | failed);
        if (--par->pend) break;
        t = par;
    }
}

static void run_task(task *t, BYTE *buf, wchar_t *scratch) {
    listing L = { 0 };
    t->t0 = GetTickCount64();
    read_dir(t->path, t->len, buf, &L, scratch);
    task *first = NULL, *last = NULL; LONG n = 0;
    if (t->depth < DEPTH_MAX) {
        for (size_t at = 0; at < L.nlen; ) {
            size_t nl = wcslen(L.names + at);
            if (t->len + 1 + nl + 1 < PATH_W) {
                task *c = new_task(t->path, t->len, L.names + at, nl);
                if (c) {
                    c->parent = t; c->w = t->w; c->depth = t->depth + 1;
                    if (last) last->next = c; else first = c;
                    last = c; n++;
                }
            }
            at += nl + 1;
        }
    }
    free(L.names);
    EnterCriticalSection(&G.cs);
    t->sum = L.own;
    t->pend = n;
    if (n) { push_front(first, last); ReleaseSemaphore(G.sem, n, NULL); }
    else finalize(t);
    LeaveCriticalSection(&G.cs);
}

static unsigned __stdcall worker_thread(void *arg) {
    (void)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    BYTE *buf = malloc(WALK_BUF);
    wchar_t *scratch = malloc(PATH_W * sizeof *scratch);
    while (buf && scratch) {
        WaitForSingleObject(G.sem, INFINITE);
        if (G.stopping) break;
        EnterCriticalSection(&G.cs);
        task *t = G.qhead;
        if (t) { G.qhead = t->next; if (!G.qhead) G.qtail = NULL; }
        if (t && t->w->cancel) { t->failed = 1; t->pend = 0; finalize(t); t = NULL; }   /* a walk nobody waits for any more */
        LeaveCriticalSection(&G.cs);
        if (t) run_task(t, buf, scratch);
    }
    free(buf); free(scratch);
    return 0;
}

/* ---- the manager: only the throttled wake-up of the window; it sleeps until one is due ---- */

static unsigned __stdcall manager_thread(void *arg) {
    (void)arg;
    for (;;) {
        DWORD wait = INFINITE;
        EnterCriticalSection(&G.cs);
        ULONGLONG now = GetTickCount64();
        if (G.notify_pending) wait = G.notify_at > now ? (DWORD)(G.notify_at - now) : 0;
        LeaveCriticalSection(&G.cs);
        WaitForSingleObject(G.mgr_ev, wait);
        if (G.stopping) break;
        bool fire = false;
        EnterCriticalSection(&G.cs);
        now = GetTickCount64();
        if (G.notify_pending && now >= G.notify_at) { G.notify_pending = false; G.last_notify = now; fire = true; }
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
    G.inited = true; G.notify = notify; G.gen = 1;
    G.cap_entries = FSZ_CACHE_MAX; G.recent_ms = FSZ_RECENT_MS;
    G.nt = (nt_query_t)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryDirectoryFile");
}

static void ensure_threads(void) {
    if (G.started || !G.nt) return;
    G.buckets = calloc(BUCKETS, sizeof *G.buckets);
    G.sem = CreateSemaphoreW(NULL, 0, 1 << 24, NULL);
    G.mgr_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!G.buckets || !G.sem || !G.mgr_ev) return;
    for (int i = 0; i < WORKERS; i++) G.workers[i] = (HANDLE)_beginthreadex(NULL, 0, worker_thread, NULL, 0, NULL);
    G.mgr = (HANDLE)_beginthreadex(NULL, 0, manager_thread, NULL, 0, NULL);
    G.started = true;
}

void fsz_stop(void) {
    if (!G.started) return;
    G.stopping = true;
    ReleaseSemaphore(G.sem, WORKERS, NULL);
    SetEvent(G.mgr_ev);
    HANDLE all[WORKERS + 1]; int n = 0;
    for (int i = 0; i < WORKERS; i++) if (G.workers[i]) all[n++] = G.workers[i];
    if (G.mgr) all[n++] = G.mgr;
    WaitForMultipleObjects((DWORD)n, all, TRUE, 1500);
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
        if (e) { e->pinned = 0; relist(e); }
        free(G.shown[i]);
    }
    G.shown_n = 0;
    evict();   /* the cap may have been exceeded while they were shown */
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
        G.gen++;
        G.open = 0;
        free(G.parent);
        G.parent = n ? _wcsdup(w) : NULL;
        /* walks of folders that are not on the way to the new one are cancelled; one that is on the way keeps going,
           it measures the children the new view will ask for */
        for (walk *x = G.walks; x; x = x->next)
            if (!(n && ((x->len == n && keq(x->root, w, n)) || under(x->root, x->len, w, n)))) x->cancel = 1;
    }
    unpin_shown_locked();
    LeaveCriticalSection(&G.cs);
}

/* Measured a moment ago, by itself or inside a walk that ended a moment ago or is still running? */
static bool fresh(const entry *e, const wchar_t *p, size_t n, uint32_t now) {
    if (!e->have) return false;
    if (now - e->last_end < G.recent_ms) return true;
    chain c; size_t len; uint32_t h;
    chain_init(&c, p, n);
    while (chain_next(&c, &len, &h)) {
        if (len == n) break;
        const entry *a = find(p, len, h);
        if (a && a->have && a->state == ST_DONE && now - a->last_end < G.recent_ms && (int32_t)(e->last_end - (a->last_end - a->walk_ms)) >= 0) return true;
    }
    for (const walk *x = G.walks; x; x = x->next)
        if (!x->cancel && under(x->root, x->len, p, n) && (int32_t)(e->last_end - x->start) >= 0) return true;
    return false;
}

void fsz_want(const char *path) {
    wchar_t w[2200];
    if (!G.started) return;
    int k = to_wide(path, w, 2200);
    if (k <= 0) return;
    size_t n = canon(w, (size_t)k);
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
    if (e && e->state == ST_PENDING) open_count(e);          /* being measured already: the view waits for it */
    else if (!e || !fresh(e, w, n, tick32())) {
        bool covered = false;
        for (const walk *x = G.walks; x; x = x->next) if (!x->cancel && under(x->root, x->len, w, n)) { covered = true; break; }
        if (!e) e = add(w, n, h);
        if (e && covered) {                                    /* inside a walk that is still running: it will bring the value */
            e->state = ST_PENDING; e->adopt = 1; e->tid = 0; G.adopters++;
            open_count(e);
        } else if (e) enqueue_root(e);
    }
    if (e) { e->pinned = 1; relist(e); }
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

bool fsz_stale(const char *path) {   /* the value shown is the last known one: a new measurement is running */
    wchar_t w[2200];
    if (!G.started) return false;
    int k = to_wide(path, w, 2200);
    if (k <= 0) return false;
    size_t n = canon(w, (size_t)k);
    EnterCriticalSection(&G.cs);
    entry *e = find(w, n, hash_of(w, n));
    bool r = e && e->state == ST_PENDING && e->have;
    LeaveCriticalSection(&G.cs);
    return r;
}

void fsz_test_delay(unsigned ms) { G.delay_ms = ms; }
void fsz_test_cap(unsigned max_entries) { G.cap_entries = max_entries ? max_entries : FSZ_CACHE_MAX; }
void fsz_test_recent(unsigned ms) { G.recent_ms = ms; }
unsigned fsz_test_reads(void) { return (unsigned)InterlockedCompareExchange(&G.reads, 0, 0); }
unsigned fsz_test_entries(unsigned long long *bytes) {   /* checks only: entries held and the bytes their keys and records take */
    EnterCriticalSection(&G.cs);
    unsigned n = (unsigned)G.count; if (bytes) *bytes = (unsigned long long)G.bytes;
    LeaveCriticalSection(&G.cs);
    return n;
}
unsigned fsz_test_peak(void) { return (unsigned)InterlockedExchange(&G.peak, G.active); }   /* and starts counting again */
