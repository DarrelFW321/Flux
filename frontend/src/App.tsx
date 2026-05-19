import { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import Editor, { Monaco } from '@monaco-editor/react';
import { JSONTree } from 'react-json-tree';
import SyntaxHighlighter from 'react-syntax-highlighter';
import { atomOneDark } from 'react-syntax-highlighter/dist/esm/styles/hljs';
import { diffLines } from 'diff';
import { AstGraph } from './AstGraph';
import { Sidebar, type SidebarView } from './components/Sidebar';
import { DocsPane } from './components/DocsPane';
import { InspectorNav } from './components/InspectorNav';
import { BenchmarksPage, type BenchmarkReport } from './components/BenchmarksPage';
import { WelcomeBanner } from './components/WelcomeBanner';
import { Footer } from './components/Footer';
import { EXAMPLES, DEFAULT_EXAMPLE_ID, type FluxExample } from './data/examples';

// ── Types ─────────────────────────────────────────────────────────────────────

interface Token {
  type: string;
  lexeme: string;
  line: number;
  col: number;
}

interface PassResult {
  name: string;
  changes: number;
}

interface PassStep {
  name: string;
  iteration: number;
  changes: number;
  mir_after: string;
}

interface FrontendResult {
  tokens: Token[] | null;
  ast: any | null;
  mir_raw: string | null;
  mir_optimized: string | null;
  passes: PassResult[] | null;
  pass_steps: PassStep[] | null;
  error: string | null;
}

type Tab = 'tokens' | 'ast' | 'mir' | 'ir' | 'output';
type AppPage = 'playground' | 'benchmarks';
type AstView = 'graph' | 'json';
type MirView = 'optimized' | 'raw' | 'diff';

// ── Constants ─────────────────────────────────────────────────────────────────

const defaultExample = EXAMPLES.find(e => e.id === DEFAULT_EXAMPLE_ID) ?? EXAMPLES[0];

const BACKEND_URL = (import.meta as any).env?.VITE_BACKEND_URL ?? '';

const PIPELINE_TABS: Tab[] = ['tokens', 'ast', 'mir', 'ir'];
const WELCOME_DISMISS_KEY = 'flux-welcome-dismissed';

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
  const [source, setSource]           = useState(defaultExample.source);
  const [selectedExampleId, setSelectedExampleId] = useState(defaultExample.id);
  const [sidebarView, setSidebarView] = useState<SidebarView>('examples');
  const [activeDocId, setActiveDocId] = useState('overview');
  const [lastPipelineTab, setLastPipelineTab] = useState<Tab>('tokens');
  const [appPage, setAppPage]             = useState<AppPage>('playground');
  const [welcomeDismissed, setWelcomeDismissed] = useState(() => {
    try { return localStorage.getItem(WELCOME_DISMISS_KEY) === '1'; } catch { return false; }
  });
  const [result, setResult]           = useState<FrontendResult>({
    tokens: null, ast: null, mir_raw: null, mir_optimized: null,
    passes: null, pass_steps: null, error: null,
  });
  const [activeTab, setActiveTab]     = useState<Tab>('tokens');
  const [astView, setAstView]         = useState<AstView>('graph');
  const [mirView, setMirView]         = useState<MirView>('optimized');
  const [ir, setIr]                   = useState('');
  const [irLoading, setIrLoading]     = useState(false);
  const [irError, setIrError]         = useState('');
  const [output, setOutput]           = useState('');
  const [runLoading, setRunLoading]   = useState(false);
  const [runError, setRunError]       = useState('');
  const [wasmReady, setWasmReady]     = useState(false);
  const [bench, setBench]             = useState<BenchmarkReport | null>(null);
  const [benchLoading, setBenchLoading] = useState(false);
  const [benchError, setBenchError]   = useState('');

  const fluxRef  = useRef<FluxWasm | null>(null);
  const debounce = useRef<ReturnType<typeof setTimeout>>();

  useEffect(() => {
    if (typeof FluxModule === 'undefined') return;
    FluxModule()
      .then(m => { fluxRef.current = m; setWasmReady(true); })
      .catch(() => {/* WASM unavailable in local dev without a build */});
  }, []);

  const dismissWelcome = () => {
    setWelcomeDismissed(true);
    try { localStorage.setItem(WELCOME_DISMISS_KEY, '1'); } catch { /* ignore */ }
  };

  const runFrontend = useCallback((src: string) => {
    if (!fluxRef.current) return;
    try {
      const raw = fluxRef.current.compile_frontend(src);
      setResult(JSON.parse(raw) as FrontendResult);
    } catch (e) {
      setResult({
        tokens: null, ast: null, mir_raw: null, mir_optimized: null,
        passes: null, pass_steps: null, error: String(e),
      });
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

  const runBenchmark = async () => {
    if (!BACKEND_URL) { setBenchError('VITE_BACKEND_URL is not set.'); return; }
    setBenchLoading(true); setBenchError(''); setBench(null);
    try {
      const res  = await fetch(`${BACKEND_URL}/benchmark`, { method: 'POST' });
      const data = await res.json() as BenchmarkReport;
      if (data.error) setBenchError(data.error);
      else            setBench(data);
    } catch {
      setBenchError('Could not reach the backend.');
    } finally {
      setBenchLoading(false);
    }
  };

  const loadExample = (ex: FluxExample) => {
    setSelectedExampleId(ex.id);
    setSource(ex.source);
    setSidebarView('examples');
  };

  const selectInspectorTab = (tab: Tab) => {
    if (PIPELINE_TABS.includes(tab)) setLastPipelineTab(tab);
    setActiveTab(tab);
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
          <span className="brand-sub">compiler visualizer</span>
        </div>
        <nav className="header-pages" aria-label="Main sections">
          <button
            type="button"
            className={`header-page-btn ${appPage === 'playground' ? 'active' : ''}`}
            onClick={() => setAppPage('playground')}
          >
            Playground
          </button>
          <button
            type="button"
            className={`header-page-btn ${appPage === 'benchmarks' ? 'active' : ''}`}
            onClick={() => setAppPage('benchmarks')}
          >
            Benchmarks
          </button>
        </nav>
        <div className="header-meta">
          <span className={`status-dot ${wasmReady ? 'ok' : 'warn'}`} aria-hidden />
          <span>{wasmReady ? 'wasm ready' : 'wasm loading'}</span>
        </div>
      </header>

      {!welcomeDismissed && (
        <WelcomeBanner
          onDismiss={dismissWelcome}
          onGoPlayground={() => { setAppPage('playground'); dismissWelcome(); }}
          onGoBenchmarks={() => { setAppPage('benchmarks'); dismissWelcome(); }}
        />
      )}

      {appPage === 'benchmarks' ? (
        <main className="benchmarks-workspace">
          <BenchmarksPage
            report={bench}
            loading={benchLoading}
            error={benchError}
            onRun={runBenchmark}
          />
        </main>
      ) : (
      <main className="workspace">
        <Sidebar
          view={sidebarView}
          onViewChange={setSidebarView}
          selectedExampleId={selectedExampleId}
          onSelectExample={loadExample}
          activeDocId={activeDocId}
          onSelectDoc={id => { setActiveDocId(id); setSidebarView('docs'); }}
        />

        <section className="pane center-pane">
          {sidebarView === 'docs' ? (
            <DocsPane activeDocId={activeDocId} />
          ) : (
            <>
              <div className="editor-toolbar">
                <span className="editor-label">
                  {EXAMPLES.find(e => e.id === selectedExampleId)?.title ?? 'Editor'}
                </span>
              </div>
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
            </>
          )}
        </section>

        <section className="pane inspector-pane">
          <InspectorNav
            activeTab={activeTab}
            lastPipelineTab={lastPipelineTab as 'tokens' | 'ast' | 'mir' | 'ir'}
            onTabChange={selectInspectorTab}
          />

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

            {/* ── MIR ── */}
            {activeTab === 'mir' && (
              <MirPane
                wasmReady={wasmReady}
                raw={result.mir_raw}
                optimized={result.mir_optimized}
                passes={result.passes}
                steps={result.pass_steps}
                view={mirView}
                onViewChange={setMirView}
              />
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
                    <span className="spinner" /> Generating LLVM IR. Cold starts can
                    add ~30s; subsequent requests are instant.
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
                    <span className="spinner" /> Compiling and executing. Cold starts
                    can add ~30s; subsequent runs are instant.
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
      )}

      <Footer />
    </div>
  );
}

function Placeholder({ children }: { children: React.ReactNode }) {
  return <div className="placeholder">{children}</div>;
}

// ── MIR pane ────────────────────────────────────────────────────────────────

interface MirPaneProps {
  wasmReady: boolean;
  raw: string | null;
  optimized: string | null;
  passes: PassResult[] | null;
  steps: PassStep[] | null;
  view: MirView;
  onViewChange: (v: MirView) => void;
}

function MirPane({
  wasmReady, raw, optimized, passes, steps, view, onViewChange,
}: MirPaneProps) {
  return (
    <div className="ast-pane">
      <div className="subtab-bar mir-subtab-bar">
        <div className="subtab-group">
          {(['optimized', 'raw', 'diff'] as MirView[]).map(v => (
            <button
              key={v}
              className={`subtab-btn ${view === v ? 'active' : ''}`}
              onClick={() => onViewChange(v)}
            >
              {v}
            </button>
          ))}
        </div>
        {passes && passes.length > 0 && (
          <div className="pass-chips" title="optimization passes applied">
            {passes.map((p, i) => (
              <span key={p.name + i} className={`pass-chip ${p.changes ? 'changed' : ''}`}>
                {p.name}
                <span className="pass-chip-count">{p.changes}</span>
              </span>
            ))}
          </div>
        )}
      </div>

      {!wasmReady ? (
        <Placeholder>Loading wasm…</Placeholder>
      ) : !raw ? (
        <Placeholder>
          Type something to see the mid-level IR.
          <br />
          <span className="dim">
            FluxIR sits between the AST and LLVM. Whole-array ops appear as
            single instructions; optimization passes run before LLVM codegen.
          </span>
        </Placeholder>
      ) : view === 'diff' ? (
        <MirDiff raw={raw} steps={steps ?? []} />
      ) : (
        <div className="scroll-area mir-pane">
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
            {view === 'optimized' ? (optimized ?? raw) : raw}
          </SyntaxHighlighter>
        </div>
      )}
    </div>
  );
}

// ── Step-driven diff ────────────────────────────────────────────────────────

interface MirDiffProps {
  raw: string;
  steps: PassStep[];
}

function MirDiff({ raw, steps }: MirDiffProps) {
  // Default selection: the last step that actually produced changes, so the
  // diff is non-empty on first load. Fall back to the last step.
  const defaultIdx = useMemo(() => {
    for (let i = steps.length - 1; i >= 0; --i) if (steps[i].changes > 0) return i;
    return steps.length - 1;
  }, [steps]);

  const [selected, setSelected] = useState(defaultIdx);
  // Re-clamp when the program changes (steps array reference changes).
  useEffect(() => { setSelected(defaultIdx); }, [defaultIdx]);

  const totalChanges = useMemo(
    () => steps.reduce((s, p) => s + p.changes, 0),
    [steps],
  );

  if (steps.length === 0) {
    return <Placeholder>No passes ran.</Placeholder>;
  }
  if (totalChanges === 0) {
    return (
      <Placeholder>
        Optimization passes produced no changes for this program.
        <br />
        <span className="dim">Try adding `let x = 2 + 3 * 4;` or `let y = a * 0;`.</span>
      </Placeholder>
    );
  }

  const safeIdx = Math.min(Math.max(selected, 0), steps.length - 1);
  const current = steps[safeIdx];
  const before  = safeIdx === 0 ? raw : steps[safeIdx - 1].mir_after;
  const after   = current.mir_after;

  return (
    <div className="mir-diff-container">
      <PassTimeline steps={steps} selectedIdx={safeIdx} onSelect={setSelected} />
      <div className="diff-header">
        <span className="diff-header-label">
          step {safeIdx + 1} / {steps.length} —{' '}
          <strong>{current.name}</strong>{' '}
          <span className="dim">iter {current.iteration}</span>
        </span>
        <span className={`diff-header-changes ${current.changes ? 'has-changes' : 'no-changes'}`}>
          {current.changes} change{current.changes === 1 ? '' : 's'}
        </span>
      </div>
      {current.changes === 0 ? (
        <div className="diff-empty">
          <span className="dim">This pass made no changes at this iteration.</span>
        </div>
      ) : (
        <DiffBody before={before} after={after} />
      )}
    </div>
  );
}

function PassTimeline({
  steps, selectedIdx, onSelect,
}: { steps: PassStep[]; selectedIdx: number; onSelect: (i: number) => void }) {
  return (
    <div className="pass-timeline">
      <button
        className="timeline-nav"
        onClick={() => onSelect(Math.max(selectedIdx - 1, 0))}
        disabled={selectedIdx === 0}
        aria-label="previous pass"
      >
        ‹
      </button>
      <div className="timeline-track">
        {steps.map((step, idx) => {
          const isActive = idx === selectedIdx;
          const changed  = step.changes > 0;
          return (
            <button
              key={idx}
              className={`timeline-step ${isActive ? 'active' : ''} ${changed ? 'changed' : 'noop'}`}
              onClick={() => onSelect(idx)}
              title={`${step.name} · iteration ${step.iteration} · ${step.changes} change${step.changes === 1 ? '' : 's'}`}
            >
              <span className="timeline-dot" />
              <span className="timeline-name">{step.name}</span>
              <span className="timeline-count">{step.changes}</span>
            </button>
          );
        })}
      </div>
      <button
        className="timeline-nav"
        onClick={() => onSelect(Math.min(selectedIdx + 1, steps.length - 1))}
        disabled={selectedIdx === steps.length - 1}
        aria-label="next pass"
      >
        ›
      </button>
    </div>
  );
}

function DiffBody({ before, after }: { before: string; after: string }) {
  const parts = useMemo(() => diffLines(before, after), [before, after]);
  return (
    <div className="scroll-area mir-diff">
      <pre>
        {parts.flatMap((part, partIdx) => {
          const lines = part.value.split('\n');
          if (lines.length && lines[lines.length - 1] === '') lines.pop();
          const cls    = part.added ? 'added' : part.removed ? 'removed' : 'context';
          const prefix = part.added ? '+' : part.removed ? '−' : ' ';
          return lines.map((line, i) => (
            <div key={`${partIdx}-${i}`} className={`diff-line ${cls}`}>
              <span className="diff-marker">{prefix}</span>
              <span className="diff-text">{line || '\u00a0'}</span>
            </div>
          ));
        })}
      </pre>
    </div>
  );
}
