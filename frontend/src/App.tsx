import { useState, useEffect, useRef, useCallback } from 'react';
import Editor, { Monaco } from '@monaco-editor/react';
import { JSONTree } from 'react-json-tree';
import SyntaxHighlighter from 'react-syntax-highlighter';
import { atomOneDark } from 'react-syntax-highlighter/dist/esm/styles/hljs';
import { AstGraph } from './AstGraph';

// ── Types ─────────────────────────────────────────────────────────────────────

interface Token {
  type: string;
  lexeme: string;
  line: number;
  col: number;
}

interface FrontendResult {
  tokens: Token[] | null;
  ast: any | null;
  error: string | null;
}

type Tab = 'tokens' | 'ast' | 'ir' | 'output';
type AstView = 'graph' | 'json';

// ── Constants ─────────────────────────────────────────────────────────────────

const DEFAULT_SOURCE = `fn scale(a: float[4], k: float) -> float[4] {
  return a * k;
}

let x: float[4] = [1.0, 2.0, 3.0, 4.0];
let y: float[4] = scale(x, 2.0);

print(dot(x, y));
print(y[2]);`;

const BACKEND_URL = (import.meta as any).env?.VITE_BACKEND_URL ?? '';

// react-json-tree theme tuned to match the minimal dark palette.
const JSON_THEME = {
  scheme: 'flux',
  base00: 'transparent',
  base01: '#18181b',
  base02: '#27272a',
  base03: '#52525b',
  base04: '#71717a',
  base05: '#a1a1aa',
  base06: '#d4d4d8',
  base07: '#fafafa',
  base08: '#f87171',
  base09: '#fbbf24',
  base0A: '#fcd34d',
  base0B: '#86efac',
  base0C: '#67e8f9',
  base0D: '#93c5fd',
  base0E: '#c4b5fd',
  base0F: '#fbcfe8',
};

// Custom Monaco theme — flat, matches the rest of the surface.
const beforeEditorMount = (monaco: Monaco) => {
  monaco.editor.defineTheme('flux-dark', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: 'keyword',    foreground: 'c4b5fd' },
      { token: 'number',     foreground: 'fbbf24' },
      { token: 'string',     foreground: '86efac' },
      { token: 'comment',    foreground: '52525b', fontStyle: 'italic' },
      { token: 'identifier', foreground: 'e4e4e7' },
      { token: 'type',       foreground: '93c5fd' },
    ],
    colors: {
      'editor.background':              '#09090b',
      'editor.foreground':              '#e4e4e7',
      'editorLineNumber.foreground':    '#3f3f46',
      'editorLineNumber.activeForeground': '#a1a1aa',
      'editor.lineHighlightBackground': '#111114',
      'editor.lineHighlightBorder':     '#11111400',
      'editor.selectionBackground':     '#27272a',
      'editor.inactiveSelectionBackground': '#1f1f23',
      'editorCursor.foreground':        '#fafafa',
      'editorGutter.background':        '#09090b',
      'editorIndentGuide.background1':  '#18181b',
      'editorIndentGuide.activeBackground1': '#27272a',
      'scrollbarSlider.background':         '#27272a80',
      'scrollbarSlider.hoverBackground':    '#3f3f4680',
      'scrollbarSlider.activeBackground':   '#52525b80',
      'editorWidget.background':       '#111114',
      'editorWidget.border':           '#1f1f23',
    },
  });
};

// ── App ───────────────────────────────────────────────────────────────────────

