# Flux

**A tiny language for numerical kernels — plus a browser visualizer that shows you every compiler stage.**

Write array code, watch it become tokens → AST → MIR → LLVM, then run it or benchmark against C and NumPy. Built as a learning project for how ML-style compute actually gets compiled.

<p align="center">
  <a href="https://darrelfw321.github.io/Flux/"><strong>Live demo</strong></a>
  ·
  <a href="https://github.com/DarrelFW321/flux">GitHub</a>
  ·
  <a href="docs/LANGUAGE.md">Language</a>
  ·
  <a href="docs/BUILD.md">Build guide</a>
  ·
  <a href="examples/">Examples</a>
  ·
  <a href="benchmarks/">Benchmarks</a>
</p>

---

## What you get

| | |
|---|---|
| **Playground** | Monaco editor, live WASM frontend (lexer through MIR), server-side compile & run |
| **Pipeline view** | Tokens, AST graph, MIR with per-pass diffs, LLVM IR on demand |
| **Benchmarks** | Fixed kernels (`dot`, `saxpy`, `relu`, `bigdot`) — Flux vs `gcc -O2` C vs NumPy |
| **Compiler** | C++17 → FluxIR → LLVM → native binary; constant folding, algebraic simplify, loop fusion, DCE |

Flux speaks **fixed-size 1D arrays**, `sum` / `dot`, and `while` loops — enough to express real micro-kernels, not enough to train a model. No autograd, no GPU, no imports. That's intentional.

---

## Try it

**Visualizer** (needs the backend for Run / IR / benchmarks):

```bash
cd frontend
npm install
npm run dev
```

Set `VITE_BACKEND_URL` to your FastAPI server if Run or Benchmarks should work locally (see [docs/BUILD.md](docs/BUILD.md)).

**Compiler only:**

```bash
# Windows
.\build.ps1 -Configure
.\build.ps1
.\build\flux --compile examples/hello.fl -o hello
./hello

# Linux
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/flux --compile examples/hello.fl -o hello
```

---

## Repo map

```
compiler/     lexer, parser, typechecker, FluxIR, LLVM codegen
wasm/         Emscripten build for in-browser frontend
frontend/     React visualizer
backend/      FastAPI — /run, /compile, /benchmark
examples/     small .fl programs
benchmarks/   kernel sources (Flux, C, Python)
docs/         LANGUAGE.md, BUILD.md
```

---

## Stack

C++17 · LLVM · CMake · Emscripten · React · TypeScript · FastAPI · Docker

---

<p align="center">
  <sub>Darrel Wihandi · SE @ UW</sub>
</p>
