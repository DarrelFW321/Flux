import { useEffect, useMemo, useState } from 'react';
import type { CompileResult } from '../lib/compiler';
import { CodeView, DiffView } from './CodeView';

type Mode = 'final' | 'lowered' | 'passes';

interface IrPanelProps {
  result: CompileResult;
  optimize: boolean;
  disabled: string[];
  onToggleOptimize: () => void;
  onTogglePass: (name: string) => void;
  linkedSrc: number | null;
  follow: boolean;
  changed?: Set<number>;
  changeKey: number;
  onHoverSrc: (l: number | null) => void;
  onPickSrc: (l: number) => void;
}

export function IrPanel(props: IrPanelProps) {
  const { result, optimize, disabled } = props;
  const [mode, setMode] = useState<Mode>('final');
  const steps = result.passes.steps;
  const changedSteps = useMemo(() => steps.map((s, i) => ({ s, i })).filter(x => x.s.changes > 0), [steps]);
  const [sel, setSel] = useState(0);
  useEffect(() => { setSel(s => Math.min(s, Math.max(0, changedSteps.length - 1))); }, [changedSteps.length]);

  const raw = result.ir.raw ?? '';
  const opt = result.ir.opt ?? '';
  const totals = result.passes.totals;

  return (
    <div className="ir-panel">
      <div className="subbar">
        <div className="seg">
          {(['final', 'lowered', 'passes'] as Mode[]).map(m => (
            <button key={m} className={mode === m ? 'active' : ''} onClick={() => setMode(m)}>
              {m === 'final' ? 'Optimized' : m === 'lowered' ? 'Lowered' : 'Pass by pass'}
            </button>
          ))}
        </div>
        <span className="subbar-stat">
          {result.ir.instsBefore} → <strong>{result.ir.instsAfter}</strong> insts
          {optimize && <span className="dim"> · {result.ir.iterations} iteration{result.ir.iterations === 1 ? '' : 's'}</span>}
        </span>
      </div>

      <div className="pass-switches">
        <button className={`pass-switch master ${optimize ? 'on' : ''}`} onClick={props.onToggleOptimize}
          title="Toggle the whole optimization pipeline (-O0)">
          {optimize ? 'optimizer on' : 'optimizer off (-O0)'}
        </button>
        {result.passes.available.map(p => {
          const on = optimize && !disabled.includes(p.name);
          const n = totals.find(t => t.name === p.name)?.changes ?? 0;
          return (
            <button key={p.name} className={`pass-switch ${on ? 'on' : ''}`} disabled={!optimize}
              onClick={() => props.onTogglePass(p.name)} title={p.description}>
              {p.name}
              {on && <span className={`pass-count ${n ? 'hot' : ''}`}>{n}</span>}
            </button>
          );
        })}
      </div>

      {mode === 'final' && (
        <CodeView text={opt} lang="ir" map={result.ir.optMap} linkedSrc={props.linkedSrc} follow={props.follow}
          changed={props.changed} changeKey={props.changeKey} onHoverSrc={props.onHoverSrc} onPickSrc={props.onPickSrc} />
      )}
      {mode === 'lowered' && (
        <CodeView text={raw} lang="ir" map={result.ir.rawMap} linkedSrc={props.linkedSrc} follow={props.follow}
          onHoverSrc={props.onHoverSrc} onPickSrc={props.onPickSrc} />
      )}
      {mode === 'passes' && (
        changedSteps.length === 0 ? (
          <div className="placeholder">{optimize ? 'No pass changed this program.' : 'The optimizer is off.'}</div>
        ) : (
          <>
            <div className="timeline">
              <button className="tl-nav" disabled={sel === 0} onClick={() => setSel(s => s - 1)} aria-label="Previous pass">‹</button>
              <div className="tl-track">
                {changedSteps.map(({ s }, k) => (
                  <button key={k} className={`tl-step ${k === sel ? 'active' : ''}`} onClick={() => setSel(k)}
                    title={`${s.pass} · iteration ${s.iteration} · ${s.changes} change(s)`}>
                    <span className="tl-name">{s.pass}</span>
                    <span className="tl-meta">it {s.iteration} · {s.changes}</span>
                  </button>
                ))}
              </div>
              <button className="tl-nav" disabled={sel >= changedSteps.length - 1} onClick={() => setSel(s => s + 1)} aria-label="Next pass">›</button>
            </div>
            <PassDiff raw={raw} steps={changedSteps.map(x => x.s)} index={Math.min(sel, changedSteps.length - 1)} />
          </>
        )
      )}
    </div>
  );
}

function PassDiff({ raw, steps, index }: { raw: string; steps: CompileResult['passes']['steps']; index: number }) {
  const before = index === 0 ? raw : steps[index - 1].ir ?? '';
  const after = steps[index].ir ?? '';
  const description = DESCRIPTIONS[steps[index].pass];
  return (
    <>
      <div className="pass-caption">
        <strong>{steps[index].pass}</strong> <span className="dim">— {description}</span>
      </div>
      <DiffView before={before} after={after} lang="ir" />
    </>
  );
}

const DESCRIPTIONS: Record<string, string> = {
  'inline': 'copies single-exit helper bodies into their callers',
  'const-fold': 'evaluates operations whose operands are all constants',
  'simplify': 'applies algebraic identities and swizzle/extract peepholes',
  'branch-fold': 'removes ifs whose condition is a known constant',
  'forward': 'replaces loads with the value last stored to the variable',
  'dse': 'deletes stores nobody reads and variables nobody loads',
  'cse': 'merges identical pure computations',
  'licm': 'hoists loop-invariant work above its loop',
  'unroll': 'replicates constant-trip for-loop bodies',
  'dce': 'deletes unused values and unreachable functions',
};
