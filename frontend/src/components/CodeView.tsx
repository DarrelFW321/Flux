import { memo, useEffect, useMemo, useRef } from 'react';
import { diffLines } from 'diff';
import { highlightLine, type Lang } from '../lib/highlight';

// Lines of `next` that are new or modified relative to `prev`.
export function changedLines(prev: string | null | undefined, next: string | null | undefined) {
  const changed = new Set<number>();
  let added = 0, removed = 0;
  if (!next || prev == null || prev === next) return { changed, added, removed };
  let line = 0;
  for (const part of diffLines(prev, next)) {
    const n = part.count ?? part.value.split('\n').length - 1;
    if (part.added) {
      for (let k = 0; k < n; k++) changed.add(line + k);
      added += n;
      line += n;
    } else if (part.removed) {
      removed += n;
    } else {
      line += n;
    }
  }
  return { changed, added, removed };
}

interface CodeViewProps {
  text: string;
  lang: Lang;
  map?: number[];                   // output line → source line (0 = none)
  linkedSrc?: number | null;        // highlight output lines produced by this source line
  follow?: boolean;                 // scroll the first linked line into view
  changed?: Set<number>;            // lines that changed in the last compile
  changeKey?: number;               // bump to replay the change flash
  onHoverSrc?: (line: number | null) => void;
  onPickSrc?: (line: number) => void;
}

export const CodeView = memo(function CodeView({
  text, lang, map, linkedSrc, follow, changed, changeKey, onHoverSrc, onPickSrc,
}: CodeViewProps) {
  const lines = useMemo(() => {
    const ls = text.replace(/\n$/, '').split('\n');
    return ls.map(l => highlightLine(l, lang));
  }, [text, lang]);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!follow || !linkedSrc || !map || !ref.current) return;
    const idx = map.findIndex(s => s === linkedSrc);
    if (idx < 0) return;
    const el = ref.current.querySelector<HTMLElement>(`[data-line="${idx}"]`);
    if (!el) return;
    const box = ref.current.getBoundingClientRect();
    const r = el.getBoundingClientRect();
    if (r.top < box.top + 24 || r.bottom > box.bottom - 24)
      el.scrollIntoView({ block: 'center', behavior: 'smooth' });
  }, [linkedSrc, follow, map]);

  return (
    <div className="code-view" ref={ref} onMouseLeave={() => onHoverSrc?.(null)}>
      <div className="code-lines" key={changeKey}>
        {lines.map((spans, i) => {
          const src = map?.[i] ?? 0;
          const cls = ['cv-line'];
          if (linkedSrc && src === linkedSrc) cls.push('linked');
          if (changed?.has(i)) cls.push('changed');
          if (src) cls.push('mapped');
          return (
            <div
              key={i}
              data-line={i}
              className={cls.join(' ')}
              onMouseEnter={() => onHoverSrc?.(src || null)}
              onClick={() => src && onPickSrc?.(src)}
              title={src ? `from source line ${src}` : undefined}
            >
              <span className="cv-ln">{i + 1}</span>
              <span className="cv-src">{src ? src : ''}</span>
              <span className="cv-code">
                {spans.length === 0 ? ' ' : spans.map(([c, t], k) => (c ? <span key={k} className={c}>{t}</span> : t))}
              </span>
            </div>
          );
        })}
      </div>
    </div>
  );
});

export function DiffView({ before, after, lang }: { before: string; after: string; lang: Lang }) {
  const parts = useMemo(() => diffLines(before, after), [before, after]);
  const ref = useRef<HTMLDivElement>(null);
  // Bring the first change into view: that's what the diff is for.
  useEffect(() => {
    const box = ref.current;
    const first = box?.querySelector<HTMLElement>('.diff-added, .diff-removed');
    if (box && first) box.scrollTop = Math.max(0, first.offsetTop - box.clientHeight / 3);
  }, [parts]);
  return (
    <div className="code-view diff-view" ref={ref}>
      <div className="code-lines">
        {parts.flatMap((part, p) => {
          const ls = part.value.replace(/\n$/, '').split('\n');
          const cls = part.added ? 'added' : part.removed ? 'removed' : 'context';
          const mark = part.added ? '+' : part.removed ? '−' : ' ';
          return ls.map((l, i) => (
            <div key={`${p}-${i}`} className={`cv-line diff-${cls}`}>
              <span className="cv-mark">{mark}</span>
              <span className="cv-code">
                {highlightLine(l, lang).map(([c, t], k) => (c ? <span key={k} className={c}>{t}</span> : t))}
                {l === '' ? ' ' : null}
              </span>
            </div>
          ));
        })}
      </div>
    </div>
  );
}
