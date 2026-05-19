"""ReLU activation microbenchmark — NumPy side.

Matches benchmarks/relu.fl and benchmarks/relu.c element for element.
NumPy uses `np.maximum(0.0, x)` which is the vectorised relu primitive.
"""
import numpy as np


def bench() -> float:
    x = np.array([
         1.0, -1.0,  2.0, -2.0,  3.0, -3.0,  4.0, -4.0,
         5.0, -5.0,  6.0, -6.0,  7.0, -7.0,  8.0, -8.0,
         1.5, -1.5,  2.5, -2.5,  3.5, -3.5,  4.5, -4.5,
         5.5, -5.5,  6.5, -6.5,  7.5, -7.5,  8.5, -8.5,
         0.5, -0.5,  1.0, -1.0,  1.5, -1.5,  2.0, -2.0,
         2.5, -2.5,  3.0, -3.0,  3.5, -3.5,  4.0, -4.0,
         4.5, -4.5,  5.0, -5.0,  5.5, -5.5,  6.0, -6.0,
         6.5, -6.5,  7.0, -7.0,  7.5, -7.5,  8.0, -8.0,
    ])
    total = 0.0
    for _ in range(200_000):
        x[0] = total / 1.0e9
        total += float(np.sum(np.maximum(0.0, x)))
    return total


if __name__ == "__main__":
    print(f"{bench():g}")
