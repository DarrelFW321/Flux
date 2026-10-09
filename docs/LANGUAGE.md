# Flux language reference

Flux is a small, statically typed shading language. `fluxc` compiles it to **WGSL** (for WebGPU) and **SPIR-V** (for Vulkan) through its own intermediate representation, **FluxIR**.

```flux
uniform time: float;
uniform resolution: vec2;
@range(0.1, 4.0) uniform speed: float = 1.0;

const TAU = 6.2831853;

fn palette(t: float) -> vec3 {
    return 0.5 + 0.5 * cos(TAU * (t + vec3(0.0, 0.33, 0.67)));
}

@fragment
fn main(@builtin(position) frag: vec4) -> vec4 {
    let uv = frag.xy / resolution;
    return vec4(palette(uv.x + time * speed), 1.0);
}
```

---

## Types

| Flux | WGSL | SPIR-V | Notes |
|---|---|---|---|
| `bool` | `bool` | `OpTypeBool` | not allowed in uniforms |
| `int` | `i32` | `OpTypeInt 32 1` | |
| `float` | `f32` | `OpTypeFloat 32` | |
| `vec2` `vec3` `vec4` | `vecNf` | `OpTypeVector %float N` | |
| `mat2` `mat3` `mat4` | `matNxNf` | `OpTypeMatrix` | column-major; `mat2` not allowed in uniforms |

**No implicit conversions.** `int` and `float` never mix: write `float(i)` or `int(x)`. The single convenience is that an *integer literal* takes float type wherever a float is expected, so `vec3(1, 0, 0)`, `x * 2` and `clamp(v, 0, 1)` all type-check.

---

## Declarations

### Uniforms

```flux
uniform time: float;
@range(0.0, 10.0) uniform speed: float = 1.0;   // default + slider range
@color uniform tint: vec3 = vec3(1.0, 0.5, 0.2); // colour picker in the playground
```

All uniforms are packed into one block using the WGSL uniform / std140 layout (`vec3` and matrices align to 16 bytes). In WGSL it is `@group(0) @binding(0) var<uniform> u: Uniforms`; in SPIR-V a `Block`-decorated struct at descriptor set 0, binding 0 with explicit `Offset` / `MatrixStride` decorations. Defaults must be constant expressions.

The playground drives `time`, `resolution`, `mouse` (xy = pointer, zw = last click, in pixels) and `frame` automatically.

### Constants

```flux
const STEPS: int = 64;
const HALF = 0.5;        // type inferred
```

Constant initializers may only use literals, other constants, operators, constructors and built-in functions.

### Functions

```flux
fn smin(a: float, b: float, k: float) -> float {
    let h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}
```

Functions are pure (nothing global is writable) and may not recurse — the checker rejects any cycle in the call graph. Every path through a non-void function must return.

### Entry points

```flux
@fragment
fn main(@builtin(position) frag: vec4, @location(0) uv: vec2) -> vec4 { ... }

@vertex
fn vs(@builtin(vertex_index) index: int) -> vec4 { ... }
```

| Stage | Parameter attributes | Result |
|---|---|---|
| `@fragment` | `@builtin(position): vec4`, `@builtin(front_facing): bool`, `@location(n)`: float/vecN | colour, `@location(0)` |
| `@vertex` | `@builtin(vertex_index): int`, `@builtin(instance_index): int`, `@location(n)` | clip-space `@builtin(position)` |

Each parameter needs exactly one attribute. A module may contain several entry points; the playground previews the first `@fragment`, using its own fullscreen-triangle vertex stage that supplies `uv` at `@location(0)`.

---

## Statements

```flux
let uv = frag.xy / resolution;      // immutable binding, type inferred
let n: vec3 = normalize(p);         // or annotated
var t = 0.0;                        // mutable
var col: vec3;                      // zero-initialized

t += d;                             // = += -= *= /=
col.xy = uv;                        // swizzle assignment (no repeated components)
m[1] = vec3(0.0);                   // column / component assignment (constant index)

if d < 0.001 { ... } else if d > 9.0 { ... } else { ... }
for i in 0..64 { ... }              // int range, end exclusive, evaluated once
while t < 20.0 { ... }
break;  continue;
discard;                            // @fragment only
return vec4(col, 1.0);
```

---

## Expressions

| Precedence (high → low) | Operators |
|---|---|
| postfix | `f(x)`, `vec3(...)`, `v.xyz` / `v.rgba`, `v[i]`, `m[c]` |
| unary | `-x`, `!b` |
| multiplicative | `*` `/` `%` |
| additive | `+` `-` |
| comparison | `<` `>` `<=` `>=` `==` `!=` |
| logical and | `&&` |
| logical or | `\|\|` |
| conditional | `c ? a : b` |

- Arithmetic is componentwise on vectors; a scalar operand is broadcast (`v * 2.0`, `1.0 - v`).
- Matrices support `m * v`, `v * m`, `m * m` and `m * s`.
- Comparisons take scalars of the same type. Vectors can't be compared with `==`; compare components or use `distance`.
- `%` truncates (like WGSL, C); `mod(x, y)` floors (like GLSL).

### Constructors

| Form | Meaning |
|---|---|
| `float(i)`, `int(x)` | numeric conversion (int ← float truncates) |
| `vec3(s)` | splat |
| `vec4(v3, 1.0)`, `vec3(a, b, c)` | concatenate scalars/vectors — total components must match |
| `mat3(s)` | `s` on the diagonal |
| `mat2(c0, c1)`, `mat2(a, b, c, d)` | from columns, or column-major scalars |

