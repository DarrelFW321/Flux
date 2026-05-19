import { useState, useEffect, useRef, useCallback } from 'react';
import Editor from '@monaco-editor/react';
import { JSONTree } from 'react-json-tree';
import SyntaxHighlighter from 'react-syntax-highlighter';
import { vs2015 } from 'react-syntax-highlighter/dist/esm/styles/hljs';

// ── Types ─────────────────────────────────────────────────────────────────────

interface Token {
  type: string;
  lexeme: string;
  line: number;
  col: number;
}

interface FrontendResult {
  tokens: Token[] | null;
  ast: object | null;
  error: string | null;
}

type Tab = 'tokens' | 'ast' | 'ir';

// ── Constants ─────────────────────────────────────────────────────────────────

const DEFAULT_SOURCE = `fn dot(n: int) -> float {
  let sum: float = 0.0;
  let i: int = 0;
  while i < n {
    sum = sum + 1.0;
    i = i + 1;
  }
  return sum;
}

print(dot(100));`;

const BACKEND_URL = (import.meta as any).env?.VITE_BACKEND_URL ?? '';

// Dark theme for react-json-tree
const JSON_THEME = {
  scheme: 'flux',
  base00: '#1e1e1e',
  base01: '#252526',
  base02: '#2d2d2d',
  base03: '#555',
  base04: '#aaa',
  base05: '#d4d4d4',
  base06: '#e0e0e0',
  base07: '#fff',
  base08: '#f44747',
  base09: '#ce9178',
  base0A: '#dcdcaa',
  base0B: '#6a9955',
  base0C: '#9cdcfe',
  base0D: '#569cd6',
  base0E: '#c586c0',
  base0F: '#d7ba7d',
};

// ── App ───────────────────────────────────────────────────────────────────────

export default function App() {
  const [source, setSource]       = useState(DEFAULT_SOURCE);
  const [result, setResult]       = useState<FrontendResult>({ tokens: null, ast: null, error: null });
  const [activeTab, setActiveTab] = useState<Tab>('tokens');
  const [ir, setIr]               = useState('');
  const [irLoading, setIrLoading] = useState(false);
  const [irError, setIrError]     = useState('');
  const [wasmReady, setWasmReady] = useState(false);

  const fluxRef    = useRef<FluxWasm | null>(null);
  const debounce   = useRef<ReturnType<typeof setTimeout>>();

  // Load WASM module once on mount.
  useEffect(() => {
    if (typeof FluxModule === 'undefined') return; // not built yet
    FluxModule()
      .then(m => { fluxRef.current = m; setWasmReady(true); })
      .catch(() => {/* WASM unavailable in local dev without a build */});
  }, []);

  // Run frontend pipeline (debounced 300 ms) whenever source changes.
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

  // Fetch IR from Render backend.
  const fetchIR = async () => {
    if (!BACKEND_URL) { setIrError('VITE_BACKEND_URL is not set.'); return; }
    setIrLoading(true);
    setIrError('');
    setIr('');
    try {
      const res  = await fetch(`${BACKEND_URL}/compile`, {
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

  // ── Render ──────────────────────────────────────────────────────────────────
  return (
    <div className="app">
      <header className="header">
        <span className="logo">FLUX</span>
        <span className="subtitle">compiler pipeline visualizer</span>
        {!wasmReady && <span className="badge warn">WASM not loaded</span>}
      </header>

      <div className="workspace">
        {/* ── Editor ── */}
        <div className="editor-pane">
          <Editor
            height="100%"
            defaultLanguage="rust"
            theme="vs-dark"
            value={source}
            onChange={v => setSource(v ?? '')}
            options={{
              fontSize: 14,
              minimap: { enabled: false },
              scrollBeyondLastLine: false,
              wordWrap: 'on',
            }}
          />
        </div>

        {/* ── Output ── */}
        <div className="output-pane">
          <div className="tab-bar">
            {(['tokens', 'ast', 'ir'] as Tab[]).map(t => (
              <button
                key={t}
                className={`tab-btn ${activeTab === t ? 'active' : ''}`}
                onClick={() => setActiveTab(t)}
              >
                {t.toUpperCase()}
              </button>
            ))}
          </div>

          <div className="panel-body">
            {result.error && (
              <div className="error-banner">{result.error}</div>
            )}

            {/* ── Tokens ── */}
            {activeTab === 'tokens' && (
              !wasmReady
                ? <Placeholder>Loading WASM…</Placeholder>
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
                              <td className="tok-lex">{tok.lexeme || <em>∅</em>}</td>
                              <td>{tok.line}</td>
                              <td>{tok.col}</td>
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
              !wasmReady
                ? <Placeholder>Loading WASM…</Placeholder>
                : result.ast
                  ? (
                    <div className="scroll-area ast-tree">
                      <JSONTree
                        data={result.ast}
                        theme={JSON_THEME}
                        invertTheme={false}
                        hideRoot={false}
                        shouldExpandNodeInitially={(_, __, level) => level < 2}
                      />
                    </div>
                  )
                  : <Placeholder>Type something to see the AST.</Placeholder>
            )}

            {/* ── IR ── */}
            {activeTab === 'ir' && (
              <div className="ir-pane">
                <button
                  className="fetch-btn"
                  onClick={fetchIR}
                  disabled={irLoading}
                >
                  {irLoading ? 'Generating…' : '⚡ Generate IR'}
                </button>

                {irLoading && (
                  <div className="cold-start">
                    <span className="spinner" /> Waking up server — Render free tier
                    may take ~30 s on the first request.
                  </div>
                )}

                {irError && <div className="error-banner">{irError}</div>}

                {ir && (
                  <div className="scroll-area">
                    <SyntaxHighlighter
                      language="llvm"
                      style={vs2015}
                      customStyle={{ margin: 0, borderRadius: 0, fontSize: 13 }}
                    >
                      {ir}
                    </SyntaxHighlighter>
                  </div>
                )}

                {!ir && !irLoading && !irError && (
                  <Placeholder>
                    Click "Generate IR" to compile via the backend.
                    <br />
                    Tokens and AST update instantly (client-side WASM).
                  </Placeholder>
                )}
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

function Placeholder({ children }: { children: React.ReactNode }) {
  return <div className="placeholder">{children}</div>;
}
