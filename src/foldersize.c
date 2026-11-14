/* Folder sizes: see foldersize.h. Every directory found is its own task in one pool of 4 below-normal workers
 * (NtQueryDirectoryFile, a big buffer, no per-file calls); a folder's total is final when its own files and all its
 * subfolders are done. Junctions and symlinks are not followed. Every finished folder keeps its own files' size and its
 * total in an LRU cache. One ReadDirectoryChangesW per drive turns a change into a small request: the changed folder's
 * own entries are read again (one directory read) and the difference is added to its cached ancestors; a new subfolder
 * is walked on its own, a removed one is subtracted. Only data that is missing (evicted, or a watcher overflow) is
 * walked again, no sooner than max(1 s, 3 x the last walk). Everything is guarded by one lock held only for table work. */
#include <windows.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "foldersize.h"

#define WORKERS 4
#define BUCKETS (1u << 17)
#define REQ_MAX 8192
#define DEPTH_MAX 1500
#define PATH_W 32768
#define WALK_BUF (128 * 1024)
#define DEBOUNCE_MS 250
#define REWALK_MIN_MS 1000

typedef LONG (NTAPI *nt_query_t)(HANDLE, HANDLE, PVOID, PVOID, void *, PVOID, ULONG, int, BOOLEAN, PVOID, BOOLEAN);
typedef struct { LONG status; ULONG_PTR info; } iosb_t;
typedef struct {
    ULONG next, index; LARGE_INTEGER created, accessed, written, changed, eof, alloc; ULONG attrs, name_len; WCHAR name[1];
} dirinfo_t;   /* FILE_DIRECTORY_INFORMATION */

enum { ST_DONE, ST_STALE, ST_PENDING };   /* STALE: the data is missing or untrusted, it needs a walk */
enum { Q_REQ = 1, Q_RUN = 2, Q_REW = 4 }; /* a refresh request is queued, a refresh task runs, a re-walk request is queued */
enum { RQ_REFRESH, RQ_NEWP, RQ_GONE, RQ_REWALK };
enum { K_WALK, K_REFRESH };

typedef struct entry {
    struct entry *hnext, *prev, *next;
    wchar_t *key;
    uint32_t hash, len;
    uint64_t size, own;          /* the folder's total, and its own direct files */
    uint32_t walk_ms, last_end;  /* how long its last walk took, and when it ended (ms ticks, low 32 bits) */
    uint32_t tid;                /* the task that owns its PENDING state */
    LONG open_gen;               /* the view generation that counts this folder as open work */
    uint8_t state, have, pinned, drive, incomplete, inlru, qbits;
} entry;

typedef struct task {
    struct task *next, *parent;
    wchar_t *path;
    uint32_t len, id, depth;
    LONG gen, pend;              /* gen 0: never cancelled; pend: unfinished children */
    uint64_t sum, own;
    ULONGLONG t0;
    LONG orphans0;
    uint8_t kind, root, counted, isnew, failed;
} task;

typedef struct req { wchar_t *path; uint32_t len; uint32_t due; uint8_t kind; } req;

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
    size_t cap_entries;
    task *qhead, *qtail;
    volatile LONG gen;
    uint32_t tid;
    wchar_t *parent;
    wchar_t **shown; size_t shown_n, shown_cap;
    long open;
    volatile LONG version;
    volatile LONG orphans;
    bool notify_pending;
    ULONGLONG notify_at, last_notify;
    req *reqs; size_t req_n, req_cap;
    ULONGLONG req_wake;
    drive *drives[26];
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