### Built-in functions

`T` is `float` or `vecN`. `T|float` arguments accept a scalar broadcast to `T`.

| Function | Signature |
|---|---|
| `sin cos tan asin acos atan exp exp2 log log2 sqrt inversesqrt floor ceil fract round trunc radians degrees` | `(T) -> T` |
| `abs sign` | `(T) -> T`, also `int` |
| `atan2 pow` | `(T, T) -> T` |
| `min max` | `(T, T\|float) -> T`, also `int` |
| `clamp` | `(T, T\|float, T\|float) -> T`, also `int` |
| `mix` | `(T, T, T\|float) -> T` |
| `step` | `(T\|float, T) -> T` |
| `smoothstep` | `(T\|float, T\|float, T) -> T` |
| `mod` | `(T, T\|float) -> T` |
| `length` / `distance` / `dot` | `(T) -> float` / `(T, T) -> float` / `(vecN, vecN) -> float` |
| `cross` | `(vec3, vec3) -> vec3` |
| `normalize reflect` | `(T) -> T` / `(T, T) -> T` |
| `refract` | `(T, T, float) -> T` |
| `transpose determinant` | `(matN) -> matN` / `(matN) -> float` |

`round` rounds half to even on both targets (`round` in WGSL, `RoundEven` in SPIR-V).

---

## Diagnostics

Syntax errors stop at the first problem with an exact location. The type checker keeps going and reports every semantic error it finds, poisoning bad expressions so one mistake doesn't cascade. It also warns about unused variables and parameters, `var`s that are never reassigned, unreachable code, and expression statements whose result is discarded.

---

## How it compiles

```
source ─► Lexer ─► Parser ─► TypeChecker ─► lower ─► FluxIR ─► pass pipeline ─┬─► WGSL emitter
                     │            │                                            └─► SPIR-V emitter
                    AST     types, reflection
```

### FluxIR

FluxIR is SSA with **structured control flow**. `if` and `loop` own nested regions rather than jumping between basic blocks — the same shape SPIR-V requires (`OpSelectionMerge`, `OpLoopMerge`) and the only shape WGSL has, so neither backend has to rebuild structure from a CFG. A `loop` has a *header* region (computing the continue condition), a *body*, and a *continuing* region, matching WGSL's `loop { … continuing { … } }` and SPIR-V's header / continue-target layout.

`let`s, parameters and temporaries are SSA values; `var`s are memory slots accessed by `load` / `store`. Lowering makes implicit operations explicit: scalar broadcasts become `splat`, `mod` expands to `x - y * floor(x / y)`, swizzle assignment becomes `load` / `shuffle` / `store`, and `for` becomes a `loop` over a counter variable.

```
@fragment fn @main(frag: vec4) -> vec4 {
  %0 = arg 0 : vec4  ; frag
  %1 = swizzle %0.xy : vec2
  %2 = uniform @resolution : vec2
  %3 = div %1, %2 : vec2  ; uv
  ...
}
```

### Passes

The pipeline runs to a fixed point (at most 8 rounds):

| Pass | |
|---|---|
| `inline` | inline calls to single-exit helpers (the result replaces the call) |
| `const-fold` | evaluate operations whose operands are constants — arithmetic, comparisons, composites, conversions and built-ins, rounded to f32 like the GPU would |
| `simplify` | `x*1`, `x+0`, `x*0`, `0-x → -x`, `x/2ⁿ → x*2⁻ⁿ`, `pow(x,2) → x*x`, `pow(x,.5) → sqrt`, `--x`, `!!b`, swizzle-of-swizzle, extract-of-construct/splat/insert, idempotent built-ins |
| `branch-fold` | splice in the taken arm of constant `if`s; drop empty `if`s and loops that never run |
| `forward` | store→load forwarding through straight-line code and `if`s, merging knowledge at joins (mem2reg-lite) |
| `dse` | remove stores that are overwritten before being read, and variables that are never loaded |
| `cse` | scoped value numbering of pure instructions (header values are visible to the body and continuing block; body values are not visible to `continuing`) |
| `licm` | hoist loop-invariant pure instructions above the loop |
| `unroll` | fully unroll `for` loops with ≤ 16 constant iterations and no `break` / `continue` |
| `dce` | remove unused pure values and functions unreachable from any entry point |

Each pass reports how many changes it made, and the playground can show the IR after every pass that changed something.

### Backends

**WGSL.** Single-use expressions are folded back into nested expressions with minimal parentheses; named and multiply-used values become `let`s. Recognized loop shapes print as `for (; cond; i++)` / `while`, falling back to `loop { … continuing { … } }`. Identifiers WGSL reserves are renamed, and `vertex_index` / `instance_index` are converted from `u32`.

**SPIR-V.** SPIR-V 1.0 for Vulkan: `Shader` capability, `GLSL.std.450` for math, `Logical GLSL450` memory model. Entry points become `void` functions that read `Input` and write `Output` variables; all `OpVariable`s are hoisted to the entry block; scalar `?:` conditions are splatted to bool vectors for vector `OpSelect`; constant composites become `OpConstantComposite`. Every instruction carries `OpLine` debug info pointing back to the Flux source. The CTest suite validates every example with `spirv-val`.

Both backends emit a line map (output line → source line) that powers the playground's source linking.
