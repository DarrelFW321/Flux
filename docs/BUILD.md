# Building Flux locally

Flux has two parts: the **native compiler** (`flux`) and the **web visualizer** (React + WASM).

---

## Prerequisites

### Windows (MSYS2)

1. Install [MSYS2](https://www.msys2.org/) and open the **MINGW64** shell.
2. Install toolchain and LLVM:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
          mingw-w64-x86_64-llvm mingw-w64-x86_64-clang mingw-w64-x86_64-zlib \
          mingw-w64-x86_64-zstd
```

3. Install [Ninja](https://ninja-build.org/) on your PATH (e.g. via winget).

From PowerShell in the repo root:

```powershell
.\build.ps1 -Configure
.\build.ps1
```

The binary is `build\flux.exe`. Ensure `C:\msys2\mingw64\bin` is on PATH when running `flux --compile` (it invokes `clang` to link).

### Linux (Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build llvm-dev clang zlib1g-dev libzstd-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Binary: `./build/flux`

---

## Compile a program

```bash
./build/flux --compile examples/hello.fl -o hello
./hello
```

Other useful flags:

| Flag | Purpose |
|------|---------|
| `--emit-llvm` | Print LLVM IR to stdout |
| `--emit-mir` | Print optimized FluxIR |
| `--dump-stages` | JSON for the visualizer (tokens, AST, MIR, IR) |

---

## Web visualizer

```bash
cd frontend
npm install
npm run dev
```

Set `VITE_BACKEND_URL=http://127.0.0.1:8080` in `frontend/.env` if you want **Run**, **IR**, and **Benchmarks** against a local API.

### Backend (optional)

```bash
pip install -r backend/requirements.txt
uvicorn backend.server:app --reload --port 8080
```

Run from the **repository root** so `build/flux` and `benchmarks/` resolve correctly.

---

## WASM frontend module

The in-browser lexer/parser/typechecker/MIR pipeline requires building `compiler/frontend` with Emscripten. CI produces `frontend/public/flux_wasm.js`. Without it, the site still loads but pipeline tabs show “Loading wasm…”.

---

## Benchmarks

Kernel sources live in `benchmarks/` (`dot`, `saxpy`, `relu`, `bigdot`). Each has `.fl`, `.c`, and `.py` variants. The hosted visualizer runs them via `POST /benchmark`.

---

## Can I run real ML projects?

**Not yet.** Flux today is a **compiler + kernel language**, not PyTorch or TensorFlow. You can:

- Write and compile small numerical kernels (dot, saxpy-style ops, ReLU loops)
- Compare against C and NumPy in the benchmark tab
- Inspect MIR and LLVM for learning and demos

Planned next steps include tensors, `matmul`, and broader loop optimizations. Until then, treat `examples/` and `benchmarks/` as the reference workloads.
