# Flux language reference

Flux is a small, statically typed language for **numerical kernels** — loops, fixed-size arrays, and arithmetic that compiles to native code via LLVM.

This document matches what the compiler accepts today (v0.1).

---

## Types

| Type    | Example         | Notes                                             |
| ------- | --------------- | ------------------------------------------------- |
| `int`   | `42`            | 32-bit signed integer                             |
| `float` | `3.14`          | 64-bit IEEE float (`double` in LLVM)              |
| `bool`  | `true`, `false` |                                                   |
| `T[N]`  | `float[4]`      | Fixed-size array; `N` must be a compile-time size |

Arrays are value types backed by stack storage. There is no heap allocation or dynamic sizing yet.

---

## Variables

```flux
let x: int = 10;
let pi: float = 3.14159;
let flag: bool = false;
let v: float[4] = [1.0, 2.0, 3.0, 4.0];
```

Every `let` requires an explicit type and an initializer.

---

## Operators

**Arithmetic** (scalars and arrays, with broadcasting):

- `+` `-` `*` `/` `%`

When one side is a scalar and the other an array, the scalar is broadcast across elements.

**Comparison** (scalars only, result `bool`):

- `==` `!=` `<` `>` `<=` `>=`

**Unary:**

- `-x` negation

---

## Control flow

```flux
if x > 0 {
  print(x);
} else {
  print(0);
}

while i < 10 {
  i = i + 1;
}
```

Blocks use `{` `}`. Conditions must be `bool`.

---

## Functions

```flux
fn scale(a: float[4], k: float) -> float[4] {
  return a * k;
}

fn add(a: int, b: int) -> int {
  return a + b;
}
```

- Functions must be declared before use (single compilation unit).
- Array parameters and returns are passed by pointer at the LLVM level; in source they look like ordinary arrays.
- Returning an array copies the data into the caller’s slot.

---

## Arrays

**Literals** — all elements required:

```flux
let a: float[3] = [1.0, 2.0, 3.0];
```

**Indexing** — bounds checked when the index is a constant:

```flux
let x: float = a[0];
a[1] = 2.5;
```

**Element-wise ops** — same shape, or scalar broadcast:

```flux
let c: float[4] = a * 2.0 + b;
```

**Whole-array copy** — assigning one array variable to another copies elements:

```flux
let b: float[4] = a;
```

---

## Built-in functions

| Function | Signature             | Description                                                       |
| -------- | --------------------- | ----------------------------------------------------------------- |
| `print`  | `print(value)`        | Prints one value (int, float, bool, or array) followed by newline |
| `sum`    | `sum(arr) -> scalar`  | Sum of all elements                                               |
| `dot`    | `dot(a, b) -> scalar` | Dot product of two arrays of the same type and length             |

```flux
let s: float = sum(v);
let d: float = dot(a, b);
```

There is no `len()`, `malloc`, or I/O beyond `print`.

---

## Comments

```flux
// line comment to end of line
```

---

## Compilation pipeline

Source is lowered in this order:

1. **Lexer / parser** → AST
2. **Type checker** → typed AST
3. **FluxIR (MIR)** → mid-level IR with whole-array ops
4. **Optimization passes** → constant folding, algebraic simplification, loop fusion, DCE
5. **LLVM IR** → native binary (`flux --compile file.fl -o out`)

Use the visualizer’s **MIR** tab to see optimization (including `fused.loop` for fused array expressions).

---

## Limitations (current)

- Fixed-size 1D arrays only — no tensors, ranks, or `matmul`
- No modules, imports, or structs
- No `for` loops (use `while`)
- No user-defined generics or operator overloading
- Benchmarks and kernels only. Not a full ML framework

For building the compiler locally, see [BUILD.md](./BUILD.md).
