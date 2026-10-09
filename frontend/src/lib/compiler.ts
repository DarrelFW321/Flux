// Typed wrapper around the Emscripten build of the Flux compiler. The whole
// pipeline — lexer to SPIR-V — runs in the browser.

export interface Diagnostic {
  severity: 'error' | 'warning' | 'note';
  message: string;
  line: number;
  col: number;
  len: number;
  source?: 'flux' | 'webgpu';
}

export interface Token {
  type: string;
  lexeme: string;
  line: number;
  col: number;
}

export interface AstNode {
  cat: 'decl' | 'stmt' | 'expr';
  label: string;
  detail: string;
  type: string;
  line: number;
  col: number;
  children: { edge: string; node: AstNode }[];
}

export interface UniformInfo {
  name: string;
  type: string;
  offset: number;
  size: number;
  default: number[];
  range: [number, number] | null;
  color: boolean;
  line: number;
}

export interface PassStep {
  pass: string;
  iteration: number;
  changes: number;
  ir: string | null;
  map: number[] | null;
}

export interface CompileResult {
  ok: boolean;
  diagnostics: Diagnostic[];
  tokens: Token[];
  ast: AstNode | null;
  reflection: {
    uniforms: UniformInfo[];
    uniformSize: number;
    entries: { name: string; stage: 'vertex' | 'fragment' }[];
  };
  ir: {
    raw: string | null;
    rawMap: number[];
    opt: string | null;
    optMap: number[];
    instsBefore: number;
    instsAfter: number;
    iterations: number;
  };
  passes: {
    available: { name: string; description: string }[];
    totals: { name: string; changes: number }[];
    steps: PassStep[];
  };
  wgsl: { code: string | null; map: number[] };
  spirv: { text: string | null; map: number[]; words: number[] | null };
  timings: { stage: string; ms: number }[];
}

type FluxWasm = FluxWasmModule;   // declared in flux.d.ts

let instance: Promise<FluxWasm> | null = null;

export function loadCompiler(): Promise<FluxWasm> {
  if (!instance) {
    instance = new Promise((resolve, reject) => {
      const start = () => {
        const factory = globalThis.FluxModule;
        if (typeof factory !== 'function') {
          reject(new Error('flux_wasm.js is missing — build the WASM module (see docs/BUILD.md)'));
          return;
        }
        factory().then(resolve, reject);
      };
      if (document.readyState === 'complete') start();
      else window.addEventListener('load', start, { once: true });
    });
  }
  return instance;
}

export interface CompileOptions {
  optimize: boolean;
  disabled: string[];
}

export function compileWith(wasm: FluxWasm, source: string, opts: CompileOptions): CompileResult {
  const flags = [...(opts.optimize ? [] : ['O0']), ...opts.disabled.map(d => `disable=${d}`)];
  const result = JSON.parse(wasm.compile(source, flags.join(','))) as CompileResult;
  for (const d of result.diagnostics) d.source = 'flux';
  return result;
}
