import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import Editor, { type Monaco, type OnMount } from '@monaco-editor/react';
import type { editor as MonacoEditor } from 'monaco-editor';
import { AstGraph } from './AstGraph';
import { CodeView, DiffView, changedLines } from './components/CodeView';
import { DocsPane } from './components/DocsPane';
import { IrPanel } from './components/IrPanel';
import { Preview } from './components/Preview';
import { DEFAULT_EXAMPLE_ID, EXAMPLES } from './data/examples';
import { compileWith, loadCompiler, type AstNode, type CompileResult, type Diagnostic } from './lib/compiler';
import { BUILTINS, registerFlux } from './lib/fluxLanguage';
import type { ShaderError } from './lib/gpu';

type Tab = 'wgsl' | 'spirv' | 'ir' | 'ast' | 'tokens';
type Page = 'playground' | 'docs';

const defaultExample = EXAMPLES.find(e => e.id === DEFAULT_EXAMPLE_ID) ?? EXAMPLES[0];

function initialSource(): { id: string; source: string } {
  const m = /^#src=(.+)$/.exec(window.location.hash);
  if (m) {
    try {
      return { id: 'shared', source: decodeURIComponent(escape(atob(m[1]))) };
    } catch { /* fall through */ }
  }
  return { id: defaultExample.id, source: defaultExample.source };
}

function walkAst(n: AstNode, f: (n: AstNode) => void) {
  f(n);
  for (const c of n.children) walkAst(c.node, f);
}

function countAst(n: AstNode | null) {
  let k = 0;
  if (n) walkAst(n, () => k++);
  return k;
}

