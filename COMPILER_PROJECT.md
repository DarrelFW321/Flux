# NumericLang — Compiler Project Plan

A statically typed compiled language for numerical kernels, built on LLVM with an interactive
pipeline visualizer running in the browser via WebAssembly.

---

## Project Identity

**Tagline:** A lightweight compiled language for numerical kernels, with an interactive
pipeline visualizer powered by WebAssembly.

**Why it exists:** Most toy compilers stop at "hello world." This one is scoped around
arithmetic-heavy, loop-intensive programs — the kind of code that matters in ML infrastructure,
HPC, and systems performance work. The visualizer makes every internal stage (tokens, AST,
type checking, LLVM IR) inspectable in real time, directly in the browser.

---

## Language Feature Set (v1)

```
Primitive types:   int, float, bool
Variables:         let x: int = 42;
Arithmetic:        +  -  *  /  %
Comparison:        ==  !=  <  >  <=  >=
Control flow:      if / else, while
Functions:         fn add(x: int, y: int) -> int { return x + y; }
Built-in output:   print(x)
```

### Example program

```
fn dot(n: int) -> float {
  let sum: float = 0.0;
  let i: int = 0;
  while i < n {
    sum = sum + 1.0;
    i = i + 1;
  }
  return sum;
}

print(dot(100));
```

---

## Architecture

```
Source code
    │
    ▼
┌─────────────┐
│   Lexer      │  → token stream
└─────────────┘
    │
    ▼
┌─────────────┐
│   Parser     │  → AST (recursive descent + Pratt for expressions)
└─────────────┘
    │
    ▼
┌─────────────┐
│ Type checker │  → symbol table, type errors
└─────────────┘
    │
    ├──── compiled to WASM → runs in browser (visualizer stages 1–3)
    │
    ▼
┌─────────────┐
│  IR codegen  │  → LLVM IR  (stays native, exposed via HTTP)
└─────────────┘
    │
    ▼
┌─────────────┐
│ Native binary│  → via clang/lld
└─────────────┘
```

### WASM split

The frontend pipeline (lexer + parser + type checker) compiles to WebAssembly via Emscripten.
It has no system dependencies and runs entirely client-side — zero latency, no backend needed
for the first three visualizer stages.

LLVM IR generation stays as a native binary, exposed via a thin FastAPI endpoint on Render.
The IR tab in the visualizer makes one HTTP call to this backend. Render's free tier spins
down after 15 min of inactivity (~30–60s cold start); the IR tab shows a "Waking up server…"
indicator while the request is in flight.

```
compiler/
├── frontend/           ← compiled to WASM via Emscripten
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── typechecker.cpp
│   └── wasm_bindings.cpp   ← EMSCRIPTEN_BINDINGS exposing compile_frontend()
├── backend/            ← stays native C++
│   └── codegen.cpp         ← LLVM IRBuilder, TargetMachine
├── main.cpp            ← CLI entry point
└── CMakeLists.txt
```

---

## 4-Week Build Plan

### Week 1 — Lexer + Parser + AST

**Goal:** Source string → typed AST in memory.

Tasks:
- Lexer: tokenize keywords (`int`, `float`, `bool`, `if`, `while`, `fn`, `return`, `let`),
  identifiers, numeric literals, operators, punctuation
- AST node types using `std::variant` (faster to write than deep inheritance)
- Recursive descent parser for statements; Pratt parser for expressions
- Basic parse error messages with line/column

Milestone: `parse("fn add(x: int) -> int { return x + 1; }")` returns a correct AST.

### Week 2 — Type Checker + LLVM IR

**Goal:** Catch type errors; emit valid LLVM IR for correct programs.

Tasks:
- Symbol table as `std::unordered_map<std::string, Type>` with scope stack
- Type checker: resolve identifiers, infer binary op types, check return types
- LLVM setup: `LLVMContext`, `Module`, `IRBuilder`
- Codegen: arithmetic → instructions, if/while → `BasicBlock`s, functions → `Function`
- `--emit-llvm` flag dumps IR to stdout

Milestone: `numericlang --emit-llvm hello.nl` prints valid LLVM IR that `lli` can run.

### Week 3 — Native Output + API Backend

**Goal:** Working compiled binary + HTTP endpoint for the visualizer.

Tasks:
- `TargetMachine` setup, object file emission, link with `clang`
- `--dump-stages` flag: outputs tokens, AST, IR as JSON to stdout
- FastAPI wrapper: `POST /compile` shells out to the binary, returns JSON with each stage
- Dockerfile + Render deploy (free tier, ~30–60s cold start on first request after inactivity)

Milestone: `curl /compile` returns `{ tokens, ast, ir, output }` for a sample program.

### Week 4 — WASM Build + Frontend Visualizer

**Goal:** Interactive pipeline visualizer on your personal site.

Tasks:
- Emscripten build of `frontend/` → `numericlang.wasm` + JS glue
- `EMSCRIPTEN_BINDINGS` exposing `compile_frontend(source: string) -> string` (returns JSON)
- React app: Monaco editor (code input) + tab panels (Tokens / AST / IR / Output)
- AST panel: collapsible JSON tree via `react-json-tree`
- IR panel: code block with LLVM syntax highlighting
- IR panel cold-start UX: "Waking up server…" spinner shown while awaiting first response;
  dismissed automatically when IR arrives
- Deploy frontend as static site (GitHub Pages or Vercel)

Milestone: Type code in the browser, see tokens/AST update instantly (WASM), IR fetched from backend.

---

## Resume Bullets

```
• Built a statically typed compiled language targeting LLVM IR, with support for
  functions, control flow, arithmetic, and static type checking.

• Implemented a full compiler pipeline in C++: lexer, recursive descent parser,
  AST construction, semantic analysis, LLVM IR generation, and native code emission
  via the LLVM/Clang toolchain.

• Compiled the compiler frontend (lexer, parser, type checker) to WebAssembly via
  Emscripten, enabling zero-latency in-browser execution of compiler stages with
  no backend dependency.

• Built an interactive pipeline visualizer deployed on a personal website: users type
  source code and see tokens, AST, and LLVM IR update in real time.
```

---

## Tech Stack

| Layer | Technology |
|---|---|
| Compiler | C++17, LLVM C++ API |
| WASM build | Emscripten (`emcc`) |
| Backend API | FastAPI (Python), Docker |
| Backend hosting | Render (free tier, cold-start indicator in UI) |
| Frontend | React, Monaco Editor, react-json-tree |
| Frontend hosting | GitHub Pages or Vercel |

---

## Future Extensions (post-v1)

- **Fixed-size arrays / vectors** — `int[8]`, element-wise ops, opens door to vectorization
- **Loop fusion / tiling passes** — the core optimization in ML kernel compilers (XLA, Triton)
- **Higher-level IR** — intermediate representation between AST and LLVM IR; staged lowering
  informed by MLIR's dialect model
- **MLIR lowering** — custom dialect, affine loop analysis, eventual LLVM backend
- **Constant folding + DCE** — first optimization passes, implemented as AST or IR walks

---

## Key References

- [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/) — canonical frontend reference
- [MLIR Toy Tutorial](https://mlir.llvm.org/docs/Tutorials/Toy/) — staged lowering, custom dialects
- [Emscripten Docs](https://emscripten.org/docs/) — WASM build toolchain
- [Crafting Interpreters](https://craftinginterpreters.com) — Pratt parsing, AST design
