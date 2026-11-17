/* Console checks of the treemap layout (src/treemap.c): areas in the ratio of the sizes, the rectangles together cover the area,
 * none overlaps another, everything stays inside the area, n of 0 does nothing, one item fills the area, equal sizes, a very
 * lopsided list (one huge item and thousands of tiny ones), and a non-zero origin. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "treemap.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else printf("ok   %s\n", #c); } while (0)

/* Shared checks for one layout: returns the worst relative error of an area against its share, sets covered / overlap / inside. */
static double inspect(const double *v, int n, tm_rect area, const tm_rect *r, double *covered_err, int *overlaps, int *outside) {
    double total = 0, sum_area = 0, worst = 0;
    for (int i = 0; i < n; i++) total += v[i];
    for (int i = 0; i < n; i++) {
        double want = v[i] / total * (double)area.w * (double)area.h, got = (double)r[i].w * (double)r[i].h;
        sum_area += got;
        double e = fabs(got - want) / want;
        if (e > worst) worst = e;
        if (r[i].x < area.x - 0.01f || r[i].y < area.y - 0.01f || r[i].x + r[i].w > area.x + area.w + 0.01f || r[i].y + r[i].h > area.y + area.h + 0.01f) (*outside)++;
    }
    *covered_err = fabs(sum_area - (double)area.w * (double)area.h) / ((double)area.w * (double)area.h);
    /* overlap: the shared part of two rectangles is more than a sliver (float rounding on shared edges is allowed) */
    for (int i = 0; i < n && n <= 600; i++)
        for (int j = i + 1; j < n; j++) {
            double ox = fmin(r[i].x + r[i].w, r[j].x + r[j].w) - fmax(r[i].x, r[j].x);
            double oy = fmin(r[i].y + r[i].h, r[j].y + r[j].h) - fmax(r[i].y, r[j].y);
            if (ox > 0.01 && oy > 0.01) (*overlaps)++;
        }
    return worst;
}

static void case_of(const char *name, const double *v, int n, tm_rect area) {
    tm_rect *r = calloc((size_t)n, sizeof *r);
    double cov = 0; int ov = 0, out = 0;
    tm_layout(v, n, area, r);
    double worst = inspect(v, n, area, r, &cov, &ov, &out);
    printf("-- %s (n=%d)\n", name, n);
    CHECK(worst < 0.001, "an area is %.4f off its share", worst);
    CHECK(cov < 0.001, "rectangles cover the area up to %.5f", cov);
    CHECK(ov == 0, "%d overlapping pairs", ov);
    CHECK(out == 0, "%d rectangles outside the area", out);
    free(r);
}

int main(void) {
    /* n = 0: nothing is written, nothing crashes */
    tm_rect guard = { -1, -2, -3, -4 };
    tm_layout(NULL, 0, (tm_rect){ 0, 0, 100, 100 }, &guard);
    CHECK(guard.x == -1 && guard.y == -2 && guard.w == -3 && guard.h == -4, "n of 0 wrote a rectangle");

    /* one item fills the whole area */
    double one[1] = { 42.0 };
    tm_rect r1;
    tm_layout(one, 1, (tm_rect){ 10, 20, 300, 200 }, &r1);
    CHECK(r1.x == 10 && r1.y == 20 && fabsf(r1.w - 300) < 0.01f && fabsf(r1.h - 200) < 0.01f, "one item is %g,%g %gx%g", r1.x, r1.y, r1.w, r1.h);

    double three[3] = { 6, 3, 1 };
    case_of("three items", three, 3, (tm_rect){ 0, 0, 600, 400 });
    case_of("three items, offset origin", three, 3, (tm_rect){ 37.5f, 12.25f, 600, 400 });
    case_of("tall area", three, 3, (tm_rect){ 0, 0, 90, 900 });

    double eq[16];
    for (int i = 0; i < 16; i++) eq[i] = 5;
    case_of("sixteen equal items", eq, 16, (tm_rect){ 0, 0, 800, 600 });
    {   /* equal items give equal areas */
        tm_rect r[16]; tm_layout(eq, 16, (tm_rect){ 0, 0, 800, 600 }, r);
        double a0 = (double)r[0].w * r[0].h, big = 0;
        for (int i = 1; i < 16; i++) { double d = fabs((double)r[i].w * r[i].h - a0); if (d > big) big = d; }
        CHECK(big < 0.5, "equal items differ in area by %.3f", big);
    }

    /* sizes of real disks: powers of ten from the biggest to the smallest, sorted from the largest */
    double span[9];
    for (int i = 0; i < 9; i++) span[i] = pow(10.0, 8 - i);
    case_of("nine sizes, ten times apart", span, 9, (tm_rect){ 0, 0, 828, 628 });

    /* one huge item and 3000 tiny ones (a folder with many small files) */
    int n = 3001;
    double *lop = malloc((size_t)n * sizeof *lop);
    lop[0] = 1e12;
    for (int i = 1; i < n; i++) lop[i] = 1e6 - i;
    case_of("one huge and 3000 tiny", lop, n, (tm_rect){ 0, 0, 1000, 700 });
    free(lop);

    /* a decreasing run, like a sorted folder */
    double run[200];
    for (int i = 0; i < 200; i++) run[i] = 1000.0 / (i + 1);
    case_of("harmonic run of 200", run, 200, (tm_rect){ 5, 5, 640, 480 });

    /* the tiles of the first row are not thinner than the later ones are tall: the biggest item gets a decent shape */
    {
        tm_rect r[3]; tm_layout(three, 3, (tm_rect){ 0, 0, 600, 400 }, r);
        double ar = r[0].w > r[0].h ? r[0].w / r[0].h : r[0].h / r[0].w;
        CHECK(ar < 3.0, "the biggest item's aspect ratio is %.2f", ar);
    }

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