export default function App() {
  const [source, setSource]           = useState(DEFAULT_SOURCE);
  const [result, setResult]           = useState<FrontendResult>({ tokens: null, ast: null, error: null });
  const [activeTab, setActiveTab]     = useState<Tab>('tokens');
  const [astView, setAstView]         = useState<AstView>('graph');
  const [ir, setIr]                   = useState('');
  const [irLoading, setIrLoading]     = useState(false);
  const [irError, setIrError]         = useState('');
  const [output, setOutput]           = useState('');
  const [runLoading, setRunLoading]   = useState(false);
  const [runError, setRunError]       = useState('');
  const [wasmReady, setWasmReady]     = useState(false);

  const fluxRef  = useRef<FluxWasm | null>(null);
  const debounce = useRef<ReturnType<typeof setTimeout>>();

  useEffect(() => {
    if (typeof FluxModule === 'undefined') return;
    FluxModule()
      .then(m => { fluxRef.current = m; setWasmReady(true); })
      .catch(() => {/* WASM unavailable in local dev without a build */});
  }, []);

  const runFrontend = useCallback((src: string) => {
    if (!fluxRef.current) return;
    try {
      const raw = fluxRef.current.compile_frontend(src);
      setResult(JSON.parse(raw) as FrontendResult);
    } catch (e) {
      setResult({ tokens: null, ast: null, error: String(e) });
    }
  }, []);

  useEffect(() => {
    if (!wasmReady) return;
    clearTimeout(debounce.current);
    debounce.current = setTimeout(() => runFrontend(source), 300);
    return () => clearTimeout(debounce.current);
  }, [source, wasmReady, runFrontend]);

  const runProgram = async () => {
    if (!BACKEND_URL) { setRunError('VITE_BACKEND_URL is not set.'); return; }
    setRunLoading(true); setRunError(''); setOutput('');
    try {
      const res = await fetch(`${BACKEND_URL}/run`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({ source }),
      });
      const data = await res.json();
      if (data.error) setRunError(data.error);
      else            setOutput(data.output ?? '');
    } catch {
      setRunError('Could not reach the backend.');
    } finally {
      setRunLoading(false);
    }
  };

  const fetchIR = async () => {
    if (!BACKEND_URL) { setIrError('VITE_BACKEND_URL is not set.'); return; }
    setIrLoading(true); setIrError(''); setIr('');
    try {
      const res = await fetch(`${BACKEND_URL}/compile`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({ source }),
      });
      const data = await res.json();
      if (data.error) setIrError(data.error);
      else            setIr(data.ir ?? '');
    } catch {
      setIrError('Could not reach the backend.');
    } finally {
      setIrLoading(false);
    }
  };

  return (
    <div className="app">
      <header className="header">
        <div className="brand">
          <span className="brand-mark">flux</span>
          <span className="brand-sep">/</span>
          <span className="brand-sub">compiler pipeline</span>
        </div>
        <div className="header-meta">
          <span className={`status-dot ${wasmReady ? 'ok' : 'warn'}`} aria-hidden />
          <span>{wasmReady ? 'wasm ready' : 'wasm loading'}</span>
        </div>
      </header>

      <main className="workspace">
        {/* ── Editor ────────────────────────────────────────────────────── */}
        <section className="pane editor-pane">
          <Editor
            height="100%"
            defaultLanguage="rust"
            theme="flux-dark"
            value={source}
            beforeMount={beforeEditorMount}
            onChange={v => setSource(v ?? '')}
            options={{
              fontSize: 13,
              fontFamily: "'JetBrains Mono', 'SF Mono', 'Cascadia Code', monospace",
              fontLigatures: true,
              minimap: { enabled: false },
              scrollBeyondLastLine: false,
              wordWrap: 'on',
              padding: { top: 16, bottom: 16 },
              renderLineHighlight: 'gutter',
              smoothScrolling: true,
              cursorBlinking: 'smooth',
              cursorSmoothCaretAnimation: 'on',
              guides: { indentation: false },
              overviewRulerLanes: 0,
              hideCursorInOverviewRuler: true,
              scrollbar: { vertical: 'auto', horizontal: 'auto', verticalScrollbarSize: 10 },
            }}
          />
        </section>

        {/* ── Output panel ──────────────────────────────────────────────── */}
        <section className="pane output-pane">
          <div className="tab-bar">
            {(['tokens', 'ast', 'ir', 'output'] as Tab[]).map(t => (
              <button
                key={t}
                className={`tab-btn ${activeTab === t ? 'active' : ''}`}
                onClick={() => setActiveTab(t)}
              >
                {t}
              </button>
            ))}
          </div>

          <div className="panel-body">
            {result.error && <div className="error-banner">{result.error}</div>}

            {/* ── Tokens ── */}
            {activeTab === 'tokens' && (
              !wasmReady
                ? <Placeholder>Loading wasm…</Placeholder>
                : result.tokens
                  ? (
                    <div className="scroll-area">
                      <table className="token-table">
                        <thead>
                          <tr><th>Type</th><th>Lexeme</th><th>Line</th><th>Col</th></tr>
                        </thead>
                        <tbody>
                          {result.tokens.map((tok, i) => (
                            <tr key={i}>
                              <td className="tok-type">{tok.type}</td>
                              <td className="tok-lex">{tok.lexeme || <span className="dim">·</span>}</td>
                              <td className="num">{tok.line}</td>
                              <td className="num">{tok.col}</td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                    </div>
                  )
                  : <Placeholder>Type something to see tokens.</Placeholder>
            )}

            {/* ── AST ── */}
            {activeTab === 'ast' && (
              <div className="ast-pane">
                <div className="subtab-bar">
                  <button
                    className={`subtab-btn ${astView === 'graph' ? 'active' : ''}`}
                    onClick={() => setAstView('graph')}
                  >Graph</button>
                  <button
                    className={`subtab-btn ${astView === 'json' ? 'active' : ''}`}
                    onClick={() => setAstView('json')}
                  >JSON</button>
                </div>
                {!wasmReady
                  ? <Placeholder>Loading wasm…</Placeholder>
                  : result.ast
                    ? (astView === 'graph'
                        ? <AstGraph ast={result.ast} />
                        : (
                          <div className="scroll-area ast-json">
                            <JSONTree
                              data={result.ast}
                              theme={JSON_THEME}
                              invertTheme={false}
                              hideRoot={false}
                              shouldExpandNodeInitially={(_, __, level) => level < 2}
                            />
                          </div>
                        ))
                    : <Placeholder>Type something to see the AST.</Placeholder>
                }
              </div>
            )}

            {/* ── IR ── */}
            {activeTab === 'ir' && (
              <div className="action-pane">
                <div className="action-bar">
                  <button className="action-btn" onClick={fetchIR} disabled={irLoading}>
                    {irLoading ? 'Generating…' : 'Generate IR'}
                  </button>
                </div>

                {irLoading && (
                  <div className="notice">
                    <span className="spinner" /> Waking up the backend — Render's free
                    tier can take ~30s on the first request.
                  </div>
                )}

                {irError && <div className="error-banner">{irError}</div>}

                {ir && (
                  <div className="scroll-area">
                    <SyntaxHighlighter
                      language="llvm"
                      style={atomOneDark}
                      customStyle={{
                        margin: 0,
                        padding: 16,
                        background: 'transparent',
                        fontSize: 12.5,
                        fontFamily: "'JetBrains Mono', 'SF Mono', monospace",
                        lineHeight: 1.55,
                      }}
                    >
                      {ir}
                    </SyntaxHighlighter>
                  </div>
                )}

                {!ir && !irLoading && !irError && (
                  <Placeholder>
                    Generate IR to compile via the backend.
                    <br />
                    <span className="dim">Tokens and AST update instantly in your browser.</span>
                  </Placeholder>
                )}
              </div>
            )}

            {/* ── Output ── */}
            {activeTab === 'output' && (
              <div className="action-pane">
                <div className="action-bar">
                  <button className="action-btn" onClick={runProgram} disabled={runLoading}>
                    {runLoading ? 'Running…' : 'Run'}
                  </button>
                </div>

                {runLoading && (
                  <div className="notice">
                    <span className="spinner" /> Compiling and running on the server…
                  </div>
                )}

                {runError && <div className="error-banner">{runError}</div>}

                {output && (
                  <div className="scroll-area">
                    <pre className="output-box">{output}</pre>
                  </div>
                )}

                {!output && !runLoading && !runError && (
                  <Placeholder>Run to compile and execute on the backend.</Placeholder>
                )}
              </div>
            )}
          </div>
        </section>
      </main>
    </div>
  );
}

function Placeholder({ children }: { children: React.ReactNode }) {
  return <div className="placeholder">{children}</div>;
}
