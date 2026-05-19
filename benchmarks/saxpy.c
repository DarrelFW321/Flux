// SAXPY-style microbenchmark — hand-written C side.
// Matches benchmarks/saxpy.fl line for line so the comparison is fair.
#include <stdio.h>

#define N 64

static double saxpy_sum(const double *x, const double *y) {
    double s = 0.0;
    for (int j = 0; j < N; ++j) s += 2.0 * x[j] + y[j];
    return s;
}

int main(void) {
    double x[N], y[N];
    for (int j = 0; j < N; ++j) { x[j] = 1.0; y[j] = 2.0; }
    double total = 0.0;
    for (int i = 0; i < 200000; ++i) {
        x[0] = total / 1.0e9;
        total += saxpy_sum(x, y);
    }
    printf("%g\n", total);
    return 0;
}
