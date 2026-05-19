// ReLU activation microbenchmark — hand-written C side.
// Matches benchmarks/relu.fl line for line so the comparison is fair.
#include <stdio.h>

#define N 64

static double relu_step(double v) {
    return v > 0.0 ? v : 0.0;
}

int main(void) {
    double x[N] = {
         1.0, -1.0,  2.0, -2.0,  3.0, -3.0,  4.0, -4.0,
         5.0, -5.0,  6.0, -6.0,  7.0, -7.0,  8.0, -8.0,
         1.5, -1.5,  2.5, -2.5,  3.5, -3.5,  4.5, -4.5,
         5.5, -5.5,  6.5, -6.5,  7.5, -7.5,  8.5, -8.5,
         0.5, -0.5,  1.0, -1.0,  1.5, -1.5,  2.0, -2.0,
         2.5, -2.5,  3.0, -3.0,  3.5, -3.5,  4.0, -4.0,
         4.5, -4.5,  5.0, -5.0,  5.5, -5.5,  6.0, -6.0,
         6.5, -6.5,  7.0, -7.0,  7.5, -7.5,  8.0, -8.0
    };
    double total = 0.0;
    for (int i = 0; i < 200000; ++i) {
        x[0] = total / 1.0e9;
        double inner = 0.0;
        for (int j = 0; j < N; ++j) inner += relu_step(x[j]);
        total += inner;
    }
    printf("%g\n", total);
    return 0;
}
