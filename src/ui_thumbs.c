/* Pictures of files in the grid: asks the thumbnail cache (thumbs.h) for the cells on screen, uploads each
 * finished picture once as a texture, and keeps a small least-recently-used set of textures. */
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "thumbs.h"

#define MAX_TEX 160
#define UPLOADS_PER_FRAME 6

typedef struct tex_ent {
    char *path;
    uint64_t mtime, used;
    int px, w, h;
    cui_texture tex;     /* 0: the shell has no picture for this file */
} tex_ent;

static tex_ent T[MAX_TEX];
static int nt;
static thumbs_cache *cache;
static uint64_t tick;
static int uploads;

bool ui_thumb_wanted(const fs_entry *e, const char *name) {
    if (e->flags & FS_DIR) return false;
    if (e->kind == FS_KIND_IMAGE || e->kind == FS_KIND_VIDEO) return true;
    return _stricmp(fs_ext_of(name), "pdf") == 0;
}

/* Request the displayed edge, rather than always loading a large picture. */
int ui_thumb_px(float icon) { return (int)(icon * cui_dpi_scale(A.ui) + 0.5f); }

void ui_thumb_begin(void) {
    tick++;
    uploads = 0;
    if (!cache) cache = thumbs_create();
}

static void drop_ent(int i) {
    if (T[i].tex) cui_image_destroy(A.ui, T[i].tex);
    free(T[i].path);
    T[i] = T[--nt];
}

static void add_ent(const char *path, uint64_t mtime, int px, cui_texture tex, int w, int h) {
    if (nt == MAX_TEX) {   /* the least recently used one, never one used this frame */
        int oldest = -1;
        for (int i = 0; i < nt; i++) if (T[i].used < tick && (oldest < 0 || T[i].used < T[oldest].used)) oldest = i;
        if (oldest < 0) { if (tex) cui_image_destroy(A.ui, tex); return; }
        drop_ent(oldest);
    }
    char *copy = _strdup(path);
    if (!copy) { if (tex) cui_image_destroy(A.ui, tex); return; }
    T[nt++] = (tex_ent){ copy, mtime, tick, px, w, h, tex };
}

/* True when the file has a picture; fills out. Otherwise the caller draws the page icon. */
bool ui_thumb_lookup(const char *path, uint64_t mtime, int px, ui_thumb *out) {
    for (int i = 0; i < nt; i++) {
        if (T[i].mtime != mtime || T[i].px != px || strcmp(T[i].path, path)) continue;
        T[i].used = tick;
        if (!T[i].tex) return false;
        out->tex = T[i].tex; out->w = T[i].w; out->h = T[i].h;
        return true;
    }
    if (!cache) return false;
    thumbs_request(cache, path, px, mtime);
    thumbs_image im;
    thumbs_result r = thumbs_get(cache, path, px, mtime, &im);
    if (r == THUMBS_PENDING || r == THUMBS_MISSING || r == THUMBS_READY) cui_request_wakeup(A.ui, 0.03f);
    if (r == THUMBS_NONE) add_ent(path, mtime, px, 0, 0, 0);
    else if (r == THUMBS_READY && uploads < UPLOADS_PER_FRAME) {
        cui_texture t = cui_image_create(A.ui, im.width, im.height, im.rgba);
        uploads++;
        if (t) {
            add_ent(path, mtime, px, t, im.width, im.height);
            out->tex = t; out->w = im.width; out->h = im.height;
            return true;
        }
    }
    return false;
}

/* Cells that scrolled away are no longer requested: the cache drops them. */
void ui_thumb_end(void) {
    if (cache) thumbs_drop_unrequested(cache);
}
