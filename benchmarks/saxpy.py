"""SAXPY-style microbenchmark — NumPy side.

Matches benchmarks/saxpy.fl and benchmarks/saxpy.c: 64-element vectors,
200_000 iterations, the same `total / 1e9` perturbation on x[0].
"""
import numpy as np


def bench() -> float:
    x = np.ones(64)
    y = np.full(64, 2.0)
    total = 0.0
    for _ in range(200_000):
        x[0] = total / 1.0e9
        total += float(np.sum(2.0 * x + y))
    return total


if __name__ == "__main__":
    print(f"{bench():g}")
