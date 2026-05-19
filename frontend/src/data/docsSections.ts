// In-app language docs (mirrors docs/LANGUAGE.md for the visualizer sidebar).

export interface DocSection {
  id: string;
  title: string;
  body: string;
}

export const DOC_SECTIONS: DocSection[] = [
  {
    id: 'overview',
    title: 'Overview',
    body: `Flux is a statically typed language for numerical kernels. Programs compile to native code via LLVM. The visualizer shows each compiler stage in your browser (WASM) except LLVM IR and execution, which use the backend API.`,
  },
  {
    id: 'types',
    title: 'Types',
    body: `Primitive types: int, float, bool.

Fixed-size arrays: float[4], int[8], etc. Array size is part of the type. No dynamic allocation or tensors yet.`,
  },
  {
    id: 'variables',
    title: 'Variables',
    body: `let name: type = expression;

Every variable needs an explicit type and initializer. Array assignment copies all elements.`,
  },
  {
    id: 'operators',
    title: 'Operators',
    body: `Arithmetic: + - * / % (scalars and arrays; scalars broadcast).

Comparisons: == != < > <= >= (scalars → bool).

Unary: -x`,
  },
  {
    id: 'control',
    title: 'Control flow',
    body: `if condition { ... } else { ... }

while condition { ... }

Use while for loops (no for-loop syntax yet).`,
  },
  {
    id: 'functions',
    title: 'Functions',
    body: `fn name(a: T, b: U) -> R { ... return expr; }

Arrays can be parameters and return types. Functions must be declared before use.`,
  },
  {
    id: 'arrays',
    title: 'Arrays',
    body: `Literals: [1.0, 2.0, 3.0, 4.0]

Indexing: a[i] = x (constant indices are bounds-checked).

Element-wise: a * 2.0 + b`,
  },
  {
    id: 'builtins',
    title: 'Built-ins',
    body: `print(x) — stdout

sum(arr) — sum of elements → scalar

dot(a, b) — dot product → scalar`,
  },
  {
    id: 'pipeline',
    title: 'Compiler pipeline',
    body: `AST → FluxIR (MIR) → optimization passes → LLVM IR → native binary.

Passes include const-fold, algebraic-simplify, loop-fusion, and DCE. Open the MIR tab (diff view) to see what changed.`,
  },
  {
    id: 'limits',
    title: 'Limitations',
    body: `No modules, structs, for-loops, or GPU. Fixed 1D arrays only. Not a full ML framework — kernels and benchmarks only.

See docs/LANGUAGE.md and docs/BUILD.md in the repository.`,
  },
  {
    id: 'build',
    title: 'Build locally',
    body: `Compiler: ./build.ps1 (Windows) or cmake -B build && cmake --build build.

Run: ./build/flux --compile examples/hello.fl -o hello

Visualizer: cd frontend && npm install && npm run dev

See docs/BUILD.md in the repository.`,
  },
];