static int drive_of(const wchar_t *k) { wchar_t c = lc(k[0]); return c >= L'a' && c <= L'z' && k[1] == L':' ? c - L'a' : -1; }
static uint32_t tick32(void) { return (uint32_t)GetTickCount64(); }

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
/* Only entries that may leave are in the LRU list: not shown, not being measured, no request waiting on them. */
static void relist(entry *e) {
    bool want = !e->pinned && e->state != ST_PENDING && !e->qbits;
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

/* A folder left the cache: its parent no longer knows every subfolder (lock held). */
static void note_evicted(const entry *v) {
    size_t i = v->len;
    while (i > 3 && v->key[i - 1] != L'\\') i--;
    size_t pl = i <= 3 ? 3 : i - 1;
    entry *par = v->len > 3 ? find(v->key, pl, hash_of(v->key, pl)) : NULL;
    if (par) par->incomplete = 1; else InterlockedIncrement(&G.orphans);
}

static void evict(void) {
    while ((G.count > G.cap_entries || G.bytes > FSZ_CACHE_BYTES) && G.tail) {
        entry *v = G.tail;
        note_evicted(v);
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
    G.count++; G.bytes += cost_of(e);
    return e;   /* PENDING: not in the LRU list, so the eviction below cannot take it */
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

/* ---- requests (lock held) ---- */

static void req_push(int kind, const wchar_t *p, size_t n, uint32_t due) {
    if (G.req_n == REQ_MAX) return;   /* the caller checks */
    if (G.req_n == G.req_cap) {
        size_t nc = G.req_cap ? G.req_cap * 2 : 256;
        req *nr = realloc(G.reqs, nc * sizeof *nr);
        if (!nr) return;
        G.reqs = nr; G.req_cap = nc;
    }
    wchar_t *c = malloc((n + 1) * sizeof *c);
    if (!c) return;
    memcpy(c, p, n * sizeof *c); c[n] = 0;
    req *r = &G.reqs[G.req_n++];
    r->path = c; r->len = (uint32_t)n; r->kind = (uint8_t)kind; r->due = due;
}

static bool mark_drive(int d);

static void req_clear_all(void) {
    for (size_t i = 0; i < G.req_n; i++) free(G.reqs[i].path);
    G.req_n = 0;
    for (size_t b = 0; b < BUCKETS; b++)
        for (entry *e = G.buckets[b]; e; e = e->hnext) if (e->qbits & (Q_REQ | Q_REW)) { e->qbits &= (uint8_t)~(Q_REQ | Q_REW); relist(e); }
}

/* Too many waiting requests: nothing is trusted on any watched drive any more. */
static void req_overflow(void) {
    req_clear_all();
    for (int d = 0; d < 26; d++) if (G.drives[d]) mark_drive(d);
}

static void req_add(int kind, const wchar_t *p, size_t n, uint32_t due) {
    if (G.req_n == REQ_MAX) { req_overflow(); return; }
    if (!G.req_n) { G.req_wake = GetTickCount64() + DEBOUNCE_MS; SetEvent(G.mgr_ev); }
    req_push(kind, p, n, due);
}

static uint32_t rewalk_due(const entry *e) {
    uint32_t gap = 3 * e->walk_ms; if (gap < REWALK_MIN_MS) gap = REWALK_MIN_MS;
    return e->have ? e->last_end + gap : tick32();
}

static void queue_rewalk(entry *e) {
    if (e->qbits & Q_REW) return;
    e->qbits |= Q_REW; relist(e);
    req_add(RQ_REWALK, e->key, e->len, rewalk_due(e));
}

/* A folder's size is unknown, or all of one drive's: it is walked when shown, or when it is shown now. */
static bool mark_drive(int d) {
    bool any = false;
    for (size_t b = 0; b < BUCKETS; b++)
        for (entry *e = G.buckets[b]; e; e = e->hnext)
            if (e->drive == d && e->state == ST_DONE) {
                e->state = ST_STALE; any = true;
                if (e->pinned) queue_rewalk(e);
            }
    return any;
}

/* ---- changes (lock held) ---- */

static bool any_cached(const wchar_t *p, size_t pl) {
    chain c; size_t len; uint32_t h;
    chain_init(&c, p, pl);
    while (chain_next(&c, &len, &h)) if (find(p, len, h)) return true;
    return false;
}

/* False while a walk is running at or above the path: the change is looked at when it has finished. */
static bool chain_ready(const wchar_t *p, size_t pl, bool include_self) {
    chain c; size_t len; uint32_t h;
    chain_init(&c, p, pl);
    while (chain_next(&c, &len, &h)) {
        if (!include_self && len == pl) break;
        entry *e = find(p, len, h);
        if (e && e->state == ST_PENDING) return false;
    }
    return true;
}

/* Adds a difference to the cached folders above a path (not the path itself). */
static void apply_up(const wchar_t *p, size_t pl, int64_t delta) {
    chain c; size_t len; uint32_t h; bool shown = false;
    if (!delta) return;
    chain_init(&c, p, pl);
    while (chain_next(&c, &len, &h)) {
        if (len == pl) break;
        entry *e = find(p, len, h);
        if (!e || !e->have || e->state == ST_PENDING) continue;
        if (delta < 0 && (uint64_t)(-delta) > e->size) e->size = 0; else e->size += (uint64_t)delta;
        if (e->pinned) shown = true;
    }
    if (shown) bump();
}

static void parent_of(const wchar_t *p, size_t pl, size_t *xl) {
    size_t i = pl;
    while (i > 3 && p[i - 1] != L'\\') i--;
    *xl = i <= 4 ? 3 : i - 1;
}

/* The nearest cached folder above a path loses trust: it is walked again, rate limited. */
static void fallback_rewalk(const wchar_t *p, size_t pl) {
    chain c; size_t len; uint32_t h; entry *best = NULL;
    chain_init(&c, p, pl);
    while (chain_next(&c, &len, &h)) { if (len == pl) break; entry *e = find(p, len, h); if (e) best = e; }
    if (!best) return;
    if (best->state == ST_DONE) best->state = ST_STALE;
    if (best->state == ST_STALE) queue_rewalk(best);
}

typedef struct fni { DWORD next, action, name_len; WCHAR name[1]; } fni;

/* One change: remember what has to be looked at again. */
static void note_change(DWORD action, const wchar_t *p, size_t pl) {
    size_t xl;
    if (pl <= 3 || !any_cached(p, pl)) return;
    parent_of(p, pl, &xl);
    if (action == FILE_ACTION_ADDED || action == FILE_ACTION_RENAMED_NEW_NAME) {
        if (G.req_n && G.reqs[G.req_n - 1].kind == RQ_NEWP && G.reqs[G.req_n - 1].len == pl && keq(G.reqs[G.req_n - 1].path, p, pl)) return;
        req_add(RQ_NEWP, p, pl, 0);
    } else if (action == FILE_ACTION_REMOVED || action == FILE_ACTION_RENAMED_OLD_NAME) {
        if (G.req_n && G.reqs[G.req_n - 1].kind == RQ_GONE && G.reqs[G.req_n - 1].len == pl && keq(G.reqs[G.req_n - 1].path, p, pl)) return;
        req_add(RQ_GONE, p, pl, 0);
    } else {
        entry *x = find(p, xl, hash_of(p, xl));
        if (x) { if (x->qbits & Q_REQ) return; x->qbits |= Q_REQ; relist(x); }
        else if (G.req_n && G.reqs[G.req_n - 1].kind == RQ_REFRESH && G.reqs[G.req_n - 1].len == xl && keq(G.reqs[G.req_n - 1].path, p, xl)) return;
        req_add(RQ_REFRESH, p, xl, 0);
    }
}

static void process_batch(int d, bool overflow, wchar_t *scratch) {
    wchar_t root[4] = { (wchar_t)(L'A' + d), L':', L'\\', 0 };
    EnterCriticalSection(&G.cs);
    if (overflow) {
        req_clear_all();
        mark_drive(d);
    } else {
        const BYTE *b = (const BYTE *)G.drives[d]->buf;
        for (const fni *f = (const fni *)b;;) {
            size_t nl = f->name_len / 2;
            if (nl && nl < PATH_W - 4) {
                memcpy(scratch, root, 3 * sizeof *scratch); memcpy(scratch + 3, f->name, nl * sizeof *scratch); scratch[3 + nl] = 0;
                size_t pl = canon(scratch, 3 + nl);
                note_change(f->action, scratch, pl);
            }
            if (!f->next) break;
            f = (const fni *)((const BYTE *)f + f->next);
        }
    }
    LeaveCriticalSection(&G.cs);
}

static unsigned __stdcall watch_thread(void *arg) {
    int d = (int)(INT_PTR)arg;
    drive *w = G.drives[d];
    wchar_t *scratch = malloc(PATH_W * sizeof *scratch);
    for (; scratch;) {
        HANDLE hs[2] = { w->ev, G.stop_ev };
        if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) != WAIT_OBJECT_0) break;
        DWORD n = 0;
        BOOL ok = GetOverlappedResult(w->h, &w->ov, &n, FALSE);
        bool overflow = !ok || n == 0;
        if (!ok && GetLastError() != ERROR_NOTIFY_ENUM_DIR) { w->broken = true; }
        process_batch(d, overflow, scratch);
        if (w->broken) break;
        ResetEvent(w->ev);
        if (!ReadDirectoryChangesW(w->h, w->buf, sizeof w->buf, TRUE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE,
                                   NULL, &w->ov, NULL)) { w->broken = true; process_batch(d, true, scratch); break; }
        if (WaitForSingleObject(G.stop_ev, 100) == WAIT_OBJECT_0) break;   /* a busy drive is read ten times a second */
    }
    free(scratch);
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

/* The shown folder's (or a background) walk: the entry becomes PENDING and its root task is queued (lock held). */
static void enqueue_root(entry *e, LONG gen, bool counted, bool isnew) {
    task *t = new_task(e->key, e->len, NULL, 0);
    if (!t) { e->state = ST_STALE; return; }
    t->root = 1; t->gen = gen; t->counted = counted; t->isnew = isnew; t->kind = K_WALK;
    t->id = e->tid = ++G.tid;
    e->state = ST_PENDING; e->qbits &= (uint8_t)~Q_REW; e->incomplete = 0;
    if (counted) { G.open++; e->open_gen = G.gen; }
    relist(e);
    push_back(t);
    ReleaseSemaphore(G.sem, 1, NULL);
}

static void enqueue_refresh(entry *e) {
    task *t = new_task(e->key, e->len, NULL, 0);
    if (!t) { e->qbits &= (uint8_t)~Q_RUN; relist(e); return; }
    t->root = 1; t->kind = K_REFRESH;
    push_back(t);
    ReleaseSemaphore(G.sem, 1, NULL);
}

/* A finished folder goes into the cache (lock held). */
static void store_dir(task *t, uint64_t total) {
    uint32_t h = hash_of(t->path, t->len);
    entry *e = find(t->path, t->len, h);
    if (t->root) {
        if (!e || e->tid != t->id) return;   /* another task owns it */
    } else {
        if (e && e->state == ST_PENDING) return;
        if (!e) { e = add(t->path, t->len, h); if (!e) return; }
    }
    bool had = e->have; uint64_t old = e->size;
    e->size = total; e->own = t->own; e->have = 1; e->state = ST_DONE;
    e->incomplete = (uint8_t)(G.orphans != t->orphans0 || (t->root && e->incomplete));
    uint32_t now = tick32();
    e->walk_ms = (uint32_t)(GetTickCount64() - t->t0); e->last_end = now;
    relist(e); touch(e);
    if (t->root) {
        if (had) apply_up(t->path, t->len, (int64_t)(total - old));
        else if (t->isnew) apply_up(t->path, t->len, (int64_t)total);
        InterlockedIncrement(&G.version); request_notify();
    }
    evict();
}

/* A root that will not finish: its entry goes back to what it was (lock held). */
static void drop_root(task *t) {
    entry *e = find(t->path, t->len, hash_of(t->path, t->len));
    if (!e || e->state != ST_PENDING || e->tid != t->id) return;
    if (e->have) { e->state = ST_STALE; relist(e); } else erase(e);
}

/* t has no unfinished children: its total is final, and so may be its parent's (lock held). */
static void finalize(task *t) {
    for (;;) {
        task *par = t->parent;
        uint64_t total = t->sum;
        bool failed = t->failed;
        if (t->kind == K_WALK) {
            if (!failed) store_dir(t, total); else if (t->root) drop_root(t);
            if (t->root && t->counted && t->gen == G.gen) { G.open--; request_notify(); }
        }
        free(t->path); free(t);
        if (!par) break;
        par->sum += total; par->failed = (uint8_t)(par->failed | failed);
        if (--par->pend) break;
        t = par;
    }
}

/* A refresh: the folder's own files were read again (lock held). */
static void finish_refresh(task *t, listing *L, wchar_t *scratch) {
    entry *e = find(t->path, t->len, hash_of(t->path, t->len));
    if (!e) return;
    e->qbits &= (uint8_t)~Q_RUN;
    if (e->state != ST_DONE) { relist(e); return; }
    int64_t delta = (int64_t)(L->own - e->own);
    bool shown = e->pinned;
    if (delta) {
        e->own = L->own;
        if (delta < 0 && (uint64_t)(-delta) > e->size) e->size = 0; else e->size += (uint64_t)delta;
        apply_up(t->path, t->len, delta);
    }
    e->last_end = tick32();
    for (size_t at = 0; at < L->nlen; ) {
        size_t nl = wcslen(L->names + at);
        at += nl + 1;
        if (t->len + 1 + nl + 1 >= PATH_W) continue;
        size_t k = t->len;
        memcpy(scratch, t->path, k * sizeof *scratch);
        if (scratch[k - 1] != L'\\') scratch[k++] = L'\\';
        memcpy(scratch + k, L->names + at - nl - 1, nl * sizeof *scratch); k += nl; scratch[k] = 0;
        uint32_t ch = hash_of(scratch, k);
        if (find(scratch, k, ch)) continue;
        if (e->incomplete) { e->state = ST_STALE; queue_rewalk(e); break; }   /* a subfolder we may have held: walk this one again */
        entry *c = add(scratch, k, ch);
        if (c) enqueue_root(c, 0, false, true);   /* a new subfolder: walked on its own, its total moves up when done */
    }
    relist(e);
    if (delta && shown) bump();
}

static void run_task(task *t, BYTE *buf, wchar_t *scratch) {
    listing L = { 0 };
    t->t0 = GetTickCount64();
    t->orphans0 = G.orphans;
    read_dir(t->path, t->len, buf, &L, scratch);
    if (t->kind == K_REFRESH) {
        EnterCriticalSection(&G.cs);
        finish_refresh(t, &L, scratch);
        LeaveCriticalSection(&G.cs);
        free(t->path); free(t); free(L.names);
        return;
    }
    task *first = NULL, *last = NULL; LONG n = 0;
    if (t->depth < DEPTH_MAX) {
        for (size_t at = 0; at < L.nlen; ) {
            size_t nl = wcslen(L.names + at);
            if (t->len + 1 + nl + 1 < PATH_W) {
                task *c = new_task(t->path, t->len, L.names + at, nl);
                if (c) {
                    c->parent = t; c->gen = t->gen; c->depth = t->depth + 1; c->kind = K_WALK;
                    if (last) last->next = c; else first = c;
                    last = c; n++;
                }
            }
            at += nl + 1;
        }
    }
    free(L.names);
    EnterCriticalSection(&G.cs);
    t->own = t->sum = L.own;
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
        bool dead = t && t->gen && t->gen != G.gen;   /* an older view's work */
        if (dead) { t->failed = 1; t->pend = 0; finalize(t); t = NULL; }
        LeaveCriticalSection(&G.cs);
        if (t) run_task(t, buf, scratch);
    }
    free(buf); free(scratch);
    return 0;
}

/* Queued tasks of an older view are dropped by the workers when they pop them; running ones stop at the next folder. */

/* ---- the manager: wake-ups for the window and the waiting requests ---- */

/* Returns true when the request is used up (lock held). */
static bool req_do(req *r) {
    const wchar_t *p = r->path; size_t pl = r->len;
    switch (r->kind) {
    case RQ_REWALK: {
        entry *e = find(p, pl, hash_of(p, pl));
        if (!e) return true;
        if (e->state != ST_STALE) { e->qbits &= (uint8_t)~Q_REW; relist(e); return true; }
        if ((int32_t)(tick32() - r->due) < 0) return false;
        if (!chain_ready(p, pl, false)) return false;
        enqueue_root(e, 0, false, false);
        return true;
    }
    case RQ_REFRESH: {
        if (!chain_ready(p, pl, true)) return false;
        entry *e = find(p, pl, hash_of(p, pl));
        if (!e) {
            wchar_t lp[600];
            if (pl < 580) {   /* a folder that is gone needs nothing: its removal is its own request */
                memcpy(lp, L"\\?\\", 4 * sizeof *lp); memcpy(lp + 4, p, pl * sizeof *lp); lp[4 + pl] = 0;
                if (GetFileAttributesW(lp) == INVALID_FILE_ATTRIBUTES) return true;
            }
            /* It exists but is not held (a new subfolder written into at once): its parent reads again and walks it as
               a new root. Only when the parent is not held or not complete is the nearest folder above walked again. */
            size_t xl; parent_of(p, pl, &xl);
            entry *x = find(p, xl, hash_of(p, xl));
            if (x && !x->incomplete) { if (!(x->qbits & Q_REQ)) { x->qbits |= Q_REQ; relist(x); req_push(RQ_REFRESH, p, xl, 0); } }
            else fallback_rewalk(p, pl);
            return true;
        }
        if (e->state == ST_STALE) { e->qbits &= (uint8_t)~Q_REQ; queue_rewalk(e); relist(e); return true; }
        if (e->qbits & Q_RUN) return false;
        e->qbits = (uint8_t)((e->qbits & ~Q_REQ) | Q_RUN);
        relist(e);
        enqueue_refresh(e);
        return true;
    }
    case RQ_NEWP: {   /* something appeared at p: its parent reads again and walks a subfolder it does not know */
        if (!chain_ready(p, pl, true)) return false;
        size_t xl; parent_of(p, pl, &xl);
        entry *x = find(p, xl, hash_of(p, xl));
        if (x) { if (!(x->qbits & Q_REQ)) { x->qbits |= Q_REQ; relist(x); req_push(RQ_REFRESH, p, xl, 0); } }
        else req_push(RQ_REFRESH, p, xl, 0);   /* the parent is not held either (a new folder): its own parent reads again */
        return true;
    }
    case RQ_GONE: {   /* something at p is gone: a cached folder is subtracted; a file only changes its parent's own size */
        if (!chain_ready(p, pl, true)) return false;
        entry *e = find(p, pl, hash_of(p, pl));
        if (e) { if (e->have) apply_up(p, pl, -(int64_t)e->size); erase(e); return true; }
        size_t xl; parent_of(p, pl, &xl);
        entry *x = find(p, xl, hash_of(p, xl));
        if (x && !x->incomplete) { if (!(x->qbits & Q_REQ)) { x->qbits |= Q_REQ; relist(x); req_push(RQ_REFRESH, p, xl, 0); } }
        else fallback_rewalk(p, pl);   /* it may have been a folder we no longer hold */
        return true;
    }
    }
    return true;
}

static void process_reqs(void) {
    req *old = G.reqs; size_t n = G.req_n;
    G.reqs = NULL; G.req_n = 0; G.req_cap = 0;
    ULONGLONG now = GetTickCount64();
    for (size_t i = 0; i < n; i++) {
        if (req_do(&old[i])) { free(old[i].path); continue; }
        if (G.req_n == REQ_MAX) { free(old[i].path); continue; }
        req_push(old[i].kind, old[i].path, old[i].len, old[i].due);
        free(old[i].path);
    }
    free(old);
    if (G.req_n) G.req_wake = now + DEBOUNCE_MS;   /* what waits (a busy walk above it, or a re-walk not due yet) is looked at again */
}

static unsigned __stdcall manager_thread(void *arg) {
    (void)arg;
    for (;;) {
        DWORD wait = INFINITE;
        EnterCriticalSection(&G.cs);
        ULONGLONG now = GetTickCount64();
        if (G.notify_pending) wait = G.notify_at > now ? (DWORD)(G.notify_at - now) : 0;
        if (G.req_n) { DWORD r = G.req_wake > now ? (DWORD)(G.req_wake - now) : 0; if (r < wait) wait = r; }
        LeaveCriticalSection(&G.cs);
        WaitForSingleObject(G.mgr_ev, wait);
        if (G.stopping) break;
        bool fire = false;
        EnterCriticalSection(&G.cs);
        now = GetTickCount64();
        if (G.notify_pending && now >= G.notify_at) { G.notify_pending = false; G.last_notify = now; fire = true; }
        if (G.req_n && now >= G.req_wake) process_reqs();
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
    G.cap_entries = FSZ_CACHE_MAX;
    G.nt = (nt_query_t)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryDirectoryFile");
}

static void ensure_threads(void) {
    if (G.started || !G.nt) return;
    G.buckets = calloc(BUCKETS, sizeof *G.buckets);
    G.sem = CreateSemaphoreW(NULL, 0, 1 << 24, NULL);
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
        if (e) enqueue_root(e, G.gen, true, false);
    } else if (e->state == ST_STALE || (e->state == ST_DONE && dr->broken) || (e->state == ST_PENDING && !e->have && e->open_gen != G.gen)) {
        enqueue_root(e, G.gen, true, false);
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

bool fsz_stale(const char *path) {
    wchar_t w[2200];
    if (!G.started) return false;
    int k = to_wide(path, w, 2200);
    if (k <= 0) return false;
    size_t n = canon(w, (size_t)k);
    EnterCriticalSection(&G.cs);
    entry *e = find(w, n, hash_of(w, n));
    bool r = e && (e->state == ST_STALE || e->qbits || (e->state == ST_PENDING && e->have));
    LeaveCriticalSection(&G.cs);
    return r;
}

void fsz_test_delay(unsigned ms) { G.delay_ms = ms; }
void fsz_test_cap(unsigned max_entries) { G.cap_entries = max_entries ? max_entries : FSZ_CACHE_MAX; }
unsigned fsz_test_reads(void) { return (unsigned)InterlockedCompareExchange(&G.reads, 0, 0); }
unsigned fsz_test_entries(unsigned long long *bytes) {   /* checks only: entries held and the bytes their keys and records take */
    EnterCriticalSection(&G.cs);
    unsigned n = (unsigned)G.count; if (bytes) *bytes = (unsigned long long)G.bytes;
    LeaveCriticalSection(&G.cs);
    return n;
}
unsigned fsz_test_peak(void) { return (unsigned)InterlockedExchange(&G.peak, G.active); }   /* and starts counting again */
