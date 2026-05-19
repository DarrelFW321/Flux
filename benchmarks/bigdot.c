// Big-dot microbenchmark — hand-written C side.
// Matches benchmarks/bigdot.fl: 512-element vectors × 50_000 iterations.
#include <stdio.h>

#define N 512

static double dotN(const double *a, const double *b) {
    double s = 0.0;
    for (int j = 0; j < N; ++j) s += a[j] * b[j];
    return s;
}

int main(void) {
    double a[N], b[N];
    for (int j = 0; j < N; ++j) { a[j] = 1.0; b[j] = 2.0; }
    double total = 0.0;
    for (int i = 0; i < 50000; ++i) {
        a[0] = total / 1.0e9;
        total += dotN(a, b);
    }
    printf("%g\n", total);
    return 0;
}
