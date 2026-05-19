"""Dot-product microbenchmark — NumPy side.

Matches benchmarks/dot.fl and benchmarks/dot.c: same array values, same
1_000_000-iteration outer loop, same perturbation each iteration.

Notes on what this measures:
  • Per-call dispatch overhead of NumPy on a TINY array (8 elements).
  • This is intentionally the worst case for NumPy — BLAS shines on large
    contiguous data, not on millions of tiny calls.
  • For a flattering NumPy comparison we'd benchmark `np.dot(huge, huge)`
    once; for THIS comparison we want apples-to-apples per-call cost.
"""
import numpy as np


def bench() -> float:
    a = np.array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0])
    b = np.array([8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0])
    total = 0.0
    for _ in range(1_000_000):
        a[0] = total / 1.0e9
        total += float(np.dot(a, b))
    return total


if __name__ == "__main__":
    print(f"{bench():g}")
