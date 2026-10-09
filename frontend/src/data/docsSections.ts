// In-app language reference (mirrors docs/LANGUAGE.md).

export type DocBlock =
  | { kind: 'p'; text: string }
  | { kind: 'code'; text: string }
  | { kind: 'table'; head: string[]; rows: string[][] };

export interface DocSection {
  id: string;
  title: string;
  blocks: DocBlock[];
}

export const DOC_SECTIONS: DocSection[] = [
  {
    id: 'overview',
    title: 'Overview',
    blocks: [
      { kind: 'p', text: 'Flux is a small, statically typed shading language. The compiler (fluxc, C++17) parses and type-checks a module, lowers it to FluxIR — a structured SSA IR — runs an optimization pipeline, and emits both WGSL and SPIR-V. In this playground the entire compiler runs as WebAssembly; the WGSL output is executed live with WebGPU.' },
      { kind: 'code', text: 'source ─► lexer ─► parser ─► type checker ─► FluxIR ─► passes ─┬─► WGSL ─► WebGPU\n                                                               └─► SPIR-V' },
    ],
  },
  {
    id: 'types',
    title: 'Types',
    blocks: [
      { kind: 'table', head: ['Type', 'WGSL', 'SPIR-V', 'Notes'], rows: [
        ['bool', 'bool', 'OpTypeBool', 'not allowed in uniforms'],
        ['int', 'i32', 'OpTypeInt 32 1', '32-bit signed'],
        ['float', 'f32', 'OpTypeFloat 32', ''],
        ['vec2 vec3 vec4', 'vecNf', 'OpTypeVector', 'float components'],
        ['mat2 mat3 mat4', 'matNxNf', 'OpTypeMatrix', 'column-major'],
      ] },
      { kind: 'p', text: 'There are no implicit conversions between int and float values — use float(i) / int(x). The one convenience: an integer literal adopts float type where a float is expected, so vec3(1, 0, 0) and x * 2 both work.' },
    ],
  },
  {
    id: 'declarations',
    title: 'Uniforms, consts, functions',
    blocks: [
      { kind: 'code', text: 'uniform time: float;                       // driven by the playground\n@range(0.0, 4.0) uniform speed: float = 1.0; // slider\n@color uniform tint: vec3 = vec3(1, 0.5, 0); // colour picker\n\nconst TAU = 6.2831853;                     // compile-time constant\n\nfn palette(t: float) -> vec3 {\n    return 0.5 + 0.5 * cos(TAU * (t + vec3(0.0, 0.33, 0.67)));\n}' },
      { kind: 'p', text: 'Uniforms are packed into one block (@group(0) @binding(0) in WGSL, a std140 Block at set 0 / binding 0 in SPIR-V). The playground fills time, resolution, mouse and frame automatically and generates controls for every other uniform.' },
    ],
  },
  {
    id: 'entry-points',
    title: 'Entry points',
    blocks: [
      { kind: 'code', text: '@fragment\nfn main(@builtin(position) frag: vec4) -> vec4 { ... }\n\n@vertex\nfn vs(@builtin(vertex_index) i: int) -> vec4 { ... }' },
      { kind: 'table', head: ['Stage', 'Inputs', 'Output'], rows: [
        ['@fragment', '@builtin(position): vec4, @builtin(front_facing): bool, @location(n): float/vecN', 'vec4 colour → @location(0)'],
        ['@vertex', '@builtin(vertex_index): int, @builtin(instance_index): int, @location(n)', 'vec4 → @builtin(position)'],
      ] },
      { kind: 'p', text: 'The playground supplies a fullscreen-triangle vertex stage that passes uv (0–1) at @location(0), and previews the first @fragment entry point.' },
    ],
  },
  {
    id: 'statements',
    title: 'Statements',
    blocks: [
      { kind: 'code', text: 'let uv = frag.xy / resolution;   // immutable, type inferred\nvar col: vec3 = vec3(0.0);       // mutable\ncol += 0.1;  col.xy = uv;        // compound and swizzle assignment\n\nif d < 0.001 { break; } else if d > 9.0 { continue; }\nfor i in 0..64 { ... }           // int range, end exclusive\nwhile t < 20.0 { ... }\ndiscard;                         // fragment only\nreturn vec4(col, 1.0);' },
      { kind: 'p', text: 'Functions are pure: they cannot write uniforms or globals, and recursion is rejected (GPUs have no call stack). Every path of a non-void function must return.' },
    ],
  },
  {
    id: 'expressions',
    title: 'Expressions',
    blocks: [
      { kind: 'table', head: ['Precedence', 'Operators'], rows: [
        ['postfix', 'f(x)  v.xyz  v.rgba  m[i]'],
        ['unary', '-x  !b'],
        ['multiplicative', '*  /  %'],
        ['additive', '+  -'],
        ['comparison', '<  >  <=  >=  ==  !='],
        ['logical', '&&  then  ||'],
        ['conditional', 'c ? a : b'],
      ] },
      { kind: 'p', text: 'Arithmetic is componentwise on vectors and broadcasts scalars (v * 2.0). Matrices support m * v, v * m, m * m and m * s. Vectors cannot be compared with ==; compare components or use distance().' },
    ],
  },
  {
    id: 'builtins',
    title: 'Built-in functions',
    blocks: [
      { kind: 'p', text: 'T is float or vecN. Arguments marked T|float accept a scalar that is broadcast to T.' },
      { kind: 'code', text: 'sin cos tan asin acos atan atan2 exp exp2 log log2 sqrt inversesqrt pow\nabs sign floor ceil fract round trunc radians degrees\nmin max clamp mix step smoothstep mod\nlength distance dot cross normalize reflect refract\ntranspose determinant' },
      { kind: 'p', text: 'mod(x, y) is floored like GLSL (x - y * floor(x / y)); the % operator truncates like WGSL. round() rounds half to even on both targets.' },
    ],
  },
  {
    id: 'ir',
    title: 'FluxIR & passes',
    blocks: [
      { kind: 'p', text: 'FluxIR is SSA with structured control flow: if and loop own nested regions instead of branching between basic blocks — exactly the shape SPIR-V (OpSelectionMerge / OpLoopMerge) and WGSL want. Mutable variables are memory slots (var / load / store); everything else is a value.' },
      { kind: 'table', head: ['Pass', 'What it does'], rows: [
        ['inline', 'copy single-exit helper bodies into their callers'],
        ['const-fold', 'evaluate operations on constants, including builtins, with f32 rounding'],
        ['simplify', 'x*1, x+0, pow(x,2)→x*x, x/2→x*0.5, swizzle-of-swizzle, extract-of-construct'],
        ['branch-fold', 'drop ifs with constant conditions and loops that never run'],
        ['forward', 'store→load forwarding across straight-line code and ifs (mem2reg-lite)'],
        ['dse', 'remove overwritten stores and never-read variables'],
        ['cse', 'scoped value numbering of pure instructions'],
        ['licm', 'hoist loop-invariant work above the loop'],
        ['unroll', 'fully unroll for-loops with ≤16 constant iterations and no break/continue'],
        ['dce', 'remove unused values and unreachable functions'],
      ] },
      { kind: 'p', text: 'The pipeline runs to a fixed point (up to 8 rounds). Toggle passes in the IR tab to see what each one buys you in the WGSL and SPIR-V.' },
    ],
  },
];