function download(name: string, data: BlobPart, type: string) {
  const url = URL.createObjectURL(new Blob([data], { type }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

interface Changes { changed: Set<number>; added: number; removed: number }
const NO_CHANGES: Changes = { changed: new Set(), added: 0, removed: 0 };

export default function App() {
  const init = useMemo(initialSource, []);
  const [page, setPage] = useState<Page>('playground');
  const [exampleId, setExampleId] = useState(init.id);
  const [source, setSource] = useState(init.source);
  const [wasm, setWasm] = useState<Awaited<ReturnType<typeof loadCompiler>> | null>(null);
  const [wasmError, setWasmError] = useState<string | null>(null);
  const [result, setResult] = useState<CompileResult | null>(null);
  const [lastOk, setLastOk] = useState<CompileResult | null>(null);
  const [prevOk, setPrevOk] = useState<CompileResult | null>(null);
  const [changeKey, setChangeKey] = useState(0);
  const [gpuErrors, setGpuErrors] = useState<ShaderError[]>([]);
  const [tab, setTab] = useState<Tab>('wgsl');
  const [showDiff, setShowDiff] = useState(false);
  const [optimize, setOptimize] = useState(true);
  const [disabled, setDisabled] = useState<string[]>([]);
  const [cursorLine, setCursorLine] = useState<number | null>(null);
  const [hoverSrc, setHoverSrc] = useState<number | null>(null);
  const [copied, setCopied] = useState(false);

  const editorRef = useRef<MonacoEditor.IStandaloneCodeEditor | null>(null);
  const monacoRef = useRef<Monaco | null>(null);
  const decoRef = useRef<MonacoEditor.IEditorDecorationsCollection | null>(null);
  const astRef = useRef<AstNode | null>(null);
  const lastOkRef = useRef<CompileResult | null>(null);

  useEffect(() => {
    loadCompiler().then(setWasm, e => setWasmError(String(e.message ?? e)));
  }, []);

  // ── Live compilation ──────────────────────────────────────────────────
  useEffect(() => {
    if (!wasm) return;
    const t = setTimeout(() => {
      const r = compileWith(wasm, source, { optimize, disabled });
      setResult(r);
      if (r.ast) astRef.current = r.ast;
      if (r.ok) {
        setPrevOk(lastOkRef.current);
        lastOkRef.current = r;
        setLastOk(r);
        setChangeKey(k => k + 1);
      }
    }, 120);
    return () => clearTimeout(t);
  }, [wasm, source, optimize, disabled]);

  const onShaderErrors = useCallback((errs: ShaderError[]) => setGpuErrors(errs), []);

  // Diagnostics: the compiler's, plus anything WebGPU reports about the WGSL,
  // mapped back to Flux source lines through the WGSL source map.
  const diagnostics: Diagnostic[] = useMemo(() => {
    const ds = [...(result?.diagnostics ?? [])];
    if (result?.ok && lastOk === result) {
      for (const e of gpuErrors) {
        const src = e.line > 0 ? lastOk.wgsl.map[e.line - 1] ?? 0 : 0;
        ds.push({ severity: 'error', message: `WebGPU: ${e.message}`, line: src || 1, col: 1, len: 1, source: 'webgpu' });
      }
    }
    return ds;
  }, [result, gpuErrors, lastOk]);

  const errors = diagnostics.filter(d => d.severity === 'error').length;
  const warnings = diagnostics.filter(d => d.severity === 'warning').length;

  // Editor markers
  useEffect(() => {
    const monaco = monacoRef.current;
    const model = editorRef.current?.getModel();
    if (!monaco || !model) return;
    monaco.editor.setModelMarkers(model, 'flux', diagnostics.map(d => ({
      severity: d.severity === 'error' ? monaco.MarkerSeverity.Error
              : d.severity === 'warning' ? monaco.MarkerSeverity.Warning : monaco.MarkerSeverity.Info,
      message: d.message,
      startLineNumber: Math.max(1, d.line),
      startColumn: Math.max(1, d.col),
      endLineNumber: Math.max(1, d.line),
      endColumn: Math.max(1, d.col) + Math.max(1, d.len),
    })));
  }, [diagnostics]);

  // Highlight the source line linked to whatever output line is hovered.
  useEffect(() => {
    const ed = editorRef.current;
    const monaco = monacoRef.current;
    if (!ed || !monaco) return;
    decoRef.current ??= ed.createDecorationsCollection();
    decoRef.current.set(hoverSrc ? [{
      range: new monaco.Range(hoverSrc, 1, hoverSrc, 1),
      options: { isWholeLine: true, className: 'src-linked-line', linesDecorationsClassName: 'src-linked-gutter' },
    }] : []);
  }, [hoverSrc]);

  const onMount: OnMount = (ed, monaco) => {
    editorRef.current = ed;
    monacoRef.current = monaco;
    ed.onDidChangeCursorPosition(e => setCursorLine(e.position.lineNumber));
    // Hover: show the inferred type of the expression under the cursor.
    monaco.languages.registerHoverProvider('flux', {
      provideHover(_model, pos) {
        const ast = astRef.current;
        if (!ast) return null;
        let best: AstNode | null = null;
        walkAst(ast, n => {
          if (n.cat !== 'expr' || n.line !== pos.lineNumber || !n.type) return;
          const len = n.label === 'Swizzle' ? n.detail.length - 1
                    : n.label === 'Call' || n.label === 'Builtin' ? n.detail.length - 2
                    : n.label === 'Construct' || n.label === 'Ident' ? n.detail.length : 0;
          if (len > 0 && pos.column >= n.col && pos.column < n.col + len) best = n;
        });
        const n = best as AstNode | null;
        if (!n) return null;
        const name = n.label === 'Swizzle' ? n.detail : n.detail.replace(/\(\)$/, '');
        const lines = [`\`\`\`flux\n${name}: ${n.type}\n\`\`\``];
        if (n.label === 'Builtin' && BUILTINS[name]) lines.push(BUILTINS[name]);
        return { contents: lines.map(value => ({ value })) };
      },
    });
  };

  const pickSrc = useCallback((line: number) => {
    const ed = editorRef.current;
    if (!ed) return;
    ed.revealLineInCenterIfOutsideViewport(line);
    ed.setPosition({ lineNumber: line, column: 1 });
    ed.focus();
  }, []);

  const jumpTo = (d: Diagnostic) => {
    const ed = editorRef.current;
    if (!ed) return;
    ed.revealLineInCenter(d.line);
    ed.setPosition({ lineNumber: d.line, column: d.col });
    ed.focus();
  };

  const loadExample = (id: string) => {
    const ex = EXAMPLES.find(e => e.id === id);
    if (!ex) return;
    setExampleId(id);
    setSource(ex.source);
    lastOkRef.current = null;   // a new example isn't an "edit": don't diff against the old one
    if (window.location.hash) history.replaceState(null, '', window.location.pathname);
  };

  const share = async () => {
    const url = `${window.location.origin}${window.location.pathname}#src=${btoa(unescape(encodeURIComponent(source)))}`;
    history.replaceState(null, '', url);
    try { await navigator.clipboard.writeText(url); } catch { /* the URL bar still has it */ }
    setCopied(true);
    setTimeout(() => setCopied(false), 1500);
  };

  // ── What changed since the previous successful compile ───────────────
  const same = !prevOk || !lastOk;
  const wgslChanges = useMemo<Changes>(() => same ? NO_CHANGES : changedLines(prevOk!.wgsl.code, lastOk!.wgsl.code), [prevOk, lastOk, same]);
  const spirvChanges = useMemo<Changes>(() => same ? NO_CHANGES : changedLines(prevOk!.spirv.text, lastOk!.spirv.text), [prevOk, lastOk, same]);
  const irChanges = useMemo<Changes>(() => same ? NO_CHANGES : changedLines(prevOk!.ir.opt, lastOk!.ir.opt), [prevOk, lastOk, same]);

  const fragmentEntry = lastOk?.reflection.entries.find(e => e.stage === 'fragment')?.name ?? null;
  const linked = hoverSrc ?? cursorLine;
  const compileMs = result?.timings.reduce((s, t) => s + t.ms, 0) ?? 0;
  const view = lastOk;   // outputs show the last good compile while the source has errors
  const stale = !!result && !result.ok;

  const tabs: { id: Tab; label: string; stat: string; changes?: Changes }[] = [
    { id: 'tokens', label: 'Tokens', stat: String(result?.tokens.length ?? 0) },
    { id: 'ast', label: 'AST', stat: `${countAst(result?.ast ?? null)} nodes` },
    { id: 'ir', label: 'FluxIR', stat: `${view?.ir.instsAfter ?? 0} insts`, changes: irChanges },
    { id: 'wgsl', label: 'WGSL', stat: `${(view?.wgsl.code ?? '').split('\n').length - 1} lines`, changes: wgslChanges },
    { id: 'spirv', label: 'SPIR-V', stat: `${((view?.spirv.words?.length ?? 0) * 4 / 1024).toFixed(1)} KB`, changes: spirvChanges },
  ];

  const common = {
    linkedSrc: linked,
    follow: hoverSrc === null,
    changeKey,
    onHoverSrc: setHoverSrc,
    onPickSrc: pickSrc,
  };

  return (
    <div className="app">
      <header className="topbar">
        <span className="brand">flux</span>
        <nav className="topnav">
          <button className={page === 'playground' ? 'active' : ''} onClick={() => setPage('playground')}>Playground</button>
          <button className={page === 'docs' ? 'active' : ''} onClick={() => setPage('docs')}>Reference</button>
        </nav>
        <a className="toplink" href="https://github.com/DarrelFW321/flux" target="_blank" rel="noopener noreferrer">GitHub</a>
      </header>

      {page === 'docs' ? <DocsPane /> : (
        <main className="workspace">
          {/* ── Editor ─────────────────────────────────────────────── */}
          <section className="panel editor-panel">
            <div className="panel-head">
              <select className="example-select" value={exampleId} onChange={e => loadExample(e.target.value)} aria-label="Example">
                {exampleId === 'shared' && <option value="shared">Shared shader</option>}
                {EXAMPLES.map(ex => <option key={ex.id} value={ex.id}>{ex.title}</option>)}
              </select>
              <span className="panel-sub">{EXAMPLES.find(e => e.id === exampleId)?.description ?? 'from a shared link'}</span>
              <button className="ghost-btn" onClick={share}>{copied ? 'Link copied' : 'Share'}</button>
            </div>
            <div className="editor-wrap">
              <Editor
                height="100%"
                language="flux"
                theme="flux-dark"
                value={source}
                beforeMount={registerFlux}
                onMount={onMount}
                onChange={v => setSource(v ?? '')}
                options={{
                  fontSize: 13,
                  fontFamily: "'JetBrains Mono', 'Cascadia Code', monospace",
                  fontLigatures: false,
                  minimap: { enabled: false },
                  scrollBeyondLastLine: false,
                  padding: { top: 14, bottom: 14 },
                  renderLineHighlight: 'all',
                  smoothScrolling: true,
                  cursorBlinking: 'smooth',
                  cursorSmoothCaretAnimation: 'on',
                  guides: { indentation: false },
                  overviewRulerLanes: 0,
                  tabSize: 4,
                  automaticLayout: true,
                }}
              />
            </div>
            <div className={`diag-panel ${errors ? 'has-errors' : ''}`}>
              <div className="diag-head">
                <span className={`diag-count err ${errors ? 'on' : ''}`}>{errors} error{errors === 1 ? '' : 's'}</span>
                <span className={`diag-count warn ${warnings ? 'on' : ''}`}>{warnings} warning{warnings === 1 ? '' : 's'}</span>
                {!errors && !warnings && result?.ok && <span className="diag-ok">No problems</span>}
              </div>
              {diagnostics.length > 0 && (
                <ul className="diag-list">
                  {diagnostics.map((d, i) => (
                    <li key={i} className={`diag diag-${d.severity}`} onClick={() => jumpTo(d)}>
                      <span className="diag-loc">{d.line}:{d.col}</span>
                      <span className="diag-msg">{d.message}</span>
                    </li>
                  ))}
                </ul>
              )}
            </div>
          </section>

          {/* ── Preview + inspector ────────────────────────────────── */}
          <div className="right-col">
            <section className="panel preview-panel">
              <Preview
                wgsl={view?.wgsl.code ?? null}
                entry={fragmentEntry}
                uniforms={view?.reflection.uniforms ?? EMPTY}
                uniformSize={view?.reflection.uniformSize ?? 0}
                onShaderErrors={onShaderErrors}
              />
            </section>

            <section className="panel inspector">
              <div className="stage-tabs" role="tablist">
                {tabs.map(t => (
                  <button key={t.id} role="tab" className={`stage-tab ${tab === t.id ? 'active' : ''}`} onClick={() => setTab(t.id)}>
                    <span className="stage-label">{t.label}</span>
                    <span className="stage-stat">{t.stat}</span>
                    {t.changes && (t.changes.added + t.changes.removed > 0) && (
                      <span className="stage-delta" title="lines changed by your last edit">
                        +{t.changes.added} −{t.changes.removed}
                      </span>
                    )}
                  </button>
                ))}
              </div>

              <div className="inspector-body">
                {!wasm && <div className="placeholder">{wasmError ?? 'Loading the compiler…'}</div>}
                {wasm && stale && (tab === 'wgsl' || tab === 'spirv' || tab === 'ir') && (
                  <div className="stale-banner">Out of date: showing the last build without errors.</div>
                )}

                {wasm && (tab === 'wgsl' || tab === 'spirv') && view && (
                  <>
                    <div className="subbar">
                      <div className="seg">
                        <button className={!showDiff ? 'active' : ''} onClick={() => setShowDiff(false)}>Output</button>
                        <button className={showDiff ? 'active' : ''} onClick={() => setShowDiff(true)} disabled={!prevOk} title="Compare with the output before your last edit">Diff</button>
                      </div>
                      <span className="subbar-fill" />
                      <button className="ghost-btn" onClick={() => tab === 'wgsl'
                        ? download('shader.wgsl', view.wgsl.code ?? '', 'text/plain')
                        : download('shader.spv', new Uint32Array(view.spirv.words ?? []), 'application/octet-stream')}>
                        Download
                      </button>
                    </div>
                    {showDiff && prevOk ? (
                      <DiffView
                        before={(tab === 'wgsl' ? prevOk.wgsl.code : prevOk.spirv.text) ?? ''}
                        after={(tab === 'wgsl' ? view.wgsl.code : view.spirv.text) ?? ''}
                        lang={tab}
                      />
                    ) : tab === 'wgsl' ? (
                      <CodeView text={view.wgsl.code ?? ''} lang="wgsl" map={view.wgsl.map} changed={wgslChanges.changed} {...common} />
                    ) : (
                      <CodeView text={view.spirv.text ?? ''} lang="spirv" map={view.spirv.map} changed={spirvChanges.changed} {...common} />
                    )}
                  </>
                )}

                {wasm && tab === 'ir' && view && (
                  <IrPanel
                    result={view}
                    optimize={optimize}
                    disabled={disabled}
                    onToggleOptimize={() => setOptimize(o => !o)}
                    onTogglePass={name => setDisabled(d => d.includes(name) ? d.filter(x => x !== name) : [...d, name])}
                    changed={irChanges.changed}
                    {...common}
                  />
                )}

                {wasm && tab === 'ast' && (result?.ast
                  ? <AstGraph ast={result.ast} linkedSrc={linked} onHoverSrc={setHoverSrc} onPickSrc={pickSrc} />
                  : <div className="placeholder">Fix the syntax error to see the tree.</div>)}

                {wasm && tab === 'tokens' && result && (
                  <div className="token-scroll">
                    <table className="token-table">
                      <thead><tr><th>Line:Col</th><th>Kind</th><th>Lexeme</th></tr></thead>
                      <tbody>
                        {result.tokens.map((t, i) => (
                          <tr key={i} className={linked === t.line ? 'linked' : ''} onMouseEnter={() => setHoverSrc(t.line)}
                            onMouseLeave={() => setHoverSrc(null)} onClick={() => pickSrc(t.line)}>
                            <td className="num">{t.line}:{t.col}</td>
                            <td className={`tok tok-${tokenClass(t.type)}`}>{t.type}</td>
                            <td className="lex">{t.lexeme}</td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </div>
                )}
              </div>

            </section>
          </div>
        </main>
      )}

      <footer className="statusbar">
        <span>{wasm ? 'fluxc (wasm)' : wasmError ? wasmError : 'Loading compiler…'}</span>
        {wasm && result && (
          <span className="status-timings" title="Time per compiler stage, measured in the browser">
            {result.timings.map(t => `${t.stage} ${t.ms < 1 ? t.ms.toFixed(2) : t.ms.toFixed(1)}`).join('  ·  ')}
          </span>
        )}
        {wasm && result && <span className="status-total">{compileMs.toFixed(1)} ms</span>}
      </footer>
    </div>
  );
}

const EMPTY: never[] = [];

function tokenClass(type: string) {
  if (type.startsWith('KW_')) return 'kw';
  if (type === 'TYPE') return 'type';
  if (type.endsWith('_LIT')) return 'lit';
  if (type === 'IDENTIFIER') return 'ident';
  return 'punct';
}
