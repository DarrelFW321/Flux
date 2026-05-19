// Dot-product microbenchmark — hand-written C side.
// Matches benchmarks/dot.fl line for line so the comparison is fair.
#include <stdio.h>

static double dot8(const double *a, const double *b) {
    double s = 0.0;
    for (int j = 0; j < 8; ++j) s += a[j] * b[j];
    return s;
}

int main(void) {
    double a[8] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
    double b[8] = {8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0};
    double total = 0.0;
    for (int i = 0; i < 1000000; ++i) {
        a[0] = total / 1.0e9;
        total += dot8(a, b);
    }
    printf("%g\n", total);
    return 0;
}
