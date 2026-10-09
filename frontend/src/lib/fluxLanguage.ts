import type { Monaco } from '@monaco-editor/react';

export const BUILTINS: Record<string, string> = {
  sin: 'sin(x: T) -> T', cos: 'cos(x: T) -> T', tan: 'tan(x: T) -> T',
  asin: 'asin(x: T) -> T', acos: 'acos(x: T) -> T', atan: 'atan(x: T) -> T',
  atan2: 'atan2(y: T, x: T) -> T', exp: 'exp(x: T) -> T', exp2: 'exp2(x: T) -> T',
  log: 'log(x: T) -> T', log2: 'log2(x: T) -> T', sqrt: 'sqrt(x: T) -> T',
  inversesqrt: 'inversesqrt(x: T) -> T', pow: 'pow(x: T, y: T) -> T',
  abs: 'abs(x: T) -> T  (T may be int)', sign: 'sign(x: T) -> T', floor: 'floor(x: T) -> T',
  ceil: 'ceil(x: T) -> T', fract: 'fract(x: T) -> T', round: 'round(x: T) -> T  (half to even)',
  trunc: 'trunc(x: T) -> T', radians: 'radians(x: T) -> T', degrees: 'degrees(x: T) -> T',
  min: 'min(a: T, b: T|float) -> T', max: 'max(a: T, b: T|float) -> T',
  clamp: 'clamp(x: T, lo: T|float, hi: T|float) -> T', mix: 'mix(a: T, b: T, t: T|float) -> T',
  step: 'step(edge: T|float, x: T) -> T', smoothstep: 'smoothstep(e0: T|float, e1: T|float, x: T) -> T',
  mod: 'mod(x: T, y: T|float) -> T  (floored, like GLSL)', length: 'length(v: T) -> float',
  distance: 'distance(a: T, b: T) -> float', dot: 'dot(a: vecN, b: vecN) -> float',
  cross: 'cross(a: vec3, b: vec3) -> vec3', normalize: 'normalize(v: T) -> T',
  reflect: 'reflect(i: T, n: T) -> T', refract: 'refract(i: T, n: T, eta: float) -> T',
  transpose: 'transpose(m: matN) -> matN', determinant: 'determinant(m: matN) -> float',
};

const KEYWORDS = ['fn', 'let', 'var', 'const', 'uniform', 'return', 'if', 'else', 'for', 'in', 'while',
  'break', 'continue', 'discard', 'true', 'false'];
const TYPES = ['void', 'bool', 'int', 'float', 'vec2', 'vec3', 'vec4', 'mat2', 'mat3', 'mat4'];

let registered = false;

export function registerFlux(monaco: Monaco) {
  if (registered) return;
  registered = true;

  monaco.languages.register({ id: 'flux', extensions: ['.flux'] });
  monaco.languages.setLanguageConfiguration('flux', {
    comments: { lineComment: '//', blockComment: ['/*', '*/'] },
    brackets: [['{', '}'], ['(', ')'], ['[', ']']],
    autoClosingPairs: [{ open: '{', close: '}' }, { open: '(', close: ')' }, { open: '[', close: ']' }],
    indentationRules: { increaseIndentPattern: /\{[^}]*$/, decreaseIndentPattern: /^\s*\}/ },
  });
  monaco.languages.setMonarchTokensProvider('flux', {
    keywords: KEYWORDS,
    types: TYPES,
    builtins: Object.keys(BUILTINS),
    tokenizer: {
      root: [
        [/@[a-zA-Z_]\w*/, 'annotation'],
        [/[a-zA-Z_]\w*/, { cases: { '@keywords': 'keyword', '@types': 'type', '@builtins': 'predefined', '@default': 'identifier' } }],
        [/\d*\.\d+([eE][-+]?\d+)?|\d+\.\d*([eE][-+]?\d+)?|\d+[eE][-+]?\d+/, 'number.float'],
        [/\d+/, 'number'],
        [/\/\/.*$/, 'comment'],
        [/\/\*/, 'comment', '@comment'],
        [/[{}()[\]]/, '@brackets'],
        [/[-+*/%=!<>&|?:.]+/, 'operator'],
      ],
      comment: [[/[^/*]+/, 'comment'], [/\*\//, 'comment', '@pop'], [/[/*]/, 'comment']],
    },
  });

  monaco.languages.registerCompletionItemProvider('flux', {
    provideCompletionItems(model, position) {
      const word = model.getWordUntilPosition(position);
      const range = { startLineNumber: position.lineNumber, endLineNumber: position.lineNumber,
                      startColumn: word.startColumn, endColumn: word.endColumn };
      const K = monaco.languages.CompletionItemKind;
      return {
        suggestions: [
          ...Object.entries(BUILTINS).map(([name, sig]) => ({
            label: name, kind: K.Function, detail: sig, insertText: name, range,
          })),
          ...TYPES.map(t => ({ label: t, kind: K.Class, insertText: t, range })),
          ...KEYWORDS.map(k => ({ label: k, kind: K.Keyword, insertText: k, range })),
        ],
      };
    },
  });

  monaco.editor.defineTheme('flux-dark', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: 'keyword', foreground: 'c792ea' },
      { token: 'type', foreground: '7dd3fc' },
      { token: 'predefined', foreground: '5eead4' },
      { token: 'annotation', foreground: 'fbbf24' },
      { token: 'number', foreground: 'fca5a5' },
      { token: 'number.float', foreground: 'fca5a5' },
      { token: 'comment', foreground: '4b5563', fontStyle: 'italic' },
      { token: 'identifier', foreground: 'e5e7eb' },
      { token: 'operator', foreground: '9ca3af' },
    ],
    colors: {
      'editor.background': '#0e0f13',
      'editor.foreground': '#e5e7eb',
      'editorLineNumber.foreground': '#2f3341',
      'editorLineNumber.activeForeground': '#9ca3af',
      'editor.lineHighlightBackground': '#14161e',
      'editor.lineHighlightBorder': '#00000000',
      'editor.selectionBackground': '#2b2f45',
      'editorCursor.foreground': '#8aa4f8',
      'editorGutter.background': '#0e0f13',
      'editorIndentGuide.background1': '#161822',
      'editorWidget.background': '#11131a',
      'editorWidget.border': '#232634',
      'editorHoverWidget.background': '#11131a',
      'editorHoverWidget.border': '#232634',
      'scrollbarSlider.background': '#23263480',
      'scrollbarSlider.hoverBackground': '#2f334180',
    },
  });
}
