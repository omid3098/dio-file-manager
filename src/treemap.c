#include <math.h>
#include <stdlib.h>
#include "treemap.h"

/* The worst aspect ratio of a row of items a..b-1 (areas, sorted descending) laid along a side of length `side`. */
static double worst(const double *a, int from, int to, double sum, double side) {
    double s2 = sum * sum, w2 = side * side;
    double r1 = w2 * a[from] / s2, r2 = s2 / (w2 * a[to - 1]);
    return r1 > r2 ? r1 : r2;
}

void tm_layout(const double *v, int n, tm_rect area, tm_rect *out) {
    if (n <= 0) return;
    double total = 0;
    for (int i = 0; i < n; i++) total += v[i];
    double *a = malloc((size_t)n * sizeof *a);
    if (!a) return;
    double scale = (double)area.w * (double)area.h / total;
    for (int i = 0; i < n; i++) a[i] = v[i] * scale;
    double x = area.x, y = area.y, w = area.w, h = area.h;
    int i = 0;
    while (i < n) {
        double side = w < h ? w : h;
        if (side <= 0) { for (; i < n; i++) out[i] = (tm_rect){ (float)x, (float)y, 0, 0 }; break; }
        int j = i + 1;
        double sum = a[i], cur = worst(a, i, j, sum, side);
        while (j < n) {
            double s2 = sum + a[j], nw = worst(a, i, j + 1, s2, side);
            if (nw > cur) break;
            sum = s2; cur = nw; j++;
        }
        double thick = sum / side;   /* the row's depth across the short side */
        double pos = 0;
        for (int k = i; k < j; k++) {
            double len = a[k] / thick;
            if (w >= h) out[k] = (tm_rect){ (float)x, (float)(y + pos), (float)thick, (float)len };   /* a column on the left */
            else out[k] = (tm_rect){ (float)(x + pos), (float)y, (float)len, (float)thick };           /* a row on top */
            pos += len;
        }
        if (w >= h) { x += thick; w -= thick; } else { y += thick; h -= thick; }
        i = j;
    }
    free(a);
}
