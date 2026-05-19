"""Big-dot microbenchmark — NumPy side.

512-element vectors × 50_000 iterations. Same total work as the small `dot`
benchmark (≈ 25M scalar ops), but per-call dispatch overhead is amortised
across 512 elements — this is the workload where NumPy's BLAS-backed
`np.dot` is finally allowed to flex.
"""
import numpy as np


def bench() -> float:
    a = np.ones(512)
    b = np.full(512, 2.0)
    total = 0.0
    for _ in range(50_000):
        a[0] = total / 1.0e9
        total += float(np.dot(a, b))
    return total


if __name__ == "__main__":
    print(f"{bench():g}")
