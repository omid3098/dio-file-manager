#ifndef DFM_THUMBS_H
#define DFM_THUMBS_H
/* One UI thread owns this cache. Each frame: request every visible UTF-8 path,
   get ready images and upload them immediately, then drop_unrequested(). Pass
   the listing's FILETIME as mtime; size is the longest edge, 1..1024 pixels.
   Frame calls never wait for the worker or touch disk. A false return means
   busy/full/invalid: retry next frame. get returns MISSING until accepted.
   Pixels are borrowed, top-down straight RGBA, valid until the next request,
   drop_unrequested or destroy. Windows' installed handlers supply video/PDF.
   Destroy joins the worker and may wait for an active Shell extraction. */
#include <stddef.h>
#include <stdint.h>

#define THUMBS_MEMORY_CAP ((size_t)64 * 1024 * 1024)
#define THUMBS_QUEUE_CAP 32

typedef struct thumbs_cache thumbs_cache;
typedef struct thumbs_image {
    const unsigned char *rgba;
    int width, height;
} thumbs_image;
typedef enum thumbs_result {
    THUMBS_MISSING, THUMBS_PENDING, THUMBS_NONE, THUMBS_READY
} thumbs_result;
typedef struct thumbs_stats {
    size_t bytes; /* Owned metadata, paths and pixels, including worker reservations.
                    Excludes Windows/COM's temporary bitmap and handler memory. */
    unsigned cached, queued, running;
} thumbs_stats;

thumbs_cache *thumbs_create(void);
void thumbs_destroy(thumbs_cache *cache);
int thumbs_request(thumbs_cache *cache, const char *path, int size, uint64_t mtime);
thumbs_result thumbs_get(thumbs_cache *cache, const char *path, int size,
                         uint64_t mtime, thumbs_image *out);
int thumbs_drop_unrequested(thumbs_cache *cache);
int thumbs_get_stats(thumbs_cache *cache, thumbs_stats *out);
#endif
