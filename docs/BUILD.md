# Building Flux

Flux has three build products:

| Product | What | Needs |
|---|---|---|
| `fluxc` | native compiler CLI | CMake ≥ 3.16, a C++17 compiler |
| `flux_wasm.{js,wasm}` | the same compiler for the browser | [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) |
| `frontend/` | the WebGPU playground | Node 20+ |

The compiler core has **no third-party dependencies** — no LLVM, no SPIRV-Tools. Catch2 is fetched only for the tests.

---

## Native compiler

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/fluxc examples/plasma.flux                 # WGSL to stdout
```

Windows (PowerShell): `.\build.ps1` (add `-Tests` and/or `-Wasm`).

### `fluxc` usage

```
fluxc <file.flux> [options]

  --emit <what>     wgsl (default) | spirv | spirv-asm | ir | ir-raw | ast | tokens | json
  -o <file>         write output to a file (spirv is binary)
  -O0               disable IR optimization
  --disable <pass>  skip one optimization pass (repeatable)
  --passes          list optimization passes
  --stats           print per-pass change counts to stderr
```

```bash
./build/fluxc examples/raymarch.flux --emit spirv -o raymarch.spv
spirv-val --target-env vulkan1.0 raymarch.spv       # from SPIRV-Tools / the Vulkan SDK
./build/fluxc examples/noise.flux --emit ir --stats  # optimized FluxIR + pass counts
```

Diagnostics go to stderr in `file:line:col: error: message` form; the exit code is non-zero on errors.

---

## Tests

```bash
cmake -S . -B build -DFLUX_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

This runs the Catch2 suite (lexer, parser, type checker, every pass, both backends) and — if `spirv-val` is on `PATH` — compiles every file in `examples/` at `-O0` and `-O2` and validates the SPIR-V for Vulkan 1.0.

---

## WebAssembly + playground

```bash
emcmake cmake -S wasm -B build_wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_wasm
cp build_wasm/flux_wasm.js build_wasm/flux_wasm.wasm frontend/public/

cd frontend
npm install
npm run dev
```

The playground needs a browser with WebGPU (recent Chrome, Edge or Safari) for the live preview; every compiler view works without it. CI (`.github/workflows/deploy.yml`) builds the WASM module and deploys the playground to GitHub Pages on every push to `main`.
