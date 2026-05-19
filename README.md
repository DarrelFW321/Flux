# Flux

A statically typed compiled language for **numerical kernels**, with an interactive compiler pipeline visualizer (lexer → AST → MIR → LLVM) in the browser.

- **Language reference:** [docs/LANGUAGE.md](docs/LANGUAGE.md)
- **Build locally:** [docs/BUILD.md](docs/BUILD.md)
- **Example programs:** [examples/](examples/)
- **Benchmark kernels:** [benchmarks/](benchmarks/)

## Quick start

```bash
# Native compiler
./build.ps1 -Configure   # Windows
./build.ps1
./build/flux --compile examples/hello.fl -o hello

# Visualizer
cd frontend && npm install && npm run dev
```

## What it is (and isn’t)

Flux is an **ML-oriented toy compiler**: fixed-size arrays, `sum`/`dot`, MIR optimizations (constant folding, loop fusion), and native codegen. It is **not** a full ML stack — no training, autograd, or GPU backends yet.

The web UI is for exploring the pipeline and running micro-benchmarks against C and NumPy.
