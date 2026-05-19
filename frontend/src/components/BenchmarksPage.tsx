import { useMemo, useState } from 'react';

interface BenchmarkRow {
  name: string;
  time_ms: number | null;
  output: string | null;
  error: string | null;
}

interface BenchmarkSources {
  flux: string;
  c: string;
  numpy: string;
}

export interface KernelReport {
  kernel: string;
  description: string;
  sources?: BenchmarkSources;
  results: BenchmarkRow[];
  error: string | null;
}

export interface BenchmarkReport {
  kernels: KernelReport[];
  error: string | null;
}

interface BenchmarksPageProps {
  report: BenchmarkReport | null;
  loading: boolean;
  error: string;
  onRun: () => void;
}

export function BenchmarksPage({ report, loading, error, onRun }: BenchmarksPageProps) {
  return (
    <div className="benchmarks-page">
      <header className="benchmarks-page-header">
        <div>
          <h1 className="benchmarks-page-title">Benchmarks</h1>
          <p className="benchmarks-page-lead">
            Fixed kernels from the <code>benchmarks/</code> directory in the repository — not
            the code in the Playground editor. Each kernel is implemented in Flux, hand-written
            C (<code>gcc -O2</code>), and NumPy so you can compare wall-clock time on the same
            workload.
          </p>
        </div>
        <button className="action-btn" onClick={onRun} disabled={loading} type="button">
          {loading ? 'Running…' : 'Run all benchmarks'}
        </button>
      </header>

      {loading && (
        <div className="notice benchmarks-notice">
          <span className="spinner" /> Compiling and running every kernel in three
          implementations. Cold starts can add ~30s; subsequent runs are instant.
        </div>
      )}

      {error && <div className="error-banner">{error}</div>}

      {report && !loading && (
        <div className="benchmarks-results scroll-area">
          {report.kernels.map(k => <KernelTable key={k.kernel} report={k} />)}
          <p className="bench-footnote">
            Lower is better. The bar shows speed relative to the fastest implementation
            for each kernel. Matching <code>output</code> values mean all three agree
            numerically.
          </p>
        </div>
      )}

      {!report && !loading && !error && (
        <div className="benchmarks-empty placeholder">
          <p>
            Four kernels: <strong>dot</strong>, <strong>saxpy</strong>, <strong>relu</strong>,
            and <strong>bigdot</strong>. Press Run to compile and time each one.
          </p>
          <p className="dim">
            Source for every kernel is available under View source (Flux, C, NumPy).
          </p>
        </div>
      )}
    </div>
  );
}

type BenchSourceLang = 'flux' | 'c' | 'numpy';

function KernelTable({ report }: { report: KernelReport }) {
  const [showSources, setShowSources] = useState(false);
  const [sourceLang, setSourceLang] = useState<BenchSourceLang>('flux');

  const fastestMs = useMemo(() => {
    const valid = report.results
      .map(r => r.time_ms)
      .filter((t): t is number => typeof t === 'number');
    return valid.length ? Math.min(...valid) : null;
  }, [report]);

  const sourceText = report.sources?.[sourceLang] ?? '';

  return (
    <div className="kernel-block">
      <header className="kernel-header">
        <span className="kernel-title">{report.kernel}</span>
        <span className="kernel-desc">{report.description}</span>
        {report.sources && (
          <button
            type="button"
            className="kernel-source-toggle"
            onClick={() => setShowSources(s => !s)}
          >
            {showSources ? 'Hide source' : 'View source'}
          </button>
        )}
      </header>

      {showSources && report.sources && (
        <div className="kernel-sources">
          <div className="kernel-source-tabs">
            {(['flux', 'c', 'numpy'] as BenchSourceLang[]).map(lang => (
              <button
                key={lang}
                type="button"
                className={`subtab-btn ${sourceLang === lang ? 'active' : ''}`}
                onClick={() => setSourceLang(lang)}
              >
                {lang === 'flux' ? 'Flux' : lang === 'c' ? 'C' : 'NumPy'}
              </button>
            ))}
          </div>
          <pre className="kernel-source-code"><code>{sourceText}</code></pre>
        </div>
      )}

      <table className="bench-table">
        <thead>
          <tr>
            <th>Implementation</th>
            <th className="num">Time</th>
            <th className="num">Relative</th>
            <th>Speed</th>
            <th>Output</th>
          </tr>
        </thead>
        <tbody>
          {report.results.map(row => {
            const ok       = typeof row.time_ms === 'number';
            const relative = ok && fastestMs != null ? row.time_ms! / fastestMs : null;
            const width    = ok && fastestMs != null
              ? Math.max(2, (fastestMs / row.time_ms!) * 100)
              : 0;
            return (
              <tr key={row.name}>
                <td className="bench-name">{row.name}</td>
                <td className="num">{ok ? `${row.time_ms!.toFixed(1)} ms` : '—'}</td>
                <td className="num">
                  {relative != null
                    ? (relative === 1 ? '1.00×' : `${relative.toFixed(2)}×`)
                    : '—'}
                </td>
                <td className="bench-bar-cell">
                  {ok && (
                    <div className="bench-bar-track">
                      <div
                        className={`bench-bar ${relative === 1 ? 'best' : ''}`}
                        style={{ width: `${width}%` }}
                      />
                    </div>
                  )}
                </td>
                <td className="bench-output" title={row.output ?? row.error ?? ''}>
                  {row.error
                    ? <span className="bench-err">error: {row.error}</span>
                    : <code>{row.output}</code>}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
