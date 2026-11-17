#ifndef DFM_IMGDEC_H
#define DFM_IMGDEC_H
/* One picture decoded by Windows (WIC) on its own thread, scaled down so it fits box_w x box_h pixels (never scaled
   up), as straight RGBA. The window polls; it never waits. Pixels are borrowed until imgdec_release. */
#include <stdbool.h>
typedef struct imgdec_job imgdec_job;
typedef struct imgdec_result {
    const unsigned char *rgba;   /* w * h * 4, top-down, straight alpha */
    int w, h;                    /* the decoded size */
    int src_w, src_h;            /* the picture's own size in pixels */
    bool has_alpha;              /* some pixel is not opaque */
} imgdec_result;
imgdec_job *imgdec_start(const char *path_utf8, int box_w, int box_h);
int imgdec_poll(imgdec_job *job, imgdec_result *out);   /* 0 still running, 1 done (out filled), -1 failed */
void imgdec_release(imgdec_job *job);                   /* the window is done with it; safe while it runs */
#endif
