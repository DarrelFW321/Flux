// Tiny line-oriented highlighters for the compiler's output languages.
// Each returns [className, text] spans for a single line so code views can
// keep per-line state (source mapping, diff marks) cheaply.

export type Lang = 'wgsl' | 'spirv' | 'ir' | 'flux';
export type Span = [string, string];

const WGSL_KEYWORDS = new Set([
  'fn', 'let', 'var', 'const', 'return', 'if', 'else', 'loop', 'for', 'while', 'break',
  'continue', 'continuing', 'discard', 'struct', 'true', 'false',
]);
const WGSL_TYPES = /^(f32|i32|u32|bool|vec[234][fiu]?|mat[234]x[234]f?|array)$/;

const FLUX_KEYWORDS = new Set([
  'fn', 'let', 'var', 'const', 'uniform', 'return', 'if', 'else', 'for', 'in', 'while',
  'break', 'continue', 'discard', 'true', 'false',
]);
const FLUX_TYPES = /^(void|bool|int|float|vec[234]|mat[234])$/;

const IR_OPS = new Set([
  'const', 'arg', 'uniform', 'add', 'sub', 'mul', 'div', 'rem', 'neg', 'matmul', 'matscale',
  'eq', 'ne', 'lt', 'le', 'gt', 'ge', 'and', 'or', 'not', 'select', 'construct', 'splat',
  'extract', 'extract.dyn', 'insert', 'swizzle', 'shuffle', 'convert', 'call', 'var', 'load', 'store',
]);
const IR_CONTROL = new Set(['if', 'else', 'loop', 'while', 'continuing', 'break', 'continue', 'return', 'discard', 'fn', 'uniforms']);

const TOKEN = /(\s+)|(\/\/.*$|;.*$)|("(?:[^"\\]|\\.)*")|(@[A-Za-z_][\w]*)|(%[\w.]+)|(-?\d+\.?\d*(?:[eE][+-]?\d+)?)|([A-Za-z_][\w.]*)|(.)/g;

function tokens(line: string, lang: Lang): Span[] {
  const out: Span[] = [];
  TOKEN.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = TOKEN.exec(line))) {
    const [text, ws, comment, str, attr, id, num, word] = m;
    if (ws) out.push(['', text]);
    else if (comment) {
      // ';' starts a comment only in SPIR-V assembly and IR.
      if (comment.startsWith(';') && lang !== 'spirv' && lang !== 'ir') { out.push(['hl-punct', ';']); TOKEN.lastIndex = m.index + 1; }
      else out.push(['hl-comment', text]);
    }
    else if (str) out.push(['hl-string', text]);
    else if (attr) out.push(['hl-attr', text]);
    else if (id) out.push([lang === 'spirv' && /^%(_ptr|v\d|mat|float$|int$|bool$|void$|fn_)/.test(text) ? 'hl-type' : 'hl-id', text]);
    else if (num) out.push(['hl-num', text]);
    else if (word) out.push([wordClass(word, lang), text]);
    else out.push(['hl-punct', text]);
  }
  return out;
}

function wordClass(w: string, lang: Lang): string {
  switch (lang) {
    case 'wgsl':
      if (WGSL_KEYWORDS.has(w)) return 'hl-kw';
      if (WGSL_TYPES.test(w)) return 'hl-type';
      return 'hl-ident';
    case 'flux':
      if (FLUX_KEYWORDS.has(w)) return 'hl-kw';
      if (FLUX_TYPES.test(w)) return 'hl-type';
      return 'hl-ident';
    case 'spirv':
      if (/^Op[A-Z]/.test(w)) return /^Op(Branch|BranchConditional|LoopMerge|SelectionMerge|Label|Return|ReturnValue|Kill|Unreachable|Function|FunctionEnd)$/.test(w) ? 'hl-kw' : 'hl-op';
      return 'hl-enum';
    case 'ir':
      if (IR_CONTROL.has(w)) return 'hl-kw';
      if (IR_OPS.has(w)) return 'hl-op';
      if (FLUX_TYPES.test(w)) return 'hl-type';
      return 'hl-ident';
  }
}

export function highlightLine(line: string, lang: Lang): Span[] {
  return tokens(line, lang);
}
