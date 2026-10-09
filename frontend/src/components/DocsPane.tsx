import { useState } from 'react';
import { DOC_SECTIONS, type DocBlock } from '../data/docsSections';
import { highlightLine } from '../lib/highlight';

function Block({ b }: { b: DocBlock }) {
  if (b.kind === 'p') return <p>{b.text}</p>;
  if (b.kind === 'code') {
    return (
      <pre className="docs-code">
        {b.text.split('\n').map((l, i) => (
          <div key={i}>{highlightLine(l, 'flux').map(([c, t], k) => (c ? <span key={k} className={c}>{t}</span> : t))}{l ? null : ' '}</div>
        ))}
      </pre>
    );
  }
  return (
    <div className="docs-table-wrap">
      <table className="docs-table">
        <thead><tr>{b.head.map(h => <th key={h}>{h}</th>)}</tr></thead>
        <tbody>{b.rows.map((r, i) => <tr key={i}>{r.map((c, k) => <td key={k}>{c}</td>)}</tr>)}</tbody>
      </table>
    </div>
  );
}

export function DocsPane() {
  const [active, setActive] = useState(DOC_SECTIONS[0].id);
  return (
    <div className="docs">
      <nav className="docs-nav">
        {DOC_SECTIONS.map(s => (
          <a key={s.id} href={`#doc-${s.id}`} className={active === s.id ? 'active' : ''}
            onClick={() => setActive(s.id)}>{s.title}</a>
        ))}
      </nav>
      <article className="docs-body">
        <h1>Flux language reference</h1>
        {DOC_SECTIONS.map(s => (
          <section key={s.id} id={`doc-${s.id}`}>
            <h2>{s.title}</h2>
            {s.blocks.map((b, i) => <Block key={i} b={b} />)}
          </section>
        ))}
      </article>
    </div>
  );
}
