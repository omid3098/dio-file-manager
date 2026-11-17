/* Squarified treemap layout (Bruls, Huizing, van Wijk). No CUI, no Windows: the disk tree view's geometry, tested alone. */
#ifndef DFM_TREEMAP_H
#define DFM_TREEMAP_H
typedef struct tm_rect { float x, y, w, h; } tm_rect;
/* v[0..n) are sizes sorted from largest to smallest, all above 0. Fills out[0..n) with rectangles inside `area` whose areas
 * are in the ratio of the sizes and that together cover the area. n of 0 does nothing. */
void tm_layout(const double *v, int n, tm_rect area, tm_rect *out);
#endif
